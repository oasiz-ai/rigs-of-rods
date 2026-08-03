#include "OgreNextDxr7Contract.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

RoR::Render::Dxr7DirectionalShadowSemanticContract CompleteSemanticProof() {
  using namespace RoR::Render;
  Dxr7DirectionalShadowSemanticContract proof;
  proof.capabilities.backend =
      NativeDirectionalShadowBackend::DIRECT3D12_DXR;
  proof.capabilities.hardware_ray_tracing = true;
  proof.capabilities.same_device_raster_and_ray_queue = true;
  proof.capabilities.two_level_acceleration_structures = true;
  proof.capabilities.primary_camera_rays = true;
  proof.capabilities.secondary_directional_visibility_rays = true;
  proof.capabilities.r16_float_visibility = true;
  proof.capabilities.rgba16_float_hybrid_composite = true;
  proof.semantic_probe_only = true;
  proof.blas_count = kNativeDirectionalShadowRequiredBlasCount;
  proof.tlas_instance_count =
      kNativeDirectionalShadowRequiredTlasInstanceCount;
  proof.receiver_instance_id = kDxr7DirectionalShadowReceiverInstanceId;
  proof.occluder_instance_id = kDxr7DirectionalShadowOccluderInstanceId;
  proof.receiver_blas_built = true;
  proof.occluder_blas_built = true;
  proof.tlas_built = true;
  proof.primary_camera_rays_per_sample =
      kNativeDirectionalShadowRequiredPrimaryRayCount;
  proof.secondary_directional_visibility_rays_per_sample =
      kNativeDirectionalShadowRequiredVisibilityRayCount;
  proof.visibility_r16_float = true;
  proof.lineage_r32_uint = true;
  proof.hybrid_rgba16_float = true;
  proof.visibility_readback_completed = true;
  proof.lineage_readback_completed = true;
  proof.hybrid_readback_completed = true;

  proof.samples[0U].visibility =
      NativeDirectionalShadowVisibility::VISIBLE;
  proof.samples[0U].visibility_r16_bits =
      kNativeDirectionalShadowVisibleR16;
  proof.samples[0U].ray_lineage =
      kDxr7DirectionalShadowVisibleLineage;
  proof.samples[0U].primary_hit_instance_id =
      kDxr7DirectionalShadowReceiverInstanceId;
  proof.samples[0U].raster_rgba16.channels =
      {0x3400U, 0x3800U, 0x3a00U, 0x3c00U};
  proof.samples[0U].hybrid_rgba16 = proof.samples[0U].raster_rgba16;

  proof.samples[1U].visibility =
      NativeDirectionalShadowVisibility::OCCLUDED;
  proof.samples[1U].visibility_r16_bits =
      kNativeDirectionalShadowOccludedR16;
  proof.samples[1U].ray_lineage =
      kDxr7DirectionalShadowOccludedLineage;
  proof.samples[1U].primary_hit_instance_id =
      kDxr7DirectionalShadowReceiverInstanceId;
  proof.samples[1U].secondary_blocker_instance_id =
      kDxr7DirectionalShadowOccluderInstanceId;
  proof.samples[1U].raster_rgba16.channels =
      {0x3a00U, 0x3800U, 0x3400U, 0x3800U};
  proof.samples[1U].hybrid_rgba16.channels =
      {0U, 0U, 0U, 0x3800U};
  return proof;
}

RoR::Render::Dxr7PassContract CompletePass() {
  RoR::Render::Dxr7PassContract proof;
  proof.candidate = {true, true, true, true, 11U};
  proof.d3d11on12_device_created = true;
  proof.d3d11on12_created_with_exact_direct_queue = true;
  proof.d3d11on12_underlying_d3d12_device_exact = true;
  proof.d3d11on12_adapter_luid_exact = true;
  proof.ogre_external_device_option_used = true;
  proof.ogre_d3d11_device_exact = true;
  proof.ogre_external_device_active = true;
  proof.ogre_native_window_created = true;
  proof.ogre_pbs_material_created = true;
  proof.ogre_compositor_workspace_created = true;
  proof.ogre_frame_submitted = true;
  proof.ogre_frame_readback_completed = true;
  proof.ogre_frame_nonblank = true;
  proof.ogre_frame_ui_free = true;
  proof.ogre_frame_resources_destroyed = true;
  proof.ogre_teardown = {true, true, true, true, true, true, true, true};
  proof.blas_built = true;
  proof.tlas_built = true;
  proof.state_object_created = true;
  proof.shader_identifiers_resolved = true;
  proof.dispatch_rays_called = true;
  proof.directional_shadow = CompleteSemanticProof();
  proof.queue_fence_before_dispatch = true;
  proof.queue_fence_after_dispatch = true;
  proof.queue_fence_after_ogre = true;
  proof.ogre_shutdown_before_d3d11_release = true;
  proof.d3d11_context_flushed_before_release = true;
  proof.d3d11_released_before_d3d12_queue = true;
  proof.d3d12_queue_released_before_device = true;
  proof.shutdown_completed = true;
  return proof;
}

