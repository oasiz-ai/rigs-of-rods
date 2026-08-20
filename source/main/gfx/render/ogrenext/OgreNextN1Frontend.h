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
#include "OgreNextHdrSceneTopology.h"
#include "OgreNextHdrTemporalContract.h"
#include "OgreNextPssmShadowPolicy.h"
#include "OgreNextN1ParticleRuntime.h"
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

/// Version 4 adds the scene-free GUI-only presentation counters to
/// OgreNextN1PresentationAudit. OgreNextN1PresentationConfiguration shares this
/// constant and is unchanged in shape, so a version-3 producer of that struct
/// remains byte-compatible; only the emitted audit grew.
constexpr std::uint32_t kOgreNextN1PresentationContractVersion = 4U;
/// Version 4: per-frame scene counters (pbs/transmission/normal/emissive/
/// caster/receiver) are retained aggregates maintained O(changed) and
/// cross-checked against the snapshot-derived shadow plan every present,
/// and native_state_verifications advances per created/updated instance
/// plus a rotating re-verification window instead of once per instance
/// per present. Values and log fields are unchanged; only the derivation
/// and the verification growth rate differ.
constexpr std::uint32_t kOgreNextNativeLightingPassAuditVersion = 5U;
constexpr std::uint32_t kOgreNextRetainedSceneAuditVersion = 2U;

/// Rotating native re-verification budget for the retained scene: after the
/// per-frame diff, up to this many retained instances that were not created
/// or updated this frame are re-read from Ogre and compared against their
/// admitted descriptors. Every retained instance is therefore re-verified at
/// least once every ceil(N / window) presented frames; any mismatch fails the
/// present closed and tears the retained scene down to empty.
constexpr std::uint64_t kOgreNextRetainedVerifyWindow = 128U;

/// Lifecycle evidence for the instance_id-keyed retained native scene.
/// `last_*` fields describe the most recent completed present; cumulative
/// counters are monotonic for the frontend lifetime. `recovery_teardowns`
/// counts a full teardown forced by a failed present, or by retained
/// instances surviving the emptying asset synchronization (the next present
/// rebuilds from an empty native scene).
struct OgreNextRetainedSceneAudit final {
  std::uint32_t version = kOgreNextRetainedSceneAuditVersion;
  std::uint64_t generation = 0U;
  std::uint64_t frames_diffed = 0U;
  std::uint64_t retained_instances = 0U;
  std::uint64_t retained_lights = 0U;
  std::uint64_t bounds_entries = 0U;
  std::uint64_t created = 0U;
  std::uint64_t updated = 0U;
  std::uint64_t destroyed = 0U;
  std::uint64_t dynamic_updates = 0U;
  std::uint64_t verified = 0U;
  std::uint64_t last_created = 0U;
  std::uint64_t last_updated = 0U;
  std::uint64_t last_destroyed = 0U;
  std::uint64_t last_dynamic_updates = 0U;
  std::uint64_t last_verified = 0U;
  std::uint64_t verify_window = kOgreNextRetainedVerifyWindow;
  std::uint64_t verify_cursor = 0U;
  std::uint64_t recovery_teardowns = 0U;
  /// Retained-light teardowns performed at a generation reset because the
  /// final scene was retired rather than rendered, so no light-set diff ran.
  /// Expected at every retire-path generation boundary; distinct from
  /// recovery_teardowns, which now counts only surviving instances.
  std::uint64_t retired_light_teardowns = 0U;
  /// Per-phase CPU cost of the most recent present, for offline
  /// scene_dispatch attribution. Metadata-only; no GPU readback.
  std::uint64_t last_light_phase_microseconds = 0U;
  std::uint64_t last_instance_phase_microseconds = 0U;
  std::uint64_t last_cleanup_phase_microseconds = 0U;
};

constexpr std::uint32_t kOgreNextRenderBoundaryDegradeAuditVersion = 1U;

