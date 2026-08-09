/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace RoR::Render::Testing {

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::Capture(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before,
    const void *bytes, std::size_t byte_count,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, before, bytes, byte_count, after, receipt,
      fault_injector);
}

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Ogre14TerrainCompositeNativeObservation CanonicalObservation() {
  Ogre14TerrainCompositeNativeObservation observation;
  observation.terrain_group_pointer_token = 0x100U;
  observation.terrain_slot_pointer_token = 0x180U;
  observation.terrain_pointer_token = 0x200U;
  observation.packed_slot_key = 0x0001ffffU;
  observation.slot_x = 1;
  observation.slot_y = -1;
  observation.exact_terrain_resource_group = "CityWorld";
  observation.exact_filename_prefix = "cityworld-page";
  observation.exact_filename_extension = "dat";
  observation.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED;
  observation.exact_definition_filename = "cityworld-import.dat";
  observation.generated_save_filename = "cityworld-page_0001ffff.dat";
  observation.exact_terrain_material_name = "CityWorld/Terrain/1/-1";
  observation.terrain_alignment = Ogre14TerrainCompositeAlignment::X_Z;
  observation.terrain_size = 1025U;
  observation.terrain_world_size = 12000.0F;
  observation.terrain_world_position = {12000.0F, 0.0F, -12000.0F};
  observation.terrain_is_loaded = true;
  observation.terrain_derived_data_update_in_progress = false;
  observation.texture_pointer_token = 0x300U;
  observation.pixel_buffer_pointer_token = 0x400U;
  observation.texture_handle = 55U;
  observation.exact_texture_resource_group = "CityWorldDerived";
  observation.exact_texture_name = "CityWorld/Terrain/1/-1/comp";
  observation.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  observation.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  observation.texture_width = 2U;
  observation.texture_height = 2U;
  observation.texture_depth = 1U;
  observation.texture_face_count = 1U;
  observation.texture_mip_count = 2U;
  observation.selected_face = 0U;
  observation.selected_mip = 0U;
  observation.texture_usage = 0x20U;
  observation.texture_is_loaded = true;
  observation.texture_is_manual = true;
  observation.texture_hardware_gamma_enabled = true;
  observation.texture_resource_revision = 7U;
  observation.tight_row_pitch_bytes = 8U;
  observation.tight_slice_pitch_bytes = 16U;
  return observation;
}

constexpr std::array<std::uint8_t, 16U> kRgbaBytes{{
    1U,  2U,  3U,  4U,  5U,  6U,  7U,  8U,
    9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
}};

constexpr std::array<std::uint8_t, 32U> kExpectedSha256{{
    0x5dU, 0xfbU, 0xabU, 0xeeU, 0xdfU, 0x31U, 0x8bU, 0xf3U,
    0x3cU, 0x09U, 0x27U, 0xc4U, 0x3dU, 0x76U, 0x30U, 0xf5U,
    0x1bU, 0x82U, 0xf3U, 0x51U, 0x74U, 0x03U, 0x01U, 0x35U,
    0x4fU, 0xa3U, 0xd7U, 0xfcU, 0x51U, 0xf0U, 0x13U, 0x2eU,
}};

Ogre14TerrainCompositeCaptureReceipt CaptureCanonical() {
  Ogre14TerrainCompositeCaptureReceipt receipt;
  const Ogre14TerrainCompositeNativeObservation observation =
      CanonicalObservation();
  const ValidationResult result =
      RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::Capture(
          {}, observation, kRgbaBytes.data(), kRgbaBytes.size(), observation,
          receipt);
  Require(result.ok(), "canonical composite capture failed");
  return receipt;
}

