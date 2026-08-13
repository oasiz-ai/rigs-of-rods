/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeSunVisibilityV2Contract.h"

#include "../HdrReference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace RoR::Render {
namespace {

ValidationResult Invalid(ValidationCode code, const char *field,
                         const char *detail, std::size_t index = 0U) {
  return ValidationResult::Failure(code, field, detail, index);
}

constexpr std::uint32_t kKnownFlags =
    NATIVE_SUN_VISIBILITY_V2_RECEIVER |
    NATIVE_SUN_VISIBILITY_V2_CASTER |
    NATIVE_SUN_VISIBILITY_V2_OPAQUE |
    NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER |
    NATIVE_SUN_VISIBILITY_V2_DECAL |
    NATIVE_SUN_VISIBILITY_V2_RT_INERT |
    NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE;

constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

void HashLittleEndian(std::uint64_t value, std::size_t byte_count,
                      std::uint64_t &digest) noexcept {
  for (std::size_t byte = 0U; byte < byte_count; ++byte) {
    digest ^= value & 0xffU;
    digest *= kFnv1a64Prime;
    value >>= 8U;
  }
}

bool IsKnownStage(NativeSunVisibilityV2Stage stage) noexcept {
  switch (stage) {
  case NativeSunVisibilityV2Stage::NONE:
  case NativeSunVisibilityV2Stage::CAPABILITY_GATE:
  case NativeSunVisibilityV2Stage::SCENE_ADMISSION:
  case NativeSunVisibilityV2Stage::GEOMETRY_EXPORT:
  case NativeSunVisibilityV2Stage::IMAGE_EXPORT:
  case NativeSunVisibilityV2Stage::TIMELINE_HANDOFF:
  case NativeSunVisibilityV2Stage::ACCELERATION_STRUCTURE_BUILD:
  case NativeSunVisibilityV2Stage::VISIBILITY_AND_COMPOSITE:
  case NativeSunVisibilityV2Stage::EXTERNAL_COMPLETION:
  case NativeSunVisibilityV2Stage::PRESENT_CONTINUATION:
  case NativeSunVisibilityV2Stage::COMPLETE:
    return true;
  }
  return false;
}

bool IsKnownCode(NativeSunVisibilityV2Code code) noexcept {
  switch (code) {
  case NativeSunVisibilityV2Code::OK:
  case NativeSunVisibilityV2Code::UNSUPPORTED:
  case NativeSunVisibilityV2Code::INVALID_ARGUMENT:
  case NativeSunVisibilityV2Code::RESOURCE_STALE:
  case NativeSunVisibilityV2Code::TIMEOUT:
  case NativeSunVisibilityV2Code::DEVICE_LOST:
  case NativeSunVisibilityV2Code::BACKEND_FAILURE:
    return true;
  }
  return false;
}

bool IsStableDetailToken(const std::string &detail) noexcept {
  if (detail.empty() || detail.size() > 64U) {
    return false;
  }
  for (const char value : detail) {
    const bool lower = value >= 'a' && value <= 'z';
    const bool digit = value >= '0' && value <= '9';
    if (!lower && !digit && value != '-') {
      return false;
    }
  }
  return detail.front() != '-' && detail.back() != '-';
}

bool PixelEqual(const NativeDirectionalShadowRgba16Pixel &lhs,
                const NativeDirectionalShadowRgba16Pixel &rhs) noexcept {
  return std::equal(lhs.channels.begin(), lhs.channels.end(),
                    rhs.channels.begin());
}

ValidationResult DecodeNonnegativeRgb(
    const NativeDirectionalShadowRgba16Pixel &pixel, const char *field,
    std::array<float, 3U> &decoded) {
  for (std::size_t channel = 0U; channel < decoded.size(); ++channel) {
    HdrR16Float value;
    const ValidationResult result =
        DecodeFiniteHdrR16Float(pixel.channels[channel], value);
    if (!result || value.decoded < 0.0F) {
      return Invalid(result ? ValidationCode::VALUE_OUT_OF_RANGE
                            : ValidationCode::NON_FINITE_VALUE,
                     field,
                     "sun-visibility HDR RGB must be finite and nonnegative",
                     channel);
    }
    decoded[channel] = value.decoded;
  }
  return ValidationResult::Success();
}

} // namespace

