/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeProcessSupervisor.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

namespace RoR {
namespace {

constexpr std::uint32_t kInvalidPropagationExitCode = 125U;

HostRenderPlatform CompileTimeHostPlatform() noexcept {
#if defined(_WIN32)
  return HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return HostRenderPlatform::LINUX;
#else
  return HostRenderPlatform::UNKNOWN;
#endif
}

bool HasValidArguments(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  if (argc < 1 || argv == nullptr || argv[0] == nullptr || argv[0][0] == 0) {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

RendererBridgeProcessResult Failure(RendererBridgeProcessStatus status,
                                    std::uint32_t native_error = 0U) {
  RendererBridgeProcessResult result;
  result.status = status;
  result.native_error_code = native_error;
  return result;
}

RendererBridgeProcessStatus MapPreflightStatus(
    RendererBridgeLaunchPlanStatus status) noexcept {
  switch (status) {
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF:
    return RendererBridgeProcessStatus::REJECTED_INVALID_HANDOFF;
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_ARGUMENTS:
    return RendererBridgeProcessStatus::REJECTED_INVALID_ARGUMENTS;
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_SESSION:
    return RendererBridgeProcessStatus::REJECTED_INVALID_SESSION;
  case RendererBridgeLaunchPlanStatus::READY:
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_STREAM_HANDLES:
  case RendererBridgeLaunchPlanStatus::REJECTED_ENDPOINT_ENCODING:
  case RendererBridgeLaunchPlanStatus::REJECTED_RENDERER_INTENT_ENCODING:
  case RendererBridgeLaunchPlanStatus::FAILED_INTERNAL:
    return RendererBridgeProcessStatus::REJECTED_LAUNCH_PLAN;
  }
  return RendererBridgeProcessStatus::REJECTED_LAUNCH_PLAN;
}

RendererBridgeStreamHandles PreviewHandles() noexcept {
  RendererBridgeStreamHandles streams;
  streams.game_to_frontend_read = 3U;
  streams.game_to_frontend_write = 4U;
  streams.frontend_to_game_read = 5U;
  streams.frontend_to_game_write = 6U;
  return streams;
}

bool HasExpectedBasenames(const RendererBridgeLaunchPlan &plan) noexcept {
#if defined(_WIN32)
  return plan.game_child_basename == L"RoR-Ogre14.exe" &&
         plan.presentation_child_basename == L"RoR-OgreNext.exe";
#else
  return plan.game_child_basename == "RoR-Ogre14" &&
         plan.presentation_child_basename == "RoR-OgreNext";
#endif
}

const char *GameBasename() noexcept {
#if defined(_WIN32)
  return "RoR-Ogre14.exe";
#else
  return "RoR-Ogre14";
#endif
}

const char *PresentationBasename() noexcept {
#if defined(_WIN32)
  return "RoR-OgreNext.exe";
#else
  return "RoR-OgreNext";
#endif
}

#if defined(_WIN32)

class WindowsHandle final {
public:
  WindowsHandle() = default;
  explicit WindowsHandle(HANDLE handle) noexcept : handle_(handle) {}
  ~WindowsHandle() { reset(); }

  WindowsHandle(const WindowsHandle &) = delete;
  WindowsHandle &operator=(const WindowsHandle &) = delete;
  WindowsHandle(WindowsHandle &&other) noexcept : handle_(other.release()) {}
  WindowsHandle &operator=(WindowsHandle &&other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  HANDLE get() const noexcept { return handle_; }
  bool valid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }
  HANDLE release() noexcept {
    const HANDLE value = handle_;
    handle_ = nullptr;
    return value;
  }
  void reset(HANDLE handle = nullptr) noexcept {
    if (valid()) {
      (void)::CloseHandle(handle_);
    }
    handle_ = handle;
  }

private:
  HANDLE handle_ = nullptr;
};

struct WindowsPipe final {
  WindowsHandle read;
  WindowsHandle write;
};

struct WindowsProcess final {
  WindowsHandle process;
  WindowsHandle thread;
};

struct WindowsAttributeList final {
  std::vector<std::uintptr_t> storage;
  LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

  ~WindowsAttributeList() {
    if (list != nullptr) {
      ::DeleteProcThreadAttributeList(list);
    }
  }
};

bool MakeInheritablePipe(WindowsPipe &pipe, std::uint32_t &error_code) {
  SECURITY_ATTRIBUTES security = {};
  security.nLength = static_cast<DWORD>(sizeof(security));
  security.bInheritHandle = TRUE;
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  if (::CreatePipe(&read, &write, &security, 0U) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  pipe.read.reset(read);
  pipe.write.reset(write);
  return true;
}

bool HandleToken(HANDLE handle, std::uint64_t &token) noexcept {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(handle);
  if (value < 3U ||
      value >= (std::numeric_limits<std::uintptr_t>::max)()) {
    return false;
  }
  token = static_cast<std::uint64_t>(value);
  return true;
}

void AppendQuotedWindowsArgument(const wchar_t *argument,
                                 std::wstring &command_line) {
  command_line.push_back(L'"');
  std::size_t backslashes = 0U;
  for (const wchar_t *cursor = argument; *cursor != L'\0'; ++cursor) {
    if (*cursor == L'\\') {
      ++backslashes;
      continue;
    }
    if (*cursor == L'"') {
      command_line.append(backslashes * 2U + 1U, L'\\');
      command_line.push_back(L'"');
      backslashes = 0U;
      continue;
    }
    command_line.append(backslashes, L'\\');
    backslashes = 0U;
    command_line.push_back(*cursor);
  }
  command_line.append(backslashes * 2U, L'\\');
  command_line.push_back(L'"');
}

bool BuildWindowsCommandLine(
    const std::wstring &child_path,
    const std::vector<RendererChildLauncherString> &arguments,
    std::vector<wchar_t> &command_line) {
  if (child_path.empty() || arguments.empty()) {
    return false;
  }
  std::wstring value;
  AppendQuotedWindowsArgument(child_path.c_str(), value);
  for (std::size_t index = 1U; index < arguments.size(); ++index) {
    value.push_back(L' ');
    AppendQuotedWindowsArgument(arguments[index].c_str(), value);
  }
  if (value.size() >= 32767U) {
    return false;
  }
  command_line.assign(value.begin(), value.end());
  command_line.push_back(L'\0');
  return true;
}

bool BuildExactHandleAttributeList(
    std::array<HANDLE, 2U> &allowed, WindowsAttributeList &attributes,
    std::uint32_t &error_code) {
  if (allowed[0U] == allowed[1U] || allowed[0U] == nullptr ||
      allowed[1U] == nullptr || allowed[0U] == INVALID_HANDLE_VALUE ||
      allowed[1U] == INVALID_HANDLE_VALUE) {
    error_code = static_cast<std::uint32_t>(ERROR_INVALID_HANDLE);
    return false;
  }
  SIZE_T bytes = 0U;
  (void)::InitializeProcThreadAttributeList(nullptr, 1U, 0U, &bytes);
  if (bytes == 0U) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  const SIZE_T words =
      (bytes + sizeof(std::uintptr_t) - 1U) / sizeof(std::uintptr_t);
  attributes.storage.resize(static_cast<std::size_t>(words));
  attributes.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
      attributes.storage.data());
  if (::InitializeProcThreadAttributeList(attributes.list, 1U, 0U,
                                          &bytes) == FALSE) {
    attributes.list = nullptr;
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  if (::UpdateProcThreadAttribute(
          attributes.list, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          allowed.data(), static_cast<SIZE_T>(sizeof(allowed)), nullptr,
          nullptr) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  return true;
}

bool CreateSuspendedChild(
    const std::wstring &path,
    const std::vector<RendererChildLauncherString> &arguments,
    std::array<HANDLE, 2U> allowed, WindowsProcess &process,
    RendererBridgeProcessStatus &failure_status,
    std::uint32_t &error_code,
    RendererBridgeProcessStatus create_failure_status) {
  std::vector<wchar_t> command_line;
  if (!BuildWindowsCommandLine(path, arguments, command_line)) {
    failure_status = RendererBridgeProcessStatus::FAILED_WINDOWS_COMMAND_LINE;
    error_code = static_cast<std::uint32_t>(ERROR_BAD_ARGUMENTS);
    return false;
  }
  WindowsAttributeList attributes;
  if (!BuildExactHandleAttributeList(allowed, attributes, error_code)) {
    failure_status =
        RendererBridgeProcessStatus::FAILED_WINDOWS_ATTRIBUTE_LIST;
    return false;
  }

  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = static_cast<DWORD>(sizeof(startup));
  startup.lpAttributeList = attributes.list;
  PROCESS_INFORMATION created = {};
  const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
  if (::CreateProcessW(path.c_str(), command_line.data(), nullptr, nullptr,
                       TRUE, flags, nullptr, nullptr, &startup.StartupInfo,
                       &created) == FALSE) {
    failure_status = create_failure_status;
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  process.process.reset(created.hProcess);
  process.thread.reset(created.hThread);
  return true;
}

bool TerminateAndWaitWindows(WindowsProcess &process,
                             std::uint32_t termination_code,
                             bool &terminated,
                             std::uint32_t &error_code) noexcept {
  if (!process.process.valid()) {
    return true;
  }
  const DWORD immediate = ::WaitForSingleObject(process.process.get(), 0U);
  if (immediate == WAIT_FAILED) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  if (immediate != WAIT_OBJECT_0) {
    if (::TerminateProcess(process.process.get(), termination_code) == FALSE) {
      const DWORD terminate_error = ::GetLastError();
      if (::WaitForSingleObject(process.process.get(), 0U) != WAIT_OBJECT_0) {
        error_code = static_cast<std::uint32_t>(terminate_error);
        return false;
      }
    } else {
      terminated = true;
    }
  }
  const DWORD waited = ::WaitForSingleObject(process.process.get(), 5000U);
  if (waited != WAIT_OBJECT_0) {
    error_code = waited == WAIT_FAILED
                     ? static_cast<std::uint32_t>(::GetLastError())
                     : static_cast<std::uint32_t>(WAIT_TIMEOUT);
    return false;
  }
  return true;
}

RendererBridgeProcessResult SuperviseWindows(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id, int argc,
    const RendererChildLauncherChar *const argv[],
    const RendererSiblingPathResult &game_path,
    const RendererSiblingPathResult &presentation_path) {
  RendererBridgeProcessResult result;
  WindowsPipe game_to_frontend;
  WindowsPipe frontend_to_game;
  std::uint32_t error_code = 0U;
  if (!MakeInheritablePipe(game_to_frontend, error_code) ||
      !MakeInheritablePipe(frontend_to_game, error_code)) {
    return Failure(RendererBridgeProcessStatus::FAILED_STREAM_CREATE,
                   error_code);
  }

  RendererBridgeStreamHandles streams;
  if (!HandleToken(game_to_frontend.read.get(),
                   streams.game_to_frontend_read) ||
      !HandleToken(game_to_frontend.write.get(),
                   streams.game_to_frontend_write) ||
      !HandleToken(frontend_to_game.read.get(),
                   streams.frontend_to_game_read) ||
      !HandleToken(frontend_to_game.write.get(),
                   streams.frontend_to_game_write)) {
    return Failure(RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET,
                   static_cast<std::uint32_t>(ERROR_INVALID_HANDLE));
  }
  const RendererBridgeLaunchPlan plan = BuildRendererBridgeLaunchPlan(
      handoff, session_id, streams, argc, argv);
  result.launch_plan_status = plan.status;
  if (!plan.accepted || !HasExpectedBasenames(plan)) {
    result.status = RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET;
    return result;
  }

  WindowsHandle job(::CreateJobObjectW(nullptr, nullptr));
  if (!job.valid()) {
    return Failure(RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CREATE,
                   static_cast<std::uint32_t>(::GetLastError()));
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                &limits,
                                static_cast<DWORD>(sizeof(limits))) == FALSE) {
    return Failure(
        RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CONFIGURE,
        static_cast<std::uint32_t>(::GetLastError()));
  }

  WindowsProcess game;
  WindowsProcess presentation;
  RendererBridgeProcessStatus create_status =
      RendererBridgeProcessStatus::FAILED_INTERNAL;
  std::array<HANDLE, 2U> game_allowed{{frontend_to_game.read.get(),
                                      game_to_frontend.write.get()}};
  if (!CreateSuspendedChild(
          game_path.path, plan.game_child_arguments, game_allowed, game,
          create_status, error_code,
          RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_PROCESS_CREATE)) {
    result.status = create_status;
    result.failed_child = RendererBridgeObservedChild::GAME_HOST;
    result.native_error_code = error_code;
    return result;
  }
  if (::AssignProcessToJobObject(job.get(), game.process.get()) == FALSE) {
    const std::uint32_t assign_error =
        static_cast<std::uint32_t>(::GetLastError());
    bool ignored = false;
    std::uint32_t cleanup_error = 0U;
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, cleanup_error);
    result.status = result.game_reaped
                        ? RendererBridgeProcessStatus::
                              FAILED_WINDOWS_GAME_JOB_ASSIGN
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.failed_child = RendererBridgeObservedChild::GAME_HOST;
    result.native_error_code =
        result.game_reaped ? assign_error : cleanup_error;
    return result;
  }

  std::array<HANDLE, 2U> presentation_allowed{{game_to_frontend.read.get(),
                                              frontend_to_game.write.get()}};
  if (!CreateSuspendedChild(
          presentation_path.path, plan.presentation_child_arguments,
          presentation_allowed, presentation, create_status, error_code,
          RendererBridgeProcessStatus::
              FAILED_WINDOWS_PRESENTATION_PROCESS_CREATE)) {
    bool ignored = false;
    std::uint32_t cleanup_error = 0U;
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, cleanup_error);
    result.status = result.game_reaped
                        ? create_status
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.failed_child =
        RendererBridgeObservedChild::PRESENTATION_FRONTEND;
    result.native_error_code =
        result.game_reaped ? error_code : cleanup_error;
    return result;
  }
  if (::AssignProcessToJobObject(job.get(), presentation.process.get()) ==
      FALSE) {
    const std::uint32_t assign_error =
        static_cast<std::uint32_t>(::GetLastError());
    bool ignored = false;
    std::uint32_t presentation_cleanup_error = 0U;
    std::uint32_t game_cleanup_error = 0U;
    result.presentation_reaped = TerminateAndWaitWindows(
        presentation, ERROR_PROCESS_ABORTED, ignored,
        presentation_cleanup_error);
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, game_cleanup_error);
    const bool cleanup_ok =
        result.presentation_reaped && result.game_reaped;
    result.status = cleanup_ok
                        ? RendererBridgeProcessStatus::
                              FAILED_WINDOWS_PRESENTATION_JOB_ASSIGN
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.failed_child =
        RendererBridgeObservedChild::PRESENTATION_FRONTEND;
    result.native_error_code =
        cleanup_ok ? assign_error
                   : (presentation_cleanup_error != 0U
                          ? presentation_cleanup_error
                          : game_cleanup_error);
    return result;
  }

  // The exact per-child HANDLE_LIST attributes have been consumed. The
  // supervisor must not retain a bridge endpoint while either child runs.
  game_to_frontend.read.reset();
  game_to_frontend.write.reset();
  frontend_to_game.read.reset();
  frontend_to_game.write.reset();

  if (::ResumeThread(presentation.thread.get()) ==
      static_cast<DWORD>(-1)) {
    const std::uint32_t resume_error =
        static_cast<std::uint32_t>(::GetLastError());
    bool ignored = false;
    std::uint32_t presentation_cleanup_error = 0U;
    std::uint32_t game_cleanup_error = 0U;
    result.presentation_reaped = TerminateAndWaitWindows(
        presentation, ERROR_PROCESS_ABORTED, ignored,
        presentation_cleanup_error);
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, game_cleanup_error);
    const bool cleanup_ok =
        result.presentation_reaped && result.game_reaped;
    result.status = cleanup_ok
                        ? RendererBridgeProcessStatus::
                              FAILED_WINDOWS_PRESENTATION_THREAD_RESUME
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.failed_child =
        RendererBridgeObservedChild::PRESENTATION_FRONTEND;
    result.native_error_code =
        cleanup_ok ? resume_error
                   : (presentation_cleanup_error != 0U
                          ? presentation_cleanup_error
                          : game_cleanup_error);
    return result;
  }
  result.presentation_exec_confirmed = true;
  if (::ResumeThread(game.thread.get()) == static_cast<DWORD>(-1)) {
    const std::uint32_t resume_error =
        static_cast<std::uint32_t>(::GetLastError());
    bool ignored = false;
    std::uint32_t presentation_cleanup_error = 0U;
    std::uint32_t game_cleanup_error = 0U;
    result.presentation_reaped = TerminateAndWaitWindows(
        presentation, ERROR_PROCESS_ABORTED, ignored,
        presentation_cleanup_error);
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, game_cleanup_error);
    const bool cleanup_ok =
        result.presentation_reaped && result.game_reaped;
    result.status = cleanup_ok
                        ? RendererBridgeProcessStatus::
                              FAILED_WINDOWS_GAME_THREAD_RESUME
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.failed_child = RendererBridgeObservedChild::GAME_HOST;
    result.native_error_code =
        cleanup_ok ? resume_error
                   : (presentation_cleanup_error != 0U
                          ? presentation_cleanup_error
                          : game_cleanup_error);
    return result;
  }
  result.game_exec_confirmed = true;

