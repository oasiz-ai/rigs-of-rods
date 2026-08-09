/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14LegacyAssetTranslator.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
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
    const std::shared_ptr<const RoR::Render::RenderAssetPayload> &a,
    const std::shared_ptr<const RoR::Render::RenderAssetPayload> &b) {
  return !a.owner_before(b) && !b.owner_before(a);
}

bool SameOwner(
    const std::shared_ptr<const RoR::Render::Ogre14LegacyMaterialPipelineAudit>
        &a,
    const std::shared_ptr<const RoR::Render::Ogre14LegacyMaterialPipelineAudit>
        &b) {
  return !a.owner_before(b) && !b.owner_before(a);
}

bool EquivalentAssetValue(const RoR::Render::Ogre14LegacyTranslatedAsset &lhs,
                          const RoR::Render::Ogre14LegacyTranslatedAsset &rhs) {
  using namespace RoR::Render;
  if (lhs.kind != rhs.kind || lhs.source_asset_id != rhs.source_asset_id ||
      lhs.source_revision != rhs.source_revision ||
      lhs.translated_revision != rhs.translated_revision ||
      lhs.stable_key != rhs.stable_key ||
      static_cast<bool>(lhs.payload) != static_cast<bool>(rhs.payload) ||
      static_cast<bool>(lhs.material_audit) !=
          static_cast<bool>(rhs.material_audit)) {
    return false;
  }
  if (lhs.payload != nullptr &&
      !EquivalentRenderAssetPayload(*lhs.payload, *rhs.payload)) {
    return false;
  }
  return lhs.material_audit == nullptr ||
         EquivalentOgre14LegacyMaterialPipelineAudit(*lhs.material_audit,
                                                     *rhs.material_audit);
}

bool EquivalentFrameValue(const RoR::Render::Ogre14LegacyTranslatedFrame &lhs,
                          const RoR::Render::Ogre14LegacyTranslatedFrame &rhs) {
  if (lhs.version != rhs.version ||
      !RoR::Render::SameOgre14LegacyCatalogIdentity(lhs.catalog_identity,
                                                    rhs.catalog_identity) ||
      lhs.source_sequence != rhs.source_sequence ||
      lhs.catalog_sequence != rhs.catalog_sequence ||
      lhs.full_snapshot != rhs.full_snapshot ||
      lhs.live_assets.size() != rhs.live_assets.size() ||
      lhs.mutations.size() != rhs.mutations.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.live_assets.size(); ++index) {
    if (!EquivalentAssetValue(lhs.live_assets[index], rhs.live_assets[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < lhs.mutations.size(); ++index) {
    const auto &left = lhs.mutations[index];
    const auto &right = rhs.mutations[index];
    RoR::Render::Ogre14LegacyTranslatedAsset left_asset;
    left_asset.kind = left.kind;
    left_asset.source_asset_id = left.source_asset_id;
    left_asset.translated_revision = left.translated_revision;
    left_asset.stable_key = left.stable_key;
    left_asset.payload = left.payload;
    left_asset.material_audit = left.material_audit;
    RoR::Render::Ogre14LegacyTranslatedAsset right_asset;
    right_asset.kind = right.kind;
    right_asset.source_asset_id = right.source_asset_id;
    right_asset.translated_revision = right.translated_revision;
    right_asset.stable_key = right.stable_key;
    right_asset.payload = right.payload;
    right_asset.material_audit = right.material_audit;
    if (left.type != right.type ||
        !EquivalentAssetValue(left_asset, right_asset)) {
      return false;
    }
  }
  return true;
}

bool SameFrameOwners(const RoR::Render::Ogre14LegacyTranslatedFrame &lhs,
                     const RoR::Render::Ogre14LegacyTranslatedFrame &rhs) {
  if (lhs.live_assets.size() != rhs.live_assets.size() ||
      lhs.mutations.size() != rhs.mutations.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.live_assets.size(); ++index) {
    if (!SameOwner(lhs.live_assets[index].payload,
                   rhs.live_assets[index].payload) ||
        !SameOwner(lhs.live_assets[index].material_audit,
                   rhs.live_assets[index].material_audit)) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < lhs.mutations.size(); ++index) {
    if (!SameOwner(lhs.mutations[index].payload,
                   rhs.mutations[index].payload) ||
        !SameOwner(lhs.mutations[index].material_audit,
                   rhs.mutations[index].material_audit)) {
      return false;
    }
  }
  return true;
}

RoR::Render::Ogre14LegacyTextureInput
MakeTexture(std::string name = "City/BaseColor",
            RoR::Render::Ogre14LegacyPixelEncoding encoding =
                RoR::Render::Ogre14LegacyPixelEncoding::RGBA8_BYTES,
            std::vector<std::uint8_t> bytes = {17U, 34U, 51U, 68U}) {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture;
  texture.key.exact_resource_group = "CityWorld";
  texture.key.exact_name = std::move(name);
  texture.source_revision = 1U;
  texture.pixel_encoding = encoding;
  texture.width = 1U;
  texture.height = 1U;
  Ogre14LegacyTextureMipInput mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = bytes.size();
  mip.slice_pitch_bytes = bytes.size();
  mip.bytes = std::move(bytes);
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RoR::Render::Ogre14LegacyMaterialInput
MakeMaterial(const RoR::Render::Ogre14LegacyTextureInput *texture = nullptr,
             std::string name = "City/Facade") {
  using namespace RoR::Render;
  Ogre14LegacyMaterialInput material;
  material.key.exact_resource_group = "CityWorld";
  material.key.exact_name = std::move(name);
  material.source_revision = 1U;
  material.diffuse_linear = {0.25F, 0.5F, 0.75F, 1.0F};
  if (texture != nullptr) {
    Ogre14LegacyTextureUnitInput unit;
    unit.texture_key = texture->key;
    unit.sampler.source_revision = 1U;
    unit.sampler.maximum_lod =
        static_cast<float>(texture->mip_levels.size() - 1U);
    material.texture_units.push_back(std::move(unit));
  }
  return material;
}

RoR::Render::Ogre14LegacyAssetFrameInput MakeFrame(std::uint64_t sequence,
                                                   bool textured = true) {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput frame;
  frame.source_sequence = sequence;
  if (textured) {
    frame.textures.push_back(MakeTexture());
    frame.materials.push_back(MakeMaterial(&frame.textures.front()));
  } else {
    frame.materials.push_back(MakeMaterial());
  }
  return frame;
}

struct BorrowedIdentityFrame final {
  std::vector<const RoR::Render::Ogre14LegacyTextureInput *> textures;
  std::vector<const RoR::Render::Ogre14LegacyMaterialInput *> materials;

  [[nodiscard]] RoR::Render::Ogre14LegacyAssetIdentityFrameView view() const {
    RoR::Render::Ogre14LegacyAssetIdentityFrameView result;
    result.texture_inputs = textures.data();
    result.texture_input_count = textures.size();
    result.material_inputs = materials.data();
    result.material_input_count = materials.size();
    return result;
  }
};

BorrowedIdentityFrame
BorrowIdentities(const RoR::Render::Ogre14LegacyAssetFrameInput &frame) {
  BorrowedIdentityFrame result;
  result.textures.reserve(frame.textures.size());
  result.materials.reserve(frame.materials.size());
  for (const auto &texture : frame.textures) {
    result.textures.push_back(&texture);
  }
  for (const auto &material : frame.materials) {
    result.materials.push_back(&material);
  }
  return result;
}

const RoR::Render::Ogre14LegacyTranslatedAsset &
FindAsset(const RoR::Render::Ogre14LegacyTranslatedFrame &frame,
          RoR::Render::RenderAssetKind kind) {
  for (const auto &asset : frame.live_assets) {
    if (asset.kind == kind) {
      return asset;
    }
  }
  std::cerr << "FAIL: requested translated asset kind is absent\n";
  std::exit(EXIT_FAILURE);
}

void TestByteExactFormatsEndiannessAndSrgbRole() {
  using namespace RoR::Render;
  static_assert(kOgre14LegacyAssetTranslatorVersion == 1U,
                "fixture requires explicit translator migration");
  const std::vector<std::uint8_t> rgba{17U, 34U, 51U, 68U};
  const std::vector<
      std::pair<Ogre14LegacyPixelEncoding, std::vector<std::uint8_t>>>
      encodings{
          {Ogre14LegacyPixelEncoding::RGBA8_BYTES, rgba},
          {Ogre14LegacyPixelEncoding::BGRA8_BYTES, {51U, 34U, 17U, 68U}},
          {Ogre14LegacyPixelEncoding::ARGB8_BYTES, {68U, 17U, 34U, 51U}},
          {Ogre14LegacyPixelEncoding::ABGR8_BYTES, {68U, 51U, 34U, 17U}},
          {Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_LITTLE_ENDIAN,
           {51U, 34U, 17U, 68U}},
          {Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_BIG_ENDIAN,
           {68U, 17U, 34U, 51U}},
      };
  for (const auto &encoding : encodings) {
    TextureResourceDescriptor descriptor;
    const ValidationResult result = DecodeOgre14LegacyTexture(
        MakeTexture("City/Format", encoding.first, encoding.second),
        descriptor);
    Require(result.ok(), "supported byte layout failed normalization");
    Require(descriptor.format == TextureResourceFormat::RGBA8_UNORM &&
                descriptor.color_space == TextureColorSpace::SRGB &&
                descriptor.mip_levels.size() == 1U &&
                descriptor.mip_levels.front().bytes == rgba,
            "RGBA normalization or explicit sRGB marking changed bytes");
  }

  TextureResourceDescriptor rgb;
  Require(DecodeOgre14LegacyTexture(
              MakeTexture("City/RGB", Ogre14LegacyPixelEncoding::RGB8_BYTES,
                          {17U, 34U, 51U}),
              rgb)
                  .ok() &&
              rgb.mip_levels.front().bytes ==
                  std::vector<std::uint8_t>({17U, 34U, 51U, 255U}),
          "RGB normalization did not add canonical opaque alpha");

  Ogre14LegacyTextureInput padded = MakeTexture();
  padded.width = 2U;
  padded.height = 2U;
  padded.mip_levels.clear();
  padded.mip_levels.push_back(
      {2U, 2U, 12U, 24U, {1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,
                          90U, 91U, 92U, 93U, 9U,  10U, 11U, 12U,
                          13U, 14U, 15U, 16U, 94U, 95U, 96U, 97U}});
  padded.mip_levels.push_back(
      {1U, 1U, 8U, 8U, {21U, 22U, 23U, 24U, 98U, 99U, 100U, 101U}});
  TextureResourceDescriptor tight;
  Require(
      DecodeOgre14LegacyTexture(padded, tight).ok() &&
          tight.mip_levels[0U].row_pitch_bytes == 8U &&
          tight.mip_levels[0U].layer_pitch_bytes == 16U &&
          tight.mip_levels[0U].bytes ==
              std::vector<std::uint8_t>({1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U,
                                         10U, 11U, 12U, 13U, 14U, 15U, 16U}) &&
          tight.mip_levels[1U].row_pitch_bytes == 4U &&
          tight.mip_levels[1U].bytes ==
              std::vector<std::uint8_t>({21U, 22U, 23U, 24U}),
      "padded source rows/slices or exact mip layout changed");
}

void TestTextureRejectsAmbiguousTransformsAndMalformedLayouts() {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture = MakeTexture();
  TextureResourceDescriptor sentinel;
  sentinel.debug_name = "unchanged";

  texture.hardware_gamma_enabled = false;
  ValidationResult result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "texture.color_transform" &&
              sentinel.debug_name == "unchanged",
          "sRGB role/hardware-gamma mismatch was accepted or mutated output");

  texture = MakeTexture();
  texture.compressed = true;
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.field == "texture.source_kind",
          "compressed texture did not fail closed");
  texture = MakeTexture();
  texture.render_target = true;
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.field == "texture.source_kind",
          "render-target texture did not fail closed");
  texture = MakeTexture();
  texture.type = Ogre14LegacyTextureType::TEXTURE_CUBE;
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.field == "texture.type",
          "cubemap texture did not fail closed");
  texture = MakeTexture();
  texture.mip_levels.front().row_pitch_bytes = 3U;
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.code == ValidationCode::SIZE_MISMATCH &&
              result.field == "texture.mip_layout",
          "undersized source row pitch was accepted");
  texture = MakeTexture();
  texture.mip_levels.front().slice_pitch_bytes = 5U;
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.field == "texture.mip_layout",
          "inconsistent source slice pitch was accepted");
  texture = MakeTexture();
  texture.mip_levels.push_back(texture.mip_levels.front());
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "texture.mip_levels",
          "duplicate terminal 1x1 mip was accepted");
  texture = MakeTexture();
  texture.pixel_encoding = static_cast<Ogre14LegacyPixelEncoding>(255U);
  result = DecodeOgre14LegacyTexture(texture, sentinel);
  Require(!result && result.code == ValidationCode::INVALID_ENUM,
          "unknown pixel format was accepted");
}

