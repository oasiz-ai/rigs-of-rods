/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontend.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer frontend contract test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::shared_ptr<const RoR::Render::SceneSnapshot>
MakeSnapshot(bool with_dynamic_mesh = false, bool with_particles = false) {
  using namespace RoR::Render;
  RoR::Render::SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 7U;
  descriptor.asset_registry_id = 3U;
  descriptor.asset_sequence = 1U;
  if (with_dynamic_mesh) {
    const RenderAssetReference mesh = RenderAssetReference::Create(
        RenderAssetKind::MESH, RenderAssetId::FromWords(3U, 1U), 1U);
    MeshInstanceDescriptor instance;
    instance.instance_id = 1U;
    instance.mesh = mesh;
    instance.material = RenderAssetReference::Create(
        RenderAssetKind::MATERIAL, RenderAssetId::FromWords(3U, 2U), 1U);
    instance.deformation_revision = 2U;
    descriptor.mesh_instances.push_back(instance);
    DynamicMeshUpdateDescriptor update;
    update.update_sequence = 1U;
    update.instance_id = instance.instance_id;
    update.mesh = mesh;
    update.deformation_revision = instance.deformation_revision;
    update.positions.push_back(Float3{});
    update.has_updated_bounds = true;
    descriptor.dynamic_mesh_updates.push_back(update);
  }
  if (with_particles) {
    ParticleEvent event;
    event.event_id = 1U;
    event.emitter_id = 1U;
    event.random_seed = 1U;
    descriptor.particle_events.push_back(event);
  }
  const RoR::Render::SceneSnapshotCreateResult result =
      RoR::Render::CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "minimal valid snapshot could not be created");
  return result.snapshot;
}

RoR::Render::Matrix4x4 MakePerspectiveProjection(
    float near_plane = 0.1F, float far_plane = 10000.0F,
    float horizontal_offset = 0.0F, float vertical_offset = 0.0F) {
  RoR::Render::Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  projection.elements[8U] = horizontal_offset;
  projection.elements[9U] = vertical_offset;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

RoR::Render::Matrix4x4 MakeOrthographicProjection(
    float near_plane = 0.1F, float far_plane = 10000.0F,
    float horizontal_offset = 0.0F, float vertical_offset = 0.0F) {
  RoR::Render::Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  const float depth_scale = 1.0F / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[12U] = horizontal_offset;
  projection.elements[13U] = vertical_offset;
  projection.elements[14U] = near_plane * depth_scale;
  projection.elements[15U] = 1.0F;
  return projection;
}

RoR::Render::RenderFrameRequest MakeFrameRequest() {
  RoR::Render::RenderFrameRequest request;
  request.frame_id = 11U;
  request.scene_snapshot = MakeSnapshot();
  RoR::Render::CameraViewRequest view;
  view.view_id = 1U;
  view.width = 1280U;
  view.height = 720U;
  view.clip_from_view = MakePerspectiveProjection();
  view.previous_clip_from_view = MakePerspectiveProjection();
  request.views.push_back(view);
  request.requested_outputs =
      RoR::Render::FrameOutputMask::COLOR | RoR::Render::FrameOutputMask::DEPTH;
  request.presentation_view_id = view.view_id;
  request.presentation_surface_revision = 1U;
  return request;
}

RoR::Render::FrameAttachment
MakeGpuAttachment(std::uint64_t view_id, RoR::Render::FrameOutputMask output,
                  RoR::Render::PixelFormat format, std::uint32_t width,
                  std::uint32_t height, std::uint32_t slot) {
  RoR::Render::FrameAttachment attachment;
  attachment.view_id = view_id;
  attachment.output = output;
  attachment.format = format;
  attachment.width = width;
  attachment.height = height;
  attachment.gpu_resource = RoR::Render::ResourceHandle::Create(
      RoR::Render::ResourceKind::RENDER_TARGET, 1U, slot, 1U);
  return attachment;
}

RoR::Render::RenderFrameOutput
MakeCorrelatedOutput(const RoR::Render::RenderFrameRequest &request) {
  RoR::Render::RenderFrameOutput output;
  output.frame_id = request.frame_id;
  output.snapshot_id = request.scene_snapshot->snapshot_id();
  output.status = RoR::Render::RenderFrameStatus::RENDERED;
  output.presented = request.present;
  output.presented_view_id = request.presentation_view_id;
  const RoR::Render::CameraViewRequest &view = request.views.front();
  output.attachments.push_back(MakeGpuAttachment(
      1U, RoR::Render::FrameOutputMask::COLOR,
      request.color_format, view.width, view.height, 1U));
  output.attachments.push_back(MakeGpuAttachment(
      1U, RoR::Render::FrameOutputMask::DEPTH,
      RoR::Render::PixelFormat::R32_FLOAT, view.width, view.height, 2U));
  return output;
}

RoR::Render::NativeObjectToken NativeToken(RoR::Render::NativeGraphicsApi api,
                                           RoR::Render::NativeObjectKind kind,
                                           std::uint64_t value,
                                           std::uint64_t context_id = 1U) {
  RoR::Render::NativeObjectToken token;
  token.api = api;
  token.kind = kind;
  token.context_id = context_id;
  token.value = value;
  token.generation = 1U;
  return token;
}

RoR::Render::NativeContextExport MakeNativeContext() {
  using namespace RoR::Render;
  NativeContextExport context;
  context.native_api = NativeGraphicsApi::METAL;
  context.context_id = 1U;
  context.device =
      NativeToken(context.native_api, NativeObjectKind::DEVICE, 10U);
  context.graphics_queue =
      NativeToken(context.native_api, NativeObjectKind::QUEUE, 11U);
  return context;
}

RoR::Render::NativeGeometryExportRequest MakeGeometryRequest() {
  using namespace RoR::Render;
  NativeGeometryExportRequest request;
  request.frame_id = 11U;
  request.snapshot_id = 7U;
  request.instance_id = 5U;
  request.mesh = RenderAssetReference::Create(
      RenderAssetKind::MESH, RenderAssetId::FromWords(1U, 2U), 1U);
  request.topology_revision = 3U;
  request.deformation_revision = 4U;
  return request;
}

RoR::Render::NativeGeometryExport
MakeGeometryExport(const RoR::Render::NativeGeometryExportRequest &request) {
  using namespace RoR::Render;
  NativeGeometryExport geometry;
  geometry.export_id = 2U;
  geometry.frame_id = request.frame_id;
  geometry.snapshot_id = request.snapshot_id;
  geometry.instance_id = request.instance_id;
  geometry.mesh = request.mesh;
  geometry.topology_revision = request.topology_revision;
  geometry.deformation_revision = request.deformation_revision;
  geometry.positions.buffer =
      NativeToken(NativeGraphicsApi::METAL, NativeObjectKind::BUFFER, 20U);
  geometry.positions.size_bytes = 36U;
  geometry.positions.stride_bytes = 12U;
  geometry.indices.buffer =
      NativeToken(NativeGraphicsApi::METAL, NativeObjectKind::BUFFER, 21U);
  geometry.indices.size_bytes = 6U;
  geometry.indices.stride_bytes = 2U;
  geometry.index_format = NativeIndexFormat::UINT16;
  geometry.vertex_count = 3U;
  geometry.index_count = 3U;
  return geometry;
}

RoR::Render::NativeFrameSynchronization MakeFrameSynchronization(
    const RoR::Render::NativeGeometryExportRequest &request) {
  using namespace RoR::Render;
  NativeFrameSynchronization synchronization;
  synchronization.frame_id = request.frame_id;
  synchronization.snapshot_id = request.snapshot_id;
  synchronization.interop_queue =
      NativeToken(NativeGraphicsApi::METAL, NativeObjectKind::QUEUE, 11U);
  synchronization.frontend_release_state =
      NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD;
  synchronization.external_return_state =
      NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD;
  synchronization.frontend_complete_timeline = NativeToken(
      NativeGraphicsApi::METAL, NativeObjectKind::TIMELINE_SYNC, 30U);
  synchronization.frontend_complete_value = 1U;
  synchronization.external_complete_timeline = NativeToken(
      NativeGraphicsApi::METAL, NativeObjectKind::TIMELINE_SYNC, 31U);
  synchronization.external_complete_value = 2U;
  return synchronization;
}

class NullInterop final : public RoR::Render::NativeRenderInterop {
public:
  RoR::Render::NativeInteropCapabilityReport
  QueryCapabilities() const override {
    return {};
  }

  RoR::Render::RenderOperationResult
  AcquireContext(RoR::Render::NativeContextExport &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "no native interop");
  }

  RoR::Render::RenderOperationResult
  AcquireGeometry(const RoR::Render::NativeGeometryExportRequest &,
                  RoR::Render::NativeGeometryExport &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "no native interop");
  }

  RoR::Render::RenderOperationResult
  BeginExternalFrame(std::uint64_t, std::uint64_t,
                     RoR::Render::NativeFrameSynchronization &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "no native interop");
  }

  RoR::Render::RenderOperationResult
  EndExternalFrame(const RoR::Render::NativeFrameSynchronization &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "no native interop");
  }

  RoR::Render::RenderOperationResult ValidateGeometryLease(
      const RoR::Render::NativeGeometryExport &) const override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::RESOURCE_STALE,
        "no native geometry lease");
  }

  RoR::Render::RenderOperationResult ValidateFrameLease(
      const RoR::Render::NativeFrameSynchronization &) const override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::RESOURCE_STALE,
        "no native frame lease");
  }

  void ReleaseGeometry(std::uint64_t) noexcept override {}
};

