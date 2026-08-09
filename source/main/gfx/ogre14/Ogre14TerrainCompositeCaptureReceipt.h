/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Lossless OGRE 14 terrain-composite transport receipt V2.

#pragma once

#include "gfx/render/RenderResourceDescriptors.h"
#include "gfx/render/RenderValidation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ogre {
class TerrainGroup;
}

namespace RoR::Render {

constexpr std::uint32_t kOgre14TerrainCompositeCaptureReceiptVersion = 2U;
constexpr std::uint32_t kOgre14TerrainCompositeCaptureConfigurationVersion = 2U;
constexpr std::uint32_t kOgre14TerrainCompositeNativeObservationVersion = 2U;
constexpr std::uint32_t kOgre14TerrainCompositeSemanticContractVersion = 2U;

/// These NUL-terminated ASCII labels are part of the V2 wire-independent
/// digest contract. The terminating NUL is hashed to keep each domain exact.
constexpr char kOgre14TerrainCompositeMipDigestDomain[] =
    "RoR/Ogre14/TerrainComposite/MipRGBA/v2";
constexpr char kOgre14TerrainCompositeMipChainDigestDomain[] =
    "RoR/Ogre14/TerrainComposite/FullMipChain/v2";

constexpr std::uint32_t kOgre14TerrainCompositeDefaultMaximumDimension = 8192U;
constexpr std::uint64_t kOgre14TerrainCompositeDefaultMaximumRgbaBytes =
    384ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14TerrainCompositeDefaultMaximumIdentifierBytes =
    16384U;
constexpr std::uint32_t kOgre14TerrainCompositeDefaultMaximumMipLevels = 32U;
constexpr std::uint32_t kOgre14TerrainCompositeHardMaximumDimension = 16384U;
constexpr std::uint64_t kOgre14TerrainCompositeHardMaximumRgbaBytes =
    1536ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14TerrainCompositeHardMaximumIdentifierBytes =
    65536U;
constexpr std::uint32_t kOgre14TerrainCompositeHardMaximumMipLevels = 32U;

struct Ogre14TerrainCompositeCaptureConfiguration final {
  std::uint32_t version = kOgre14TerrainCompositeCaptureConfigurationVersion;
  std::uint32_t maximum_dimension =
      kOgre14TerrainCompositeDefaultMaximumDimension;
  /// Aggregate cap for every tightly packed RGBA mip, not merely level zero.
  std::uint64_t maximum_rgba_bytes =
      kOgre14TerrainCompositeDefaultMaximumRgbaBytes;
  std::size_t maximum_identifier_bytes =
      kOgre14TerrainCompositeDefaultMaximumIdentifierBytes;
  std::uint32_t maximum_mip_levels =
      kOgre14TerrainCompositeDefaultMaximumMipLevels;
};

enum class Ogre14TerrainCompositeAlignment : std::uint8_t {
  X_Z = 0U,
  X_Y = 1U,
  Y_Z = 2U,
};

enum class Ogre14TerrainCompositePageDefinitionKind : std::uint8_t {
  FILE_BACKED = 0U,
  LIVE_IMPORT = 1U,
  CONSUMED_OR_RUNTIME = 2U,
};

enum class Ogre14TerrainCompositePixelEncoding : std::uint8_t {
  BYTE_RGBA = 0U,
};

enum class Ogre14TerrainCompositeTextureType : std::uint8_t {
  TEXTURE_2D = 0U,
};

enum class Ogre14TerrainCompositeTextureLoadingState : std::uint8_t {
  LOADED = 0U,
};

enum class Ogre14TerrainCompositeRowOrder : std::uint8_t {
  OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP = 0U,
};

enum class Ogre14TerrainCompositeChannelOrder : std::uint8_t {
  RED_GREEN_BLUE_ALPHA = 0U,
};

/// RGB is already the baked terrain diffuse result; it is not an albedo layer
/// that may be lit again by the receiving renderer.
enum class Ogre14TerrainCompositeRgbSemantic : std::uint8_t {
  BAKED_DIFFUSE = 0U,
};

/// Alpha is a linear scalar and must never be treated as coverage, opacity,
/// metallic, roughness, or another PBR channel.
enum class Ogre14TerrainCompositeAlphaSemantic : std::uint8_t {
  LINEAR_SPECULAR_MASK = 0U,
};

/// When hardware gamma is enabled, the sampler decodes RGB before spatial
/// filtering while alpha stays linear. The legacy path filters stored UNORM
/// RGB directly in its display-domain encoding.
enum class Ogre14TerrainCompositeRgbTransfer : std::uint8_t {
  DECODE_BEFORE_FILTER = 0U,
  LEGACY_UNORM_DISPLAY_DOMAIN = 1U,
};

enum class Ogre14TerrainCompositeAddressMode : std::uint8_t {
  CLAMP = 0U,
};

enum class Ogre14TerrainCompositeFilter : std::uint8_t {
  POINT = 0U,
  LINEAR = 1U,
};

/// Direct SceneManager::getFogMode() observation. Capture transports every
/// pinned OGRE fog mode; opaque target lowering admits FOG_NONE only.
enum class Ogre14TerrainCompositeSceneFogMode : std::uint8_t {
  FOG_NONE = 0U,
  FOG_EXP = 1U,
  FOG_EXP2 = 2U,
  FOG_LINEAR = 3U,
};

enum class Ogre14TerrainCompositeCompareFunction : std::uint8_t {
  GREATER_EQUAL = 0U,
};

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
enum class Ogre14TerrainCompositeCaptureStage : std::uint8_t {
  AFTER_NATIVE_IDENTITY_CAPTURE = 0U,
  AFTER_RGBA_ALLOCATION = 1U,
  AFTER_NATIVE_READBACK = 2U,
  BEFORE_RECEIPT_PUBLICATION = 3U,
};

class IOgre14TerrainCompositeCaptureFaultInjector {
public:
  virtual ~IOgre14TerrainCompositeCaptureFaultInjector() = default;
  virtual void
  BeforeTerrainCompositeCaptureStage(Ogre14TerrainCompositeCaptureStage) {}
};
#endif

struct Ogre14TerrainCompositeNativeMipObservation final {
  std::uint32_t mip_level = 0U;
  std::uintptr_t pixel_buffer_pointer_token = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t depth = 0U;
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
};

/// Transport-only texture-unit, UV, sampler, gamma, and direct scene-fog
/// facts observed through pinned public OGRE APIs. This is not an attestation
/// of a Pass, RTShader graph, generated program, lighting, or shadow response.
/// Pointer tokens are identity-only and are revalidated around readback.
struct Ogre14TerrainCompositeSamplingObservation final {
  std::uintptr_t scene_manager_pointer_token = 0U;
  std::uintptr_t texture_unit_pointer_token = 0U;
  std::uintptr_t sampler_pointer_token = 0U;
  std::uintptr_t bound_texture_pointer_token = 0U;
  bool texture_unit_content_named = false;
  std::uint32_t texture_unit_frame_count = 0U;
  std::uint32_t texture_unit_current_frame = 0U;
  bool texture_unit_texture_2d = false;
  bool texture_unit_is_blank = true;
  bool texture_unit_load_failing = true;
  std::int32_t unordered_access_mip_level = 0;
  std::uint32_t texture_coord_set = 0U;
  bool texcoord_calculation_none = false;
  std::uint32_t texture_effect_count = 0U;
  float texture_u_scroll = 0.0F;
  float texture_v_scroll = 0.0F;
  float texture_u_scale = 1.0F;
  float texture_v_scale = 1.0F;
  float texture_rotation_radians = 0.0F;
  std::array<float, 16U> texture_transform{};
  Ogre14TerrainCompositeAddressMode address_u =
      Ogre14TerrainCompositeAddressMode::CLAMP;
  Ogre14TerrainCompositeAddressMode address_v =
      Ogre14TerrainCompositeAddressMode::CLAMP;
  Ogre14TerrainCompositeAddressMode address_w =
      Ogre14TerrainCompositeAddressMode::CLAMP;
  Ogre14TerrainCompositeFilter min_filter =
      Ogre14TerrainCompositeFilter::LINEAR;
  Ogre14TerrainCompositeFilter mag_filter =
      Ogre14TerrainCompositeFilter::LINEAR;
  Ogre14TerrainCompositeFilter mip_filter = Ogre14TerrainCompositeFilter::POINT;
  std::uint32_t maximum_anisotropy = 1U;
  float mipmap_bias = 0.0F;
  bool compare_enabled = false;
  Ogre14TerrainCompositeCompareFunction compare_function =
      Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL;
  std::array<float, 4U> border_colour{{0.0F, 0.0F, 0.0F, 1.0F}};
  bool texture_unit_hardware_gamma_enabled = false;
  Ogre14TerrainCompositeSceneFogMode scene_fog_mode =
      Ogre14TerrainCompositeSceneFogMode::FOG_NONE;
};

/// Exact native observation on the serialized OGRE render/resource thread.
struct Ogre14TerrainCompositeNativeObservation final {
  std::uint32_t version = kOgre14TerrainCompositeNativeObservationVersion;

