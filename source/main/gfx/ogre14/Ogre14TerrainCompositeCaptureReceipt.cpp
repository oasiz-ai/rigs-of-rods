/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

static_assert(
    std::is_nothrow_move_assignable<
        RoR::Render::Ogre14TerrainCompositeCaptureReceipt>::value,
    "terrain composite receipt publication must be transactional");

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool CheckedMultiplyU64(std::uint64_t lhs, std::uint64_t rhs,
                        std::uint64_t &result) noexcept {
  if (lhs != 0U &&
      rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

ValidationResult TightRgbaLayout(std::uint32_t width, std::uint32_t height,
                                 std::uint64_t &row_pitch,
                                 std::uint64_t &slice_pitch) {
  if (width == 0U || height == 0U) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "terrain_composite.texture.dimensions",
                   "terrain composite dimensions must be nonzero");
  }
  if (!CheckedMultiplyU64(width, 4U, row_pitch) ||
      !CheckedMultiplyU64(row_pitch, height, slice_pitch)) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.texture.layout",
                   "tight PF_BYTE_RGBA byte count overflows uint64");
  }
  return ValidationResult::Success();
}

bool IsIdentifier(const std::string &value, std::size_t maximum_bytes,
                  bool allow_empty = false) noexcept {
  return (allow_empty || !value.empty()) && value.size() <= maximum_bytes &&
         value.find('\0') == std::string::npos;
}

bool IsKnownAlignment(Ogre14TerrainCompositeAlignment value) noexcept {
  switch (value) {
  case Ogre14TerrainCompositeAlignment::X_Z:
  case Ogre14TerrainCompositeAlignment::X_Y:
  case Ogre14TerrainCompositeAlignment::Y_Z:
    return true;
  }
  return false;
}

bool IsKnownPageDefinitionKind(
    Ogre14TerrainCompositePageDefinitionKind value) noexcept {
  switch (value) {
  case Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED:
  case Ogre14TerrainCompositePageDefinitionKind::LIVE_IMPORT:
  case Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME:
    return true;
  }
  return false;
}

bool IsFinitePosition(const std::array<float, 3U> &position) noexcept {
  return std::isfinite(position[0U]) && std::isfinite(position[1U]) &&
         std::isfinite(position[2U]);
}