class ProvenInterop final : public RoR::Render::NativeRenderInterop {
public:
  ProvenInterop(RoR::Render::NativeInteropCapabilityReport report,
                RoR::Render::NativeContextExport context,
                RoR::Render::NativeGeometryExport geometry,
                RoR::Render::NativeFrameSynchronization synchronization)
      : report_(report), context_(context), geometry_(geometry),
        synchronization_(synchronization) {}

  RoR::Render::NativeInteropCapabilityReport
  QueryCapabilities() const override {
    return report_;
  }

  RoR::Render::RenderOperationResult
  AcquireContext(RoR::Render::NativeContextExport &output) override {
    output = context_;
    return RoR::Render::RenderOperationResult::Success();
  }

  RoR::Render::RenderOperationResult
  AcquireGeometry(const RoR::Render::NativeGeometryExportRequest &,
                  RoR::Render::NativeGeometryExport &) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  BeginExternalFrame(std::uint64_t, std::uint64_t,
                     RoR::Render::NativeFrameSynchronization &) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  EndExternalFrame(const RoR::Render::NativeFrameSynchronization &) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult ValidateGeometryLease(
      const RoR::Render::NativeGeometryExport &geometry) const override {
    const bool matches =
        geometry.export_id == geometry_.export_id &&
        geometry.frame_id == geometry_.frame_id &&
        geometry.snapshot_id == geometry_.snapshot_id &&
        geometry.instance_id == geometry_.instance_id &&
        geometry.mesh == geometry_.mesh &&
        geometry.topology_revision == geometry_.topology_revision &&
        geometry.deformation_revision == geometry_.deformation_revision &&
        geometry.positions.buffer.value == geometry_.positions.buffer.value &&
        geometry.indices.buffer.value == geometry_.indices.buffer.value;
    return matches ? RoR::Render::RenderOperationResult::Success() : Stale();
  }

  RoR::Render::RenderOperationResult ValidateFrameLease(
      const RoR::Render::NativeFrameSynchronization &synchronization)
      const override {
    const bool matches =
        synchronization.frame_id == synchronization_.frame_id &&
        synchronization.snapshot_id == synchronization_.snapshot_id &&
        synchronization.frontend_complete_timeline.value ==
            synchronization_.frontend_complete_timeline.value &&
        synchronization.external_complete_timeline.value ==
            synchronization_.external_complete_timeline.value;
    return matches ? RoR::Render::RenderOperationResult::Success() : Stale();
  }

  void ReleaseGeometry(std::uint64_t) noexcept override {}

private:
  static RoR::Render::RenderOperationResult Unsupported() {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "test proof interop");
  }

  static RoR::Render::RenderOperationResult Stale() {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::RESOURCE_STALE,
        "test proof lease is stale");
  }

  RoR::Render::NativeInteropCapabilityReport report_;
  RoR::Render::NativeContextExport context_;
  RoR::Render::NativeGeometryExport geometry_;
  RoR::Render::NativeFrameSynchronization synchronization_;
};

class NullRayTracingBackend final
    : public RoR::Render::INativeRayTracingBackend {
public:
  RoR::Render::NativeRayTracingCapabilityReport
  QueryCapabilities() const override {
    return {};
  }

  RoR::Render::RenderOperationResult
  Initialize(RoR::Render::NativeRenderInterop &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "RT unavailable");
  }

  RoR::Render::RenderOperationResult
  Render(const RoR::Render::NativeRayTracingFrameRequest &,
         RoR::Render::RenderFrameOutput &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "RT unavailable");
  }

  RoR::Render::RenderOperationResult ValidateInteropEvidence(
      const RoR::Render::NativeGeometryExport &,
      const RoR::Render::NativeFrameSynchronization &) const override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "RT unavailable");
  }

  RoR::Render::RenderOperationResult Shutdown(std::uint64_t) override {
    return RoR::Render::RenderOperationResult::Success();
  }
};