bool SameTeardownContract(
    const RoR::Render::Dxr7OgreTeardownContract& lhs,
    const RoR::Render::Dxr7OgreTeardownContract& rhs) {
  return lhs.workspace_removed == rhs.workspace_removed &&
         lhs.workspace_definition_removed ==
             rhs.workspace_definition_removed &&
         lhs.render_target_destroyed == rhs.render_target_destroyed &&
         lhs.scene_destroyed == rhs.scene_destroyed &&
         lhs.pbs_datablock_destroyed == rhs.pbs_datablock_destroyed &&
         lhs.pbs_hlms_unregistered == rhs.pbs_hlms_unregistered &&
         lhs.native_window_destroyed == rhs.native_window_destroyed &&
         lhs.root_shutdown_completed == rhs.root_shutdown_completed;
}

void RequireInvalidTeardownStepsAreTransactional() {
  using RoR::Render::Dxr7OgreTeardownStep;
  using RoR::Render::Dxr7OgreTeardownTracker;

  constexpr auto kStepCount =
      static_cast<std::uint8_t>(
          Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED) +
      1U;
  constexpr auto kUnderlyingMaximum =
      std::numeric_limits<std::uint8_t>::max();

  // Exercise every non-enumerator byte at every reachable state: before any
  // teardown, between every ordered pair, and after completion. Each attempt
  // gets a fresh tracker so one invalid value cannot mask another's mutation.
  for (std::uint16_t completed_steps = 0U;
       completed_steps <= kStepCount; ++completed_steps) {
    for (std::uint16_t raw_step = kStepCount;
         raw_step <= kUnderlyingMaximum; ++raw_step) {
      Dxr7OgreTeardownTracker tracker;
      for (std::uint16_t valid_step = 0U;
           valid_step < completed_steps; ++valid_step) {
        Require(tracker.Record(static_cast<Dxr7OgreTeardownStep>(valid_step)),
                "valid teardown setup step was rejected");
      }

      const auto before = tracker.contract();
      const bool was_complete = tracker.complete();
      Require(!tracker.Record(static_cast<Dxr7OgreTeardownStep>(raw_step)),
              "invalid teardown enum value was accepted");
      Require(SameTeardownContract(before, tracker.contract()),
              "invalid teardown enum value mutated the public contract");
      Require(tracker.complete() == was_complete,
              "invalid teardown enum value mutated tracker progress");

      if (completed_steps < kStepCount) {
        Require(tracker.Record(
                    static_cast<Dxr7OgreTeardownStep>(completed_steps)),
                "invalid teardown enum value changed the required next step");
      }
    }
  }
}

}  // namespace

