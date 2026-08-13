/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextTaaContract.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using namespace RoR::Render;

constexpr std::uint64_t kTestLifecycleEpoch = 41U;

OgreNextTaaConfiguration
Configuration(std::uint64_t lifecycle_epoch = kTestLifecycleEpoch) {
  OgreNextTaaConfiguration configuration;
  configuration.lifecycle_epoch = lifecycle_epoch;
  return configuration;
}

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Ogre-Next TAA contract test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void HashFloat(std::uint64_t &hash, float value) {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    hash ^= static_cast<std::uint8_t>(bits >> shift);
    hash *= 1099511628211ULL;
  }
}

OgreNextTaaFrameInput Frame(std::uint64_t frame_id, std::uint64_t snapshot_id,
                            std::uint64_t view_id = 7U,
                            std::uint32_t width = 192U,
                            std::uint32_t height = 108U,
                            float pre_exposure = 1.0F,
                            bool camera_cut = false) {
  OgreNextTaaFrameInput input;
  input.lifecycle_epoch = kTestLifecycleEpoch;
  input.frame_id = frame_id;
  input.snapshot_id = snapshot_id;
  input.view.view_id = view_id;
  input.view.width = width;
  input.view.height = height;
  input.view.near_plane = 0.1F;
  input.view.far_plane = 1000.0F;
  input.view.exposure = pre_exposure;
  input.view.clip_from_view.elements.fill(0.0F);
  input.view.clip_from_view.elements[0U] = 1.0F;
  input.view.clip_from_view.elements[5U] = 1.0F;
  input.view.clip_from_view.elements[10U] =
      input.view.far_plane / (input.view.near_plane - input.view.far_plane);
  input.view.clip_from_view.elements[11U] = -1.0F;
  input.view.clip_from_view.elements[14U] =
      input.view.near_plane * input.view.far_plane /
      (input.view.near_plane - input.view.far_plane);
  input.view.previous_clip_from_view = input.view.clip_from_view;
  Require(ComputeOgreNextTaaJitterPixels(frame_id,
                                         input.view.temporal_jitter_pixels)
              .ok(),
          "test TAA jitter planning failed");
  input.pre_exposure = pre_exposure;
  input.camera_cut = camera_cut;
  return input;
}

OgreNextTaaImageBinding Binding(std::uint64_t identity, PixelFormat format,
                                const OgreNextTaaFramePlan &plan) {
  OgreNextTaaImageBinding binding;
  binding.native_identity = identity;
  binding.generation = plan.destination_history_generation + 10U;
  binding.format = format;
  binding.width = plan.width;
  binding.height = plan.height;
  return binding;
}

OgreNextTaaExecutionEvidence Evidence(const OgreNextTaaFramePlan &plan,
                                      std::uint64_t identity_base = 100U) {
  OgreNextTaaExecutionEvidence evidence;
  evidence.lifecycle_epoch = plan.lifecycle_epoch;
  evidence.frame_id = plan.frame_id;
  evidence.snapshot_id = plan.snapshot_id;
  evidence.view_id = plan.view_id;
  evidence.camera_lineage_fnv1a64 = plan.camera_lineage_fnv1a64;
  evidence.current_colour =
      Binding(identity_base + 0U, PixelFormat::RGBA16_FLOAT, plan);
  evidence.current_depth =
      Binding(identity_base + 1U, PixelFormat::R32_FLOAT, plan);
  evidence.motion_vectors =
      Binding(identity_base + 2U, PixelFormat::RG16_FLOAT, plan);
  evidence.reactive_mask =
      Binding(identity_base + 3U, PixelFormat::R32_FLOAT, plan);
  evidence.history_source =
      Binding(identity_base + 4U, PixelFormat::RGBA16_FLOAT, plan);
  evidence.history_destination =
      Binding(identity_base + 5U, PixelFormat::RGBA16_FLOAT, plan);
  evidence.prepare_count = 1U;
  evidence.execute_count = 1U;
  evidence.history_read_count = plan.history_available ? 1U : 0U;
  evidence.history_write_count = 1U;
  evidence.history_advance_count = 1U;
  evidence.jitter_application_count = 1U;
  evidence.native_state_verification_count = 1U;
  evidence.unjittered_culling = true;
  evidence.motion_vectors_remove_jitter = true;
  evidence.current_previous_transform_lineage = true;
  evidence.non_reversed_depth_reprojection = true;
  evidence.pre_exposure_history_rescale = true;
  evidence.reactive_mask_consumed = true;
  evidence.variance_neighbourhood_clipping = true;
  evidence.output_alpha_one = true;
  return evidence;
}

