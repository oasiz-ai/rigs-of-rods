/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Isolated Ogre-Next N1 static-PBR offscreen frontend.

#pragma once

#include "../RendererFrontend.h"
#include "OgreNextHdrTemporalContract.h"
#include "OgreNextPssmShadowPolicy.h"
#include "RasterFeatureTier.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

enum class OgreNextNativeFeatureTier : std::uint8_t;
struct OgreNextReflectionProbeAudit;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
struct OgreNextReflectionProbeCaptureEvidence;
struct OgreNextReflectionProbeNativeOwnershipEvidence;
#endif

constexpr std::uint32_t kOgreNextN1PresentationContractVersion = 2U;

/// The exact one-frame gate remains the default and is deliberately unchanged.
/// The production run loop is a separate opt-in lifetime contract that reuses
/// its source target and Compositor2 graph across presented frames.
enum class OgreNextN1PresentationMode : std::uint8_t {
  EXACT_ONE_FRAME_GATE = 0,
  PRODUCTION_RUN_LOOP,
};

struct OgreNextN1PresentationParameter final {
  std::string name;
  std::string value;
};

/// Optional probe-only presentation binding copied from the hardened SDL
/// window host. The frontend owns these strings, but native identities named
/// by them remain borrowed from the host until Shutdown completes. Production
/// child/package admission deliberately remains outside this contract.
struct OgreNextN1PresentationConfiguration final {
  std::uint32_t version = kOgreNextN1PresentationContractVersion;
  bool enabled = false;
  OgreNextN1PresentationMode mode =
      OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE;
  /// Production child mode exports only a frontend-owned GPU lease for the
  /// already-presented source target. No CPU attachment bytes are generated.
  /// The legacy one-frame and evidence smokes retain readback by default.
  bool gpu_only_output = false;
  std::string shader_media_root;
  NativeWindowHandle exact_window;
  std::array<OgreNextN1PresentationParameter, 2U> renderer_options{};
  std::size_t renderer_option_count = 0U;
  std::array<OgreNextN1PresentationParameter, 2U>
      bootstrap_window_parameters{};
  std::size_t bootstrap_window_parameter_count = 0U;
  std::array<OgreNextN1PresentationParameter, 8U>
      presentation_window_parameters{};
  std::size_t presentation_window_parameter_count = 0U;
  /// Invoked only after the two-channel Compositor2 workspace exists and its
  /// external window texture has passed identity/extent validation. The host
  /// callback performs its bounded native show/configure acknowledgement.
  void *show_callback_context = nullptr;
  bool (*show_after_workspace_ready)(
      void *context, FrontendSurfaceUpdate *acknowledged_surface) = nullptr;
};

/// Exact evidence for the optional one-frame native presentation gate.
/// Counters are committed only after source-only GPU readback and the matching
/// RenderFrameOutput have both validated successfully.
struct OgreNextN1PresentationAudit final {
  std::uint32_t version = kOgreNextN1PresentationContractVersion;
  bool enabled = false;
  OgreNextN1PresentationMode mode =
      OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE;
  bool exact_external_window_binding = false;
  bool exact_two_external_channels = false;
  bool ui_free_source = false;
  bool gpu_quad_copy = false;
  bool cpu_window_copy = false;
  bool workspace_ready_before_show = false;
  bool bounded_swap_completed = false;
  bool monotonic_presented_frame_ids = false;
  std::uint64_t window_moved_or_resized_calls = 0U;
  std::uint64_t show_callback_calls = 0U;
  std::uint64_t source_target_creates = 0U;
  std::uint64_t source_target_destroys = 0U;
  std::uint64_t compositor_node_definition_creates = 0U;
  std::uint64_t compositor_node_definition_destroys = 0U;
  std::uint64_t compositor_workspace_creates = 0U;
  std::uint64_t compositor_workspace_destroys = 0U;
  std::uint64_t compositor_workspace_rebinds = 0U;
  std::uint64_t surface_graph_rebuilds = 0U;
  std::uint64_t suspended_surface_updates = 0U;
  std::uint64_t restored_surface_updates = 0U;
  std::uint64_t source_scene_passes = 0U;
  std::uint64_t presentation_quad_passes = 0U;
  std::uint64_t render_one_frame_calls = 0U;
  std::uint64_t window_final_target_updates = 0U;
  std::uint64_t window_swap_completions = 0U;
  std::uint64_t presented_frames = 0U;
  std::uint64_t source_readbacks = 0U;
  std::uint64_t gpu_only_output_frames = 0U;
  std::uint64_t first_presented_frame_id = 0U;
  std::uint64_t last_presented_frame_id = 0U;
  std::uint64_t last_view_id = 0U;
  std::uint64_t last_surface_revision = 0U;
  std::uint32_t last_width = 0U;
  std::uint32_t last_height = 0U;
};

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
/// Isolated native-smoke fault seam; never compiled into the production RoR
/// target. Each value injects one failure after the named Ogre allocation step.
enum class OgreNextN1TextureUploadFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_CREATE,
  AFTER_SET_RESOLUTION,
  AFTER_SET_MIPMAPS,
  AFTER_SET_PIXEL_FORMAT,
  AFTER_SCHEDULE_TRANSITION,
};