bool ValidateNativeSunVisibilityV2Result(
    const NativeSunVisibilityV2Result &result) noexcept {
  if (result.version != kNativeSunVisibilityV2ContractVersion ||
      !IsKnownCode(result.code) || !IsKnownStage(result.stage) ||
      !IsStableDetailToken(result.detail) || result.frame_id == 0U ||
      result.snapshot_id == 0U) {
    return false;
  }
  if (result.code == NativeSunVisibilityV2Code::OK) {
    return result.stage != NativeSunVisibilityV2Stage::NONE &&
           result.detail == "ok";
  }
  if (result.stage == NativeSunVisibilityV2Stage::NONE ||
      result.stage == NativeSunVisibilityV2Stage::COMPLETE ||
      result.detail == "ok") {
    return false;
  }
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::Initialize() {
  if (state_ != NativeSunVisibilityV2LifecycleState::UNINITIALIZED) {
    return false;
  }
  pending_frame_id_ = 0U;
  pending_snapshot_id_ = 0U;
  last_completed_frame_id_ = 0U;
  pending_width_ = 0U;
  pending_height_ = 0U;
  width_ = 0U;
  height_ = 0U;
  rollback_count_ = 0U;
  last_result_ = {};
  state_ = NativeSunVisibilityV2LifecycleState::READY;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::BeginFrame(
    std::uint64_t frame_id, std::uint64_t snapshot_id, std::uint32_t width,
    std::uint32_t height) {
  if (state_ != NativeSunVisibilityV2LifecycleState::READY || frame_id == 0U ||
      frame_id <= last_completed_frame_id_ || snapshot_id == 0U ||
      width == 0U || height == 0U) {
    return false;
  }
  pending_frame_id_ = frame_id;
  pending_snapshot_id_ = snapshot_id;
  pending_width_ = width;
  pending_height_ = height;
  state_ = NativeSunVisibilityV2LifecycleState::ENCODING;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::MarkSubmitted() {
  if (state_ != NativeSunVisibilityV2LifecycleState::ENCODING) {
    return false;
  }
  state_ = NativeSunVisibilityV2LifecycleState::SUBMITTED;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::Complete() {
  if (state_ != NativeSunVisibilityV2LifecycleState::SUBMITTED) {
    return false;
  }
  width_ = pending_width_;
  height_ = pending_height_;
  last_result_ = {};
  last_result_.stage = NativeSunVisibilityV2Stage::COMPLETE;
  last_result_.frame_id = pending_frame_id_;
  last_result_.snapshot_id = pending_snapshot_id_;
  last_completed_frame_id_ = pending_frame_id_;
  pending_frame_id_ = 0U;
  pending_snapshot_id_ = 0U;
  pending_width_ = 0U;
  pending_height_ = 0U;
  state_ = NativeSunVisibilityV2LifecycleState::READY;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::RollbackBeforeSubmission(
    const NativeSunVisibilityV2Result &failure) {
  if (state_ != NativeSunVisibilityV2LifecycleState::ENCODING ||
      !ValidateNativeSunVisibilityV2Result(failure) ||
      failure.code == NativeSunVisibilityV2Code::OK ||
      failure.frame_id != pending_frame_id_ ||
      failure.snapshot_id != pending_snapshot_id_) {
    return false;
  }
  last_result_ = failure;
  pending_frame_id_ = 0U;
  pending_snapshot_id_ = 0U;
  pending_width_ = 0U;
  pending_height_ = 0U;
  ++rollback_count_;
  state_ = NativeSunVisibilityV2LifecycleState::READY;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::ObserveSubmittedFault(
    const NativeSunVisibilityV2Result &failure) {
  if (state_ != NativeSunVisibilityV2LifecycleState::SUBMITTED ||
      !ValidateNativeSunVisibilityV2Result(failure) ||
      (failure.code != NativeSunVisibilityV2Code::TIMEOUT &&
       failure.code != NativeSunVisibilityV2Code::DEVICE_LOST) ||
      failure.frame_id != pending_frame_id_ ||
      failure.snapshot_id != pending_snapshot_id_) {
    return false;
  }
  last_result_ = failure;
  state_ = NativeSunVisibilityV2LifecycleState::FAULTED;
  return true;
}

bool NativeSunVisibilityV2LifecycleTracker::Shutdown(
    bool native_work_complete) {
  if (state_ == NativeSunVisibilityV2LifecycleState::UNINITIALIZED ||
      state_ == NativeSunVisibilityV2LifecycleState::ENCODING ||
      state_ == NativeSunVisibilityV2LifecycleState::SUBMITTED ||
      (state_ == NativeSunVisibilityV2LifecycleState::FAULTED &&
       !native_work_complete)) {
    return false;
  }
  pending_frame_id_ = 0U;
  pending_snapshot_id_ = 0U;
  pending_width_ = 0U;
  pending_height_ = 0U;
  state_ = NativeSunVisibilityV2LifecycleState::UNINITIALIZED;
  return true;
}

NativeSunVisibilityV2LifecycleState
NativeSunVisibilityV2LifecycleTracker::state() const noexcept {
  return state_;
}

std::uint32_t NativeSunVisibilityV2LifecycleTracker::width() const noexcept {
  return width_;
}

std::uint32_t NativeSunVisibilityV2LifecycleTracker::height() const noexcept {
  return height_;
}

std::uint64_t
NativeSunVisibilityV2LifecycleTracker::rollback_count() const noexcept {
  return rollback_count_;
}

const NativeSunVisibilityV2Result &
NativeSunVisibilityV2LifecycleTracker::last_result() const noexcept {
  return last_result_;
}

ValidationResult TryBuildNativeSunVisibilityV2ScenePlan(
    const std::vector<NativeSunVisibilityV2InstanceSelection> &selection,
    NativeSunVisibilityV2ScenePlan &output) {
  if (selection.empty() ||
      selection.size() > kNativeSunVisibilityV2MaximumSelectedInstances) {
    return Invalid(ValidationCode::SIZE_MISMATCH, "selection",
                   "sun-visibility V2 requires 1..256 explicitly classified instances");
  }
  NativeSunVisibilityV2ScenePlan candidate;
  std::uint64_t digest = kFnv1a64Offset;
  HashLittleEndian(kNativeSunVisibilityV2ContractVersion,
                   sizeof(kNativeSunVisibilityV2ContractVersion), digest);
  HashLittleEndian(selection.size(), sizeof(std::uint64_t), digest);
  try {
    const std::size_t admitted_capacity = std::min<std::size_t>(
        selection.size(), kNativeSunVisibilityV2MaximumAdmittedInstances);
    candidate.admitted_instances.reserve(admitted_capacity);
    candidate.unique_mesh_ids.reserve(admitted_capacity);
  } catch (...) {
    return Invalid(ValidationCode::SIZE_MISMATCH, "selection",
                   "sun-visibility V2 could not reserve its bounded scene plan");
  }

  std::uint64_t previous_id = 0U;
  for (std::size_t index = 0U; index < selection.size(); ++index) {
    const NativeSunVisibilityV2InstanceSelection &instance = selection[index];
    if (instance.instance_id == 0U || instance.instance_id <= previous_id) {
      return Invalid(ValidationCode::INVALID_IDENTIFIER,
                     "selection.instance_id",
                     "sun-visibility instance identifiers must be nonzero and strictly increasing",
                     index);
    }
    previous_id = instance.instance_id;
    if ((instance.flags & ~kKnownFlags) != 0U) {
      return Invalid(ValidationCode::VALUE_OUT_OF_RANGE, "selection.flags",
                     "sun-visibility selection contains unknown flag bits",
                     index);
    }
    if (instance.mesh_id == 0U) {
      return Invalid(ValidationCode::INVALID_IDENTIFIER,
                     "selection.mesh_id",
                     "every selected instance must name a stable mesh identity",
                     index);
    }
    HashLittleEndian(instance.instance_id, sizeof(instance.instance_id),
                     digest);
    HashLittleEndian(instance.mesh_id, sizeof(instance.mesh_id), digest);
    HashLittleEndian(instance.flags, sizeof(instance.flags), digest);
    const bool alpha =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER) != 0U;
    const bool decal =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_DECAL) != 0U;
    const bool inert =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_RT_INERT) != 0U;
    const bool receiver =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_RECEIVER) != 0U;
    const bool caster =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_CASTER) != 0U;
    const bool opaque =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_OPAQUE) != 0U;
    const bool raster_visible =
        (instance.flags & NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE) != 0U;
    const std::uint32_t exclusion_count = static_cast<std::uint32_t>(alpha) +
                                          static_cast<std::uint32_t>(decal) +
                                          static_cast<std::uint32_t>(inert);
    if (exclusion_count != 0U) {
      if (opaque || receiver || caster || exclusion_count != 1U) {
        return Invalid(
            ValidationCode::VALUE_OUT_OF_RANGE, "selection.flags",
            "alpha, decal, and RT-inert exclusions must be exclusive and cannot claim an RT role",
            index);
      }
      candidate.excluded_alpha_layer_count += static_cast<std::uint32_t>(alpha);
      candidate.excluded_decal_count += static_cast<std::uint32_t>(decal);
      candidate.excluded_rt_inert_count += static_cast<std::uint32_t>(inert);
      continue;
    }
    if (!opaque || (!receiver && !caster)) {
      return Invalid(
          ValidationCode::UNSUPPORTED_FEATURE, "selection.flags",
          "every admitted sun-visibility instance needs a mesh and must be explicitly opaque and a receiver or caster",
          index);
    }
    if (raster_visible && caster && !receiver) {
      return Invalid(
          ValidationCode::UNSUPPORTED_FEATURE, "selection.flags",
          "a raster-visible opaque caster must also be a receiver",
          index);
    }
    if (candidate.admitted_instances.size() >=
        kNativeSunVisibilityV2MaximumAdmittedInstances) {
      return Invalid(ValidationCode::SIZE_MISMATCH, "selection",
                     "sun-visibility V2 admitted-instance bound exceeded",
                     index);
    }
    candidate.admitted_instances.push_back(instance);
    candidate.receiver_count += static_cast<std::uint32_t>(receiver);
    candidate.caster_count += static_cast<std::uint32_t>(caster);
    candidate.raster_visible_receiver_count +=
        static_cast<std::uint32_t>(raster_visible && receiver);
    candidate.raster_visible_caster_count +=
        static_cast<std::uint32_t>(raster_visible && caster);
    if (std::find(candidate.unique_mesh_ids.begin(),
                  candidate.unique_mesh_ids.end(), instance.mesh_id) ==
        candidate.unique_mesh_ids.end()) {
      candidate.unique_mesh_ids.push_back(instance.mesh_id);
    }
  }
  if (candidate.admitted_instances.empty() || candidate.receiver_count == 0U ||
      candidate.caster_count == 0U) {
    return Invalid(
        ValidationCode::UNSUPPORTED_FEATURE, "selection",
        "sun-visibility V2 requires at least one admitted opaque receiver and caster");
  }
  candidate.scene_plan_digest = digest == 0U ? kFnv1a64Offset : digest;
  output = std::move(candidate);
  return ValidationResult::Success();
}

