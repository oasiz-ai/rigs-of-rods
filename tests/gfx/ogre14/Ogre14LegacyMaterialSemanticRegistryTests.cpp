/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Ogre14LegacyMaterialSemanticDeclaration MakeDeclaration(
    std::string group, std::string name, std::uint64_t revision,
    Ogre14LegacyBaseColorSemantic semantic =
        Ogre14LegacyBaseColorSemantic::UNLIT,
    Ogre14LegacyMaterialSemanticSource source =
        Ogre14LegacyMaterialSemanticSource::CONTENT_METADATA) {
  Ogre14LegacyMaterialSemanticDeclaration declaration;
  declaration.material_key.exact_resource_group = std::move(group);
  declaration.material_key.exact_name = std::move(name);
  declaration.source = source;
  declaration.source_revision = revision;
  declaration.base_color_semantic = semantic;
  return declaration;
}

void RequireResolutionUnchanged(
    const Ogre14LegacyMaterialSemanticResolution &resolution,
    const char *message) {
  Require(resolution.version == 77U && resolution.source_revision == 88U &&
              resolution.registry_fingerprint == 99U &&
              resolution.native_declaration.version == 66U &&
              resolution.material_key.exact_resource_group ==
                  "sentinel-group" &&
              resolution.material_key.exact_name == "sentinel-name",
          message);
}

Ogre14LegacyMaterialSemanticResolution SentinelResolution() {
  Ogre14LegacyMaterialSemanticResolution resolution;
  resolution.version = 77U;
  resolution.material_key.exact_resource_group = "sentinel-group";
  resolution.material_key.exact_name = "sentinel-name";
  resolution.source_revision = 88U;
  resolution.registry_fingerprint = 99U;
  resolution.native_declaration.version = 66U;
  return resolution;
}

class ThrowingFault final
    : public IOgre14LegacyMaterialSemanticRegistryFaultInjector {
public:
  Ogre14LegacyMaterialSemanticRegistryBuildStage stage =
      Ogre14LegacyMaterialSemanticRegistryBuildStage::AFTER_FIRST_DECLARATION;
  bool bad_allocation = true;

  void BeforeRegistryBuildStage(
      Ogre14LegacyMaterialSemanticRegistryBuildStage current) override {
    if (current != stage) {
      return;
    }
    if (bad_allocation) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void TestExactResolutionAndStableOrdering() {
  Ogre14LegacyMaterialSemanticRegistryConfiguration configuration;
  Ogre14LegacyMaterialSemanticDeclaration building = MakeDeclaration(
      "CityWorld", "NeoQ/Building", 7U,
      Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR,
      Ogre14LegacyMaterialSemanticSource::VERSIONED_COMPATIBILITY_TABLE);
  Ogre14LegacyMaterialSemanticDeclaration road =
      MakeDeclaration("CityWorld", "Road", 4U);

  Ogre14LegacyMaterialSemanticRegistry forward;
  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              configuration, {building, road}, forward)
              .ok(),
          "valid explicit declarations did not build");
  Require(forward.initialized() && forward.size() == 2U &&
              forward.content_fingerprint() == 0x500B5BE128A7A3C2ULL,
          "built registry did not retain bounded immutable state");

  Ogre14LegacyMaterialSemanticRegistry reverse;
  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              configuration, {road, building}, reverse)
              .ok() &&
              reverse.content_fingerprint() == forward.content_fingerprint(),
          "semantic fingerprint depended on caller declaration ordering");

  Ogre14LegacyAssetTranslatorConfiguration translator_configuration;
  translator_configuration.maximum_decoded_bytes_per_asset = 31U;
  translator_configuration.maximum_decoded_bytes_per_frame = 63U;
  Ogre14LegacyMaterialSemanticResolution resolution;
  Require(forward.Resolve(building.material_key, translator_configuration,
                          resolution)
              .ok() &&
              resolution.version ==
                  kOgre14LegacyMaterialSemanticResolutionVersion &&
              resolution.source ==
                  Ogre14LegacyMaterialSemanticSource::
                      VERSIONED_COMPATIBILITY_TABLE &&
              resolution.source_revision == 7U &&
              resolution.registry_fingerprint ==
                  forward.content_fingerprint() &&
              Ogre14LegacyMaterialSemanticResolutionMatchesKey(
                  resolution, building.material_key) &&
              resolution.native_declaration.base_color_semantic ==
                  Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR &&
              resolution.native_declaration.texture_color_role ==
                  Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB &&
              resolution.native_declaration.translator_configuration
                      .maximum_decoded_bytes_per_asset == 31U &&
              resolution.native_declaration.translator_configuration
                      .maximum_decoded_bytes_per_frame == 63U,
          "exact semantic declaration did not resolve with provenance");
  Require(!Ogre14LegacyMaterialSemanticResolutionMatchesKey(resolution, road.material_key),
          "semantic resolution matched a different exact material key");

  Ogre14LegacyAssetKey wrong_case = building.material_key;
  wrong_case.exact_name = "NeoQ/building";
  Ogre14LegacyMaterialSemanticResolution sentinel = SentinelResolution();
  const ValidationResult missing =
      forward.Resolve(wrong_case, translator_configuration, sentinel);
  Require(!missing && missing.code == ValidationCode::MISSING_REFERENCE,
          "case-folded material key silently matched an exact declaration");
  RequireResolutionUnchanged(sentinel,
                             "missing resolution mutated caller sentinel");

  // The immutable registry must not alias mutable declaration input strings.
  building.material_key.exact_name = "mutated";
  Ogre14LegacyAssetKey original_key{"CityWorld", "NeoQ/Building"};
  Require(forward.Resolve(original_key, translator_configuration, resolution)
              .ok(),
          "caller mutation changed immutable registry keys");
}

