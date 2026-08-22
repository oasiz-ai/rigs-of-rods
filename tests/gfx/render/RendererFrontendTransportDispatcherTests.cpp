/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontendDirectDispatcher.h"
#include "RendererFrontendTransportDispatcher.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
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
static_assert(!std::is_copy_constructible_v<RendererFrontendDirectDispatcher>);
static_assert(!std::is_move_constructible_v<RendererFrontendDirectDispatcher>);

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

void RequireDirectStatus(RendererFrontendDirectDispatchStatus actual,
                         RendererFrontendDirectDispatchStatus expected,
                         const char *message) {
  if (actual != expected) {
    std::cerr << "frontend direct dispatcher test failed: " << message
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

/// A scene whose shape is NOT a generation-finalizing empty scene. It carries
/// one asset-free directional light so the dispatcher treats it as ordinary
/// renderable work. Every scene that is not deliberately finalizing a
/// generation must use this: the dispatcher retires final empty scenes without
/// entering the frontend, so a lightless fixture would silently stop
/// exercising the render path it means to test.
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
  LightDescriptor light;
  light.light_id = snapshot_id;
  descriptor.lights.push_back(light);
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "scene fixture must freeze successfully");
  return created.snapshot;
}

/// The generation-finalizing empty scene: no instances, no lights, no probes,
/// no environment binding. The transport dispatcher retires this shape instead
/// of rendering it, because a lightless scene can never pass a shadow-enabled
/// raster policy.
std::shared_ptr<const SceneSnapshot> FinalScene(
    std::uint64_t snapshot_id, std::uint64_t registry_id,
    std::uint64_t asset_sequence,
    std::uint64_t simulation_tick =
        (std::numeric_limits<std::uint64_t>::max)()) {
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
  Require(created.ok(), "final scene fixture must freeze successfully");
  return created.snapshot;
}

