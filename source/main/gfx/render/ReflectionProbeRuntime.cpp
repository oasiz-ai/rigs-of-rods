/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeRuntime.h"

#include "ReflectionProbeCaptureReceipt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace RoR::Render {
namespace {

constexpr float kMinimumProbeHalfExtent = 0.01F;
constexpr float kMaximumProbeCoordinate = 1000000.0F;
constexpr std::uint16_t kMinimumProbeResolution = 32U;
constexpr std::uint16_t kMaximumProbeResolution = 2048U;
constexpr std::uint32_t kMaximumProbeCount = 4096U;

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool IsPositiveBounded(const Float3 &value) noexcept {
  return IsFinite(value) && value.x >= kMinimumProbeHalfExtent &&
         value.y >= kMinimumProbeHalfExtent &&
         value.z >= kMinimumProbeHalfExtent &&
         value.x <= kMaximumProbeCoordinate &&
         value.y <= kMaximumProbeCoordinate &&
         value.z <= kMaximumProbeCoordinate;
}

bool IsBounded(const Float3 &value) noexcept {
  return IsFinite(value) && std::fabs(value.x) <= kMaximumProbeCoordinate &&
         std::fabs(value.y) <= kMaximumProbeCoordinate &&
         std::fabs(value.z) <= kMaximumProbeCoordinate;
}

bool IsUnitFraction(const Float3 &value) noexcept {
  return IsFinite(value) && value.x >= 0.0F && value.x <= 1.0F &&
         value.y >= 0.0F && value.y <= 1.0F && value.z >= 0.0F &&
         value.z <= 1.0F;
}

bool IsPowerOfTwo(std::uint16_t value) noexcept {
  return value != 0U && (value & static_cast<std::uint16_t>(value - 1U)) == 0U;
}

bool StrictlyInside(const Float3 &point, const Float3 &center,
                    const Float3 &half_size) noexcept {
  return std::fabs(static_cast<double>(point.x) - center.x) < half_size.x &&
         std::fabs(static_cast<double>(point.y) - center.y) < half_size.y &&
         std::fabs(static_cast<double>(point.z) - center.z) < half_size.z;
}

bool FullyContains(const Float3 &outer_center, const Float3 &outer_half_size,
                   const Float3 &inner_center,
                   const Float3 &inner_half_size) noexcept {
  return std::fabs(static_cast<double>(inner_center.x) - outer_center.x) +
                 inner_half_size.x <=
             outer_half_size.x &&
         std::fabs(static_cast<double>(inner_center.y) - outer_center.y) +
                 inner_half_size.y <=
             outer_half_size.y &&
         std::fabs(static_cast<double>(inner_center.z) - outer_center.z) +
                 inner_half_size.z <=
             outer_half_size.z;
}

double FarthestCornerDistance(const Float3 &point, const Float3 &center,
                              const Float3 &half_size) noexcept {
  const double dx = std::fabs(static_cast<double>(point.x) - center.x) +
                    static_cast<double>(half_size.x);
  const double dy = std::fabs(static_cast<double>(point.y) - center.y) +
                    static_cast<double>(half_size.y);
  const double dz = std::fabs(static_cast<double>(point.z) - center.z) +
                    static_cast<double>(half_size.z);
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double NearestFaceDistance(const Float3 &point, const Float3 &center,
                           const Float3 &half_size) noexcept {
  const double dx = static_cast<double>(half_size.x) -
                    std::fabs(static_cast<double>(point.x) - center.x);
  const double dy = static_cast<double>(half_size.y) -
                    std::fabs(static_cast<double>(point.y) - center.y);
  const double dz = static_cast<double>(half_size.z) -
                    std::fabs(static_cast<double>(point.z) - center.z);
  return (std::min)({dx, dy, dz});
}

std::uint16_t RequiredMipCount(std::uint16_t resolution) noexcept {
  std::uint16_t full_chain_count = 1U;
  while (resolution > 1U) {
    resolution = static_cast<std::uint16_t>(resolution >> 1U);
    ++full_chain_count;
  }
  // Mirrors Ogre-Next ParallaxCorrectedCubemapBase getIblNumMipmaps exactly.
  // PCC's manual output owns only the filtered IBL levels down to 16x16; it
  // does not own the source cubemap's complete raw chain.
  return static_cast<std::uint16_t>(
      (std::max)(full_chain_count, static_cast<std::uint16_t>(5U)) - 4U);
}

class StableHasher final {
public:
  void AddByte(std::uint8_t value) noexcept {
    hash_ ^= value;
    hash_ *= UINT64_C(1099511628211);
  }

  void AddBool(bool value) noexcept { AddByte(value ? 1U : 0U); }

  void AddU16(std::uint16_t value) noexcept {
    AddByte(static_cast<std::uint8_t>(value & 0xFFU));
    AddByte(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }

  void AddU32(std::uint32_t value) noexcept {
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
      AddByte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void AddU64(std::uint64_t value) noexcept {
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
      AddByte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void AddFloat(float value) noexcept {
    if (value == 0.0F) {
      value = 0.0F;
    }
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU32(bits);
  }

  void AddDouble(double value) noexcept {
    if (value == 0.0) {
      value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU64(bits);
  }

  void AddFloat3(const Float3 &value) noexcept {
    AddFloat(value.x);
    AddFloat(value.y);
    AddFloat(value.z);
  }

  void AddDouble3(const Double3 &value) noexcept {
    AddDouble(value.x);
    AddDouble(value.y);
    AddDouble(value.z);
  }

  void AddMatrix(const Matrix4x4 &value) noexcept {
    for (const float element : value.elements) {
      AddFloat(element);
    }
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
  std::uint64_t hash_ = UINT64_C(14695981039346656037);
};

std::uint8_t ReasonRank(ReflectionProbeUpdateReason reason) noexcept {
  return static_cast<std::uint8_t>(reason);
}

} // namespace

bool IsKnownReflectionProbeUpdateMode(
    ReflectionProbeUpdateMode mode) noexcept {
  switch (mode) {
  case ReflectionProbeUpdateMode::STATIC_ON_INVALIDATION:
  case ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS:
    return true;
  }
  return false;
}

bool IsKnownReflectionProbeUpdateReason(
    ReflectionProbeUpdateReason reason) noexcept {
  switch (reason) {
  case ReflectionProbeUpdateReason::NEVER_CAPTURED:
  case ReflectionProbeUpdateReason::CONTENT_REVISION_CHANGED:
  case ReflectionProbeUpdateReason::PERIOD_ELAPSED:
    return true;
  }
  return false;
}

ValidationResult ValidateReflectionProbeRuntimeDescriptor(
    const ReflectionProbeRuntimeDescriptor &descriptor) {
  if (descriptor.version != kReflectionProbeRuntimeVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION, "version",
                   "unsupported reflection-probe runtime version");
  }
  if (descriptor.probe_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "probe_id",
                   "reflection-probe identity must be nonzero");
  }
  if (descriptor.content_revision == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH, "content_revision",
                   "reflection-probe content revision must be nonzero");
  }
  if (!IsFinite(descriptor.absolute_world_position_meters)) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "absolute_world_position_meters",
                   "absolute probe position must be finite binary64 meters");
  }
  if (!HasRigidRightHandedAffineTransform(
          descriptor.world_from_probe_orientation) ||
      descriptor.world_from_probe_orientation.elements[12U] != 0.0F ||
      descriptor.world_from_probe_orientation.elements[13U] != 0.0F ||
      descriptor.world_from_probe_orientation.elements[14U] != 0.0F) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "world_from_probe_orientation",
        "probe orientation must be finite, rigid, right-handed, and untranslated");
  }
  if (!IsBounded(descriptor.capture_position_local)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "capture_position_local",
                   "capture position must be finite and bounded");
  }
  if (!IsBounded(descriptor.influence_center_local) ||
      !IsPositiveBounded(descriptor.influence_half_size)) {
    return Failure(ValidationCode::INVALID_BOUNDS, "influence_box",
                   "influence box must be finite, bounded, and nondegenerate");
  }
  if (!IsUnitFraction(descriptor.influence_inner_fraction)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "influence_inner_fraction",
                   "inner influence fractions must be in the closed unit interval");
  }
  if (!IsBounded(descriptor.correction_shape_center_local) ||
      !IsPositiveBounded(descriptor.correction_shape_half_size)) {
    return Failure(ValidationCode::INVALID_BOUNDS, "correction_shape",
                   "correction shape must be finite, bounded, and nondegenerate");
  }
  if (!FullyContains(descriptor.correction_shape_center_local,
                     descriptor.correction_shape_half_size,
                     descriptor.influence_center_local,
                     descriptor.influence_half_size)) {
    return Failure(ValidationCode::INVALID_BOUNDS, "influence_box",
                   "influence box must be fully contained by correction shape");
  }
  if (!StrictlyInside(descriptor.capture_position_local,
                      descriptor.correction_shape_center_local,
                      descriptor.correction_shape_half_size)) {
    return Failure(ValidationCode::INVALID_BOUNDS,
                   "capture_position_local",
                   "capture position must be strictly inside correction shape");
  }
  if (!IsPowerOfTwo(descriptor.resolution) ||
      descriptor.resolution < kMinimumProbeResolution ||
      descriptor.resolution > kMaximumProbeResolution) {
    return Failure(ValidationCode::INVALID_DIMENSIONS, "resolution",
                   "probe resolution must be a reviewed power of two from 32 to 2048");
  }
  if (descriptor.priority == 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "priority",
                   "reflection-probe priority must be nonzero");
  }
  if (!IsFinite(descriptor.capture_near_meters) ||
      !IsFinite(descriptor.capture_far_meters) ||
      descriptor.capture_near_meters <= 0.0F ||
      descriptor.capture_far_meters <= descriptor.capture_near_meters) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "capture_planes",
                   "probe capture planes must be finite, positive, and ordered");
  }
  const double farthest = FarthestCornerDistance(
      descriptor.capture_position_local,
      descriptor.correction_shape_center_local,
      descriptor.correction_shape_half_size);
  if (!std::isfinite(farthest) ||
      static_cast<double>(descriptor.capture_far_meters) < farthest) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "capture_far_meters",
                   "far plane must contain the entire correction shape");
  }
  const double nearest = NearestFaceDistance(
      descriptor.capture_position_local,
      descriptor.correction_shape_center_local,
      descriptor.correction_shape_half_size);
  if (!std::isfinite(nearest) ||
      static_cast<double>(descriptor.capture_near_meters) > nearest) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "capture_near_meters",
                   "near plane must not clip the nearest correction-shape face");
  }
  if (descriptor.visibility_mask == 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "visibility_mask",
                   "probe capture visibility mask must be nonzero");
  }
  if (!IsKnownReflectionProbeUpdateMode(descriptor.update_mode)) {
    return Failure(ValidationCode::INVALID_ENUM, "update_mode",
                   "unknown reflection-probe update mode");
  }
  if (descriptor.update_mode ==
          ReflectionProbeUpdateMode::STATIC_ON_INVALIDATION &&
      descriptor.update_interval_simulation_ticks != 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "update_interval_simulation_ticks",
                   "static probes require a zero update interval");
  }
  if (descriptor.update_mode ==
          ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS &&
      descriptor.update_interval_simulation_ticks == 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "update_interval_simulation_ticks",
                   "periodic probes require a nonzero update interval");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateReflectionProbeRuntimeSet(
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors) {
  std::uint64_t previous_id = 0U;
  for (std::size_t index = 0U; index < descriptors.size(); ++index) {
    ValidationResult validation =
        ValidateReflectionProbeRuntimeDescriptor(descriptors[index]);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    if (index != 0U && descriptors[index].probe_id <= previous_id) {
      return Failure(
          descriptors[index].probe_id == previous_id
              ? ValidationCode::DUPLICATE_IDENTIFIER
              : ValidationCode::NON_DETERMINISTIC_ORDER,
          "descriptors.probe_id",
          "reflection probes must have unique strictly increasing identities",
          index);
    }
    previous_id = descriptors[index].probe_id;
  }
  return ValidationResult::Success();
}

