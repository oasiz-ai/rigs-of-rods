/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral temporal anti-aliasing contract and CPU oracle.

#include "OgreNextTaaContract.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace RoR::Render {
namespace {

constexpr float kMaximumHdrChannel = 65504.0F;
constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

// The scalar oracle mirrors a shader's ordered binary32 operations. Volatile
// stores make every intermediate round to binary32 and prohibit contraction
// across these explicit operation boundaries.
float F32Add(float lhs, float rhs) noexcept {
  volatile float result = lhs + rhs;
  return result;
}

float F32Subtract(float lhs, float rhs) noexcept {
  volatile float result = lhs - rhs;
  return result;
}

float F32Multiply(float lhs, float rhs) noexcept {
  volatile float result = lhs * rhs;
  return result;
}

float F32Divide(float lhs, float rhs) noexcept {
  volatile float result = lhs / rhs;
  return result;
}

float F32Sqrt(float value) noexcept {
  volatile float result = std::sqrt(value);
  return result;
}

constexpr std::array<Float2, kOgreNextTaaJitterPhaseCount> kJitterSequence{{
    {0.0F, -1.0F / 6.0F},
    {-0.25F, 1.0F / 6.0F},
    {0.25F, -7.0F / 18.0F},
    {-0.375F, -1.0F / 18.0F},
    {0.125F, 5.0F / 18.0F},
    {-0.125F, -5.0F / 18.0F},
    {0.375F, 1.0F / 18.0F},
    {-0.4375F, 7.0F / 18.0F},
}};

ValidationResult Invalid(const char *field, const char *detail) {
  return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE, field,
                                   detail);
}

void HashByte(std::uint64_t &hash, std::uint8_t value) noexcept {
  hash ^= static_cast<std::uint64_t>(value);
  hash *= kFnv1a64Prime;
}

void HashU32(std::uint64_t &hash, std::uint32_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    HashByte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void HashU64(std::uint64_t &hash, std::uint64_t value) noexcept {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    HashByte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

void HashFloat(std::uint64_t &hash, float value) noexcept {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "TAA camera lineage requires IEEE binary32 storage");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  HashU32(hash, bits);
}

bool IsFinitePositiveNormal(float value) noexcept {
  return std::isfinite(value) && value > 0.0F &&
         std::fpclassify(value) == FP_NORMAL;
}

ValidationResult ValidateCamera(const CameraViewRequest &view) {
  if (view.view_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "input.view.view_id",
                                     "TAA view identifier must be nonzero");
  }
  if (view.width == 0U || view.height == 0U || view.width > 16384U ||
      view.height > 16384U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "input.view.extent",
        "TAA view extent must be nonzero and no greater than 16384 per axis");
  }
  if (!IsFinite(view.view_from_render) || !IsFinite(view.clip_from_view) ||
      !IsFinite(view.previous_view_from_render) ||
      !IsFinite(view.previous_clip_from_view) ||
      !IsFinite(view.temporal_jitter_pixels) ||
      !std::isfinite(view.near_plane) || !std::isfinite(view.far_plane) ||
      !IsFinitePositiveNormal(view.exposure)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "input.view.camera",
        "TAA camera matrices and scalar state must be finite with positive "
        "normal exposure");
  }
  if (view.near_plane <= 0.0F || view.far_plane <= view.near_plane ||
      view.visibility_mask == 0U ||
      std::fabs(view.temporal_jitter_pixels.x) > 0.5F ||
      std::fabs(view.temporal_jitter_pixels.y) > 0.5F) {
    return Invalid(
        "input.view.camera",
        "TAA clipping, visibility, and half-pixel jitter bounds are invalid");
  }
  if (!HasRigidRightHandedAffineTransform(view.view_from_render) ||
      !HasRigidRightHandedAffineTransform(view.previous_view_from_render)) {
    return Invalid("input.view.view_from_render",
                   "TAA current and previous views must be rigid right-handed "
                   "affine transforms");
  }
  if (!IsCanonicalProjection(view.clip_from_view, view.near_plane,
                             view.far_plane) ||
      !IsCanonicalProjection(view.previous_clip_from_view, view.near_plane,
                             view.far_plane)) {
    return Invalid("input.view.clip_from_view",
                   "TAA current and previous projections must use the portable "
                   "non-reversed convention");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateConfiguration(const OgreNextTaaConfiguration &configuration) {
  if (configuration.version != kOgreNextTaaContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "configuration.version",
        "unsupported Ogre-Next TAA configuration version");
  }
  if (configuration.lifecycle_epoch == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "configuration.lifecycle_epoch",
        "TAA frontend lifecycle epoch must be nonzero and never reused");
  }
  if (!std::isfinite(configuration.history_weight) ||
      configuration.history_weight < 0.0F ||
      configuration.history_weight >= 1.0F) {
    return Invalid("configuration.history_weight",
                   "TAA history weight must be finite and inside [0, 1)");
  }
  if (!std::isfinite(configuration.variance_clip_gamma) ||
      configuration.variance_clip_gamma <= 0.0F ||
      configuration.variance_clip_gamma > 8.0F) {
    return Invalid(
        "configuration.variance_clip_gamma",
        "TAA variance clipping gamma must be finite and inside (0, 8]");
  }
  if (!std::isfinite(configuration.disocclusion_absolute_depth) ||
      !std::isfinite(configuration.disocclusion_relative_depth) ||
      configuration.disocclusion_absolute_depth < 0.0F ||
      configuration.disocclusion_relative_depth < 0.0F ||
      configuration.disocclusion_absolute_depth > 1.0F ||
      configuration.disocclusion_relative_depth > 1.0F) {
    return Invalid(
        "configuration.disocclusion_depth",
        "TAA disocclusion thresholds must be finite and inside [0, 1]");
  }
  if (!IsFinitePositiveNormal(configuration.full_motion_rejection_pixels) ||
      configuration.full_motion_rejection_pixels > 16384.0F) {
    return Invalid("configuration.full_motion_rejection_pixels",
                   "TAA full-motion rejection must be a positive normal value "
                   "no greater than 16384 pixels");
  }
  if (!IsFinitePositiveNormal(configuration.maximum_exposure_ratio) ||
      configuration.maximum_exposure_ratio <= 1.0F ||
      configuration.maximum_exposure_ratio > 256.0F) {
    return Invalid(
        "configuration.maximum_exposure_ratio",
        "TAA maximum exposure ratio must be a normal value inside (1, 256]");
  }
  return ValidationResult::Success();
}

