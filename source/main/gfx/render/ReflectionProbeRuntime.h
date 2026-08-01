/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral reflection-probe admission and update scheduling.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

class ReflectionProbeCaptureReceipt;

constexpr std::uint32_t kReflectionProbeRuntimeVersion = 1U;
constexpr std::uint32_t kReflectionProbeCubemapFaceCount = 6U;

enum class ReflectionProbeUpdateMode : std::uint8_t {
  STATIC_ON_INVALIDATION = 0,
  PERIODIC_SIMULATION_TICKS = 1,
};

enum class ReflectionProbeUpdateReason : std::uint8_t {
  NEVER_CAPTURED = 0,
  CONTENT_REVISION_CHANGED = 1,
  PERIOD_ELAPSED = 2,
};

/// One oriented parallax-corrected probe in absolute simulation coordinates.
///
/// `world_from_probe_orientation` is rigid with exact zero translation: scale
/// lives only in the explicit local boxes and position is carried separately
/// in binary64. BeginFrame derives the float render-relative transform from the
/// frame origin, so large-world origin rebasing does not impersonate a probe
/// content revision or force a needless recapture.
/// The influence box must be fully contained by the correction shape so the
/// backend never relies on Ogre-Next's warning-and-clamp fallback. Capture is
/// always UI-free. A backend may raster, ray trace, or hybridize the six faces,
/// but it must publish only a complete cubemap generation.
struct ReflectionProbeRuntimeDescriptor {
  std::uint32_t version = kReflectionProbeRuntimeVersion;
  std::uint64_t probe_id = 0U;
  std::uint64_t content_revision = 1U;
  Double3 absolute_world_position_meters{};
  Matrix4x4 world_from_probe_orientation;
  Float3 capture_position_local{};
  Float3 influence_center_local{};
  Float3 influence_half_size{1.0F, 1.0F, 1.0F};
  Float3 influence_inner_fraction{0.8F, 0.8F, 0.8F};
  Float3 correction_shape_center_local{};
  Float3 correction_shape_half_size{1.0F, 1.0F, 1.0F};
  std::uint16_t priority = 1U;
  std::uint16_t resolution = 256U;
  float capture_near_meters = 0.05F;
  float capture_far_meters = 1000.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  ReflectionProbeUpdateMode update_mode =
      ReflectionProbeUpdateMode::STATIC_ON_INVALIDATION;
  /// Required to be zero for static probes and nonzero for periodic probes.
  std::uint64_t update_interval_simulation_ticks = 0U;
  bool include_dynamic_geometry = true;
};

struct ReflectionProbeSchedulerConfiguration {
  std::uint32_t version = kReflectionProbeRuntimeVersion;
  std::uint32_t maximum_live_probes = 256U;
  std::uint32_t maximum_captures_per_frame = 1U;
};

struct ReflectionProbeUpdateRequest {
  std::uint64_t probe_id = 0U;
  std::uint64_t content_revision = 0U;
  std::uint64_t candidate_generation = 0U;
  std::uint64_t simulation_tick = 0U;
  std::uint64_t deterministic_seed = 0U;
  std::uint64_t descriptor_fingerprint = 0U;
  Double3 absolute_world_origin_meters{};
  Matrix4x4 render_from_probe;
  ReflectionProbeUpdateReason reason =
      ReflectionProbeUpdateReason::NEVER_CAPTURED;
  std::uint16_t resolution = 0U;
  std::uint16_t expected_mip_count = 0U;
  std::uint32_t expected_face_count = kReflectionProbeCubemapFaceCount;
  /// Exact immutable descriptor used to configure the native capture. Keeping
  /// it in the plan prevents caller mutation after BeginFrame from changing
  /// what the candidate generation means.
  ReflectionProbeRuntimeDescriptor descriptor;
};

struct ReflectionProbeUpdatePlan {
  std::uint32_t version = kReflectionProbeRuntimeVersion;
  std::uint64_t plan_id = 0U;
  std::uint64_t render_frame_id = 0U;
  std::uint64_t simulation_tick = 0U;
  Double3 absolute_world_origin_meters{};
  std::vector<ReflectionProbeUpdateRequest> requests;
};

struct ReflectionProbePlanResult {
  ReflectionProbeUpdatePlan plan;
  ValidationResult validation;

  [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
  explicit operator bool() const noexcept { return ok(); }
};

struct ReflectionProbeCommitResult {
  ValidationResult validation;
  std::uint32_t completed_capture_count = 0U;
  std::uint32_t failed_capture_count = 0U;
  std::uint64_t committed_state_digest = 0U;