std::uint64_t ComputeReflectionProbeDescriptorFingerprint(
    const ReflectionProbeRuntimeDescriptor &descriptor) noexcept {
  StableHasher hash;
  hash.AddU32(descriptor.version);
  hash.AddU64(descriptor.probe_id);
  hash.AddU64(descriptor.content_revision);
  hash.AddDouble3(descriptor.absolute_world_position_meters);
  hash.AddMatrix(descriptor.world_from_probe_orientation);
  hash.AddFloat3(descriptor.capture_position_local);
  hash.AddFloat3(descriptor.influence_center_local);
  hash.AddFloat3(descriptor.influence_half_size);
  hash.AddFloat3(descriptor.influence_inner_fraction);
  hash.AddFloat3(descriptor.correction_shape_center_local);
  hash.AddFloat3(descriptor.correction_shape_half_size);
  hash.AddU16(descriptor.priority);
  hash.AddU16(descriptor.resolution);
  hash.AddFloat(descriptor.capture_near_meters);
  hash.AddFloat(descriptor.capture_far_meters);
  hash.AddU32(descriptor.visibility_mask);
  hash.AddByte(static_cast<std::uint8_t>(descriptor.update_mode));
  hash.AddU64(descriptor.update_interval_simulation_ticks);
  hash.AddBool(descriptor.include_dynamic_geometry);
  return hash.value();
}