std::shared_ptr<const Ogre14ParticleCapturedFrame> ParticleFrameFor(
    const std::shared_ptr<const SceneSnapshot> &scene,
    std::uint64_t source_sequence) {
  auto particles = std::make_shared<Ogre14ParticleCapturedFrame>();
  particles->source_sequence = source_sequence;
  particles->material_catalog_registry_id = scene->asset_registry_id();
  particles->material_catalog_sequence = scene->asset_sequence();
  particles->simulation_tick = scene->simulation_tick();
  particles->simulation_time_seconds = scene->simulation_time_seconds();
  particles->absolute_world_origin_meters =
      scene->absolute_world_origin_meters();
  particles->joined_buffer_epoch = source_sequence;
  return particles;
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
ReservedAssetV1Frame(std::uint64_t envelope_sequence,
                     const RenderAssetDelta &delta) {
  RenderAssetDeltaTransportEncodeResult encoded =
      EncodeRenderAssetDeltaTransportFrame(envelope_sequence, delta);
  Require(encoded.ok(), "reserved asset V1 fixture must encode as V2 first");
  Require(encoded.bytes.size() >= kRenderTransportEnvelopeHeaderBytes,
          "reserved asset V1 fixture must contain a complete envelope");
  encoded.bytes[12U] = static_cast<std::uint8_t>(
      static_cast<std::uint16_t>(
          RenderTransportMessageKind::RENDER_ASSET_DELTA_V1) &
      0xFFU);
  encoded.bytes[13U] = static_cast<std::uint8_t>(
      static_cast<std::uint16_t>(
          RenderTransportMessageKind::RENDER_ASSET_DELTA_V1) >>
      8U);
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
    capabilities_.supports_continuous_particles = true;
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
    if (throw_bad_alloc_synchronize) {
      throw std::bad_alloc();
    }
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
    if (throw_length_error_scene_generation_reset) {
      throw std::length_error("injected reset allocation failure");
    }
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
    if (throw_bad_alloc_render) {
      throw std::bad_alloc();
    }
    if (fail_render) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE, "injected render failure",
          render_failure_recovery);
    }
    simulation_lineage_initialized = true;
    last_simulation_time_seconds =
        request.scene_snapshot->simulation_time_seconds();
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  RetireFrameState(const RenderFrameRequest &request) override {
    calls.emplace_back("retire-frame-state");
    retired_requests.push_back(request);
    if (fail_retire_frame_state) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "injected retired-frame state failure");
    }
    last_completed_frame_id = request.frame_id;
    return RenderOperationResult::Success();
  }

  bool IsFrameComplete(std::uint64_t frame_id) const noexcept override {
    return frame_id <= last_completed_frame_id ||
           (!rendered_requests.empty() &&
            frame_id <= rendered_requests.back().frame_id);
  }

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
  std::vector<RenderFrameRequest> retired_requests;
  std::vector<std::uint64_t> waited_frame_ids;
  std::vector<std::uint64_t> waited_timeouts;
  std::vector<ResourceHandle> release_attempts;
  std::vector<std::uint64_t> reset_generations;
  bool fail_synchronize = false;
  bool throw_bad_alloc_synchronize = false;
  bool fail_render = false;
  RenderOperationRecovery render_failure_recovery =
      RenderOperationRecovery::NONE;
  bool populate_before_render_failure = false;
  bool throw_bad_alloc_render = false;
  bool fail_retire_frame_state = false;
  bool fail_wait = false;
  bool fail_scene_generation_reset = false;
  bool throw_length_error_scene_generation_reset = false;
  bool simulation_lineage_initialized = false;
  double last_simulation_time_seconds = 0.0;
  bool invalid_output = false;
  bool duplicate_output_resource = false;
  std::size_t fail_release_attempt = 0U;
  std::uint64_t last_completed_frame_id = 0U;

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
    const RendererFrontendTransportDispatchResult finalized =
        dispatcher.Dispatch(
            SceneFrame(2U, FinalScene(40U, registry_id, 1U, 500U)),
            OffscreenPolicy());
    // The child session has no notion of generation finalization, so the
    // dispatcher itself retires the final empty scene: a lightless scene can
    // never pass a shadow-enabled raster policy, so presenting it could only
    // fail.
    RequireStatus(finalized.status,
                  RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED,
                  "final empty scene was rendered instead of retired");
    Require(frontend.rendered_requests.empty(),
            "final empty scene entered the frontend");
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
    // Exactly one render: the retired final scene never reached the frontend.
    Require(!dispatcher.terminal() &&
                frontend.rendered_requests.size() == 1U,
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
                .Dispatch(SceneFrame(2U, FinalScene(60U, registry_id, 1U, 500U)),
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
                        SceneFrame(2U, FinalScene(70U, registry_id, 1U, 500U)),
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
                        SceneFrame(2U, FinalScene(80U, registry_id, 1U, 500U)),
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
                        SceneFrame(2U, FinalScene(90U, registry_id, 1U, 500U)),
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
              frontend.rendered_requests.front().frame_id == 1U &&
              dispatcher.next_expected_sequence() == 4U,
          "post-retirement scene consumed a frontend ID or failed to render");
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
  Require(request1.frame_id == 1U && request1.views.size() == 1U,
          "first rendered scene did not receive frontend frame ID one");
  Require(request1.in_process_scene_asset_validation != nullptr &&
              request1.in_process_scene_asset_validation->Authenticates(
                  request1.scene_snapshot, frontend, registry_id, 1U),
          "decoded scene lost exact direct-dispatch validation authority");
  Require(request1.present && request1.presentation_view_id == 7U &&
              request1.presentation_surface_revision == 9U,
          "caller presentation surface policy was not preserved");
  Require(request1.color_format == PixelFormat::RGBA16_FLOAT &&
              request1.allow_async_compute,
          "caller render policy was not preserved");
  Require(frontend.waited_frame_ids == std::vector<std::uint64_t>{1U} &&
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
  Require(request2.frame_id == 2U && !request2.present &&
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
    frontend.throw_bad_alloc_synchronize = true;
    RendererFrontendTransportDispatcher dispatcher(frontend, Session(63U));
    const auto asset =
        AssetFrame(1U, AssetDelta(dispatcher.registry_id(), 1U, true));
    const RendererFrontendTransportDispatchResult rejected =
        dispatcher.Dispatch(asset, OffscreenPolicy());
    RequireStatus(rejected.status,
                  RendererFrontendTransportDispatchStatus::FAILED_INTERNAL,
                  "transport asset allocation exception changed status");
    Require(rejected.transport_status ==
                    RenderTransportStatus::ALLOCATION_FAILURE &&
                rejected.frontend_code == RenderOperationCode::OUT_OF_MEMORY &&
                dispatcher.asset_registry().sequence() == 1U,
            "transport asset allocation failure lost decode commit or code");
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

void TestReservedAssetV1KindFailsWithoutMutation() {
  FakeFrontend frontend;
  RendererFrontendTransportDispatcher dispatcher(frontend, Session(101U));
  const RenderAssetDelta delta =
      AssetDelta(dispatcher.registry_id(), 1U, true);

  const RendererFrontendTransportDispatchResult rejected =
      dispatcher.Dispatch(ReservedAssetV1Frame(1U, delta), OffscreenPolicy());
  RequireStatus(rejected.status,
                RendererFrontendTransportDispatchStatus::FAILED_INTERNAL,
                "reserved asset V1 kind was accepted");
  Require(rejected.transport_status ==
              RenderTransportStatus::UNKNOWN_MESSAGE_KIND &&
              rejected.kind ==
                  RenderTransportMessageKind::RENDER_ASSET_DELTA_V1 &&
              rejected.sequence == 1U && rejected.terminal &&
              dispatcher.terminal(),
          "reserved asset V1 kind did not terminal-fail as an unsupported kind");
  Require(dispatcher.next_expected_sequence() == 1U &&
              dispatcher.last_accepted_sequence() == 0U &&
              dispatcher.asset_registry().sequence() == 0U &&
              dispatcher.asset_registry().record_count() == 0U &&
              dispatcher.asset_registry().live_count() == 0U &&
              frontend.calls.empty(),
          "reserved asset V1 kind mutated sequence, catalog, or frontend");

  const RendererFrontendTransportDispatchResult after_terminal =
      dispatcher.Dispatch(AssetFrame(1U, delta), OffscreenPolicy());
  RequireStatus(after_terminal.status,
                RendererFrontendTransportDispatchStatus::REJECTED_TERMINAL,
                "dispatcher accepted V2 after terminal V1 rejection");
  Require(dispatcher.next_expected_sequence() == 1U &&
              dispatcher.last_accepted_sequence() == 0U &&
              dispatcher.asset_registry().sequence() == 0U &&
              frontend.calls.empty(),
          "post-terminal dispatch mutated sequence, catalog, or frontend");
}

void TestDirectDispatcherTypedLifecycle() {
  FakeFrontend frontend;
  constexpr std::uint64_t registry_id = 0xD1EC700000000001ULL;
  RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);

  const RendererFrontendDirectDispatchResult synchronized =
      dispatcher.SynchronizeAssets(AssetDelta(registry_id, 1U, true));
  RequireDirectStatus(
      synchronized.status,
      RendererFrontendDirectDispatchStatus::ASSET_DELTA_SYNCHRONIZED,
      "typed asset catalog did not synchronize");
  Require(synchronized.asset_sequence == 1U &&
              frontend.synchronized_registry_ids ==
                  std::vector<std::uint64_t>{registry_id},
          "typed asset catalog lost its exact registry lineage");

  const RendererFrontendDirectDispatchResult first = dispatcher.RenderScene(
      Scene(101U, registry_id, 1U), Camera(), PresentedPolicy());
  RequireDirectStatus(
      first.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
      "typed presented scene did not complete");
  Require(first.scene_snapshot_id == 101U && first.frontend_frame_id == 1U &&
              first.resources_released == 2U && !first.terminal &&
              frontend.rendered_requests.size() == 1U &&
              frontend.rendered_requests.front().frame_id == 1U &&
              frontend.rendered_requests.front().scene_snapshot->snapshot_id() ==
                  101U,
          "typed scene was copied, renumbered, or incompletely drained");
  const RenderFrameRequest &first_request = frontend.rendered_requests.front();
  Require(first_request.in_process_scene_asset_validation != nullptr &&
              first_request.in_process_scene_asset_validation->Authenticates(
                  first_request.scene_snapshot, frontend, registry_id, 1U),
          "typed scene did not carry exact direct-dispatch validation authority");
  FakeFrontend foreign_frontend;
  Require(!first_request.in_process_scene_asset_validation->Authenticates(
              first_request.scene_snapshot, foreign_frontend, registry_id,
              1U) &&
              !first_request.in_process_scene_asset_validation->Authenticates(
                  first_request.scene_snapshot, frontend, registry_id, 2U),
          "typed scene authority admitted a foreign frontend or sequence");

  RendererFrontendPresentationPolicy resized = PresentedPolicy();
  resized.presentation_drawable_width = 1280U;
  resized.presentation_drawable_height = 720U;
  const RendererFrontendDirectDispatchResult retired = dispatcher.RenderScene(
      Scene(102U, registry_id, 1U), Camera(), resized);
  RequireDirectStatus(
      retired.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED,
      "stale typed scene was not retired before frontend submission");
  Require(retired.scene_snapshot_id == 102U &&
              retired.frontend_frame_id == 0U &&
              dispatcher.last_frontend_frame_id() == 1U &&
              frontend.rendered_requests.size() == 1U,
          "retired typed scene consumed frontend work or a frame identity");

  Require(dispatcher
              .SynchronizeAssets(AssetDelta(registry_id, 2U, false))
              .ok(),
          "typed incremental asset transaction did not synchronize");
  // The generation's final empty scene. The DIRECT dispatcher still renders
  // it -- only the transport dispatcher, which cannot see caller intent,
  // retires this shape.
  const RendererFrontendDirectDispatchResult second = dispatcher.RenderScene(
      FinalScene(103U, registry_id, 2U, 2000U), Camera(8U),
      OffscreenPolicy());
  RequireDirectStatus(
      second.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
      "typed post-retirement scene did not complete");
  Require(second.frontend_frame_id == 2U &&
              dispatcher.last_frontend_frame_id() == 2U &&
              frontend.rendered_requests.size() == 2U &&
              frontend.rendered_requests.back().frame_id == 2U,
          "typed dispatcher introduced a sparse frontend frame identity");

  const RendererFrontendDirectDispatchResult reset =
      dispatcher.ResetSceneGeneration();
  RequireDirectStatus(
      reset.status,
      RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET,
      "typed final-empty scene did not authorize generation reset");
  Require(dispatcher.scene_generation() == 2U &&
              dispatcher.last_frontend_frame_id() == 2U &&
              frontend.reset_generations == std::vector<std::uint64_t>{2U},
          "typed generation reset changed process-lifetime frame lineage");

  const RendererFrontendDirectDispatchResult next_generation =
      dispatcher.RenderScene(Scene(104U, registry_id, 2U, 0U), Camera(9U),
                             OffscreenPolicy());
  RequireDirectStatus(
      next_generation.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
      "typed dispatcher did not accept the next map generation");
  Require(next_generation.frontend_frame_id == 3U &&
              dispatcher.last_consumed_scene_snapshot_id() == 104U,
          "typed generation reset reused a process-lifetime identity");
}

void TestDirectDispatcherRetiresContinuousParticleStateExactlyOnce() {
  FakeFrontend frontend;
  constexpr std::uint64_t registry_id = 0xD1EC700000000011ULL;
  RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
  Require(dispatcher
              .SynchronizeAssets(AssetDelta(registry_id, 1U, true))
              .ok(),
          "particle retirement fixture did not synchronize assets");

  const auto retired_scene = Scene(201U, registry_id, 1U);
  const RendererFrontendDirectDispatchResult retired = dispatcher.RenderScene(
      retired_scene, Camera(), RetiredPolicy(),
      ParticleFrameFor(retired_scene, 1U));
  RequireDirectStatus(
      retired.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED,
      "continuous-particle scene was not retired through frontend state");
  Require(retired.frontend_frame_id == 1U &&
              dispatcher.last_frontend_frame_id() == 1U &&
              frontend.rendered_requests.empty() &&
              frontend.retired_requests.size() == 1U &&
              frontend.retired_requests.front().frame_id == 1U &&
              frontend.retired_requests.front().scene_snapshot.get() ==
                  retired_scene.get() &&
              frontend.retired_requests.front().continuous_particles !=
                  nullptr &&
              frontend.retired_requests.front()
                      .in_process_scene_asset_validation != nullptr &&
              frontend.retired_requests.front()
                  .in_process_scene_asset_validation->Authenticates(
                      frontend.retired_requests.front().scene_snapshot,
                      frontend, registry_id, 1U) &&
              frontend.waited_frame_ids == std::vector<std::uint64_t>{1U},
          "retired particle state was dropped, rendered, copied, or incomplete");

  const RendererFrontendDirectDispatchResult rendered =
      dispatcher.RenderScene(Scene(202U, registry_id, 1U), Camera(8U),
                             OffscreenPolicy());
  RequireDirectStatus(
      rendered.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
      "render after particle-state retirement did not complete");
  Require(rendered.frontend_frame_id == 2U &&
              dispatcher.last_frontend_frame_id() == 2U &&
              frontend.rendered_requests.size() == 1U &&
              frontend.rendered_requests.front().frame_id == 2U,
          "particle-state retirement left sparse or reused frontend IDs");

  FakeFrontend failing_frontend;
  failing_frontend.fail_retire_frame_state = true;
  RendererFrontendDirectDispatcher failing_dispatcher(failing_frontend,
                                                       registry_id + 1U);
  Require(failing_dispatcher
              .SynchronizeAssets(AssetDelta(registry_id + 1U, 1U, true))
              .ok(),
          "failed-retirement fixture did not synchronize assets");
  const auto failing_scene = Scene(301U, registry_id + 1U, 1U);
  const RendererFrontendDirectDispatchResult failed =
      failing_dispatcher.RenderScene(
          failing_scene, Camera(), RetiredPolicy(),
          ParticleFrameFor(failing_scene, 1U));
  RequireDirectStatus(
      failed.status,
      RendererFrontendDirectDispatchStatus::
          FAILED_FRONTEND_FRAME_RETIREMENT,
      "failed particle-state retirement was reported as successful");
  Require(failed.terminal && failed.frontend_frame_id == 0U &&
              failing_dispatcher.last_frontend_frame_id() == 0U &&
              failing_dispatcher.last_consumed_scene_snapshot_id() == 0U,
          "failed particle-state retirement consumed dispatcher lineage");
}

/// A scene whose descriptor is well-formed but whose environment binding names
/// a texture/sampler pair that no registry sequence ever created. Descriptor
/// validation passes; only `ValidateSceneSnapshotAssets` can reject it, which
/// is exactly the prologue validator under test.
std::shared_ptr<const SceneSnapshot> SceneWithMissingEnvironmentAssets(
    std::uint64_t snapshot_id, std::uint64_t registry_id,
    std::uint64_t asset_sequence) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick = snapshot_id * 10U;
  descriptor.simulation_time_seconds =
      static_cast<double>(descriptor.simulation_tick) / 2000.0;
  LightDescriptor light;
  light.light_id = snapshot_id;
  descriptor.lights.push_back(light);
  descriptor.environment.environment_texture = RenderAssetReference::Create(
      RenderAssetKind::TEXTURE, RenderAssetId::FromWords(0xE0U, 0x11U), 1U);
  descriptor.environment.environment_sampler = RenderAssetReference::Create(
      RenderAssetKind::SAMPLER, RenderAssetId::FromWords(0xE0U, 0x12U), 1U);
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(),
          "missing-environment fixture must still freeze successfully");
  return created.snapshot;
}

