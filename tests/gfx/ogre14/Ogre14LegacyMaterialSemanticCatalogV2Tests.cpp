/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14LegacyMaterialSemanticCatalogV2.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef ROR_OGRE14_MATERIAL_SEMANTIC_CATALOG_V2_FIXTURE
#error "compiled synthetic catalog fixture path is required"
#endif

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::uint8_t> LoadFixture() {
  std::ifstream input(ROR_OGRE14_MATERIAL_SEMANTIC_CATALOG_V2_FIXTURE,
                      std::ios::binary);
  Require(input.good(), "could not open compiled synthetic catalog fixture");
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

Ogre14LegacySha256 ExpectedDigest() {
  return Ogre14LegacySha256{{
      0x4bU, 0x72U, 0x3fU, 0x73U, 0xc8U, 0x74U, 0xb4U, 0xdfU,
      0xe0U, 0x6aU, 0xa4U, 0xddU, 0xd7U, 0x02U, 0x6aU, 0x62U,
      0x76U, 0x4cU, 0x6fU, 0xd5U, 0xdaU, 0x30U, 0x45U, 0xb5U,
      0x50U, 0x1fU, 0x99U, 0x0eU, 0xcdU, 0x28U, 0xc2U, 0xceU,
  }};
}

class ThrowingCatalogFault final
    : public IOgre14LegacyMaterialSemanticCatalogV2FaultInjector {
public:
  Ogre14LegacyMaterialSemanticCatalogV2ParseStage stage =
      Ogre14LegacyMaterialSemanticCatalogV2ParseStage::AFTER_FIRST_RECORD;
  bool bad_allocation = true;

  void BeforeCatalogParseStage(
      Ogre14LegacyMaterialSemanticCatalogV2ParseStage current) override {
    if (current != stage) {
      return;
    }
    if (bad_allocation) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void TestExactCatalogAndRegistryFeed() {
  const std::vector<std::uint8_t> bytes = LoadFixture();
  Require(bytes.size() == 1067U, "synthetic compiler output size drifted");
  Ogre14LegacyMaterialSemanticCatalogV2 catalog;
  Require(ParseOgre14LegacyMaterialSemanticCatalogV2(
              Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, bytes,
              catalog)
              .ok(),
          "valid compiled catalog did not parse");
  Require(catalog.initialized() && catalog.size() == 2U &&
              catalog.content_sha256() == ExpectedDigest(),
          "parsed catalog did not retain exact stable content identity");

  const Ogre14LegacyAssetKey key{"SyntheticGroup", "AlphaReflective"};
  const Ogre14LegacyMaterialSemanticCatalogV2Record *record =
      catalog.FindExact(key);
  Require(record != nullptr && record->resource_generation == 7U &&
              record->repair_plan_version == 3U &&
              record->declaration_revision == 41U &&
              record->selected_scheme == "ShaderGeneratorDefaultScheme" &&
              record->selected_lod == 2U &&
              record->runtime_generation ==
                  Ogre14LegacyMaterialRuntimeGeneration::REPAIRED_SCRIPT &&
              record->exact_lowering_algorithm ==
                  "ror.ogre14.explicit-multiunit-pbr" &&
              record->lowering_version == 2U,
          "catalog lost exact package/script/native/lowering selection facts");
  Require(record->package_archive_sha256.front() == 0xaaU &&
              record->source_script_sha256.front() == 0xbbU &&
              record->effective_script_sha256.front() == 0xccU &&
              record->native_structure_sha256.front() == 0xddU,
          "catalog lost an authenticated SHA-256 binding");
  Require(record->pass.source_color ==
              Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
              !record->pass.depth_write_enabled &&
              record->pass.cull ==
                  Ogre14LegacyCullMode::ANTICLOCKWISE &&
              record->pass.alpha_reject ==
                  Ogre14LegacyCompareOperation::GREATER &&
              record->pass.alpha_reject_value == 8U &&
              record->pass.alpha_to_coverage,
          "catalog lost exact blend/depth/cull/alpha facts");
  Require(record->environment_augmentation ==
              Ogre14LegacyEnvironmentAugmentation::CUBE_REFLECTION &&
              record->environment_texture_unit == 1U &&
              record->shadow_augmentation ==
                  Ogre14LegacyShadowAugmentation::RECEIVE_AND_CAST &&
              record->shadow_technique ==
                  Ogre14LegacyShadowTechnique::PSSM,
          "catalog lost explicit environment or shadow augmentation");
  Require(record->texture_units.size() == 2U &&
              record->texture_units[0U].semantic ==
                  Ogre14LegacyTextureSemantic::BASE_COLOR &&
              record->texture_units[0U].swizzle[3U] ==
                  Ogre14LegacyTextureSwizzle::ALPHA &&
              record->texture_units[0U].texture_coordinate_set == 0U &&
              record->texture_units[0U].uv_transform_f32_bits[0U] ==
                  0x3F800000U &&
              record->texture_units[0U].sampler.maximum_anisotropy == 8U &&
              record->texture_units[0U].combine.color_source_two ==
                  Ogre14LegacyTextureCombineSource::DIFFUSE &&
              record->texture_units[0U]
                      .combine.color_manual_one_f32_bits[0U] ==
                  0x3F800000U &&
              record->texture_units[1U].semantic ==
                  Ogre14LegacyTextureSemantic::ENVIRONMENT,
          "catalog lost explicit per-unit semantic/role/swizzle/UV/sampler/combine facts");
  Ogre14LegacyAssetKey wrong_case = key;
  wrong_case.exact_name = "alphareflective";
  Require(catalog.FindExact(wrong_case) == nullptr,
          "catalog performed a case-insensitive material lookup");

  Ogre14LegacyMaterialSemanticRegistry registry;
  Require(BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
              catalog, Ogre14LegacyMaterialSemanticRegistryConfiguration{},
              registry)
              .ok() &&
              registry.size() == 2U,
          "catalog did not feed the existing immutable registry");
  Ogre14LegacyMaterialSemanticResolution resolution;
  Ogre14LegacyAssetTranslatorConfiguration translator_configuration;
  Require(registry.Resolve(key, translator_configuration, resolution).ok() &&
              resolution.source ==
                  Ogre14LegacyMaterialSemanticSource::
                      VERSIONED_COMPATIBILITY_TABLE &&
              resolution.source_revision == 41U &&
              resolution.declaration_identity.has_value() &&
              resolution.native_declaration.base_color_semantic ==
                  Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR &&
              resolution.native_declaration.texture_color_role ==
                  Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB,
          "registry feed invented or lost explicit lowering semantics");
}

void TestFailClosedFramingCapsAndEmptyOutputInvariance() {
  const std::vector<std::uint8_t> bytes = LoadFixture();
  Ogre14LegacyMaterialSemanticCatalogV2 catalog;
  Require(ParseOgre14LegacyMaterialSemanticCatalogV2(
              Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, bytes,
              catalog)
              .ok(),
          "baseline catalog parse failed");
  const Ogre14LegacyMaterialSemanticCatalogV2 owner = catalog;
  const Ogre14LegacySha256 digest = catalog.content_sha256();

  auto RequireUnchanged = [&catalog, &owner, &digest](const char *message) {
    Require(catalog.SharesImmutableStateWith(owner) && catalog.size() == 2U &&
                catalog.content_sha256() == digest,
            message);
  };

  std::vector<std::uint8_t> empty;
  Require(!ParseOgre14LegacyMaterialSemanticCatalogV2(
              Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, empty,
              catalog),
          "empty compiled input was accepted");
  RequireUnchanged("empty input mutated committed output");

  std::vector<std::uint8_t> trailing = bytes;
  trailing.push_back(0U);
  const ValidationResult trailing_result =
      ParseOgre14LegacyMaterialSemanticCatalogV2(
          Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, trailing,
          catalog);
  Require(!trailing_result &&
              trailing_result.code == ValidationCode::SIZE_MISMATCH,
          "unknown trailing compiled field was accepted");
  RequireUnchanged("trailing data mutated committed output");

  std::vector<std::uint8_t> tampered = bytes;
  tampered.back() ^= 1U;
  const ValidationResult digest_result =
      ParseOgre14LegacyMaterialSemanticCatalogV2(
          Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, tampered,
          catalog);
  Require(!digest_result &&
              digest_result.field == "semantic_catalog.content_sha256",
          "tampered catalog passed stable content authentication");
  RequireUnchanged("digest failure mutated committed output");

  std::vector<std::uint8_t> extended_header = bytes;
  extended_header[12U] = 1U;
  Require(!ParseOgre14LegacyMaterialSemanticCatalogV2(
              Ogre14LegacyMaterialSemanticCatalogV2Configuration{},
              extended_header, catalog),
          "unknown compiled header flag was accepted");
  RequireUnchanged("unknown header flag mutated committed output");

  Ogre14LegacyMaterialSemanticCatalogV2Configuration constrained;
  constrained.maximum_records = 1U;
  Require(!ParseOgre14LegacyMaterialSemanticCatalogV2(
              constrained, bytes, catalog),
          "record cap was not enforced before allocation");
  RequireUnchanged("cap failure mutated committed output");
  constrained = Ogre14LegacyMaterialSemanticCatalogV2Configuration{};
  constrained.maximum_catalog_bytes = bytes.size() - 1U;
  Require(!ParseOgre14LegacyMaterialSemanticCatalogV2(
              constrained, bytes, catalog),
          "byte cap was not enforced before allocation");
  RequireUnchanged("byte-cap failure mutated committed output");
}

void TestTransactionalExceptionRollback() {
  const std::vector<std::uint8_t> bytes = LoadFixture();
  Ogre14LegacyMaterialSemanticCatalogV2 catalog;
  Require(ParseOgre14LegacyMaterialSemanticCatalogV2(
              Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, bytes,
              catalog)
              .ok(),
          "baseline catalog parse failed");
  const Ogre14LegacyMaterialSemanticCatalogV2 owner = catalog;

  ThrowingCatalogFault fault;
  ValidationResult result = ParseOgre14LegacyMaterialSemanticCatalogV2(
      Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, bytes, catalog,
      &fault);
  Require(!result && result.field == "semantic_catalog.allocation" &&
              catalog.SharesImmutableStateWith(owner),
          "bad_alloc changed catalog output or immutable owner");

  fault.bad_allocation = false;
  fault.stage =
      Ogre14LegacyMaterialSemanticCatalogV2ParseStage::BEFORE_COMMIT;
  result = ParseOgre14LegacyMaterialSemanticCatalogV2(
      Ogre14LegacyMaterialSemanticCatalogV2Configuration{}, bytes, catalog,
      &fault);
  Require(!result && result.field == "semantic_catalog.exception" &&
              catalog.SharesImmutableStateWith(owner),
          "unexpected exception changed catalog output or immutable owner");
}

void TestConfigurationAndTypeContracts() {
  Ogre14LegacyMaterialSemanticCatalogV2Configuration configuration;
  configuration.version += 1U;
  Require(!ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(
              configuration),
          "unknown catalog configuration version was accepted");
  configuration = Ogre14LegacyMaterialSemanticCatalogV2Configuration{};
  configuration.maximum_texture_units_per_record = 0U;
  Require(!ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(
              configuration),
          "zero texture-unit cap was accepted");
  configuration = Ogre14LegacyMaterialSemanticCatalogV2Configuration{};
  configuration.maximum_total_string_bytes =
      kOgre14LegacyMaterialSemanticCatalogV2MaximumStringBytes + 1U;
  Require(!ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(
              configuration),
          "string cap above the hard maximum was accepted");

  Ogre14LegacyMaterialSemanticRegistry registry;
  Ogre14LegacyMaterialSemanticCatalogV2 empty;
  Require(!BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
              empty, Ogre14LegacyMaterialSemanticRegistryConfiguration{},
              registry),
          "uninitialized catalog fed the semantic registry");

  static_assert(std::is_nothrow_copy_constructible_v<
                    Ogre14LegacyMaterialSemanticCatalogV2> &&
                    std::is_nothrow_copy_assignable_v<
                        Ogre14LegacyMaterialSemanticCatalogV2> &&
                    std::is_nothrow_move_constructible_v<
                        Ogre14LegacyMaterialSemanticCatalogV2> &&
                    std::is_nothrow_move_assignable_v<
                        Ogre14LegacyMaterialSemanticCatalogV2>,
                "immutable catalog ownership must be noexcept transferable");
}

} // namespace

int main() {
  TestExactCatalogAndRegistryFeed();
  TestFailClosedFramingCapsAndEmptyOutputInvariance();
  TestTransactionalExceptionRollback();
  TestConfigurationAndTypeContracts();
  std::cout << "OGRE14 material semantic catalog v2 tests passed\n";
  return EXIT_SUCCESS;
}
