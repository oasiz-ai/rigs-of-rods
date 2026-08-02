/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererChildLauncher.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer child launcher test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#else
  return RoR::HostRenderPlatform::LINUX;
#endif
}

RoR::RendererStartupHandoffResult MakeAcceptedHandoff(
    RoR::HostRenderPlatform platform = CurrentPlatform(),
    RoR::RendererFrontendPreference frontend =
        RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
  RoR::RendererStartupHandoffRequest request;
  request.startup.frontend = frontend;
  request.startup.directional_shadows =
      RoR::DirectionalShadowPreference::PSSM;
  request.startup.host_platform = platform;

  RoR::RendererStartupPackageAvailability package;
  package.package_platform = platform;
  package.ogre14_child_present = true;
  package.ogre_next_child_present = true;
  package.ogre_next_child_production_ready = true;
  package.ogre_next_pssm_admitted = true;
  return RoR::ResolveRendererStartupHandoff(request, package);
}

void TestContractAndFailures() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto status = static_cast<RoR::RendererChildLaunchStatus>(value);
    Require(RoR::IsKnownRendererChildLaunchStatus(status) == (value <= 16U),
            "status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererChildLaunchStatus::FAILED_POSIX_EXEC),
              "failed-posix-exec") == 0,
          "POSIX failure string changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererChildLaunchStatus::FAILED_WINDOWS_JOB_ASSIGN),
              "failed-windows-job-assign") == 0,
          "Windows job failure string changed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererChildLaunchStatus>(255U)),
              "invalid") == 0,
          "unknown status did not fail closed");

#if defined(_WIN32)
  const wchar_t *valid_arguments[] = {L"launcher"};
#else
  const char *valid_arguments[] = {"launcher"};
#endif
  RoR::RendererStartupHandoffResult rejected;
  const auto rejected_failure = RoR::LaunchRendererChildAndPropagateExit(
      rejected, 1, valid_arguments);
  Require(rejected_failure.version ==
                  RoR::kRendererChildLauncherContractVersion &&
              rejected_failure.status ==
                  RoR::RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF &&
              rejected_failure.native_error_code == 0U,
          "rejected handoff reached process launch");

  const auto accepted = MakeAcceptedHandoff();
  Require(accepted.accepted &&
              accepted.child == RoR::RendererFrontendChild::OGRE_NEXT,
          "test handoff was not admitted");
  const auto missing_arguments = RoR::LaunchRendererChildAndPropagateExit(
      accepted, 0, nullptr);
  Require(missing_arguments.status ==
              RoR::RendererChildLaunchStatus::REJECTED_INVALID_ARGUMENTS,
          "missing argv reached process launch");
#if defined(_WIN32)
  const wchar_t *null_argument[] = {L"launcher", nullptr};
#else
  const char *null_argument[] = {"launcher", nullptr};
#endif
  const auto null_failure = RoR::LaunchRendererChildAndPropagateExit(
      accepted, 2, null_argument);
  Require(null_failure.status ==
              RoR::RendererChildLaunchStatus::REJECTED_INVALID_ARGUMENTS,
          "embedded null argument reached process launch");

  const RoR::HostRenderPlatform platforms[] = {
      RoR::HostRenderPlatform::MACOS,
      RoR::HostRenderPlatform::LINUX,
      RoR::HostRenderPlatform::WINDOWS,
  };
  unsigned int foreign_platforms_rejected = 0U;
  for (RoR::HostRenderPlatform platform : platforms) {
    if (platform == CurrentPlatform()) {
      continue;
    }
    const auto foreign_handoff = MakeAcceptedHandoff(platform);
    Require(foreign_handoff.accepted &&
                foreign_handoff.package_platform == platform,
            "foreign-platform test handoff was not internally valid");
    const char *foreign_basename =
        RoR::RendererFrontendChildExecutableName(foreign_handoff);
    const bool has_windows_suffix =
        std::strcmp(foreign_basename, "RoR-OgreNext.exe") == 0;
    Require(has_windows_suffix ==
                (platform == RoR::HostRenderPlatform::WINDOWS),
            "foreign-platform fixture did not exercise executable suffix");
    const auto foreign_failure = RoR::LaunchRendererChildAndPropagateExit(
        foreign_handoff, 1, valid_arguments);
    Require(foreign_failure.status ==
                    RoR::RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF &&
                foreign_failure.native_error_code == 0U,
            "foreign package platform reached basename or process launch");
    ++foreign_platforms_rejected;
  }
  Require(foreign_platforms_rejected == 2U,
          "foreign-platform rejection matrix was not exhaustive");
}

