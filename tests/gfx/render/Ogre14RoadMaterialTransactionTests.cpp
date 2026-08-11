/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14ProceduralRoadSource.h"

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
    std::cerr << "OGRE 14 road-material transaction test failed: " << message
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

RoR::Render::Ogre14LegacyTextureInput
MakeTexture(std::string name = "Road/Asphalt") {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture;
  texture.key.exact_resource_group = "General";
  texture.key.exact_name = std::move(name);
  texture.source_revision = 1U;
  texture.width = 1U;
  texture.height = 1U;
  Ogre14LegacyTextureMipInput mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.slice_pitch_bytes = 4U;
  mip.bytes = {51U, 68U, 85U, 255U};
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RoR::Render::Ogre14LegacyMaterialInput
MakeRoadMaterial(const RoR::Render::Ogre14LegacyTextureInput &texture,
                 bool reverse_winding = false,
                 bool legacy_alpha_greater = false) {
  using namespace RoR::Render;
  Ogre14LegacyMaterialInput material;
  material.key.exact_resource_group = "General";
  material.key.exact_name = "road2";
  material.source_revision = 1U;
  material.diffuse_linear = {0.5F, 0.5F, 0.5F, 1.0F};
  material.base_color_semantic = Ogre14LegacyBaseColorSemantic::UNLIT;
  material.lighting_enabled = false;
  material.pipeline.cull = reverse_winding ? Ogre14LegacyCullMode::ANTICLOCKWISE
                                           : Ogre14LegacyCullMode::CLOCKWISE;
  if (legacy_alpha_greater) {
    material.pipeline.source_color = Ogre14LegacyBlendFactor::SOURCE_ALPHA;
    material.pipeline.destination_color =
        Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
    material.pipeline.source_alpha = Ogre14LegacyBlendFactor::SOURCE_ALPHA;
    material.pipeline.destination_alpha =
        Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
    material.pipeline.alpha_reject = Ogre14LegacyCompareOperation::GREATER;
    material.pipeline.alpha_reject_value = 2U;
    material.pipeline.depth_write_enabled = false;
  }
  Ogre14LegacyTextureUnitInput unit;
  unit.texture_key = texture.key;
  unit.sampler.source_revision = 1U;
  unit.sampler.maximum_lod = 0.0F;
  material.texture_units.push_back(std::move(unit));
  return material;
}

RoR::Render::Ogre14LegacyTranslatedFrame
MakeMaterialFrame(bool reverse_winding = false,
                  bool legacy_alpha_greater = false) {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput input;
  input.source_sequence = 1U;
  input.textures.push_back(MakeTexture());
  input.materials.push_back(
      MakeRoadMaterial(input.textures.front(), reverse_winding,
                       legacy_alpha_greater));
  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame frame;
  Require(translator.Translate(input, frame).ok() && frame.full_snapshot,
          "could not create the authoritative road-material fixture");
  return frame;
}

const RoR::Render::Ogre14LegacyTranslatedAsset &
FindAsset(const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
          RoR::Render::RenderAssetKind kind) {
  const auto found =
      std::find_if(frame.live_assets.begin(), frame.live_assets.end(),
                   [kind](const auto &asset) { return asset.kind == kind; });
  Require(found != frame.live_assets.end(), "fixture asset is absent");
  return *found;
}

RoR::Render::Ogre14LegacyAssetKey RoadMaterialKey() {
  RoR::Render::Ogre14LegacyAssetKey key;
  key.exact_resource_group = "General";
  key.exact_name = "road2";
  return key;
}

RoR::Render::Ogre14ProceduralRoadCapture
MakeRoad(const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
         std::uint64_t id = 1U) {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedAsset &translated =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  const MaterialDescriptor &descriptor =
      std::get<MaterialDescriptor>(*translated.payload);
  const Ogre14LegacyMaterialPipelineAudit &audit = *translated.material_audit;

  Ogre14ProceduralRoadCapture capture;
  capture.stable_graphics_id = id;
  capture.topology_revision = 1U;
  capture.exact_native_mesh_resource_group = "General";
  capture.exact_native_mesh_name = "RoadSystem-native-" + std::to_string(id);
  capture.exact_native_entity_name =
      "RoadSystem_Instance-native-" + std::to_string(id);
  capture.positions = {{0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F},
                       {1.0F, 1.0F, 0.0F},
                       {0.0F, 1.0F, 0.0F}};
  capture.exact_render_normals = {{0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F}};
  capture.texture_coordinates_0 = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
  capture.indices = {0U, 1U, 2U, 0U, 2U, 3U};
  capture.material.exact_resource_group = "General";
  capture.material.exact_name = "road2";
  capture.material.pass_count = 1U;
  capture.material.texture_unit_count = 1U;
  capture.material.lighting_enabled = false;
  capture.material.diffuse_linear = descriptor.base_color_factor;
  capture.material.ambient_linear = {};
  capture.material.specular_linear = {};
  capture.material.emissive_linear = {};
  capture.material.shininess = 0.0F;
  switch (audit.pipeline.cull) {
  case Ogre14LegacyCullMode::NONE:
    capture.material.cull = Ogre14GraphicsSceneMaterialCull::NONE;
    break;
  case Ogre14LegacyCullMode::CLOCKWISE:
    capture.material.cull = Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
    break;
  case Ogre14LegacyCullMode::ANTICLOCKWISE:
    capture.material.cull = Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
    break;
  }
  const bool true_source_over =
      audit.pipeline.source_color == Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
      audit.pipeline.destination_color ==
          Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA &&
      audit.pipeline.source_alpha == Ogre14LegacyBlendFactor::ONE &&
      audit.pipeline.destination_alpha ==
          Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
  const bool legacy_alpha =
      audit.pipeline.source_color == Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
      audit.pipeline.destination_color ==
          Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA &&
      audit.pipeline.source_alpha == Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
      audit.pipeline.destination_alpha ==
          Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
  capture.material.blend =
      true_source_over
          ? Ogre14GraphicsSceneMaterialBlend::STRAIGHT_SOURCE_OVER
          : legacy_alpha
                ? Ogre14GraphicsSceneMaterialBlend::LEGACY_STRAIGHT_ALPHA
                : Ogre14GraphicsSceneMaterialBlend::REPLACE;
  capture.material.alpha_reject =
      audit.pipeline.alpha_reject == Ogre14LegacyCompareOperation::GREATER
          ? Ogre14GraphicsSceneMaterialAlphaReject::GREATER
          : audit.pipeline.alpha_reject ==
                    Ogre14LegacyCompareOperation::GREATER_EQUAL
                ? Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL
                : Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
  capture.material.alpha_reject_value = audit.pipeline.alpha_reject_value;
  capture.material.depth_write = audit.pipeline.depth_write_enabled;
  capture.native_material_audit_complete = true;
  capture.exact_native_material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(audit);
  capture.finalized = true;
  return capture;
}

struct PreparedSections {
  RoR::Render::Ogre14ProceduralRoadInventory inventory;
  std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
      sections;
};

PreparedSections PrepareExactSections(
    const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
    std::vector<RoR::Render::Ogre14ProceduralRoadCapture> roads) {
  using namespace RoR::Render;
  PreparedSections prepared;
  const ValidationResult validation = BuildOgre14ProceduralRoadInventory(
      roads, frame, prepared.inventory, prepared.sections);
  Require(validation.ok(), "valid exact road inventory was rejected");
  return prepared;
}

void RequireStaticFailureUnchanged(
    const std::vector<RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput>
        &sections,
    RoR::Render::ValidationCode expected_code, const char *message,
    RoR::Render::IOgre14GraphicsSceneStaticInventoryFaultInjector *fault =
        nullptr) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets(1U);
  assets.front().source_asset_id = 991U;
  std::vector<GraphicsSceneStaticMeshInput> meshes(1U);
  meshes.front().source_object_id = 992U;
  const ValidationResult result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, meshes, fault);
  Require(!result && result.code == expected_code &&
              registry.asset_identity_count() == 0U &&
              registry.object_identity_count() == 0U && assets.size() == 1U &&
              assets.front().source_asset_id == 991U && meshes.size() == 1U &&
              meshes.front().source_object_id == 992U,
          message);
}

