/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Native Ogre-Next parallax-corrected reflection-probe adapter.

#pragma once

#include "../ReflectionProbeCaptureReceipt.h"
#include "../RendererFrontend.h"
#include "../SceneSnapshot.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

/// Ogre reserves render queue 250 for PCC proxy geometry. The capture bit is
/// separate from the proxy bit so a probe face can never sample the blend
/// geometry that consumes prior probe generations.
constexpr std::uint8_t kOgreNextPccReservedRenderQueue = 250U;
constexpr std::uint32_t kOgreNextPccCaptureVisibilityBit = 1U << 28U;
constexpr std::uint32_t kOgreNextPccProxyVisibilityBit = 1U << 29U;
constexpr std::uint64_t kOgreNextPccDeferredReadbackMinimumFrames = 2U;

[[nodiscard]] constexpr std::uint64_t
ComputeOgreNextPccEarliestReadbackFrame(
    std::uint64_t issue_frame_id) noexcept {
  return issue_frame_id <= UINT64_MAX -
                               kOgreNextPccDeferredReadbackMinimumFrames
             ? issue_frame_id + kOgreNextPccDeferredReadbackMinimumFrames
             : UINT64_MAX;
}

[[nodiscard]] constexpr bool IsOgreNextPccReadbackPollEligible(
    std::uint64_t issue_frame_id, std::uint64_t render_frame_id) noexcept {
  return render_frame_id >=
         ComputeOgreNextPccEarliestReadbackFrame(issue_frame_id);
}

[[nodiscard]] constexpr std::uint32_t
ComputeOgreNextPccReadbackLatencyFrames(
    std::uint64_t issue_frame_id,
    std::uint64_t publication_frame_id) noexcept {
  const std::uint64_t latency = publication_frame_id >= issue_frame_id
                                    ? publication_frame_id - issue_frame_id
                                    : 0U;
  return static_cast<std::uint32_t>(
      latency <= UINT32_MAX ? latency : UINT32_MAX);
}

struct OgreNextReflectionProbeRuntimeConfiguration final {
  std::uintptr_t ogre_root = 0U;
  std::uintptr_t ogre_scene_manager = 0U;
  std::uintptr_t ogre_hlms_pbs = 0U;
  std::string shader_media_root;
  std::uint16_t maximum_blend_resolution = 2048U;
  /// The runtime may create and retire Ogre's PCC blend texture only when the
  /// caller owns a fresh HlmsPbs in its default automatic IBL-mipmap mode.
  /// This lets removal use resetIblSpecMipmap(0) to recompute the canonical
  /// live-texture high-water mark without clobbering caller policy.
  bool owns_automatic_ibl_mipmap_policy = false;
  /// Production captures keep the last committed generation visible while
  /// native GPU readback completes. The standalone byte-evidence smoke turns
  /// this off because it intentionally requires the complete capture in the
  /// issuing frame.
  bool defer_capture_readback = true;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  bool retain_capture_evidence = false;
#endif
};

/// One live Ogre Item and the renderer-neutral facts that decide whether it is
/// eligible for a particular probe capture. Native pointers remain borrowed
/// for the duration of PrepareFrame only.
struct OgreNextReflectionProbeItemBinding final {
  std::uintptr_t ogre_item = 0U;
  std::uint32_t authored_visibility_mask = 0U;
  std::uint32_t instance_flags = 0U;
  bool dynamic_mesh = false;
};

/// Optional camera-anchored analytic-sky background admitted into a probe
/// capture. The two Items are the frontend's internal per-present sky objects
/// (not portable instances), so they never travel through the item-binding
/// eligibility rules: an environment is always visible in reflections. For
/// exactly the capture duration the Items carry
/// kOgreNextPccCaptureVisibilityBit, the node is re-centred on the probe's
/// capture position (the dome is camera-relative background geometry, so a
/// probe away from the view camera would otherwise see a displaced or absent
/// sky), and both Unlit datablock colours are scaled by
/// capture_radiance_scale; all three are restored by the same fail-closed
/// restore path as the item flags. Native pointers remain borrowed for the
/// duration of PrepareFrame only.
///
/// capture_radiance_scale seats the dome's physical radiance onto the
/// scene's calibrated ambient scale before it enters the probe. The dome is
/// authored at descriptor radiance (sky pixels), while everything else the
/// probe influences - and everything the probe captures besides the dome -
/// lives at the calibrated level the SH-9 ambient is seated to. An unscaled
/// dome makes the probe's diffuse-GI term several times the entire seated
/// ambient (measured: open ground (37,47,20) -> (91,106,95), the exact
/// wash-out class that retired the hemisphere split), so the frontend passes
/// the same derived SH calibration gain here and the probe stores
/// calibrated-scale sky radiance instead.
struct OgreNextReflectionProbeSkyBinding final {
  std::uintptr_t background_item = 0U;
  std::uintptr_t sun_item = 0U;
  std::uintptr_t sky_node = 0U;
  std::uintptr_t background_datablock = 0U;
  std::uintptr_t sun_datablock = 0U;
  float capture_radiance_scale = 1.0F;
  std::uint32_t authored_visibility_mask = 0U;
  bool enabled = false;
};

