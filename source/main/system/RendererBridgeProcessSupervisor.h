/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact-sibling two-process render-bridge lifecycle supervisor.

#pragma once

#include "RendererBridgeLaunchPlan.h"
#include "RendererSiblingPath.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererBridgeProcessSupervisorContractVersion = 2U;

enum class RendererBridgeObservedChild : std::uint8_t {
  NONE = 0U,
  GAME_HOST,
  PRESENTATION_FRONTEND,
};

enum class RendererBridgeGameExitKind : std::uint8_t {
  UNAVAILABLE = 0U,
  EXIT_CODE,
  TERMINATION_SIGNAL,
};

enum class RendererBridgeProcessStatus : std::uint8_t {
  COMPLETED_GAME_EXIT = 0U,
  PRESENTATION_EXITED_FIRST,
  REJECTED_INVALID_HANDOFF,
  REJECTED_INVALID_SESSION,
  REJECTED_INVALID_ARGUMENTS,
  REJECTED_LAUNCH_PLAN,
  FAILED_GAME_CHILD_PATH,
  FAILED_PRESENTATION_CHILD_PATH,
  FAILED_STREAM_CREATE,
  FAILED_NATIVE_HANDLE_SET,
  FAILED_POSIX_GAME_FORK,
  FAILED_POSIX_PRESENTATION_FORK,
  FAILED_POSIX_DESCRIPTOR_LIMIT,
  FAILED_POSIX_PROCESS_GROUP,
  FAILED_POSIX_STARTUP_GATE,
  FAILED_GAME_EXEC,
  FAILED_PRESENTATION_EXEC,
  FAILED_WINDOWS_COMMAND_LINE,
  FAILED_WINDOWS_JOB_CREATE,
  FAILED_WINDOWS_JOB_CONFIGURE,
  FAILED_WINDOWS_ATTRIBUTE_LIST,
  FAILED_WINDOWS_GAME_PROCESS_CREATE,
  FAILED_WINDOWS_PRESENTATION_PROCESS_CREATE,
  FAILED_WINDOWS_GAME_JOB_ASSIGN,
  FAILED_WINDOWS_PRESENTATION_JOB_ASSIGN,
  FAILED_WINDOWS_PRESENTATION_THREAD_RESUME,
  FAILED_WINDOWS_GAME_THREAD_RESUME,
  FAILED_WAIT,
  FAILED_EXIT_QUERY,
  FAILED_PEER_TERMINATION,
  FAILED_INTERNAL,
};

/// Complete, platform-neutral observation of one supervision transaction.
/// `completed` is true only after the game host exited naturally, its exact
/// exit semantics were captured, and the presentation peer was terminated (if
/// still alive) and reaped. A presentation-first result deliberately cannot be
/// propagated as a game exit because the game outcome was supervisor-induced.
struct RendererBridgeProcessResult final {
  std::uint32_t version =
      kRendererBridgeProcessSupervisorContractVersion;
  RendererBridgeProcessStatus status =
      RendererBridgeProcessStatus::REJECTED_INVALID_HANDOFF;
  RendererBridgeLaunchPlanStatus launch_plan_status =
      RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF;
  RendererSiblingPathStatus sibling_path_status =
      RendererSiblingPathStatus::REJECTED_INVALID_BASENAME;
  RendererBridgeObservedChild failed_child =
      RendererBridgeObservedChild::NONE;
  RendererBridgeObservedChild first_exit =
      RendererBridgeObservedChild::NONE;
  RendererBridgeGameExitKind game_exit_kind =
      RendererBridgeGameExitKind::UNAVAILABLE;
  /// Natural presentation-child exit captured when the supervisor observes
  /// that child before any supervisor-induced termination. A same-observation
  /// tie may still resolve `first_exit` to GAME_HOST so its natural outcome
  /// can be propagated. The existing exit-kind domain is process-generic even
  /// though its historical type name refers to the game child.
  RendererBridgeGameExitKind presentation_exit_kind =
      RendererBridgeGameExitKind::UNAVAILABLE;
  std::uint32_t native_error_code = 0U;
  std::uint32_t game_exit_code = 0U;
  std::uint32_t game_termination_signal = 0U;
  std::uint32_t native_game_wait_status = 0U;
  std::uint32_t presentation_exit_code = 0U;
  std::uint32_t presentation_termination_signal = 0U;
  std::uint32_t native_presentation_wait_status = 0U;
  bool game_exec_confirmed = false;
  bool presentation_exec_confirmed = false;
  bool game_reaped = false;
  bool presentation_reaped = false;
  /// True only when the peer's observed exit semantics prove the supervisor's
  /// platform termination operation ended it; an attempted signal is not
  /// sufficient.
  bool peer_terminated = false;
  bool completed = false;
};

/// Launch the exact canonical `RoR-Ogre14` game host and admitted
/// `RoR-OgreNext` presentation sibling with one pipe in each direction.
///
/// The caller supplies a nonzero session identifier and the original game
/// argv. It cannot override either executable path, cwd, PATH, environment,
/// stream handles, process group, or job. The process owns and closes all
/// bridge endpoints after the children inherit their exact two-handle
/// allow-lists. Existing SceneSnapshotTransport and RenderAssetDeltaTransport
/// frames may be interleaved on the game-to-presentation byte stream; the
/// reverse byte stream is reserved for frontend input/control transport.
///
/// This function never changes renderer package-admission facts and does not
/// terminate the caller. Use PropagateRendererBridgeGameExit() only after a
/// COMPLETED_GAME_EXIT result when the public launcher is ready to adopt this
/// supervisor.
RendererBridgeProcessResult SuperviseRendererBridgeProcesses(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept;

/// Terminate the current process with the exact completed game-host semantics:
/// DWORD exit code on Windows, or exit code / original terminating signal on
/// POSIX. Invalid/incomplete results fail closed with a reserved failure code.
[[noreturn]] void PropagateRendererBridgeGameExit(
    const RendererBridgeProcessResult &result) noexcept;

bool IsKnownRendererBridgeObservedChild(
    RendererBridgeObservedChild child) noexcept;
bool IsKnownRendererBridgeGameExitKind(
    RendererBridgeGameExitKind kind) noexcept;
bool IsKnownRendererBridgeProcessStatus(
    RendererBridgeProcessStatus status) noexcept;
const char *ToString(RendererBridgeObservedChild child) noexcept;
const char *ToString(RendererBridgeGameExitKind kind) noexcept;
const char *ToString(RendererBridgeProcessStatus status) noexcept;

} // namespace RoR
