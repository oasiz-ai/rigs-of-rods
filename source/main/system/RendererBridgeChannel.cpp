/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeChannel.h"

#include <algorithm>
#include <cerrno>
#include <climits>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace RoR {
namespace {

constexpr std::size_t kMaximumIoChunkBytes = 1024U * 1024U;

#if defined(_WIN32)

HANDLE NativeHandle(std::uint64_t value) noexcept {
  return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}

bool ValidatePipeHandle(HANDLE handle, std::uint32_t &error_code) noexcept {
  DWORD flags = 0U;
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE ||
      ::GetHandleInformation(handle, &flags) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  ::SetLastError(ERROR_SUCCESS);
  const DWORD type = ::GetFileType(handle);
  if (type != FILE_TYPE_PIPE) {
    const DWORD native_error = ::GetLastError();
    error_code = static_cast<std::uint32_t>(
        native_error == ERROR_SUCCESS ? ERROR_INVALID_HANDLE : native_error);
    return false;
  }
  DWORD pipe_flags = 0U;
  if (::GetNamedPipeInfo(handle, &pipe_flags, nullptr, nullptr, nullptr) ==
      FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  if ((pipe_flags & PIPE_TYPE_MESSAGE) != 0U) {
    error_code = static_cast<std::uint32_t>(ERROR_INVALID_DATA);
    return false;
  }
  DWORD pipe_state = 0U;
  if (::GetNamedPipeHandleStateW(handle, &pipe_state, nullptr, nullptr,
                                 nullptr, nullptr, 0U) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  if ((pipe_state & (PIPE_NOWAIT | PIPE_READMODE_MESSAGE)) != 0U) {
    error_code = static_cast<std::uint32_t>(ERROR_INVALID_DATA);
    return false;
  }
  return true;
}

bool HardenHandle(HANDLE handle, std::uint32_t &error_code) noexcept {
  if (::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0U) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  return true;
}

#else

bool ValidatePipeDescriptor(int descriptor, int expected_access,
                            std::uint32_t &error_code) noexcept {
  errno = 0;
  const int descriptor_flags = ::fcntl(descriptor, F_GETFD);
  if (descriptor_flags < 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  errno = 0;
  const int status_flags = ::fcntl(descriptor, F_GETFL);
  if (status_flags < 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  if ((status_flags & O_ACCMODE) != expected_access ||
      (status_flags & O_NONBLOCK) != 0) {
    error_code = static_cast<std::uint32_t>(EINVAL);
    return false;
  }
  struct stat metadata {};
  errno = 0;
  if (::fstat(descriptor, &metadata) != 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  if (!S_ISFIFO(metadata.st_mode)) {
    error_code = static_cast<std::uint32_t>(EINVAL);
    return false;
  }
  return true;
}

bool HardenDescriptor(int descriptor,
                      std::uint32_t &error_code) noexcept {
  errno = 0;
  const int flags = ::fcntl(descriptor, F_GETFD);
  if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  return true;
}

class ScopedSigpipeBlock final {
public:
  bool Initialize(std::uint32_t &error_code) noexcept {
    if (sigemptyset(&set_) != 0 || sigaddset(&set_, SIGPIPE) != 0) {
      error_code = static_cast<std::uint32_t>(errno);
      return false;
    }
    const int mask_error = ::pthread_sigmask(SIG_BLOCK, &set_, &old_set_);
    if (mask_error != 0) {
      error_code = static_cast<std::uint32_t>(mask_error);
      return false;
    }
    active_ = true;
    sigset_t pending;
    if (::sigpending(&pending) != 0) {
      error_code = static_cast<std::uint32_t>(errno);
      (void)Restore();
      return false;
    }
    pending_before_ = sigismember(&pending, SIGPIPE) == 1;
    return true;
  }

  std::uint32_t ConsumeGeneratedSigpipe() noexcept {
    if (!active_ || pending_before_) {
      return 0U;
    }
    sigset_t pending;
    errno = 0;
    if (::sigpending(&pending) != 0) {
      return static_cast<std::uint32_t>(errno != 0 ? errno : EIO);
    }
    if (sigismember(&pending, SIGPIPE) != 1) {
      // SIG_IGN legitimately discards the generated signal.
      return 0U;
    }
    int received = 0;
    const int wait_error = ::sigwait(&set_, &received);
    if (wait_error != 0) {
      return static_cast<std::uint32_t>(wait_error);
    }
    return received == SIGPIPE ? 0U : static_cast<std::uint32_t>(EIO);
  }

  int Restore() noexcept {
    if (!active_) {
      return 0;
    }
    active_ = false;
    return ::pthread_sigmask(SIG_SETMASK, &old_set_, nullptr);
  }

  ~ScopedSigpipeBlock() { (void)Restore(); }

private:
  sigset_t set_{};
  sigset_t old_set_{};
  bool active_ = false;
  bool pending_before_ = false;
};

#endif

} // namespace

RendererBridgeChannel::RendererBridgeChannel(
    const RendererBridgeEndpoint &endpoint) noexcept
    : endpoint_(endpoint) {}

RendererBridgeChannel::~RendererBridgeChannel() { CloseNoexcept(); }

RendererBridgeChannelResult RendererBridgeChannel::MakeResult(
    RendererBridgeChannelStatus status, std::size_t bytes_transferred,
    std::uint32_t native_error_code, bool peer_closed) const noexcept {
  RendererBridgeChannelResult result;
  result.status = status;
  result.bytes_transferred = bytes_transferred;
  result.native_error_code = native_error_code;
  result.peer_closed = peer_closed;
  result.terminal = terminal_;
  return result;
}

RendererBridgeChannelResult RendererBridgeChannel::FailAndClose(
    RendererBridgeChannelStatus status,
    std::uint32_t native_error_code,
    std::size_t bytes_transferred) noexcept {
  status_ = status;
  terminal_ = true;
  terminal_bytes_transferred_ = bytes_transferred;
  terminal_native_error_code_ = native_error_code;
  CloseNoexcept();
  status_ = status;
  terminal_ = true;
  return MakeResult(status, bytes_transferred, native_error_code);
}

void RendererBridgeChannel::RefreshClosedStatus() noexcept {
  if (adopted_ && !inbound_open_ && !outbound_open_ && !terminal_) {
    status_ = RendererBridgeChannelStatus::CLOSED;
  }
}

std::uint32_t RendererBridgeChannel::CloseInboundNative() noexcept {
  if (!inbound_open_) {
    return 0U;
  }
  inbound_open_ = false;
#if defined(_WIN32)
  const HANDLE handle = NativeHandle(inbound_native_handle_);
  inbound_native_handle_ = 0U;
  if (::CloseHandle(handle) == FALSE) {
    return static_cast<std::uint32_t>(::GetLastError());
  }
#else
  const int descriptor = static_cast<int>(inbound_native_handle_);
  inbound_native_handle_ = 0U;
  errno = 0;
  if (::close(descriptor) != 0) {
    return static_cast<std::uint32_t>(errno);
  }
#endif
  return 0U;
}

std::uint32_t RendererBridgeChannel::CloseOutboundNative() noexcept {
  if (!outbound_open_) {
    return 0U;
  }
  outbound_open_ = false;
#if defined(_WIN32)
  const HANDLE handle = NativeHandle(outbound_native_handle_);
  outbound_native_handle_ = 0U;
  if (::CloseHandle(handle) == FALSE) {
    return static_cast<std::uint32_t>(::GetLastError());
  }
#else
  const int descriptor = static_cast<int>(outbound_native_handle_);
  outbound_native_handle_ = 0U;
  errno = 0;
  if (::close(descriptor) != 0) {
    return static_cast<std::uint32_t>(errno);
  }
#endif
  return 0U;
}

void RendererBridgeChannel::CloseNoexcept() noexcept {
  (void)CloseInboundNative();
  (void)CloseOutboundNative();
  RefreshClosedStatus();
}

RendererBridgeChannelResult RendererBridgeChannel::Adopt() noexcept {
  if (status_ != RendererBridgeChannelStatus::UNINITIALIZED || adopted_) {
    return MakeResult(RendererBridgeChannelStatus::REJECTED_NOT_READY);
  }
  if (!IsValidRendererBridgeEndpoint(endpoint_)) {
    status_ = RendererBridgeChannelStatus::REJECTED_INVALID_ENDPOINT;
    terminal_ = true;
    return MakeResult(status_);
  }

  inbound_native_handle_ = endpoint_.inbound_native_handle;
  outbound_native_handle_ = endpoint_.outbound_native_handle;
  inbound_open_ = true;
  outbound_open_ = true;
  adopted_ = true;
  std::uint32_t native_error = 0U;
#if defined(_WIN32)
  if (!ValidatePipeHandle(NativeHandle(inbound_native_handle_), native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE, native_error);
  }
  if (!ValidatePipeHandle(NativeHandle(outbound_native_handle_), native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_OUTBOUND_HANDLE, native_error);
  }
  if (!HardenHandle(NativeHandle(inbound_native_handle_), native_error) ||
      !HardenHandle(NativeHandle(outbound_native_handle_), native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_HANDLE_HARDENING, native_error);
  }
#else
  if (!ValidatePipeDescriptor(static_cast<int>(inbound_native_handle_), O_RDONLY,
                              native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE, native_error);
  }
  if (!ValidatePipeDescriptor(static_cast<int>(outbound_native_handle_),
                              O_WRONLY, native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_OUTBOUND_HANDLE, native_error);
  }
  if (!HardenDescriptor(static_cast<int>(inbound_native_handle_),
                        native_error) ||
      !HardenDescriptor(static_cast<int>(outbound_native_handle_),
                        native_error)) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_HANDLE_HARDENING, native_error);
  }
#endif
  status_ = RendererBridgeChannelStatus::READY;
  return MakeResult(status_);
}

RendererBridgeChannelResult RendererBridgeChannel::ReadSome(
    std::uint8_t *bytes, std::size_t capacity) noexcept {
  if (bytes == nullptr || capacity == 0U) {
    return MakeResult(RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT);
  }
  if (terminal_ || !adopted_ || status_ == RendererBridgeChannelStatus::CLOSED ||
      !inbound_open_) {
    return MakeResult(RendererBridgeChannelStatus::REJECTED_NOT_READY);
  }
#if defined(_WIN32)
  const DWORD requested = static_cast<DWORD>((std::min)(
      capacity, kMaximumIoChunkBytes));
  DWORD transferred = 0U;
  if (::ReadFile(NativeHandle(inbound_native_handle_), bytes, requested,
                 &transferred, nullptr) != FALSE) {
    if (transferred == 0U) {
      (void)CloseInboundNative();
      RefreshClosedStatus();
      return MakeResult(RendererBridgeChannelStatus::PEER_CLOSED, 0U, 0U,
                        true);
    }
    return MakeResult(RendererBridgeChannelStatus::READY,
                      static_cast<std::size_t>(transferred));
  }
  const DWORD error = ::GetLastError();
  if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
      error == ERROR_NO_DATA) {
    (void)CloseInboundNative();
    RefreshClosedStatus();
    return MakeResult(RendererBridgeChannelStatus::PEER_CLOSED, 0U,
                      static_cast<std::uint32_t>(error), true);
  }
  return FailAndClose(RendererBridgeChannelStatus::FAILED_READ,
                      static_cast<std::uint32_t>(error));
#else
  const std::size_t requested = (std::min)(
      capacity, kMaximumIoChunkBytes);
  for (;;) {
    errno = 0;
    const ssize_t transferred =
        ::read(static_cast<int>(inbound_native_handle_), bytes, requested);
    if (transferred > 0) {
      return MakeResult(RendererBridgeChannelStatus::READY,
                        static_cast<std::size_t>(transferred));
    }
    if (transferred == 0) {
      (void)CloseInboundNative();
      RefreshClosedStatus();
      return MakeResult(RendererBridgeChannelStatus::PEER_CLOSED, 0U, 0U,
                        true);
    }
    if (errno == EINTR) {
      continue;
    }
    return FailAndClose(RendererBridgeChannelStatus::FAILED_READ,
                        static_cast<std::uint32_t>(errno));
  }
#endif
}

RendererBridgeChannelResult RendererBridgeChannel::WriteAll(
    const std::uint8_t *bytes, std::size_t size) noexcept {
  if (bytes == nullptr && size != 0U) {
    return MakeResult(RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT);
  }
  if (terminal_ || !adopted_ || status_ == RendererBridgeChannelStatus::CLOSED ||
      !outbound_open_) {
    return MakeResult(RendererBridgeChannelStatus::REJECTED_NOT_READY);
  }
  if (size == 0U) {
    return MakeResult(RendererBridgeChannelStatus::READY);
  }

  std::size_t total = 0U;
#if defined(_WIN32)
  while (total < size) {
    const DWORD requested = static_cast<DWORD>((std::min)(
        size - total, kMaximumIoChunkBytes));
    DWORD transferred = 0U;
    if (::WriteFile(NativeHandle(outbound_native_handle_), bytes + total,
                    requested, &transferred, nullptr) == FALSE) {
      const DWORD error = ::GetLastError();
      if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
          error == ERROR_NO_DATA) {
        (void)CloseOutboundNative();
        RefreshClosedStatus();
        return MakeResult(RendererBridgeChannelStatus::PEER_CLOSED, total,
                          static_cast<std::uint32_t>(error), true);
      }
      return FailAndClose(RendererBridgeChannelStatus::FAILED_WRITE,
                          static_cast<std::uint32_t>(error), total);
    }
    if (transferred == 0U) {
      return FailAndClose(RendererBridgeChannelStatus::FAILED_WRITE,
                          static_cast<std::uint32_t>(ERROR_WRITE_FAULT),
                          total);
    }
    total += static_cast<std::size_t>(transferred);
  }
#else
  ScopedSigpipeBlock sigpipe;
  std::uint32_t sigpipe_error = 0U;
  if (!sigpipe.Initialize(sigpipe_error)) {
    return FailAndClose(RendererBridgeChannelStatus::FAILED_WRITE,
                        sigpipe_error);
  }
  int write_error = 0;
  std::uint32_t sigpipe_drain_error = 0U;
  while (total < size) {
    const std::size_t requested = (std::min)(
        size - total, kMaximumIoChunkBytes);
    errno = 0;
    const ssize_t transferred = ::write(
        static_cast<int>(outbound_native_handle_), bytes + total, requested);
    if (transferred > 0) {
      total += static_cast<std::size_t>(transferred);
      continue;
    }
    if (transferred < 0 && errno == EINTR) {
      continue;
    }
    write_error = transferred == 0 ? EIO : errno;
    if (write_error == EPIPE) {
      sigpipe_drain_error = sigpipe.ConsumeGeneratedSigpipe();
    }
    break;
  }
  const int restore_error = sigpipe.Restore();
  if (sigpipe_drain_error != 0U) {
    return FailAndClose(RendererBridgeChannelStatus::FAILED_WRITE,
                        sigpipe_drain_error, total);
  }
  if (restore_error != 0) {
    return FailAndClose(RendererBridgeChannelStatus::FAILED_WRITE,
                        static_cast<std::uint32_t>(restore_error), total);
  }
  if (write_error == EPIPE) {
    (void)CloseOutboundNative();
    RefreshClosedStatus();
    return MakeResult(RendererBridgeChannelStatus::PEER_CLOSED, total,
                      static_cast<std::uint32_t>(write_error), true);
  }
  if (write_error != 0) {
    return FailAndClose(
        RendererBridgeChannelStatus::FAILED_WRITE,
        static_cast<std::uint32_t>(write_error), total);
  }
#endif
  return MakeResult(RendererBridgeChannelStatus::READY, total);
}

RendererBridgeChannelResult RendererBridgeChannel::Close() noexcept {
  if (terminal_) {
    CloseNoexcept();
    return MakeResult(status_, terminal_bytes_transferred_,
                      terminal_native_error_code_);
  }
  if (status_ == RendererBridgeChannelStatus::CLOSED && !inbound_open_ &&
      !outbound_open_) {
    return MakeResult(RendererBridgeChannelStatus::CLOSED);
  }
  const std::uint32_t inbound_error = CloseInboundNative();
  const std::uint32_t outbound_error = CloseOutboundNative();
  if (inbound_error != 0U || outbound_error != 0U) {
    status_ = RendererBridgeChannelStatus::FAILED_CLOSE;
    terminal_ = true;
    terminal_bytes_transferred_ = 0U;
    terminal_native_error_code_ =
        inbound_error != 0U ? inbound_error : outbound_error;
    return MakeResult(status_, 0U, terminal_native_error_code_);
  }
  status_ = RendererBridgeChannelStatus::CLOSED;
  return MakeResult(status_);
}

bool IsKnownRendererBridgeChannelStatus(
    RendererBridgeChannelStatus status) noexcept {
  switch (status) {
  case RendererBridgeChannelStatus::UNINITIALIZED:
  case RendererBridgeChannelStatus::READY:
  case RendererBridgeChannelStatus::CLOSED:
  case RendererBridgeChannelStatus::PEER_CLOSED:
  case RendererBridgeChannelStatus::REJECTED_INVALID_ENDPOINT:
  case RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT:
  case RendererBridgeChannelStatus::REJECTED_NOT_READY:
  case RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE:
  case RendererBridgeChannelStatus::FAILED_OUTBOUND_HANDLE:
  case RendererBridgeChannelStatus::FAILED_HANDLE_HARDENING:
  case RendererBridgeChannelStatus::FAILED_READ:
  case RendererBridgeChannelStatus::FAILED_WRITE:
  case RendererBridgeChannelStatus::FAILED_CLOSE:
  case RendererBridgeChannelStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererBridgeChannelStatus status) noexcept {
  switch (status) {
  case RendererBridgeChannelStatus::UNINITIALIZED:
    return "uninitialized";
  case RendererBridgeChannelStatus::READY:
    return "ready";
  case RendererBridgeChannelStatus::CLOSED:
    return "closed";
  case RendererBridgeChannelStatus::PEER_CLOSED:
    return "peer-closed";
  case RendererBridgeChannelStatus::REJECTED_INVALID_ENDPOINT:
    return "rejected-invalid-endpoint";
  case RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT:
    return "rejected-invalid-argument";
  case RendererBridgeChannelStatus::REJECTED_NOT_READY:
    return "rejected-not-ready";
  case RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE:
    return "failed-inbound-handle";
  case RendererBridgeChannelStatus::FAILED_OUTBOUND_HANDLE:
    return "failed-outbound-handle";
  case RendererBridgeChannelStatus::FAILED_HANDLE_HARDENING:
    return "failed-handle-hardening";
  case RendererBridgeChannelStatus::FAILED_READ:
    return "failed-read";
  case RendererBridgeChannelStatus::FAILED_WRITE:
    return "failed-write";
  case RendererBridgeChannelStatus::FAILED_CLOSE:
    return "failed-close";
  case RendererBridgeChannelStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