struct OgreNextReflectionProbeAudit final {
  std::uint32_t version = 5U;
  std::uint64_t committed_state_digest = 0U;
  std::uint64_t successful_capture_count = 0U;
  std::uint64_t failed_capture_count = 0U;
  std::uint64_t native_execution_evidence = 0U;
  std::uint64_t last_capture_frame_id = 0U;
  std::uint64_t last_capture_simulation_tick = 0U;
  std::uint64_t last_probe_id = 0U;
  std::uint64_t last_content_revision = 0U;
  std::uint64_t last_candidate_generation = 0U;
  std::uint64_t last_deterministic_seed = 0U;
  std::uint64_t last_capture_digest = 0U;
  std::uint64_t last_canonical_payload_bytes = 0U;
  std::uint64_t filtered_finite_component_count = 0U;
  std::uint64_t filtered_nonzero_rgb_component_count = 0U;
  float filtered_max_absolute_rgb = 0.0F;
  std::uint32_t live_probe_count = 0U;
  /// Probes torn down by RetireProbesForSceneGeneration because the
  /// generation's final scene was RETIRED rather than rendered. Monotonic for
  /// the runtime lifetime. Nonzero is expected at every terrain unload, map
  /// change, bundle reload and shutdown that took the retire path; it is the
  /// diagnostic that replaces what the reset refusal used to signal.
  std::uint64_t scene_reset_retired_probe_count = 0U;
  std::uint64_t deferred_capture_issue_count = 0U;
  std::uint64_t deferred_capture_completion_count = 0U;
  std::uint64_t last_capture_publication_frame_id = 0U;
  std::uint32_t last_capture_readback_latency_frames = 0U;
  std::uint32_t scene_reset_teardowns = 0U;
  std::uint32_t completed_face_count = 0U;
  std::uint16_t last_probe_resolution = 0U;
  std::uint16_t blend_resolution = 0U;
  std::uint16_t completed_mip_count = 0U;
  bool initialized = false;
  bool compositor_defined_in_code = false;
  bool exact_resources_loaded = false;
  bool pcc_enabled = false;
  bool pbs_bound = false;
  bool blend_texture_ready = false;
  bool ui_free_capture = false;
  bool reserved_render_queue_excluded = false;
};

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
/// Canonical diagnostic bytes retained only by the standalone native smoke.
/// Raw bytes are RGBA16F mip zero in face-major order. Filtered bytes are
/// RGBA16F in mip-major then face-major order. Row padding is excluded.
struct OgreNextReflectionProbeCaptureEvidence final {
  std::uint32_t version = 1U;
  ReflectionProbeCaptureBackend backend =
      ReflectionProbeCaptureBackend::OGRE_NEXT_METAL;
  std::string render_system;
  std::string device_name;
  std::string driver_version;
  std::uint64_t render_frame_id = 0U;
  std::uint64_t simulation_tick = 0U;
  std::uint64_t probe_id = 0U;
  std::uint64_t content_revision = 0U;
  std::uint64_t candidate_generation = 0U;
  std::uint64_t deterministic_seed = 0U;
  std::uint16_t capture_resolution = 0U;
  ReflectionProbeCaptureMipMetadata filtered_mips;
  std::vector<std::uint8_t> raw_mip_zero_rgba16f;
  std::vector<std::uint8_t> filtered_rgba16f;
  bool valid = false;
};

/// Native ownership ledger exposed only to the standalone smoke. No Ogre type
/// or address crosses the renderer-neutral boundary.
struct OgreNextReflectionProbeNativeOwnershipEvidence final {
  std::uint32_t version = 1U;
  std::uint64_t pcc_create_count = 0U;
  std::uint64_t pcc_destroy_count = 0U;
  std::uint32_t live_pcc_count = 0U;
  bool pbs_query_succeeded = false;
  bool pbs_unbound = false;
  bool pbs_bound_to_runtime = false;
};
#endif