  std::uintptr_t terrain_group_pointer_token = 0U;
  std::uintptr_t terrain_slot_pointer_token = 0U;
  std::uintptr_t terrain_pointer_token = 0U;
  std::uint32_t packed_slot_key = 0U;
  std::int32_t slot_x = 0;
  std::int32_t slot_y = 0;
  std::string exact_terrain_resource_group;
  std::string exact_filename_prefix;
  std::string exact_filename_extension;
  Ogre14TerrainCompositePageDefinitionKind page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  std::string exact_definition_filename;
  std::uintptr_t definition_import_data_pointer_token = 0U;
  std::string generated_save_filename;
  std::string exact_terrain_material_name;
  Ogre14TerrainCompositeAlignment terrain_alignment =
      Ogre14TerrainCompositeAlignment::X_Z;
  std::uint32_t terrain_size = 0U;
  float terrain_world_size = 0.0F;
  std::array<float, 3U> terrain_world_position{};
  bool terrain_is_loaded = false;
  bool terrain_derived_data_update_in_progress = true;

  std::uintptr_t texture_pointer_token = 0U;
  /// Compatibility alias for mip_chain.front().pixel_buffer_pointer_token.
  std::uintptr_t pixel_buffer_pointer_token = 0U;
  std::uint64_t texture_handle = 0U;
  std::string exact_texture_resource_group;
  std::string exact_texture_name;
  Ogre14TerrainCompositePixelEncoding pixel_encoding =
      Ogre14TerrainCompositePixelEncoding::BYTE_RGBA;
  Ogre14TerrainCompositeTextureType texture_type =
      Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  Ogre14TerrainCompositeTextureLoadingState texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  std::uint32_t texture_width = 0U;
  std::uint32_t texture_height = 0U;
  std::uint32_t texture_depth = 0U;
  std::uint32_t texture_face_count = 0U;
  /// Raw OGRE Texture::getNumMipmaps(): levels additional to level zero.
  std::uint32_t texture_additional_mip_count = 0U;
  /// Total levels retained by this contract: additional levels plus one.
  std::uint32_t texture_mip_count = 0U;
  /// V1-compatible readback origin. V2 still begins at face/mip zero before
  /// retaining the complete additional mip chain.
  std::uint32_t selected_face = 0U;
  std::uint32_t selected_mip = 0U;
  std::uint32_t texture_usage = 0U;
  bool texture_is_loaded = false;
  bool texture_is_manual = false;
  bool texture_hardware_gamma_enabled = false;
  std::uint64_t texture_resource_revision = 0U;
  /// Compatibility aliases for level zero.
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
  std::vector<Ogre14TerrainCompositeNativeMipObservation> mip_chain;
  Ogre14TerrainCompositeSamplingObservation sampling;
};

struct Ogre14TerrainCompositeMipMetadata final {
  std::uint32_t mip_level = 0U;
  std::uintptr_t pixel_buffer_pointer_token = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
  std::array<std::uint8_t, 32U> rgba_sha256{};
};

struct Ogre14TerrainCompositeCaptureMetadata final {
  std::uint32_t version = kOgre14TerrainCompositeCaptureReceiptVersion;
  std::uint32_t semantic_contract_version =
      kOgre14TerrainCompositeSemanticContractVersion;

