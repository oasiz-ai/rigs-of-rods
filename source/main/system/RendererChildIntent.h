/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free, versioned launcher-to-renderer intent protocol.

#pragma once

#include "RendererStartupHandoff.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RoR {

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

} // namespace RoR
