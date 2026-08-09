/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

static_assert(std::is_nothrow_move_assignable<
                  RoR::Render::Ogre14TerrainCompositeCaptureReceipt>::value,
              "terrain composite receipt publication must be transactional");

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool CheckedAddU64(std::uint64_t lhs, std::uint64_t rhs,
                   std::uint64_t &result) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

bool CheckedMultiplyU64(std::uint64_t lhs, std::uint64_t rhs,
                        std::uint64_t &result) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
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

std::uint32_t FullMipLevelCount(std::uint32_t width,
                                std::uint32_t height) noexcept {
  std::uint32_t count = 0U;
  while (width != 0U && height != 0U) {
    ++count;
    if (width == 1U && height == 1U) {
      break;
    }
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
  }
  return count;
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

std::uint32_t FloatBits(float value) noexcept {
  std::uint32_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value),
                "terrain composite float facts require binary32 storage");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

bool SameFloatBits(float lhs, float rhs) noexcept {
  return FloatBits(lhs) == FloatBits(rhs);
}

template <std::size_t Size>
bool SameFloatArrayBits(const std::array<float, Size> &lhs,
                        const std::array<float, Size> &rhs) noexcept {
  for (std::size_t index = 0U; index < Size; ++index) {
    if (!SameFloatBits(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

const std::array<float, 16U> &IdentityTextureTransform() noexcept {
  static const std::array<float, 16U> identity{{
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
  }};
  return identity;
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
          kOgre14TerrainCompositeHardMaximumIdentifierBytes ||
      configuration.maximum_mip_levels == 0U ||
      configuration.maximum_mip_levels >
          kOgre14TerrainCompositeHardMaximumMipLevels) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.configuration.caps",
                   "terrain composite capture caps exceed hard bounds");
  }
  return ValidationResult::Success();
}

bool IsKnownSceneFogMode(Ogre14TerrainCompositeSceneFogMode mode) noexcept {
  switch (mode) {
  case Ogre14TerrainCompositeSceneFogMode::FOG_NONE:
  case Ogre14TerrainCompositeSceneFogMode::FOG_EXP:
  case Ogre14TerrainCompositeSceneFogMode::FOG_EXP2:
  case Ogre14TerrainCompositeSceneFogMode::FOG_LINEAR:
    return true;
  }
  return false;
}

ValidationResult ValidateSampling(
    const Ogre14TerrainCompositeNativeObservation &observation) {
  const Ogre14TerrainCompositeSamplingObservation &sampling =
      observation.sampling;
  if (sampling.scene_manager_pointer_token == 0U ||
      sampling.texture_unit_pointer_token == 0U ||
      sampling.sampler_pointer_token == 0U ||
      sampling.bound_texture_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "terrain_composite.observation.sampling_identity",
                   "scene, texture-unit, sampler, and texture identities must "
                   "be nonzero");
  }
  if (sampling.bound_texture_pointer_token !=
          observation.texture_pointer_token ||
      !sampling.texture_unit_content_named ||
      sampling.texture_unit_frame_count != 1U ||
      sampling.texture_unit_current_frame != 0U ||
      !sampling.texture_unit_texture_2d || sampling.texture_unit_is_blank ||
      sampling.texture_unit_load_failing ||
      sampling.unordered_access_mip_level != -1) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.observation.texture_unit_binding",
                   "the exact named frame-zero 2D texture unit must remain "
                   "bound to the captured composite texture");
  }
  if (sampling.texture_coord_set != 0U ||
      !sampling.texcoord_calculation_none ||
      sampling.texture_effect_count != 0U ||
      !SameFloatBits(sampling.texture_u_scroll, 0.0F) ||
      !SameFloatBits(sampling.texture_v_scroll, 0.0F) ||
      !SameFloatBits(sampling.texture_u_scale, 1.0F) ||
      !SameFloatBits(sampling.texture_v_scale, 1.0F) ||
      !SameFloatBits(sampling.texture_rotation_radians, 0.0F) ||
      !SameFloatArrayBits(sampling.texture_transform,
                          IdentityTextureTransform())) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.observation.sampling_uv",
                   "terrain composite transport requires UV0, TEXCALC_NONE, "
                   "no effects, and an exact identity texture transform");
  }
  const std::array<float, 4U> black{{0.0F, 0.0F, 0.0F, 1.0F}};
  if (sampling.address_u != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.address_v != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.address_w != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.min_filter != Ogre14TerrainCompositeFilter::LINEAR ||
      sampling.mag_filter != Ogre14TerrainCompositeFilter::LINEAR ||
      sampling.mip_filter != Ogre14TerrainCompositeFilter::POINT ||
      sampling.maximum_anisotropy != 1U ||
      !SameFloatBits(sampling.mipmap_bias, 0.0F) ||
      sampling.compare_enabled ||
      sampling.compare_function !=
          Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL ||
      !SameFloatArrayBits(sampling.border_colour, black)) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.observation.sampling_sampler",
                   "terrain composite sampler must be exact clamp/bilinear, "
                   "point mip, black border, and non-comparison state");
  }
  if (sampling.texture_unit_hardware_gamma_enabled !=
      observation.texture_hardware_gamma_enabled) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.observation.gamma_agreement",
                   "Texture and exact bound TextureUnitState hardware-gamma "
                   "facts must agree");
  }
  if (!IsKnownSceneFogMode(sampling.scene_fog_mode)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "terrain_composite.observation.scene_fog_mode",
                   "SceneManager exposes an unknown direct fog mode");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateObservation(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &observation) {
  if (observation.version != kOgre14TerrainCompositeNativeObservationVersion) {
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
                   "terrain, texture, and level-zero pixel-buffer identities "
                   "must be nonzero");
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
                   "native terrain composite identifiers are empty, oversized, "
                   "or contain NUL");
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
                   "terrain page definition kind disagrees with its exact "
                   "source identity");
  }
  if (!IsKnownAlignment(observation.terrain_alignment) ||
      observation.terrain_size < 2U ||
      !std::isfinite(observation.terrain_world_size) ||
      observation.terrain_world_size <= 0.0F ||
      !IsFinitePosition(observation.terrain_world_position) ||
      !observation.terrain_is_loaded ||
      observation.terrain_derived_data_update_in_progress) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "terrain_composite.observation.terrain_page",
                   "native terrain page layout or loaded state is invalid");
  }
  if (observation.texture_width == 0U || observation.texture_height == 0U ||
      observation.texture_width > configuration.maximum_dimension ||
      observation.texture_height > configuration.maximum_dimension) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "terrain_composite.observation.texture_cap",
        "native terrain composite dimensions exceed configured capture caps");
  }
  if (observation.pixel_encoding !=
          Ogre14TerrainCompositePixelEncoding::BYTE_RGBA ||
      observation.texture_type !=
          Ogre14TerrainCompositeTextureType::TEXTURE_2D ||
      observation.texture_loading_state !=
          Ogre14TerrainCompositeTextureLoadingState::LOADED ||
      observation.texture_depth != 1U || observation.texture_face_count != 1U ||
      observation.selected_face != 0U || observation.selected_mip != 0U ||
      !observation.texture_is_loaded || !observation.texture_is_manual) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "terrain_composite.observation.texture_state",
        "capture requires one loaded manual 2D PF_BYTE_RGBA texture");
  }
  if (observation.texture_resource_revision == 0U ||
      observation.texture_resource_revision ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.observation.resource_revision",
                   "terrain composite resource revision must be stable, "
                   "nonzero, and advanceable");
  }

  const std::uint32_t expected_mips =
      FullMipLevelCount(observation.texture_width, observation.texture_height);
  if (observation.texture_additional_mip_count ==
          (std::numeric_limits<std::uint32_t>::max)() ||
      observation.texture_mip_count !=
          observation.texture_additional_mip_count + 1U ||
      observation.texture_mip_count != expected_mips ||
      observation.texture_mip_count > configuration.maximum_mip_levels ||
      observation.mip_chain.size() != observation.texture_mip_count) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.observation.full_mip_chain",
                   "native observation must contain every mip through the "
                   "exact 1x1 level");
  }

  std::uint64_t total_bytes = 0U;
  std::uint32_t expected_width = observation.texture_width;
  std::uint32_t expected_height = observation.texture_height;
  for (std::size_t index = 0U; index < observation.mip_chain.size(); ++index) {
    const Ogre14TerrainCompositeNativeMipObservation &mip =
        observation.mip_chain[index];
    std::uint64_t row_pitch = 0U;
    std::uint64_t slice_pitch = 0U;
    ValidationResult layout = TightRgbaLayout(expected_width, expected_height,
                                              row_pitch, slice_pitch);
    if (!layout) {
      return layout;
    }
    if (mip.mip_level != index || mip.pixel_buffer_pointer_token == 0U ||
        mip.width != expected_width || mip.height != expected_height ||
        mip.depth != 1U || mip.tight_row_pitch_bytes != row_pitch ||
        mip.tight_slice_pitch_bytes != slice_pitch) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "terrain_composite.observation.mip_layout",
                     "native mip identity, order, dimensions, or tight RGBA "
                     "layout is invalid");
    }
    for (std::size_t prior = 0U; prior < index; ++prior) {
      if (observation.mip_chain[prior].pixel_buffer_pointer_token ==
          mip.pixel_buffer_pointer_token) {
        return Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain_composite.observation.mip_identity",
            "two mip levels alias the same native pixel-buffer identity");
      }
    }
    if (!CheckedAddU64(total_bytes, slice_pitch, total_bytes) ||
        total_bytes > configuration.maximum_rgba_bytes) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "terrain_composite.observation.full_mip_cap",
          "aggregate terrain composite mip bytes exceed the configured cap");
    }
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }
  const Ogre14TerrainCompositeNativeMipObservation &level_zero =
      observation.mip_chain.front();
  if (observation.pixel_buffer_pointer_token !=
          level_zero.pixel_buffer_pointer_token ||
      observation.tight_row_pitch_bytes != level_zero.tight_row_pitch_bytes ||
      observation.tight_slice_pitch_bytes !=
          level_zero.tight_slice_pitch_bytes) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "terrain_composite.observation.level_zero_alias",
        "V1-compatible level-zero aliases disagree with mip-chain authority");
  }
  return ValidateSampling(observation);
}