/// Named counters for the render-boundary severity invariant:
///
///   A per-frame validation may reject a frame or an object, but may not end
///   a session and may not permanently stop publication. Terminal is reserved
///   for load-time-unrecoverable state, or a rollback that demonstrably
///   failed. Every degrade increments a named counter -- a silent degrade
///   trades a crash for a wrong picture.
///
/// Each counter below is the observable half of one gate that used to end the
/// session and now degrades instead. They are monotonic for the frontend
/// lifetime. A degrade nobody can see is not a fix, so every one of these is
/// relayed to the combined heartbeat.
struct OgreNextN1RenderBoundaryDegradeAudit final {
  std::uint32_t version = kOgreNextRenderBoundaryDegradeAuditVersion;
  /// Frames that failed after the native frame executed but left no
  /// half-written HDR history, so the frontend stayed usable instead of
  /// latching a permanent fault.
  std::uint64_t post_submit_recoverable_failures = 0U;
  /// Frames presented with the HUD overlay hidden because the rate-capped HUD
  /// readback extent did not match the freshly re-normalized view extent --
  /// what a window resize produces for ~33 ms. Previously fatal.
  std::uint64_t hud_extent_mismatch_frames = 0U;
  /// Frames that shipped without their particle batch because the camera
  /// basis was not rigid-orthonormal within the calibrated bound. One frame
  /// without dust is invisible; ending the session mid-drive is not.
  std::uint64_t particle_basis_rejections = 0U;
  /// Frames whose PSSM shadow-camera pose was renormalized to the nearest
  /// rigid frame instead of failing on a decomposition noise floor that is
  /// documented to exceed the generic 1.0e-6 bound.
  std::uint64_t pssm_pose_renormalizations = 0U;
  /// Mesh instances skipped for this frame because their transform carries a
  /// non-uniform scale the pinned PBS tangent path cannot represent. The
  /// instance is dropped; the frame still renders.
  std::uint64_t non_uniform_scale_instance_rejections = 0U;
};

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
  /// The startup graph targets only the borrowed window and contains one native
  /// PASS_CLEAR. It has no scene pass, asset registry, snapshot, camera view,
  /// particle transaction, output lease, or frontend frame identity.
  bool bootstrap_clear_only = false;
  bool bootstrap_presented_before_scene = false;
  std::uint64_t window_moved_or_resized_calls = 0U;
  std::uint64_t show_callback_calls = 0U;
  std::uint64_t bootstrap_node_definition_creates = 0U;
  std::uint64_t bootstrap_node_definition_destroys = 0U;
  std::uint64_t bootstrap_workspace_creates = 0U;
  std::uint64_t bootstrap_workspace_destroys = 0U;
  std::uint64_t bootstrap_clear_passes = 0U;
  std::uint64_t bootstrap_render_one_frame_calls = 0U;
  std::uint64_t bootstrap_window_swap_completions = 0U;
  /// Scene-free GUI-only presentation (PresentUiOverlayFrame). Its graph is a
  /// disabled-by-default overlay-only workspace enabled for exactly one
  /// renderOneFrame() at a time, so ui_overlay_presented_frames is the number
  /// of menu/loading frames the presenter actually swapped. None of these
  /// advance presented_frames, first/last_presented_frame_id, or any scene
  /// identity: a GUI-only frame consumes no frontend frame ID.
  std::uint64_t ui_overlay_workspace_creates = 0U;
  std::uint64_t ui_overlay_workspace_destroys = 0U;
  std::uint64_t ui_overlay_image_creates = 0U;
  std::uint64_t ui_overlay_image_destroys = 0U;
  /// GPU uploads of GUI pixels. Strictly less than ui_overlay_presented_frames
  /// whenever the GUI was static across presents; equal counts mean the
  /// content hash changed every frame.
  std::uint64_t ui_overlay_image_uploads = 0U;
  std::uint64_t ui_overlay_presented_frames = 0U;
  std::uint64_t ui_overlay_render_one_frame_calls = 0U;
  std::uint32_t ui_overlay_last_width = 0U;
  std::uint32_t ui_overlay_last_height = 0U;
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
  /// Fires after a replacement texture with a changed sampling role has been
  /// uploaded and verified, while the previous-role allocation is still live.
  AFTER_ROLE_TRANSITION_CANDIDATE_TEXTURES,
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

