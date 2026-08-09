/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontendTransportDispatcher.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

static_assert(
    !std::is_copy_constructible_v<RendererFrontendTransportDispatcher>);
static_assert(
    !std::is_move_constructible_v<RendererFrontendTransportDispatcher>);

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "frontend transport dispatcher test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireStatus(RendererFrontendTransportDispatchStatus actual,
                   RendererFrontendTransportDispatchStatus expected,
                   const char *message) {
  if (actual != expected) {
    std::cerr << "frontend transport dispatcher test failed: " << message
              << " (actual=" << ToString(actual)
              << ", expected=" << ToString(expected) << ")\n";
    std::exit(EXIT_FAILURE);
  }
}

RenderBridgeSessionIdentity Session(std::uint8_t seed) {
  RenderBridgeSessionIdentity session{};
  for (std::size_t index = 0U; index < session.size(); ++index) {
    session[index] = static_cast<std::uint8_t>(
        static_cast<unsigned>(seed) + static_cast<unsigned>(index * 17U));
  }
  if (seed == 0U) {
    session.back() = 1U;
  }
  return session;
}

Matrix4x4 Perspective(float near_plane = 0.1F, float far_plane = 10000.0F) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

CameraViewRequest Camera(std::uint64_t view_id = 7U) {
  CameraViewRequest camera;
  camera.view_id = view_id;
  camera.width = 640U;
  camera.height = 480U;
  camera.clip_from_view = Perspective();
  camera.previous_clip_from_view = camera.clip_from_view;
  Require(ValidateCameraViewRequest(camera).ok(),
          "camera fixture must be valid");
  return camera;
}

RenderAssetDelta AssetDelta(std::uint64_t registry_id, std::uint64_t sequence,
                            bool full_snapshot) {
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.base_sequence = full_snapshot ? 0U : sequence - 1U;
  delta.sequence = sequence;
  delta.full_snapshot = full_snapshot;
  Require(ValidateRenderAssetDelta(delta).ok(),
          "empty catalog transaction fixture must be valid");
  return delta;
}

std::shared_ptr<const SceneSnapshot> Scene(std::uint64_t snapshot_id,
                                           std::uint64_t registry_id,
                                           std::uint64_t asset_sequence,
                                           std::uint64_t simulation_tick =
                                               (std::numeric_limits<
                                                   std::uint64_t>::max)()) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick =
      simulation_tick == (std::numeric_limits<std::uint64_t>::max)()
          ? snapshot_id * 10U
          : simulation_tick;
  descriptor.simulation_time_seconds =
      static_cast<double>(descriptor.simulation_tick) / 2000.0;
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "scene fixture must freeze successfully");
  return created.snapshot;
}

