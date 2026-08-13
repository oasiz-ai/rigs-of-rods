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
    kRendererOgreNextInProcessPresenterContractVersion = 1U;

struct RendererOgreNextInProcessPresenterConfiguration final {
  std::uint32_t version =
      kRendererOgreNextInProcessPresenterContractVersion;
  std::string shader_media_root;
  std::string presentation_media_root;
  std::uint32_t logical_width = 1280U;
  std::uint32_t logical_height = 720U;
  /// The forward-native A0 preview keeps its authored shadow sun and selects
  /// the already-validated raster PSSM path. The ordinary combined runtime
  /// retains the persistent HDR split while its shadow composition remains a
  /// named pending milestone.
  bool enable_native_showcase_pssm_preview = false;
};

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
  std::uint32_t pbs_items = 0U;
  std::uint32_t normal_mapped_items = 0U;
  std::uint32_t emissive_items = 0U;
  std::uint32_t shadow_casters = 0U;
  std::uint32_t shadow_receivers = 0U;
  bool native_scene_lighting_pass = false;
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
  bool analytic_sky_contribution = false;
  bool emissive_material_response = false;
  bool pssm_shadow_response = false;
  bool hdr_auto_exposure = false;
  bool gpu_hdr_history_sequenced = false;
  bool hdr_bloom = false;
  bool filmic_tone_map = false;
  bool srgb_presentation = false;
  bool production_gpu_only = false;
  bool no_ogre14_lighting = false;
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