/// Probe-only GPU readback proof for the exact display-domain texture role.
/// The audit compares every authored RGBA byte in every mip against a native
/// TextureGpu download after residency and after the asset transaction commits.
struct OgreNextN1DisplayDomainUploadAudit final {
  std::uint32_t version = 1U;
  std::uint64_t source_textures = 0U;
  std::uint64_t native_readbacks = 0U;
  std::uint64_t expected_mip_levels = 0U;
  std::uint64_t verified_mip_levels = 0U;
  std::uint64_t verified_rows = 0U;
  std::uint64_t verified_texels = 0U;
  std::uint64_t verified_rgba_bytes = 0U;
  bool exact_source_rgba_to_native_texture = false;
};

constexpr std::uint32_t kOgreNextHdrLightingSplitContentEvidenceVersion = 1U;

/// Test-artifact-only synchronous downloads of the four exact pre-tone-map
/// linear targets needed to validate the GPU SunDirect derivation. Production
/// cannot name this type or call the capture method.
struct OgreNextHdrLightingSplitContentEvidence final {
  std::uint32_t version =
      kOgreNextHdrLightingSplitContentEvidenceVersion;
  std::uint64_t frame_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::vector<std::uint16_t> base_hdr_rgba16;
  std::vector<std::uint16_t> sun_full_hdr_rgba16;
  std::vector<std::uint16_t> sun_direct_hdr_rgba16;
  std::vector<std::uint16_t> raster_lit_hdr_rgba16;
};

constexpr std::uint32_t
    kOgreNextSunVisibilityV2ContentEvidenceVersion = 2U;

/// Test-artifact-only download of the exact four-image V2 transaction after
/// its external completion and LitHdr continuation.
/// This type and its capture method do not exist in production builds. The
/// runtime path itself remains GPU-only and never reads any texture back while
/// preparing, publishing, tracing, or presenting a frame.
struct OgreNextSunVisibilityV2ContentEvidence final {
  std::uint32_t version =
      kOgreNextSunVisibilityV2ContentEvidenceVersion;
  std::uint64_t frame_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::vector<std::uint16_t> base_hdr_rgba16;
  std::vector<std::uint16_t> sun_direct_hdr_rgba16;
  std::vector<std::uint16_t> visibility_r16;
  std::vector<std::uint16_t> lit_hdr_rgba16;
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
  AFTER_SINGLE_SCENE_PSSM_DEFINITION,
  AFTER_SINGLE_SCENE_PSSM_WORKSPACE_RECREATE,
  /// Corrupts only the test audit counter after the real native absence
  /// baseline is captured. The warmup verifier must reject the drift instead
  /// of publishing its deferred/zero-light receipts.
  BEFORE_SINGLE_SCENE_WARMUP_ABSENCE_CHECK_COUNTER_DRIFT,
  /// Fires after native HDR history has been read and transactionally
  /// prepared, but before any public frame or audit state is committed.
  AFTER_FRAME_COMMIT_PREPARE,
};

