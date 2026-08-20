/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextHdrTemporalContract.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Ogre-Next HDR temporal contract test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Matrix4x4 Projection(float near_plane = 0.1F,
                     float far_plane = 1000.0F) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  projection.elements[10U] =
      far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

std::shared_ptr<const SceneSnapshot>
Scene(std::uint64_t snapshot_id, double simulation_time_seconds,
      float exposure_compensation_ev = 0.0F) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = 77U;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = simulation_time_seconds;
  descriptor.environment.exposure_compensation_ev =
      exposure_compensation_ev;
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "test scene failed renderer-neutral validation");
  return created.snapshot;
}

RenderFrameRequest Frame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene,
    float view_exposure = 1.0F,
    PixelFormat format = PixelFormat::RGBA8_SRGB) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = false;
  request.color_format = format;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = 192U;
  view.height = 108U;
  view.near_plane = 0.1F;
  view.far_plane = 1000.0F;
  view.exposure = view_exposure;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = view.clip_from_view;
  request.views.push_back(view);
  return request;
}

HdrR16Float ExpectedStored(const OgreNextHdrTemporalFramePlan &plan,
                           float average_log_luminance) {
  HdrShaderAutoExposureInput input;
  input.exposure = plan.ogre_exposure;
  input.minimum_auto_exposure = plan.minimum_auto_exposure;
  input.maximum_auto_exposure = plan.maximum_auto_exposure;
  input.average_log_luminance = average_log_luminance;
  input.previous_inverse_luminance =
      plan.previous_inverse_luminance_r16.decoded;
  input.delta_seconds = plan.delta_seconds;
  HdrShaderAutoExposureResult output;
  Require(EvaluateHdrShaderAutoExposure(input, output).ok(),
          "oracle could not evaluate admitted temporal plan");
  return output.stored_inverse_luminance_r16;
}

bool SameHistoryComparison(const OgreNextHdrHistoryComparison &left,
                           const OgreNextHdrHistoryComparison &right) {
  return left.version == right.version && left.mode == right.mode &&
         left.native_inverse_luminance_r16.bits ==
             right.native_inverse_luminance_r16.bits &&
         left.native_inverse_luminance_r16.decoded ==
             right.native_inverse_luminance_r16.decoded &&
         left.reference_inverse_luminance_r16.bits ==
             right.reference_inverse_luminance_r16.bits &&
         left.reference_inverse_luminance_r16.decoded ==
             right.reference_inverse_luminance_r16.decoded &&
         left.ogre_exposure == right.ogre_exposure &&
         left.minimum_auto_exposure == right.minimum_auto_exposure &&
         left.maximum_auto_exposure == right.maximum_auto_exposure &&
         left.average_log_luminance == right.average_log_luminance &&
         left.previous_inverse_luminance_r16.bits ==
             right.previous_inverse_luminance_r16.bits &&
         left.previous_inverse_luminance_r16.decoded ==
             right.previous_inverse_luminance_r16.decoded &&
         left.delta_seconds == right.delta_seconds &&
         left.absolute_error == right.absolute_error &&
         left.allowed_error == right.allowed_error &&
         left.conditioning_bound == right.conditioning_bound &&
         left.binary32_rounding_bound ==
             right.binary32_rounding_bound &&
         left.storage_ulp == right.storage_ulp &&
         left.r16_ulp_distance == right.r16_ulp_distance &&
         left.accepted == right.accepted;
}

void TestConfigurationIsTransactional() {
  OgreNextHdrTemporalState state;
  OgreNextHdrTemporalConfiguration invalid;
  invalid.bloom_full_colour_threshold =
      invalid.bloom_minimum_threshold;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "invalid bloom configuration mutated temporal state");

  invalid = OgreNextHdrTemporalConfiguration{};
  invalid.minimum_auto_exposure = 3.0F;
  invalid.maximum_auto_exposure = 2.0F;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "reversed auto-exposure bounds mutated temporal state");

  invalid = OgreNextHdrTemporalConfiguration{};
  invalid.initial_inverse_luminance =
      std::numeric_limits<float>::infinity();
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "non-finite initial history mutated temporal state");

  invalid = OgreNextHdrTemporalConfiguration{};
  invalid.initial_inverse_luminance = 0.02F;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "unpinned custom initial history was accepted by version 2");

  const OgreNextHdrTemporalConfiguration valid;
  Require(state.Initialize(valid).ok() && state.initialized(),
          "default temporal configuration was rejected");
  const HdrR16Float initial = state.previous_inverse_luminance();
  Require(initial.decoded > 0.0F && initial.bits != 0U,
          "initial inverse luminance was not stored as positive R16");
  Require(!state.Initialize(valid).ok() &&
              state.previous_inverse_luminance().bits == initial.bits,
          "double initialization changed committed temporal state");
}

