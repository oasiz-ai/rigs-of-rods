/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererStartupHandoff.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer startup handoff test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::RendererStartupHandoffRequest
MakeRequest(RoR::RendererFrontendPreference frontend,
            RoR::DirectionalShadowPreference shadows,
            RoR::HostRenderPlatform platform) {
  RoR::RendererStartupHandoffRequest request;
  request.startup.frontend = frontend;
  request.startup.directional_shadows = shadows;
  request.startup.host_platform = platform;
  return request;
}

RoR::RendererStartupPackageAvailability
MakePackage(RoR::HostRenderPlatform platform, bool legacy = true,
            bool next = true, bool production_ready = true,
            bool pssm_admitted = true, bool native = true) {
  RoR::RendererStartupPackageAvailability availability;
  availability.package_platform = platform;
  availability.ogre14_child_present = legacy;
  availability.ogre_next_child_present = next;
  availability.ogre_next_child_production_ready = next && production_ready;
  availability.ogre_next_pssm_admitted = next && pssm_admitted;
  availability.native_directional_shadow_backend =
      next && native ? RoR::ExpectedNativeRayTracingBackend(platform)
                     : RoR::NativeRayTracingBackend::NONE;
  return availability;
}

RoR::RendererStartupBuildAvailability MakeSelectedChildAvailability(
    const RoR::RendererStartupPackageAvailability &package,
    RoR::RendererFrontendChild child) {
  RoR::RendererStartupBuildAvailability availability;
  availability.ogre14_frontend_available =
      child == RoR::RendererFrontendChild::OGRE14 &&
      package.ogre14_child_present;
  availability.ogre_next_frontend_available =
      child == RoR::RendererFrontendChild::OGRE_NEXT &&
      package.ogre_next_child_present &&
      package.ogre_next_child_production_ready;
  availability.ogre_next_pssm_available =
      availability.ogre_next_frontend_available &&
      package.ogre_next_pssm_admitted;
  availability.native_directional_shadow_backend =
      availability.ogre_next_frontend_available
          ? package.native_directional_shadow_backend
          : RoR::NativeRayTracingBackend::NONE;
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

void RequireResultCore(const RoR::RendererStartupHandoffResult &result,
                       bool accepted, RoR::RendererFrontendChild child,
                       RoR::RendererStartupHandoffStatus status,
                       bool resolve_in_child, bool native_preflight,
                       bool shadow_fallback_allowed, bool frontend_fallback,
                       bool shadow_fallback, const char *message) {
  Require(result.version == RoR::kRendererStartupHandoffContractVersion,
          message);
  Require(result.accepted == accepted, message);
  Require(result.child == child, message);
  Require(result.status == status, message);
  Require(result.child_must_resolve_startup_plan == resolve_in_child, message);
  Require(result.child_native_preflight_required == native_preflight, message);
  Require(result.child_shadow_fallback_allowed == shadow_fallback_allowed,
          message);
  Require(result.used_frontend_fallback == frontend_fallback, message);
  Require(result.used_shadow_fallback == shadow_fallback, message);
}

void RequireResult(const RoR::RendererStartupHandoffResult &result,
                   bool accepted, RoR::RendererFrontendChild child,
                   RoR::RendererStartupHandoffStatus status,
                   bool resolve_in_child, bool native_preflight,
                   bool shadow_fallback_allowed, bool frontend_fallback,
                   bool shadow_fallback, const char *message) {
  RequireResultCore(result, accepted, child, status, resolve_in_child,
                    native_preflight, shadow_fallback_allowed,
                    frontend_fallback, shadow_fallback, message);
  Require(!result.used_production_gate_fallback, message);
}

void RequireResult(const RoR::RendererStartupHandoffResult &result,
                   bool accepted, RoR::RendererFrontendChild child,
                   RoR::RendererStartupHandoffStatus status,
                   bool resolve_in_child, bool native_preflight,
                   bool shadow_fallback_allowed, bool frontend_fallback,
                   bool shadow_fallback, bool production_gate_fallback,
                   const char *message) {
  RequireResultCore(result, accepted, child, status, resolve_in_child,
                    native_preflight, shadow_fallback_allowed,
                    frontend_fallback, shadow_fallback, message);
  Require(result.used_production_gate_fallback == production_gate_fallback,
          message);
}

void RequireNoExecutableName(RoR::RendererStartupHandoffResult result,
                             const char *message) {
  Require(RoR::RendererFrontendChildExecutableName(result)[0] == '\0', message);
}

void TestClassifiersAndStrings() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const RoR::RendererFrontendChild child =
        static_cast<RoR::RendererFrontendChild>(value);
    Require(RoR::IsKnownRendererFrontendChild(child) == (value <= 2U),
            "child classifier accepted an unknown value");
    const RoR::RendererStartupHandoffStatus status =
        static_cast<RoR::RendererStartupHandoffStatus>(value);
    Require(RoR::IsKnownRendererStartupHandoffStatus(status) == (value <= 7U),
            "handoff status classifier accepted an unknown value");
  }
  Require(std::strcmp(RoR::ToString(RoR::RendererFrontendChild::OGRE14),
                      "ogre14") == 0,
          "legacy child string changed");
  Require(std::strcmp(RoR::ToString(RoR::RendererStartupHandoffStatus::
                                        REJECTED_NATIVE_REQUIRED_UNAVAILABLE),
                      "rejected-native-required-unavailable") == 0,
          "handoff status string changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererStartupHandoffStatus::
                      REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY),
              "rejected-ogre-next-child-not-production-ready") == 0,
          "production-gate status string changed");
  Require(
      std::strcmp(RoR::ToString(static_cast<RoR::RendererFrontendChild>(255U)),
                  "invalid") == 0,
      "unknown child string did not fail closed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererStartupHandoffStatus>(255U)),
              "invalid") == 0,
          "unknown handoff status string did not fail closed");
}

