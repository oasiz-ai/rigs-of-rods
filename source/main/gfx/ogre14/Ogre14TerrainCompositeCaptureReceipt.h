/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Lossless, revision-stable OGRE 14 terrain-composite capture.

#pragma once

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

constexpr std::uint32_t kOgre14TerrainCompositeCaptureReceiptVersion = 1U;
constexpr std::uint32_t kOgre14TerrainCompositeCaptureConfigurationVersion =
    1U;
constexpr std::uint32_t kOgre14TerrainCompositeNativeObservationVersion = 1U;
constexpr std::uint32_t kOgre14TerrainCompositeSemanticContractVersion = 1U;

constexpr std::uint32_t kOgre14TerrainCompositeDefaultMaximumDimension =
    8192U;
constexpr std::uint64_t kOgre14TerrainCompositeDefaultMaximumRgbaBytes =
    256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14TerrainCompositeDefaultMaximumIdentifierBytes =
    16384U;
constexpr std::uint32_t kOgre14TerrainCompositeHardMaximumDimension = 16384U;
constexpr std::uint64_t kOgre14TerrainCompositeHardMaximumRgbaBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14TerrainCompositeHardMaximumIdentifierBytes =
    65536U;

struct Ogre14TerrainCompositeCaptureConfiguration final {
  std::uint32_t version =
      kOgre14TerrainCompositeCaptureConfigurationVersion;
  std::uint32_t maximum_dimension =
      kOgre14TerrainCompositeDefaultMaximumDimension;
  std::uint64_t maximum_rgba_bytes =
      kOgre14TerrainCompositeDefaultMaximumRgbaBytes;
  std::size_t maximum_identifier_bytes =
      kOgre14TerrainCompositeDefaultMaximumIdentifierBytes;
};

enum class Ogre14TerrainCompositeAlignment : std::uint8_t {
  X_Z = 0U,
  X_Y = 1U,
  Y_Z = 2U,
};

/// Exact TerrainGroup::TerrainSlotDefinition state at capture time. OGRE frees
/// ImportData after a successful imported-page prepare, so CONSUMED_OR_RUNTIME
/// deliberately makes no claim about the page's unavailable historical input.
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

/// The first retained row is the first row returned by OGRE's PixelBox API.
/// The pinned Metal patch deliberately performs no backend-specific Y flip.
enum class Ogre14TerrainCompositeRowOrder : std::uint8_t {
  OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP = 0U,
};

enum class Ogre14TerrainCompositeChannelOrder : std::uint8_t {
  RED_GREEN_BLUE_ALPHA = 0U,
};

/// The map has three diffuse channels plus one specular-mask channel.
/// OGRE 14 TerrainMaterialGeneratorA defines RGB as the composite diffuse
/// result and A as the blended per-layer specular mask. Alpha is not coverage.
enum class Ogre14TerrainCompositeAlphaSemantic : std::uint8_t {
  LINEAR_SPECULAR_MASK = 0U,
};

enum class Ogre14TerrainCompositeMaterialLoweringStatus : std::uint8_t {
  BLOCKED_ALPHA_IS_SPECULAR_MASK = 0U,
};

enum class Ogre14TerrainCompositeCaptureStage : std::uint8_t {
  AFTER_NATIVE_IDENTITY_CAPTURE = 0U,
  AFTER_RGBA_ALLOCATION = 1U,
  AFTER_NATIVE_READBACK = 2U,
  BEFORE_RECEIPT_PUBLICATION = 3U,
};

class IOgre14TerrainCompositeCaptureFaultInjector {
public:
  virtual ~IOgre14TerrainCompositeCaptureFaultInjector() = default;
  /// Borrowed test seam. Production always passes null. May throw anything.
  virtual void BeforeTerrainCompositeCaptureStage(
      Ogre14TerrainCompositeCaptureStage) {}
};

/// Exact native observation on the serialized OGRE render/resource thread.
/// This value is not authority by itself. Only the native adapter can mint a
/// nonempty receipt after two identical observations enclose one readback.
struct Ogre14TerrainCompositeNativeObservation final {
  std::uint32_t version =
      kOgre14TerrainCompositeNativeObservationVersion;

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
  std::uint32_t texture_mip_count = 0U;
  std::uint32_t selected_face = 0U;
  std::uint32_t selected_mip = 0U;
  std::uint32_t texture_usage = 0U;
  bool texture_is_loaded = false;
  bool texture_is_manual = false;
  bool texture_hardware_gamma_enabled = false;
  std::uint64_t texture_resource_revision = 0U;
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
};