void TestExactSamplerMaterialAndPipelineTranslation() {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput frame = MakeFrame(1U);
  Ogre14LegacyMaterialInput &source = frame.materials.front();
  source.base_color_semantic =
      Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR;
  source.lighting_enabled = true;
  source.pipeline.cull = Ogre14LegacyCullMode::ANTICLOCKWISE;
  source.pipeline.alpha_reject = Ogre14LegacyCompareOperation::GREATER_EQUAL;
  source.pipeline.alpha_reject_value = 127U;
  Ogre14LegacySamplerInput &sampler = source.texture_units.front().sampler;
  sampler.minification = Ogre14LegacyFilter::ANISOTROPIC;
  sampler.magnification = Ogre14LegacyFilter::LINEAR;
  sampler.mip = Ogre14LegacyFilter::POINT;
  sampler.address_u = Ogre14LegacyAddressMode::MIRROR;
  sampler.address_v = Ogre14LegacyAddressMode::CLAMP;
  sampler.address_w = Ogre14LegacyAddressMode::BORDER;
  sampler.mip_lod_bias = -0.75F;
  sampler.maximum_anisotropy = 8U;
  sampler.border_color = {0.1F, 0.2F, 0.3F, 0.4F};

  Ogre14LegacyMaterialPipelineAudit independently_derived_audit;
  Require(DeriveOgre14LegacyMaterialPipelineAudit(source,
                                                  independently_derived_audit)
              .ok(),
          "pure material audit derivation rejected the canonical source");

  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame output;
  const ValidationResult result = translator.Translate(frame, output);
  Require(result.ok() && output.full_snapshot &&
              output.catalog_sequence == 1U &&
              output.live_assets.size() == 3U &&
              output.live_assets[0U].kind == RenderAssetKind::TEXTURE &&
              output.live_assets[1U].kind == RenderAssetKind::SAMPLER &&
              output.live_assets[2U].kind == RenderAssetKind::MATERIAL,
          "dependency-ordered first translation failed");
  const auto &texture_asset = FindAsset(output, RenderAssetKind::TEXTURE);
  const auto &sampler_asset = FindAsset(output, RenderAssetKind::SAMPLER);
  const auto &material_asset = FindAsset(output, RenderAssetKind::MATERIAL);
  const auto &portable_texture =
      std::get<TextureResourceDescriptor>(*texture_asset.payload);
  const auto &portable_sampler =
      std::get<SamplerResourceDescriptor>(*sampler_asset.payload);
  const auto &portable_material =
      std::get<MaterialDescriptor>(*material_asset.payload);
  Require(portable_texture.color_space == TextureColorSpace::SRGB &&
              portable_texture.mip_levels.front().bytes ==
                  std::vector<std::uint8_t>({17U, 34U, 51U, 68U}),
          "base color was gamma-transformed instead of marked exactly once");
  Require(
      portable_sampler.minification_filter == SamplerFilter::LINEAR &&
          portable_sampler.magnification_filter == SamplerFilter::LINEAR &&
          portable_sampler.mip_filter == SamplerFilter::NEAREST &&
          portable_sampler.address_u == SamplerAddressMode::MIRRORED_REPEAT &&
          portable_sampler.address_v == SamplerAddressMode::CLAMP_TO_EDGE &&
          portable_sampler.address_w == SamplerAddressMode::CLAMP_TO_BORDER &&
          portable_sampler.anisotropy_enabled &&
          portable_sampler.maximum_anisotropy == 8.0F &&
          portable_sampler.mip_lod_bias == -0.75F &&
          portable_sampler.border_color == sampler.border_color,
      "exact sampler state changed during translation");
  Require(portable_material.model == MaterialModel::PBR_METALLIC_ROUGHNESS &&
              portable_material.metallic_factor == 0.0F &&
              portable_material.roughness_factor == 1.0F &&
              portable_material.alpha_mode == MaterialAlphaMode::MASK &&
              portable_material.alpha_cutoff == 127.0F / 255.0F &&
              !portable_material.double_sided &&
              material_asset.material_audit != nullptr &&
              material_asset.material_audit->requires_reverse_winding &&
              material_asset.material_audit->texture_source_asset_id ==
                  texture_asset.source_asset_id &&
              material_asset.material_audit->sampler_source_asset_id ==
                  sampler_asset.source_asset_id &&
              EquivalentOgre14LegacyMaterialPipelineAudit(
                  independently_derived_audit, *material_asset.material_audit),
          "explicit base-color material or exact pipeline audit changed");

  Ogre14LegacyMaterialPipelineAudit sentinel_audit;
  sentinel_audit.texture_source_asset_id = 0xA11D17U;
  const Ogre14LegacyMaterialPipelineAudit expected_sentinel = sentinel_audit;
  Ogre14LegacyMaterialInput invalid_material = source;
  invalid_material.pass_count = 2U;
  Require(!DeriveOgre14LegacyMaterialPipelineAudit(invalid_material,
                                                   sentinel_audit) &&
              EquivalentOgre14LegacyMaterialPipelineAudit(sentinel_audit,
                                                          expected_sentinel),
          "failed pure audit derivation mutated caller output");
}

void TestUnsupportedMaterialFeaturesRejectWithoutGuessing() {
  using namespace RoR::Render;
  Ogre14LegacyTextureInput texture = MakeTexture();
  Ogre14LegacyMaterialInput material = MakeMaterial(&texture);
  ValidationResult result = ValidateOgre14LegacyMaterialInput(material);
  Require(result.ok(), "baseline exact material fixture is invalid");

  material.pass_count = 2U;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.pass_structure",
          "multipass material was accepted");
  material = MakeMaterial(&texture);
  material.generated_rtss_program = true;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.programs",
          "generated RTSS program was accepted");
  material = MakeMaterial(&texture);
  material.texture_units.front().frame_count = 2U;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.texture_unit",
          "animated texture unit was accepted");
  material = MakeMaterial(&texture);
  material.texture_units.front().projective = true;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.texture_unit",
          "projective texture unit was accepted");
  material = MakeMaterial(&texture);
  material.texture_units.front().identity_texture_transform = false;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.texture_unit",
          "nonidentity UV transform was silently approximated");
  material = MakeMaterial(&texture);
  material.specular_linear = {0.2F, 0.2F, 0.2F};
  material.shininess = 64.0F;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.fixed_function_lobes",
          "legacy specular/shininess was guessed into a PBR role");
  material = MakeMaterial(&texture);
  material.pipeline.source_color = Ogre14LegacyBlendFactor::ONE;
  material.pipeline.destination_color = Ogre14LegacyBlendFactor::ONE;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.pipeline.blend",
          "additive blend was accepted as source-over");
  material = MakeMaterial(&texture);
  material.texture_units.front().sampler.compare_enabled = true;
  result = ValidateOgre14LegacyMaterialInput(material);
  Require(!result && result.field == "material.sampler.compare",
          "base-color comparison sampler was accepted");
}

