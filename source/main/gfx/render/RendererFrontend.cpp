/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontend.h"

#include <limits>

namespace RoR::Render {
namespace {

bool IsConcreteNativeGraphicsApi(NativeGraphicsApi api) noexcept {
  return api == NativeGraphicsApi::METAL ||
         api == NativeGraphicsApi::DIRECT3D12 ||
         api == NativeGraphicsApi::VULKAN;
}

bool SameNativeObject(const NativeObjectToken &lhs,
                      const NativeObjectToken &rhs) noexcept {
  return lhs.api == rhs.api && lhs.kind == rhs.kind &&
         lhs.context_id == rhs.context_id && lhs.value == rhs.value &&
         lhs.generation == rhs.generation;
}

bool SameNativeContext(const NativeContextExport &lhs,
                       const NativeContextExport &rhs) noexcept {
  return lhs.version == rhs.version && lhs.native_api == rhs.native_api &&
         lhs.context_id == rhs.context_id &&
         SameNativeObject(lhs.device, rhs.device) &&
         SameNativeObject(lhs.physical_device, rhs.physical_device) &&
         SameNativeObject(lhs.graphics_queue, rhs.graphics_queue) &&
         SameNativeObject(lhs.compute_queue, rhs.compute_queue) &&
         lhs.graphics_queue_family == rhs.graphics_queue_family &&
         lhs.compute_queue_family == rhs.compute_queue_family;
}

bool SameFrontendCapabilities(const FrontendCapabilityReport &lhs,
                              const FrontendCapabilityReport &rhs) noexcept {
  return lhs.version == rhs.version &&
         lhs.scene_snapshot_version == rhs.scene_snapshot_version &&
         lhs.frontend_kind == rhs.frontend_kind &&
         lhs.native_api == rhs.native_api &&
         lhs.frontend_name == rhs.frontend_name &&
         lhs.frontend_version == rhs.frontend_version &&
         lhs.maximum_texture_dimension_2d == rhs.maximum_texture_dimension_2d &&
         lhs.maximum_views == rhs.maximum_views &&
         lhs.maximum_frames_in_flight == rhs.maximum_frames_in_flight &&
         lhs.raster_ready == rhs.raster_ready &&
         lhs.supports_hdr_output == rhs.supports_hdr_output &&
         lhs.supports_compute == rhs.supports_compute &&
         lhs.supports_async_compute == rhs.supports_async_compute &&
         lhs.supports_dynamic_mesh_updates ==
             rhs.supports_dynamic_mesh_updates &&
         lhs.supports_particle_events == rhs.supports_particle_events &&
         lhs.supports_native_interop == rhs.supports_native_interop &&
         lhs.supports_native_ray_tracing_api ==
             rhs.supports_native_ray_tracing_api &&
         lhs.native_ray_tracing_hardware_accelerated ==
             rhs.native_ray_tracing_hardware_accelerated &&
         lhs.native_ray_tracing_probe_passed ==
             rhs.native_ray_tracing_probe_passed &&
         lhs.native_ray_tracing_geometry_interop_ready ==
             rhs.native_ray_tracing_geometry_interop_ready;
}

bool SameInteropCapabilities(
    const NativeInteropCapabilityReport &lhs,
    const NativeInteropCapabilityReport &rhs) noexcept {
  return lhs.version == rhs.version && lhs.native_api == rhs.native_api &&
         lhs.exports_native_context == rhs.exports_native_context &&
         lhs.exports_vertex_buffers == rhs.exports_vertex_buffers &&
         lhs.exports_index_buffers == rhs.exports_index_buffers &&
         lhs.exports_deformed_meshes == rhs.exports_deformed_meshes &&
         lhs.provides_explicit_frame_synchronization ==
             rhs.provides_explicit_frame_synchronization &&
         lhs.preserves_resource_generations ==
             rhs.preserves_resource_generations &&
         lhs.geometry_interop_proven == rhs.geometry_interop_proven;
}

bool SameRayTracingCapabilities(
    const NativeRayTracingCapabilityReport &lhs,
    const NativeRayTracingCapabilityReport &rhs) noexcept {
  return lhs.version == rhs.version && lhs.native_api == rhs.native_api &&
         lhs.backend_compiled == rhs.backend_compiled &&
         lhs.api_supported == rhs.api_supported &&
         lhs.hardware_accelerated == rhs.hardware_accelerated &&
         lhs.dispatch_readback_probe_passed ==
             rhs.dispatch_readback_probe_passed &&
         lhs.geometry_interop_ready == rhs.geometry_interop_ready &&
         lhs.maximum_instances == rhs.maximum_instances;
}

bool IsAbsent(const NativeObjectToken &token) noexcept {
  return token.api == NativeGraphicsApi::NONE &&
         token.kind == NativeObjectKind::INVALID && token.context_id == 0U &&
         token.value == 0U && token.generation == 0U;
}

ValidationResult ValidateNativeToken(const NativeObjectToken &token,
                                     NativeGraphicsApi expected_api,
                                     std::uint64_t expected_context_id,
                                     NativeObjectKind expected_kind,
                                     const char *field, bool required) {
  if (IsAbsent(token)) {
    return required ? ValidationResult::Failure(
                          ValidationCode::MISSING_REFERENCE, field,
                          "required native object token is absent")
                    : ValidationResult::Success();
  }
  if (!token.valid()) {
    return ValidationResult::Failure(ValidationCode::INVALID_HANDLE, field,
                                     "native object token is malformed");
  }
  if (token.api != expected_api) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_RESOURCE_KIND, field,
        "native object belongs to a different graphics API");
  }
  if (token.context_id != expected_context_id) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, field,
        "native object belongs to a different device context");
  }
  if (token.kind != expected_kind) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_RESOURCE_KIND, field,
        "native object kind does not match the required object");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeBufferSlice(const NativeBufferSlice &slice,
                                           NativeGraphicsApi expected_api,
                                           std::uint64_t expected_context_id,
                                           const char *field) {
  ValidationResult validation =
      ValidateNativeToken(slice.buffer, expected_api, expected_context_id,
                          NativeObjectKind::BUFFER, field, true);
  if (!validation) {
    return validation;
  }
  if (slice.size_bytes == 0U || slice.stride_bytes == 0U) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, field,
        "native buffer slice requires nonzero size and stride");
  }
  if (slice.offset_bytes >
      (std::numeric_limits<std::uint64_t>::max)() - slice.size_bytes) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, field,
        "native buffer slice byte range overflows");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateSliceCapacity(const NativeBufferSlice &slice,
                                       std::uint32_t element_count,
                                       std::uint32_t element_size,
                                       const char *field) {
  if (element_count == 0U) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD, field,
                                     "native stream cannot be empty");
  }
  const std::uint64_t last_index =
      static_cast<std::uint64_t>(element_count - 1U);
  if (last_index != 0U &&
      slice.stride_bytes >
          (std::numeric_limits<std::uint64_t>::max)() / last_index) {
    return ValidationResult::Failure(ValidationCode::SIZE_MISMATCH, field,
                                     "native stream stride overflows");
  }
  const std::uint64_t last_offset = last_index * slice.stride_bytes;
  if (last_offset >
          (std::numeric_limits<std::uint64_t>::max)() - element_size ||
      slice.size_bytes < last_offset + element_size) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, field,
        "native buffer slice is too small for its declared element count");
  }
  return ValidationResult::Success();
}

} // namespace