  std::uintptr_t terrain_group_pointer_token = 0U;
  std::uintptr_t terrain_slot_pointer_token = 0U;
  std::uintptr_t terrain_pointer_token = 0U;
  std::uint32_t packed_slot_key = 0U;
  std::int32_t slot_x = 0;
  std::int32_t slot_y = 0;
  std::string exact_terrain_resource_group;
  std::string exact_filename_prefix;
  std::string exact_filename_extension;
  Ogre14TerrainCompositePageDefinitionKind page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  std::string exact_definition_filename;
  std::uintptr_t definition_import_data_pointer_token = 0U;
  std::string generated_save_filename;
  std::string exact_terrain_material_name;
  Ogre14TerrainCompositeAlignment terrain_alignment =
      Ogre14TerrainCompositeAlignment::X_Z;
  std::uint32_t terrain_size = 0U;
  float terrain_world_size = 0.0F;
  std::array<float, 3U> terrain_world_position{};
  bool terrain_is_loaded = false;
  bool terrain_derived_data_update_in_progress = true;

  std::uintptr_t texture_pointer_token = 0U;
  /// V1-compatible alias for mip_chain.front().pixel_buffer_pointer_token.
  std::uintptr_t pixel_buffer_pointer_token = 0U;
  std::uint64_t texture_handle = 0U;
  std::string exact_texture_resource_group;
  std::string exact_texture_name;
  Ogre14TerrainCompositePixelEncoding pixel_encoding =
      Ogre14TerrainCompositePixelEncoding::BYTE_RGBA;
  Ogre14TerrainCompositeTextureType texture_type =
      Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  Ogre14TerrainCompositeTextureLoadingState texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  Ogre14TerrainCompositeRowOrder row_order =
      Ogre14TerrainCompositeRowOrder::OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP;
  Ogre14TerrainCompositeChannelOrder channel_order =
      Ogre14TerrainCompositeChannelOrder::RED_GREEN_BLUE_ALPHA;
  Ogre14TerrainCompositeRgbSemantic rgb_semantic =
      Ogre14TerrainCompositeRgbSemantic::BAKED_DIFFUSE;
  Ogre14TerrainCompositeAlphaSemantic alpha_semantic =
      Ogre14TerrainCompositeAlphaSemantic::LINEAR_SPECULAR_MASK;
  Ogre14TerrainCompositeRgbTransfer rgb_transfer =
      Ogre14TerrainCompositeRgbTransfer::LEGACY_UNORM_DISPLAY_DOMAIN;
  std::uint32_t texture_width = 0U;
  std::uint32_t texture_height = 0U;
  std::uint32_t texture_depth = 0U;
  std::uint32_t texture_face_count = 0U;
  std::uint32_t texture_additional_mip_count = 0U;
  std::uint32_t texture_mip_count = 0U;
  /// V1-compatible authority aliases: V2 retains face zero and starts at mip
  /// zero, then additionally owns every subsequent level through 1x1.
  std::uint32_t selected_face = 0U;
  std::uint32_t selected_mip = 0U;
  std::uint32_t texture_usage = 0U;
  bool texture_is_loaded = false;
  bool texture_is_manual = false;
  bool texture_hardware_gamma_enabled = false;
  std::uint64_t texture_resource_revision_before_readback = 0U;
  std::uint64_t texture_resource_revision_after_readback = 0U;
  /// V1-compatible level-zero facts.
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
  std::uint64_t rgba_byte_count = 0U;
  std::array<std::uint8_t, 32U> rgba_sha256{};
  std::uint64_t full_mip_chain_rgba_byte_count = 0U;
  std::array<std::uint8_t, 32U> full_mip_chain_sha256{};
  std::vector<Ogre14TerrainCompositeMipMetadata> mip_chain;
  Ogre14TerrainCompositeSamplingObservation sampling;
};

constexpr std::uint32_t kOgre14TerrainCompositeOpaqueLoweringVersion = 1U;

struct Ogre14TerrainCompositeOpaqueMip final {
  std::uint32_t mip_level = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
  std::vector<std::uint8_t> rgba_bytes;
};

/// Renderer-neutral, owned target payload. It deliberately does not map the
/// transfer to current TextureColorSpace or MaterialDescriptor fields. The
/// follow-on MaterialDescriptor V3 integration consumes `rgb_transfer`.
/// Source alpha remains available only in the immutable receipt as
/// LINEAR_SPECULAR_MASK evidence; every lowered mip forces alpha to 255.
struct Ogre14TerrainCompositeOpaqueLowering final {
  std::uint32_t version = kOgre14TerrainCompositeOpaqueLoweringVersion;
  Ogre14TerrainCompositeRgbSemantic rgb_semantic =
      Ogre14TerrainCompositeRgbSemantic::BAKED_DIFFUSE;
  Ogre14TerrainCompositeRgbTransfer rgb_transfer =
      Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER;
  std::uint8_t forced_opaque_alpha = 255U;
  std::vector<Ogre14TerrainCompositeOpaqueMip> mip_chain;
  SamplerResourceDescriptor sampler;
  std::uint32_t texture_coordinate_set = 0U;
  std::array<float, 2U> texture_scale{{1.0F, 1.0F}};
  std::array<float, 2U> texture_offset{};
  float texture_rotation_radians = 0.0F;
};

struct Ogre14TerrainCompositeOracleTexel final {
  std::array<std::uint8_t, 4U> rgba{};
};

struct Ogre14TerrainCompositeOracleSample final {
  /// RGB is linear for DECODE_BEFORE_FILTER and encoded-display-domain UNORM
  /// for LEGACY_UNORM_DISPLAY_DOMAIN. Alpha is always a linear UNORM scalar.
  std::array<float, 4U> rgba{};
};

/// Renderer-neutral bilinear reference for one mip. Fractions are inclusive
/// [0,1], texels are ordered top-left, top-right, bottom-left, bottom-right.
[[nodiscard]] ValidationResult EvaluateOgre14TerrainCompositeBilinearOracle(
    Ogre14TerrainCompositeRgbTransfer transfer,
    const std::array<Ogre14TerrainCompositeOracleTexel, 4U> &texels,
    float u_fraction, float v_fraction,
    Ogre14TerrainCompositeOracleSample &sample);

class Ogre14TerrainCompositeNativeAdapter;

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
class Ogre14TerrainCompositeCaptureReceipt;

namespace Testing {
class Ogre14TerrainCompositeCaptureTestAccess final {
public:
  [[nodiscard]] static ValidationResult Capture(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      const void *rgba_bytes, std::size_t rgba_byte_count,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector = nullptr);

