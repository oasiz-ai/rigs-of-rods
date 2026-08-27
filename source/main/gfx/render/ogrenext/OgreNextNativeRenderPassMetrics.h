/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral accounting for Ogre-Next native pass metrics.

#pragma once

#include "OgreNextHdrSceneTopology.h"

#include <cstddef>
#include <cstdint>

namespace RoR::Render {

/// One monotonic snapshot of the counters published by a native renderer.
/// The type deliberately contains no Ogre handles so its partition rules can
/// be tested on every build host without creating a graphics device.
struct OgreNextNativeRenderMetrics final {
  std::size_t batches = 0U;
  std::size_t draws = 0U;
  std::size_t instances = 0U;
  std::size_t faces = 0U;
  std::size_t vertices = 0U;
};

[[nodiscard]] bool TryAddOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &left,
    const OgreNextNativeRenderMetrics &right,
    OgreNextNativeRenderMetrics &output) noexcept;

[[nodiscard]] bool TrySubtractOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &after,
    const OgreNextNativeRenderMetrics &before,
    OgreNextNativeRenderMetrics &output) noexcept;

[[nodiscard]] bool SameOgreNextNativeRenderMetrics(
    const OgreNextNativeRenderMetrics &left,
    const OgreNextNativeRenderMetrics &right) noexcept;

enum class OgreNextNativeScenePass : std::uint8_t {
  UNTRACKED = 0,
  HDR_SINGLE,
  HDR_BASE,
  HDR_SUN_FULL,
  HDR_RASTER_LIT,
};

struct OgreNextNativeRenderPassMetricsReceipt final {
  OgreNextNativeRenderMetrics before_hdr_scene;
  OgreNextNativeRenderMetrics shadow_maps;
  OgreNextNativeRenderMetrics hdr_scene;
  OgreNextNativeRenderMetrics hdr_post;
  OgreNextNativeRenderMetrics after_hdr_workspace;
};

/// Fail-closed state machine for one native HDR workspace submission.
///
/// The Ogre listener maps its reviewed pass identifiers to NativeScenePass and
/// supplies the pass object's stable address as pass_identity. Unknown Ogre
/// passes are not submitted to this helper. Every required tracked pass must
/// then produce pre, after-shadow, and post seams in the topology's exact
/// order. Counter work between tracked scene passes, counter regression,
/// duplicate/wrong seams, and an early or repeated workspace seam all make the
/// final receipt inexact. EndFrame publishes transactionally: a false result
/// leaves output untouched.
class OgreNextNativeRenderPassMetricsState final {
public:
  void BeginFrame(OgreNextHdrSceneTopology topology, bool recording) noexcept;

  void ScenePre(OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
                const OgreNextNativeRenderMetrics &metrics) noexcept;

  void SceneAfterShadowMaps(
      OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
      const OgreNextNativeRenderMetrics &metrics) noexcept;

  void ScenePost(OgreNextNativeScenePass pass, std::uintptr_t pass_identity,
                 const OgreNextNativeRenderMetrics &metrics) noexcept;

  void WorkspacePost(const OgreNextNativeRenderMetrics &metrics) noexcept;

  [[nodiscard]] bool EndFrame(
      const OgreNextNativeRenderMetrics &total,
      OgreNextNativeRenderPassMetricsReceipt &output) noexcept;

private:
  [[nodiscard]] OgreNextNativeScenePass
  ExpectedScenePass(std::uint32_t index) const noexcept;

  OgreNextNativeRenderMetrics active_scene_pre_;
  OgreNextNativeRenderMetrics active_shadow_post_;
  OgreNextNativeRenderMetrics first_scene_pre_;
  OgreNextNativeRenderMetrics last_scene_post_;
  OgreNextNativeRenderMetrics aggregate_shadow_maps_;
  OgreNextNativeRenderMetrics aggregate_hdr_scene_;
  OgreNextNativeRenderMetrics workspace_post_;
  std::uintptr_t active_pass_identity_ = 0U;
  std::uint32_t scene_pre_count_ = 0U;
  std::uint32_t shadow_post_count_ = 0U;
  std::uint32_t scene_post_count_ = 0U;
  std::uint32_t expected_scene_pass_count_ = 0U;
  OgreNextHdrSceneTopology topology_ =
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2;
  OgreNextNativeScenePass active_scene_pass_ =
      OgreNextNativeScenePass::UNTRACKED;
  bool recording_ = false;
  bool seams_valid_ = false;
  bool active_shadow_observed_ = false;
  bool last_scene_post_observed_ = false;
  bool workspace_post_observed_ = false;
};

} // namespace RoR::Render