bool IsKnownRendererFrontendKind(RendererFrontendKind kind) noexcept {
  switch (kind) {
  case RendererFrontendKind::OGRE14:
  case RendererFrontendKind::OGRE_NEXT:
  case RendererFrontendKind::CUSTOM:
    return true;
  }
  return false;
}

bool IsKnownNativeGraphicsApi(NativeGraphicsApi api) noexcept {
  switch (api) {
  case NativeGraphicsApi::NONE:
  case NativeGraphicsApi::METAL:
  case NativeGraphicsApi::DIRECT3D12:
  case NativeGraphicsApi::VULKAN:
    return true;
  }
  return false;
}

bool IsKnownNativeWindowSystem(NativeWindowSystem system) noexcept {
  switch (system) {
  case NativeWindowSystem::NONE:
  case NativeWindowSystem::COCOA:
  case NativeWindowSystem::WINDOWS:
  case NativeWindowSystem::X11:
  case NativeWindowSystem::WAYLAND:
    return true;
  }
  return false;
}

bool IsKnownNativeObjectKind(NativeObjectKind kind) noexcept {
  switch (kind) {
  case NativeObjectKind::INVALID:
  case NativeObjectKind::DEVICE:
  case NativeObjectKind::PHYSICAL_DEVICE:
  case NativeObjectKind::QUEUE:
  case NativeObjectKind::BUFFER:
  case NativeObjectKind::TIMELINE_SYNC:
    return true;
  }
  return false;
}

bool IsKnownNativeIndexFormat(NativeIndexFormat format) noexcept {
  switch (format) {
  case NativeIndexFormat::UINT16:
  case NativeIndexFormat::UINT32:
    return true;
  }
  return false;
}

bool IsKnownNativeVertexPositionFormat(
    NativeVertexPositionFormat format) noexcept {
  switch (format) {
  case NativeVertexPositionFormat::FLOAT32_XYZ:
    return true;
  }
  return false;
}

bool IsKnownNativeGeometryBufferState(
    NativeGeometryBufferState state) noexcept {
  switch (state) {
  case NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD:
    return true;
  case NativeGeometryBufferState::INVALID:
    return false;
  }
  return false;
}