OgreNextTaaExecutionEvidence EvidenceForState(const OgreNextTaaState &state,
                                              const OgreNextTaaFramePlan &plan,
                                              std::uint64_t identity_base) {
  OgreNextTaaExecutionEvidence evidence = Evidence(plan, identity_base);
  if (state.committed_frame_id() != 0U &&
      plan.reset_reason != OgreNextTaaHistoryResetReason::EXTENT_CHANGED) {
    evidence.history_source =
        state.committed_history_binding(plan.source_history_slot);
    evidence.history_destination =
        state.committed_history_binding(plan.destination_history_slot);
  }
  return evidence;
}

void Commit(OgreNextTaaState &state, const OgreNextTaaFrameInput &input,
            OgreNextTaaFramePlan &plan, std::uint64_t identity_base) {
  Require(state.PrepareFrame(input, plan).ok(),
          "admitted TAA frame failed planning");
  Require(
      state.PrepareCommit(plan, EvidenceForState(state, plan, identity_base))
              .ok() &&
          state.CanCommitPrepared(),
      "admitted TAA frame failed two-phase preparation");
  state.CommitPrepared();
  Require(
      state.committed_frame_id() == input.frame_id &&
          state.history_generation() == plan.destination_history_generation &&
          state.committed_plan().frame_id == plan.frame_id &&
          state.last_execution_evidence().frame_id == plan.frame_id &&
          state.last_execution_evidence().production_content_readback_count ==
              0U,
      "TAA commit did not publish exact frame/history lineage");
}

void TestConfigurationIsTransactional() {
  OgreNextTaaState state;
  OgreNextTaaConfiguration invalid;
  invalid.lifecycle_epoch = kTestLifecycleEpoch;
  invalid.version = 99U;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "unsupported TAA version mutated state");

  invalid = Configuration();
  invalid.history_weight = 1.0F;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "unit TAA history weight mutated state");

  invalid = Configuration();
  invalid.full_motion_rejection_pixels =
      std::numeric_limits<float>::denorm_min();
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "subnormal motion threshold mutated state");

  invalid = Configuration();
  invalid.maximum_exposure_ratio = 1.0F;
  Require(!state.Initialize(invalid).ok() && !state.initialized(),
          "unit exposure ratio mutated state");

  OgreNextTaaConfiguration absent_epoch;
  Require(!state.Initialize(absent_epoch).ok() && !state.initialized(),
          "zero TAA lifecycle epoch mutated state");

  Require(state.Initialize(Configuration()).ok() && state.initialized() &&
              !state.history_valid() && state.committed_frame_id() == 0U &&
              state.history_generation() == 0U &&
              kOgreNextTaaCurrentRawHdrPreExposure == 1.0F,
          "default TAA configuration did not initialize cleanly");
  Require(!state.Initialize(Configuration()).ok(),
          "double TAA initialization was accepted");
}