void TestStableIdsContentRevisionsCacheAndTombstones() {
  using namespace RoR::Render;
  std::uint64_t first_id = 0U;
  std::uint64_t repeated_id = 0U;
  const Ogre14LegacyAssetKey key{"CityWorld", "City/BaseColor"};
  Require(
      DeriveOgre14LegacySourceAssetId(RenderAssetKind::TEXTURE, key, first_id)
              .ok() &&
          DeriveOgre14LegacySourceAssetId(RenderAssetKind::TEXTURE, key,
                                          repeated_id)
              .ok() &&
          first_id == repeated_id && first_id != 0U,
      "stable source ID derivation is nondeterministic");

  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame first;
  Require(translator.Translate(MakeFrame(1U), first).ok(),
          "first catalog translation failed");
  const auto first_texture = FindAsset(first, RenderAssetKind::TEXTURE);
  const auto first_sampler = FindAsset(first, RenderAssetKind::SAMPLER);
  const auto first_material = FindAsset(first, RenderAssetKind::MATERIAL);

  Ogre14LegacyAssetFrameInput stable_input = MakeFrame(2U);
  Ogre14LegacyTranslatedFrame stable;
  Require(translator.Translate(stable_input, stable).ok() &&
              stable.mutations.empty() && stable.catalog_sequence == 1U &&
              SameOwner(FindAsset(stable, RenderAssetKind::TEXTURE).payload,
                        first_texture.payload) &&
              SameOwner(FindAsset(stable, RenderAssetKind::SAMPLER).payload,
                        first_sampler.payload) &&
              SameOwner(FindAsset(stable, RenderAssetKind::MATERIAL).payload,
                        first_material.payload),
          "owner replacement changed semantic revisions or cache owners");

  Ogre14LegacyAssetFrameInput source_only = MakeFrame(3U);
  source_only.textures.front().source_revision = 2U;
  source_only.materials.front().source_revision = 2U;
  source_only.materials.front().texture_units.front().sampler.source_revision =
      2U;
  Ogre14LegacyTranslatedFrame source_advanced;
  Require(translator.Translate(source_only, source_advanced).ok() &&
              source_advanced.mutations.empty() &&
              FindAsset(source_advanced, RenderAssetKind::TEXTURE)
                      .translated_revision == 1U &&
              SameOwner(
                  FindAsset(source_advanced, RenderAssetKind::TEXTURE).payload,
                  first_texture.payload),
          "source-only revision advance changed semantic output revision");

  Ogre14LegacyAssetFrameInput changed = MakeFrame(4U);
  changed.textures.front().source_revision = 3U;
  changed.materials.front().source_revision = 3U;
  changed.materials.front().texture_units.front().sampler.source_revision = 3U;
  changed.textures.front().mip_levels.front().bytes[0U] = 99U;
  Ogre14LegacyTranslatedFrame updated;
  Require(
      translator.Translate(changed, updated).ok() &&
          updated.catalog_sequence == 2U && updated.mutations.size() == 1U &&
          updated.mutations.front().kind == RenderAssetKind::TEXTURE &&
          updated.mutations.front().translated_revision == 2U &&
          FindAsset(updated, RenderAssetKind::MATERIAL).translated_revision ==
              1U,
      "semantic byte change did not advance exactly one asset revision");

  Ogre14LegacyAssetFrameInput removed;
  removed.source_sequence = 5U;
  Ogre14LegacyTranslatedFrame tombstoned;
  Require(translator.Translate(removed, tombstoned).ok() &&
              tombstoned.live_assets.empty() &&
              tombstoned.mutations.size() == 3U &&
              tombstoned.mutations[0U].kind == RenderAssetKind::MATERIAL &&
              tombstoned.mutations[1U].kind == RenderAssetKind::SAMPLER &&
              tombstoned.mutations[2U].kind == RenderAssetKind::TEXTURE,
          "destroy mutations are not reverse dependency ordered");
  for (const auto &mutation : tombstoned.mutations) {
    Require(mutation.type == Ogre14LegacyAssetMutationType::DESTROY &&
                mutation.payload == nullptr,
            "destroy mutation retained a live payload");
  }

  Ogre14LegacyTranslatedFrame full;
  Require(translator.BuildFullSnapshot(full).ok() && full.full_snapshot &&
              full.mutations.size() == 3U && full.live_assets.empty(),
          "full snapshot omitted permanent tombstones");
  Ogre14LegacyTranslatedFrame sentinel = full;
  Ogre14LegacyAssetFrameInput resurrected = MakeFrame(6U);
  const ValidationResult resurrection =
      translator.Translate(resurrected, sentinel);
  Require(!resurrection &&
              resurrection.code == ValidationCode::REVISION_MISMATCH &&
              translator.source_sequence() == 5U &&
              sentinel.source_sequence == full.source_sequence,
          "tombstoned identity resurrected or modified state/output");
}

class OneShotFault final
    : public RoR::Render::IOgre14LegacyAssetTranslatorFaultInjector {
public:
  bool fail = true;

  RoR::Render::ValidationResult BeforeCommit() noexcept override {
    if (!fail) {
      return RoR::Render::ValidationResult::Success();
    }
    return RoR::Render::ValidationResult::Failure(
        RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
        "translator.test_fault", "injected precommit failure");
  }
};

class CloneFault final
    : public RoR::Render::IOgre14LegacyAssetTranslatorFaultInjector {
public:
  enum class Behavior {
    DISABLED,
    THROW_BAD_ALLOC,
    THROW_UNEXPECTED,
  };

  Behavior behavior = Behavior::DISABLED;
  std::size_t clone_callback_count = 0U;
  RoR::Render::Ogre14LegacyAssetTranslatorCloneStage stage =
      RoR::Render::Ogre14LegacyAssetTranslatorCloneStage::BEFORE_STATE_COPY;

  RoR::Render::ValidationResult BeforeCommit() noexcept override {
    return RoR::Render::ValidationResult::Success();
  }

  void BeforeTransactionClone(
      RoR::Render::Ogre14LegacyAssetTranslatorCloneStage current) override {
    ++clone_callback_count;
    if (current != stage || behavior == Behavior::DISABLED) {
      return;
    }
    if (behavior == Behavior::THROW_BAD_ALLOC) {
      throw std::bad_alloc();
    }
    throw 73;
  }
};

class LifetimeAdmissionFault final
    : public RoR::Render::IOgre14LegacyAssetTranslatorFaultInjector {
public:
  enum class Behavior {
    DISABLED,
    OVERRIDE_SOURCE_ID,
    THROW_BAD_ALLOC,
    THROW_UNEXPECTED,
  };

  Behavior behavior = Behavior::DISABLED;
  std::uint64_t source_id_override = 0U;
  std::size_t callback_count = 0U;

  RoR::Render::ValidationResult BeforeCommit() noexcept override {
    return RoR::Render::ValidationResult::Success();
  }

  void AtLifetimeAdmissionIdentityForTesting(
      RoR::Render::RenderAssetKind, const RoR::Render::Ogre14LegacyAssetKey &,
      std::uint64_t &source_asset_id) override {
    ++callback_count;
    switch (behavior) {
    case Behavior::DISABLED:
      return;
    case Behavior::OVERRIDE_SOURCE_ID:
      source_asset_id = source_id_override;
      return;
    case Behavior::THROW_BAD_ALLOC:
      throw std::bad_alloc();
    case Behavior::THROW_UNEXPECTED:
      throw 91;
    }
  }
};

class ExclusiveFault final
    : public RoR::Render::IOgre14LegacyAssetTranslatorFaultInjector {
public:
  bool fail_translate = false;

  RoR::Render::ValidationResult BeforeCommit() noexcept override {
    if (!fail_translate) {
      return RoR::Render::ValidationResult::Success();
    }
    return RoR::Render::ValidationResult::Failure(
        RoR::Render::ValidationCode::UNSUPPORTED_FEATURE,
        "translator.exclusive_test_fault", "injected candidate failure");
  }
};

void TestTransactionsFaultsAndLineageAreAtomic() {
  using namespace RoR::Render;
  OneShotFault fault;
  Ogre14LegacyAssetTranslator translator(&fault);
  Ogre14LegacyTranslatedFrame output;
  output.source_sequence = 777U;
  ValidationResult result = translator.Translate(MakeFrame(1U), output);
  Require(!result && result.field == "translator.test_fault" &&
              translator.source_sequence() == 0U &&
              translator.catalog_sequence() == 0U &&
              output.source_sequence == 777U,
          "injected failure partially committed translator or output state");
  fault.fail = false;
  Require(translator.Translate(MakeFrame(1U), output).ok() &&
              translator.source_sequence() == 1U,
          "same source frame could not retry after atomic fault");

  Ogre14LegacyAssetFrameInput malformed = MakeFrame(2U);
  malformed.textures.front().mip_levels.front().bytes[0U] = 88U;
  Ogre14LegacyTranslatedFrame sentinel = output;
  result = translator.Translate(malformed, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              translator.source_sequence() == 1U &&
              sentinel.source_sequence == output.source_sequence,
          "same-revision semantic change partially committed");

  Ogre14LegacyAssetFrameInput skipped = MakeFrame(3U);
  result = translator.Translate(skipped, sentinel);
  Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
              translator.source_sequence() == 1U,
          "source sequence gap was accepted");

  Ogre14LegacyAssetFrameInput duplicate = MakeFrame(2U);
  duplicate.textures.push_back(duplicate.textures.front());
  result = translator.Translate(duplicate, sentinel);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              translator.source_sequence() == 1U,
          "duplicate texture identity was accepted or committed");
}

void TestEmptyAuthoritativeCatalogInitializesSequence() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyAssetFrameInput first;
  first.source_sequence = 1U;
  Ogre14LegacyTranslatedFrame output;
  Require(translator.Translate(first, output).ok() && output.full_snapshot &&
              output.catalog_sequence == 1U && output.live_assets.empty() &&
              output.mutations.empty(),
          "empty first inventory did not initialize a replayable catalog");

  Ogre14LegacyTranslatedFrame full;
  Require(translator.BuildFullSnapshot(full).ok() && full.full_snapshot &&
              full.catalog_sequence == 1U && full.live_assets.empty() &&
              full.mutations.empty(),
          "empty initialized catalog could not produce a full snapshot");

  Ogre14LegacyAssetFrameInput unchanged;
  unchanged.source_sequence = 2U;
  Require(translator.Translate(unchanged, output).ok() &&
              !output.full_snapshot && output.catalog_sequence == 1U &&
              output.mutations.empty(),
          "unchanged empty inventory advanced the semantic catalog");
}

void TestLinearTextureCannotAcquireAmbiguousPbrRole() {
  using namespace RoR::Render;
  Ogre14LegacyAssetFrameInput frame = MakeFrame(1U);
  frame.textures.front().color_role = Ogre14LegacyTextureColorRole::LINEAR_DATA;
  frame.textures.front().hardware_gamma_enabled = false;
  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTranslatedFrame output;
  const ValidationResult result = translator.Translate(frame, output);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "material.texture_color_role" &&
              translator.source_sequence() == 0U,
          "linear data texture was guessed into the base-color PBR role");
}