bool AreReflectionProbeRuntimeDescriptorsEquivalent(
    const ReflectionProbeRuntimeDescriptor &lhs,
    const ReflectionProbeRuntimeDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.probe_id == rhs.probe_id &&
         lhs.content_revision == rhs.content_revision &&
         lhs.absolute_world_position_meters ==
             rhs.absolute_world_position_meters &&
         lhs.world_from_probe_orientation ==
             rhs.world_from_probe_orientation &&
         lhs.capture_position_local == rhs.capture_position_local &&
         lhs.influence_center_local == rhs.influence_center_local &&
         lhs.influence_half_size == rhs.influence_half_size &&
         lhs.influence_inner_fraction == rhs.influence_inner_fraction &&
         lhs.correction_shape_center_local ==
             rhs.correction_shape_center_local &&
         lhs.correction_shape_half_size == rhs.correction_shape_half_size &&
         lhs.priority == rhs.priority && lhs.resolution == rhs.resolution &&
         lhs.capture_near_meters == rhs.capture_near_meters &&
         lhs.capture_far_meters == rhs.capture_far_meters &&
         lhs.visibility_mask == rhs.visibility_mask &&
         lhs.update_mode == rhs.update_mode &&
         lhs.update_interval_simulation_ticks ==
             rhs.update_interval_simulation_ticks &&
         lhs.include_dynamic_geometry == rhs.include_dynamic_geometry;
}

