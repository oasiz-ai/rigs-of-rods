/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererStartupPlan.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer startup plan test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::RendererStartupBuildAvailability MakeAllPathsAvailable(
    RoR::NativeRayTracingBackend backend =
        RoR::NativeRayTracingBackend::METAL) {
  RoR::RendererStartupBuildAvailability availability;
  availability.ogre14_frontend_available = true;
  availability.ogre_next_frontend_available = true;
  availability.ogre_next_pssm_available = true;
  availability.native_directional_shadow_backend = backend;
  return availability;
}

RoR::RendererNativeShadowPreflight
MakeEligiblePreflight(RoR::NativeRayTracingBackend backend) {
  RoR::RendererNativeShadowPreflight preflight;
  preflight.source =
      RoR::RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API;
  preflight.backend = backend;
  preflight.completed = true;
  preflight.device_selected = true;
  preflight.device_identity = 0x1234U;
  preflight.api_supported = true;
  preflight.hardware_accelerated = true;
  preflight.hardware_floor_met = true;
  return preflight;
}

RoR::RendererStartupRequest MakeRequest(
    RoR::RendererFrontendPreference frontend,
    RoR::DirectionalShadowPreference directional_shadows,
    RoR::HostRenderPlatform platform) {
  RoR::RendererStartupRequest request;
  request.frontend = frontend;
  request.directional_shadows = directional_shadows;
  request.host_platform = platform;
  return request;
}

void RequireResult(
    const RoR::RendererStartupPlanResult &result, bool accepted,
    RoR::RendererStartupPath path,
    RoR::NativeRayTracingBackend native_backend,
    RoR::RendererNativePreflightReadiness native_readiness,
    RoR::RendererStartupSelectionStatus status, bool shadow_fallback,
    bool frontend_fallback, const char *message) {
  Require(result.version == RoR::kRendererStartupPlanContractVersion, message);
  Require(result.accepted == accepted, message);
  Require(result.effective_path == path, message);
  Require(result.effective_native_backend == native_backend, message);
  Require(result.native_preflight_readiness == native_readiness, message);
  Require(result.status == status, message);
  Require(result.used_shadow_fallback == shadow_fallback, message);
  Require(result.used_frontend_fallback == frontend_fallback, message);
}

void RequireInvalid(const RoR::RendererStartupRequest &request,
                    const RoR::RendererStartupBuildAvailability &availability,
                    const RoR::RendererNativeShadowPreflight &preflight,
                    const char *message) {
  RequireResult(
      RoR::ResolveRendererStartupPlan(request, availability, preflight), false,
      RoR::RendererStartupPath::NONE, RoR::NativeRayTracingBackend::NONE,
      RoR::RendererNativePreflightReadiness::NOT_REQUESTED,
      RoR::RendererStartupSelectionStatus::REJECTED_INVALID_REQUEST, false,
      false, message);
}