void TestConfiguredBoundsRejectTransactionally() {
  using namespace RoR::Render;

  Ogre14LegacyAssetTranslatorConfiguration invalid_configuration;
  invalid_configuration.maximum_decoded_bytes_per_asset = 0U;
  Ogre14LegacyAssetTranslator invalid(invalid_configuration);
  Ogre14LegacyTranslatedFrame sentinel;
  sentinel.source_sequence = 777U;
  ValidationResult result = invalid.Translate(MakeFrame(1U), sentinel);
  Require(
      !result && result.field == "configuration.limits" &&
          invalid.source_sequence() == 0U && sentinel.source_sequence == 777U,
      "invalid nonzero configuration requirement mutated translator output");
  result = invalid.BuildFullSnapshot(sentinel);
  Require(!result && result.field == "configuration.limits" &&
              sentinel.source_sequence == 777U,
          "invalid configuration full snapshot mutated caller output");

  Ogre14LegacyAssetTranslatorConfiguration count_configuration;
  count_configuration.maximum_texture_inputs_per_frame = 1U;
  count_configuration.maximum_material_inputs_per_frame = 1U;
  count_configuration.maximum_live_assets_per_frame = 3U;
  count_configuration.maximum_lifetime_asset_records = 3U;
  Ogre14LegacyAssetTranslator count_bounded(count_configuration);
  Ogre14LegacyAssetFrameInput excess_count = MakeFrame(1U);
  excess_count.textures.push_back(MakeTexture("City/Second"));
  result = count_bounded.Translate(excess_count, sentinel);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "frame.asset_inputs" &&
              count_bounded.source_sequence() == 0U &&
              sentinel.source_sequence == 777U,
          "per-frame source count cap failed to roll back exactly");

  Ogre14LegacyAssetTranslatorConfiguration live_configuration;
  live_configuration.maximum_texture_inputs_per_frame = 2U;
  live_configuration.maximum_material_inputs_per_frame = 2U;
  live_configuration.maximum_live_assets_per_frame = 2U;
  live_configuration.maximum_lifetime_asset_records = 3U;
  Ogre14LegacyAssetTranslator live_bounded(live_configuration);
  result = live_bounded.Translate(MakeFrame(1U), sentinel);
  Require(!result && result.field == "frame.live_assets" &&
              live_bounded.source_sequence() == 0U,
          "derived sampler escaped the configured live-asset cap");

  Ogre14LegacyAssetTranslatorConfiguration byte_configuration;
  byte_configuration.maximum_decoded_bytes_per_asset = 3U;
  byte_configuration.maximum_decoded_bytes_per_frame = 8U;
  Ogre14LegacyAssetTranslator byte_bounded(byte_configuration);
  result = byte_bounded.Translate(MakeFrame(1U), sentinel);
  Require(!result && result.field == "texture.decoded_bytes" &&
              byte_bounded.source_sequence() == 0U,
          "per-asset decoded-byte cap failed before commit");

  TextureResourceDescriptor decode_sentinel;
  decode_sentinel.debug_name = "unchanged";
  result = DecodeOgre14LegacyTexture(MakeTexture(), decode_sentinel, 3U);
  Require(!result && result.field == "texture.decoded_bytes" &&
              decode_sentinel.debug_name == "unchanged",
          "standalone decode cap mutated output");

  Ogre14LegacyAssetTranslatorConfiguration aggregate_configuration;
  aggregate_configuration.maximum_decoded_bytes_per_asset = 4U;
  aggregate_configuration.maximum_decoded_bytes_per_frame = 4U;
  Ogre14LegacyAssetTranslator aggregate_bounded(aggregate_configuration);
  Ogre14LegacyAssetFrameInput two_textures;
  two_textures.source_sequence = 1U;
  two_textures.textures.push_back(MakeTexture("City/First"));
  two_textures.textures.push_back(MakeTexture("City/Second"));
  result = aggregate_bounded.Translate(two_textures, sentinel);
  Require(!result && result.field == "frame.decoded_texture_bytes" &&
              result.element_index == 1U &&
              aggregate_bounded.source_sequence() == 0U,
          "aggregate decoded-byte cap partially committed a texture frame");
}

void TestLifetimeRecordCapIncludesPermanentTombstones() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration configuration;
  configuration.maximum_texture_inputs_per_frame = 3U;
  configuration.maximum_material_inputs_per_frame = 3U;
  configuration.maximum_live_assets_per_frame = 3U;
  configuration.maximum_lifetime_asset_records = 4U;
  Ogre14LegacyAssetTranslator translator(configuration);
  Ogre14LegacyTranslatedFrame output;
  Require(translator.Translate(MakeFrame(1U), output).ok(),
          "bounded catalog initial frame failed");

  Ogre14LegacyAssetFrameInput removed;
  removed.source_sequence = 2U;
  Require(translator.Translate(removed, output).ok() &&
              output.live_assets.empty(),
          "bounded catalog tombstone frame failed");

  Ogre14LegacyAssetFrameInput new_identity;
  new_identity.source_sequence = 3U;
  new_identity.textures.push_back(MakeTexture("City/NewBase"));
  new_identity.materials.push_back(
      MakeMaterial(&new_identity.textures.front(), "City/NewFacade"));
  Ogre14LegacyTranslatedFrame sentinel = output;
  const ValidationResult result = translator.Translate(new_identity, sentinel);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "frame.lifetime_asset_records" &&
              translator.source_sequence() == 2U &&
              sentinel.source_sequence == output.source_sequence &&
              sentinel.mutations.size() == output.mutations.size() &&
              sentinel.live_assets.size() == output.live_assets.size(),
          "partial new identities escaped the tombstone lifetime cap or "
          "committed");

  Ogre14LegacyAssetFrameInput retry;
  retry.source_sequence = 3U;
  Require(translator.Translate(retry, sentinel).ok() &&
              translator.source_sequence() == 3U,
          "same source sequence could not retry after lifetime-cap rollback");
}

void TestLifetimeAdmissionPreflightBoundsReuseAndRollback() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration configuration;
  configuration.maximum_texture_inputs_per_frame = 2U;
  configuration.maximum_material_inputs_per_frame = 2U;
  configuration.maximum_live_assets_per_frame = 4U;
  configuration.maximum_lifetime_asset_records = 4U;
  Ogre14LegacyAssetTranslator translator(configuration);

  Ogre14LegacyAssetFrameInput initial = MakeFrame(1U);
  Ogre14LegacyTranslatedFrame initial_output;
  Require(translator.Translate(initial, initial_output).ok() &&
              initial_output.live_assets.size() == 3U,
          "lifetime-preflight baseline catalog failed");

  const BorrowedIdentityFrame initial_identities = BorrowIdentities(initial);
  Require(
      translator.PreflightLifetimeAdmission(initial_identities.view()).ok() &&
          translator.source_sequence() == 1U &&
          translator.catalog_sequence() == 1U,
      "exact persistent identity reuse was rejected or mutated state");

  Ogre14LegacyMaterialInput one_new_material =
      MakeMaterial(nullptr, "City/OneNewMaterial");
  BorrowedIdentityFrame exact_cap = initial_identities;
  exact_cap.materials.push_back(&one_new_material);
  Require(translator.PreflightLifetimeAdmission(exact_cap.view()).ok() &&
              translator.source_sequence() == 1U,
          "one new permanent identity at the exact lifetime cap was rejected");

  Ogre14LegacyAssetFrameInput removed;
  removed.source_sequence = 2U;
  Ogre14LegacyTranslatedFrame tombstoned;
  Require(translator.Translate(removed, tombstoned).ok() &&
              tombstoned.live_assets.empty(),
          "lifetime-preflight tombstone fixture failed");
  Ogre14LegacyTranslatedFrame before_failure;
  Require(translator.BuildFullSnapshot(before_failure).ok(),
          "lifetime-preflight rollback snapshot failed");

  Ogre14LegacyMaterialInput second_new_material =
      MakeMaterial(nullptr, "City/SecondNewMaterial");
  BorrowedIdentityFrame over_cap;
  over_cap.materials = {&one_new_material, &second_new_material};
  const ValidationResult result =
      translator.PreflightLifetimeAdmission(over_cap.view());
  Ogre14LegacyTranslatedFrame after_failure;
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "frame.lifetime_asset_records" &&
              result.element_index == 1U &&
              translator.source_sequence() == 2U &&
              translator.catalog_sequence() == 2U &&
              translator.BuildFullSnapshot(after_failure).ok() &&
              EquivalentFrameValue(before_failure, after_failure) &&
              SameFrameOwners(before_failure, after_failure),
          "prospective lifetime cap failure changed persistent translator "
          "state");

  Require(translator.PreflightLifetimeAdmission(initial_identities.view()).ok(),
          "a tombstoned exact key was counted as a second lifetime identity");
  Ogre14LegacyAssetFrameInput retry;
  retry.source_sequence = 3U;
  Require(translator.Translate(retry, tombstoned).ok() &&
              translator.source_sequence() == 3U,
          "read-only lifetime preflight consumed the retry source sequence");
}

void TestLifetimeAdmissionPreflightRejectsInvalidBorrowedIdentities() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration bounded_configuration;
  bounded_configuration.maximum_texture_inputs_per_frame = 1U;
  bounded_configuration.maximum_material_inputs_per_frame = 1U;
  bounded_configuration.maximum_live_assets_per_frame = 2U;
  bounded_configuration.maximum_lifetime_asset_records = 2U;
  Ogre14LegacyAssetTranslator bounded(bounded_configuration);

  Ogre14LegacyAssetIdentityFrameView view;
  view.version = kOgre14LegacyAssetIdentityFrameViewVersion + 1U;
  ValidationResult result = bounded.PreflightLifetimeAdmission(view);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_VERSION &&
              result.field == "identity_frame.version",
          "unsupported borrowed identity-frame version was accepted");

  view = {};
  view.texture_input_count = 2U;
  result = bounded.PreflightLifetimeAdmission(view);
  Require(!result && result.field == "identity_frame.asset_inputs" &&
              bounded.source_sequence() == 0U,
          "borrowed input count cap was not checked before pointer access");

  view.texture_input_count = 1U;
  result = bounded.PreflightLifetimeAdmission(view);
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
              result.field == "identity_frame.input_ranges",
          "null nonempty borrowed range was accepted");

  const Ogre14LegacyTextureInput *null_texture = nullptr;
  view.texture_inputs = &null_texture;
  result = bounded.PreflightLifetimeAdmission(view);
  Require(!result && result.field == "identity_frame.texture_inputs" &&
              result.element_index == 0U,
          "null borrowed texture element was accepted");

  view = {};
  const Ogre14LegacyMaterialInput *null_material = nullptr;
  view.material_inputs = &null_material;
  view.material_input_count = 1U;
  result = bounded.PreflightLifetimeAdmission(view);
  Require(!result && result.field == "identity_frame.material_inputs" &&
              result.element_index == 0U,
          "null borrowed material element was accepted");

  Ogre14LegacyAssetTranslator translator;
  Ogre14LegacyTextureInput texture = MakeTexture();
  BorrowedIdentityFrame duplicate;
  duplicate.textures = {&texture, &texture};
  result = translator.PreflightLifetimeAdmission(duplicate.view());
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              result.field == "identity_frame.texture_key" &&
              result.element_index == 1U,
          "duplicate borrowed texture identity was accepted");

  Ogre14LegacyTextureInput unsupported_texture = MakeTexture();
  unsupported_texture.version = kOgre14LegacyTextureInputVersion + 1U;
  BorrowedIdentityFrame unsupported_texture_identity;
  unsupported_texture_identity.textures = {&unsupported_texture};
  result = translator.PreflightLifetimeAdmission(
      unsupported_texture_identity.view());
  Require(!result && result.code == ValidationCode::UNSUPPORTED_VERSION &&
              result.field == "identity_frame.texture_version",
          "unsupported borrowed texture input version was accepted");

  Ogre14LegacyMaterialInput unsupported_material = MakeMaterial();
  unsupported_material.version = kOgre14LegacyMaterialInputVersion + 1U;
  BorrowedIdentityFrame unsupported_material_identity;
  unsupported_material_identity.materials = {&unsupported_material};
  result = translator.PreflightLifetimeAdmission(
      unsupported_material_identity.view());
  Require(!result && result.code == ValidationCode::UNSUPPORTED_VERSION &&
              result.field == "identity_frame.material_version",
          "unsupported borrowed material input version was accepted");

  Ogre14LegacyTextureInput invalid = MakeTexture();
  invalid.key.exact_name.clear();
  BorrowedIdentityFrame invalid_identity;
  invalid_identity.textures = {&invalid};
  result = translator.PreflightLifetimeAdmission(invalid_identity.view());
  Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
              translator.source_sequence() == 0U,
          "invalid borrowed texture identity was accepted");

  Ogre14LegacyMaterialInput missing = MakeMaterial(&texture);
  BorrowedIdentityFrame missing_reference;
  missing_reference.materials = {&missing};
  result = translator.PreflightLifetimeAdmission(missing_reference.view());
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
              result.field == "identity_frame.material_texture_key",
          "absent borrowed material texture identity was accepted");

  Ogre14LegacyTextureInput malformed = MakeTexture("City/Malformed");
  malformed.mip_levels.front().slice_pitch_bytes = 3U;
  BorrowedIdentityFrame malformed_identity;
  malformed_identity.textures = {&malformed};
  Require(translator.PreflightLifetimeAdmission(malformed_identity.view()).ok(),
          "identity-only preflight read or decoded texture mip payloads");
  Ogre14LegacyAssetFrameInput malformed_frame;
  malformed_frame.source_sequence = 1U;
  malformed_frame.textures.push_back(malformed);
  Ogre14LegacyTranslatedFrame output;
  result = translator.Translate(malformed_frame, output);
  Require(!result && result.field == "texture.mip_layout" &&
              translator.source_sequence() == 0U,
          "Translate stopped authoritatively revalidating admitted identities");
}

