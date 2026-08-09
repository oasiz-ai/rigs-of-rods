/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14LegacyMaterialClosure.h"

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
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool SameOwner(
    const std::shared_ptr<const RoR::Render::RenderAssetPayload> &lhs,
    const std::shared_ptr<const RoR::Render::RenderAssetPayload> &rhs) {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

RoR::Render::Ogre14LegacyTextureInput MakeTexture(
    std::string name = "City/BaseColor") {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture;
  texture.key.exact_resource_group = "CityWorld";
  texture.key.exact_name = std::move(name);
  texture.source_revision = 1U;
  texture.width = 1U;
  texture.height = 1U;
  Ogre14LegacyTextureMipInput mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.slice_pitch_bytes = 4U;
  mip.bytes = {17U, 34U, 51U, 255U};
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RoR::Render::Ogre14LegacyMaterialInput MakeMaterial(
    const RoR::Render::Ogre14LegacyTextureInput *texture,
    std::string name = "City/Facade", bool pbr = false,
    bool reverse_winding = false) {
  using namespace RoR::Render;
  Ogre14LegacyMaterialInput material;
  material.key.exact_resource_group = "CityWorld";
  material.key.exact_name = std::move(name);
  material.source_revision = 1U;
  material.diffuse_linear = {0.25F, 0.5F, 0.75F, 1.0F};
  if (pbr) {
    material.base_color_semantic =
        Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR;
    material.lighting_enabled = true;
  }
  if (reverse_winding) {
    material.pipeline.cull = Ogre14LegacyCullMode::ANTICLOCKWISE;
  }
  if (texture != nullptr) {
    Ogre14LegacyTextureUnitInput unit;
    unit.texture_key = texture->key;
    unit.sampler.source_revision = 1U;
    unit.sampler.maximum_lod = 0.0F;
    unit.texture_coordinate_set = 1U;
    material.texture_units.push_back(std::move(unit));
  }
  return material;
}

RoR::Render::Ogre14LegacyTranslatedFrame MakeTranslatedFrame(
    bool textured = true, bool pbr = false, bool reverse_winding = false,
    bool include_second_material = false) {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput input;
  input.source_sequence = 1U;
  if (textured) {
    input.textures.push_back(MakeTexture());
    input.materials.push_back(MakeMaterial(&input.textures.front(),
                                           "City/Facade", pbr,
                                           reverse_winding));
  } else {
    input.materials.push_back(
        MakeMaterial(nullptr, "City/Facade", pbr, reverse_winding));
  }
  if (include_second_material) {
    input.materials.push_back(MakeMaterial(
        textured ? &input.textures.front() : nullptr, "City/Roof"));
  }
  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame frame;
  Require(translator.Translate(input, frame).ok(),
          "translator could not build material-closure fixture");
  Require(frame.full_snapshot, "first translated frame is not a full snapshot");
  return frame;
}

RoR::Render::Ogre14LegacyAssetKey MaterialKey(
    std::string name = "City/Facade") {
  RoR::Render::Ogre14LegacyAssetKey key;
  key.exact_resource_group = "CityWorld";
  key.exact_name = std::move(name);
  return key;
}

RoR::Render::Ogre14LegacyTranslatedAsset &FindAsset(
    RoR::Render::Ogre14LegacyTranslatedFrame &frame,
    RoR::Render::RenderAssetKind kind, std::size_t ordinal = 0U) {
  for (auto &asset : frame.live_assets) {
    if (asset.kind == kind) {
      if (ordinal == 0U) {
        return asset;
      }
      --ordinal;
    }
  }
  std::cerr << "FAIL: requested fixture asset is absent\n";
  std::exit(EXIT_FAILURE);
}

const RoR::Render::Ogre14LegacyTranslatedAsset &FindAsset(
    const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
    RoR::Render::RenderAssetKind kind, std::size_t ordinal = 0U) {
  for (const auto &asset : frame.live_assets) {
    if (asset.kind == kind) {
      if (ordinal == 0U) {
        return asset;
      }
      --ordinal;
    }
  }
  std::cerr << "FAIL: requested const fixture asset is absent\n";
  std::exit(EXIT_FAILURE);
}

void SyncMutation(RoR::Render::Ogre14LegacyTranslatedFrame &frame,
                  const RoR::Render::Ogre14LegacyTranslatedAsset &asset) {
  for (auto &mutation : frame.mutations) {
    if (mutation.source_asset_id == asset.source_asset_id) {
      mutation.kind = asset.kind;
      mutation.translated_revision = asset.translated_revision;
      mutation.stable_key = asset.stable_key;
      mutation.payload = asset.payload;
      mutation.material_audit = asset.material_audit;
      return;
    }
  }
  std::cerr << "FAIL: fixture mutation is absent\n";
  std::exit(EXIT_FAILURE);
}

RoR::Render::Ogre14LegacyMaterialClosure Sentinel() {
  using namespace RoR::Render;
  Ogre14LegacyMaterialClosure closure;
  closure.version = 77U;
  closure.source_sequence = 88U;
  closure.catalog_sequence = 99U;
  closure.material_source_asset_id = 111U;
  closure.requires_reverse_winding = true;
  GraphicsSceneAssetInput asset;
  asset.source_asset_id = 222U;
  MaterialDescriptor material;
  material.debug_name = "sentinel";
  asset.payload =
      std::make_shared<const RenderAssetPayload>(std::move(material));
  closure.assets.push_back(std::move(asset));
  return closure;
}

void RequireSentinel(const RoR::Render::Ogre14LegacyMaterialClosure &closure,
                     const std::shared_ptr<const
                         RoR::Render::RenderAssetPayload> &owner,
                     const RoR::Render::RenderAssetPayload &payload_value,
                     const char *message) {
  Require(closure.version == 77U && closure.source_sequence == 88U &&
              closure.catalog_sequence == 99U &&
              closure.material_source_asset_id == 111U &&
              closure.requires_reverse_winding && closure.assets.size() == 1U &&
              closure.assets.front().source_asset_id == 222U &&
              SameOwner(closure.assets.front().payload, owner) &&
              EquivalentRenderAssetPayload(*closure.assets.front().payload,
                                           payload_value),
          message);
}

void RequireFailureUnchanged(
    const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
    const RoR::Render::Ogre14LegacyAssetKey &key, const char *message) {
  using namespace RoR::Render;
  Ogre14LegacyMaterialClosure output = Sentinel();
  const auto owner = output.assets.front().payload;
  const RenderAssetPayload payload_value = *owner;
  const ValidationResult result =
      ResolveOgre14LegacyMaterialClosure(frame, key, output);
  Require(!result, message);
  RequireSentinel(output, owner, payload_value,
                  "failed closure resolution mutated output");
}

class ThrowingClosureFaultInjector final
    : public RoR::Render::IOgre14LegacyMaterialClosureFaultInjector {
public:
  ThrowingClosureFaultInjector(
      RoR::Render::Ogre14LegacyMaterialClosureFaultPoint point,
      bool allocation_failure) noexcept
      : point_(point), allocation_failure_(allocation_failure) {}

  void AtFaultPoint(
      RoR::Render::Ogre14LegacyMaterialClosureFaultPoint point) override {
    if (point != point_) {
      return;
    }
    if (allocation_failure_) {
      throw std::bad_alloc{};
    }
    throw std::runtime_error("deterministic unexpected closure fault");
  }

private:
  RoR::Render::Ogre14LegacyMaterialClosureFaultPoint point_;
  bool allocation_failure_ = false;
};

void TestStableKeyHelperIsCanonicalAndAtomic() {
  using namespace RoR::Render;
  std::string stable = "unchanged";
  Require(BuildOgre14LegacyStableAssetKey(RenderAssetKind::MATERIAL,
                                          MaterialKey(), stable)
              .ok() &&
              stable ==
                  "material|group=9:CityWorld|name=11:City/Facade",
          "public stable-key helper changed canonical identity encoding");
  Ogre14LegacyAssetKey invalid = MaterialKey();
  invalid.exact_name.clear();
  stable = "unchanged";
  Require(!BuildOgre14LegacyStableAssetKey(RenderAssetKind::MATERIAL,
                                           invalid, stable) &&
              stable == "unchanged",
          "invalid stable-key build changed its output");
}

void TestTexturedClosureUsesExactDependencyOrderAndBinding() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame =
      MakeTranslatedFrame(true, true, true);
  Ogre14LegacyMaterialClosure closure;
  Require(ResolveOgre14LegacyMaterialClosure(frame, MaterialKey(), closure)
              .ok(),
          "valid textured material closure failed");
  const auto &texture = FindAsset(frame, RenderAssetKind::TEXTURE);
  const auto &sampler = FindAsset(frame, RenderAssetKind::SAMPLER);
  const auto &material = FindAsset(frame, RenderAssetKind::MATERIAL);
  Require(closure.version == kOgre14LegacyMaterialClosureVersion &&
              closure.source_sequence == frame.source_sequence &&
              closure.catalog_sequence == frame.catalog_sequence &&
              closure.material_source_asset_id == material.source_asset_id &&
              closure.requires_reverse_winding && closure.assets.size() == 3U,
          "textured closure lineage or winding was not preserved");
  Require(closure.assets[0U].source_asset_id == texture.source_asset_id &&
              closure.assets[1U].source_asset_id == sampler.source_asset_id &&
              closure.assets[2U].source_asset_id == material.source_asset_id &&
              SameOwner(closure.assets[0U].payload, texture.payload) &&
              SameOwner(closure.assets[1U].payload, sampler.payload) &&
              SameOwner(closure.assets[2U].payload, material.payload),
          "closure did not preserve texture/sampler/material owner order");
  const GraphicsSceneAssetBinding &binding =
      closure.assets[2U].material_bindings[static_cast<std::size_t>(
          MaterialTextureSlot::BASE_COLOR)];
  Require(binding.texture_source_asset_id == texture.source_asset_id &&
              binding.sampler_source_asset_id == sampler.source_asset_id,
          "material input did not receive exact audited base-color binding");
  const MaterialDescriptor &descriptor =
      std::get<MaterialDescriptor>(*closure.assets[2U].payload);
  Require(IsAbsentRenderAssetReference(descriptor.base_color_texture.texture) &&
              IsAbsentRenderAssetReference(descriptor.base_color_texture.sampler) &&
              descriptor.base_color_texture.texture_coordinate_set == 1U,
          "closure overwrote canonical producer-owned descriptor references");
}

void TestUntexturedClosureHasOnlyMaterial() {
  using namespace RoR::Render;
  const Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame(false);
  Ogre14LegacyMaterialClosure closure;
  Require(ResolveOgre14LegacyMaterialClosure(frame, MaterialKey(), closure)
              .ok() &&
              closure.assets.size() == 1U &&
              RenderAssetPayloadKind(*closure.assets.front().payload) ==
                  RenderAssetKind::MATERIAL &&
              !closure.requires_reverse_winding,
          "untextured material closure synthesized dependencies");
  for (const GraphicsSceneAssetBinding &binding :
       closure.assets.front().material_bindings) {
    Require(binding.texture_source_asset_id == 0U &&
                binding.sampler_source_asset_id == 0U,
            "untextured material closure synthesized a binding");
  }
}

void TestSnapshotLineageOrderAndMutationParityReject() {
  using namespace RoR::Render;
  Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame();
  frame.full_snapshot = false;
  RequireFailureUnchanged(frame, MaterialKey(),
                          "incremental translated frame was accepted");

  frame = MakeTranslatedFrame();
  frame.catalog_sequence = frame.source_sequence + 1U;
  RequireFailureUnchanged(frame, MaterialKey(),
                          "impossible catalog/source lineage was accepted");

  frame = MakeTranslatedFrame();
  std::swap(frame.live_assets.front(), frame.live_assets.back());
  RequireFailureUnchanged(frame, MaterialKey(),
                          "misordered live dependency inventory was accepted");

  frame = MakeTranslatedFrame();
  std::swap(frame.mutations.front(), frame.mutations.back());
  RequireFailureUnchanged(frame, MaterialKey(),
                          "misordered full-snapshot mutations were accepted");

  frame = MakeTranslatedFrame();
  frame.mutations.pop_back();
  RequireFailureUnchanged(frame, MaterialKey(),
                          "full snapshot missing a live upsert was accepted");

  frame = MakeTranslatedFrame();
  frame.mutations.front().payload =
      std::make_shared<const RenderAssetPayload>(
          *frame.mutations.front().payload);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "upsert with a different immutable owner was accepted");
}

