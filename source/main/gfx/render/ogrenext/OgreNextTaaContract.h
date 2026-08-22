/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral temporal anti-aliasing contract and CPU oracle.

#pragma once

#include "../RenderFrame.h"

#include <array>
#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kOgreNextTaaContractVersion = 1U;
constexpr std::uint32_t kOgreNextTaaJitterPhaseCount = 8U;
constexpr const char kOgreNextTaaMotionVectorConvention[] =
    "previous_pixel_minus_current_pixel_jitter_removed_v1";
constexpr const char kOgreNextTaaDepthConvention[] =
    "portable_ndc_zero_to_one_non_reversed_v1";
constexpr const char kOgreNextTaaColourConvention[] =
    "pre_exposed_linear_rgba16f_alpha_one_v1";
constexpr const char kOgreNextTaaOracleArithmetic[] =
    "ordered_binary32_no_contraction_v1";
constexpr float kOgreNextTaaCurrentRawHdrPreExposure = 1.0F;

enum class OgreNextTaaHistoryResetReason : std::uint8_t {
  NONE = 0,
  INITIAL_FRAME,
  CAMERA_CUT,
  VIEW_CHANGED,
  EXTENT_CHANGED,
  EXPOSURE_DISCONTINUITY,
  EXPLICIT_INVALIDATION,
};

struct OgreNextTaaConfiguration final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  /// Frontend-owned, process-lifetime epoch. A renderer/device lifetime must
  /// never reuse this value, including after Reset and reinitialization.
  std::uint64_t lifecycle_epoch = 0U;
  float history_weight = 0.90F;
  float variance_clip_gamma = 1.0F;
  float disocclusion_absolute_depth = 0.001F;
  float disocclusion_relative_depth = 0.02F;
  float full_motion_rejection_pixels = 32.0F;
  float maximum_exposure_ratio = 16.0F;
};

/// Renderer-neutral inputs needed before a native TAA pass is constructed.
/// The frontend owns the native textures; this contract owns only lineage and
/// deterministic sampling policy.
struct OgreNextTaaFrameInput final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  std::uint64_t lifecycle_epoch = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  /// Exact portable camera state submitted to the renderer. Its jitter must
  /// equal the deterministic phase for frame_id; projections remain
  /// unjittered and current/previous matrices stay independently visible.
  CameraViewRequest view{};
  /// Extent of the resolved temporal result when it differs from the extent
  /// the scene is rasterized at. Zero means "identical to the view", which is
  /// the case for every in-engine resolve. A temporal upscaler resolves the
  /// jittered lower-resolution scene into a larger target, so its history
  /// images legitimately live at this extent while every per-pixel scene
  /// input stays at the view extent. It must never be SMALLER than the view.
  std::uint32_t resolve_width = 0U;
  std::uint32_t resolve_height = 0U;
  /// Scale already applied to the linear HDR scene colour for this frame.
  /// This may differ from CameraViewRequest::exposure because the HDR
  /// compositor's temporal auto-exposure owns pre-exposure history. The
  /// current Ogre-Next split writes raw scene-referred HDR, so integration
  /// supplies exactly kOgreNextTaaCurrentRawHdrPreExposure for both frames;
  /// oldLumRt is not a TAA pre-exposure scalar.
  float pre_exposure = 1.0F;
  bool camera_cut = false;
};

struct OgreNextTaaFramePlan final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  std::uint64_t lifecycle_epoch = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  /// Extent of the resolved temporal result. Equals width/height unless a
  /// temporal upscaler resolves into a larger target.
  std::uint32_t resolve_width = 0U;
  std::uint32_t resolve_height = 0U;
  CameraViewRequest view{};
  std::uint64_t camera_lineage_fnv1a64 = 0U;
  std::uint32_t jitter_phase = 0U;
  Float2 jitter_pixels{};
  Float2 previous_jitter_pixels{};
  float current_pre_exposure = 1.0F;
  float previous_pre_exposure = 1.0F;
  float history_exposure_ratio = 1.0F;
  std::uint64_t source_history_generation = 0U;
  std::uint64_t destination_history_generation = 0U;
  std::uint8_t source_history_slot = 0U;
  std::uint8_t destination_history_slot = 0U;
  OgreNextTaaHistoryResetReason reset_reason =
      OgreNextTaaHistoryResetReason::INITIAL_FRAME;
  bool history_available = false;
};