/// Native Image2 staging proof exposed only by the standalone smoke seam.
/// Every counted byte was read back from Ogre's row-pitched RG8 TextureBox
/// before the same Image2 was handed to TextureGpu residency upload.
struct OgreNextN1NormalUploadAudit final {
  std::uint32_t version = 1U;
  std::uint64_t verified_uploads = 0U;
  std::uint64_t verified_mip_levels = 0U;
  std::uint64_t verified_rows = 0U;
  std::uint64_t verified_texels = 0U;
  std::uint64_t verified_rg_bytes = 0U;
  std::uint64_t verified_padded_source_rows = 0U;
  bool exact_source_rg_to_native_image = false;
};

/// PSSM-only transactional fault seam for the standalone native smoke.
enum class OgreNextN1PssmFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_D32_ATLAS_CREATE,
  DURING_D32_ATLAS_CLEANUP_LOOKUP,
  AFTER_RECEIVER_DATABLOCK_CLONE,
  AFTER_WORKSPACE_NODE_DEFINITION,
  DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP,
  DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP,
  DURING_WORKSPACE_NODE_CLEANUP_LOOKUP,
  DURING_SHADOW_NODE_CLEANUP_LOOKUP,
  DURING_TARGET_TEXTURE_CLEANUP_LOOKUP,
};

/// HDR-only transactional fault seam for the standalone native smoke.
enum class OgreNextN1HdrFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_RESOURCE_GROUP_CREATE,
  AFTER_RESOURCE_LOCATIONS,
  AFTER_RESOURCE_GROUP_INITIALIZE,
  AFTER_WORKSPACE_DEFINITION,
  AFTER_OUTPUT_CREATE,
  AFTER_OUTPUT_CONFIGURE,
  AFTER_WORKSPACE_CREATE,
  AFTER_PARAMETER_BINDING,
  AFTER_WARMUP_FRAME_ONE,
  AFTER_WARMUP_FRAME_TWO,
  /// Fires after native HDR history has been read and transactionally
  /// prepared, but before any public frame or audit state is committed.
  AFTER_FRAME_COMMIT_PREPARE,
};
#endif

/// Runtime-owned Ogre shader media. The root is an absolute UTF-8 path containing
/// the pinned `Hlms` directory; packaging code resolves its own relative
/// resource layout before constructing the frontend.
struct OgreNextN1Configuration final {
  std::string shader_media_root;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1TextureUploadFailureStage texture_upload_failure_stage =
      OgreNextN1TextureUploadFailureStage::NONE;
  bool retain_reflection_capture_evidence = false;
  OgreNextN1PssmFailureStage pssm_failure_stage =
      OgreNextN1PssmFailureStage::NONE;
  OgreNextN1HdrFailureStage hdr_failure_stage =
      OgreNextN1HdrFailureStage::NONE;
  /// Connects Ogre's real `HdrRenderUi` node and creates a full-screen magenta
  /// Overlay panel for the isolated negative-control proof. Production callers
  /// must leave this false.
  bool hdr_ui_overlay_control = false;
#endif
  // Kept after the optional fault-injection seam so the existing standalone
  // test aggregate remains source-compatible. Production callers should set
  // this field by name after constructing the configuration.
  OgreNextDirectionalShadowMode directional_shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  /// Enables Ogre-Next's persistent RGBA16 scene, R16 auto-exposure,
  /// multi-pass bloom, filmic tone-map, and sRGB output compositor.  It is an
  /// explicit RT4 raster mode so raw linear-HDR capture remains unchanged.
  bool enable_hdr_compositor = false;
  OgreNextHdrTemporalConfiguration hdr_temporal_configuration{};
  OgreNextN1PresentationConfiguration presentation{};
};