bool IsValidHdrColour(const Float4 &colour) noexcept {
  return IsFinite(colour) && colour.x >= 0.0F &&
         colour.x <= kMaximumHdrChannel && colour.y >= 0.0F &&
         colour.y <= kMaximumHdrChannel && colour.z >= 0.0F &&
         colour.z <= kMaximumHdrChannel && colour.w == 1.0F;
}

Float3 ToYCoCg(const Float4 &colour) noexcept {
  return Float3{
      F32Add(F32Add(F32Multiply(0.25F, colour.x), F32Multiply(0.5F, colour.y)),
             F32Multiply(0.25F, colour.z)),
      F32Subtract(F32Multiply(0.5F, colour.x), F32Multiply(0.5F, colour.z)),
      F32Subtract(F32Subtract(F32Multiply(0.5F, colour.y),
                              F32Multiply(0.25F, colour.x)),
                  F32Multiply(0.25F, colour.z))};
}

Float3 FromYCoCg(const Float3 &colour) noexcept {
  return Float3{F32Subtract(F32Add(colour.x, colour.y), colour.z),
                F32Add(colour.x, colour.z),
                F32Subtract(F32Subtract(colour.x, colour.y), colour.z)};
}

float ClampHdr(float value) noexcept {
  return (std::clamp)(value, 0.0F, kMaximumHdrChannel);
}

bool SamePlan(const OgreNextTaaFramePlan &lhs,
              const OgreNextTaaFramePlan &rhs) noexcept {
  return lhs.version == rhs.version &&
         lhs.lifecycle_epoch == rhs.lifecycle_epoch &&
         lhs.frame_id == rhs.frame_id && lhs.snapshot_id == rhs.snapshot_id &&
         lhs.view_id == rhs.view_id && lhs.width == rhs.width &&
         lhs.height == rhs.height && lhs.view == rhs.view &&
         lhs.camera_lineage_fnv1a64 == rhs.camera_lineage_fnv1a64 &&
         lhs.jitter_phase == rhs.jitter_phase &&
         lhs.jitter_pixels == rhs.jitter_pixels &&
         lhs.previous_jitter_pixels == rhs.previous_jitter_pixels &&
         lhs.current_pre_exposure == rhs.current_pre_exposure &&
         lhs.previous_pre_exposure == rhs.previous_pre_exposure &&
         lhs.history_exposure_ratio == rhs.history_exposure_ratio &&
         lhs.source_history_generation == rhs.source_history_generation &&
         lhs.destination_history_generation ==
             rhs.destination_history_generation &&
         lhs.source_history_slot == rhs.source_history_slot &&
         lhs.destination_history_slot == rhs.destination_history_slot &&
         lhs.reset_reason == rhs.reset_reason &&
         lhs.history_available == rhs.history_available;
}

bool SameBinding(const OgreNextTaaImageBinding &lhs,
                 const OgreNextTaaImageBinding &rhs) noexcept {
  return lhs.native_identity == rhs.native_identity &&
         lhs.generation == rhs.generation && lhs.format == rhs.format &&
         lhs.width == rhs.width && lhs.height == rhs.height;
}

