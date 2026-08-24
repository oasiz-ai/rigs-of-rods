/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeCaptureReceipt.h"
#include "ReflectionProbeCaptureTestAdapter.h"
#include "ReflectionProbeRuntime.h"
#include "ogrenext/OgreNextReflectionProbeRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace RoR::Render;
using RoR::Render::Testing::ReflectionProbeCaptureTestAdapter;

static_assert(!std::is_default_constructible<ReflectionProbeCaptureReceipt>::value,
              "raw callers must not default-construct a successful receipt");
static_assert(!std::is_aggregate<ReflectionProbeCaptureReceipt>::value,
              "raw callers must not populate receipt authority fields");
static_assert(
    !std::is_convertible<ReflectionProbeCaptureMeasurementResult,
                         ReflectionProbeCaptureReceipt>::value,
    "portable measurements must not implicitly authorize Commit");

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "reflection-probe runtime test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void TestOgreNextDeferredReadbackPacing() {
  Require(kOgreNextPccDeferredReadbackMinimumFrames == 2U,
          "native PCC readback delay changed without a contract update");
  Require(ComputeOgreNextPccEarliestReadbackFrame(41U) == 43U,
          "ordinary deferred PCC poll frame drifted");
  Require(!IsOgreNextPccReadbackPollEligible(41U, 41U) &&
              !IsOgreNextPccReadbackPollEligible(41U, 42U) &&
              IsOgreNextPccReadbackPollEligible(41U, 43U),
          "deferred PCC readback admitted a same-frame GPU wait");
  Require(ComputeOgreNextPccEarliestReadbackFrame(UINT64_MAX - 1U) ==
              UINT64_MAX &&
              !IsOgreNextPccReadbackPollEligible(UINT64_MAX - 1U,
                                                 UINT64_MAX - 1U) &&
              IsOgreNextPccReadbackPollEligible(UINT64_MAX - 1U,
                                                UINT64_MAX),
          "deferred PCC readback frame arithmetic did not saturate");
  Require(ComputeOgreNextPccReadbackLatencyFrames(41U, 45U) == 4U &&
              ComputeOgreNextPccReadbackLatencyFrames(45U, 41U) == 0U &&
              ComputeOgreNextPccReadbackLatencyFrames(0U, UINT64_MAX) ==
                  UINT32_MAX,
          "deferred PCC publication latency is not bounded and monotonic");

  const OgreNextReflectionProbeAudit audit;
  Require(audit.version == 5U && audit.deferred_capture_issue_count == 0U &&
              audit.deferred_capture_completion_count == 0U &&
              audit.last_capture_publication_frame_id == 0U &&
              audit.last_capture_readback_latency_frames == 0U,
          "native PCC audit defaults do not fail closed");
}

ReflectionProbeRuntimeDescriptor Probe(
    std::uint64_t id, std::uint16_t priority = 1U,
    ReflectionProbeUpdateMode mode =
        ReflectionProbeUpdateMode::STATIC_ON_INVALIDATION,
    std::uint64_t interval = 0U) {
  ReflectionProbeRuntimeDescriptor descriptor;
  descriptor.probe_id = id;
  descriptor.priority = priority;
  descriptor.resolution = 32U;
  descriptor.influence_half_size = {4.0F, 3.0F, 2.0F};
  descriptor.influence_inner_fraction = {0.7F, 0.8F, 0.9F};
  descriptor.correction_shape_half_size = {5.0F, 4.0F, 3.0F};
  descriptor.capture_near_meters = 0.05F;
  descriptor.capture_far_meters = 16.0F;
  descriptor.update_mode = mode;
  descriptor.update_interval_simulation_ticks = interval;
  return descriptor;
}

ReflectionProbeCaptureReceipt Complete(
    std::uint64_t plan_id, std::size_t request_index,
    const ReflectionProbeUpdateRequest &request, bool success = true) {
  if (!success) {
    return ReflectionProbeCaptureReceipt::Failed(plan_id, request_index,
                                                 request);
  }
  return ReflectionProbeCaptureTestAdapter::CaptureSynthetic(
      plan_id, request_index, request,
      ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
      UINT64_C(0x7465737400000001) + request_index);
}

std::vector<ReflectionProbeCaptureReceipt>
CompleteAll(const ReflectionProbeUpdatePlan &plan, bool success = true) {
  std::vector<ReflectionProbeCaptureReceipt> receipts;
  receipts.reserve(plan.requests.size());
  for (std::size_t index = 0U; index < plan.requests.size(); ++index) {
    receipts.push_back(
        Complete(plan.plan_id, index, plan.requests[index], success));
  }
  return receipts;
}

