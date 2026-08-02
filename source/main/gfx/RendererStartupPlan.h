/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free, pre-initialization renderer startup planning.

#pragma once

#include "RendererBackendPolicy.h"

#include <cstdint>

namespace RoR {

/// Startup planning is intentionally versioned separately from the live
/// renderer contracts. A launcher must reject versions it does not understand.
constexpr std::uint32_t kRendererStartupPlanContractVersion = 1U;

/// Frontend intent expressed before either process-global OGRE Root exists.
enum class RendererFrontendPreference : std::uint8_t {
  LEGACY_ONLY = 0,
  OGRE_NEXT_PREFER = 1,
  OGRE_NEXT_REQUIRE = 2,
};

/// Directional-shadow intent. Native means the renderer-neutral
/// NATIVE_DIRECTIONAL_HARD_SHADOW_V1 path; PSSM is the validated raster path.
enum class DirectionalShadowPreference : std::uint8_t {
  PSSM = 0,
  PREFER_NATIVE = 1,
  REQUIRE_NATIVE = 2,
};

/// Provenance for cheap native capability observations made before frontend
/// initialization. Persisted CI evidence and observations from another process
/// are deliberately inadmissible.
enum class RendererNativePreflightSource : std::uint8_t {
  NONE = 0,
  CURRENT_PROCESS_NATIVE_API = 1,
};

/// Why the native path is or is not an eligible startup candidate. ELIGIBLE is
/// not post-initialization readiness: the selected frontend/backend must still
/// revalidate exact-device interop and execute their normal evidence gates.
enum class RendererNativePreflightReadiness : std::uint8_t {
  NOT_REQUESTED = 0,
  ELIGIBLE = 1,
  FRONTEND_INCOMPATIBLE = 2,
  FRONTEND_UNAVAILABLE = 3,
  PLATFORM_UNSUPPORTED = 4,
  BACKEND_UNAVAILABLE = 5,
  PREFLIGHT_NOT_COMPLETED = 6,
  EVIDENCE_NOT_CURRENT_PROCESS = 7,
  BACKEND_MISMATCH = 8,
  DEVICE_NOT_SELECTED = 9,
  DEVICE_IDENTITY_UNAVAILABLE = 10,
  API_UNSUPPORTED = 11,
  HARDWARE_ACCELERATION_UNAVAILABLE = 12,
  HARDWARE_FLOOR_NOT_MET = 13,
};

/// One executable/configuration path which may be initialized. NONE is
/// result-only and must never be launched.
enum class RendererStartupPath : std::uint8_t {
  NONE = 0,
  OGRE14_PSSM = 1,
  OGRE_NEXT_PSSM_3_CASCADE_V1 = 2,
  OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1 = 3,
};

enum class RendererStartupSelectionStatus : std::uint8_t {
  SELECTED_REQUESTED_PATH = 0,
  FALLBACK_TO_OGRE_NEXT_PSSM = 1,
  FALLBACK_TO_OGRE14_PSSM = 2,
  REJECTED_INVALID_REQUEST = 3,
  REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE = 4,
  REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE = 5,
  REJECTED_NATIVE_REQUIRED_UNAVAILABLE = 6,
  REJECTED_NO_VIABLE_PATH = 7,
};

struct RendererStartupRequest {
  std::uint32_t version = kRendererStartupPlanContractVersion;
  RendererFrontendPreference frontend =
      RendererFrontendPreference::OGRE_NEXT_PREFER;
  DirectionalShadowPreference directional_shadows =
      DirectionalShadowPreference::PSSM;
  HostRenderPlatform host_platform = HostRenderPlatform::UNKNOWN;
};

/// Build/package facts, not initialized runtime state. The two frontends may be
/// separate executables; these flags never imply that both OGRE ABIs coexist in
/// one process.
struct RendererStartupBuildAvailability {
  std::uint32_t version = kRendererStartupPlanContractVersion;
  bool ogre14_frontend_available = false;
  bool ogre_next_frontend_available = false;
  bool ogre_next_pssm_available = false;
  NativeRayTracingBackend native_directional_shadow_backend =
      NativeRayTracingBackend::NONE;
};

/// Narrow pre-initialization observation. `device_identity` is an opaque,
/// process-local nonzero identity supplied by the native platform adapter. It
/// must later match the exact device selected by the Ogre-Next frontend.
struct RendererNativeShadowPreflight {
  std::uint32_t version = kRendererStartupPlanContractVersion;
  RendererNativePreflightSource source = RendererNativePreflightSource::NONE;
  NativeRayTracingBackend backend = NativeRayTracingBackend::NONE;
  bool completed = false;
  bool device_selected = false;
  std::uint64_t device_identity = 0U;
  bool api_supported = false;
  bool hardware_accelerated = false;
  bool hardware_floor_met = false;
};

struct RendererStartupPlanResult {
  std::uint32_t version = kRendererStartupPlanContractVersion;
  RendererFrontendPreference requested_frontend =
      RendererFrontendPreference::LEGACY_ONLY;
  DirectionalShadowPreference requested_directional_shadows =
      DirectionalShadowPreference::PSSM;
  RendererStartupPath effective_path = RendererStartupPath::NONE;
  NativeRayTracingBackend effective_native_backend =
      NativeRayTracingBackend::NONE;
  RendererNativePreflightReadiness native_preflight_readiness =
      RendererNativePreflightReadiness::NOT_REQUESTED;
  RendererStartupSelectionStatus status =
      RendererStartupSelectionStatus::REJECTED_INVALID_REQUEST;
  bool accepted = false;
  bool used_shadow_fallback = false;
  bool used_frontend_fallback = false;
};

bool IsKnownRendererFrontendPreference(
    RendererFrontendPreference preference) noexcept;
bool IsKnownDirectionalShadowPreference(
    DirectionalShadowPreference preference) noexcept;
bool IsKnownRendererNativePreflightSource(
    RendererNativePreflightSource source) noexcept;
bool IsKnownRendererNativePreflightReadiness(
    RendererNativePreflightReadiness readiness) noexcept;
bool IsKnownRendererStartupPath(RendererStartupPath path) noexcept;
bool IsKnownRendererStartupSelectionStatus(
    RendererStartupSelectionStatus status) noexcept;

/// Resolves exactly one path without initializing OGRE or a graphics API.
/// Unknown versions/enums fail closed. Prefer-native falls back first to the
/// Ogre-Next PSSM path, then (only for OGRE_NEXT_PREFER) to OGRE14/PSSM.
/// Require-native and OGRE_NEXT_REQUIRE never silently cross their hard gate.
RendererStartupPlanResult ResolveRendererStartupPlan(
    const RendererStartupRequest &request,
    const RendererStartupBuildAvailability &availability,
    const RendererNativeShadowPreflight &preflight) noexcept;

const char *ToString(RendererFrontendPreference preference) noexcept;
const char *ToString(DirectionalShadowPreference preference) noexcept;
const char *ToString(RendererNativePreflightSource source) noexcept;
const char *ToString(RendererNativePreflightReadiness readiness) noexcept;
const char *ToString(RendererStartupPath path) noexcept;
const char *ToString(RendererStartupSelectionStatus status) noexcept;

} // namespace RoR