std::uint16_t ComputeReflectionProbeRequiredMipCount(
    std::uint16_t resolution) noexcept {
  return RequiredMipCount(resolution);
}

std::uint64_t ComputeReflectionProbeCaptureSeed(
    const ReflectionProbeRuntimeDescriptor &descriptor,
    std::uint64_t candidate_generation,
    std::uint64_t simulation_tick) noexcept {
  StableHasher hash;
  hash.AddU64(UINT64_C(0x524f525043435631));
  hash.AddU64(descriptor.probe_id);
  hash.AddU64(descriptor.content_revision);
  hash.AddU64(candidate_generation);
  hash.AddU64(simulation_tick);
  hash.AddU64(ComputeReflectionProbeDescriptorFingerprint(descriptor));
  const std::uint64_t value = hash.value();
  return value != 0U ? value : UINT64_C(0x524f525043435631);
}

ValidationResult ValidateReflectionProbeUpdateRequest(
    const ReflectionProbeUpdateRequest &request) {
  const ValidationResult descriptor =
      ValidateReflectionProbeRuntimeDescriptor(request.descriptor);
  if (!descriptor) {
    return Failure(descriptor.code, "request.descriptor",
                   "capture request carries an invalid probe descriptor");
  }
  if (request.probe_id == 0U ||
      request.probe_id != request.descriptor.probe_id) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "request.probe_id",
                   "capture request identity differs from its descriptor");
  }
  if (request.content_revision == 0U ||
      request.content_revision != request.descriptor.content_revision) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "request.content_revision",
                   "capture request revision differs from its descriptor");
  }
  if (request.candidate_generation == 0U) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "request.candidate_generation",
                   "capture generation must be nonzero");
  }
  const std::uint64_t expected_fingerprint =
      ComputeReflectionProbeDescriptorFingerprint(request.descriptor);
  if (expected_fingerprint == 0U ||
      request.descriptor_fingerprint != expected_fingerprint) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "request.descriptor_fingerprint",
                   "capture request descriptor fingerprint is stale");
  }
  const std::uint64_t expected_seed = ComputeReflectionProbeCaptureSeed(
      request.descriptor, request.candidate_generation,
      request.simulation_tick);
  if (request.deterministic_seed != expected_seed) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "request.deterministic_seed",
                   "capture request seed differs from its exact lineage");
  }
  if (!IsKnownReflectionProbeUpdateReason(request.reason)) {
    return Failure(ValidationCode::INVALID_ENUM, "request.reason",
                   "capture request carries an unknown update reason");
  }
  if (request.resolution != request.descriptor.resolution ||
      request.expected_face_count != kReflectionProbeCubemapFaceCount ||
      request.expected_mip_count !=
          ComputeReflectionProbeRequiredMipCount(request.resolution)) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "request.capture_shape",
                   "capture request must match Ogre-Next's PCC filtered-IBL mip layout");
  }
  if (!IsFinite(request.absolute_world_origin_meters)) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "request.absolute_world_origin_meters",
                   "capture request origin must be finite binary64 meters");
  }

  Matrix4x4 expected_transform =
      request.descriptor.world_from_probe_orientation;
  const std::array<double, 3U> relative_position{{
      request.descriptor.absolute_world_position_meters.x -
          request.absolute_world_origin_meters.x,
      request.descriptor.absolute_world_position_meters.y -
          request.absolute_world_origin_meters.y,
      request.descriptor.absolute_world_position_meters.z -
          request.absolute_world_origin_meters.z,
  }};
  for (std::size_t axis = 0U; axis < relative_position.size(); ++axis) {
    if (!std::isfinite(relative_position[axis]) ||
        std::fabs(relative_position[axis]) > kMaximumProbeCoordinate) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "request.absolute_world_origin_meters",
          "probe position is outside the reviewed render-relative range");
    }
    expected_transform.elements[12U + axis] =
        static_cast<float>(relative_position[axis]);
  }
  if (!IsFinite(request.render_from_probe) ||
      request.render_from_probe != expected_transform) {
    return Failure(
        ValidationCode::SEQUENCE_MISMATCH, "request.render_from_probe",
        "capture transform differs from the exact descriptor/origin transform");
  }
  return ValidationResult::Success();
}