void TestDeterministicSimulationTimeAndGpuLineage() {
  OgreNextHdrTemporalState state;
  Require(state.Initialize(OgreNextHdrTemporalConfiguration{}).ok(),
          "temporal state initialization failed");

  const RenderFrameRequest first = Frame(1U, Scene(1U, 10.0, 1.0F), 2.0F);
  OgreNextHdrTemporalFramePlan first_plan;
  Require(state.PrepareFrame(first,
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             first_plan)
              .ok(),
          "first HDR temporal plan failed");
  Require(first_plan.frame_id == 1U && first_plan.snapshot_id == 1U &&
              first_plan.delta_seconds == 0.0F &&
              first_plan.effective_exposure == 4.0F &&
              std::fabs(first_plan.ogre_exposure - std::log(4.0F)) <
                  1.0e-6F &&
              first_plan.bloom_inverse_transition_width == 0.5F,
          "first temporal plan did not preserve exposure/bloom semantics");

  const HdrR16Float first_expected = ExpectedStored(first_plan, 2.0F);
  OgreNextHdrTemporalFramePlan forged_plan = first_plan;
  forged_plan.effective_exposure = 8.0F;
  Require(!state.CommitFrame(forged_plan, 2.0F, first_expected).ok() &&
              state.committed_frame_id() == 0U,
          "forged effective/shader exposure relationship advanced state");
  Require(!state.CommitFrame(
                    first_plan, std::numeric_limits<float>::quiet_NaN(),
                    first_expected)
               .ok() &&
              state.committed_frame_id() == 0U,
          "non-finite luminance advanced temporal state");
  HdrR16Float wrong;
  Require(DecodeFiniteHdrR16Float(
              static_cast<std::uint16_t>(first_expected.bits + 3U), wrong)
              .ok(),
          "far native R16 fixture did not decode");
  Require(!state.CommitFrame(first_plan, 2.0F, wrong).ok() &&
              state.committed_frame_id() == 0U,
          "out-of-bound native R16 history advanced temporal state");
  wrong = first_expected;
  wrong.decoded = std::nextafter(wrong.decoded,
                                 std::numeric_limits<float>::infinity());
  Require(!state.CommitFrame(first_plan, 2.0F, wrong).ok() &&
              state.committed_frame_id() == 0U,
          "inconsistent native R16 bits/decoded value advanced state");
  HdrR16Float adjacent_native;
  Require(DecodeFiniteHdrR16Float(
              static_cast<std::uint16_t>(first_expected.bits + 1U),
              adjacent_native)
              .ok(),
          "adjacent native R16 fixture did not decode");
  Require(state.CommitFrame(first_plan, 2.0F, adjacent_native).ok() &&
              state.committed_frame_id() == 1U &&
              state.previous_inverse_luminance().bits == adjacent_native.bits,
          "bounded native-authoritative R16 history did not commit");
  const OgreNextHdrHistoryComparison first_comparison =
      state.last_history_comparison();
  Require(first_comparison.version == 2U && first_comparison.accepted &&
              first_comparison.mode ==
                  OgreNextHdrHistoryValidationMode::
                      NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP &&
              first_comparison.native_inverse_luminance_r16.bits ==
                  adjacent_native.bits &&
              first_comparison.reference_inverse_luminance_r16.bits ==
                  first_expected.bits &&
              first_comparison.r16_ulp_distance == 1U &&
              first_comparison.absolute_error > 0.0 &&
              first_comparison.absolute_error <=
                  first_comparison.allowed_error &&
              first_comparison.storage_ulp > 0.0,
          "native/reference/error/bound comparison evidence is incomplete");

  constexpr double kSecondTime = 10.0 + 1.0 / 48.0;
  const RenderFrameRequest second = Frame(2U, Scene(2U, kSecondTime));
  OgreNextHdrTemporalFramePlan second_plan;
  Require(state.PrepareFrame(second,
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             second_plan)
              .ok(),
          "second HDR temporal plan failed");
  Require(second_plan.delta_seconds ==
              static_cast<float>(kSecondTime - 10.0) &&
              second_plan.previous_inverse_luminance_r16.bits ==
                  adjacent_native.bits,
          "second plan used wall time or lost exact R16 lineage");
  const HdrR16Float second_expected = ExpectedStored(second_plan, -0.75F);
  Require(state.CommitFrame(second_plan, -0.75F, second_expected).ok() &&
              state.committed_frame_id() == 2U,
          "second exact native R16 history did not commit");

  Require(!state.CommitFrame(second_plan, -0.75F, second_expected).ok() &&
              state.committed_frame_id() == 2U,
          "stale temporal plan was committed twice");
  OgreNextHdrTemporalFramePlan untouched;
  const OgreNextHdrTemporalFramePlan original = untouched;
  Require(!state.PrepareFrame(Frame(3U, Scene(3U, 9.0)),
                              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                              untouched)
               .ok() &&
              untouched.frame_id == original.frame_id &&
              untouched.snapshot_id == original.snapshot_id,
          "nonmonotonic simulation time mutated the output plan");
  Require(!state.PrepareFrame(Frame(3U, Scene(4U, 100.0)),
                              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                              untouched)
               .ok(),
          "oversized deterministic frame delta was accepted");

  HdrR16Float initial_history;
  Require(QuantizeHdrR16Float(
              OgreNextHdrTemporalConfiguration{}.initial_inverse_luminance,
              initial_history)
              .ok() &&
              state.ResetSceneGeneration().ok() && state.initialized() &&
              state.committed_frame_id() == 2U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              !state.last_history_comparison().accepted,
          "scene reset lost global frame lineage or retained map history");
  OgreNextHdrTemporalFramePlan reloaded_plan;
  Require(state.PrepareFrame(Frame(3U, Scene(5U, 0.0)),
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             reloaded_plan)
                  .ok() &&
              reloaded_plan.frame_id == 3U &&
              reloaded_plan.delta_seconds == 0.0F &&
              reloaded_plan.previous_inverse_luminance_r16.bits ==
                  initial_history.bits,
          "marked next generation did not admit tick-zero temporal history");
}

