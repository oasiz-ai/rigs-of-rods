/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Authenticated, renderer-neutral material compatibility catalog v2.

#pragma once

#include "Ogre14LegacyMaterialSemanticRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyMaterialSemanticCatalogV2Version = 2U;
constexpr std::size_t kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes = 64U;
constexpr std::size_t kOgre14LegacyMaterialSemanticCatalogV2DigestBytes = 32U;
constexpr std::size_t kOgre14LegacyMaterialSemanticCatalogV2MaximumBytes =
    64U * 1024U * 1024U;
constexpr std::size_t kOgre14LegacyMaterialSemanticCatalogV2MaximumRecords =
    65536U;
constexpr std::size_t
    kOgre14LegacyMaterialSemanticCatalogV2MaximumTextureUnitsPerRecord = 32U;
constexpr std::uint64_t
    kOgre14LegacyMaterialSemanticCatalogV2MaximumStringBytes =
        16U * 1024U * 1024U;

using Ogre14LegacySha256 =
    std::array<std::uint8_t,
               kOgre14LegacyMaterialSemanticCatalogV2DigestBytes>;

/// Describes why the selected native material exists in this exact resource
/// generation. This is declared data, never inferred from names or pass state.
enum class Ogre14LegacyMaterialRuntimeGeneration : std::uint8_t {
  AUTHORED = 0U,
  REPAIRED_SCRIPT = 1U,
  RTSS_GENERATED = 2U,
  RUNTIME_LISTENER = 3U,
};

enum class Ogre14LegacyTextureSemantic : std::uint8_t {
  BASE_COLOR = 0U,
  NORMAL = 1U,
  METALLIC_ROUGHNESS = 2U,
  OCCLUSION = 3U,
  EMISSIVE = 4U,
  ENVIRONMENT = 5U,
  DETAIL = 6U,
};

enum class Ogre14LegacyTextureSwizzle : std::uint8_t {
  RED = 0U,
  GREEN = 1U,
  BLUE = 2U,
  ALPHA = 3U,
  ZERO = 4U,
  ONE = 5U,
};

enum class Ogre14LegacyTextureCombineOperation : std::uint8_t {
  REPLACE = 0U,
  MODULATE = 1U,
  ADD = 2U,
  SUBTRACT = 3U,
  BLEND_TEXTURE_ALPHA = 4U,
  DOT_PRODUCT = 5U,
};

enum class Ogre14LegacyTextureCombineSource : std::uint8_t {
  TEXTURE = 0U,
  CURRENT = 1U,
  DIFFUSE = 2U,
  SPECULAR = 3U,
  MANUAL = 4U,
};

enum class Ogre14LegacyEnvironmentAugmentation : std::uint8_t {
  NONE = 0U,
  REFLECTION_2D = 1U,
  CUBE_REFLECTION = 2U,
  CUBE_NORMAL = 3U,
};

enum class Ogre14LegacyShadowAugmentation : std::uint8_t {
  NONE = 0U,
  RECEIVE = 1U,
  CAST = 2U,
  RECEIVE_AND_CAST = 3U,
};

enum class Ogre14LegacyShadowTechnique : std::uint8_t {
  NONE = 0U,
  STENCIL = 1U,
  PSSM = 2U,
};

/// IEEE-754 float32 bit patterns are used instead of host floats in the
/// compatibility file. This keeps compilation and parsing byte-exact on all
/// supported hosts.
struct Ogre14LegacyCatalogSamplerFacts final {
  Ogre14LegacyFilter minification = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyFilter magnification = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyFilter mip = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyAddressMode address_u = Ogre14LegacyAddressMode::WRAP;
  Ogre14LegacyAddressMode address_v = Ogre14LegacyAddressMode::WRAP;
  Ogre14LegacyAddressMode address_w = Ogre14LegacyAddressMode::WRAP;
  std::uint32_t mip_lod_bias_f32_bits = 0U;
  std::uint32_t minimum_lod_f32_bits = 0U;
  std::uint32_t maximum_lod_f32_bits = 0U;
  std::uint32_t maximum_anisotropy = 1U;
  bool compare_enabled = false;
  Ogre14LegacyCompareOperation compare_operation =
      Ogre14LegacyCompareOperation::ALWAYS_PASS;
  std::array<std::uint32_t, 4U> border_color_f32_bits{};
};

