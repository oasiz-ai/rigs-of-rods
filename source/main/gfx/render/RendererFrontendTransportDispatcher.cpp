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
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::uint32_t kKnownFrameOutputBits =
    static_cast<std::uint32_t>(FrameOutputMask::COLOR) |
    static_cast<std::uint32_t>(FrameOutputMask::DEPTH) |
    static_cast<std::uint32_t>(FrameOutputMask::MOTION_VECTORS) |
    static_cast<std::uint32_t>(FrameOutputMask::OBJECT_ID) |
    static_cast<std::uint32_t>(FrameOutputMask::SURFACE_NORMAL) |
    static_cast<std::uint32_t>(FrameOutputMask::MATERIAL_ID);

} // namespace

ValidationResult ValidateRendererFrontendPresentationPolicy(
    const RendererFrontendPresentationPolicy &policy) {
  if (policy.version != kRendererFrontendPresentationPolicyVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported frontend presentation policy version");
  }
  const std::uint32_t outputs =
      static_cast<std::uint32_t>(policy.requested_outputs);
  if (outputs == 0U || (outputs & ~kKnownFrameOutputBits) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
        "presentation policy requires only known, nonzero outputs");
  }
  if (policy.color_format != PixelFormat::RGBA8_SRGB &&
      policy.color_format != PixelFormat::RGBA16_FLOAT) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "color_format",
        "presentation policy color must be SDR sRGB or linear HDR");
  }
  if (policy.retire_scene_without_render &&
      (policy.present || policy.presentation_surface_revision != 0U ||
       policy.presentation_drawable_width != 0U ||
       policy.presentation_drawable_height != 0U ||
       policy.retire_scene_on_presentation_extent_mismatch)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "retire_scene_without_render",
        "retired scene policy cannot name or present a native surface");
  }
  if (policy.present) {
    if (!HasFrameOutput(policy.requested_outputs, FrameOutputMask::COLOR)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
          "native presentation requires a color output");
    }
    if (policy.presentation_surface_revision == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "presentation_surface_revision",
          "native presentation requires the active surface revision");
    }
    if (policy.retire_scene_on_presentation_extent_mismatch &&
        (policy.presentation_drawable_width == 0U ||
         policy.presentation_drawable_height == 0U)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "presentation_drawable_extent",
          "extent-guarded presentation requires a nonzero drawable extent");
    }
    if (!policy.retire_scene_on_presentation_extent_mismatch &&
        (policy.presentation_drawable_width != 0U ||
         policy.presentation_drawable_height != 0U)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "presentation_drawable_extent",
          "presentation drawable extent requires the stale-scene guard");
    }
  } else if (policy.presentation_surface_revision != 0U ||
             policy.presentation_drawable_width != 0U ||
             policy.presentation_drawable_height != 0U ||
             policy.retire_scene_on_presentation_extent_mismatch) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "presentation_surface_revision",
        "UI-free offscreen rendering cannot name a presentation surface or extent");
  }
  return ValidationResult::Success();
}