void TestTwoPhaseCommitIsAtomicAndRetryable() {
  OgreNextHdrTemporalState state;
  Require(state.Initialize(OgreNextHdrTemporalConfiguration{}).ok(),
          "temporal state initialization failed");

  const RenderFrameRequest first = Frame(1U, Scene(10U, 5.0));
  OgreNextHdrTemporalFramePlan first_plan;
  Require(state.PrepareFrame(first,
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             first_plan)
              .ok(),
          "first two-phase temporal plan failed");
  const HdrR16Float expected = ExpectedStored(first_plan, 1.25F);
  const HdrR16Float initial_history = state.previous_inverse_luminance();
  const OgreNextHdrHistoryComparison initial_comparison =
      state.last_history_comparison();

  HdrR16Float invalid_native = expected;
  invalid_native.decoded = std::nextafter(
      invalid_native.decoded, std::numeric_limits<float>::infinity());
  Require(!state.PrepareCommit(first_plan, 1.25F, invalid_native).ok() &&
              !state.CanCommitPrepared() && state.initialized() &&
              state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    initial_comparison),
          "failed prepare exposed or staged temporal state");

  Require(state.PrepareCommit(first_plan, 1.25F, expected).ok() &&
              state.CanCommitPrepared(),
          "valid temporal commit did not enter prepared state");
  Require(state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    initial_comparison),
          "prepared temporal candidate became visible before commit");

  OgreNextHdrTemporalFramePlan blocked_output;
  blocked_output.frame_id = 91U;
  blocked_output.snapshot_id = 92U;
  Require(!state.PrepareFrame(
                    Frame(1U, Scene(11U, 5.0)),
                    OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                    blocked_output)
               .ok() &&
              blocked_output.frame_id == 91U &&
              blocked_output.snapshot_id == 92U,
          "PrepareFrame admitted work or mutated output while commit pending");
  Require(!state.PrepareCommit(first_plan, 1.25F, expected).ok() &&
              state.CanCommitPrepared() &&
              state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    initial_comparison),
          "double prepare replaced or published the pending candidate");

  state.AbortPrepared();
  Require(!state.CanCommitPrepared() && state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    initial_comparison),
          "abort changed committed temporal accessors");
  state.CommitPrepared();
  Require(state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits ==
                  initial_history.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    initial_comparison),
          "commit after abort published a discarded candidate");

  Require(state.PrepareCommit(first_plan, 1.25F, expected).ok() &&
              state.CanCommitPrepared(),
          "aborted temporal candidate could not be retried");
  state.CommitPrepared();
  const OgreNextHdrHistoryComparison committed_comparison =
      state.last_history_comparison();
  Require(!state.CanCommitPrepared() && state.committed_frame_id() == 1U &&
              state.previous_inverse_luminance().bits == expected.bits &&
              committed_comparison.accepted &&
              committed_comparison.native_inverse_luminance_r16.bits ==
                  expected.bits,
          "CommitPrepared did not atomically publish temporal state");

  state.CommitPrepared();
  Require(state.committed_frame_id() == 1U &&
              state.previous_inverse_luminance().bits == expected.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    committed_comparison),
          "double commit changed committed temporal state");
  Require(!state.PrepareCommit(first_plan, 1.25F, expected).ok() &&
              !state.CanCommitPrepared() &&
              state.committed_frame_id() == 1U &&
              state.previous_inverse_luminance().bits == expected.bits &&
              SameHistoryComparison(state.last_history_comparison(),
                                    committed_comparison),
          "stale plan prepared or changed committed temporal state");

  constexpr double kSecondTime = 5.0 + 1.0 / 48.0;
  OgreNextHdrTemporalFramePlan second_plan;
  Require(state.PrepareFrame(Frame(2U, Scene(12U, kSecondTime)),
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             second_plan)
              .ok(),
          "second two-phase temporal plan failed");
  const HdrR16Float second_expected = ExpectedStored(second_plan, -0.5F);
  Require(state.PrepareCommit(second_plan, -0.5F, second_expected).ok() &&
              state.CanCommitPrepared(),
          "second temporal candidate was not prepared");
  Require(!state.ResetSceneGeneration().ok() && state.CanCommitPrepared(),
          "scene reset discarded a prepared temporal commit");
  state.Reset();
  Require(!state.CanCommitPrepared() && !state.initialized() &&
              state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits == 0U &&
              !state.last_history_comparison().accepted,
          "Reset retained a prepared temporal candidate");
  state.CommitPrepared();
  Require(!state.initialized() && state.committed_frame_id() == 0U,
          "commit after Reset revived a discarded temporal candidate");
}

