/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact, fail-closed OGRE 14 legacy asset translation contract.

#pragma once

#include "RenderAssetRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyAssetTranslatorVersion = 1U;
constexpr std::uint32_t kOgre14LegacyTextureInputVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialInputVersion = 1U;
constexpr std::uint32_t kOgre14LegacyPipelineAuditVersion = 1U;
constexpr std::uint32_t kOgre14LegacyTranslatedFrameVersion = 1U;
constexpr std::uint32_t kOgre14LegacyAssetTranslatorConfigurationVersion = 1U;

/// Defaults match the joined graphics producer's lifetime-record and payload
/// budgets so this earlier decode/catalog stage cannot consume more resources
/// than the transaction which will ultimately publish it.
constexpr std::size_t kDefaultOgre14LegacyMaximumTextureInputsPerFrame =
    65536U;
constexpr std::size_t kDefaultOgre14LegacyMaximumMaterialInputsPerFrame =
    65536U;
constexpr std::size_t kDefaultOgre14LegacyMaximumLiveAssetsPerFrame = 65536U;
constexpr std::size_t kDefaultOgre14LegacyMaximumLifetimeAssetRecords =
    65536U;
constexpr std::uint64_t kDefaultOgre14LegacyMaximumDecodedBytesPerAsset =
    512U * 1024U * 1024U;
constexpr std::uint64_t kDefaultOgre14LegacyMaximumDecodedBytesPerFrame =
    512U * 1024U * 1024U;

struct Ogre14LegacyAssetTranslatorConfiguration {
  std::uint32_t version =
      kOgre14LegacyAssetTranslatorConfigurationVersion;
  std::size_t maximum_texture_inputs_per_frame =
      kDefaultOgre14LegacyMaximumTextureInputsPerFrame;
  std::size_t maximum_material_inputs_per_frame =
      kDefaultOgre14LegacyMaximumMaterialInputsPerFrame;
  /// Textures + materials + material-owned samplers in one authoritative frame.
  std::size_t maximum_live_assets_per_frame =
      kDefaultOgre14LegacyMaximumLiveAssetsPerFrame;
  /// Includes permanent tombstones for the complete translator lifetime.
  std::size_t maximum_lifetime_asset_records =
      kDefaultOgre14LegacyMaximumLifetimeAssetRecords;
  std::uint64_t maximum_decoded_bytes_per_asset =
      kDefaultOgre14LegacyMaximumDecodedBytesPerAsset;
  std::uint64_t maximum_decoded_bytes_per_frame =
      kDefaultOgre14LegacyMaximumDecodedBytesPerFrame;
};

/// Byte layouts are explicit and independent of the compiling host. The two
/// packed-word encodings describe an A8R8G8B8 integer serialized in the named
/// byte order; they exist so captures never reinterpret native words.
enum class Ogre14LegacyPixelEncoding : std::uint8_t {
  RGB8_BYTES = 0U,
  BGR8_BYTES = 1U,
  RGBA8_BYTES = 2U,
  BGRA8_BYTES = 3U,
  ARGB8_BYTES = 4U,
  ABGR8_BYTES = 5U,
  A8R8G8B8_WORD_LITTLE_ENDIAN = 6U,
  A8R8G8B8_WORD_BIG_ENDIAN = 7U,
};

enum class Ogre14LegacyTextureType : std::uint8_t {
  TEXTURE_1D = 0U,
  TEXTURE_2D = 1U,
  TEXTURE_3D = 2U,
  TEXTURE_CUBE = 3U,
  TEXTURE_2D_ARRAY = 4U,
  TEXTURE_2D_MULTISAMPLE = 5U,
  TEXTURE_EXTERNAL = 6U,
};

/// Color interpretation is authored input, never inferred from a filename.
/// BASE_COLOR_SRGB stores the original encoded bytes and marks the portable
/// texture sRGB exactly once. LINEAR_DATA is retained for future roles but may
/// not bind a v1 base-color material.
enum class Ogre14LegacyTextureColorRole : std::uint8_t {
  BASE_COLOR_SRGB = 0U,
  LINEAR_DATA = 1U,
};

struct Ogre14LegacyAssetKey {
  std::string exact_resource_group;
  std::string exact_name;