void TestExactRoadClosureAndSharedOwners() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  PreparedSections prepared =
      PrepareExactSections(frame, {MakeRoad(frame, 2U), MakeRoad(frame, 1U)});
  Require(
      prepared.sections.size() == 2U &&
          prepared.sections[0U].resolved_material != nullptr &&
          SameOwner(prepared.sections[0U].resolved_material,
                    prepared.sections[1U].resolved_material),
      "same exact road2 material did not reuse one immutable closure owner");
  const Ogre14LegacyMaterialClosure &closure =
      *prepared.sections.front().resolved_material;
  Require(closure.source_sequence == frame.source_sequence &&
              closure.catalog_sequence == frame.catalog_sequence &&
              closure.assets.size() == 3U &&
              closure.asset_keys.size() == closure.assets.size() &&
              SameOwner(closure.assets[0U].payload,
                        FindAsset(frame, RenderAssetKind::TEXTURE).payload) &&
              SameOwner(closure.assets[1U].payload,
                        FindAsset(frame, RenderAssetKind::SAMPLER).payload) &&
              SameOwner(closure.assets[2U].payload,
                        FindAsset(frame, RenderAssetKind::MATERIAL).payload),
          "road closure lost authoritative lineage, keys, or payload owners");

  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> meshes;
  ValidationResult result = BuildOgre14GraphicsSceneStaticInventory(
      prepared.sections, registry, assets, meshes);
  Require(
      result.ok() && assets.size() == 5U && meshes.size() == 2U &&
          std::all_of(meshes.begin(), meshes.end(),
                      [&closure](const auto &mesh) {
                        return mesh.material_source_asset_id ==
                               closure.material_source_asset_id;
                      }),
      "resolved two-road static transaction did not deduplicate dependencies");
  const auto material =
      std::find_if(assets.begin(), assets.end(), [](const auto &asset) {
        return RenderAssetPayloadKind(*asset.payload) ==
               RenderAssetKind::MATERIAL;
      });
  Require(material != assets.end(), "resolved material is absent");
  const GraphicsSceneAssetBinding &binding =
      material->material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)];
  Require(
      binding.texture_source_asset_id == closure.assets[0U].source_asset_id &&
          binding.sampler_source_asset_id == closure.assets[1U].source_asset_id,
      "static transaction lost exact road2 texture/sampler bindings");

  const auto first_texture =
      std::find_if(assets.begin(), assets.end(), [](const auto &asset) {
        return RenderAssetPayloadKind(*asset.payload) ==
               RenderAssetKind::TEXTURE;
      });
  const auto first_sampler =
      std::find_if(assets.begin(), assets.end(), [](const auto &asset) {
        return RenderAssetPayloadKind(*asset.payload) ==
               RenderAssetKind::SAMPLER;
      });
  Require(first_texture != assets.end() && first_sampler != assets.end() &&
              SameOwner(first_texture->payload, closure.assets[0U].payload) &&
              SameOwner(first_sampler->payload, closure.assets[1U].payload),
          "static transaction replaced shared immutable dependency owners");
}

