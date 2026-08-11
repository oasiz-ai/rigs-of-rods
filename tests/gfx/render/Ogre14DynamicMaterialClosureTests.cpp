/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "OGRE 14 dynamic-material closure test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

template <typename T>
bool SameOwner(const std::shared_ptr<const T> &lhs,
               const std::shared_ptr<const T> &rhs) noexcept {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

RoR::Render::Matrix4x4 Translation(float x, float y = 0.0F,
                                   float z = 0.0F) {
  RoR::Render::Matrix4x4 transform;
  transform.elements[12U] = x;
  transform.elements[13U] = y;
  transform.elements[14U] = z;
  return transform;
}

RoR::Render::Ogre14GraphicsSceneCpuMeshSectionInput MakeCpuTriangle(
    const std::string &name, bool reverse_winding = false) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneCpuMeshSectionInput input;
  input.debug_name = name;
  input.index_format = MeshIndexFormat::UINT16;
  input.topology_revision = 7U;
  input.reverse_winding = reverse_winding;
  input.positions = {{-1.0F, 2.0F, 3.0F}, {4.0F, -5.0F, 6.0F},
                     {0.0F, 1.0F, -2.0F}};
  input.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  input.texture_coordinates_0 = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {0.25F, 1.0F}};
  input.indices = {0U, 1U, 2U};
  return input;
}

std::shared_ptr<const RoR::Render::Ogre14GraphicsSceneJoinedDynamicState>
MakeJoinedState(float x_offset = 0.0F) {
  using namespace RoR::Render;
  auto state = std::make_shared<Ogre14GraphicsSceneJoinedDynamicState>();
  state->topology_revision = 7U;
  state->positions = {{-1.0F + x_offset, 2.0F, 3.0F},
                      {4.0F + x_offset, -5.0F, 6.0F},
                      {x_offset, 1.0F, -2.0F}};
  state->normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  state->updated_local_bounds.minimum = {-1.0F + x_offset, -5.0F, -2.0F};
  state->updated_local_bounds.maximum = {4.0F + x_offset, 2.0F, 6.0F};
  return state;
}

RoR::Render::Ogre14LegacyTextureInput MakeTexture() {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture;
  texture.key.exact_resource_group = "General";
  texture.key.exact_name = "Vehicle/PaintAlbedo";
  texture.source_revision = 1U;
  texture.width = 1U;
  texture.height = 1U;
  Ogre14LegacyTextureMipInput mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.slice_pitch_bytes = 4U;
  mip.bytes = {32U, 96U, 192U, 255U};
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RoR::Render::Ogre14LegacyMaterialInput MakeMaterial(
    const RoR::Render::Ogre14LegacyTextureInput &texture,
    RoR::Render::Ogre14LegacyCullMode cull =
        RoR::Render::Ogre14LegacyCullMode::CLOCKWISE) {
  using namespace RoR::Render;
  Ogre14LegacyMaterialInput material;
  material.key.exact_resource_group = "General";
  material.key.exact_name = "Vehicle/Paint";
  material.source_revision = 1U;
  material.diffuse_linear = {0.25F, 0.5F, 0.75F, 1.0F};
  material.base_color_semantic = Ogre14LegacyBaseColorSemantic::UNLIT;
  material.lighting_enabled = false;
  material.pipeline.cull = cull;
  Ogre14LegacyTextureUnitInput unit;
  unit.texture_key = texture.key;
  unit.sampler.source_revision = 1U;
  unit.sampler.maximum_lod = 0.0F;
  material.texture_units.push_back(std::move(unit));
  return material;
}

RoR::Render::Ogre14LegacyTranslatedFrame MakeMaterialFrame(
    RoR::Render::Ogre14LegacyCullMode cull =
        RoR::Render::Ogre14LegacyCullMode::CLOCKWISE) {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput input;
  input.source_sequence = 1U;
  input.textures.push_back(MakeTexture());
  input.materials.push_back(MakeMaterial(input.textures.front(), cull));
  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame frame;
  Require(translator.Translate(input, frame).ok() && frame.full_snapshot,
          "could not create the authoritative dynamic-material fixture");
  return frame;
}

RoR::Render::Ogre14LegacyAssetKey PaintMaterialKey() {
  RoR::Render::Ogre14LegacyAssetKey key;
  key.exact_resource_group = "General";
  key.exact_name = "Vehicle/Paint";
  return key;
}

std::shared_ptr<const RoR::Render::Ogre14LegacyMaterialClosure>
ResolvePaintClosure(const RoR::Render::Ogre14LegacyTranslatedFrame &frame) {
  using namespace RoR::Render;
  Ogre14LegacyMaterialClosure closure;
  Require(ResolveOgre14LegacyMaterialClosure(frame, PaintMaterialKey(),
                                              closure)
              .ok(),
          "could not resolve the exact dynamic-material closure");
  return std::make_shared<const Ogre14LegacyMaterialClosure>(
      std::move(closure));
}

RoR::Render::Ogre14GraphicsSceneMaterialCull ToCapturedCull(
    RoR::Render::Ogre14LegacyCullMode cull) {
  using namespace RoR::Render;
  switch (cull) {
  case Ogre14LegacyCullMode::NONE:
    return Ogre14GraphicsSceneMaterialCull::NONE;
  case Ogre14LegacyCullMode::CLOCKWISE:
    return Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  case Ogre14LegacyCullMode::ANTICLOCKWISE:
    return Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  }
  return Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
}

RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput MakeCapturedMaterial(
    const RoR::Render::Ogre14LegacyMaterialClosure &closure) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneMaterialCaptureInput material;
  material.exact_resource_group = "General";
  material.exact_name = "Vehicle/Paint";
  material.texture_unit_count = 1U;
  material.lighting_enabled = false;
  material.diffuse_linear =
      std::get<MaterialDescriptor>(*closure.assets.back().payload)
          .base_color_factor;
  material.ambient_linear = {};
  material.specular_linear = {};
  material.emissive_linear = {};
  material.shininess = 0.0F;
  material.cull = ToCapturedCull(closure.material_audit->pipeline.cull);
  return material;
}

RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput MakeExactDynamic(
    const std::shared_ptr<const RoR::Render::Ogre14LegacyMaterialClosure>
        &closure,
    std::uint32_t section_index = 0U) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionCaptureInput input;
  input.identity.actor_instance_id = 41;
  input.identity.component_kind =
      Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY;
  input.identity.component_id = 2U;
  input.identity.section_index = section_index;
  input.exact_entity_name = "actor-41-flexbody-2";
  input.material = MakeCapturedMaterial(*closure);
  input.resolved_material = closure;
  input.mesh_reverse_winding = closure->requires_reverse_winding;
  const std::string mesh_name =
      "actor-41-flexbody-2/section-" + std::to_string(section_index);
  const ValidationResult validation =
      BuildOgre14GraphicsSceneDynamicMeshPayload(
          MakeCpuTriangle(mesh_name, input.mesh_reverse_winding),
          input.mesh_payload);
  Require(validation.ok(), "exact dynamic fixture mesh was rejected");
  input.render_from_object = Translation(10.0F, 2.0F, -3.0F);
  input.state = MakeJoinedState();
  return input;
}

RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput
MakeFallbackDynamic() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionCaptureInput input;
  input.identity.actor_instance_id = 7;
  input.identity.component_kind = Ogre14GraphicsSceneDynamicComponentKind::CAB;
  input.identity.component_id = 0U;
  input.identity.section_index = 0U;
  input.exact_entity_name = "actor-7-cab";
  input.material.exact_resource_group = "General";
  input.material.exact_name = "Vehicle/FactorOnly";
  input.material.diffuse_linear = {0.125F, 0.25F, 0.5F, 1.0F};
  input.material.ambient_linear = {0.1F, 0.1F, 0.1F};
  input.material.emissive_linear = {0.01F, 0.02F, 0.03F};
  input.material.shininess = 30.0F;
  Require(BuildOgre14GraphicsSceneDynamicMeshPayload(
              MakeCpuTriangle("actor-7-cab"), input.mesh_payload)
              .ok(),
          "fallback dynamic fixture mesh was rejected");
  input.render_from_object = Translation(1.0F);
  input.state = MakeJoinedState();
  return input;
}

RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput MakeExactStatic(
    const std::shared_ptr<const RoR::Render::Ogre14LegacyMaterialClosure>
        &closure) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneStaticSectionCaptureInput input;
  input.stable_object_id = 99U;
  input.section_index = 0U;
  input.exact_entity_name = "static-paint-proof";
  input.mesh_identity.exact_resource_group = "General";
  input.mesh_identity.exact_mesh_name = "static-paint-proof.mesh";
  input.mesh_identity.vertex_count = 3U;
  input.mesh_identity.index_count = 3U;
  input.mesh_identity.reverse_winding = closure->requires_reverse_winding;
  input.material = MakeCapturedMaterial(*closure);
  input.resolved_material = closure;
  Require(BuildOgre14GraphicsSceneStaticMeshPayload(
              MakeCpuTriangle("static-paint-proof",
                              closure->requires_reverse_winding),
              input.mesh_payload)
              .ok(),
          "static equivalence fixture mesh was rejected");
  input.render_from_object = Translation(20.0F);
  return input;
}

const RoR::Render::GraphicsSceneAssetInput &FindAsset(
    const std::vector<RoR::Render::GraphicsSceneAssetInput> &assets,
    std::uint64_t source_asset_id) {
  const auto found =
      std::find_if(assets.begin(), assets.end(),
                   [source_asset_id](const auto &asset) {
                     return asset.source_asset_id == source_asset_id;
                   });
  Require(found != assets.end(), "expected material dependency is absent");
  return *found;
}