void TestLifetimeAdmissionCollisionAndExceptionRollback() {
  using namespace RoR::Render;
  LifetimeAdmissionFault fault;
  Ogre14LegacyAssetTranslator translator(&fault);
  Ogre14LegacyAssetFrameInput initial = MakeFrame(1U);
  Ogre14LegacyTranslatedFrame output;
  Require(translator.Translate(initial, output).ok(),
          "lifetime-preflight collision baseline failed");
  Ogre14LegacyTranslatedFrame before;
  Require(translator.BuildFullSnapshot(before).ok(),
          "lifetime-preflight collision snapshot failed");

  Ogre14LegacyTextureInput prospective = MakeTexture("City/Prospective");
  BorrowedIdentityFrame prospective_identity;
  prospective_identity.textures = {&prospective};
  fault.behavior = LifetimeAdmissionFault::Behavior::OVERRIDE_SOURCE_ID;
  std::uint64_t noncolliding_override = 1U;
  bool collided = false;
  do {
    collided = false;
    for (const Ogre14LegacyTranslatedAsset &asset : output.live_assets) {
      if (asset.source_asset_id == noncolliding_override) {
        ++noncolliding_override;
        collided = true;
        break;
      }
    }
  } while (collided);
  fault.source_id_override = noncolliding_override;
  const BorrowedIdentityFrame initial_identity = BorrowIdentities(initial);
  ValidationResult result =
      translator.PreflightLifetimeAdmission(initial_identity.view());
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              result.field == "identity_frame.existing_identity",
          "exact persistent key accepted a changed noncolliding source ID");

  fault.source_id_override =
      FindAsset(output, RenderAssetKind::TEXTURE).source_asset_id;
  result = translator.PreflightLifetimeAdmission(prospective_identity.view());
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              result.field == "identity_frame.source_asset_id",
          "prospective identity collision with a persistent ID was accepted");

  LifetimeAdmissionFault local_fault;
  local_fault.behavior = LifetimeAdmissionFault::Behavior::OVERRIDE_SOURCE_ID;
  local_fault.source_id_override = 0xA5A5U;
  Ogre14LegacyAssetTranslator fresh(&local_fault);
  Ogre14LegacyTextureInput local_a = MakeTexture("City/LocalA");
  Ogre14LegacyTextureInput local_b = MakeTexture("City/LocalB");
  BorrowedIdentityFrame local_collision;
  local_collision.textures = {&local_a, &local_b};
  result = fresh.PreflightLifetimeAdmission(local_collision.view());
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              result.field == "identity_frame.source_asset_id" &&
              result.element_index == 1U && fresh.source_sequence() == 0U,
          "two prospective keys sharing one source ID were accepted");

  fault.behavior = LifetimeAdmissionFault::Behavior::THROW_BAD_ALLOC;
  result = translator.PreflightLifetimeAdmission(prospective_identity.view());
  Require(!result && result.code == ValidationCode::EMPTY_PAYLOAD &&
              result.field == "translator.lifetime_preflight.allocation",
          "lifetime-preflight allocation exception escaped");
  fault.behavior = LifetimeAdmissionFault::Behavior::THROW_UNEXPECTED;
  result = translator.PreflightLifetimeAdmission(prospective_identity.view());
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "translator.lifetime_preflight.exception",
          "lifetime-preflight unexpected exception escaped");

  Ogre14LegacyTranslatedFrame after;
  Require(translator.BuildFullSnapshot(after).ok() &&
              translator.source_sequence() == 1U &&
              translator.catalog_sequence() == 1U &&
              EquivalentFrameValue(before, after) &&
              SameFrameOwners(before, after),
          "collision or exception preflight changed committed catalog state");
  fault.behavior = LifetimeAdmissionFault::Behavior::DISABLED;
  Require(
      translator.PreflightLifetimeAdmission(prospective_identity.view()).ok(),
      "valid identity could not retry after preflight rollback");
}

void TestTransactionClonePreservesSourceAndImmutableOwners() {
  using namespace RoR::Render;
  static_assert(
      !std::is_move_assignable_v<Ogre14LegacyAssetTranslator>,
      "implicit move assignment must not bypass transaction validation");
  static_assert(
      noexcept(std::declval<Ogre14LegacyAssetTranslator &>().CommitTransaction(
          std::declval<Ogre14LegacyAssetTranslator &>())),
      "transaction publication must be allocation-free and noexcept");

  Ogre14LegacyAssetTranslator source;
  Ogre14LegacyTranslatedFrame first;
  Require(source.Translate(MakeFrame(1U), first).ok(),
          "transaction source initialization failed");
  Ogre14LegacyTranslatedFrame source_before;
  Require(source.BuildFullSnapshot(source_before).ok(),
          "transaction source snapshot failed");

  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(source.CloneForTransaction(candidate).ok() && candidate != nullptr,
          "transaction candidate clone failed");
  Ogre14LegacyTranslatedFrame candidate_before;
  Require(candidate->BuildFullSnapshot(candidate_before).ok() &&
              EquivalentFrameValue(source_before, candidate_before) &&
              SameFrameOwners(source_before, candidate_before),
          "clone did not deep-copy state while sharing immutable owners");

  Ogre14LegacyAssetFrameInput changed = MakeFrame(2U);
  changed.textures.front().source_revision = 2U;
  changed.textures.front().mip_levels.front().bytes[0U] = 99U;
  Ogre14LegacyTranslatedFrame candidate_advanced;
  Require(candidate->Translate(changed, candidate_advanced).ok() &&
              candidate->source_sequence() == 2U &&
              source.source_sequence() == 1U,
          "candidate translation advanced its committed source");
  Ogre14LegacyTranslatedFrame source_after_candidate;
  Require(source.BuildFullSnapshot(source_after_candidate).ok() &&
              EquivalentFrameValue(source_before, source_after_candidate) &&
              SameFrameOwners(source_before, source_after_candidate),
          "candidate translation replaced source state or owners");

  candidate.reset();
  std::unique_ptr<Ogre14LegacyAssetTranslator> retry;
  Require(source.CloneForTransaction(retry).ok(),
          "discarded transaction could not be cloned again");
  Ogre14LegacyTranslatedFrame retry_advanced;
  Require(retry->Translate(changed, retry_advanced).ok() &&
              EquivalentFrameValue(candidate_advanced, retry_advanced),
          "discard/retry changed deterministic translation output");

  std::unique_ptr<Ogre14LegacyAssetTranslator> nested;
  Require(!retry->CloneForTransaction(nested) && nested == nullptr,
          "candidate recursively forked an unsupported nested transaction");
}

