/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererChildLauncher.h"
#include "RendererSiblingPath.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
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
#include <fcntl.h>
#include <unistd.h>
#endif

namespace RoR {
namespace {

RendererChildLaunchFailure Failure(RendererChildLaunchStatus status,
                                   std::uint32_t native_error_code = 0U) {
  RendererChildLaunchFailure failure;
  failure.status = status;
  failure.native_error_code = native_error_code;
  return failure;
}

template <typename Character>
bool HasValidArguments(int argc, const Character *const argv[]) {
  if (argc < 1 || argv == nullptr) {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

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

#if defined(_WIN32)

class WindowsHandle final {
public:
  WindowsHandle() = default;
  explicit WindowsHandle(HANDLE handle) : m_handle(handle) {}
  ~WindowsHandle() {
    if (IsValid()) {
      ::CloseHandle(m_handle);
    }
  }

  WindowsHandle(const WindowsHandle &) = delete;
  WindowsHandle &operator=(const WindowsHandle &) = delete;

  HANDLE Get() const { return m_handle; }
  bool IsValid() const {
    return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
  }
  void Reset(HANDLE handle = nullptr) {
    if (IsValid()) {
      ::CloseHandle(m_handle);
    }
    m_handle = handle;
  }

private:
  HANDLE m_handle = nullptr;
};

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

bool BuildWindowsCommandLine(const std::wstring &child_path, int argc,
                             const wchar_t *const argv[],
                             std::vector<wchar_t> &command_line) {
  std::wstring value;
  AppendQuotedWindowsArgument(child_path.c_str(), value);
  for (int index = 1; index < argc; ++index) {
    value.push_back(L' ');
    AppendQuotedWindowsArgument(argv[index], value);
  }
  // CreateProcessW limits the command line to 32,767 characters including
  // the terminating null. Reject rather than truncate any forwarded argument.
  if (value.size() >= 32767U) {
    return false;
  }
  command_line.assign(value.begin(), value.end());
  command_line.push_back(L'\0');
  return true;
}

bool IsUsableStandardHandle(HANDLE handle) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD flags = 0U;
  return ::GetHandleInformation(handle, &flags) != FALSE;
}

struct InheritedStandardHandles {
  WindowsHandle input;
  WindowsHandle output;
  WindowsHandle error;
  WindowsHandle absent_handle_sentinel;
  HANDLE input_value = nullptr;
  HANDLE output_value = nullptr;
  HANDLE error_value = nullptr;
  HANDLE list[4] = {nullptr, nullptr, nullptr, nullptr};
  SIZE_T count = 0U;
};

bool DuplicateStandardHandle(DWORD identifier, WindowsHandle &duplicate,
                             HANDLE &startup_value,
                             InheritedStandardHandles &inherited,
                             std::uint32_t &error_code) {
  const HANDLE source = ::GetStdHandle(identifier);
  startup_value = source;
  if (!IsUsableStandardHandle(source)) {
    return true;
  }
  HANDLE target = nullptr;
  if (::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(),
                        &target, 0U, TRUE, DUPLICATE_SAME_ACCESS) == FALSE) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  duplicate.Reset(target);
  startup_value = target;
  inherited.list[inherited.count++] = target;
  return true;
}

bool EnsureNonemptyInheritedHandleList(
    InheritedStandardHandles &inherited,
    std::uint32_t &error_code) {
  if (inherited.count != 0U) {
    return true;
  }
  SECURITY_ATTRIBUTES security = {};
  security.nLength = static_cast<DWORD>(sizeof(security));
  security.bInheritHandle = TRUE;
  inherited.absent_handle_sentinel.Reset(
      ::CreateEventW(&security, TRUE, FALSE, nullptr));
  if (!inherited.absent_handle_sentinel.IsValid()) {
    error_code = static_cast<std::uint32_t>(::GetLastError());
    return false;
  }
  // STARTF_USESTDHANDLES is required to preserve explicit NULL or
  // INVALID_HANDLE_VALUE standard handles, while CreateProcessW requires
  // bInheritHandles=TRUE. A private unnamed event makes the allow-list
  // nonempty without manufacturing a standard stream or exposing a useful
  // external resource to the child.
  inherited.list[inherited.count++] =
      inherited.absent_handle_sentinel.Get();
  return true;
}

struct ProcessAttributeList {
  std::vector<std::uintptr_t> storage;
  LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