void TestEnumClassifiersRejectUnknownValues() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const RoR::RendererFrontendPreference frontend =
        static_cast<RoR::RendererFrontendPreference>(value);
    const bool expected_frontend =
        frontend == RoR::RendererFrontendPreference::LEGACY_ONLY ||
        frontend == RoR::RendererFrontendPreference::OGRE_NEXT_PREFER ||
        frontend == RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE;
    Require(RoR::IsKnownRendererFrontendPreference(frontend) ==
                expected_frontend,
            "frontend preference classifier accepted an unknown value");

    const RoR::DirectionalShadowPreference shadows =
        static_cast<RoR::DirectionalShadowPreference>(value);
    const bool expected_shadows =
        shadows == RoR::DirectionalShadowPreference::PSSM ||
        shadows == RoR::DirectionalShadowPreference::PREFER_NATIVE ||
        shadows == RoR::DirectionalShadowPreference::REQUIRE_NATIVE;
    Require(RoR::IsKnownDirectionalShadowPreference(shadows) ==
                expected_shadows,
            "shadow preference classifier accepted an unknown value");

    const RoR::RendererNativePreflightSource source =
        static_cast<RoR::RendererNativePreflightSource>(value);
    const bool expected_source =
        source == RoR::RendererNativePreflightSource::NONE ||
        source ==
            RoR::RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API;
    Require(RoR::IsKnownRendererNativePreflightSource(source) ==
                expected_source,
            "preflight source classifier accepted an unknown value");

    const RoR::RendererNativePreflightReadiness readiness =
        static_cast<RoR::RendererNativePreflightReadiness>(value);
    const bool expected_readiness = value <= static_cast<unsigned int>(
                                                 RoR::RendererNativePreflightReadiness::
                                                     HARDWARE_FLOOR_NOT_MET);
    Require(RoR::IsKnownRendererNativePreflightReadiness(readiness) ==
                expected_readiness,
            "preflight readiness classifier accepted an unknown value");

    const RoR::RendererStartupPath path =
        static_cast<RoR::RendererStartupPath>(value);
    const bool expected_path =
        path == RoR::RendererStartupPath::NONE ||
        path == RoR::RendererStartupPath::OGRE14_PSSM ||
        path == RoR::RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1 ||
        path == RoR::RendererStartupPath::
                    OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1;
    Require(RoR::IsKnownRendererStartupPath(path) == expected_path,
            "startup path classifier accepted an unknown value");

    const RoR::RendererStartupSelectionStatus status =
        static_cast<RoR::RendererStartupSelectionStatus>(value);
    const bool expected_status =
        value <= static_cast<unsigned int>(
                     RoR::RendererStartupSelectionStatus::
                         REJECTED_NO_VIABLE_PATH);
    Require(RoR::IsKnownRendererStartupSelectionStatus(status) ==
                expected_status,
            "startup status classifier accepted an unknown value");
  }
}

void TestDefaultRemainsOgre14Pssm() {
  const RoR::RendererStartupRequest request;
  RoR::RendererStartupBuildAvailability availability;
  availability.ogre14_frontend_available = true;
  const RoR::RendererNativeShadowPreflight preflight;

  const RoR::RendererStartupPlanResult result =
      RoR::ResolveRendererStartupPlan(request, availability, preflight);
  Require(result.requested_frontend ==
              RoR::RendererFrontendPreference::LEGACY_ONLY,
          "default frontend changed away from legacy-only");
  Require(result.requested_directional_shadows ==
              RoR::DirectionalShadowPreference::PSSM,
          "default shadow path changed away from PSSM");
  RequireResult(
      result, true, RoR::RendererStartupPath::OGRE14_PSSM,
      RoR::NativeRayTracingBackend::NONE,
      RoR::RendererNativePreflightReadiness::NOT_REQUESTED,
      RoR::RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false,
      false, "default OGRE14/PSSM selection changed");
}

void TestPssmFrontendPreferences() {
  using RoR::DirectionalShadowPreference;
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererFrontendPreference;
  using RoR::RendererNativePreflightReadiness;
  using RoR::RendererStartupPath;
  using RoR::RendererStartupSelectionStatus;

  const RoR::RendererNativeShadowPreflight preflight;
  RoR::RendererStartupBuildAvailability availability = MakeAllPathsAvailable();

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          availability, preflight),
      true, RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
      NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false, false,
      "preferred Ogre-Next PSSM path was not selected");

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          availability, preflight),
      true, RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
      NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false, false,
      "required Ogre-Next PSSM path was not selected");

  availability.ogre_next_pssm_available = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          availability, preflight),
      true, RendererStartupPath::OGRE14_PSSM, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM, false, true,
      "Ogre-Next preference did not fall back to OGRE14/PSSM");

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          availability, preflight),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::
          REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE,
      false, false, "required Ogre-Next path silently crossed to OGRE14");

  RoR::RendererStartupBuildAvailability no_legacy = MakeAllPathsAvailable();
  no_legacy.ogre14_frontend_available = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::LEGACY_ONLY,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          no_legacy, preflight),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::
          REJECTED_REQUIRED_LEGACY_PATH_UNAVAILABLE,
      false, false, "legacy-only selected an Ogre-Next path");

  availability.ogre14_frontend_available = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PSSM,
                      HostRenderPlatform::MACOS),
          availability, preflight),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::NOT_REQUESTED,
      RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH, false, false,
      "missing PSSM paths did not fail closed");
}