void RequireFailureUnchanged(
    const std::vector<
        RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput> &inputs,
    RoR::Render::ValidationCode expected_code, const char *message,
    RoR::Render::IOgre14GraphicsSceneDynamicInventoryFaultInjector *fault =
        nullptr) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  MaterialDescriptor sentinel_material;
  sentinel_material.debug_name = "sentinel/material";
  const auto sentinel_payload =
      std::make_shared<const RenderAssetPayload>(sentinel_material);
  std::vector<GraphicsSceneAssetInput> assets(1U);
  assets.front().source_asset_id = 991U;
  assets.front().payload = sentinel_payload;
  assets.front()
      .material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)] = {771U, 772U};
  auto mutable_sentinel_state =
      std::make_shared<GraphicsSceneDynamicMeshState>();
  mutable_sentinel_state->topology_revision = 31U;
  mutable_sentinel_state->deformation_revision = 47U;
  const std::shared_ptr<const GraphicsSceneDynamicMeshState> sentinel_state =
      mutable_sentinel_state;
  std::vector<GraphicsSceneDynamicMeshInput> meshes(1U);
  meshes.front().source_object_id = 992U;
  meshes.front().mesh_source_asset_id = 993U;
  meshes.front().material_source_asset_id = 994U;
  meshes.front().visibility_mask = 0x12345678U;
  meshes.front().state = sentinel_state;
  const ValidationResult result = BuildOgre14GraphicsSceneDynamicInventory(
      inputs, registry, assets, meshes, fault);
  Require(!result && result.code == expected_code &&
              registry.asset_identity_count() == 0U &&
              registry.object_identity_count() == 0U && assets.size() == 1U &&
              assets.front().source_asset_id == 991U &&
              SameOwner(assets.front().payload, sentinel_payload) &&
              assets.front()
                      .material_bindings[static_cast<std::size_t>(
                          MaterialTextureSlot::BASE_COLOR)] ==
                  GraphicsSceneAssetBinding{771U, 772U} &&
              meshes.size() == 1U &&
              meshes.front().source_object_id == 992U &&
              meshes.front().mesh_source_asset_id == 993U &&
              meshes.front().material_source_asset_id == 994U &&
              meshes.front().visibility_mask == 0x12345678U &&
              SameOwner(meshes.front().state, sentinel_state),
          message);
}

void TestExactDynamicClosureSharedOwnersAndStaticEquivalence() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  const auto closure = ResolvePaintClosure(frame);
  const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> inputs{
      MakeExactDynamic(closure, 0U), MakeExactDynamic(closure, 1U)};

  Ogre14GraphicsSceneDynamicIdentityRegistry dynamic_registry;
  std::vector<GraphicsSceneAssetInput> dynamic_assets;
  std::vector<GraphicsSceneDynamicMeshInput> dynamic_meshes;
  ValidationResult result = BuildOgre14GraphicsSceneDynamicInventory(
      inputs, dynamic_registry, dynamic_assets, dynamic_meshes);
  Require(result.ok() && dynamic_assets.size() == 5U &&
              dynamic_meshes.size() == 2U &&
              dynamic_registry.asset_identity_count() == 5U &&
              dynamic_registry.object_identity_count() == 2U,
          "exact deformable dependencies were not collision-audited once");
  for (const GraphicsSceneDynamicMeshInput &mesh : dynamic_meshes) {
    Require(mesh.material_source_asset_id ==
                closure->material_source_asset_id,
            "dynamic section lost its translator-derived material ID");
  }
  for (const GraphicsSceneAssetInput &dependency : closure->assets) {
    const GraphicsSceneAssetInput &published =
        FindAsset(dynamic_assets, dependency.source_asset_id);
    Require(SameOwner(published.payload, dependency.payload) &&
                published.material_bindings == dependency.material_bindings,
            "dynamic transaction replaced a closure owner or binding");
  }

  const std::vector<GraphicsSceneAssetInput> first_assets = dynamic_assets;
  result = BuildOgre14GraphicsSceneDynamicInventory(
      inputs, dynamic_registry, dynamic_assets, dynamic_meshes);
  Require(result.ok(), "stable exact dynamic inventory was rejected");
  for (const GraphicsSceneAssetInput &prior : first_assets) {
    const GraphicsSceneAssetInput &current =
        FindAsset(dynamic_assets, prior.source_asset_id);
    Require(SameOwner(prior.payload, current.payload) &&
                prior.material_bindings == current.material_bindings,
            "stable dynamic asset did not reuse its full canonical owner");
  }

  Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
  std::vector<GraphicsSceneAssetInput> static_assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
  const Ogre14GraphicsSceneStaticSectionCaptureInput static_input =
      MakeExactStatic(closure);
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {static_input}, static_registry, static_assets, static_meshes)
              .ok(),
          "static equivalence inventory was rejected");
  for (const GraphicsSceneAssetInput &dependency : closure->assets) {
    const GraphicsSceneAssetInput &dynamic_asset =
        FindAsset(dynamic_assets, dependency.source_asset_id);
    const GraphicsSceneAssetInput &static_asset =
        FindAsset(static_assets, dependency.source_asset_id);
    Require(SameOwner(dynamic_asset.payload, static_asset.payload) &&
                dynamic_asset.material_bindings ==
                    static_asset.material_bindings,
            "static and dynamic payload/binding equivalence rules diverged");
  }

  Ogre14GraphicsSceneResolvedMaterialFrameLineage lineage;
  Require(ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage(
              {static_input}, inputs, lineage)
              .ok() &&
              lineage.source_sequence == frame.source_sequence &&
              lineage.catalog_sequence == frame.catalog_sequence,
          "joined static/dynamic material lineage was not proven");

  Require(BuildOgre14GraphicsSceneDynamicInventory(
              {}, dynamic_registry, dynamic_assets, dynamic_meshes)
              .ok() &&
              dynamic_meshes.empty() && dynamic_assets.size() == 2U,
          "dynamic removal did not retain only its immutable base meshes");
  const std::vector<GraphicsSceneAssetInput> retained_assets = dynamic_assets;
  result = BuildOgre14GraphicsSceneDynamicInventory(
      inputs, dynamic_registry, dynamic_assets, dynamic_meshes);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              dynamic_meshes.empty() &&
              dynamic_assets.size() == retained_assets.size(),
          "removed exact dynamic identity was resurrected or mutated output");
}