ValidationResult ValidateBinding(const OgreNextTaaImageBinding &binding,
                                 PixelFormat expected_format,
                                 const OgreNextTaaFramePlan &plan,
                                 const char *field) {
  if (binding.native_identity == 0U || binding.generation == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, field,
        "TAA native image identity and generation must both be nonzero");
  }
  if (binding.format != expected_format) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, field,
        "TAA native image format does not match its exact role");
  }
  if (binding.width != plan.width || binding.height != plan.height) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, field,
        "TAA native image extent does not match the planned view");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateEvidence(const OgreNextTaaFramePlan &plan,
                 const OgreNextTaaExecutionEvidence &evidence) {
  if (evidence.version != kOgreNextTaaContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "evidence.version",
        "unsupported Ogre-Next TAA execution-evidence version");
  }
  if (evidence.lifecycle_epoch != plan.lifecycle_epoch ||
      evidence.frame_id != plan.frame_id ||
      evidence.snapshot_id != plan.snapshot_id ||
      evidence.view_id != plan.view_id ||
      evidence.camera_lineage_fnv1a64 != plan.camera_lineage_fnv1a64) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "evidence.lineage",
        "TAA execution evidence does not match planned frame, snapshot, and "
        "view lineage");
  }

  const struct {
    const OgreNextTaaImageBinding *binding;
    PixelFormat format;
    const char *field;
  } bindings[] = {
      {&evidence.current_colour, PixelFormat::RGBA16_FLOAT,
       "evidence.current_colour"},
      {&evidence.current_depth, PixelFormat::R32_FLOAT,
       "evidence.current_depth"},
      {&evidence.motion_vectors, PixelFormat::RG16_FLOAT,
       "evidence.motion_vectors"},
      {&evidence.reactive_mask, PixelFormat::R32_FLOAT,
       "evidence.reactive_mask"},
      {&evidence.history_source, PixelFormat::RGBA16_FLOAT,
       "evidence.history_source"},
      {&evidence.history_destination, PixelFormat::RGBA16_FLOAT,
       "evidence.history_destination"},
  };
  for (const auto &entry : bindings) {
    const ValidationResult validation =
        ValidateBinding(*entry.binding, entry.format, plan, entry.field);
    if (!validation) {
      return validation;
    }
  }
  for (std::size_t left = 0U; left < std::size(bindings); ++left) {
    for (std::size_t right = left + 1U; right < std::size(bindings); ++right) {
      if (bindings[left].binding->native_identity ==
          bindings[right].binding->native_identity) {
        return ValidationResult::Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "evidence.images",
            "all TAA image roles require distinct native identities", right);
      }
    }
  }
  if (evidence.prepare_count != 1U || evidence.execute_count != 1U ||
      evidence.history_read_count != (plan.history_available ? 1U : 0U) ||
      evidence.history_write_count != 1U ||
      evidence.history_advance_count != 1U ||
      evidence.jitter_application_count != 1U ||
      evidence.native_state_verification_count == 0U) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "evidence.counts",
        "TAA must prepare, execute, write, advance, and apply jitter exactly "
        "once per committed view");
  }
  if (evidence.production_content_readback_count != 0U ||
      evidence.production_framebuffer_readback_count != 0U) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE,
                                     "evidence.production_readbacks",
                                     "production TAA may not read image or "
                                     "framebuffer content back to the CPU");
  }
  if (!evidence.unjittered_culling || !evidence.motion_vectors_remove_jitter ||
      !evidence.current_previous_transform_lineage ||
      !evidence.non_reversed_depth_reprojection ||
      !evidence.pre_exposure_history_rescale ||
      !evidence.reactive_mask_consumed ||
      !evidence.variance_neighbourhood_clipping || !evidence.output_alpha_one) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "evidence.policy",
        "TAA execution omitted a required motion, depth, exposure, reactive, "
        "clipping, or alpha invariant");
  }
  return ValidationResult::Success();
}

} // namespace

ValidationResult ComputeOgreNextTaaJitterPixels(std::uint64_t frame_id,
                                                Float2 &output) {
  if (frame_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "frame_id",
        "TAA jitter requires a nonzero frontend frame identifier");
  }
  const std::size_t phase =
      static_cast<std::size_t>((frame_id - 1U) % kOgreNextTaaJitterPhaseCount);
  output = kJitterSequence[phase];
  return ValidationResult::Success();
}