void TestStableIdentityAndPayloadKindReject() {
  using namespace RoR::Render;
  Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &texture =
      FindAsset(frame, RenderAssetKind::TEXTURE);
  texture.stable_key += "x";
  SyncMutation(frame, texture);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "noncanonical stable key was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &wrong_texture =
      FindAsset(frame, RenderAssetKind::TEXTURE);
  SamplerResourceDescriptor sampler;
  sampler.debug_name = "wrong-kind";
  wrong_texture.payload =
      std::make_shared<const RenderAssetPayload>(std::move(sampler));
  SyncMutation(frame, wrong_texture);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "payload kind mismatch was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &malformed_texture =
      FindAsset(frame, RenderAssetKind::TEXTURE);
  RenderAssetPayload payload = *malformed_texture.payload;
  std::get<TextureResourceDescriptor>(payload).version = 999U;
  malformed_texture.payload =
      std::make_shared<const RenderAssetPayload>(std::move(payload));
  SyncMutation(frame, malformed_texture);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "invalid texture descriptor was accepted");
}

void TestAuditDependencyColorSemanticAndWindingReject() {
  using namespace RoR::Render;
  Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &material =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  auto audit =
      std::make_shared<Ogre14LegacyMaterialPipelineAudit>(*material.material_audit);
  audit->version = 99U;
  material.material_audit = std::move(audit);
  SyncMutation(frame, material);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "unsupported audit version was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &missing_sampler_material =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  audit = std::make_shared<Ogre14LegacyMaterialPipelineAudit>(
      *missing_sampler_material.material_audit);
  audit->sampler_source_asset_id = 0U;
  missing_sampler_material.material_audit = std::move(audit);
  SyncMutation(frame, missing_sampler_material);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "unpaired texture/sampler audit was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &linear_texture =
      FindAsset(frame, RenderAssetKind::TEXTURE);
  RenderAssetPayload texture_payload = *linear_texture.payload;
  std::get<TextureResourceDescriptor>(texture_payload).color_space =
      TextureColorSpace::LINEAR;
  linear_texture.payload =
      std::make_shared<const RenderAssetPayload>(std::move(texture_payload));
  SyncMutation(frame, linear_texture);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "linear texture was accepted as exact base color");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &wrong_model =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  RenderAssetPayload material_payload = *wrong_model.payload;
  std::get<MaterialDescriptor>(material_payload).model =
      MaterialModel::PBR_METALLIC_ROUGHNESS;
  wrong_model.payload =
      std::make_shared<const RenderAssetPayload>(std::move(material_payload));
  SyncMutation(frame, wrong_model);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "audit/material semantic mismatch was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &wrong_winding =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  audit = std::make_shared<Ogre14LegacyMaterialPipelineAudit>(
      *wrong_winding.material_audit);
  audit->requires_reverse_winding = true;
  wrong_winding.material_audit = std::move(audit);
  SyncMutation(frame, wrong_winding);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "audit winding/cull mismatch was accepted");
}

