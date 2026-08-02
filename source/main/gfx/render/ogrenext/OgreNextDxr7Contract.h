/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "NativeDirectionalShadowContract.h"

#include <array>
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

enum class Dxr7OgreTeardownStep : std::uint8_t {
  WORKSPACE_REMOVED = 0,
  WORKSPACE_DEFINITION_REMOVED,
  RENDER_TARGET_DESTROYED,
  SCENE_DESTROYED,
  PBS_DATABLOCK_DESTROYED,
  PBS_HLMS_UNREGISTERED,
  NATIVE_WINDOW_DESTROYED,
  ROOT_SHUTDOWN_COMPLETED,
};

struct Dxr7OgreTeardownContract {
  bool workspace_removed = false;
  bool workspace_definition_removed = false;
  bool render_target_destroyed = false;
  bool scene_destroyed = false;
  bool pbs_datablock_destroyed = false;
  bool pbs_hlms_unregistered = false;
  bool native_window_destroyed = false;
  bool root_shutdown_completed = false;
};

class Dxr7OgreTeardownTracker final {
 public:
  [[nodiscard]] bool Record(Dxr7OgreTeardownStep step) noexcept;
  [[nodiscard]] bool complete() const noexcept;
  [[nodiscard]] const Dxr7OgreTeardownContract& contract() const noexcept;

 private:
  std::uint8_t next_step_ = 0U;
  Dxr7OgreTeardownContract contract_;
};

struct Dxr7CandidateContract {
  bool hardware_adapter = false;
  bool d3d12_device_available = false;
  bool direct_queue_available = false;
  bool fence_available = false;
  std::uint32_t raytracing_tier = 0U;
};

constexpr std::uint32_t kDxr7DirectionalShadowSemanticSampleCount = 2U;
constexpr std::uint64_t kDxr7DirectionalShadowReceiverInstanceId = 1U;
constexpr std::uint64_t kDxr7DirectionalShadowOccluderInstanceId = 2U;
constexpr std::uint32_t kDxr7DirectionalShadowVisibleLineage = 1U;
constexpr std::uint32_t kDxr7DirectionalShadowOccludedLineage = 3U;

/// Readback for one deterministic DXR directional-shadow semantic sample.
/// This deliberately stops before claiming that the raster pixel came from an
/// Ogre-owned RGBA16 target or that the hybrid result was written back to it.
struct Dxr7DirectionalShadowSemanticSample {
  NativeDirectionalShadowVisibility visibility =
      NativeDirectionalShadowVisibility::INVALID;
  std::uint16_t visibility_r16_bits = 0xffffU;
  std::uint32_t ray_lineage = 0U;
  std::uint64_t primary_hit_instance_id = 0U;
  std::uint64_t secondary_blocker_instance_id = 0U;
  NativeDirectionalShadowRgba16Pixel raster_rgba16;
  NativeDirectionalShadowRgba16Pixel hybrid_rgba16;
};

/// Windows DXR N4A proves the renderer-neutral two-ray semantics and typed
/// formats on the exact RT7 D3D12 queue. It is not the later D3D11On12 resource
/// bridge: both scope-limitation booleans must remain false for this milestone.
struct Dxr7DirectionalShadowSemanticContract {
  std::uint32_t version = kNativeDirectionalShadowContractVersion;
  NativeDirectionalShadowCapabilities capabilities;
  bool semantic_probe_only = false;
  bool exact_ogre_rgba16_source = false;
  bool hybrid_ogre_image_composite = false;

  std::uint32_t blas_count = 0U;
  std::uint32_t tlas_instance_count = 0U;
  std::uint64_t receiver_instance_id = 0U;
  std::uint64_t occluder_instance_id = 0U;
  bool receiver_blas_built = false;
  bool occluder_blas_built = false;
  bool tlas_built = false;

  std::uint32_t primary_camera_rays_per_sample = 0U;
  std::uint32_t secondary_directional_visibility_rays_per_sample = 0U;
  bool visibility_r16_float = false;
  bool lineage_r32_uint = false;
  bool hybrid_rgba16_float = false;
  bool visibility_readback_completed = false;
  bool lineage_readback_completed = false;
  bool hybrid_readback_completed = false;

  std::array<Dxr7DirectionalShadowSemanticSample,
             kDxr7DirectionalShadowSemanticSampleCount>
      samples{};
};

[[nodiscard]] bool ValidateDxr7DirectionalShadowSemanticContract(
    const Dxr7DirectionalShadowSemanticContract& contract) noexcept;

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
  Dxr7OgreTeardownContract ogre_teardown;
  bool blas_built = false;
  bool tlas_built = false;
  bool state_object_created = false;
  bool shader_identifiers_resolved = false;
  bool dispatch_rays_called = false;
  Dxr7DirectionalShadowSemanticContract directional_shadow;
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
