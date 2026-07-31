/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBackendPolicy.h"

namespace RoR {
namespace {

bool IsFrontendCompiled(
    RendererFrontend frontend,
    const RendererRuntimeCapabilities &capabilities) noexcept {
  switch (frontend) {
  case RendererFrontend::OGRE14:
    return capabilities.ogre14_compiled;
  case RendererFrontend::OGRE_NEXT:
    return capabilities.ogre_next_compiled;
  case RendererFrontend::NONE:
    return false;
  }
  return false;
}

bool IsRasterFallbackReady(
    RendererFrontend frontend,
    const RendererRuntimeCapabilities &capabilities) noexcept {
  switch (frontend) {
  case RendererFrontend::OGRE14:
    return capabilities.ogre14_raster_ready;
  case RendererFrontend::OGRE_NEXT:
    return capabilities.ogre_next_raster_ready;
  case RendererFrontend::NONE:
    return false;
  }
  return false;
}

NativeRayTracingReadiness ResolveNativeRayTracingReadiness(
    const RendererSelectionRequest &request,
    const RendererRuntimeCapabilities &capabilities) noexcept {
  if (request.ray_tracing_mode == RayTracingMode::DISABLED) {
    return NativeRayTracingReadiness::NOT_REQUESTED;
  }
  if (request.requested_frontend != RendererFrontend::OGRE_NEXT) {
    return NativeRayTracingReadiness::FRONTEND_INCOMPATIBLE;
  }
  if (!IsRasterFallbackReady(request.requested_frontend, capabilities)) {
    return NativeRayTracingReadiness::RASTER_FRONTEND_NOT_READY;
  }

  const NativeRayTracingBackend expected_backend =
      ExpectedNativeRayTracingBackend(request.host_platform);
  if (expected_backend == NativeRayTracingBackend::NONE) {
    return NativeRayTracingReadiness::PLATFORM_UNSUPPORTED;
  }
  if (capabilities.native_rt_backend != expected_backend) {
    return NativeRayTracingReadiness::BACKEND_MISMATCH;
  }
  if (!capabilities.native_rt_backend_compiled) {
    return NativeRayTracingReadiness::BACKEND_NOT_COMPILED;
  }
  if (!capabilities.native_rt_api_supported) {
    return NativeRayTracingReadiness::API_UNSUPPORTED;
  }
  if (!capabilities.native_rt_hardware_accelerated) {
    return NativeRayTracingReadiness::HARDWARE_ACCELERATION_UNAVAILABLE;
  }
  if (!capabilities.native_rt_probe_passed) {
    return NativeRayTracingReadiness::PROBE_NOT_PASSED;
  }
  if (!capabilities.native_rt_scene_interop_ready) {
    return NativeRayTracingReadiness::SCENE_INTEROP_NOT_READY;
  }
  return NativeRayTracingReadiness::READY;
}

} // namespace

bool IsKnownRendererFrontend(RendererFrontend frontend) noexcept {
  switch (frontend) {
  case RendererFrontend::NONE:
  case RendererFrontend::OGRE14:
  case RendererFrontend::OGRE_NEXT:
    return true;
  }
  return false;
}

bool IsKnownRayTracingMode(RayTracingMode mode) noexcept {
  switch (mode) {
  case RayTracingMode::DISABLED:
  case RayTracingMode::PREFER_HARDWARE:
  case RayTracingMode::REQUIRE_HARDWARE:
    return true;
  }
  return false;
}

bool IsKnownHostRenderPlatform(HostRenderPlatform platform) noexcept {
  switch (platform) {
  case HostRenderPlatform::UNKNOWN:
  case HostRenderPlatform::MACOS:
  case HostRenderPlatform::WINDOWS:
  case HostRenderPlatform::LINUX:
    return true;
  }
  return false;
}

bool IsKnownNativeRayTracingBackend(NativeRayTracingBackend backend) noexcept {
  switch (backend) {
  case NativeRayTracingBackend::NONE:
  case NativeRayTracingBackend::METAL:
  case NativeRayTracingBackend::DXR:
  case NativeRayTracingBackend::VULKAN_KHR:
    return true;
  }
  return false;
}

NativeRayTracingBackend
ExpectedNativeRayTracingBackend(HostRenderPlatform platform) noexcept {
  switch (platform) {
  case HostRenderPlatform::MACOS:
    return NativeRayTracingBackend::METAL;
  case HostRenderPlatform::WINDOWS:
    return NativeRayTracingBackend::DXR;
  case HostRenderPlatform::LINUX:
    return NativeRayTracingBackend::VULKAN_KHR;
  case HostRenderPlatform::UNKNOWN:
    return NativeRayTracingBackend::NONE;
  }
  return NativeRayTracingBackend::NONE;
}

RendererSelectionResult ResolveRendererBackendPolicy(
    const RendererSelectionRequest &request,
    const RendererRuntimeCapabilities &capabilities) noexcept {
  RendererSelectionResult result;
  result.requested_frontend = request.requested_frontend;
  result.requested_ray_tracing_mode = request.ray_tracing_mode;

  if (!IsKnownRendererFrontend(request.requested_frontend) ||
      request.requested_frontend == RendererFrontend::NONE ||
      !IsKnownRayTracingMode(request.ray_tracing_mode) ||
      !IsKnownHostRenderPlatform(request.host_platform) ||
      !IsKnownNativeRayTracingBackend(capabilities.native_rt_backend)) {
    result.status = RendererSelectionStatus::REJECTED_INVALID_REQUEST;
    return result;
  }

  if (!IsFrontendCompiled(request.requested_frontend, capabilities)) {
    result.status = RendererSelectionStatus::REJECTED_FRONTEND_UNAVAILABLE;
    return result;
  }

  if (request.ray_tracing_mode == RayTracingMode::DISABLED) {
    if (!IsRasterFallbackReady(request.requested_frontend, capabilities)) {
      result.status = RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE;
      return result;
    }
    result.effective_frontend = request.requested_frontend;
    result.native_rt_readiness = NativeRayTracingReadiness::NOT_REQUESTED;
    result.status = RendererSelectionStatus::SELECTED_RASTER;
    result.accepted = true;
    return result;
  }

  result.native_rt_readiness =
      ResolveNativeRayTracingReadiness(request, capabilities);
  if (result.native_rt_readiness == NativeRayTracingReadiness::READY) {
    result.effective_frontend = request.requested_frontend;
    result.effective_native_rt_backend = capabilities.native_rt_backend;
    result.status = RendererSelectionStatus::SELECTED_NATIVE_RT;
    result.accepted = true;
    return result;
  }

  if (request.ray_tracing_mode == RayTracingMode::REQUIRE_HARDWARE) {
    result.status = RendererSelectionStatus::REJECTED_RT_REQUIRED;
    return result;
  }

  if (!IsRasterFallbackReady(request.requested_frontend, capabilities)) {
    result.status = RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE;
    return result;
  }

  result.effective_frontend = request.requested_frontend;
  result.status = RendererSelectionStatus::FALLBACK_RASTER;
  result.accepted = true;
  result.used_raster_fallback = true;
  return result;
}

const char *ToString(RendererFrontend frontend) noexcept {
  switch (frontend) {
  case RendererFrontend::NONE:
    return "none";
  case RendererFrontend::OGRE14:
    return "ogre14";
  case RendererFrontend::OGRE_NEXT:
    return "ogre-next";
  }
  return "invalid";
}

const char *ToString(RayTracingMode mode) noexcept {
  switch (mode) {
  case RayTracingMode::DISABLED:
    return "disabled";
  case RayTracingMode::PREFER_HARDWARE:
    return "prefer-hardware";
  case RayTracingMode::REQUIRE_HARDWARE:
    return "require-hardware";
  }
  return "invalid";
}

const char *ToString(HostRenderPlatform platform) noexcept {
  switch (platform) {
  case HostRenderPlatform::UNKNOWN:
    return "unknown";
  case HostRenderPlatform::MACOS:
    return "macos";
  case HostRenderPlatform::WINDOWS:
    return "windows";
  case HostRenderPlatform::LINUX:
    return "linux";
  }
  return "invalid";
}

const char *ToString(NativeRayTracingBackend backend) noexcept {
  switch (backend) {
  case NativeRayTracingBackend::NONE:
    return "none";
  case NativeRayTracingBackend::METAL:
    return "metal";
  case NativeRayTracingBackend::DXR:
    return "dxr";
  case NativeRayTracingBackend::VULKAN_KHR:
    return "vulkan-khr";
  }
  return "invalid";
}

const char *ToString(NativeRayTracingReadiness readiness) noexcept {
  switch (readiness) {
  case NativeRayTracingReadiness::NOT_REQUESTED:
    return "not-requested";
  case NativeRayTracingReadiness::READY:
    return "ready";
  case NativeRayTracingReadiness::FRONTEND_INCOMPATIBLE:
    return "frontend-incompatible";
  case NativeRayTracingReadiness::RASTER_FRONTEND_NOT_READY:
    return "raster-frontend-not-ready";
  case NativeRayTracingReadiness::PLATFORM_UNSUPPORTED:
    return "platform-unsupported";
  case NativeRayTracingReadiness::BACKEND_MISMATCH:
    return "backend-mismatch";
  case NativeRayTracingReadiness::BACKEND_NOT_COMPILED:
    return "backend-not-compiled";
  case NativeRayTracingReadiness::API_UNSUPPORTED:
    return "api-unsupported";
  case NativeRayTracingReadiness::HARDWARE_ACCELERATION_UNAVAILABLE:
    return "hardware-acceleration-unavailable";
  case NativeRayTracingReadiness::PROBE_NOT_PASSED:
    return "probe-not-passed";
  case NativeRayTracingReadiness::SCENE_INTEROP_NOT_READY:
    return "scene-interop-not-ready";
  }
  return "invalid";
}

const char *ToString(RendererSelectionStatus status) noexcept {
  switch (status) {
  case RendererSelectionStatus::SELECTED_RASTER:
    return "selected-raster";
  case RendererSelectionStatus::SELECTED_NATIVE_RT:
    return "selected-native-rt";
  case RendererSelectionStatus::FALLBACK_RASTER:
    return "fallback-raster";
  case RendererSelectionStatus::REJECTED_INVALID_REQUEST:
    return "rejected-invalid-request";
  case RendererSelectionStatus::REJECTED_FRONTEND_UNAVAILABLE:
    return "rejected-frontend-unavailable";
  case RendererSelectionStatus::REJECTED_RT_REQUIRED:
    return "rejected-rt-required";
  case RendererSelectionStatus::REJECTED_RASTER_UNAVAILABLE:
    return "rejected-raster-unavailable";
  }
  return "invalid";
}

} // namespace RoR