void TestTransactionCommitIsExactAndRejectsInvalidLineage() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslator source;
  Ogre14LegacyTranslatedFrame output;
  Require(source.Translate(MakeFrame(1U), output).ok(),
          "commit source initialization failed");

  std::unique_ptr<Ogre14LegacyAssetTranslator> stale;
  std::unique_ptr<Ogre14LegacyAssetTranslator> winner;
  Require(source.CloneForTransaction(stale).ok() &&
              source.CloneForTransaction(winner).ok(),
          "sibling candidate creation failed");
  Ogre14LegacyAssetFrameInput changed = MakeFrame(2U);
  changed.textures.front().source_revision = 2U;
  changed.textures.front().mip_levels.front().bytes[0U] = 101U;
  Ogre14LegacyTranslatedFrame stale_output;
  Ogre14LegacyTranslatedFrame winner_output;
  Require(stale->Translate(changed, stale_output).ok() &&
              winner->Translate(changed, winner_output).ok(),
          "sibling candidates did not advance independently");
  Ogre14LegacyTranslatedFrame prepared;
  Require(winner->BuildFullSnapshot(prepared).ok(),
          "prepared candidate snapshot failed");
  Require(source.CommitTransaction(*winner) ==
                  Ogre14LegacyAssetTranslatorCommitResult::COMMITTED &&
              winner->source_sequence() == 0U,
          "valid candidate did not publish or invalidate exactly once");
  Ogre14LegacyTranslatedFrame committed;
  Require(source.BuildFullSnapshot(committed).ok() &&
              EquivalentFrameValue(prepared, committed) &&
              SameFrameOwners(prepared, committed),
          "commit did not publish the candidate's exact state and owners");
  Ogre14LegacyTranslatedFrame before_consumed_rejection = committed;
  Require(source.CommitTransaction(*winner) ==
              Ogre14LegacyAssetTranslatorCommitResult::INVALID_CANDIDATE,
          "consumed candidate committed twice");
  Ogre14LegacyTranslatedFrame after_consumed_rejection;
  Require(
      source.BuildFullSnapshot(after_consumed_rejection).ok() &&
          EquivalentFrameValue(before_consumed_rejection,
                               after_consumed_rejection) &&
          SameFrameOwners(before_consumed_rejection, after_consumed_rejection),
      "consumed-candidate rejection modified committed state");
  std::unique_ptr<Ogre14LegacyAssetTranslator> consumed_clone;
  Require(!winner->CloneForTransaction(consumed_clone) &&
              consumed_clone == nullptr,
          "consumed candidate cloned after successful publication");

  Ogre14LegacyTranslatedFrame before_stale_rejection = committed;
  Ogre14LegacyTranslatedFrame stale_before_rejection;
  Require(stale->BuildFullSnapshot(stale_before_rejection).ok(),
          "stale candidate snapshot failed before rejection");
  Require(source.CommitTransaction(*stale) ==
              Ogre14LegacyAssetTranslatorCommitResult::STALE_SOURCE,
          "stale sibling candidate was accepted");
  Ogre14LegacyTranslatedFrame after_stale_rejection;
  Ogre14LegacyTranslatedFrame stale_after_rejection;
  Require(
      source.BuildFullSnapshot(after_stale_rejection).ok() &&
          stale->BuildFullSnapshot(stale_after_rejection).ok() &&
          EquivalentFrameValue(before_stale_rejection, after_stale_rejection) &&
          SameFrameOwners(before_stale_rejection, after_stale_rejection),
      "stale rejection modified committed state or owners");
  Require(EquivalentFrameValue(stale_before_rejection, stale_after_rejection) &&
              SameFrameOwners(stale_before_rejection, stale_after_rejection),
          "stale rejection modified the candidate state or owners");

  Ogre14LegacyAssetTranslator foreign_source;
  Require(foreign_source.Translate(MakeFrame(1U), output).ok(),
          "foreign source initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> foreign_candidate;
  Require(foreign_source.CloneForTransaction(foreign_candidate).ok(),
          "foreign candidate clone failed");
  Ogre14LegacyTranslatedFrame foreign_before;
  Ogre14LegacyTranslatedFrame source_before_foreign;
  Require(foreign_candidate->BuildFullSnapshot(foreign_before).ok() &&
              source.BuildFullSnapshot(source_before_foreign).ok(),
          "foreign rejection snapshots failed");
  Require(source.CommitTransaction(*foreign_candidate) ==
              Ogre14LegacyAssetTranslatorCommitResult::FOREIGN_LINEAGE,
          "foreign lineage candidate was accepted");
  Ogre14LegacyTranslatedFrame foreign_after;
  Ogre14LegacyTranslatedFrame source_after_foreign;
  Require(
      foreign_candidate->BuildFullSnapshot(foreign_after).ok() &&
          source.BuildFullSnapshot(source_after_foreign).ok() &&
          EquivalentFrameValue(foreign_before, foreign_after) &&
          SameFrameOwners(foreign_before, foreign_after) &&
          EquivalentFrameValue(source_before_foreign, source_after_foreign) &&
          SameFrameOwners(source_before_foreign, source_after_foreign),
      "foreign rejection changed source or candidate state/owners");

  Ogre14LegacyAssetTranslator forged_root;
  Require(forged_root.Translate(MakeFrame(1U), output).ok(),
          "forged-root fixture initialization failed");
  Ogre14LegacyTranslatedFrame source_before_terminal_rejections;
  Ogre14LegacyTranslatedFrame forged_before;
  Ogre14LegacyTranslatedFrame receiver_before;
  Require(source.BuildFullSnapshot(source_before_terminal_rejections).ok() &&
              forged_root.BuildFullSnapshot(forged_before).ok() &&
              foreign_candidate->BuildFullSnapshot(receiver_before).ok(),
          "terminal rejection snapshots failed");
  Require(source.CommitTransaction(forged_root) ==
              Ogre14LegacyAssetTranslatorCommitResult::INVALID_CANDIDATE,
          "normal root committed as a forged candidate");
  Require(source.CommitTransaction(source) ==
              Ogre14LegacyAssetTranslatorCommitResult::SELF_COMMIT,
          "self commit was accepted");
  Require(foreign_candidate->CommitTransaction(source) ==
              Ogre14LegacyAssetTranslatorCommitResult::INVALID_SOURCE,
          "candidate acted as a committed source");
  Ogre14LegacyTranslatedFrame source_after_terminal_rejections;
  Ogre14LegacyTranslatedFrame forged_after;
  Ogre14LegacyTranslatedFrame receiver_after;
  Require(source.BuildFullSnapshot(source_after_terminal_rejections).ok() &&
              forged_root.BuildFullSnapshot(forged_after).ok() &&
              foreign_candidate->BuildFullSnapshot(receiver_after).ok() &&
              EquivalentFrameValue(source_before_terminal_rejections,
                                   source_after_terminal_rejections) &&
              SameFrameOwners(source_before_terminal_rejections,
                              source_after_terminal_rejections) &&
              EquivalentFrameValue(forged_before, forged_after) &&
              SameFrameOwners(forged_before, forged_after) &&
              EquivalentFrameValue(receiver_before, receiver_after) &&
              SameFrameOwners(receiver_before, receiver_after),
          "rejected self, forged, or invalid-source commit changed state");
}

void TestSourceAdvanceStalesCandidateWithoutChangingIt() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslator source;
  Ogre14LegacyTranslatedFrame output;
  Require(source.Translate(MakeFrame(1U), output).ok(),
          "source-advance fixture initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(source.CloneForTransaction(candidate).ok(),
          "source-advance candidate clone failed");
  Ogre14LegacyTranslatedFrame candidate_before;
  Require(candidate->BuildFullSnapshot(candidate_before).ok(),
          "source-advance candidate snapshot failed");

  Require(source.Translate(MakeFrame(2U), output).ok(),
          "committed source could not advance independently");
  Ogre14LegacyTranslatedFrame source_before_rejection;
  Require(source.BuildFullSnapshot(source_before_rejection).ok(),
          "advanced source snapshot failed");
  Require(source.CommitTransaction(*candidate) ==
              Ogre14LegacyAssetTranslatorCommitResult::STALE_SOURCE,
          "candidate survived a direct committed-source advance");
  Ogre14LegacyTranslatedFrame candidate_after;
  Ogre14LegacyTranslatedFrame source_after_rejection;
  Require(candidate->BuildFullSnapshot(candidate_after).ok() &&
              source.BuildFullSnapshot(source_after_rejection).ok() &&
              EquivalentFrameValue(candidate_before, candidate_after) &&
              SameFrameOwners(candidate_before, candidate_after) &&
              EquivalentFrameValue(source_before_rejection,
                                   source_after_rejection) &&
              SameFrameOwners(source_before_rejection, source_after_rejection),
          "stale direct-source rejection changed either translator");
}

void TestTransactionEpochExhaustionRejectsCommitExactly() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration configuration;
  Ogre14LegacyAssetTranslatorTransactionConfiguration transaction_configuration;
  transaction_configuration.maximum_epoch = 1U;
  Ogre14LegacyAssetTranslator source(configuration, transaction_configuration);
  Ogre14LegacyTranslatedFrame output;
  Require(source.Translate(MakeFrame(1U), output).ok(),
          "epoch-exhaustion source initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(source.CloneForTransaction(candidate).ok(),
          "epoch-exhaustion candidate clone failed");
  Require(candidate->Translate(MakeFrame(2U), output).ok(),
          "isolated candidate could not stage at the exhausted source epoch");
  Ogre14LegacyTranslatedFrame source_before;
  Ogre14LegacyTranslatedFrame candidate_before;
  Require(source.BuildFullSnapshot(source_before).ok() &&
              candidate->BuildFullSnapshot(candidate_before).ok(),
          "epoch-exhaustion precommit snapshots failed");
  Require(
      source.CommitTransaction(*candidate) ==
          Ogre14LegacyAssetTranslatorCommitResult::TRANSACTION_EPOCH_EXHAUSTED,
      "epoch-exhausted candidate committed");
  Ogre14LegacyTranslatedFrame source_after;
  Ogre14LegacyTranslatedFrame candidate_after;
  Require(source.BuildFullSnapshot(source_after).ok() &&
              candidate->BuildFullSnapshot(candidate_after).ok() &&
              EquivalentFrameValue(source_before, source_after) &&
              SameFrameOwners(source_before, source_after) &&
              EquivalentFrameValue(candidate_before, candidate_after) &&
              SameFrameOwners(candidate_before, candidate_after),
          "epoch exhaustion modified source or candidate state/owners");
}

void TestCandidateWorkConsumesOneEpochOnlyAtPublication() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration configuration;
  Ogre14LegacyAssetTranslatorTransactionConfiguration transaction_configuration;
  transaction_configuration.maximum_epoch = 2U;
  Ogre14LegacyAssetTranslator source(configuration, transaction_configuration);
  Ogre14LegacyTranslatedFrame output;
  Require(source.Translate(MakeFrame(1U), output).ok(),
          "single-epoch source initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(source.CloneForTransaction(candidate).ok() &&
              candidate->Translate(MakeFrame(2U), output).ok() &&
              candidate->Translate(MakeFrame(3U), output).ok(),
          "isolated candidate work consumed its publication epoch");
  Require(source.CommitTransaction(*candidate) ==
                  Ogre14LegacyAssetTranslatorCommitResult::COMMITTED &&
              source.source_sequence() == 3U,
          "one candidate publication did not consume exactly one epoch");

  std::unique_ptr<Ogre14LegacyAssetTranslator> exhausted_candidate;
  Require(source.CloneForTransaction(exhausted_candidate).ok(),
          "max-epoch source could not create a discardable staging fork");
  Ogre14LegacyTranslatedFrame source_before;
  Ogre14LegacyTranslatedFrame candidate_before;
  Require(source.BuildFullSnapshot(source_before).ok() &&
              exhausted_candidate->BuildFullSnapshot(candidate_before).ok(),
          "max-epoch publication snapshots failed");
  Require(source.CommitTransaction(*exhausted_candidate) ==
                  Ogre14LegacyAssetTranslatorCommitResult::
                      TRANSACTION_EPOCH_EXHAUSTED &&
              !source.Translate(MakeFrame(4U), output),
          "max epoch allowed another committed publication");
  Ogre14LegacyTranslatedFrame source_after;
  Ogre14LegacyTranslatedFrame candidate_after;
  Require(source.BuildFullSnapshot(source_after).ok() &&
              exhausted_candidate->BuildFullSnapshot(candidate_after).ok() &&
              EquivalentFrameValue(source_before, source_after) &&
              SameFrameOwners(source_before, source_after) &&
              EquivalentFrameValue(candidate_before, candidate_after) &&
              SameFrameOwners(candidate_before, candidate_after),
          "max-epoch rejection changed source or discardable fork");
}