ValidationResult ValidateConfiguration(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration) {
  if (configuration.version !=
      kOgre14TerrainCompositeCaptureConfigurationVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "terrain_composite.configuration.version",
                   "unsupported terrain composite capture configuration");
  }
  if (configuration.maximum_dimension == 0U ||
      configuration.maximum_dimension >
          kOgre14TerrainCompositeHardMaximumDimension ||
      configuration.maximum_rgba_bytes == 0U ||
      configuration.maximum_rgba_bytes >
          kOgre14TerrainCompositeHardMaximumRgbaBytes ||
      configuration.maximum_identifier_bytes == 0U ||
      configuration.maximum_identifier_bytes >
          kOgre14TerrainCompositeHardMaximumIdentifierBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.configuration.caps",
                   "terrain composite capture caps exceed hard bounds");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateObservation(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &observation) {
  if (observation.version !=
      kOgre14TerrainCompositeNativeObservationVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "terrain_composite.observation.version",
                   "unsupported native terrain composite observation");
  }
  if (observation.terrain_group_pointer_token == 0U ||
      observation.terrain_slot_pointer_token == 0U ||
      observation.terrain_pointer_token == 0U ||
      observation.texture_pointer_token == 0U ||
      observation.pixel_buffer_pointer_token == 0U ||
      observation.texture_handle == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "terrain_composite.observation.native_identity",
                   "terrain, texture, and pixel-buffer identities must be nonzero");
  }
  if (!IsIdentifier(observation.exact_terrain_resource_group,
                    configuration.maximum_identifier_bytes) ||
      !IsIdentifier(observation.exact_filename_prefix,
                    configuration.maximum_identifier_bytes, true) ||
      !IsIdentifier(observation.exact_filename_extension,
                    configuration.maximum_identifier_bytes, true) ||
      !IsIdentifier(observation.exact_definition_filename,
                    configuration.maximum_identifier_bytes, true) ||
      !IsIdentifier(observation.generated_save_filename,
                    configuration.maximum_identifier_bytes) ||
      !IsIdentifier(observation.exact_terrain_material_name,
                    configuration.maximum_identifier_bytes) ||
      !IsIdentifier(observation.exact_texture_resource_group,
                    configuration.maximum_identifier_bytes) ||
      !IsIdentifier(observation.exact_texture_name,
                    configuration.maximum_identifier_bytes)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "terrain_composite.observation.identifiers",
                   "native terrain composite identifiers are empty, oversized, or contain NUL");
  }
  if (!IsKnownPageDefinitionKind(observation.page_definition_kind) ||
      (observation.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED &&
       (observation.exact_definition_filename.empty() ||
        observation.definition_import_data_pointer_token != 0U)) ||
      (observation.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::LIVE_IMPORT &&
       (!observation.exact_definition_filename.empty() ||
        observation.definition_import_data_pointer_token == 0U)) ||
      (observation.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME &&
       (!observation.exact_definition_filename.empty() ||
        observation.definition_import_data_pointer_token != 0U))) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "terrain_composite.observation.page_definition",
                   "Terrain page definition kind, filename, and ImportData identity disagree");
  }
  if (observation.slot_x < -32768 || observation.slot_x > 32767 ||
      observation.slot_y < -32768 || observation.slot_y > 32767 ||
      !IsKnownAlignment(observation.terrain_alignment) ||
      observation.terrain_size < 3U ||
      !std::isfinite(observation.terrain_world_size) ||
      observation.terrain_world_size <= 0.0F ||
      !IsFinitePosition(observation.terrain_world_position) ||
      !observation.terrain_is_loaded ||
      observation.terrain_derived_data_update_in_progress) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "terrain_composite.observation.terrain_page",
                   "native terrain page layout or transform is invalid");
  }

  std::uint64_t row_pitch = 0U;
  std::uint64_t slice_pitch = 0U;
  ValidationResult layout = TightRgbaLayout(
      observation.texture_width, observation.texture_height, row_pitch,
      slice_pitch);
  if (!layout) {
    return layout;
  }
  if (observation.texture_width > configuration.maximum_dimension ||
      observation.texture_height > configuration.maximum_dimension ||
      slice_pitch > configuration.maximum_rgba_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.observation.texture_cap",
                   "native terrain composite exceeds configured capture caps");
  }
  if (observation.pixel_encoding !=
          Ogre14TerrainCompositePixelEncoding::BYTE_RGBA ||
      observation.texture_type !=
          Ogre14TerrainCompositeTextureType::TEXTURE_2D ||
      observation.texture_loading_state !=
          Ogre14TerrainCompositeTextureLoadingState::LOADED ||
      observation.texture_depth != 1U ||
      observation.texture_face_count != 1U ||
      observation.texture_mip_count == 0U ||
      observation.selected_face != 0U || observation.selected_mip != 0U ||
      !observation.texture_is_loaded ||
      !observation.texture_is_manual) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.observation.texture_state",
                   "capture requires one loaded manual 2D PF_BYTE_RGBA texture at face zero, mip zero");
  }
  if (observation.texture_resource_revision == 0U ||
      observation.texture_resource_revision ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.observation.resource_revision",
                   "terrain composite resource revision must be stable, nonzero, and advanceable");
  }
  if (observation.tight_row_pitch_bytes != row_pitch ||
      observation.tight_slice_pitch_bytes != slice_pitch) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.observation.tight_layout",
                   "native observation does not describe tight RGBA rows and slice");
  }
  return ValidationResult::Success();
}