bool HasAttestedNativeSunVisibilityV2Capabilities(
    const NativeSunVisibilityV2Capabilities &capabilities) noexcept {
  return capabilities.version == kNativeSunVisibilityV2ContractVersion &&
         capabilities.backend == NativeDirectionalShadowBackend::METAL &&
         capabilities.supports_raytracing && capabilities.apple_family_9 &&
         capabilities.same_ogre_device && capabilities.same_ogre_queue &&
         capabilities.same_ogre_timeline &&
         capabilities.two_level_acceleration_structures &&
         capabilities.r16_float_visibility &&
         capabilities.separate_rgba16_base_and_sun_direct &&
         capabilities.rgba16_float_lit_composite &&
         capabilities.directional_self_hit_bias;
}

ValidationResult TryBuildNativeSunVisibilityV2SampleOracle(
    NativeDirectionalShadowVisibility visibility,
    const NativeDirectionalShadowRgba16Pixel &base_hdr_rgba16,
    const NativeDirectionalShadowRgba16Pixel &sun_direct_hdr_rgba16,
    NativeSunVisibilityV2Sample &output) {
  if (visibility != NativeDirectionalShadowVisibility::VISIBLE &&
      visibility != NativeDirectionalShadowVisibility::OCCLUDED) {
    return Invalid(ValidationCode::INVALID_ENUM, "visibility",
                   "sun visibility must be explicitly visible or occluded");
  }
  if (base_hdr_rgba16.channels[3U] !=
      kNativeDirectionalShadowVisibleR16) {
    return Invalid(ValidationCode::VALUE_OUT_OF_RANGE,
                   "base_hdr_rgba16.alpha",
                   "BaseHdr must use canonical opaque alpha");
  }
  if (sun_direct_hdr_rgba16.channels[3U] != 0U) {
    return Invalid(ValidationCode::VALUE_OUT_OF_RANGE,
                   "sun_direct_hdr_rgba16.alpha",
                   "SunDirectHdr alpha must be canonical positive zero");
  }
  std::array<float, 3U> base{};
  std::array<float, 3U> direct{};
  ValidationResult validation =
      DecodeNonnegativeRgb(base_hdr_rgba16, "base_hdr_rgba16.rgb", base);
  if (!validation) {
    return validation;
  }
  validation = DecodeNonnegativeRgb(sun_direct_hdr_rgba16,
                                    "sun_direct_hdr_rgba16.rgb", direct);
  if (!validation) {
    return validation;
  }

  NativeSunVisibilityV2Sample candidate;
  candidate.visibility = visibility;
  candidate.visibility_r16_bits =
      visibility == NativeDirectionalShadowVisibility::VISIBLE
          ? kNativeDirectionalShadowVisibleR16
          : kNativeDirectionalShadowOccludedR16;
  candidate.base_hdr_rgba16 = base_hdr_rgba16;
  candidate.sun_direct_hdr_rgba16 = sun_direct_hdr_rgba16;
  candidate.lit_hdr_rgba16 = base_hdr_rgba16;
  candidate.lit_hdr_rgba16.channels[3U] =
      kNativeDirectionalShadowVisibleR16;
  if (visibility == NativeDirectionalShadowVisibility::VISIBLE) {
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      const float sum = base[channel] + direct[channel];
      HdrR16Float quantized;
      validation = QuantizeHdrR16Float(sum, quantized);
      if (!validation) {
        return Invalid(validation.code, "lit_hdr_rgba16.rgb",
                       "BaseHdr plus SunDirectHdr exceeds the finite RGBA16 envelope",
                       channel);
      }
      candidate.lit_hdr_rgba16.channels[channel] = quantized.bits;
    }
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult ValidateNativeSunVisibilityV2FrameContract(
    const NativeSunVisibilityV2FrameContract &contract) {
  if (contract.version != kNativeSunVisibilityV2ContractVersion) {
    return Invalid(ValidationCode::UNSUPPORTED_VERSION, "contract.version",
                   "unsupported native sun-visibility contract version");
  }
  if (!HasAttestedNativeSunVisibilityV2Capabilities(contract.capabilities)) {
    return Invalid(ValidationCode::UNSUPPORTED_FEATURE,
                   "contract.capabilities",
                   "native sun-visibility V2 capabilities are incomplete");
  }
  if (contract.frame_id == 0U || contract.snapshot_id == 0U ||
      contract.view_id == 0U || contract.scene_plan_digest == 0U) {
    return Invalid(ValidationCode::INVALID_IDENTIFIER, "contract.lineage",
                   "frame, snapshot, view, and scene-plan identifiers must be nonzero");
  }
  if (contract.width == 0U || contract.height == 0U ||
      static_cast<std::uint64_t>(contract.width) >
          (std::numeric_limits<std::uint64_t>::max)() / contract.height) {
    return Invalid(ValidationCode::INVALID_DIMENSIONS,
                   "contract.dimensions",
                   "sun-visibility V2 dimensions are invalid");
  }
  const std::uint64_t pixels =
      static_cast<std::uint64_t>(contract.width) * contract.height;
  const std::uint64_t excluded_category_count =
      static_cast<std::uint64_t>(contract.excluded_alpha_layer_count) +
      static_cast<std::uint64_t>(contract.excluded_decal_count) +
      static_cast<std::uint64_t>(contract.excluded_rt_inert_count);
  if (contract.selected_instance_count == 0U ||
      contract.selected_instance_count >
          kNativeSunVisibilityV2MaximumSelectedInstances ||
      contract.admitted_instance_count == 0U ||
      contract.admitted_instance_count >
          kNativeSunVisibilityV2MaximumAdmittedInstances ||
      static_cast<std::uint64_t>(contract.admitted_instance_count) +
              contract.excluded_instance_count !=
          contract.selected_instance_count ||
      excluded_category_count != contract.excluded_instance_count ||
      contract.receiver_count == 0U || contract.caster_count == 0U ||
      contract.receiver_count > contract.admitted_instance_count ||
      contract.caster_count > contract.admitted_instance_count ||
      contract.raster_visible_receiver_count > contract.receiver_count ||
      contract.raster_visible_caster_count > contract.caster_count ||
      contract.raster_visible_caster_receiver_count !=
          contract.raster_visible_caster_count ||
      contract.raster_visible_caster_receiver_count >
          contract.raster_visible_receiver_count) {
    return Invalid(ValidationCode::SIZE_MISMATCH,
                   "contract.instance_counts",
                   "sun-visibility V2 instance admission counts disagree");
  }
  const std::uint64_t blas_coverage =
      static_cast<std::uint64_t>(contract.blas_build_count) +
      static_cast<std::uint64_t>(contract.blas_cache_hit_count) +
      static_cast<std::uint64_t>(contract.blas_refit_count);
  const std::uint64_t tlas_work =
      static_cast<std::uint64_t>(contract.tlas_build_count) +
      static_cast<std::uint64_t>(contract.tlas_cache_hit_count) +
      static_cast<std::uint64_t>(contract.tlas_refit_count);
  if (contract.unique_mesh_count == 0U ||
      contract.unique_mesh_count > contract.admitted_instance_count ||
      blas_coverage != contract.unique_mesh_count || tlas_work != 1U ||
      contract.tlas_instance_count != contract.admitted_instance_count ||
      contract.blas_resident_bytes == 0U ||
      contract.tlas_resident_bytes == 0U ||
      contract.acceleration_structure_scratch_peak_bytes == 0U) {
    return Invalid(ValidationCode::SIZE_MISMATCH,
                   "contract.acceleration_structures",
                   "every unique mesh and the shared TLAS require exactly one build, refit, or cache hit while every admitted instance requires one TLAS entry");
  }
  if (contract.primary_ray_count != pixels ||
      contract.secondary_sun_visibility_ray_count == 0U ||
      contract.secondary_sun_visibility_ray_count > pixels ||
      contract.primary_miss_count !=
          pixels - contract.secondary_sun_visibility_ray_count ||
      contract.visible_visibility_texel_count > pixels ||
      contract.occluded_visibility_texel_count !=
          pixels - contract.visible_visibility_texel_count ||
      contract.occluded_visibility_texel_count >
          contract.secondary_sun_visibility_ray_count ||
      contract.visibility_texel_count != pixels ||
      contract.composite_pixel_count != pixels ||
      contract.opaque_alpha_pixel_count != pixels) {
    return Invalid(ValidationCode::SIZE_MISMATCH, "contract.work_counts",
                   "sun-visibility V2 ray, visibility, composite, or alpha counters disagree with the output extent");
  }
  if (contract.acceleration_structure_encode_nanoseconds == 0U ||
      contract.ray_composite_encode_nanoseconds == 0U ||
      contract.gpu_execution_nanoseconds == 0U ||
      !std::isfinite(contract.minimum_ray_distance_meters) ||
      contract.minimum_ray_distance_meters <= 0.0F ||
      !std::isfinite(contract.self_hit_origin_bias_multiplier) ||
      contract.self_hit_origin_bias_multiplier != 2.0F) {
    return Invalid(ValidationCode::VALUE_OUT_OF_RANGE, "contract.timings",
                   "sun-visibility V2 requires nonzero timings and the exact positive directional self-hit bias");
  }
  if (contract.production_cpu_content_readbacks != 0U ||
      contract.production_gpu_content_readbacks != 0U) {
    return Invalid(ValidationCode::UNSUPPORTED_FEATURE,
                   "contract.production_content_readbacks",
                   "production sun-visibility V2 may not read image content back");
  }
  if (!contract.shader_lock_verified ||
      !contract.base_hdr_preserved_under_occlusion ||
      !contract.sun_direct_only_visibility_modulation ||
      !contract.output_opaque_alpha || !contract.submission_completed) {
    return Invalid(ValidationCode::SEQUENCE_MISMATCH,
                   "contract.composition",
                   "sun-visibility V2 composition or completion proof is incomplete");
  }
  if (!ValidateNativeSunVisibilityV2Result(contract.result) ||
      contract.result.code != NativeSunVisibilityV2Code::OK ||
      contract.result.stage != NativeSunVisibilityV2Stage::COMPLETE ||
      contract.result.frame_id != contract.frame_id ||
      contract.result.snapshot_id != contract.snapshot_id) {
    return Invalid(ValidationCode::SEQUENCE_MISMATCH, "contract.result",
                   "sun-visibility V2 terminal result lost its exact stage or frame lineage");
  }
  if (!contract.acceptance_samples_validated) {
    return ValidationResult::Success();
  }
  if (contract.acceptance_caster_instance_id == 0U ||
      contract.acceptance_caster_transform_revision == 0U) {
    return Invalid(
        ValidationCode::INVALID_IDENTIFIER, "contract.acceptance_lineage",
        "acceptance samples require a nonzero caster identity and transform revision");
  }
  for (std::size_t index = 0U; index < contract.acceptance_samples.size(); ++index) {
    const NativeSunVisibilityV2Sample &sample = contract.acceptance_samples[index];
    const NativeDirectionalShadowVisibility expected =
        index == 1U ? NativeDirectionalShadowVisibility::OCCLUDED
                    : NativeDirectionalShadowVisibility::VISIBLE;
    NativeSunVisibilityV2Sample oracle;
    const ValidationResult oracle_result =
        TryBuildNativeSunVisibilityV2SampleOracle(
            sample.visibility, sample.base_hdr_rgba16,
            sample.sun_direct_hdr_rgba16, oracle);
    if (!oracle_result || sample.version != kNativeSunVisibilityV2ContractVersion ||
        sample.visibility != expected || sample.primary_hit_instance_id == 0U ||
        !sample.primary_hit_is_receiver ||
        (sample.visibility == NativeDirectionalShadowVisibility::OCCLUDED) !=
            (sample.secondary_blocker_instance_id != 0U) ||
        sample.visibility_r16_bits != oracle.visibility_r16_bits ||
        !PixelEqual(sample.lit_hdr_rgba16, oracle.lit_hdr_rgba16)) {
      return Invalid(ValidationCode::REVISION_MISMATCH,
                     "contract.acceptance_samples",
                     "sun-visibility V2 sample differs from BaseHdr plus visibility times SunDirectHdr",
                     index);
    }
    if (index == 2U &&
        (!sample.primary_hit_is_caster || !sample.primary_hit_is_receiver ||
         sample.visibility != NativeDirectionalShadowVisibility::VISIBLE ||
         !PixelEqual(sample.lit_hdr_rgba16, oracle.lit_hdr_rgba16))) {
      return Invalid(
          ValidationCode::REVISION_MISMATCH,
          "contract.acceptance_samples",
          "visible caster pixels must hit receiver+caster geometry and avoid self-shadow",
          index);
    }
  }
  if (contract.acceptance_samples[1U].secondary_blocker_instance_id !=
          contract.acceptance_caster_instance_id ||
      contract.acceptance_samples[2U].primary_hit_instance_id !=
          contract.acceptance_caster_instance_id) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH, "contract.acceptance_lineage",
        "shadowed-road and visible-caster samples must name the exact acceptance caster");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeSunVisibilityV2FirstFrameSmokeContract(
    const NativeSunVisibilityV2FrameContract &contract) {
  const ValidationResult general =
      ValidateNativeSunVisibilityV2FrameContract(contract);
  if (!general) {
    return general;
  }
  if (contract.selected_instance_count != 5U ||
      contract.admitted_instance_count != 3U ||
      contract.receiver_count != 3U || contract.caster_count != 1U ||
      contract.excluded_instance_count != 2U ||
      contract.excluded_alpha_layer_count != 1U ||
      contract.excluded_decal_count != 0U ||
      contract.excluded_rt_inert_count != 1U ||
      contract.raster_visible_receiver_count != 3U ||
      contract.raster_visible_caster_count != 1U ||
      contract.raster_visible_caster_receiver_count != 1U ||
      contract.unique_mesh_count != 3U || contract.blas_build_count != 3U ||
      contract.blas_cache_hit_count != 0U ||
      contract.blas_refit_count != 0U || contract.tlas_build_count != 1U ||
      contract.tlas_cache_hit_count != 0U ||
      contract.tlas_refit_count != 0U || contract.tlas_instance_count != 3U ||
      contract.secondary_sun_visibility_ray_count !=
          contract.primary_ray_count ||
      contract.primary_miss_count != 0U ||
      contract.acceptance_samples[0U].primary_hit_instance_id != 1U ||
      contract.acceptance_samples[0U].secondary_blocker_instance_id != 0U ||
      contract.acceptance_samples[1U].primary_hit_instance_id != 1U ||
      contract.acceptance_samples[1U].secondary_blocker_instance_id != 3U ||
      contract.acceptance_samples[2U].primary_hit_instance_id != 3U ||
      contract.acceptance_samples[2U].secondary_blocker_instance_id != 0U ||
      contract.acceptance_caster_instance_id != 3U ||
      contract.acceptance_caster_transform_revision == 0U ||
      !contract.acceptance_samples_validated) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH, "contract.first_frame_smoke",
        "road/gate first-frame smoke requires exact selection, fresh AS builds, full receiver coverage, and byte-exact samples");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateNativeSunVisibilityV2MovedCasterSmokeContract(
    const NativeSunVisibilityV2FrameContract &first_frame,
    const NativeSunVisibilityV2FrameContract &moved_frame) {
  const ValidationResult first =
      ValidateNativeSunVisibilityV2FirstFrameSmokeContract(first_frame);
  if (!first) {
    return first;
  }
  const ValidationResult general =
      ValidateNativeSunVisibilityV2FrameContract(moved_frame);
  if (!general) {
    return general;
  }
  if (moved_frame.selected_instance_count != 5U ||
      moved_frame.admitted_instance_count != 3U ||
      moved_frame.receiver_count != 3U || moved_frame.caster_count != 1U ||
      moved_frame.excluded_instance_count != 2U ||
      moved_frame.excluded_alpha_layer_count != 1U ||
      moved_frame.excluded_decal_count != 0U ||
      moved_frame.excluded_rt_inert_count != 1U ||
      moved_frame.raster_visible_receiver_count != 3U ||
      moved_frame.raster_visible_caster_count != 1U ||
      moved_frame.raster_visible_caster_receiver_count != 1U ||
      moved_frame.unique_mesh_count != 3U ||
      moved_frame.blas_build_count != 0U ||
      moved_frame.blas_cache_hit_count != 3U ||
      moved_frame.blas_refit_count != 0U ||
      moved_frame.tlas_build_count != 0U ||
      moved_frame.tlas_cache_hit_count != 0U ||
      moved_frame.tlas_refit_count != 1U ||
      moved_frame.tlas_instance_count != 3U ||
      moved_frame.secondary_sun_visibility_ray_count !=
          moved_frame.primary_ray_count ||
      moved_frame.primary_miss_count != 0U ||
      moved_frame.acceptance_samples[0U].primary_hit_instance_id != 1U ||
      moved_frame.acceptance_samples[0U].secondary_blocker_instance_id != 0U ||
      moved_frame.acceptance_samples[1U].primary_hit_instance_id != 1U ||
      moved_frame.acceptance_samples[1U].secondary_blocker_instance_id != 3U ||
      moved_frame.acceptance_samples[2U].primary_hit_instance_id != 3U ||
      moved_frame.acceptance_samples[2U].secondary_blocker_instance_id != 0U ||
      moved_frame.acceptance_caster_instance_id != 3U ||
      !moved_frame.acceptance_samples_validated ||
      moved_frame.scene_plan_digest != first_frame.scene_plan_digest ||
      moved_frame.acceptance_caster_instance_id !=
          first_frame.acceptance_caster_instance_id ||
      moved_frame.acceptance_caster_transform_revision ==
          first_frame.acceptance_caster_transform_revision ||
      moved_frame.frame_id <= first_frame.frame_id ||
      moved_frame.snapshot_id == first_frame.snapshot_id ||
      moved_frame.view_id != first_frame.view_id ||
      moved_frame.width != first_frame.width ||
      moved_frame.height != first_frame.height) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH, "contract.moved_caster_smoke",
        "moved-gate smoke requires stable selection and view lineage, a changed caster transform, exact BLAS reuse, one TLAS refit, and exact sample identities");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