bool SameMipObservation(
    const Ogre14TerrainCompositeNativeMipObservation &lhs,
    const Ogre14TerrainCompositeNativeMipObservation &rhs) noexcept {
  return lhs.mip_level == rhs.mip_level &&
         lhs.pixel_buffer_pointer_token == rhs.pixel_buffer_pointer_token &&
         lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.depth == rhs.depth &&
         lhs.tight_row_pitch_bytes == rhs.tight_row_pitch_bytes &&
         lhs.tight_slice_pitch_bytes == rhs.tight_slice_pitch_bytes;
}

bool SameSampling(const Ogre14TerrainCompositeSamplingObservation &lhs,
                  const Ogre14TerrainCompositeSamplingObservation &rhs) noexcept {
  return lhs.scene_manager_pointer_token == rhs.scene_manager_pointer_token &&
         lhs.texture_unit_pointer_token == rhs.texture_unit_pointer_token &&
         lhs.sampler_pointer_token == rhs.sampler_pointer_token &&
         lhs.bound_texture_pointer_token == rhs.bound_texture_pointer_token &&
         lhs.texture_unit_content_named == rhs.texture_unit_content_named &&
         lhs.texture_unit_frame_count == rhs.texture_unit_frame_count &&
         lhs.texture_unit_current_frame == rhs.texture_unit_current_frame &&
         lhs.texture_unit_texture_2d == rhs.texture_unit_texture_2d &&
         lhs.texture_unit_is_blank == rhs.texture_unit_is_blank &&
         lhs.texture_unit_load_failing == rhs.texture_unit_load_failing &&
         lhs.unordered_access_mip_level == rhs.unordered_access_mip_level &&
         lhs.texture_coord_set == rhs.texture_coord_set &&
         lhs.texcoord_calculation_none == rhs.texcoord_calculation_none &&
         lhs.texture_effect_count == rhs.texture_effect_count &&
         SameFloatBits(lhs.texture_u_scroll, rhs.texture_u_scroll) &&
         SameFloatBits(lhs.texture_v_scroll, rhs.texture_v_scroll) &&
         SameFloatBits(lhs.texture_u_scale, rhs.texture_u_scale) &&
         SameFloatBits(lhs.texture_v_scale, rhs.texture_v_scale) &&
         SameFloatBits(lhs.texture_rotation_radians,
                       rhs.texture_rotation_radians) &&
         SameFloatArrayBits(lhs.texture_transform, rhs.texture_transform) &&
         lhs.address_u == rhs.address_u && lhs.address_v == rhs.address_v &&
         lhs.address_w == rhs.address_w && lhs.min_filter == rhs.min_filter &&
         lhs.mag_filter == rhs.mag_filter && lhs.mip_filter == rhs.mip_filter &&
         lhs.maximum_anisotropy == rhs.maximum_anisotropy &&
         SameFloatBits(lhs.mipmap_bias, rhs.mipmap_bias) &&
         lhs.compare_enabled == rhs.compare_enabled &&
         lhs.compare_function == rhs.compare_function &&
         SameFloatArrayBits(lhs.border_colour, rhs.border_colour) &&
         lhs.texture_unit_hardware_gamma_enabled ==
             rhs.texture_unit_hardware_gamma_enabled &&
         lhs.scene_fog_mode == rhs.scene_fog_mode;
}

