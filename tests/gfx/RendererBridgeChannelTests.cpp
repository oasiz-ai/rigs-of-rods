/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeChannel.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer bridge channel test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return RoR::HostRenderPlatform::LINUX;
#else
  return RoR::HostRenderPlatform::UNKNOWN;
#endif
}

#if defined(_WIN32)

using NativeHandle = HANDLE;
const NativeHandle kInvalidNativeHandle = INVALID_HANDLE_VALUE;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

NativePipe MakePipe() {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  NativePipe result;
  Require(::CreatePipe(&result.read_handle, &result.write_handle, &security,
                       0U) != FALSE,
          "CreatePipe failed");
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(handle));
}

bool IsNativeHandleOpen(NativeHandle handle) {
  DWORD flags = 0U;
  return handle != nullptr && handle != kInvalidNativeHandle &&
         ::GetHandleInformation(handle, &flags) != FALSE;
}

bool IsNativeHandleInheritable(NativeHandle handle) {
  DWORD flags = 0U;
  Require(::GetHandleInformation(handle, &flags) != FALSE,
          "GetHandleInformation failed");
  return (flags & HANDLE_FLAG_INHERIT) != 0U;
}

void CloseNative(NativeHandle &handle) {
  if (handle == nullptr || handle == kInvalidNativeHandle) {
    return;
  }
  Require(::CloseHandle(handle) != FALSE, "CloseHandle failed");
  handle = kInvalidNativeHandle;
}

void WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    const DWORD requested = static_cast<DWORD>((std::min)(
        size - total, static_cast<std::size_t>(1024U * 1024U)));
    DWORD transferred = 0U;
    Require(::WriteFile(handle, bytes + total, requested, &transferred,
                        nullptr) != FALSE &&
                transferred != 0U,
            "native WriteFile failed");
    total += static_cast<std::size_t>(transferred);
  }
}

std::vector<std::uint8_t> ReadNativeExact(NativeHandle handle,
                                          std::size_t size) {
  std::vector<std::uint8_t> result(size);
  std::size_t total = 0U;
  while (total < size) {
    const DWORD requested = static_cast<DWORD>((std::min)(
        size - total, static_cast<std::size_t>(1024U * 1024U)));
    DWORD transferred = 0U;
    Require(::ReadFile(handle, result.data() + total, requested, &transferred,
                       nullptr) != FALSE &&
                transferred != 0U,
            "native ReadFile failed");
    total += static_cast<std::size_t>(transferred);
  }
  return result;
}

bool ReadNativeEof(NativeHandle handle) {
  std::uint8_t byte = 0U;
  DWORD transferred = 0U;
  if (::ReadFile(handle, &byte, 1U, &transferred, nullptr) != FALSE) {
    return transferred == 0U;
  }
  const DWORD error = ::GetLastError();
  return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
}

#else

using NativeHandle = int;
const NativeHandle kInvalidNativeHandle = -1;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

int PromoteReservedDescriptor(int descriptor) {
  if (descriptor >= 3) {
    return descriptor;
  }
  errno = 0;
  const int promoted = ::fcntl(descriptor, F_DUPFD, 3);
  Require(promoted >= 3, "could not promote a reserved pipe descriptor");
  Require(::close(descriptor) == 0,
          "could not close a reserved pipe descriptor");
  return promoted;
}

NativePipe MakePipe() {
  int descriptors[2] = {-1, -1};
  Require(::pipe(descriptors) == 0, "pipe failed");
  NativePipe result;
  result.read_handle = PromoteReservedDescriptor(descriptors[0]);
  result.write_handle = PromoteReservedDescriptor(descriptors[1]);
  Require(result.read_handle != result.write_handle,
          "pipe returned equal descriptors");
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(handle);
}

bool IsNativeHandleOpen(NativeHandle handle) {
  if (handle < 0) {
    return false;
  }
  errno = 0;
  return ::fcntl(handle, F_GETFD) >= 0;
}

bool IsNativeHandleInheritable(NativeHandle handle) {
  errno = 0;
  const int flags = ::fcntl(handle, F_GETFD);
  Require(flags >= 0, "fcntl(F_GETFD) failed");
  return (flags & FD_CLOEXEC) == 0;
}

void CloseNative(NativeHandle &handle) {
  if (handle < 0) {
    return;
  }
  errno = 0;
  Require(::close(handle) == 0, "close failed");
  handle = kInvalidNativeHandle;
}

void WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    errno = 0;
    const ssize_t transferred =
        ::write(handle, bytes + total, size - total);
    if (transferred < 0 && errno == EINTR) {
      continue;
    }
    Require(transferred > 0, "native write failed");
    total += static_cast<std::size_t>(transferred);
  }
}

std::vector<std::uint8_t> ReadNativeExact(NativeHandle handle,
                                          std::size_t size) {
  std::vector<std::uint8_t> result(size);
  std::size_t total = 0U;
  while (total < size) {
    errno = 0;
    const ssize_t transferred =
        ::read(handle, result.data() + total, size - total);
    if (transferred < 0 && errno == EINTR) {
      continue;
    }
    Require(transferred > 0, "native read failed");
    total += static_cast<std::size_t>(transferred);
  }
  return result;
}

bool ReadNativeEof(NativeHandle handle) {
  std::uint8_t byte = 0U;
  ssize_t transferred = -1;
  do {
    errno = 0;
    transferred = ::read(handle, &byte, 1U);
  } while (transferred < 0 && errno == EINTR);
  return transferred == 0;
}

bool IsSigpipeBlocked() {
  sigset_t current;
  Require(::pthread_sigmask(SIG_SETMASK, nullptr, &current) == 0,
          "could not inspect the thread signal mask");
  return sigismember(&current, SIGPIPE) == 1;
}

volatile sig_atomic_t g_sigpipe_handler_calls = 0;

void CountSigpipe(int signal_number) {
  if (signal_number == SIGPIPE) {
    ++g_sigpipe_handler_calls;
  }
}

#endif

RoR::RendererBridgeEndpoint MakeEndpoint(NativeHandle inbound,
                                         NativeHandle outbound) {
  RoR::RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = RoR::RendererBridgeRole::PRESENTATION_FRONTEND;
  for (std::size_t index = 0U; index < endpoint.session_id.size(); ++index) {
    endpoint.session_id[index] = static_cast<std::uint8_t>(index + 1U);
  }
  endpoint.inbound_native_handle = NativeToken(inbound);
  endpoint.outbound_native_handle = NativeToken(outbound);
  return endpoint;
}

void TestKnownStatusContract() {
  Require(RoR::kRendererBridgeChannelContractVersion == 1U,
          "channel contract version changed");
  for (int value = 0;
       value <= static_cast<int>(
                    RoR::RendererBridgeChannelStatus::FAILED_INTERNAL);
       ++value) {
    const auto status =
        static_cast<RoR::RendererBridgeChannelStatus>(value);
    Require(RoR::IsKnownRendererBridgeChannelStatus(status) &&
                std::string(RoR::ToString(status)) != "invalid",
            "known channel status was omitted");
  }
  Require(!RoR::IsKnownRendererBridgeChannelStatus(
              static_cast<RoR::RendererBridgeChannelStatus>(255U)) &&
              std::string(RoR::ToString(
                  static_cast<RoR::RendererBridgeChannelStatus>(255U))) ==
                  "invalid",
          "unknown channel status was accepted");

  RoR::RendererBridgeChannelResult result;
  Require(result.version == RoR::kRendererBridgeChannelContractVersion &&
              !result.ok() && !static_cast<bool>(result),
          "default result contract changed");
  result.status = RoR::RendererBridgeChannelStatus::READY;
  Require(result.ok() && static_cast<bool>(result),
          "ready result is not successful");
  result.status = RoR::RendererBridgeChannelStatus::CLOSED;
  Require(result.ok() && static_cast<bool>(result),
          "closed result is not successful");
  result.status = RoR::RendererBridgeChannelStatus::PEER_CLOSED;
  Require(!result.ok(), "peer closure was treated as a successful transfer");
}