void TestExactWindingCacheReplacementAndRollback() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame clockwise_frame = MakeMaterialFrame();
  const Ogre14LegacyTranslatedFrame anticlockwise_frame =
      MakeMaterialFrame(true);
  Ogre14ProceduralRoadInventoryConfiguration configuration;
  configuration.maximum_live_roads = 1U;
  configuration.maximum_lifetime_roads = 1U;
  configuration.maximum_payload_bytes = 152U;
  Ogre14ProceduralRoadInventory inventory(configuration);
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections;

  const Ogre14ProceduralRoadCapture clockwise = MakeRoad(clockwise_frame);
  ValidationResult result = BuildOgre14ProceduralRoadInventory(
      {clockwise}, clockwise_frame, inventory, sections);
  Require(result.ok() && sections.size() == 1U &&
              !sections.front().mesh_identity.reverse_winding,
          "exact clockwise road did not fill the exact payload cap");
  const auto clockwise_owner = sections.front().mesh_payload;

  const Ogre14ProceduralRoadCapture anticlockwise =
      MakeRoad(anticlockwise_frame);
  result = BuildOgre14ProceduralRoadInventory(
      {anticlockwise}, anticlockwise_frame, inventory, sections);
  Require(
      result.ok() && sections.size() == 1U &&
          sections.front().mesh_identity.reverse_winding &&
          sections.front().resolved_material != nullptr &&
          sections.front().resolved_material->requires_reverse_winding &&
          !SameOwner(clockwise_owner, sections.front().mesh_payload) &&
          inventory.known_identity_count() == 1U &&
          inventory.live_identity_count() == 1U,
      "same-geometry exact cull flip reused the old owner or exceeded caps");
  const MeshResourceDescriptor &reversed_mesh =
      std::get<MeshResourceDescriptor>(*sections.front().mesh_payload);
  Require(reversed_mesh.topology_revision == 1U &&
              reversed_mesh.indices ==
                  std::vector<std::uint32_t>{0U, 2U, 1U, 0U, 3U, 2U},
          "exact cull flip did not replace index bytes at the same topology "
          "revision");

  const auto anticlockwise_owner = sections.front().mesh_payload;
  result = BuildOgre14ProceduralRoadInventory(
      {anticlockwise}, anticlockwise_frame, inventory, sections);
  Require(result.ok() &&
              SameOwner(anticlockwise_owner, sections.front().mesh_payload),
          "unchanged exact winding did not reuse the immutable owner");

  Ogre14ProceduralRoadCapture forged = anticlockwise;
  forged.material.cull = Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  const std::size_t known_before = inventory.known_identity_count();
  const std::size_t live_before = inventory.live_identity_count();
  result = BuildOgre14ProceduralRoadInventory({forged}, anticlockwise_frame,
                                              inventory, sections);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              inventory.known_identity_count() == known_before &&
              inventory.live_identity_count() == live_before &&
              sections.size() == 1U &&
              SameOwner(anticlockwise_owner, sections.front().mesh_payload),
          "forged admitted cull mismatch mutated cache or published output");
}