void TestRetiredFrameAccounting() {
  OgreNextHdrTemporalState state;
  Require(state.Initialize(OgreNextHdrTemporalConfiguration{}).ok(),
          "temporal state initialization failed");

  constexpr double kFirstTime = 10.0;
  const RenderFrameRequest first = Frame(1U, Scene(1U, kFirstTime));
  OgreNextHdrTemporalFramePlan first_plan;
  Require(state.PrepareFrame(first,
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             first_plan)
              .ok(),
          "first retired-accounting temporal plan failed");
  const HdrR16Float first_expected = ExpectedStored(first_plan, 0.5F);
  Require(state.CommitFrame(first_plan, 0.5F, first_expected).ok() &&
              state.committed_frame_id() == 1U,
          "first retired-accounting frame did not commit");
  const HdrR16Float committed_history = state.previous_inverse_luminance();
  const OgreNextHdrHistoryComparison committed_comparison =
      state.last_history_comparison();

  constexpr double kRetiredTime = kFirstTime + 1.0 / 48.0;
  Require(!state.CanAccountRetiredFrame(1U, kRetiredTime) &&
              !state.CanAccountRetiredFrame(3U, kRetiredTime),
          "a retired frame was accounted off the committed lineage");
  Require(!state.CanAccountRetiredFrame(
                  2U, std::numeric_limits<double>::quiet_NaN()) &&
              !state.CanAccountRetiredFrame(
                  2U, std::numeric_limits<double>::infinity()) &&
              !state.CanAccountRetiredFrame(2U, -1.0) &&
              !state.CanAccountRetiredFrame(2U, kFirstTime - 1.0),
          "non-finite, negative or nonmonotonic retired time was accounted");
  // Deliberate: kHdrMaximumFrameDeltaSeconds bounds what the temporal shader
  // blends, and a retired frame runs no shader. Refusing a long suspension
  // here would turn a recoverable retirement into a fatal reset.
  Require(state.CanAccountRetiredFrame(
              2U, kFirstTime + 10.0 * kHdrMaximumFrameDeltaSeconds),
          "the retire path wrongly inherited the rendered delta envelope");
  Require(!state.AccountRetiredFrame(3U, kRetiredTime) &&
              state.committed_frame_id() == 1U,
          "a refused retirement advanced committed frame identity");

  Require(state.CanAccountRetiredFrame(2U, kRetiredTime) &&
              state.AccountRetiredFrame(2U, kRetiredTime) &&
              state.committed_frame_id() == 2U &&
              state.previous_inverse_luminance().bits ==
                  committed_history.bits &&
              state.previous_inverse_luminance().decoded ==
                  committed_history.decoded &&
              SameHistoryComparison(state.last_history_comparison(),
                                    committed_comparison),
          "accounting a retirement fabricated exposure history");

  // The regression this accounting exists for: without it, every rendered
  // frame after a retirement is rejected as noncontiguous.
  constexpr double kThirdTime = kRetiredTime + 1.0 / 48.0;
  OgreNextHdrTemporalFramePlan third_plan;
  Require(state.PrepareFrame(Frame(3U, Scene(3U, kThirdTime)),
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             third_plan)
                  .ok() &&
              third_plan.frame_id == 3U &&
              third_plan.delta_seconds ==
                  static_cast<float>(kThirdTime - kRetiredTime) &&
              third_plan.previous_inverse_luminance_r16.bits ==
                  committed_history.bits,
          "the frame after a retirement was not admitted contiguously");

  const HdrR16Float third_expected = ExpectedStored(third_plan, -0.25F);
  Require(state.PrepareCommit(third_plan, -0.25F, third_expected).ok() &&
              state.CanCommitPrepared(),
          "third retired-accounting candidate was not prepared");
  Require(!state.CanAccountRetiredFrame(3U, kThirdTime) &&
              !state.AccountRetiredFrame(3U, kThirdTime) &&
              state.committed_frame_id() == 2U,
          "a retirement was accounted while a commit was prepared");
  state.AbortPrepared();

  Require(state.ResetSceneGeneration().ok() &&
              state.committed_frame_id() == 2U &&
              state.CanAccountRetiredFrame(3U, 0.0),
          "scene reset broke retired-frame accounting for the next generation");

  state.Reset();
  Require(!state.CanAccountRetiredFrame(1U, 0.0) &&
              !state.AccountRetiredFrame(1U, 0.0) &&
              state.committed_frame_id() == 0U,
          "an uninitialized temporal state accounted a retired frame");
}