ReflectionProbeUpdatePlan Begin(
    ReflectionProbeUpdateScheduler &scheduler, std::uint64_t frame,
    std::uint64_t tick,
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors,
    const Double3 &absolute_world_origin_meters = {}) {
  ReflectionProbePlanResult result =
      scheduler.BeginFrame(frame, tick, absolute_world_origin_meters,
                           descriptors);
  Require(result.ok(), "valid reflection-probe frame was rejected");
  Require(result.plan.plan_id != 0U, "valid plan has a zero identity");
  Require(result.plan.render_frame_id == frame, "plan frame identity drifted");
  Require(result.plan.simulation_tick == tick, "plan simulation tick drifted");
  Require(result.plan.absolute_world_origin_meters ==
              absolute_world_origin_meters,
          "plan absolute render origin drifted");
  return result.plan;
}

void CommitAll(ReflectionProbeUpdateScheduler &scheduler,
               const ReflectionProbeUpdatePlan &plan, bool success = true) {
  const ReflectionProbeCommitResult result =
      scheduler.Commit(plan.plan_id, CompleteAll(plan, success));
  Require(result.ok(), "valid reflection-probe completion was rejected");
  Require(result.completed_capture_count ==
              (success ? plan.requests.size() : 0U),
          "completed-capture count drifted");
  Require(result.failed_capture_count ==
              (success ? 0U : plan.requests.size()),
          "failed-capture count drifted");
  Require(result.committed_state_digest ==
              scheduler.committed_state_digest(),
          "commit did not publish the returned state digest");
}

void TestDescriptorAdmissionAndFingerprint() {
  ReflectionProbeRuntimeDescriptor descriptor = Probe(7U);
  Require(ValidateReflectionProbeRuntimeDescriptor(descriptor).ok(),
          "canonical descriptor was rejected");
  const std::uint64_t baseline =
      ComputeReflectionProbeDescriptorFingerprint(descriptor);
  Require(baseline != 0U, "canonical descriptor fingerprint is zero");

  ReflectionProbeRuntimeDescriptor signed_zero = descriptor;
  signed_zero.capture_position_local.x = -0.0F;
  signed_zero.influence_center_local.y = -0.0F;
  Require(ComputeReflectionProbeDescriptorFingerprint(signed_zero) == baseline,
          "descriptor fingerprint did not canonicalize signed zero");
  Require(AreReflectionProbeRuntimeDescriptorsEquivalent(descriptor,
                                                          signed_zero),
          "signed zero changed exact descriptor semantics");

  ReflectionProbeRuntimeDescriptor changed_semantics = descriptor;
  ++changed_semantics.priority;
  Require(!AreReflectionProbeRuntimeDescriptorsEquivalent(
              descriptor, changed_semantics),
          "exact descriptor equality ignored a categorical field");
  changed_semantics = descriptor;
  changed_semantics.include_dynamic_geometry =
      !changed_semantics.include_dynamic_geometry;
  Require(!AreReflectionProbeRuntimeDescriptorsEquivalent(
              descriptor, changed_semantics),
          "exact descriptor equality ignored dynamic-geometry policy");

  descriptor.version += 1U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "unknown descriptor version was accepted");
  descriptor = Probe(7U);
  descriptor.probe_id = 0U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "zero probe identity was accepted");
  descriptor = Probe(7U);
  descriptor.content_revision = 0U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "zero probe revision was accepted");
  descriptor = Probe(7U);
  descriptor.world_from_probe_orientation.elements[0U] = 2.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "scaled probe transform was accepted");
  descriptor = Probe(7U);
  descriptor.world_from_probe_orientation.elements[0U] = -1.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "mirrored probe transform was accepted");
  descriptor = Probe(7U);
  descriptor.world_from_probe_orientation.elements[12U] = 1.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "translated probe orientation was accepted");
  descriptor = Probe(7U);
  descriptor.absolute_world_position_meters.x =
      (std::numeric_limits<double>::infinity)();
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "non-finite absolute probe position was accepted");
  descriptor = Probe(7U);
  descriptor.influence_half_size.x = 6.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "influence outside correction shape was accepted");
  descriptor = Probe(7U);
  descriptor.capture_position_local.x = 5.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "capture on correction boundary was accepted");
  descriptor = Probe(7U);
  descriptor.influence_inner_fraction.z = 1.01F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "inner influence fraction above one was accepted");
  descriptor = Probe(7U);
  descriptor.priority = 0U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "zero probe priority was accepted");
  descriptor = Probe(7U);
  descriptor.resolution = 16U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "probe resolution below Ogre PCC's reviewed minimum was accepted");
  descriptor = Probe(7U);
  descriptor.resolution = 96U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "non-power-of-two probe resolution was accepted");
  descriptor = Probe(7U);
  descriptor.capture_near_meters = 3.1F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "near plane clipping a correction face was accepted");
  descriptor = Probe(7U);
  descriptor.capture_far_meters = 2.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "far plane clipping the correction shape was accepted");
  descriptor = Probe(7U);
  descriptor.visibility_mask = 0U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "zero capture visibility mask was accepted");
  descriptor = Probe(7U);
  descriptor.update_interval_simulation_ticks = 1U;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "static probe with periodic interval was accepted");
  descriptor = Probe(7U,
                     1U,
                     ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS,
                     0U);
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "periodic probe with zero interval was accepted");
  descriptor = Probe(7U);
  descriptor.capture_position_local.x =
      (std::numeric_limits<float>::quiet_NaN)();
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "non-finite capture position was accepted");

  std::vector<ReflectionProbeRuntimeDescriptor> set{Probe(1U), Probe(2U)};
  Require(ValidateReflectionProbeRuntimeSet(set).ok(),
          "strictly sorted probe set was rejected");
  std::swap(set[0U], set[1U]);
  Require(!ValidateReflectionProbeRuntimeSet(set),
          "unsorted probe set was accepted");
  set = {Probe(1U), Probe(1U)};
  Require(!ValidateReflectionProbeRuntimeSet(set),
          "duplicate probe identity was accepted");
}