int main() {
  using namespace RoR::Render;
  Dxr7CandidateContract candidate;
  Require(EvaluateDxr7Candidate(candidate) ==
              Dxr7CandidateDecision::NO_HARDWARE_ADAPTER,
          "missing hardware was accepted");
  candidate.hardware_adapter = true;
  Require(EvaluateDxr7Candidate(candidate) ==
              Dxr7CandidateDecision::D3D12_UNAVAILABLE,
          "missing D3D12 ownership primitives were accepted");
  candidate.d3d12_device_available = true;
  candidate.direct_queue_available = true;
  candidate.fence_available = true;
  candidate.raytracing_tier = 10U;
  Require(EvaluateDxr7Candidate(candidate) ==
              Dxr7CandidateDecision::DXR_TIER_BELOW_1_1,
          "DXR tier 1.0 was accepted");
  candidate.raytracing_tier = 11U;
  Require(EvaluateDxr7Candidate(candidate) ==
              Dxr7CandidateDecision::ACCEPT,
          "DXR tier 1.1 hardware was rejected");

  Require(EvaluateDxr7FenceCompletion(1U, 1U) ==
              Dxr7FenceCompletionDecision::COMPLETE,
          "completed fence was not accepted");
  Require(EvaluateDxr7FenceCompletion(0U, 1U) ==
              Dxr7FenceCompletionDecision::WAIT,
          "incomplete fence did not require a wait");
  Require(EvaluateDxr7FenceCompletion(
              std::numeric_limits<std::uint64_t>::max(), 1U) ==
              Dxr7FenceCompletionDecision::DEVICE_REMOVED,
          "device-removal fence sentinel was accepted as completion");

  Dxr7OgreTeardownTracker teardown;
  Require(!teardown.Record(Dxr7OgreTeardownStep::RENDER_TARGET_DESTROYED),
          "out-of-order teardown fault was accepted");
  Require(teardown.Record(Dxr7OgreTeardownStep::WORKSPACE_REMOVED),
          "teardown retry did not recover at the required first step");
  Require(teardown.Record(
              Dxr7OgreTeardownStep::WORKSPACE_DEFINITION_REMOVED),
          "workspace definition teardown was rejected");
  Require(teardown.Record(Dxr7OgreTeardownStep::RENDER_TARGET_DESTROYED),
          "render target teardown was rejected");
  Require(teardown.Record(Dxr7OgreTeardownStep::SCENE_DESTROYED),
          "scene teardown was rejected");
  Require(teardown.Record(Dxr7OgreTeardownStep::PBS_DATABLOCK_DESTROYED),
          "PBS datablock teardown was rejected");
  Require(teardown.Record(Dxr7OgreTeardownStep::PBS_HLMS_UNREGISTERED),
          "PBS HLMS teardown was rejected");
  Require(teardown.Record(Dxr7OgreTeardownStep::NATIVE_WINDOW_DESTROYED),
          "native window teardown was rejected");
  Require(!teardown.complete(),
          "teardown completed before Ogre Root shutdown");
  Require(teardown.Record(Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED),
          "Ogre Root shutdown was rejected");
  Require(teardown.complete(), "complete ordered teardown was rejected");
  Require(!teardown.Record(Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED),
          "duplicate teardown completion was accepted");
  RequireInvalidTeardownStepsAreTransactional();

  Dxr7PassContract proof = CompletePass();
  Require(ValidateDxr7PassContract(proof), "complete RT7 proof rejected");
  Require(ValidateDxr7DirectionalShadowSemanticContract(
              proof.directional_shadow),
          "complete DXR N4A semantic proof rejected");
  proof.dispatch_rays_called = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without DispatchRays");
  proof = CompletePass();
  proof.directional_shadow.exact_ogre_rgba16_source = true;
  Require(!ValidateDxr7PassContract(proof),
          "semantic probe forged exact Ogre RGBA16 ownership");
  proof = CompletePass();
  proof.directional_shadow.hybrid_ogre_image_composite = true;
  Require(!ValidateDxr7PassContract(proof),
          "semantic probe forged an Ogre hybrid composite");
  proof = CompletePass();
  proof.directional_shadow.blas_count = 1U;
  Require(!ValidateDxr7PassContract(proof),
          "semantic probe accepted one BLAS");
  proof = CompletePass();
  proof.directional_shadow.samples[1U].ray_lineage =
      kDxr7DirectionalShadowVisibleLineage;
  Require(!ValidateDxr7PassContract(proof),
          "semantic probe accepted false occluder lineage");
  proof = CompletePass();
  proof.directional_shadow.samples[1U].hybrid_rgba16.channels[0U] = 0x3400U;
  Require(!ValidateDxr7PassContract(proof),
          "semantic probe accepted a nonzero occluded RGB channel");
  proof = CompletePass();
  proof.ogre_d3d11_device_exact = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without exact Ogre device identity");
  proof = CompletePass();
  proof.ogre_frame_readback_completed = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without an Ogre frame readback");
  proof = CompletePass();
  proof.ogre_frame_resources_destroyed = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without Ogre frame-resource teardown");
  proof = CompletePass();
  proof.ogre_teardown.native_window_destroyed = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without native-window teardown");
  proof = CompletePass();
  proof.ogre_teardown.root_shutdown_completed = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted before Ogre Root shutdown");
  proof = CompletePass();
  proof.d3d12_queue_released_before_device = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without ordered owner teardown");
  return 0;
}