struct OgreNextTaaImageBinding final {
  /// Native allocation identity and allocation generation. The generation is
  /// stable across content writes and changes only when the image is retired
  /// and replaced.
  std::uint64_t native_identity = 0U;
  std::uint64_t generation = 0U;
  PixelFormat format = PixelFormat::INVALID;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
};

/// Native metadata proving that one pass executed against the planned frame.
/// These fields deliberately exclude content bytes. Production evidence is
/// GPU-only and must never force a texture or framebuffer readback.
struct OgreNextTaaExecutionEvidence final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  std::uint64_t lifecycle_epoch = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::uint64_t camera_lineage_fnv1a64 = 0U;
  OgreNextTaaImageBinding current_colour{};
  OgreNextTaaImageBinding current_depth{};
  OgreNextTaaImageBinding motion_vectors{};
  OgreNextTaaImageBinding reactive_mask{};
  OgreNextTaaImageBinding history_source{};
  OgreNextTaaImageBinding history_destination{};
  std::uint32_t prepare_count = 0U;
  std::uint32_t execute_count = 0U;
  std::uint32_t history_read_count = 0U;
  std::uint32_t history_write_count = 0U;
  std::uint32_t history_advance_count = 0U;
  std::uint32_t jitter_application_count = 0U;
  std::uint32_t native_state_verification_count = 0U;
  std::uint64_t production_content_readback_count = 0U;
  std::uint64_t production_framebuffer_readback_count = 0U;
  bool unjittered_culling = false;
  bool motion_vectors_remove_jitter = false;
  bool current_previous_transform_lineage = false;
  bool non_reversed_depth_reprojection = false;
  bool pre_exposure_history_rescale = false;
  bool reactive_mask_consumed = false;
  bool variance_neighbourhood_clipping = false;
  bool output_alpha_one = false;
};

struct OgreNextTaaPixelInput final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  /// Row-major 3x3 neighbourhood; element four is the current pixel.
  std::array<Float4, 9U> current_neighbourhood{};
  Float4 history_colour{};
  float current_depth = 1.0F;
  float reprojected_previous_depth = 1.0F;
  Float2 motion_pixels{};
  float reactive_mask = 0.0F;
  float current_pre_exposure = 1.0F;
  float previous_pre_exposure = 1.0F;
  bool history_available = false;
};

struct OgreNextTaaPixelResult final {
  std::uint32_t version = kOgreNextTaaContractVersion;
  Float4 colour{};
  float history_weight = 0.0F;
  float history_exposure_ratio = 1.0F;
  bool depth_rejected = false;
  bool motion_rejected = false;
  bool reactive_rejected = false;
  bool exposure_rejected = false;
  bool history_clipped = false;
};

/// Evaluates the deterministic scalar reference for one TAA pixel. Every
/// shader-facing arithmetic step is explicitly rounded to ordered binary32;
/// participating targets compile without fast-math or contraction. It is an
/// oracle for native shader tests, not a production image implementation.
[[nodiscard]] ValidationResult
EvaluateOgreNextTaaPixel(const OgreNextTaaConfiguration &configuration,
                         const OgreNextTaaPixelInput &input,
                         OgreNextTaaPixelResult &output);

/// Returns the exact centered Halton(2,3) output-pixel jitter for a nonzero
/// frontend frame ID. Failure leaves output unchanged.
[[nodiscard]] ValidationResult
ComputeOgreNextTaaJitterPixels(std::uint64_t frame_id, Float2 &output);

/// Domain-separated FNV-1a-64 over one validated portable camera request,
/// including current/previous matrices and the exact temporal jitter. Failure
/// leaves output unchanged. This is a lineage identity, not a security digest.
[[nodiscard]] ValidationResult
ComputeOgreNextTaaCameraLineage(const CameraViewRequest &view,
                                std::uint64_t &output);