void TestBidirectionalTransferAndHalfClose() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const NativeHandle adopted_inbound = inbound.read_handle;
  const NativeHandle adopted_outbound = outbound.write_handle;
  const RoR::RendererBridgeEndpoint endpoint =
      MakeEndpoint(adopted_inbound, adopted_outbound);
  Require(RoR::IsValidRendererBridgeEndpoint(endpoint),
          "valid native endpoint was rejected structurally");
  Require(IsNativeHandleInheritable(adopted_inbound) &&
              IsNativeHandleInheritable(adopted_outbound),
          "test endpoint did not begin inheritable");

  RoR::RendererBridgeChannel channel(endpoint);
  std::uint8_t scratch[32]{};
  Require(channel.ReadSome(scratch, sizeof(scratch)).status ==
              RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY,
          "read before adoption was accepted");
  Require(channel.TryReadSome(scratch, sizeof(scratch)).status ==
              RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY &&
              channel.TryReadSome(nullptr, sizeof(scratch)).status ==
                  RoR::RendererBridgeChannelStatus::
                      REJECTED_INVALID_ARGUMENT &&
              channel.ReadSome(nullptr, sizeof(scratch)).status ==
              RoR::RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT &&
              channel.WriteAll(nullptr, 1U).status ==
                  RoR::RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT,
          "invalid transfer arguments were accepted");

  const RoR::RendererBridgeChannelResult adopted = channel.Adopt();
  Require(adopted.status == RoR::RendererBridgeChannelStatus::READY &&
              adopted.ok() && adopted.bytes_transferred == 0U &&
              adopted.native_error_code == 0U && !adopted.peer_closed &&
              !adopted.terminal && channel.adopted() &&
              channel.inbound_open() && channel.outbound_open() &&
              !channel.terminal(),
          "valid channel adoption failed");
  Require(!IsNativeHandleInheritable(adopted_inbound) &&
              !IsNativeHandleInheritable(adopted_outbound),
          "adoption did not remove child-handle inheritance");
  Require(channel.Adopt().status ==
              RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY,
          "double adoption was accepted");
  Require(channel.WriteAll(nullptr, 0U).status ==
              RoR::RendererBridgeChannelStatus::READY,
          "empty write was rejected");
  const RoR::RendererBridgeChannelResult empty_try =
      channel.TryReadSome(scratch, sizeof(scratch));
  Require(empty_try.status == RoR::RendererBridgeChannelStatus::READY &&
              empty_try.bytes_transferred == 0U &&
              !empty_try.peer_closed && !empty_try.terminal,
          "empty zero-wait read did not preserve the open channel");
#if !defined(_WIN32)
  Require((::fcntl(static_cast<int>(adopted_inbound), F_GETFL) &
           O_NONBLOCK) == 0,
          "zero-wait read changed the descriptor blocking mode");
#endif

  const std::vector<std::uint8_t> inbound_message{
      0x00U, 0x7fU, 0x80U, 0xffU, 0x13U, 0x37U};
  WriteNative(inbound.write_handle, inbound_message.data(),
              inbound_message.size());
  const RoR::RendererBridgeChannelResult read =
      channel.TryReadSome(scratch, sizeof(scratch));
  Require(read.status == RoR::RendererBridgeChannelStatus::READY &&
              read.bytes_transferred == inbound_message.size() &&
              std::equal(inbound_message.begin(), inbound_message.end(),
                         scratch),
          "inbound bytes changed");

  const std::vector<std::uint8_t> outbound_message{
      0xdeU, 0xadU, 0xbeU, 0xefU, 0x00U, 0x42U};
  const RoR::RendererBridgeChannelResult write = channel.WriteAll(
      outbound_message.data(), outbound_message.size());
  Require(write.status == RoR::RendererBridgeChannelStatus::READY &&
              write.bytes_transferred == outbound_message.size() &&
              ReadNativeExact(outbound.read_handle,
                              outbound_message.size()) == outbound_message,
          "outbound bytes changed");

  std::vector<std::uint8_t> large_message(2U * 1024U * 1024U + 17U);
  for (std::size_t index = 0U; index < large_message.size(); ++index) {
    large_message[index] = static_cast<std::uint8_t>(
        (index * 131U + 17U) & 0xffU);
  }
  std::vector<std::uint8_t> large_received;
  std::thread reader([&]() {
    large_received =
        ReadNativeExact(outbound.read_handle, large_message.size());
  });
  const RoR::RendererBridgeChannelResult large_write =
      channel.WriteAll(large_message.data(), large_message.size());
  reader.join();
  Require(large_write.status == RoR::RendererBridgeChannelStatus::READY &&
              large_write.bytes_transferred == large_message.size() &&
              large_received == large_message,
          "multi-chunk outbound transfer changed bytes");

  CloseNative(inbound.write_handle);
  const RoR::RendererBridgeChannelResult eof =
      channel.TryReadSome(scratch, sizeof(scratch));
  Require(eof.status == RoR::RendererBridgeChannelStatus::PEER_CLOSED &&
              eof.peer_closed && !eof.terminal &&
              eof.bytes_transferred == 0U && !channel.inbound_open() &&
              channel.outbound_open() &&
              channel.status() == RoR::RendererBridgeChannelStatus::READY,
          "inbound EOF did not preserve the outbound half-channel");
  Require(channel.ReadSome(scratch, sizeof(scratch)).status ==
              RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY,
          "closed inbound half remained readable");

  const std::uint8_t after_eof = 0xa5U;
  Require(channel.WriteAll(&after_eof, 1U).status ==
              RoR::RendererBridgeChannelStatus::READY &&
              ReadNativeExact(outbound.read_handle, 1U)[0] == after_eof,
          "outbound half stopped after inbound EOF");