  friend bool operator==(const Ogre14LegacyAssetKey &lhs,
                         const Ogre14LegacyAssetKey &rhs) noexcept {
    return lhs.exact_resource_group == rhs.exact_resource_group &&
           lhs.exact_name == rhs.exact_name;
  }
  friend bool operator!=(const Ogre14LegacyAssetKey &lhs,
                         const Ogre14LegacyAssetKey &rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct Ogre14LegacyTextureMipInput {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  /// Source pitches are bytes, including any explicit row/slice padding.
  std::uint64_t row_pitch_bytes = 0U;
  std::uint64_t slice_pitch_bytes = 0U;
  std::vector<std::uint8_t> bytes;
};

struct Ogre14LegacyTextureInput {
  std::uint32_t version = kOgre14LegacyTextureInputVersion;
  Ogre14LegacyAssetKey key;
  std::uint64_t source_revision = 0U;
  Ogre14LegacyTextureType type = Ogre14LegacyTextureType::TEXTURE_2D;
  Ogre14LegacyPixelEncoding pixel_encoding =
      Ogre14LegacyPixelEncoding::RGBA8_BYTES;
  Ogre14LegacyTextureColorRole color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  /// Must agree with color_role. No gamma conversion is performed on bytes.
  bool hardware_gamma_enabled = true;
  bool compressed = false;
  bool render_target = false;
  bool generated = false;
  bool procedural = false;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::vector<Ogre14LegacyTextureMipInput> mip_levels;
};

enum class Ogre14LegacyFilter : std::uint8_t {
  NONE = 0U,
  POINT = 1U,
  LINEAR = 2U,
  ANISOTROPIC = 3U,
};

enum class Ogre14LegacyAddressMode : std::uint8_t {
  WRAP = 0U,
  MIRROR = 1U,
  CLAMP = 2U,
  BORDER = 3U,
};

enum class Ogre14LegacyCompareOperation : std::uint8_t {
  ALWAYS_FAIL = 0U,
  ALWAYS_PASS = 1U,
  LESS = 2U,
  LESS_EQUAL = 3U,
  EQUAL = 4U,
  NOT_EQUAL = 5U,
  GREATER_EQUAL = 6U,
  GREATER = 7U,
};

struct Ogre14LegacySamplerInput {
  std::uint64_t source_revision = 0U;
  Ogre14LegacyFilter minification = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyFilter magnification = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyFilter mip = Ogre14LegacyFilter::LINEAR;
  Ogre14LegacyAddressMode address_u = Ogre14LegacyAddressMode::WRAP;
  Ogre14LegacyAddressMode address_v = Ogre14LegacyAddressMode::WRAP;
  Ogre14LegacyAddressMode address_w = Ogre14LegacyAddressMode::WRAP;
  float mip_lod_bias = 0.0F;
  /// Exact effective LOD bounds after OGRE resource-level clamping.
  float minimum_lod = 0.0F;
  float maximum_lod = 0.0F;
  std::uint32_t maximum_anisotropy = 1U;
  bool compare_enabled = false;
  Ogre14LegacyCompareOperation compare_operation =
      Ogre14LegacyCompareOperation::ALWAYS_PASS;
  Float4 border_color{};
};

enum class Ogre14LegacyBaseColorSemantic : std::uint8_t {
  /// The legacy pass has lighting disabled and is reproduced as unlit.
  UNLIT = 0U,
  /// An authoring declaration explicitly elects rough dielectric PBR. This is
  /// never inferred from legacy specular/shininess values.
  ROUGH_DIELECTRIC_PBR = 1U,
};

enum class Ogre14LegacyBlendFactor : std::uint8_t {
  ONE = 0U,
  ZERO = 1U,
  DESTINATION_COLOR = 2U,
  SOURCE_COLOR = 3U,
  ONE_MINUS_DESTINATION_COLOR = 4U,
  ONE_MINUS_SOURCE_COLOR = 5U,
  DESTINATION_ALPHA = 6U,
  SOURCE_ALPHA = 7U,
  ONE_MINUS_DESTINATION_ALPHA = 8U,
  ONE_MINUS_SOURCE_ALPHA = 9U,
};

enum class Ogre14LegacyBlendOperation : std::uint8_t {
  ADD = 0U,
  SUBTRACT = 1U,
  REVERSE_SUBTRACT = 2U,
  MINIMUM = 3U,
  MAXIMUM = 4U,
};

enum class Ogre14LegacyCullMode : std::uint8_t {
  NONE = 0U,
  CLOCKWISE = 1U,
  ANTICLOCKWISE = 2U,
};

enum class Ogre14LegacyManualCullMode : std::uint8_t {
  NONE = 0U,
  BACK = 1U,
  FRONT = 2U,
};

struct Ogre14LegacyPipelineStateInput {
  Ogre14LegacyBlendFactor source_color = Ogre14LegacyBlendFactor::ONE;
  Ogre14LegacyBlendFactor destination_color = Ogre14LegacyBlendFactor::ZERO;
  Ogre14LegacyBlendFactor source_alpha = Ogre14LegacyBlendFactor::ONE;
  Ogre14LegacyBlendFactor destination_alpha = Ogre14LegacyBlendFactor::ZERO;
  Ogre14LegacyBlendOperation color_operation = Ogre14LegacyBlendOperation::ADD;
  Ogre14LegacyBlendOperation alpha_operation = Ogre14LegacyBlendOperation::ADD;
  std::uint8_t color_write_mask = 0x0FU;
  bool depth_check_enabled = true;
  bool depth_write_enabled = true;
  Ogre14LegacyCompareOperation depth_compare =
      Ogre14LegacyCompareOperation::LESS_EQUAL;
  float constant_depth_bias = 0.0F;
  float slope_scale_depth_bias = 0.0F;
  float iteration_depth_bias = 0.0F;
  Ogre14LegacyCullMode cull = Ogre14LegacyCullMode::CLOCKWISE;
  Ogre14LegacyManualCullMode manual_cull = Ogre14LegacyManualCullMode::BACK;
  Ogre14LegacyCompareOperation alpha_reject =
      Ogre14LegacyCompareOperation::ALWAYS_PASS;
  std::uint8_t alpha_reject_value = 0U;
  bool alpha_to_coverage = false;
  bool solid_fill = true;
  std::uint32_t pass_iteration_count = 1U;
};

struct Ogre14LegacyTextureUnitInput {
  Ogre14LegacyAssetKey texture_key;
  Ogre14LegacySamplerInput sampler;
  std::uint8_t texture_coordinate_set = 0U;
  bool named_content = true;
  bool texture_2d = true;
  std::uint32_t frame_count = 1U;
  bool has_animated_or_procedural_effect = false;
  bool projective = false;
  bool environment_mapping = false;
  bool compositor = false;
  bool render_target = false;
  /// v1 accepts only OGRE's default texture*current color and alpha combine.
  bool canonical_color_modulate = true;
  bool canonical_alpha_modulate = true;
  /// v1 accepts only the identity UV matrix. The explicit flag is produced by
  /// the native extractor after exact matrix comparison.
  bool identity_texture_transform = true;
};

struct Ogre14LegacyMaterialInput {
  std::uint32_t version = kOgre14LegacyMaterialInputVersion;
  Ogre14LegacyAssetKey key;
  std::uint64_t source_revision = 0U;
  std::uint32_t technique_count = 1U;
  std::uint32_t pass_count = 1U;
  bool generated_rtss_program = false;
  bool has_vertex_program = false;
  bool has_fragment_program = false;
  bool has_geometry_program = false;
  bool has_tessellation_program = false;
  bool has_compute_program = false;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  bool lighting_enabled = false;
  Float4 diffuse_linear{1.0F, 1.0F, 1.0F, 1.0F};
  /// Non-base-color fixed-function lobes must be absent; v1 never guesses a
  /// metallic, roughness, normal, occlusion, or emissive role from them.
  Float3 ambient_linear{};
  Float3 specular_linear{};
  Float3 emissive_linear{};
  float shininess = 0.0F;
  Ogre14LegacyPipelineStateInput pipeline;
  /// Empty or exactly one texture unit.
  std::vector<Ogre14LegacyTextureUnitInput> texture_units;
};

struct Ogre14LegacyAssetFrameInput {
  std::uint32_t version = kOgre14LegacyAssetTranslatorVersion;
  /// Starts at one and advances after each successful capture. A failed call
  /// may be retried with the same sequence because it commits no state.
  std::uint64_t source_sequence = 0U;
  std::vector<Ogre14LegacyTextureInput> textures;
  std::vector<Ogre14LegacyMaterialInput> materials;
};

/// Immutable exact source-state companion for every translated material. The
/// portable descriptor is valid independently; a later OgreNext inventory
/// adapter must consume this companion to prove the admitted pipeline state
/// and apply reverse_winding before it may publish a static/terrain section.
struct Ogre14LegacyMaterialPipelineAudit {
  std::uint32_t version = kOgre14LegacyPipelineAuditVersion;
  Ogre14LegacyPipelineStateInput pipeline;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  bool requires_reverse_winding = false;
  std::uint64_t texture_source_asset_id = 0U;
  std::uint64_t sampler_source_asset_id = 0U;
};

struct Ogre14LegacyTranslatedAsset {
  RenderAssetKind kind = RenderAssetKind::INVALID;
  std::uint64_t source_asset_id = 0U;
  std::uint64_t source_revision = 0U;
  std::uint64_t translated_revision = 0U;
  std::string stable_key;
  std::shared_ptr<const RenderAssetPayload> payload;
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> material_audit;
};

enum class Ogre14LegacyAssetMutationType : std::uint8_t {
  UPSERT = 0U,
  DESTROY = 1U,
};

struct Ogre14LegacyAssetMutation {
  Ogre14LegacyAssetMutationType type = Ogre14LegacyAssetMutationType::UPSERT;
  RenderAssetKind kind = RenderAssetKind::INVALID;
  std::uint64_t source_asset_id = 0U;
  std::uint64_t translated_revision = 0U;
  std::string stable_key;
  std::shared_ptr<const RenderAssetPayload> payload;
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> material_audit;
};

/// `live_assets` is a complete frame-owned inventory in dependency order:
/// textures, samplers, then materials. Incremental UPSERTs use the same order;
/// DESTROYs use material, sampler, texture order. Every owner is immutable.
struct Ogre14LegacyTranslatedFrame {
  std::uint32_t version = kOgre14LegacyTranslatedFrameVersion;
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  bool full_snapshot = false;
  std::vector<Ogre14LegacyTranslatedAsset> live_assets;
  std::vector<Ogre14LegacyAssetMutation> mutations;
};

class IOgre14LegacyAssetTranslatorFaultInjector {
public:
  virtual ~IOgre14LegacyAssetTranslatorFaultInjector() = default;
  /// Called after the entire candidate catalog and output frame validate but
  /// before commit. Tests return a failure to prove atomic rollback.
  [[nodiscard]] virtual ValidationResult BeforeCommit() noexcept = 0;
};

[[nodiscard]] ValidationResult
ValidateOgre14LegacyTextureInput(const Ogre14LegacyTextureInput &input);
[[nodiscard]] ValidationResult
ValidateOgre14LegacyMaterialInput(const Ogre14LegacyMaterialInput &input);
[[nodiscard]] ValidationResult ValidateOgre14LegacyAssetTranslatorConfiguration(
    const Ogre14LegacyAssetTranslatorConfiguration &configuration);
[[nodiscard]] ValidationResult
DeriveOgre14LegacySourceAssetId(RenderAssetKind kind,
                                const Ogre14LegacyAssetKey &key,
                                std::uint64_t &source_asset_id);
[[nodiscard]] ValidationResult
DecodeOgre14LegacyTexture(const Ogre14LegacyTextureInput &input,
                          TextureResourceDescriptor &descriptor,
                          std::uint64_t maximum_decoded_bytes =
                              kDefaultOgre14LegacyMaximumDecodedBytesPerAsset);

class Ogre14LegacyAssetTranslator final {
public:
  explicit Ogre14LegacyAssetTranslator(
      IOgre14LegacyAssetTranslatorFaultInjector *fault_injector = nullptr);
  explicit Ogre14LegacyAssetTranslator(
      const Ogre14LegacyAssetTranslatorConfiguration &configuration,
      IOgre14LegacyAssetTranslatorFaultInjector *fault_injector = nullptr);
  ~Ogre14LegacyAssetTranslator();

  Ogre14LegacyAssetTranslator(const Ogre14LegacyAssetTranslator &) = delete;
  Ogre14LegacyAssetTranslator &
  operator=(const Ogre14LegacyAssetTranslator &) = delete;
  Ogre14LegacyAssetTranslator(Ogre14LegacyAssetTranslator &&) noexcept;
  Ogre14LegacyAssetTranslator &
  operator=(Ogre14LegacyAssetTranslator &&) noexcept;

  [[nodiscard]] std::uint64_t source_sequence() const noexcept;
  [[nodiscard]] std::uint64_t catalog_sequence() const noexcept;

  /// Fully transactional. Any validation, allocation, collision, dependency,
  /// source-lineage, or injected failure leaves state and `output` untouched.
  [[nodiscard]] ValidationResult
  Translate(const Ogre14LegacyAssetFrameInput &input,
            Ogre14LegacyTranslatedFrame &output);
  [[nodiscard]] ValidationResult
  BuildFullSnapshot(Ogre14LegacyTranslatedFrame &output) const;

private:
  struct State;
  std::unique_ptr<State> state_;
  IOgre14LegacyAssetTranslatorFaultInjector *fault_injector_ = nullptr;
};

} // namespace RoR::Render