ValidationResult
ValidateFrontendCapabilityReport(const FrontendCapabilityReport &report) {
  if (report.version != kRendererFrontendContractVersion ||
      report.scene_snapshot_version != kSceneSnapshotVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "frontend and snapshot contract versions must match");
  }
  if (!IsKnownRendererFrontendKind(report.frontend_kind) ||
      !IsKnownNativeGraphicsApi(report.native_api)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "backend",
                                     "unknown frontend kind or native API");
  }
  if (report.frontend_name.empty() || report.frontend_version.empty() ||
      report.frontend_name.size() > 127U ||
      report.frontend_version.size() > 63U ||
      report.frontend_name.find('\0') != std::string::npos ||
      report.frontend_version.find('\0') != std::string::npos) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "frontend_name",
        "frontend identity strings are empty, oversized, or contain NUL");
  }
  if (report.raster_ready &&
      (report.maximum_texture_dimension_2d == 0U ||
       report.maximum_views == 0U || report.maximum_frames_in_flight == 0U)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "limits",
        "a ready raster frontend must report nonzero limits");
  }
  if (report.supports_native_interop &&
      report.native_api == NativeGraphicsApi::NONE) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "native_api",
        "native interop requires a concrete graphics API");
  }
  if (report.supports_async_compute && !report.supports_compute) {
    return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                     "supports_compute",
                                     "async compute requires compute support");
  }
  if (report.supports_native_ray_tracing_api &&
      report.native_api == NativeGraphicsApi::NONE) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "native_api",
        "native ray tracing requires a concrete graphics API");
  }
  if ((report.native_ray_tracing_hardware_accelerated ||
       report.native_ray_tracing_probe_passed ||
       report.native_ray_tracing_geometry_interop_ready) &&
      !report.supports_native_ray_tracing_api) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "supports_native_ray_tracing_api",
        "RT proof fields require native ray-tracing API support");
  }
  if (report.native_ray_tracing_probe_passed &&
      !report.native_ray_tracing_hardware_accelerated) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "native_ray_tracing_hardware_accelerated",
        "accepted RT probe requires hardware acceleration");
  }
  if (report.native_ray_tracing_geometry_interop_ready &&
      (!report.native_ray_tracing_probe_passed ||
       !report.supports_native_interop ||
       !report.supports_dynamic_mesh_updates)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "native_ray_tracing_geometry_interop_ready",
        "geometry interop requires probe, native interop, and deformable "
        "meshes");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeInteropCapabilityReport(
    const NativeInteropCapabilityReport &report) {
  if (report.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported native interop capability version");
  }
  if (!IsKnownNativeGraphicsApi(report.native_api)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "native_api",
                                     "unknown native graphics API");
  }
  const bool reports_any_interop =
      report.exports_native_context || report.exports_vertex_buffers ||
      report.exports_index_buffers || report.exports_deformed_meshes ||
      report.provides_explicit_frame_synchronization ||
      report.preserves_resource_generations || report.geometry_interop_proven;
  if (reports_any_interop && report.native_api == NativeGraphicsApi::NONE) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "native_api",
        "native interop features require a concrete graphics API");
  }
  if (report.geometry_interop_proven &&
      (!report.exports_native_context || !report.exports_vertex_buffers ||
       !report.exports_index_buffers || !report.exports_deformed_meshes ||
       !report.provides_explicit_frame_synchronization ||
       !report.preserves_resource_generations)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "geometry_interop_proven",
        "geometry interop proof requires context, "
        "geometry, synchronization, and generations");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeRayTracingCapabilityReport(
    const NativeRayTracingCapabilityReport &report) {
  if (report.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported native RT capability version");
  }
  if (!IsKnownNativeGraphicsApi(report.native_api)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "native_api",
                                     "unknown native graphics API");
  }
  const bool reports_any_rt =
      report.backend_compiled || report.api_supported ||
      report.hardware_accelerated || report.dispatch_readback_probe_passed ||
      report.geometry_interop_ready || report.maximum_instances != 0U;
  if (reports_any_rt && report.native_api == NativeGraphicsApi::NONE) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "native_api",
        "native RT capabilities require a concrete graphics API");
  }
  if (report.api_supported && !report.backend_compiled) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "backend_compiled",
        "RT API support requires a compiled backend");
  }
  if (report.hardware_accelerated && !report.api_supported) {
    return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                     "api_supported",
                                     "hardware RT requires API support");
  }
  if (report.maximum_instances != 0U && !report.api_supported) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "api_supported",
        "RT instance capacity requires API support");
  }
  if (report.dispatch_readback_probe_passed && !report.hardware_accelerated) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "hardware_accelerated",
        "dispatch/readback proof requires hardware acceleration");
  }
  if (report.geometry_interop_ready &&
      (!report.dispatch_readback_probe_passed ||
       report.maximum_instances == 0U)) {
    return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                     "geometry_interop_ready",
                                     "geometry interop requires a passed probe "
                                     "and nonzero instance capacity");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateFrontendInitializationRequest(
    const FrontendInitializationRequest &request) {
  if (request.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported frontend initialization version");
  }
  if (request.initial_surface_revision == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "initial_surface_revision",
        "initial surface revision must be nonzero");
  }
  if (!IsKnownNativeWindowSystem(request.window.system)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "window.system",
                                     "unknown native window system");
  }
  const bool absent_window = request.window.connection == 0U &&
                             request.window.surface == 0U &&
                             request.window.generation == 0U;
  if ((request.window.system == NativeWindowSystem::NONE) != absent_window) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "window",
        "window system and native identities must both be set or all absent");
  }
  if (request.window.system != NativeWindowSystem::NONE &&
      !request.window.valid()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_HANDLE, "window",
        "native window is incomplete for its window system");
  }
  if (request.initial_width == 0U || request.initial_height == 0U ||
      request.initial_width > kMaximumRenderDimension ||
      request.initial_height > kMaximumRenderDimension) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "initial_extent",
        "initial dimensions must be in [1, 65535]");
  }
  if (!IsFinite(request.initial_content_scale) ||
      request.initial_content_scale.x <= 0.0F ||
      request.initial_content_scale.y <= 0.0F ||
      request.initial_content_scale.x > 16.0F ||
      request.initial_content_scale.y > 16.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "initial_content_scale",
        "content scale must be finite and in (0, 16]");
  }
  if (request.maximum_frames_in_flight == 0U ||
      request.maximum_frames_in_flight > 8U) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "maximum_frames_in_flight",
                                     "frames in flight must be in [1, 8]");
  }
  if (!request.headless && !request.window.valid()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "window",
        "non-headless frontend requires a native window");
  }
  if (request.headless && !absent_window) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_HANDLE, "window",
        "headless frontend cannot retain a presentation window");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateFrontendSurfaceUpdate(const FrontendSurfaceUpdate &update,
                              bool headless) {
  if (update.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "surface.version",
        "unsupported frontend surface update version");
  }
  if (update.surface_revision == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "surface.surface_revision",
                                     "surface revision must be nonzero");
  }
  if (!IsKnownNativeWindowSystem(update.window.system)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "surface.window.system",
                                     "unknown native window system");
  }
  const bool absent_window = update.window.connection == 0U &&
                             update.window.surface == 0U &&
                             update.window.generation == 0U &&
                             update.window.system == NativeWindowSystem::NONE;
  if ((headless && !absent_window) || (!headless && !update.window.valid())) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_HANDLE, "surface.window",
        "surface window presence must match frontend headless mode");
  }
  const bool valid_suspended_extent =
      update.suspended && update.pixel_width == 0U && update.pixel_height == 0U;
  const bool valid_active_extent =
      !update.suspended && update.pixel_width != 0U &&
      update.pixel_height != 0U &&
      update.pixel_width <= kMaximumRenderDimension &&
      update.pixel_height <= kMaximumRenderDimension;
  if (!valid_suspended_extent && !valid_active_extent) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "surface.extent",
        "suspended extent must be 0x0; active extent must be in [1, 65535]");
  }
  if (!IsFinite(update.content_scale) || update.content_scale.x <= 0.0F ||
      update.content_scale.y <= 0.0F || update.content_scale.x > 16.0F ||
      update.content_scale.y > 16.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "surface.content_scale",
        "content scale must be finite and in (0, 16]");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateFrontendSurfaceTransition(const FrontendSurfaceUpdate &current_surface,
                                  const FrontendSurfaceUpdate &next_surface,
                                  bool headless,
                                  bool prior_surface_frames_complete) {
  ValidationResult validation =
      ValidateFrontendSurfaceUpdate(current_surface, headless);
  if (!validation) {
    return validation;
  }
  validation = ValidateFrontendSurfaceUpdate(next_surface, headless);
  if (!validation) {
    return validation;
  }
  if (current_surface.surface_revision ==
      (std::numeric_limits<std::uint64_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "current_surface_revision",
        "surface revision space is exhausted; reinitialize the frontend");
  }
  if (next_surface.surface_revision <= current_surface.surface_revision) {
    return ValidationResult::Failure(
        ValidationCode::NON_DETERMINISTIC_ORDER, "surface.surface_revision",
        "next surface revision must be strictly greater than current");
  }
  if (!headless) {
    const bool same_window_identity =
        current_surface.window.system == next_surface.window.system &&
        current_surface.window.connection == next_surface.window.connection &&
        current_surface.window.surface == next_surface.window.surface;
    if (same_window_identity &&
        next_surface.window.generation != current_surface.window.generation) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "surface.window.generation",
          "unchanged native window identity must retain its generation");
    }
    if (!same_window_identity &&
        next_surface.window.generation <= current_surface.window.generation) {
      return ValidationResult::Failure(
          ValidationCode::NON_DETERMINISTIC_ORDER, "surface.window.generation",
          "replacement native window identity requires a newer generation");
    }
  }
  if (!prior_surface_frames_complete) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "prior_surface_frames_complete",
        "old-surface presented frames must complete before replacement");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateRenderFramePresentation(const RenderFrameRequest &request,
                                const FrontendSurfaceUpdate &current_surface) {
  ValidationResult validation = ValidateRenderFrameRequest(request);
  if (!validation) {
    return validation;
  }
  validation = ValidateFrontendSurfaceUpdate(current_surface, false);
  if (!validation) {
    return validation;
  }
  if (!request.present) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "presentation",
        "presentation validation requires a presented frame");
  }
  if (current_surface.suspended) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "surface.extent",
        "a suspended zero-extent surface cannot be presented");
  }
  if (request.presentation_surface_revision !=
      current_surface.surface_revision) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "presentation_surface_revision",
        "frame presentation revision is not the current surface revision");
  }
  for (const CameraViewRequest &view : request.views) {
    if (view.view_id != request.presentation_view_id) {
      continue;
    }
    if (view.width != current_surface.pixel_width ||
        view.height != current_surface.pixel_height) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "presentation_view.extent",
          "presentation view must exactly match the drawable pixel extent");
    }
    return ValidationResult::Success();
  }
  return ValidationResult::Failure(
      ValidationCode::MISSING_REFERENCE, "presentation_view_id",
      "presentation view is absent from the request");
}