class ProvenRayTracingBackend final
    : public RoR::Render::INativeRayTracingBackend {
public:
  ProvenRayTracingBackend(
      RoR::Render::NativeRayTracingCapabilityReport report,
      RoR::Render::NativeGeometryExport geometry,
      RoR::Render::NativeFrameSynchronization synchronization)
      : report_(report), geometry_(geometry),
        synchronization_(synchronization) {}

  RoR::Render::NativeRayTracingCapabilityReport
  QueryCapabilities() const override {
    return report_;
  }

  RoR::Render::RenderOperationResult
  Initialize(RoR::Render::NativeRenderInterop &) override {
    return RoR::Render::RenderOperationResult::Success();
  }

  RoR::Render::RenderOperationResult
  Render(const RoR::Render::NativeRayTracingFrameRequest &,
         RoR::Render::RenderFrameOutput &) override {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "test proof only");
  }

  RoR::Render::RenderOperationResult
  ValidateInteropEvidence(const RoR::Render::NativeGeometryExport &geometry,
                          const RoR::Render::NativeFrameSynchronization
                              &synchronization) const override {
    if (geometry.export_id == geometry_.export_id &&
        geometry.frame_id == geometry_.frame_id &&
        geometry.snapshot_id == geometry_.snapshot_id &&
        synchronization.frame_id == synchronization_.frame_id &&
        synchronization.snapshot_id == synchronization_.snapshot_id) {
      return RoR::Render::RenderOperationResult::Success();
    }
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::RESOURCE_STALE,
        "test RT evidence is stale");
  }

  RoR::Render::RenderOperationResult Shutdown(std::uint64_t) override {
    return RoR::Render::RenderOperationResult::Success();
  }

private:
  RoR::Render::NativeRayTracingCapabilityReport report_;
  RoR::Render::NativeGeometryExport geometry_;
  RoR::Render::NativeFrameSynchronization synchronization_;
};

class NullFrontend final : public RoR::Render::IRendererFrontend {
public:
  NullFrontend() {
    report_.frontend_kind = RoR::Render::RendererFrontendKind::CUSTOM;
    report_.frontend_name = "null-test-frontend";
    report_.frontend_version = "1";
  }

  NullFrontend(const RoR::Render::FrontendCapabilityReport &report,
               RoR::Render::NativeRenderInterop *interop)
      : report_(report), interop_(interop) {}

  RoR::Render::FrontendCapabilityReport QueryCapabilities() const override {
    return report_;
  }

  RoR::Render::RenderOperationResult
  Initialize(const RoR::Render::FrontendInitializationRequest &) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  UpdateSurface(const RoR::Render::FrontendSurfaceUpdate &, bool,
                std::uint64_t) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  SynchronizeAssets(const RoR::Render::RenderAssetDelta &) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  ReleaseResource(RoR::Render::ResourceHandle) override {
    return Unsupported();
  }

  RoR::Render::RenderOperationResult
  Render(const RoR::Render::RenderFrameRequest &,
         RoR::Render::RenderFrameOutput &) override {
    return Unsupported();
  }

  bool IsFrameComplete(std::uint64_t) const noexcept override { return false; }

  RoR::Render::RenderOperationResult WaitForFrame(std::uint64_t,
                                                  std::uint64_t) override {
    return Unsupported();
  }

  RoR::Render::NativeRenderInterop *GetNativeInterop() noexcept override {
    return interop_;
  }

  RoR::Render::RenderOperationResult Shutdown(std::uint64_t) override {
    return RoR::Render::RenderOperationResult::Success();
  }

private:
  RoR::Render::FrontendCapabilityReport report_;
  RoR::Render::NativeRenderInterop *interop_ = nullptr;

  static RoR::Render::RenderOperationResult Unsupported() {
    return RoR::Render::RenderOperationResult::Failure(
        RoR::Render::RenderOperationCode::UNSUPPORTED, "null frontend");
  }
};