void TestMoveConstructionPreservesTransactionRolesAndLineage() {
  using namespace RoR::Render;
  static_assert(
      std::is_nothrow_move_constructible_v<Ogre14LegacyAssetTranslator>,
      "role-preserving translator move must remain noexcept");

  Ogre14LegacyAssetTranslator first_source;
  Ogre14LegacyTranslatedFrame output;
  Require(first_source.Translate(MakeFrame(1U), output).ok(),
          "move-candidate source initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> first_candidate;
  Require(first_source.CloneForTransaction(first_candidate).ok(),
          "move-candidate clone failed");
  Ogre14LegacyAssetTranslator moved_candidate(std::move(*first_candidate));
  Require(first_source.CommitTransaction(moved_candidate) ==
                  Ogre14LegacyAssetTranslatorCommitResult::COMMITTED &&
              !first_candidate->BuildFullSnapshot(output),
          "move construction bypassed or lost candidate role/lineage");

  Ogre14LegacyAssetTranslator second_source;
  Require(second_source.Translate(MakeFrame(1U), output).ok(),
          "move-source initialization failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> second_candidate;
  Require(second_source.CloneForTransaction(second_candidate).ok(),
          "move-source candidate clone failed");
  Ogre14LegacyAssetTranslator moved_source(std::move(second_source));
  Require(moved_source.CommitTransaction(*second_candidate) ==
                  Ogre14LegacyAssetTranslatorCommitResult::COMMITTED &&
              second_source.CommitTransaction(moved_source) ==
                  Ogre14LegacyAssetTranslatorCommitResult::INVALID_SOURCE,
          "source move construction bypassed or lost role/lineage");
}

void TestTransactionCloneFaultsLeaveSentinelAndSourceUntouched() {
  using namespace RoR::Render;
  CloneFault fault;
  Ogre14LegacyAssetTranslator source(&fault);
  Ogre14LegacyTranslatedFrame output;
  Require(source.Translate(MakeFrame(1U), output).ok(),
          "clone-fault source initialization failed");
  Ogre14LegacyTranslatedFrame source_before;
  Require(source.BuildFullSnapshot(source_before).ok(),
          "clone-fault source snapshot failed");

  const auto exercise_failure = [&](CloneFault::Behavior behavior,
                                    Ogre14LegacyAssetTranslatorCloneStage stage,
                                    const char *expected_field) {
    auto sentinel = std::make_unique<Ogre14LegacyAssetTranslator>();
    Ogre14LegacyTranslatedFrame sentinel_output;
    Require(sentinel->Translate(MakeFrame(1U, false), sentinel_output).ok(),
            "clone-fault sentinel initialization failed");
    Ogre14LegacyTranslatedFrame sentinel_before;
    Require(sentinel->BuildFullSnapshot(sentinel_before).ok(),
            "clone-fault sentinel snapshot failed");
    Ogre14LegacyAssetTranslator *const sentinel_identity = sentinel.get();
    fault.behavior = behavior;
    fault.stage = stage;
    const ValidationResult result = source.CloneForTransaction(sentinel);
    fault.behavior = CloneFault::Behavior::DISABLED;
    Ogre14LegacyTranslatedFrame sentinel_after;
    Ogre14LegacyTranslatedFrame source_after;
    Require(!result && result.field == expected_field &&
                sentinel.get() == sentinel_identity &&
                sentinel->BuildFullSnapshot(sentinel_after).ok() &&
                source.BuildFullSnapshot(source_after).ok() &&
                EquivalentFrameValue(sentinel_before, sentinel_after) &&
                SameFrameOwners(sentinel_before, sentinel_after) &&
                EquivalentFrameValue(source_before, source_after) &&
                SameFrameOwners(source_before, source_after),
            "clone exception changed source, sentinel, or immutable owners");
  };

  exercise_failure(CloneFault::Behavior::THROW_BAD_ALLOC,
                   Ogre14LegacyAssetTranslatorCloneStage::BEFORE_STATE_COPY,
                   "translator.transaction_allocation");
  exercise_failure(CloneFault::Behavior::THROW_UNEXPECTED,
                   Ogre14LegacyAssetTranslatorCloneStage::AFTER_STATE_COPY,
                   "translator.transaction_exception");
  exercise_failure(
      CloneFault::Behavior::THROW_BAD_ALLOC,
      Ogre14LegacyAssetTranslatorCloneStage::BEFORE_CANDIDATE_PUBLISH,
      "translator.transaction_allocation");

  std::unique_ptr<Ogre14LegacyAssetTranslator> committed_candidate;
  Require(source.CloneForTransaction(committed_candidate).ok() &&
              source.CommitTransaction(*committed_candidate) ==
                  Ogre14LegacyAssetTranslatorCommitResult::COMMITTED,
          "borrowed-injector commit fixture failed");
  const std::size_t callbacks_before = fault.clone_callback_count;
  fault.behavior = CloneFault::Behavior::THROW_UNEXPECTED;
  fault.stage = Ogre14LegacyAssetTranslatorCloneStage::BEFORE_STATE_COPY;
  std::unique_ptr<Ogre14LegacyAssetTranslator> postcommit_candidate;
  const ValidationResult postcommit_result =
      source.CloneForTransaction(postcommit_candidate);
  fault.behavior = CloneFault::Behavior::DISABLED;
  Require(!postcommit_result &&
              postcommit_result.field == "translator.transaction_exception" &&
              postcommit_candidate == nullptr &&
              fault.clone_callback_count == callbacks_before + 1U,
          "commit replaced or detached the borrowed source fault injector");
}

void TestTransactionCloneMetadataCapIsCheckedBeforeCopy() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslator baseline;
  Ogre14LegacyTranslatedFrame baseline_frame;
  Require(baseline.Translate(MakeFrame(1U), baseline_frame).ok(),
          "clone-metadata baseline translation failed");
  std::uint64_t exact_metadata_bytes = 0U;
  for (const auto &asset : baseline_frame.live_assets) {
    Require(asset.stable_key.size() <=
                (std::numeric_limits<std::uint64_t>::max)() / 3U,
            "fixture stable key cannot be represented in clone byte budget");
    exact_metadata_bytes +=
        static_cast<std::uint64_t>(asset.stable_key.size()) * 3U;
  }
  Require(exact_metadata_bytes > 1U,
          "clone-metadata fixture did not create mutable key bytes");

  Ogre14LegacyAssetTranslatorConfiguration translator_configuration;
  Ogre14LegacyAssetTranslatorTransactionConfiguration exact_configuration;
  exact_configuration.maximum_clone_metadata_bytes = exact_metadata_bytes;
  Ogre14LegacyAssetTranslator exact(translator_configuration,
                                    exact_configuration);
  Ogre14LegacyTranslatedFrame output;
  Require(exact.Translate(MakeFrame(1U), output).ok(),
          "exact clone-metadata budget translation failed");
  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(exact.CloneForTransaction(candidate).ok() && candidate != nullptr,
          "exact clone-metadata budget was rejected");

  Ogre14LegacyAssetTranslatorTransactionConfiguration below_configuration =
      exact_configuration;
  below_configuration.maximum_clone_metadata_bytes = exact_metadata_bytes - 1U;
  Ogre14LegacyAssetTranslator below(translator_configuration,
                                    below_configuration);
  Require(below.Translate(MakeFrame(1U), output).ok(),
          "below-cap translator could not establish source state");
  auto sentinel = std::make_unique<Ogre14LegacyAssetTranslator>();
  Ogre14LegacyAssetTranslator *const sentinel_identity = sentinel.get();
  const ValidationResult result = below.CloneForTransaction(sentinel);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "translator.transaction_clone_metadata" &&
              sentinel.get() == sentinel_identity &&
              below.source_sequence() == 1U,
          "cap+1 clone metadata was copied or modified the sentinel/source");

  Ogre14LegacyAssetTranslatorTransactionConfiguration invalid_configuration;
  invalid_configuration.maximum_clone_metadata_bytes = 0U;
  Ogre14LegacyAssetTranslator invalid(translator_configuration,
                                      invalid_configuration);
  candidate.reset();
  Require(!invalid.CloneForTransaction(candidate) && candidate == nullptr,
          "zero clone-metadata limit was accepted");

  Ogre14LegacyAssetTranslatorTransactionConfiguration
      invalid_epoch_configuration;
  invalid_epoch_configuration.maximum_epoch = 0U;
  Ogre14LegacyAssetTranslator invalid_epoch(translator_configuration,
                                            invalid_epoch_configuration);
  Require(!invalid_epoch.CloneForTransaction(candidate) && candidate == nullptr,
          "zero transaction-epoch limit was accepted");

  Ogre14LegacyAssetTranslatorTransactionConfiguration
      invalid_version_configuration;
  invalid_version_configuration.version =
      kOgre14LegacyAssetTranslatorTransactionConfigurationVersion + 1U;
  const ValidationResult invalid_version =
      ValidateOgre14LegacyAssetTranslatorTransactionConfiguration(
          invalid_version_configuration);
  Require(!invalid_version &&
              invalid_version.code == ValidationCode::UNSUPPORTED_VERSION &&
              invalid_version.field == "transaction_configuration.version",
          "unsupported transaction configuration version was accepted");
}

void TestCatalogIdentityIsOpaqueFreshAndCloneExact() {
  using namespace RoR::Render;
  static_assert(!std::is_constructible_v<Ogre14LegacyCatalogIdentityReceipt,
                                         std::shared_ptr<const void>>,
                "callers must not be able to mint a catalog identity receipt");
  static_assert(
      std::is_nothrow_move_constructible_v<
          Ogre14LegacyCatalogIdentityReceipt> &&
          std::is_nothrow_move_assignable_v<
              Ogre14LegacyCatalogIdentityReceipt> &&
          noexcept(std::declval<Ogre14LegacyCatalogIdentityReceipt &>().swap(
              std::declval<Ogre14LegacyCatalogIdentityReceipt &>())) &&
          std::is_nothrow_move_constructible_v<Ogre14LegacyTranslatedFrame> &&
          std::is_nothrow_move_assignable_v<Ogre14LegacyTranslatedFrame>,
      "post-publication identity and frame moves must remain noexcept");

  Ogre14LegacyCatalogIdentityReceipt empty_a;
  Ogre14LegacyCatalogIdentityReceipt empty_b;
  Require(!empty_a.has_value() &&
              !SameOgre14LegacyCatalogIdentity(empty_a, empty_b),
          "empty catalog receipts forged lineage agreement");

  Ogre14LegacyAssetTranslator first;
  Ogre14LegacyAssetTranslator fresh;
  Ogre14LegacyTranslatedFrame first_frame;
  Ogre14LegacyTranslatedFrame fresh_frame;
  Require(first.Translate(MakeFrame(1U), first_frame).ok() &&
              fresh.Translate(MakeFrame(1U), fresh_frame).ok() &&
              first_frame.catalog_identity.has_value() &&
              fresh_frame.catalog_identity.has_value() &&
              !SameOgre14LegacyCatalogIdentity(first_frame.catalog_identity,
                                               fresh_frame.catalog_identity),
          "fresh translators with repeated sequences shared catalog identity");

  Ogre14LegacyTranslatedFrame first_snapshot;
  Require(first.BuildFullSnapshot(first_snapshot).ok() &&
              SameOgre14LegacyCatalogIdentity(first_frame.catalog_identity,
                                              first_snapshot.catalog_identity),
          "one committed translator changed catalog identity");
  std::unique_ptr<Ogre14LegacyAssetTranslator> candidate;
  Require(first.CloneForTransaction(candidate).ok(),
          "identity candidate clone failed");
  Ogre14LegacyTranslatedFrame candidate_snapshot;
  Require(
      candidate->BuildFullSnapshot(candidate_snapshot).ok() &&
          SameOgre14LegacyCatalogIdentity(first_frame.catalog_identity,
                                          candidate_snapshot.catalog_identity),
      "transaction clone did not share exact catalog identity");
  candidate.reset();
}