bool SameObservation(
    const Ogre14TerrainCompositeNativeObservation &lhs,
    const Ogre14TerrainCompositeNativeObservation &rhs) noexcept {
  if (lhs.version != rhs.version ||
      lhs.terrain_group_pointer_token != rhs.terrain_group_pointer_token ||
      lhs.terrain_slot_pointer_token != rhs.terrain_slot_pointer_token ||
      lhs.terrain_pointer_token != rhs.terrain_pointer_token ||
      lhs.packed_slot_key != rhs.packed_slot_key || lhs.slot_x != rhs.slot_x ||
      lhs.slot_y != rhs.slot_y ||
      lhs.exact_terrain_resource_group != rhs.exact_terrain_resource_group ||
      lhs.exact_filename_prefix != rhs.exact_filename_prefix ||
      lhs.exact_filename_extension != rhs.exact_filename_extension ||
      lhs.page_definition_kind != rhs.page_definition_kind ||
      lhs.exact_definition_filename != rhs.exact_definition_filename ||
      lhs.definition_import_data_pointer_token !=
          rhs.definition_import_data_pointer_token ||
      lhs.generated_save_filename != rhs.generated_save_filename ||
      lhs.exact_terrain_material_name != rhs.exact_terrain_material_name ||
      lhs.terrain_alignment != rhs.terrain_alignment ||
      lhs.terrain_size != rhs.terrain_size ||
      !SameFloatBits(lhs.terrain_world_size, rhs.terrain_world_size) ||
      !SameFloatArrayBits(lhs.terrain_world_position,
                          rhs.terrain_world_position) ||
      lhs.terrain_is_loaded != rhs.terrain_is_loaded ||
      lhs.terrain_derived_data_update_in_progress !=
          rhs.terrain_derived_data_update_in_progress ||
      lhs.texture_pointer_token != rhs.texture_pointer_token ||
      lhs.pixel_buffer_pointer_token != rhs.pixel_buffer_pointer_token ||
      lhs.texture_handle != rhs.texture_handle ||
      lhs.exact_texture_resource_group != rhs.exact_texture_resource_group ||
      lhs.exact_texture_name != rhs.exact_texture_name ||
      lhs.pixel_encoding != rhs.pixel_encoding ||
      lhs.texture_type != rhs.texture_type ||
      lhs.texture_loading_state != rhs.texture_loading_state ||
      lhs.texture_width != rhs.texture_width ||
      lhs.texture_height != rhs.texture_height ||
      lhs.texture_depth != rhs.texture_depth ||
      lhs.texture_face_count != rhs.texture_face_count ||
      lhs.texture_additional_mip_count != rhs.texture_additional_mip_count ||
      lhs.texture_mip_count != rhs.texture_mip_count ||
      lhs.selected_face != rhs.selected_face ||
      lhs.selected_mip != rhs.selected_mip ||
      lhs.texture_usage != rhs.texture_usage ||
      lhs.texture_is_loaded != rhs.texture_is_loaded ||
      lhs.texture_is_manual != rhs.texture_is_manual ||
      lhs.texture_hardware_gamma_enabled !=
          rhs.texture_hardware_gamma_enabled ||
      lhs.texture_resource_revision != rhs.texture_resource_revision ||
      lhs.tight_row_pitch_bytes != rhs.tight_row_pitch_bytes ||
      lhs.tight_slice_pitch_bytes != rhs.tight_slice_pitch_bytes ||
      lhs.mip_chain.size() != rhs.mip_chain.size() ||
      !SameSampling(lhs.sampling, rhs.sampling)) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.mip_chain.size(); ++index) {
    if (!SameMipObservation(lhs.mip_chain[index], rhs.mip_chain[index])) {
      return false;
    }
  }
  return true;
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

  void UpdateU32(std::uint32_t value) noexcept {
    std::array<std::uint8_t, 4U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    Update(bytes.data(), bytes.size());
  }

  void UpdateU64(std::uint64_t value) noexcept {
    std::array<std::uint8_t, 8U> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
      bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    Update(bytes.data(), bytes.size());
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
        digest[word * 4U + byte] =
            static_cast<std::uint8_t>(state_[word] >> ((3U - byte) * 8U));
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
                     (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block_[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t before = words[index - 15U];
      const std::uint32_t after = words[index - 2U];
      const std::uint32_t sigma0 =
          RotateRight(before, 7U) ^ RotateRight(before, 18U) ^ (before >> 3U);
      const std::uint32_t sigma1 =
          RotateRight(after, 17U) ^ RotateRight(after, 19U) ^ (after >> 10U);
      words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
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
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  }};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_ = 0U;
  std::size_t total_bytes_ = 0U;
};