/// The governing invariant at the render boundary: a per-frame validation may
/// reject a frame, but may not end a session and may not permanently stop
/// publication. All five `RenderSceneImpl` prologue validators run before the
/// dispatcher writes `last_consumed_scene_snapshot_id_` and before the first
/// `frontend_->` call, so each one must drop its frame, count it, leave the
/// dispatcher clean, and accept the NEXT frame.
void TestDirectDispatcherPrologueValidatorsRejectWithoutPoisoning() {
  struct RejectionCase final {
    const char *name;
    // Returns the result of the frame that must be rejected.
    RendererFrontendDirectDispatchResult (*submit)(
        RendererFrontendDirectDispatcher &, std::uint64_t);
    RendererFrontendDirectDispatchStatus expected;
    /// When non-zero, one scene is rendered before the rejection so the
    /// replay watermark is already established. Snapshot identity zero is
    /// unfreezable, so monotonicity can only be violated from above zero.
    std::uint64_t prime_snapshot_id = 0U;
  };

  const RejectionCase cases[] = {
      {"presentation policy",
       [](RendererFrontendDirectDispatcher &dispatcher,
          std::uint64_t registry_id) {
         // Presenting without naming the active surface revision.
         RendererFrontendPresentationPolicy policy = PresentedPolicy();
         policy.presentation_surface_revision = 0U;
         return dispatcher.RenderScene(Scene(500U, registry_id, 1U), Camera(),
                                       policy);
       },
       RendererFrontendDirectDispatchStatus::
           REJECTED_INVALID_PRESENTATION_POLICY},
      {"registry sequence",
       [](RendererFrontendDirectDispatcher &dispatcher,
          std::uint64_t registry_id) {
         // A scene cut against an asset sequence the registry never reached.
         return dispatcher.RenderScene(Scene(500U, registry_id, 9U), Camera(),
                                       OffscreenPolicy());
       },
       RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION},
      {"snapshot id monotonicity",
       [](RendererFrontendDirectDispatcher &dispatcher,
          std::uint64_t registry_id) {
         // Replays the identity the primed frame already consumed.
         return dispatcher.RenderScene(Scene(400U, registry_id, 1U), Camera(),
                                       OffscreenPolicy());
       },
       RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION, 400U},
      {"scene snapshot assets",
       [](RendererFrontendDirectDispatcher &dispatcher,
          std::uint64_t registry_id) {
         return dispatcher.RenderScene(
             SceneWithMissingEnvironmentAssets(500U, registry_id, 1U),
             Camera(), OffscreenPolicy());
       },
       RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION},
      {"camera view request",
       [](RendererFrontendDirectDispatcher &dispatcher,
          std::uint64_t registry_id) {
         CameraViewRequest camera = Camera();
         camera.width = 0U;
         return dispatcher.RenderScene(Scene(500U, registry_id, 1U), camera,
                                       OffscreenPolicy());
       },
       RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION},
  };

  std::uint64_t registry_seed = 0xD1EC700000000100ULL;
  for (const RejectionCase &rejection_case : cases) {
    FakeFrontend frontend;
    const std::uint64_t registry_id = ++registry_seed;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    Require(dispatcher.SynchronizeAssets(AssetDelta(registry_id, 1U, true))
                .ok(),
            "prologue rejection fixture did not synchronize assets");

    std::uint64_t primed_frames = 0U;
    if (rejection_case.prime_snapshot_id != 0U) {
      Require(dispatcher
                  .RenderScene(Scene(rejection_case.prime_snapshot_id,
                                     registry_id, 1U),
                               Camera(), OffscreenPolicy())
                  .ok(),
              "prologue rejection fixture did not prime its watermark");
      primed_frames = 1U;
    }
    const std::uint64_t watermark_before =
        dispatcher.last_consumed_scene_snapshot_id();

    const RendererFrontendDirectDispatchResult rejected =
        rejection_case.submit(dispatcher, registry_id);
    RequireDirectStatus(rejected.status, rejection_case.expected,
                        rejection_case.name);
    Require(!rejected.terminal && !dispatcher.terminal(),
            "a prologue validator poisoned the dispatcher");
    Require(rejected.rejected_frames == 1U &&
                dispatcher.rejected_frames() == 1U,
            "a prologue rejection was not counted");
    // Nothing may have committed: no new frontend call, no lineage advance.
    Require(frontend.rendered_requests.size() == primed_frames &&
                frontend.retired_requests.empty() &&
                dispatcher.last_frontend_frame_id() == primed_frames,
            "a prologue rejection reached the frontend");
    Require(dispatcher.last_consumed_scene_snapshot_id() == watermark_before,
            "a prologue rejection advanced the snapshot replay watermark");

    // The whole point: the NEXT frame is accepted.
    const RendererFrontendDirectDispatchResult accepted =
        dispatcher.RenderScene(Scene(501U, registry_id, 1U), Camera(),
                               OffscreenPolicy());
    RequireDirectStatus(
        accepted.status,
        RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
        "the frame after a prologue rejection was refused");
    Require(accepted.frontend_frame_id == primed_frames + 1U &&
                dispatcher.last_consumed_scene_snapshot_id() == 501U &&
                accepted.rejected_frames == 1U,
            "recovery after a prologue rejection lost or double-counted "
            "dispatcher lineage");
  }
}