void TestConfigurationAndDeclarationValidation() {
  Ogre14LegacyMaterialSemanticRegistryConfiguration configuration;
  Ogre14LegacyMaterialSemanticRegistry output;
  const Ogre14LegacyMaterialSemanticDeclaration valid =
      MakeDeclaration("Main", "Valid", 1U);

  Ogre14LegacyMaterialSemanticRegistryConfiguration invalid = configuration;
  invalid.version += 1U;
  Require(!BuildOgre14LegacyMaterialSemanticRegistry(invalid, {valid}, output),
          "unknown registry configuration version was accepted");
  invalid = configuration;
  invalid.maximum_declarations = 0U;
  Require(!BuildOgre14LegacyMaterialSemanticRegistry(invalid, {valid}, output),
          "zero declaration cap was accepted");
  invalid = configuration;
  invalid.maximum_declarations =
      kDefaultOgre14LegacyMaximumSemanticDeclarations + 1U;
  Require(!BuildOgre14LegacyMaterialSemanticRegistry(invalid, {valid}, output),
          "declaration cap above the hard limit was accepted");
  invalid = configuration;
  invalid.maximum_total_key_bytes =
      kDefaultOgre14LegacyMaximumSemanticKeyBytes + 1U;
  Require(!BuildOgre14LegacyMaterialSemanticRegistry(invalid, {valid}, output),
          "key-byte cap above the hard limit was accepted");

  auto RequireInvalid = [&configuration, &output](
                            Ogre14LegacyMaterialSemanticDeclaration candidate,
                            const char *message) {
    const ValidationResult result = BuildOgre14LegacyMaterialSemanticRegistry(
        configuration, {candidate}, output);
    Require(!result, message);
  };
  Ogre14LegacyMaterialSemanticDeclaration candidate = valid;
  candidate.version += 1U;
  RequireInvalid(candidate, "unknown declaration version was accepted");
  candidate = valid;
  candidate.material_key.exact_name.clear();
  RequireInvalid(candidate, "empty exact material name was accepted");
  candidate = valid;
  candidate.material_key.exact_name = std::string("bad\0name", 8U);
  RequireInvalid(candidate, "embedded-NUL exact material name was accepted");
  candidate = valid;
  candidate.source =
      static_cast<Ogre14LegacyMaterialSemanticSource>(255U);
  RequireInvalid(candidate, "unknown declaration source was accepted");
  candidate = valid;
  candidate.source_revision = 0U;
  RequireInvalid(candidate, "zero declaration revision was accepted");
  candidate = valid;
  candidate.source_revision = (std::numeric_limits<std::uint64_t>::max)();
  RequireInvalid(candidate, "exhausted declaration revision was accepted");
  candidate = valid;
  candidate.base_color_semantic =
      static_cast<Ogre14LegacyBaseColorSemantic>(255U);
  RequireInvalid(candidate, "unknown base-color semantic was accepted");
  candidate = valid;
  candidate.texture_color_role =
      static_cast<Ogre14LegacyTextureColorRole>(255U);
  RequireInvalid(candidate, "unknown texture color role was accepted");
  candidate = valid;
  candidate.material_key.exact_name.assign(
      kMaximumOgre14LegacyStableAssetKeyBytes + 1U, 'x');
  RequireInvalid(candidate, "oversized stable material key was accepted");
}