void TestDefaultAndExactChildNames() {
  RoR::RendererStartupHandoffRequest request;
  request.startup.host_platform = RoR::HostRenderPlatform::MACOS;
  const auto preferred = RoR::ResolveRendererStartupHandoff(
      request, MakePackage(RoR::HostRenderPlatform::MACOS));
  RequireResult(preferred, true, RoR::RendererFrontendChild::OGRE_NEXT,
                RoR::RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD,
                true, false, false, false, false,
                "default handoff changed away from the Ogre-Next child");

  const auto fallback = RoR::ResolveRendererStartupHandoff(
      request, MakePackage(RoR::HostRenderPlatform::MACOS, true, false));
  RequireResult(fallback, true, RoR::RendererFrontendChild::OGRE14,
                RoR::RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD,
                false, false, false, true, false,
                "default handoff did not preserve the legacy package fallback");

  const RoR::HostRenderPlatform unix_platforms[] = {
      RoR::HostRenderPlatform::MACOS,
      RoR::HostRenderPlatform::LINUX,
  };
  for (RoR::HostRenderPlatform platform : unix_platforms) {
    const auto legacy = RoR::ResolveRendererStartupHandoff(
        MakeRequest(RoR::RendererFrontendPreference::LEGACY_ONLY,
                    RoR::DirectionalShadowPreference::PSSM, platform),
        MakePackage(platform));
    const auto next = RoR::ResolveRendererStartupHandoff(
        MakeRequest(RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                    RoR::DirectionalShadowPreference::PSSM, platform),
        MakePackage(platform));
    Require(std::strcmp(RoR::RendererFrontendChildExecutableName(legacy),
                        "RoR-Ogre14") == 0,
            "Unix legacy child basename changed");
    Require(std::strcmp(RoR::RendererFrontendChildExecutableName(next),
                        "RoR-OgreNext") == 0,
            "Unix Ogre-Next child basename changed");
  }
  const auto windows_legacy = RoR::ResolveRendererStartupHandoff(
      MakeRequest(RoR::RendererFrontendPreference::LEGACY_ONLY,
                  RoR::DirectionalShadowPreference::PSSM,
                  RoR::HostRenderPlatform::WINDOWS),
      MakePackage(RoR::HostRenderPlatform::WINDOWS));
  const auto windows_next = RoR::ResolveRendererStartupHandoff(
      MakeRequest(RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                  RoR::DirectionalShadowPreference::PSSM,
                  RoR::HostRenderPlatform::WINDOWS),
      MakePackage(RoR::HostRenderPlatform::WINDOWS));
  Require(std::strcmp(RoR::RendererFrontendChildExecutableName(windows_legacy),
                      "RoR-Ogre14.exe") == 0,
          "Windows legacy child basename changed");
  Require(std::strcmp(RoR::RendererFrontendChildExecutableName(windows_next),
                      "RoR-OgreNext.exe") == 0,
          "Windows Ogre-Next child basename changed");

  RoR::RendererStartupHandoffResult malformed;
  Require(malformed.requested_frontend ==
                  RoR::RendererFrontendPreference::LEGACY_ONLY &&
              !malformed.accepted,
          "result-only handoff default claimed Ogre-Next intent");
  RequireNoExecutableName(malformed,
                          "rejected result produced an executable name");
  malformed = preferred;
  malformed.version += 1U;
  RequireNoExecutableName(
      malformed, "unknown result version produced an executable name");
  malformed = preferred;
  malformed.package_platform = RoR::HostRenderPlatform::UNKNOWN;
  RequireNoExecutableName(
      malformed,
      "unknown trusted package platform produced an executable name");
  malformed = preferred;
  malformed.status =
      RoR::RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST;
  RequireNoExecutableName(malformed,
                          "rejected status produced an executable name");

  malformed = preferred;
  malformed.status =
      RoR::RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD;
  RequireNoExecutableName(
      malformed, "Ogre-Next child accepted a legacy-fallback status");
  malformed = preferred;
  malformed.child = RoR::RendererFrontendChild::OGRE14;
  RequireNoExecutableName(
      malformed, "mutated legacy child retained Ogre-Next lifecycle flags");
  malformed = preferred;
  malformed.child_must_resolve_startup_plan = false;
  RequireNoExecutableName(
      malformed, "Ogre-Next child bypassed process-local startup planning");
  malformed = preferred;
  malformed.child_native_preflight_required = true;
  RequireNoExecutableName(malformed,
                          "PSSM request acquired a native preflight");
  malformed = preferred;
  malformed.child_shadow_fallback_allowed = true;
  RequireNoExecutableName(malformed,
                          "PSSM request acquired a shadow fallback");
  malformed = preferred;
  malformed.used_frontend_fallback = true;
  RequireNoExecutableName(malformed,
                          "Ogre-Next child claimed a frontend fallback");
  malformed = preferred;
  malformed.used_shadow_fallback = true;
  RequireNoExecutableName(malformed,
                          "PSSM request claimed a shadow fallback");
  malformed = preferred;
  malformed.used_production_gate_fallback = true;
  RequireNoExecutableName(malformed,
                          "Ogre-Next child claimed a production fallback");
  malformed = preferred;
  malformed.declared_native_backend = RoR::NativeRayTracingBackend::DXR;
  RequireNoExecutableName(malformed,
                          "macOS result accepted a DXR package identity");
  malformed = preferred;
  malformed.requested_frontend =
      RoR::RendererFrontendPreference::LEGACY_ONLY;
  RequireNoExecutableName(malformed,
                          "legacy-only intent launched an Ogre-Next child");
  malformed = preferred;
  malformed.requested_directional_shadows =
      RoR::DirectionalShadowPreference::PREFER_NATIVE;
  RequireNoExecutableName(
      malformed, "prefer-native intent bypassed its preflight contract");
  malformed = preferred;
  malformed.requested_frontend =
      static_cast<RoR::RendererFrontendPreference>(255U);
  RequireNoExecutableName(malformed,
                          "unknown frontend intent produced a child name");
  malformed = preferred;
  malformed.requested_directional_shadows =
      static_cast<RoR::DirectionalShadowPreference>(255U);
  RequireNoExecutableName(malformed,
                          "unknown shadow intent produced a child name");
  malformed = preferred;
  malformed.child = static_cast<RoR::RendererFrontendChild>(255U);
  RequireNoExecutableName(malformed,
                          "unknown child enum produced a child name");
  malformed = preferred;
  malformed.declared_native_backend =
      static_cast<RoR::NativeRayTracingBackend>(255U);
  RequireNoExecutableName(malformed,
                          "unknown native backend produced a child name");

  malformed = fallback;
  malformed.used_frontend_fallback = false;
  RequireNoExecutableName(
      malformed, "legacy fallback omitted its frontend-fallback lineage");
}