/// Persistent two-phase lineage for exactly one temporal history stream.
class OgreNextTaaState final {
public:
  [[nodiscard]] ValidationResult
  Initialize(const OgreNextTaaConfiguration &configuration);

  [[nodiscard]] ValidationResult
  PrepareFrame(const OgreNextTaaFrameInput &input,
               OgreNextTaaFramePlan &output) const;

  [[nodiscard]] ValidationResult
  PrepareCommit(const OgreNextTaaFramePlan &plan,
                const OgreNextTaaExecutionEvidence &evidence);

  [[nodiscard]] bool CanCommitPrepared() const noexcept;
  void CommitPrepared() noexcept;
  void AbortPrepared() noexcept;

  /// Invalidates history without consuming a frame ID or replacing its two
  /// allocations. Use this for suspend/restore or another temporal break in
  /// the same frontend lifetime. Device/context retirement requires Reset and
  /// reinitialization with a new lifecycle epoch.
  [[nodiscard]] ValidationResult InvalidateHistory();

  void Reset() noexcept;

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] std::uint64_t lifecycle_epoch() const noexcept {
    return lifecycle_epoch_;
  }
  [[nodiscard]] bool history_valid() const noexcept { return history_valid_; }
  [[nodiscard]] std::uint64_t committed_frame_id() const noexcept {
    return committed_frame_id_;
  }
  [[nodiscard]] std::uint64_t history_generation() const noexcept {
    return history_generation_;
  }
  [[nodiscard]] OgreNextTaaFramePlan committed_plan() const noexcept {
    return committed_plan_;
  }
  [[nodiscard]] OgreNextTaaExecutionEvidence
  last_execution_evidence() const noexcept {
    return last_execution_evidence_;
  }
  [[nodiscard]] OgreNextTaaImageBinding
  committed_history_binding(std::uint8_t slot) const noexcept {
    return slot < committed_history_bindings_.size()
               ? committed_history_bindings_[slot]
               : OgreNextTaaImageBinding{};
  }

private:
  void ClearPending() noexcept;

  OgreNextTaaConfiguration configuration_{};
  std::uint64_t lifecycle_epoch_ = 0U;
  /// Preserved by Reset so the same state object cannot admit a retired epoch.
  std::uint64_t last_lifecycle_epoch_ = 0U;
  OgreNextTaaFramePlan committed_plan_{};
  OgreNextTaaExecutionEvidence last_execution_evidence_{};
  std::uint64_t committed_frame_id_ = 0U;
  std::uint64_t committed_snapshot_id_ = 0U;
  std::uint64_t committed_view_id_ = 0U;
  std::uint64_t history_generation_ = 0U;
  std::uint32_t committed_width_ = 0U;
  std::uint32_t committed_height_ = 0U;
  Float2 committed_jitter_pixels_{};
  std::array<OgreNextTaaImageBinding, 2U> committed_history_bindings_{};
  float committed_pre_exposure_ = 1.0F;
  bool history_valid_ = false;
  bool explicitly_invalidated_ = false;

  OgreNextTaaFramePlan pending_plan_{};
  OgreNextTaaExecutionEvidence pending_execution_evidence_{};
  std::uint64_t pending_base_frame_id_ = 0U;
  std::uint64_t pending_base_snapshot_id_ = 0U;
  std::uint64_t pending_base_history_generation_ = 0U;
  std::uint64_t pending_base_view_id_ = 0U;
  std::uint32_t pending_base_width_ = 0U;
  std::uint32_t pending_base_height_ = 0U;
  Float2 pending_base_jitter_pixels_{};
  std::array<OgreNextTaaImageBinding, 2U> pending_base_history_bindings_{};
  std::array<OgreNextTaaImageBinding, 2U> pending_history_bindings_{};
  float pending_base_pre_exposure_ = 1.0F;
  bool pending_base_history_valid_ = false;
  bool pending_base_explicitly_invalidated_ = false;
  bool commit_prepared_ = false;
  bool initialized_ = false;
};

} // namespace RoR::Render