void TestFailClosedAdmission() {
  OgreNextHdrTemporalState state;
  Require(state.Initialize(OgreNextHdrTemporalConfiguration{}).ok(),
          "temporal state initialization failed");
  OgreNextHdrTemporalFramePlan plan;
  RenderFrameRequest request = Frame(1U, Scene(1U, 0.0));
  Require(!state.PrepareFrame(request,
                              OgreNextRasterFeatureTier::STATIC_PBR_N1,
                              plan)
               .ok(),
          "texture-free N1 tier was accepted by HDR temporal contract");

  request.color_format = PixelFormat::RGBA16_FLOAT;
  Require(!state.PrepareFrame(request,
                              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                              plan)
               .ok(),
          "raw HDR output was mislabeled as tone-mapped SDR");
  Require(state.PrepareFrame(request,
                             OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                             plan, true)
              .ok(),
          "deferred sun-visibility V2 linear HDR preparation was rejected");
  request.present = true;
  Require(!state.PrepareFrame(request,
                              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                              plan, true)
               .ok(),
          "sun-visibility V2 temporal preparation presented before its continuation");

  request = Frame(1U, Scene(2U, 0.0, 24.0F));
  Require(!state.PrepareFrame(request,
                              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
                              plan)
               .ok(),
          "out-of-envelope natural-log exposure was accepted");

  state.Reset();
  Require(!state.initialized() && state.committed_frame_id() == 0U &&
              state.previous_inverse_luminance().bits == 0U &&
              !state.last_history_comparison().accepted,
          "Reset retained HDR temporal history");
}

} // namespace

int main() {
  TestConfigurationIsTransactional();
  TestDeterministicSimulationTimeAndGpuLineage();
  TestTwoPhaseCommitIsAtomicAndRetryable();
  TestRetiredFrameAccounting();
  TestFailClosedAdmission();
  std::cout << "Ogre-Next HDR temporal contract tests passed\n";
  return EXIT_SUCCESS;
}