void TestPssmSelectionAndFrontendFallback() {
  using namespace RoR;
  const auto next = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::MACOS),
      MakePackage(HostRenderPlatform::MACOS));
  RequireResult(next, true, RendererFrontendChild::OGRE_NEXT,
                RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD, true,
                false, false, false, false,
                "preferred Ogre-Next child was not selected");

  const auto fallback = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::MACOS),
      MakePackage(HostRenderPlatform::MACOS, true, false));
  RequireResult(fallback, true, RendererFrontendChild::OGRE14,
                RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD, false,
                false, false, true, false,
                "preferred Ogre-Next child did not fall back to legacy");

  const auto required = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::MACOS),
      MakePackage(HostRenderPlatform::MACOS, true, false));
  RequireResult(required, false, RendererFrontendChild::NONE,
                RendererStartupHandoffStatus::
                    REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE,
                false, false, false, false, false,
                "required Ogre-Next request crossed into the legacy child");
}

void TestProbeChildCannotPassTheProductionGate() {
  using namespace RoR;
  const RendererStartupPackageAvailability probe_only =
      MakePackage(HostRenderPlatform::MACOS, true, true, false, true, true);
  RendererStartupHandoffRequest request =
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::MACOS);

  const RendererStartupHandoffResult fallback =
      ResolveRendererStartupHandoff(request, probe_only);
  RequireResult(fallback, true, RendererFrontendChild::OGRE14,
                RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD, false,
                false, false, true, false, true,
                "unready probe child displaced the production legacy child");
  Require(std::strcmp(RendererFrontendChildExecutableName(fallback),
                      "RoR-Ogre14") == 0,
          "production-gate fallback changed executable identity");

  request.startup.frontend = RendererFrontendPreference::OGRE_NEXT_REQUIRE;
  RequireResult(ResolveRendererStartupHandoff(request, probe_only), false,
                RendererFrontendChild::NONE,
                RendererStartupHandoffStatus::
                    REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY,
                false, false, false, false, false,
                "Ogre-Next-required request admitted a probe child");

  request.startup.frontend = RendererFrontendPreference::OGRE_NEXT_PREFER;
  request.startup.directional_shadows =
      DirectionalShadowPreference::REQUIRE_NATIVE;
  RequireResult(ResolveRendererStartupHandoff(request, probe_only), false,
                RendererFrontendChild::NONE,
                RendererStartupHandoffStatus::
                    REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY,
                false, false, false, false, false,
                "native-required request admitted a probe child");

  RendererStartupPackageAvailability no_legacy = probe_only;
  no_legacy.ogre14_child_present = false;
  request.startup.directional_shadows = DirectionalShadowPreference::PSSM;
  RequireResult(ResolveRendererStartupHandoff(request, no_legacy), false,
                RendererFrontendChild::NONE,
                RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD, false,
                false, false, false, false,
                "unready probe child became viable when legacy was absent");
}