/// Analytic-sky-only transactional fault seam for the native smoke. The
/// production target never compiles this enum or its injections.
enum class OgreNextN1AnalyticSkyFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_BACKGROUND_DATABLOCK,
  AFTER_SUN_DATABLOCK,
  AFTER_BACKGROUND_MESH,
  AFTER_BACKGROUND_CPU_VERTEX_ALLOCATION,
  AFTER_BACKGROUND_VERTEX_BUFFER,
  AFTER_BACKGROUND_CPU_INDEX_ALLOCATION,
  AFTER_BACKGROUND_INDEX_BUFFER,
  AFTER_BACKGROUND_VAO,
  AFTER_BACKGROUND_SUBMESH_ATTACH,
  AFTER_SUN_MESH,
  AFTER_SUN_CPU_VERTEX_ALLOCATION,
  AFTER_SUN_VERTEX_BUFFER,
  AFTER_SUN_CPU_INDEX_ALLOCATION,
  AFTER_SUN_INDEX_BUFFER,
  AFTER_SUN_VAO,
  AFTER_SUN_SUBMESH_ATTACH,
  AFTER_BACKGROUND_ITEM,
  AFTER_SUN_ITEM,
  AFTER_SCENE_NODE,
  AFTER_ATTACHED_STATE_VERIFICATION,
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
  OgreNextN1AnalyticSkyFailureStage analytic_sky_failure_stage =
      OgreNextN1AnalyticSkyFailureStage::NONE;
  /// Enables four synchronous immutable VB/IB byte reads per analytic-sky
  /// frame for the isolated native artifact only. Production does not compile
  /// a caller-visible switch and therefore remains zero-readback.
  bool retain_analytic_sky_geometry_content_evidence = false;
  /// Enables synchronous D32 capability and HDR luminance/history downloads
  /// for isolated native artifacts. Production does not compile a
  /// caller-visible switch and proves the same graph through metadata and GPU
  /// sequencing without content readbacks.
  bool retain_native_lighting_content_evidence = false;
  /// Enables an explicit post-continuation Visibility/LitHdr download in the
  /// isolated Metal V2 acceptance executable. It does not enable any
  /// production frame readback and is rejected for every other feature tier.
  bool retain_sun_visibility_v2_content_evidence = false;
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
  OgreNextHdrSceneTopology hdr_scene_topology =
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2;
  OgreNextHdrTemporalConfiguration hdr_temporal_configuration{};
  OgreNextN1PresentationConfiguration presentation{};
};