RenderTransportStreamFrameResult
CompleteFrame(const std::vector<std::uint8_t> &bytes) {
  RenderTransportStreamDecoder stream(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  const RenderTransportStreamResult accepted =
      stream.Accept(bytes.data(), bytes.size());
  Require(accepted.status == RenderTransportStreamStatus::FRAME_READY,
          "encoded transport fixture must become one complete frame");
  Require(accepted.bytes_consumed == bytes.size(),
          "stream fixture must consume exactly one frame");
  RenderTransportStreamFrameResult frame = stream.TakeFrame();
  Require(frame.ok(), "stream fixture must return validated frame ownership");
  return frame;
}

RenderTransportStreamFrameResult AssetFrame(std::uint64_t envelope_sequence,
                                            const RenderAssetDelta &delta) {
  const RenderAssetDeltaTransportEncodeResult encoded =
      EncodeRenderAssetDeltaTransportFrame(envelope_sequence, delta);
  Require(encoded.ok(), "asset fixture must encode");
  return CompleteFrame(encoded.bytes);
}

RenderTransportStreamFrameResult
SceneFrame(std::uint64_t envelope_sequence,
           const std::shared_ptr<const SceneSnapshot> &scene,
           const CameraViewRequest &camera = Camera()) {
  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(envelope_sequence, *scene, camera);
  Require(encoded.ok(), "scene fixture must encode");
  return CompleteFrame(encoded.bytes);
}

RenderTransportStreamFrameResult BoundaryFrame(
    std::uint64_t envelope_sequence, std::uint64_t registry_id,
    std::uint64_t asset_sequence, std::uint64_t finalized_snapshot_id,
    std::uint64_t completed_generation = 1U) {
  SceneGenerationBoundary boundary;
  boundary.registry_id = registry_id;
  boundary.completed_generation = completed_generation;
  boundary.next_generation = completed_generation + 1U;
  boundary.asset_sequence = asset_sequence;
  boundary.finalized_snapshot_id = finalized_snapshot_id;
  const RenderTransportEnvelopeEncodeResult encoded =
      EncodeSceneGenerationBoundaryFrame(envelope_sequence, boundary);
  Require(encoded.ok(), "scene-generation boundary fixture must encode");
  return CompleteFrame(encoded.bytes);
}

void RewriteEnvelopeDigest(std::vector<std::uint8_t> &bytes) {
  Require(bytes.size() >= kRenderTransportEnvelopeHeaderBytes,
          "digest fixture requires a complete envelope");
  const std::size_t payload_size =
      bytes.size() - kRenderTransportEnvelopeHeaderBytes;
  const auto digest = ComputeRenderTransportPayloadDigest(
      bytes.data() + kRenderTransportEnvelopeHeaderBytes, payload_size);
  std::copy(digest.begin(), digest.end(), bytes.begin() + 32U);
}

RendererFrontendPresentationPolicy OffscreenPolicy() {
  RendererFrontendPresentationPolicy policy;
  policy.present = false;
  policy.presentation_surface_revision = 0U;
  return policy;
}

RendererFrontendPresentationPolicy PresentedPolicy() {
  RendererFrontendPresentationPolicy policy;
  policy.requested_outputs = FrameOutputMask::COLOR | FrameOutputMask::DEPTH;
  policy.color_format = PixelFormat::RGBA16_FLOAT;
  policy.presentation_surface_revision = 9U;
  policy.presentation_drawable_width = 640U;
  policy.presentation_drawable_height = 480U;
  policy.present = true;
  policy.allow_async_compute = true;
  policy.retire_scene_on_presentation_extent_mismatch = true;
  return policy;
}

RendererFrontendPresentationPolicy RetiredPolicy() {
  RendererFrontendPresentationPolicy policy = OffscreenPolicy();
  policy.retire_scene_without_render = true;
  return policy;
}

PixelFormat FormatForOutput(FrameOutputMask output, PixelFormat color_format) {
  switch (output) {
  case FrameOutputMask::COLOR:
    return color_format;
  case FrameOutputMask::DEPTH:
    return PixelFormat::R32_FLOAT;
  case FrameOutputMask::MOTION_VECTORS:
    return PixelFormat::RG16_FLOAT;
  case FrameOutputMask::OBJECT_ID:
    return PixelFormat::RG32_UINT;
  case FrameOutputMask::SURFACE_NORMAL:
    return PixelFormat::RGBA16_SNORM;
  case FrameOutputMask::MATERIAL_ID:
    return PixelFormat::RGBA32_UINT;
  case FrameOutputMask::NONE:
    break;
  }
  return PixelFormat::INVALID;
}

class FakeFrontend final : public IRendererFrontend {
public:
  FakeFrontend() {
    capabilities_.frontend_kind = RendererFrontendKind::CUSTOM;
    capabilities_.raster_api = RasterGraphicsApi::VULKAN;
    capabilities_.native_api = NativeGraphicsApi::VULKAN;
    capabilities_.frontend_name = "transport-dispatch-test";
    capabilities_.frontend_version = "1";
    capabilities_.maximum_texture_dimension_2d = 8192U;
    capabilities_.maximum_views = 4U;
    capabilities_.maximum_frames_in_flight = 2U;
    capabilities_.supported_outputs =
        FrameOutputMask::COLOR | FrameOutputMask::DEPTH |
        FrameOutputMask::MOTION_VECTORS | FrameOutputMask::OBJECT_ID |
        FrameOutputMask::SURFACE_NORMAL | FrameOutputMask::MATERIAL_ID;
    capabilities_.raster_ready = true;
    capabilities_.supports_hdr_output = true;
    capabilities_.supports_compute = true;
    capabilities_.supports_async_compute = true;
    capabilities_.supports_dynamic_mesh_updates = true;
    capabilities_.supports_particle_events = true;
    Require(ValidateFrontendCapabilityReport(capabilities_).ok(),
            "fake frontend capabilities must be valid");
  }

  FrontendCapabilityReport QueryCapabilities() const override {
    return capabilities_;
  }

  RenderOperationResult
  Initialize(const FrontendInitializationRequest &) override {
    return RenderOperationResult::Success();
  }

  RenderOperationResult UpdateSurface(const FrontendSurfaceUpdate &, bool,
                                      std::uint64_t) override {
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override {
    calls.emplace_back("synchronize-assets");
    synchronized_registry_ids.push_back(delta.registry_id);
    synchronized_asset_sequences.push_back(delta.sequence);
    if (fail_synchronize) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, "injected asset failure");
    }
    return RenderOperationResult::Success();
  }

  RenderOperationResult ResetSceneGeneration(
      std::uint64_t next_generation) override {
    calls.emplace_back("reset-scene-generation");
    reset_generations.push_back(next_generation);
    if (fail_scene_generation_reset) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "injected scene-generation reset failure");
    }
    simulation_lineage_initialized = false;
    last_simulation_time_seconds = 0.0;
    return RenderOperationResult::Success();
  }

  RenderOperationResult ReleaseResource(ResourceHandle resource) override {
    calls.emplace_back("release-resource");
    release_attempts.push_back(resource);
    if (release_attempts.size() == fail_release_attempt) {
      return RenderOperationResult::Failure(RenderOperationCode::RESOURCE_STALE,
                                            "injected release failure");
    }
    return RenderOperationResult::Success();
  }

  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override {
    calls.emplace_back("render");
    rendered_requests.push_back(request);
    if (simulation_lineage_initialized &&
        request.scene_snapshot->simulation_time_seconds() <
            last_simulation_time_seconds) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "unmarked simulation-time rollback");
    }
    if (!fail_render || populate_before_render_failure) {
      PopulateOutput(request, output);
    }
    if (fail_render) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, "injected render failure");
    }
    simulation_lineage_initialized = true;
    last_simulation_time_seconds =
        request.scene_snapshot->simulation_time_seconds();
    return RenderOperationResult::Success();
  }

  bool IsFrameComplete(std::uint64_t) const noexcept override { return true; }

  RenderOperationResult WaitForFrame(std::uint64_t frame_id,
                                     std::uint64_t timeout) override {
    calls.emplace_back("wait-frame");
    waited_frame_ids.push_back(frame_id);
    waited_timeouts.push_back(timeout);
    if (fail_wait) {
      return RenderOperationResult::Failure(RenderOperationCode::DEVICE_LOST,
                                            "injected wait failure");
    }
    return RenderOperationResult::Success();
  }

  NativeRenderInterop *GetNativeInterop() noexcept override { return nullptr; }

  RenderOperationResult Shutdown(std::uint64_t) override {
    return RenderOperationResult::Success();
  }

  FrontendCapabilityReport capabilities_;
  std::vector<std::string> calls;
  std::vector<std::uint64_t> synchronized_registry_ids;
  std::vector<std::uint64_t> synchronized_asset_sequences;
  std::vector<RenderFrameRequest> rendered_requests;
  std::vector<std::uint64_t> waited_frame_ids;
  std::vector<std::uint64_t> waited_timeouts;
  std::vector<ResourceHandle> release_attempts;
  std::vector<std::uint64_t> reset_generations;
  bool fail_synchronize = false;
  bool fail_render = false;
  bool populate_before_render_failure = false;
  bool fail_wait = false;
  bool fail_scene_generation_reset = false;
  bool simulation_lineage_initialized = false;
  double last_simulation_time_seconds = 0.0;
  bool invalid_output = false;
  bool duplicate_output_resource = false;
  std::size_t fail_release_attempt = 0U;