void TestExclusiveCommittableTransactionIsAtomicAndInfallible() {
  using namespace RoR::Render;
  static_assert(
      std::is_nothrow_move_constructible_v<
          Ogre14LegacyAssetTranslatorCommittableTransaction> &&
          std::is_nothrow_move_assignable_v<
              Ogre14LegacyAssetTranslatorCommittableTransaction> &&
          noexcept(std::declval<
                       Ogre14LegacyAssetTranslatorCommittableTransaction &>()
                       .CommitAfterAcceptedExposure()),
      "exclusive post-publication path must remain noexcept");

  ExclusiveFault fault;
  Ogre14LegacyAssetTranslator source(&fault);
  Ogre14LegacyTranslatedFrame initial;
  Require(source.Translate(MakeFrame(1U), initial).ok(),
          "exclusive source initialization failed");

  std::unique_ptr<Ogre14LegacyAssetTranslator> sibling;
  Require(source.CloneForTransaction(sibling).ok(),
          "exclusive sibling fixture clone failed");
  Ogre14LegacyAssetTranslatorCommittableTransaction rejected;
  ValidationResult validation = source.BeginCommittableTransaction(rejected);
  Require(!validation &&
              validation.field == "translator.transaction_candidates" &&
              !rejected.active() && source.source_sequence() == 1U,
          "exclusive begin ignored an outstanding sibling or mutated output");
  sibling.reset();

  Ogre14LegacyAssetTranslatorCommittableTransaction transaction;
  Require(source.BeginCommittableTransaction(transaction).ok() &&
              transaction.active() && transaction.candidate() != nullptr,
          "exclusive transaction did not acquire its unique lease");
  Ogre14LegacyAssetTranslatorCommittableTransaction nested;
  std::unique_ptr<Ogre14LegacyAssetTranslator> nested_clone;
  Ogre14LegacyTranslatedFrame source_sentinel = initial;
  Ogre14LegacyAssetTranslator foreign_source;
  Ogre14LegacyTranslatedFrame foreign_output;
  Require(foreign_source.Translate(MakeFrame(1U), foreign_output).ok(),
          "exclusive foreign-source fixture failed");
  Require(
      !source.BeginCommittableTransaction(nested) && !nested.active() &&
          !source.CloneForTransaction(nested_clone) &&
          nested_clone == nullptr &&
          !source.Translate(MakeFrame(2U), source_sentinel) &&
          source.source_sequence() == 1U &&
          EquivalentFrameValue(initial, source_sentinel) &&
          source.CommitTransaction(*transaction.candidate()) ==
              Ogre14LegacyAssetTranslatorCommitResult::
                  EXCLUSIVE_LEASE_REQUIRED &&
          foreign_source.CommitTransaction(*transaction.candidate()) ==
              Ogre14LegacyAssetTranslatorCommitResult::FOREIGN_LINEAGE,
      "nested fork, direct source mutation, or legacy commit bypassed lease");

  fault.fail_translate = true;
  Ogre14LegacyTranslatedFrame failed_output = initial;
  validation = transaction.candidate()->Translate(MakeFrame(2U), failed_output);
  fault.fail_translate = false;
  Require(!validation &&
              validation.field == "translator.exclusive_test_fault" &&
              EquivalentFrameValue(initial, failed_output) &&
              transaction.candidate()->source_sequence() == 1U,
          "injected candidate failure escaped the isolated transaction");

  Ogre14LegacyAssetFrameInput changed = MakeFrame(2U);
  changed.textures.front().source_revision = 2U;
  changed.textures.front().mip_levels.front().bytes[0U] = 201U;
  Ogre14LegacyTranslatedFrame staged;
  Ogre14LegacyTranslatedFrame prepared;
  Require(transaction.candidate()->Translate(changed, staged).ok() &&
              transaction.candidate()->BuildFullSnapshot(prepared).ok(),
          "exclusive candidate could not stage accepted state");

  Ogre14LegacyAssetTranslatorCommittableTransaction moved_transaction(
      std::move(transaction));
  Require(!transaction.active() && moved_transaction.active(),
          "exclusive lease move duplicated or dropped ownership");
  Ogre14LegacyAssetTranslator moved_source(std::move(source));
  Require(moved_transaction.CommitAfterAcceptedExposure() ==
                  Ogre14LegacyAssetTranslatorExclusiveCommitResult::COMMITTED &&
              !moved_transaction.active() &&
              moved_transaction.CommitAfterAcceptedExposure() ==
                  Ogre14LegacyAssetTranslatorExclusiveCommitResult::
                      ALREADY_CONSUMED,
          "exclusive accepted publication was fallible or committed twice");
  Ogre14LegacyTranslatedFrame committed;
  Require(moved_source.BuildFullSnapshot(committed).ok() &&
              EquivalentFrameValue(prepared, committed) &&
              SameFrameOwners(prepared, committed),
          "exclusive publication did not expose the exact prepared owners");

  Ogre14LegacyAssetTranslatorCommittableTransaction discarded;
  Require(moved_source.BeginCommittableTransaction(discarded).ok(),
          "postcommit exclusive lease could not begin");
  changed.source_sequence = 3U;
  Ogre14LegacyTranslatedFrame discarded_stage;
  Require(discarded.candidate()->Translate(changed, discarded_stage).ok(),
          "discard candidate staging failed");
  discarded.Discard();
  Require(!discarded.active() && moved_source.source_sequence() == 2U &&
              moved_source.catalog_sequence() == prepared.catalog_sequence &&
              moved_source.Translate(changed, committed).ok() &&
              moved_source.source_sequence() == 3U,
          "discard changed source lineage or failed to release lease");
}

void TestExclusiveLeaseFaultExhaustionAndDestructionRelease() {
  using namespace RoR::Render;
  Ogre14LegacyAssetTranslatorConfiguration configuration;
  Ogre14LegacyAssetTranslatorTransactionConfiguration transaction_configuration;
  transaction_configuration.maximum_epoch = 1U;
  Ogre14LegacyAssetTranslator exhausted(configuration,
                                        transaction_configuration);
  Ogre14LegacyTranslatedFrame output;
  Require(exhausted.Translate(MakeFrame(1U), output).ok(),
          "exclusive exhaustion fixture failed");
  Ogre14LegacyAssetTranslatorCommittableTransaction exhausted_transaction;
  const ValidationResult exhausted_result =
      exhausted.BeginCommittableTransaction(exhausted_transaction);
  Require(!exhausted_result &&
              exhausted_result.field == "translator.transaction_epoch" &&
              !exhausted_transaction.active(),
          "exclusive begin did not preflight epoch exhaustion");

  CloneFault clone_fault;
  Ogre14LegacyAssetTranslator clone_source(&clone_fault);
  Require(clone_source.Translate(MakeFrame(1U), output).ok(),
          "exclusive clone-fault fixture failed");
  clone_fault.behavior = CloneFault::Behavior::THROW_BAD_ALLOC;
  clone_fault.stage =
      Ogre14LegacyAssetTranslatorCloneStage::BEFORE_CANDIDATE_PUBLISH;
  Ogre14LegacyAssetTranslatorCommittableTransaction clone_failed;
  const ValidationResult clone_result =
      clone_source.BeginCommittableTransaction(clone_failed);
  clone_fault.behavior = CloneFault::Behavior::DISABLED;
  Require(!clone_result &&
              clone_result.field == "translator.transaction_allocation" &&
              !clone_failed.active(),
          "exclusive clone failure published output or retained its lease");
  {
    Ogre14LegacyAssetTranslatorCommittableTransaction scoped;
    Require(clone_source.BeginCommittableTransaction(scoped).ok() &&
                scoped.active(),
            "exclusive lease did not recover after clone failure");
  }
  Require(clone_source.Translate(MakeFrame(2U), output).ok(),
          "RAII destruction did not release the exclusive lease");

  Ogre14LegacyAssetTranslator move_source_a;
  Ogre14LegacyAssetTranslator move_source_b;
  Require(move_source_a.Translate(MakeFrame(1U), output).ok() &&
              move_source_b.Translate(MakeFrame(1U), output).ok(),
          "exclusive move-assignment sources failed");
  Ogre14LegacyAssetTranslatorCommittableTransaction move_a;
  Ogre14LegacyAssetTranslatorCommittableTransaction move_b;
  Require(move_source_a.BeginCommittableTransaction(move_a).ok() &&
              move_source_b.BeginCommittableTransaction(move_b).ok(),
          "exclusive move-assignment leases failed");
  move_b = std::move(move_a);
  Require(!move_a.active() && move_b.active() &&
              move_source_b.Translate(MakeFrame(2U), output).ok(),
          "lease move assignment did not discard its previous ownership");
  move_b.Discard();
  Require(move_source_a.Translate(MakeFrame(2U), output).ok(),
          "moved lease did not preserve then release new ownership");

  Ogre14LegacyAssetTranslatorCommittableTransaction orphaned;
  {
    auto dying_source = std::make_unique<Ogre14LegacyAssetTranslator>();
    Require(dying_source->Translate(MakeFrame(1U), output).ok() &&
                dying_source->BeginCommittableTransaction(orphaned).ok(),
            "orphaned exclusive fixture failed");
  }
  Require(orphaned.CommitAfterAcceptedExposure() ==
                  Ogre14LegacyAssetTranslatorExclusiveCommitResult::
                      INVALID_SOURCE &&
              orphaned.CommitAfterAcceptedExposure() ==
                  Ogre14LegacyAssetTranslatorExclusiveCommitResult::
                      ALREADY_CONSUMED,
          "destroyed source left a forgeable or reusable exclusive lease");
}

} // namespace

int main() {
  TestByteExactFormatsEndiannessAndSrgbRole();
  TestTextureRejectsAmbiguousTransformsAndMalformedLayouts();
  TestExactSamplerMaterialAndPipelineTranslation();
  TestUnsupportedMaterialFeaturesRejectWithoutGuessing();
  TestStableIdsContentRevisionsCacheAndTombstones();
  TestTransactionsFaultsAndLineageAreAtomic();
  TestEmptyAuthoritativeCatalogInitializesSequence();
  TestLinearTextureCannotAcquireAmbiguousPbrRole();
  TestConfiguredBoundsRejectTransactionally();
  TestLifetimeRecordCapIncludesPermanentTombstones();
  TestLifetimeAdmissionPreflightBoundsReuseAndRollback();
  TestLifetimeAdmissionPreflightRejectsInvalidBorrowedIdentities();
  TestLifetimeAdmissionCollisionAndExceptionRollback();
  TestTransactionClonePreservesSourceAndImmutableOwners();
  TestTransactionCommitIsExactAndRejectsInvalidLineage();
  TestSourceAdvanceStalesCandidateWithoutChangingIt();
  TestTransactionEpochExhaustionRejectsCommitExactly();
  TestCandidateWorkConsumesOneEpochOnlyAtPublication();
  TestMoveConstructionPreservesTransactionRolesAndLineage();
  TestTransactionCloneFaultsLeaveSentinelAndSourceUntouched();
  TestTransactionCloneMetadataCapIsCheckedBeforeCopy();
  TestCatalogIdentityIsOpaqueFreshAndCloneExact();
  TestExclusiveCommittableTransactionIsAtomicAndInfallible();
  TestExclusiveLeaseFaultExhaustionAndDestructionRelease();
  std::cout << "OGRE 14 legacy asset translator tests passed\n";
  return EXIT_SUCCESS;
}