  [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
  explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] bool
IsKnownReflectionProbeUpdateMode(ReflectionProbeUpdateMode mode) noexcept;
[[nodiscard]] bool
IsKnownReflectionProbeUpdateReason(ReflectionProbeUpdateReason reason) noexcept;
[[nodiscard]] ValidationResult ValidateReflectionProbeRuntimeDescriptor(
    const ReflectionProbeRuntimeDescriptor &descriptor);
[[nodiscard]] ValidationResult ValidateReflectionProbeRuntimeSet(
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors);
[[nodiscard]] std::uint64_t ComputeReflectionProbeDescriptorFingerprint(
    const ReflectionProbeRuntimeDescriptor &descriptor) noexcept;
/// Exact semantic equality for revision enforcement. IEEE signed zero is
/// intentionally equivalent; every admitted nonzero value and categorical
/// field must otherwise match. The 64-bit fingerprint remains a digest/cache
/// key and is never trusted as collision-free identity.
[[nodiscard]] bool AreReflectionProbeRuntimeDescriptorsEquivalent(
    const ReflectionProbeRuntimeDescriptor &lhs,
    const ReflectionProbeRuntimeDescriptor &rhs) noexcept;
/// Mirrors Ogre-Next ParallaxCorrectedCubemapBase::getIblNumMipmaps:
/// max(full_chain_mips, 5) - 4. This is the owned filtered-IBL output texture
/// shape (32 => 2, 256 => 5), not the source cubemap's full raw mip chain.
[[nodiscard]] std::uint16_t ComputeReflectionProbeRequiredMipCount(
    std::uint16_t resolution) noexcept;
[[nodiscard]] std::uint64_t ComputeReflectionProbeCaptureSeed(
    const ReflectionProbeRuntimeDescriptor &descriptor,
    std::uint64_t candidate_generation,
    std::uint64_t simulation_tick) noexcept;
/// Revalidates every scheduler-derived field, including the exact PCC filtered
/// IBL mip shape, deterministic seed, render-relative transform, origin, and
/// update reason.
[[nodiscard]] ValidationResult ValidateReflectionProbeUpdateRequest(
    const ReflectionProbeUpdateRequest &request);
/// Exact semantic equality for transaction authorization. Digests are never
/// substituted for this comparison.
[[nodiscard]] bool AreReflectionProbeUpdateRequestsEquivalent(
    const ReflectionProbeUpdateRequest &lhs,
    const ReflectionProbeUpdateRequest &rhs) noexcept;

/// Stateful deterministic scheduler for expensive cubemap capture work.
///
/// BeginFrame is non-mutating with respect to committed probe lineage. Commit
/// applies the entire descriptor/tombstone transaction and advances only those
/// generations with complete native receipts. Abort drops the candidate. One
/// plan may be outstanding at a time; probe IDs retired during this scheduler
/// lifetime can never be reused. An older simulation tick is admitted only as
/// an exact live-probe-set replay; it schedules no capture and cannot lower the
/// committed simulation high-water mark.
class ReflectionProbeUpdateScheduler final {
public:
  explicit ReflectionProbeUpdateScheduler(
      ReflectionProbeSchedulerConfiguration configuration = {});
  ~ReflectionProbeUpdateScheduler();

  ReflectionProbeUpdateScheduler(const ReflectionProbeUpdateScheduler &) =
      delete;
  ReflectionProbeUpdateScheduler &
  operator=(const ReflectionProbeUpdateScheduler &) = delete;
  ReflectionProbeUpdateScheduler(ReflectionProbeUpdateScheduler &&) = delete;
  ReflectionProbeUpdateScheduler &
  operator=(ReflectionProbeUpdateScheduler &&) = delete;

  [[nodiscard]] ReflectionProbePlanResult BeginFrame(
      std::uint64_t render_frame_id, std::uint64_t simulation_tick,
      const Double3 &absolute_world_origin_meters,
      const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors);
  [[nodiscard]] ReflectionProbeCommitResult Commit(
      std::uint64_t plan_id,
      const std::vector<ReflectionProbeCaptureReceipt> &receipts);
  [[nodiscard]] ValidationResult Abort(std::uint64_t plan_id);
  /// Clears scene/tombstone/frame lineage after all caller-owned native work
  /// has been quiesced. Plan IDs remain scheduler-lifetime monotonic so a late
  /// pre-reset completion can never authenticate a post-reset transaction.
  void Reset() noexcept;

  [[nodiscard]] bool has_pending_plan() const noexcept;
  [[nodiscard]] std::uint64_t committed_state_digest() const noexcept;
  [[nodiscard]] std::uint64_t completed_generation(
      std::uint64_t probe_id) const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