void TestActivationGateAndNativeAuditEquality() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  Ogre14ProceduralRoadCapture road = MakeRoad(frame);
  Ogre14ProceduralRoadInventory inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections(1U);
  sections.front().stable_object_id = 77U;

  road.exact_native_material_audit.reset();
  ValidationResult result =
      BuildOgre14ProceduralRoadInventory({road}, frame, inventory, sections);
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
              inventory.known_identity_count() == 0U && sections.size() == 1U &&
              sections.front().stable_object_id == 77U,
          "missing exact native audit was promoted to resolved state");

  road = MakeRoad(frame);
  Ogre14LegacyMaterialPipelineAudit mismatched =
      *road.exact_native_material_audit;
  mismatched.pipeline.depth_write_enabled =
      !mismatched.pipeline.depth_write_enabled;
  road.exact_native_material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(mismatched);
  result =
      BuildOgre14ProceduralRoadInventory({road}, frame, inventory, sections);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              inventory.known_identity_count() == 0U,
          "bit-different independent native audit was accepted");

  road = MakeRoad(frame);
  road.material.diffuse_linear.x = 0.25F;
  result =
      BuildOgre14ProceduralRoadInventory({road}, frame, inventory, sections);
  Require(!result && result.field == "road.material.native_semantics" &&
              inventory.known_identity_count() == 0U,
          "base-factor drift was coerced to translated material state");

  road = MakeRoad(frame);
  result = BuildOgre14ProceduralRoadInventory({road}, inventory, sections);
  Require(
      !result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
          result.field == "road.material.texture_units" &&
          inventory.known_identity_count() == 0U,
      "legacy overload no longer preserves the exact textured-road blocker");

  Ogre14LegacyTranslatedFrame forged = frame;
  forged.catalog_sequence = forged.source_sequence + 1U;
  result = BuildOgre14ProceduralRoadInventory({MakeRoad(frame)}, forged,
                                              inventory, sections);
  Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
              inventory.known_identity_count() == 0U,
          "forged authoritative catalog lineage was accepted");
}