std::array<std::uint8_t, 32U>
ComputeMipDigest(const Ogre14TerrainCompositeNativeMipObservation &mip,
                 const std::uint8_t *bytes, std::size_t byte_count) noexcept {
  Sha256 hasher;
  hasher.Update(reinterpret_cast<const std::uint8_t *>(
                    kOgre14TerrainCompositeMipDigestDomain),
                sizeof(kOgre14TerrainCompositeMipDigestDomain));
  hasher.UpdateU32(mip.mip_level);
  hasher.UpdateU32(mip.width);
  hasher.UpdateU32(mip.height);
  hasher.UpdateU64(static_cast<std::uint64_t>(byte_count));
  if (byte_count != 0U) {
    hasher.Update(bytes, byte_count);
  }
  return hasher.Final();
}

std::array<std::uint8_t, 32U>
ComputeMipDigest(const Ogre14TerrainCompositeNativeMipObservation &mip,
                 const std::vector<std::uint8_t> &bytes) noexcept {
  return ComputeMipDigest(mip, bytes.data(), bytes.size());
}

std::array<std::uint8_t, 32U> ComputeMipChainDigest(
    const std::vector<Ogre14TerrainCompositeMipMetadata> &metadata,
    std::uint64_t total_bytes) noexcept {
  Sha256 hasher;
  hasher.Update(reinterpret_cast<const std::uint8_t *>(
                    kOgre14TerrainCompositeMipChainDigestDomain),
                sizeof(kOgre14TerrainCompositeMipChainDigestDomain));
  hasher.UpdateU32(static_cast<std::uint32_t>(metadata.size()));
  hasher.UpdateU64(total_bytes);
  for (const Ogre14TerrainCompositeMipMetadata &mip : metadata) {
    hasher.UpdateU32(mip.mip_level);
    hasher.UpdateU32(mip.width);
    hasher.UpdateU32(mip.height);
    hasher.UpdateU64(mip.tight_slice_pitch_bytes);
    hasher.Update(mip.rgba_sha256.data(), mip.rgba_sha256.size());
  }
  return hasher.Final();
}

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
void MaybeInject(Ogre14TerrainCompositeCaptureStage stage,
                 IOgre14TerrainCompositeCaptureFaultInjector *injector) {
  if (injector != nullptr) {
    injector->BeforeTerrainCompositeCaptureStage(stage);
  }
}
#endif

Ogre14TerrainCompositeCaptureMetadata
BuildMetadata(const Ogre14TerrainCompositeNativeObservation &before,
              const Ogre14TerrainCompositeNativeObservation &after,
              const std::vector<std::vector<std::uint8_t>> &mip_bytes) {
  Ogre14TerrainCompositeCaptureMetadata metadata;
  metadata.terrain_group_pointer_token = before.terrain_group_pointer_token;
  metadata.terrain_slot_pointer_token = before.terrain_slot_pointer_token;
  metadata.terrain_pointer_token = before.terrain_pointer_token;
  metadata.packed_slot_key = before.packed_slot_key;
  metadata.slot_x = before.slot_x;
  metadata.slot_y = before.slot_y;
  metadata.exact_terrain_resource_group = before.exact_terrain_resource_group;
  metadata.exact_filename_prefix = before.exact_filename_prefix;
  metadata.exact_filename_extension = before.exact_filename_extension;
  metadata.page_definition_kind = before.page_definition_kind;
  metadata.exact_definition_filename = before.exact_definition_filename;
  metadata.definition_import_data_pointer_token =
      before.definition_import_data_pointer_token;
  metadata.generated_save_filename = before.generated_save_filename;
  metadata.exact_terrain_material_name = before.exact_terrain_material_name;
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
  metadata.exact_texture_resource_group = before.exact_texture_resource_group;
  metadata.exact_texture_name = before.exact_texture_name;
  metadata.pixel_encoding = before.pixel_encoding;
  metadata.texture_type = before.texture_type;
  metadata.texture_loading_state = before.texture_loading_state;
  metadata.texture_width = before.texture_width;
  metadata.texture_height = before.texture_height;
  metadata.texture_depth = before.texture_depth;
  metadata.texture_face_count = before.texture_face_count;
  metadata.texture_additional_mip_count = before.texture_additional_mip_count;
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
  metadata.rgb_transfer =
      before.texture_hardware_gamma_enabled
          ? Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER
          : Ogre14TerrainCompositeRgbTransfer::LEGACY_UNORM_DISPLAY_DOMAIN;
  metadata.sampling = before.sampling;

  metadata.mip_chain.reserve(before.mip_chain.size());
  for (std::size_t index = 0U; index < before.mip_chain.size(); ++index) {
    const Ogre14TerrainCompositeNativeMipObservation &native =
        before.mip_chain[index];
    Ogre14TerrainCompositeMipMetadata mip;
    mip.mip_level = native.mip_level;
    mip.pixel_buffer_pointer_token = native.pixel_buffer_pointer_token;
    mip.width = native.width;
    mip.height = native.height;
    mip.tight_row_pitch_bytes = native.tight_row_pitch_bytes;
    mip.tight_slice_pitch_bytes = native.tight_slice_pitch_bytes;
    mip.rgba_sha256 = ComputeMipDigest(native, mip_bytes[index]);
    metadata.full_mip_chain_rgba_byte_count += native.tight_slice_pitch_bytes;
    metadata.mip_chain.push_back(mip);
  }
  metadata.full_mip_chain_sha256 = ComputeMipChainDigest(
      metadata.mip_chain, metadata.full_mip_chain_rgba_byte_count);
  const Ogre14TerrainCompositeMipMetadata &level_zero =
      metadata.mip_chain.front();
  metadata.tight_row_pitch_bytes = level_zero.tight_row_pitch_bytes;
  metadata.tight_slice_pitch_bytes = level_zero.tight_slice_pitch_bytes;
  metadata.rgba_byte_count = level_zero.tight_slice_pitch_bytes;
  metadata.rgba_sha256 = level_zero.rgba_sha256;
  return metadata;
}