void TestInitialBudgetAndStablePriorityOrder() {
  ReflectionProbeSchedulerConfiguration configuration;
  configuration.maximum_live_probes = 8U;
  configuration.maximum_captures_per_frame = 2U;
  ReflectionProbeUpdateScheduler scheduler(configuration);
  const std::vector<ReflectionProbeRuntimeDescriptor> descriptors{
      Probe(10U, 1U), Probe(20U, 9U), Probe(30U, 5U)};

  const ReflectionProbeUpdatePlan first = Begin(scheduler, 1U, 100U, descriptors);
  Require(first.requests.size() == 2U, "initial capture budget drifted");
  Require(first.requests[0U].probe_id == 20U &&
              first.requests[1U].probe_id == 30U,
          "never-captured probes were not ordered by priority and stable ID");
  for (const ReflectionProbeUpdateRequest &request : first.requests) {
    Require(request.reason == ReflectionProbeUpdateReason::NEVER_CAPTURED,
            "initial request has the wrong reason");
    Require(request.candidate_generation == 1U,
            "initial generation did not start at one");
    Require(request.expected_face_count == 6U &&
                request.expected_mip_count == 2U,
            "32-pixel Ogre PCC filtered-IBL face/mip contract drifted");
    Require(request.descriptor.probe_id == request.probe_id &&
                request.descriptor.content_revision ==
                    request.content_revision,
            "capture request did not retain its immutable descriptor");
    Require(request.deterministic_seed != 0U,
            "capture request seed is zero");
  }
  std::vector<ReflectionProbeCaptureReceipt> first_receipts =
      CompleteAll(first);
  std::swap(first_receipts[0U], first_receipts[1U]);
  const ReflectionProbeCommitResult swapped =
      scheduler.Commit(first.plan_id, first_receipts);
  Require(!swapped && swapped.validation.field == "receipts.plan_binding" &&
              scheduler.has_pending_plan(),
          "receipt from another request slot was accepted");
  first_receipts = CompleteAll(first);
  const ReflectionProbeCommitResult committed =
      scheduler.Commit(first.plan_id, first_receipts);
  Require(committed.ok() && committed.completed_capture_count == 2U,
          "valid initial receipts did not recover after swapped rejection");
  Require(scheduler.completed_generation(20U) == 1U &&
              scheduler.completed_generation(30U) == 1U &&
              scheduler.completed_generation(10U) == 0U,
          "initial capture generations did not commit transactionally");

  const ReflectionProbeUpdatePlan second = Begin(scheduler, 2U, 101U, descriptors);
  Require(second.requests.size() == 1U &&
              second.requests[0U].probe_id == 10U,
          "uncaptured probe did not survive the first budget cut");
  CommitAll(scheduler, second);

  const ReflectionProbeUpdatePlan settled = Begin(scheduler, 3U, 102U, descriptors);
  Require(settled.requests.empty(),
          "settled static probes scheduled an unnecessary capture");
  CommitAll(scheduler, settled);
}