void TestResolvedClosureLifecycleBelongsToTranslator() {
  using namespace RoR::Render;
  const auto closure = ResolvePaintClosure(MakeMaterialFrame());
  const Ogre14GraphicsSceneStaticSectionCaptureInput static_a =
      MakeExactStatic(closure);
  Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
  std::vector<GraphicsSceneAssetInput> static_assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {static_a}, static_registry, static_assets, static_meshes)
              .ok() &&
              static_assets.size() == closure->assets.size() + 1U &&
              static_registry.asset_identity_count() ==
                  closure->assets.size() + 1U &&
              static_registry.object_identity_count() == 1U,
          "static A did not collision-audit translator-owned closure keys");
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {}, static_registry, static_assets, static_meshes)
              .ok() &&
              static_assets.empty() && static_meshes.empty(),
          "static A could not leave without per-domain closure retention");

  Ogre14GraphicsSceneDynamicIdentityRegistry dynamic_registry;
  std::vector<GraphicsSceneAssetInput> dynamic_assets;
  std::vector<GraphicsSceneDynamicMeshInput> dynamic_meshes;
  const Ogre14GraphicsSceneDynamicSectionCaptureInput dynamic =
      MakeExactDynamic(closure);
  Require(BuildOgre14GraphicsSceneDynamicInventory(
              {dynamic}, dynamic_registry, dynamic_assets, dynamic_meshes)
              .ok() &&
              dynamic_assets.size() == closure->assets.size() + 1U &&
              dynamic_registry.asset_identity_count() ==
                  closure->assets.size() + 1U,
          "shared closure could not move from static A into the dynamic domain");
  for (const GraphicsSceneAssetInput &dependency : closure->assets) {
    const GraphicsSceneAssetInput &published =
        FindAsset(dynamic_assets, dependency.source_asset_id);
    Require(SameOwner(published.payload, dependency.payload) &&
                published.material_bindings == dependency.material_bindings,
            "dynamic interlude replaced a translator-owned closure reference");
  }

  Ogre14GraphicsSceneStaticSectionCaptureInput static_b = static_a;
  static_b.stable_object_id = 100U;
  static_b.exact_entity_name = "static-paint-proof-b";
  static_b.mesh_identity.exact_mesh_name = "static-paint-proof-b.mesh";
  static_b.render_from_object = Translation(30.0F);
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {static_b}, static_registry, static_assets, static_meshes)
              .ok() &&
              static_assets.size() == closure->assets.size() + 1U &&
              static_meshes.size() == 1U &&
              static_registry.asset_identity_count() ==
                  closure->assets.size() + 2U &&
              static_registry.object_identity_count() == 2U,
          "distinct static B could not re-enter with the shared translator closure");
  for (const GraphicsSceneAssetInput &dependency : closure->assets) {
    const GraphicsSceneAssetInput &published =
        FindAsset(static_assets, dependency.source_asset_id);
    Require(SameOwner(published.payload, dependency.payload) &&
                published.material_bindings == dependency.material_bindings,
            "static B did not preserve the translator closure owner/bindings");
  }

  const std::vector<GraphicsSceneAssetInput> accepted_assets = static_assets;
  const std::vector<GraphicsSceneStaticMeshInput> accepted_meshes =
      static_meshes;
  Ogre14GraphicsSceneStaticSectionCaptureInput resurrected_a = static_a;
  resurrected_a.mesh_identity.exact_mesh_name =
      "static-paint-proof-a-new-mesh.mesh";
  const ValidationResult resurrected =
      BuildOgre14GraphicsSceneStaticInventory(
          {resurrected_a}, static_registry, static_assets, static_meshes);
  bool unchanged =
      !resurrected && resurrected.code == ValidationCode::REVISION_MISMATCH &&
      resurrected.field.find("source_object_id") != std::string::npos &&
      resurrected.detail.find("removed static-section identity") !=
          std::string::npos &&
      static_assets.size() == accepted_assets.size() &&
      static_meshes.size() == accepted_meshes.size();
  for (std::size_t index = 0U; unchanged && index < static_assets.size();
       ++index) {
    unchanged =
        static_assets[index].source_asset_id ==
            accepted_assets[index].source_asset_id &&
        SameOwner(static_assets[index].payload,
                  accepted_assets[index].payload) &&
        static_assets[index].material_bindings ==
            accepted_assets[index].material_bindings;
  }
  Require(unchanged,
          "translator ownership weakened static A's permanent object tombstone");
}