bool HasEveryV2Invariant(
    const Ogre14TerrainCompositeCaptureReceipt &receipt) noexcept {
  const Ogre14TerrainCompositeCaptureMetadata *const metadata_pointer =
      receipt.metadata();
  if (metadata_pointer == nullptr) {
    return false;
  }
  const Ogre14TerrainCompositeCaptureMetadata &metadata = *metadata_pointer;
  const Ogre14TerrainCompositeSamplingObservation &sampling =
      metadata.sampling;
  if (metadata.version != kOgre14TerrainCompositeCaptureReceiptVersion ||
      metadata.semantic_contract_version !=
          kOgre14TerrainCompositeSemanticContractVersion ||
      metadata.terrain_group_pointer_token == 0U ||
      metadata.terrain_slot_pointer_token == 0U ||
      metadata.terrain_pointer_token == 0U ||
      !IsIdentifier(metadata.exact_terrain_resource_group,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes) ||
      !IsIdentifier(metadata.exact_filename_prefix,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes, true) ||
      !IsIdentifier(metadata.exact_filename_extension,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes, true) ||
      !IsIdentifier(metadata.exact_definition_filename,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes, true) ||
      !IsIdentifier(metadata.generated_save_filename,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes) ||
      !IsIdentifier(metadata.exact_terrain_material_name,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes) ||
      !IsKnownPageDefinitionKind(metadata.page_definition_kind) ||
      (metadata.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED &&
       (metadata.exact_definition_filename.empty() ||
        metadata.definition_import_data_pointer_token != 0U)) ||
      (metadata.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::LIVE_IMPORT &&
       (!metadata.exact_definition_filename.empty() ||
        metadata.definition_import_data_pointer_token == 0U)) ||
      (metadata.page_definition_kind ==
           Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME &&
       (!metadata.exact_definition_filename.empty() ||
        metadata.definition_import_data_pointer_token != 0U)) ||
      !IsKnownAlignment(metadata.terrain_alignment) ||
      metadata.terrain_size < 2U ||
      !std::isfinite(metadata.terrain_world_size) ||
      metadata.terrain_world_size <= 0.0F ||
      !IsFinitePosition(metadata.terrain_world_position) ||
      !metadata.terrain_is_loaded ||
      metadata.terrain_derived_data_update_in_progress ||
      metadata.texture_pointer_token == 0U ||
      metadata.pixel_buffer_pointer_token == 0U ||
      metadata.texture_handle == 0U ||
      !IsIdentifier(metadata.exact_texture_resource_group,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes) ||
      !IsIdentifier(metadata.exact_texture_name,
                    kOgre14TerrainCompositeHardMaximumIdentifierBytes) ||
      metadata.pixel_encoding !=
          Ogre14TerrainCompositePixelEncoding::BYTE_RGBA ||
      metadata.texture_type != Ogre14TerrainCompositeTextureType::TEXTURE_2D ||
      metadata.texture_loading_state !=
          Ogre14TerrainCompositeTextureLoadingState::LOADED ||
      metadata.row_order != Ogre14TerrainCompositeRowOrder::
                                OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP ||
      metadata.channel_order !=
          Ogre14TerrainCompositeChannelOrder::RED_GREEN_BLUE_ALPHA ||
      metadata.rgb_semantic !=
          Ogre14TerrainCompositeRgbSemantic::BAKED_DIFFUSE ||
      metadata.alpha_semantic !=
          Ogre14TerrainCompositeAlphaSemantic::LINEAR_SPECULAR_MASK ||
      metadata.texture_width == 0U || metadata.texture_height == 0U ||
      metadata.texture_width > kOgre14TerrainCompositeHardMaximumDimension ||
      metadata.texture_height > kOgre14TerrainCompositeHardMaximumDimension ||
      metadata.texture_depth != 1U || metadata.texture_face_count != 1U ||
      metadata.selected_face != 0U || metadata.selected_mip != 0U ||
      !metadata.texture_is_loaded || !metadata.texture_is_manual ||
      metadata.texture_resource_revision_before_readback == 0U ||
      metadata.texture_resource_revision_before_readback ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      metadata.texture_resource_revision_before_readback !=
          metadata.texture_resource_revision_after_readback ||
      metadata.texture_additional_mip_count ==
          (std::numeric_limits<std::uint32_t>::max)() ||
      metadata.texture_mip_count !=
          metadata.texture_additional_mip_count + 1U ||
      metadata.texture_mip_count !=
          FullMipLevelCount(metadata.texture_width, metadata.texture_height) ||
      metadata.texture_mip_count >
          kOgre14TerrainCompositeHardMaximumMipLevels ||
      metadata.mip_chain.empty() ||
      metadata.mip_chain.size() != metadata.texture_mip_count ||
      receipt.mip_level_count() != metadata.texture_mip_count ||
      metadata.full_mip_chain_rgba_byte_count == 0U ||
      sampling.scene_manager_pointer_token == 0U ||
      sampling.texture_unit_pointer_token == 0U ||
      sampling.sampler_pointer_token == 0U ||
      sampling.bound_texture_pointer_token != metadata.texture_pointer_token ||
      !sampling.texture_unit_content_named ||
      sampling.texture_unit_frame_count != 1U ||
      sampling.texture_unit_current_frame != 0U ||
      !sampling.texture_unit_texture_2d || sampling.texture_unit_is_blank ||
      sampling.texture_unit_load_failing ||
      sampling.unordered_access_mip_level != -1 ||
      sampling.texture_coord_set != 0U ||
      !sampling.texcoord_calculation_none ||
      sampling.texture_effect_count != 0U ||
      !SameFloatBits(sampling.texture_u_scroll, 0.0F) ||
      !SameFloatBits(sampling.texture_v_scroll, 0.0F) ||
      !SameFloatBits(sampling.texture_u_scale, 1.0F) ||
      !SameFloatBits(sampling.texture_v_scale, 1.0F) ||
      !SameFloatBits(sampling.texture_rotation_radians, 0.0F) ||
      !SameFloatArrayBits(sampling.texture_transform,
                          IdentityTextureTransform()) ||
      sampling.address_u != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.address_v != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.address_w != Ogre14TerrainCompositeAddressMode::CLAMP ||
      sampling.min_filter != Ogre14TerrainCompositeFilter::LINEAR ||
      sampling.mag_filter != Ogre14TerrainCompositeFilter::LINEAR ||
      sampling.mip_filter != Ogre14TerrainCompositeFilter::POINT ||
      sampling.maximum_anisotropy != 1U ||
      !SameFloatBits(sampling.mipmap_bias, 0.0F) ||
      sampling.compare_enabled ||
      sampling.compare_function !=
          Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL ||
      !SameFloatArrayBits(
          sampling.border_colour,
          std::array<float, 4U>{{0.0F, 0.0F, 0.0F, 1.0F}}) ||
      sampling.texture_unit_hardware_gamma_enabled !=
          metadata.texture_hardware_gamma_enabled ||
      !IsKnownSceneFogMode(sampling.scene_fog_mode)) {
    return false;
  }

  std::uint64_t total_bytes = 0U;
  std::uint32_t expected_width = metadata.texture_width;
  std::uint32_t expected_height = metadata.texture_height;
  for (std::size_t index = 0U; index < metadata.mip_chain.size(); ++index) {
    const Ogre14TerrainCompositeMipMetadata &mip = metadata.mip_chain[index];
    std::uint64_t expected_row_pitch = 0U;
    std::uint64_t expected_slice_pitch = 0U;
    if (!TightRgbaLayout(expected_width, expected_height, expected_row_pitch,
                         expected_slice_pitch) ||
        mip.mip_level != index || mip.pixel_buffer_pointer_token == 0U ||
        mip.width != expected_width || mip.height != expected_height ||
        mip.tight_row_pitch_bytes != expected_row_pitch ||
        mip.tight_slice_pitch_bytes != expected_slice_pitch ||
        receipt.mip_rgba_size(index) != expected_slice_pitch ||
        receipt.mip_rgba_bytes(index) == nullptr ||
        !CheckedAddU64(total_bytes, expected_slice_pitch, total_bytes)) {
      return false;
    }
    for (std::size_t prior = 0U; prior < index; ++prior) {
      if (metadata.mip_chain[prior].pixel_buffer_pointer_token ==
          mip.pixel_buffer_pointer_token) {
        return false;
      }
    }
    Ogre14TerrainCompositeNativeMipObservation digest_identity;
    digest_identity.mip_level = mip.mip_level;
    digest_identity.width = mip.width;
    digest_identity.height = mip.height;
    if (ComputeMipDigest(digest_identity, receipt.mip_rgba_bytes(index),
                         receipt.mip_rgba_size(index)) != mip.rgba_sha256) {
      return false;
    }
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }

  const Ogre14TerrainCompositeMipMetadata &level_zero =
      metadata.mip_chain.front();
  if (total_bytes != metadata.full_mip_chain_rgba_byte_count ||
      total_bytes > kOgre14TerrainCompositeHardMaximumRgbaBytes ||
      ComputeMipChainDigest(metadata.mip_chain, total_bytes) !=
          metadata.full_mip_chain_sha256 ||
      metadata.pixel_buffer_pointer_token !=
          level_zero.pixel_buffer_pointer_token ||
      metadata.tight_row_pitch_bytes != level_zero.tight_row_pitch_bytes ||
      metadata.tight_slice_pitch_bytes != level_zero.tight_slice_pitch_bytes ||
      metadata.rgba_byte_count != level_zero.tight_slice_pitch_bytes ||
      metadata.rgba_sha256 != level_zero.rgba_sha256 ||
      receipt.rgba_bytes() != receipt.mip_rgba_bytes(0U) ||
      receipt.rgba_size() != receipt.mip_rgba_size(0U)) {
    return false;
  }

  const Ogre14TerrainCompositeRgbTransfer expected_transfer =
      metadata.texture_hardware_gamma_enabled
          ? Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER
          : Ogre14TerrainCompositeRgbTransfer::LEGACY_UNORM_DISPLAY_DOMAIN;
  return metadata.rgb_transfer == expected_transfer;
}