void TestNativeRequestsResolveInsideEachPlatformChild() {
  using namespace RoR;
  const struct {
    HostRenderPlatform platform;
    NativeRayTracingBackend backend;
  } cases[] = {
      {HostRenderPlatform::MACOS, NativeRayTracingBackend::METAL},
      {HostRenderPlatform::WINDOWS, NativeRayTracingBackend::DXR},
      {HostRenderPlatform::LINUX, NativeRayTracingBackend::VULKAN_KHR},
  };
  for (const auto &test_case : cases) {
    const auto result = ResolveRendererStartupHandoff(
        MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                    DirectionalShadowPreference::REQUIRE_NATIVE,
                    test_case.platform),
        MakePackage(test_case.platform));
    RequireResult(result, true, RendererFrontendChild::OGRE_NEXT,
                  RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD, true,
                  true, false, false, false,
                  "native-required handoff did not select the platform child");
    Require(result.declared_native_backend == test_case.backend,
            "native-required handoff declared the wrong backend");
  }

  auto no_native = MakePackage(HostRenderPlatform::MACOS);
  no_native.native_directional_shadow_backend = NativeRayTracingBackend::NONE;
  const auto rejected = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                  DirectionalShadowPreference::REQUIRE_NATIVE,
                  HostRenderPlatform::MACOS),
      no_native);
  RequireResult(
      rejected, false, RendererFrontendChild::NONE,
      RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE, false,
      false, false, false, false,
      "native-required request launched a child without a backend");

  const auto preferred = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                  DirectionalShadowPreference::PREFER_NATIVE,
                  HostRenderPlatform::MACOS),
      no_native);
  RequireResult(preferred, true, RendererFrontendChild::OGRE_NEXT,
                RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD, true,
                false, true, false, true,
                "prefer-native did not preserve Ogre-Next PSSM fallback");

  const auto legacy_fallback = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PREFER_NATIVE,
                  HostRenderPlatform::MACOS),
      MakePackage(HostRenderPlatform::MACOS, true, false));
  RequireResult(legacy_fallback, true, RendererFrontendChild::OGRE14,
                RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD, false,
                false, false, true, true,
                "prefer-native lost the package-level legacy fallback");

  const auto impossible = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::LEGACY_ONLY,
                  DirectionalShadowPreference::REQUIRE_NATIVE,
                  HostRenderPlatform::MACOS),
      MakePackage(HostRenderPlatform::MACOS));
  RequireResult(
      impossible, false, RendererFrontendChild::NONE,
      RendererStartupHandoffStatus::REJECTED_NATIVE_REQUIRED_UNAVAILABLE, false,
      false, false, false, false,
      "legacy-only accepted a native-required request");
}

