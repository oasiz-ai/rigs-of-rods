/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "reflection-probe runtime test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

ReflectionProbeRuntimeDescriptor Probe(
    std::uint64_t id, std::uint16_t priority = 1U,
    ReflectionProbeUpdateMode mode =
        ReflectionProbeUpdateMode::STATIC_ON_INVALIDATION,
    std::uint64_t interval = 0U) {
  ReflectionProbeRuntimeDescriptor descriptor;
  descriptor.probe_id = id;
  descriptor.priority = priority;
  descriptor.resolution = 16U;
  descriptor.influence_half_size = {4.0F, 3.0F, 2.0F};
  descriptor.influence_inner_fraction = {0.7F, 0.8F, 0.9F};
  descriptor.correction_shape_half_size = {5.0F, 4.0F, 3.0F};
  descriptor.capture_near_meters = 0.05F;
  descriptor.capture_far_meters = 16.0F;
  descriptor.update_mode = mode;
  descriptor.update_interval_simulation_ticks = interval;
  return descriptor;
}

ReflectionProbeCaptureCompletion
Complete(const ReflectionProbeUpdateRequest &request, bool success = true) {
  ReflectionProbeCaptureCompletion completion;
  completion.probe_id = request.probe_id;
  completion.candidate_generation = request.candidate_generation;
  completion.success = success;
  if (success) {
    completion.completed_face_count = request.expected_face_count;
    completion.completed_mip_count = request.expected_mip_count;
    completion.capture_digest =
        request.deterministic_seed ^ UINT64_C(0x9e3779b97f4a7c15);
    if (completion.capture_digest == 0U) {
      completion.capture_digest = 1U;
    }
  }
  return completion;
}

std::vector<ReflectionProbeCaptureCompletion>
CompleteAll(const ReflectionProbeUpdatePlan &plan, bool success = true) {
  std::vector<ReflectionProbeCaptureCompletion> completions;
  completions.reserve(plan.requests.size());
  for (const ReflectionProbeUpdateRequest &request : plan.requests) {
    completions.push_back(Complete(request, success));
  }
  return completions;
}

ReflectionProbeUpdatePlan Begin(
    ReflectionProbeUpdateScheduler &scheduler, std::uint64_t frame,
    std::uint64_t tick,
    const std::vector<ReflectionProbeRuntimeDescriptor> &descriptors) {
  ReflectionProbePlanResult result =
      scheduler.BeginFrame(frame, tick, descriptors);
  Require(result.ok(), "valid reflection-probe frame was rejected");
  Require(result.plan.plan_id != 0U, "valid plan has a zero identity");
  Require(result.plan.render_frame_id == frame, "plan frame identity drifted");
  Require(result.plan.simulation_tick == tick, "plan simulation tick drifted");
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
  descriptor.render_from_probe.elements[0U] = 2.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "scaled probe transform was accepted");
  descriptor = Probe(7U);
  descriptor.render_from_probe.elements[0U] = -1.0F;
  Require(!ValidateReflectionProbeRuntimeDescriptor(descriptor),
          "mirrored probe transform was accepted");
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
                request.expected_mip_count == 5U,
            "16-pixel cubemap face/mip contract drifted");
    Require(request.descriptor.probe_id == request.probe_id &&
                request.descriptor.content_revision ==
                    request.content_revision,
            "capture request did not retain its immutable descriptor");
    Require(request.deterministic_seed != 0U,
            "capture request seed is zero");
  }
  CommitAll(scheduler, first);
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
      scheduler.BeginFrame(2U, 11U, descriptors);
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
  Require(!scheduler.BeginFrame(3U, 12U, descriptors),
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
  Require(scheduler.Abort(aborted_plan_id).ok(), "valid plan abort failed");
  Require(!scheduler.has_pending_plan(), "abort left a pending plan");

  plan = Begin(scheduler, 2U, 51U, descriptors);
  Require(plan.plan_id != aborted_plan_id,
          "aborted plan identity was reused");
  Require(plan.requests[0U].deterministic_seed == retry.deterministic_seed &&
              plan.requests[0U].candidate_generation ==
                  retry.candidate_generation,
          "same-frame replan changed deterministic capture contents");

  ReflectionProbeCaptureCompletion malformed = Complete(plan.requests[0U]);
  malformed.completed_face_count = 5U;
  const std::uint64_t digest_before = scheduler.committed_state_digest();
  ReflectionProbeCommitResult rejected =
      scheduler.Commit(plan.plan_id, {malformed});
  Require(!rejected, "partial successful cubemap was accepted");
  Require(scheduler.has_pending_plan(),
          "malformed completion destroyed the pending plan");
  Require(scheduler.committed_state_digest() == digest_before &&
              scheduler.completed_generation(1U) == 0U,
          "malformed completion changed committed state");

  malformed = Complete(plan.requests[0U], false);
  malformed.capture_digest = 99U;
  rejected = scheduler.Commit(plan.plan_id, {malformed});
  Require(!rejected, "failed capture published a digest");
  Require(scheduler.has_pending_plan(),
          "failed-receipt rejection destroyed the pending plan");

  CommitAll(scheduler, plan);
  Require(scheduler.completed_generation(1U) == 1U,
          "valid retry did not advance generation");
}

