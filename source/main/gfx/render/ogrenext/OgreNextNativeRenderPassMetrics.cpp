/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral accounting for Ogre-Next native pass metrics.

#include "OgreNextNativeRenderPassMetrics.h"

#include <limits>

namespace RoR::Render {

bool TryAddOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &left,
    const OgreNextNativeRenderMetrics &right,
    OgreNextNativeRenderMetrics &output) noexcept {
  const auto can_add = [](std::size_t first, std::size_t second) noexcept {
    return second <= (std::numeric_limits<std::size_t>::max)() - first;
  };
  if (!can_add(left.batches, right.batches) ||
      !can_add(left.draws, right.draws) ||
      !can_add(left.instances, right.instances) ||
      !can_add(left.faces, right.faces) ||
      !can_add(left.vertices, right.vertices)) {
    return false;
  }
  const OgreNextNativeRenderMetrics candidate{
      left.batches + right.batches, left.draws + right.draws,
      left.instances + right.instances, left.faces + right.faces,
      left.vertices + right.vertices};
  output = candidate;
  return true;
}

bool TrySubtractOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &after,
    const OgreNextNativeRenderMetrics &before,
    OgreNextNativeRenderMetrics &output) noexcept {
  if (after.batches < before.batches || after.draws < before.draws ||
      after.instances < before.instances || after.faces < before.faces ||
      after.vertices < before.vertices) {
    return false;
  }
  const OgreNextNativeRenderMetrics candidate{
      after.batches - before.batches, after.draws - before.draws,
      after.instances - before.instances, after.faces - before.faces,
      after.vertices - before.vertices};
  output = candidate;
  return true;
}

bool SameOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &left,
    const OgreNextNativeRenderMetrics &right) noexcept {
  return left.batches == right.batches && left.draws == right.draws &&
         left.instances == right.instances && left.faces == right.faces &&
         left.vertices == right.vertices;
}

void OgreNextNativeRenderPassMetricsState::BeginFrame(
    OgreNextHdrSceneTopology topology, bool recording) noexcept {
  topology_ = topology;
  expected_scene_pass_count_ =
      topology == OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1
          ? 1U
          : topology == OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2
                ? 3U
                : 0U;
  active_scene_pre_ = {};
  active_shadow_post_ = {};
  first_scene_pre_ = {};
  last_scene_post_ = {};
  aggregate_shadow_maps_ = {};
  aggregate_hdr_scene_ = {};
  workspace_post_ = {};
  active_pass_identity_ = 0U;
  scene_pre_count_ = 0U;
  shadow_post_count_ = 0U;
  scene_post_count_ = 0U;
  active_scene_pass_ = OgreNextNativeScenePass::UNTRACKED;
  recording_ = recording;
  seams_valid_ = recording && expected_scene_pass_count_ != 0U;
  active_shadow_observed_ = false;
  last_scene_post_observed_ = false;
  workspace_post_observed_ = false;
}

void OgreNextNativeRenderPassMetricsState::ScenePre(
    OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
    const OgreNextNativeRenderMetrics &metrics) noexcept {
  if (!recording_) {
    return;
  }
  if (workspace_post_observed_ || pass_identity == 0U ||
      active_scene_pass_ != OgreNextNativeScenePass::UNTRACKED ||
      active_pass_identity_ != 0U ||
      scene_pre_count_ >= expected_scene_pass_count_ ||
      pass != ExpectedScenePass(scene_pre_count_)) {
    seams_valid_ = false;
    return;
  }
  active_scene_pass_ = pass;
  active_pass_identity_ = pass_identity;
  active_scene_pre_ = metrics;
  if (scene_pre_count_ == 0U) {
    first_scene_pre_ = metrics;
  } else if (!last_scene_post_observed_ ||
             !SameOgreNextNativeRenderMetrics(metrics, last_scene_post_)) {
    seams_valid_ = false;
  }
  active_shadow_observed_ = false;
  ++scene_pre_count_;
}

void OgreNextNativeRenderPassMetricsState::SceneAfterShadowMaps(
    OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
    const OgreNextNativeRenderMetrics &metrics) noexcept {
  if (!recording_) {
    return;
  }
  if (workspace_post_observed_ || pass == OgreNextNativeScenePass::UNTRACKED ||
      pass_identity == 0U || pass != active_scene_pass_ ||
      pass_identity != active_pass_identity_) {
    seams_valid_ = false;
    return;
  }
  if (active_shadow_observed_) {
    seams_valid_ = false;
    return;
  }
  active_shadow_post_ = metrics;
  active_shadow_observed_ = true;
  ++shadow_post_count_;
}

