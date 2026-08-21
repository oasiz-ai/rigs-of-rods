/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic renderer-neutral decoder for authenticated source textures.

#pragma once

#include "RenderValidation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14SourceTextureDecodeOptionsVersion = 1U;
constexpr std::uint32_t kOgre14DecodedSourceTextureVersion = 1U;
constexpr std::uint32_t kOgre14DecodedSourceTextureMipVersion = 1U;

constexpr std::uint32_t kOgre14SourceTextureHardMaximumDimension = 16384U;
constexpr std::uint32_t kOgre14SourceTextureHardMaximumMipLevels = 32U;
/// PNG/JPEG decoding is deliberately narrower than the legacy DDS cap and
/// matches the audited CityWorld/Alexis source-content envelope.
constexpr std::uint32_t kOgre14SourceImageCodecMaximumDimension = 8192U;
constexpr std::uint32_t kOgre14SourceImageCodecMaximumPngChunks = 65536U;
constexpr std::uint64_t kOgre14SourceTextureHardMaximumEncodedBytes =
    512U * 1024U * 1024U;
constexpr std::uint64_t kOgre14SourceTextureHardMaximumDecodedBytes =
    1024U * 1024U * 1024U;

/// DDS does not reliably describe transfer functions. The authoritative
/// content/material layer must provide this semantic; the decoder never
/// guesses it from a filename, FourCC, pixel masks, or payload bytes.
enum class Ogre14SourceTextureColorSemantic : std::uint8_t {
  UNSPECIFIED = 0U,
  SRGB_COLOR = 1U,
  LINEAR_DATA = 2U,
};

/// DXT1/BC1 has two legitimate interpretations when endpoint zero is not
/// greater than endpoint one. Callers must select the authored interpretation.
enum class Ogre14SourceTextureBc1AlphaMode : std::uint8_t {
  NOT_APPLICABLE = 0U,
  OPAQUE_COLOR = 1U,
  ONE_BIT_ALPHA = 2U,
};

enum class Ogre14SourceTextureFormat : std::uint8_t {
  BC1_UNORM = 0U,
  BC2_UNORM = 1U,
  BC3_UNORM = 2U,
  BC4_UNORM = 3U,
  BC5_UNORM = 4U,
  RGBA8_UNORM = 5U,
  RGBX8_UNORM = 6U,
  BGRA8_UNORM = 7U,
  BGRX8_UNORM = 8U,
};

struct Ogre14SourceTextureDecodeOptions final {
  std::uint32_t version = kOgre14SourceTextureDecodeOptionsVersion;
  Ogre14SourceTextureColorSemantic color_semantic =
      Ogre14SourceTextureColorSemantic::UNSPECIFIED;
  Ogre14SourceTextureBc1AlphaMode bc1_alpha_mode =
      Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  std::uint32_t maximum_dimension =
      kOgre14SourceTextureHardMaximumDimension;
  std::uint32_t maximum_mip_levels =
      kOgre14SourceTextureHardMaximumMipLevels;
  std::uint64_t maximum_encoded_bytes =
      kOgre14SourceTextureHardMaximumEncodedBytes;
  std::uint64_t maximum_decoded_bytes =
      kOgre14SourceTextureHardMaximumDecodedBytes;
};

struct Ogre14DecodedSourceTextureMip final {
  std::uint32_t version = kOgre14DecodedSourceTextureMipVersion;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  /// Canonical rows are always tightly packed width * 4 bytes.
  std::uint64_t row_pitch_bytes = 0U;
  /// Canonical slices are always tightly packed row_pitch_bytes * height.
  std::uint64_t slice_pitch_bytes = 0U;
  /// Canonical R, G, B, A bytes in texel row-major order. Values are UNORM;
  /// `color_semantic` on the parent remains separate metadata.
  std::vector<std::uint8_t> rgba8_unorm;
};

struct Ogre14DecodedSourceTexture final {
  std::uint32_t version = kOgre14DecodedSourceTextureVersion;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  Ogre14SourceTextureFormat source_format =
      Ogre14SourceTextureFormat::RGBA8_UNORM;
  Ogre14SourceTextureColorSemantic color_semantic =
      Ogre14SourceTextureColorSemantic::UNSPECIFIED;
  Ogre14SourceTextureBc1AlphaMode bc1_alpha_mode =
      Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  bool source_has_alpha = false;
  std::vector<Ogre14DecodedSourceTextureMip> mip_levels;
};

enum class Ogre14SourceTextureDecoderFaultStage : std::uint8_t {
  AFTER_HEADER_VALIDATION = 0U,
  AFTER_FIRST_MIP_DECODE = 1U,
  BEFORE_COMMIT = 2U,
};

class IOgre14SourceTextureDecoderFaultInjector {
public:
  virtual ~IOgre14SourceTextureDecoderFaultInjector() = default;
  /// Borrowed test seam. Production passes null. Implementations may throw.
  virtual void BeforeDecoderStage(Ogre14SourceTextureDecoderFaultStage) {}
};

[[nodiscard]] ValidationResult ValidateOgre14SourceTextureDecodeOptions(
    const Ogre14SourceTextureDecodeOptions &options);

/// Auto-detects an admitted legacy 2D DDS, PNG, or JPEG container and decodes
/// it to canonical tightly packed, top-down RGBA8_UNORM. PNG is restricted to
/// 8-bit RGB, indexed, or RGBA (including Adam7 and palette/tRNS); JPEG is
/// restricted to 8-bit three-component baseline or progressive streams.
///
/// This API normalizes bytes; it does not authenticate them. Product callers
/// must pass exact bytes retained by an authenticated source-texture receipt.
/// Every failure leaves `output` byte-for-byte unchanged.
[[nodiscard]] ValidationResult DecodeOgre14SourceTexture(
    const std::vector<std::uint8_t> &encoded_source,
    const Ogre14SourceTextureDecodeOptions &options,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector = nullptr);

/// Decodes one legacy 2D DDS and all of its declared mip levels into canonical
/// tightly packed RGBA8_UNORM. Arrays, cube maps, volumes, DX10 extensions,
/// signed/unknown formats, truncation, trailing data, and ambiguous layouts
/// fail closed. Every failure leaves `output` byte-for-byte unchanged.
[[nodiscard]] ValidationResult DecodeOgre14SourceTextureDds(
    const std::vector<std::uint8_t> &encoded_dds,
    const Ogre14SourceTextureDecodeOptions &options,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector = nullptr);

} // namespace RoR::Render