#if defined(_WIN32)

std::wstring CurrentExecutablePath() {
  std::vector<wchar_t> buffer(32768U);
  const DWORD length = ::GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  Require(length != 0U && length < buffer.size(),
          "could not resolve test executable");
  return std::wstring(buffer.data(), length);
}

std::wstring MakeTemporaryDirectory() {
  std::vector<wchar_t> temp_path(32768U);
  const DWORD path_length = ::GetTempPathW(
      static_cast<DWORD>(temp_path.size()), temp_path.data());
  Require(path_length != 0U && path_length < temp_path.size(),
          "GetTempPathW failed");
  std::vector<wchar_t> temporary_name(32768U);
  Require(::GetTempFileNameW(temp_path.data(), L"ror", 0U,
                             temporary_name.data()) != 0U,
          "GetTempFileNameW failed");
  Require(::DeleteFileW(temporary_name.data()) != FALSE,
          "temporary placeholder delete failed");
  Require(::CreateDirectoryW(temporary_name.data(), nullptr) != FALSE,
          "temporary directory create failed");
  return std::wstring(temporary_name.data());
}

void AppendQuotedArgument(const std::wstring &argument,
                          std::wstring &command_line) {
  command_line.push_back(L'"');
  std::size_t backslashes = 0U;
  for (wchar_t value : argument) {
    if (value == L'\\') {
      ++backslashes;
    } else if (value == L'"') {
      command_line.append(backslashes * 2U + 1U, L'\\');
      command_line.push_back(L'"');
      backslashes = 0U;
    } else {
      command_line.append(backslashes, L'\\');
      backslashes = 0U;
      command_line.push_back(value);
    }
  }
  command_line.append(backslashes * 2U, L'\\');
  command_line.push_back(L'"');
}

std::string ReadHandle(HANDLE handle) {
  std::string output;
  char buffer[256] = {};
  for (;;) {
    DWORD count = 0U;
    if (::ReadFile(handle, buffer, sizeof(buffer), &count, nullptr) == FALSE ||
        count == 0U) {
      break;
    }
    output.append(buffer, count);
  }
  return output;
}

int InvokeLauncher(int argc, wchar_t *argv[]) {
  HANDLE absent_standard_handle = nullptr;
  if (argc == 2 &&
      std::wstring(argv[1]) == L"--invoke-launcher-invalid-stdio") {
    absent_standard_handle = INVALID_HANDLE_VALUE;
  }
  if (argc == 2 &&
      (std::wstring(argv[1]) == L"--invoke-launcher-null-stdio" ||
       std::wstring(argv[1]) == L"--invoke-launcher-invalid-stdio")) {
    if (::SetStdHandle(STD_INPUT_HANDLE, absent_standard_handle) == FALSE ||
        ::SetStdHandle(STD_OUTPUT_HANDLE, absent_standard_handle) == FALSE ||
        ::SetStdHandle(STD_ERROR_HANDLE, absent_standard_handle) == FALSE) {
      return 121;
    }
  }
  const auto failure = RoR::LaunchRendererChildAndPropagateExit(
      MakeAcceptedHandoff(), argc, const_cast<const wchar_t *const *>(argv));
  std::cerr << "launcher-returned:" << RoR::ToString(failure.status) << ':'
            << failure.native_error_code << '\n';
  return 120;
}

