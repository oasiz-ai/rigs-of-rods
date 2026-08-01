/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextPssmShadowPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "Ogre-Next PSSM policy test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RenderAssetId Id(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x5053534D5F544553), low);
}

RenderAssetReference Ref(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, Id(low), 1U);
}

MeshResourceDescriptor Mesh(bool dynamic) {
  MeshResourceDescriptor mesh;
  mesh.debug_name = dynamic ? "dynamic shadow triangle"
                            : "static shadow triangle";
  mesh.dynamic = dynamic;
  mesh.local_bounds.minimum = {-1.0F, -1.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {{-1.0F, -1.0F, 0.0F},
                    {1.0F, -1.0F, 0.0F},
                    {0.0F, 1.0F, 0.0F}};
  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0 = {
      {0.0F, 1.0F}, {1.0F, 1.0F}, {0.5F, 0.0F}};
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

RenderAssetRegistry Registry(std::uint64_t registry_id) {
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  for (std::uint64_t index = 0U; index < 2U; ++index) {
    RenderAssetMutation mesh;
    mesh.asset = Ref(RenderAssetKind::MESH, index + 1U);
    mesh.payload = Mesh(index == 1U);
    delta.mutations.push_back(std::move(mesh));
  }
  MaterialDescriptor material;
  material.debug_name = "PSSM policy material";
  material.roughness_factor = 0.5F;
  RenderAssetMutation material_mutation;
  material_mutation.asset = Ref(RenderAssetKind::MATERIAL, 3U);
  material_mutation.payload = material;
  delta.mutations.push_back(std::move(material_mutation));
  RenderAssetRegistry registry(registry_id);
  Require(registry.Apply(delta).ok(), "could not build asset registry");
  return registry;
}

CameraViewRequest ShadowView() {
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = 192U;
  view.height = 128U;
  view.near_plane = kOgreNextPssmNearMeters;
  view.far_plane = kOgreNextPssmFarMeters;
  return view;
}

std::shared_ptr<const SceneSnapshot> Scene(
    std::uint64_t registry_id, std::vector<LightDescriptor> lights,
    std::uint32_t static_flags = MESH_INSTANCE_DEFAULT_FLAGS,
    std::uint32_t dynamic_flags = MESH_INSTANCE_DEFAULT_FLAGS) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  for (std::uint64_t index = 0U; index < 2U; ++index) {
    MeshInstanceDescriptor instance;
    instance.instance_id = index + 1U;
    instance.mesh = Ref(RenderAssetKind::MESH, index + 1U);
    instance.material = Ref(RenderAssetKind::MATERIAL, 3U);
    instance.local_bounds = Mesh(index == 1U).local_bounds;
    instance.flags = index == 0U ? static_flags : dynamic_flags;
    descriptor.mesh_instances.push_back(instance);
  }
  descriptor.lights = std::move(lights);
  SceneSnapshotCreateResult result =
      CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "could not build scene snapshot");
  return result.snapshot;
}

LightDescriptor Directional(std::uint32_t shadow_flags) {
  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  light.shadow_flags = shadow_flags;
  return light;
}

void TestConstantsAndSplits() {
  Require(kOgreNextPssmCascadeCount == 3U &&
              kOgreNextPssmNearMeters == 0.5F &&
              kOgreNextPssmFarMeters == 350.0F &&
              kOgreNextPssmLambda == 0.97F &&
              kOgreNextPssmSplitBlend == 0.125F &&
              kOgreNextPssmSplitPaddingMeters == 1.0F &&
              kOgreNextPssmSplitFade == 0.313F &&
              kOgreNextPssmXyPadding == 1.5F &&
              kOgreNextPssmStableCascadeCount == 1U &&
              kOgreNextPssmPcfKernelWidth == 4U,
          "reviewed split or filter constants changed");
  Require(kOgreNextPssmAtlasWidth == 2048U &&
              kOgreNextPssmAtlasHeight == 3072U &&
              kOgreNextPssmCascadeLayouts[0U].width == 2048U &&
              kOgreNextPssmCascadeLayouts[0U].height == 2048U &&
              kOgreNextPssmCascadeLayouts[1U].atlas_y == 2048U &&
              kOgreNextPssmCascadeLayouts[2U].atlas_x == 1024U,
          "reviewed atlas layout changed");

  OgreNextPssmSplitPolicy splits;
  Require(TryBuildOgreNextPssmSplitPolicy(splits),
          "fixed split policy is not representable");
  Require(splits.split_points.front() == kOgreNextPssmNearMeters &&
              splits.split_points.back() == kOgreNextPssmFarMeters,
          "split endpoints changed");
  for (std::size_t index = 1U; index < splits.split_points.size(); ++index) {
    Require(std::isfinite(splits.split_points[index]) &&
                splits.split_points[index] >
                    splits.split_points[index - 1U],
            "split points are not finite and ordered");
  }
  OgreNextPssmSplitPolicy repeated;
  Require(TryBuildOgreNextPssmSplitPolicy(repeated) &&
              repeated.split_points == splits.split_points &&
              repeated.blend_points == splits.blend_points &&
              repeated.fade_point == splits.fade_point,
          "fixed split arithmetic is not repeatable");
}