ValidationResult ComputeOgreNextTaaCameraLineage(const CameraViewRequest &view,
                                                 std::uint64_t &output) {
  const ValidationResult validation = ValidateCamera(view);
  if (!validation) {
    return validation;
  }
  std::uint64_t hash = kFnv1a64Offset;
  constexpr char domain[] = "ror.ogrenext.taa.camera.v1";
  for (std::size_t index = 0U; index + 1U < sizeof(domain); ++index) {
    HashByte(hash, static_cast<std::uint8_t>(domain[index]));
  }
  HashU64(hash, view.view_id);
  HashU32(hash, view.width);
  HashU32(hash, view.height);
  for (const float value : view.view_from_render.elements) {
    HashFloat(hash, value);
  }
  for (const float value : view.clip_from_view.elements) {
    HashFloat(hash, value);
  }
  for (const float value : view.previous_view_from_render.elements) {
    HashFloat(hash, value);
  }
  for (const float value : view.previous_clip_from_view.elements) {
    HashFloat(hash, value);
  }
  HashFloat(hash, view.temporal_jitter_pixels.x);
  HashFloat(hash, view.temporal_jitter_pixels.y);
  HashFloat(hash, view.near_plane);
  HashFloat(hash, view.far_plane);
  HashFloat(hash, view.exposure);
  HashU32(hash, view.visibility_mask);
  output = hash == 0U ? kFnv1a64Offset : hash;
  return ValidationResult::Success();
}