void TestFallbackRemainsExactAndTexturedFailClosed() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionCaptureInput input = MakeFallbackDynamic();
  MaterialDescriptor expected;
  Require(BuildOgre14GraphicsSceneMaterialFallback(input.material, expected)
              .ok(),
          "factor-only expected payload could not be built");
  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneDynamicMeshInput> meshes;
  Require(BuildOgre14GraphicsSceneDynamicInventory({input}, registry, assets,
                                                    meshes)
              .ok() &&
              assets.size() == 2U && meshes.size() == 1U,
          "factor-only dynamic fallback changed behavior");
  const auto material =
      std::find_if(assets.begin(), assets.end(), [](const auto &asset) {
        return RenderAssetPayloadKind(*asset.payload) ==
               RenderAssetKind::MATERIAL;
      });
  const RenderAssetPayload expected_payload = expected;
  Require(material != assets.end() &&
              EquivalentRenderAssetPayload(*material->payload,
                                           expected_payload) &&
              std::all_of(material->material_bindings.begin(),
                          material->material_bindings.end(),
                          [](const GraphicsSceneAssetBinding &binding) {
                            return binding.texture_source_asset_id == 0U &&
                                   binding.sampler_source_asset_id == 0U;
                          }),
          "factor-only output bytes or absent bindings changed");

  input = MakeFallbackDynamic();
  input.material.texture_unit_count = 1U;
  RequireFailureUnchanged(
      {input}, ValidationCode::UNSUPPORTED_FEATURE,
      "textured dynamic material without a closure no longer fails closed");
  input = MakeFallbackDynamic();
  input.material.has_fragment_program = true;
  RequireFailureUnchanged(
      {input}, ValidationCode::UNSUPPORTED_FEATURE,
      "shader-authored dynamic material without a closure no longer fails closed");
}