void TestRevisionLineageAndStaticInvalidation() {
  ReflectionProbeUpdateScheduler scheduler;
  std::vector<ReflectionProbeRuntimeDescriptor> descriptors{Probe(1U)};
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 10U, descriptors);
  CommitAll(scheduler, plan);

  descriptors[0U].capture_position_local.x = 0.25F;
  ReflectionProbePlanResult unchanged_revision =
      scheduler.BeginFrame(2U, 11U, {}, descriptors);
  Require(!unchanged_revision,
          "changed descriptor with unchanged revision was accepted");
  Require(!scheduler.has_pending_plan(),
          "failed descriptor admission created a pending plan");
  Require(scheduler.completed_generation(1U) == 1U,
          "failed descriptor admission changed committed generation");

  descriptors[0U].content_revision = 2U;
  plan = Begin(scheduler, 2U, 11U, descriptors);
  Require(plan.requests.size() == 1U &&
              plan.requests[0U].reason ==
                  ReflectionProbeUpdateReason::CONTENT_REVISION_CHANGED &&
              plan.requests[0U].candidate_generation == 2U,
          "new content revision did not schedule the next generation");
  CommitAll(scheduler, plan);
  Require(scheduler.completed_generation(1U) == 2U,
          "content revision capture did not commit generation two");

  descriptors[0U].content_revision = 1U;
  Require(!scheduler.BeginFrame(3U, 12U, {}, descriptors),
          "backwards content revision was accepted");
}

void TestPeriodicTicksAndOverdueFairness() {
  ReflectionProbeSchedulerConfiguration configuration;
  configuration.maximum_live_probes = 4U;
  configuration.maximum_captures_per_frame = 2U;
  ReflectionProbeUpdateScheduler scheduler(configuration);
  std::vector<ReflectionProbeRuntimeDescriptor> descriptors{
      Probe(1U, 100U,
            ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS, 10U),
      Probe(2U, 1U,
            ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS, 15U)};
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 100U, descriptors);
  CommitAll(scheduler, plan);

  plan = Begin(scheduler, 2U, 109U, descriptors);
  Require(plan.requests.empty(), "periodic probe ran before its exact tick");
  CommitAll(scheduler, plan);

  plan = Begin(scheduler, 3U, 110U, descriptors);
  Require(plan.requests.size() == 1U && plan.requests[0U].probe_id == 1U,
          "ten-tick probe did not run on its exact due tick");
  CommitAll(scheduler, plan);

  plan = Begin(scheduler, 4U, 121U, descriptors);
  Require(plan.requests.size() == 2U && plan.requests[0U].probe_id == 2U &&
              plan.requests[1U].probe_id == 1U,
          "periodic ordering did not prefer the more overdue probe");
  CommitAll(scheduler, plan);
}

