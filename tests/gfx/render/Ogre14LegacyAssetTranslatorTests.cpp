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
                  sampler_asset.source_asset_id,
          "explicit base-color material or exact pipeline audit changed");
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
  std::cout << "OGRE 14 legacy asset translator tests passed\n";
  return EXIT_SUCCESS;
}