private:
  void PopulateOutput(const RenderFrameRequest &request,
                      RenderFrameOutput &output) {
    output.frame_id = request.frame_id;
    output.snapshot_id = request.scene_snapshot->snapshot_id();
    output.status = RenderFrameStatus::RENDERED;
    output.presented = request.present;
    output.presented_view_id = request.presentation_view_id;
    std::uint32_t slot = 0U;
    constexpr std::array<FrameOutputMask, 6U> ordered_outputs{{
        FrameOutputMask::COLOR,
        FrameOutputMask::DEPTH,
        FrameOutputMask::MOTION_VECTORS,
        FrameOutputMask::OBJECT_ID,
        FrameOutputMask::SURFACE_NORMAL,
        FrameOutputMask::MATERIAL_ID,
    }};
    for (const FrameOutputMask output_kind : ordered_outputs) {
      if (!HasFrameOutput(request.requested_outputs, output_kind)) {
        continue;
      }
      FrameAttachment attachment;
      attachment.view_id = request.views.front().view_id;
      attachment.output = output_kind;
      attachment.format = FormatForOutput(output_kind, request.color_format);
      attachment.width = request.views.front().width;
      attachment.height = request.views.front().height;
      const std::uint32_t resource_slot =
          duplicate_output_resource && slot != 0U ? 0U : slot;
      attachment.gpu_resource = ResourceHandle::Create(
          ResourceKind::RENDER_TARGET, 77U, resource_slot, 1U);
      output.attachments.push_back(std::move(attachment));
      ++slot;
    }
    if (invalid_output) {
      ++output.snapshot_id;
    }
  }
};

void TestIdentityDerivationAndStatusDomain() {
  RenderBridgeSessionIdentity zero{};
  Require(DeriveRenderAssetRegistryIdFromBridgeSession(zero) == 0U,
          "invalid all-zero bridge session derived a registry");

  const RenderBridgeSessionIdentity first = Session(1U);
  const RenderBridgeSessionIdentity second = Session(201U);
  RenderBridgeSessionIdentity maximum{};
  maximum.fill(0xFFU);
  const std::uint64_t first_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(first);
  const std::uint64_t second_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(second);
  const std::uint64_t maximum_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(maximum);
  Require(first_id == 0x461eee86bdb42eeaULL,
          "domain-separated identity golden vector changed");
  Require(first_id != 0U && second_id != 0U && maximum_id != 0U,
          "valid session derived zero registry identity");
  Require(first_id != (std::numeric_limits<std::uint64_t>::max)() &&
              second_id != (std::numeric_limits<std::uint64_t>::max)() &&
              maximum_id != (std::numeric_limits<std::uint64_t>::max)(),
          "valid session derived reserved maximum registry identity");
  Require(first_id != second_id && first_id != maximum_id &&
              second_id != maximum_id,
          "distinct identity fixtures collided");
  Require(first_id == DeriveRenderAssetRegistryIdFromBridgeSession(first),
          "registry derivation was not deterministic");

  for (unsigned value = 0U; value <= 18U; ++value) {
    const auto status =
        static_cast<RendererFrontendTransportDispatchStatus>(value);
    Require(IsKnownRendererFrontendTransportDispatchStatus(status),
            "known dispatcher status was rejected");
    Require(std::string(ToString(status)) != "unknown",
            "known dispatcher status lacked stable text");
  }
  Require(!IsKnownRendererFrontendTransportDispatchStatus(
              static_cast<RendererFrontendTransportDispatchStatus>(19U)),
          "unknown dispatcher status was accepted");
}