void TestWindowsAbsentStandardHandles() {
  const std::wstring executable = CurrentExecutablePath();
  struct AbsentHandleCase {
    const wchar_t *mode;
    DWORD expected_exit;
  };
  const AbsentHandleCase cases[] = {
      {L"--invoke-launcher-null-stdio", 0xc0de0043U},
      {L"--invoke-launcher-invalid-stdio", 0xc0de0044U},
  };
  for (const AbsentHandleCase &test_case : cases) {
    std::wstring command_line;
    AppendQuotedArgument(executable, command_line);
    command_line.push_back(L' ');
    AppendQuotedArgument(test_case.mode, command_line);
    std::vector<wchar_t> mutable_command_line(command_line.begin(),
                                               command_line.end());
    mutable_command_line.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    PROCESS_INFORMATION process = {};
    Require(::CreateProcessW(executable.c_str(), mutable_command_line.data(),
                             nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                             &startup, &process) != FALSE,
            "absent-standard-handle launcher subprocess create failed");
    ::CloseHandle(process.hThread);
    Require(::WaitForSingleObject(process.hProcess, INFINITE) ==
                WAIT_OBJECT_0,
            "absent-standard-handle launcher subprocess wait failed");
    DWORD exit_code = 0U;
    Require(::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE,
            "absent-standard-handle launcher exit query failed");
    ::CloseHandle(process.hProcess);
    Require(exit_code == test_case.expected_exit,
            "absent standard handle identity was not preserved");
  }
}

void TestWindowsEndToEnd() {
  const std::wstring temporary_directory = MakeTemporaryDirectory();
  const std::wstring decoy =
      temporary_directory + L"\\RoR-OgreNext.exe";
  HANDLE decoy_handle = ::CreateFileW(
      decoy.c_str(), GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(decoy_handle != INVALID_HANDLE_VALUE, "decoy create failed");
  const char decoy_bytes[] = "not-a-renderer-child";
  DWORD written = 0U;
  Require(::WriteFile(decoy_handle, decoy_bytes,
                      static_cast<DWORD>(sizeof(decoy_bytes)), &written,
                      nullptr) != FALSE,
          "decoy write failed");
  ::CloseHandle(decoy_handle);

  Require(::SetEnvironmentVariableW(L"ROR_RENDERER_CHILD_TEST_TOKEN",
                                    L"renderer-child-env-ok") != FALSE,
          "test token environment setup failed");
  Require(::SetEnvironmentVariableW(L"ROR_RENDERER_CHILD_EXPECTED_CWD",
                                    temporary_directory.c_str()) != FALSE,
          "cwd environment setup failed");
  Require(::SetEnvironmentVariableW(L"ROR_RENDERER_CHILD_PATH",
                                    decoy.c_str()) != FALSE,
          "path override decoy setup failed");
  Require(::SetEnvironmentVariableW(L"PATH", temporary_directory.c_str()) !=
              FALSE,
          "PATH decoy setup failed");

  SECURITY_ATTRIBUTES security = {};
  security.nLength = static_cast<DWORD>(sizeof(security));
  security.bInheritHandle = TRUE;
  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;
  Require(::CreatePipe(&stdin_read, &stdin_write, &security, 0U) != FALSE &&
              ::CreatePipe(&stdout_read, &stdout_write, &security, 0U) !=
                  FALSE &&
              ::CreatePipe(&stderr_read, &stderr_write, &security, 0U) !=
                  FALSE,
          "standard stream pipe creation failed");
  Require(::SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0U) !=
                  FALSE &&
              ::SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0U) !=
                  FALSE &&
              ::SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0U) !=
                  FALSE,
          "parent stream handles remained inheritable");

  const std::wstring executable = CurrentExecutablePath();
  const std::wstring launcher_alias =
      temporary_directory + L"\\renderer-launcher-alias.exe";
  constexpr DWORD allow_unprivileged_symlink_create = 0x2U;
  BOOL launcher_alias_created = ::CreateSymbolicLinkW(
      launcher_alias.c_str(), executable.c_str(),
      allow_unprivileged_symlink_create);
  DWORD launcher_alias_error = ERROR_SUCCESS;
  if (launcher_alias_created == FALSE) {
    launcher_alias_error = ::GetLastError();
    if (launcher_alias_error == ERROR_INVALID_PARAMETER) {
      launcher_alias_created = ::CreateSymbolicLinkW(
          launcher_alias.c_str(), executable.c_str(), 0U);
      if (launcher_alias_created == FALSE) {
        launcher_alias_error = ::GetLastError();
      }
    }
  }
  if (launcher_alias_created == FALSE) {
    std::clog << "renderer child launcher test note: symlink-alias "
                 "regression skipped; CreateSymbolicLinkW error="
              << launcher_alias_error << '\n';
  }
  const std::wstring launcher_entry =
      launcher_alias_created != FALSE ? launcher_alias : executable;
  const std::wstring arguments[] = {
      launcher_entry,
      L"--invoke-launcher",
      L"--renderer-child-e2e",
      L"",
      L"space value",
      L"quote\"value",
      L"trailing\\",
      L"unicode-\u03a9",
  };
  std::wstring command_line;
  for (const std::wstring &argument : arguments) {
    if (!command_line.empty()) {
      command_line.push_back(L' ');
    }
    AppendQuotedArgument(argument, command_line);
  }
  std::vector<wchar_t> mutable_command_line(command_line.begin(),
                                             command_line.end());
  mutable_command_line.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = static_cast<DWORD>(sizeof(startup));
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = stdin_read;
  startup.hStdOutput = stdout_write;
  startup.hStdError = stderr_write;
  PROCESS_INFORMATION process = {};
  Require(::CreateProcessW(
              launcher_entry.c_str(), mutable_command_line.data(), nullptr,
              nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT, nullptr,
              temporary_directory.c_str(), &startup, &process) != FALSE,
          "launcher subprocess create failed");
  ::CloseHandle(stdin_read);
  ::CloseHandle(stdout_write);
  ::CloseHandle(stderr_write);
  ::CloseHandle(process.hThread);

  const char input[] = "renderer-child-stdin\n";
  written = 0U;
  Require(::WriteFile(stdin_write, input,
                      static_cast<DWORD>(sizeof(input) - 1U), &written,
                      nullptr) != FALSE,
          "standard input write failed");
  ::CloseHandle(stdin_write);
  Require(::WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0,
          "launcher subprocess wait failed");
  DWORD exit_code = 0U;
  Require(::GetExitCodeProcess(process.hProcess, &exit_code) != FALSE,
          "launcher exit query failed");
  ::CloseHandle(process.hProcess);

  const std::string standard_output = ReadHandle(stdout_read);
  const std::string standard_error = ReadHandle(stderr_read);
  ::CloseHandle(stdout_read);
  ::CloseHandle(stderr_read);
  Require(exit_code == 0xc0de0042U,
          "Windows DWORD child exit code was not propagated exactly");
  Require(standard_output == "renderer-child-stdout-ok\n",
          "standard output was not preserved");
  Require(standard_error == "renderer-child-stderr-ok\n",
          "standard error was not preserved");

  if (launcher_alias_created != FALSE) {
    Require(::DeleteFileW(launcher_alias.c_str()) != FALSE,
            "launcher alias delete failed");
  }
  Require(::DeleteFileW(decoy.c_str()) != FALSE, "decoy delete failed");
  Require(::RemoveDirectoryW(temporary_directory.c_str()) != FALSE,
          "temporary directory delete failed");
}