void TestExactJitterLineageAndAtomicCommit() {
  constexpr std::array<Float2, 8U> expected{{
      {0.0F, -1.0F / 6.0F},
      {-0.25F, 1.0F / 6.0F},
      {0.25F, -7.0F / 18.0F},
      {-0.375F, -1.0F / 18.0F},
      {0.125F, 5.0F / 18.0F},
      {-0.125F, -5.0F / 18.0F},
      {0.375F, 1.0F / 18.0F},
      {-0.4375F, 7.0F / 18.0F},
  }};

  OgreNextTaaState state;
  Require(state.Initialize(Configuration()).ok(),
          "TAA state initialization failed");

  OgreNextTaaFramePlan first;
  Require(state.PrepareFrame(Frame(1U, 11U), first).ok() &&
              first.jitter_phase == 0U && first.jitter_pixels == expected[0U] &&
              first.previous_jitter_pixels == expected[0U] &&
              !first.history_available &&
              first.reset_reason ==
                  OgreNextTaaHistoryResetReason::INITIAL_FRAME &&
              first.source_history_slot == 0U &&
              first.destination_history_slot == 1U &&
              first.camera_lineage_fnv1a64 != 0U,
          "first TAA frame did not use exact initial jitter/reset semantics");

  OgreNextTaaFrameInput wrong_jitter = Frame(1U, 11U);
  wrong_jitter.view.temporal_jitter_pixels.x = 0.5F;
  OgreNextTaaFramePlan untouched_jitter;
  untouched_jitter.frame_id = 777U;
  Require(!state.PrepareFrame(wrong_jitter, untouched_jitter).ok() &&
              untouched_jitter.frame_id == 777U,
          "view jitter not selected by frame lineage mutated TAA output");

  std::uint64_t original_camera_digest = 0U;
  std::uint64_t moved_camera_digest = 0U;
  CameraViewRequest moved_camera = first.view;
  moved_camera.previous_view_from_render.elements[12U] = 1.0F;
  Require(ComputeOgreNextTaaCameraLineage(first.view, original_camera_digest)
                  .ok() &&
              ComputeOgreNextTaaCameraLineage(moved_camera, moved_camera_digest)
                  .ok() &&
              original_camera_digest == first.camera_lineage_fnv1a64 &&
              moved_camera_digest != original_camera_digest,
          "TAA camera lineage omitted previous-camera motion state");

  OgreNextTaaExecutionEvidence first_evidence = Evidence(first);
  OgreNextTaaExecutionEvidence wrong_camera = first_evidence;
  wrong_camera.camera_lineage_fnv1a64 += 1U;
  Require(!state.PrepareCommit(first, wrong_camera).ok() &&
              state.committed_frame_id() == 0U,
          "wrong TAA camera lineage advanced temporal state");
  OgreNextTaaExecutionEvidence alias = first_evidence;
  alias.history_destination.native_identity =
      alias.history_source.native_identity;
  Require(!state.PrepareCommit(first, alias).ok() &&
              state.committed_frame_id() == 0U,
          "aliased TAA image roles advanced temporal state");

  OgreNextTaaExecutionEvidence readback = first_evidence;
  readback.production_content_readback_count = 1U;
  Require(!state.PrepareCommit(first, readback).ok() &&
              state.committed_frame_id() == 0U,
          "production TAA content readback was admitted");

  Require(state.PrepareCommit(first, first_evidence).ok() &&
              state.CanCommitPrepared(),
          "valid first TAA frame did not prepare");
  Require(!state.InvalidateHistory().ok() && state.CanCommitPrepared(),
          "history invalidation bypassed a pending TAA transaction");
  state.AbortPrepared();
  Require(!state.CanCommitPrepared() && state.committed_frame_id() == 0U &&
              !state.history_valid(),
          "aborted first TAA frame changed committed state");
  Require(state.PrepareCommit(first, first_evidence).ok(),
          "aborted first TAA frame was not retryable");
  state.CommitPrepared();
  Require(state.history_valid() && state.committed_frame_id() == 1U &&
              state.history_generation() == 1U,
          "first TAA commit did not publish one history generation");
  state.CommitPrepared();
  Require(state.committed_frame_id() == 1U && state.history_generation() == 1U,
          "repeated CommitPrepared advanced TAA twice");

  for (std::uint64_t frame_id = 2U; frame_id <= 9U; ++frame_id) {
    OgreNextTaaFramePlan plan;
    const std::uint32_t phase =
        static_cast<std::uint32_t>((frame_id - 1U) % expected.size());
    Require(
        state.PrepareFrame(Frame(frame_id, 10U + frame_id), plan).ok() &&
            plan.jitter_phase == phase &&
            plan.jitter_pixels == expected[phase] &&
            plan.previous_jitter_pixels ==
                expected[(phase + expected.size() - 1U) % expected.size()] &&
            plan.history_available &&
            plan.reset_reason == OgreNextTaaHistoryResetReason::NONE &&
            plan.history_exposure_ratio == 1.0F,
        "TAA jitter sequence or committed previous jitter drifted");

    OgreNextTaaFramePlan forged = plan;
    forged.jitter_pixels.x = 0.5F;
    Require(
        !state.PrepareCommit(
                  forged, EvidenceForState(state, plan, 1000U + frame_id * 10U))
                .ok() &&
            state.committed_frame_id() == frame_id - 1U,
        "forged TAA jitter advanced committed lineage");
    OgreNextTaaExecutionEvidence evidence =
        EvidenceForState(state, plan, 1000U + frame_id * 10U);
    OgreNextTaaExecutionEvidence stale_history = evidence;
    stale_history.history_source.generation += 1U;
    Require(!state.PrepareCommit(plan, stale_history).ok() &&
                state.committed_frame_id() == frame_id - 1U,
            "stale TAA history generation advanced committed lineage");
    stale_history = evidence;
    stale_history.history_destination.native_identity += 100000U;
    Require(!state.PrepareCommit(plan, stale_history).ok() &&
                state.committed_frame_id() == frame_id - 1U,
            "reallocated TAA ping-pong destination advanced committed lineage");
    Require(state.PrepareCommit(plan, evidence).ok(),
            "valid sequenced TAA frame did not prepare");
    state.CommitPrepared();
  }
  Require(state.committed_frame_id() == 9U && state.history_generation() == 9U,
          "TAA eight-phase cycle did not commit exactly once per frame");

  OgreNextTaaFramePlan untouched;
  untouched.frame_id = 991U;
  Require(!state.PrepareFrame(Frame(11U, 99U), untouched).ok() &&
              untouched.frame_id == 991U,
          "noncontiguous TAA frame mutated output plan");
  OgreNextTaaFramePlan repeated_snapshot;
  Require(state.PrepareFrame(Frame(10U, 19U), repeated_snapshot).ok() &&
              repeated_snapshot.snapshot_id == 19U &&
              repeated_snapshot.history_available,
          "stationary repeated TAA snapshot was not admitted");
  OgreNextTaaFramePlan stale_snapshot;
  stale_snapshot.frame_id = 992U;
  Require(!state.PrepareFrame(Frame(10U, 18U), stale_snapshot).ok() &&
              stale_snapshot.frame_id == 992U,
          "backward TAA snapshot lineage mutated output plan");
}