void TestFrameRequestAndOutputValidation() {
  using namespace RoR::Render;

  RenderFrameRequest request = MakeFrameRequest();
  Require(ValidateRenderFrameRequest(request).ok(),
          "valid frame request was rejected");

  request.scene_snapshot.reset();
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::MISSING_REFERENCE,
          "missing scene snapshot was accepted");

  request = MakeFrameRequest();
  request.requested_outputs = static_cast<FrameOutputMask>(1U << 30U);
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::INVALID_OUTPUT_MASK,
          "unknown frame output bit was accepted");

  request = MakeFrameRequest();
  request.requested_outputs = FrameOutputMask::DEPTH;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::INVALID_OUTPUT_MASK,
          "presented frame without color was accepted");
  request.present = false;
  request.presentation_view_id = 0U;
  request.presentation_surface_revision = 0U;
  Require(ValidateRenderFrameRequest(request).ok(),
          "headless depth-only frame was rejected");

  request = MakeFrameRequest();
  request.presentation_view_id = 2U;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::MISSING_REFERENCE,
          "presentation selected a view absent from the request");
  request = MakeFrameRequest();
  request.presentation_surface_revision = 0U;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::INVALID_IDENTIFIER,
          "presentation without a surface revision was accepted");

  request = MakeFrameRequest();
  request.views.push_back(request.views.front());
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate view identifier was accepted");

  request = MakeFrameRequest();
  request.views.front().view_from_render.elements[0U] = 0.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "singular view matrix was accepted");
  request = MakeFrameRequest();
  request.views.front().view_from_render.elements[0U] = 2.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "scaled camera view matrix was accepted");
  request = MakeFrameRequest();
  request.views.front().view_from_render.elements[4U] = 0.1F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "sheared camera view matrix was accepted");
  request = MakeFrameRequest();
  request.views.front().view_from_render.elements[0U] = -1.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "reflected camera view matrix was accepted");
  request = MakeFrameRequest();
  request.views.front().clip_from_view =
      MakePerspectiveProjection(0.1F, 10000.0F, 0.2F, -0.1F);
  request.views.front().previous_clip_from_view =
      MakeOrthographicProjection(0.1F, 10000.0F, -0.2F, 0.1F);
  request.views.front().temporal_jitter_pixels = {0.5F, -0.5F};
  Require(ValidateRenderFrameRequest(request).ok(),
          "canonical asymmetric projections and separate jitter were rejected");
  request.views.front().temporal_jitter_pixels.x = 0.5001F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "temporal jitter outside half a pixel was accepted");
  request = MakeFrameRequest();
  request.views.front().clip_from_view.elements[11U] = 1.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "left-handed perspective projection was accepted");
  request = MakeFrameRequest();
  request.views.front().clip_from_view.elements[2U] = 0.01F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "oblique projection skew was accepted");
  request = MakeFrameRequest();
  request.views.front().clip_from_view.elements.fill(0.0F);
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "singular projection matrix was accepted");
  request = MakeFrameRequest();
  request.views.front().previous_clip_from_view =
      MakePerspectiveProjection(0.2F, 10000.0F);
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "previous projection with different clip planes was accepted");
  request = MakeFrameRequest();
  request.views.front().near_plane = 0.1F;
  request.views.front().far_plane = 1.0e30F;
  request.views.front().clip_from_view =
      MakeOrthographicProjection(0.1F, 1.0e30F);
  request.views.front().previous_clip_from_view =
      MakeOrthographicProjection(0.1F, 1.0e30F);
  Require(ValidateRenderFrameRequest(request).ok(),
          "valid extreme-range orthographic projection was rejected");
  request.views.front().clip_from_view.elements[10U] = 0.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "singular extreme-range orthographic depth scale was accepted");
  request = MakeFrameRequest();
  request.views.front().near_plane = 1.0e-30F;
  request.views.front().far_plane = 1.0F;
  request.views.front().clip_from_view =
      MakePerspectiveProjection(1.0e-30F, 1.0F);
  request.views.front().previous_clip_from_view =
      MakePerspectiveProjection(1.0e-30F, 1.0F);
  Require(ValidateRenderFrameRequest(request).ok(),
          "valid tiny-near perspective projection was rejected");
  request.views.front().clip_from_view.elements[14U] = 0.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "singular tiny-near perspective depth offset was accepted");
  request = MakeFrameRequest();
  request.views.front().far_plane = 1.0e30F;
  request.views.front().clip_from_view =
      MakeOrthographicProjection(0.1F, 1.0e30F);
  request.views.front().previous_clip_from_view =
      MakeOrthographicProjection(0.1F, 1.0e30F);
  request.views.front().clip_from_view.elements[10U] *= 2.0F;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "materially wrong tiny orthographic depth scale was accepted");
  request = MakeFrameRequest();
  request.views.front().previous_clip_from_view.elements[0U] =
      std::numeric_limits<float>::quiet_NaN();
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::NON_FINITE_VALUE,
          "non-finite previous projection was accepted");
  request = MakeFrameRequest();
  request.color_format = PixelFormat::R32_FLOAT;
  Require(ValidateRenderFrameRequest(request).code ==
              ValidationCode::INVALID_ENUM,
          "non-color frame format was accepted as a color request");

  RenderFrameOutput output;
  output.frame_id = 11U;
  output.snapshot_id = 7U;
  output.status = RenderFrameStatus::RENDERED;
  FrameAttachment attachment;
  attachment.view_id = 1U;
  attachment.output = FrameOutputMask::COLOR;
  attachment.format = PixelFormat::RGBA8_SRGB;
  attachment.width = 2U;
  attachment.height = 2U;
  attachment.row_pitch_bytes = 8U;
  attachment.bytes.resize(16U);
  output.attachments.push_back(attachment);
  Require(ValidateRenderFrameOutput(output).ok(),
          "valid CPU frame output was rejected");

  RenderFrameOutput object_id_output;
  object_id_output.frame_id = 12U;
  object_id_output.snapshot_id = 7U;
  object_id_output.status = RenderFrameStatus::RENDERED;
  FrameAttachment object_id_attachment;
  object_id_attachment.view_id = 1U;
  object_id_attachment.output = FrameOutputMask::OBJECT_ID;
  object_id_attachment.format = PixelFormat::RG32_UINT;
  object_id_attachment.width = 1U;
  object_id_attachment.height = 1U;
  object_id_attachment.row_pitch_bytes = 8U;
  object_id_attachment.bytes.resize(8U);
  object_id_output.attachments.push_back(object_id_attachment);
  Require(ValidateRenderFrameOutput(object_id_output).ok(),
          "lossless uint64 object-ID attachment was rejected");
  object_id_output.attachments.front().bytes.pop_back();
  Require(ValidateRenderFrameOutput(object_id_output).code ==
              ValidationCode::SIZE_MISMATCH,
          "truncated uint64 object-ID attachment was accepted");

  RenderFrameOutput material_id_output;
  material_id_output.frame_id = 13U;
  material_id_output.snapshot_id = 7U;
  material_id_output.status = RenderFrameStatus::RENDERED;
  FrameAttachment material_id_attachment;
  material_id_attachment.view_id = 1U;
  material_id_attachment.output = FrameOutputMask::MATERIAL_ID;
  material_id_attachment.format = PixelFormat::RGBA32_UINT;
  material_id_attachment.width = 1U;
  material_id_attachment.height = 1U;
  material_id_attachment.row_pitch_bytes = 16U;
  material_id_attachment.bytes.resize(16U);
  material_id_output.attachments.push_back(material_id_attachment);
  Require(ValidateRenderFrameOutput(material_id_output).ok(),
          "lossless 128-bit material-ID attachment was rejected");

  output.attachments.front().bytes.pop_back();
  Require(ValidateRenderFrameOutput(output).code ==
              ValidationCode::SIZE_MISMATCH,
          "truncated frame readback was accepted");

  output.attachments.front().bytes.resize(16U);
  output.attachments.push_back(output.attachments.front());
  Require(ValidateRenderFrameOutput(output).code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate frame attachment was accepted");

  output.attachments.resize(1U);
  output.attachments.front().bytes.clear();
  output.attachments.front().row_pitch_bytes = 0U;
  Require(ValidateRenderFrameOutput(output).code ==
              ValidationCode::EMPTY_PAYLOAD,
          "attachment without GPU or CPU payload was accepted");

  request = MakeFrameRequest();
  output = MakeCorrelatedOutput(request);
  Require(ValidateRenderFrameOutput(request, output).ok(),
          "complete request-correlated output was rejected");
  request.color_format = PixelFormat::RGBA16_FLOAT;
  output = MakeCorrelatedOutput(request);
  Require(ValidateRenderFrameOutput(request, output).ok(),
          "requested linear HDR output was rejected");
  output.attachments.front().format = PixelFormat::RGBA8_SRGB;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::INVALID_ENUM,
          "SDR attachment was accepted for an HDR request");
  request = MakeFrameRequest();

  output.attachments.back().gpu_resource =
      output.attachments.front().gpu_resource;
  Require(ValidateRenderFrameOutput(output).code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "one owned GPU output handle was transferred twice");

  output = MakeCorrelatedOutput(request);
  output.frame_id = 12U;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::MISSING_REFERENCE,
          "output for a different frame was accepted");
  output = MakeCorrelatedOutput(request);
  output.presented_view_id = 2U;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::MISSING_REFERENCE,
          "output presented a different view than requested");
  output = MakeCorrelatedOutput(request);
  output.snapshot_id = 8U;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::MISSING_REFERENCE,
          "output for a different snapshot was accepted");
  output = MakeCorrelatedOutput(request);
  output.attachments.pop_back();
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::SIZE_MISMATCH,
          "rendered output missing a requested attachment was accepted");
  output = MakeCorrelatedOutput(request);
  output.attachments.front().width = 640U;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::INVALID_DIMENSIONS,
          "attachment with the wrong view extent was accepted");
  output = MakeCorrelatedOutput(request);
  output.attachments.back().output = FrameOutputMask::OBJECT_ID;
  output.attachments.back().format = PixelFormat::RG32_UINT;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::MISSING_REFERENCE,
          "unrequested output was accepted in place of requested depth");
  output = MakeCorrelatedOutput(request);
  output.status = RenderFrameStatus::SKIPPED;
  output.presented = false;
  output.presented_view_id = 0U;
  Require(ValidateRenderFrameOutput(request, output).code ==
              ValidationCode::SIZE_MISMATCH,
          "skipped output containing rendered attachments was accepted");
  output.attachments.clear();
  Require(ValidateRenderFrameOutput(request, output).ok(),
          "well-formed skipped output was rejected");
}

