/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief One-process OgreNext window, frontend, and direct game-input owner.

#pragma once

#include "RendererGameInputTarget.h"
#include "RendererInProcessSession.h"

#include <cstdint>
#include <memory>
#include <string>

namespace RoR {

namespace Detail {

/// Allocation-free FIFO policy shared by the concrete SDL drain and its
/// deterministic contract test. Focus and native visibility are independent:
/// keyboard, text, and mouse events are admitted only after both transitions
/// have made the presentation window interactive.
class RendererOgreNextInProcessInputGate final {
public:
  void ObserveFocus(bool focused) noexcept { focused_ = focused; }
  void ObserveWindowSuppressed(bool suppressed) noexcept {
    window_suppressed_ = suppressed;
  }

  [[nodiscard]] bool focused() const noexcept { return focused_; }
  [[nodiscard]] bool window_suppressed() const noexcept {
    return window_suppressed_;
  }
  [[nodiscard]] bool AcceptsKeyboardTextMouse() const noexcept {
    return focused_ && !window_suppressed_;
  }
  [[nodiscard]] bool AcceptsPhysicalInput() const noexcept {
    return AcceptsKeyboardTextMouse();
  }

private:
  bool focused_ = false;
  bool window_suppressed_ = true;
};

} // namespace Detail

constexpr std::uint32_t
    kRendererOgreNextInProcessPresenterContractVersion = 3U;

enum class RendererOgreNextInProcessLightingMode : std::uint8_t {
  INVALID = 0U,
  RASTER_HDR_PSSM,
  METAL_RT_SUN_VISIBILITY_V2,
};

struct RendererOgreNextInProcessPresenterConfiguration final {
  std::uint32_t version =
      kRendererOgreNextInProcessPresenterContractVersion;
  std::string shader_media_root;
  std::string presentation_media_root;
  std::uint32_t logical_width = 1280U;
  std::uint32_t logical_height = 720U;
  /// Optional exact backing-pixel contract for isolated image/performance
  /// gates. Zero/zero keeps the ordinary HiDPI interactive behaviour. A
  /// nonzero pair must be acknowledged exactly before Ogre-Next frontend
  /// construction; there is no approximate or low-DPI fallback.
  std::uint32_t exact_drawable_width = 0U;
  std::uint32_t exact_drawable_height = 0U;
  /// Exact production graph. Ordinary joined gameplay currently selects the
  /// reviewed raster HDR/PSSM topology. The bounded project-owned native
  /// showcase may instead require the Apple-family-9 Metal V2 graph; that mode
  /// has no silent raster fallback after native initialization begins.
  RendererOgreNextInProcessLightingMode lighting_mode =
      RendererOgreNextInProcessLightingMode::INVALID;
};

/// Allocation-free admission used before creating SDL, Metal, or Ogre state.
/// The exact HDR/PSSM topology is mandatory for this combined presenter; all
/// default, partial, stale-version, or out-of-range configurations fail closed.
[[nodiscard]] inline bool
IsValidRendererOgreNextInProcessPresenterConfiguration(
    const RendererOgreNextInProcessPresenterConfiguration &configuration)
    noexcept {
  return configuration.version ==
             kRendererOgreNextInProcessPresenterContractVersion &&
         !configuration.shader_media_root.empty() &&
         !configuration.presentation_media_root.empty() &&
         configuration.logical_width > 0U &&
         configuration.logical_height > 0U &&
         configuration.logical_width <= 32768U &&
         configuration.logical_height <= 32768U &&
         ((configuration.exact_drawable_width == 0U &&
           configuration.exact_drawable_height == 0U) ||
          (configuration.exact_drawable_width > 0U &&
           configuration.exact_drawable_height > 0U &&
           configuration.exact_drawable_width <= 32768U &&
           configuration.exact_drawable_height <= 32768U)) &&
         (configuration.lighting_mode ==
              RendererOgreNextInProcessLightingMode::RASTER_HDR_PSSM ||
          configuration.lighting_mode ==
              RendererOgreNextInProcessLightingMode::
                  METAL_RT_SUN_VISIBILITY_V2);
}

enum class RendererOgreNextInProcessPresenterStatus : std::uint8_t {
  COMPLETED = 0U,
  REJECTED_CONFIGURATION,
  REJECTED_LIFECYCLE,
  FAILED_WINDOW_INITIALIZATION,
  FAILED_FRONTEND_CONFIGURATION,
  FAILED_INPUT_ACTIVATION,
  FAILED_WINDOW_SHUTDOWN,
  FAILED_ALLOCATION,
  FAILED_INTERNAL,
};

/// Renderer-neutral copy of the combined frontend's continuous-particle
/// lifetime counters. `native_state_readbacks` counts GPU/native texture
/// readbacks and must remain zero; `native_state_verifications` counts
/// read-only datablock/ownership checks performed after native construction.
struct RendererContinuousParticleAudit final {
  std::uint64_t committed_source_sequence = 0U;
  std::uint64_t create_commands = 0U;
  std::uint64_t update_commands = 0U;
  std::uint64_t stop_commands = 0U;
  std::uint64_t destroy_commands = 0U;
  std::uint64_t live_systems = 0U;
  std::uint64_t live_particles = 0U;
  std::uint64_t lifetime_max_live_systems = 0U;
  std::uint64_t lifetime_max_live_particles = 0U;
  std::uint64_t source_backed_textures = 0U;
  std::uint64_t source_alpha_textures = 0U;
  std::uint64_t lifetime_max_source_backed_textures = 0U;
  std::uint64_t lifetime_max_source_alpha_textures = 0U;
  std::uint64_t gpu_readbacks = 0U;
  std::uint64_t native_batch_creates = 0U;
  std::uint64_t native_batch_destroys = 0U;
  std::uint64_t native_particles_submitted = 0U;
  std::uint64_t native_state_readbacks = 0U;
  std::uint64_t native_state_verifications = 0U;
  bool available = false;
};

/// Renderer-neutral production receipt for the native analytic-sky slice.
/// Production construction cannot enable the isolated evidence seam, so
/// `native_gpu_content_readbacks` remains zero while native Item/VAO metadata
/// and render state are verified every completed sky frame.
struct RendererAnalyticSkyAudit final {
  std::uint64_t completed_frames = 0U;
  std::uint64_t sun_light_id = 0U;
  std::uint64_t cpu_geometry_fnv1a64 = 0U;
  std::uint64_t native_gpu_content_readbacks = 0U;
  std::uint64_t native_state_verifications = 0U;
  bool native_ownership_balanced = false;
  bool expected_per_frame_ownership = false;
  bool cpu_geometry_digest_verified = false;
  bool native_geometry_metadata_verified = false;
  bool production_gpu_readbacks_zero = false;
  bool exact_native_geometry_readback = false;
  bool separate_sun_alpha_replace = false;
  bool available = false;
};

/// Renderer-neutral, versioned production receipt for the native OgreNext RT4
/// PBS lighting, HDR, shadow, and presentation path.
struct RendererNativeLightingAudit final {
  std::uint32_t version = 0U;
  std::uint64_t completed_frames = 0U;
  std::uint64_t last_frame_id = 0U;
  std::uint64_t last_snapshot_id = 0U;
  std::uint64_t native_state_verifications = 0U;
  std::uint64_t production_content_readbacks = 0U;
  std::uint64_t production_framebuffer_readbacks = 0U;
  std::uint64_t ogre14_lighting_passes = 0U;
  std::uint32_t material_descriptor_version = 0U;
  std::uint32_t directional_lights = 0U;
  /// Stage 2 additive fields: native point/spot lights applied this frame
  /// and whether Forward+ clustered answered readback.
  std::uint32_t point_lights = 0U;
  std::uint32_t spot_lights = 0U;
  bool forward_clustered = false;
  std::uint32_t pbs_items = 0U;
  std::uint32_t transmission_items = 0U;
  std::uint32_t normal_mapped_items = 0U;
  std::uint32_t emissive_items = 0U;
  std::uint32_t shadow_casters = 0U;
  std::uint32_t shadow_receivers = 0U;
  std::uint32_t distance_lod_items = 0U;
  std::uint32_t distance_lod_reduced_items = 0U;
  std::uint32_t distance_lod_max_selected_level = 0U;
  std::uint64_t distance_lod_selected_level_sum = 0U;
  std::uint64_t base_triangles = 0U;
  std::uint64_t selected_triangles = 0U;
  bool exact_native_distance_lod_state = false;
  std::uint32_t hdr_scene_topology = 0U;
  std::uint32_t reflection_probe_audit_version = 0U;
  std::uint32_t reflection_live_probe_count = 0U;
  std::uint32_t reflection_completed_face_count = 0U;
  std::uint32_t reflection_completed_mip_count = 0U;
  std::uint32_t reflection_probe_resolution = 0U;
  std::uint32_t reflection_blend_resolution = 0U;
  std::uint64_t reflection_successful_capture_count = 0U;
  std::uint64_t reflection_failed_capture_count = 0U;
  std::uint64_t reflection_native_execution_evidence = 0U;
  std::uint64_t reflection_last_capture_frame_id = 0U;
  std::uint64_t reflection_last_capture_simulation_tick = 0U;
  /// Probe lifecycle completed because a generation's final scene was retired
  /// instead of rendered. This is the diagnostic that replaces the reset
  /// refusal the retire path used to hit.
  std::uint64_t reflection_scene_reset_retired_probe_count = 0U;
  std::uint32_t reflection_scene_reset_teardowns = 0U;
  bool reflection_initialized = false;
  bool reflection_exact_resources_loaded = false;
  bool reflection_pcc_enabled = false;
  bool reflection_pbs_bound = false;
  bool reflection_blend_texture_ready = false;
  bool reflection_ui_free_capture = false;
  bool reflection_reserved_render_queue_excluded = false;
  bool native_scene_lighting_pass = false;
  bool pssm_finalized_with_populated_scene = false;
  bool linear_rgba16_hdr_target = false;
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
  bool ambient_sh_bound = false;
  float ambient_sh_gain = 0.0F;
  float ambient_sh_band0_luminance = 0.0F;
  bool probe_sky_admission = false;
  bool analytic_sky_contribution = false;
  /// Aerial perspective (audit v5). `aerial_haze_applied` false means the pass
  /// ran as a bit-exact pass-through under the canonical identity binding, not
  /// that it was skipped: there is no present-without-haze fallback. The
  /// extinction and inscatter are the values the frontend actually bound, so a
  /// live run can prove the presenter consumed transported producer policy.
  bool aerial_haze_applied = false;
  bool aerial_haze_workspace_verified = false;
  bool aerial_haze_constants_bound = false;
  bool aerial_haze_depth_export_verified = false;
  float aerial_haze_extinction_per_meter = 0.0F;
  float aerial_haze_inscatter_r = 0.0F;
  float aerial_haze_inscatter_g = 0.0F;
  float aerial_haze_inscatter_b = 0.0F;
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
  bool no_ogre14_lighting = false;
  bool available = false;
};

/// Renderer-neutral copy of the combined frontend's retained-native-scene
/// lifecycle evidence. `last_*` fields describe the most recent completed
/// present; cumulative counters are monotonic for the frontend lifetime.
struct RendererRetainedSceneAudit final {
  std::uint32_t version = 0U;
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
  bool last_diff_used_retained_block_proof = false;
  std::uint64_t verify_window = 0U;
  std::uint64_t verify_cursor = 0U;
  std::uint64_t recovery_teardowns = 0U;
  std::uint64_t retired_light_teardowns = 0U;
  std::uint64_t last_validation_phase_microseconds = 0U;
  std::uint64_t last_frame_prepare_phase_microseconds = 0U;
  std::uint64_t last_light_phase_microseconds = 0U;
  std::uint64_t last_instance_phase_microseconds = 0U;
  std::uint64_t last_native_prepare_phase_microseconds = 0U;
  std::uint64_t last_native_render_phase_microseconds = 0U;
  std::uint64_t last_post_render_phase_microseconds = 0U;
  std::uint64_t last_cleanup_phase_microseconds = 0U;
  std::uint64_t last_publication_phase_microseconds = 0U;
  std::uint64_t last_native_renderer_frame_id = 0U;
  std::uint64_t last_native_frame_batches = 0U;
  std::uint64_t last_native_frame_draws = 0U;
  std::uint64_t last_native_frame_instances = 0U;
  std::uint64_t last_native_frame_faces = 0U;
  std::uint64_t last_native_frame_vertices = 0U;
  std::uint64_t last_native_pre_hdr_draws = 0U;
  std::uint64_t last_native_shadow_draws = 0U;
  std::uint64_t last_native_scene_draws = 0U;
  std::uint64_t last_native_hdr_post_draws = 0U;
  std::uint64_t last_native_after_hdr_draws = 0U;
  std::uint64_t last_native_shadow_instances = 0U;
  std::uint64_t last_native_scene_instances = 0U;
  std::uint64_t last_native_shadow_faces = 0U;
  std::uint64_t last_native_scene_faces = 0U;
  bool last_native_pass_metrics_exact = false;
  bool available = false;
};

/// Evidence for scene-free GUI-only presentation (the main menu and any other
/// application state that draws a GUI without a world). `presented_frames`
/// counts window swaps that carried only the GUI overlay; `image_uploads`
/// counts the GPU uploads those frames needed, so uploads far below presents
/// proves the content-hash gate is working. None of these advance any scene
/// identity - `scene_presented_frames` is relayed beside them so a reader can
/// see the two lineages are disjoint.
struct RendererUiOverlayPresentationAudit final {
  std::uint32_t version = 0U;
  std::uint64_t presented_frames = 0U;
  std::uint64_t render_one_frame_calls = 0U;
  std::uint64_t image_uploads = 0U;
  std::uint64_t image_creates = 0U;
  std::uint64_t image_destroys = 0U;
  std::uint64_t workspace_creates = 0U;
  std::uint64_t workspace_destroys = 0U;
  std::uint64_t scene_presented_frames = 0U;
  std::uint64_t bootstrap_clear_passes = 0U;
  std::uint32_t last_width = 0U;
  std::uint32_t last_height = 0U;
  bool available = false;
};

/// Renderer-neutral copy of the render-boundary degrade counters.
///
/// The invariant they exist to make observable: a per-frame validation may
/// reject a frame or an object, but may not end a session and may not
/// permanently stop publication. Every degrade increments a named counter --
/// a silent degrade trades a crash for a wrong picture. These are the names.
struct RendererRenderBoundaryDegradeAudit final {
  std::uint32_t version = 0U;
  std::uint64_t post_submit_recoverable_failures = 0U;
  std::uint64_t hud_extent_mismatch_frames = 0U;
  std::uint64_t particle_basis_rejections = 0U;
  std::uint64_t pssm_pose_renormalizations = 0U;
  std::uint64_t non_uniform_scale_instance_rejections = 0U;
  bool available = false;