bool IsKnownRendererFrontendTransportDispatchStatus(
    RendererFrontendTransportDispatchStatus status) noexcept {
  switch (status) {
  case RendererFrontendTransportDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED:
  case RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED:
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
  case RendererFrontendTransportDispatchStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "unknown";
}

RendererFrontendTransportDispatcher::RendererFrontendTransportDispatcher(
    IRendererFrontend &frontend,
    const RenderBridgeSessionIdentity &session_id) noexcept
    : frontend_(&frontend),
      registry_id_(DeriveRenderAssetRegistryIdFromBridgeSession(session_id)),
      sequence_state_(1U), asset_decoder_(registry_id_, sequence_state_),
      scene_decoder_(sequence_state_) {
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
  if (registry_id_ == 0U || frontend_ == nullptr) {
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
    case RenderTransportMessageKind::RENDER_ASSET_DELTA_V1:
      return DispatchAsset(frame);
    case RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2:
      return DispatchScene(frame, presentation_policy);
    case RenderTransportMessageKind::INPUT_EVENT_BATCH_V1:
    case RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1:
    case RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1:
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
  const RenderOperationResult synchronized =
      frontend_->SynchronizeAssets(*decoded.message->delta());
  if (!synchronized) {
    return Fail(RendererFrontendTransportDispatchStatus::
                    FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
                &frame, RenderTransportStatus::OK, ValidationCode::OK,
                synchronized.code);
  }
  return Success(
      RendererFrontendTransportDispatchStatus::ASSET_DELTA_SYNCHRONIZED, frame);
}

RendererFrontendTransportDispatcher::ResourceReleaseResult
RendererFrontendTransportDispatcher::ReleaseTransferredResources(
    const RenderFrameOutput &output) noexcept {
  ResourceReleaseResult result;
  for (std::size_t index = 0U; index < output.attachments.size(); ++index) {
    const ResourceHandle resource = output.attachments[index].gpu_resource;
    if (!resource.valid()) {
      continue;
    }
    bool already_released = false;
    for (std::size_t prior = 0U; prior < index; ++prior) {
      if (output.attachments[prior].gpu_resource == resource) {
        already_released = true;
        break;
      }
    }
    if (already_released) {
      continue;
    }
    try {
      const RenderOperationResult released =
          frontend_->ReleaseResource(resource);
      if (released) {
        ++result.released;
      } else if (result.first_failure == RenderOperationCode::OK) {
        result.first_failure = released.code;
      }
    } catch (...) {
      if (result.first_failure == RenderOperationCode::OK) {
        result.first_failure = RenderOperationCode::BACKEND_FAILURE;
      }
    }
  }
  return result;
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

  const ValidationResult asset_validation = ValidateSceneSnapshotAssets(
      *decoded.message->scene_snapshot(), asset_decoder_.registry());
  if (!asset_validation) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_LINEAGE, &frame,
                RenderTransportStatus::OK, asset_validation.code,
                RenderOperationCode::OK);
  }

  const CameraViewRequest &camera = decoded.message->camera();
  const bool stale_presentation_extent =
      presentation_policy.retire_scene_on_presentation_extent_mismatch &&
      (camera.width != presentation_policy.presentation_drawable_width ||
       camera.height != presentation_policy.presentation_drawable_height);
  if (presentation_policy.retire_scene_without_render ||
      stale_presentation_extent) {
    RendererFrontendTransportDispatchResult result = Success(
        RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED, frame);
    result.scene_snapshot_id = scene_snapshot_id;
    return result;
  }

  RenderFrameRequest request;
  request.frame_id = decoded.message->sequence();
  request.scene_snapshot = decoded.message->scene_snapshot();
  request.views.push_back(camera);
  request.requested_outputs = presentation_policy.requested_outputs;
  request.color_format = presentation_policy.color_format;
  request.present = presentation_policy.present;
  request.allow_async_compute = presentation_policy.allow_async_compute;
  if (request.present) {
    request.presentation_view_id = decoded.message->camera().view_id;
    request.presentation_surface_revision =
        presentation_policy.presentation_surface_revision;
  }

  const FrontendCapabilityReport capabilities = frontend_->QueryCapabilities();
  const ValidationResult capability_validation =
      ValidateRenderFrameRequestAgainstCapabilities(request, capabilities);
  if (!capability_validation) {
    return Fail(
        RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_CAPABILITIES,
        &frame, RenderTransportStatus::OK, capability_validation.code,
        RenderOperationCode::UNSUPPORTED);
  }

  RenderFrameOutput output;
  RenderOperationResult rendered;
  try {
    rendered = frontend_->Render(request, output);
  } catch (...) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER
            : RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE,
        &frame, RenderTransportStatus::OK, ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::BACKEND_FAILURE
            : cleanup.first_failure,
        cleanup.released);
  }

  RenderOperationResult waited;
  bool wait_threw = false;
  if (rendered) {
    try {
      waited = frontend_->WaitForFrame(request.frame_id,
                                       kInfiniteRenderTimeoutNanoseconds);
    } catch (...) {
      waited.code = RenderOperationCode::BACKEND_FAILURE;
      wait_threw = true;
    }
  }

  ValidationResult output_validation = ValidationResult::Success();
  try {
    if (rendered) {
      output_validation = ValidateRenderFrameOutput(request, output);
    }
  } catch (...) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendTransportDispatchStatus::FAILED_INTERNAL
            : RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE,
        &frame, RenderTransportStatus::ALLOCATION_FAILURE, ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::OUT_OF_MEMORY
            : cleanup.first_failure,
        cleanup.released);
  }
  // A failed infinite wait (normally device loss) cannot cancel the caller's
  // exact-once release obligation. ReleaseResource remains the frontend-owned
  // safe retirement point and may defer destruction behind native queue work.
  const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
  if (cleanup.first_failure != RenderOperationCode::OK) {
    return Fail(
        RendererFrontendTransportDispatchStatus::FAILED_RESOURCE_RELEASE,
        &frame, RenderTransportStatus::OK, output_validation.code,
        cleanup.first_failure, cleanup.released);
  }
  if (!rendered) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_RENDER,
                &frame, RenderTransportStatus::OK, ValidationCode::OK,
                rendered.code, cleanup.released);
  }
  if (!waited || wait_threw) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_WAIT,
                &frame, RenderTransportStatus::OK, output_validation.code,
                waited.code, cleanup.released);
  }
  if (!output_validation) {
    return Fail(RendererFrontendTransportDispatchStatus::FAILED_FRONTEND_OUTPUT,
                &frame, RenderTransportStatus::OK, output_validation.code,
                RenderOperationCode::BACKEND_FAILURE, cleanup.released);
  }
  RendererFrontendTransportDispatchResult result = Success(
      RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED, frame,
      cleanup.released);
  result.scene_snapshot_id = scene_snapshot_id;
  return result;
}

} // namespace RoR::Render