/// Exact observable state of the opt-in persistent HDR compositor. Counts
/// advance only after a finite-positive native R16 history sample has passed
/// the versioned conditioning/storage bound and the corresponding public frame
/// has been committed. Accepted native bits are authoritative.
struct OgreNextHdrCompositorAudit final {
  std::uint32_t version = 5U;
  OgreNextHdrSceneTopology scene_topology =
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2;
  bool enabled = false;
  bool native_workspace_live = false;
  bool deterministic_delta_bound = false;
  bool native_r16_history_validated = false;
  bool exact_current_to_old_copy_verified = false;
  /// Version 4: the production workspace terminates on the stock HdrRenderUi
  /// node so the transported menu/HUD composites post-tonemap. This proves
  /// the exact workspace closure including that UI node; the former
  /// `ui_free_workspace_verified` flag is retired with the UI-free topology.
  bool hud_workspace_verified = false;
  /// Version 5: the single-evaluation production workspace exports the
  /// scene's D32 opaque depth and inserts the aerial-haze quad between the
  /// scene node and the stock HDR post node.
  ///
  /// `opaque_depth_export_verified` proves RoROpaqueDepth was resolved as a
  /// D32 texture at the reviewed extent. `aerial_haze_workspace_verified`
  /// proves the haze node instance and its RGBA16F output were likewise
  /// resolved (both are vacuously true for DIRECTIONAL_SPLIT_V2 without
  /// sun-visibility V2, which has no such consumer).
  /// `aerial_haze_constants_bound` proves every named haze constant survived
  /// its `_readRawConstants` readback. `aerial_haze_applied` distinguishes a
  /// live atmosphere from the canonical identity binding: false means the
  /// pass ran as a bit-exact pass-through, never that it was skipped.
  bool opaque_depth_export_verified = false;
  bool aerial_haze_workspace_verified = false;
  bool aerial_haze_constants_bound = false;
  bool aerial_haze_applied = false;
  /// Frames presented with identity haze because the camera basis was not
  /// rigid and orthonormal. Zero in a healthy session.
  std::uint64_t aerial_haze_basis_rejections = 0U;
  /// The atmosphere the last bind actually carried, so a live run can prove
  /// the presenter consumed the producer's transported policy rather than
  /// re-deriving one. Zero while the identity binding is in force.
  float aerial_haze_extinction_per_meter = 0.0F;
  Float3 aerial_haze_inscatter{};
  OgreNextHdrHistoryValidationMode history_validation_mode =
      OgreNextHdrHistoryValidationMode::NONE;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t warmup_frames = 0U;
  std::uint64_t committed_frames = 0U;
  std::uint64_t pssm_finalization_attempts = 0U;
  std::uint64_t pssm_finalization_commits = 0U;
  std::uint64_t pssm_finalization_rollbacks = 0U;
  std::uint64_t pssm_warmup_native_absence_checks = 0U;
  bool pssm_deferred_until_scene_population = false;
  bool pssm_finalized_with_populated_scene = false;
  bool zero_light_pssm_warmup_avoided = false;
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

/// Transactionally published evidence for the native RT4 lighting and HDR
/// presentation path. Descriptor counts are copied only after Ogre's live
/// Light, SceneManager, PBS datablock, Compositor2, and shadow-node state has
/// been read back and the corresponding public frame has committed.
///
/// `production_*_readbacks` deliberately excludes the isolated native-smoke
/// artifact download path. A production GPU-only presentation must keep both
/// counters at zero for its complete frontend lifetime.
struct OgreNextNativeLightingPassAudit final {
  std::uint32_t version = kOgreNextNativeLightingPassAuditVersion;
  std::uint64_t completed_frames = 0U;
  std::uint64_t last_frame_id = 0U;
  std::uint64_t last_snapshot_id = 0U;
  std::uint64_t native_state_verifications = 0U;
  std::uint64_t production_content_readbacks = 0U;
  std::uint64_t production_framebuffer_readbacks = 0U;
  std::uint64_t test_artifact_content_readbacks = 0U;
  std::uint64_t test_artifact_framebuffer_readbacks = 0U;
  std::uint64_t ogre14_lighting_passes = 0U;
  std::uint32_t last_material_descriptor_version = 0U;
  std::uint32_t last_directional_lights = 0U;
  std::uint32_t last_pbs_items = 0U;
  std::uint32_t last_transmission_items = 0U;
  std::uint32_t last_normal_mapped_items = 0U;
  std::uint32_t last_emissive_items = 0U;
  std::uint32_t last_shadow_casters = 0U;
  std::uint32_t last_shadow_receivers = 0U;
  OgreNextDirectionalShadowMode shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  OgreNextHdrSceneTopology hdr_scene_topology =
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2;
  bool pssm_finalized_with_populated_scene = false;
  bool native_scene_lighting_pass = false;
  bool linear_rgba16_hdr_target = false;
  /// Three scene evaluations share one immutable scene/camera/material state:
  /// Base excludes only the tagged directional sun; SunFull includes that sun
  /// without PSSM; RasterLit includes it with PSSM.
  bool separate_base_hdr_target = false;
  bool separate_unoccluded_sun_full_hdr_target = false;
  bool separate_sun_direct_hdr_target = false;
  bool gpu_sun_direct_derivation = false;
  bool transactional_directional_sun_toggle = false;
  bool raster_lit_hdr_target = false;
  bool single_step_hdr_history = false;
  std::uint32_t raster_scene_evaluations = 0U;
  bool calibrated_directional_lighting = false;
  bool ambient_environment_lighting = false;
  bool analytic_sky_contribution = false;
  /// Version 5: a depth-based aerial-perspective pass extinguished the scene
  /// toward the analytic sky's exact horizon color before tone mapping. False
  /// means the frame bound the canonical identity (no sky, or the validated
  /// zero-extinction payload) and the pass was a bit-exact pass-through - it
  /// never means the pass was skipped.
  bool aerial_haze_applied = false;
  bool emissive_material_response = false;
  bool pssm_shadow_response = false;
  bool thin_parallel_slab_refraction = false;
  bool physical_snell_refraction = false;
  bool beer_lambert_attenuation = false;
  bool screen_space_radiance_lookup = false;
  std::uint32_t refraction_scene_evaluations = 0U;
  bool hdr_auto_exposure = false;
  bool gpu_hdr_history_sequenced = false;
  bool hdr_bloom = false;
  bool filmic_tone_map = false;
  bool srgb_presentation = false;
  bool production_gpu_only = false;
  bool no_ogre14_lighting = true;
};

/// Native evidence for the renderer-neutral analytic sky. One completed frame
/// owns two frontend-private v2 Mesh/Item sections on one camera-centred node:
/// a replace-gradient background and an RGB-additive, alpha-replacing sun.
/// These resources are internal renderer state and carry no portable scene
/// identity. Lifetime counters include aborted transactions so a native-smoke
/// fault seam can prove that every acquired resource was retired before retry.
struct OgreNextAnalyticSkyRuntimeAudit final {
  std::uint32_t version = 2U;
  std::uint32_t native_render_policy_version = 1U;
  std::uint64_t completed_frames = 0U;
  std::uint64_t native_mesh_creates = 0U;
  std::uint64_t native_mesh_destroys = 0U;
  std::uint64_t native_vertex_buffer_creates = 0U;
  std::uint64_t native_vertex_buffer_destroys = 0U;
  std::uint64_t native_index_buffer_creates = 0U;
  std::uint64_t native_index_buffer_destroys = 0U;
  std::uint64_t native_vao_creates = 0U;
  std::uint64_t native_vao_destroys = 0U;
  std::uint64_t native_item_creates = 0U;
  std::uint64_t native_item_destroys = 0U;
  std::uint64_t native_scene_node_creates = 0U;
  std::uint64_t native_scene_node_destroys = 0U;
  std::uint64_t native_datablock_creates = 0U;
  std::uint64_t native_datablock_destroys = 0U;
  std::uint64_t native_mesh_absence_checks = 0U;
  std::uint64_t native_item_absence_checks = 0U;
  std::uint64_t native_scene_node_absence_checks = 0U;
  std::uint64_t native_datablock_absence_checks = 0U;
  /// Exact immutable VB/IB byte reads enabled only by the isolated test seam.
  /// Production keeps this zero and uses the CPU digest plus native metadata.
  std::uint64_t native_gpu_content_readbacks = 0U;
  std::uint64_t native_state_verifications = 0U;
  std::uint64_t last_sun_light_id = 0U;
  std::uint32_t last_background_vertex_count = 0U;
  std::uint32_t last_background_index_count = 0U;
  std::uint32_t last_sun_vertex_count = 0U;
  std::uint32_t last_sun_index_count = 0U;
  std::uint64_t last_native_content_bytes = 0U;
  /// FNV-1a over the four exact CPU byte ranges handed to immutable buffers.
  std::uint64_t last_cpu_geometry_fnv1a64 = 0U;
  AnalyticSkyDescriptor last_descriptor;
  bool camera_centered = false;
  bool rendered_first = false;
  bool depth_check_disabled = false;
  bool depth_write_disabled = false;
  bool additive_sun_disk = false;
  bool separate_sun_alpha_replace = false;
  bool native_geometry_metadata_verified = false;
  bool exact_native_geometry_readback = false;
  bool casts_shadows = false;
  bool portable_scene_identity_absent = false;
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
  /// True when the retained observation set below is complete for the
  /// admitted scene and every entry was observed natively at admission or
  /// during the rotating re-verification window since.
  bool native_bounds_readback_verified = false;
  /// Cumulative native AABB readbacks that refreshed a retained observation
  /// entry (create, update, or rotating window), so evidence consumers can
  /// see the refresh rate behind the retained set.
  std::uint64_t bounds_observations_refreshed = 0U;
  std::vector<OgreNextPssmNativeBoundsObservation>
      last_native_bounds_observations;
};

/// Runtime audit of the native RT4/V1 texture variants owned by one frontend.
///
/// A source texture may require one sampled sRGB RGBA allocation (base colour
/// or emissive), one linear RGBA allocation for authored specular RGB, the two
/// R8 derivatives used by a packed metallic-roughness binding, or one RG8
/// derivative for an admitted positive-Z normal map.
/// `exact_usage` is false unless every live native allocation exactly matches
/// the roles discovered from the currently published material graph.
struct OgreNextN1TextureAllocationAudit final {
  std::uint32_t version = 2U;
  std::uint32_t live_source_textures = 0U;
  std::uint32_t sampled_rgba_allocations = 0U;
  std::uint32_t linear_rgba_allocations = 0U;
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

constexpr std::uint32_t kOgreNextN1PbsUv0AffineStateVersion = 1U;

/// Exact CPU-visible Ogre datablock receipt for one live RT4/V1 PBS material.
/// This reads native material metadata only; it performs no framebuffer or
/// texture-content readback. `native_texture_slot_readbacks` counts every
/// actual PBS slot, including both native slots lowered from one authored
/// metallic-roughness binding.
struct OgreNextN1PbsUv0AffineState final {
  std::uint32_t version = kOgreNextN1PbsUv0AffineStateVersion;
  RenderAssetReference material;
  Float2 scale{1.0F, 1.0F};
  Float2 offset;
  std::uint32_t portable_texture_binding_count = 0U;
  std::uint32_t native_texture_slot_count = 0U;
  std::uint32_t native_texture_slot_readbacks = 0U;
  std::uint32_t native_user_value_readbacks = 0U;
  bool live = false;
  bool pbs = false;
  bool transformed = false;
  bool uv0_only = false;
  bool positive_scale = false;
  bool rotation_zero = false;
  bool shared_across_bound_slots = false;
  bool shader_piece_selected = false;
  bool exact_native_state = false;
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
  [[nodiscard]] OgreNextN1PbsUv0AffineState
  QueryPbsUv0AffineState(RenderAssetReference material) const noexcept;
  [[nodiscard]] OgreNextReflectionProbeAudit
  QueryReflectionProbeAudit() const noexcept;
  [[nodiscard]] OgreNextN1ParticleRuntimeAudit
  QueryParticleRuntimeAudit() const noexcept;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextN1NormalUploadAudit
  QueryNormalUploadAudit() const noexcept;
  [[nodiscard]] OgreNextN1DisplayDomainUploadAudit
  QueryDisplayDomainUploadAudit() const;
  [[nodiscard]] OgreNextReflectionProbeCaptureEvidence
  QueryReflectionProbeCaptureEvidence() const;
  [[nodiscard]] OgreNextReflectionProbeNativeOwnershipEvidence
  QueryReflectionProbeNativeOwnershipEvidence() const noexcept;
  [[nodiscard]] OgreNextHdrLightingSplitContentEvidence
  CaptureHdrLightingSplitContentEvidence();
  [[nodiscard]] OgreNextSunVisibilityV2ContentEvidence
  CaptureSunVisibilityV2ContentEvidence();
#endif
  [[nodiscard]] OgreNextPssmShadowRuntimeAudit
  QueryDirectionalShadowAudit() const noexcept;
  [[nodiscard]] OgreNextHdrCompositorAudit
  QueryHdrCompositorAudit() const noexcept;
  [[nodiscard]] OgreNextNativeLightingPassAudit
  QueryNativeLightingPassAudit() const noexcept;
  [[nodiscard]] OgreNextRetainedSceneAudit
  QueryRetainedSceneAudit() const noexcept;
  [[nodiscard]] OgreNextN1RenderBoundaryDegradeAudit
  QueryRenderBoundaryDegradeAudit() const noexcept;
  [[nodiscard]] OgreNextN1PresentationAudit
  QueryPresentationAudit() const noexcept;
  [[nodiscard]] OgreNextAnalyticSkyRuntimeAudit
  QueryAnalyticSkyAudit() const noexcept;
  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override;
  RenderOperationResult PresentBootstrapFrame() override;
  RenderOperationResult
  PresentUiOverlayFrame(const UiOverlayFrameRequest &request) override;
  RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) override;
  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override;
  RenderOperationResult
  ResetSceneGeneration(std::uint64_t next_generation) override;
  RenderOperationResult ReleaseResource(ResourceHandle resource) override;
  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override;
  RenderOperationResult
  RetireFrameState(const RenderFrameRequest &request) override;
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