void TestCapabilitiesFailClosedUntilEveryProofExists() {
  using namespace RoR::Render;

  FrontendCapabilityReport report;
  Require(!ValidateFrontendCapabilityReport(report),
          "anonymous default capability report was accepted");

  report.frontend_kind = RendererFrontendKind::OGRE_NEXT;
  report.raster_api = RasterGraphicsApi::METAL;
  report.frontend_name = "test-ogre-next";
  report.frontend_version = "0";
  report.maximum_texture_dimension_2d = 16384U;
  report.maximum_views = 4U;
  report.maximum_frames_in_flight = 3U;
  report.supported_outputs = FrameOutputMask::COLOR;
  report.raster_ready = true;
  Require(ValidateFrontendCapabilityReport(report).ok(),
          "valid raster capability report was rejected");

  FrontendCapabilityReport cross_api = report;
  cross_api.native_api = NativeGraphicsApi::VULKAN;
  cross_api.supports_native_interop = true;
  Require(ValidateFrontendCapabilityReport(cross_api).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "Metal raster was allowed to claim Vulkan same-device interop");
  cross_api.raster_api = RasterGraphicsApi::DIRECT3D11;
  cross_api.native_api = NativeGraphicsApi::DIRECT3D12;
  Require(ValidateFrontendCapabilityReport(cross_api).code ==
              ValidationCode::UNSUPPORTED_FEATURE,
          "D3D11 raster was allowed to claim same-device DXR interop");
  cross_api.raster_api = RasterGraphicsApi::DIRECT3D12;
  Require(ValidateFrontendCapabilityReport(cross_api).ok(),
          "matching D3D12 raster/native APIs were rejected");

  RenderFrameRequest capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
              .ok(),
          "color-only request within frontend limits was rejected");
  capability_request.requested_outputs =
      FrameOutputMask::COLOR | FrameOutputMask::DEPTH;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "unsupported depth attachment was accepted");
  capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  capability_request.color_format = PixelFormat::RGBA16_FLOAT;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "HDR request was accepted by an SDR-only frontend");
  FrontendCapabilityReport hdr_report = report;
  hdr_report.supports_hdr_output = true;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        hdr_report)
              .ok(),
          "HDR request was rejected after explicit capability proof");
  capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  capability_request.views.front().width = 20000U;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "view exceeding the frontend texture limit was accepted");
  capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  CameraViewRequest second_view = capability_request.views.front();
  second_view.view_id = 2U;
  capability_request.views.push_back(second_view);
  FrontendCapabilityReport one_view_report = report;
  one_view_report.maximum_views = 1U;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        one_view_report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "request exceeding the frontend view limit was accepted");
  capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  capability_request.allow_async_compute = true;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "async compute request was accepted without capability support");
  FrontendCapabilityReport async_report = report;
  async_report.supports_compute = true;
  async_report.supports_async_compute = true;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        async_report)
              .ok(),
          "async compute request was rejected after capability proof");
  capability_request = MakeFrameRequest();
  capability_request.requested_outputs = FrameOutputMask::COLOR;
  capability_request.scene_snapshot = MakeSnapshot(true, false);
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "deformable snapshot was accepted without frontend support");
  FrontendCapabilityReport dynamic_report = report;
  dynamic_report.supports_dynamic_mesh_updates = true;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        dynamic_report)
              .ok(),
          "deformable snapshot was rejected after capability proof");
  capability_request.scene_snapshot = MakeSnapshot(false, true);
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "particle snapshot was accepted without frontend support");
  FrontendCapabilityReport particle_report = report;
  particle_report.supports_particle_events = true;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        particle_report)
              .ok(),
          "particle snapshot was rejected after capability proof");
  FrontendCapabilityReport unavailable_report = report;
  unavailable_report.raster_ready = false;
  Require(ValidateRenderFrameRequestAgainstCapabilities(capability_request,
                                                        unavailable_report)
                  .code == ValidationCode::UNSUPPORTED_FEATURE,
          "request was accepted by a non-ready raster frontend");

  FrontendCapabilityReport windows_raster = report;
  windows_raster.raster_api = RasterGraphicsApi::DIRECT3D11;
  windows_raster.native_api = NativeGraphicsApi::NONE;
  Require(ValidateFrontendCapabilityReport(windows_raster).ok(),
          "D3D11 raster was incorrectly coupled to D3D12/DXR interop");
  FrontendCapabilityReport invalid_outputs = report;
  invalid_outputs.supported_outputs =
      static_cast<FrameOutputMask>(1U << 31U);
  Require(ValidateFrontendCapabilityReport(invalid_outputs).code ==
              ValidationCode::INVALID_OUTPUT_MASK,
          "unknown supported-output bit was accepted");
  invalid_outputs = report;
  invalid_outputs.supported_outputs = FrameOutputMask::NONE;
  Require(ValidateFrontendCapabilityReport(invalid_outputs).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "ready raster frontend without color output was accepted");

  report.supports_async_compute = true;
  Require(ValidateFrontendCapabilityReport(report).code ==
              ValidationCode::MISSING_REFERENCE,
          "async compute was accepted without compute support");
  report.supports_compute = true;
  Require(ValidateFrontendCapabilityReport(report).ok(),
          "compute plus async compute was rejected");

  report.native_ray_tracing_probe_passed = true;
  Require(ValidateFrontendCapabilityReport(report).code ==
              ValidationCode::MISSING_REFERENCE,
          "RT probe was accepted without API and hardware proof");

  report.supports_native_ray_tracing_api = true;
  report.native_api = NativeGraphicsApi::METAL;
  report.native_ray_tracing_hardware_accelerated = true;
  Require(ValidateFrontendCapabilityReport(report).ok(),
          "API, hardware, and probe proof was rejected");

  report.native_ray_tracing_geometry_interop_ready = true;
  Require(ValidateFrontendCapabilityReport(report).code ==
              ValidationCode::MISSING_REFERENCE,
          "geometry interop was accepted without frontend interop");

  report.supports_native_interop = true;
  report.supports_dynamic_mesh_updates = true;
  Require(ValidateFrontendCapabilityReport(report).ok(),
          "complete RT proof chain was rejected");

  NativeInteropCapabilityReport interop;
  Require(ValidateNativeInteropCapabilityReport(interop).ok(),
          "fail-closed empty interop report was rejected");
  interop.native_api = NativeGraphicsApi::METAL;
  interop.geometry_interop_proven = true;
  Require(ValidateNativeInteropCapabilityReport(interop).code ==
              ValidationCode::MISSING_REFERENCE,
          "interop proof was accepted without geometry and synchronization");
  interop.exports_vertex_buffers = true;
  interop.exports_native_context = true;
  interop.exports_index_buffers = true;
  interop.exports_deformed_meshes = true;
  interop.provides_explicit_frame_synchronization = true;
  interop.preserves_resource_generations = true;
  Require(ValidateNativeInteropCapabilityReport(interop).ok(),
          "complete native interop proof was rejected");

  NativeRayTracingCapabilityReport ray_tracing;
  Require(ValidateNativeRayTracingCapabilityReport(ray_tracing).ok(),
          "fail-closed empty RT report was rejected");
  ray_tracing.native_api = NativeGraphicsApi::METAL;
  ray_tracing.geometry_interop_ready = true;
  Require(ValidateNativeRayTracingCapabilityReport(ray_tracing).code ==
              ValidationCode::MISSING_REFERENCE,
          "native RT geometry interop bypassed the proof chain");
  ray_tracing.backend_compiled = true;
  ray_tracing.api_supported = true;
  ray_tracing.hardware_accelerated = true;
  ray_tracing.dispatch_readback_probe_passed = true;
  ray_tracing.maximum_instances = 1024U;
  Require(ValidateNativeRayTracingCapabilityReport(ray_tracing).ok(),
          "complete native RT proof chain was rejected");

  NativeGeometryInteropProofSet proof;
  proof.frontend = report;
  proof.interop = interop;
  proof.ray_tracing = ray_tracing;
  Require(ValidateNativeGeometryInteropProofSet(proof).code ==
              ValidationCode::MISSING_REFERENCE,
          "geometry interop proof was accepted without a live frontend");
  NullFrontend mismatched_frontend;
  proof.frontend_object = &mismatched_frontend;
  Require(ValidateNativeGeometryInteropProofSet(proof).code ==
              ValidationCode::MISSING_REFERENCE,
          "fabricated frontend report was accepted for a live frontend");
  NullInterop live_interop;
  proof.native_interop_object = &live_interop;
  Require(ValidateNativeGeometryInteropProofSet(proof).code ==
              ValidationCode::MISSING_REFERENCE,
          "claimed capabilities were not bound to the live interop report");

  const NativeContextExport context = MakeNativeContext();
  proof.native_context = context;
  proof.geometry_request = MakeGeometryRequest();
  proof.geometry_export = MakeGeometryExport(proof.geometry_request);
  proof.frame_synchronization =
      MakeFrameSynchronization(proof.geometry_request);
  ProvenInterop proven_interop(interop, context, proof.geometry_export,
                               proof.frame_synchronization);
  proof.native_interop_object = &proven_interop;
  NullFrontend proven_frontend(report, &proven_interop);
  proof.frontend_object = &proven_frontend;
  Require(!ValidateNativeGeometryInteropProofSet(proof),
          "geometry interop proof was accepted without a live RT backend");
  NullRayTracingBackend null_ray_tracing;
  proof.native_ray_tracing_backend = &null_ray_tracing;
  Require(!ValidateNativeGeometryInteropProofSet(proof),
          "claimed RT report was not bound to the live RT backend");
  ProvenRayTracingBackend proven_ray_tracing(ray_tracing, proof.geometry_export,
                                             proof.frame_synchronization);
  proof.native_ray_tracing_backend = &proven_ray_tracing;
  Require(ValidateNativeGeometryInteropProofSet(proof).ok(),
          "complete cross-report native rendering proof was rejected");
  proof.geometry_request.deformation_revision = 1U;
  proof.geometry_export.deformation_revision = 1U;
  Require(ValidateNativeGeometryInteropProofSet(proof).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "static base geometry was accepted as deformable interop proof");
  proof.geometry_request.deformation_revision = 4U;
  proof.geometry_export.deformation_revision = 4U;

  NativeGeometryInteropProofSet partial_proof;
  partial_proof.frontend = report;
  partial_proof.frontend.native_ray_tracing_geometry_interop_ready = false;
  partial_proof.frontend.native_ray_tracing_hardware_accelerated = false;
  partial_proof.frontend.native_ray_tracing_probe_passed = false;
  partial_proof.interop = interop;
  partial_proof.interop.geometry_interop_proven = false;
  partial_proof.ray_tracing = ray_tracing;
  partial_proof.ray_tracing.geometry_interop_ready = false;
  ProvenInterop partial_interop(partial_proof.interop, context,
                                proof.geometry_export,
                                proof.frame_synchronization);
  ProvenRayTracingBackend partial_ray_tracing(partial_proof.ray_tracing,
                                              proof.geometry_export,
                                              proof.frame_synchronization);
  partial_proof.native_interop_object = &partial_interop;
  partial_proof.native_ray_tracing_backend = &partial_ray_tracing;
  NullFrontend partial_frontend(partial_proof.frontend, &partial_interop);
  partial_proof.frontend_object = &partial_frontend;
  ValidationResult partial_validation =
      ValidateNativeGeometryInteropProofSet(partial_proof);
  Require(!partial_validation &&
              partial_validation.field == "proof.ray_tracing",
          "contradictory frontend and live RT readiness was accepted");
  partial_proof.frontend.native_ray_tracing_hardware_accelerated = true;
  partial_proof.frontend.native_ray_tracing_probe_passed = true;
  NullFrontend consistent_partial_frontend(partial_proof.frontend,
                                           &partial_interop);
  partial_proof.frontend_object = &consistent_partial_frontend;
  partial_validation = ValidateNativeGeometryInteropProofSet(partial_proof);
  Require(!partial_validation &&
              partial_validation.field == "proof.geometry_interop",
          "readiness proof passed without geometry evidence");

  proof.version = 0U;
  Require(ValidateNativeGeometryInteropProofSet(proof).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "unsupported geometry proof version was accepted");
  proof.version = kRendererFrontendContractVersion;
  proof.native_context.context_id = 2U;
  Require(!ValidateNativeGeometryInteropProofSet(proof),
          "context different from the live interop object was accepted");
  proof.native_context = context;
  proof.ray_tracing.native_api = NativeGraphicsApi::VULKAN;
  Require(!ValidateNativeGeometryInteropProofSet(proof),
          "mismatched native APIs were accepted as one proof chain");
}