#if !defined(_WIN32)
  const bool sigpipe_was_blocked = IsSigpipeBlocked();
#endif
  CloseNative(outbound.read_handle);
  const RoR::RendererBridgeChannelResult broken =
      channel.WriteAll(&after_eof, 1U);
  Require(broken.status == RoR::RendererBridgeChannelStatus::PEER_CLOSED &&
              broken.peer_closed && broken.native_error_code != 0U &&
              !broken.terminal && !channel.inbound_open() &&
              !channel.outbound_open() &&
              channel.status() == RoR::RendererBridgeChannelStatus::CLOSED,
          "outbound peer closure was not contained");
#if !defined(_WIN32)
  Require(IsSigpipeBlocked() == sigpipe_was_blocked,
          "write changed the caller's SIGPIPE mask");
#endif
  const RoR::RendererBridgeChannelResult closed = channel.Close();
  Require(closed.status == RoR::RendererBridgeChannelStatus::CLOSED &&
              closed.ok() && !closed.terminal &&
              channel.Close().status ==
                  RoR::RendererBridgeChannelStatus::CLOSED,
          "channel close was not idempotent");

  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
}

void TestExplicitHalfClose() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  RoR::RendererBridgeChannel channel(
      MakeEndpoint(inbound.read_handle, outbound.write_handle));
  Require(channel.Adopt().ok(), "explicit half-close setup failed");
  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;

  std::uint8_t scratch[2U]{};
  const std::uint8_t first = 0x31U;
  const RoR::RendererBridgeChannelResult outbound_closed =
      channel.CloseOutbound();
  Require(outbound_closed.status == RoR::RendererBridgeChannelStatus::READY &&
              channel.inbound_open() && !channel.outbound_open(),
          "explicit outbound half-close closed the inbound half");
  Require(ReadNativeEof(outbound.read_handle),
          "peer did not observe explicit outbound EOF");
  Require(channel.WriteAll(&first, 1U).status ==
              RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY,
          "explicitly closed outbound half remained writable");

  const std::uint8_t second = 0x72U;
  WriteNative(inbound.write_handle, &second, 1U);
  const RoR::RendererBridgeChannelResult inbound_ready =
      channel.ReadSome(scratch, sizeof(scratch));
  Require(inbound_ready.status == RoR::RendererBridgeChannelStatus::READY &&
              inbound_ready.bytes_transferred == 1U &&
              scratch[0U] == second,
          "inbound half stopped after explicit outbound close");

  const RoR::RendererBridgeChannelResult inbound_closed =
      channel.CloseInbound();
  Require(inbound_closed.status == RoR::RendererBridgeChannelStatus::CLOSED &&
              !channel.inbound_open() && !channel.outbound_open() &&
              channel.ReadSome(scratch, sizeof(scratch)).status ==
                  RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY &&
              channel.CloseInbound().status ==
                  RoR::RendererBridgeChannelStatus::CLOSED &&
              channel.CloseOutbound().status ==
                  RoR::RendererBridgeChannelStatus::CLOSED,
          "explicit half-close was not stateful and idempotent");

  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);
}

