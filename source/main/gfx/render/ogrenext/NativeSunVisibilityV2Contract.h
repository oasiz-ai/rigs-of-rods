/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral V2 contract for hardware-RT sun visibility.

#pragma once

#include "../RenderValidation.h"
#include "NativeDirectionalShadowContract.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kNativeSunVisibilityV2ContractVersion = 2U;
constexpr std::uint32_t kNativeSunVisibilityV2MaximumAdmittedInstances = 64U;
constexpr std::uint32_t kNativeSunVisibilityV2MaximumSelectedInstances = 256U;

/// Visibility is a bit-exact R16_FLOAT wire value. In particular, negative
/// zero (0x8000) is not interchangeable with the canonical occluded value.
[[nodiscard]] constexpr bool IsCanonicalNativeSunVisibilityV2R16(
    std::uint16_t bits) noexcept {
  return bits == kNativeDirectionalShadowOccludedR16 ||
         bits == kNativeDirectionalShadowVisibleR16;
}

enum class NativeSunVisibilityV2Stage : std::uint8_t {
  NONE = 0,
  CAPABILITY_GATE,
  SCENE_ADMISSION,
  GEOMETRY_EXPORT,
  IMAGE_EXPORT,
  TIMELINE_HANDOFF,
  ACCELERATION_STRUCTURE_BUILD,
  VISIBILITY_AND_COMPOSITE,
  EXTERNAL_COMPLETION,
  PRESENT_CONTINUATION,
  COMPLETE,
};

enum class NativeSunVisibilityV2Code : std::uint8_t {
  OK = 0,
  UNSUPPORTED,
  INVALID_ARGUMENT,
  RESOURCE_STALE,
  TIMEOUT,
  DEVICE_LOST,
  BACKEND_FAILURE,
};

struct NativeSunVisibilityV2Result final {
  std::uint32_t version = kNativeSunVisibilityV2ContractVersion;
  NativeSunVisibilityV2Code code = NativeSunVisibilityV2Code::OK;
  NativeSunVisibilityV2Stage stage = NativeSunVisibilityV2Stage::NONE;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  /// Stable detail token, not driver-provided prose. This survives rollback so
  /// telemetry can distinguish timeout, device loss, stale leases, and policy.
  std::string detail = "ok";
};

[[nodiscard]] bool ValidateNativeSunVisibilityV2Result(
    const NativeSunVisibilityV2Result &result) noexcept;

enum class NativeSunVisibilityV2LifecycleState : std::uint8_t {
  UNINITIALIZED = 0,
  READY,
  ENCODING,
  SUBMITTED,
  FAULTED,
};

/// Deterministic lifecycle oracle for backend implementations. Resize is
/// committed only with a completed frame. A pre-submit rollback returns to
/// READY while retaining the original failure stage/detail; a submitted
/// timeout/device loss remains faulted until native work is known complete.
class NativeSunVisibilityV2LifecycleTracker final {
public:
  [[nodiscard]] bool Initialize();
  [[nodiscard]] bool BeginFrame(std::uint64_t frame_id,
                                std::uint64_t snapshot_id,
                                std::uint32_t width,
                                std::uint32_t height);
  [[nodiscard]] bool MarkSubmitted();
  [[nodiscard]] bool Complete();
  [[nodiscard]] bool RollbackBeforeSubmission(
      const NativeSunVisibilityV2Result &failure);
  [[nodiscard]] bool ObserveSubmittedFault(
      const NativeSunVisibilityV2Result &failure);
  [[nodiscard]] bool Shutdown(bool native_work_complete);

  [[nodiscard]] NativeSunVisibilityV2LifecycleState state() const noexcept;
  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] std::uint64_t rollback_count() const noexcept;
  [[nodiscard]] const NativeSunVisibilityV2Result &last_result() const noexcept;

private:
  NativeSunVisibilityV2LifecycleState state_ =
      NativeSunVisibilityV2LifecycleState::UNINITIALIZED;
  std::uint64_t last_completed_frame_id_ = 0U;
  std::uint64_t pending_frame_id_ = 0U;
  std::uint64_t pending_snapshot_id_ = 0U;
  std::uint32_t pending_width_ = 0U;
  std::uint32_t pending_height_ = 0U;
  std::uint32_t width_ = 0U;
  std::uint32_t height_ = 0U;
  std::uint64_t rollback_count_ = 0U;
  NativeSunVisibilityV2Result last_result_;
};