/// Exact observable state of the opt-in persistent HDR compositor. Counts
/// advance only after a finite-positive native R16 history sample has passed
/// the versioned conditioning/storage bound and the corresponding public frame
/// has been committed. Accepted native bits are authoritative.
struct OgreNextHdrCompositorAudit final {
  std::uint32_t version = 2U;
  bool enabled = false;
  bool native_workspace_live = false;
  bool deterministic_delta_bound = false;
  bool native_r16_history_validated = false;
  bool exact_current_to_old_copy_verified = false;
  bool ui_free_workspace_verified = false;
  OgreNextHdrHistoryValidationMode history_validation_mode =
      OgreNextHdrHistoryValidationMode::NONE;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t warmup_frames = 0U;
  std::uint64_t committed_frames = 0U;
  std::uint16_t previous_inverse_luminance_r16_bits = 0U;
  std::uint16_t reference_inverse_luminance_r16_bits = 0U;
  float history_ogre_exposure = 0.0F;
  float history_minimum_auto_exposure = 0.0F;
  float history_maximum_auto_exposure = 0.0F;
  float history_average_log_luminance = 0.0F;
  std::uint16_t history_previous_inverse_luminance_r16_bits = 0U;
  float history_delta_seconds = 0.0F;
  double history_absolute_error = 0.0;
  double history_allowed_error = 0.0;
  double history_conditioning_bound = 0.0;
  double history_binary32_rounding_bound = 0.0;
  double history_storage_ulp = 0.0;
  std::uint32_t history_r16_ulp_distance = 0U;
};

struct OgreNextPssmNativeAabb final {
  Float3 minimum;
  Float3 maximum;
};

/// One direct native bounds observation for a PSSM frame item. The expected
/// values are retained beside Ogre's Mesh and Item readbacks so an evidence
/// consumer can reject a report that mutates either side of the comparison.
struct OgreNextPssmNativeBoundsObservation final {
  std::uint64_t instance_id = 0U;
  bool casts_shadow = false;
  bool receives_shadow = false;
  OgreNextPssmNativeAabb expected_local;
  OgreNextPssmNativeAabb ogre_mesh_local;
  OgreNextPssmNativeAabb ogre_item_local;
  OgreNextPssmNativeAabb expected_world;
  OgreNextPssmNativeAabb ogre_item_world;
};

struct OgreNextPssmShadowRuntimeAudit final {
  std::uint32_t version = kOgreNextPssmShadowContractVersion;
  OgreNextDirectionalShadowMode configured_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  std::uint64_t shadow_frames_completed = 0U;
  std::uint64_t shadow_node_creates = 0U;
  std::uint64_t shadow_node_destroys = 0U;
  std::uint64_t workspace_node_definition_creates = 0U;
  std::uint64_t workspace_node_definition_destroys = 0U;
  std::uint64_t receiver_datablock_creates = 0U;
  std::uint64_t receiver_datablock_destroys = 0U;
  bool capability_check_completed = false;
  std::uint32_t observed_maximum_texture_dimension = 0U;
  bool atlas_dimensions_supported = false;
  bool texture_gather_supported = false;
  bool d32_probe_attempted = false;
  bool d32_render_target_supported = false;
  bool d32_atlas_allocation_verified = false;
  bool d32_atlas_readback_verified = false;
  bool d32_atlas_cleanup_verified = false;
  std::uint64_t d32_atlas_cleanup_absence_checks = 0U;
  std::uint64_t workspace_definition_cleanup_absence_checks = 0U;
  std::uint64_t workspace_node_cleanup_absence_checks = 0U;
  std::uint64_t shadow_node_cleanup_absence_checks = 0U;
  std::uint64_t receiver_datablock_cleanup_absence_checks = 0U;
  std::uint64_t target_texture_cleanup_absence_checks = 0U;
  OgreNextPssmShadowFramePlan last_frame;
  OgreNextPssmSplitPolicy last_native_splits;
  std::array<float, kOgreNextPssmCascadeCount>
      last_native_normal_offset_bias{};
  bool native_projection_extents_verified = false;
  bool native_readback_verified = false;
  bool native_bounds_readback_verified = false;
  std::vector<OgreNextPssmNativeBoundsObservation>
      last_native_bounds_observations;
};