void TestAdmissionAndMasks() {
  constexpr std::uint64_t kRegistryId = 97U;
  RenderAssetRegistry registry = Registry(kRegistryId);
  const CameraViewRequest view = ShadowView();
  const auto static_only =
      Scene(kRegistryId, {Directional(LIGHT_SHADOW_STATIC_GEOMETRY)},
            MESH_INSTANCE_DEFAULT_FLAGS,
            MESH_INSTANCE_RECEIVES_SHADOW);
  OgreNextPssmShadowFramePlan plan;
  Require(TryBuildOgreNextPssmShadowFramePlan(
              *static_only, registry, view,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, plan)
              .ok(),
          "valid static-only PSSM scene was rejected");
  Require(plan.enabled && plan.shadow_light_id == 1U &&
              plan.static_caster_count == 1U &&
              plan.dynamic_caster_count == 0U &&
              plan.receiver_count == 2U,
          "static/dynamic shadow mask or receiver plan is wrong");

  const auto dynamic_only =
      Scene(kRegistryId, {Directional(LIGHT_SHADOW_DYNAMIC_GEOMETRY)},
            MESH_INSTANCE_CASTS_SHADOW,
            MESH_INSTANCE_DEFAULT_FLAGS);
  Require(TryBuildOgreNextPssmShadowFramePlan(
              *dynamic_only, registry, view,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, plan)
                  .ok() &&
              plan.static_caster_count == 0U &&
              plan.dynamic_caster_count == 1U &&
              plan.receiver_count == 1U,
          "dynamic-only shadow mask or instance flags are wrong");

  OgreNextPssmShadowFramePlan disabled_sentinel;
  disabled_sentinel.enabled = true;
  disabled_sentinel.shadow_light_id = 44U;
  const ValidationResult disabled = TryBuildOgreNextPssmShadowFramePlan(
      *static_only, registry, view,
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
      OgreNextDirectionalShadowMode::DISABLED, disabled_sentinel);
  Require(disabled.code == ValidationCode::UNSUPPORTED_FEATURE &&
              disabled_sentinel.enabled &&
              disabled_sentinel.shadow_light_id == 44U,
          "disabled mode admitted shadows or mutated output on failure");

  const auto no_shadow = Scene(kRegistryId, {Directional(0U)});
  Require(TryBuildOgreNextPssmShadowFramePlan(
              *no_shadow, registry, view,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::DISABLED, plan)
                  .ok() &&
              !plan.enabled && plan.static_caster_count == 0U &&
              plan.dynamic_caster_count == 0U &&
              plan.receiver_count == 0U,
          "shadow-disabled plan is not the exact no-op contract");
}

void TestFailClosedEdges() {
  constexpr std::uint64_t kRegistryId = 98U;
  RenderAssetRegistry registry = Registry(kRegistryId);
  CameraViewRequest view = ShadowView();
  const auto shadowed =
      Scene(kRegistryId, {Directional(LIGHT_SHADOW_DEFAULT_FLAGS)});
  OgreNextPssmShadowFramePlan plan;
  Require(ValidateOgreNextPssmInitialization(
              OgreNextRasterFeatureTier::STATIC_PBR_N1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "static N1 silently enabled PSSM");
  Require(ValidateOgreNextPssmInitialization(
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              static_cast<OgreNextDirectionalShadowMode>(255U))
              .code == ValidationCode::INVALID_ENUM,
          "unknown shadow mode was accepted");

  view.near_plane = std::nextafter(kOgreNextPssmNearMeters, 1.0F);
  Require(TryBuildOgreNextPssmShadowFramePlan(
              *shadowed, registry, view,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, plan)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "near-plane drift was accepted");
  view = ShadowView();
  view.far_plane = std::nextafter(kOgreNextPssmFarMeters, 0.0F);
  Require(TryBuildOgreNextPssmShadowFramePlan(
              *shadowed, registry, view,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1, plan)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "far-plane drift was accepted");

  LightDescriptor local = Directional(LIGHT_SHADOW_DEFAULT_FLAGS);
  local.type = LightType::POINT;
  local.position = {1.0F, 2.0F, 3.0F};
  local.previous_position = local.position;
  local.direction = {0.0F, -1.0F, 0.0F};
  local.previous_direction = local.direction;
  local.range = 10.0F;
  const auto local_scene = Scene(kRegistryId, {local});
  Require(ValidateOgreNextPssmShadowScene(
              *local_scene,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "local-light shadow substitution was accepted");

  LightDescriptor second = Directional(LIGHT_SHADOW_DEFAULT_FLAGS);
  second.light_id = 2U;
  const auto two_lights = Scene(
      kRegistryId,
      {Directional(LIGHT_SHADOW_DEFAULT_FLAGS), second});
  Require(ValidateOgreNextPssmShadowScene(
              *two_lights,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "multiple shadow lights were accepted");

  const auto zero_mask = Scene(kRegistryId, {Directional(0U)});
  Require(ValidateOgreNextPssmShadowScene(
              *zero_mask,
              OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
              OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1)
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "enabled PSSM accepted a zero shadow mask");
}

} // namespace

int main() {
  TestConstantsAndSplits();
  TestAdmissionAndMasks();
  TestFailClosedEdges();
  return EXIT_SUCCESS;
}