#else

std::string ReadDescriptor(int descriptor) {
  std::string output;
  char buffer[256] = {};
  for (;;) {
    const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
    if (count == 0) {
      break;
    }
    Require(count > 0, "pipe read failed");
    output.append(buffer, static_cast<std::size_t>(count));
  }
  return output;
}

void TestPosixExecFailureRestoresCloseOnExec(const char *program_path) {
  char *canonical_executable = ::realpath(program_path, nullptr);
  Require(canonical_executable != nullptr,
          "test executable canonicalization failed");
  std::string legacy_child(canonical_executable);
  std::free(canonical_executable);
  const std::string::size_type separator = legacy_child.find_last_of('/');
  Require(separator != std::string::npos,
          "test executable had no parent directory");
  legacy_child.erase(separator + 1U);
  legacy_child.append("RoR-Ogre14");
  errno = 0;
  Require(::access(legacy_child.c_str(), F_OK) != 0 && errno == ENOENT,
          "test-only output unexpectedly contained a legacy child");

  struct DescriptorFlags {
    int descriptor;
    int original;
    int expected_during_launch;
    int observed_after_failure;
    bool valid;
  };
  DescriptorFlags descriptors[] = {
      {STDIN_FILENO, 0, 0, 0, false},
      {STDOUT_FILENO, 0, 0, 0, false},
      {STDERR_FILENO, 0, 0, 0, false},
  };
  unsigned int valid_descriptors = 0U;
  for (DescriptorFlags &descriptor : descriptors) {
    descriptor.original = ::fcntl(descriptor.descriptor, F_GETFD);
    if (descriptor.original < 0) {
      Require(errno == EBADF,
              "standard descriptor flags could not be inspected");
      continue;
    }
    descriptor.valid = true;
    ++valid_descriptors;
    descriptor.expected_during_launch = descriptor.original | FD_CLOEXEC;
    Require(::fcntl(descriptor.descriptor, F_SETFD,
                    descriptor.expected_during_launch) == 0,
            "could not set close-on-exec for failure-path test");
  }
  Require(valid_descriptors != 0U,
          "failure-path test had no standard descriptors");

  const char *arguments[] = {program_path};
  const auto legacy_handoff = MakeAcceptedHandoff(
      CurrentPlatform(), RoR::RendererFrontendPreference::LEGACY_ONLY);
  Require(legacy_handoff.accepted &&
              legacy_handoff.child == RoR::RendererFrontendChild::OGRE14,
          "legacy failure-path handoff was not admitted");
  const auto failure = RoR::LaunchRendererChildAndPropagateExit(
      legacy_handoff, 1, arguments);

  for (DescriptorFlags &descriptor : descriptors) {
    if (!descriptor.valid) {
      continue;
    }
    descriptor.observed_after_failure =
        ::fcntl(descriptor.descriptor, F_GETFD);
    const int restore_result =
        ::fcntl(descriptor.descriptor, F_SETFD, descriptor.original);
    Require(restore_result == 0,
            "could not restore standard descriptor test flags");
  }
  Require(failure.status == RoR::RendererChildLaunchStatus::FAILED_POSIX_EXEC &&
              failure.native_error_code == static_cast<std::uint32_t>(ENOENT),
          "missing exact sibling did not return the execv failure");
  for (const DescriptorFlags &descriptor : descriptors) {
    if (descriptor.valid) {
      Require(descriptor.observed_after_failure ==
                  descriptor.expected_during_launch,
              "execv failure did not restore close-on-exec flags");
    }
  }
}