void TestNativeInteropPayloadValidation() {
  using namespace RoR::Render;

  NativeContextExport context;
  context.native_api = NativeGraphicsApi::METAL;
  context.context_id = 1U;
  context.device =
      NativeToken(context.native_api, NativeObjectKind::DEVICE, 10U);
  context.graphics_queue =
      NativeToken(context.native_api, NativeObjectKind::QUEUE, 11U);
  Require(ValidateNativeContextExport(context).ok(),
          "valid Metal context export was rejected");
  context.graphics_queue.kind = NativeObjectKind::BUFFER;
  Require(ValidateNativeContextExport(context).code ==
              ValidationCode::WRONG_RESOURCE_KIND,
          "buffer token was accepted as a graphics queue");

  const NativeGeometryExportRequest geometry_request = MakeGeometryRequest();
  NativeGeometryExport geometry = MakeGeometryExport(geometry_request);
  Require(ValidateNativeGeometryExport(geometry_request, geometry,
                                       NativeGraphicsApi::METAL, 1U)
              .ok(),
          "valid native geometry export was rejected");
  geometry.positions.buffer.context_id = 2U;
  Require(ValidateNativeGeometryExport(geometry, NativeGraphicsApi::METAL, 1U)
                  .code == ValidationCode::MISSING_REFERENCE,
          "geometry from a different device context was accepted");
  geometry.positions.buffer.context_id = 1U;
  geometry.positions.size_bytes = 35U;
  Require(ValidateNativeGeometryExport(geometry, NativeGraphicsApi::METAL, 1U)
                  .code == ValidationCode::SIZE_MISMATCH,
          "undersized position export was accepted");

  geometry = MakeGeometryExport(geometry_request);
  geometry.positions.stride_bytes = 13U;
  Require(ValidateNativeGeometryExport(geometry, NativeGraphicsApi::METAL, 1U)
                  .code == ValidationCode::SIZE_MISMATCH,
          "unaligned position stride was accepted");

  geometry = MakeGeometryExport(geometry_request);
  geometry.index_count = 2U;
  Require(ValidateNativeGeometryExport(geometry, NativeGraphicsApi::METAL, 1U)
                  .code == ValidationCode::SIZE_MISMATCH,
          "incomplete triangle primitive was accepted");

  geometry = MakeGeometryExport(geometry_request);
  NativeGeometryExportRequest mismatched_request = geometry_request;
  mismatched_request.deformation_revision += 1U;
  Require(ValidateNativeGeometryExport(mismatched_request, geometry,
                                       NativeGraphicsApi::METAL, 1U)
                  .code == ValidationCode::MISSING_REFERENCE,
          "geometry from a different deformation revision was accepted");

  NativeFrameSynchronization synchronization =
      MakeFrameSynchronization(geometry_request);
  const NativeContextExport synchronization_context = MakeNativeContext();
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
              .ok(),
          "valid explicit frame synchronization was rejected");
  synchronization.version = 0U;
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
                  .code == ValidationCode::UNSUPPORTED_VERSION,
          "unsupported synchronization version was accepted");
  synchronization = MakeFrameSynchronization(geometry_request);
  synchronization.interop_queue.value += 1U;
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
                  .code == ValidationCode::MISSING_REFERENCE,
          "noncanonical interop queue was accepted");
  synchronization = MakeFrameSynchronization(geometry_request);
  synchronization.external_return_state = NativeGeometryBufferState::INVALID;
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "missing geometry return state was accepted");
  synchronization = MakeFrameSynchronization(geometry_request);
  synchronization.external_complete_timeline =
      synchronization.frontend_complete_timeline;
  synchronization.external_complete_value =
      synchronization.frontend_complete_value;
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "already-satisfied shared timeline completion was accepted");
  synchronization.external_complete_value =
      synchronization.frontend_complete_value + 1U;
  Require(ValidateNativeFrameSynchronization(synchronization,
                                             synchronization_context, true)
              .ok(),
          "strictly ordered shared timeline completion was rejected");
  synchronization = MakeFrameSynchronization(geometry_request);
  synchronization.external_complete_timeline.api = NativeGraphicsApi::VULKAN;
  Require(!ValidateNativeFrameSynchronization(synchronization,
                                              synchronization_context, true),
          "cross-API completion timeline was accepted");
}