void TestSceneGenerationBoundaryCodecFailClosed() {
  const std::uint64_t registry_id = 77U;
  SceneGenerationBoundary boundary;
  boundary.registry_id = registry_id;
  boundary.completed_generation = 1U;
  boundary.next_generation = 2U;
  boundary.asset_sequence = 9U;
  boundary.finalized_snapshot_id = 12U;
  const RenderTransportEnvelopeEncodeResult encoded =
      EncodeSceneGenerationBoundaryFrame(1U, boundary);
  Require(encoded.ok(), "valid boundary did not encode");

  {
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id, sequence);
    const SceneGenerationBoundaryTransportDecodeResult decoded =
        decoder.Accept(encoded.bytes);
    Require(decoded.ok() && decoded.sequence == 1U &&
                decoded.boundary.registry_id == registry_id &&
                decoded.boundary.completed_generation == 1U &&
                decoded.boundary.next_generation == 2U &&
                decoded.boundary.asset_sequence == 9U &&
                decoded.boundary.finalized_snapshot_id == 12U &&
                sequence.last_accepted_sequence() == 1U,
            "boundary codec changed exact fields or sequence");
    Require(decoder.Accept(encoded.bytes).status ==
                RenderTransportStatus::REPLAYED_SEQUENCE,
            "boundary replay advanced sequence");
  }
  {
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id, sequence);
    const RenderTransportEnvelopeEncodeResult out_of_order =
        EncodeSceneGenerationBoundaryFrame(2U, boundary);
    Require(out_of_order.ok() &&
                decoder.Accept(out_of_order.bytes).status ==
                    RenderTransportStatus::OUT_OF_ORDER_SEQUENCE &&
                decoder.Accept(encoded.bytes).ok(),
            "out-of-order boundary poisoned recoverable decoder state");
  }
  {
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id + 1U,
                                                      sequence);
    Require(decoder.Accept(encoded.bytes).status ==
                RenderTransportStatus::REGISTRY_VALIDATION_FAILED &&
                sequence.last_accepted_sequence() == 0U,
            "cross-registry boundary advanced sequence");
  }
  {
    std::vector<std::uint8_t> truncated = encoded.bytes;
    truncated.pop_back();
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id, sequence);
    Require(decoder.Accept(truncated).status ==
                RenderTransportStatus::FRAME_SIZE_MISMATCH,
            "truncated boundary was admitted");
  }
  {
    std::vector<std::uint8_t> reserved = encoded.bytes;
    reserved[kRenderTransportEnvelopeHeaderBytes + 4U] = 1U;
    RewriteEnvelopeDigest(reserved);
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id, sequence);
    Require(decoder.Accept(reserved).status ==
                RenderTransportStatus::MALFORMED_PAYLOAD,
            "nonzero boundary reserved field was admitted");
  }
  {
    std::vector<std::uint8_t> version = encoded.bytes;
    version[kRenderTransportEnvelopeHeaderBytes] = 2U;
    RewriteEnvelopeDigest(version);
    RenderTransportSequenceState sequence;
    SceneGenerationBoundaryTransportDecoder decoder(registry_id, sequence);
    Require(decoder.Accept(version).status ==
                RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION,
            "unknown boundary payload version was admitted");
  }
}

void TestAuthenticatedSceneGenerationBoundaryAndUnmarkedRollback() {
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(23U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok(),
            "generation fixture asset did not synchronize");
    Require(dispatcher
                .Dispatch(SceneFrame(2U, Scene(40U, registry_id, 1U, 500U)),
                          OffscreenPolicy())
                .ok(),
            "old generation scene did not render");
    const RendererFrontendTransportDispatchResult reset = dispatcher.Dispatch(
        BoundaryFrame(3U, registry_id, 1U, 40U), OffscreenPolicy());
    RequireStatus(
        reset.status,
        RendererFrontendTransportDispatchStatus::
            SCENE_GENERATION_BOUNDARY_CONSUMED,
        "authenticated generation boundary was not consumed");
    Require(frontend.reset_generations == std::vector<std::uint64_t>{2U} &&
                !frontend.calls.empty() &&
                frontend.calls.back() == "reset-scene-generation",
            "boundary did not reset only after final scene completion");
    const RendererFrontendTransportDispatchResult reloaded =
        dispatcher.Dispatch(
            SceneFrame(4U, Scene(41U, registry_id, 1U, 0U)),
            OffscreenPolicy());
    RequireStatus(reloaded.status,
                  RendererFrontendTransportDispatchStatus::
                      SCENE_FRAME_COMPLETED,
                  "marked reload tick zero did not render");
    Require(!dispatcher.terminal() &&
                frontend.rendered_requests.size() == 2U,
            "marked generation reset poisoned the live dispatcher");
  }

  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(24U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok(),
            "rollback fixture asset did not synchronize");
    Require(dispatcher
                .Dispatch(SceneFrame(2U, Scene(50U, registry_id, 1U, 500U)),
                          OffscreenPolicy())
                .ok(),
            "rollback fixture old scene did not render");
    const RendererFrontendTransportDispatchResult rollback =
        dispatcher.Dispatch(
            SceneFrame(3U, Scene(51U, registry_id, 1U, 0U)),
            OffscreenPolicy());
    RequireStatus(rollback.status,
                  RendererFrontendTransportDispatchStatus::
                      FAILED_FRONTEND_RENDER,
                  "unmarked simulation-time rollback was admitted");
    Require(rollback.terminal && frontend.reset_generations.empty(),
            "unmarked rollback reset frontend history");
  }

  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(25U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok(),
            "mismatch fixture asset did not synchronize");
    Require(dispatcher
                .Dispatch(SceneFrame(2U, Scene(60U, registry_id, 1U, 500U)),
                          OffscreenPolicy())
                .ok(),
            "mismatch fixture final scene did not render");
    const RendererFrontendTransportDispatchResult mismatch =
        dispatcher.Dispatch(BoundaryFrame(3U, registry_id, 1U, 59U),
                            OffscreenPolicy());
    RequireStatus(mismatch.status,
                  RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                  "boundary for another final snapshot was admitted");
    Require(mismatch.terminal && frontend.reset_generations.empty(),
            "mismatched boundary mutated frontend generation");
  }


  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(26U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok(),
            "early-boundary fixture asset did not synchronize");
    const RendererFrontendTransportDispatchResult early =
        dispatcher.Dispatch(BoundaryFrame(2U, registry_id, 1U, 1U),
                            OffscreenPolicy());
    RequireStatus(early.status,
                  RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                  "boundary before final empty scene was admitted");
    Require(early.terminal && frontend.reset_generations.empty(),
            "early boundary reset frontend state");
  }

  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(27U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok() &&
                dispatcher
                    .Dispatch(
                        SceneFrame(2U, Scene(70U, registry_id, 1U, 500U)),
                        OffscreenPolicy())
                    .ok(),
            "asset-sequence mismatch fixture did not initialize");
    const RendererFrontendTransportDispatchResult mismatch =
        dispatcher.Dispatch(BoundaryFrame(3U, registry_id, 2U, 70U),
                            OffscreenPolicy());
    RequireStatus(mismatch.status,
                  RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                  "boundary with foreign asset sequence was admitted");
    Require(frontend.reset_generations.empty(),
            "foreign asset sequence reset frontend state");
  }

  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(28U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok() &&
                dispatcher
                    .Dispatch(
                        SceneFrame(2U, Scene(80U, registry_id, 1U, 500U)),
                        OffscreenPolicy())
                    .ok(),
            "generation mismatch fixture did not initialize");
    const RendererFrontendTransportDispatchResult mismatch =
        dispatcher.Dispatch(
            BoundaryFrame(3U, registry_id, 1U, 80U, 2U),
            OffscreenPolicy());
    RequireStatus(mismatch.status,
                  RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                  "boundary with foreign generation was admitted");
    Require(frontend.reset_generations.empty(),
            "foreign generation reset frontend state");
  }

  {
    FakeFrontend frontend;
    frontend.fail_scene_generation_reset = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(29U));
    const std::uint64_t registry_id = dispatcher.registry_id();
    Require(dispatcher
                .Dispatch(AssetFrame(1U, AssetDelta(registry_id, 1U, true)),
                          OffscreenPolicy())
                .ok() &&
                dispatcher
                    .Dispatch(
                        SceneFrame(2U, Scene(90U, registry_id, 1U, 500U)),
                        OffscreenPolicy())
                    .ok(),
            "reset-failure fixture did not initialize");
    const RendererFrontendTransportDispatchResult failed =
        dispatcher.Dispatch(BoundaryFrame(3U, registry_id, 1U, 90U),
                            OffscreenPolicy());
    RequireStatus(
        failed.status,
        RendererFrontendTransportDispatchStatus::
            FAILED_FRONTEND_SCENE_GENERATION_RESET,
        "frontend scene-generation reset failure was not terminal");
    Require(failed.terminal && dispatcher.terminal() &&
                frontend.reset_generations ==
                    std::vector<std::uint64_t>{2U},
            "reset failure did not poison dispatcher exactly once");
  }
}