bool AreReflectionProbeUpdateRequestsEquivalent(
    const ReflectionProbeUpdateRequest &lhs,
    const ReflectionProbeUpdateRequest &rhs) noexcept {
  return lhs.probe_id == rhs.probe_id &&
         lhs.content_revision == rhs.content_revision &&
         lhs.candidate_generation == rhs.candidate_generation &&
         lhs.simulation_tick == rhs.simulation_tick &&
         lhs.deterministic_seed == rhs.deterministic_seed &&
         lhs.descriptor_fingerprint == rhs.descriptor_fingerprint &&
         lhs.absolute_world_origin_meters == rhs.absolute_world_origin_meters &&
         lhs.render_from_probe == rhs.render_from_probe &&
         lhs.reason == rhs.reason && lhs.resolution == rhs.resolution &&
         lhs.expected_mip_count == rhs.expected_mip_count &&
         lhs.expected_face_count == rhs.expected_face_count &&
         AreReflectionProbeRuntimeDescriptorsEquivalent(lhs.descriptor,
                                                        rhs.descriptor);
}

class ReflectionProbeUpdateScheduler::Impl final {
public:
  struct ProbeState {
    ReflectionProbeRuntimeDescriptor descriptor;
    std::uint64_t descriptor_fingerprint = 0U;
    std::uint64_t completed_generation = 0U;
    std::uint64_t completed_content_revision = 0U;
    std::uint64_t last_completed_simulation_tick = 0U;
    std::uint64_t capture_digest = 0U;
    bool live = false;
    bool retired = false;
  };

  struct Pending {
    ReflectionProbeUpdatePlan plan;
    std::map<std::uint64_t, ProbeState> candidate_states;
  };

  explicit Impl(ReflectionProbeSchedulerConfiguration value)
      : configuration(value) {
    if (configuration.version != kReflectionProbeRuntimeVersion) {
      configuration_validation =
          Failure(ValidationCode::UNSUPPORTED_VERSION,
                  "configuration.version",
                  "unsupported reflection-probe scheduler version");
    } else if (configuration.maximum_live_probes == 0U ||
               configuration.maximum_live_probes > kMaximumProbeCount) {
      configuration_validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "configuration.maximum_live_probes",
          "maximum live probe count is outside the reviewed range");
    } else if (configuration.maximum_captures_per_frame == 0U ||
               configuration.maximum_captures_per_frame >
                   configuration.maximum_live_probes) {
      configuration_validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "configuration.maximum_captures_per_frame",
          "capture budget must be nonzero and no greater than live-probe limit");
    }
  }

  struct DueProbe {
    ProbeState *state = nullptr;
    ReflectionProbeUpdateReason reason =
        ReflectionProbeUpdateReason::NEVER_CAPTURED;
    std::uint64_t overdue_ticks = 0U;
  };

  ReflectionProbeSchedulerConfiguration configuration;
  ValidationResult configuration_validation;
  std::map<std::uint64_t, ProbeState> states;
  std::unique_ptr<Pending> pending;
  std::uint64_t next_plan_id = 1U;
  std::uint64_t last_committed_render_frame_id = 0U;
  std::uint64_t last_committed_simulation_tick = 0U;
  bool has_committed_frame = false;

  [[nodiscard]] std::uint64_t StateDigest() const noexcept {
    StableHasher hash;
    hash.AddU32(configuration.version);
    hash.AddU32(configuration.maximum_live_probes);
    hash.AddU32(configuration.maximum_captures_per_frame);
    hash.AddBool(has_committed_frame);
    hash.AddU64(last_committed_render_frame_id);
    hash.AddU64(last_committed_simulation_tick);
    hash.AddU64(static_cast<std::uint64_t>(states.size()));
    for (const auto &entry : states) {
      const ProbeState &state = entry.second;
      hash.AddU64(entry.first);
      hash.AddBool(state.live);
      hash.AddBool(state.retired);
      hash.AddU64(state.descriptor_fingerprint);
      hash.AddU64(state.completed_generation);
      hash.AddU64(state.completed_content_revision);
      hash.AddU64(state.last_completed_simulation_tick);
      hash.AddU64(state.capture_digest);
    }
    return hash.value();
  }

};

ReflectionProbeUpdateScheduler::ReflectionProbeUpdateScheduler(
    ReflectionProbeSchedulerConfiguration configuration)
    : impl_(std::make_unique<Impl>(configuration)) {}

ReflectionProbeUpdateScheduler::~ReflectionProbeUpdateScheduler() = default;