  HANDLE children[2U] = {game.process.get(), presentation.process.get()};
  const DWORD first = ::WaitForMultipleObjects(2U, children, FALSE, INFINITE);
  if (first != WAIT_OBJECT_0 && first != WAIT_OBJECT_0 + 1U) {
    error_code = first == WAIT_FAILED
                     ? static_cast<std::uint32_t>(::GetLastError())
                     : static_cast<std::uint32_t>(ERROR_INVALID_DATA);
    const std::uint32_t wait_error = error_code;
    bool ignored = false;
    std::uint32_t presentation_cleanup_error = 0U;
    std::uint32_t game_cleanup_error = 0U;
    result.presentation_reaped = TerminateAndWaitWindows(
        presentation, ERROR_PROCESS_ABORTED, ignored,
        presentation_cleanup_error);
    result.game_reaped = TerminateAndWaitWindows(
        game, ERROR_PROCESS_ABORTED, ignored, game_cleanup_error);
    const bool cleanup_ok =
        result.presentation_reaped && result.game_reaped;
    result.status = cleanup_ok
                        ? RendererBridgeProcessStatus::FAILED_WAIT
                        : RendererBridgeProcessStatus::
                              FAILED_PEER_TERMINATION;
    result.native_error_code =
        cleanup_ok ? wait_error
                   : (presentation_cleanup_error != 0U
                          ? presentation_cleanup_error
                          : game_cleanup_error);
    return result;
  }