ValidationResult
EvaluateOgreNextTaaPixel(const OgreNextTaaConfiguration &configuration,
                         const OgreNextTaaPixelInput &input,
                         OgreNextTaaPixelResult &output) {
  const ValidationResult configuration_validation =
      ValidateConfiguration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (input.version != kOgreNextTaaContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "input.version",
        "unsupported Ogre-Next TAA pixel-input version");
  }
  for (std::size_t index = 0U; index < input.current_neighbourhood.size();
       ++index) {
    if (!IsValidHdrColour(input.current_neighbourhood[index])) {
      return Invalid("input.current_neighbourhood",
                     "TAA current samples must be finite nonnegative "
                     "RGBA16-range colour with alpha one");
    }
  }
  if (!IsValidHdrColour(input.history_colour)) {
    return Invalid("input.history_colour",
                   "TAA history must be finite nonnegative RGBA16-range colour "
                   "with alpha one");
  }
  if (!std::isfinite(input.current_depth) ||
      !std::isfinite(input.reprojected_previous_depth) ||
      input.current_depth < 0.0F || input.current_depth > 1.0F ||
      input.reprojected_previous_depth < 0.0F ||
      input.reprojected_previous_depth > 1.0F) {
    return Invalid("input.depth",
                   "TAA depth inputs must use finite non-reversed [0, 1] NDC");
  }
  if (!IsFinite(input.motion_pixels) || !std::isfinite(input.reactive_mask) ||
      input.reactive_mask < 0.0F || input.reactive_mask > 1.0F) {
    return Invalid("input.motion_reactive",
                   "TAA motion must be finite and reactive coverage must be "
                   "inside [0, 1]");
  }
  if (!IsFinitePositiveNormal(input.current_pre_exposure) ||
      !IsFinitePositiveNormal(input.previous_pre_exposure)) {
    return Invalid("input.pre_exposure",
                   "TAA pre-exposure values must be finite positive normal "
                   "binary32 values");
  }

  OgreNextTaaPixelResult candidate;
  candidate.colour = input.current_neighbourhood[4U];
  const double exact_exposure_ratio =
      static_cast<double>(input.current_pre_exposure) /
      static_cast<double>(input.previous_pre_exposure);
  const double minimum_exposure_ratio =
      1.0 / static_cast<double>(configuration.maximum_exposure_ratio);
  candidate.exposure_rejected =
      !std::isfinite(exact_exposure_ratio) ||
      exact_exposure_ratio < minimum_exposure_ratio ||
      exact_exposure_ratio >
          static_cast<double>(configuration.maximum_exposure_ratio);
  const float exposure_ratio = candidate.exposure_rejected
                                   ? 1.0F
                                   : static_cast<float>(exact_exposure_ratio);
  if (!IsFinitePositiveNormal(exposure_ratio)) {
    candidate.exposure_rejected = true;
    candidate.history_exposure_ratio = 1.0F;
  } else {
    candidate.history_exposure_ratio = exposure_ratio;
  }

  const float depth_error = std::fabs(
      F32Subtract(input.current_depth, input.reprojected_previous_depth));
  const float depth_tolerance =
      F32Add(configuration.disocclusion_absolute_depth,
             F32Multiply(configuration.disocclusion_relative_depth,
                         (std::max)(input.current_depth,
                                    input.reprojected_previous_depth)));
  candidate.depth_rejected = depth_error > depth_tolerance;

  const float motion_x_squared =
      F32Multiply(input.motion_pixels.x, input.motion_pixels.x);
  const float motion_y_squared =
      F32Multiply(input.motion_pixels.y, input.motion_pixels.y);
  const float motion_squared = F32Add(motion_x_squared, motion_y_squared);
  const float motion_length = F32Sqrt(motion_squared);
  if (!std::isfinite(motion_length)) {
    return Invalid("input.motion_pixels",
                   "TAA motion magnitude overflowed the binary32 envelope");
  }
  candidate.motion_rejected =
      motion_length >= configuration.full_motion_rejection_pixels;
  candidate.reactive_rejected = input.reactive_mask == 1.0F;

  if (!input.history_available || candidate.exposure_rejected ||
      candidate.depth_rejected || candidate.motion_rejected ||
      candidate.reactive_rejected) {
    output = candidate;
    return ValidationResult::Success();
  }

  std::array<Float3, 9U> neighbourhood{};
  Float3 minimum{(std::numeric_limits<float>::max)(),
                 (std::numeric_limits<float>::max)(),
                 (std::numeric_limits<float>::max)()};
  Float3 maximum{(std::numeric_limits<float>::lowest)(),
                 (std::numeric_limits<float>::lowest)(),
                 (std::numeric_limits<float>::lowest)()};
  float sum[3U] = {0.0F, 0.0F, 0.0F};
  for (std::size_t index = 0U; index < neighbourhood.size(); ++index) {
    neighbourhood[index] = ToYCoCg(input.current_neighbourhood[index]);
    const float values[3U] = {neighbourhood[index].x, neighbourhood[index].y,
                              neighbourhood[index].z};
    float *minimum_values[3U] = {&minimum.x, &minimum.y, &minimum.z};
    float *maximum_values[3U] = {&maximum.x, &maximum.y, &maximum.z};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      *minimum_values[channel] =
          (std::min)(*minimum_values[channel], values[channel]);
      *maximum_values[channel] =
          (std::max)(*maximum_values[channel], values[channel]);
      sum[channel] = F32Add(sum[channel], values[channel]);
    }
  }

  float squared_error[3U] = {0.0F, 0.0F, 0.0F};
  const float mean[3U] = {F32Divide(sum[0U], 9.0F), F32Divide(sum[1U], 9.0F),
                          F32Divide(sum[2U], 9.0F)};
  for (const Float3 &sample : neighbourhood) {
    const float values[3U] = {sample.x, sample.y, sample.z};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      const float difference = F32Subtract(values[channel], mean[channel]);
      squared_error[channel] =
          F32Add(squared_error[channel], F32Multiply(difference, difference));
    }
  }

  Float4 rescaled_history = input.history_colour;
  rescaled_history.x = F32Multiply(rescaled_history.x, exposure_ratio);
  rescaled_history.y = F32Multiply(rescaled_history.y, exposure_ratio);
  rescaled_history.z = F32Multiply(rescaled_history.z, exposure_ratio);
  if (!IsFinite(rescaled_history) || rescaled_history.x > kMaximumHdrChannel ||
      rescaled_history.y > kMaximumHdrChannel ||
      rescaled_history.z > kMaximumHdrChannel) {
    candidate.exposure_rejected = true;
    output = candidate;
    return ValidationResult::Success();
  }

  Float3 clipped = ToYCoCg(rescaled_history);
  const float minimum_values[3U] = {minimum.x, minimum.y, minimum.z};
  const float maximum_values[3U] = {maximum.x, maximum.y, maximum.z};
  float *clipped_values[3U] = {&clipped.x, &clipped.y, &clipped.z};
  for (std::size_t channel = 0U; channel < 3U; ++channel) {
    const float standard_deviation =
        F32Sqrt(F32Divide(squared_error[channel], 9.0F));
    const float variance_radius =
        F32Multiply(configuration.variance_clip_gamma, standard_deviation);
    const float lower = (std::max)(minimum_values[channel],
                                   F32Subtract(mean[channel], variance_radius));
    const float upper = (std::min)(maximum_values[channel],
                                   F32Add(mean[channel], variance_radius));
    const float original = *clipped_values[channel];
    *clipped_values[channel] = (std::clamp)(original, lower, upper);
    candidate.history_clipped =
        candidate.history_clipped || *clipped_values[channel] != original;
  }

  const Float3 clipped_rgb = FromYCoCg(clipped);
  const float motion_factor =
      (std::max)(0.0F,
                 F32Subtract(
                     1.0F,
                     F32Divide(motion_length,
                               configuration.full_motion_rejection_pixels)));
  const float weight =
      F32Multiply(F32Multiply(configuration.history_weight,
                              F32Subtract(1.0F, input.reactive_mask)),
                  motion_factor);
  if (!std::isfinite(weight) || weight < 0.0F || weight >= 1.0F) {
    return Invalid("result.history_weight",
                   "TAA history weight left the finite [0, 1) envelope");
  }
  const Float4 &current = input.current_neighbourhood[4U];
  candidate.history_weight = weight;
  candidate.colour =
      Float4{ClampHdr(F32Add(F32Multiply(current.x, F32Subtract(1.0F, weight)),
                             F32Multiply(ClampHdr(clipped_rgb.x), weight))),
             ClampHdr(F32Add(F32Multiply(current.y, F32Subtract(1.0F, weight)),
                             F32Multiply(ClampHdr(clipped_rgb.y), weight))),
             ClampHdr(F32Add(F32Multiply(current.z, F32Subtract(1.0F, weight)),
                             F32Multiply(ClampHdr(clipped_rgb.z), weight))),
             1.0F};
  if (!IsValidHdrColour(candidate.colour)) {
    return Invalid("result.colour",
                   "TAA reference produced invalid RGBA16-range colour");
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
OgreNextTaaState::Initialize(const OgreNextTaaConfiguration &configuration) {
  if (initialized_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "Ogre-Next TAA state is already initialized");
  }
  const ValidationResult validation = ValidateConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  if (configuration.lifecycle_epoch <= last_lifecycle_epoch_) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "configuration.lifecycle_epoch",
        "TAA frontend lifecycle epochs must increase after reset");
  }
  configuration_ = configuration;
  lifecycle_epoch_ = configuration.lifecycle_epoch;
  last_lifecycle_epoch_ = configuration.lifecycle_epoch;
  committed_plan_ = OgreNextTaaFramePlan{};
  last_execution_evidence_ = OgreNextTaaExecutionEvidence{};
  committed_frame_id_ = 0U;
  committed_snapshot_id_ = 0U;
  committed_view_id_ = 0U;
  history_generation_ = 0U;
  committed_width_ = 0U;
  committed_height_ = 0U;
  committed_jitter_pixels_ = Float2{};
  committed_history_bindings_ = {};
  committed_pre_exposure_ = 1.0F;
  history_valid_ = false;
  explicitly_invalidated_ = false;
  ClearPending();
  initialized_ = true;
  return ValidationResult::Success();
}

