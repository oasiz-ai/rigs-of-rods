/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontendTransportDispatcher.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace RoR::Render {
namespace {

bool IsFinalEmptyScene(const SceneSnapshot &snapshot) noexcept {
  return snapshot.mesh_instances().empty() && snapshot.lights().empty() &&
         snapshot.reflection_probes().empty() &&
         snapshot.dynamic_mesh_updates().empty() &&
         snapshot.particle_events().empty() &&
         IsAbsentRenderAssetReference(
             snapshot.environment().environment_texture) &&
         IsAbsentRenderAssetReference(
             snapshot.environment().environment_sampler);
}

} // namespace

bool IsKnownRendererFrontendTransportDispatchStatus(
    RendererFrontendTransportDispatchStatus status) noexcept {
  switch (status) {
  case RendererFrontendTransportDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED:
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED:
  case RendererFrontendTransportDispatchStatus::
      SCENE_GENERATION_BOUNDARY_CONSUMED:
  case RendererFrontendTransportDispatchStatus::REJECTED_TERMINAL:
  case RendererFrontendTransportDispatchStatus::REJECTED_INVALID_SESSION:
  case RendererFrontendTransportDispatchStatus::REJECTED_INVALID_FRAME:
  case RendererFrontendTransportDispatchStatus::REJECTED_REVERSE_DIRECTION:
  case RendererFrontendTransportDispatchStatus::
      REJECTED_INVALID_PRESENTATION_POLICY:
  case RendererFrontendTransportDispatchStatus::FAILED_DECODE:
  case RendererFrontendTransportDispatchStatus::FAILED_LINEAGE:
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_CAPABILITIES:
  case RendererFrontendTransportDispatchStatus::
      FAILED_FRONTEND_ASSET_SYNCHRONIZATION:
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER:
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_WAIT:
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT:
  case RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE:
  case RendererFrontendTransportDispatchStatus::
      FAILED_FRONTEND_SCENE_GENERATION_RESET:
  case RendererFrontendTransportDispatchStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererFrontendTransportDispatchStatus status) noexcept {
  switch (status) {
  case RendererFrontendTransportDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
    return "asset-delta-synchronized";
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED:
    return "scene-frame-completed";
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED:
    return "scene-frame-retired";
  case RendererFrontendTransportDispatchStatus::
      SCENE_GENERATION_BOUNDARY_CONSUMED:
    return "scene-generation-boundary-consumed";
  case RendererFrontendTransportDispatchStatus::REJECTED_TERMINAL:
    return "rejected-terminal";
  case RendererFrontendTransportDispatchStatus::REJECTED_INVALID_SESSION:
    return "rejected-invalid-session";
  case RendererFrontendTransportDispatchStatus::REJECTED_INVALID_FRAME:
    return "rejected-invalid-frame";
  case RendererFrontendTransportDispatchStatus::REJECTED_REVERSE_DIRECTION:
    return "rejected-reverse-direction";
  case RendererFrontendTransportDispatchStatus::
      REJECTED_INVALID_PRESENTATION_POLICY:
    return "rejected-invalid-presentation-policy";
  case RendererFrontendTransportDispatchStatus::FAILED_DECODE:
    return "failed-decode";
  case RendererFrontendTransportDispatchStatus::FAILED_LINEAGE:
    return "failed-lineage";
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_CAPABILITIES:
    return "failed-frontend-capabilities";
  case RendererFrontendTransportDispatchStatus::
      FAILED_FRONTEND_ASSET_SYNCHRONIZATION:
    return "failed-frontend-asset-synchronization";
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER:
    return "failed-frontend-render";
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_WAIT:
    return "failed-frontend-wait";
  case RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT:
    return "failed-frontend-output";
  case RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE:
    return "failed-resource-release";
  case RendererFrontendTransportDispatchStatus::
      FAILED_FRONTEND_SCENE_GENERATION_RESET:
    return "failed-frontend-scene-generation-reset";
  case RendererFrontendTransportDispatchStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "unknown";
}

RendererFrontendTransportDispatcher::RendererFrontendTransportDispatcher(
    IRendererFrontend &frontend,
    const RenderBridgeSessionIdentity &session_id) noexcept
    : registry_id_(DeriveRenderAssetRegistryIdFromBridgeSession(session_id)),
      sequence_state_(1U), asset_decoder_(registry_id_, sequence_state_),
      scene_decoder_(sequence_state_),
      scene_generation_decoder_(registry_id_, sequence_state_),
      direct_dispatcher_(frontend, registry_id_) {
  if (registry_id_ == 0U ||
      registry_id_ == (std::numeric_limits<std::uint64_t>::max)()) {
    terminal_ = true;
    terminal_cause_ =
        RendererFrontendTransportDispatchStatus::REJECTED_INVALID_SESSION;
  }
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::Success(
    RendererFrontendTransportDispatchStatus status,
    const RenderTransportStreamFrameResult &frame,
    std::uint32_t resources_released) const noexcept {
  RendererFrontendTransportDispatchResult result;
  result.status = status;
  result.terminal_cause = terminal_cause_;
  result.kind = frame.kind;
  result.sequence = frame.sequence;
  result.transport_status = RenderTransportStatus::OK;
  result.resources_released = resources_released;
  result.terminal = false;
  return result;
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::FailFromDirect(
    const RendererFrontendDirectDispatchResult &direct,
    const RenderTransportStreamFrameResult &frame) noexcept {
  RendererFrontendTransportDispatchStatus mapped =
      RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
  switch (direct.status) {
  case RendererFrontendDirectDispatchStatus::REJECTED_INVALID_PRESENTATION_POLICY:
    mapped = RendererFrontendTransportDispatchStatus::
        REJECTED_INVALID_PRESENTATION_POLICY;
    break;
  case RendererFrontendDirectDispatchStatus::REJECTED_INVALID_REGISTRY:
  case RendererFrontendDirectDispatchStatus::FAILED_ASSET_VALIDATION:
  case RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_LINEAGE;
    break;
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_ASSET_SYNCHRONIZATION:
    mapped = RendererFrontendTransportDispatchStatus::
        FAILED_FRONTEND_ASSET_SYNCHRONIZATION;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_CAPABILITIES:
    mapped =
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_CAPABILITIES;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_FRAME_RETIREMENT:
  case RendererFrontendDirectDispatchStatus::
      SCENE_FRAME_PRESENTATION_SURFACE_STALE:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_WAIT:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_WAIT;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_OUTPUT:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE;
    break;
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_SCENE_GENERATION_RESET:
    mapped = RendererFrontendTransportDispatchStatus::
        FAILED_FRONTEND_SCENE_GENERATION_RESET;
    break;
  case RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION:
  case RendererFrontendDirectDispatchStatus::FAILED_INTERNAL:
  case RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
    break;
  case RendererFrontendDirectDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED:
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED:
  case RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET:
    mapped = RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
    break;
  }
  const RenderTransportStatus transport_status =
      direct.status == RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION
          ? RenderTransportStatus::ALLOCATION_FAILURE
          : RenderTransportStatus::OK;
  return Fail(mapped, &frame, transport_status, direct.validation_code,
              direct.frontend_code, direct.resources_released);
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::Fail(
    RendererFrontendTransportDispatchStatus status,
    const RenderTransportStreamFrameResult *frame,
    RenderTransportStatus transport_status, ValidationCode validation_code,
    RenderOperationCode frontend_code,
    std::uint32_t resources_released) noexcept {
  terminal_ = true;
  terminal_cause_ = status;
  RendererFrontendTransportDispatchResult result;
  result.status = status;
  result.terminal_cause = status;
  if (frame != nullptr) {
    result.kind = frame->kind;
    result.sequence = frame->sequence;
  }
  result.transport_status = transport_status;
  result.validation_code = validation_code;
  result.frontend_code = frontend_code;
  result.resources_released = resources_released;
  result.terminal = true;
  return result;
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::Dispatch(
    const RenderTransportStreamFrameResult &frame,
    const RendererFrontendPresentationPolicy &presentation_policy) noexcept {
  if (terminal_) {
    RendererFrontendTransportDispatchResult result;
    result.status = RendererFrontendTransportDispatchStatus::REJECTED_TERMINAL;
    result.terminal_cause = terminal_cause_;
    result.kind = frame.kind;
    result.sequence = frame.sequence;
    result.terminal = true;
    return result;
  }
  if (registry_id_ == 0U) {
    return Fail(
        RendererFrontendTransportDispatchStatus::REJECTED_INVALID_SESSION,
        &frame, RenderTransportStatus::INVALID_ARGUMENT,
        ValidationCode::INVALID_IDENTIFIER, RenderOperationCode::OK);
  }
  if (!frame.ok()) {
    return Fail(RendererFrontendTransportDispatchStatus::REJECTED_INVALID_FRAME,
                &frame, RenderTransportStatus::INVALID_ARGUMENT,
                ValidationCode::OK, RenderOperationCode::OK);
  }
  if (frame.kind == RenderTransportMessageKind::INPUT_EVENT_BATCH_V1 ||
      frame.kind ==
          RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 ||
      frame.kind == RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1) {
    return Fail(
        RendererFrontendTransportDispatchStatus::REJECTED_REVERSE_DIRECTION,
        &frame, RenderTransportStatus::UNKNOWN_MESSAGE_KIND, ValidationCode::OK,
        RenderOperationCode::OK);
  }

  try {
    switch (frame.kind) {
    case RenderTransportMessageKind::RENDER_ASSET_DELTA_V2:
      return DispatchAsset(frame);
    case RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2:
      return DispatchScene(frame, presentation_policy);
    case RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1:
      return DispatchSceneGenerationBoundary(frame);
    case RenderTransportMessageKind::INPUT_EVENT_BATCH_V1:
    case RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1:
    case RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1:
    case RenderTransportMessageKind::RENDER_ASSET_DELTA_V1:
      break;
    }
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_INTERNAL,
                &frame, RenderTransportStatus::ALLOCATION_FAILURE,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_INTERNAL,
                &frame, RenderTransportStatus::ALLOCATION_FAILURE,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_INTERNAL,
                &frame, RenderTransportStatus::INVALID_ARGUMENT,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
  return Fail(RendererFrontendTransportDispatchStatus::FAILED_INTERNAL, &frame,
              RenderTransportStatus::UNKNOWN_MESSAGE_KIND,
              ValidationCode::INVALID_ENUM, RenderOperationCode::OK);
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::DispatchAsset(
    const RenderTransportStreamFrameResult &frame) {
  const RenderAssetDeltaTransportDecodeResult decoded =
      asset_decoder_.Accept(frame.bytes);
  if (!decoded) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_DECODE, &frame,
                decoded.status, ValidationCode::OK, RenderOperationCode::OK);
  }
  if (decoded.message->kind() != frame.kind ||
      decoded.message->sequence() != frame.sequence ||
      decoded.message->delta() == nullptr ||
      decoded.message->delta()->registry_id != registry_id_) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_LINEAGE, &frame,
                RenderTransportStatus::INVALID_SEQUENCE,
                ValidationCode::INVALID_IDENTIFIER, RenderOperationCode::OK);
  }
  const RendererFrontendDirectDispatchResult synchronized =
      direct_dispatcher_.SynchronizeAssets(*decoded.message->delta());
  if (!synchronized) {
    return FailFromDirect(synchronized, frame);
  }
  return Success(
      RendererFrontendTransportDispatchStatus::ASSET_DELTA_SYNCHRONIZED, frame);
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::DispatchScene(
    const RenderTransportStreamFrameResult &frame,
    const RendererFrontendPresentationPolicy &presentation_policy) {
  const ValidationResult policy_validation =
      ValidateRendererFrontendPresentationPolicy(presentation_policy);
  if (!policy_validation) {
    return Fail(RendererFrontendTransportDispatchStatus::
                    REJECTED_INVALID_PRESENTATION_POLICY,
                &frame, RenderTransportStatus::OK, policy_validation.code,
                RenderOperationCode::OK);
  }

  const SceneSnapshotTransportDecodeResult decoded =
      scene_decoder_.Accept(frame.bytes);
  if (!decoded) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_DECODE, &frame,
                decoded.status, ValidationCode::OK, RenderOperationCode::OK);
  }
  if (decoded.message->kind() != frame.kind ||
      decoded.message->sequence() != frame.sequence ||
      decoded.message->scene_snapshot() == nullptr) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_LINEAGE, &frame,
                RenderTransportStatus::INVALID_SEQUENCE,
                ValidationCode::INVALID_IDENTIFIER, RenderOperationCode::OK);
  }
  const std::uint64_t scene_snapshot_id =
      decoded.message->scene_snapshot()->snapshot_id();
  const RendererFrontendDirectDispatchResult dispatched =
      direct_dispatcher_.RenderScene(decoded.message->scene_snapshot(),
                                     decoded.message->camera(),
                                     presentation_policy);
  if (!dispatched) {
    return FailFromDirect(dispatched, frame);
  }

  const RendererFrontendTransportDispatchStatus status =
      dispatched.status ==
              RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED
          ? RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED
          : RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED;
  RendererFrontendTransportDispatchResult result =
      Success(status, frame, dispatched.resources_released);
  result.scene_snapshot_id = scene_snapshot_id;
  last_scene_snapshot_id_ = scene_snapshot_id;
  last_scene_asset_sequence_ =
      decoded.message->scene_snapshot()->asset_sequence();
  last_scene_was_empty_ =
      IsFinalEmptyScene(*decoded.message->scene_snapshot());
  return result;
}

RendererFrontendTransportDispatchResult
RendererFrontendTransportDispatcher::DispatchSceneGenerationBoundary(
    const RenderTransportStreamFrameResult &frame) {
  const SceneGenerationBoundaryTransportDecodeResult decoded =
      scene_generation_decoder_.Accept(frame.bytes);
  if (!decoded) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_DECODE, &frame,
                decoded.status, ValidationCode::OK, RenderOperationCode::OK);
  }
  const SceneGenerationBoundary &boundary = decoded.boundary;
  if (frame.kind !=
          RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1 ||
      decoded.sequence != frame.sequence ||
      boundary.registry_id != registry_id_ ||
      boundary.completed_generation != scene_generation_ ||
      boundary.next_generation != scene_generation_ + 1U ||
      boundary.asset_sequence != asset_decoder_.registry().sequence() ||
      boundary.asset_sequence != last_scene_asset_sequence_ ||
      boundary.finalized_snapshot_id != last_scene_snapshot_id_ ||
      !last_scene_was_empty_ || asset_decoder_.registry().live_count() != 0U) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_LINEAGE,
                &frame, RenderTransportStatus::RECONCILIATION_MISMATCH,
                ValidationCode::SEQUENCE_MISMATCH,
                RenderOperationCode::OK);
  }
  const RendererFrontendDirectDispatchResult reset =
      direct_dispatcher_.ResetSceneGeneration();
  if (!reset) {
    return FailFromDirect(reset, frame);
  }
  scene_generation_ = boundary.next_generation;
  last_scene_snapshot_id_ = 0U;
  last_scene_asset_sequence_ = 0U;
  last_scene_was_empty_ = false;
  return Success(RendererFrontendTransportDispatchStatus::
                     SCENE_GENERATION_BOUNDARY_CONSUMED,
                 frame);
}

} // namespace RoR::Render