void TestFailureAbortAndTransactionalCompletion() {
  ReflectionProbeUpdateScheduler scheduler;
  const std::vector<ReflectionProbeRuntimeDescriptor> descriptors{Probe(1U)};
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 50U, descriptors);
  const std::uint64_t first_seed = plan.requests[0U].deterministic_seed;
  CommitAll(scheduler, plan, false);
  Require(scheduler.completed_generation(1U) == 0U,
          "failed capture advanced its generation");

  plan = Begin(scheduler, 2U, 51U, descriptors);
  Require(plan.requests[0U].candidate_generation == 1U,
          "failed capture skipped a generation on retry");
  Require(plan.requests[0U].deterministic_seed != first_seed,
          "capture seed ignored the new simulation tick");
  const ReflectionProbeUpdateRequest retry = plan.requests[0U];
  const std::uint64_t aborted_plan_id = plan.plan_id;
  const ReflectionProbeCaptureReceipt delayed_aborted =
      Complete(aborted_plan_id, 0U, retry);
  Require(scheduler.Abort(aborted_plan_id).ok(), "valid plan abort failed");
  Require(!scheduler.has_pending_plan(), "abort left a pending plan");

  plan = Begin(scheduler, 2U, 51U, descriptors);
  Require(plan.plan_id != aborted_plan_id,
          "aborted plan identity was reused");
  Require(plan.requests[0U].deterministic_seed == retry.deterministic_seed &&
              plan.requests[0U].candidate_generation ==
                  retry.candidate_generation,
          "same-frame replan changed deterministic capture contents");

  const std::uint64_t digest_before = scheduler.committed_state_digest();
  ReflectionProbeCommitResult rejected =
      scheduler.Commit(plan.plan_id, {delayed_aborted});
  Require(!rejected &&
              rejected.validation.code == ValidationCode::SEQUENCE_MISMATCH,
          "receipt from an aborted plan authenticated its identical replan");
  Require(scheduler.has_pending_plan(),
          "stale receipt destroyed the pending plan");
  Require(scheduler.committed_state_digest() == digest_before &&
              scheduler.completed_generation(1U) == 0U,
          "stale receipt changed committed state");

  ReflectionProbeUpdateRequest altered = plan.requests[0U];
  ++altered.descriptor.priority;
  altered.descriptor_fingerprint =
      ComputeReflectionProbeDescriptorFingerprint(altered.descriptor);
  altered.deterministic_seed = ComputeReflectionProbeCaptureSeed(
      altered.descriptor, altered.candidate_generation,
      altered.simulation_tick);
  const ReflectionProbeCaptureReceipt altered_receipt =
      ReflectionProbeCaptureTestAdapter::CaptureSynthetic(
          plan.plan_id, 0U, altered,
          ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
          UINT64_C(0x74657374000000aa));
  rejected = scheduler.Commit(plan.plan_id, {altered_receipt});
  Require(!rejected &&
              rejected.validation.field == "receipts.request_binding",
          "exactly different descriptor receipt authenticated pending state");

  altered = plan.requests[0U];
  altered.absolute_world_origin_meters.x += 1.0;
  altered.render_from_probe.elements[12U] -= 1.0F;
  const ReflectionProbeCaptureReceipt altered_origin_receipt =
      ReflectionProbeCaptureTestAdapter::CaptureSynthetic(
          plan.plan_id, 0U, altered,
          ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
          UINT64_C(0x74657374000000ab));
  rejected = scheduler.Commit(plan.plan_id, {altered_origin_receipt});
  Require(!rejected &&
              rejected.validation.field == "receipts.request_binding",
          "different origin/transform receipt authenticated pending state");

  altered = plan.requests[0U];
  altered.reason = ReflectionProbeUpdateReason::PERIOD_ELAPSED;
  const ReflectionProbeCaptureReceipt altered_reason_receipt =
      ReflectionProbeCaptureTestAdapter::CaptureSynthetic(
          plan.plan_id, 0U, altered,
          ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
          UINT64_C(0x74657374000000ac));
  rejected = scheduler.Commit(plan.plan_id, {altered_reason_receipt});
  Require(!rejected &&
              rejected.validation.field == "receipts.request_binding",
          "different update-reason receipt authenticated pending state");
  Require(scheduler.has_pending_plan(),
          "altered-request rejection destroyed the pending plan");

  CommitAll(scheduler, plan);
  Require(scheduler.completed_generation(1U) == 1U,
          "valid retry did not advance generation");
}

void TestRetirementFrameAndTickLineage() {
  ReflectionProbeUpdateScheduler scheduler;
  std::vector<ReflectionProbeRuntimeDescriptor> descriptors{Probe(9U)};
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 100U, descriptors);
  CommitAll(scheduler, plan);

  Require(!scheduler.BeginFrame(1U, 100U, {}, descriptors),
          "committed render frame identity was reused");
  ReflectionProbeUpdatePlan replay = Begin(scheduler, 2U, 99U, descriptors);
  Require(replay.requests.empty(),
          "exact historical snapshot replay scheduled a capture");
  CommitAll(scheduler, replay);
  Require(scheduler.completed_generation(9U) == 1U,
          "historical snapshot replay changed capture generation");

  const std::uint64_t replay_digest = scheduler.committed_state_digest();
  ReflectionProbeRuntimeDescriptor changed = descriptors.front();
  ++changed.content_revision;
  Require(!scheduler.BeginFrame(3U, 99U, {}, {changed}),
          "historical snapshot replay changed probe contents");
  Require(!scheduler.BeginFrame(3U, 99U, {}, {}),
          "historical snapshot replay retired a live probe");
  Require(!scheduler.has_pending_plan() &&
              scheduler.committed_state_digest() == replay_digest &&
              scheduler.completed_generation(9U) == 1U,
          "rejected historical replay mutated committed probe lineage");

  plan = Begin(scheduler, 3U, 100U, {});
  Require(plan.requests.empty(), "probe retirement scheduled a capture");
  CommitAll(scheduler, plan);
  Require(scheduler.completed_generation(9U) == 0U,
          "retired probe remained queryable as live");
  Require(!scheduler.BeginFrame(4U, 101U, {}, descriptors),
          "retired probe identity was reused");

  scheduler.Reset();
  plan = Begin(scheduler, 1U, 0U, descriptors);
  Require(plan.requests.size() == 1U,
          "reset did not clear tombstones and frame lineage");
  CommitAll(scheduler, plan);
}