void TestDetachedClosureHostileMutations() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  Ogre14LegacyMaterialClosure valid;
  Require(
      ResolveOgre14LegacyMaterialClosure(frame, RoadMaterialKey(), valid)
              .ok() &&
          ValidateOgre14LegacyMaterialClosure(valid, RoadMaterialKey()).ok(),
      "valid detached road2 closure was rejected");

  const auto require_invalid = [&](Ogre14LegacyMaterialClosure forged,
                                   const char *message) {
    Require(!ValidateOgre14LegacyMaterialClosure(forged, RoadMaterialKey()),
            message);
  };
  Ogre14LegacyMaterialClosure forged = valid;
  forged.version += 1U;
  require_invalid(std::move(forged), "forged closure version was accepted");
  forged = valid;
  forged.source_sequence = 0U;
  require_invalid(std::move(forged), "zero closure lineage was accepted");
  forged = valid;
  forged.catalog_sequence = forged.source_sequence + 1U;
  require_invalid(std::move(forged), "stale closure catalog was accepted");
  forged = valid;
  forged.material_source_asset_id ^= 1U;
  require_invalid(std::move(forged), "wrong exact material ID was accepted");
  forged = valid;
  forged.assets[0U].source_asset_id ^= 1U;
  require_invalid(std::move(forged), "wrong exact texture ID was accepted");
  forged = valid;
  std::swap(forged.assets[0U], forged.assets[1U]);
  std::swap(forged.asset_keys[0U], forged.asset_keys[1U]);
  require_invalid(std::move(forged),
                  "wrong dependency kind/order was accepted");
  forged = valid;
  forged.assets.back()
      .material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)]
      .texture_source_asset_id ^= 1U;
  require_invalid(std::move(forged), "wrong material binding was accepted");
  forged = valid;
  forged.requires_reverse_winding = !forged.requires_reverse_winding;
  require_invalid(std::move(forged), "wrong closure winding was accepted");
  forged = valid;
  forged.asset_keys[0U].exact_name = "Road/Forged";
  require_invalid(std::move(forged),
                  "forged texture key/payload/ID association was accepted");
  forged = valid;
  TextureResourceDescriptor foreign =
      std::get<TextureResourceDescriptor>(*valid.assets[0U].payload);
  foreign.debug_name = "General/Road/Other";
  forged.assets[0U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(foreign));
  require_invalid(
      std::move(forged),
      "foreign valid-shaped texture payload was accepted under the road key");

  forged = valid;
  MaterialDescriptor workflow_mutation =
      std::get<MaterialDescriptor>(*valid.assets.back().payload);
  workflow_mutation.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  workflow_mutation.metallic_factor = 0.0F;
  workflow_mutation.specular_factor = {0.5F, 0.5F, 0.5F};
  forged.assets.back().payload =
      std::make_shared<const RenderAssetPayload>(workflow_mutation);
  require_invalid(std::move(forged),
                  "forged PBR workflow/specular factor escaped closure");

  forged = valid;
  MaterialDescriptor specular_binding_mutation =
      std::get<MaterialDescriptor>(*valid.assets.back().payload);
  specular_binding_mutation.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  specular_binding_mutation.metallic_factor = 0.0F;
  specular_binding_mutation.specular_texture =
      specular_binding_mutation.base_color_texture;
  forged.assets.back().payload =
      std::make_shared<const RenderAssetPayload>(specular_binding_mutation);
  require_invalid(std::move(forged),
                  "forged SPECULAR binding escaped closure provenance");
}

void TestExactLegacyAlphaGreaterRoadTuple() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame(false, true);
  const Ogre14ProceduralRoadCapture road = MakeRoad(frame);
  Ogre14ProceduralRoadInventory inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections;
  const ValidationResult result = BuildOgre14ProceduralRoadInventory(
      {road}, frame, inventory, sections);
  Require(result.ok() && sections.size() == 1U &&
              sections.front().resolved_material != nullptr,
          "exact legacy-alpha/GREATER/depth-write-off road was rejected");
  const MaterialDescriptor &material = std::get<MaterialDescriptor>(
      *sections.front().resolved_material->assets.back().payload);
  Require(material.blend_mode == MaterialBlendMode::LEGACY_STRAIGHT_ALPHA &&
              material.alpha_test_mode == MaterialAlphaTestMode::GREATER &&
              material.alpha_cutoff == 2.0F / 255.0F &&
              !material.depth_write,
          "legacy-alpha/GREATER/depth tuple changed during road translation");
}