void TestNativeEligibleOnEveryPlatform() {
  using RoR::DirectionalShadowPreference;
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererFrontendPreference;
  using RoR::RendererNativePreflightReadiness;
  using RoR::RendererStartupPath;
  using RoR::RendererStartupSelectionStatus;

  const struct {
    HostRenderPlatform platform;
    NativeRayTracingBackend backend;
  } cases[] = {
      {HostRenderPlatform::MACOS, NativeRayTracingBackend::METAL},
      {HostRenderPlatform::WINDOWS, NativeRayTracingBackend::DXR},
      {HostRenderPlatform::LINUX, NativeRayTracingBackend::VULKAN_KHR},
  };

  for (const auto &test_case : cases) {
    const RoR::RendererStartupBuildAvailability availability =
        MakeAllPathsAvailable(test_case.backend);
    const RoR::RendererStartupPlanResult result =
        RoR::ResolveRendererStartupPlan(
            MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                        DirectionalShadowPreference::REQUIRE_NATIVE,
                        test_case.platform),
            availability, MakeEligiblePreflight(test_case.backend));
    RequireResult(
        result, true,
        RendererStartupPath::
            OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1,
        test_case.backend, RendererNativePreflightReadiness::ELIGIBLE,
        RendererStartupSelectionStatus::SELECTED_REQUESTED_PATH, false, false,
        "eligible platform-native startup path was not selected");
  }
}

void RequirePreferNativeFallback(
    const RoR::RendererStartupBuildAvailability &availability,
    RoR::HostRenderPlatform platform,
    const RoR::RendererNativeShadowPreflight &preflight,
    RoR::RendererNativePreflightReadiness expected_readiness,
    RoR::RendererStartupPath expected_path, bool frontend_fallback,
    const char *message) {
  const RoR::RendererStartupPlanResult result =
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RoR::RendererFrontendPreference::OGRE_NEXT_PREFER,
                      RoR::DirectionalShadowPreference::PREFER_NATIVE,
                      platform),
          availability, preflight);
  const RoR::RendererStartupSelectionStatus expected_status =
      expected_path == RoR::RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1
          ? RoR::RendererStartupSelectionStatus::FALLBACK_TO_OGRE_NEXT_PSSM
          : RoR::RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM;
  RequireResult(result, true, expected_path,
                RoR::NativeRayTracingBackend::NONE, expected_readiness,
                expected_status, true, frontend_fallback, message);
}

void TestEveryNativePreflightFactIsRequired() {
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererNativePreflightReadiness;
  using RoR::RendererStartupPath;

  RoR::RendererStartupBuildAvailability availability = MakeAllPathsAvailable();
  RoR::RendererNativeShadowPreflight preflight =
      MakeEligiblePreflight(NativeRayTracingBackend::METAL);

  RoR::RendererStartupBuildAvailability no_frontend = availability;
  no_frontend.ogre_next_frontend_available = false;
  RequirePreferNativeFallback(
      no_frontend, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::FRONTEND_UNAVAILABLE,
      RendererStartupPath::OGRE14_PSSM, true,
      "native selection ignored missing Ogre-Next frontend");

  RequirePreferNativeFallback(
      availability, HostRenderPlatform::UNKNOWN, preflight,
      RendererNativePreflightReadiness::PLATFORM_UNSUPPORTED,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored unsupported platform");

  RoR::RendererStartupBuildAvailability no_backend = availability;
  no_backend.native_directional_shadow_backend =
      NativeRayTracingBackend::NONE;
  RequirePreferNativeFallback(
      no_backend, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::BACKEND_UNAVAILABLE,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored missing backend package");

  RoR::RendererStartupBuildAvailability wrong_backend = availability;
  wrong_backend.native_directional_shadow_backend =
      NativeRayTracingBackend::VULKAN_KHR;
  RequirePreferNativeFallback(
      wrong_backend, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::BACKEND_MISMATCH,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection accepted the wrong packaged backend");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.completed = false;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored incomplete preflight");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.source = RoR::RendererNativePreflightSource::NONE;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::EVIDENCE_NOT_CURRENT_PROCESS,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection accepted inadmissible preflight provenance");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::VULKAN_KHR);
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::BACKEND_MISMATCH,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection accepted the wrong platform backend");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.device_selected = false;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::DEVICE_NOT_SELECTED,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored absent device selection");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.device_identity = 0U;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::DEVICE_IDENTITY_UNAVAILABLE,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored absent device identity");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.api_supported = false;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::API_UNSUPPORTED,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored API capability failure");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.hardware_accelerated = false;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::HARDWARE_ACCELERATION_UNAVAILABLE,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored hardware acceleration failure");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.hardware_floor_met = false;
  RequirePreferNativeFallback(
      availability, HostRenderPlatform::MACOS, preflight,
      RendererNativePreflightReadiness::HARDWARE_FLOOR_NOT_MET,
      RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1, false,
      "native selection ignored platform hardware floor");
}

