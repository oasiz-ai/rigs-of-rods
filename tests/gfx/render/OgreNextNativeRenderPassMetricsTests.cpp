/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextNativeRenderPassMetrics.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Ogre-Next native render pass metrics test failed: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

OgreNextNativeRenderMetrics Metrics(std::size_t batches, std::size_t draws,
                                    std::size_t instances, std::size_t faces,
                                    std::size_t vertices) {
  return {batches, draws, instances, faces, vertices};
}

bool SameReceipt(const OgreNextNativeRenderPassMetricsReceipt &left,
                 const OgreNextNativeRenderPassMetricsReceipt &right) {
  return SameOgreNextNativeRenderMetrics(left.before_hdr_scene,
                                         right.before_hdr_scene) &&
         SameOgreNextNativeRenderMetrics(left.shadow_maps,
                                         right.shadow_maps) &&
         SameOgreNextNativeRenderMetrics(left.hdr_scene, right.hdr_scene) &&
         SameOgreNextNativeRenderMetrics(left.hdr_post, right.hdr_post) &&
         SameOgreNextNativeRenderMetrics(left.after_hdr_workspace,
                                         right.after_hdr_workspace);
}

OgreNextNativeRenderPassMetricsReceipt SentinelReceipt() {
  OgreNextNativeRenderPassMetricsReceipt receipt;
  receipt.before_hdr_scene = Metrics(91U, 92U, 93U, 94U, 95U);
  receipt.shadow_maps = Metrics(81U, 82U, 83U, 84U, 85U);
  receipt.hdr_scene = Metrics(71U, 72U, 73U, 74U, 75U);
  receipt.hdr_post = Metrics(61U, 62U, 63U, 64U, 65U);
  receipt.after_hdr_workspace = Metrics(51U, 52U, 53U, 54U, 55U);
  return receipt;
}

void ExpectRejected(OgreNextNativeRenderPassMetricsState &state,
                    const OgreNextNativeRenderMetrics &total,
                    const char *message) {
  OgreNextNativeRenderPassMetricsReceipt output = SentinelReceipt();
  const OgreNextNativeRenderPassMetricsReceipt before = output;
  Require(!state.EndFrame(total, output), message);
  Require(SameReceipt(output, before),
          "rejected frame changed the caller's receipt");
}

void CompleteSingle(OgreNextNativeRenderPassMetricsState &state) {
  constexpr std::uintptr_t kSingleIdentity = 101U;
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kSingleIdentity,
                 Metrics(1U, 2U, 3U, 4U, 5U));
  state.SceneAfterShadowMaps(
      OgreNextNativeScenePass::HDR_SINGLE, kSingleIdentity,
      Metrics(2U, 5U, 7U, 11U, 13U));
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kSingleIdentity,
                  Metrics(4U, 11U, 17U, 23U, 31U));
  state.WorkspacePost(Metrics(5U, 13U, 20U, 27U, 36U));
}

void CompleteSplit(OgreNextNativeRenderPassMetricsState &state,
                   bool add_inter_pass_gap = false) {
  constexpr std::uintptr_t kBaseIdentity = 201U;
  constexpr std::uintptr_t kSunFullIdentity = 202U;
  constexpr std::uintptr_t kRasterLitIdentity = 203U;
  const OgreNextNativeRenderMetrics zero;
  const OgreNextNativeRenderMetrics base_post =
      Metrics(1U, 10U, 10U, 100U, 300U);
  const OgreNextNativeRenderMetrics sun_pre =
      add_inter_pass_gap ? Metrics(1U, 11U, 10U, 100U, 300U) : base_post;
  const OgreNextNativeRenderMetrics sun_post =
      add_inter_pass_gap ? Metrics(2U, 21U, 20U, 200U, 600U)
                         : Metrics(2U, 20U, 20U, 200U, 600U);
  const OgreNextNativeRenderMetrics raster_post =
      add_inter_pass_gap ? Metrics(3U, 31U, 30U, 300U, 900U)
                         : Metrics(3U, 30U, 30U, 300U, 900U);

  state.ScenePre(OgreNextNativeScenePass::HDR_BASE, kBaseIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_BASE, kBaseIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_BASE, kBaseIdentity, base_post);
  state.ScenePre(OgreNextNativeScenePass::HDR_SUN_FULL, kSunFullIdentity,
                 sun_pre);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SUN_FULL,
                             kSunFullIdentity, sun_pre);
  state.ScenePost(OgreNextNativeScenePass::HDR_SUN_FULL, kSunFullIdentity,
                  sun_post);
  state.ScenePre(OgreNextNativeScenePass::HDR_RASTER_LIT, kRasterLitIdentity,
                 sun_post);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_RASTER_LIT,
                             kRasterLitIdentity, sun_post);
  state.ScenePost(OgreNextNativeScenePass::HDR_RASTER_LIT, kRasterLitIdentity,
                  raster_post);
  state.WorkspacePost(add_inter_pass_gap
                          ? Metrics(4U, 33U, 32U, 302U, 906U)
                          : Metrics(4U, 32U, 32U, 302U, 906U));
}