void TestWindingAndJoinedLineageAreExact() {
  using namespace RoR::Render;
  const auto anticlockwise = ResolvePaintClosure(
      MakeMaterialFrame(Ogre14LegacyCullMode::ANTICLOCKWISE));
  Require(anticlockwise->requires_reverse_winding,
          "anticlockwise fixture did not require canonical winding");
  Ogre14GraphicsSceneDynamicSectionCaptureInput exact =
      MakeExactDynamic(anticlockwise);
  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneDynamicMeshInput> meshes;
  Require(BuildOgre14GraphicsSceneDynamicInventory({exact}, registry, assets,
                                                    meshes)
              .ok(),
          "closure-derived reverse winding was rejected");
  exact.mesh_reverse_winding = false;
  RequireFailureUnchanged(
      {exact}, ValidationCode::REVISION_MISMATCH,
      "dynamic exact path used fallback cull instead of closure winding");
  exact = MakeExactDynamic(anticlockwise);
  exact.material.cull = Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  RequireFailureUnchanged(
      {exact}, ValidationCode::REVISION_MISMATCH,
      "dynamic native cull disagreement was accepted");

  const auto base = ResolvePaintClosure(MakeMaterialFrame());
  auto later_value = std::make_shared<Ogre14LegacyMaterialClosure>(*base);
  later_value->source_sequence += 1U;
  const std::shared_ptr<const Ogre14LegacyMaterialClosure> later = later_value;
  Ogre14GraphicsSceneDynamicSectionCaptureInput first =
      MakeExactDynamic(base, 0U);
  Ogre14GraphicsSceneDynamicSectionCaptureInput second =
      MakeExactDynamic(later, 1U);
  RequireFailureUnchanged(
      {first, second}, ValidationCode::SEQUENCE_MISMATCH,
      "equivalent detached closures from different source epochs were merged");

  auto source_two_catalog_one =
      std::make_shared<Ogre14LegacyMaterialClosure>(*base);
  source_two_catalog_one->source_sequence = 2U;
  source_two_catalog_one->catalog_sequence = 1U;
  auto source_two_catalog_two =
      std::make_shared<Ogre14LegacyMaterialClosure>(*base);
  source_two_catalog_two->source_sequence = 2U;
  source_two_catalog_two->catalog_sequence = 2U;
  const Ogre14GraphicsSceneStaticSectionCaptureInput static_input =
      MakeExactStatic(source_two_catalog_one);
  const Ogre14GraphicsSceneDynamicSectionCaptureInput dynamic_input =
      MakeExactDynamic(source_two_catalog_two);
  Ogre14GraphicsSceneResolvedMaterialFrameLineage sentinel;
  sentinel.source_sequence = 99U;
  sentinel.catalog_sequence = 98U;
  const ValidationResult mismatch =
      ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage(
          {static_input}, {dynamic_input}, sentinel);
  Require(!mismatch && mismatch.code == ValidationCode::SEQUENCE_MISMATCH &&
              sentinel.source_sequence == 99U &&
              sentinel.catalog_sequence == 98U,
          "cross-domain catalog mismatch changed lineage sentinel state");
}

void TestHostileClosuresCollisionsAndUvGate() {
  using namespace RoR::Render;
  const auto valid = ResolvePaintClosure(MakeMaterialFrame());
  Ogre14GraphicsSceneDynamicSectionCaptureInput input =
      MakeExactDynamic(valid);

  auto forged = std::make_shared<Ogre14LegacyMaterialClosure>(*valid);
  forged->version += 1U;
  input.resolved_material = forged;
  RequireFailureUnchanged(
      {input}, ValidationCode::UNSUPPORTED_VERSION,
      "forged dynamic closure version mutated transaction state");

  forged = std::make_shared<Ogre14LegacyMaterialClosure>(*valid);
  forged->source_sequence = 0U;
  input = MakeExactDynamic(valid);
  input.resolved_material = forged;
  RequireFailureUnchanged(
      {input}, ValidationCode::SEQUENCE_MISMATCH,
      "stale dynamic closure lineage mutated transaction state");

  input = MakeExactDynamic(valid);
  input.material.exact_name = "Vehicle/Forged";
  RequireFailureUnchanged(
      {input}, ValidationCode::INVALID_IDENTIFIER,
      "closure was accepted under a forged group/name identity");

  forged = std::make_shared<Ogre14LegacyMaterialClosure>(*valid);
  std::swap(forged->assets[0U], forged->assets[1U]);
  std::swap(forged->asset_keys[0U], forged->asset_keys[1U]);
  input = MakeExactDynamic(valid);
  input.resolved_material = forged;
  RequireFailureUnchanged(
      {input}, ValidationCode::WRONG_ASSET_KIND,
      "forged dynamic dependency order mutated transaction state");

  auto payload_conflict =
      std::make_shared<Ogre14LegacyMaterialClosure>(*valid);
  TextureResourceDescriptor changed_texture =
      std::get<TextureResourceDescriptor>(
          *payload_conflict->assets[0U].payload);
  changed_texture.mip_levels[0U].bytes[0U] ^= 1U;
  payload_conflict->assets[0U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(changed_texture));
  Require(ValidateOgre14LegacyMaterialClosure(*payload_conflict,
                                               PaintMaterialKey())
              .ok(),
          "payload-collision fixture was not independently valid");
  Ogre14GraphicsSceneDynamicSectionCaptureInput first =
      MakeExactDynamic(valid, 0U);
  Ogre14GraphicsSceneDynamicSectionCaptureInput second =
      MakeExactDynamic(payload_conflict, 1U);
  RequireFailureUnchanged(
      {first, second}, ValidationCode::REVISION_MISMATCH,
      "shared dynamic dependency ID accepted conflicting payloads");

  auto binding_conflict =
      std::make_shared<Ogre14LegacyMaterialClosure>(*valid);
  Ogre14LegacyAssetKey alternate_key = binding_conflict->asset_keys[0U];
  alternate_key.exact_name += "-alternate";
  std::uint64_t alternate_id = 0U;
  Require(DeriveOgre14LegacySourceAssetId(
              RenderAssetKind::TEXTURE, alternate_key, alternate_id)
              .ok(),
          "could not derive alternate dynamic texture identity");
  TextureResourceDescriptor alternate_texture =
      std::get<TextureResourceDescriptor>(
          *binding_conflict->assets[0U].payload);
  alternate_texture.debug_name = "General/Vehicle/PaintAlbedo-alternate";
  binding_conflict->asset_keys[0U] = alternate_key;
  binding_conflict->assets[0U].source_asset_id = alternate_id;
  binding_conflict->assets[0U].payload =
      std::make_shared<const RenderAssetPayload>(
          std::move(alternate_texture));
  Ogre14LegacyMaterialPipelineAudit alternate_audit =
      *binding_conflict->material_audit;
  alternate_audit.texture_source_asset_id = alternate_id;
  binding_conflict->material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
          std::move(alternate_audit));
  binding_conflict->assets.back()
      .material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)]
      .texture_source_asset_id = alternate_id;
  Require(ValidateOgre14LegacyMaterialClosure(*binding_conflict,
                                               PaintMaterialKey())
              .ok(),
          "binding-collision fixture was not independently valid");
  second = MakeExactDynamic(binding_conflict, 1U);
  RequireFailureUnchanged(
      {first, second}, ValidationCode::REVISION_MISMATCH,
      "shared dynamic material ID accepted conflicting exact bindings");

  input = MakeExactDynamic(valid);
  MeshResourceDescriptor missing_uv =
      std::get<MeshResourceDescriptor>(*input.mesh_payload);
  missing_uv.texture_coordinates_0.clear();
  input.mesh_payload =
      std::make_shared<const RenderAssetPayload>(std::move(missing_uv));
  RequireFailureUnchanged(
      {input}, ValidationCode::MISSING_REFERENCE,
      "producer-bound deformable material accepted a missing authored UV");

  Ogre14GraphicsSceneDynamicIdentityRegistry collision_registry;
  const std::uint64_t texture_id = valid->assets[0U].source_asset_id;
  Require(collision_registry
              .RegisterDerivedAssetIdentity("forged-mesh-identity", texture_id)
              .ok(),
          "could not seed a dynamic cross-kind collision");
  std::vector<GraphicsSceneAssetInput> assets(1U);
  assets.front().source_asset_id = 123U;
  std::vector<GraphicsSceneDynamicMeshInput> meshes(1U);
  meshes.front().source_object_id = 456U;
  const ValidationResult collision = BuildOgre14GraphicsSceneDynamicInventory(
      {MakeExactDynamic(valid)}, collision_registry, assets, meshes);
  Require(!collision &&
              collision.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              collision_registry.asset_identity_count() == 1U &&
              collision_registry.object_identity_count() == 0U &&
              assets.size() == 1U && assets.front().source_asset_id == 123U &&
              meshes.size() == 1U && meshes.front().source_object_id == 456U,
          "dynamic mesh/dependency source-ID collision was not transactional");
}

