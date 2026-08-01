/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <cstdint>

namespace RoR::Render {

enum class Dxr7CandidateDecision : std::uint8_t {
  ACCEPT = 0,
  NO_HARDWARE_ADAPTER,
  D3D12_UNAVAILABLE,
  DXR_TIER_BELOW_1_1,
};

enum class Dxr7FenceCompletionDecision : std::uint8_t {
  COMPLETE = 0,
  WAIT,
  DEVICE_REMOVED,
};

struct Dxr7CandidateContract {
  bool hardware_adapter = false;
  bool d3d12_device_available = false;
  bool direct_queue_available = false;
  bool fence_available = false;
  std::uint32_t raytracing_tier = 0U;
};

struct Dxr7PassContract {
  Dxr7CandidateContract candidate;
  bool d3d11on12_device_created = false;
  bool d3d11on12_created_with_exact_direct_queue = false;
  bool d3d11on12_underlying_d3d12_device_exact = false;
  bool d3d11on12_adapter_luid_exact = false;
  bool ogre_external_device_option_used = false;
  bool ogre_d3d11_device_exact = false;
  bool ogre_external_device_active = false;
  bool ogre_native_window_created = false;
  bool ogre_pbs_material_created = false;
  bool ogre_compositor_workspace_created = false;
  bool ogre_frame_submitted = false;
  bool ogre_frame_readback_completed = false;
  bool ogre_frame_nonblank = false;
  bool ogre_frame_ui_free = false;
  bool ogre_frame_resources_destroyed = false;
  bool blas_built = false;
  bool tlas_built = false;
  bool state_object_created = false;
  bool shader_identifiers_resolved = false;
  bool dispatch_rays_called = false;
  bool closest_hit_readback_exact = false;
  bool queue_fence_before_dispatch = false;
  bool queue_fence_after_dispatch = false;
  bool queue_fence_after_ogre = false;
  bool ogre_shutdown_before_d3d11_release = false;
  bool d3d11_context_flushed_before_release = false;
  bool d3d11_released_before_d3d12_queue = false;
  bool d3d12_queue_released_before_device = false;
  bool shutdown_completed = false;
};

[[nodiscard]] Dxr7CandidateDecision EvaluateDxr7Candidate(
    const Dxr7CandidateContract& candidate) noexcept;

[[nodiscard]] Dxr7FenceCompletionDecision EvaluateDxr7FenceCompletion(
    std::uint64_t completed_value, std::uint64_t required_value) noexcept;

[[nodiscard]] bool ValidateDxr7PassContract(
    const Dxr7PassContract& contract) noexcept;

const char* Dxr7CandidateDecisionName(
    Dxr7CandidateDecision decision) noexcept;

}  // namespace RoR::Render