/// Runtime audit of the native RT4/V1 texture variants owned by one frontend.
///
/// A source texture may require one sampled RGBA allocation (base colour or
/// emissive), the two R8 derivatives used by a packed metallic-roughness
/// binding, or one RG8 derivative for an admitted positive-Z normal map.
/// `exact_usage` is false unless every live native allocation exactly matches
/// the roles discovered from the currently published material graph.
struct OgreNextN1TextureAllocationAudit final {
  std::uint32_t version = 1U;
  std::uint32_t live_source_textures = 0U;
  std::uint32_t sampled_rgba_allocations = 0U;
  std::uint32_t roughness_r8_allocations = 0U;
  std::uint32_t metallic_r8_allocations = 0U;
  std::uint32_t normal_rg8_allocations = 0U;
  std::uint64_t native_allocation_creates = 0U;
  std::uint64_t native_allocation_destroys = 0U;
  std::uint64_t live_native_allocations = 0U;
  std::uint64_t retired_name_lookups = 0U;
  std::uint64_t retired_name_rejections = 0U;
  bool exact_usage = false;
};

/// First production adapter behind the renderer-neutral boundary.
///
/// Ogre headers and native objects are confined to the private implementation.
/// This class must only be built in the standalone Ogre-Next target; it must
/// never be linked into the OGRE 1.14 RoR executable.
class OgreNextN1Frontend final : public IRendererFrontend {
public:
  explicit OgreNextN1Frontend(OgreNextN1Configuration configuration);
  OgreNextN1Frontend(OgreNextN1Configuration configuration,
                     OgreNextNativeFeatureTier native_feature_tier);
  ~OgreNextN1Frontend() override;

  OgreNextN1Frontend(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend &operator=(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend(OgreNextN1Frontend &&) = delete;
  OgreNextN1Frontend &operator=(OgreNextN1Frontend &&) = delete;

  [[nodiscard]] FrontendCapabilityReport QueryCapabilities() const override;
  [[nodiscard]] OgreNextN1TextureAllocationAudit
  QueryTextureAllocationAudit() const noexcept;
  [[nodiscard]] OgreNextReflectionProbeAudit
  QueryReflectionProbeAudit() const noexcept;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextN1NormalUploadAudit
  QueryNormalUploadAudit() const noexcept;
  [[nodiscard]] OgreNextReflectionProbeCaptureEvidence
  QueryReflectionProbeCaptureEvidence() const;
  [[nodiscard]] OgreNextReflectionProbeNativeOwnershipEvidence
  QueryReflectionProbeNativeOwnershipEvidence() const noexcept;
#endif
  [[nodiscard]] OgreNextPssmShadowRuntimeAudit
  QueryDirectionalShadowAudit() const noexcept;
  [[nodiscard]] OgreNextHdrCompositorAudit
  QueryHdrCompositorAudit() const noexcept;
  [[nodiscard]] OgreNextN1PresentationAudit
  QueryPresentationAudit() const noexcept;
  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override;
  RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) override;
  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override;
  RenderOperationResult ReleaseResource(ResourceHandle resource) override;
  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override;
  [[nodiscard]] bool
  IsFrameComplete(std::uint64_t frame_id) const noexcept override;
  RenderOperationResult
  WaitForFrame(std::uint64_t frame_id,
               std::uint64_t timeout_nanoseconds) override;
  [[nodiscard]] NativeRenderInterop *GetNativeInterop() noexcept override;
  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