bool SameObservation(
    const Ogre14TerrainCompositeNativeObservation &lhs,
    const Ogre14TerrainCompositeNativeObservation &rhs) noexcept {
  return lhs.version == rhs.version &&
         lhs.terrain_group_pointer_token == rhs.terrain_group_pointer_token &&
         lhs.terrain_slot_pointer_token == rhs.terrain_slot_pointer_token &&
         lhs.terrain_pointer_token == rhs.terrain_pointer_token &&
         lhs.packed_slot_key == rhs.packed_slot_key &&
         lhs.slot_x == rhs.slot_x && lhs.slot_y == rhs.slot_y &&
         lhs.exact_terrain_resource_group ==
             rhs.exact_terrain_resource_group &&
         lhs.exact_filename_prefix == rhs.exact_filename_prefix &&
         lhs.exact_filename_extension == rhs.exact_filename_extension &&
         lhs.page_definition_kind == rhs.page_definition_kind &&
         lhs.exact_definition_filename == rhs.exact_definition_filename &&
         lhs.definition_import_data_pointer_token ==
             rhs.definition_import_data_pointer_token &&
         lhs.generated_save_filename == rhs.generated_save_filename &&
         lhs.exact_terrain_material_name ==
             rhs.exact_terrain_material_name &&
         lhs.terrain_alignment == rhs.terrain_alignment &&
         lhs.terrain_size == rhs.terrain_size &&
         lhs.terrain_world_size == rhs.terrain_world_size &&
         lhs.terrain_world_position == rhs.terrain_world_position &&
         lhs.terrain_is_loaded == rhs.terrain_is_loaded &&
         lhs.terrain_derived_data_update_in_progress ==
             rhs.terrain_derived_data_update_in_progress &&
         lhs.texture_pointer_token == rhs.texture_pointer_token &&
         lhs.pixel_buffer_pointer_token == rhs.pixel_buffer_pointer_token &&
         lhs.texture_handle == rhs.texture_handle &&
         lhs.exact_texture_resource_group ==
             rhs.exact_texture_resource_group &&
         lhs.exact_texture_name == rhs.exact_texture_name &&
         lhs.pixel_encoding == rhs.pixel_encoding &&
         lhs.texture_type == rhs.texture_type &&
         lhs.texture_loading_state == rhs.texture_loading_state &&
         lhs.texture_width == rhs.texture_width &&
         lhs.texture_height == rhs.texture_height &&
         lhs.texture_depth == rhs.texture_depth &&
         lhs.texture_face_count == rhs.texture_face_count &&
         lhs.texture_mip_count == rhs.texture_mip_count &&
         lhs.selected_face == rhs.selected_face &&
         lhs.selected_mip == rhs.selected_mip &&
         lhs.texture_usage == rhs.texture_usage &&
         lhs.texture_is_loaded == rhs.texture_is_loaded &&
         lhs.texture_is_manual == rhs.texture_is_manual &&
         lhs.texture_hardware_gamma_enabled ==
             rhs.texture_hardware_gamma_enabled &&
         lhs.texture_resource_revision == rhs.texture_resource_revision &&
         lhs.tight_row_pitch_bytes == rhs.tight_row_pitch_bytes &&
         lhs.tight_slice_pitch_bytes == rhs.tight_slice_pitch_bytes;
}

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

constexpr std::uint32_t RotateRight(std::uint32_t value,
                                    std::uint32_t amount) noexcept {
  return (value >> amount) | (value << (32U - amount));
}

class Sha256 final {
public:
  void Update(const std::uint8_t *bytes, std::size_t size) noexcept {
    for (std::size_t index = 0U; index < size; ++index) {
      block_[block_size_++] = bytes[index];
      if (block_size_ == block_.size()) {
        Transform();
        total_bytes_ += block_.size();
        block_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32U> Final() noexcept {
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(total_bytes_ + block_size_) * 8ULL;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0U;
      }
      Transform();
      block_size_ = 0U;
    }
    while (block_size_ < 56U) {
      block_[block_size_++] = 0U;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_count >> (index * 8U));
    }
    Transform();

    std::array<std::uint8_t, 32U> digest{};
    for (std::size_t word = 0U; word < state_.size(); ++word) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        digest[word * 4U + byte] = static_cast<std::uint8_t>(
            state_[word] >> ((3U - byte) * 8U));
      }
    }
    return digest;
  }

private:
  void Transform() noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block_[offset + 1U])
                      << 16U) |
                     (static_cast<std::uint32_t>(block_[offset + 2U])
                      << 8U) |
                     static_cast<std::uint32_t>(block_[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t before = words[index - 15U];
      const std::uint32_t after = words[index - 2U];
      const std::uint32_t sigma0 = RotateRight(before, 7U) ^
                                   RotateRight(before, 18U) ^ (before >> 3U);
      const std::uint32_t sigma1 = RotateRight(after, 17U) ^
                                   RotateRight(after, 19U) ^ (after >> 10U);
      words[index] =
          words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];

    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t sum1 =
          RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary1 =
          h + sum1 + choose + kSha256RoundConstants[index] + words[index];
      const std::uint32_t sum0 =
          RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
  }

  std::array<std::uint32_t, 8U> state_{{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  }};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_ = 0U;
  std::size_t total_bytes_ = 0U;
};

std::array<std::uint8_t, 32U>
ComputeSha256(const std::vector<std::uint8_t> &bytes) noexcept {
  Sha256 hasher;
  if (!bytes.empty()) {
    hasher.Update(bytes.data(), bytes.size());
  }
  return hasher.Final();
}

void MaybeInject(Ogre14TerrainCompositeCaptureStage stage,
                 IOgre14TerrainCompositeCaptureFaultInjector *injector) {
  if (injector != nullptr) {
    injector->BeforeTerrainCompositeCaptureStage(stage);
  }
}