void TestCapsAndExceptionRollback() {
  using namespace RoR::Render;
  std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> over_cap(
      kMaximumOgre14GraphicsSceneDynamicSections + 1U);
  RequireFailureUnchanged(
      over_cap, ValidationCode::VALUE_OUT_OF_RANGE,
      "dynamic hostile section cap+1 was not rejected before allocation");

  const auto closure = ResolvePaintClosure(MakeMaterialFrame());
  Ogre14GraphicsSceneDynamicIdentityRegistry lifetime_registry;
  for (std::size_t index = 0U;
       index < kMaximumOgre14GraphicsSceneDynamicAssets; ++index) {
    Require(lifetime_registry
                .RegisterDerivedAssetIdentity(
                    "dynamic-lifetime-cap-seed-" + std::to_string(index),
                    static_cast<std::uint64_t>(index) + 1U)
                .ok(),
            "could not seed the exact dynamic identity lifetime cap");
  }
  std::vector<GraphicsSceneAssetInput> assets(1U);
  assets.front().source_asset_id = 321U;
  std::vector<GraphicsSceneDynamicMeshInput> meshes(1U);
  meshes.front().source_object_id = 654U;
  const ValidationResult lifetime = BuildOgre14GraphicsSceneDynamicInventory(
      {MakeExactDynamic(closure)}, lifetime_registry, assets, meshes);
  Require(!lifetime && lifetime.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              lifetime_registry.asset_identity_count() ==
                  kMaximumOgre14GraphicsSceneDynamicAssets &&
              lifetime_registry.object_identity_count() == 0U &&
              assets.size() == 1U && assets.front().source_asset_id == 321U &&
              meshes.size() == 1U && meshes.front().source_object_id == 654U,
          "dynamic lifetime cap+1 published partial state");
}