  ~ProcessAttributeList() {
    if (list != nullptr) {
      ::DeleteProcThreadAttributeList(list);
    }
  }
};

RendererChildLaunchFailure LaunchWindows(
    const std::wstring &child_path, int argc, const wchar_t *const argv[]) {
  std::vector<wchar_t> command_line;
  if (!BuildWindowsCommandLine(child_path, argc, argv, command_line)) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_COMMAND_LINE,
                   static_cast<std::uint32_t>(ERROR_BAD_ARGUMENTS));
  }

  WindowsHandle job(::CreateJobObjectW(nullptr, nullptr));
  if (!job.IsValid()) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CREATE,
                   static_cast<std::uint32_t>(::GetLastError()));
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits = {};
  job_limits.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation,
                                &job_limits,
                                static_cast<DWORD>(sizeof(job_limits))) ==
      FALSE) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CONFIGURE,
                   static_cast<std::uint32_t>(::GetLastError()));
  }

  InheritedStandardHandles inherited;
  std::uint32_t duplicate_error = 0U;
  if (!DuplicateStandardHandle(STD_INPUT_HANDLE, inherited.input,
                               inherited.input_value, inherited,
                               duplicate_error) ||
      !DuplicateStandardHandle(STD_OUTPUT_HANDLE, inherited.output,
                               inherited.output_value, inherited,
                               duplicate_error) ||
      !DuplicateStandardHandle(STD_ERROR_HANDLE, inherited.error,
                               inherited.error_value, inherited,
                               duplicate_error) ||
      !EnsureNonemptyInheritedHandleList(inherited, duplicate_error)) {
    return Failure(
        RendererChildLaunchStatus::FAILED_WINDOWS_STANDARD_HANDLE_DUPLICATION,
        duplicate_error);
  }

  ProcessAttributeList attributes;
  SIZE_T bytes = 0U;
  (void)::InitializeProcThreadAttributeList(nullptr, 1U, 0U, &bytes);
  if (bytes == 0U) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_ATTRIBUTE_LIST,
                   static_cast<std::uint32_t>(::GetLastError()));
  }
  attributes.storage.resize(
      (bytes + sizeof(std::uintptr_t) - 1U) / sizeof(std::uintptr_t));
  attributes.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
      attributes.storage.data());
  if (::InitializeProcThreadAttributeList(attributes.list, 1U, 0U, &bytes) ==
      FALSE) {
    const std::uint32_t error =
        static_cast<std::uint32_t>(::GetLastError());
    attributes.list = nullptr;
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_ATTRIBUTE_LIST,
                   error);
  }
  if (::UpdateProcThreadAttribute(
          attributes.list, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited.list, inherited.count * sizeof(HANDLE), nullptr,
          nullptr) == FALSE) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_ATTRIBUTE_LIST,
                   static_cast<std::uint32_t>(::GetLastError()));
  }

  STARTUPINFOEXW startup = {};
  startup.StartupInfo.cb = attributes.list != nullptr
                               ? static_cast<DWORD>(sizeof(STARTUPINFOEXW))
                               : static_cast<DWORD>(sizeof(STARTUPINFOW));
  startup.lpAttributeList = attributes.list;
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = inherited.input_value;
  startup.StartupInfo.hStdOutput = inherited.output_value;
  startup.StartupInfo.hStdError = inherited.error_value;

  PROCESS_INFORMATION process = {};
  const DWORD creation_flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                               EXTENDED_STARTUPINFO_PRESENT;
  const BOOL created = ::CreateProcessW(
      child_path.c_str(), command_line.data(), nullptr, nullptr,
      TRUE, creation_flags, nullptr, nullptr,
      &startup.StartupInfo, &process);
  if (created == FALSE) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_PROCESS_CREATE,
                   static_cast<std::uint32_t>(::GetLastError()));
  }
  WindowsHandle process_handle(process.hProcess);
  WindowsHandle thread_handle(process.hThread);

  if (::AssignProcessToJobObject(job.Get(), process_handle.Get()) == FALSE) {
    const std::uint32_t error =
        static_cast<std::uint32_t>(::GetLastError());
    (void)::TerminateProcess(process_handle.Get(),
                             static_cast<UINT>(ERROR_PROCESS_ABORTED));
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_JOB_ASSIGN,
                   error);
  }
  if (::ResumeThread(thread_handle.Get()) == static_cast<DWORD>(-1)) {
    const std::uint32_t error =
        static_cast<std::uint32_t>(::GetLastError());
    (void)::TerminateJobObject(job.Get(),
                               static_cast<UINT>(ERROR_PROCESS_ABORTED));
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_THREAD_RESUME,
                   error);
  }
  thread_handle.Reset();

  if (::WaitForSingleObject(process_handle.Get(), INFINITE) != WAIT_OBJECT_0) {
    const std::uint32_t error =
        static_cast<std::uint32_t>(::GetLastError());
    (void)::TerminateJobObject(job.Get(),
                               static_cast<UINT>(ERROR_PROCESS_ABORTED));
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_WAIT, error);
  }
  DWORD child_exit_code = 0U;
  if (::GetExitCodeProcess(process_handle.Get(), &child_exit_code) == FALSE) {
    return Failure(RendererChildLaunchStatus::FAILED_WINDOWS_EXIT_QUERY,
                   static_cast<std::uint32_t>(::GetLastError()));
  }

  // Close the completed process and job explicitly. If this launcher had died
  // while waiting, closing the process-owned job handle would instead have
  // killed the still-running child.
  process_handle.Reset();
  job.Reset();
  ::ExitProcess(child_exit_code);
}

