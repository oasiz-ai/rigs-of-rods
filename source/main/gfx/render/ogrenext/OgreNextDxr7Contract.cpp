/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextDxr7Contract.h"

#include <cstddef>
#include <limits>

namespace RoR::Render {

Dxr7CandidateDecision EvaluateDxr7Candidate(
    const Dxr7CandidateContract& candidate) noexcept {
  if (!candidate.hardware_adapter) {
    return Dxr7CandidateDecision::NO_HARDWARE_ADAPTER;
  }
  if (!candidate.d3d12_device_available) {
    return Dxr7CandidateDecision::D3D12_UNAVAILABLE;
  }
  // D3D12_RAYTRACING_TIER_1_1 is numerically 11. Keeping the portable
  // contract independent of Windows headers lets every platform mutation-test
  // this admission gate.
  if (candidate.raytracing_tier < 11U) {
    return Dxr7CandidateDecision::DXR_TIER_BELOW_1_1;
  }
  if (!candidate.direct_queue_available || !candidate.fence_available) {
    return Dxr7CandidateDecision::D3D12_UNAVAILABLE;
  }
  return Dxr7CandidateDecision::ACCEPT;
}

Dxr7FenceCompletionDecision EvaluateDxr7FenceCompletion(
    std::uint64_t completed_value, std::uint64_t required_value) noexcept {
  // ID3D12Fence::GetCompletedValue returns UINT64_MAX after device removal.
  // Treating that sentinel as a very large successful fence value would turn
  // a lost device into forged synchronization evidence.
  if (completed_value == std::numeric_limits<std::uint64_t>::max()) {
    return Dxr7FenceCompletionDecision::DEVICE_REMOVED;
  }
  return completed_value >= required_value
             ? Dxr7FenceCompletionDecision::COMPLETE
             : Dxr7FenceCompletionDecision::WAIT;
}

bool Dxr7OgreTeardownTracker::Record(
    Dxr7OgreTeardownStep step) noexcept {
  const auto observed = static_cast<std::uint8_t>(step);
  constexpr auto kStepCount =
      static_cast<std::uint8_t>(
          Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED) +
      1U;
  if (observed >= kStepCount || observed != next_step_) {
    return false;
  }
  switch (step) {
    case Dxr7OgreTeardownStep::WORKSPACE_REMOVED:
      contract_.workspace_removed = true;
      break;
    case Dxr7OgreTeardownStep::WORKSPACE_DEFINITION_REMOVED:
      contract_.workspace_definition_removed = true;
      break;
    case Dxr7OgreTeardownStep::RENDER_TARGET_DESTROYED:
      contract_.render_target_destroyed = true;
      break;
    case Dxr7OgreTeardownStep::SCENE_DESTROYED:
      contract_.scene_destroyed = true;
      break;
    case Dxr7OgreTeardownStep::PBS_DATABLOCK_DESTROYED:
      contract_.pbs_datablock_destroyed = true;
      break;
    case Dxr7OgreTeardownStep::PBS_HLMS_UNREGISTERED:
      contract_.pbs_hlms_unregistered = true;
      break;
    case Dxr7OgreTeardownStep::NATIVE_WINDOW_DESTROYED:
      contract_.native_window_destroyed = true;
      break;
    case Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED:
      contract_.root_shutdown_completed = true;
      break;
    default:
      return false;
  }
  ++next_step_;
  return true;
}

bool Dxr7OgreTeardownTracker::complete() const noexcept {
  return next_step_ ==
             static_cast<std::uint8_t>(
                 Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED) +
                 1U &&
         contract_.workspace_removed &&
         contract_.workspace_definition_removed &&
         contract_.render_target_destroyed && contract_.scene_destroyed &&
         contract_.pbs_datablock_destroyed &&
         contract_.pbs_hlms_unregistered &&
         contract_.native_window_destroyed &&
         contract_.root_shutdown_completed;
}

const Dxr7OgreTeardownContract& Dxr7OgreTeardownTracker::contract()
    const noexcept {
  return contract_;
}

bool ValidateDxr7DirectionalShadowSemanticContract(
    const Dxr7DirectionalShadowSemanticContract& contract) noexcept {
  if (contract.version != kNativeDirectionalShadowContractVersion ||
      !contract.semantic_probe_only || contract.exact_ogre_rgba16_source ||
      contract.hybrid_ogre_image_composite ||
      contract.capabilities.backend !=
          NativeDirectionalShadowBackend::DIRECT3D12_DXR ||
      !HasAttestedNativeDirectionalShadowCapabilities(contract.capabilities) ||
      contract.blas_count != kNativeDirectionalShadowRequiredBlasCount ||
      contract.tlas_instance_count !=
          kNativeDirectionalShadowRequiredTlasInstanceCount ||
      contract.receiver_instance_id !=
          kDxr7DirectionalShadowReceiverInstanceId ||
      contract.occluder_instance_id !=
          kDxr7DirectionalShadowOccluderInstanceId ||
      !contract.receiver_blas_built || !contract.occluder_blas_built ||
      !contract.tlas_built ||
      contract.primary_camera_rays_per_sample !=
          kNativeDirectionalShadowRequiredPrimaryRayCount ||
      contract.secondary_directional_visibility_rays_per_sample !=
          kNativeDirectionalShadowRequiredVisibilityRayCount ||
      !contract.visibility_r16_float || !contract.lineage_r32_uint ||
      !contract.hybrid_rgba16_float ||
      !contract.visibility_readback_completed ||
      !contract.lineage_readback_completed ||
      !contract.hybrid_readback_completed) {
    return false;
  }

  constexpr std::array<NativeDirectionalShadowVisibility,
                       kDxr7DirectionalShadowSemanticSampleCount>
      kExpectedVisibility = {NativeDirectionalShadowVisibility::VISIBLE,
                             NativeDirectionalShadowVisibility::OCCLUDED};
  constexpr std::array<std::uint32_t,
                       kDxr7DirectionalShadowSemanticSampleCount>
      kExpectedLineage = {kDxr7DirectionalShadowVisibleLineage,
                          kDxr7DirectionalShadowOccludedLineage};
  try {
    for (std::size_t index = 0U; index < contract.samples.size(); ++index) {
      const Dxr7DirectionalShadowSemanticSample& sample =
          contract.samples[index];
      if (sample.visibility != kExpectedVisibility[index] ||
          sample.ray_lineage != kExpectedLineage[index] ||
          sample.primary_hit_instance_id != contract.receiver_instance_id ||
          sample.secondary_blocker_instance_id !=
              (sample.visibility == NativeDirectionalShadowVisibility::OCCLUDED
                   ? contract.occluder_instance_id
                   : 0U)) {
        return false;
      }
      NativeDirectionalShadowSampleOracle oracle;
      if (!TryBuildNativeDirectionalShadowSampleOracle(
              sample.visibility, sample.raster_rgba16, oracle) ||
          sample.visibility_r16_bits != oracle.visibility_r16_bits ||
          sample.hybrid_rgba16.channels != oracle.hybrid_rgba16.channels) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

bool ValidateDxr7PassContract(const Dxr7PassContract& contract) noexcept {
  return EvaluateDxr7Candidate(contract.candidate) ==
             Dxr7CandidateDecision::ACCEPT &&
         contract.d3d11on12_device_created &&
         contract.d3d11on12_created_with_exact_direct_queue &&
         contract.d3d11on12_underlying_d3d12_device_exact &&
         contract.d3d11on12_adapter_luid_exact &&
         contract.ogre_external_device_option_used &&
         contract.ogre_d3d11_device_exact &&
         contract.ogre_external_device_active &&
         contract.ogre_native_window_created &&
         contract.ogre_pbs_material_created &&
         contract.ogre_compositor_workspace_created &&
         contract.ogre_frame_submitted &&
         contract.ogre_frame_readback_completed &&
         contract.ogre_frame_nonblank && contract.ogre_frame_ui_free &&
         contract.ogre_frame_resources_destroyed &&
         contract.ogre_teardown.workspace_removed &&
         contract.ogre_teardown.workspace_definition_removed &&
         contract.ogre_teardown.render_target_destroyed &&
         contract.ogre_teardown.scene_destroyed &&
         contract.ogre_teardown.pbs_datablock_destroyed &&
         contract.ogre_teardown.pbs_hlms_unregistered &&
         contract.ogre_teardown.native_window_destroyed &&
         contract.ogre_teardown.root_shutdown_completed &&
         contract.blas_built &&
         contract.tlas_built && contract.state_object_created &&
         contract.shader_identifiers_resolved &&
         contract.dispatch_rays_called &&
         ValidateDxr7DirectionalShadowSemanticContract(
             contract.directional_shadow) &&
         contract.queue_fence_before_dispatch &&
         contract.queue_fence_after_dispatch &&
         contract.queue_fence_after_ogre &&
         contract.ogre_shutdown_before_d3d11_release &&
         contract.d3d11_context_flushed_before_release &&
         contract.d3d11_released_before_d3d12_queue &&
         contract.d3d12_queue_released_before_device &&
         contract.shutdown_completed;
}

const char* Dxr7CandidateDecisionName(
    Dxr7CandidateDecision decision) noexcept {
  switch (decision) {
    case Dxr7CandidateDecision::ACCEPT:
      return "accept";
    case Dxr7CandidateDecision::NO_HARDWARE_ADAPTER:
      return "no_hardware_adapter";
    case Dxr7CandidateDecision::D3D12_UNAVAILABLE:
      return "d3d12_unavailable";
    case Dxr7CandidateDecision::DXR_TIER_BELOW_1_1:
      return "dxr_tier_below_1_1";
  }
  return "unknown";
}

}  // namespace RoR::Render