ValidationResult
OgreNextTaaState::PrepareFrame(const OgreNextTaaFrameInput &input,
                               OgreNextTaaFramePlan &output) const {
  if (!initialized_) {
    return ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH, "state",
                                     "Ogre-Next TAA state is not initialized");
  }
  if (input.version != kOgreNextTaaContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "input.version",
        "unsupported Ogre-Next TAA frame-input version");
  }
  if (input.lifecycle_epoch != lifecycle_epoch_) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "input.lifecycle_epoch",
        "TAA frame belongs to a different frontend lifecycle");
  }
  if (committed_frame_id_ == (std::numeric_limits<std::uint64_t>::max)() ||
      input.frame_id != committed_frame_id_ + 1U || input.snapshot_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "input.lineage",
        "TAA frame IDs must be contiguous and snapshot IDs nonzero");
  }
  const ValidationResult camera_validation = ValidateCamera(input.view);
  if (!camera_validation) {
    return camera_validation;
  }
  if (!IsFinitePositiveNormal(input.pre_exposure) ||
      input.pre_exposure > kMaximumHdrChannel) {
    return Invalid("input.pre_exposure",
                   "TAA pre-exposure must be a finite positive normal "
                   "RGBA16-representable value");
  }
  if (history_generation_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state.history_generation",
        "TAA history generation exhausted its identifier domain");
  }

  OgreNextTaaFramePlan candidate;
  candidate.lifecycle_epoch = lifecycle_epoch_;
  candidate.frame_id = input.frame_id;
  candidate.snapshot_id = input.snapshot_id;
  candidate.view_id = input.view.view_id;
  candidate.width = input.view.width;
  candidate.height = input.view.height;
  candidate.view = input.view;
  const ValidationResult camera_lineage = ComputeOgreNextTaaCameraLineage(
      input.view, candidate.camera_lineage_fnv1a64);
  if (!camera_lineage) {
    return camera_lineage;
  }
  candidate.jitter_phase = static_cast<std::uint32_t>(
      (input.frame_id - 1U) % kOgreNextTaaJitterPhaseCount);
  candidate.jitter_pixels = kJitterSequence[candidate.jitter_phase];
  if (input.view.temporal_jitter_pixels != candidate.jitter_pixels) {
    return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                     "input.view.temporal_jitter_pixels",
                                     "TAA view jitter differs from the exact "
                                     "phase selected by its frontend frame ID");
  }
  if (committed_frame_id_ != 0U && input.snapshot_id < committed_snapshot_id_) {
    return ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH,
                                     "input.snapshot_id",
                                     "TAA snapshot identifiers must not move "
                                     "backward across committed frames");
  }
  candidate.current_pre_exposure = input.pre_exposure;
  candidate.source_history_generation = history_generation_;
  candidate.destination_history_generation = history_generation_ + 1U;
  candidate.source_history_slot =
      static_cast<std::uint8_t>(history_generation_ & 1U);
  candidate.destination_history_slot =
      static_cast<std::uint8_t>(candidate.destination_history_generation & 1U);

  candidate.reset_reason = OgreNextTaaHistoryResetReason::NONE;
  if (!history_valid_ && committed_frame_id_ == 0U) {
    candidate.reset_reason = OgreNextTaaHistoryResetReason::INITIAL_FRAME;
  } else if (explicitly_invalidated_) {
    candidate.reset_reason =
        OgreNextTaaHistoryResetReason::EXPLICIT_INVALIDATION;
  } else if (input.camera_cut) {
    candidate.reset_reason = OgreNextTaaHistoryResetReason::CAMERA_CUT;
  } else if (history_valid_ && input.view.view_id != committed_view_id_) {
    candidate.reset_reason = OgreNextTaaHistoryResetReason::VIEW_CHANGED;
  } else if (history_valid_ && (input.view.width != committed_width_ ||
                                input.view.height != committed_height_)) {
    candidate.reset_reason = OgreNextTaaHistoryResetReason::EXTENT_CHANGED;
  }

  candidate.previous_pre_exposure =
      history_valid_ ? committed_pre_exposure_ : input.pre_exposure;
  if (candidate.reset_reason == OgreNextTaaHistoryResetReason::NONE) {
    const double exposure_ratio =
        static_cast<double>(input.pre_exposure) /
        static_cast<double>(candidate.previous_pre_exposure);
    const double minimum_ratio =
        1.0 / static_cast<double>(configuration_.maximum_exposure_ratio);
    if (!std::isfinite(exposure_ratio) || exposure_ratio < minimum_ratio ||
        exposure_ratio >
            static_cast<double>(configuration_.maximum_exposure_ratio)) {
      candidate.reset_reason =
          OgreNextTaaHistoryResetReason::EXPOSURE_DISCONTINUITY;
    } else {
      candidate.history_exposure_ratio = static_cast<float>(exposure_ratio);
      if (!IsFinitePositiveNormal(candidate.history_exposure_ratio)) {
        candidate.reset_reason =
            OgreNextTaaHistoryResetReason::EXPOSURE_DISCONTINUITY;
      }
    }
  }
  candidate.history_available =
      history_valid_ &&
      candidate.reset_reason == OgreNextTaaHistoryResetReason::NONE;
  if (!candidate.history_available) {
    candidate.previous_pre_exposure = input.pre_exposure;
    candidate.history_exposure_ratio = 1.0F;
    candidate.previous_jitter_pixels = candidate.jitter_pixels;
  } else {
    candidate.previous_jitter_pixels = committed_jitter_pixels_;
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
OgreNextTaaState::PrepareCommit(const OgreNextTaaFramePlan &plan,
                                const OgreNextTaaExecutionEvidence &evidence) {
  if (!initialized_) {
    return ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH, "state",
                                     "Ogre-Next TAA state is not initialized");
  }
  if (commit_prepared_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "an Ogre-Next TAA commit is already prepared");
  }

  OgreNextTaaFrameInput input;
  input.lifecycle_epoch = plan.lifecycle_epoch;
  input.frame_id = plan.frame_id;
  input.snapshot_id = plan.snapshot_id;
  input.view = plan.view;
  input.pre_exposure = plan.current_pre_exposure;
  input.camera_cut =
      plan.reset_reason == OgreNextTaaHistoryResetReason::CAMERA_CUT;
  OgreNextTaaFramePlan expected;
  const ValidationResult planning = PrepareFrame(input, expected);
  if (!planning) {
    return planning;
  }
  if (!SamePlan(plan, expected)) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "plan",
        "TAA frame plan differs from current committed temporal lineage");
  }
  const ValidationResult evidence_validation = ValidateEvidence(plan, evidence);
  if (!evidence_validation) {
    return evidence_validation;
  }
  std::array<OgreNextTaaImageBinding, 2U> candidate_history_bindings =
      committed_history_bindings_;
  const bool persistent_history_allocations_required =
      committed_frame_id_ != 0U &&
      plan.reset_reason != OgreNextTaaHistoryResetReason::EXTENT_CHANGED;
  if (plan.history_available || persistent_history_allocations_required) {
    if (!SameBinding(evidence.history_source,
                     committed_history_bindings_[plan.source_history_slot]) ||
        !SameBinding(
            evidence.history_destination,
            committed_history_bindings_[plan.destination_history_slot])) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "evidence.history",
          "TAA history source and destination are not the exact persistent "
          "ping-pong allocations committed by preceding frames");
    }
  } else {
    candidate_history_bindings[plan.source_history_slot] =
        evidence.history_source;
    candidate_history_bindings[plan.destination_history_slot] =
        evidence.history_destination;
  }

  pending_plan_ = plan;
  pending_execution_evidence_ = evidence;
  pending_base_frame_id_ = committed_frame_id_;
  pending_base_snapshot_id_ = committed_snapshot_id_;
  pending_base_history_generation_ = history_generation_;
  pending_base_view_id_ = committed_view_id_;
  pending_base_width_ = committed_width_;
  pending_base_height_ = committed_height_;
  pending_base_jitter_pixels_ = committed_jitter_pixels_;
  pending_base_history_bindings_ = committed_history_bindings_;
  pending_history_bindings_ = candidate_history_bindings;
  pending_base_pre_exposure_ = committed_pre_exposure_;
  pending_base_history_valid_ = history_valid_;
  pending_base_explicitly_invalidated_ = explicitly_invalidated_;
  commit_prepared_ = true;
  return ValidationResult::Success();
}

