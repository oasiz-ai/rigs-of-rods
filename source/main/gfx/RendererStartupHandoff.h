/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free launcher-to-renderer-child handoff planning.

#pragma once

#include "RendererStartupPlan.h"

#include <cstdint>

namespace RoR {

/// This contract is package-facing and deliberately separate from the
/// process-local startup plan. Unknown versions must fail closed.
constexpr std::uint32_t kRendererStartupHandoffContractVersion = 1U;

enum class RendererFrontendChild : std::uint8_t {
  NONE = 0,
  OGRE14 = 1,
  OGRE_NEXT = 2,
};

enum class RendererStartupHandoffStatus : std::uint8_t {
  SELECTED_REQUESTED_CHILD = 0,
  FALLBACK_TO_OGRE14_CHILD = 1,
  REJECTED_INVALID_REQUEST = 2,
  REJECTED_REQUIRED_LEGACY_CHILD_UNAVAILABLE = 3,
  REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE = 4,
  REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY = 5,
  REJECTED_NATIVE_REQUIRED_UNAVAILABLE = 6,
  REJECTED_NO_VIABLE_CHILD = 7,
};

/// Trusted package facts supplied by generated launcher/package metadata.
/// `ogre_next_child_production_ready` is an admission result, not a build or
/// file-presence observation: it may be true only for a real game child which
/// passed the content, script, UI, visual, and package gates for this exact
/// artifact. Probe executables such as N1 are never eligible to set it.
struct RendererStartupPackageAvailability {
  std::uint32_t version = kRendererStartupHandoffContractVersion;
  HostRenderPlatform package_platform = HostRenderPlatform::UNKNOWN;
  bool ogre14_child_present = false;
  bool ogre_next_child_present = false;
  bool ogre_next_child_production_ready = false;
  bool ogre_next_pssm_admitted = false;
  NativeRayTracingBackend native_directional_shadow_backend =
      NativeRayTracingBackend::NONE;
};

struct RendererStartupHandoffRequest {
  std::uint32_t version = kRendererStartupHandoffContractVersion;
  RendererStartupRequest startup;
};

struct RendererStartupHandoffResult {
  std::uint32_t version = kRendererStartupHandoffContractVersion;
  RendererFrontendPreference requested_frontend =
      RendererFrontendPreference::LEGACY_ONLY;
  DirectionalShadowPreference requested_directional_shadows =
      DirectionalShadowPreference::PSSM;
  RendererFrontendChild child = RendererFrontendChild::NONE;
  HostRenderPlatform package_platform = HostRenderPlatform::UNKNOWN;
  NativeRayTracingBackend declared_native_backend =
      NativeRayTracingBackend::NONE;
  RendererStartupHandoffStatus status =
      RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST;
  bool accepted = false;
  bool child_must_resolve_startup_plan = false;
  bool child_native_preflight_required = false;
  bool child_shadow_fallback_allowed = false;
  bool used_frontend_fallback = false;
  bool used_shadow_fallback = false;
  bool used_production_gate_fallback = false;
};

bool IsKnownRendererFrontendChild(RendererFrontendChild child) noexcept;
bool IsKnownRendererStartupHandoffStatus(
    RendererStartupHandoffStatus status) noexcept;

/// Selects only a packaged child executable. It never performs or accepts
/// native-device preflight because that evidence is process-local. Every
/// Ogre-Next child must call ResolveRendererStartupPlan again before creating
/// Ogre::Root; REQUIRE_NATIVE can never fall back in either process.
RendererStartupHandoffResult ResolveRendererStartupHandoff(
    const RendererStartupHandoffRequest &request,
    const RendererStartupPackageAvailability &availability) noexcept;

/// Returns the exact sibling executable basename for an accepted handoff. The
/// trusted package platform recorded by the resolver determines the suffix;
/// the request cannot choose it. The caller must join this basename to its own
/// trusted executable directory and launch it without a shell. Rejected or
/// malformed results return an empty string.
const char *RendererFrontendChildExecutableName(
    const RendererStartupHandoffResult &handoff) noexcept;

const char *ToString(RendererFrontendChild child) noexcept;
const char *ToString(RendererStartupHandoffStatus status) noexcept;

} // namespace RoR