void TestPresentationPolicyValidation() {
  Require(ValidateRendererFrontendPresentationPolicy(OffscreenPolicy()).ok(),
          "valid UI-free offscreen policy was rejected");
  Require(ValidateRendererFrontendPresentationPolicy(PresentedPolicy()).ok(),
          "valid native presentation policy was rejected");
  Require(ValidateRendererFrontendPresentationPolicy(RetiredPolicy()).ok(),
          "valid stale-scene retirement policy was rejected");

  RendererFrontendPresentationPolicy invalid = OffscreenPolicy();
  invalid.presentation_surface_revision = 1U;
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_IDENTIFIER,
          "offscreen policy named a native surface");
  invalid = PresentedPolicy();
  invalid.requested_outputs = FrameOutputMask::DEPTH;
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_OUTPUT_MASK,
          "presented policy omitted color");
  invalid = OffscreenPolicy();
  invalid.requested_outputs = static_cast<FrameOutputMask>(1U << 31U);
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_OUTPUT_MASK,
          "unknown output policy was accepted");
  invalid = PresentedPolicy();
  invalid.retire_scene_without_render = true;
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_IDENTIFIER,
          "retired scene policy retained native presentation identity");
  invalid = PresentedPolicy();
  invalid.presentation_drawable_width = 0U;
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_DIMENSIONS,
          "extent guard accepted an empty drawable extent");
  invalid = PresentedPolicy();
  invalid.retire_scene_on_presentation_extent_mismatch = false;
  Require(ValidateRendererFrontendPresentationPolicy(invalid).code ==
              ValidationCode::INVALID_DIMENSIONS,
          "unguarded policy retained a presentation drawable extent");
}

void TestRetiredSceneAdvancesLineageWithoutFrontendWork() {
  FakeFrontend frontend;
  const RenderBridgeSessionIdentity session = Session(17U);
  RendererFrontendTransportDispatcher dispatcher(frontend, session);
  const std::uint64_t registry_id = dispatcher.registry_id();
  const auto asset = AssetFrame(1U, AssetDelta(registry_id, 1U, true));
  Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
          "retirement fixture asset did not synchronize");

  const auto stale_scene = SceneFrame(2U, Scene(11U, registry_id, 1U));
  const RendererFrontendTransportDispatchResult retired =
      dispatcher.Dispatch(stale_scene, RetiredPolicy());
  RequireStatus(retired.status,
                RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED,
                "stale scene was not retired nonterminally");
  Require(retired.ok() && !retired.terminal &&
              retired.scene_snapshot_id == 11U &&
              retired.resources_released == 0U &&
              dispatcher.last_accepted_sequence() == 2U &&
              dispatcher.next_expected_sequence() == 3U &&
              frontend.rendered_requests.empty() &&
              frontend.waited_frame_ids.empty() && frontend.calls.size() == 1U,
          "retired scene invoked frontend work or lost forward lineage");

  const auto current_scene = SceneFrame(3U, Scene(12U, registry_id, 1U));
  const RendererFrontendTransportDispatchResult rendered =
      dispatcher.Dispatch(current_scene, PresentedPolicy());
  RequireStatus(rendered.status,
                RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED,
                "dispatcher did not continue after stale-scene retirement");
  Require(rendered.scene_snapshot_id == 12U && !rendered.terminal &&
              frontend.rendered_requests.size() == 1U &&
              dispatcher.next_expected_sequence() == 4U,
          "post-retirement scene did not render on the same live dispatcher");
}

