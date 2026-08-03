/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererStartupHandoff.h"

namespace RoR {
namespace {

bool HasAdmittedOgreNextChild(
    const RendererStartupPackageAvailability &availability) noexcept {
  return availability.ogre_next_child_present &&
         availability.ogre_next_child_production_ready &&
         availability.ogre_next_pssm_admitted;
}

bool HasConsistentPackageFacts(
    const RendererStartupPackageAvailability &availability) noexcept {
  if (!availability.ogre_next_child_present &&
      (availability.ogre_next_child_production_ready ||
       availability.ogre_next_pssm_admitted ||
       availability.native_directional_shadow_backend !=
           NativeRayTracingBackend::NONE)) {
    return false;
  }
  if (availability.ogre_next_child_production_ready &&
      !availability.ogre_next_pssm_admitted) {
    return false;
  }
  const NativeRayTracingBackend declared =
      availability.native_directional_shadow_backend;
  return declared == NativeRayTracingBackend::NONE ||
         declared ==
             ExpectedNativeRayTracingBackend(availability.package_platform);
}

bool HasValidInputs(
    const RendererStartupHandoffRequest &request,
    const RendererStartupPackageAvailability &availability) noexcept {
  return request.version == kRendererStartupHandoffContractVersion &&
         request.startup.version == kRendererStartupPlanContractVersion &&
         availability.version == kRendererStartupHandoffContractVersion &&
         IsKnownRendererFrontendPreference(request.startup.frontend) &&
         IsKnownDirectionalShadowPreference(
             request.startup.directional_shadows) &&
         IsKnownHostRenderPlatform(request.startup.host_platform) &&
         request.startup.host_platform != HostRenderPlatform::UNKNOWN &&
         IsKnownHostRenderPlatform(availability.package_platform) &&
         availability.package_platform != HostRenderPlatform::UNKNOWN &&
         request.startup.host_platform == availability.package_platform &&
         IsKnownNativeRayTracingBackend(
             availability.native_directional_shadow_backend) &&
         HasConsistentPackageFacts(availability);
}

void Select(RendererStartupHandoffResult &result, RendererFrontendChild child,
            RendererStartupHandoffStatus status,
            const RendererStartupPackageAvailability &availability,
            bool frontend_fallback, bool shadow_fallback,
            bool production_gate_fallback) noexcept {
  result.child = child;
  result.package_platform = availability.package_platform;
  result.status = status;
  result.accepted = true;
  result.child_must_resolve_startup_plan =
      child == RendererFrontendChild::OGRE_NEXT;
  result.child_native_preflight_required =
      child == RendererFrontendChild::OGRE_NEXT &&
      result.requested_directional_shadows !=
          DirectionalShadowPreference::PSSM &&
      availability.native_directional_shadow_backend !=
          NativeRayTracingBackend::NONE;
  result.child_shadow_fallback_allowed =
      child == RendererFrontendChild::OGRE_NEXT &&
      result.requested_directional_shadows ==
          DirectionalShadowPreference::PREFER_NATIVE;
  result.declared_native_backend =
      child == RendererFrontendChild::OGRE_NEXT
          ? availability.native_directional_shadow_backend
          : NativeRayTracingBackend::NONE;
  result.used_frontend_fallback = frontend_fallback;
  result.used_shadow_fallback = shadow_fallback;
  result.used_production_gate_fallback = production_gate_fallback;
}

bool HasConsistentAcceptedResult(
    const RendererStartupHandoffResult &result) noexcept {
  if (result.version != kRendererStartupHandoffContractVersion ||
      !result.accepted ||
      !IsKnownRendererFrontendPreference(result.requested_frontend) ||
      !IsKnownDirectionalShadowPreference(
          result.requested_directional_shadows) ||
      !IsKnownRendererFrontendChild(result.child) ||
      result.child == RendererFrontendChild::NONE ||
      !IsKnownHostRenderPlatform(result.package_platform) ||
      result.package_platform == HostRenderPlatform::UNKNOWN ||
      !IsKnownNativeRayTracingBackend(result.declared_native_backend) ||
      !IsKnownRendererStartupHandoffStatus(result.status)) {
    return false;
  }

  if (result.child == RendererFrontendChild::OGRE14) {
    if (result.child_must_resolve_startup_plan ||
        result.child_native_preflight_required ||
        result.child_shadow_fallback_allowed ||
        result.declared_native_backend != NativeRayTracingBackend::NONE ||
        result.requested_directional_shadows ==
            DirectionalShadowPreference::REQUIRE_NATIVE ||
        result.used_shadow_fallback !=
            (result.requested_directional_shadows ==
             DirectionalShadowPreference::PREFER_NATIVE)) {
      return false;
    }
    if (result.requested_frontend ==
        RendererFrontendPreference::LEGACY_ONLY) {
      const RendererStartupHandoffStatus expected_status =
          result.requested_directional_shadows ==
                  DirectionalShadowPreference::PREFER_NATIVE
              ? RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD
              : RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD;
      return !result.used_frontend_fallback &&
             !result.used_production_gate_fallback &&
             result.status == expected_status;
    }
    return result.requested_frontend ==
               RendererFrontendPreference::OGRE_NEXT_PREFER &&
           result.status ==
               RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD &&
           result.used_frontend_fallback;
  }

  if (result.requested_frontend ==
          RendererFrontendPreference::LEGACY_ONLY ||
      result.status !=
          RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD ||
      !result.child_must_resolve_startup_plan ||
      result.used_frontend_fallback || result.used_production_gate_fallback ||
      (result.declared_native_backend != NativeRayTracingBackend::NONE &&
       result.declared_native_backend !=
           ExpectedNativeRayTracingBackend(result.package_platform))) {
    return false;
  }

  switch (result.requested_directional_shadows) {
  case DirectionalShadowPreference::PSSM:
    return !result.child_native_preflight_required &&
           !result.child_shadow_fallback_allowed &&
           !result.used_shadow_fallback;
  case DirectionalShadowPreference::PREFER_NATIVE:
    return result.child_native_preflight_required ==
               (result.declared_native_backend !=
                NativeRayTracingBackend::NONE) &&
           result.child_shadow_fallback_allowed &&
           result.used_shadow_fallback ==
               (result.declared_native_backend ==
                NativeRayTracingBackend::NONE);
  case DirectionalShadowPreference::REQUIRE_NATIVE:
    return result.declared_native_backend != NativeRayTracingBackend::NONE &&
           result.child_native_preflight_required &&
           !result.child_shadow_fallback_allowed &&
           !result.used_shadow_fallback;
  }
  return false;
}

} // namespace

bool IsKnownRendererFrontendChild(RendererFrontendChild child) noexcept {
  switch (child) {
  case RendererFrontendChild::NONE:
  case RendererFrontendChild::OGRE14:
  case RendererFrontendChild::OGRE_NEXT:
    return true;
  }
  return false;
}

bool IsKnownRendererStartupHandoffStatus(
    RendererStartupHandoffStatus status) noexcept {
  switch (status) {
  case RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD:
  case RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD:
  case RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST:
  case RendererStartupHandoffStatus::REJECTED_REQUIRED_LEGACY_CHILD_UNAVAILABLE:
  case RendererStartupHandoffStatus::
      REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE:
  case RendererStartupHandoffStatus::
      REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY:
  case RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE:
  case RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD:
    return true;
  }
  return false;
}

RendererStartupHandoffResult ResolveRendererStartupHandoff(
    const RendererStartupHandoffRequest &request,
    const RendererStartupPackageAvailability &availability) noexcept {
  RendererStartupHandoffResult result;
  result.requested_frontend = request.startup.frontend;
  result.requested_directional_shadows = request.startup.directional_shadows;

  if (!HasValidInputs(request, availability)) {
    return result;
  }

  const bool ogre_next_admitted = HasAdmittedOgreNextChild(availability);
  const bool ogre_next_blocked_by_production_gate =
      availability.ogre_next_child_present &&
      !availability.ogre_next_child_production_ready;

  if (request.startup.directional_shadows ==
      DirectionalShadowPreference::REQUIRE_NATIVE) {
    if (request.startup.frontend == RendererFrontendPreference::LEGACY_ONLY) {
      result.status =
          RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
      return result;
    }
    if (!availability.ogre_next_child_present) {
      result.status = request.startup.frontend ==
                              RendererFrontendPreference::OGRE_NEXT_REQUIRE
                          ? RendererStartupHandoffStatus::
                                REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE
                          : RendererStartupHandoffStatus::
                                REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
      return result;
    }
    if (!ogre_next_admitted) {
      result.status = RendererStartupHandoffStatus::
          REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY;
      return result;
    }
    if (availability.native_directional_shadow_backend ==
        NativeRayTracingBackend::NONE) {
      result.status =
          RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
      return result;
    }
    Select(result, RendererFrontendChild::OGRE_NEXT,
           RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD, availability,
           false, false, false);
    return result;
  }

  if (request.startup.frontend == RendererFrontendPreference::LEGACY_ONLY) {
    if (!availability.ogre14_child_present) {
      result.status = RendererStartupHandoffStatus::
          REJECTED_REQUIRED_LEGACY_CHILD_UNAVAILABLE;
      return result;
    }
    const bool shadow_fallback = request.startup.directional_shadows ==
                                 DirectionalShadowPreference::PREFER_NATIVE;
    Select(result, RendererFrontendChild::OGRE14,
           shadow_fallback
               ? RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD
               : RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD,
           availability, false, shadow_fallback, false);
    return result;
  }

  if (ogre_next_admitted) {
    const bool immediate_shadow_fallback =
        request.startup.directional_shadows ==
            DirectionalShadowPreference::PREFER_NATIVE &&
        availability.native_directional_shadow_backend ==
            NativeRayTracingBackend::NONE;
    Select(result, RendererFrontendChild::OGRE_NEXT,
           RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD, availability,
           false, immediate_shadow_fallback, false);
    return result;
  }

  if (request.startup.frontend ==
      RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
    result.status = availability.ogre_next_child_present
                        ? RendererStartupHandoffStatus::
                              REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY
                        : RendererStartupHandoffStatus::
                              REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE;
    return result;
  }
  if (availability.ogre14_child_present) {
    Select(result, RendererFrontendChild::OGRE14,
           RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD, availability,
           true,
           request.startup.directional_shadows ==
               DirectionalShadowPreference::PREFER_NATIVE,
           ogre_next_blocked_by_production_gate);
    return result;
  }

  result.status = RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD;
  return result;
}

const char *RendererFrontendChildExecutableName(
    const RendererStartupHandoffResult &handoff) noexcept {
  if (!HasConsistentAcceptedResult(handoff)) {
    return "";
  }
  if (handoff.package_platform == HostRenderPlatform::WINDOWS) {
    return handoff.child == RendererFrontendChild::OGRE14 ? "RoR-Ogre14.exe"
                                                          : "RoR-OgreNext.exe";
  }
  return handoff.child == RendererFrontendChild::OGRE14 ? "RoR-Ogre14"
                                                        : "RoR-OgreNext";
}

const char *ToString(RendererFrontendChild child) noexcept {
  switch (child) {
  case RendererFrontendChild::NONE:
    return "none";
  case RendererFrontendChild::OGRE14:
    return "ogre14";
  case RendererFrontendChild::OGRE_NEXT:
    return "ogre-next";
  }
  return "invalid";
}

const char *ToString(RendererStartupHandoffStatus status) noexcept {
  switch (status) {
  case RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD:
    return "selected-requested-child";
  case RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD:
    return "fallback-to-ogre14-child";
  case RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST:
    return "rejected-invalid-request";
  case RendererStartupHandoffStatus::REJECTED_REQUIRED_LEGACY_CHILD_UNAVAILABLE:
    return "rejected-required-legacy-child-unavailable";
  case RendererStartupHandoffStatus::
      REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE:
    return "rejected-required-ogre-next-child-unavailable";
  case RendererStartupHandoffStatus::
      REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY:
    return "rejected-ogre-next-child-not-production-ready";
  case RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE:
    return "rejected-native-required-unavailable";
  case RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD:
    return "rejected-no-viable-child";
  }
  return "invalid";
}

} // namespace RoR