/// These flags are deliberately separate from MeshInstanceFlag. The producer
/// must resolve material alpha/decal semantics before requesting RT admission;
/// the Metal backend never guesses them from a legacy material name.
enum NativeSunVisibilityV2InstanceFlag : std::uint32_t {
  NATIVE_SUN_VISIBILITY_V2_RECEIVER = 1U << 0U,
  NATIVE_SUN_VISIBILITY_V2_CASTER = 1U << 1U,
  NATIVE_SUN_VISIBILITY_V2_OPAQUE = 1U << 2U,
  NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER = 1U << 3U,
  NATIVE_SUN_VISIBILITY_V2_DECAL = 1U << 4U,
  NATIVE_SUN_VISIBILITY_V2_RT_INERT = 1U << 5U,
  /// Camera-specific admission assertion. A raster-visible opaque caster must
  /// also be a receiver so the primary RT hit names the rasterized surface,
  /// never geometry hidden behind it. General surface-ID MRT is deliberately
  /// deferred beyond this bounded first slice.
  NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE = 1U << 6U,
};

struct NativeSunVisibilityV2InstanceSelection final {
  std::uint64_t instance_id = 0U;
  /// Stable mesh identity used for BLAS sharing across instances and frames.
  std::uint64_t mesh_id = 0U;
  std::uint32_t flags = 0U;
};

struct NativeSunVisibilityV2ScenePlan final {
  std::uint32_t version = kNativeSunVisibilityV2ContractVersion;
  /// Deterministic FNV-1a digest of the complete ordered selection, including
  /// excluded alpha/decal/RT-inert entries. Transform state is intentionally
  /// separate so a moved-caster smoke can prove stable admission plus motion.
  std::uint64_t scene_plan_digest = 0U;
  std::vector<NativeSunVisibilityV2InstanceSelection> admitted_instances;
  std::uint32_t receiver_count = 0U;
  std::uint32_t caster_count = 0U;
  std::uint32_t excluded_alpha_layer_count = 0U;
  std::uint32_t excluded_decal_count = 0U;
  std::uint32_t excluded_rt_inert_count = 0U;
  std::uint32_t raster_visible_receiver_count = 0U;
  std::uint32_t raster_visible_caster_count = 0U;
  std::vector<std::uint64_t> unique_mesh_ids;
};

/// Builds a deterministic plan transactionally. Input identifiers must be
/// strictly increasing, making the TLAS order stable without a hidden sort.
/// Alpha layers, decals, and RT-inert instances are counted but never admitted.
[[nodiscard]] ValidationResult TryBuildNativeSunVisibilityV2ScenePlan(
    const std::vector<NativeSunVisibilityV2InstanceSelection> &selection,
    NativeSunVisibilityV2ScenePlan &output);

struct NativeSunVisibilityV2Capabilities final {
  std::uint32_t version = kNativeSunVisibilityV2ContractVersion;
  NativeDirectionalShadowBackend backend =
      NativeDirectionalShadowBackend::INVALID;
  bool supports_raytracing = false;
  bool apple_family_9 = false;
  bool same_ogre_device = false;
  bool same_ogre_queue = false;
  bool same_ogre_timeline = false;
  bool two_level_acceleration_structures = false;
  bool r16_float_visibility = false;
  bool separate_rgba16_base_and_sun_direct = false;
  bool rgba16_float_lit_composite = false;
  bool directional_self_hit_bias = false;
};

[[nodiscard]] bool HasAttestedNativeSunVisibilityV2Capabilities(
    const NativeSunVisibilityV2Capabilities &capabilities) noexcept;

/// Exact texture-layer semantics for one receiver pixel. BaseHdr contains
/// ambient, sky, emissive, and every non-sun-direct term. SunDirectHdr has
/// zero alpha. LitHdr is BaseHdr + visibility * SunDirectHdr in RGB and always
/// writes canonical opaque alpha. All channels are exact binary16 values.
struct NativeSunVisibilityV2Sample final {
  std::uint32_t version = kNativeSunVisibilityV2ContractVersion;
  NativeDirectionalShadowVisibility visibility =
      NativeDirectionalShadowVisibility::INVALID;
  std::uint16_t visibility_r16_bits = 0xffffU;
  std::uint64_t primary_hit_instance_id = 0U;
  std::uint64_t secondary_blocker_instance_id = 0U;
  bool primary_hit_is_receiver = false;
  bool primary_hit_is_caster = false;
  NativeDirectionalShadowRgba16Pixel base_hdr_rgba16;
  NativeDirectionalShadowRgba16Pixel sun_direct_hdr_rgba16;
  NativeDirectionalShadowRgba16Pixel lit_hdr_rgba16;
};

/// Builds the V2 sample oracle transactionally using the reviewed HDR
/// binary16 conversion. Failure leaves output unchanged.
[[nodiscard]] ValidationResult TryBuildNativeSunVisibilityV2SampleOracle(
    NativeDirectionalShadowVisibility visibility,
    const NativeDirectionalShadowRgba16Pixel &base_hdr_rgba16,
    const NativeDirectionalShadowRgba16Pixel &sun_direct_hdr_rgba16,
    NativeSunVisibilityV2Sample &output);