void TestNonblockingOutboundBackpressure() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  RoR::RendererBridgeChannel channel(
      MakeEndpoint(inbound.read_handle, outbound.write_handle));
  Require(channel.Adopt().ok(), "nonblocking outbound setup failed");
  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;

  const std::uint8_t byte = 0xa5U;
  Require(channel.TryWriteSome(nullptr, 1U).status ==
                  RoR::RendererBridgeChannelStatus::REJECTED_INVALID_ARGUMENT &&
              channel.TryWriteSome(&byte, 1U).status ==
                  RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY &&
              !channel.outbound_nonblocking(),
          "zero-wait write accepted an invalid precondition");
  Require(channel.EnableNonblockingOutbound().ok() &&
              channel.outbound_nonblocking() &&
              channel.EnableNonblockingOutbound().ok() &&
              channel.WriteAll(&byte, 1U).status ==
                  RoR::RendererBridgeChannelStatus::REJECTED_NOT_READY,
          "outbound zero-wait mode was not explicit and idempotent");

  const std::vector<std::uint8_t> bytes(16U * 1024U * 1024U, 0x5aU);
  std::size_t offset = 0U;
  bool observed_backpressure = false;
  while (offset < bytes.size()) {
    const RoR::RendererBridgeChannelResult written =
        channel.TryWriteSome(bytes.data() + offset, bytes.size() - offset);
    Require(written.status == RoR::RendererBridgeChannelStatus::READY &&
                !written.terminal && !written.peer_closed,
            "zero-wait write failed while its peer remained open");
    if (written.bytes_transferred == 0U) {
      observed_backpressure = true;
      break;
    }
    offset += written.bytes_transferred;
  }
  Require(offset != 0U && observed_backpressure &&
              channel.outbound_open() && channel.inbound_open(),
          "zero-wait write blocked instead of reporting bounded backpressure");
  Require(channel.Close().status ==
                  RoR::RendererBridgeChannelStatus::CLOSED &&
              !channel.outbound_nonblocking(),
          "nonblocking channel did not close cleanly");

  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);
}

void TestCloseAndDestructorOwnership() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const NativeHandle adopted_inbound = inbound.read_handle;
  const NativeHandle adopted_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(adopted_inbound, adopted_outbound));
    Require(channel.Adopt().ok(), "close ownership setup failed");
    Require(channel.Close().status ==
                RoR::RendererBridgeChannelStatus::CLOSED &&
                !channel.inbound_open() && !channel.outbound_open(),
            "explicit close did not release both handles");
  }
  Require(!IsNativeHandleOpen(adopted_inbound) &&
              !IsNativeHandleOpen(adopted_outbound),
          "explicit close leaked an adopted handle");
  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);

  inbound = MakePipe();
  outbound = MakePipe();
  const NativeHandle destructed_inbound = inbound.read_handle;
  const NativeHandle destructed_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(destructed_inbound, destructed_outbound));
    Require(channel.Adopt().ok(), "destructor ownership setup failed");
  }
  Require(!IsNativeHandleOpen(destructed_inbound) &&
              !IsNativeHandleOpen(destructed_outbound),
          "destructor leaked an adopted handle");
  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);
}

void TestStructuralRejectionPreservesCallerOwnership() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const NativeHandle caller_inbound = inbound.read_handle;
  const NativeHandle caller_outbound = outbound.write_handle;
  RoR::RendererBridgeEndpoint endpoint =
      MakeEndpoint(caller_inbound, caller_outbound);
  endpoint.session_id.fill(0U);
  Require(!RoR::IsValidRendererBridgeEndpoint(endpoint),
          "all-zero session passed structural validation");
  {
    RoR::RendererBridgeChannel channel(endpoint);
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::REJECTED_INVALID_ENDPOINT &&
                rejected.terminal && channel.terminal() &&
                !channel.adopted(),
            "invalid endpoint adoption was not rejected terminally");
    Require(channel.Close().status ==
                RoR::RendererBridgeChannelStatus::REJECTED_INVALID_ENDPOINT,
            "rejected endpoint lost its terminal status");
  }
  Require(IsNativeHandleOpen(caller_inbound) &&
              IsNativeHandleOpen(caller_outbound),
          "structural rejection stole caller-owned handles");
  CloseNative(inbound.read_handle);
  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);
  CloseNative(outbound.write_handle);
}

void TestNativeRejectionReleasesTransferredOwnership() {
  NativePipe outbound = MakePipe();
#if defined(_WIN32)
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  NativeHandle non_pipe =
      ::CreateEventW(&security, TRUE, FALSE, nullptr);
  Require(non_pipe != nullptr, "CreateEvent failed");
#else
  NativeHandle non_pipe = ::open("/dev/null", O_RDONLY);
  Require(non_pipe >= 0, "open(/dev/null) failed");
  non_pipe = PromoteReservedDescriptor(non_pipe);
#endif
  const NativeHandle transferred_non_pipe = non_pipe;
  const NativeHandle transferred_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(transferred_non_pipe, transferred_outbound));
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE &&
                rejected.native_error_code != 0U && rejected.terminal &&
                channel.adopted() && !channel.inbound_open() &&
                !channel.outbound_open(),
            "non-pipe inbound handle was not rejected terminally");
    const RoR::RendererBridgeChannelResult repeated = channel.Close();
    Require(repeated.status == rejected.status &&
                repeated.native_error_code == rejected.native_error_code &&
                repeated.terminal,
            "terminal close lost native failure diagnostics");
  }
  Require(!IsNativeHandleOpen(transferred_non_pipe) &&
              !IsNativeHandleOpen(transferred_outbound),
          "native validation failure leaked transferred handles");
  non_pipe = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(outbound.read_handle);

  NativePipe inbound = MakePipe();