void TestStalePresentationExtentRetiresAfterDecode() {
  FakeFrontend frontend;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(18U));
  const auto asset = AssetFrame(
      1U, AssetDelta(dispatcher.registry_id(), 1U, true));
  Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
          "extent-retirement fixture asset did not synchronize");

  RendererFrontendPresentationPolicy resized = PresentedPolicy();
  resized.presentation_surface_revision = 10U;
  resized.presentation_drawable_width = 1280U;
  resized.presentation_drawable_height = 720U;
  const auto stale = SceneFrame(
      2U, Scene(21U, dispatcher.registry_id(), 1U), Camera());
  const RendererFrontendTransportDispatchResult retired =
      dispatcher.Dispatch(stale, resized);
  RequireStatus(retired.status,
                RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED,
                "pre-resize camera became presentable at the new extent");
  Require(retired.scene_snapshot_id == 21U && !retired.terminal &&
              frontend.rendered_requests.empty() &&
              frontend.waited_frame_ids.empty() &&
              dispatcher.next_expected_sequence() == 3U,
          "extent retirement invoked frontend work or lost lineage");
}

void TestInterleavedAssetsScenesAndPresentation() {
  FakeFrontend frontend;
  const RenderBridgeSessionIdentity session = Session(19U);
  RendererFrontendTransportDispatcher dispatcher(frontend, session);
  const std::uint64_t registry_id = dispatcher.registry_id();
  Require(registry_id == DeriveRenderAssetRegistryIdFromBridgeSession(session),
          "dispatcher did not require the session-derived registry");

  const auto asset1 = AssetFrame(1U, AssetDelta(registry_id, 1U, true));
  const RendererFrontendTransportDispatchResult synchronized1 =
      dispatcher.Dispatch(asset1, OffscreenPolicy());
  Require(synchronized1.ok(), "initial asset catalog was not synchronized");
  Require(synchronized1.scene_snapshot_id == 0U,
          "asset result claimed a scene snapshot identity");

  const auto scene1 = SceneFrame(2U, Scene(1U, registry_id, 1U));
  const RendererFrontendTransportDispatchResult rendered1 =
      dispatcher.Dispatch(scene1, PresentedPolicy());
  RequireStatus(rendered1.status,
                RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED,
                "presented scene did not complete");
  Require(rendered1.scene_snapshot_id == 1U,
          "presented scene did not expose its decoded snapshot identity");
  Require(rendered1.resources_released == 2U,
          "presented color/depth resources were not released");
  Require(frontend.rendered_requests.size() == 1U,
          "frontend did not receive exactly one scene");
  const RenderFrameRequest &request1 = frontend.rendered_requests.front();
  Require(request1.frame_id == 2U && request1.views.size() == 1U,
          "envelope sequence/camera did not become the frame request");
  Require(request1.present && request1.presentation_view_id == 7U &&
              request1.presentation_surface_revision == 9U,
          "caller presentation surface policy was not preserved");
  Require(request1.color_format == PixelFormat::RGBA16_FLOAT &&
              request1.allow_async_compute,
          "caller render policy was not preserved");
  Require(frontend.waited_frame_ids == std::vector<std::uint64_t>{2U} &&
              frontend.waited_timeouts.front() ==
                  kInfiniteRenderTimeoutNanoseconds,
          "scene was not synchronously drained");
  Require(frontend.calls.size() >= 5U && frontend.calls[1U] == "render" &&
              frontend.calls[2U] == "wait-frame" &&
              frontend.calls[3U] == "release-resource" &&
              frontend.calls[4U] == "release-resource",
          "attachments were not released after frame drain");

  const auto asset2 = AssetFrame(3U, AssetDelta(registry_id, 2U, false));
  Require(dispatcher.Dispatch(asset2, OffscreenPolicy()).ok(),
          "incremental asset transaction did not interleave");
  const auto scene2 = SceneFrame(4U, Scene(2U, registry_id, 2U), Camera(8U));
  const RendererFrontendTransportDispatchResult rendered2 =
      dispatcher.Dispatch(scene2, OffscreenPolicy());
  Require(rendered2.ok() && rendered2.resources_released == 1U,
          "offscreen scene did not complete and release its attachment");
  Require(rendered2.scene_snapshot_id == 2U,
          "offscreen scene did not expose its decoded snapshot identity");
  const RenderFrameRequest &request2 = frontend.rendered_requests.back();
  Require(request2.frame_id == 4U && !request2.present &&
              request2.presentation_view_id == 0U &&
              request2.presentation_surface_revision == 0U,
          "UI-free offscreen request acquired presentation identity");
  Require(dispatcher.last_accepted_sequence() == 4U &&
              dispatcher.next_expected_sequence() == 5U &&
              dispatcher.asset_registry().sequence() == 2U,
          "shared scene/asset lineage did not advance exactly");
  Require(frontend.synchronized_registry_ids ==
              std::vector<std::uint64_t>{registry_id, registry_id},
          "frontend observed a registry other than the derived identity");
}