template <typename Mutation>
void RequireMutationRejected(const Ogre14TerrainCompositeCaptureReceipt &owner,
                             Mutation mutate,
                             const char *message) {
  Ogre14TerrainCompositeNativeObservation before = CanonicalObservation();
  Ogre14TerrainCompositeNativeObservation after = before;
  mutate(after);
  Ogre14TerrainCompositeCaptureReceipt output = owner;
  const auto *const metadata_before = output.metadata();
  const std::uint8_t *const bytes_before = output.rgba_bytes();
  const ValidationResult result =
      RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::Capture(
          {}, before, kRgbaBytes.data(), kRgbaBytes.size(), after, output);
  Require(!result.ok(), message);
  Require(output.SharesImmutableStateWith(owner),
          "failed revalidation replaced receipt owner");
  Require(output.metadata() == metadata_before &&
              output.rgba_bytes() == bytes_before,
          "failed revalidation changed deep owner pointers");
}

class ThrowingInjector final
    : public IOgre14TerrainCompositeCaptureFaultInjector {
public:
  Ogre14TerrainCompositeCaptureStage target =
      Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE;
  bool throw_bad_alloc = false;
  std::size_t calls = 0U;

  void BeforeTerrainCompositeCaptureStage(
      Ogre14TerrainCompositeCaptureStage stage) override {
    ++calls;
    if (stage != target) {
      return;
    }
    if (throw_bad_alloc) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void CheckCanonicalReceipt() {
  const Ogre14TerrainCompositeCaptureReceipt receipt = CaptureCanonical();
  Require(receipt.initialized() && receipt.metadata() != nullptr,
          "canonical receipt is empty");
  const Ogre14TerrainCompositeCaptureMetadata &metadata =
      *receipt.metadata();
  Require(metadata.version ==
              kOgre14TerrainCompositeCaptureReceiptVersion &&
              metadata.semantic_contract_version ==
                  kOgre14TerrainCompositeSemanticContractVersion,
          "receipt version changed");
  Require(metadata.terrain_group_pointer_token == 0x100U &&
              metadata.terrain_slot_pointer_token == 0x180U &&
              metadata.terrain_pointer_token == 0x200U &&
              metadata.packed_slot_key == 0x0001ffffU &&
              metadata.slot_x == 1 && metadata.slot_y == -1 &&
              metadata.page_definition_kind ==
                  Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED &&
              metadata.exact_definition_filename ==
                  "cityworld-import.dat" &&
              metadata.definition_import_data_pointer_token == 0U &&
              metadata.generated_save_filename ==
                  "cityworld-page_0001ffff.dat" &&
              metadata.terrain_is_loaded &&
              !metadata.terrain_derived_data_update_in_progress,
          "TerrainGroup slot identity was not retained");
  Require(metadata.texture_pointer_token == 0x300U &&
              metadata.pixel_buffer_pointer_token == 0x400U &&
              metadata.texture_handle == 55U &&
              metadata.texture_resource_revision_before_readback == 7U &&
              metadata.texture_resource_revision_after_readback == 7U &&
              metadata.texture_type ==
                  Ogre14TerrainCompositeTextureType::TEXTURE_2D &&
              metadata.texture_loading_state ==
                  Ogre14TerrainCompositeTextureLoadingState::LOADED,
          "texture pointer, handle, or stable revision was not retained");
  Require(metadata.texture_width == 2U && metadata.texture_height == 2U &&
              metadata.texture_depth == 1U &&
              metadata.texture_face_count == 1U &&
              metadata.texture_mip_count == 2U &&
              metadata.selected_face == 0U &&
              metadata.selected_mip == 0U &&
              metadata.tight_row_pitch_bytes == 8U &&
              metadata.tight_slice_pitch_bytes == 16U,
          "texture layout metadata changed");
  Require(metadata.pixel_encoding ==
              Ogre14TerrainCompositePixelEncoding::BYTE_RGBA &&
              metadata.row_order ==
                  Ogre14TerrainCompositeRowOrder::
                      OGRE_PIXELBOX_ROW_ZERO_FIRST_NO_FLIP &&
              metadata.channel_order ==
                  Ogre14TerrainCompositeChannelOrder::
                      RED_GREEN_BLUE_ALPHA,
          "channel or row-order contract changed");
  Require(metadata.alpha_semantic ==
              Ogre14TerrainCompositeAlphaSemantic::
                  LINEAR_SPECULAR_MASK &&
              metadata.material_lowering_status ==
                  Ogre14TerrainCompositeMaterialLoweringStatus::
                      BLOCKED_ALPHA_IS_SPECULAR_MASK,
          "terrain composite alpha was misclassified as coverage");
  Require(receipt.rgba_size() == kRgbaBytes.size() &&
              std::equal(kRgbaBytes.begin(), kRgbaBytes.end(),
                         receipt.rgba_bytes()),
          "lossless RGBA bytes changed");
  Require(std::equal(kRgbaBytes.begin(), kRgbaBytes.begin() + 8U,
                     receipt.rgba_bytes()) &&
              std::equal(kRgbaBytes.begin() + 8U, kRgbaBytes.end(),
                         receipt.rgba_bytes() + 8U),
          "readback rows were flipped or repacked");
  Require(receipt.rgba_bytes()[3U] == 4U &&
              receipt.rgba_bytes()[7U] == 8U &&
              receipt.rgba_bytes()[11U] == 12U &&
              receipt.rgba_bytes()[15U] == 16U,
          "specular-mask alpha bytes were discarded");
  Require(metadata.rgba_sha256 == kExpectedSha256,
          "retained RGBA SHA-256 changed");

  Ogre14TerrainCompositeCaptureReceipt copy = receipt;
  Require(copy.SharesImmutableStateWith(receipt),
          "copy lost immutable owner identity");
  const Ogre14TerrainCompositeCaptureReceipt independent =
      CaptureCanonical();
  Require(!independent.SharesImmutableStateWith(receipt),
          "separately minted receipt shared authority");

  const ValidationResult lowering =
      ValidateOgre14TerrainCompositeMaterialDescriptorLowering(receipt);
  Require(lowering.code == ValidationCode::UNSUPPORTED_FEATURE &&
              lowering.field ==
                  "terrain_composite.material_descriptor.alpha_specular_mask",
          "UNLIT/PBR lowering did not return the exact semantic blocker");
}

void CheckRevalidationAndCaps() {
  const Ogre14TerrainCompositeCaptureReceipt owner = CaptureCanonical();
  RequireMutationRejected(owner,
                          [](auto &value) {
                            ++value.texture_resource_revision;
                          },
                          "stale texture revision was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            ++value.texture_pointer_token;
                          },
                          "substituted texture pointer was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            ++value.pixel_buffer_pointer_token;
                          },
                          "substituted pixel-buffer pointer was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            ++value.terrain_slot_pointer_token;
                          },
                          "substituted Terrain slot pointer was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            ++value.terrain_pointer_token;
                          },
                          "substituted Terrain pointer was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) { ++value.texture_handle; },
                          "substituted texture handle was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.exact_texture_name += ".replacement";
                          },
                          "name-only texture substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.exact_texture_resource_group = "Other";
                          },
                          "texture group substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) { ++value.packed_slot_key; },
                          "TerrainGroup page substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.exact_definition_filename += ".replacement";
                          },
                          "Terrain page definition substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.generated_save_filename += ".replacement";
                          },
                          "Terrain generated save identity substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.terrain_world_position[0U] += 1.0F;
                          },
                          "Terrain transform substitution was accepted");
  RequireMutationRejected(owner,
                          [](auto &value) {
                            value.texture_hardware_gamma_enabled = false;
                          },
                          "hardware-gamma state substitution was accepted");

  Ogre14TerrainCompositeNativeObservation consumed_import =
      CanonicalObservation();
  consumed_import.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  consumed_import.exact_definition_filename.clear();
  Ogre14TerrainCompositeCaptureReceipt consumed_receipt;
  const ValidationResult consumed_result =
      RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::Capture(
          {}, consumed_import, kRgbaBytes.data(), kRgbaBytes.size(),
          consumed_import, consumed_receipt);
  Require(consumed_result.ok() && consumed_receipt.initialized() &&
              consumed_receipt.metadata()->page_definition_kind ==
                  Ogre14TerrainCompositePageDefinitionKind::
                      CONSUMED_OR_RUNTIME &&
              consumed_receipt.metadata()->exact_definition_filename.empty() &&
              consumed_receipt.metadata()->generated_save_filename ==
                  "cityworld-page_0001ffff.dat",
          "consumed import was mislabeled as a file-backed page source");

  const auto capture = [&](const Ogre14TerrainCompositeCaptureConfiguration
                               &configuration,
                           Ogre14TerrainCompositeNativeObservation before,
                           const void *bytes, std::size_t byte_count) {
    Ogre14TerrainCompositeCaptureReceipt output = owner;
    const auto *const metadata_before = output.metadata();
    const std::uint8_t *const bytes_before = output.rgba_bytes();
    const ValidationResult result =
        RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::
            Capture(configuration, before, bytes, byte_count, before, output);
    Require(!result.ok(), "hostile capture unexpectedly succeeded");
    Require(output.SharesImmutableStateWith(owner) &&
                output.metadata() == metadata_before &&
                output.rgba_bytes() == bytes_before,
            "hostile capture changed deep receipt owner");
    return result;
  };

  Ogre14TerrainCompositeCaptureConfiguration dimension_cap;
  dimension_cap.maximum_dimension = 1U;
  Require(capture(dimension_cap, CanonicalObservation(), kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "dimension cap returned the wrong failure");
  Ogre14TerrainCompositeCaptureConfiguration byte_cap;
  byte_cap.maximum_rgba_bytes = 15U;
  Require(capture(byte_cap, CanonicalObservation(), kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "decoded byte cap returned the wrong failure");
  Ogre14TerrainCompositeCaptureConfiguration identifier_cap;
  identifier_cap.maximum_identifier_bytes = 4U;
  Require(capture(identifier_cap, CanonicalObservation(), kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::INVALID_IDENTIFIER,
          "identifier cap returned the wrong failure");
  Require(capture({}, CanonicalObservation(), kRgbaBytes.data(),
                  kRgbaBytes.size() - 1U)
              .code == ValidationCode::SIZE_MISMATCH,
          "truncated readback returned the wrong failure");
  Require(capture({}, CanonicalObservation(), nullptr, kRgbaBytes.size())
              .code == ValidationCode::EMPTY_PAYLOAD,
          "null readback returned the wrong failure");

  Ogre14TerrainCompositeNativeObservation zero_revision =
      CanonicalObservation();
  zero_revision.texture_resource_revision = 0U;
  Require(capture({}, zero_revision, kRgbaBytes.data(), kRgbaBytes.size())
              .code == ValidationCode::REVISION_MISMATCH,
          "zero resource revision returned the wrong failure");
  Ogre14TerrainCompositeNativeObservation missing_definition =
      CanonicalObservation();
  missing_definition.exact_definition_filename.clear();
  Require(capture({}, missing_definition, kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::INVALID_ASSET_REFERENCE,
          "file-backed page without a definition filename was accepted");
  Ogre14TerrainCompositeNativeObservation updating_terrain =
      CanonicalObservation();
  updating_terrain.terrain_derived_data_update_in_progress = true;
  Require(capture({}, updating_terrain, kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::INVALID_DIMENSIONS,
          "updating Terrain page returned the wrong failure");
  Ogre14TerrainCompositeNativeObservation invalid_texture_state =
      CanonicalObservation();
  invalid_texture_state.texture_loading_state =
      static_cast<Ogre14TerrainCompositeTextureLoadingState>(255U);
  Require(capture({}, invalid_texture_state, kRgbaBytes.data(),
                  kRgbaBytes.size())
              .code == ValidationCode::UNSUPPORTED_FEATURE,
          "non-loaded texture state returned the wrong failure");
  Ogre14TerrainCompositeNativeObservation bad_layout =
      CanonicalObservation();
  ++bad_layout.tight_row_pitch_bytes;
  Require(capture({}, bad_layout, kRgbaBytes.data(), kRgbaBytes.size())
              .code == ValidationCode::SIZE_MISMATCH,
          "padded-row claim returned the wrong failure");
  Ogre14TerrainCompositeNativeObservation overflow =
      CanonicalObservation();
  overflow.texture_width = (std::numeric_limits<std::uint32_t>::max)();
  overflow.texture_height = (std::numeric_limits<std::uint32_t>::max)();
  Require(capture({}, overflow, kRgbaBytes.data(), kRgbaBytes.size())
              .code == ValidationCode::SIZE_MISMATCH,
          "RGBA layout overflow returned the wrong failure");
}

void CheckExceptionRollback() {
  const Ogre14TerrainCompositeCaptureReceipt owner = CaptureCanonical();
  const std::array<Ogre14TerrainCompositeCaptureStage, 4U> stages{{
      Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE,
      Ogre14TerrainCompositeCaptureStage::AFTER_RGBA_ALLOCATION,
      Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_READBACK,
      Ogre14TerrainCompositeCaptureStage::BEFORE_RECEIPT_PUBLICATION,
  }};
  for (const Ogre14TerrainCompositeCaptureStage stage : stages) {
    for (const bool bad_alloc : {false, true}) {
      ThrowingInjector injector;
      injector.target = stage;
      injector.throw_bad_alloc = bad_alloc;
      Ogre14TerrainCompositeCaptureReceipt output = owner;
      const auto *const metadata_before = output.metadata();
      const std::uint8_t *const bytes_before = output.rgba_bytes();
      const Ogre14TerrainCompositeNativeObservation observation =
          CanonicalObservation();
      const ValidationResult result =
          RoR::Render::Testing::Ogre14TerrainCompositeCaptureTestAccess::
              Capture({}, observation, kRgbaBytes.data(), kRgbaBytes.size(),
                      observation, output, &injector);
      Require(result.code == (bad_alloc ? ValidationCode::EMPTY_PAYLOAD
                                        : ValidationCode::UNSUPPORTED_FEATURE),
              "fault injection returned the wrong failure class");
      Require(injector.calls != 0U,
              "fault injector was not reached");
      Require(output.SharesImmutableStateWith(owner) &&
                  output.metadata() == metadata_before &&
                  output.rgba_bytes() == bytes_before,
              "exception path changed deep receipt owner");
    }
  }
}

} // namespace

int main() {
  static_assert(
      std::is_nothrow_default_constructible<
          Ogre14TerrainCompositeCaptureReceipt>::value,
      "empty receipt construction must be nonthrowing");
  static_assert(
      std::is_nothrow_copy_constructible<
          Ogre14TerrainCompositeCaptureReceipt>::value,
      "receipt copies must preserve immutable authority without allocation");
  static_assert(
      std::is_nothrow_move_assignable<
          Ogre14TerrainCompositeCaptureReceipt>::value,
      "receipt publication must be nonthrowing");

  Ogre14TerrainCompositeCaptureReceipt empty;
  Require(!empty.initialized() && empty.metadata() == nullptr &&
              empty.rgba_bytes() == nullptr && empty.rgba_size() == 0U,
          "default receipt is not empty");
  Require(ValidateOgre14TerrainCompositeMaterialDescriptorLowering(empty)
              .code == ValidationCode::MISSING_REFERENCE,
          "empty receipt did not fail material lowering");

  CheckCanonicalReceipt();
  CheckRevalidationAndCaps();
  CheckExceptionRollback();
  std::cout << "OGRE 14 terrain composite capture receipt tests passed\n";
  return EXIT_SUCCESS;
}