void TestHistoryResetCauses() {
  OgreNextTaaState state;
  Require(state.Initialize(Configuration()).ok(),
          "TAA reset test initialization failed");
  OgreNextTaaFramePlan plan;
  Commit(state, Frame(1U, 1U), plan, 200U);

  Commit(state, Frame(2U, 2U, 7U, 192U, 108U, 1.0F, true), plan, 220U);
  Require(!plan.history_available &&
              plan.reset_reason == OgreNextTaaHistoryResetReason::CAMERA_CUT,
          "camera cut consumed stale TAA history");

  Commit(state, Frame(3U, 3U, 8U), plan, 240U);
  Require(!plan.history_available &&
              plan.reset_reason == OgreNextTaaHistoryResetReason::VIEW_CHANGED,
          "view change consumed stale TAA history");

  Commit(state, Frame(4U, 4U, 8U, 256U, 144U), plan, 260U);
  Require(!plan.history_available &&
              plan.reset_reason ==
                  OgreNextTaaHistoryResetReason::EXTENT_CHANGED,
          "resize consumed stale TAA history");

  Commit(state, Frame(5U, 5U, 8U, 256U, 144U, 32.0F), plan, 280U);
  Require(!plan.history_available &&
              plan.reset_reason ==
                  OgreNextTaaHistoryResetReason::EXPOSURE_DISCONTINUITY &&
              plan.previous_pre_exposure == 32.0F &&
              plan.history_exposure_ratio == 1.0F,
          "exposure discontinuity consumed or rescaled stale TAA history");

  Require(state.InvalidateHistory().ok() && !state.history_valid(),
          "explicit suspend/device invalidation failed");
  Commit(state, Frame(6U, 6U, 8U, 256U, 144U, 32.0F), plan, 300U);
  Require(!plan.history_available &&
              plan.reset_reason ==
                  OgreNextTaaHistoryResetReason::EXPLICIT_INVALIDATION,
          "explicit invalidation consumed stale TAA history");

  state.Reset();
  Require(!state.initialized() && !state.history_valid() &&
              state.committed_frame_id() == 0U &&
              state.history_generation() == 0U,
          "TAA Reset retained frontend-lifetime state");
}