#else

RendererChildLaunchFailure LaunchPosix(const std::string &child_path, int argc,
                                       const char *const argv[]) {
  std::vector<char *> child_arguments;
  child_arguments.reserve(static_cast<std::size_t>(argc) + 1U);
  child_arguments.push_back(const_cast<char *>(child_path.c_str()));
  for (int index = 1; index < argc; ++index) {
    child_arguments.push_back(const_cast<char *>(argv[index]));
  }
  child_arguments.push_back(nullptr);

  struct StandardDescriptorState {
    int descriptor;
    int original_flags;
    bool changed;
  };
  StandardDescriptorState descriptors[] = {
      {STDIN_FILENO, 0, false},
      {STDOUT_FILENO, 0, false},
      {STDERR_FILENO, 0, false},
  };
  const auto restore_standard_descriptor_flags = [&descriptors]() {
    for (StandardDescriptorState &descriptor : descriptors) {
      if (descriptor.changed) {
        (void)::fcntl(descriptor.descriptor, F_SETFD,
                      descriptor.original_flags);
      }
    }
  };
  for (StandardDescriptorState &descriptor : descriptors) {
    errno = 0;
    descriptor.original_flags = ::fcntl(descriptor.descriptor, F_GETFD);
    if (descriptor.original_flags < 0) {
      if (errno == EBADF) {
        continue;
      }
      const std::uint32_t error = static_cast<std::uint32_t>(errno);
      restore_standard_descriptor_flags();
      return Failure(
          RendererChildLaunchStatus::FAILED_POSIX_STANDARD_HANDLE_PREPARE,
          error);
    }
    if ((descriptor.original_flags & FD_CLOEXEC) != 0 &&
        ::fcntl(descriptor.descriptor, F_SETFD,
                descriptor.original_flags & ~FD_CLOEXEC) < 0) {
      const std::uint32_t error = static_cast<std::uint32_t>(errno);
      restore_standard_descriptor_flags();
      return Failure(
          RendererChildLaunchStatus::FAILED_POSIX_STANDARD_HANDLE_PREPARE,
          error);
    }
    descriptor.changed =
        (descriptor.original_flags & FD_CLOEXEC) != 0;
  }

  // execv uses this exact path, inherits the caller's environment, cwd, and
  // standard descriptors, and replaces the launcher process on success.
  ::execv(child_path.c_str(), child_arguments.data());
  const std::uint32_t exec_error = static_cast<std::uint32_t>(errno);
  restore_standard_descriptor_flags();
  return Failure(RendererChildLaunchStatus::FAILED_POSIX_EXEC, exec_error);
}

#endif

} // namespace