  const bool game_observed_first = first == WAIT_OBJECT_0;
  result.first_exit = game_observed_first
                          ? RendererBridgeObservedChild::GAME_HOST
                          : RendererBridgeObservedChild::PRESENTATION_FRONTEND;
  if (!game_observed_first &&
      ::WaitForSingleObject(game.process.get(), 0U) == WAIT_OBJECT_0) {
    // Both were already signaled by the time the wait result was inspected.
    // Preserve the natural game exit rather than manufacturing a failure.
    result.first_exit = RendererBridgeObservedChild::GAME_HOST;
  }

  if (result.first_exit == RendererBridgeObservedChild::GAME_HOST) {
    DWORD game_exit = 0U;
    if (::GetExitCodeProcess(game.process.get(), &game_exit) == FALSE) {
      result.status = RendererBridgeProcessStatus::FAILED_EXIT_QUERY;
      result.native_error_code = static_cast<std::uint32_t>(::GetLastError());
      result.game_reaped = true;
      bool ignored = false;
      std::uint32_t cleanup_error = 0U;
      result.presentation_reaped = TerminateAndWaitWindows(
          presentation, ERROR_PROCESS_ABORTED, ignored, cleanup_error);
      if (!result.presentation_reaped) {
        result.status =
            RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
        result.native_error_code = cleanup_error;
      }
      return result;
    }
    result.game_exit_kind = RendererBridgeGameExitKind::EXIT_CODE;
    result.game_exit_code = static_cast<std::uint32_t>(game_exit);
    result.native_game_wait_status = static_cast<std::uint32_t>(game_exit);
    result.game_reaped = true;

    bool terminated = false;
    if (!TerminateAndWaitWindows(presentation, ERROR_PROCESS_ABORTED,
                                 terminated, error_code)) {
      result.status = RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
      result.native_error_code = error_code;
      return result;
    }
    result.presentation_reaped = true;
    result.peer_terminated = terminated;
    result.status = RendererBridgeProcessStatus::COMPLETED_GAME_EXIT;
    result.completed = true;
    return result;
  }