void TestNoGuessedMaterialStateAndWholeFrameValidation() {
  using namespace RoR::Render;
  Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &material =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  RenderAssetPayload material_payload = *material.payload;
  std::get<MaterialDescriptor>(material_payload).metallic_factor = 0.5F;
  material.payload =
      std::make_shared<const RenderAssetPayload>(std::move(material_payload));
  SyncMutation(frame, material);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "guessed metallic state was accepted");

  frame = MakeTranslatedFrame();
  Ogre14LegacyTranslatedAsset &bound_material =
      FindAsset(frame, RenderAssetKind::MATERIAL);
  material_payload = *bound_material.payload;
  MaterialDescriptor &descriptor =
      std::get<MaterialDescriptor>(material_payload);
  descriptor.base_color_texture.texture = RenderAssetReference::Create(
      RenderAssetKind::TEXTURE, RenderAssetId::FromWords(1U, 2U), 1U);
  descriptor.base_color_texture.sampler = RenderAssetReference::Create(
      RenderAssetKind::SAMPLER, RenderAssetId::FromWords(3U, 4U), 1U);
  bound_material.payload =
      std::make_shared<const RenderAssetPayload>(std::move(material_payload));
  SyncMutation(frame, bound_material);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "pre-resolved material references were accepted");

  frame = MakeTranslatedFrame(true, false, false, true);
  Ogre14LegacyTranslatedAsset &unrelated_material =
      FindAsset(frame, RenderAssetKind::MATERIAL, 1U);
  auto audit = std::make_shared<Ogre14LegacyMaterialPipelineAudit>(
      *unrelated_material.material_audit);
  audit->requires_reverse_winding = true;
  unrelated_material.material_audit = std::move(audit);
  SyncMutation(frame, unrelated_material);
  RequireFailureUnchanged(
      frame, MaterialKey(),
      "hostile unrelated material escaped whole-snapshot validation");

  frame = MakeTranslatedFrame();
  RequireFailureUnchanged(frame, MaterialKey("City/Missing"),
                          "missing exact material key was accepted");

  Ogre14LegacyAssetKey oversized_key = MaterialKey();
  oversized_key.exact_name.assign(kMaximumMaterialDebugNameBytes + 1U, 'x');
  RequireFailureUnchanged(frame, oversized_key,
                          "oversized requested material key was accepted");
}