#if defined(_WIN32)
  non_pipe = ::CreateEventW(&security, TRUE, FALSE, nullptr);
  Require(non_pipe != nullptr, "second CreateEvent failed");
#else
  non_pipe = ::open("/dev/null", O_WRONLY);
  Require(non_pipe >= 0, "second open(/dev/null) failed");
  non_pipe = PromoteReservedDescriptor(non_pipe);
#endif
  const NativeHandle transferred_inbound = inbound.read_handle;
  const NativeHandle transferred_non_pipe_outbound = non_pipe;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(transferred_inbound,
                     transferred_non_pipe_outbound));
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::FAILED_OUTBOUND_HANDLE &&
                rejected.native_error_code != 0U && rejected.terminal &&
                channel.adopted() && !channel.inbound_open() &&
                !channel.outbound_open(),
            "non-pipe outbound handle was not rejected terminally");
  }
  Require(!IsNativeHandleOpen(transferred_inbound) &&
              !IsNativeHandleOpen(transferred_non_pipe_outbound),
          "outbound validation failure leaked transferred handles");
  inbound.read_handle = kInvalidNativeHandle;
  non_pipe = kInvalidNativeHandle;
  CloseNative(inbound.write_handle);
}

#if defined(_WIN32)

void TestWindowsIncompatiblePipeModeValidation() {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  const std::wstring pipe_name =
      std::wstring(L"\\\\.\\pipe\\ror-renderer-bridge-channel-") +
      std::to_wstring(static_cast<unsigned long long>(
          ::GetCurrentProcessId())) +
      L"-incompatible";
  NativeHandle message_pipe = ::CreateNamedPipeW(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT, 1U, 4096U,
      4096U, 0U, &security);
  Require(message_pipe != kInvalidNativeHandle,
          "CreateNamedPipeW failed");
  NativePipe outbound = MakePipe();
  const NativeHandle transferred_message_pipe = message_pipe;
  const NativeHandle transferred_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(transferred_message_pipe, transferred_outbound));
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE &&
                rejected.native_error_code ==
                    static_cast<std::uint32_t>(ERROR_INVALID_DATA) &&
                rejected.terminal,
            "message/nonblocking Windows pipe was accepted");
  }
  Require(!IsNativeHandleOpen(transferred_message_pipe) &&
              !IsNativeHandleOpen(transferred_outbound),
          "incompatible Windows mode rejection leaked handles");
  message_pipe = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(outbound.read_handle);
}

#endif

#if !defined(_WIN32)

void TestPosixDirectionAndBlockingValidation() {
  NativePipe wrong_inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const int transferred_wrong_direction = wrong_inbound.write_handle;
  const int transferred_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(transferred_wrong_direction, transferred_outbound));
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE &&
                rejected.native_error_code == static_cast<std::uint32_t>(EINVAL),
            "write-only POSIX inbound descriptor was accepted");
  }
  Require(!IsNativeHandleOpen(transferred_wrong_direction) &&
              !IsNativeHandleOpen(transferred_outbound),
          "direction rejection leaked transferred descriptors");
  wrong_inbound.write_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(wrong_inbound.read_handle);
  CloseNative(outbound.read_handle);

  NativePipe nonblocking_inbound = MakePipe();
  outbound = MakePipe();
  errno = 0;
  const int flags = ::fcntl(nonblocking_inbound.read_handle, F_GETFL);
  Require(flags >= 0 &&
              ::fcntl(nonblocking_inbound.read_handle, F_SETFL,
                      flags | O_NONBLOCK) == 0,
          "could not make test descriptor nonblocking");
  const int transferred_nonblocking = nonblocking_inbound.read_handle;
  const int second_transferred_outbound = outbound.write_handle;
  {
    RoR::RendererBridgeChannel channel(
        MakeEndpoint(transferred_nonblocking,
                     second_transferred_outbound));
    const RoR::RendererBridgeChannelResult rejected = channel.Adopt();
    Require(rejected.status ==
                RoR::RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE &&
                rejected.native_error_code == static_cast<std::uint32_t>(EINVAL),
            "nonblocking POSIX descriptor was accepted");
  }
  Require(!IsNativeHandleOpen(transferred_nonblocking) &&
              !IsNativeHandleOpen(second_transferred_outbound),
          "nonblocking rejection leaked transferred descriptors");
  nonblocking_inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(nonblocking_inbound.write_handle);
  CloseNative(outbound.read_handle);
}