  result.presentation_reaped = true;
  bool terminated = false;
  if (!TerminateAndWaitWindows(game, ERROR_PROCESS_ABORTED, terminated,
                               error_code)) {
    result.status = RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
    result.native_error_code = error_code;
    return result;
  }
  result.game_reaped = true;
  result.peer_terminated = terminated;
  result.status = RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST;
  return result;
}

#else

class PosixDescriptor final {
public:
  PosixDescriptor() = default;
  explicit PosixDescriptor(int descriptor) noexcept
      : descriptor_(descriptor) {}
  ~PosixDescriptor() { reset(); }

  PosixDescriptor(const PosixDescriptor &) = delete;
  PosixDescriptor &operator=(const PosixDescriptor &) = delete;
  PosixDescriptor(PosixDescriptor &&other) noexcept
      : descriptor_(other.release()) {}
  PosixDescriptor &operator=(PosixDescriptor &&other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  int get() const noexcept { return descriptor_; }
  bool valid() const noexcept { return descriptor_ >= 0; }
  int release() noexcept {
    const int value = descriptor_;
    descriptor_ = -1;
    return value;
  }
  void reset(int descriptor = -1) noexcept {
    if (valid()) {
      while (::close(descriptor_) != 0 && errno == EINTR) {
      }
    }
    descriptor_ = descriptor;
  }

private:
  int descriptor_ = -1;
};

struct PosixPipe final {
  PosixDescriptor read;
  PosixDescriptor write;
};

bool SetCloseOnExec(int descriptor, bool close_on_exec,
                    std::uint32_t &error_code) noexcept {
  const int current = ::fcntl(descriptor, F_GETFD);
  if (current < 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  const int desired = close_on_exec ? current | FD_CLOEXEC
                                    : current & ~FD_CLOEXEC;
  if (::fcntl(descriptor, F_SETFD, desired) < 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  return true;
}

bool NormalizePipeDescriptor(int &descriptor,
                             std::uint32_t &error_code) noexcept {
  if (descriptor >= 3) {
    return SetCloseOnExec(descriptor, true, error_code);
  }
#if defined(F_DUPFD_CLOEXEC)
  const int replacement = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
#else
  const int replacement = ::fcntl(descriptor, F_DUPFD, 3);
#endif
  if (replacement < 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
#if !defined(F_DUPFD_CLOEXEC)
  if (!SetCloseOnExec(replacement, true, error_code)) {
    (void)::close(replacement);
    return false;
  }
#endif
  (void)::close(descriptor);
  descriptor = replacement;
  return true;
}

bool MakeCloseOnExecPipe(PosixPipe &pipe,
                         std::uint32_t &error_code) noexcept {
  int descriptors[2U] = {-1, -1};
#if defined(__linux__) && defined(O_CLOEXEC)
  if (::pipe2(descriptors, O_CLOEXEC) != 0) {
#else
  if (::pipe(descriptors) != 0) {
#endif
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  if (!NormalizePipeDescriptor(descriptors[0U], error_code) ||
      !NormalizePipeDescriptor(descriptors[1U], error_code)) {
    if (descriptors[0U] >= 0) {
      (void)::close(descriptors[0U]);
    }
    if (descriptors[1U] >= 0) {
      (void)::close(descriptors[1U]);
    }
    return false;
  }
  pipe.read.reset(descriptors[0U]);
  pipe.write.reset(descriptors[1U]);
  return true;
}

bool DescriptorCloseLimit(int &limit,
                          std::uint32_t &error_code) noexcept {
  struct rlimit descriptor_limit = {};
  if (::getrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  if (descriptor_limit.rlim_cur == RLIM_INFINITY) {
    errno = 0;
    const long open_max = ::sysconf(_SC_OPEN_MAX);
    if (open_max < 4L) {
      error_code = static_cast<std::uint32_t>(
          errno != 0 ? errno : EOVERFLOW);
      return false;
    }
    limit = open_max > static_cast<long>((std::numeric_limits<int>::max)())
                ? (std::numeric_limits<int>::max)()
                : static_cast<int>(open_max);
  } else if (descriptor_limit.rlim_cur >
             static_cast<rlim_t>((std::numeric_limits<int>::max)())) {
    limit = (std::numeric_limits<int>::max)();
  } else {
    limit = static_cast<int>(descriptor_limit.rlim_cur);
  }
  return limit > 3;
}

enum class PosixChildFailureStage : std::uint32_t {
  PROCESS_GROUP = 1U,
  STARTUP_GATE = 2U,
  ENDPOINT_INHERITANCE = 3U,
  EXEC = 4U,
};

struct PosixChildFailureRecord final {
  std::uint32_t stage = 0U;
  std::uint32_t error_code = 0U;
};

[[noreturn]] void WriteChildFailureAndExit(
    int error_descriptor, PosixChildFailureStage stage,
    int error_code) noexcept {
  PosixChildFailureRecord record;
  record.stage = static_cast<std::uint32_t>(stage);
  record.error_code = static_cast<std::uint32_t>(error_code);
  const auto *bytes = reinterpret_cast<const unsigned char *>(&record);
  std::size_t offset = 0U;
  while (offset < sizeof(record)) {
    const ssize_t written =
        ::write(error_descriptor, bytes + offset, sizeof(record) - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
    } else if (written < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  ::_exit(static_cast<int>(kInvalidPropagationExitCode));
}

bool IsKeptDescriptor(int descriptor,
                      const std::array<int, 4U> &kept) noexcept {
  for (const int candidate : kept) {
    if (descriptor == candidate) {
      return true;
    }
  }
  return false;
}

void CloseUnrelatedDescriptors(int close_limit,
                               const std::array<int, 4U> &kept) noexcept {
  for (int descriptor = 3; descriptor < close_limit; ++descriptor) {
    if (!IsKeptDescriptor(descriptor, kept)) {
      (void)::close(descriptor);
    }
  }
}

bool WaitForStartupGate(int descriptor) noexcept {
  unsigned char unexpected = 0U;
  for (;;) {
    const ssize_t count = ::read(descriptor, &unexpected, 1U);
    if (count == 0) {
      return true;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
}

[[noreturn]] void ExecPosixChild(
    const std::string &path, std::vector<char *> &arguments,
    pid_t process_group, int inbound_descriptor, int outbound_descriptor,
    int gate_descriptor, int error_descriptor, int close_limit) noexcept {
  if (::setpgid(0, process_group) != 0) {
    WriteChildFailureAndExit(error_descriptor,
                             PosixChildFailureStage::PROCESS_GROUP, errno);
  }
  const std::array<int, 4U> kept{{inbound_descriptor, outbound_descriptor,
                                 gate_descriptor, error_descriptor}};
  CloseUnrelatedDescriptors(close_limit, kept);
  if (!WaitForStartupGate(gate_descriptor)) {
    WriteChildFailureAndExit(error_descriptor,
                             PosixChildFailureStage::STARTUP_GATE, errno);
  }
  (void)::close(gate_descriptor);

  std::uint32_t inherit_error = 0U;
  if (!SetCloseOnExec(inbound_descriptor, false, inherit_error) ||
      !SetCloseOnExec(outbound_descriptor, false, inherit_error)) {
    WriteChildFailureAndExit(
        error_descriptor, PosixChildFailureStage::ENDPOINT_INHERITANCE,
        static_cast<int>(inherit_error));
  }
  ::execv(path.c_str(), arguments.data());
  WriteChildFailureAndExit(error_descriptor, PosixChildFailureStage::EXEC,
                           errno);
}

std::vector<char *> BuildPosixArguments(
    const std::string &path,
    const std::vector<RendererChildLauncherString> &arguments) {
  std::vector<char *> pointers;
  pointers.reserve(arguments.size() + 1U);
  pointers.push_back(const_cast<char *>(path.c_str()));
  for (std::size_t index = 1U; index < arguments.size(); ++index) {
    pointers.push_back(const_cast<char *>(arguments[index].c_str()));
  }
  pointers.push_back(nullptr);
  return pointers;
}

bool ParentSetProcessGroup(pid_t child, pid_t group,
                           std::uint32_t &error_code) noexcept {
  if (::setpgid(child, group) == 0) {
    return true;
  }
  error_code = static_cast<std::uint32_t>(errno);
  return false;
}

bool ReapBlocking(pid_t child, int &status,
                  std::uint32_t &error_code) noexcept {
  for (;;) {
    const pid_t waited = ::waitpid(child, &status, 0);
    if (waited == child) {
      return true;
    }
    if (waited < 0 && errno == EINTR) {
      continue;
    }
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
}

bool KillPartialChildren(pid_t process_group, pid_t game,
                         pid_t presentation, bool &game_reaped,
                         bool &presentation_reaped,
                         std::uint32_t &error_code) noexcept {
  bool cleanup_ok = true;
  if (process_group > 0) {
    if (::kill(-process_group, SIGKILL) != 0 && errno != ESRCH) {
      error_code = static_cast<std::uint32_t>(errno);
      cleanup_ok = false;
    }
  }
  if (game > 0) {
    if (::kill(game, SIGKILL) != 0 && errno != ESRCH) {
      error_code = static_cast<std::uint32_t>(errno);
      cleanup_ok = false;
    }
  }
  if (presentation > 0) {
    if (::kill(presentation, SIGKILL) != 0 && errno != ESRCH) {
      error_code = static_cast<std::uint32_t>(errno);
      cleanup_ok = false;
    }
  }
  int ignored_status = 0;
  if (game > 0) {
    std::uint32_t reap_error = 0U;
    game_reaped = ReapBlocking(game, ignored_status, reap_error);
    if (!game_reaped) {
      error_code = reap_error;
      cleanup_ok = false;
    }
  }
  if (presentation > 0) {
    std::uint32_t reap_error = 0U;
    presentation_reaped =
        ReapBlocking(presentation, ignored_status, reap_error);
    if (!presentation_reaped) {
      error_code = reap_error;
      cleanup_ok = false;
    }
  }
  return cleanup_ok;
}

void ApplyPartialCleanup(
    RendererBridgeProcessResult &result, pid_t process_group, pid_t game,
    pid_t presentation, RendererBridgeProcessStatus intended_status,
    RendererBridgeObservedChild failed_child,
    std::uint32_t intended_error) noexcept {
  std::uint32_t cleanup_error = 0U;
  const bool cleanup_ok = KillPartialChildren(
      process_group, game, presentation, result.game_reaped,
      result.presentation_reaped, cleanup_error);
  result.failed_child = failed_child;
  result.status = cleanup_ok
                      ? intended_status
                      : RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
  result.native_error_code = cleanup_ok ? intended_error : cleanup_error;
}

bool ReadExecConfirmation(int descriptor,
                          PosixChildFailureRecord &failure,
                          std::uint32_t &error_code) noexcept {
  auto *bytes = reinterpret_cast<unsigned char *>(&failure);
  std::size_t offset = 0U;
  for (;;) {
    const ssize_t count =
        ::read(descriptor, bytes + offset, sizeof(failure) - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      if (offset == sizeof(failure)) {
        return false;
      }
      continue;
    }
    if (count == 0) {
      if (offset == 0U) {
        return true;
      }
      error_code = static_cast<std::uint32_t>(EPROTO);
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
}

RendererBridgeProcessStatus MapChildStartupFailure(
    const PosixChildFailureRecord &failure,
    RendererBridgeObservedChild child) noexcept {
  switch (static_cast<PosixChildFailureStage>(failure.stage)) {
  case PosixChildFailureStage::PROCESS_GROUP:
    return RendererBridgeProcessStatus::FAILED_POSIX_PROCESS_GROUP;
  case PosixChildFailureStage::STARTUP_GATE:
    return RendererBridgeProcessStatus::FAILED_POSIX_STARTUP_GATE;
  case PosixChildFailureStage::ENDPOINT_INHERITANCE:
    return RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET;
  case PosixChildFailureStage::EXEC:
    return child == RendererBridgeObservedChild::GAME_HOST
               ? RendererBridgeProcessStatus::FAILED_GAME_EXEC
               : RendererBridgeProcessStatus::FAILED_PRESENTATION_EXEC;
  }
  return RendererBridgeProcessStatus::FAILED_INTERNAL;
}

bool WaitNoHang(pid_t child, int &status, bool &reaped,
                std::uint32_t &error_code) noexcept {
  for (;;) {
    const pid_t waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) {
      reaped = true;
      return true;
    }
    if (waited == 0) {
      return true;
    }
    if (waited < 0 && errno == EINTR) {
      continue;
    }
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
}

bool TerminateProcessGroupAndReapPeer(pid_t process_group, pid_t peer,
                                     int &peer_status, bool &terminated,
                                     std::uint32_t &error_code) noexcept {
  bool reaped = false;
  if (!WaitNoHang(peer, peer_status, reaped, error_code)) {
    return false;
  }
  if (reaped) {
    return true;
  }
  if (::kill(-process_group, SIGTERM) != 0 && errno != ESRCH) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  terminated = true;
  const struct timespec delay = {0, 10L * 1000L * 1000L};
  for (unsigned int attempt = 0U; attempt < 50U; ++attempt) {
    if (!WaitNoHang(peer, peer_status, reaped, error_code)) {
      return false;
    }
    if (reaped) {
      return true;
    }
    struct timespec remaining = delay;
    while (::nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
  }
  if (::kill(-process_group, SIGKILL) != 0 && errno != ESRCH) {
    error_code = static_cast<std::uint32_t>(errno);
    return false;
  }
  return ReapBlocking(peer, peer_status, error_code);
}

void CapturePosixGameExit(int status,
                          RendererBridgeProcessResult &result) noexcept {
  result.native_game_wait_status = static_cast<std::uint32_t>(status);
  if (WIFEXITED(status)) {
    result.game_exit_kind = RendererBridgeGameExitKind::EXIT_CODE;
    result.game_exit_code =
        static_cast<std::uint32_t>(WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    result.game_exit_kind =
        RendererBridgeGameExitKind::TERMINATION_SIGNAL;
    result.game_termination_signal =
        static_cast<std::uint32_t>(WTERMSIG(status));
  }
}

RendererBridgeProcessResult SupervisePosix(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id, int argc,
    const RendererChildLauncherChar *const argv[],
    const RendererSiblingPathResult &game_path,
    const RendererSiblingPathResult &presentation_path) {
  RendererBridgeProcessResult result;
  std::uint32_t error_code = 0U;
  int close_limit = 0;
  if (!DescriptorCloseLimit(close_limit, error_code)) {
    return Failure(
        RendererBridgeProcessStatus::FAILED_POSIX_DESCRIPTOR_LIMIT,
        error_code);
  }

  PosixPipe game_to_frontend;
  PosixPipe frontend_to_game;
  PosixPipe startup_gate;
  PosixPipe game_error;
  PosixPipe presentation_error;
  if (!MakeCloseOnExecPipe(game_to_frontend, error_code) ||
      !MakeCloseOnExecPipe(frontend_to_game, error_code) ||
      !MakeCloseOnExecPipe(startup_gate, error_code) ||
      !MakeCloseOnExecPipe(game_error, error_code) ||
      !MakeCloseOnExecPipe(presentation_error, error_code)) {
    return Failure(RendererBridgeProcessStatus::FAILED_STREAM_CREATE,
                   error_code);
  }

  RendererBridgeStreamHandles streams;
  streams.game_to_frontend_read =
      static_cast<std::uint64_t>(game_to_frontend.read.get());
  streams.game_to_frontend_write =
      static_cast<std::uint64_t>(game_to_frontend.write.get());
  streams.frontend_to_game_read =
      static_cast<std::uint64_t>(frontend_to_game.read.get());
  streams.frontend_to_game_write =
      static_cast<std::uint64_t>(frontend_to_game.write.get());
  const RendererBridgeLaunchPlan plan = BuildRendererBridgeLaunchPlan(
      handoff, session_id, streams, argc, argv);
  result.launch_plan_status = plan.status;
  if (!plan.accepted || !HasExpectedBasenames(plan)) {
    result.status = RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET;
    return result;
  }

  std::vector<char *> game_arguments =
      BuildPosixArguments(game_path.path, plan.game_child_arguments);
  std::vector<char *> presentation_arguments = BuildPosixArguments(
      presentation_path.path, plan.presentation_child_arguments);

  const pid_t game = ::fork();
  if (game < 0) {
    return Failure(RendererBridgeProcessStatus::FAILED_POSIX_GAME_FORK,
                   static_cast<std::uint32_t>(errno));
  }
  if (game == 0) {
    game_error.read.reset();
    presentation_error.read.reset();
    presentation_error.write.reset();
    startup_gate.write.reset();
    ExecPosixChild(game_path.path, game_arguments, 0,
                   frontend_to_game.read.get(), game_to_frontend.write.get(),
                   startup_gate.read.get(), game_error.write.get(),
                   close_limit);
  }

  if (!ParentSetProcessGroup(game, game, error_code)) {
    const std::uint32_t process_group_error = error_code;
    startup_gate.write.reset();
    ApplyPartialCleanup(
        result, game, game, -1,
        RendererBridgeProcessStatus::FAILED_POSIX_PROCESS_GROUP,
        RendererBridgeObservedChild::GAME_HOST, process_group_error);
    return result;
  }

  const pid_t presentation = ::fork();
  if (presentation < 0) {
    const std::uint32_t fork_error = static_cast<std::uint32_t>(errno);
    startup_gate.write.reset();
    ApplyPartialCleanup(
        result, game, game, -1,
        RendererBridgeProcessStatus::FAILED_POSIX_PRESENTATION_FORK,
        RendererBridgeObservedChild::PRESENTATION_FRONTEND, fork_error);
    return result;
  }
  if (presentation == 0) {
    presentation_error.read.reset();
    game_error.read.reset();
    game_error.write.reset();
    startup_gate.write.reset();
    ExecPosixChild(presentation_path.path, presentation_arguments, game,
                   game_to_frontend.read.get(), frontend_to_game.write.get(),
                   startup_gate.read.get(), presentation_error.write.get(),
                   close_limit);
  }

  if (!ParentSetProcessGroup(presentation, game, error_code)) {
    const std::uint32_t process_group_error = error_code;
    startup_gate.write.reset();
    ApplyPartialCleanup(
        result, game, game, presentation,
        RendererBridgeProcessStatus::FAILED_POSIX_PROCESS_GROUP,
        RendererBridgeObservedChild::PRESENTATION_FRONTEND,
        process_group_error);
    return result;
  }

  game_error.write.reset();
  presentation_error.write.reset();
  startup_gate.read.reset();
  game_to_frontend.read.reset();
  game_to_frontend.write.reset();
  frontend_to_game.read.reset();
  frontend_to_game.write.reset();
  // EOF is the single atomic release after both children joined one group.
  startup_gate.write.reset();

  PosixChildFailureRecord game_startup_failure;
  PosixChildFailureRecord presentation_startup_failure;
  std::uint32_t game_confirmation_error = 0U;
  std::uint32_t presentation_confirmation_error = 0U;
  const bool game_exec = ReadExecConfirmation(
      game_error.read.get(), game_startup_failure, game_confirmation_error);
  const bool presentation_exec = ReadExecConfirmation(
      presentation_error.read.get(), presentation_startup_failure,
      presentation_confirmation_error);
  game_error.read.reset();
  presentation_error.read.reset();
  result.game_exec_confirmed = game_exec;
  result.presentation_exec_confirmed = presentation_exec;
  if (!game_exec || !presentation_exec) {
    const bool game_failed = !game_exec;
    const PosixChildFailureRecord &failure =
        game_failed ? game_startup_failure : presentation_startup_failure;
    const RendererBridgeObservedChild failed_child =
        game_failed ? RendererBridgeObservedChild::GAME_HOST
                    : RendererBridgeObservedChild::PRESENTATION_FRONTEND;
    const std::uint32_t startup_error =
        failure.error_code != 0U
            ? failure.error_code
            : (game_failed ? game_confirmation_error
                           : presentation_confirmation_error);
    ApplyPartialCleanup(result, game, game, presentation,
                        MapChildStartupFailure(failure, failed_child),
                        failed_child, startup_error);
    return result;
  }

  int first_status = 0;
  pid_t first = -1;
  for (;;) {
    first = ::waitpid(-game, &first_status, 0);
    if (first > 0) {
      break;
    }
    if (first < 0 && errno == EINTR) {
      continue;
    }
    const std::uint32_t wait_error = static_cast<std::uint32_t>(errno);
    ApplyPartialCleanup(
        result, game, game, presentation,
        RendererBridgeProcessStatus::FAILED_WAIT,
        RendererBridgeObservedChild::NONE, wait_error);
    return result;
  }

  result.first_exit = first == game
                          ? RendererBridgeObservedChild::GAME_HOST
                          : RendererBridgeObservedChild::PRESENTATION_FRONTEND;
  if (first == game) {
    result.game_reaped = true;
    CapturePosixGameExit(first_status, result);
    int presentation_status = 0;
    bool terminated = false;
    if (!TerminateProcessGroupAndReapPeer(
            game, presentation, presentation_status, terminated,
            error_code)) {
      result.status = RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
      result.native_error_code = error_code;
      return result;
    }
    result.presentation_reaped = true;
    result.peer_terminated = terminated;
    if (result.game_exit_kind == RendererBridgeGameExitKind::UNAVAILABLE) {
      result.status = RendererBridgeProcessStatus::FAILED_EXIT_QUERY;
      return result;
    }
    result.status = RendererBridgeProcessStatus::COMPLETED_GAME_EXIT;
    result.completed = true;
    return result;
  }

  result.presentation_reaped = true;
  int game_status = 0;
  bool game_already_reaped = false;
  if (!WaitNoHang(game, game_status, game_already_reaped, error_code)) {
    const std::uint32_t wait_error = error_code;
    ApplyPartialCleanup(
        result, game, game, -1,
        RendererBridgeProcessStatus::FAILED_WAIT,
        RendererBridgeObservedChild::NONE, wait_error);
    return result;
  }
  if (game_already_reaped) {
    result.first_exit = RendererBridgeObservedChild::GAME_HOST;
    result.game_reaped = true;
    CapturePosixGameExit(game_status, result);
    if (result.game_exit_kind == RendererBridgeGameExitKind::UNAVAILABLE) {
      result.status = RendererBridgeProcessStatus::FAILED_EXIT_QUERY;
      return result;
    }
    result.status = RendererBridgeProcessStatus::COMPLETED_GAME_EXIT;
    result.completed = true;
    return result;
  }

  bool terminated = false;
  if (!TerminateProcessGroupAndReapPeer(game, game, game_status, terminated,
                                        error_code)) {
    result.status = RendererBridgeProcessStatus::FAILED_PEER_TERMINATION;
    result.native_error_code = error_code;
    return result;
  }
  result.game_reaped = true;
  result.peer_terminated = terminated;
  result.status = RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST;
  return result;
}

#endif

} // namespace

RendererBridgeProcessResult SuperviseRendererBridgeProcesses(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept {
  try {
    // Run the complete pure transaction first with reserved, distinct preview
    // handles. Invalid argv/session/handoff data cannot create an OS resource.
    const RendererBridgeLaunchPlan preview = BuildRendererBridgeLaunchPlan(
        handoff, session_id, PreviewHandles(), argc, argv);
    if (!preview.accepted || !HasValidArguments(argc, argv) ||
        !HasExpectedBasenames(preview) ||
        preview.platform != CompileTimeHostPlatform()) {
      RendererBridgeProcessResult rejected =
          Failure(MapPreflightStatus(preview.status));
      rejected.launch_plan_status = preview.status;
      return rejected;
    }

    const RendererSiblingPathResult game_path =
        ResolveRendererSiblingPath(GameBasename());
    if (!game_path.accepted) {
      RendererBridgeProcessResult failed =
          Failure(RendererBridgeProcessStatus::FAILED_GAME_CHILD_PATH,
                  game_path.native_error_code);
      failed.failed_child = RendererBridgeObservedChild::GAME_HOST;
      failed.sibling_path_status = game_path.status;
      failed.launch_plan_status = preview.status;
      return failed;
    }
    const RendererSiblingPathResult presentation_path =
        ResolveRendererSiblingPath(PresentationBasename());
    if (!presentation_path.accepted) {
      RendererBridgeProcessResult failed = Failure(
          RendererBridgeProcessStatus::FAILED_PRESENTATION_CHILD_PATH,
          presentation_path.native_error_code);
      failed.failed_child =
          RendererBridgeObservedChild::PRESENTATION_FRONTEND;
      failed.sibling_path_status = presentation_path.status;
      failed.launch_plan_status = preview.status;
      return failed;
    }

#if defined(_WIN32)
    RendererBridgeProcessResult result =
        SuperviseWindows(handoff, session_id, argc, argv, game_path,
                         presentation_path);
#else
    RendererBridgeProcessResult result =
        SupervisePosix(handoff, session_id, argc, argv, game_path,
                       presentation_path);
#endif
    if (result.launch_plan_status ==
        RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF) {
      result.launch_plan_status = preview.status;
    }
    result.sibling_path_status = RendererSiblingPathStatus::READY;
    return result;
  } catch (...) {
    return Failure(RendererBridgeProcessStatus::FAILED_INTERNAL);
  }
}

[[noreturn]] void PropagateRendererBridgeGameExit(
    const RendererBridgeProcessResult &result) noexcept {
  if (!result.completed ||
      result.status != RendererBridgeProcessStatus::COMPLETED_GAME_EXIT) {
#if defined(_WIN32)
    ::ExitProcess(static_cast<UINT>(kInvalidPropagationExitCode));
#else
    ::_exit(static_cast<int>(kInvalidPropagationExitCode));
#endif
  }
#if defined(_WIN32)
  if (result.game_exit_kind != RendererBridgeGameExitKind::EXIT_CODE) {
    ::ExitProcess(static_cast<UINT>(kInvalidPropagationExitCode));
  }
  ::ExitProcess(static_cast<UINT>(result.game_exit_code));
#else
  if (result.game_exit_kind == RendererBridgeGameExitKind::EXIT_CODE &&
      result.game_exit_code <= 255U) {
    ::_exit(static_cast<int>(result.game_exit_code));
  }
  if (result.game_exit_kind ==
          RendererBridgeGameExitKind::TERMINATION_SIGNAL &&
      result.game_termination_signal > 0U &&
      result.game_termination_signal < static_cast<std::uint32_t>(NSIG)) {
    const int signal_number =
        static_cast<int>(result.game_termination_signal);
    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)::sigaction(signal_number, &action, nullptr);
    sigset_t unblocked;
    (void)sigemptyset(&unblocked);
    (void)sigaddset(&unblocked, signal_number);
    (void)::sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    (void)::kill(::getpid(), signal_number);
  }
  ::_exit(static_cast<int>(kInvalidPropagationExitCode));
#endif
}

bool IsKnownRendererBridgeObservedChild(
    RendererBridgeObservedChild child) noexcept {
  switch (child) {
  case RendererBridgeObservedChild::NONE:
  case RendererBridgeObservedChild::GAME_HOST:
  case RendererBridgeObservedChild::PRESENTATION_FRONTEND:
    return true;
  }
  return false;
}

bool IsKnownRendererBridgeGameExitKind(
    RendererBridgeGameExitKind kind) noexcept {
  switch (kind) {
  case RendererBridgeGameExitKind::UNAVAILABLE:
  case RendererBridgeGameExitKind::EXIT_CODE:
  case RendererBridgeGameExitKind::TERMINATION_SIGNAL:
    return true;
  }
  return false;
}

bool IsKnownRendererBridgeProcessStatus(
    RendererBridgeProcessStatus status) noexcept {
  switch (status) {
  case RendererBridgeProcessStatus::COMPLETED_GAME_EXIT:
  case RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST:
  case RendererBridgeProcessStatus::REJECTED_INVALID_HANDOFF:
  case RendererBridgeProcessStatus::REJECTED_INVALID_SESSION:
  case RendererBridgeProcessStatus::REJECTED_INVALID_ARGUMENTS:
  case RendererBridgeProcessStatus::REJECTED_LAUNCH_PLAN:
  case RendererBridgeProcessStatus::FAILED_GAME_CHILD_PATH:
  case RendererBridgeProcessStatus::FAILED_PRESENTATION_CHILD_PATH:
  case RendererBridgeProcessStatus::FAILED_STREAM_CREATE:
  case RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET:
  case RendererBridgeProcessStatus::FAILED_POSIX_GAME_FORK:
  case RendererBridgeProcessStatus::FAILED_POSIX_PRESENTATION_FORK:
  case RendererBridgeProcessStatus::FAILED_POSIX_DESCRIPTOR_LIMIT:
  case RendererBridgeProcessStatus::FAILED_POSIX_PROCESS_GROUP:
  case RendererBridgeProcessStatus::FAILED_POSIX_STARTUP_GATE:
  case RendererBridgeProcessStatus::FAILED_GAME_EXEC:
  case RendererBridgeProcessStatus::FAILED_PRESENTATION_EXEC:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_COMMAND_LINE:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CREATE:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CONFIGURE:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_ATTRIBUTE_LIST:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_PROCESS_CREATE:
  case RendererBridgeProcessStatus::
      FAILED_WINDOWS_PRESENTATION_PROCESS_CREATE:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_JOB_ASSIGN:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_PRESENTATION_JOB_ASSIGN:
  case RendererBridgeProcessStatus::
      FAILED_WINDOWS_PRESENTATION_THREAD_RESUME:
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_THREAD_RESUME:
  case RendererBridgeProcessStatus::FAILED_WAIT:
  case RendererBridgeProcessStatus::FAILED_EXIT_QUERY:
  case RendererBridgeProcessStatus::FAILED_PEER_TERMINATION:
  case RendererBridgeProcessStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererBridgeObservedChild child) noexcept {
  switch (child) {
  case RendererBridgeObservedChild::NONE:
    return "none";
  case RendererBridgeObservedChild::GAME_HOST:
    return "game-host";
  case RendererBridgeObservedChild::PRESENTATION_FRONTEND:
    return "presentation-frontend";
  }
  return "invalid";
}

const char *ToString(RendererBridgeGameExitKind kind) noexcept {
  switch (kind) {
  case RendererBridgeGameExitKind::UNAVAILABLE:
    return "unavailable";
  case RendererBridgeGameExitKind::EXIT_CODE:
    return "exit-code";
  case RendererBridgeGameExitKind::TERMINATION_SIGNAL:
    return "termination-signal";
  }
  return "invalid";
}

const char *ToString(RendererBridgeProcessStatus status) noexcept {
  switch (status) {
  case RendererBridgeProcessStatus::COMPLETED_GAME_EXIT:
    return "completed-game-exit";
  case RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST:
    return "presentation-exited-first";
  case RendererBridgeProcessStatus::REJECTED_INVALID_HANDOFF:
    return "rejected-invalid-handoff";
  case RendererBridgeProcessStatus::REJECTED_INVALID_SESSION:
    return "rejected-invalid-session";
  case RendererBridgeProcessStatus::REJECTED_INVALID_ARGUMENTS:
    return "rejected-invalid-arguments";
  case RendererBridgeProcessStatus::REJECTED_LAUNCH_PLAN:
    return "rejected-launch-plan";
  case RendererBridgeProcessStatus::FAILED_GAME_CHILD_PATH:
    return "failed-game-child-path";
  case RendererBridgeProcessStatus::FAILED_PRESENTATION_CHILD_PATH:
    return "failed-presentation-child-path";
  case RendererBridgeProcessStatus::FAILED_STREAM_CREATE:
    return "failed-stream-create";
  case RendererBridgeProcessStatus::FAILED_NATIVE_HANDLE_SET:
    return "failed-native-handle-set";
  case RendererBridgeProcessStatus::FAILED_POSIX_GAME_FORK:
    return "failed-posix-game-fork";
  case RendererBridgeProcessStatus::FAILED_POSIX_PRESENTATION_FORK:
    return "failed-posix-presentation-fork";
  case RendererBridgeProcessStatus::FAILED_POSIX_DESCRIPTOR_LIMIT:
    return "failed-posix-descriptor-limit";
  case RendererBridgeProcessStatus::FAILED_POSIX_PROCESS_GROUP:
    return "failed-posix-process-group";
  case RendererBridgeProcessStatus::FAILED_POSIX_STARTUP_GATE:
    return "failed-posix-startup-gate";
  case RendererBridgeProcessStatus::FAILED_GAME_EXEC:
    return "failed-game-exec";
  case RendererBridgeProcessStatus::FAILED_PRESENTATION_EXEC:
    return "failed-presentation-exec";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_COMMAND_LINE:
    return "failed-windows-command-line";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CREATE:
    return "failed-windows-job-create";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_JOB_CONFIGURE:
    return "failed-windows-job-configure";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_ATTRIBUTE_LIST:
    return "failed-windows-attribute-list";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_PROCESS_CREATE:
    return "failed-windows-game-process-create";
  case RendererBridgeProcessStatus::
      FAILED_WINDOWS_PRESENTATION_PROCESS_CREATE:
    return "failed-windows-presentation-process-create";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_JOB_ASSIGN:
    return "failed-windows-game-job-assign";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_PRESENTATION_JOB_ASSIGN:
    return "failed-windows-presentation-job-assign";
  case RendererBridgeProcessStatus::
      FAILED_WINDOWS_PRESENTATION_THREAD_RESUME:
    return "failed-windows-presentation-thread-resume";
  case RendererBridgeProcessStatus::FAILED_WINDOWS_GAME_THREAD_RESUME:
    return "failed-windows-game-thread-resume";
  case RendererBridgeProcessStatus::FAILED_WAIT:
    return "failed-wait";
  case RendererBridgeProcessStatus::FAILED_EXIT_QUERY:
    return "failed-exit-query";
  case RendererBridgeProcessStatus::FAILED_PEER_TERMINATION:
    return "failed-peer-termination";
  case RendererBridgeProcessStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