struct Ogre14TerrainCompositeCaptureMetadata final {
  std::uint32_t version =
      kOgre14TerrainCompositeCaptureReceiptVersion;
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
  Ogre14TerrainCompositeAlphaSemantic alpha_semantic =
      Ogre14TerrainCompositeAlphaSemantic::LINEAR_SPECULAR_MASK;
  Ogre14TerrainCompositeMaterialLoweringStatus material_lowering_status =
      Ogre14TerrainCompositeMaterialLoweringStatus::
          BLOCKED_ALPHA_IS_SPECULAR_MASK;
  std::uint32_t texture_width = 0U;
  std::uint32_t texture_height = 0U;
  std::uint32_t texture_depth = 0U;
  std::uint32_t texture_face_count = 0U;
  std::uint32_t texture_mip_count = 0U;
  std::uint32_t selected_face = 0U;
  std::uint32_t selected_mip = 0U;
  std::uint32_t texture_usage = 0U;
  bool texture_is_loaded = false;
  bool texture_is_manual = false;
  bool texture_hardware_gamma_enabled = false;
  std::uint64_t texture_resource_revision_before_readback = 0U;
  std::uint64_t texture_resource_revision_after_readback = 0U;
  std::uint64_t tight_row_pitch_bytes = 0U;
  std::uint64_t tight_slice_pitch_bytes = 0U;
  std::uint64_t rgba_byte_count = 0U;
  std::array<std::uint8_t, 32U> rgba_sha256{};
};

class Ogre14TerrainCompositeNativeAdapter;

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
class Ogre14TerrainCompositeCaptureReceipt;

namespace Testing {
/// Compile-only test authority. This type and its synthetic entry point do not
/// exist in production translation units, so external code cannot define a
/// same-named friend class and mint a receipt from caller-authored metadata.
class Ogre14TerrainCompositeCaptureTestAccess final {
public:
  [[nodiscard]] static ValidationResult Capture(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      const void *rgba_bytes, std::size_t rgba_byte_count,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector = nullptr);

  Ogre14TerrainCompositeCaptureTestAccess() = delete;
};
}
#endif

/// Immutable owner for one exact, tightly packed level-zero RGBA readback.
/// Default construction is deliberately empty. A nonempty owner can only be
/// minted by Ogre14TerrainCompositeNativeAdapter.
class Ogre14TerrainCompositeCaptureReceipt final {
public:
  Ogre14TerrainCompositeCaptureReceipt() noexcept = default;
  ~Ogre14TerrainCompositeCaptureReceipt() = default;
  Ogre14TerrainCompositeCaptureReceipt(
      const Ogre14TerrainCompositeCaptureReceipt &) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt &operator=(
      const Ogre14TerrainCompositeCaptureReceipt &) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt(
      Ogre14TerrainCompositeCaptureReceipt &&) noexcept = default;
  Ogre14TerrainCompositeCaptureReceipt &operator=(
      Ogre14TerrainCompositeCaptureReceipt &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14TerrainCompositeCaptureMetadata *metadata()
      const noexcept;
  [[nodiscard]] const std::uint8_t *rgba_bytes() const noexcept;
  [[nodiscard]] std::size_t rgba_size() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14TerrainCompositeCaptureReceipt &other) const noexcept;

private:
  struct State;
  explicit Ogre14TerrainCompositeCaptureReceipt(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14TerrainCompositeNativeAdapter;
};

/// OGRE 14.5.2-native capture edge. Call only on OGRE's serialized
/// render/resource thread while TerrainGroup slot and resource lifecycle
/// mutation are excluded. The function invokes Terrain::updateCompositeMap()
/// before acquiring the texture and performs level-zero readback through the
/// public HardwarePixelBuffer::blitToMemory(PF_BYTE_RGBA) API.
class Ogre14TerrainCompositeNativeAdapter final {
public:
  [[nodiscard]] static ValidationResult Capture(
      Ogre::TerrainGroup &terrain_group, std::int32_t slot_x,
      std::int32_t slot_y,
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector = nullptr);

private:
  [[nodiscard]] static ValidationResult ValidateCaptureInputs(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &observation);

  [[nodiscard]] static ValidationResult PublishOwnedReadback(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      std::vector<std::uint8_t> rgba_bytes,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector);

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
  [[nodiscard]] static ValidationResult CaptureSyntheticForTesting(
      const Ogre14TerrainCompositeCaptureConfiguration &configuration,
      const Ogre14TerrainCompositeNativeObservation &before_readback,
      const void *rgba_bytes, std::size_t rgba_byte_count,
      const Ogre14TerrainCompositeNativeObservation &after_readback,
      Ogre14TerrainCompositeCaptureReceipt &receipt,
      IOgre14TerrainCompositeCaptureFaultInjector *fault_injector);

  friend class Testing::Ogre14TerrainCompositeCaptureTestAccess;
#endif
};

/// The current MaterialDescriptor interprets base texture alpha as coverage
/// and exposes no legacy specular-mask texture channel. Therefore neither
/// UNLIT nor metallic/roughness PBR can consume this receipt losslessly.
[[nodiscard]] ValidationResult
ValidateOgre14TerrainCompositeMaterialDescriptorLowering(
    const Ogre14TerrainCompositeCaptureReceipt &receipt);

} // namespace RoR::Render