void TestArbitrarySessionsAndRequiredRegistry() {
  constexpr std::array<std::uint8_t, 5U> seeds{{1U, 2U, 63U, 127U, 250U}};
  for (const std::uint8_t seed : seeds) {
    FakeFrontend frontend;
    const RenderBridgeSessionIdentity session = Session(seed);
    RendererFrontendTransportDispatcher dispatcher(frontend, session);
    const std::uint64_t expected =
        DeriveRenderAssetRegistryIdFromBridgeSession(session);
    const auto frame = AssetFrame(1U, AssetDelta(expected, 1U, true));
    Require(dispatcher.Dispatch(frame, OffscreenPolicy()).ok(),
            "arbitrary valid session/registry pair was rejected");
  }

  FakeFrontend frontend;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(42U));
  std::uint64_t foreign = dispatcher.registry_id() + 1U;
  if (foreign == 0U || foreign == (std::numeric_limits<std::uint64_t>::max)()) {
    foreign = 1U;
  }
  const auto foreign_frame = AssetFrame(1U, AssetDelta(foreign, 1U, true));
  const RendererFrontendTransportDispatchResult rejected =
      dispatcher.Dispatch(foreign_frame, OffscreenPolicy());
  RequireStatus(rejected.status,
                RendererFrontendTransportDispatchStatus::FAILED_DECODE,
                "foreign registry did not fail in the typed decoder");
  Require(rejected.transport_status ==
              RenderTransportStatus::REGISTRY_VALIDATION_FAILED,
          "foreign registry reported the wrong transport failure");
  Require(frontend.synchronized_registry_ids.empty(),
          "foreign registry reached the frontend");
}

void TestReplayOutOfOrderAndTerminalState() {
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(31U));
    const auto frame =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(frame, OffscreenPolicy()).ok(),
            "replay fixture setup failed");
    const RendererFrontendTransportDispatchResult replay =
        dispatcher.Dispatch(frame, OffscreenPolicy());
    RequireStatus(replay.status,
                  RendererFrontendTransportDispatchStatus::FAILED_DECODE,
                  "replayed envelope was not rejected");
    Require(replay.transport_status == RenderTransportStatus::REPLAYED_SEQUENCE,
            "replay reported the wrong typed sequence failure");
    const RendererFrontendTransportDispatchResult terminal =
        dispatcher.Dispatch(frame, OffscreenPolicy());
    RequireStatus(terminal.status,
                  RendererFrontendTransportDispatchStatus::REJECTED_TERMINAL,
                  "poisoned dispatcher accepted another frame");
    Require(terminal.terminal_cause ==
                    RendererFrontendTransportDispatchStatus::FAILED_DECODE &&
                frontend.synchronized_registry_ids.size() == 1U,
            "terminal rejection mutated frontend state");
  }
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(32U));
    const auto gap =
        AssetFrame(2U, AssetDelta(dispatcher.registry_id(), 1U, true));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(gap, OffscreenPolicy());
    Require(rejected.transport_status ==
                    RenderTransportStatus::OUT_OF_ORDER_SEQUENCE &&
                dispatcher.terminal(),
            "out-of-order first frame did not poison the dispatcher");
  }
}

void TestSceneCannotPrecedeAssets() {
  FakeFrontend frontend;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(50U));
  const auto scene = SceneFrame(1U, Scene(1U, dispatcher.registry_id(), 1U));
  const RendererFrontendTransportDispatchResult rejected =
      dispatcher.Dispatch(scene, OffscreenPolicy());
  RequireStatus(rejected.status,
                RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                "scene before its asset state was accepted");
  Require(rejected.validation_code == ValidationCode::SEQUENCE_MISMATCH &&
              dispatcher.last_accepted_sequence() == 1U &&
              frontend.rendered_requests.empty(),
          "scene-before-assets did not fail after deterministic decode commit");
}

void TestFrontendFailuresAfterDecoderCommit() {
  {
    FakeFrontend frontend;
    frontend.fail_synchronize = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(61U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(asset, OffscreenPolicy());
    RequireStatus(rejected.status,
                  RendererFrontendTransportDispatchStatus::
                      FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
                  "frontend asset failure was not terminal");
    Require(dispatcher.last_accepted_sequence() == 1U &&
                dispatcher.asset_registry().sequence() == 1U &&
                dispatcher.terminal(),
            "decoder commit was hidden or rolled back after frontend failure");
  }
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(62U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "render failure fixture asset setup failed");
    frontend.fail_render = true;
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, OffscreenPolicy());
    RequireStatus(
        rejected.status,
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER,
        "frontend render failure was not terminal");
    Require(dispatcher.last_accepted_sequence() == 2U &&
                frontend.waited_frame_ids.empty(),
            "render failure did not preserve committed decode/no-wait rules");
  }
}