class ThrowingDynamicFault final
    : public RoR::Render::IOgre14GraphicsSceneDynamicInventoryFaultInjector {
public:
  explicit ThrowingDynamicFault(bool allocation) noexcept
      : allocation_(allocation) {}

  void AtFaultPoint(
      RoR::Render::Ogre14GraphicsSceneDynamicInventoryFaultPoint) override {
    if (allocation_) {
      throw std::bad_alloc{};
    }
    throw std::runtime_error("deterministic dynamic-material fault");
  }

private:
  bool allocation_ = false;
};

void TestInjectedExceptionRollback() {
  using namespace RoR::Render;
  const auto closure = ResolvePaintClosure(MakeMaterialFrame());
  const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> inputs{
      MakeExactDynamic(closure)};
  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneDynamicMeshInput> meshes;
  const Ogre14GraphicsSceneDynamicSectionCaptureInput fallback =
      MakeFallbackDynamic();
  Require(BuildOgre14GraphicsSceneDynamicInventory(
              {fallback}, registry, assets, meshes)
              .ok(),
          "could not seed deep dynamic rollback state");
  const std::vector<GraphicsSceneAssetInput> accepted_assets = assets;
  const std::vector<GraphicsSceneDynamicMeshInput> accepted_meshes = meshes;
  const std::size_t accepted_asset_count = registry.asset_identity_count();
  const std::size_t accepted_object_count = registry.object_identity_count();
  const auto require_deep_unchanged =
      [&](ValidationCode expected_code,
          IOgre14GraphicsSceneDynamicInventoryFaultInjector &fault,
          const char *message) {
        const ValidationResult result =
            BuildOgre14GraphicsSceneDynamicInventory(
                inputs, registry, assets, meshes, &fault);
        bool exact = !result && result.code == expected_code &&
                     registry.asset_identity_count() ==
                         accepted_asset_count &&
                     registry.object_identity_count() ==
                         accepted_object_count &&
                     assets.size() == accepted_assets.size() &&
                     meshes.size() == accepted_meshes.size();
        for (std::size_t index = 0U; exact && index < assets.size(); ++index) {
          exact = assets[index].source_asset_id ==
                      accepted_assets[index].source_asset_id &&
                  SameOwner(assets[index].payload,
                            accepted_assets[index].payload) &&
                  assets[index].material_bindings ==
                      accepted_assets[index].material_bindings;
        }
        for (std::size_t index = 0U; exact && index < meshes.size(); ++index) {
          exact = meshes[index].source_object_id ==
                      accepted_meshes[index].source_object_id &&
                  meshes[index].mesh_source_asset_id ==
                      accepted_meshes[index].mesh_source_asset_id &&
                  meshes[index].material_source_asset_id ==
                      accepted_meshes[index].material_source_asset_id &&
                  meshes[index].render_from_object ==
                      accepted_meshes[index].render_from_object &&
                  meshes[index].visibility_mask ==
                      accepted_meshes[index].visibility_mask &&
                  meshes[index].flags == accepted_meshes[index].flags &&
                  SameOwner(meshes[index].state,
                            accepted_meshes[index].state);
        }
        Require(exact, message);
      };
  ThrowingDynamicFault allocation(true);
  require_deep_unchanged(
      ValidationCode::EMPTY_PAYLOAD, allocation,
      "allocation exception changed deep registry/output owners or values");
  RequireFailureUnchanged(
      inputs, ValidationCode::EMPTY_PAYLOAD,
      "allocation exception after dynamic dependency changed durable state",
      &allocation);
  ThrowingDynamicFault unexpected(false);
  require_deep_unchanged(
      ValidationCode::UNSUPPORTED_FEATURE, unexpected,
      "unexpected exception changed deep registry/output owners or values");
  RequireFailureUnchanged(
      inputs, ValidationCode::UNSUPPORTED_FEATURE,
      "unexpected exception after dynamic dependency changed durable state",
      &unexpected);

  Require(BuildOgre14GraphicsSceneDynamicInventory(
              {fallback}, registry, assets, meshes)
              .ok(),
          "stable registry was poisoned by injected dynamic exceptions");
  for (std::size_t index = 0U; index < assets.size(); ++index) {
    Require(SameOwner(assets[index].payload,
                      accepted_assets[index].payload) &&
                assets[index].material_bindings ==
                    accepted_assets[index].material_bindings,
            "post-fault dynamic asset owner was not reusable");
  }
  Require(SameOwner(meshes.front().state, accepted_meshes.front().state),
          "post-fault dynamic deformation owner was not reusable");
}

} // namespace

int main() {
  TestExactDynamicClosureSharedOwnersAndStaticEquivalence();
  TestResolvedClosureLifecycleBelongsToTranslator();
  TestFallbackRemainsExactAndTexturedFailClosed();
  TestWindingAndJoinedLineageAreExact();
  TestHostileClosuresCollisionsAndUvGate();
  TestCapsAndExceptionRollback();
  TestInjectedExceptionRollback();
  return EXIT_SUCCESS;
}