  [[nodiscard]] std::uint64_t total() const noexcept {
    return post_submit_recoverable_failures + hud_extent_mismatch_frames +
           particle_basis_rejections + pssm_pose_renormalizations +
           non_uniform_scale_instance_rejections;
  }
};

/// Renderer-neutral receipt for the actual product-owned Metal V2 dispatch.
/// It is published only after the backend completed its same-device GPU work,
/// continued the LitHdr presentation, and the reusable V2 contract validated.
struct RendererNativeSunVisibilityV2Audit final {
  std::uint32_t version = 0U;
  std::uint64_t completed_frames = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::uint64_t scene_plan_digest = 0U;
  std::uint32_t selected_instances = 0U;
  std::uint32_t admitted_instances = 0U;
  std::uint32_t excluded_instances = 0U;
  std::uint32_t receivers = 0U;
  std::uint32_t casters = 0U;
  std::uint32_t unique_meshes = 0U;
  std::uint32_t blas_builds = 0U;
  std::uint32_t blas_cache_hits = 0U;
  std::uint32_t blas_refits = 0U;
  std::uint32_t tlas_builds = 0U;
  std::uint32_t tlas_cache_hits = 0U;
  std::uint32_t tlas_refits = 0U;
  std::uint64_t primary_rays = 0U;
  std::uint64_t sun_visibility_rays = 0U;
  std::uint64_t visible_texels = 0U;
  std::uint64_t occluded_texels = 0U;
  std::uint64_t gpu_execution_nanoseconds = 0U;
  std::uint32_t production_cpu_content_readbacks = 0U;
  std::uint32_t production_gpu_content_readbacks = 0U;
  bool supports_raytracing = false;
  bool apple_family_9 = false;
  bool same_ogre_device = false;
  bool same_ogre_queue = false;
  bool same_ogre_timeline = false;
  bool shader_lock_verified = false;
  bool sun_direct_only_visibility_modulation = false;
  bool submission_completed = false;
  bool available = false;
};

/// Owns the sole visible SDL/Metal presentation window and an uninitialized
/// OgreNext N1 frontend. The public boundary is renderer-neutral; the Pimpl
/// implementation is the only translation unit that includes OgreNext or SDL.
///
/// PrepareWindow() must run before the transitional Ogre14 resource host so
/// this object becomes SDL's process video owner. AttachInputTarget() runs
/// after RoR creates its InputEngine. RendererInProcessSession then initializes
/// and shuts down Frontend() while this object services PollEvents().
/// ShutdownEventPump() deliberately only quiesces polling: the caller must
/// first destroy the hidden Ogre14 window and only then call ShutdownWindow(),
/// which releases the visible native window and process-global SDL ownership.
class RendererOgreNextInProcessPresenter final
    : public IRendererInProcessEventPump {
public:
  RendererOgreNextInProcessPresenter();
  ~RendererOgreNextInProcessPresenter() override;

  RendererOgreNextInProcessPresenter(
      const RendererOgreNextInProcessPresenter &) = delete;
  RendererOgreNextInProcessPresenter &operator=(
      const RendererOgreNextInProcessPresenter &) = delete;
  RendererOgreNextInProcessPresenter(
      RendererOgreNextInProcessPresenter &&) = delete;
  RendererOgreNextInProcessPresenter &operator=(
      RendererOgreNextInProcessPresenter &&) = delete;

  [[nodiscard]] RendererOgreNextInProcessPresenterStatus PrepareWindow(
      const RendererOgreNextInProcessPresenterConfiguration &configuration)
      noexcept;
  /// The hidden resource window is never treated as an input or presentation
  /// owner. SHOWN/RESTORED events for it are forced back to hidden by the
  /// presenter's single SDL drain. Pass null to clear the protection.
  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  ProtectHiddenResourceWindow(void *sdl_window) noexcept;
  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  AttachInputTarget(IRendererGameInputTarget &target) noexcept;

  [[nodiscard]] Render::IRendererFrontend *Frontend() noexcept;
  [[nodiscard]] const Render::IRendererFrontend *Frontend() const noexcept;
  [[nodiscard]] Render::FrontendInitializationRequest
  InitialFrontendRequest() const noexcept;
  [[nodiscard]] Render::FrontendSurfaceUpdate CurrentSurface() const noexcept;
  [[nodiscard]] RendererContinuousParticleAudit
  ContinuousParticleAudit() const noexcept;
  [[nodiscard]] RendererAnalyticSkyAudit AnalyticSkyAudit() const noexcept;
  [[nodiscard]] RendererNativeLightingAudit
  NativeLightingAudit() const noexcept;
  [[nodiscard]] RendererRetainedSceneAudit
  RetainedSceneAudit() const noexcept;
  [[nodiscard]] RendererRenderBoundaryDegradeAudit
  RenderBoundaryDegradeAudit() const noexcept;
  [[nodiscard]] RendererNativeSunVisibilityV2Audit
  NativeSunVisibilityV2Audit() const noexcept;
  [[nodiscard]] RendererUiOverlayPresentationAudit
  UiOverlayPresentationAudit() const noexcept;

  [[nodiscard]] Render::ValidationResult PollEvents(
      RendererInProcessEventPollPoint point,
      RendererInProcessEventObservation &observation) override;
  void ShutdownEventPump() noexcept override;

  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  ShutdownWindow() noexcept;
  [[nodiscard]] bool prepared() const noexcept;
  [[nodiscard]] bool input_attached() const noexcept;
  [[nodiscard]] bool quiesced() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool IsKnownRendererOgreNextInProcessPresenterStatus(
    RendererOgreNextInProcessPresenterStatus status) noexcept;
[[nodiscard]] const char *ToString(
    RendererOgreNextInProcessPresenterStatus status) noexcept;

} // namespace RoR
