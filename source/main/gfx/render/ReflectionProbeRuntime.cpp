/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeRuntime.h"

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
constexpr std::uint16_t kMinimumProbeResolution = 16U;
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
  std::uint16_t count = 1U;
  while (resolution > 1U) {
    resolution = static_cast<std::uint16_t>(resolution >> 1U);
    ++count;
  }
  return count;
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

  void AddFloat3(const Float3 &value) noexcept {
    AddFloat(value.x);
    AddFloat(value.y);
    AddFloat(value.z);
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
  if (!HasRigidRightHandedAffineTransform(descriptor.render_from_probe)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "render_from_probe",
                   "probe transform must be finite, rigid, and right-handed");
  }
  const Float3 translation{descriptor.render_from_probe.elements[12U],
                           descriptor.render_from_probe.elements[13U],
                           descriptor.render_from_probe.elements[14U]};
  if (!IsBounded(translation)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "render_from_probe",
                   "probe render-relative translation exceeds the reviewed range");
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
                   "probe resolution must be a reviewed power of two from 16 to 2048");
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
  hash.AddMatrix(descriptor.render_from_probe);
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

  static std::uint64_t CaptureSeed(const ProbeState &state,
                                   std::uint64_t candidate_generation,
                                   std::uint64_t simulation_tick) noexcept {
    StableHasher hash;
    hash.AddU64(UINT64_C(0x524f525043435631));
    hash.AddU64(state.descriptor.probe_id);
    hash.AddU64(state.descriptor.content_revision);
    hash.AddU64(candidate_generation);
    hash.AddU64(simulation_tick);
    hash.AddU64(state.descriptor_fingerprint);
    const std::uint64_t value = hash.value();
    return value != 0U ? value : UINT64_C(0x524f525043435631);
  }
};

ReflectionProbeUpdateScheduler::ReflectionProbeUpdateScheduler(
    ReflectionProbeSchedulerConfiguration configuration)
    : impl_(std::make_unique<Impl>(configuration)) {}

ReflectionProbeUpdateScheduler::~ReflectionProbeUpdateScheduler() = default;

ReflectionProbePlanResult ReflectionProbeUpdateScheduler::BeginFrame(
    std::uint64_t render_frame_id, std::uint64_t simulation_tick,
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
        fingerprint != state.descriptor_fingerprint) {
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
  pending->plan.plan_id = impl_->next_plan_id++;
  pending->plan.render_frame_id = render_frame_id;
  pending->plan.simulation_tick = simulation_tick;
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
    request.deterministic_seed = Impl::CaptureSeed(
        state, request.candidate_generation, simulation_tick);
    request.reason = due_probe.reason;
    request.resolution = state.descriptor.resolution;
    request.expected_mip_count =
        RequiredMipCount(state.descriptor.resolution);
    request.descriptor = state.descriptor;
    pending->plan.requests.push_back(request);
  }
  pending->candidate_states = std::move(candidate_states);
  result.plan = pending->plan;
  result.validation = ValidationResult::Success();
  impl_->pending = std::move(pending);
  return result;
}

ReflectionProbeCommitResult ReflectionProbeUpdateScheduler::Commit(
    std::uint64_t plan_id,
    const std::vector<ReflectionProbeCaptureCompletion> &completions) {
  ReflectionProbeCommitResult result;
  if (!impl_->pending || plan_id == 0U ||
      plan_id != impl_->pending->plan.plan_id) {
    result.validation = Failure(ValidationCode::SEQUENCE_MISMATCH, "plan_id",
                                "completion does not match the pending plan");
    return result;
  }
  const std::vector<ReflectionProbeUpdateRequest> &requests =
      impl_->pending->plan.requests;
  if (completions.size() != requests.size()) {
    result.validation = Failure(
        ValidationCode::SIZE_MISMATCH, "completions",
        "completion count must exactly match the pending request count");
    return result;
  }
  for (std::size_t index = 0U; index < requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &request = requests[index];
    const ReflectionProbeCaptureCompletion &completion = completions[index];
    if (completion.probe_id != request.probe_id ||
        completion.candidate_generation != request.candidate_generation) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "completions.identity",
          "completion identity or generation differs from its request", index);
      return result;
    }
    if (completion.success) {
      if (completion.completed_face_count != request.expected_face_count ||
          completion.completed_mip_count != request.expected_mip_count ||
          completion.capture_digest == 0U) {
        result.validation = Failure(
            ValidationCode::SIZE_MISMATCH, "completions.native_receipt",
            "successful capture requires every face, mip, and a native digest",
            index);
        return result;
      }
    } else if (completion.completed_face_count != 0U ||
               completion.completed_mip_count != 0U ||
               completion.capture_digest != 0U) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "completions.failure_receipt",
          "failed capture must not publish partial faces, mips, or a digest",
          index);
      return result;
    }
  }

  for (std::size_t index = 0U; index < requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &request = requests[index];
    const ReflectionProbeCaptureCompletion &completion = completions[index];
    Impl::ProbeState &state =
        impl_->pending->candidate_states.at(request.probe_id);
    if (completion.success) {
      state.completed_generation = request.candidate_generation;
      state.completed_content_revision = request.content_revision;
      state.last_completed_simulation_tick = request.simulation_tick;
      state.capture_digest = completion.capture_digest;
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
  impl_->next_plan_id = 1U;
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