float DecodeSrgb(std::uint8_t encoded) noexcept {
  const float value = static_cast<float>(encoded) / 255.0F;
  if (value <= 0.04045F) {
    return value / 12.92F;
  }
  return std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float Lerp(float lhs, float rhs, float amount) noexcept {
  return lhs + (rhs - lhs) * amount;
}

} // namespace

struct Ogre14TerrainCompositeCaptureReceipt::State final {
  Ogre14TerrainCompositeCaptureMetadata metadata;
  std::vector<std::vector<std::uint8_t>> mip_rgba_bytes;
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
  return mip_rgba_bytes(0U);
}

std::size_t Ogre14TerrainCompositeCaptureReceipt::rgba_size() const noexcept {
  return mip_rgba_size(0U);
}

std::size_t
Ogre14TerrainCompositeCaptureReceipt::mip_level_count() const noexcept {
  return state_ != nullptr ? state_->mip_rgba_bytes.size() : 0U;
}

const std::uint8_t *Ogre14TerrainCompositeCaptureReceipt::mip_rgba_bytes(
    std::size_t mip_level) const noexcept {
  if (state_ == nullptr || mip_level >= state_->mip_rgba_bytes.size() ||
      state_->mip_rgba_bytes[mip_level].empty()) {
    return nullptr;
  }
  return state_->mip_rgba_bytes[mip_level].data();
}