void TestSingleHappyPath() {
  OgreNextNativeRenderPassMetricsState state;
  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  CompleteSingle(state);
  OgreNextNativeRenderPassMetricsReceipt output = SentinelReceipt();
  Require(state.EndFrame(Metrics(7U, 18U, 26U, 35U, 47U), output),
          "single-scene topology was rejected");
  Require(SameOgreNextNativeRenderMetrics(
              output.before_hdr_scene, Metrics(1U, 2U, 3U, 4U, 5U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.shadow_maps, Metrics(1U, 3U, 4U, 7U, 8U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.hdr_scene, Metrics(2U, 6U, 10U, 12U, 18U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.hdr_post, Metrics(1U, 2U, 3U, 4U, 5U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.after_hdr_workspace,
                  Metrics(2U, 5U, 6U, 8U, 11U)),
          "single-scene partition did not preserve every native counter");

  const OgreNextNativeRenderPassMetricsReceipt sentinel = SentinelReceipt();
  output = sentinel;
  Require(!state.EndFrame(Metrics(7U, 18U, 26U, 35U, 47U), output) &&
              SameReceipt(output, sentinel),
          "a completed frame published twice");
}

void TestDirectionalSplitHappyPath() {
  OgreNextNativeRenderPassMetricsState state;
  state.BeginFrame(
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2, true);
  CompleteSplit(state);
  OgreNextNativeRenderPassMetricsReceipt output;
  Require(state.EndFrame(Metrics(5U, 33U, 33U, 304U, 912U), output),
          "directional split topology was rejected");
  Require(SameOgreNextNativeRenderMetrics(output.before_hdr_scene,
                                         Metrics(0U, 0U, 0U, 0U, 0U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.shadow_maps, Metrics(0U, 0U, 0U, 0U, 0U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.hdr_scene, Metrics(3U, 30U, 30U, 300U, 900U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.hdr_post, Metrics(1U, 2U, 2U, 2U, 6U)) &&
              SameOgreNextNativeRenderMetrics(
                  output.after_hdr_workspace,
                  Metrics(1U, 1U, 1U, 2U, 6U)),
          "directional split did not aggregate all three scenes exactly");
}

void TestMissingSeamsFailClosed() {
  constexpr std::uintptr_t kIdentity = 301U;
  const OgreNextNativeRenderMetrics zero;
  OgreNextNativeRenderPassMetricsState state;

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "missing scene-pre seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "missing after-shadow seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "missing scene-post seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  ExpectRejected(state, zero, "missing workspace-post seam was accepted");

  state.BeginFrame(
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2, true);
  state.ScenePre(OgreNextNativeScenePass::HDR_BASE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_BASE, kIdentity, zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_BASE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero,
                 "missing directional split scene passes were accepted");
}

void TestDuplicateAndWrongSeamsFailClosed() {
  constexpr std::uintptr_t kIdentity = 401U;
  constexpr std::uintptr_t kOtherIdentity = 402U;
  const OgreNextNativeRenderMetrics zero;
  OgreNextNativeRenderPassMetricsState state;

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "duplicate scene-pre seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero,
                 "duplicate after-shadow seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "duplicate scene-post seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE,
                             kOtherIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "wrong pass identity was accepted");

  state.BeginFrame(
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2, true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SUN_FULL, kIdentity, zero);
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SUN_FULL, kIdentity,
                             zero);
  state.ScenePost(OgreNextNativeScenePass::HDR_SUN_FULL, kIdentity, zero);
  state.WorkspacePost(zero);
  ExpectRejected(state, zero, "wrong split pass order was accepted");
}

void TestCounterGapsAndRegressionsFailClosed() {
  OgreNextNativeRenderPassMetricsState state;
  state.BeginFrame(
      OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2, true);
  CompleteSplit(state, true);
  ExpectRejected(state, Metrics(5U, 34U, 33U, 304U, 912U),
                 "inter-pass counter work was charged to HDR post");

  constexpr std::uintptr_t kIdentity = 501U;
  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.ScenePre(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                 Metrics(1U, 5U, 1U, 1U, 1U));
  state.SceneAfterShadowMaps(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                             Metrics(1U, 4U, 1U, 1U, 1U));
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                  Metrics(1U, 6U, 1U, 1U, 1U));
  state.WorkspacePost(Metrics(1U, 6U, 1U, 1U, 1U));
  ExpectRejected(state, Metrics(1U, 6U, 1U, 1U, 1U),
                 "counter regression inside a scene pass was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  CompleteSingle(state);
  ExpectRejected(state, Metrics(4U, 12U, 19U, 26U, 35U),
                 "total counter regression after workspace was accepted");
}

void TestWorkspaceOrderingFailsClosed() {
  const OgreNextNativeRenderMetrics zero;
  OgreNextNativeRenderPassMetricsState state;
  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  state.WorkspacePost(zero);
  CompleteSingle(state);
  ExpectRejected(state, Metrics(7U, 18U, 26U, 35U, 47U),
                 "early workspace seam was accepted");

  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  CompleteSingle(state);
  state.WorkspacePost(Metrics(5U, 13U, 20U, 27U, 36U));
  ExpectRejected(state, Metrics(7U, 18U, 26U, 35U, 47U),
                 "duplicate workspace seam was accepted");

  constexpr std::uintptr_t kIdentity = 601U;
  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   true);
  CompleteSingle(state);
  state.ScenePost(OgreNextNativeScenePass::HDR_SINGLE, kIdentity,
                  Metrics(5U, 13U, 20U, 27U, 36U));
  ExpectRejected(state, Metrics(7U, 18U, 26U, 35U, 47U),
                 "tracked scene callback after workspace was accepted");
}

void TestArithmeticOverflowAndAliasing() {
  const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
  OgreNextNativeRenderMetrics output = Metrics(7U, 8U, 9U, 10U, 11U);
  Require(TryAddOgreNextNativeRenderMetrics(
              Metrics(maximum - 1U, maximum - 1U, maximum - 1U,
                      maximum - 1U, maximum - 1U),
              Metrics(1U, 1U, 1U, 1U, 1U), output) &&
              SameOgreNextNativeRenderMetrics(
                  output,
                  Metrics(maximum, maximum, maximum, maximum, maximum)),
          "exact size_t boundary addition failed");

  output = Metrics(7U, 8U, 9U, 10U, 11U);
  const OgreNextNativeRenderMetrics before = output;
  Require(!TryAddOgreNextNativeRenderMetrics(
              Metrics(maximum, 1U, 1U, 1U, 1U),
              Metrics(1U, 1U, 1U, 1U, 1U), output) &&
              SameOgreNextNativeRenderMetrics(output, before),
          "overflow changed the output candidate");

  OgreNextNativeRenderMetrics aliased = Metrics(1U, 2U, 3U, 4U, 5U);
  Require(TryAddOgreNextNativeRenderMetrics(
              aliased, Metrics(5U, 4U, 3U, 2U, 1U), aliased) &&
              SameOgreNextNativeRenderMetrics(
                  aliased, Metrics(6U, 6U, 6U, 6U, 6U)),
          "addition did not support output aliasing its left input");
  Require(TrySubtractOgreNextNativeRenderMetrics(
              aliased, Metrics(1U, 1U, 1U, 1U, 1U), aliased) &&
              SameOgreNextNativeRenderMetrics(
                  aliased, Metrics(5U, 5U, 5U, 5U, 5U)),
          "subtraction did not support output aliasing its after input");

  OgreNextNativeRenderMetrics right_alias = Metrics(5U, 4U, 3U, 2U, 1U);
  Require(TryAddOgreNextNativeRenderMetrics(
              Metrics(1U, 2U, 3U, 4U, 5U), right_alias, right_alias) &&
              SameOgreNextNativeRenderMetrics(
                  right_alias, Metrics(6U, 6U, 6U, 6U, 6U)),
          "addition did not support output aliasing its right input");
  Require(TrySubtractOgreNextNativeRenderMetrics(
              Metrics(7U, 8U, 9U, 10U, 11U), right_alias, right_alias) &&
              SameOgreNextNativeRenderMetrics(
                  right_alias, Metrics(1U, 2U, 3U, 4U, 5U)),
          "subtraction did not support output aliasing its before input");

  output = Metrics(7U, 8U, 9U, 10U, 11U);
  const OgreNextNativeRenderMetrics subtraction_before = output;
  Require(!TrySubtractOgreNextNativeRenderMetrics(
              Metrics(0U, 2U, 3U, 4U, 5U),
              Metrics(1U, 2U, 3U, 4U, 5U), output) &&
              SameOgreNextNativeRenderMetrics(output, subtraction_before),
          "counter underflow changed the output candidate");
}

void TestDisabledAndUnknownTopologiesNeverPublish() {
  OgreNextNativeRenderPassMetricsState state;
  state.BeginFrame(OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1,
                   false);
  CompleteSingle(state);
  ExpectRejected(state, Metrics(7U, 18U, 26U, 35U, 47U),
                 "disabled recording published a receipt");

  state.BeginFrame(static_cast<OgreNextHdrSceneTopology>(0xffU), true);
  ExpectRejected(state, OgreNextNativeRenderMetrics{},
                 "unknown topology published a receipt");
}

} // namespace

int main() {
  TestSingleHappyPath();
  TestDirectionalSplitHappyPath();
  TestMissingSeamsFailClosed();
  TestDuplicateAndWrongSeamsFailClosed();
  TestCounterGapsAndRegressionsFailClosed();
  TestWorkspaceOrderingFailsClosed();
  TestArithmeticOverflowAndAliasing();
  TestDisabledAndUnknownTopologiesNeverPublish();
  return EXIT_SUCCESS;
}