ReflectionProbePlanResult ReflectionProbeUpdateScheduler::BeginFrame(
    std::uint64_t render_frame_id, std::uint64_t simulation_tick,
    const Double3 &absolute_world_origin_meters,
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors) {
  ReflectionProbePlanResult result;
  if (!impl_->configuration_validation) {
    result.validation = impl_->configuration_validation;
    return result;
  }
  if (impl_->pending) {
    result.validation = Failure(ValidationCode::SEQUENCE_MISMATCH, "plan",
                                "a reflection-probe plan is already pending");
    return result;
  }
  if (render_frame_id == 0U) {
    result.validation = Failure(ValidationCode::INVALID_IDENTIFIER,
                                "render_frame_id",
                                "render frame identity must be nonzero");
    return result;
  }
  if (!IsFinite(absolute_world_origin_meters)) {
    result.validation = Failure(
        ValidationCode::NON_FINITE_VALUE, "absolute_world_origin_meters",
        "reflection-probe frame origin must be finite binary64 meters");
    return result;
  }
  if (impl_->has_committed_frame &&
      render_frame_id <= impl_->last_committed_render_frame_id) {
    result.validation = Failure(
        ValidationCode::SEQUENCE_MISMATCH, "render_frame_id",
        "render frame identity must advance after every committed plan");
    return result;
  }
  if (impl_->has_committed_frame &&
      simulation_tick < impl_->last_committed_simulation_tick) {
    result.validation = Failure(
        ValidationCode::SEQUENCE_MISMATCH, "simulation_tick",
        "simulation tick must not move backwards");
    return result;
  }
  ValidationResult validation =
      ValidateReflectionProbeRuntimeSet(descriptors);
  if (!validation) {
    result.validation = std::move(validation);
    return result;
  }
  if (descriptors.size() > impl_->configuration.maximum_live_probes) {
    result.validation = Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "descriptors",
        "live reflection-probe set exceeds the configured limit");
    return result;
  }

  auto candidate_states = impl_->states;
  for (auto &entry : candidate_states) {
    entry.second.live = false;
  }
  for (std::size_t index = 0U; index < descriptors.size(); ++index) {
    const ReflectionProbeRuntimeDescriptor &descriptor = descriptors[index];
    const std::uint64_t fingerprint =
        ComputeReflectionProbeDescriptorFingerprint(descriptor);
    auto current = candidate_states.find(descriptor.probe_id);
    if (current == candidate_states.end()) {
      Impl::ProbeState state;
      state.descriptor = descriptor;
      state.descriptor_fingerprint = fingerprint;
      state.live = true;
      candidate_states.emplace(descriptor.probe_id, std::move(state));
      continue;
    }
    Impl::ProbeState &state = current->second;
    if (state.retired) {
      result.validation = Failure(
          ValidationCode::INVALID_IDENTIFIER, "descriptors.probe_id",
          "a retired reflection-probe identity cannot be reused", index);
      return result;
    }
    if (descriptor.content_revision < state.descriptor.content_revision) {
      result.validation = Failure(
          ValidationCode::REVISION_MISMATCH,
          "descriptors.content_revision",
          "reflection-probe content revision moved backwards", index);
      return result;
    }
    if (descriptor.content_revision == state.descriptor.content_revision &&
        !AreReflectionProbeRuntimeDescriptorsEquivalent(descriptor,
                                                        state.descriptor)) {
      result.validation = Failure(
          ValidationCode::REVISION_MISMATCH,
          "descriptors.content_revision",
          "changed reflection-probe contents require a newer revision", index);
      return result;
    }
    state.descriptor = descriptor;
    state.descriptor_fingerprint = fingerprint;
    state.live = true;
  }
  for (auto &entry : candidate_states) {
    Impl::ProbeState &state = entry.second;
    if (!state.live && !state.retired) {
      state.retired = true;
    }
  }

  std::vector<Impl::DueProbe> due;
  due.reserve(descriptors.size());
  for (auto &entry : candidate_states) {
    Impl::ProbeState &state = entry.second;
    if (!state.live) {
      continue;
    }
    Impl::DueProbe candidate;
    candidate.state = &state;
    if (state.completed_generation == 0U) {
      candidate.reason = ReflectionProbeUpdateReason::NEVER_CAPTURED;
    } else if (state.completed_content_revision !=
               state.descriptor.content_revision) {
      candidate.reason =
          ReflectionProbeUpdateReason::CONTENT_REVISION_CHANGED;
    } else if (state.descriptor.update_mode ==
                   ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS &&
               simulation_tick >= state.last_completed_simulation_tick &&
               simulation_tick - state.last_completed_simulation_tick >=
                   state.descriptor.update_interval_simulation_ticks) {
      candidate.reason = ReflectionProbeUpdateReason::PERIOD_ELAPSED;
      candidate.overdue_ticks =
          simulation_tick - state.last_completed_simulation_tick -
          state.descriptor.update_interval_simulation_ticks;
    } else {
      continue;
    }
    if (state.completed_generation ==
        (std::numeric_limits<std::uint64_t>::max)()) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "candidate_generation",
          "reflection-probe generation is exhausted");
      return result;
    }
    due.push_back(candidate);
  }

  std::sort(due.begin(), due.end(),
            [](const Impl::DueProbe &lhs, const Impl::DueProbe &rhs) {
              const std::uint8_t lhs_rank = ReasonRank(lhs.reason);
              const std::uint8_t rhs_rank = ReasonRank(rhs.reason);
              if (lhs_rank != rhs_rank) {
                return lhs_rank < rhs_rank;
              }
              if (lhs.reason == ReflectionProbeUpdateReason::PERIOD_ELAPSED &&
                  lhs.overdue_ticks != rhs.overdue_ticks) {
                return lhs.overdue_ticks > rhs.overdue_ticks;
              }
              if (lhs.state->descriptor.priority !=
                  rhs.state->descriptor.priority) {
                return lhs.state->descriptor.priority >
                       rhs.state->descriptor.priority;
              }
              return lhs.state->descriptor.probe_id <
                     rhs.state->descriptor.probe_id;
            });

  if (impl_->next_plan_id == 0U) {
    result.validation = Failure(ValidationCode::SEQUENCE_MISMATCH, "plan_id",
                                "reflection-probe plan identity is exhausted");
    return result;
  }
  auto pending = std::make_unique<Impl::Pending>();
  pending->plan.render_frame_id = render_frame_id;
  pending->plan.simulation_tick = simulation_tick;
  pending->plan.absolute_world_origin_meters = absolute_world_origin_meters;
  const std::size_t request_count =
      (std::min)(due.size(), static_cast<std::size_t>(
                                  impl_->configuration.maximum_captures_per_frame));
  pending->plan.requests.reserve(request_count);
  for (std::size_t index = 0U; index < request_count; ++index) {
    const Impl::DueProbe &due_probe = due[index];
    const Impl::ProbeState &state = *due_probe.state;
    ReflectionProbeUpdateRequest request;
    request.probe_id = state.descriptor.probe_id;
    request.content_revision = state.descriptor.content_revision;
    request.candidate_generation = state.completed_generation + 1U;
    request.simulation_tick = simulation_tick;
    request.descriptor_fingerprint = state.descriptor_fingerprint;
    request.deterministic_seed = ComputeReflectionProbeCaptureSeed(
        state.descriptor, request.candidate_generation, simulation_tick);
    request.reason = due_probe.reason;
    request.resolution = state.descriptor.resolution;
    request.expected_mip_count =
        ComputeReflectionProbeRequiredMipCount(state.descriptor.resolution);
    request.descriptor = state.descriptor;
    request.absolute_world_origin_meters = absolute_world_origin_meters;
    request.render_from_probe = state.descriptor.world_from_probe_orientation;
    const std::array<double, 3U> relative_position{{
        state.descriptor.absolute_world_position_meters.x -
            absolute_world_origin_meters.x,
        state.descriptor.absolute_world_position_meters.y -
            absolute_world_origin_meters.y,
        state.descriptor.absolute_world_position_meters.z -
            absolute_world_origin_meters.z,
    }};
    for (std::size_t axis = 0U; axis < relative_position.size(); ++axis) {
      if (!std::isfinite(relative_position[axis]) ||
          std::fabs(relative_position[axis]) > kMaximumProbeCoordinate) {
        result.validation = Failure(
            ValidationCode::VALUE_OUT_OF_RANGE,
            "absolute_world_origin_meters",
            "probe position is outside the reviewed render-relative range");
        return result;
      }
      request.render_from_probe.elements[12U + axis] =
          static_cast<float>(relative_position[axis]);
    }
    const ValidationResult request_validation =
        ValidateReflectionProbeUpdateRequest(request);
    if (!request_validation) {
      result.validation = request_validation;
      return result;
    }
    pending->plan.requests.push_back(request);
  }
  pending->plan.plan_id = impl_->next_plan_id++;
  pending->candidate_states = std::move(candidate_states);
  result.plan = pending->plan;
  result.validation = ValidationResult::Success();
  impl_->pending = std::move(pending);
  return result;
}

