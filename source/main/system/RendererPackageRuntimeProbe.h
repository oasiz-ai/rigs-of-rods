/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed executable-relative renderer package preflight.

#pragma once

#include "RendererPackagedMediaPath.h"
#include "RendererStartupHandoff.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererPackageRuntimeProbeContractVersion = 1U;

enum class RendererPackageRuntimeArtifactStatus : std::uint8_t {
  NOT_REQUIRED = 0U,
  READY,
  MISSING,
  REJECTED_LINK_OR_REPARSE_POINT,
  REJECTED_WRONG_TYPE,
  REJECTED_NOT_EXECUTABLE,
  FAILED_INSPECTION,
};

enum class RendererPackageRuntimeProbeStatus : std::uint8_t {
  READY = 0U,
  READY_OGRE14_FALLBACK,
  REJECTED_INVALID_DECLARATION,
  REJECTED_INVALID_OBSERVATION,
  REJECTED_PLATFORM_MISMATCH,
  FAILED_CURRENT_EXECUTABLE_PATH,
  FAILED_PACKAGE_LAYOUT,
  REJECTED_OGRE14_CHILD,
  FAILED_INTERNAL,
};

/// Filesystem evidence is separated from policy so every degrade/reject rule
/// can be exhaustively tested without introducing a mutable package-fact
/// channel. Production observations are created only from exact siblings of
/// the final opened public launcher path.
struct RendererPackageRuntimeObservation final {
  std::uint32_t version = kRendererPackageRuntimeProbeContractVersion;
  HostRenderPlatform package_platform = HostRenderPlatform::UNKNOWN;
  RendererPackageRuntimeArtifactStatus ogre14_child =
      RendererPackageRuntimeArtifactStatus::NOT_REQUIRED;
  RendererPackageRuntimeArtifactStatus ogre_next_child =
      RendererPackageRuntimeArtifactStatus::NOT_REQUIRED;
  RendererPackageRuntimeArtifactStatus ogre_next_shader_media =
      RendererPackageRuntimeArtifactStatus::NOT_REQUIRED;
  RendererPackageRuntimeArtifactStatus ogre_next_presentation_media =
      RendererPackageRuntimeArtifactStatus::NOT_REQUIRED;
};

struct RendererPackageRuntimeProbeResult final {
  std::uint32_t version = kRendererPackageRuntimeProbeContractVersion;
  RendererPackageRuntimeProbeStatus status =
      RendererPackageRuntimeProbeStatus::REJECTED_INVALID_DECLARATION;
  RendererStartupPackageAvailability declared_availability;
  RendererStartupPackageAvailability effective_availability;
  RendererPackageRuntimeObservation observation;
  RendererSiblingPathStatus sibling_path_status =
      RendererSiblingPathStatus::REJECTED_INVALID_BASENAME;
  RendererPackagedMediaPathStatus media_path_status =
      RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM;
  std::uint32_t native_error_code = 0U;
  bool accepted = false;
  bool ogre_next_runtime_degraded = false;
};

/// Pure policy seam. A missing/unusable declared Ogre-Next artifact clears all
/// Ogre-Next admission facts, allowing OGRE_NEXT_PREFER to fall back through
/// the existing handoff policy. The exact OGRE 14 game host is mandatory for
/// both frontends and therefore fails closed.
RendererPackageRuntimeProbeResult ResolveRendererPackageRuntimeObservation(
    const RendererStartupPackageAvailability &declared_availability,
    const RendererPackageRuntimeObservation &observation) noexcept;

/// Testable filesystem seam. The supplied path must already be the canonical
/// final public-launcher executable path; it is never taken from argv, cwd,
/// PATH, an environment variable, or a config file.
RendererPackageRuntimeProbeResult
ProbeRendererPackageRuntimeAvailabilityFromExecutable(
    const RendererStartupPackageAvailability &declared_availability,
    const RendererChildLauncherString &canonical_public_executable_path)
    noexcept;

/// Production entrypoint anchored to the final opened executable path.
RendererPackageRuntimeProbeResult ProbeRendererPackageRuntimeAvailability(
    const RendererStartupPackageAvailability &declared_availability) noexcept;

bool IsKnownRendererPackageRuntimeArtifactStatus(
    RendererPackageRuntimeArtifactStatus status) noexcept;
bool IsKnownRendererPackageRuntimeProbeStatus(
    RendererPackageRuntimeProbeStatus status) noexcept;
const char *ToString(RendererPackageRuntimeArtifactStatus status) noexcept;
const char *ToString(RendererPackageRuntimeProbeStatus status) noexcept;

} // namespace RoR