void TestStaticHostileTransactionsAndLineage() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  PreparedSections prepared =
      PrepareExactSections(frame, {MakeRoad(frame, 1U), MakeRoad(frame, 2U)});

  auto mismatched_lineage = prepared.sections;
  Ogre14LegacyMaterialClosure stale = *mismatched_lineage[1U].resolved_material;
  stale.source_sequence += 1U;
  mismatched_lineage[1U].resolved_material =
      std::make_shared<const Ogre14LegacyMaterialClosure>(std::move(stale));
  RequireStaticFailureUnchanged(
      mismatched_lineage, ValidationCode::SEQUENCE_MISMATCH,
      "mixed closure source epochs mutated the static transaction");

  auto mismatched_catalog = prepared.sections;
  Ogre14LegacyMaterialClosure first_catalog =
      *mismatched_catalog[0U].resolved_material;
  Ogre14LegacyMaterialClosure second_catalog =
      *mismatched_catalog[1U].resolved_material;
  first_catalog.source_sequence = 2U;
  first_catalog.catalog_sequence = 1U;
  second_catalog.source_sequence = 2U;
  second_catalog.catalog_sequence = 2U;
  mismatched_catalog[0U].resolved_material =
      std::make_shared<const Ogre14LegacyMaterialClosure>(
          std::move(first_catalog));
  mismatched_catalog[1U].resolved_material =
      std::make_shared<const Ogre14LegacyMaterialClosure>(
          std::move(second_catalog));
  RequireStaticFailureUnchanged(
      mismatched_catalog, ValidationCode::SEQUENCE_MISMATCH,
      "mixed closure catalog epochs mutated the static transaction");

  auto wrong_winding = prepared.sections;
  wrong_winding.front().mesh_identity.reverse_winding = true;
  RequireStaticFailureUnchanged(
      wrong_winding, ValidationCode::REVISION_MISMATCH,
      "mesh/translated-material winding disagreement was accepted");

  auto wrong_key = prepared.sections;
  wrong_key.front().material.exact_name = "road2-forged";
  RequireStaticFailureUnchanged(
      wrong_key, ValidationCode::INVALID_IDENTIFIER,
      "closure was accepted for the wrong exact material key");

  auto conflicting = prepared.sections;
  Ogre14LegacyMaterialClosure conflict = *conflicting[1U].resolved_material;
  TextureResourceDescriptor changed_texture =
      std::get<TextureResourceDescriptor>(*conflict.assets[0U].payload);
  changed_texture.mip_levels[0U].bytes[0U] ^= 1U;
  conflict.assets[0U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(changed_texture));
  conflicting[1U].resolved_material =
      std::make_shared<const Ogre14LegacyMaterialClosure>(std::move(conflict));
  RequireStaticFailureUnchanged(
      conflicting, ValidationCode::REVISION_MISMATCH,
      "shared dependency ID accepted conflicting immutable payloads");

  auto binding_conflicting = prepared.sections;
  Ogre14LegacyMaterialClosure binding_conflict =
      *binding_conflicting[1U].resolved_material;
  Ogre14LegacyAssetKey alternate_texture_key = binding_conflict.asset_keys[0U];
  alternate_texture_key.exact_name += "-alternate";
  std::uint64_t alternate_texture_id = 0U;
  Require(DeriveOgre14LegacySourceAssetId(RenderAssetKind::TEXTURE,
                                          alternate_texture_key,
                                          alternate_texture_id)
              .ok(),
          "could not derive alternate texture identity fixture");
  TextureResourceDescriptor alternate_texture =
      std::get<TextureResourceDescriptor>(*binding_conflict.assets[0U].payload);
  alternate_texture.debug_name = "General/Road/Asphalt-alternate";
  binding_conflict.asset_keys[0U] = alternate_texture_key;
  binding_conflict.assets[0U].source_asset_id = alternate_texture_id;
  binding_conflict.assets[0U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(alternate_texture));
  Ogre14LegacyMaterialPipelineAudit alternate_audit =
      *binding_conflict.material_audit;
  alternate_audit.texture_source_asset_id = alternate_texture_id;
  binding_conflict.material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
          std::move(alternate_audit));
  binding_conflict.assets.back()
      .material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)]
      .texture_source_asset_id = alternate_texture_id;
  Require(
      ValidateOgre14LegacyMaterialClosure(binding_conflict, RoadMaterialKey())
          .ok(),
      "binding-conflict fixture was not independently valid");
  binding_conflicting[1U].resolved_material =
      std::make_shared<const Ogre14LegacyMaterialClosure>(
          std::move(binding_conflict));
  RequireStaticFailureUnchanged(
      binding_conflicting, ValidationCode::REVISION_MISMATCH,
      "shared material ID accepted conflicting producer-owned bindings");

  auto missing_uv = std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>{
      prepared.sections.front()};
  MeshResourceDescriptor mesh_without_uv =
      std::get<MeshResourceDescriptor>(*missing_uv.front().mesh_payload);
  mesh_without_uv.texture_coordinates_0.clear();
  missing_uv.front().mesh_payload =
      std::make_shared<const RenderAssetPayload>(std::move(mesh_without_uv));
  RequireStaticFailureUnchanged(
      missing_uv, ValidationCode::MISSING_REFERENCE,
      "producer-bound road material accepted a mesh without its authored UVs");

  Ogre14GraphicsSceneStaticIdentityRegistry collision_registry;
  const std::uint64_t texture_id =
      prepared.sections.front().resolved_material->assets[0U].source_asset_id;
  Require(collision_registry
              .RegisterDerivedAssetIdentity("forged-mesh-identity", texture_id)
              .ok(),
          "could not seed cross-kind asset collision fixture");
  std::vector<GraphicsSceneAssetInput> assets(1U);
  assets.front().source_asset_id = 123U;
  std::vector<GraphicsSceneStaticMeshInput> meshes(1U);
  meshes.front().source_object_id = 456U;
  const ValidationResult collision = BuildOgre14GraphicsSceneStaticInventory(
      {prepared.sections.front()}, collision_registry, assets, meshes);
  Require(!collision &&
              collision.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              collision_registry.asset_identity_count() == 1U &&
              assets.size() == 1U && assets.front().source_asset_id == 123U &&
              meshes.size() == 1U && meshes.front().source_object_id == 456U,
          "mesh/dependency source-ID collision was not transactional");

  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> over_cap(
      kMaximumOgre14GraphicsSceneStaticSections + 1U);
  RequireStaticFailureUnchanged(
      over_cap, ValidationCode::VALUE_OUT_OF_RANGE,
      "static hostile-input cap+1 was not rejected before allocation");

  Ogre14GraphicsSceneStaticIdentityRegistry lifetime_cap_registry;
  for (std::size_t index = 0U; index < kMaximumOgre14GraphicsSceneStaticAssets;
       ++index) {
    Require(lifetime_cap_registry
                .RegisterDerivedAssetIdentity(
                    "lifetime-cap-seed-" + std::to_string(index),
                    static_cast<std::uint64_t>(index) + 1U)
                .ok(),
            "could not seed the exact static identity lifetime cap");
  }
  assets.front().source_asset_id = 321U;
  meshes.front().source_object_id = 654U;
  const ValidationResult lifetime_cap = BuildOgre14GraphicsSceneStaticInventory(
      {prepared.sections.front()}, lifetime_cap_registry, assets, meshes);
  Require(!lifetime_cap &&
              lifetime_cap.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              lifetime_cap_registry.asset_identity_count() ==
                  kMaximumOgre14GraphicsSceneStaticAssets &&
              assets.size() == 1U && assets.front().source_asset_id == 321U &&
              meshes.size() == 1U && meshes.front().source_object_id == 654U,
          "static lifetime cap+1 committed a partial identity transaction");
}