void TestHistoricalReplayDoesNotRetryFailedPeriodicCapture() {
  ReflectionProbeUpdateScheduler scheduler;
  const ReflectionProbeRuntimeDescriptor periodic = Probe(
      10U, 1U, ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS, 10U);
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 100U, {periodic});
  CommitAll(scheduler, plan);

  plan = Begin(scheduler, 2U, 120U, {periodic});
  Require(plan.requests.size() == 1U,
          "periodic capture was not due at the live high-water tick");
  CommitAll(scheduler, plan, false);
  Require(scheduler.completed_generation(10U) == 1U,
          "failed periodic capture advanced the generation");

  plan = Begin(scheduler, 3U, 115U, {periodic});
  Require(plan.requests.empty(),
          "historical replay retried failed periodic capture work");
  CommitAll(scheduler, plan);
  plan = Begin(scheduler, 4U, 116U, {periodic});
  Require(plan.requests.empty(),
          "historical replay lowered the simulation high-water mark");
  CommitAll(scheduler, plan);

  plan = Begin(scheduler, 5U, 120U, {periodic});
  Require(plan.requests.size() == 1U &&
              plan.requests.front().candidate_generation == 2U,
          "live high-water retry lost periodic capture lineage");
  CommitAll(scheduler, plan);
}

void TestResetNeverReusesPlanIdentity() {
  ReflectionProbeUpdateScheduler scheduler;
  ReflectionProbeRuntimeDescriptor before = Probe(11U);
  const ReflectionProbeUpdatePlan old_plan =
      Begin(scheduler, 1U, 10U, {before});
  const std::vector<ReflectionProbeCaptureReceipt> delayed =
      CompleteAll(old_plan);

  scheduler.Reset();
  const ReflectionProbeUpdatePlan new_plan =
      Begin(scheduler, 1U, 10U, {before});
  Require(new_plan.plan_id != old_plan.plan_id &&
              new_plan.requests.size() == 1U &&
              new_plan.requests[0U].candidate_generation == 1U,
          "reset reused a scheduler-lifetime plan identity");
  const ReflectionProbeCommitResult stale =
      scheduler.Commit(new_plan.plan_id, delayed);
  Require(!stale && stale.validation.code == ValidationCode::SEQUENCE_MISMATCH &&
              stale.validation.field == "receipts.plan_binding" &&
              scheduler.has_pending_plan() &&
              scheduler.completed_generation(before.probe_id) == 0U,
          "delayed pre-reset completion authenticated post-reset state");
  CommitAll(scheduler, new_plan);
  Require(scheduler.completed_generation(before.probe_id) == 1U,
          "valid post-reset completion did not recover after stale rejection");
}

void TestCommittedReceiptCannotReplay() {
  ReflectionProbeUpdateScheduler scheduler;
  const ReflectionProbeRuntimeDescriptor descriptor = Probe(
      12U, 1U, ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS, 1U);
  const ReflectionProbeUpdatePlan first =
      Begin(scheduler, 1U, 10U, {descriptor});
  const std::vector<ReflectionProbeCaptureReceipt> first_receipts =
      CompleteAll(first);
  const ReflectionProbeCommitResult first_commit =
      scheduler.Commit(first.plan_id, first_receipts);
  Require(first_commit.ok() && scheduler.completed_generation(12U) == 1U,
          "initial replay fixture did not commit");

  const ReflectionProbeUpdatePlan second =
      Begin(scheduler, 2U, 11U, {descriptor});
  const std::uint64_t before_replay = scheduler.committed_state_digest();
  const ReflectionProbeCommitResult replay =
      scheduler.Commit(second.plan_id, first_receipts);
  Require(!replay && replay.validation.field == "receipts.plan_binding" &&
              scheduler.has_pending_plan() &&
              scheduler.completed_generation(12U) == 1U &&
              scheduler.committed_state_digest() == before_replay,
          "committed receipt replay advanced or destroyed pending state");
  CommitAll(scheduler, second);
  Require(scheduler.completed_generation(12U) == 2U,
          "valid receipt did not recover after replay rejection");
}

