/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontendDirectDispatcher.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

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

bool IsKnownRendererFrontendDirectDispatchStatus(
    RendererFrontendDirectDispatchStatus status) noexcept {
  switch (status) {
  case RendererFrontendDirectDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED:
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED:
  case RendererFrontendDirectDispatchStatus::
      SCENE_FRAME_PRESENTATION_SURFACE_STALE:
  case RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET:
  case RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL:
  case RendererFrontendDirectDispatchStatus::REJECTED_INVALID_REGISTRY:
  case RendererFrontendDirectDispatchStatus::
      REJECTED_INVALID_PRESENTATION_POLICY:
  case RendererFrontendDirectDispatchStatus::FAILED_ASSET_VALIDATION:
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_ASSET_SYNCHRONIZATION:
  case RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_CAPABILITIES:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_FRAME_RETIREMENT:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_WAIT:
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_OUTPUT:
  case RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE:
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_SCENE_GENERATION_RESET:
  case RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION:
  case RendererFrontendDirectDispatchStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererFrontendDirectDispatchStatus status) noexcept {
  switch (status) {
  case RendererFrontendDirectDispatchStatus::ASSET_DELTA_SYNCHRONIZED:
    return "asset_delta_synchronized";
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED:
    return "scene_frame_completed";
  case RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED:
    return "scene_frame_retired";
  case RendererFrontendDirectDispatchStatus::
      SCENE_FRAME_PRESENTATION_SURFACE_STALE:
    return "scene_frame_presentation_surface_stale";
  case RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET:
    return "scene_generation_reset";
  case RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL:
    return "rejected_terminal";
  case RendererFrontendDirectDispatchStatus::REJECTED_INVALID_REGISTRY:
    return "rejected_invalid_registry";
  case RendererFrontendDirectDispatchStatus::
      REJECTED_INVALID_PRESENTATION_POLICY:
    return "rejected_invalid_presentation_policy";
  case RendererFrontendDirectDispatchStatus::FAILED_ASSET_VALIDATION:
    return "failed_asset_validation";
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_ASSET_SYNCHRONIZATION:
    return "failed_frontend_asset_synchronization";
  case RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION:
    return "failed_scene_validation";
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_CAPABILITIES:
    return "failed_frontend_capabilities";
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER:
    return "failed_frontend_render";
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_FRAME_RETIREMENT:
    return "failed_frontend_frame_retirement";
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_WAIT:
    return "failed_frontend_wait";
  case RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_OUTPUT:
    return "failed_frontend_output";
  case RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE:
    return "failed_resource_release";
  case RendererFrontendDirectDispatchStatus::
      FAILED_FRONTEND_SCENE_GENERATION_RESET:
    return "failed_frontend_scene_generation_reset";
  case RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION:
    return "failed_allocation";
  case RendererFrontendDirectDispatchStatus::FAILED_INTERNAL:
    return "failed_internal";
  }
  return "unknown";
}

RendererFrontendDirectDispatcher::RendererFrontendDirectDispatcher(
    IRendererFrontend &frontend, std::uint64_t registry_id) noexcept
    : frontend_(&frontend), registry_(registry_id) {
  if (registry_id == 0U) {
    terminal_ = true;
    terminal_cause_ =
        RendererFrontendDirectDispatchStatus::REJECTED_INVALID_REGISTRY;
  }
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::Success(
    RendererFrontendDirectDispatchStatus status,
    std::uint64_t scene_snapshot_id, std::uint64_t frontend_frame_id,
    std::uint32_t resources_released) const noexcept {
  RendererFrontendDirectDispatchResult result;
  result.status = status;
  result.terminal_cause = terminal_cause_;
  result.asset_sequence = registry_.sequence();
  result.scene_snapshot_id = scene_snapshot_id;
  result.frontend_frame_id = frontend_frame_id;
  result.resources_released = resources_released;
  result.terminal = terminal_;
  return result;
}

RendererFrontendDirectDispatchResult RendererFrontendDirectDispatcher::Fail(
    RendererFrontendDirectDispatchStatus status,
    ValidationCode validation_code, RenderOperationCode frontend_code,
    std::uint32_t resources_released) noexcept {
  if (!terminal_) {
    terminal_ = true;
    terminal_cause_ = status;
  }
  RendererFrontendDirectDispatchResult result;
  result.status = status;
  result.terminal_cause = terminal_cause_;
  result.validation_code = validation_code;
  result.frontend_code = frontend_code;
  result.asset_sequence = registry_.sequence();
  result.resources_released = resources_released;
  result.terminal = true;
  return result;
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::RetryablePresentationSurfaceStale(
    std::uint64_t scene_snapshot_id,
    std::uint32_t resources_released) const noexcept {
  RendererFrontendDirectDispatchResult result = Success(
      RendererFrontendDirectDispatchStatus::
          SCENE_FRAME_PRESENTATION_SURFACE_STALE,
      scene_snapshot_id, 0U, resources_released);
  result.frontend_code = RenderOperationCode::RESOURCE_STALE;
  return result;
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::SynchronizeAssets(
    const RenderAssetDelta &delta) noexcept {
  if (terminal_) {
    return Fail(RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL);
  }
  try {
    return SynchronizeAssetsImpl(delta);
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_INTERNAL,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::SynchronizeAssetsImpl(
    const RenderAssetDelta &delta) {
  RenderAssetRegistry candidate = registry_;
  const ValidationResult validation = candidate.Apply(delta);
  if (!validation) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ASSET_VALIDATION,
                validation.code);
  }

  RenderOperationResult synchronized;
  try {
    synchronized = frontend_->SynchronizeAssets(delta);
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendDirectDispatchStatus::
                    FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
  if (!synchronized) {
    return Fail(RendererFrontendDirectDispatchStatus::
                    FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
                ValidationCode::OK, synchronized.code);
  }
  registry_ = std::move(candidate);
  return Success(
      RendererFrontendDirectDispatchStatus::ASSET_DELTA_SYNCHRONIZED);
}

RendererFrontendDirectDispatcher::ResourceReleaseResult
RendererFrontendDirectDispatcher::ReleaseTransferredResources(
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

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::RenderScene(
    std::shared_ptr<const SceneSnapshot> scene,
    const CameraViewRequest &camera,
    const RendererFrontendPresentationPolicy &presentation_policy,
    std::shared_ptr<const Ogre14ParticleCapturedFrame>
        continuous_particles) noexcept {
  if (terminal_) {
    return Fail(RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL);
  }
  try {
    return RenderSceneImpl(std::move(scene), camera, presentation_policy,
                           std::move(continuous_particles));
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_INTERNAL,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::RenderSceneImpl(
    std::shared_ptr<const SceneSnapshot> scene,
    const CameraViewRequest &camera,
    const RendererFrontendPresentationPolicy &presentation_policy,
    std::shared_ptr<const Ogre14ParticleCapturedFrame>
        continuous_particles) {
  const ValidationResult policy_validation =
      ValidateRendererFrontendPresentationPolicy(presentation_policy);
  if (!policy_validation) {
    return Fail(RendererFrontendDirectDispatchStatus::
                    REJECTED_INVALID_PRESENTATION_POLICY,
                policy_validation.code);
  }
  if (scene == nullptr || registry_.sequence() == 0U ||
      scene->asset_registry_id() != registry_.registry_id() ||
      scene->asset_sequence() != registry_.sequence()) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
                ValidationCode::SEQUENCE_MISMATCH);
  }
  const std::uint64_t scene_snapshot_id = scene->snapshot_id();
  if (scene_snapshot_id <= last_consumed_scene_snapshot_id_) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
                ValidationCode::SEQUENCE_MISMATCH);
  }
  const ValidationResult asset_validation =
      ValidateSceneSnapshotAssets(*scene, registry_);
  if (!asset_validation) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
                asset_validation.code);
  }
  const ValidationResult camera_validation = ValidateCameraViewRequest(camera);
  if (!camera_validation) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
                camera_validation.code);
  }

  const bool stale_presentation_extent =
      presentation_policy.retire_scene_on_presentation_extent_mismatch &&
      (camera.width != presentation_policy.presentation_drawable_width ||
       camera.height != presentation_policy.presentation_drawable_height);
  const bool retire_without_render =
      presentation_policy.retire_scene_without_render ||
      stale_presentation_extent;

  // Ordinary retired scenes retain the historical no-frontend-work path.
  // Continuous particle frames cannot: lifecycle commands are deltas, so
  // dropping one CREATE/STOP/DESTROY would permanently corrupt N1 state.
  if (retire_without_render && continuous_particles == nullptr) {
    last_consumed_scene_snapshot_id_ = scene_snapshot_id;
    last_scene_snapshot_id_ = scene_snapshot_id;
    last_scene_asset_sequence_ = scene->asset_sequence();
    last_scene_was_empty_ = IsFinalEmptyScene(*scene);
    return Success(RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED,
                   scene_snapshot_id);
  }

  if (last_frontend_frame_id_ ==
      (std::numeric_limits<std::uint64_t>::max)()) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_INTERNAL,
                ValidationCode::VALUE_OUT_OF_RANGE,
                RenderOperationCode::INVALID_ARGUMENT);
  }

  RenderFrameRequest request;
  request.frame_id = last_frontend_frame_id_ + 1U;
  request.scene_snapshot = std::move(scene);
  request.continuous_particles = std::move(continuous_particles);
  request.views.push_back(camera);
  request.requested_outputs = presentation_policy.requested_outputs;
  request.color_format = presentation_policy.color_format;
  request.present = presentation_policy.present;
  request.allow_async_compute = presentation_policy.allow_async_compute;
  if (request.present) {
    request.presentation_view_id = camera.view_id;
    request.presentation_surface_revision =
        presentation_policy.presentation_surface_revision;
  }

  const FrontendCapabilityReport capabilities = frontend_->QueryCapabilities();
  const ValidationResult capability_validation =
      ValidateRenderFrameRequestAgainstCapabilities(request, capabilities);
  if (!capability_validation) {
    return Fail(
        RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_CAPABILITIES,
        capability_validation.code, RenderOperationCode::UNSUPPORTED);
  }

  if (retire_without_render) {
    RenderOperationResult retired;
    try {
      retired = frontend_->RetireFrameState(request);
    } catch (const std::bad_alloc &) {
      return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                  ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
    } catch (const std::length_error &) {
      return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                  ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
    } catch (...) {
      return Fail(
          RendererFrontendDirectDispatchStatus::
              FAILED_FRONTEND_FRAME_RETIREMENT,
          ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
    }
    if (!retired || !frontend_->IsFrameComplete(request.frame_id)) {
      return Fail(
          RendererFrontendDirectDispatchStatus::
              FAILED_FRONTEND_FRAME_RETIREMENT,
          ValidationCode::OK,
          retired ? RenderOperationCode::BACKEND_FAILURE : retired.code);
    }
    RenderOperationResult waited;
    try {
      waited = frontend_->WaitForFrame(request.frame_id,
                                       kInfiniteRenderTimeoutNanoseconds);
    } catch (...) {
      waited = RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "state-only retired frame wait threw an exception");
    }
    if (!waited) {
      return Fail(RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_WAIT,
                  ValidationCode::OK, waited.code);
    }
    last_frontend_frame_id_ = request.frame_id;
    last_consumed_scene_snapshot_id_ = scene_snapshot_id;
    last_scene_snapshot_id_ = scene_snapshot_id;
    last_scene_asset_sequence_ = request.scene_snapshot->asset_sequence();
    last_scene_was_empty_ = IsFinalEmptyScene(*request.scene_snapshot);
    return Success(RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED,
                   scene_snapshot_id, request.frame_id);
  }

  RenderFrameOutput output;
  RenderOperationResult rendered;
  try {
    rendered = frontend_->Render(request, output);
  } catch (const std::bad_alloc &) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::OUT_OF_MEMORY
            : cleanup.first_failure,
        cleanup.released);
  } catch (const std::length_error &) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::OUT_OF_MEMORY
            : cleanup.first_failure,
        cleanup.released);
  } catch (...) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
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
  } catch (const std::bad_alloc &) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::OUT_OF_MEMORY
            : cleanup.first_failure,
        cleanup.released);
  } catch (const std::length_error &) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::OUT_OF_MEMORY
            : cleanup.first_failure,
        cleanup.released);
  } catch (...) {
    const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
    return Fail(
        cleanup.first_failure == RenderOperationCode::OK
            ? RendererFrontendDirectDispatchStatus::FAILED_INTERNAL
            : RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
        ValidationCode::OK,
        cleanup.first_failure == RenderOperationCode::OK
            ? RenderOperationCode::BACKEND_FAILURE
            : cleanup.first_failure,
        cleanup.released);
  }

  const ResourceReleaseResult cleanup = ReleaseTransferredResources(output);
  if (cleanup.first_failure != RenderOperationCode::OK) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_RESOURCE_RELEASE,
                output_validation.code, cleanup.first_failure,
                cleanup.released);
  }
  if (!rendered) {
    if (rendered.code == RenderOperationCode::RESOURCE_STALE &&
        rendered.recovery == RenderOperationRecovery::
                                 RETRY_AFTER_PRESENTATION_SURFACE_UPDATE &&
        request.present) {
      return RetryablePresentationSurfaceStale(scene_snapshot_id,
                                               cleanup.released);
    }
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_RENDER,
                ValidationCode::OK, rendered.code, cleanup.released);
  }
  if (!waited || wait_threw) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_WAIT,
                output_validation.code, waited.code, cleanup.released);
  }
  if (!output_validation) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_FRONTEND_OUTPUT,
                output_validation.code, RenderOperationCode::BACKEND_FAILURE,
                cleanup.released);
  }

  last_frontend_frame_id_ = request.frame_id;
  last_consumed_scene_snapshot_id_ = scene_snapshot_id;
  last_scene_snapshot_id_ = scene_snapshot_id;
  last_scene_asset_sequence_ = request.scene_snapshot->asset_sequence();
  last_scene_was_empty_ = IsFinalEmptyScene(*request.scene_snapshot);
  return Success(RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED,
                 scene_snapshot_id, request.frame_id, cleanup.released);
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::ResetSceneGeneration() noexcept {
  if (terminal_) {
    return Fail(RendererFrontendDirectDispatchStatus::REJECTED_TERMINAL);
  }
  try {
    return ResetSceneGenerationImpl();
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_INTERNAL,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
}

RendererFrontendDirectDispatchResult
RendererFrontendDirectDispatcher::ResetSceneGenerationImpl() {
  if (last_scene_snapshot_id_ == 0U || !last_scene_was_empty_ ||
      last_scene_asset_sequence_ != registry_.sequence() ||
      registry_.live_count() != 0U ||
      scene_generation_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_SCENE_VALIDATION,
                ValidationCode::SEQUENCE_MISMATCH);
  }
  const std::uint64_t next_generation = scene_generation_ + 1U;
  RenderOperationResult reset;
  try {
    reset = frontend_->ResetSceneGeneration(next_generation);
  } catch (const std::bad_alloc &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (const std::length_error &) {
    return Fail(RendererFrontendDirectDispatchStatus::FAILED_ALLOCATION,
                ValidationCode::OK, RenderOperationCode::OUT_OF_MEMORY);
  } catch (...) {
    return Fail(RendererFrontendDirectDispatchStatus::
                    FAILED_FRONTEND_SCENE_GENERATION_RESET,
                ValidationCode::OK, RenderOperationCode::BACKEND_FAILURE);
  }
  if (!reset) {
    return Fail(RendererFrontendDirectDispatchStatus::
                    FAILED_FRONTEND_SCENE_GENERATION_RESET,
                ValidationCode::OK, reset.code);
  }
  scene_generation_ = next_generation;
  last_scene_snapshot_id_ = 0U;
  last_scene_asset_sequence_ = 0U;
  last_scene_was_empty_ = false;
  return Success(
      RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET);
}

} // namespace RoR::Render