void TestAttachmentCleanupOnEveryPostRenderPath() {
  {
    FakeFrontend frontend;
    frontend.invalid_output = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(71U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "invalid-output fixture asset setup failed");
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, PresentedPolicy());
    RequireStatus(
        rejected.status,
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT,
        "invalid frontend output was accepted");
    Require(rejected.resources_released == 2U &&
                frontend.release_attempts.size() == 2U &&
                frontend.waited_frame_ids.size() == 1U,
            "invalid output resources were not drained and released");
  }
  {
    FakeFrontend frontend;
    frontend.fail_wait = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(72U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "wait-failure fixture asset setup failed");
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, OffscreenPolicy());
    RequireStatus(rejected.status,
                  RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_WAIT,
                  "wait failure was not terminal");
    Require(frontend.release_attempts.size() == 1U,
            "wait failure leaked transferred output");
  }
  {
    FakeFrontend frontend;
    frontend.fail_render = true;
    frontend.populate_before_render_failure = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(73U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "render-cleanup fixture asset setup failed");
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, PresentedPolicy());
    RequireStatus(
        rejected.status,
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER,
        "render failure changed its primary error");
    Require(frontend.waited_frame_ids.empty() &&
                frontend.release_attempts.size() == 2U,
            "render failure did not release defensively transferred handles");
  }
  {
    FakeFrontend frontend;
    frontend.duplicate_output_resource = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(74U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "duplicate-output fixture asset setup failed");
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, PresentedPolicy());
    RequireStatus(
        rejected.status,
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT,
        "duplicated output ownership was accepted");
    Require(rejected.resources_released == 1U &&
                frontend.release_attempts.size() == 1U,
            "duplicate output handle was released more than exactly once");
  }
}

void TestReleaseFailureContinuesCleanupAndPoisons() {
  FakeFrontend frontend;
  frontend.fail_release_attempt = 1U;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(80U));
  const auto asset =
      AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
  Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
          "release-failure fixture asset setup failed");
  const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
  const RendererFrontendTransportDispatchResult rejected =
      dispatcher.Dispatch(scene, PresentedPolicy());
  RequireStatus(
      rejected.status,
      RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE,
      "resource release failure did not poison dispatcher");
  Require(frontend.release_attempts.size() == 2U &&
              rejected.resources_released == 1U && dispatcher.terminal(),
          "cleanup stopped after the first release failure");
}

void TestReverseInputInvalidPolicyAndInvalidSession() {
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(90U));
    const RenderTransportEnvelopeEncodeResult input =
        EncodeRenderTransportEnvelope(
            RenderTransportMessageKind::INPUT_EVENT_BATCH_V1, 1U, {},
            kRenderTransportStreamInputMaximumPayloadBytes);
    Require(input.ok(), "reverse input envelope fixture did not encode");
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(CompleteFrame(input.bytes), OffscreenPolicy());
    RequireStatus(
        rejected.status,
        RendererFrontendTransportDispatchStatus::REJECTED_REVERSE_DIRECTION,
        "reverse input was accepted on the game-to-frontend stream");
    Require(frontend.calls.empty(), "reverse input touched the frontend");
  }
  {
    FakeFrontend frontend;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(91U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    Require(dispatcher.Dispatch(asset, OffscreenPolicy()).ok(),
            "invalid-policy fixture asset setup failed");
    RendererFrontendPresentationPolicy invalid = PresentedPolicy();
    invalid.presentation_surface_revision = 0U;
    const auto scene = SceneFrame(2U, Scene(1U, dispatcher.registry_id(), 1U));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(scene, invalid);
    RequireStatus(rejected.status,
                  RendererFrontendTransportDispatchStatus::
                      REJECTED_INVALID_PRESENTATION_POLICY,
                  "invalid presentation surface policy was accepted");
    Require(dispatcher.last_accepted_sequence() == 1U &&
                frontend.rendered_requests.empty(),
            "invalid policy committed or rendered its scene");
  }
  {
    FakeFrontend frontend;
    RenderBridgeSessionIdentity invalid_session{};
    RendererFrontendTransportDispatcher dispatcher(frontend, invalid_session);
    Require(dispatcher.terminal() && dispatcher.registry_id() == 0U &&
                dispatcher.terminal_cause() ==
                    RendererFrontendTransportDispatchStatus::
                        REJECTED_INVALID_SESSION,
            "invalid bridge session did not fail closed at construction");
  }
}

void TestForgedCompleteFrameMetadataFailsClosed() {
  FakeFrontend frontend;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(99U));
  RenderTransportStreamFrameResult forged =
      AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
  forged.sequence = 2U;
  const RendererFrontendTransportDispatchResult rejected =
      dispatcher.Dispatch(forged, OffscreenPolicy());
  RequireStatus(rejected.status,
                RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                "forged validated-frame metadata was accepted");
  Require(dispatcher.last_accepted_sequence() == 1U && dispatcher.terminal(),
          "forged metadata did not fail after typed envelope verification");
}

} // namespace

int main() {
  TestIdentityDerivationAndStatusDomain();
  TestSceneGenerationBoundaryCodecFailClosed();
  TestAuthenticatedSceneGenerationBoundaryAndUnmarkedRollback();
  TestPresentationPolicyValidation();
  TestRetiredSceneAdvancesLineageWithoutFrontendWork();
  TestStalePresentationExtentRetiresAfterDecode();
  TestInterleavedAssetsScenesAndPresentation();
  TestArbitrarySessionsAndRequiredRegistry();
  TestReplayOutOfOrderAndTerminalState();
  TestSceneCannotPrecedeAssets();
  TestFrontendFailuresAfterDecoderCommit();
  TestAttachmentCleanupOnEveryPostRenderPath();
  TestReleaseFailureContinuesCleanupAndPoisons();
  TestReverseInputInvalidPolicyAndInvalidSession();
  TestForgedCompleteFrameMetadataFailsClosed();
  std::cout << "frontend transport dispatcher tests passed\n";
  return EXIT_SUCCESS;
}
