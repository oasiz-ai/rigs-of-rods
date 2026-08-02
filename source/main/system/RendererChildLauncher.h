/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free, exact-sibling renderer child process launcher.

#pragma once

#include "RendererStartupHandoff.h"

#include <cstdint>

namespace RoR {

/// This process boundary is intentionally versioned independently from the
/// renderer-selection handoff. Unknown versions and malformed handoffs fail
/// before any child process is created.
constexpr std::uint32_t kRendererChildLauncherContractVersion = 1U;

#if defined(_WIN32)
using RendererChildLauncherChar = wchar_t;
#else
using RendererChildLauncherChar = char;
#endif

enum class RendererChildLaunchStatus : std::uint8_t {
  REJECTED_INVALID_HANDOFF = 0,
  REJECTED_INVALID_ARGUMENTS = 1,
  FAILED_CURRENT_EXECUTABLE_PATH = 2,
  FAILED_CHILD_PATH = 3,
  FAILED_POSIX_STANDARD_HANDLE_PREPARE = 4,
  FAILED_POSIX_EXEC = 5,
  FAILED_WINDOWS_COMMAND_LINE = 6,
  FAILED_WINDOWS_JOB_CREATE = 7,
  FAILED_WINDOWS_JOB_CONFIGURE = 8,
  FAILED_WINDOWS_STANDARD_HANDLE_DUPLICATION = 9,
  FAILED_WINDOWS_ATTRIBUTE_LIST = 10,
  FAILED_WINDOWS_PROCESS_CREATE = 11,
  FAILED_WINDOWS_JOB_ASSIGN = 12,
  FAILED_WINDOWS_THREAD_RESUME = 13,
  FAILED_WINDOWS_WAIT = 14,
  FAILED_WINDOWS_EXIT_QUERY = 15,
  FAILED_INTERNAL = 16,
};

/// Returned only when the launcher could not transfer control to the selected
/// child. A successful POSIX launch replaces this process with execv. A
/// successful Windows launch terminates this process with the child's exact
/// DWORD exit code after the child exits.
struct RendererChildLaunchFailure {
  std::uint32_t version = kRendererChildLauncherContractVersion;
  RendererChildLaunchStatus status =
      RendererChildLaunchStatus::REJECTED_INVALID_HANDOFF;
  std::uint32_t native_error_code = 0U;
};

bool IsKnownRendererChildLaunchStatus(
    RendererChildLaunchStatus status) noexcept;

/// Launch the renderer child named by an accepted handoff.
///
/// There is deliberately no path, working-directory, environment, or standard
/// handle override in this API. The child is resolved only as an exact sibling
/// of the running launcher, and the handoff's trusted package platform must
/// equal the compile-time host platform before even a basename is derived.
/// `argv[1..argc)` is forwarded byte-for-byte on POSIX and
/// code-unit-for-code-unit on Windows; argv[0] becomes that exact child path.
/// The current working directory, environment, and standard streams are
/// inherited unchanged.
/// This core does not parse or silently remove launcher-only options. A public
/// launcher which owns such options must construct the final forwarded argv
/// explicitly before calling this function.
///
/// POSIX uses execv, so successful execution never returns and the child is
/// the original process. Windows uses CreateProcessW with the exact
/// lpApplicationName derived from the executable's final opened-handle path,
/// preserving the validated `\\?\` DOS prefix for exact long-path semantics.
/// It uses a kill-on-close Job Object and ExitProcess with the child's exit
/// code. Valid Windows standard handles are duplicated into an explicit
/// allow-list; explicit NULL or INVALID_HANDLE_VALUE standard handles remain
/// absent through STARTF_USESTDHANDLES and a private unnamed sentinel used only
/// to keep the inheritance allow-list nonempty. Job
/// creation/configuration/assignment is mandatory and fails closed rather than
/// launching an uncontained child.
/// Failures return a structured status and native errno or GetLastError value.
RendererChildLaunchFailure LaunchRendererChildAndPropagateExit(
    const RendererStartupHandoffResult &handoff, int argc,
    const RendererChildLauncherChar *const argv[]);

const char *ToString(RendererChildLaunchStatus status) noexcept;

} // namespace RoR