void TestPosixEndToEnd() {
  char directory_template[] =
      "/tmp/ror-renderer-child-launcher-XXXXXX";
  char *created_directory = ::mkdtemp(directory_template);
  Require(created_directory != nullptr, "temporary directory create failed");
  char *canonical_directory = ::realpath(created_directory, nullptr);
  Require(canonical_directory != nullptr,
          "temporary directory canonicalization failed");
  const std::string temporary_directory(canonical_directory);
  std::free(canonical_directory);
  const std::string decoy = temporary_directory + "/RoR-OgreNext";
  const int decoy_descriptor =
      ::open(decoy.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0700);
  Require(decoy_descriptor >= 0, "decoy create failed");
  const char decoy_script[] = "#!/bin/sh\nexit 99\n";
  Require(::write(decoy_descriptor, decoy_script,
                  sizeof(decoy_script) - 1U) ==
              static_cast<ssize_t>(sizeof(decoy_script) - 1U),
          "decoy write failed");
  Require(::close(decoy_descriptor) == 0, "decoy close failed");
  Require(::chmod(decoy.c_str(), 0700) == 0, "decoy chmod failed");

  Require(::setenv("ROR_RENDERER_CHILD_TEST_TOKEN",
                   "renderer-child-env-ok", 1) == 0,
          "test token environment setup failed");
  Require(::setenv("ROR_RENDERER_CHILD_EXPECTED_CWD",
                   temporary_directory.c_str(),
                   1) == 0,
          "cwd environment setup failed");
  Require(::setenv("ROR_RENDERER_CHILD_PATH", decoy.c_str(), 1) == 0,
          "path override decoy setup failed");
  Require(::setenv("PATH", temporary_directory.c_str(), 1) == 0,
          "PATH decoy setup failed");

  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  Require(::pipe(input_pipe) == 0 && ::pipe(output_pipe) == 0 &&
              ::pipe(error_pipe) == 0,
          "standard stream pipe creation failed");

  const pid_t process = ::fork();
  Require(process >= 0, "fork failed");
  if (process == 0) {
    (void)::close(input_pipe[1]);
    (void)::close(output_pipe[0]);
    (void)::close(error_pipe[0]);
    if (::dup2(input_pipe[0], STDIN_FILENO) < 0 ||
        ::dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
        ::dup2(error_pipe[1], STDERR_FILENO) < 0) {
      ::_exit(110);
    }
    (void)::close(input_pipe[0]);
    (void)::close(output_pipe[1]);
    (void)::close(error_pipe[1]);
    if (::fcntl(STDIN_FILENO, F_SETFD, FD_CLOEXEC) < 0 ||
        ::fcntl(STDOUT_FILENO, F_SETFD, FD_CLOEXEC) < 0 ||
        ::fcntl(STDERR_FILENO, F_SETFD, FD_CLOEXEC) < 0) {
      ::_exit(112);
    }
    if (::chdir(temporary_directory.c_str()) != 0) {
      ::_exit(111);
    }

    const char *forwarded_arguments[] = {
        "argv-zero-must-not-select-a-path",
        "--renderer-child-e2e",
        "",
        "space value",
        "quote\"value",
        "trailing\\",
        "unicode-\xcf\xa9",
    };
    const auto failure = RoR::LaunchRendererChildAndPropagateExit(
        MakeAcceptedHandoff(),
        static_cast<int>(sizeof(forwarded_arguments) /
                         sizeof(forwarded_arguments[0])),
        forwarded_arguments);
    std::cerr << "launcher-returned:" << RoR::ToString(failure.status) << ':'
              << failure.native_error_code << '\n';
    ::_exit(120);
  }

  (void)::close(input_pipe[0]);
  (void)::close(output_pipe[1]);
  (void)::close(error_pipe[1]);
  const char input[] = "renderer-child-stdin\n";
  Require(::write(input_pipe[1], input, sizeof(input) - 1U) ==
              static_cast<ssize_t>(sizeof(input) - 1U),
          "standard input write failed");
  Require(::close(input_pipe[1]) == 0, "standard input close failed");

  int status = 0;
  Require(::waitpid(process, &status, 0) == process,
          "launcher subprocess wait failed");
  const std::string standard_output = ReadDescriptor(output_pipe[0]);
  const std::string standard_error = ReadDescriptor(error_pipe[0]);
  (void)::close(output_pipe[0]);
  (void)::close(error_pipe[0]);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 37) {
    std::cerr << "renderer child launcher subprocess status=" << status
              << " stdout=" << standard_output << " stderr="
              << standard_error << '\n';
  }
  Require(WIFEXITED(status) && WEXITSTATUS(status) == 37,
          "POSIX child exit status was not propagated by execv");
  Require(standard_output == "renderer-child-stdout-ok\n",
          "standard output was not preserved");
  Require(standard_error == "renderer-child-stderr-ok\n",
          "standard error was not preserved");

  Require(::unlink(decoy.c_str()) == 0, "decoy delete failed");
  Require(::rmdir(temporary_directory.c_str()) == 0,
          "temporary directory delete failed");
}

#endif

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t *argv[]) {
  if (argc > 1) {
    const std::wstring mode(argv[1]);
    if (mode == L"--invoke-launcher" ||
        mode == L"--invoke-launcher-null-stdio" ||
        mode == L"--invoke-launcher-invalid-stdio") {
      return InvokeLauncher(argc, argv);
    }
  }
  TestContractAndFailures();
  TestWindowsEndToEnd();
  TestWindowsAbsentStandardHandles();
  return EXIT_SUCCESS;
}
#else
int main(int argc, char *argv[]) {
  Require(argc >= 1 && argv != nullptr && argv[0] != nullptr,
          "test process argv was unavailable");
  TestContractAndFailures();
  TestPosixExecFailureRestoresCloseOnExec(argv[0]);
  TestPosixEndToEnd();
  return EXIT_SUCCESS;
}
#endif