void TestNativeAndFrontendFallbackPrecedence() {
  using RoR::DirectionalShadowPreference;
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererFrontendPreference;
  using RoR::RendererNativePreflightReadiness;
  using RoR::RendererStartupPath;
  using RoR::RendererStartupSelectionStatus;

  const RoR::RendererNativeShadowPreflight unsupported =
      RoR::RendererNativeShadowPreflight{};
  RoR::RendererStartupBuildAvailability availability = MakeAllPathsAvailable();

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PREFER_NATIVE,
                      HostRenderPlatform::MACOS),
          availability, unsupported),
      true, RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
      NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupSelectionStatus::FALLBACK_TO_OGRE_NEXT_PSSM, true, false,
      "prefer-native skipped the nearest Ogre-Next PSSM fallback");

  availability.ogre_next_pssm_available = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PREFER_NATIVE,
                      HostRenderPlatform::MACOS),
          availability, unsupported),
      true, RendererStartupPath::OGRE14_PSSM, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM, true, true,
      "prefer-native did not use the final OGRE14/PSSM fallback");

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                      DirectionalShadowPreference::PREFER_NATIVE,
                      HostRenderPlatform::MACOS),
          availability, unsupported),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupSelectionStatus::
          REJECTED_REQUIRED_OGRE_NEXT_PATH_UNAVAILABLE,
      false, false, "Ogre-Next require crossed to the legacy frontend");

  availability.ogre14_frontend_available = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                      DirectionalShadowPreference::PREFER_NATIVE,
                      HostRenderPlatform::MACOS),
          availability, unsupported),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupSelectionStatus::REJECTED_NO_VIABLE_PATH, false, false,
      "prefer-native accepted a nonexistent fallback");
}

void TestRequireNativeNeverFallsBack() {
  using RoR::DirectionalShadowPreference;
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererFrontendPreference;
  using RoR::RendererNativePreflightReadiness;
  using RoR::RendererStartupPath;
  using RoR::RendererStartupSelectionStatus;

  const RoR::RendererStartupBuildAvailability availability =
      MakeAllPathsAvailable();
  const RoR::RendererStartupRequest request =
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::REQUIRE_NATIVE,
                  HostRenderPlatform::MACOS);

  RoR::RendererNativeShadowPreflight preflight =
      MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.hardware_floor_met = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(request, availability, preflight), false,
      RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::HARDWARE_FLOOR_NOT_MET,
      RendererStartupSelectionStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE,
      false, false, "require-native silently degraded to PSSM");

  preflight = MakeEligiblePreflight(NativeRayTracingBackend::METAL);
  preflight.completed = false;
  RequireResult(
      RoR::ResolveRendererStartupPlan(request, availability, preflight), false,
      RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::PREFLIGHT_NOT_COMPLETED,
      RendererStartupSelectionStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE,
      false, false, "require-native accepted incomplete preflight");

  RequireResult(
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RendererFrontendPreference::LEGACY_ONLY,
                      DirectionalShadowPreference::REQUIRE_NATIVE,
                      HostRenderPlatform::MACOS),
          availability, MakeEligiblePreflight(NativeRayTracingBackend::METAL)),
      false, RendererStartupPath::NONE, NativeRayTracingBackend::NONE,
      RendererNativePreflightReadiness::FRONTEND_INCOMPATIBLE,
      RendererStartupSelectionStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE,
      false, false, "legacy-only claimed a required native path");
}

