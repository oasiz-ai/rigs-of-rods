#include "OgreNextDxr7Contract.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
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
  proof.blas_built = true;
  proof.tlas_built = true;
  proof.state_object_created = true;
  proof.shader_identifiers_resolved = true;
  proof.dispatch_rays_called = true;
  proof.closest_hit_readback_exact = true;
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

  Dxr7PassContract proof = CompletePass();
  Require(ValidateDxr7PassContract(proof), "complete RT7 proof rejected");
  proof.dispatch_rays_called = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without DispatchRays");
  proof = CompletePass();
  proof.ogre_d3d11_device_exact = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without exact Ogre device identity");
  proof = CompletePass();
  proof.d3d12_queue_released_before_device = false;
  Require(!ValidateDxr7PassContract(proof),
          "pass accepted without ordered owner teardown");
  return 0;
}