bool IsKnownRendererChildLaunchStatus(
    RendererChildLaunchStatus status) noexcept {
  switch (status) {
  case RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF:
  case RendererChildLaunchStatus::REJECTED_INVALID_ARGUMENTS:
  case RendererChildLaunchStatus::FAILED_CURRENT_EXECUTABLE_PATH:
  case RendererChildLaunchStatus::FAILED_CHILD_PATH:
  case RendererChildLaunchStatus::FAILED_POSIX_STANDARD_HANDLE_PREPARE:
  case RendererChildLaunchStatus::FAILED_POSIX_EXEC:
  case RendererChildLaunchStatus::FAILED_WINDOWS_COMMAND_LINE:
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CREATE:
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CONFIGURE:
  case RendererChildLaunchStatus::FAILED_WINDOWS_STANDARD_HANDLE_DUPLICATION:
  case RendererChildLaunchStatus::FAILED_WINDOWS_ATTRIBUTE_LIST:
  case RendererChildLaunchStatus::FAILED_WINDOWS_PROCESS_CREATE:
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_ASSIGN:
  case RendererChildLaunchStatus::FAILED_WINDOWS_THREAD_RESUME:
  case RendererChildLaunchStatus::FAILED_WINDOWS_WAIT:
  case RendererChildLaunchStatus::FAILED_WINDOWS_EXIT_QUERY:
  case RendererChildLaunchStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

RendererChildLaunchFailure LaunchRendererChildAndPropagateExit(
    const RendererStartupHandoffResult &handoff, int argc,
    const RendererChildLauncherChar *const argv[]) {
  try {
    const HostRenderPlatform host_platform = CompileTimeHostPlatform();
    if (host_platform == HostRenderPlatform::UNKNOWN ||
        !handoff.accepted || handoff.package_platform != host_platform) {
      return Failure(RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF);
    }
    const char *basename = RendererFrontendChildExecutableName(handoff);
    if (basename == nullptr || basename[0] == '\0') {
      return Failure(RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF);
    }
    if (!HasValidArguments(argc, argv)) {
      return Failure(RendererChildLaunchStatus::REJECTED_INVALID_ARGUMENTS);
    }

    const RendererSiblingPathResult sibling =
        ResolveRendererSiblingPath(basename);
    if (!sibling.accepted) {
      switch (sibling.status) {
      case RendererSiblingPathStatus::REJECTED_INVALID_BASENAME:
        return Failure(
            RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF,
            sibling.native_error_code);
      case RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH:
        return Failure(
            RendererChildLaunchStatus::FAILED_CURRENT_EXECUTABLE_PATH,
            sibling.native_error_code);
      case RendererSiblingPathStatus::FAILED_CHILD_PATH:
        return Failure(RendererChildLaunchStatus::FAILED_CHILD_PATH,
                       sibling.native_error_code);
      case RendererSiblingPathStatus::FAILED_INTERNAL:
        return Failure(RendererChildLaunchStatus::FAILED_INTERNAL,
                       sibling.native_error_code);
      case RendererSiblingPathStatus::READY:
        return Failure(RendererChildLaunchStatus::FAILED_INTERNAL);
      }
      return Failure(RendererChildLaunchStatus::FAILED_INTERNAL);
    }
#if defined(_WIN32)
    return LaunchWindows(sibling.path, argc, argv);
#else
    return LaunchPosix(sibling.path, argc, argv);
#endif
  } catch (...) {
    return Failure(RendererChildLaunchStatus::FAILED_INTERNAL);
  }
}

const char *ToString(RendererChildLaunchStatus status) noexcept {
  switch (status) {
  case RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF:
    return "rejected-invalid-handoff";
  case RendererChildLaunchStatus::REJECTED_INVALID_ARGUMENTS:
    return "rejected-invalid-arguments";
  case RendererChildLaunchStatus::FAILED_CURRENT_EXECUTABLE_PATH:
    return "failed-current-executable-path";
  case RendererChildLaunchStatus::FAILED_CHILD_PATH:
    return "failed-child-path";
  case RendererChildLaunchStatus::FAILED_POSIX_STANDARD_HANDLE_PREPARE:
    return "failed-posix-standard-handle-prepare";
  case RendererChildLaunchStatus::FAILED_POSIX_EXEC:
    return "failed-posix-exec";
  case RendererChildLaunchStatus::FAILED_WINDOWS_COMMAND_LINE:
    return "failed-windows-command-line";
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CREATE:
    return "failed-windows-job-create";
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_CONFIGURE:
    return "failed-windows-job-configure";
  case RendererChildLaunchStatus::FAILED_WINDOWS_STANDARD_HANDLE_DUPLICATION:
    return "failed-windows-standard-handle-duplication";
  case RendererChildLaunchStatus::FAILED_WINDOWS_ATTRIBUTE_LIST:
    return "failed-windows-attribute-list";
  case RendererChildLaunchStatus::FAILED_WINDOWS_PROCESS_CREATE:
    return "failed-windows-process-create";
  case RendererChildLaunchStatus::FAILED_WINDOWS_JOB_ASSIGN:
    return "failed-windows-job-assign";
  case RendererChildLaunchStatus::FAILED_WINDOWS_THREAD_RESUME:
    return "failed-windows-thread-resume";
  case RendererChildLaunchStatus::FAILED_WINDOWS_WAIT:
    return "failed-windows-wait";
  case RendererChildLaunchStatus::FAILED_WINDOWS_EXIT_QUERY:
    return "failed-windows-exit-query";
  case RendererChildLaunchStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