/// Auditable production counters. Content readbacks are forbidden; only the
/// isolated acceptance executable may retain image bytes behind its test seam.
struct NativeSunVisibilityV2FrameContract final {
  std::uint32_t version = kNativeSunVisibilityV2ContractVersion;
  NativeSunVisibilityV2Capabilities capabilities;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  /// Copied from the exact NativeSunVisibilityV2ScenePlan used by this frame.
  std::uint64_t scene_plan_digest = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t selected_instance_count = 0U;
  std::uint32_t admitted_instance_count = 0U;
  std::uint32_t receiver_count = 0U;
  std::uint32_t caster_count = 0U;
  std::uint32_t excluded_instance_count = 0U;
  std::uint32_t excluded_alpha_layer_count = 0U;
  std::uint32_t excluded_decal_count = 0U;
  std::uint32_t excluded_rt_inert_count = 0U;
  std::uint32_t raster_visible_receiver_count = 0U;
  std::uint32_t raster_visible_caster_count = 0U;
  std::uint32_t raster_visible_caster_receiver_count = 0U;
  std::uint32_t unique_mesh_count = 0U;
  std::uint32_t blas_build_count = 0U;
  std::uint32_t blas_cache_hit_count = 0U;
  std::uint32_t blas_refit_count = 0U;
  std::uint32_t tlas_build_count = 0U;
  std::uint32_t tlas_cache_hit_count = 0U;
  std::uint32_t tlas_refit_count = 0U;
  std::uint32_t tlas_instance_count = 0U;
  std::uint64_t blas_resident_bytes = 0U;
  std::uint64_t tlas_resident_bytes = 0U;
  std::uint64_t acceleration_structure_scratch_peak_bytes = 0U;
  std::uint64_t primary_ray_count = 0U;
  std::uint64_t secondary_sun_visibility_ray_count = 0U;
  std::uint64_t primary_miss_count = 0U;
  std::uint64_t visible_visibility_texel_count = 0U;
  std::uint64_t occluded_visibility_texel_count = 0U;
  std::uint64_t visibility_texel_count = 0U;
  std::uint64_t composite_pixel_count = 0U;
  std::uint64_t opaque_alpha_pixel_count = 0U;
  std::uint64_t acceleration_structure_encode_nanoseconds = 0U;
  std::uint64_t ray_composite_encode_nanoseconds = 0U;
  std::uint64_t gpu_execution_nanoseconds = 0U;
  float minimum_ray_distance_meters = 0.0F;
  float self_hit_origin_bias_multiplier = 0.0F;
  std::uint32_t production_cpu_content_readbacks = 0U;
  std::uint32_t production_gpu_content_readbacks = 0U;
  bool shader_lock_verified = false;
  bool base_hdr_preserved_under_occlusion = false;
  bool sun_direct_only_visibility_modulation = false;
  bool output_opaque_alpha = false;
  bool submission_completed = false;
  /// Byte-exact samples exist only in the isolated acceptance executable.
  /// Production frames leave this false and retain no image-content bytes.
  bool acceptance_samples_validated = false;
  /// Test-seam lineage for the receiver+caster gate named by samples 1 and 2.
  /// A moved-caster proof compares two contracts and requires the same scene
  /// plan and caster identity but a different nonzero transform revision.
  std::uint64_t acceptance_caster_instance_id = 0U;
  std::uint64_t acceptance_caster_transform_revision = 0U;
  NativeSunVisibilityV2Result result;
  /// Lit road, shadowed road, and visible receiver+caster surface.
  std::array<NativeSunVisibilityV2Sample, 3U> acceptance_samples{};
};

[[nodiscard]] ValidationResult ValidateNativeSunVisibilityV2FrameContract(
    const NativeSunVisibilityV2FrameContract &contract);

/// Stricter artifact predicates for the project-owned road/gate acceptance
/// scene. The reusable production validator above permits persistent BLAS
/// and TLAS cache hits/refits; the first frame must prove three fresh BLAS
/// builds and one fresh TLAS build, while the moved-gate pair must prove stable
/// admission, a changed caster transform revision, BLAS reuse, and one TLAS
/// refit. Neither predicate authorizes production content readback.
[[nodiscard]] ValidationResult
ValidateNativeSunVisibilityV2FirstFrameSmokeContract(
    const NativeSunVisibilityV2FrameContract &contract);
[[nodiscard]] ValidationResult
ValidateNativeSunVisibilityV2MovedCasterSmokeContract(
    const NativeSunVisibilityV2FrameContract &first_frame,
    const NativeSunVisibilityV2FrameContract &moved_frame);

} // namespace RoR::Render
