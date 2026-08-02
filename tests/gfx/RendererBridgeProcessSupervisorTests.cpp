/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeProcessSupervisor.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

namespace {

using NativeString = RoR::RendererChildLauncherString;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer bridge process supervisor test failed: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() noexcept {
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

RoR::RendererStartupHandoffResult MakeAdmittedHandoff() {
  RoR::RendererStartupHandoffRequest request;
  request.startup.frontend =
      RoR::RendererFrontendPreference::OGRE_NEXT_PREFER;
  request.startup.directional_shadows =
      RoR::DirectionalShadowPreference::PSSM;
  request.startup.host_platform = CurrentPlatform();
  RoR::RendererStartupPackageAvailability package;
  package.package_platform = CurrentPlatform();
  package.ogre14_child_present = true;
  package.ogre_next_child_present = true;
  package.ogre_next_child_production_ready = true;
  package.ogre_next_pssm_admitted = true;
  return RoR::ResolveRendererStartupHandoff(request, package);
}

RoR::RendererBridgeSessionId Session() noexcept {
  RoR::RendererBridgeSessionId session{};
  for (std::size_t index = 0U; index < session.size(); ++index) {
    session[index] = static_cast<std::uint8_t>(0xa0U + index);
  }
  return session;
}

std::vector<const RoR::RendererChildLauncherChar *>
Pointers(const std::vector<NativeString> &arguments) {
  std::vector<const RoR::RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const NativeString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

RoR::RendererBridgeProcessResult Run(
    const std::vector<NativeString> &arguments) {
  const auto pointers = Pointers(arguments);
  return RoR::SuperviseRendererBridgeProcesses(
      MakeAdmittedHandoff(), Session(), static_cast<int>(pointers.size()),
      pointers.data());
}

#if !defined(_WIN32)
void RequireNoChildProcesses(const char *message) {
  int status = 0;
  errno = 0;
  const pid_t child = ::waitpid(-1, &status, WNOHANG);
  Require(child == -1 && errno == ECHILD, message);
}
#endif

void TestStatusDomains() {
  for (unsigned int value = 0U;
       value <= static_cast<unsigned int>(
                    RoR::RendererBridgeProcessStatus::FAILED_INTERNAL);
       ++value) {
    const auto status =
        static_cast<RoR::RendererBridgeProcessStatus>(value);
    Require(RoR::IsKnownRendererBridgeProcessStatus(status) &&
                std::strcmp(RoR::ToString(status), "invalid") != 0,
            "known supervisor status was omitted");
  }
  Require(!RoR::IsKnownRendererBridgeProcessStatus(
              static_cast<RoR::RendererBridgeProcessStatus>(255U)) &&
              std::strcmp(
                  RoR::ToString(static_cast<
                                RoR::RendererBridgeProcessStatus>(255U)),
                  "invalid") == 0,
          "unknown supervisor status was accepted");
  for (unsigned int value = 0U; value <= 2U; ++value) {
    const auto child =
        static_cast<RoR::RendererBridgeObservedChild>(value);
    const auto kind = static_cast<RoR::RendererBridgeGameExitKind>(value);
    Require(RoR::IsKnownRendererBridgeObservedChild(child) &&
                RoR::IsKnownRendererBridgeGameExitKind(kind) &&
                std::strcmp(RoR::ToString(child), "invalid") != 0 &&
                std::strcmp(RoR::ToString(kind), "invalid") != 0,
            "known child/exit domain was omitted");
  }
  Require(!RoR::IsKnownRendererBridgeObservedChild(
              static_cast<RoR::RendererBridgeObservedChild>(255U)) &&
              !RoR::IsKnownRendererBridgeGameExitKind(
                  static_cast<RoR::RendererBridgeGameExitKind>(255U)),
          "unknown child/exit domain was accepted");
}

void TestPreflightFailuresCreateNoChildren() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("untrusted-launcher")};
  RoR::RendererStartupHandoffResult handoff = MakeAdmittedHandoff();
  handoff.accepted = false;
  RoR::RendererBridgeProcessResult result =
      RoR::SuperviseRendererBridgeProcesses(handoff, Session(), 1, arguments);
  Require(!result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::REJECTED_INVALID_HANDOFF &&
              !result.game_exec_confirmed &&
              !result.presentation_exec_confirmed,
          "rejected handoff reached process creation");

  RoR::RendererBridgeSessionId zero{};
  result = RoR::SuperviseRendererBridgeProcesses(
      MakeAdmittedHandoff(), zero, 1, arguments);
  Require(!result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::REJECTED_INVALID_SESSION,
          "zero session reached process creation");

  result = RoR::SuperviseRendererBridgeProcesses(
      MakeAdmittedHandoff(), Session(), 0, nullptr);
  Require(!result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::REJECTED_INVALID_ARGUMENTS,
          "invalid argv reached process creation");

  const RoR::RendererChildLauncherChar *reserved[] = {
      ROR_NATIVE_TEXT("untrusted-launcher"),
      ROR_NATIVE_TEXT("--ror-render-bridge-role=duplicate")};
  result = RoR::SuperviseRendererBridgeProcesses(
      MakeAdmittedHandoff(), Session(), 2, reserved);
  Require(!result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::REJECTED_LAUNCH_PLAN &&
              result.launch_plan_status ==
                  RoR::RendererBridgeLaunchPlanStatus::
                      REJECTED_ENDPOINT_ENCODING,
          "reserved bridge option entered process creation");
#if !defined(_WIN32)
  RequireNoChildProcesses("preflight failure left a child process");
#endif
}

#if !defined(_WIN32)
class PosixIsolation final {
public:
  PosixIsolation() {
    char *cwd = ::getcwd(nullptr, 0U);
    Require(cwd != nullptr, "could not capture current directory");
    original_cwd_.assign(cwd);
    std::free(cwd);
    const char *path = std::getenv("PATH");
    had_path_ = path != nullptr;
    if (path != nullptr) {
      original_path_.assign(path);
    }
    char pattern[] = "/tmp/ror-render-bridge-cwd.XXXXXX";
    char *created = ::mkdtemp(pattern);
    Require(created != nullptr, "could not create isolated cwd");
    isolated_cwd_.assign(created);
    Require(::chdir(isolated_cwd_.c_str()) == 0,
            "could not enter isolated cwd");
    Require(::setenv("PATH", "/definitely/not/a/renderer/path", 1) == 0,
            "could not isolate PATH");
  }