void TestLargeWorldOriginRebasing() {
  ReflectionProbeUpdateScheduler scheduler;
  ReflectionProbeRuntimeDescriptor descriptor = Probe(77U);
  descriptor.absolute_world_position_meters = {
      1000000012.5, -1999999996.0, 3000000002.0};
  const Double3 first_origin{1000000000.0, -2000000000.0, 3000000000.0};
  ReflectionProbeUpdatePlan plan =
      Begin(scheduler, 1U, 100U, {descriptor}, first_origin);
  Require(plan.requests.size() == 1U,
          "large-world probe did not schedule its initial capture");
  const Matrix4x4 &first_transform = plan.requests[0U].render_from_probe;
  Require(first_transform.elements[12U] == 12.5F &&
              first_transform.elements[13U] == 4.0F &&
              first_transform.elements[14U] == 2.0F,
          "large-world probe did not derive exact render-relative translation");
  Require(plan.requests[0U].absolute_world_origin_meters == first_origin,
          "capture request lost its exact binary64 render origin");
  CommitAll(scheduler, plan);

  const Double3 rebased_origin{1000000010.0, -1999999998.0, 3000000001.0};
  plan = Begin(scheduler, 2U, 101U, {descriptor}, rebased_origin);
  Require(plan.requests.empty(),
          "render-origin rebase impersonated a static content revision");
  CommitAll(scheduler, plan);

  ++descriptor.content_revision;
  descriptor.capture_position_local.x = 0.25F;
  plan = Begin(scheduler, 3U, 102U, {descriptor}, rebased_origin);
  Require(plan.requests.size() == 1U &&
              plan.requests[0U].render_from_probe.elements[12U] == 2.5F &&
              plan.requests[0U].render_from_probe.elements[13U] == 2.0F &&
              plan.requests[0U].render_from_probe.elements[14U] == 1.0F,
          "rebased revision capture used the wrong relative transform");
  CommitAll(scheduler, plan);

  ReflectionProbePlanResult nonfinite = scheduler.BeginFrame(
      4U, 103U,
      {(std::numeric_limits<double>::quiet_NaN)(), 0.0, 0.0},
      {descriptor});
  Require(!nonfinite && !scheduler.has_pending_plan(),
          "non-finite frame origin was accepted");

  ReflectionProbeRuntimeDescriptor distant = Probe(78U);
  distant.absolute_world_position_meters = {2000000.0, 0.0, 0.0};
  ReflectionProbePlanResult outside =
      scheduler.BeginFrame(4U, 103U, {}, {descriptor, distant});
  Require(!outside && !scheduler.has_pending_plan(),
          "unrepresentable render-relative probe position was accepted");
}

void TestConfigurationAndCapacityFailures() {
  ReflectionProbeSchedulerConfiguration invalid;
  invalid.maximum_live_probes = 0U;
  ReflectionProbeUpdateScheduler bad_scheduler(invalid);
  Require(!bad_scheduler.BeginFrame(1U, 0U, {}, {}),
          "zero-capacity scheduler accepted a frame");

  ReflectionProbeSchedulerConfiguration configuration;
  configuration.maximum_live_probes = 1U;
  configuration.maximum_captures_per_frame = 1U;
  ReflectionProbeUpdateScheduler scheduler(configuration);
  Require(!scheduler.BeginFrame(1U, 0U, {}, {Probe(1U), Probe(2U)}),
          "live-probe capacity overflow was accepted");
  Require(!scheduler.BeginFrame(0U, 0U, {}, {Probe(1U)}),
          "zero render frame identity was accepted");
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 0U, {Probe(1U)});
  Require(!scheduler.BeginFrame(2U, 1U, {}, {Probe(1U)}),
          "second plan began while one was pending");
  Require(!scheduler.Commit(plan.plan_id + 1U, CompleteAll(plan)),
          "wrong plan identity was committed");
  Require(scheduler.has_pending_plan(),
          "wrong-plan rejection discarded the valid pending plan");
  CommitAll(scheduler, plan);
}