void TestInvalidAndInconsistentFactsFailClosed() {
  using namespace RoR;
  const RendererStartupHandoffRequest valid =
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::MACOS);
  const RendererStartupPackageAvailability available =
      MakePackage(HostRenderPlatform::MACOS);
  const auto require_invalid = [&](RendererStartupHandoffRequest request,
                                   RendererStartupPackageAvailability facts,
                                   const char *message) {
    const auto result = ResolveRendererStartupHandoff(request, facts);
    RequireResult(result, false, RendererFrontendChild::NONE,
                  RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST, false,
                  false, false, false, false, message);
  };

  RendererStartupHandoffRequest request = valid;
  request.version += 1U;
  require_invalid(request, available, "unknown handoff version was accepted");
  request = valid;
  request.startup.version += 1U;
  require_invalid(request, available, "unknown startup version was accepted");
  request = valid;
  request.startup.host_platform = HostRenderPlatform::UNKNOWN;
  require_invalid(request, available, "unknown host platform was accepted");
  request = valid;
  request.startup.frontend = static_cast<RendererFrontendPreference>(255U);
  require_invalid(request, available, "unknown frontend intent was accepted");

  RendererStartupPackageAvailability facts = available;
  facts.version += 1U;
  require_invalid(valid, facts, "unknown package version was accepted");
  facts = available;
  facts.package_platform = HostRenderPlatform::UNKNOWN;
  require_invalid(valid, facts, "unknown package platform was accepted");
  facts = available;
  facts.package_platform = HostRenderPlatform::WINDOWS;
  facts.native_directional_shadow_backend = NativeRayTracingBackend::DXR;
  require_invalid(valid, facts,
                  "request-controlled package platform was accepted");
  facts = available;
  facts.package_platform = static_cast<HostRenderPlatform>(255U);
  require_invalid(valid, facts, "unknown package platform enum was accepted");
  facts = available;
  facts.ogre_next_child_present = false;
  require_invalid(valid, facts,
                  "PSSM/backend facts survived a missing Ogre-Next child");
  facts = available;
  facts.ogre_next_child_present = false;
  facts.ogre_next_child_production_ready = false;
  facts.ogre_next_pssm_admitted = false;
  require_invalid(valid, facts,
                  "native backend survived a missing Ogre-Next child");
  facts = available;
  facts.ogre_next_pssm_admitted = false;
  require_invalid(valid, facts,
                  "production readiness survived missing PSSM admission");
  facts = available;
  facts.native_directional_shadow_backend = NativeRayTracingBackend::VULKAN_KHR;
  require_invalid(valid, facts,
                  "cross-platform native backend mismatch was accepted");
  facts = available;
  facts.native_directional_shadow_backend =
      static_cast<NativeRayTracingBackend>(255U);
  require_invalid(valid, facts, "unknown package backend was accepted");
}

