/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free renderer frontend and native ray-tracing policy.

#pragma once

#include <cstdint>

namespace RoR {

/// Scene-renderer frontend selected by the application.
///
/// OGRE14 remains the default until an Ogre-Next runtime has passed the
/// compatibility and visual acceptance gates. NONE is result-only.
enum class RendererFrontend : std::uint8_t {
  NONE = 0,
  OGRE14 = 1,
  OGRE_NEXT = 2,
};

/// User intent for hardware-accelerated native ray tracing.
enum class RayTracingMode : std::uint8_t {
  DISABLED = 0,
  PREFER_HARDWARE = 1,
  REQUIRE_HARDWARE = 2,
};

/// Normalized host platform used to select one native API.
enum class HostRenderPlatform : std::uint8_t {
  UNKNOWN = 0,
  MACOS = 1,
  WINDOWS = 2,
  LINUX = 3,
};

/// Native API contract for the selected platform.
enum class NativeRayTracingBackend : std::uint8_t {
  NONE = 0,
  METAL = 1,
  DXR = 2,
  VULKAN_KHR = 3,
};

/// Readiness of the native RT path, independent of fallback selection.
///
/// Precedence is stable and follows the order below. A backend is READY only
/// after a real API dispatch/readback probe and scene-resource interop have
/// passed; an API symbol or extension bit by itself is not sufficient.
enum class NativeRayTracingReadiness : std::uint8_t {
  NOT_REQUESTED = 0,
  READY = 1,
  FRONTEND_INCOMPATIBLE = 2,
  RASTER_FRONTEND_NOT_READY = 3,
  PLATFORM_UNSUPPORTED = 4,
  BACKEND_MISMATCH = 5,
  BACKEND_NOT_COMPILED = 6,
  API_UNSUPPORTED = 7,
  HARDWARE_ACCELERATION_UNAVAILABLE = 8,
  PROBE_NOT_PASSED = 9,
  SCENE_INTEROP_NOT_READY = 10,
};

/// Final requested-to-effective renderer decision.
enum class RendererSelectionStatus : std::uint8_t {
  SELECTED_RASTER = 0,
  SELECTED_NATIVE_RT = 1,
  FALLBACK_RASTER = 2,
  REJECTED_INVALID_REQUEST = 3,
  REJECTED_FRONTEND_UNAVAILABLE = 4,
  REJECTED_RT_REQUIRED = 5,
  REJECTED_RASTER_UNAVAILABLE = 6,
};

struct RendererSelectionRequest {
  RendererFrontend requested_frontend = RendererFrontend::OGRE14;
  RayTracingMode ray_tracing_mode = RayTracingMode::DISABLED;
  HostRenderPlatform host_platform = HostRenderPlatform::UNKNOWN;
};

/// Facts supplied by build configuration and runtime probes.
///
/// Every member defaults to a fail-closed value. Platform adapters must set
/// these fields only from observed state:
///
/// - `*_compiled`: the implementation is linked into this executable;
/// - `*_raster_ready`: the high-quality raster fallback passed its runtime
///   initialization gate;
/// - `native_rt_api_supported`: the native device capability query passed;
/// - `native_rt_hardware_accelerated`: the selected adapter has an accepted
///   hardware RT implementation;
/// - `native_rt_probe_passed`: a BLAS/TLAS build, ray dispatch, and readback
///   produced the expected result in this process; and
/// - `native_rt_scene_interop_ready`: the renderer and RT backend share scene
///   geometry, frame resources, synchronization, and lifecycle safely.
struct RendererRuntimeCapabilities {
  bool ogre14_compiled = false;
  bool ogre_next_compiled = false;
  bool ogre14_raster_ready = false;
  bool ogre_next_raster_ready = false;

  NativeRayTracingBackend native_rt_backend = NativeRayTracingBackend::NONE;
  bool native_rt_backend_compiled = false;
  bool native_rt_api_supported = false;
  bool native_rt_hardware_accelerated = false;
  bool native_rt_probe_passed = false;
  bool native_rt_scene_interop_ready = false;
};

struct RendererSelectionResult {
  RendererFrontend requested_frontend = RendererFrontend::NONE;
  RendererFrontend effective_frontend = RendererFrontend::NONE;
  RayTracingMode requested_ray_tracing_mode = RayTracingMode::DISABLED;
  NativeRayTracingBackend effective_native_rt_backend =
      NativeRayTracingBackend::NONE;
  NativeRayTracingReadiness native_rt_readiness =
      NativeRayTracingReadiness::NOT_REQUESTED;
  RendererSelectionStatus status =
      RendererSelectionStatus::REJECTED_INVALID_REQUEST;
  bool accepted = false;
  bool used_raster_fallback = false;
};

bool IsKnownRendererFrontend(RendererFrontend frontend) noexcept;
bool IsKnownRayTracingMode(RayTracingMode mode) noexcept;
bool IsKnownHostRenderPlatform(HostRenderPlatform platform) noexcept;
bool IsKnownNativeRayTracingBackend(NativeRayTracingBackend backend) noexcept;

NativeRayTracingBackend
ExpectedNativeRayTracingBackend(HostRenderPlatform platform) noexcept;

/// Resolve renderer selection without touching an API or global state.
///
/// Unknown enum values, missing frontends, and incomplete RT paths fail closed.
/// PREFER_HARDWARE may fall back only to an explicitly ready raster path.
/// REQUIRE_HARDWARE never falls back.
RendererSelectionResult ResolveRendererBackendPolicy(
    const RendererSelectionRequest &request,
    const RendererRuntimeCapabilities &capabilities) noexcept;

const char *ToString(RendererFrontend frontend) noexcept;
const char *ToString(RayTracingMode mode) noexcept;
const char *ToString(HostRenderPlatform platform) noexcept;
const char *ToString(NativeRayTracingBackend backend) noexcept;
const char *ToString(NativeRayTracingReadiness readiness) noexcept;
const char *ToString(RendererSelectionStatus status) noexcept;

} // namespace RoR