class ThrowingStaticFault final
    : public RoR::Render::IOgre14GraphicsSceneStaticInventoryFaultInjector {
public:
  explicit ThrowingStaticFault(bool allocation) noexcept
      : allocation_(allocation) {}

  void AtFaultPoint(
      RoR::Render::Ogre14GraphicsSceneStaticInventoryFaultPoint) override {
    if (allocation_) {
      throw std::bad_alloc{};
    }
    throw std::runtime_error("deterministic road-material fault");
  }

private:
  bool allocation_ = false;
};

void TestStaticExceptionRollback() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeMaterialFrame();
  PreparedSections prepared = PrepareExactSections(frame, {MakeRoad(frame)});
  ThrowingStaticFault allocation(true);
  RequireStaticFailureUnchanged(
      prepared.sections, ValidationCode::EMPTY_PAYLOAD,
      "allocation exception after first dependency changed durable state",
      &allocation);
  ThrowingStaticFault unexpected(false);
  RequireStaticFailureUnchanged(
      prepared.sections, ValidationCode::UNSUPPORTED_FEATURE,
      "unexpected exception after first dependency changed durable state",
      &unexpected);
}

} // namespace

int main() {
  TestExactRoadClosureAndSharedOwners();
  TestExactWindingCacheReplacementAndRollback();
  TestActivationGateAndNativeAuditEquality();
  TestDetachedClosureHostileMutations();
  TestExactLegacyAlphaGreaterRoadTuple();
  TestStaticHostileTransactionsAndLineage();
  TestStaticExceptionRollback();
  return EXIT_SUCCESS;
}