void OgreNextNativeRenderPassMetricsState::ScenePost(
    OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
    const OgreNextNativeRenderMetrics &metrics) noexcept {
  if (!recording_) {
    return;
  }
  if (workspace_post_observed_ || pass == OgreNextNativeScenePass::UNTRACKED ||
      pass_identity == 0U || pass != active_scene_pass_ ||
      pass_identity != active_pass_identity_) {
    seams_valid_ = false;
    return;
  }

  OgreNextNativeRenderMetrics shadow_delta;
  OgreNextNativeRenderMetrics scene_delta;
  OgreNextNativeRenderMetrics aggregate_shadow;
  OgreNextNativeRenderMetrics aggregate_scene;
  if (!active_shadow_observed_ ||
      !TrySubtractOgreNextNativeRenderMetrics(active_shadow_post_,
                                              active_scene_pre_,
                                              shadow_delta) ||
      !TrySubtractOgreNextNativeRenderMetrics(metrics, active_shadow_post_,
                                              scene_delta) ||
      !TryAddOgreNextNativeRenderMetrics(aggregate_shadow_maps_, shadow_delta,
                                         aggregate_shadow) ||
      !TryAddOgreNextNativeRenderMetrics(aggregate_hdr_scene_, scene_delta,
                                         aggregate_scene)) {
    seams_valid_ = false;
  } else {
    aggregate_shadow_maps_ = aggregate_shadow;
    aggregate_hdr_scene_ = aggregate_scene;
  }
  last_scene_post_ = metrics;
  last_scene_post_observed_ = true;
  active_scene_pass_ = OgreNextNativeScenePass::UNTRACKED;
  active_pass_identity_ = 0U;
  active_shadow_observed_ = false;
  ++scene_post_count_;
}

void OgreNextNativeRenderPassMetricsState::WorkspacePost(
    const OgreNextNativeRenderMetrics &metrics) noexcept {
  if (!recording_) {
    return;
  }
  if (workspace_post_observed_ ||
      active_scene_pass_ != OgreNextNativeScenePass::UNTRACKED ||
      active_pass_identity_ != 0U ||
      scene_pre_count_ != expected_scene_pass_count_ ||
      shadow_post_count_ != expected_scene_pass_count_ ||
      scene_post_count_ != expected_scene_pass_count_ ||
      !last_scene_post_observed_) {
    seams_valid_ = false;
    return;
  }
  workspace_post_ = metrics;
  workspace_post_observed_ = true;
}

bool OgreNextNativeRenderPassMetricsState::EndFrame(
    const OgreNextNativeRenderMetrics &total,
    OgreNextNativeRenderPassMetricsReceipt &output) noexcept {
  if (!recording_) {
    return false;
  }

  bool exact = seams_valid_ &&
               active_scene_pass_ == OgreNextNativeScenePass::UNTRACKED &&
               active_pass_identity_ == 0U &&
               scene_pre_count_ == expected_scene_pass_count_ &&
               shadow_post_count_ == expected_scene_pass_count_ &&
               scene_post_count_ == expected_scene_pass_count_ &&
               last_scene_post_observed_ && workspace_post_observed_;
  OgreNextNativeRenderPassMetricsReceipt candidate;
  candidate.before_hdr_scene = first_scene_pre_;
  candidate.shadow_maps = aggregate_shadow_maps_;
  candidate.hdr_scene = aggregate_hdr_scene_;

  OgreNextNativeRenderMetrics classified_through_scene;
  OgreNextNativeRenderMetrics classified_with_scene;
  if (exact) {
    exact = TryAddOgreNextNativeRenderMetrics(candidate.before_hdr_scene,
                                              candidate.shadow_maps,
                                              classified_through_scene) &&
            TryAddOgreNextNativeRenderMetrics(
                classified_through_scene, candidate.hdr_scene,
                classified_with_scene) &&
            SameOgreNextNativeRenderMetrics(classified_with_scene,
                                            last_scene_post_) &&
            TrySubtractOgreNextNativeRenderMetrics(
                workspace_post_, last_scene_post_, candidate.hdr_post) &&
            TrySubtractOgreNextNativeRenderMetrics(
                total, workspace_post_, candidate.after_hdr_workspace);
  }

  recording_ = false;
  if (exact) {
    output = candidate;
  }
  return exact;
}

OgreNextNativeScenePass
OgreNextNativeRenderPassMetricsState::ExpectedScenePass(
    std::uint32_t index) const noexcept {
  if (topology_ ==
      OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1) {
    return index == 0U ? OgreNextNativeScenePass::HDR_SINGLE
                       : OgreNextNativeScenePass::UNTRACKED;
  }
  if (topology_ != OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2) {
    return OgreNextNativeScenePass::UNTRACKED;
  }
  switch (index) {
  case 0U:
    return OgreNextNativeScenePass::HDR_BASE;
  case 1U:
    return OgreNextNativeScenePass::HDR_SUN_FULL;
  case 2U:
    return OgreNextNativeScenePass::HDR_RASTER_LIT;
  default:
    return OgreNextNativeScenePass::UNTRACKED;
  }
}

} // namespace RoR::Render
