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

struct OgreNextReflectionProbeAudit final {
  std::uint32_t version = 2U;
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
  std::uint32_t completed_face_count = 0U;
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
  /// publishing scheduler lineage or replacing a committed native cubemap.
  /// `tracking_camera` and every item pointer are borrowed native Ogre objects
  /// owned by the calling frontend. Exactly one FinalizeFrame or AbortFrame
  /// must follow every successful PrepareFrame.
  [[nodiscard]] RenderOperationResult
  PrepareFrame(std::uint64_t render_frame_id, std::uint64_t simulation_tick,
               const Double3 &absolute_world_origin_meters,
               const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
               const std::vector<OgreNextReflectionProbeItemBinding> &items,
               std::uintptr_t tracking_camera);

  /// Atomically publishes the prepared capture after the enclosing frontend
  /// has completed all failure-prone rendering, readback, validation, and
  /// interop publication work.
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

  /// Called only after the final empty scene has committed. Destroys deferred
  /// probes and resets scheduler/tick/tombstone lineage while retaining the
  /// initialized Ogre owners, compositor definitions, and global device.
  [[nodiscard]] RenderOperationResult ResetSceneGeneration();

  /// Quiesces captures, unbinds PBS, and destroys probes while Root,
  /// SceneManager, and HLMS PBS are still alive.
  [[nodiscard]] bool Shutdown() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