enum class SigpipeDisposition : std::uint8_t {
  DEFAULT_DISPOSITION = 0U,
  IGNORE,
  CUSTOM_HANDLER,
  ALREADY_PENDING,
};

int ExerciseClosedPipeInChild(SigpipeDisposition disposition) {
  g_sigpipe_handler_calls = 0;
  if (disposition == SigpipeDisposition::DEFAULT_DISPOSITION) {
    if (::signal(SIGPIPE, SIG_DFL) == SIG_ERR) {
      return 10;
    }
  } else if (disposition == SigpipeDisposition::IGNORE) {
    if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
      return 11;
    }
  } else if (disposition == SigpipeDisposition::CUSTOM_HANDLER) {
    if (::signal(SIGPIPE, CountSigpipe) == SIG_ERR) {
      return 12;
    }
  }

  sigset_t pending_set;
  sigset_t original_set;
  bool pending_mode = false;
  if (disposition == SigpipeDisposition::ALREADY_PENDING) {
    if (sigemptyset(&pending_set) != 0 ||
        sigaddset(&pending_set, SIGPIPE) != 0 ||
        ::pthread_sigmask(SIG_BLOCK, &pending_set, &original_set) != 0 ||
        ::raise(SIGPIPE) != 0) {
      return 13;
    }
    pending_mode = true;
  }

  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  RoR::RendererBridgeChannel channel(
      MakeEndpoint(inbound.read_handle, outbound.write_handle));
  if (!channel.Adopt().ok()) {
    return 14;
  }
  CloseNative(outbound.read_handle);
  const std::uint8_t byte = 0xa5U;
  const RoR::RendererBridgeChannelResult result =
      channel.WriteAll(&byte, 1U);
  if (result.status != RoR::RendererBridgeChannelStatus::PEER_CLOSED ||
      !result.peer_closed || result.terminal ||
      g_sigpipe_handler_calls != 0) {
    return 15;
  }

  if (pending_mode) {
    sigset_t observed;
    if (::sigpending(&observed) != 0 ||
        sigismember(&observed, SIGPIPE) != 1) {
      return 16;
    }
    int received = 0;
    if (::sigwait(&pending_set, &received) != 0 || received != SIGPIPE ||
        ::pthread_sigmask(SIG_SETMASK, &original_set, nullptr) != 0) {
      return 17;
    }
  }
  return 0;
}

void TestSigpipeDispositionsInIsolatedProcesses() {
  for (int value = static_cast<int>(
           SigpipeDisposition::DEFAULT_DISPOSITION);
       value <= static_cast<int>(SigpipeDisposition::ALREADY_PENDING);
       ++value) {
    const pid_t child = ::fork();
    Require(child >= 0, "fork failed for SIGPIPE isolation");
    if (child == 0) {
      const int result = ExerciseClosedPipeInChild(
          static_cast<SigpipeDisposition>(value));
      ::_exit(result);
    }
    int status = 0;
    pid_t waited = -1;
    do {
      errno = 0;
      waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    Require(waited == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0,
            "SIGPIPE disposition contract failed");
  }
}

#endif

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestKnownStatusContract();
  TestBidirectionalTransferAndHalfClose();
  TestExplicitHalfClose();
  TestNonblockingOutboundBackpressure();
  TestCloseAndDestructorOwnership();
  TestStructuralRejectionPreservesCallerOwnership();
  TestNativeRejectionReleasesTransferredOwnership();
#if defined(_WIN32)
  TestWindowsIncompatiblePipeModeValidation();
#endif
#if !defined(_WIN32)
  TestPosixDirectionAndBlockingValidation();
  TestSigpipeDispositionsInIsolatedProcesses();
#endif
  return EXIT_SUCCESS;
}