void TestNoViableChildFailsClosed() {
  using namespace RoR;
  const auto result = ResolveRendererStartupHandoff(
      MakeRequest(RendererFrontendPreference::OGRE_NEXT_PREFER,
                  DirectionalShadowPreference::PSSM, HostRenderPlatform::LINUX),
      MakePackage(HostRenderPlatform::LINUX, false, false));
  RequireResult(result, false, RendererFrontendChild::NONE,
                RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD, false,
                false, false, false, false,
                "empty package selected a renderer child");
}

void TestCartesianPackageFactsAndHardGatePrecedence() {
  using namespace RoR;
  const HostRenderPlatform platforms[] = {
      HostRenderPlatform::MACOS,
      HostRenderPlatform::WINDOWS,
      HostRenderPlatform::LINUX,
  };
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

  for (HostRenderPlatform platform : platforms) {
    const NativeRayTracingBackend expected_backend =
        ExpectedNativeRayTracingBackend(platform);
    const NativeRayTracingBackend wrong_backend =
        platform == HostRenderPlatform::WINDOWS ? NativeRayTracingBackend::METAL
                                                : NativeRayTracingBackend::DXR;
    const NativeRayTracingBackend backends[] = {
        NativeRayTracingBackend::NONE,
        expected_backend,
        wrong_backend,
    };
    for (RendererFrontendPreference frontend : frontends) {
      for (DirectionalShadowPreference shadow : shadows) {
        for (unsigned int bits = 0U; bits < 16U; ++bits) {
          const bool legacy = (bits & 1U) != 0U;
          const bool next = (bits & 2U) != 0U;
          const bool production_ready = (bits & 4U) != 0U;
          const bool pssm_admitted = (bits & 8U) != 0U;
          for (NativeRayTracingBackend backend : backends) {
            RendererStartupPackageAvailability package;
            package.package_platform = platform;
            package.ogre14_child_present = legacy;
            package.ogre_next_child_present = next;
            package.ogre_next_child_production_ready = production_ready;
            package.ogre_next_pssm_admitted = pssm_admitted;
            package.native_directional_shadow_backend = backend;

            const RendererStartupHandoffRequest request =
                MakeRequest(frontend, shadow, platform);
            const RendererStartupHandoffResult result =
                ResolveRendererStartupHandoff(request, package);
            const bool facts_valid =
                (next || (!production_ready && !pssm_admitted &&
                          backend == NativeRayTracingBackend::NONE)) &&
                (!production_ready || pssm_admitted) &&
                (backend == NativeRayTracingBackend::NONE ||
                 backend == expected_backend);
            if (!facts_valid) {
              RequireResult(
                  result, false, RendererFrontendChild::NONE,
                  RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST, false,
                  false, false, false, false,
                  "inconsistent package facts escaped fail-closed validation");
              continue;
            }

            const bool admitted = next && production_ready && pssm_admitted;
            bool accepted = false;
            RendererFrontendChild child = RendererFrontendChild::NONE;
            RendererStartupHandoffStatus status =
                RendererStartupHandoffStatus::REJECTED_INVALID_REQUEST;

            if (shadow == DirectionalShadowPreference::REQUIRE_NATIVE) {
              if (frontend == RendererFrontendPreference::LEGACY_ONLY) {
                status = RendererStartupHandoffStatus::
                    REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
              } else if (!next) {
                status =
                    frontend == RendererFrontendPreference::OGRE_NEXT_REQUIRE
                        ? RendererStartupHandoffStatus::
                              REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE
                        : RendererStartupHandoffStatus::
                              REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
              } else if (!admitted) {
                status = RendererStartupHandoffStatus::
                    REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY;
              } else if (backend == NativeRayTracingBackend::NONE) {
                status = RendererStartupHandoffStatus::
                    REJECTED_NATIVE_REQUIRED_UNAVAILABLE;
              } else {
                accepted = true;
                child = RendererFrontendChild::OGRE_NEXT;
                status = RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD;
              }
            } else if (frontend == RendererFrontendPreference::LEGACY_ONLY) {
              if (legacy) {
                accepted = true;
                child = RendererFrontendChild::OGRE14;
                status =
                    shadow == DirectionalShadowPreference::PREFER_NATIVE
                        ? RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD
                        : RendererStartupHandoffStatus::
                              SELECTED_REQUESTED_CHILD;
              } else {
                status = RendererStartupHandoffStatus::
                    REJECTED_REQUIRED_LEGACY_CHILD_UNAVAILABLE;
              }
            } else if (admitted) {
              accepted = true;
              child = RendererFrontendChild::OGRE_NEXT;
              status = RendererStartupHandoffStatus::SELECTED_REQUESTED_CHILD;
            } else if (frontend ==
                       RendererFrontendPreference::OGRE_NEXT_REQUIRE) {
              status = next ? RendererStartupHandoffStatus::
                                  REJECTED_OGRE_NEXT_CHILD_NOT_PRODUCTION_READY
                            : RendererStartupHandoffStatus::
                                  REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE;
            } else if (legacy) {
              accepted = true;
              child = RendererFrontendChild::OGRE14;
              status = RendererStartupHandoffStatus::FALLBACK_TO_OGRE14_CHILD;
            } else {
              status = RendererStartupHandoffStatus::REJECTED_NO_VIABLE_CHILD;
            }

            const bool resolve_in_child =
                accepted && child == RendererFrontendChild::OGRE_NEXT;
            const bool native_preflight =
                resolve_in_child &&
                shadow != DirectionalShadowPreference::PSSM &&
                backend != NativeRayTracingBackend::NONE;
            const bool shadow_fallback_allowed =
                resolve_in_child &&
                shadow == DirectionalShadowPreference::PREFER_NATIVE;
            const bool frontend_fallback =
                accepted && child == RendererFrontendChild::OGRE14 &&
                frontend == RendererFrontendPreference::OGRE_NEXT_PREFER;
            const bool shadow_fallback =
                accepted &&
                shadow == DirectionalShadowPreference::PREFER_NATIVE &&
                (child == RendererFrontendChild::OGRE14 ||
                 backend == NativeRayTracingBackend::NONE);
            const bool production_gate_fallback =
                frontend_fallback && next && !production_ready;
            RequireResult(
                result, accepted, child, status, resolve_in_child,
                native_preflight, shadow_fallback_allowed, frontend_fallback,
                shadow_fallback, production_gate_fallback,
                "Cartesian package-fact decision diverged from the oracle");
            Require(result.package_platform ==
                        (accepted ? platform : HostRenderPlatform::UNKNOWN),
                    "trusted package platform leaked into a rejected result");
            Require(result.declared_native_backend ==
                        (resolve_in_child ? backend
                                          : NativeRayTracingBackend::NONE),
                    "selected child declared the wrong native backend");
            Require((RendererFrontendChildExecutableName(result)[0] != '\0') ==
                        accepted,
                    "executable naming disagreed with acceptance");
            if (accepted) {
              const RendererNativeShadowPreflight preflight =
                  result.child_native_preflight_required
                      ? MakeEligiblePreflight(
                            result.declared_native_backend)
                      : RendererNativeShadowPreflight{};
              const RendererStartupPlanResult child_plan =
                  ResolveRendererStartupPlan(
                      request.startup,
                      MakeSelectedChildAvailability(package, result.child),
                      preflight);
              Require(child_plan.accepted,
                      "accepted package handoff failed in the selected child");
              if (result.child == RendererFrontendChild::OGRE14) {
                Require(child_plan.effective_path ==
                            RendererStartupPath::OGRE14_PSSM,
                        "Cartesian legacy handoff crossed renderer ABIs");
              } else {
                const RendererStartupPath expected_path =
                    shadow != DirectionalShadowPreference::PSSM &&
                            backend != NativeRayTracingBackend::NONE
                        ? RendererStartupPath::
                              OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1
                        : RendererStartupPath::
                              OGRE_NEXT_PSSM_3_CASCADE_V1;
                Require(child_plan.effective_path == expected_path,
                        "Cartesian Ogre-Next handoff and child plan diverged");
              }
            }
          }
        }
      }
    }
  }
}

