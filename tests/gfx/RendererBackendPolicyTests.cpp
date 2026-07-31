/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBackendPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer backend policy test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::RendererRuntimeCapabilities MakeOgre14Ready() {
  RoR::RendererRuntimeCapabilities capabilities;
  capabilities.ogre14_compiled = true;
  capabilities.ogre14_raster_ready = true;
  return capabilities;
}

RoR::RendererRuntimeCapabilities
MakeOgreNextReady(RoR::NativeRayTracingBackend backend) {
  RoR::RendererRuntimeCapabilities capabilities;
  capabilities.ogre_next_compiled = true;
  capabilities.ogre_next_raster_ready = true;
  capabilities.native_rt_backend = backend;
  capabilities.native_rt_backend_compiled = true;
  capabilities.native_rt_api_supported = true;
  capabilities.native_rt_hardware_accelerated = true;
  capabilities.native_rt_probe_passed = true;
  capabilities.native_rt_scene_interop_ready = true;
  return capabilities;
}

RoR::RendererSelectionRequest MakeRequest(RoR::RendererFrontend frontend,
                                          RoR::RayTracingMode ray_tracing_mode,
                                          RoR::HostRenderPlatform platform) {
  RoR::RendererSelectionRequest request;
  request.requested_frontend = frontend;
  request.ray_tracing_mode = ray_tracing_mode;
  request.host_platform = platform;
  return request;
}

void RequireResult(const RoR::RendererSelectionResult &result, bool accepted,
                   RoR::RendererFrontend effective_frontend,
                   RoR::NativeRayTracingBackend effective_rt_backend,
                   RoR::NativeRayTracingReadiness readiness,
                   RoR::RendererSelectionStatus status,
                   bool used_raster_fallback, const char *message) {
  Require(result.accepted == accepted, message);
  Require(result.effective_frontend == effective_frontend, message);
  Require(result.effective_native_rt_backend == effective_rt_backend, message);
  Require(result.native_rt_readiness == readiness, message);
  Require(result.status == status, message);
  Require(result.used_raster_fallback == used_raster_fallback, message);
}

void TestEnumClassifiersRejectUnknownValues() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0; value <= maximum; ++value) {
    const RoR::RendererFrontend frontend =
        static_cast<RoR::RendererFrontend>(value);
    const bool expected_frontend = frontend == RoR::RendererFrontend::NONE ||
                                   frontend == RoR::RendererFrontend::OGRE14 ||
                                   frontend == RoR::RendererFrontend::OGRE_NEXT;
    Require(RoR::IsKnownRendererFrontend(frontend) == expected_frontend,
            "frontend classifier accepted an unknown value");

    const RoR::RayTracingMode mode = static_cast<RoR::RayTracingMode>(value);
    const bool expected_mode = mode == RoR::RayTracingMode::DISABLED ||
                               mode == RoR::RayTracingMode::PREFER_HARDWARE ||
                               mode == RoR::RayTracingMode::REQUIRE_HARDWARE;
    Require(RoR::IsKnownRayTracingMode(mode) == expected_mode,
            "RT mode classifier accepted an unknown value");

    const RoR::HostRenderPlatform platform =
        static_cast<RoR::HostRenderPlatform>(value);
    const bool expected_platform =
        platform == RoR::HostRenderPlatform::UNKNOWN ||
        platform == RoR::HostRenderPlatform::MACOS ||
        platform == RoR::HostRenderPlatform::WINDOWS ||
        platform == RoR::HostRenderPlatform::LINUX;
    Require(RoR::IsKnownHostRenderPlatform(platform) == expected_platform,
            "platform classifier accepted an unknown value");

    const RoR::NativeRayTracingBackend backend =
        static_cast<RoR::NativeRayTracingBackend>(value);
    const bool expected_backend =
        backend == RoR::NativeRayTracingBackend::NONE ||
        backend == RoR::NativeRayTracingBackend::METAL ||
        backend == RoR::NativeRayTracingBackend::DXR ||
        backend == RoR::NativeRayTracingBackend::VULKAN_KHR;
    Require(RoR::IsKnownNativeRayTracingBackend(backend) == expected_backend,
            "native backend classifier accepted an unknown value");
  }
}

