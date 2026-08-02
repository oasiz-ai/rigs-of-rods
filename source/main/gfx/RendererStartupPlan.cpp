/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererStartupPlan.h"

namespace RoR {
namespace {

bool HasOgreNextPssmPath(
    const RendererStartupBuildAvailability &availability) noexcept {
  return availability.ogre_next_frontend_available &&
         availability.ogre_next_pssm_available;
}

RendererNativePreflightReadiness ResolveNativePreflightReadiness(
    const RendererStartupRequest &request,
    const RendererStartupBuildAvailability &availability,
    const RendererNativeShadowPreflight &preflight) noexcept {
  if (request.frontend == RendererFrontendPreference::LEGACY_ONLY) {
    return RendererNativePreflightReadiness::FRONTEND_INCOMPATIBLE;
  }
  if (!availability.ogre_next_frontend_available) {
    return RendererNativePreflightReadiness::FRONTEND_UNAVAILABLE;
  }

  const NativeRayTracingBackend expected_backend =
      ExpectedNativeRayTracingBackend(request.host_platform);
  if (expected_backend == NativeRayTracingBackend::NONE) {
    return RendererNativePreflightReadiness::PLATFORM_UNSUPPORTED;
  }
  if (availability.native_directional_shadow_backend ==
      NativeRayTracingBackend::NONE) {
    return RendererNativePreflightReadiness::BACKEND_UNAVAILABLE;
  }
  if (availability.native_directional_shadow_backend != expected_backend) {
    return RendererNativePreflightReadiness::BACKEND_MISMATCH;
  }
  if (!preflight.completed) {
    return RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED;
  }
  if (preflight.source !=
      RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API) {
    return RendererNativePreflightReadiness::EVIDENCE_NOT_CURRENT_PROCESS;
  }
  if (preflight.backend != expected_backend) {
    return RendererNativePreflightReadiness::BACKEND_MISMATCH;
  }
  if (!preflight.device_selected) {
    return RendererNativePreflightReadiness::DEVICE_NOT_SELECTED;
  }
  if (preflight.device_identity == 0U) {
    return RendererNativePreflightReadiness::DEVICE_IDENTITY_UNAVAILABLE;
  }
  if (!preflight.api_supported) {
    return RendererNativePreflightReadiness::API_UNSUPPORTED;
  }
  if (!preflight.hardware_accelerated) {
    return RendererNativePreflightReadiness::
        HARDWARE_ACCELERATION_UNAVAILABLE;
  }
  if (!preflight.hardware_floor_met) {
    return RendererNativePreflightReadiness::HARDWARE_FLOOR_NOT_MET;
  }
  return RendererNativePreflightReadiness::ELIGIBLE;
}

void Select(RendererStartupPlanResult &result, RendererStartupPath path,
            RendererStartupSelectionStatus status, bool shadow_fallback,
            bool frontend_fallback) noexcept {
  result.effective_path = path;
  result.status = status;
  result.accepted = true;
  result.used_shadow_fallback = shadow_fallback;
  result.used_frontend_fallback = frontend_fallback;
}

void SelectOgre14Fallback(RendererStartupPlanResult &result,
                          bool shadow_fallback) noexcept {
  Select(result, RendererStartupPath::OGRE14_PSSM,
         RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM,
         shadow_fallback, true);
}

bool HasValidContractVersions(
    const RendererStartupRequest &request,
    const RendererStartupBuildAvailability &availability,
    const RendererNativeShadowPreflight &preflight) noexcept {
  return request.version == kRendererStartupPlanContractVersion &&
         availability.version == kRendererStartupPlanContractVersion &&
         preflight.version == kRendererStartupPlanContractVersion;
}

} // namespace

bool IsKnownRendererFrontendPreference(
    RendererFrontendPreference preference) noexcept {
  switch (preference) {
  case RendererFrontendPreference::LEGACY_ONLY:
  case RendererFrontendPreference::OGRE_NEXT_PREFER:
  case RendererFrontendPreference::OGRE_NEXT_REQUIRE:
    return true;
  }
  return false;
}

bool IsKnownDirectionalShadowPreference(
    DirectionalShadowPreference preference) noexcept {
  switch (preference) {
  case DirectionalShadowPreference::PSSM:
  case DirectionalShadowPreference::PREFER_NATIVE:
  case DirectionalShadowPreference::REQUIRE_NATIVE:
    return true;
  }
  return false;
}

bool IsKnownRendererNativePreflightSource(
    RendererNativePreflightSource source) noexcept {
  switch (source) {
  case RendererNativePreflightSource::NONE:
  case RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API:
    return true;
  }
  return false;
}

bool IsKnownRendererNativePreflightReadiness(
    RendererNativePreflightReadiness readiness) noexcept {
  switch (readiness) {
  case RendererNativePreflightReadiness::NOT_REQUESTED:
  case RendererNativePreflightReadiness::ELIGIBLE:
  case RendererNativePreflightReadiness::FRONTEND_INCOMPATIBLE:
  case RendererNativePreflightReadiness::FRONTEND_UNAVAILABLE:
  case RendererNativePreflightReadiness::PLATFORM_UNSUPPORTED:
  case RendererNativePreflightReadiness::BACKEND_UNAVAILABLE:
  case RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED:
  case RendererNativePreflightReadiness::EVIDENCE_NOT_CURRENT_PROCESS:
  case RendererNativePreflightReadiness::BACKEND_MISMATCH:
  case RendererNativePreflightReadiness::DEVICE_NOT_SELECTED:
  case RendererNativePreflightReadiness::DEVICE_IDENTITY_UNAVAILABLE:
  case RendererNativePreflightReadiness::API_UNSUPPORTED:
  case RendererNativePreflightReadiness::HARDWARE_ACCELERATION_UNAVAILABLE:
  case RendererNativePreflightReadiness::HARDWARE_FLOOR_NOT_MET:
    return true;
  }
  return false;
}

bool IsKnownRendererStartupPath(RendererStartupPath path) noexcept {
  switch (path) {
  case RendererStartupPath::NONE:
  case RendererStartupPath::OGRE14_PSSM:
  case RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1:
  case RendererStartupPath::OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1:
    return true;
  }
  return false;
}

bool IsKnownRendererStartupSelectionStatus(
    RendererStartupSelectionStatus status) noexcept {
  switch (status) {
  case RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH:
  case RendererStartupSelectionStatus::FALLBACK_TO_OGRE_NEXT_PSSM:
  case RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM:
  case RendererStartupSelectionStatus::REJECTED_INVALID_REQUEST:
  case RendererStartupSelectionStatus::
      REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE:
  case RendererStartupSelectionStatus::
      REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE:
  case RendererStartupSelectionStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE:
  case RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH:
    return true;
  }
  return false;
}

RendererStartupPlanResult ResolveRendererStartupPlan(
    const RendererStartupRequest &request,
    const RendererStartupBuildAvailability &availability,
    const RendererNativeShadowPreflight &preflight) noexcept {
  RendererStartupPlanResult result;
  result.requested_frontend = request.frontend;
  result.requested_directional_shadows = request.directional_shadows;

  if (!HasValidContractVersions(request, availability, preflight) ||
      !IsKnownRendererFrontendPreference(request.frontend) ||
      !IsKnownDirectionalShadowPreference(request.directional_shadows) ||
      !IsKnownHostRenderPlatform(request.host_platform) ||
      !IsKnownNativeRayTracingBackend(
          availability.native_directional_shadow_backend) ||
      !IsKnownRendererNativePreflightSource(preflight.source) ||
      !IsKnownNativeRayTracingBackend(preflight.backend)) {
    return result;
  }

  if (request.directional_shadows == DirectionalShadowPreference::PSSM) {
    if (request.frontend == RendererFrontendPreference::LEGACY_ONLY) {
      if (availability.ogre14_frontend_available) {
        Select(result, RendererStartupPath::OGRE14_PSSM,
               RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false,
               false);
      } else {
        result.status = RendererStartupSelectionStatus::
            REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE;
      }
      return result;
    }

    if (HasOgreNextPssmPath(availability)) {
      Select(result, RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
             RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false,
             false);
      return result;
    }
    if (request.frontend == RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
      result.status = RendererStartupSelectionStatus::
          REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE;
      return result;
    }
    if (availability.ogre14_frontend_available) {
      SelectOgre14Fallback(result, false);
      return result;
    }
    result.status = RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH;
    return result;
  }

  result.native_preflight_readiness =
      ResolveNativePreflightReadiness(request, availability, preflight);
  if (result.native_preflight_readiness ==
      RendererNativePreflightReadiness::ELIGIBLE) {
    result.effective_native_backend = preflight.backend;
    Select(result,
           RendererStartupPath::
               OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1,
           RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false,
           false);
    return result;
  }

  if (request.directional_shadows ==
      DirectionalShadowPreference::REQUIRE_NATIVE) {
    result.status = RendererStartupSelectionStatus::
        REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
    return result;
  }

  if (request.frontend != RendererFrontendPreference::LEGACY_ONLY &&
      HasOgreNextPssmPath(availability)) {
    Select(result, RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
           RendererStartupSelectionStatus::FALLBACK_TO_OGRE_NEXT_PSSM, true,
           false);
    return result;
  }

  if (request.frontend == RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
    result.status = RendererStartupSelectionStatus::
        REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE;
    return result;
  }
  if (availability.ogre14_frontend_available) {
    Select(result, RendererStartupPath::OGRE14_PSSM,
           RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM, true,
           request.frontend == RendererFrontendPreference::OGRE_NEXT_PREFER);
    return result;
  }

  result.status = request.frontend == RendererFrontendPreference::LEGACY_ONLY
                      ? RendererStartupSelectionStatus::
                            REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE
                      : RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH;
  return result;
}

const char *ToString(RendererFrontendPreference preference) noexcept {
  switch (preference) {
  case RendererFrontendPreference::LEGACY_ONLY:
    return "legacy-only";
  case RendererFrontendPreference::OGRE_NEXT_PREFER:
    return "ogre-next-prefer";
  case RendererFrontendPreference::OGRE_NEXT_REQUIRE:
    return "ogre-next-require";
  }
  return "invalid";
}

const char *ToString(DirectionalShadowPreference preference) noexcept {
  switch (preference) {
  case DirectionalShadowPreference::PSSM:
    return "pssm";
  case DirectionalShadowPreference::PREFER_NATIVE:
    return "prefer-native";
  case DirectionalShadowPreference::REQUIRE_NATIVE:
    return "require-native";
  }
  return "invalid";
}

const char *ToString(RendererNativePreflightSource source) noexcept {
  switch (source) {
  case RendererNativePreflightSource::NONE:
    return "none";
  case RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API:
    return "current-process-native-api";
  }
  return "invalid";
}

const char *ToString(RendererNativePreflightReadiness readiness) noexcept {
  switch (readiness) {
  case RendererNativePreflightReadiness::NOT_REQUESTED:
    return "not-requested";
  case RendererNativePreflightReadiness::ELIGIBLE:
    return "eligible";
  case RendererNativePreflightReadiness::FRONTEND_INCOMPATIBLE:
    return "frontend-incompatible";
  case RendererNativePreflightReadiness::FRONTEND_UNAVAILABLE:
    return "frontend-unavailable";
  case RendererNativePreflightReadiness::PLATFORM_UNSUPPORTED:
    return "platform-unsupported";
  case RendererNativePreflightReadiness::BACKEND_UNAVAILABLE:
    return "backend-unavailable";
  case RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED:
    return "preflight-not-completed";
  case RendererNativePreflightReadiness::EVIDENCE_NOT_CURRENT_PROCESS:
    return "evidence-not-current-process";
  case RendererNativePreflightReadiness::BACKEND_MISMATCH:
    return "backend-mismatch";
  case RendererNativePreflightReadiness::DEVICE_NOT_SELECTED:
    return "device-not-selected";
  case RendererNativePreflightReadiness::DEVICE_IDENTITY_UNAVAILABLE:
    return "device-identity-unavailable";
  case RendererNativePreflightReadiness::API_UNSUPPORTED:
    return "api-unsupported";
  case RendererNativePreflightReadiness::HARDWARE_ACCELERATION_UNAVAILABLE:
    return "hardware-acceleration-unavailable";
  case RendererNativePreflightReadiness::HARDWARE_FLOOR_NOT_MET:
    return "hardware-floor-not-met";
  }
  return "invalid";
}

const char *ToString(RendererStartupPath path) noexcept {
  switch (path) {
  case RendererStartupPath::NONE:
    return "none";
  case RendererStartupPath::OGRE14_PSSM:
    return "ogre14-pssm";
  case RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1:
    return "ogre-next-pssm-3-cascade-v1";
  case RendererStartupPath::OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1:
    return "ogre-next-native-directional-hard-shadow-v1";
  }
  return "invalid";
}

const char *ToString(RendererStartupSelectionStatus status) noexcept {
  switch (status) {
  case RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH:
    return "selected-requested-path";
  case RendererStartupSelectionStatus::FALLBACK_TO_OGRE_NEXT_PSSM:
    return "fallback-to-ogre-next-pssm";
  case RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM:
    return "fallback-to-ogre14-pssm";
  case RendererStartupSelectionStatus::REJECTED_INVALID_REQUEST:
    return "rejected-invalid-request";
  case RendererStartupSelectionStatus::
      REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE:
    return "rejected-required-legacy-path-unavailable";
  case RendererStartupSelectionStatus::
      REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE:
    return "rejected-required-ogre-next-path-unavailable";
  case RendererStartupSelectionStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE:
    return "rejected-native-required-unavailable";
  case RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH:
    return "rejected-no-viable-path";
  }
  return "invalid";
}

} // namespace RoR