void TestHandoffAndChildPlanStayOnOneFrontend() {
  using namespace RoR;
  const HostRenderPlatform platforms[] = {
      HostRenderPlatform::MACOS,
      HostRenderPlatform::WINDOWS,
      HostRenderPlatform::LINUX,
  };
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

  for (HostRenderPlatform platform : platforms) {
    const RendererStartupPackageAvailability package = MakePackage(platform);
    for (RendererFrontendPreference frontend : frontends) {
      for (DirectionalShadowPreference shadow : shadows) {
        const RendererStartupHandoffRequest request =
            MakeRequest(frontend, shadow, platform);
        const RendererStartupHandoffResult handoff =
            ResolveRendererStartupHandoff(request, package);
        const RendererNativeShadowPreflight preflight =
            shadow == DirectionalShadowPreference::PSSM
                ? RendererNativeShadowPreflight{}
                : MakeEligiblePreflight(
                      package.native_directional_shadow_backend);
        const RendererStartupPlanResult child_plan = ResolveRendererStartupPlan(
            request.startup,
            MakeSelectedChildAvailability(package, handoff.child), preflight);

        if (!handoff.accepted) {
          Require(!child_plan.accepted,
                  "rejected handoff became viable inside a child");
          continue;
        }
        Require(child_plan.accepted,
                "selected handoff was rejected by an eligible child plan");
        if (handoff.child == RendererFrontendChild::OGRE14) {
          Require(child_plan.effective_path == RendererStartupPath::OGRE14_PSSM,
                  "legacy handoff crossed into the Ogre-Next ABI");
        } else {
          Require(child_plan.effective_path ==
                          RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1 ||
                      child_plan.effective_path ==
                          RendererStartupPath::
                              OGRE_NEXT_NATIVE_DIRECTIONAL_HARD_SHADOW_V1,
                  "Ogre-Next handoff crossed into the legacy ABI");
        }
      }
    }

    RendererStartupPackageAvailability no_native = package;
    no_native.native_directional_shadow_backend = NativeRayTracingBackend::NONE;
    const RendererStartupHandoffRequest preferred =
        MakeRequest(RendererFrontendPreference::OGRE_NEXT_REQUIRE,
                    DirectionalShadowPreference::PREFER_NATIVE, platform);
    const RendererStartupHandoffResult handoff =
        ResolveRendererStartupHandoff(preferred, no_native);
    const RendererStartupPlanResult child_plan = ResolveRendererStartupPlan(
        preferred.startup,
        MakeSelectedChildAvailability(no_native, handoff.child),
        RendererNativeShadowPreflight{});
    Require(handoff.accepted &&
                handoff.child == RendererFrontendChild::OGRE_NEXT &&
                child_plan.accepted &&
                child_plan.effective_path ==
                    RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
            "prefer-native did not stay inside the Ogre-Next PSSM fallback");
  }
}

} // namespace

int main() {
  TestClassifiersAndStrings();
  TestDefaultAndExactChildNames();
  TestPssmSelectionAndFrontendFallback();
  TestProbeChildCannotPassTheProductionGate();
  TestNativeRequestsResolveInsideEachPlatformChild();
  TestInvalidAndInconsistentFactsFailClosed();
  TestNoViableChildFailsClosed();
  TestCartesianPackageFactsAndHardGatePrecedence();
  TestHandoffAndChildPlanStayOnOneFrontend();
  return EXIT_SUCCESS;
}