void TestPlatformBackendMapping() {
  Require(
      RoR::ExpectedNativeRayTracingBackend(RoR::HostRenderPlatform::UNKNOWN) ==
          RoR::NativeRayTracingBackend::NONE,
      "unknown platform acquired an RT backend");
  Require(
      RoR::ExpectedNativeRayTracingBackend(RoR::HostRenderPlatform::MACOS) ==
          RoR::NativeRayTracingBackend::METAL,
      "macOS did not map to Metal");
  Require(
      RoR::ExpectedNativeRayTracingBackend(RoR::HostRenderPlatform::WINDOWS) ==
          RoR::NativeRayTracingBackend::DXR,
      "Windows did not map to DXR");
  Require(
      RoR::ExpectedNativeRayTracingBackend(RoR::HostRenderPlatform::LINUX) ==
          RoR::NativeRayTracingBackend::VULKAN_KHR,
      "Linux did not map to Vulkan KHR");
  Require(RoR::ExpectedNativeRayTracingBackend(
              static_cast<RoR::HostRenderPlatform>(255)) ==
              RoR::NativeRayTracingBackend::NONE,
          "unknown platform enum acquired an RT backend");
}

void TestOgre14RemainsDefaultRasterSelection() {
  const RoR::RendererSelectionRequest request;
  const RoR::RendererSelectionResult result =
      RoR::ResolveRendererBackendPolicy(request, MakeOgre14Ready());

  Require(result.requested_frontend == RoR::RendererFrontend::OGRE14,
          "default request changed away from OGRE14");
  Require(result.requested_ray_tracing_mode == RoR::RayTracingMode::DISABLED,
          "default request enabled RT");
  RequireResult(result, true, RoR::RendererFrontend::OGRE14,
                RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::SELECTED_RASTER, false,
                "default OGRE14 raster selection changed");
}

void TestDisabledRtIgnoresRtCapabilityNoise() {
  RoR::RendererRuntimeCapabilities capabilities = MakeOgre14Ready();
  capabilities.native_rt_backend = RoR::NativeRayTracingBackend::VULKAN_KHR;
  capabilities.native_rt_backend_compiled = true;
  capabilities.native_rt_api_supported = true;
  capabilities.native_rt_hardware_accelerated = true;
  capabilities.native_rt_probe_passed = true;
  capabilities.native_rt_scene_interop_ready = true;

  const RoR::RendererSelectionResult result = RoR::ResolveRendererBackendPolicy(
      MakeRequest(RoR::RendererFrontend::OGRE14, RoR::RayTracingMode::DISABLED,
                  RoR::HostRenderPlatform::MACOS),
      capabilities);
  RequireResult(result, true, RoR::RendererFrontend::OGRE14,
                RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::SELECTED_RASTER, false,
                "disabled RT depended on unrelated capability bits");

  capabilities.ogre14_raster_ready = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(
                    MakeRequest(RoR::RendererFrontend::OGRE14,
                                RoR::RayTracingMode::DISABLED,
                                RoR::HostRenderPlatform::MACOS),
                    capabilities),
                false, RoR::RendererFrontend::NONE,
                RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE,
                false, "disabled RT selected an unready raster frontend");
}

void TestNativeRtRequiresEveryProof() {
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::NativeRayTracingReadiness;
  using RoR::RayTracingMode;
  using RoR::RendererFrontend;
  using RoR::RendererSelectionStatus;

  const RoR::RendererSelectionRequest request =
      MakeRequest(RendererFrontend::OGRE_NEXT, RayTracingMode::REQUIRE_HARDWARE,
                  HostRenderPlatform::MACOS);

  RoR::RendererRuntimeCapabilities capabilities =
      MakeOgreNextReady(NativeRayTracingBackend::METAL);
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), true,
                RendererFrontend::OGRE_NEXT, NativeRayTracingBackend::METAL,
                NativeRayTracingReadiness::READY,
                RendererSelectionStatus::SELECTED_NATIVE_RT, false,
                "fully proven Metal RT was not selected");

  capabilities.ogre_next_raster_ready = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::RASTER_FRONTEND_NOT_READY,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without its raster frontend");

  capabilities.ogre_next_raster_ready = true;
  capabilities.native_rt_scene_interop_ready = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::SCENE_INTEROP_NOT_READY,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without scene interop");

  capabilities.native_rt_probe_passed = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::PROBE_NOT_PASSED,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without dispatch/readback proof");

  capabilities.native_rt_hardware_accelerated = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::HARDWARE_ACCELERATION_UNAVAILABLE,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without accepted hardware acceleration");

  capabilities.native_rt_api_supported = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::API_UNSUPPORTED,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without native API support");

  capabilities.native_rt_backend_compiled = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::BACKEND_NOT_COMPILED,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "RT selected without a compiled backend");

  capabilities.native_rt_backend = NativeRayTracingBackend::VULKAN_KHR;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RendererFrontend::NONE, NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::BACKEND_MISMATCH,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "macOS selected a non-Metal backend");
}