void TestLifecycleEpochAndExtremeExposureReset() {
  OgreNextTaaState state;
  Require(state.Initialize(Configuration(100U)).ok(),
          "first TAA lifecycle initialization failed");
  OgreNextTaaFrameInput first_input = Frame(1U, 1U);
  first_input.lifecycle_epoch = 100U;
  OgreNextTaaFramePlan stale_plan;
  Require(state.PrepareFrame(first_input, stale_plan).ok(),
          "first TAA lifecycle frame failed planning");
  OgreNextTaaExecutionEvidence stale_evidence = Evidence(stale_plan, 800U);
  Require(state.PrepareCommit(stale_plan, stale_evidence).ok(),
          "first TAA lifecycle frame failed preparation");
  state.CommitPrepared();
  state.Reset();

  Require(!state.Initialize(Configuration(100U)).ok() && !state.initialized(),
          "retired TAA lifecycle epoch was reused");
  Require(state.Initialize(Configuration(101U)).ok() &&
              state.lifecycle_epoch() == 101U,
          "next TAA lifecycle epoch was not admitted");
  Require(!state.PrepareCommit(stale_plan, stale_evidence).ok() &&
              state.committed_frame_id() == 0U,
          "retired TAA plan/evidence replay crossed a lifecycle reset");

  OgreNextTaaFrameInput new_first = Frame(1U, 2U);
  new_first.lifecycle_epoch = 101U;
  OgreNextTaaFramePlan new_plan;
  Commit(state, new_first, new_plan, 900U);

  OgreNextTaaFrameInput extreme =
      Frame(2U, 3U, 7U, 192U, 108U, (std::numeric_limits<float>::min)());
  extreme.lifecycle_epoch = 101U;
  OgreNextTaaFramePlan extreme_plan;
  Require(state.PrepareFrame(extreme, extreme_plan).ok() &&
              !extreme_plan.history_available &&
              extreme_plan.reset_reason ==
                  OgreNextTaaHistoryResetReason::EXPOSURE_DISCONTINUITY &&
              extreme_plan.history_exposure_ratio == 1.0F,
          "extreme positive-normal exposure did not reset TAA history");
  Commit(state, extreme, extreme_plan, 920U);
  OgreNextTaaFrameInput overflow = Frame(3U, 4U, 7U, 192U, 108U, 65504.0F);
  overflow.lifecycle_epoch = 101U;
  OgreNextTaaFramePlan overflow_plan;
  Require(state.PrepareFrame(overflow, overflow_plan).ok() &&
              !overflow_plan.history_available &&
              overflow_plan.reset_reason ==
                  OgreNextTaaHistoryResetReason::EXPOSURE_DISCONTINUITY &&
              overflow_plan.history_exposure_ratio == 1.0F,
          "overflowing exposure ratio did not reset TAA history");

  const OgreNextTaaConfiguration configuration = Configuration(101U);
  OgreNextTaaPixelInput pixel;
  pixel.current_neighbourhood.fill(Float4{1.0F, 1.0F, 1.0F, 1.0F});
  pixel.history_colour = Float4{1.0F, 1.0F, 1.0F, 1.0F};
  pixel.current_depth = 0.5F;
  pixel.reprojected_previous_depth = 0.5F;
  pixel.history_available = true;
  pixel.current_pre_exposure = (std::numeric_limits<float>::min)();
  pixel.previous_pre_exposure = (std::numeric_limits<float>::max)();
  OgreNextTaaPixelResult pixel_result;
  Require(EvaluateOgreNextTaaPixel(configuration, pixel, pixel_result).ok() &&
              pixel_result.exposure_rejected &&
              pixel_result.history_weight == 0.0F &&
              pixel_result.history_exposure_ratio == 1.0F,
          "underflowing exposure ratio invalidated instead of rejecting TAA "
          "history");
}