  ~PosixIsolation() {
    (void)::chdir(original_cwd_.c_str());
    if (had_path_) {
      (void)::setenv("PATH", original_path_.c_str(), 1);
    } else {
      (void)::unsetenv("PATH");
    }
    (void)::rmdir(isolated_cwd_.c_str());
  }

  PosixIsolation(const PosixIsolation &) = delete;
  PosixIsolation &operator=(const PosixIsolation &) = delete;

private:
  std::string original_cwd_;
  std::string original_path_;
  std::string isolated_cwd_;
  bool had_path_ = false;
};
#endif

void TestExactSiblingBridgeAndGameExit() {
  const std::vector<NativeString> arguments{
      ROR_NATIVE_TEXT("untrusted-launcher"),
      ROR_NATIVE_TEXT("--bridge-test-game-exit=37"),
      ROR_NATIVE_TEXT("space and unicode \u03a9")};
#if !defined(_WIN32)
  PosixIsolation isolation;
#endif
  const RoR::RendererBridgeProcessResult result = Run(arguments);
  Require(result.version ==
                  RoR::kRendererBridgeProcessSupervisorContractVersion &&
              result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::COMPLETED_GAME_EXIT &&
              result.launch_plan_status ==
                  RoR::RendererBridgeLaunchPlanStatus::READY &&
              result.sibling_path_status ==
                  RoR::RendererSiblingPathStatus::READY &&
              result.first_exit ==
                  RoR::RendererBridgeObservedChild::GAME_HOST &&
              result.game_exit_kind ==
                  RoR::RendererBridgeGameExitKind::EXIT_CODE &&
              result.game_exit_code == 37U &&
              result.game_exec_confirmed &&
              result.presentation_exec_confirmed && result.game_reaped &&
              result.presentation_reaped && result.peer_terminated,
          "exact sibling bridge did not preserve game exit 37");
#if !defined(_WIN32)
  int native_wait_status =
      static_cast<int>(result.native_game_wait_status);
  Require(WIFEXITED(native_wait_status) &&
              WEXITSTATUS(native_wait_status) == 37,
          "raw POSIX game wait status changed");
  RequireNoChildProcesses("successful supervision left a zombie");
#endif
}

void TestPresentationFirstTerminatesGame() {
  const std::vector<NativeString> arguments{
      ROR_NATIVE_TEXT("untrusted-launcher"),
      ROR_NATIVE_TEXT("--bridge-test-presentation-first")};
  const RoR::RendererBridgeProcessResult result = Run(arguments);
  Require(!result.completed &&
              result.status == RoR::RendererBridgeProcessStatus::
                                   PRESENTATION_EXITED_FIRST &&
              result.first_exit ==
                  RoR::RendererBridgeObservedChild::PRESENTATION_FRONTEND &&
              result.game_exit_kind ==
                  RoR::RendererBridgeGameExitKind::UNAVAILABLE &&
              result.game_exec_confirmed &&
              result.presentation_exec_confirmed && result.game_reaped &&
              result.presentation_reaped && result.peer_terminated,
          "presentation-first exit did not terminate and reap game host");
#if !defined(_WIN32)
  RequireNoChildProcesses("presentation-first supervision left a zombie");
#endif
}

#if !defined(_WIN32)
void TestSignalExitAndPropagation() {
  const std::vector<NativeString> signal_arguments{
      ROR_NATIVE_TEXT("untrusted-launcher"),
      ROR_NATIVE_TEXT("--bridge-test-game-signal=6")};
  const RoR::RendererBridgeProcessResult result = Run(signal_arguments);
  int native_wait_status =
      static_cast<int>(result.native_game_wait_status);
  Require(result.completed &&
              result.status ==
                  RoR::RendererBridgeProcessStatus::COMPLETED_GAME_EXIT &&
              result.game_exit_kind ==
                  RoR::RendererBridgeGameExitKind::TERMINATION_SIGNAL &&
              result.game_termination_signal ==
                  static_cast<std::uint32_t>(SIGABRT) &&
              WIFSIGNALED(native_wait_status) &&
              WTERMSIG(native_wait_status) == SIGABRT,
          "game terminating signal was not captured exactly");
  RequireNoChildProcesses("signal supervision left a zombie");

  const pid_t exit_wrapper = ::fork();
  Require(exit_wrapper >= 0, "could not fork exit propagation wrapper");
  if (exit_wrapper == 0) {
    const std::vector<NativeString> arguments{
        ROR_NATIVE_TEXT("untrusted-launcher"),
        ROR_NATIVE_TEXT("--bridge-test-game-exit=42")};
    RoR::PropagateRendererBridgeGameExit(Run(arguments));
  }
  int wrapper_status = 0;
  Require(::waitpid(exit_wrapper, &wrapper_status, 0) == exit_wrapper &&
              WIFEXITED(wrapper_status) && WEXITSTATUS(wrapper_status) == 42,
          "exit-code propagation changed the game exit");

  const pid_t signal_wrapper = ::fork();
  Require(signal_wrapper >= 0, "could not fork signal propagation wrapper");
  if (signal_wrapper == 0) {
    RoR::PropagateRendererBridgeGameExit(Run(signal_arguments));
  }
  wrapper_status = 0;
  Require(::waitpid(signal_wrapper, &wrapper_status, 0) == signal_wrapper &&
              WIFSIGNALED(wrapper_status) &&
              WTERMSIG(wrapper_status) == SIGABRT,
          "signal propagation changed the game terminating signal");
  RequireNoChildProcesses("propagation wrappers left a zombie");
}
#endif

bool RenameNative(const NativeString &from, const NativeString &to) {
#if defined(_WIN32)
  return ::_wrename(from.c_str(), to.c_str()) == 0;
#else
  return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

void TestPartialStartupFailsClosed() {
  const RoR::RendererSiblingPathResult presentation =
      RoR::ResolveRendererSiblingPath(
#if defined(_WIN32)
          "RoR-OgreNext.exe"
#else
          "RoR-OgreNext"
#endif
      );
  Require(presentation.accepted, "could not resolve fake presentation child");
  NativeString hidden = presentation.path;
  hidden.append(ROR_NATIVE_TEXT(".bridge-test-hidden"));
  Require(RenameNative(presentation.path, hidden),
          "could not hide fake presentation child");
  const std::vector<NativeString> arguments{
      ROR_NATIVE_TEXT("untrusted-launcher"),
      ROR_NATIVE_TEXT("--bridge-test-game-exit=11")};
  const RoR::RendererBridgeProcessResult result = Run(arguments);
  const bool restored = RenameNative(hidden, presentation.path);
  Require(restored, "could not restore fake presentation child");
#if defined(_WIN32)
  const bool exact_failure =
      result.status == RoR::RendererBridgeProcessStatus::
                           FAILED_WINDOWS_PRESENTATION_PROCESS_CREATE;
  const bool exact_cleanup = result.game_reaped &&
                             !result.presentation_reaped;
#else
  const bool exact_failure =
      result.status ==
      RoR::RendererBridgeProcessStatus::FAILED_PRESENTATION_EXEC;
  const bool exact_cleanup =
      result.game_reaped && result.presentation_reaped;
#endif
  Require(!result.completed && exact_failure &&
              result.failed_child ==
                  RoR::RendererBridgeObservedChild::PRESENTATION_FRONTEND &&
              exact_cleanup,
          "partial startup did not fail closed and reap both children");
#if !defined(_WIN32)
  Require(result.native_error_code == static_cast<std::uint32_t>(ENOENT),
          "missing presentation exec did not preserve ENOENT");
  RequireNoChildProcesses("partial startup left a zombie");
#endif
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestStatusDomains();
  TestPreflightFailuresCreateNoChildren();
  TestExactSiblingBridgeAndGameExit();
  TestPresentationFirstTerminatesGame();
#if !defined(_WIN32)
  TestSignalExitAndPropagation();
#endif
  TestPartialStartupFailsClosed();
  return EXIT_SUCCESS;
}