void TestRetirementFrameAndTickLineage() {
  ReflectionProbeUpdateScheduler scheduler;
  std::vector<ReflectionProbeRuntimeDescriptor> descriptors{Probe(9U)};
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 100U, descriptors);
  CommitAll(scheduler, plan);

  Require(!scheduler.BeginFrame(1U, 100U, descriptors),
          "committed render frame identity was reused");
  Require(!scheduler.BeginFrame(2U, 99U, descriptors),
          "simulation tick moved backwards");

  plan = Begin(scheduler, 2U, 100U, {});
  Require(plan.requests.empty(), "probe retirement scheduled a capture");
  CommitAll(scheduler, plan);
  Require(scheduler.completed_generation(9U) == 0U,
          "retired probe remained queryable as live");
  Require(!scheduler.BeginFrame(3U, 101U, descriptors),
          "retired probe identity was reused");

  scheduler.Reset();
  plan = Begin(scheduler, 1U, 0U, descriptors);
  Require(plan.requests.size() == 1U,
          "reset did not clear tombstones and frame lineage");
  CommitAll(scheduler, plan);
}

void TestConfigurationAndCapacityFailures() {
  ReflectionProbeSchedulerConfiguration invalid;
  invalid.maximum_live_probes = 0U;
  ReflectionProbeUpdateScheduler bad_scheduler(invalid);
  Require(!bad_scheduler.BeginFrame(1U, 0U, {}),
          "zero-capacity scheduler accepted a frame");

  ReflectionProbeSchedulerConfiguration configuration;
  configuration.maximum_live_probes = 1U;
  configuration.maximum_captures_per_frame = 1U;
  ReflectionProbeUpdateScheduler scheduler(configuration);
  Require(!scheduler.BeginFrame(1U, 0U, {Probe(1U), Probe(2U)}),
          "live-probe capacity overflow was accepted");
  Require(!scheduler.BeginFrame(0U, 0U, {Probe(1U)}),
          "zero render frame identity was accepted");
  ReflectionProbeUpdatePlan plan = Begin(scheduler, 1U, 0U, {Probe(1U)});
  Require(!scheduler.BeginFrame(2U, 1U, {Probe(1U)}),
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
              lhs.requests.size() == rhs.requests.size(),
          "replayed plan header diverged");
  for (std::size_t index = 0U; index < lhs.requests.size(); ++index) {
    const ReflectionProbeUpdateRequest &a = lhs.requests[index];
    const ReflectionProbeUpdateRequest &b = rhs.requests[index];
    Require(a.probe_id == b.probe_id &&
                a.content_revision == b.content_revision &&
                a.candidate_generation == b.candidate_generation &&
                a.simulation_tick == b.simulation_tick &&
                a.deterministic_seed == b.deterministic_seed &&
                a.descriptor_fingerprint == b.descriptor_fingerprint &&
                a.reason == b.reason && a.resolution == b.resolution &&
                a.expected_mip_count == b.expected_mip_count &&
                a.expected_face_count == b.expected_face_count &&
                ComputeReflectionProbeDescriptorFingerprint(a.descriptor) ==
                    ComputeReflectionProbeDescriptorFingerprint(b.descriptor),
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

    std::vector<ReflectionProbeCaptureCompletion> completions_a;
    std::vector<ReflectionProbeCaptureCompletion> completions_b;
    for (const ReflectionProbeUpdateRequest &request : a.requests) {
      const bool success = (NextRandom(random) & 7U) != 0U;
      completions_a.push_back(Complete(request, success));
    }
    for (std::size_t index = 0U; index < b.requests.size(); ++index) {
      completions_b.push_back(
          Complete(b.requests[index], completions_a[index].success));
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
  TestDescriptorAdmissionAndFingerprint();
  TestInitialBudgetAndStablePriorityOrder();
  TestRevisionLineageAndStaticInvalidation();
  TestPeriodicTicksAndOverdueFairness();
  TestFailureAbortAndTransactionalCompletion();
  TestRetirementFrameAndTickLineage();
  TestConfigurationAndCapacityFailures();
  TestFixedSeedReplayDeterminism();
  std::cout << "renderer-neutral reflection-probe runtime tests passed\n";
  return EXIT_SUCCESS;
}