ReflectionProbeCommitResult ReflectionProbeUpdateScheduler::Commit(
    std::uint64_t plan_id,
    const std::vector<ReflectionProbeCaptureReceipt> &receipts) {
  ReflectionProbeCommitResult result;
  if (!impl_->pending || plan_id == 0U ||
      plan_id != impl_->pending->plan.plan_id) {
    result.validation = Failure(ValidationCode::SEQUENCE_MISMATCH, "plan_id",
                                "completion does not match the pending plan");
    return result;
  }
  const std::vector<ReflectionProbeUpdateRequest> &requests =
      impl_->pending->plan.requests;
  if (receipts.size() != requests.size()) {
    result.validation = Failure(
        ValidationCode::SIZE_MISMATCH, "receipts",
        "receipt count must exactly match the pending request count");
    return result;
  }
  for (std::size_t index = 0U; index < requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &request = requests[index];
    const ReflectionProbeCaptureReceipt &receipt = receipts[index];
    const ReflectionProbeCaptureMipMetadata expected_mip_metadata =
        ComputeReflectionProbeCaptureMipMetadata(request.resolution);
    if (receipt.version_ != kReflectionProbeCaptureReceiptVersion) {
      result.validation = Failure(
          ValidationCode::UNSUPPORTED_VERSION, "receipts.version",
          "receipt version differs from the scheduler contract", index);
      return result;
    }
    if (receipt.plan_id_ != plan_id || receipt.request_index_ != index) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "receipts.plan_binding",
          "receipt belongs to a different plan or request slot", index);
      return result;
    }
    if (!AreReflectionProbeUpdateRequestsEquivalent(receipt.request_,
                                                     request)) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "receipts.request_binding",
          "receipt request differs from the exact pending request", index);
      return result;
    }
    if (receipt.successful_) {
      if (!receipt.adapter_authoritative_ ||
          receipt.native_execution_evidence_ == 0U ||
          !IsKnownReflectionProbeCaptureBackend(receipt.backend_) ||
          !IsKnownReflectionProbeCapturePixelFormat(receipt.pixel_format_) ||
          receipt.completed_face_count_ != request.expected_face_count ||
          receipt.completed_mip_count_ != request.expected_mip_count ||
          !AreReflectionProbeCaptureMipMetadataEquivalent(
              receipt.mip_metadata_, expected_mip_metadata) ||
          receipt.canonical_payload_bytes_ == 0U ||
          receipt.capture_digest_ == 0U) {
        result.validation = Failure(
            ValidationCode::MISSING_REFERENCE, "receipts.adapter_authority",
            "successful capture requires an authoritative concrete-adapter receipt",
            index);
        return result;
      }
    } else if (receipt.adapter_authoritative_ ||
               receipt.native_execution_evidence_ != 0U ||
               receipt.completed_face_count_ != 0U ||
               receipt.completed_mip_count_ != 0U ||
               receipt.canonical_payload_bytes_ != 0U ||
               receipt.capture_digest_ != 0U) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "receipts.failure",
          "failed capture must not carry adapter authority or measured payload",
          index);
      return result;
    }
  }

  for (std::size_t index = 0U; index < requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &request = requests[index];
    const ReflectionProbeCaptureReceipt &receipt = receipts[index];
    Impl::ProbeState &state =
        impl_->pending->candidate_states.at(request.probe_id);
    if (receipt.successful_) {
      state.completed_generation = request.candidate_generation;
      state.completed_content_revision = request.content_revision;
      state.last_completed_simulation_tick = request.simulation_tick;
      state.capture_digest = receipt.capture_digest_;
      ++result.completed_capture_count;
    } else {
      ++result.failed_capture_count;
    }
  }
  impl_->states = std::move(impl_->pending->candidate_states);
  impl_->last_committed_render_frame_id =
      impl_->pending->plan.render_frame_id;
  impl_->last_committed_simulation_tick =
      impl_->pending->plan.simulation_tick;
  impl_->has_committed_frame = true;
  impl_->pending.reset();
  result.committed_state_digest = impl_->StateDigest();
  result.validation = ValidationResult::Success();
  return result;
}