ValidationResult
ValidateNativeContextExport(const NativeContextExport &context) {
  if (context.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "context.version",
        "unsupported native context export version");
  }
  if (!IsConcreteNativeGraphicsApi(context.native_api)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "context.native_api",
                                     "native context requires a concrete API");
  }
  if (context.context_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "context.context_id",
                                     "native context identifier is zero");
  }

  ValidationResult validation = ValidateNativeToken(
      context.device, context.native_api, context.context_id,
      NativeObjectKind::DEVICE, "context.device", true);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeToken(
      context.physical_device, context.native_api, context.context_id,
      NativeObjectKind::PHYSICAL_DEVICE, "context.physical_device",
      context.native_api == NativeGraphicsApi::VULKAN);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeToken(context.graphics_queue, context.native_api,
                                   context.context_id, NativeObjectKind::QUEUE,
                                   "context.graphics_queue", true);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeToken(context.compute_queue, context.native_api,
                                   context.context_id, NativeObjectKind::QUEUE,
                                   "context.compute_queue", false);
  if (!validation) {
    return validation;
  }

  if (context.native_api == NativeGraphicsApi::VULKAN) {
    if (context.graphics_queue_family == kInvalidNativeQueueFamily) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "context.graphics_queue_family",
          "Vulkan graphics queue requires a queue family index");
    }
    if (context.compute_queue.valid() !=
        (context.compute_queue_family != kInvalidNativeQueueFamily)) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "context.compute_queue_family",
          "Vulkan compute queue and family must be supplied together");
    }
  } else if (context.graphics_queue_family != kInvalidNativeQueueFamily ||
             context.compute_queue_family != kInvalidNativeQueueFamily) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "context.queue_family",
                                     "queue family indices are Vulkan-only");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeGeometryExportRequest(
    const NativeGeometryExportRequest &request) {
  if (request.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "geometry_request.version",
        "unsupported native geometry request version");
  }
  if (request.frame_id == 0U || request.snapshot_id == 0U ||
      request.instance_id == 0U || request.topology_revision == 0U ||
      request.deformation_revision == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "geometry_request.identity",
        "frame, snapshot, instance, and geometry revisions must be nonzero");
  }
  if (!request.mesh.valid()) {
    return ValidationResult::Failure(ValidationCode::INVALID_HANDLE,
                                     "geometry_request.mesh",
                                     "geometry request mesh is invalid");
  }
  if (request.mesh.kind() != ResourceKind::MESH) {
    return ValidationResult::Failure(ValidationCode::WRONG_RESOURCE_KIND,
                                     "geometry_request.mesh",
                                     "geometry request must reference a mesh");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateNativeGeometryExport(const NativeGeometryExport &geometry,
                             NativeGraphicsApi expected_api,
                             std::uint64_t expected_context_id) {
  if (!IsConcreteNativeGraphicsApi(expected_api) || expected_context_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "geometry.native_api",
        "expected API and context must be concrete");
  }
  if (geometry.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "geometry.version",
        "unsupported native geometry export version");
  }
  if (geometry.export_id == 0U || geometry.frame_id == 0U ||
      geometry.snapshot_id == 0U || geometry.instance_id == 0U ||
      geometry.topology_revision == 0U || geometry.deformation_revision == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "geometry.identity",
        "export, scene, instance, and geometry revisions must be nonzero");
  }
  if (!geometry.mesh.valid()) {
    return ValidationResult::Failure(ValidationCode::INVALID_HANDLE,
                                     "geometry.mesh",
                                     "geometry export mesh is invalid");
  }
  if (geometry.mesh.kind() != ResourceKind::MESH) {
    return ValidationResult::Failure(ValidationCode::WRONG_RESOURCE_KIND,
                                     "geometry.mesh",
                                     "geometry export must reference a mesh");
  }
  if (!IsKnownMeshPrimitiveTopology(geometry.topology) ||
      !IsKnownNativeVertexPositionFormat(geometry.position_format) ||
      !IsKnownNativeIndexFormat(geometry.index_format)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "geometry.format",
                                     "geometry export format is unknown");
  }

  ValidationResult validation =
      ValidateNativeBufferSlice(geometry.positions, expected_api,
                                expected_context_id, "geometry.positions");
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeBufferSlice(
      geometry.indices, expected_api, expected_context_id, "geometry.indices");
  if (!validation) {
    return validation;
  }

  constexpr std::uint32_t kFloat32XyzBytes = 12U;
  if (geometry.positions.stride_bytes < kFloat32XyzBytes ||
      geometry.positions.stride_bytes % alignof(float) != 0U ||
      geometry.positions.offset_bytes % alignof(float) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "geometry.positions",
        "FLOAT32_XYZ positions require an aligned stride of at least 12 bytes");
  }
  validation = ValidateSliceCapacity(geometry.positions, geometry.vertex_count,
                                     kFloat32XyzBytes, "geometry.positions");
  if (!validation) {
    return validation;
  }

  const std::uint32_t index_bytes =
      geometry.index_format == NativeIndexFormat::UINT16 ? 2U : 4U;
  if (geometry.indices.stride_bytes != index_bytes ||
      geometry.indices.offset_bytes % index_bytes != 0U) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "geometry.indices",
        "index stride and alignment must match the declared index format");
  }
  validation = ValidateSliceCapacity(geometry.indices, geometry.index_count,
                                     index_bytes, "geometry.indices");
  if (!validation) {
    return validation;
  }

  std::uint32_t primitive_width = 1U;
  switch (geometry.topology) {
  case MeshPrimitiveTopology::TRIANGLE_LIST:
    primitive_width = 3U;
    break;
  case MeshPrimitiveTopology::LINE_LIST:
    primitive_width = 2U;
    break;
  case MeshPrimitiveTopology::POINT_LIST:
    primitive_width = 1U;
    break;
  }
  if (geometry.index_count < primitive_width ||
      geometry.index_count % primitive_width != 0U) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "geometry.index_count",
        "native index count must contain complete primitives");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateNativeGeometryExport(const NativeGeometryExportRequest &request,
                             const NativeGeometryExport &geometry,
                             NativeGraphicsApi expected_api,
                             std::uint64_t expected_context_id) {
  ValidationResult validation = ValidateNativeGeometryExportRequest(request);
  if (!validation) {
    return validation;
  }
  validation =
      ValidateNativeGeometryExport(geometry, expected_api, expected_context_id);
  if (!validation) {
    return validation;
  }
  if (geometry.frame_id != request.frame_id ||
      geometry.snapshot_id != request.snapshot_id ||
      geometry.instance_id != request.instance_id ||
      geometry.mesh != request.mesh ||
      geometry.topology_revision != request.topology_revision ||
      geometry.deformation_revision != request.deformation_revision) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "geometry.request_identity",
        "geometry export does not match the requested scene revision");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeFrameSynchronization(
    const NativeFrameSynchronization &synchronization,
    const NativeContextExport &context, bool require_external_completion) {
  if (synchronization.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "synchronization.version",
        "unsupported native frame synchronization version");
  }
  ValidationResult validation = ValidateNativeContextExport(context);
  if (!validation) {
    return validation;
  }
  if (synchronization.frame_id == 0U || synchronization.snapshot_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "synchronization.frame_id",
        "synchronized frame and snapshot identifiers must be nonzero");
  }
  validation = ValidateNativeToken(
      synchronization.interop_queue, context.native_api, context.context_id,
      NativeObjectKind::QUEUE, "synchronization.interop_queue", true);
  if (!validation) {
    return validation;
  }
  if (!SameNativeObject(synchronization.interop_queue,
                        context.graphics_queue) ||
      synchronization.interop_queue_family != context.graphics_queue_family) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "synchronization.interop_queue",
        "interop must use the exported graphics queue and queue family");
  }
  if (!IsKnownNativeGeometryBufferState(
          synchronization.frontend_release_state) ||
      !IsKnownNativeGeometryBufferState(
          synchronization.external_return_state) ||
      synchronization.frontend_release_state !=
          NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD ||
      synchronization.external_return_state !=
          synchronization.frontend_release_state) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "synchronization.geometry_buffer_state",
        "geometry handoff and return must use the canonical read-only state");
  }
  validation = ValidateNativeToken(
      synchronization.frontend_complete_timeline, context.native_api,
      context.context_id, NativeObjectKind::TIMELINE_SYNC,
      "synchronization.frontend_complete_timeline", true);
  if (!validation) {
    return validation;
  }
  if (synchronization.frontend_complete_value == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "synchronization.frontend_complete_value",
        "frontend completion value must be nonzero");
  }
  validation = ValidateNativeToken(synchronization.external_complete_timeline,
                                   context.native_api, context.context_id,
                                   NativeObjectKind::TIMELINE_SYNC,
                                   "synchronization.external_complete_timeline",
                                   require_external_completion);
  if (!validation) {
    return validation;
  }
  const bool has_external_timeline =
      synchronization.external_complete_timeline.valid();
  if (has_external_timeline !=
      (synchronization.external_complete_value != 0U)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "synchronization.external_complete_value",
        "external completion timeline and value must be supplied together");
  }
  if (has_external_timeline &&
      SameNativeObject(synchronization.frontend_complete_timeline,
                       synchronization.external_complete_timeline) &&
      synchronization.external_complete_value <=
          synchronization.frontend_complete_value) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "synchronization.external_complete_value",
        "shared completion timeline requires a strictly later external value");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeRayTracingFrameRequest(
    const NativeRayTracingFrameRequest &request) {
  if (request.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "ray_tracing_request.version",
        "unsupported native ray-tracing request version");
  }
  ValidationResult validation = ValidateRenderFrameRequest(request.frame);
  if (!validation) {
    return validation;
  }
  if (request.frame.present) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "ray_tracing_request.frame.present",
        "native RT version 1 supports offscreen CPU readback only");
  }
  if (request.samples_per_pixel == 0U ||
      request.samples_per_pixel > kMaximumNativeRayTracingSamplesPerPixel) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "samples_per_pixel",
        "ray-tracing samples per pixel must be in [1, 64]");
  }
  if (request.maximum_bounces == 0U ||
      request.maximum_bounces > kMaximumNativeRayTracingBounces) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "maximum_bounces",
        "ray-tracing bounce count must be in [1, 32]");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateNativeRayTracingFrameOutput(const NativeRayTracingFrameRequest &request,
                                    const RenderFrameOutput &output) {
  ValidationResult validation = ValidateNativeRayTracingFrameRequest(request);
  if (!validation) {
    return validation;
  }
  validation = ValidateRenderFrameOutput(request.frame, output);
  if (!validation) {
    return validation;
  }
  if (output.status != RenderFrameStatus::RENDERED) {
    return ValidationResult::Success();
  }
  for (std::size_t index = 0U; index < output.attachments.size(); ++index) {
    const FrameAttachment &attachment = output.attachments[index];
    if (attachment.gpu_resource.valid() || attachment.bytes.empty()) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_RESOURCE_KIND, "output.attachments",
          "native RT version 1 outputs CPU readback bytes and no GPU handles",
          index);
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeGeometryInteropProofSet(
    const NativeGeometryInteropProofSet &proof) {
  if (proof.version != kRendererFrontendContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "proof.version",
        "unsupported native geometry interop proof version");
  }
  ValidationResult validation =
      ValidateFrontendCapabilityReport(proof.frontend);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeInteropCapabilityReport(proof.interop);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeRayTracingCapabilityReport(proof.ray_tracing);
  if (!validation) {
    return validation;
  }

  if (proof.frontend_object == nullptr) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.frontend_object",
        "geometry readiness requires the live selected frontend");
  }
  const FrontendCapabilityReport live_frontend_report =
      proof.frontend_object->QueryCapabilities();
  validation = ValidateFrontendCapabilityReport(live_frontend_report);
  if (!validation) {
    return validation;
  }
  if (!SameFrontendCapabilities(live_frontend_report, proof.frontend)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.frontend",
        "claimed frontend report differs from the live selected frontend");
  }

  const bool interop_reports_features =
      proof.interop.exports_native_context ||
      proof.interop.exports_vertex_buffers ||
      proof.interop.exports_index_buffers ||
      proof.interop.exports_deformed_meshes ||
      proof.interop.provides_explicit_frame_synchronization ||
      proof.interop.preserves_resource_generations ||
      proof.interop.geometry_interop_proven;
  const bool ray_tracing_reports_features =
      proof.ray_tracing.backend_compiled || proof.ray_tracing.api_supported ||
      proof.ray_tracing.hardware_accelerated ||
      proof.ray_tracing.dispatch_readback_probe_passed ||
      proof.ray_tracing.geometry_interop_ready ||
      proof.ray_tracing.maximum_instances != 0U;
  if (interop_reports_features && !proof.frontend.supports_native_interop) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "proof.frontend.supports_native_interop",
        "interop report features must be advertised by the frontend");
  }
  if (ray_tracing_reports_features &&
      !proof.frontend.supports_native_ray_tracing_api) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "proof.frontend.supports_native_ray_tracing_api",
        "ray-tracing report features must be advertised by the frontend");
  }
  if (!proof.frontend.supports_native_interop &&
      (interop_reports_features || proof.native_interop_object != nullptr)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.native_interop_object",
        "interop report or object conflicts with the frontend capability");
  }
  if (!proof.frontend.supports_native_ray_tracing_api &&
      (ray_tracing_reports_features ||
       proof.native_ray_tracing_backend != nullptr)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.native_ray_tracing_backend",
        "RT report or object conflicts with the frontend capability");
  }

  if (proof.frontend.supports_native_interop) {
    if (proof.native_interop_object == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "proof.native_interop_object",
          "frontend native interop requires a live interop object");
    }
    if (proof.frontend_object->GetNativeInterop() !=
        proof.native_interop_object) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "proof.native_interop_object",
          "interop object is not owned by the live selected frontend");
    }
    if (!IsConcreteNativeGraphicsApi(proof.frontend.native_api) ||
        proof.interop.native_api != proof.frontend.native_api) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_RESOURCE_KIND, "proof.interop.native_api",
          "frontend and interop reports must name the same concrete API");
    }
    const NativeInteropCapabilityReport live_report =
        proof.native_interop_object->QueryCapabilities();
    validation = ValidateNativeInteropCapabilityReport(live_report);
    if (!validation) {
      return validation;
    }
    if (!SameInteropCapabilities(live_report, proof.interop)) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "proof.interop",
          "claimed interop report differs from the live interop object");
    }
  }
  if (proof.frontend.supports_native_ray_tracing_api) {
    if (proof.native_ray_tracing_backend == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "proof.native_ray_tracing_backend",
          "frontend RT support requires a live native RT backend");
    }
    if (proof.ray_tracing.native_api != proof.frontend.native_api) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_RESOURCE_KIND, "proof.ray_tracing.native_api",
          "frontend and ray-tracing reports must name the same API");
    }
    const NativeRayTracingCapabilityReport live_report =
        proof.native_ray_tracing_backend->QueryCapabilities();
    validation = ValidateNativeRayTracingCapabilityReport(live_report);
    if (!validation) {
      return validation;
    }
    if (!SameRayTracingCapabilities(live_report, proof.ray_tracing)) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "proof.ray_tracing",
          "claimed RT report differs from the live native RT backend");
    }
  }
  if (proof.frontend.supports_native_ray_tracing_api !=
          proof.ray_tracing.api_supported ||
      proof.frontend.native_ray_tracing_hardware_accelerated !=
          proof.ray_tracing.hardware_accelerated ||
      proof.frontend.native_ray_tracing_probe_passed !=
          proof.ray_tracing.dispatch_readback_probe_passed) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.ray_tracing",
        "frontend and live RT report must agree on API, hardware, and probe "
        "readiness");
  }
  if (proof.ray_tracing.geometry_interop_ready &&
      (!proof.interop.geometry_interop_proven ||
       proof.native_interop_object == nullptr)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.ray_tracing.geometry_interop",
        "RT geometry readiness requires proven live native interop");
  }

  const bool claims_geometry_interop =
      proof.frontend.native_ray_tracing_geometry_interop_ready ||
      proof.interop.geometry_interop_proven ||
      proof.ray_tracing.geometry_interop_ready;
  if (!claims_geometry_interop) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.geometry_interop",
        "geometry readiness gate requires complete live interop evidence");
  }

  if (!proof.frontend.native_ray_tracing_geometry_interop_ready ||
      !proof.interop.geometry_interop_proven ||
      !proof.ray_tracing.geometry_interop_ready ||
      proof.native_interop_object == nullptr ||
      proof.native_ray_tracing_backend == nullptr ||
      proof.frontend.native_api != proof.interop.native_api ||
      proof.frontend.native_api != proof.ray_tracing.native_api) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.geometry_interop",
        "geometry interop readiness requires one live API and every proof");
  }

  NativeContextExport live_context;
  const RenderOperationResult context_result =
      proof.native_interop_object->AcquireContext(live_context);
  if (!context_result) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.native_context",
        "live interop object did not provide its claimed native context");
  }
  validation = ValidateNativeContextExport(live_context);
  if (!validation) {
    return validation;
  }
  validation = ValidateNativeContextExport(proof.native_context);
  if (!validation) {
    return validation;
  }
  if (!SameNativeContext(live_context, proof.native_context) ||
      proof.native_context.native_api != proof.frontend.native_api) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.native_context",
        "proof context differs from the live frontend device context");
  }

  validation = ValidateNativeGeometryExport(
      proof.geometry_request, proof.geometry_export,
      proof.native_context.native_api, proof.native_context.context_id);
  if (!validation) {
    return validation;
  }
  if (proof.geometry_export.topology != MeshPrimitiveTopology::TRIANGLE_LIST) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "proof.geometry_export.topology",
        "ray-tracing geometry proof requires triangle geometry");
  }
  if (proof.geometry_request.deformation_revision <= 1U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "proof.geometry_request.deformation_revision",
        "geometry interop readiness requires a non-base deformable revision");
  }
  validation = ValidateNativeFrameSynchronization(proof.frame_synchronization,
                                                  proof.native_context, true);
  if (!validation) {
    return validation;
  }
  if (proof.frame_synchronization.frame_id != proof.geometry_request.frame_id ||
      proof.frame_synchronization.snapshot_id !=
          proof.geometry_request.snapshot_id) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.frame_synchronization",
        "geometry and synchronization must identify the same frame snapshot");
  }

  const RenderOperationResult geometry_lease =
      proof.native_interop_object->ValidateGeometryLease(proof.geometry_export);
  if (!geometry_lease) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.geometry_export",
        "geometry export is not a live lease owned by the interop object");
  }
  const RenderOperationResult frame_lease =
      proof.native_interop_object->ValidateFrameLease(
          proof.frame_synchronization);
  if (!frame_lease) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.frame_synchronization",
        "frame synchronization is not a live interop lease");
  }
  const RenderOperationResult rt_evidence =
      proof.native_ray_tracing_backend->ValidateInteropEvidence(
          proof.geometry_export, proof.frame_synchronization);
  if (!rt_evidence) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "proof.ray_tracing_evidence",
        "live RT backend did not validate the geometry interop evidence");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