void TestLegacyOnlyPreferNativeStaysLegacy() {
  const RoR::RendererStartupPlanResult result =
      RoR::ResolveRendererStartupPlan(
          MakeRequest(RoR::RendererFrontendPreference::LEGACY_ONLY,
                      RoR::DirectionalShadowPreference::PREFER_NATIVE,
                      RoR::HostRenderPlatform::MACOS),
          MakeAllPathsAvailable(),
          MakeEligiblePreflight(RoR::NativeRayTracingBackend::METAL));
  RequireResult(
      result, true, RoR::RendererStartupPath::OGRE14_PSSM,
      RoR::NativeRayTracingBackend::NONE,
      RoR::RendererNativePreflightReadiness::FRONTEND_INCOMPATIBLE,
      RoR::RendererStartupSelectionStatus::FALLBACK_TO_OGRE14_PSSM, true,
      false, "legacy-only preference escaped to Ogre-Next native");
}

void TestCompleteStateSpaceMaintainsHardGates() {
  using RoR::DirectionalShadowPreference;
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::RendererFrontendPreference;
  using RoR::RendererNativePreflightSource;
  using RoR::RendererStartupPath;

  const RendererFrontendPreference frontends[] = {
      RendererFrontendPreference::LEGACY_ONLY,
      RendererFrontendPreference::OGRE_NEXT_PREFER,
      RendererFrontendPreference::OGRE_NEXT_REQUIRE,
  };
  const DirectionalShadowPreference shadows[] = {
      DirectionalShadowPreference::PSSM,
      DirectionalShadowPreference::PREFER_NATIVE,
      DirectionalShadowPreference::REQUIRE_NATIVE,
  };
  const HostRenderPlatform platforms[] = {
      HostRenderPlatform::UNKNOWN,
      HostRenderPlatform::MACOS,
      HostRenderPlatform::WINDOWS,
      HostRenderPlatform::LINUX,
  };
  const NativeRayTracingBackend backends[] = {
      NativeRayTracingBackend::NONE,
      NativeRayTracingBackend::METAL,
      NativeRayTracingBackend::DXR,
      NativeRayTracingBackend::VULKAN_KHR,
  };
  const RendererNativePreflightSource sources[] = {
      RendererNativePreflightSource::NONE,
      RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API,
  };

  for (const RendererFrontendPreference frontend : frontends) {
    for (const DirectionalShadowPreference shadow : shadows) {
      for (const HostRenderPlatform platform : platforms) {
        for (unsigned int availability_bits = 0U; availability_bits < 8U;
             ++availability_bits) {
          for (const NativeRayTracingBackend packaged_backend : backends) {
            RoR::RendererStartupBuildAvailability availability;
            availability.ogre14_frontend_available =
                (availability_bits & 1U) != 0U;
            availability.ogre_next_frontend_available =
                (availability_bits & 2U) != 0U;
            availability.ogre_next_pssm_available =
                (availability_bits & 4U) != 0U;
            availability.native_directional_shadow_backend =
                packaged_backend;

            for (const RendererNativePreflightSource source : sources) {
              for (const NativeRayTracingBackend observed_backend : backends) {
                for (unsigned int preflight_bits = 0U; preflight_bits < 64U;
                     ++preflight_bits) {
                  RoR::RendererNativeShadowPreflight preflight;
                  preflight.source = source;
                  preflight.backend = observed_backend;
                  preflight.completed = (preflight_bits & 1U) != 0U;
                  preflight.device_selected = (preflight_bits & 2U) != 0U;
                  preflight.device_identity =
                      (preflight_bits & 4U) != 0U ? 0x4321U : 0U;
                  preflight.api_supported = (preflight_bits & 8U) != 0U;
                  preflight.hardware_accelerated =
                      (preflight_bits & 16U) != 0U;
                  preflight.hardware_floor_met =
                      (preflight_bits & 32U) != 0U;

                  const RoR::RendererStartupPlanResult result =
                      RoR::ResolveRendererStartupPlan(
                          MakeRequest(frontend, shadow, platform), availability,
                          preflight);
                  Require(result.accepted ==
                              (result.effective_path != RendererStartupPath::NONE),
                          "accepted/path invariant failed in state space");
                  Require(
                      RoR::IsKnownRendererNativePreflightReadiness(
                          result.native_preflight_readiness) &&
                          RoR::IsKnownRendererStartupSelectionStatus(
                              result.status),
                      "planner emitted an unknown result enum");

                  if (frontend == RendererFrontendPreference::LEGACY_ONLY) {
                    Require(result.effective_path == RendererStartupPath::NONE ||
                                result.effective_path ==
                                    RendererStartupPath::OGRE14_PSSM,
                            "legacy-only escaped to Ogre-Next");
                  }
                  if (frontend ==
                      RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
                    Require(result.effective_path !=
                                RendererStartupPath::OGRE14_PSSM,
                            "Ogre-Next require crossed the frontend gate");
                  }
                  if (shadow == DirectionalShadowPreference::REQUIRE_NATIVE) {
                    Require(!result.accepted ||
                                result.effective_path ==
                                    RendererStartupPath::
                                        OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1,
                            "require-native selected a raster fallback");
                  }
                  if (shadow == DirectionalShadowPreference::PSSM) {
                    Require(result.effective_path !=
                                RendererStartupPath::
                                    OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1 &&
                                !result.used_shadow_fallback,
                            "PSSM request selected or fell back from native");
                  }

                  if (result.effective_path == RendererStartupPath::OGRE14_PSSM) {
                    Require(availability.ogre14_frontend_available,
                            "OGRE14 selected without an available path");
                  } else if (result.effective_path ==
                             RendererStartupPath::
                                 OGRE_NEXT_PSSM_3_CASCADE_V1) {
                    Require(availability.ogre_next_frontend_available &&
                                availability.ogre_next_pssm_available,
                            "Ogre-Next PSSM selected without an available path");
                  } else if (result.effective_path ==
                             RendererStartupPath::
                                 OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1) {
                    const NativeRayTracingBackend expected_backend =
                        RoR::ExpectedNativeRayTracingBackend(platform);
                    Require(
                        frontend != RendererFrontendPreference::LEGACY_ONLY &&
                            shadow != DirectionalShadowPreference::PSSM &&
                            availability.ogre_next_frontend_available &&
                            packaged_backend == expected_backend &&
                            expected_backend != NativeRayTracingBackend::NONE &&
                            source == RendererNativePreflightSource::
                                          CURRENT_PROCESS_NATIVE_API &&
                            observed_backend == expected_backend &&
                            preflight.completed && preflight.device_selected &&
                            preflight.device_identity != 0U &&
                            preflight.api_supported &&
                            preflight.hardware_accelerated &&
                            preflight.hardware_floor_met &&
                            result.effective_native_backend == expected_backend,
                        "native path crossed a preflight hard gate");
                  }

                  if (result.effective_path !=
                      RendererStartupPath::
                          OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1) {
                    Require(result.effective_native_backend ==
                                NativeRayTracingBackend::NONE,
                            "raster/rejected path retained a native backend");
                  }
                  if (result.used_frontend_fallback) {
                    Require(frontend ==
                                RendererFrontendPreference::OGRE_NEXT_PREFER &&
                                result.effective_path ==
                                    RendererStartupPath::OGRE14_PSSM,
                            "frontend fallback flag crossed its contract");
                  }
                  if (result.used_shadow_fallback) {
                    Require(shadow ==
                                DirectionalShadowPreference::PREFER_NATIVE,
                            "shadow fallback occurred without prefer-native");
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void TestInvalidVersionsAndEnumsFailClosed() {
  RoR::RendererStartupRequest request =
      MakeRequest(RoR::RendererFrontendPreference::OGRE_NEXT_PREFER,
                  RoR::DirectionalShadowPreference::PREFER_NATIVE,
                  RoR::HostRenderPlatform::MACOS);
  RoR::RendererStartupBuildAvailability availability = MakeAllPathsAvailable();
  RoR::RendererNativeShadowPreflight preflight =
      MakeEligiblePreflight(RoR::NativeRayTracingBackend::METAL);

  request.version = 0U;
  RequireInvalid(request, availability, preflight,
                 "unknown request version did not fail closed");
  request.version = RoR::kRendererStartupPlanContractVersion;

  availability.version = 0U;
  RequireInvalid(request, availability, preflight,
                 "unknown availability version did not fail closed");
  availability.version = RoR::kRendererStartupPlanContractVersion;

  availability.native_directional_shadow_backend =
      static_cast<RoR::NativeRayTracingBackend>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown packaged backend did not fail closed");
  availability.native_directional_shadow_backend =
      RoR::NativeRayTracingBackend::METAL;

  preflight.version = 0U;
  RequireInvalid(request, availability, preflight,
                 "unknown preflight version did not fail closed");
  preflight.version = RoR::kRendererStartupPlanContractVersion;

  request.frontend = static_cast<RoR::RendererFrontendPreference>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown frontend preference did not fail closed");
  request.frontend = RoR::RendererFrontendPreference::OGRE_NEXT_PREFER;

  request.directional_shadows =
      static_cast<RoR::DirectionalShadowPreference>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown shadow preference did not fail closed");
  request.directional_shadows =
      RoR::DirectionalShadowPreference::PREFER_NATIVE;

  request.host_platform = static_cast<RoR::HostRenderPlatform>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown host platform did not fail closed");
  request.host_platform = RoR::HostRenderPlatform::MACOS;

  preflight.source = static_cast<RoR::RendererNativePreflightSource>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown preflight source did not fail closed");
  preflight.source =
      RoR::RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API;

  preflight.backend = static_cast<RoR::NativeRayTracingBackend>(255);
  RequireInvalid(request, availability, preflight,
                 "unknown native backend did not fail closed");
}

void TestStableDiagnosticStrings() {
  Require(std::strcmp(RoR::ToString(
                          RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE),
                      "ogre-next-require") == 0,
          "frontend preference diagnostic changed");
  Require(std::strcmp(RoR::ToString(
                          RoR::DirectionalShadowPreference::REQUIRE_NATIVE),
                      "require-native") == 0,
          "shadow preference diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererNativePreflightSource::CURRENT_PROCESS_NATIVE_API),
              "current-process-native-api") == 0,
          "preflight provenance diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererNativePreflightReadiness::
                      DEVICE_IDENTITY_UNAVAILABLE),
              "device-identity-unavailable") == 0,
          "preflight readiness diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererStartupPath::
                      OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1),
              "ogre-next-native-directional-hard-shadow-v1") == 0,
          "startup path diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererStartupSelectionStatus::
                      REJECTED_NATIVE_REQUIRED_UNAVAILABLE),
              "rejected-native-required-unavailable") == 0,
          "startup status diagnostic changed");

  Require(std::strcmp(RoR::ToString(
                          static_cast<RoR::RendererFrontendPreference>(255)),
                      "invalid") == 0,
          "unknown frontend diagnostic did not fail closed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererNativePreflightReadiness>(255)),
              "invalid") == 0,
          "unknown readiness diagnostic did not fail closed");
  Require(std::strcmp(
              RoR::ToString(static_cast<RoR::RendererStartupPath>(255)),
              "invalid") == 0,
          "unknown path diagnostic did not fail closed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererStartupSelectionStatus>(255)),
              "invalid") == 0,
          "unknown status diagnostic did not fail closed");
}

} // namespace

int main() {
  TestEnumClassifiersRejectUnknownValues();
  TestDefaultRemainsOgre14Pssm();
  TestPssmFrontendPreferences();
  TestNativeEligibleOnEveryPlatform();
  TestEveryNativePreflightFactIsRequired();
  TestNativeAndFrontendFallbackPrecedence();
  TestRequireNativeNeverFallsBack();
  TestLegacyOnlyPreferNativeStaysLegacy();
  TestCompleteStateSpaceMaintainsHardGates();
  TestInvalidVersionsAndEnumsFailClosed();
  TestStableDiagnosticStrings();
  return EXIT_SUCCESS;
}