ValidationResult ReflectionProbeUpdateScheduler::Abort(std::uint64_t plan_id) {
  if (!impl_->pending || plan_id == 0U ||
      plan_id != impl_->pending->plan.plan_id) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH, "plan_id",
                   "abort does not match the pending plan");
  }
  impl_->pending.reset();
  return ValidationResult::Success();
}

void ReflectionProbeUpdateScheduler::Reset() noexcept {
  impl_->states.clear();
  impl_->pending.reset();
  // Plan identities belong to this scheduler object's entire lifetime, not
  // one scene epoch. Reusing them here would let a delayed pre-reset native
  // completion authenticate a post-reset plan with the same probe/generation
  // tuple (an ABA). Exhaustion therefore remains permanent until the scheduler
  // object itself is destroyed and replaced after native work is quiescent.
  impl_->last_committed_render_frame_id = 0U;
  impl_->last_committed_simulation_tick = 0U;
  impl_->has_committed_frame = false;
}

bool ReflectionProbeUpdateScheduler::has_pending_plan() const noexcept {
  return impl_->pending != nullptr;
}

std::uint64_t
ReflectionProbeUpdateScheduler::committed_state_digest() const noexcept {
  return impl_->StateDigest();
}

std::uint64_t ReflectionProbeUpdateScheduler::completed_generation(
    std::uint64_t probe_id) const noexcept {
  const auto state = impl_->states.find(probe_id);
  if (state == impl_->states.end() || !state->second.live) {
    return 0U;
  }
  return state->second.completed_generation;
}

} // namespace RoR::Render