void TestNativeRayTracingRequestValidation() {
  using namespace RoR::Render;

  NativeRayTracingFrameRequest request;
  request.frame = MakeFrameRequest();
  request.samples_per_pixel = 4U;
  request.maximum_bounces = 8U;
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "presented native RT v1 request was accepted");
  request.frame.present = false;
  request.frame.presentation_view_id = 0U;
  request.frame.presentation_surface_revision = 0U;
  Require(ValidateNativeRayTracingFrameRequest(request).ok(),
          "valid native ray-tracing request was rejected");

  request.frame.views.front().width = 2U;
  request.frame.views.front().height = 2U;
  RenderFrameOutput rt_output = MakeCorrelatedOutput(request.frame);
  for (FrameAttachment &attachment : rt_output.attachments) {
    attachment.gpu_resource = {};
    attachment.row_pitch_bytes = 8U;
    attachment.bytes.resize(16U);
  }
  Require(ValidateNativeRayTracingFrameOutput(request, rt_output).ok(),
          "valid offscreen native RT CPU readback was rejected");
  rt_output.attachments.front().gpu_resource =
      ResourceHandle::Create(ResourceKind::RENDER_TARGET, 1U, 9U, 1U);
  Require(ValidateNativeRayTracingFrameOutput(request, rt_output).code ==
              ValidationCode::WRONG_RESOURCE_KIND,
          "native RT v1 minted a frontend-owned GPU output handle");

  request.version = 0U;
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "unsupported native ray-tracing request version was accepted");
  request.version = kRendererFrontendContractVersion;

  request.samples_per_pixel = 0U;
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "zero ray-tracing samples were accepted");
  request.samples_per_pixel = kMaximumNativeRayTracingSamplesPerPixel + 1U;
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "unbounded ray-tracing samples were accepted");

  request = {};
  request.frame = MakeFrameRequest();
  request.frame.present = false;
  request.frame.presentation_view_id = 0U;
  request.frame.presentation_surface_revision = 0U;
  request.maximum_bounces = 0U;
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "zero ray-tracing bounces were accepted");
  request.maximum_bounces = 1U;
  request.frame.scene_snapshot.reset();
  Require(ValidateNativeRayTracingFrameRequest(request).code ==
              ValidationCode::MISSING_REFERENCE,
          "invalid nested frame request reached native ray tracing");
}