OgreNextTaaPixelInput ConstantPixel(Float4 current, Float4 history) {
  OgreNextTaaPixelInput input;
  input.current_neighbourhood.fill(current);
  input.history_colour = history;
  input.current_depth = 0.5F;
  input.reprojected_previous_depth = 0.5F;
  input.history_available = true;
  return input;
}

void TestPixelReferenceSemantics() {
  const OgreNextTaaConfiguration configuration = Configuration();
  OgreNextTaaPixelResult result;

  OgreNextTaaPixelInput input = ConstantPixel(Float4{2.0F, 1.0F, 0.5F, 1.0F},
                                              Float4{2.0F, 1.0F, 0.5F, 1.0F});
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.colour == Float4{2.0F, 1.0F, 0.5F, 1.0F} &&
              result.history_weight == configuration.history_weight &&
              !result.history_clipped,
          "stationary matching TAA history did not preserve exact colour");

  input = ConstantPixel(Float4{2.0F, 2.0F, 2.0F, 1.0F},
                        Float4{1.0F, 1.0F, 1.0F, 1.0F});
  input.current_pre_exposure = 2.0F;
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.colour == Float4{2.0F, 2.0F, 2.0F, 1.0F} &&
              result.history_exposure_ratio == 2.0F &&
              !result.exposure_rejected,
          "TAA history did not rescale by exact pre-exposure ratio");

  input = ConstantPixel(Float4{3.0F, 2.0F, 1.0F, 1.0F},
                        Float4{3.0F, 2.0F, 1.0F, 1.0F});
  input.reprojected_previous_depth = 0.9F;
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.depth_rejected && result.history_weight == 0.0F &&
              result.colour == input.current_neighbourhood[4U],
          "TAA disocclusion retained history");

  input.reprojected_previous_depth = input.current_depth;
  input.motion_pixels.x = configuration.full_motion_rejection_pixels;
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.motion_rejected && result.history_weight == 0.0F,
          "full-motion threshold retained TAA history");

  input.motion_pixels = Float2{};
  input.reactive_mask = 1.0F;
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.reactive_rejected && result.history_weight == 0.0F,
          "fully reactive pixel retained TAA history");

  input = ConstantPixel(Float4{1.0F, 1.0F, 1.0F, 1.0F},
                        Float4{100.0F, 20.0F, 2.0F, 1.0F});
  input.current_neighbourhood[0U] = Float4{1.2F, 0.9F, 1.1F, 1.0F};
  input.current_neighbourhood[8U] = Float4{0.8F, 1.1F, 0.9F, 1.0F};
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.history_clipped && result.history_weight > 0.0F &&
              result.colour.w == 1.0F && result.colour.x < 2.0F,
          "YCoCg variance clipping did not bound hostile history");

  input = ConstantPixel(Float4{1.0F, 1.0F, 1.0F, 1.0F},
                        Float4{1.0F, 1.0F, 1.0F, 1.0F});
  input.current_pre_exposure = 32.0F;
  Require(EvaluateOgreNextTaaPixel(configuration, input, result).ok() &&
              result.exposure_rejected && result.history_weight == 0.0F &&
              result.colour == input.current_neighbourhood[4U],
          "large exposure discontinuity retained TAA history");
}

