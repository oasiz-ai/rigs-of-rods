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
  TestFailClosedAdmission();
  std::cout << "Ogre-Next HDR temporal contract tests passed\n";
  return EXIT_SUCCESS;
}