void TestCapsDuplicatesAndEmptyRegistry() {
  Ogre14LegacyMaterialSemanticRegistryConfiguration one_configuration;
  one_configuration.maximum_declarations = 1U;
  Ogre14LegacyMaterialSemanticRegistry registry;
  const auto first = MakeDeclaration("A", "First", 1U);
  const auto second = MakeDeclaration("B", "Second", 1U);
  ValidationResult result = BuildOgre14LegacyMaterialSemanticRegistry(
      one_configuration, {first, second}, registry);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE,
          "declaration count cap+1 was accepted");

  result = BuildOgre14LegacyMaterialSemanticRegistry(
      Ogre14LegacyMaterialSemanticRegistryConfiguration{}, {first, first},
      registry);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate exact declaration key was accepted");

  Ogre14LegacyMaterialSemanticRegistryConfiguration byte_configuration;
  byte_configuration.maximum_total_key_bytes = 1U;
  result = BuildOgre14LegacyMaterialSemanticRegistry(
      byte_configuration, {MakeDeclaration("A", "B", 1U)}, registry);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE,
          "declaration key byte cap+1 was accepted");

  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              Ogre14LegacyMaterialSemanticRegistryConfiguration{}, {},
              registry)
              .ok() &&
              registry.initialized() && registry.size() == 0U &&
              registry.content_fingerprint() != 0U,
          "valid empty explicit registry was not distinguishable from absent");
  Ogre14LegacyMaterialSemanticResolution sentinel = SentinelResolution();
  result = registry.Resolve(first.material_key,
                            Ogre14LegacyAssetTranslatorConfiguration{},
                            sentinel);
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE,
          "empty registry resolved an undeclared material");
  RequireResolutionUnchanged(sentinel,
                             "empty-registry miss mutated resolution");
}

void TestTransactionalRollbackAndOwnerPreservation() {
  Ogre14LegacyMaterialSemanticRegistry registry;
  const auto sentinel_declaration =
      MakeDeclaration("Sentinel", "Material", 3U);
  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              Ogre14LegacyMaterialSemanticRegistryConfiguration{},
              {sentinel_declaration}, registry)
              .ok(),
          "sentinel semantic registry did not build");
  const Ogre14LegacyMaterialSemanticRegistry owner = registry;
  const std::uint64_t fingerprint = registry.content_fingerprint();

  ThrowingFault fault;
  ValidationResult result = BuildOgre14LegacyMaterialSemanticRegistry(
      Ogre14LegacyMaterialSemanticRegistryConfiguration{},
      {MakeDeclaration("New", "First", 1U),
       MakeDeclaration("New", "Second", 1U)},
      registry, &fault);
  Require(!result && result.field == "semantic_registry.allocation" &&
              registry.SharesImmutableStateWith(owner) &&
              registry.content_fingerprint() == fingerprint,
          "bad_alloc changed committed semantic registry or owner");

  fault.stage =
      Ogre14LegacyMaterialSemanticRegistryBuildStage::BEFORE_COMMIT;
  fault.bad_allocation = false;
  result = BuildOgre14LegacyMaterialSemanticRegistry(
      Ogre14LegacyMaterialSemanticRegistryConfiguration{},
      {MakeDeclaration("New", "Only", 1U)}, registry, &fault);
  Require(!result && result.field == "semantic_registry.exception" &&
              registry.SharesImmutableStateWith(owner) &&
              registry.content_fingerprint() == fingerprint,
          "unexpected exception changed semantic registry or owner");

  Ogre14LegacyMaterialSemanticResolution resolution;
  Require(registry.Resolve(sentinel_declaration.material_key,
                           Ogre14LegacyAssetTranslatorConfiguration{},
                           resolution)
              .ok() &&
              resolution.source_revision == 3U,
          "rollback did not preserve reusable immutable declaration state");

  Ogre14LegacyMaterialSemanticResolution sentinel = SentinelResolution();
  Ogre14LegacyAssetTranslatorConfiguration invalid_translator;
  invalid_translator.maximum_live_assets_per_frame = 0U;
  result = registry.Resolve(sentinel_declaration.material_key,
                            invalid_translator, sentinel);
  Require(!result, "invalid translator configuration resolved a declaration");
  RequireResolutionUnchanged(
      sentinel, "invalid translator configuration mutated resolution");
}

} // namespace

int main() {
  static_assert(
      std::is_nothrow_copy_constructible_v<
          Ogre14LegacyMaterialSemanticRegistry> &&
          std::is_nothrow_copy_assignable_v<
              Ogre14LegacyMaterialSemanticRegistry> &&
          std::is_nothrow_move_constructible_v<
              Ogre14LegacyMaterialSemanticRegistry> &&
          std::is_nothrow_move_assignable_v<
              Ogre14LegacyMaterialSemanticRegistry>,
      "immutable semantic registry ownership must move/copy without throwing");
  static_assert(
      std::is_nothrow_move_assignable_v<
          Ogre14LegacyMaterialSemanticResolution>,
      "resolved native declaration publication must not throw");
  TestExactResolutionAndStableOrdering();
  TestConfigurationAndDeclarationValidation();
  TestCapsDuplicatesAndEmptyRegistry();
  TestTransactionalRollbackAndOwnerPreservation();
  std::cout << "OGRE 14 material semantic registry tests passed\n";
  return EXIT_SUCCESS;
}