void TestPreferFallsBackOnlyToReadyRaster() {
  RoR::RendererRuntimeCapabilities capabilities =
      MakeOgreNextReady(RoR::NativeRayTracingBackend::VULKAN_KHR);
  capabilities.native_rt_probe_passed = false;

  const RoR::RendererSelectionRequest request = MakeRequest(
      RoR::RendererFrontend::OGRE_NEXT, RoR::RayTracingMode::PREFER_HARDWARE,
      RoR::HostRenderPlatform::LINUX);

  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), true,
                RoR::RendererFrontend::OGRE_NEXT,
                RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::PROBE_NOT_PASSED,
                RoR::RendererSelectionStatus::FALLBACK_RASTER, true,
                "prefer did not use the ready raster fallback");

  capabilities.ogre_next_raster_ready = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RoR::RendererFrontend::NONE, RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::RASTER_FRONTEND_NOT_READY,
                RoR::RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE,
                false, "prefer accepted an unready raster fallback");
}

void TestEveryPlatformSelectsOnlyItsNativeBackend() {
  using RoR::HostRenderPlatform;
  using RoR::NativeRayTracingBackend;
  using RoR::NativeRayTracingReadiness;
  using RoR::RayTracingMode;
  using RoR::RendererFrontend;
  using RoR::RendererSelectionStatus;

  const struct {
    HostRenderPlatform platform;
    NativeRayTracingBackend backend;
  } cases[] = {
      {HostRenderPlatform::MACOS, NativeRayTracingBackend::METAL},
      {HostRenderPlatform::WINDOWS, NativeRayTracingBackend::DXR},
      {HostRenderPlatform::LINUX, NativeRayTracingBackend::VULKAN_KHR},
  };

  for (const auto &test_case : cases) {
    const RoR::RendererSelectionResult result =
        RoR::ResolveRendererBackendPolicy(
            MakeRequest(RendererFrontend::OGRE_NEXT,
                        RayTracingMode::REQUIRE_HARDWARE, test_case.platform),
            MakeOgreNextReady(test_case.backend));
    RequireResult(result, true, RendererFrontend::OGRE_NEXT, test_case.backend,
                  NativeRayTracingReadiness::READY,
                  RendererSelectionStatus::SELECTED_NATIVE_RT, false,
                  "platform did not select only its native RT backend");
  }

  const RoR::RendererSelectionResult unsupported =
      RoR::ResolveRendererBackendPolicy(
          MakeRequest(RendererFrontend::OGRE_NEXT,
                      RayTracingMode::REQUIRE_HARDWARE,
                      HostRenderPlatform::UNKNOWN),
          MakeOgreNextReady(NativeRayTracingBackend::METAL));
  RequireResult(unsupported, false, RendererFrontend::NONE,
                NativeRayTracingBackend::NONE,
                NativeRayTracingReadiness::PLATFORM_UNSUPPORTED,
                RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
                "unknown platform acquired native RT");
}

void TestOgre14CannotClaimNativeRt() {
  RoR::RendererRuntimeCapabilities capabilities = MakeOgre14Ready();
  capabilities.native_rt_backend = RoR::NativeRayTracingBackend::METAL;
  capabilities.native_rt_backend_compiled = true;
  capabilities.native_rt_api_supported = true;
  capabilities.native_rt_hardware_accelerated = true;
  capabilities.native_rt_probe_passed = true;
  capabilities.native_rt_scene_interop_ready = true;

  const RoR::RendererSelectionRequest require_request = MakeRequest(
      RoR::RendererFrontend::OGRE14, RoR::RayTracingMode::REQUIRE_HARDWARE,
      RoR::HostRenderPlatform::MACOS);
  RequireResult(
      RoR::ResolveRendererBackendPolicy(require_request, capabilities), false,
      RoR::RendererFrontend::NONE, RoR::NativeRayTracingBackend::NONE,
      RoR::NativeRayTracingReadiness::FRONTEND_INCOMPATIBLE,
      RoR::RendererSelectionStatus::REJECTED_RT_REQUIRED, false,
      "OGRE14 claimed a native RT backend");

  RoR::RendererSelectionRequest prefer_request = require_request;
  prefer_request.ray_tracing_mode = RoR::RayTracingMode::PREFER_HARDWARE;
  RequireResult(RoR::ResolveRendererBackendPolicy(prefer_request, capabilities),
                true, RoR::RendererFrontend::OGRE14,
                RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::FRONTEND_INCOMPATIBLE,
                RoR::RendererSelectionStatus::FALLBACK_RASTER, true,
                "OGRE14 prefer mode did not remain raster-only");
}