/// A frontend failure carrying RenderOperationRecovery::RETRY_NEXT_FRAME is a
/// measured frame drop, not a terminal session failure, when the dispatcher's
/// own scene/frame lineage proves that the frontend committed nothing. The
/// named counters keep the degrade observable and the next valid frame must
/// reuse the uncommitted frontend frame id.
void TestDirectDispatcherHonoursMeasuredRetryNextFrame() {
  FakeFrontend frontend;
  frontend.fail_render = true;
  frontend.populate_before_render_failure = true;
  frontend.render_failure_recovery = RenderOperationRecovery::RETRY_NEXT_FRAME;
  constexpr std::uint64_t registry_id = 0xD1EC700000000200ULL;
  RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
  Require(dispatcher.SynchronizeAssets(AssetDelta(registry_id, 1U, true)).ok(),
          "retry-next-frame fixture did not synchronize assets");

  const RendererFrontendDirectDispatchResult failed = dispatcher.RenderScene(
      Scene(600U, registry_id, 1U), Camera(), PresentedPolicy());
  RequireDirectStatus(
      failed.status,
      RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER,
      "a recoverable frontend render failure changed its primary status");
  Require(failed.recoverable_frame_failures == 1U &&
              dispatcher.recoverable_frame_failures() == 1U,
          "a recoverable frontend render failure was not counted");
  Require(!failed.terminal && !dispatcher.terminal(),
          "a measured RETRY_NEXT_FRAME poisoned the dispatcher");
  Require(failed.rejected_frames == 1U &&
              dispatcher.rejected_frames() == 1U,
          "a measured RETRY_NEXT_FRAME was not counted as a dropped frame");
  Require(dispatcher.last_consumed_scene_snapshot_id() == 0U &&
              dispatcher.last_frontend_frame_id() == 0U,
          "a failed frontend render advanced dispatcher lineage");

  frontend.fail_render = false;
  frontend.render_failure_recovery = RenderOperationRecovery::NONE;
  const RendererFrontendDirectDispatchResult recovered =
      dispatcher.RenderScene(Scene(601U, registry_id, 1U), Camera(),
                             PresentedPolicy());
  RequireDirectStatus(
      recovered.status,
      RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
      "the frame after a measured RETRY_NEXT_FRAME was refused");
  Require(recovered.frontend_frame_id == 1U &&
              dispatcher.last_frontend_frame_id() == 1U &&
              dispatcher.last_consumed_scene_snapshot_id() == 601U,
          "recovery did not reuse and commit the unadvanced frame lineage");
  Require(recovered.recoverable_frame_failures == 1U &&
              recovered.rejected_frames == 1U,
          "recovery lost the measured frame-drop counters");

  // A plain failure carrying no recovery must not touch the counter.
  FakeFrontend plain_frontend;
  plain_frontend.fail_render = true;
  plain_frontend.populate_before_render_failure = true;
  RendererFrontendDirectDispatcher plain(plain_frontend, registry_id + 1U);
  Require(plain.SynchronizeAssets(AssetDelta(registry_id + 1U, 1U, true)).ok(),
          "plain render-failure fixture did not synchronize assets");
  const RendererFrontendDirectDispatchResult plain_failed = plain.RenderScene(
      Scene(600U, registry_id + 1U, 1U), Camera(), PresentedPolicy());
  Require(!plain_failed && plain_failed.recoverable_frame_failures == 0U,
          "a failure with no recovery verdict was counted as recoverable");
}