std::size_t Ogre14TerrainCompositeCaptureReceipt::mip_rgba_size(
    std::size_t mip_level) const noexcept {
  return state_ != nullptr && mip_level < state_->mip_rgba_bytes.size()
             ? state_->mip_rgba_bytes[mip_level].size()
             : 0U;
}

bool Ogre14TerrainCompositeCaptureReceipt::SharesImmutableStateWith(
    const Ogre14TerrainCompositeCaptureReceipt &other) const noexcept {
  return state_ != nullptr && other.state_ != nullptr &&
         state_.get() == other.state_.get() &&
         !state_.owner_before(other.state_) &&
         !other.state_.owner_before(state_);
}

ValidationResult EvaluateOgre14TerrainCompositeBilinearOracle(
    Ogre14TerrainCompositeRgbTransfer transfer,
    const std::array<Ogre14TerrainCompositeOracleTexel, 4U> &texels,
    float u_fraction, float v_fraction,
    Ogre14TerrainCompositeOracleSample &sample) {
  if ((transfer != Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER &&
       transfer !=
           Ogre14TerrainCompositeRgbTransfer::LEGACY_UNORM_DISPLAY_DOMAIN) ||
      !std::isfinite(u_fraction) || !std::isfinite(v_fraction) ||
      u_fraction < 0.0F || u_fraction > 1.0F || v_fraction < 0.0F ||
      v_fraction > 1.0F) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.oracle.coordinates_or_transfer",
                   "oracle transfer must be known and bilinear fractions must "
                   "be finite in [0,1]");
  }
  Ogre14TerrainCompositeOracleSample candidate;
  for (std::size_t channel = 0U; channel < 4U; ++channel) {
    std::array<float, 4U> values{};
    for (std::size_t texel = 0U; texel < values.size(); ++texel) {
      if (channel < 3U &&
          transfer == Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER) {
        values[texel] = DecodeSrgb(texels[texel].rgba[channel]);
      } else {
        values[texel] =
            static_cast<float>(texels[texel].rgba[channel]) / 255.0F;
      }
    }
    const float top = Lerp(values[0U], values[1U], u_fraction);
    const float bottom = Lerp(values[2U], values[3U], u_fraction);
    candidate.rgba[channel] = Lerp(top, bottom, v_fraction);
  }
  sample = candidate;
  return ValidationResult::Success();
}

ValidationResult Ogre14TerrainCompositeNativeAdapter::ValidateCaptureInputs(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &observation) {
  ValidationResult validation = ValidateConfiguration(configuration);
  return validation ? ValidateObservation(configuration, observation)
                    : validation;
}

ValidationResult
Ogre14TerrainCompositeNativeAdapter::ValidateCaptureConfiguration(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration) {
  return ValidateConfiguration(configuration);
}

ValidationResult Ogre14TerrainCompositeNativeAdapter::PublishOwnedReadback(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before_readback,
    std::vector<std::vector<std::uint8_t>> mip_rgba_bytes,
    const Ogre14TerrainCompositeNativeObservation &after_readback,
    Ogre14TerrainCompositeCaptureReceipt &receipt) {
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
                     "terrain, full mip chain, texture-unit binding, UV, "
                     "sampler, direct fog, gamma, or resource revision "
                     "changed during readback");
    }
    if (mip_rgba_bytes.size() != before_readback.mip_chain.size()) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "terrain_composite.readback.full_mip_chain",
                     "native readback did not return every observed mip level");
    }
    for (std::size_t index = 0U; index < mip_rgba_bytes.size(); ++index) {
      if (mip_rgba_bytes[index].size() !=
          before_readback.mip_chain[index].tight_slice_pitch_bytes) {
        return Failure(
            ValidationCode::SIZE_MISMATCH,
            "terrain_composite.readback.mip_rgba_bytes",
            "native mip readback byte count differs from its tight RGBA slice");
      }
    }

    auto candidate =
        std::make_shared<Ogre14TerrainCompositeCaptureReceipt::State>();
    candidate->metadata =
        BuildMetadata(before_readback, after_readback, mip_rgba_bytes);
    candidate->mip_rgba_bytes = std::move(mip_rgba_bytes);
    Ogre14TerrainCompositeCaptureReceipt candidate_receipt(candidate);
    if (!HasEveryV2Invariant(candidate_receipt)) {
      return Failure(ValidationCode::UNSUPPORTED_VERSION,
                     "terrain_composite.capture.seal_transport_v2",
                     "adapter-minted immutable owner failed final V2 digest "
                     "and transport validation");
    }
    receipt = std::move(candidate_receipt);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(
        ValidationCode::EMPTY_PAYLOAD, "terrain_composite.capture.allocation",
        "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "terrain_composite.capture.unexpected_exception",
        "unexpected exception before terrain composite receipt publication");
  }
}