/// Concrete capture authority named by ReflectionProbeCaptureReceipt.
///
/// This class is the only shipping code allowed to issue a successful opaque
/// receipt. Ogre headers and allocations are confined to the private
/// implementation so the renderer-neutral boundary remains cross-platform.
class OgreNextReflectionProbeRuntime final {
public:
  OgreNextReflectionProbeRuntime();
  ~OgreNextReflectionProbeRuntime();

  OgreNextReflectionProbeRuntime(const OgreNextReflectionProbeRuntime &) =
      delete;
  OgreNextReflectionProbeRuntime &
  operator=(const OgreNextReflectionProbeRuntime &) = delete;
  OgreNextReflectionProbeRuntime(OgreNextReflectionProbeRuntime &&) = delete;
  OgreNextReflectionProbeRuntime &
  operator=(OgreNextReflectionProbeRuntime &&) = delete;

  [[nodiscard]] RenderOperationResult
  Initialize(OgreNextReflectionProbeRuntimeConfiguration configuration);

  /// Plans and captures at most one scheduler-selected probe without
  /// replacing a committed native cubemap. In deferred mode, later calls poll
  /// the native readback without blocking and keep preparing pass-through
  /// reflection transactions until the complete candidate is publishable.
  /// `tracking_camera` and every item pointer are borrowed native Ogre objects
  /// owned by the calling frontend. Exactly one FinalizeFrame or AbortFrame
  /// must follow every successful PrepareFrame.
  [[nodiscard]] RenderOperationResult
  PrepareFrame(std::uint64_t render_frame_id, std::uint64_t simulation_tick,
               const Double3 &absolute_world_origin_meters,
               const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
               const std::vector<OgreNextReflectionProbeItemBinding> &items,
               std::uintptr_t tracking_camera,
               const OgreNextReflectionProbeSkyBinding &sky = {});

  /// Atomically publishes a completed prepared capture after the enclosing
  /// frontend has completed all failure-prone rendering, validation, and
  /// interop publication work. A deferred pass-through frame finalizes
  /// successfully without changing capture lineage or the committed cubemap.
  [[nodiscard]] RenderOperationResult
  FinalizeFrame(std::uint64_t render_frame_id);

  /// Drops the prepared scheduler plan and native candidate. Returns false if
  /// cleanup could not restore the pre-frame reflection ownership state.
  [[nodiscard]] bool AbortFrame(std::uint64_t render_frame_id) noexcept;

  [[nodiscard]] OgreNextReflectionProbeAudit QueryAudit() const noexcept;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextReflectionProbeCaptureEvidence
  QueryLastCaptureEvidence() const;
  [[nodiscard]] OgreNextReflectionProbeNativeOwnershipEvidence
  QueryNativeOwnershipEvidence() const noexcept;
#endif

  /// Completes probe lifecycle for a generation whose final scene was RETIRED
  /// instead of rendered. Unbinds HlmsPbs, moves every committed native probe
  /// onto the deferred list, and empties the live set: exactly the state
  /// PrepareFrame+FinalizeFrame reach from an empty descriptor list, minus the
  /// scheduler/capture transaction they cannot legitimately open outside a
  /// render. Idempotent -- returns Success without touching native state when
  /// the live set is already empty and PBS already unbound, so a rendered
  /// final scene behaves byte-identically to today. Native destruction is NOT
  /// performed here: it stays with the deferred drain inside
  /// ResetSceneGeneration, so both paths converge on one destruction site.
  /// Call immediately before ResetSceneGeneration, which still re-queries the
  /// native binding and refuses if this did not achieve the required state.
  [[nodiscard]] RenderOperationResult RetireProbesForSceneGeneration();

  /// Called only after the final empty scene has committed -- and, when that
  /// scene was retired without a render, only after
  /// RetireProbesForSceneGeneration. Destroys deferred probes and resets
  /// scheduler/tick/tombstone lineage while retaining the initialized Ogre
  /// owners, compositor definitions, and global device.
  [[nodiscard]] RenderOperationResult ResetSceneGeneration();

  /// Quiesces captures, unbinds PBS, and destroys probes while Root,
  /// SceneManager, and HLMS PBS are still alive.
  [[nodiscard]] bool Shutdown() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