Ogre14TerrainCompositeCaptureMetadata BuildMetadata(
    const Ogre14TerrainCompositeNativeObservation &before,
    const Ogre14TerrainCompositeNativeObservation &after,
    const std::vector<std::uint8_t> &bytes) {
  Ogre14TerrainCompositeCaptureMetadata metadata;
  metadata.terrain_group_pointer_token =
      before.terrain_group_pointer_token;
  metadata.terrain_slot_pointer_token = before.terrain_slot_pointer_token;
  metadata.terrain_pointer_token = before.terrain_pointer_token;
  metadata.packed_slot_key = before.packed_slot_key;
  metadata.slot_x = before.slot_x;
  metadata.slot_y = before.slot_y;
  metadata.exact_terrain_resource_group =
      before.exact_terrain_resource_group;
  metadata.exact_filename_prefix = before.exact_filename_prefix;
  metadata.exact_filename_extension = before.exact_filename_extension;
  metadata.page_definition_kind = before.page_definition_kind;
  metadata.exact_definition_filename = before.exact_definition_filename;
  metadata.definition_import_data_pointer_token =
      before.definition_import_data_pointer_token;
  metadata.generated_save_filename = before.generated_save_filename;
  metadata.exact_terrain_material_name =
      before.exact_terrain_material_name;
  metadata.terrain_alignment = before.terrain_alignment;
  metadata.terrain_size = before.terrain_size;
  metadata.terrain_world_size = before.terrain_world_size;
  metadata.terrain_world_position = before.terrain_world_position;
  metadata.terrain_is_loaded = before.terrain_is_loaded;
  metadata.terrain_derived_data_update_in_progress =
      before.terrain_derived_data_update_in_progress;
  metadata.texture_pointer_token = before.texture_pointer_token;
  metadata.pixel_buffer_pointer_token = before.pixel_buffer_pointer_token;
  metadata.texture_handle = before.texture_handle;
  metadata.exact_texture_resource_group =
      before.exact_texture_resource_group;
  metadata.exact_texture_name = before.exact_texture_name;
  metadata.pixel_encoding = before.pixel_encoding;
  metadata.texture_type = before.texture_type;
  metadata.texture_loading_state = before.texture_loading_state;
  metadata.texture_width = before.texture_width;
  metadata.texture_height = before.texture_height;
  metadata.texture_depth = before.texture_depth;
  metadata.texture_face_count = before.texture_face_count;
  metadata.texture_mip_count = before.texture_mip_count;
  metadata.selected_face = before.selected_face;
  metadata.selected_mip = before.selected_mip;
  metadata.texture_usage = before.texture_usage;
  metadata.texture_is_loaded = before.texture_is_loaded;
  metadata.texture_is_manual = before.texture_is_manual;
  metadata.texture_hardware_gamma_enabled =
      before.texture_hardware_gamma_enabled;
  metadata.texture_resource_revision_before_readback =
      before.texture_resource_revision;
  metadata.texture_resource_revision_after_readback =
      after.texture_resource_revision;
  metadata.tight_row_pitch_bytes = before.tight_row_pitch_bytes;
  metadata.tight_slice_pitch_bytes = before.tight_slice_pitch_bytes;
  metadata.rgba_byte_count = static_cast<std::uint64_t>(bytes.size());
  metadata.rgba_sha256 = ComputeSha256(bytes);
  return metadata;
}

} // namespace

struct Ogre14TerrainCompositeCaptureReceipt::State final {
  Ogre14TerrainCompositeCaptureMetadata metadata;
  std::vector<std::uint8_t> rgba_bytes;
};