void TestInitializationAndAbstractInterfaces() {
  using namespace RoR::Render;

  static_assert(std::is_abstract_v<IRendererFrontend>,
                "renderer frontend must remain an abstract contract");
  static_assert(std::is_abstract_v<NativeRenderInterop>,
                "native interop must remain an abstract contract");
  static_assert(std::is_abstract_v<INativeRayTracingBackend>,
                "native RT backend must remain an abstract contract");
  static_assert(std::has_virtual_destructor_v<IRendererFrontend> &&
                    std::has_virtual_destructor_v<NativeRenderInterop> &&
                    std::has_virtual_destructor_v<INativeRayTracingBackend>,
                "renderer interfaces require virtual destruction");

  FrontendInitializationRequest request;
  request.headless = true;
  request.initial_width = 1920U;
  request.initial_height = 1080U;
  Require(ValidateFrontendInitializationRequest(request).ok(),
          "valid headless initialization was rejected");
  request.initial_surface_revision = 0U;
  Require(ValidateFrontendInitializationRequest(request).code ==
              ValidationCode::INVALID_IDENTIFIER,
          "zero initial surface revision was accepted");
  request.initial_surface_revision = 1U;
  request.initial_content_scale.x = 0.0F;
  Require(ValidateFrontendInitializationRequest(request).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "zero initial content scale was accepted");
  request.initial_content_scale = {1.0F, 1.0F};

  request.headless = false;
  Require(ValidateFrontendInitializationRequest(request).code ==
              ValidationCode::MISSING_REFERENCE,
          "windowed initialization without a window was accepted");

  request.window.system = NativeWindowSystem::COCOA;
  request.window.surface = 1U;
  request.window.generation = 1U;
  Require(ValidateFrontendInitializationRequest(request).ok(),
          "valid Cocoa window token was rejected");

  request.window.system = NativeWindowSystem::X11;
  Require(ValidateFrontendInitializationRequest(request).code ==
              ValidationCode::INVALID_HANDLE,
          "X11 window without its Display connection was accepted");
  request.window.connection = 2U;
  Require(ValidateFrontendInitializationRequest(request).ok(),
          "valid X11 Display plus Window pair was rejected");

  FrontendSurfaceUpdate surface;
  surface.surface_revision = 2U;
  surface.window.system = NativeWindowSystem::COCOA;
  surface.window.surface = 3U;
  surface.window.generation = 1U;
  surface.pixel_width = 2560U;
  surface.pixel_height = 1440U;
  surface.content_scale = {2.0F, 2.0F};
  Require(ValidateFrontendSurfaceUpdate(surface, false).ok(),
          "valid high-DPI Cocoa surface update was rejected");
  FrontendSurfaceUpdate next_surface = surface;
  next_surface.surface_revision = 3U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, true)
              .ok(),
          "strictly newer drained surface transition was rejected");
  next_surface.surface_revision = 2U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, true)
                  .code == ValidationCode::NON_DETERMINISTIC_ORDER,
          "equal surface revision rollback was accepted");
  next_surface.surface_revision = 3U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, false)
                  .code == ValidationCode::MISSING_REFERENCE,
          "surface replacement with an in-flight old frame was accepted");
  next_surface.window.generation = 2U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, true)
                  .code == ValidationCode::INVALID_IDENTIFIER,
          "same native window identity with a new generation was accepted");
  next_surface.window.surface = 4U;
  next_surface.window.generation = 1U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, true)
                  .code == ValidationCode::NON_DETERMINISTIC_ORDER,
          "replacement native window with a stale generation was accepted");
  next_surface.window.generation = 2U;
  Require(ValidateFrontendSurfaceTransition(surface, next_surface, false, true)
              .ok(),
          "replacement native window with a newer generation was rejected");
  next_surface = surface;
  next_surface.surface_revision = (std::numeric_limits<std::uint64_t>::max)();
  FrontendSurfaceUpdate exhausted_surface = surface;
  exhausted_surface.surface_revision =
      (std::numeric_limits<std::uint64_t>::max)();
  Require(ValidateFrontendSurfaceTransition(exhausted_surface, next_surface,
                                            false, true)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "terminal surface revision exhaustion was accepted");

  FrontendSurfaceUpdate presentation_surface = surface;
  presentation_surface.surface_revision = 1U;
  presentation_surface.pixel_width = 1280U;
  presentation_surface.pixel_height = 720U;
  RenderFrameRequest presentation_request = MakeFrameRequest();
  Require(ValidateRenderFramePresentation(presentation_request,
                                          presentation_surface)
              .ok(),
          "exact surface/view presentation match was rejected");
  presentation_surface.pixel_width = 2560U;
  presentation_surface.pixel_height = 1440U;
  Require(ValidateRenderFramePresentation(presentation_request,
                                          presentation_surface)
                  .code == ValidationCode::INVALID_DIMENSIONS,
          "implicit drawable scaling was accepted for presentation");
  presentation_surface.pixel_width = 1280U;
  presentation_surface.pixel_height = 720U;
  presentation_surface.surface_revision = 2U;
  Require(ValidateRenderFramePresentation(presentation_request,
                                          presentation_surface)
                  .code == ValidationCode::MISSING_REFERENCE,
          "frame targeting a stale surface revision was accepted");

  surface.suspended = true;
  surface.pixel_width = 0U;
  surface.pixel_height = 0U;
  Require(ValidateFrontendSurfaceUpdate(surface, false).ok(),
          "valid suspended surface update was rejected");
  surface.pixel_width = 1U;
  Require(ValidateFrontendSurfaceUpdate(surface, false).code ==
              ValidationCode::INVALID_DIMENSIONS,
          "suspended surface with a nonzero dimension was accepted");

  surface = {};
  surface.surface_revision = 3U;
  surface.pixel_width = 1920U;
  surface.pixel_height = 1080U;
  Require(ValidateFrontendSurfaceUpdate(surface, true).ok(),
          "valid headless offscreen resize was rejected");
  surface.window.system = NativeWindowSystem::COCOA;
  surface.window.surface = 3U;
  surface.window.generation = 1U;
  Require(ValidateFrontendSurfaceUpdate(surface, true).code ==
              ValidationCode::INVALID_HANDLE,
          "headless surface retained a presentation window");

  surface = {};
  surface.surface_revision = 4U;
  surface.window.system = NativeWindowSystem::X11;
  surface.window.surface = 4U;
  surface.window.generation = 1U;
  surface.pixel_width = 1280U;
  surface.pixel_height = 720U;
  Require(ValidateFrontendSurfaceUpdate(surface, false).code ==
              ValidationCode::INVALID_HANDLE,
          "X11 surface without its Display connection was accepted");
  surface.window.connection = 5U;
  Require(ValidateFrontendSurfaceUpdate(surface, false).ok(),
          "valid X11 surface update was rejected");
  surface.content_scale.x = (std::numeric_limits<float>::quiet_NaN)();
  Require(ValidateFrontendSurfaceUpdate(surface, false).code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "non-finite surface content scale was accepted");
  surface.content_scale = {1.0F, 1.0F};
  surface.version = 0U;
  Require(ValidateFrontendSurfaceUpdate(surface, false).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "unsupported surface update version was accepted");

  NullFrontend frontend;
  NullInterop interop;
  NullRayTracingBackend ray_tracing;
  Require(frontend.GetNativeInterop() == nullptr,
          "null frontend exposed native interop");
  Require(!ray_tracing.Initialize(interop),
          "null native RT backend initialized successfully");
  Require(!frontend.QueryCapabilities().supports_native_ray_tracing_api,
          "null frontend enabled RT by default");
}

} // namespace

int main() {
  TestFrameRequestAndOutputValidation();
  TestCapabilitiesFailClosedUntilEveryProofExists();
  TestNativeInteropPayloadValidation();
  TestNativeRayTracingRequestValidation();
  TestInitializationAndAbstractInterfaces();
  std::cout << "renderer frontend contract tests passed\n";
  return EXIT_SUCCESS;
}
