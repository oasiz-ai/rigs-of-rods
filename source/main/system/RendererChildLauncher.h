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
#include <string>
#include <vector>

namespace RoR {

/// This process boundary is intentionally versioned independently from the
/// renderer-selection handoff. Unknown versions and malformed handoffs fail
/// before any child process is created.
constexpr std::uint32_t kRendererChildLauncherContractVersion = 1U;

/// Exact launcher-to-Ogre-Next argv contract. The selected child must decode
/// this prefix and independently resolve RendererStartupPlan before creating
/// Ogre::Root. Environment variables and mutable files are never inputs.
constexpr std::uint32_t kRendererOgreNextChildIntentArgvContractVersion = 1U;

#if defined(_WIN32)
using RendererChildLauncherChar = wchar_t;
#else
using RendererChildLauncherChar = char;
#endif

using RendererChildLauncherString =
    std::basic_string<RendererChildLauncherChar>;

enum class RendererOgreNextChildIntentArgvStatus : std::uint8_t {
  READY = 0,
  REJECTED_INVALID_HANDOFF = 1,
  REJECTED_INVALID_ARGUMENTS = 2,
  REJECTED_INVALID_PLATFORM = 3,
  REJECTED_MISSING_CONTRACT = 4,
  REJECTED_MALFORMED_CONTRACT = 5,
  FAILED_INTERNAL = 6,
};

/// Fully owned argv produced for an admitted RoR-OgreNext child. argv[0] is
/// retained as a placeholder for the exact sibling launcher, followed by the
/// four launcher-owned intent records and then the unmodified game suffix.
struct RendererOgreNextChildIntentEncoding {
  std::uint32_t version =
      kRendererOgreNextChildIntentArgvContractVersion;
  RendererOgreNextChildIntentArgvStatus status =
      RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_HANDOFF;
  RendererStartupRequest startup;
  NativeRayTracingBackend declared_native_backend =
      NativeRayTracingBackend::NONE;
  std::vector<RendererChildLauncherString> arguments;
  bool accepted = false;
};

/// Fully owned argv returned inside RoR-OgreNext after its exact prefix has
/// been removed. The startup request is complete and bound to the compile-time
/// child host; the remaining arguments preserve argv[0] and the game suffix
/// exactly without depending on the native entrypoint's argv lifetime.
struct RendererOgreNextChildIntentParseResult {
  std::uint32_t version =
      kRendererOgreNextChildIntentArgvContractVersion;
  RendererOgreNextChildIntentArgvStatus status =
      RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_ARGUMENTS;
  RendererStartupRequest startup;
  NativeRayTracingBackend declared_native_backend =
      NativeRayTracingBackend::NONE;
  std::vector<RendererChildLauncherString> forwarded_arguments;
  bool accepted = false;
};

/// Encode the immutable accepted Ogre-Next handoff as an exact native argv
/// prefix. The input argv is the public launcher's already-filtered game argv.
RendererOgreNextChildIntentEncoding EncodeRendererOgreNextChildIntent(
    const RendererStartupHandoffResult &handoff, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept;

/// Decode only the exact four-record prefix emitted above. Unknown versions,
/// reordered fields, legacy frontend values, malformed values, null argv
/// entries, reserved duplicate records, and unsupported compile-time hosts fail
/// closed before renderer initialization. This argv protocol conveys immutable
/// intent; it is not an authentication boundary for local process execution.
RendererOgreNextChildIntentParseResult ParseRendererOgreNextChildIntent(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept;

bool IsKnownRendererOgreNextChildIntentArgvStatus(
    RendererOgreNextChildIntentArgvStatus status) noexcept;
const char *ToString(RendererOgreNextChildIntentArgvStatus status) noexcept;

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