bool OgreNextTaaState::CanCommitPrepared() const noexcept {
  return initialized_ && commit_prepared_ &&
         committed_frame_id_ == pending_base_frame_id_ &&
         committed_snapshot_id_ == pending_base_snapshot_id_ &&
         history_generation_ == pending_base_history_generation_ &&
         committed_view_id_ == pending_base_view_id_ &&
         committed_width_ == pending_base_width_ &&
         committed_height_ == pending_base_height_ &&
         committed_jitter_pixels_ == pending_base_jitter_pixels_ &&
         SameBinding(committed_history_bindings_[0U],
                     pending_base_history_bindings_[0U]) &&
         SameBinding(committed_history_bindings_[1U],
                     pending_base_history_bindings_[1U]) &&
         committed_pre_exposure_ == pending_base_pre_exposure_ &&
         history_valid_ == pending_base_history_valid_ &&
         explicitly_invalidated_ == pending_base_explicitly_invalidated_;
}

void OgreNextTaaState::CommitPrepared() noexcept {
  if (!CanCommitPrepared()) {
    return;
  }
  committed_frame_id_ = pending_plan_.frame_id;
  committed_plan_ = pending_plan_;
  last_execution_evidence_ = pending_execution_evidence_;
  committed_snapshot_id_ = pending_plan_.snapshot_id;
  committed_view_id_ = pending_plan_.view_id;
  history_generation_ = pending_plan_.destination_history_generation;
  committed_width_ = pending_plan_.width;
  committed_height_ = pending_plan_.height;
  committed_jitter_pixels_ = pending_plan_.jitter_pixels;
  committed_history_bindings_ = pending_history_bindings_;
  committed_pre_exposure_ = pending_plan_.current_pre_exposure;
  history_valid_ = true;
  explicitly_invalidated_ = false;
  ClearPending();
}