struct Ogre14LegacyCatalogCombineFacts final {
  Ogre14LegacyTextureCombineOperation color_operation =
      Ogre14LegacyTextureCombineOperation::MODULATE;
  Ogre14LegacyTextureCombineSource color_source_one =
      Ogre14LegacyTextureCombineSource::TEXTURE;
  Ogre14LegacyTextureCombineSource color_source_two =
      Ogre14LegacyTextureCombineSource::CURRENT;
  Ogre14LegacyTextureCombineOperation alpha_operation =
      Ogre14LegacyTextureCombineOperation::MODULATE;
  Ogre14LegacyTextureCombineSource alpha_source_one =
      Ogre14LegacyTextureCombineSource::TEXTURE;
  Ogre14LegacyTextureCombineSource alpha_source_two =
      Ogre14LegacyTextureCombineSource::CURRENT;
  std::array<std::uint32_t, 4U> color_manual_one_f32_bits{};
  std::array<std::uint32_t, 4U> color_manual_two_f32_bits{};
  std::uint32_t color_manual_factor_f32_bits = 0U;
  std::uint32_t alpha_manual_one_f32_bits = 0U;
  std::uint32_t alpha_manual_two_f32_bits = 0U;
  std::uint32_t alpha_manual_factor_f32_bits = 0U;
};

struct Ogre14LegacyCatalogTextureUnitFacts final {
  std::uint16_t ordinal = 0U;
  std::string exact_unit_name;
  Ogre14LegacyAssetKey texture_key;
  Ogre14LegacyTextureSemantic semantic =
      Ogre14LegacyTextureSemantic::BASE_COLOR;
  Ogre14LegacyTextureColorRole color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  std::array<Ogre14LegacyTextureSwizzle, 4U> swizzle{
      Ogre14LegacyTextureSwizzle::RED, Ogre14LegacyTextureSwizzle::GREEN,
      Ogre14LegacyTextureSwizzle::BLUE, Ogre14LegacyTextureSwizzle::ALPHA};
  std::uint8_t texture_coordinate_set = 0U;
  bool projective = false;
  std::array<std::uint32_t, 9U> uv_transform_f32_bits{};
  Ogre14LegacyCatalogSamplerFacts sampler;
  Ogre14LegacyCatalogCombineFacts combine;
};

struct Ogre14LegacyCatalogPassFacts final {
  Ogre14LegacyBlendFactor source_color = Ogre14LegacyBlendFactor::ONE;
  Ogre14LegacyBlendFactor destination_color = Ogre14LegacyBlendFactor::ZERO;
  Ogre14LegacyBlendFactor source_alpha = Ogre14LegacyBlendFactor::ONE;
  Ogre14LegacyBlendFactor destination_alpha = Ogre14LegacyBlendFactor::ZERO;
  Ogre14LegacyBlendOperation color_operation =
      Ogre14LegacyBlendOperation::ADD;
  Ogre14LegacyBlendOperation alpha_operation =
      Ogre14LegacyBlendOperation::ADD;
  std::uint8_t color_write_mask = 0x0FU;
  bool depth_check_enabled = true;
  bool depth_write_enabled = true;
  Ogre14LegacyCompareOperation depth_compare =
      Ogre14LegacyCompareOperation::LESS_EQUAL;
  std::uint32_t constant_depth_bias_f32_bits = 0U;
  std::uint32_t slope_scale_depth_bias_f32_bits = 0U;
  std::uint32_t iteration_depth_bias_f32_bits = 0U;
  Ogre14LegacyCullMode cull = Ogre14LegacyCullMode::CLOCKWISE;
  Ogre14LegacyManualCullMode manual_cull = Ogre14LegacyManualCullMode::BACK;
  Ogre14LegacyCompareOperation alpha_reject =
      Ogre14LegacyCompareOperation::ALWAYS_PASS;
  std::uint8_t alpha_reject_value = 0U;
  bool alpha_to_coverage = false;
  bool solid_fill = true;
  std::uint32_t pass_iteration_count = 1U;
};

struct Ogre14LegacyMaterialSemanticCatalogV2Record final {
  Ogre14LegacySha256 package_archive_sha256{};
  Ogre14LegacyAssetKey material_key;
  std::uint64_t resource_generation = 0U;
  std::string exact_source_script_member;
  Ogre14LegacySha256 source_script_sha256{};
  Ogre14LegacySha256 effective_script_sha256{};
  std::uint32_t repair_plan_version = 0U;
  Ogre14LegacySha256 native_structure_sha256{};
  std::string selected_scheme;
  std::uint32_t selected_lod = 0U;
  Ogre14LegacyMaterialRuntimeGeneration runtime_generation =
      Ogre14LegacyMaterialRuntimeGeneration::AUTHORED;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  Ogre14LegacyTextureColorRole registry_texture_color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  std::string exact_lowering_algorithm;
  std::uint32_t lowering_version = 0U;
  std::uint64_t declaration_revision = 0U;
  Ogre14LegacyCatalogPassFacts pass;
  Ogre14LegacyEnvironmentAugmentation environment_augmentation =
      Ogre14LegacyEnvironmentAugmentation::NONE;
  std::uint16_t environment_texture_unit = 0xFFFFU;
  Ogre14LegacyShadowAugmentation shadow_augmentation =
      Ogre14LegacyShadowAugmentation::NONE;
  Ogre14LegacyShadowTechnique shadow_technique =
      Ogre14LegacyShadowTechnique::NONE;
  std::vector<Ogre14LegacyCatalogTextureUnitFacts> texture_units;
};