void TestHostilePixelInputsAndDeterminism() {
  const OgreNextTaaConfiguration configuration = Configuration();
  OgreNextTaaPixelInput input = ConstantPixel(Float4{1.0F, 1.0F, 1.0F, 1.0F},
                                              Float4{1.0F, 1.0F, 1.0F, 1.0F});
  OgreNextTaaPixelResult output;
  output.colour.x = 999.0F;
  input.current_neighbourhood[3U].w = 0.5F;
  Require(!EvaluateOgreNextTaaPixel(configuration, input, output).ok() &&
              output.colour.x == 999.0F,
          "non-opaque current input mutated TAA output");
  input.current_neighbourhood[3U].w = 1.0F;
  input.motion_pixels.x = std::numeric_limits<float>::quiet_NaN();
  Require(!EvaluateOgreNextTaaPixel(configuration, input, output).ok(),
          "NaN TAA motion was accepted");

  std::uint32_t seed = 0x13579bdfU;
  std::uint64_t oracle_hash = 14695981039346656037ULL;
  const auto random_unit = [&seed]() noexcept {
    seed = seed * 1664525U + 1013904223U;
    return static_cast<float>(seed >> 8U) / 16777215.0F;
  };
  for (std::size_t iteration = 0U; iteration < 4096U; ++iteration) {
    OgreNextTaaPixelInput candidate;
    for (Float4 &sample : candidate.current_neighbourhood) {
      sample = Float4{32.0F * random_unit(), 32.0F * random_unit(),
                      32.0F * random_unit(), 1.0F};
    }
    candidate.history_colour =
        Float4{32.0F * random_unit(), 32.0F * random_unit(),
               32.0F * random_unit(), 1.0F};
    candidate.current_depth = random_unit();
    candidate.reprojected_previous_depth = random_unit();
    candidate.motion_pixels =
        Float2{80.0F * random_unit() - 40.0F, 80.0F * random_unit() - 40.0F};
    candidate.reactive_mask = random_unit();
    candidate.current_pre_exposure = iteration % 31U == 0U ? 32.0F : 1.0F;
    candidate.previous_pre_exposure = 1.0F;
    candidate.history_available = iteration % 7U != 0U;

    OgreNextTaaPixelResult first;
    OgreNextTaaPixelResult second;
    Require(
        EvaluateOgreNextTaaPixel(configuration, candidate, first).ok() &&
            EvaluateOgreNextTaaPixel(configuration, candidate, second).ok() &&
            first.colour == second.colour &&
            first.history_weight == second.history_weight &&
            IsFinite(first.colour) && first.colour.w == 1.0F &&
            first.colour.x >= 0.0F && first.colour.x <= 65504.0F &&
            first.colour.y >= 0.0F && first.colour.y <= 65504.0F &&
            first.colour.z >= 0.0F && first.colour.z <= 65504.0F &&
            first.history_weight >= 0.0F && first.history_weight < 1.0F,
        "fixed-seed TAA oracle lost determinism or finite bounds");
    HashFloat(oracle_hash, first.colour.x);
    HashFloat(oracle_hash, first.colour.y);
    HashFloat(oracle_hash, first.colour.z);
    HashFloat(oracle_hash, first.colour.w);
    HashFloat(oracle_hash, first.history_weight);
  }
  Require(oracle_hash == 11809102002151841037ULL,
          "ordered binary32 TAA oracle drifted from its fixed-seed golden");
}

} // namespace

int main() {
  TestConfigurationIsTransactional();
  TestExactJitterLineageAndAtomicCommit();
  TestHistoryResetCauses();
  TestLifecycleEpochAndExtremeExposureReset();
  TestPixelReferenceSemantics();
  TestHostilePixelInputsAndDeterminism();
  std::cout << "Ogre-Next TAA contract tests passed\n";
  return EXIT_SUCCESS;
}