void TestDirectDispatcherFailuresAreTerminal() {
  {
    FakeFrontend frontend;
    RendererFrontendDirectDispatcher dispatcher(frontend, 0U);
    Require(dispatcher.terminal() &&
                dispatcher.terminal_cause() ==
                    RendererFrontendDirectDispatchStatus::
                        REJECTED_INVALID_REGISTRY,
            "zero direct registry identity did not fail at construction");
  }

  {
    FakeFrontend frontend;
    constexpr std::uint64_t registry_id = 0xD1EC700000000002ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.RenderScene(Scene(1U, registry_id, 1U), Camera(),
                               OffscreenPolicy());
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
        "typed scene was accepted before its asset catalog");
    // A prologue validator drops the frame; it does not end the session.
    Require(!rejected.terminal && !dispatcher.terminal() &&
                rejected.rejected_frames == 1U && frontend.calls.empty(),
            "typed scene-before-assets failure touched the frontend");
  }

  {
    FakeFrontend frontend;
    frontend.fail_synchronize = true;
    constexpr std::uint64_t registry_id = 0xD1EC700000000003ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.SynchronizeAssets(AssetDelta(registry_id, 1U, true));
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::
            FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
        "typed frontend asset failure was not terminal");
    Require(rejected.terminal && dispatcher.asset_sequence() == 0U &&
                frontend.synchronized_asset_sequences ==
                    std::vector<std::uint64_t>{1U},
            "failed typed asset transaction committed its logical registry");
  }

  {
    FakeFrontend frontend;
    frontend.throw_bad_alloc_synchronize = true;
    constexpr std::uint64_t registry_id = 0xD1EC700000000006ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.SynchronizeAssets(AssetDelta(registry_id, 1U, true));
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
        "typed asset allocation exception became a backend failure");
    Require(rejected.frontend_code == RenderOperationCode::OUT_OF_MEMORY &&
                dispatcher.asset_sequence() == 0U,
            "typed asset allocation failure committed or lost its code");
  }

  {
    FakeFrontend frontend;
    constexpr std::uint64_t registry_id = 0xD1EC700000000005ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    Require(dispatcher
                .SynchronizeAssets(AssetDelta(registry_id, 1U, true))
                .ok(),
            "typed replay fixture did not initialize");
    Require(dispatcher
                .RenderScene(Scene(10U, registry_id, 1U), Camera(),
                             RetiredPolicy())
                .ok(),
            "typed replay fixture did not retire its first scene");
    const RendererFrontendDirectDispatchResult replayed =
        dispatcher.RenderScene(Scene(10U, registry_id, 1U), Camera(),
                               OffscreenPolicy());
    RequireDirectStatus(
        replayed.status,
        RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
        "retired typed snapshot identity was replayed into the frontend");
    Require(!replayed.terminal && !dispatcher.terminal() &&
                replayed.rejected_frames == 1U &&
                frontend.rendered_requests.empty() &&
                dispatcher.last_consumed_scene_snapshot_id() == 10U,
            "typed replay advanced or rendered an irreversible retirement");
  }

  {
    FakeFrontend frontend;
    frontend.fail_render = true;
    frontend.populate_before_render_failure = true;
    constexpr std::uint64_t registry_id = 0xD1EC700000000004ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    Require(dispatcher
                .SynchronizeAssets(AssetDelta(registry_id, 1U, true))
                .ok(),
            "typed render-failure fixture did not initialize");
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.RenderScene(Scene(1U, registry_id, 1U), Camera(),
                               PresentedPolicy());
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER,
        "typed render failure changed its primary status");
    Require(rejected.terminal && rejected.resources_released == 2U &&
                frontend.release_attempts.size() == 2U &&
                frontend.waited_frame_ids.empty(),
            "typed render failure leaked transferred resources or waited");
  }

  {
    FakeFrontend frontend;
    frontend.throw_bad_alloc_render = true;
    constexpr std::uint64_t registry_id = 0xD1EC700000000007ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    Require(dispatcher
                .SynchronizeAssets(AssetDelta(registry_id, 1U, true))
                .ok(),
            "typed render-allocation fixture did not initialize");
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.RenderScene(Scene(1U, registry_id, 1U), Camera(),
                               PresentedPolicy());
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
        "typed render allocation exception became a backend failure");
    Require(rejected.frontend_code == RenderOperationCode::OUT_OF_MEMORY &&
                rejected.resources_released == 2U &&
                frontend.release_attempts.size() == 2U,
            "typed render allocation failure leaked transferred resources");
  }

  {
    FakeFrontend frontend;
    frontend.throw_length_error_scene_generation_reset = true;
    constexpr std::uint64_t registry_id = 0xD1EC700000000008ULL;
    RendererFrontendDirectDispatcher dispatcher(frontend, registry_id);
    Require(dispatcher
                .SynchronizeAssets(AssetDelta(registry_id, 1U, true))
                .ok() &&
                dispatcher
                    .RenderScene(FinalScene(1U, registry_id, 1U), Camera(),
                                 OffscreenPolicy())
                    .ok(),
            "typed reset-allocation fixture did not initialize");
    const RendererFrontendDirectDispatchResult rejected =
        dispatcher.ResetSceneGeneration();
    RequireDirectStatus(
        rejected.status,
        RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
        "typed reset length exception became a backend failure");
    Require(rejected.frontend_code == RenderOperationCode::OUT_OF_MEMORY &&
                dispatcher.scene_generation() == 1U,
            "typed reset allocation failure advanced its generation");
  }
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
  TestReservedAssetV1KindFailsWithoutMutation();
  TestDirectDispatcherTypedLifecycle();
  TestDirectDispatcherRetiresContinuousParticleStateExactlyOnce();
  TestDirectDispatcherPrologueValidatorsRejectWithoutPoisoning();
  TestDirectDispatcherHonoursMeasuredRetryNextFrame();
  TestDirectDispatcherFailuresAreTerminal();
  std::cout << "frontend direct and transport dispatcher tests passed\n";
  return EXIT_SUCCESS;
}