  [[nodiscard]] static ValidationResult CaptureMipChain(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      const std::vector<std::vector<std::uint8_t>> &mip_rgba_bytes,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector = nullptr);

  Ogre14TerrainCompositeCaptureTestAccess() = delete;
};
} // namespace Testing
#endif

/// Immutable owner for all tightly packed PF_BYTE_RGBA mips. The legacy
/// rgba_bytes()/rgba_size() accessors continue to name level zero only.
class Ogre14TerrainCompositeCaptureReceipt final {
public:
  Ogre14TerrainCompositeCaptureReceipt() noexcept = default;
  ~Ogre14TerrainCompositeCaptureReceipt() = default;
  Ogre14TerrainCompositeCaptureReceipt(
      const Ogre14TerrainCompositeCaptureReceipt &) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt &
  operator=(const Ogre14TerrainCompositeCaptureReceipt &) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt(
      Ogre14TerrainCompositeCaptureReceipt &&) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt &
  operator=(Ogre14TerrainCompositeCaptureReceipt &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14TerrainCompositeCaptureMetadata *
  metadata() const noexcept;
  [[nodiscard]] const std::uint8_t *rgba_bytes() const noexcept;
  [[nodiscard]] std::size_t rgba_size() const noexcept;
  [[nodiscard]] std::size_t mip_level_count() const noexcept;
  [[nodiscard]] const std::uint8_t *
  mip_rgba_bytes(std::size_t mip_level) const noexcept;
  [[nodiscard]] std::size_t mip_rgba_size(std::size_t mip_level) const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14TerrainCompositeCaptureReceipt &other) const noexcept;

private:
  struct State;
  explicit Ogre14TerrainCompositeCaptureReceipt(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14TerrainCompositeNativeAdapter;
};

/// OGRE 14.5.2-native capture edge. The caller must serialize terrain,
/// texture-unit, sampler, scene-fog, and resource mutation.
class Ogre14TerrainCompositeNativeAdapter final {
public:
  [[nodiscard]] static ValidationResult
  Capture(Ogre::TerrainGroup &terrain_group, std::int32_t slot_x,
          std::int32_t slot_y,
          const Ogre14TerrainCompositeCaptureConfiguration &configuration,
          Ogre14TerrainCompositeCaptureReceipt &receipt);

private:
  [[nodiscard]] static ValidationResult ValidateCaptureConfiguration(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration);

  [[nodiscard]] static ValidationResult ValidateCaptureInputs(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &observation);

  [[nodiscard]] static ValidationResult PublishOwnedReadback(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      std::vector<std::vector<std::uint8_t>> mip_rgba_bytes,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt);

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
  [[nodiscard]] static ValidationResult CaptureSyntheticForTesting(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      const std::vector<std::vector<std::uint8_t>> &mip_rgba_bytes,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector);

  friend class Testing::Ogre14TerrainCompositeCaptureTestAccess;
#endif
};

/// Transactionally derives tight RGBA mips for an eventual UNLIT/OPAQUE
/// target. RGB bytes are copied exactly, destination alpha is forced to 255 at
/// every mip, and both transfer modes remain explicit without relabeling the
/// legacy display-domain path as sRGB storage. Non-FOG_NONE receipts fail and
/// leave `lowering` unchanged. The source receipt is never modified.
[[nodiscard]] ValidationResult
LowerOgre14TerrainCompositeOpaque(
    const Ogre14TerrainCompositeCaptureReceipt &receipt,
    Ogre14TerrainCompositeOpaqueLowering &lowering);

} // namespace RoR::Render