struct Ogre14LegacyMaterialSemanticCatalogV2Configuration final {
  std::uint32_t version = kOgre14LegacyMaterialSemanticCatalogV2Version;
  std::size_t maximum_catalog_bytes =
      kOgre14LegacyMaterialSemanticCatalogV2MaximumBytes;
  std::size_t maximum_records =
      kOgre14LegacyMaterialSemanticCatalogV2MaximumRecords;
  std::size_t maximum_texture_units_per_record =
      kOgre14LegacyMaterialSemanticCatalogV2MaximumTextureUnitsPerRecord;
  std::uint64_t maximum_total_string_bytes =
      kOgre14LegacyMaterialSemanticCatalogV2MaximumStringBytes;
};

enum class Ogre14LegacyMaterialSemanticCatalogV2ParseStage : std::uint8_t {
  AFTER_HEADER = 0U,
  AFTER_FIRST_RECORD = 1U,
  BEFORE_COMMIT = 2U,
};

class IOgre14LegacyMaterialSemanticCatalogV2FaultInjector {
public:
  virtual ~IOgre14LegacyMaterialSemanticCatalogV2FaultInjector() = default;
  virtual void BeforeCatalogParseStage(
      Ogre14LegacyMaterialSemanticCatalogV2ParseStage) {}
};

class Ogre14LegacyMaterialSemanticCatalogV2 final {
public:
  Ogre14LegacyMaterialSemanticCatalogV2() = default;
  ~Ogre14LegacyMaterialSemanticCatalogV2() = default;
  Ogre14LegacyMaterialSemanticCatalogV2(
      const Ogre14LegacyMaterialSemanticCatalogV2 &) noexcept = default;
  Ogre14LegacyMaterialSemanticCatalogV2 &operator=(
      const Ogre14LegacyMaterialSemanticCatalogV2 &) noexcept = default;
  Ogre14LegacyMaterialSemanticCatalogV2(
      Ogre14LegacyMaterialSemanticCatalogV2 &&) noexcept = default;
  Ogre14LegacyMaterialSemanticCatalogV2 &operator=(
      Ogre14LegacyMaterialSemanticCatalogV2 &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const Ogre14LegacySha256 &content_sha256() const noexcept;
  [[nodiscard]] const Ogre14LegacyMaterialSemanticCatalogV2Record *FindExact(
      const Ogre14LegacyAssetKey &material_key) const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyMaterialSemanticCatalogV2 &other) const noexcept;

private:
  struct State;
  explicit Ogre14LegacyMaterialSemanticCatalogV2(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend ValidationResult ParseOgre14LegacyMaterialSemanticCatalogV2(
      const Ogre14LegacyMaterialSemanticCatalogV2Configuration &,
      const std::vector<std::uint8_t> &,
      Ogre14LegacyMaterialSemanticCatalogV2 &,
      IOgre14LegacyMaterialSemanticCatalogV2FaultInjector *);
  friend ValidationResult BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
      const Ogre14LegacyMaterialSemanticCatalogV2 &,
      const Ogre14LegacyMaterialSemanticRegistryConfiguration &,
      Ogre14LegacyMaterialSemanticRegistry &,
      IOgre14LegacyMaterialSemanticRegistryFaultInjector *);
};

[[nodiscard]] ValidationResult
ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(
    const Ogre14LegacyMaterialSemanticCatalogV2Configuration &configuration);

/// Parses the fixed little-endian `RORMAT2` format. The parser has no JSON or
/// RapidJSON dependency. Authentication, canonical ordering, every bound, and
/// exact payload consumption are checked before the immutable owner is swapped.
/// Every failure, including allocation and unexpected exceptions, leaves
/// `output` byte-for-byte and owner-identical to its previous value.
[[nodiscard]] ValidationResult ParseOgre14LegacyMaterialSemanticCatalogV2(
    const Ogre14LegacyMaterialSemanticCatalogV2Configuration &configuration,
    const std::vector<std::uint8_t> &bytes,
    Ogre14LegacyMaterialSemanticCatalogV2 &output,
    IOgre14LegacyMaterialSemanticCatalogV2FaultInjector *fault_injector =
        nullptr);

/// Converts only the catalog's explicit lowering result into the existing
/// exact-key registry. No filename, unit-position, lighting, or specular
/// heuristic participates. Registry construction remains transactional.
[[nodiscard]] ValidationResult
BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
    const Ogre14LegacyMaterialSemanticCatalogV2 &catalog,
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration,
    Ogre14LegacyMaterialSemanticRegistry &output,
    IOgre14LegacyMaterialSemanticRegistryFaultInjector *fault_injector =
        nullptr);

} // namespace RoR::Render