std::uint64_t NextRandom(std::uint64_t &state) noexcept {
  state ^= state >> 12U;
  state ^= state << 25U;
  state ^= state >> 27U;
  return state * UINT64_C(2685821657736338717);
}

void RequireSamePlan(const ReflectionProbeUpdatePlan &lhs,
                     const ReflectionProbeUpdatePlan &rhs) {
  Require(lhs.render_frame_id == rhs.render_frame_id &&
              lhs.simulation_tick == rhs.simulation_tick &&
              lhs.absolute_world_origin_meters ==
                  rhs.absolute_world_origin_meters &&
              lhs.requests.size() == rhs.requests.size(),
          "replayed plan header diverged");
  for (std::size_t index = 0U; index < lhs.requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &a = lhs.requests[index];
    const ReflectionProbeUpdateRequest &b = rhs.requests[index];
    Require(AreReflectionProbeUpdateRequestsEquivalent(a, b),
            "replayed capture request diverged");
  }
}

void TestFixedSeedReplayDeterminism() {
  ReflectionProbeSchedulerConfiguration configuration;
  configuration.maximum_live_probes = 16U;
  configuration.maximum_captures_per_frame = 3U;
  ReflectionProbeUpdateScheduler first(configuration);
  ReflectionProbeUpdateScheduler second(configuration);
  std::vector<ReflectionProbeRuntimeDescriptor> descriptors;
  for (std::uint64_t id = 1U; id <= 8U; ++id) {
    descriptors.push_back(Probe(
        id, static_cast<std::uint16_t>(1U + id * 3U),
        ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS, 5U + id));
  }

  std::uint64_t random = UINT64_C(0x726f725f70636331);
  std::uint64_t tick = 1000U;
  for (std::uint64_t frame = 1U; frame <= 500U; ++frame) {
    tick += NextRandom(random) % 4U;
    if ((NextRandom(random) & 31U) == 0U) {
      const std::size_t index =
          static_cast<std::size_t>(NextRandom(random) % descriptors.size());
      ++descriptors[index].content_revision;
      descriptors[index].capture_position_local.x =
          static_cast<float>(descriptors[index].content_revision % 3U) * 0.1F;
    }
    ReflectionProbeUpdatePlan a = Begin(first, frame, tick, descriptors);
    ReflectionProbeUpdatePlan b = Begin(second, frame, tick, descriptors);
    RequireSamePlan(a, b);

    std::vector<ReflectionProbeCaptureReceipt> completions_a;
    std::vector<ReflectionProbeCaptureReceipt> completions_b;
    for (std::size_t index = 0U; index < a.requests.size(); ++index) {
      const bool success = (NextRandom(random) & 7U) != 0U;
      completions_a.push_back(
          Complete(a.plan_id, index, a.requests[index], success));
    }
    for (std::size_t index = 0U; index < b.requests.size(); ++index) {
      completions_b.push_back(
          Complete(b.plan_id, index, b.requests[index],
                   completions_a[index].successful()));
    }
    ReflectionProbeCommitResult committed_a =
        first.Commit(a.plan_id, completions_a);
    ReflectionProbeCommitResult committed_b =
        second.Commit(b.plan_id, completions_b);
    Require(committed_a.ok() && committed_b.ok(),
            "replay completion was rejected");
    Require(committed_a.completed_capture_count ==
                committed_b.completed_capture_count &&
                committed_a.failed_capture_count ==
                    committed_b.failed_capture_count &&
                committed_a.committed_state_digest ==
                    committed_b.committed_state_digest &&
                first.committed_state_digest() ==
                    second.committed_state_digest(),
            "fixed-seed scheduler replay state diverged");
  }
}

} // namespace

int main() {
  TestOgreNextDeferredReadbackPacing();
  TestDescriptorAdmissionAndFingerprint();
  TestInitialBudgetAndStablePriorityOrder();
  TestRevisionLineageAndStaticInvalidation();
  TestPeriodicTicksAndOverdueFairness();
  TestFailureAbortAndTransactionalCompletion();
  TestRetirementFrameAndTickLineage();
  TestHistoricalReplayDoesNotRetryFailedPeriodicCapture();
  TestResetNeverReusesPlanIdentity();
  TestCommittedReceiptCannotReplay();
  TestLargeWorldOriginRebasing();
  TestConfigurationAndCapacityFailures();
  TestFixedSeedReplayDeterminism();
  std::cout << "renderer-neutral reflection-probe runtime tests passed\n";
  return EXIT_SUCCESS;
}