Ogre14TerrainCompositeCaptureReceipt::Ogre14TerrainCompositeCaptureReceipt(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14TerrainCompositeCaptureReceipt::initialized() const noexcept {
  return state_ != nullptr;
}

const Ogre14TerrainCompositeCaptureMetadata *
Ogre14TerrainCompositeCaptureReceipt::metadata() const noexcept {
  return state_ != nullptr ? &state_->metadata : nullptr;
}

const std::uint8_t *
Ogre14TerrainCompositeCaptureReceipt::rgba_bytes() const noexcept {
  return state_ != nullptr && !state_->rgba_bytes.empty()
             ? state_->rgba_bytes.data()
             : nullptr;
}

std::size_t Ogre14TerrainCompositeCaptureReceipt::rgba_size() const noexcept {
  return state_ != nullptr ? state_->rgba_bytes.size() : 0U;
}

bool Ogre14TerrainCompositeCaptureReceipt::SharesImmutableStateWith(
    const Ogre14TerrainCompositeCaptureReceipt &other) const noexcept {
  return state_ != nullptr && other.state_ != nullptr &&
         state_.get() == other.state_.get() &&
         !state_.owner_before(other.state_) &&
         !other.state_.owner_before(state_);
}

ValidationResult Ogre14TerrainCompositeNativeAdapter::ValidateCaptureInputs(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &observation) {
  ValidationResult validation = ValidateConfiguration(configuration);
  return validation ? ValidateObservation(configuration, observation)
                    : validation;
}

ValidationResult Ogre14TerrainCompositeNativeAdapter::PublishOwnedReadback(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before_readback,
    std::vector<std::uint8_t> rgba_bytes,
    const Ogre14TerrainCompositeNativeObservation &after_readback,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  try {
    ValidationResult validation =
        ValidateCaptureInputs(configuration, before_readback);
    if (!validation) {
      return validation;
    }
    validation = ValidateCaptureInputs(configuration, after_readback);
    if (!validation) {
      return validation;
    }
    if (!SameObservation(before_readback, after_readback)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "terrain_composite.readback.revalidation",
                     "terrain page, texture identity, or resource revision changed during readback");
    }
    if (rgba_bytes.size() != before_readback.tight_slice_pitch_bytes) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "terrain_composite.readback.rgba_bytes",
                     "native readback byte count differs from the tight RGBA slice");
    }

    auto candidate = std::make_shared<Ogre14TerrainCompositeCaptureReceipt::
                                          State>();
    candidate->metadata =
        BuildMetadata(before_readback, after_readback, rgba_bytes);
    candidate->rgba_bytes = std::move(rgba_bytes);
    Ogre14TerrainCompositeCaptureReceipt candidate_receipt(candidate);
    MaybeInject(
        Ogre14TerrainCompositeCaptureStage::BEFORE_RECEIPT_PUBLICATION,
        fault_injector);
    receipt = std::move(candidate_receipt);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "terrain_composite.capture.allocation",
                   "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.capture.unexpected_exception",
                   "unexpected exception before terrain composite receipt publication");
  }
}

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
ValidationResult
Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before_readback,
    const void *rgba_bytes, std::size_t rgba_byte_count,
    const Ogre14TerrainCompositeNativeObservation &after_readback,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  try {
    ValidationResult validation =
        ValidateCaptureInputs(configuration, before_readback);
    if (!validation) {
      return validation;
    }
    MaybeInject(
        Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE,
        fault_injector);
    if (rgba_bytes == nullptr && rgba_byte_count != 0U) {
      return Failure(ValidationCode::EMPTY_PAYLOAD,
                     "terrain_composite.readback.rgba_bytes",
                     "nonempty synthetic readback has no source bytes");
    }
    std::vector<std::uint8_t> owned_bytes;
    if (rgba_byte_count != 0U) {
      const auto *first = static_cast<const std::uint8_t *>(rgba_bytes);
      owned_bytes.assign(first, first + rgba_byte_count);
    }
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_RGBA_ALLOCATION,
                fault_injector);
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_READBACK,
                fault_injector);
    return PublishOwnedReadback(configuration, before_readback,
                                std::move(owned_bytes), after_readback,
                                receipt, fault_injector);
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "terrain_composite.capture.allocation",
                   "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.capture.unexpected_exception",
                   "unexpected exception before terrain composite receipt publication");
  }
}
#endif

ValidationResult ValidateOgre14TerrainCompositeMaterialDescriptorLowering(
    const Ogre14TerrainCompositeCaptureReceipt &receipt) {
  if (!receipt.initialized() || receipt.metadata() == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "terrain_composite.material_descriptor.receipt",
                   "terrain composite lowering requires a native receipt");
  }
  if (receipt.metadata()->alpha_semantic !=
          Ogre14TerrainCompositeAlphaSemantic::LINEAR_SPECULAR_MASK ||
      receipt.metadata()->material_lowering_status !=
          Ogre14TerrainCompositeMaterialLoweringStatus::
              BLOCKED_ALPHA_IS_SPECULAR_MASK) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "terrain_composite.material_descriptor.semantics",
                   "terrain composite receipt carries unknown channel semantics");
  }
  return Failure(
      ValidationCode::UNSUPPORTED_FEATURE,
      "terrain_composite.material_descriptor.alpha_specular_mask",
      "MaterialDescriptor base alpha is coverage and metallic-roughness has no legacy specular-mask channel; UNLIT and PBR lowering are not lossless");
}

} // namespace RoR::Render