void OgreNextTaaState::AbortPrepared() noexcept { ClearPending(); }

ValidationResult OgreNextTaaState::InvalidateHistory() {
  if (!initialized_) {
    return ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH, "state",
                                     "Ogre-Next TAA state is not initialized");
  }
  if (commit_prepared_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "cannot invalidate TAA history while a commit is prepared");
  }
  history_valid_ = false;
  explicitly_invalidated_ = true;
  return ValidationResult::Success();
}

void OgreNextTaaState::Reset() noexcept {
  configuration_ = OgreNextTaaConfiguration{};
  lifecycle_epoch_ = 0U;
  committed_plan_ = OgreNextTaaFramePlan{};
  last_execution_evidence_ = OgreNextTaaExecutionEvidence{};
  committed_frame_id_ = 0U;
  committed_snapshot_id_ = 0U;
  committed_view_id_ = 0U;
  history_generation_ = 0U;
  committed_width_ = 0U;
  committed_height_ = 0U;
  committed_jitter_pixels_ = Float2{};
  committed_history_bindings_ = {};
  committed_pre_exposure_ = 1.0F;
  history_valid_ = false;
  explicitly_invalidated_ = false;
  ClearPending();
  initialized_ = false;
}

void OgreNextTaaState::ClearPending() noexcept {
  pending_plan_ = OgreNextTaaFramePlan{};
  pending_execution_evidence_ = OgreNextTaaExecutionEvidence{};
  pending_base_frame_id_ = 0U;
  pending_base_snapshot_id_ = 0U;
  pending_base_history_generation_ = 0U;
  pending_base_view_id_ = 0U;
  pending_base_width_ = 0U;
  pending_base_height_ = 0U;
  pending_base_jitter_pixels_ = Float2{};
  pending_base_history_bindings_ = {};
  pending_history_bindings_ = {};
  pending_base_pre_exposure_ = 1.0F;
  pending_base_history_valid_ = false;
  pending_base_explicitly_invalidated_ = false;
  commit_prepared_ = false;
}

} // namespace RoR::Render