void TestCountCapsAndExceptionAtomicity() {
  using namespace RoR::Render;
  Ogre14LegacyTranslatedFrame frame = MakeTranslatedFrame();
  frame.live_assets.resize(
      kMaximumOgre14LegacyMaterialClosureLiveAssets + 1U);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "live-asset count cap was not enforced");

  frame = MakeTranslatedFrame();
  frame.mutations.resize(kMaximumOgre14LegacyMaterialClosureMutations + 1U);
  RequireFailureUnchanged(frame, MaterialKey(),
                          "mutation count cap was not enforced");

  frame = MakeTranslatedFrame();
  Ogre14LegacyMaterialClosure output = Sentinel();
  const auto owner = output.assets.front().payload;
  const RenderAssetPayload payload_value = *owner;
  ThrowingClosureFaultInjector allocation_fault(
      Ogre14LegacyMaterialClosureFaultPoint::BEFORE_INDEX_CONSTRUCTION, true);
  ValidationResult result = ResolveOgre14LegacyMaterialClosure(
      frame, MaterialKey(), output, &allocation_fault);
  Require(!result && result.field == "material_closure.allocation",
          "deterministic allocation exception did not fail closed");
  RequireSentinel(output, owner, payload_value,
                  "allocation exception mutated closure output or owner");

  output = Sentinel();
  const auto unexpected_owner = output.assets.front().payload;
  const RenderAssetPayload unexpected_value = *unexpected_owner;
  ThrowingClosureFaultInjector unexpected_fault(
      Ogre14LegacyMaterialClosureFaultPoint::DURING_DEPENDENCY_ASSEMBLY,
      false);
  result = ResolveOgre14LegacyMaterialClosure(
      frame, MaterialKey(), output, &unexpected_fault);
  Require(!result && result.field == "material_closure.exception",
          "deterministic unexpected exception did not fail closed");
  RequireSentinel(output, unexpected_owner, unexpected_value,
                  "mid-assembly exception exposed a partial closure");
}

} // namespace

int main() {
  TestStableKeyHelperIsCanonicalAndAtomic();
  TestTexturedClosureUsesExactDependencyOrderAndBinding();
  TestUntexturedClosureHasOnlyMaterial();
  TestSnapshotLineageOrderAndMutationParityReject();
  TestStableIdentityAndPayloadKindReject();
  TestAuditDependencyColorSemanticAndWindingReject();
  TestNoGuessedMaterialStateAndWholeFrameValidation();
  TestCountCapsAndExceptionAtomicity();
  std::cout << "OGRE 14 legacy material closure tests passed\n";
  return EXIT_SUCCESS;
}