#if defined(ROR_OGRE14_TERRAIN_COMPOSITE_CAPTURE_INTERNAL_TESTING)
ValidationResult
Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before_readback,
    const std::vector<std::vector<std::uint8_t>> &mip_rgba_bytes,
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
    std::vector<std::vector<std::uint8_t>> owned_bytes = mip_rgba_bytes;
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_RGBA_ALLOCATION,
                fault_injector);
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_READBACK,
                fault_injector);
    MaybeInject(Ogre14TerrainCompositeCaptureStage::BEFORE_RECEIPT_PUBLICATION,
                fault_injector);
    return PublishOwnedReadback(configuration, before_readback,
                                std::move(owned_bytes), after_readback,
                                receipt);
  } catch (const std::bad_alloc &) {
    return Failure(
        ValidationCode::EMPTY_PAYLOAD, "terrain_composite.capture.allocation",
        "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "terrain_composite.capture.unexpected_exception",
        "unexpected exception before terrain composite receipt publication");
  }
}
#endif

ValidationResult LowerOgre14TerrainCompositeOpaque(
    const Ogre14TerrainCompositeCaptureReceipt &receipt,
    Ogre14TerrainCompositeOpaqueLowering &lowering) {
  try {
    if (!receipt.initialized() || receipt.metadata() == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.lowering.receipt",
                     "opaque lowering requires an initialized V2 receipt");
    }
    if (!HasEveryV2Invariant(receipt)) {
      return Failure(
          ValidationCode::UNSUPPORTED_VERSION,
          "terrain_composite.lowering.transport_v2_invariants",
          "opaque lowering requires every V2 transport, mip, binding, UV, "
          "sampler, gamma, alpha-evidence, and direct-fog invariant");
    }
    const Ogre14TerrainCompositeCaptureMetadata &metadata =
        *receipt.metadata();
    if (metadata.sampling.scene_fog_mode !=
        Ogre14TerrainCompositeSceneFogMode::FOG_NONE) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "terrain_composite.lowering.scene_fog_mode",
                     "opaque terrain composite lowering admits direct "
                     "SceneManager FOG_NONE only");
    }
    Ogre14TerrainCompositeOpaqueLowering candidate;
    candidate.rgb_transfer = metadata.rgb_transfer;
    candidate.mip_chain.reserve(metadata.mip_chain.size());
    for (std::size_t level = 0U; level < metadata.mip_chain.size(); ++level) {
      const Ogre14TerrainCompositeMipMetadata &source_metadata =
          metadata.mip_chain[level];
      const std::uint8_t *const source_bytes =
          receipt.mip_rgba_bytes(level);
      const std::size_t source_size = receipt.mip_rgba_size(level);
      Ogre14TerrainCompositeOpaqueMip mip;
      mip.mip_level = source_metadata.mip_level;
      mip.width = source_metadata.width;
      mip.height = source_metadata.height;
      mip.tight_row_pitch_bytes = source_metadata.tight_row_pitch_bytes;
      mip.tight_slice_pitch_bytes = source_metadata.tight_slice_pitch_bytes;
      mip.rgba_bytes.assign(source_bytes, source_bytes + source_size);
      for (std::size_t alpha = 3U; alpha < mip.rgba_bytes.size(); alpha += 4U) {
        mip.rgba_bytes[alpha] = 255U;
      }
      candidate.mip_chain.push_back(std::move(mip));
    }

    candidate.sampler.debug_name = "ogre14-terrain-composite-sampler";
    candidate.sampler.minification_filter = SamplerFilter::LINEAR;
    candidate.sampler.magnification_filter = SamplerFilter::LINEAR;
    candidate.sampler.mip_filter = SamplerFilter::NEAREST;
    candidate.sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
    candidate.sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
    candidate.sampler.address_w = SamplerAddressMode::CLAMP_TO_EDGE;
    candidate.sampler.mip_lod_bias = 0.0F;
    candidate.sampler.minimum_lod = 0.0F;
    candidate.sampler.maximum_lod =
        static_cast<float>(candidate.mip_chain.size() - 1U);
    candidate.sampler.anisotropy_enabled = false;
    candidate.sampler.maximum_anisotropy = 1.0F;
    candidate.sampler.compare_enabled = false;
    candidate.sampler.compare_operation = SamplerCompareOperation::ALWAYS;
    // Source authority retains OGRE Black {+0,+0,+0,+1}. CLAMP_TO_EDGE never
    // samples a border, so the portable descriptor uses its canonical inert
    // Float4{} value without claiming the source alpha was zero.
    candidate.sampler.border_color = {};
    ValidationResult validation =
        ValidateSamplerResourceDescriptor(candidate.sampler);
    if (!validation) {
      return validation;
    }
    lowering = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "terrain_composite.lowering.allocation",
                   "allocation failed before opaque lowering publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.lowering.unexpected_exception",
                   "unexpected exception before opaque lowering publication");
  }
}

} // namespace RoR::Render