void TestInvalidAndUnavailableRequestsFailClosed() {
  RoR::RendererSelectionRequest request;
  request.requested_frontend = static_cast<RoR::RendererFrontend>(255);
  request.ray_tracing_mode = static_cast<RoR::RayTracingMode>(255);
  request.host_platform = static_cast<RoR::HostRenderPlatform>(255);

  RoR::RendererRuntimeCapabilities capabilities =
      MakeOgreNextReady(RoR::NativeRayTracingBackend::METAL);
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RoR::RendererFrontend::NONE, RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::REJECTED_INVALID_REQUEST, false,
                "unknown request values did not fail closed");

  request = MakeRequest(RoR::RendererFrontend::OGRE_NEXT,
                        RoR::RayTracingMode::DISABLED,
                        RoR::HostRenderPlatform::MACOS);
  capabilities.ogre_next_compiled = false;
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RoR::RendererFrontend::NONE, RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::REJECTED_FRONTEND_UNAVAILABLE,
                false, "uncompiled frontend was selected");

  capabilities.ogre_next_compiled = true;
  capabilities.native_rt_backend =
      static_cast<RoR::NativeRayTracingBackend>(255);
  RequireResult(RoR::ResolveRendererBackendPolicy(request, capabilities), false,
                RoR::RendererFrontend::NONE, RoR::NativeRayTracingBackend::NONE,
                RoR::NativeRayTracingReadiness::NOT_REQUESTED,
                RoR::RendererSelectionStatus::REJECTED_INVALID_REQUEST, false,
                "unknown capability backend did not fail closed");
}

void TestStableDiagnosticStrings() {
  Require(std::strcmp(RoR::ToString(RoR::RendererFrontend::OGRE_NEXT),
                      "ogre-next") == 0,
          "frontend diagnostic changed");
  Require(std::strcmp(RoR::ToString(RoR::RayTracingMode::REQUIRE_HARDWARE),
                      "require-hardware") == 0,
          "RT mode diagnostic changed");
  Require(std::strcmp(RoR::ToString(RoR::HostRenderPlatform::LINUX), "linux") ==
              0,
          "platform diagnostic changed");
  Require(std::strcmp(RoR::ToString(RoR::NativeRayTracingBackend::VULKAN_KHR),
                      "vulkan-khr") == 0,
          "backend diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(RoR::NativeRayTracingReadiness::PROBE_NOT_PASSED),
              "probe-not-passed") == 0,
          "readiness diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::NativeRayTracingReadiness::RASTER_FRONTEND_NOT_READY),
              "raster-frontend-not-ready") == 0,
          "raster readiness diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(RoR::RendererSelectionStatus::REJECTED_RT_REQUIRED),
              "rejected-rt-required") == 0,
          "selection diagnostic changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE),
              "rejected-raster-unavailable") == 0,
          "raster selection diagnostic changed");

  Require(
      std::strcmp(RoR::ToString(static_cast<RoR::RendererSelectionStatus>(255)),
                  "invalid") == 0,
      "unknown diagnostic did not fail closed");
}

} // namespace

int main() {
  TestEnumClassifiersRejectUnknownValues();
  TestPlatformBackendMapping();
  TestOgre14RemainsDefaultRasterSelection();
  TestDisabledRtIgnoresRtCapabilityNoise();
  TestNativeRtRequiresEveryProof();
  TestPreferFallsBackOnlyToReadyRaster();
  TestEveryPlatformSelectsOnlyItsNativeBackend();
  TestOgre14CannotClaimNativeRt();
  TestInvalidAndUnavailableRequestsFailClosed();
  TestStableDiagnosticStrings();

  std::cout << "renderer backend policy tests passed\n";
  return EXIT_SUCCESS;
}
