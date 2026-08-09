/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14SourceTextureDecoder.h"

#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::size_t kDdsHeaderBytes = 128U;
constexpr std::uint32_t kDdsMagic = 0x20534444U;
constexpr std::uint32_t kDdsHeaderSize = 124U;
constexpr std::uint32_t kDdsPixelFormatSize = 32U;

constexpr std::uint32_t kDdsdCaps = 0x00000001U;
constexpr std::uint32_t kDdsdHeight = 0x00000002U;
constexpr std::uint32_t kDdsdWidth = 0x00000004U;
constexpr std::uint32_t kDdsdPitch = 0x00000008U;
constexpr std::uint32_t kDdsdPixelFormat = 0x00001000U;
constexpr std::uint32_t kDdsdMipMapCount = 0x00020000U;
constexpr std::uint32_t kDdsdLinearSize = 0x00080000U;
constexpr std::uint32_t kDdsdDepth = 0x00800000U;
constexpr std::uint32_t kSupportedHeaderFlags =
    kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPitch |
    kDdsdPixelFormat | kDdsdMipMapCount | kDdsdLinearSize | kDdsdDepth;

constexpr std::uint32_t kDdpfAlphaPixels = 0x00000001U;
constexpr std::uint32_t kDdpfFourCc = 0x00000004U;
constexpr std::uint32_t kDdpfRgb = 0x00000040U;
constexpr std::uint32_t kSupportedPixelFormatFlags =
    kDdpfAlphaPixels | kDdpfFourCc | kDdpfRgb;

constexpr std::uint32_t kDdsCapsComplex = 0x00000008U;
constexpr std::uint32_t kDdsCapsTexture = 0x00001000U;
constexpr std::uint32_t kDdsCapsMipMap = 0x00400000U;
constexpr std::uint32_t kSupportedCaps =
    kDdsCapsComplex | kDdsCapsTexture | kDdsCapsMipMap;
constexpr std::uint32_t kDdsCaps2CubeMap = 0x00000200U;
constexpr std::uint32_t kDdsCaps2CubeMapFaces = 0x0000FC00U;
constexpr std::uint32_t kDdsCaps2Volume = 0x00200000U;

constexpr std::uint32_t FourCc(char a, char b, char c, char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

constexpr std::uint32_t kFourCcDxt1 = FourCc('D', 'X', 'T', '1');
constexpr std::uint32_t kFourCcDxt3 = FourCc('D', 'X', 'T', '3');
constexpr std::uint32_t kFourCcDxt5 = FourCc('D', 'X', 'T', '5');
constexpr std::uint32_t kFourCcAti1 = FourCc('A', 'T', 'I', '1');
constexpr std::uint32_t kFourCcAti2 = FourCc('A', 'T', 'I', '2');
constexpr std::uint32_t kFourCcBc4u = FourCc('B', 'C', '4', 'U');
constexpr std::uint32_t kFourCcBc5u = FourCc('B', 'C', '5', 'U');
constexpr std::uint32_t kFourCcDx10 = FourCc('D', 'X', '1', '0');

struct ParsedDds final {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t mip_count = 0U;
  std::uint32_t header_flags = 0U;
  std::uint32_t pitch_or_linear_size = 0U;
  Ogre14SourceTextureFormat format = Ogre14SourceTextureFormat::RGBA8_UNORM;
  bool block_compressed = false;
  bool source_has_alpha = false;
  std::uint32_t block_bytes = 0U;
};

struct MipSpan final {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t source_offset = 0U;
  std::uint64_t source_bytes = 0U;
  std::uint64_t decoded_bytes = 0U;
};

struct Pixel final {
  std::uint8_t r = 0U;
  std::uint8_t g = 0U;
  std::uint8_t b = 0U;
  std::uint8_t a = 255U;
};

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool ReadU32LittleEndian(const std::vector<std::uint8_t> &bytes,
                         std::size_t offset, std::uint32_t &value) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < 4U) {
    return false;
  }
  value = static_cast<std::uint32_t>(bytes[offset]) |
          (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
          (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
          (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
  return true;
}

std::uint16_t ReadBlockU16(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadBlockU32(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t ReadBlockU48(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::uint32_t index = 0U; index < 6U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

std::uint64_t ReadBlockU64(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

bool CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                std::uint64_t &result) noexcept {
  if (lhs > (std::numeric_limits<std::uint64_t>::max)() - rhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &result) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

bool IsKnownColorSemantic(
    Ogre14SourceTextureColorSemantic semantic) noexcept {
  switch (semantic) {
  case Ogre14SourceTextureColorSemantic::SRGB_COLOR:
  case Ogre14SourceTextureColorSemantic::LINEAR_DATA:
    return true;
  case Ogre14SourceTextureColorSemantic::UNSPECIFIED:
    return false;
  }
  return false;
}

bool IsKnownBc1AlphaMode(Ogre14SourceTextureBc1AlphaMode mode) noexcept {
  switch (mode) {
  case Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE:
  case Ogre14SourceTextureBc1AlphaMode::OPAQUE:
  case Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA:
    return true;
  }
  return false;
}

std::uint8_t Expand5To8(std::uint16_t value) noexcept {
  const std::uint8_t narrowed = static_cast<std::uint8_t>(value & 31U);
  return static_cast<std::uint8_t>((narrowed << 3U) | (narrowed >> 2U));
}

std::uint8_t Expand6To8(std::uint16_t value) noexcept {
  const std::uint8_t narrowed = static_cast<std::uint8_t>(value & 63U);
  return static_cast<std::uint8_t>((narrowed << 2U) | (narrowed >> 4U));
}

Pixel DecodeRgb565(std::uint16_t value) noexcept {
  Pixel result;
  result.r = Expand5To8(static_cast<std::uint16_t>(value >> 11U));
  result.g = Expand6To8(static_cast<std::uint16_t>(value >> 5U));
  result.b = Expand5To8(value);
  return result;
}

std::uint8_t InterpolateThird(std::uint8_t first, std::uint8_t second,
                              std::uint32_t first_weight) noexcept {
  return static_cast<std::uint8_t>(
      (first_weight * static_cast<std::uint32_t>(first) +
       (3U - first_weight) * static_cast<std::uint32_t>(second)) /
      3U);
}

Pixel InterpolateColorThird(const Pixel &first, const Pixel &second,
                            std::uint32_t first_weight) noexcept {
  Pixel result;
  result.r = InterpolateThird(first.r, second.r, first_weight);
  result.g = InterpolateThird(first.g, second.g, first_weight);
  result.b = InterpolateThird(first.b, second.b, first_weight);
  return result;
}

Pixel InterpolateColorHalf(const Pixel &first, const Pixel &second) noexcept {
  Pixel result;
  result.r = static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(first.r) + second.r) / 2U);
  result.g = static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(first.g) + second.g) / 2U);
  result.b = static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(first.b) + second.b) / 2U);
  return result;
}

void DecodeColorBlock(const std::uint8_t *block, bool permit_one_bit_alpha,
                      std::array<Pixel, 16U> &pixels) noexcept {
  const std::uint16_t color_zero = ReadBlockU16(block);
  const std::uint16_t color_one = ReadBlockU16(block + 2U);
  std::array<Pixel, 4U> palette{};
  palette[0] = DecodeRgb565(color_zero);
  palette[1] = DecodeRgb565(color_one);
  if (!permit_one_bit_alpha || color_zero > color_one) {
    palette[2] = InterpolateColorThird(palette[0], palette[1], 2U);
    palette[3] = InterpolateColorThird(palette[0], palette[1], 1U);
  } else {
    palette[2] = InterpolateColorHalf(palette[0], palette[1]);
    palette[3] = Pixel{};
    palette[3].a = 0U;
  }
  const std::uint32_t indices = ReadBlockU32(block + 4U);
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    pixels[texel] = palette[(indices >> (texel * 2U)) & 3U];
  }
}

std::array<std::uint8_t, 8U>
BuildInterpolatedChannelTable(std::uint8_t endpoint_zero,
                              std::uint8_t endpoint_one) noexcept {
  std::array<std::uint8_t, 8U> table{};
  table[0] = endpoint_zero;
  table[1] = endpoint_one;
  if (endpoint_zero > endpoint_one) {
    for (std::uint32_t index = 1U; index <= 6U; ++index) {
      table[index + 1U] = static_cast<std::uint8_t>(
          ((7U - index) * static_cast<std::uint32_t>(endpoint_zero) +
           index * static_cast<std::uint32_t>(endpoint_one)) /
          7U);
    }
  } else {
    for (std::uint32_t index = 1U; index <= 4U; ++index) {
      table[index + 1U] = static_cast<std::uint8_t>(
          ((5U - index) * static_cast<std::uint32_t>(endpoint_zero) +
           index * static_cast<std::uint32_t>(endpoint_one)) /
          5U);
    }
    table[6] = 0U;
    table[7] = 255U;
  }
  return table;
}

void DecodeInterpolatedChannelBlock(const std::uint8_t *block,
                                    std::array<std::uint8_t, 16U> &values) noexcept {
  const std::array<std::uint8_t, 8U> table =
      BuildInterpolatedChannelTable(block[0], block[1]);
  const std::uint64_t indices = ReadBlockU48(block + 2U);
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    values[texel] = table[(indices >> (texel * 3U)) & 7U];
  }
}

void DecodeBlock(Ogre14SourceTextureFormat format,
                 Ogre14SourceTextureBc1AlphaMode bc1_alpha_mode,
                 const std::uint8_t *block,
                 std::array<Pixel, 16U> &pixels) noexcept {
  switch (format) {
  case Ogre14SourceTextureFormat::BC1_UNORM:
    DecodeColorBlock(
        block,
        bc1_alpha_mode == Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA,
        pixels);
    return;
  case Ogre14SourceTextureFormat::BC2_UNORM: {
    DecodeColorBlock(block + 8U, false, pixels);
    const std::uint64_t alpha = ReadBlockU64(block);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
      const std::uint8_t nibble =
          static_cast<std::uint8_t>((alpha >> (texel * 4U)) & 15U);
      pixels[texel].a = static_cast<std::uint8_t>(nibble * 17U);
    }
    return;
  }
  case Ogre14SourceTextureFormat::BC3_UNORM: {
    DecodeColorBlock(block + 8U, false, pixels);
    std::array<std::uint8_t, 16U> alpha{};
    DecodeInterpolatedChannelBlock(block, alpha);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
      pixels[texel].a = alpha[texel];
    }
    return;
  }
  case Ogre14SourceTextureFormat::BC4_UNORM: {
    std::array<std::uint8_t, 16U> red{};
    DecodeInterpolatedChannelBlock(block, red);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
      pixels[texel] = Pixel{};
      pixels[texel].r = red[texel];
    }
    return;
  }
  case Ogre14SourceTextureFormat::BC5_UNORM: {
    std::array<std::uint8_t, 16U> red{};
    std::array<std::uint8_t, 16U> green{};
    DecodeInterpolatedChannelBlock(block, red);
    DecodeInterpolatedChannelBlock(block + 8U, green);
    for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
      pixels[texel] = Pixel{};
      pixels[texel].r = red[texel];
      pixels[texel].g = green[texel];
    }
    return;
  }
  case Ogre14SourceTextureFormat::RGBA8_UNORM:
  case Ogre14SourceTextureFormat::RGBX8_UNORM:
  case Ogre14SourceTextureFormat::BGRA8_UNORM:
  case Ogre14SourceTextureFormat::BGRX8_UNORM:
    return;
  }
}

void StorePixel(const Pixel &pixel, std::uint32_t x, std::uint32_t y,
                Ogre14DecodedSourceTextureMip &mip) noexcept {
  const std::size_t destination =
      (static_cast<std::size_t>(y) * mip.width + x) * 4U;
  mip.rgba8_unorm[destination] = pixel.r;
  mip.rgba8_unorm[destination + 1U] = pixel.g;
  mip.rgba8_unorm[destination + 2U] = pixel.b;
  mip.rgba8_unorm[destination + 3U] = pixel.a;
}

void DecodeBlockCompressedMip(const std::vector<std::uint8_t> &bytes,
                              const MipSpan &span,
                              Ogre14SourceTextureFormat format,
                              Ogre14SourceTextureBc1AlphaMode bc1_alpha_mode,
                              std::uint32_t block_bytes,
                              Ogre14DecodedSourceTextureMip &mip) noexcept {
  const std::uint32_t blocks_wide = (span.width + 3U) / 4U;
  const std::uint32_t blocks_high = (span.height + 3U) / 4U;
  std::size_t source = static_cast<std::size_t>(span.source_offset);
  for (std::uint32_t block_y = 0U; block_y < blocks_high; ++block_y) {
    for (std::uint32_t block_x = 0U; block_x < blocks_wide; ++block_x) {
      std::array<Pixel, 16U> decoded{};
      DecodeBlock(format, bc1_alpha_mode, bytes.data() + source, decoded);
      source += block_bytes;
      for (std::uint32_t local_y = 0U; local_y < 4U; ++local_y) {
        const std::uint32_t y = block_y * 4U + local_y;
        if (y >= span.height) {
          continue;
        }
        for (std::uint32_t local_x = 0U; local_x < 4U; ++local_x) {
          const std::uint32_t x = block_x * 4U + local_x;
          if (x < span.width) {
            StorePixel(decoded[local_y * 4U + local_x], x, y, mip);
          }
        }
      }
    }
  }
}

void DecodeUncompressedMip(const std::vector<std::uint8_t> &bytes,
                           const MipSpan &span,
                           Ogre14SourceTextureFormat format,
                           Ogre14DecodedSourceTextureMip &mip) noexcept {
  std::size_t source = static_cast<std::size_t>(span.source_offset);
  for (std::uint32_t y = 0U; y < span.height; ++y) {
    for (std::uint32_t x = 0U; x < span.width; ++x) {
      const std::uint8_t byte_zero = bytes[source];
      const std::uint8_t byte_one = bytes[source + 1U];
      const std::uint8_t byte_two = bytes[source + 2U];
      const std::uint8_t byte_three = bytes[source + 3U];
      source += 4U;
      Pixel pixel;
      if (format == Ogre14SourceTextureFormat::RGBA8_UNORM ||
          format == Ogre14SourceTextureFormat::RGBX8_UNORM) {
        pixel.r = byte_zero;
        pixel.g = byte_one;
        pixel.b = byte_two;
      } else {
        pixel.r = byte_two;
        pixel.g = byte_one;
        pixel.b = byte_zero;
      }
      if (format == Ogre14SourceTextureFormat::RGBA8_UNORM ||
          format == Ogre14SourceTextureFormat::BGRA8_UNORM) {
        pixel.a = byte_three;
      }
      StorePixel(pixel, x, y, mip);
    }
  }
}

std::uint32_t MaximumGeometricMipCount(std::uint32_t width,
                                       std::uint32_t height) noexcept {
  std::uint32_t levels = 1U;
  std::uint32_t largest = width > height ? width : height;
  while (largest > 1U) {
    largest >>= 1U;
    ++levels;
  }
  return levels;
}

ValidationResult ParseDdsHeader(
    const std::vector<std::uint8_t> &bytes,
    const Ogre14SourceTextureDecodeOptions &options, ParsedDds &parsed) {
  if (bytes.size() < kDdsHeaderBytes) {
    return Failure(ValidationCode::SIZE_MISMATCH, "source_texture.dds.header",
                   "DDS is truncated before the complete legacy header");
  }

  std::uint32_t magic = 0U;
  std::uint32_t header_size = 0U;
  std::uint32_t flags = 0U;
  std::uint32_t height = 0U;
  std::uint32_t width = 0U;
  std::uint32_t pitch_or_linear_size = 0U;
  std::uint32_t depth = 0U;
  std::uint32_t mip_count_raw = 0U;
  std::uint32_t pixel_format_size = 0U;
  std::uint32_t pixel_flags = 0U;
  std::uint32_t four_cc = 0U;
  std::uint32_t rgb_bit_count = 0U;
  std::uint32_t red_mask = 0U;
  std::uint32_t green_mask = 0U;
  std::uint32_t blue_mask = 0U;
  std::uint32_t alpha_mask = 0U;
  std::uint32_t caps = 0U;
  std::uint32_t caps2 = 0U;
  std::uint32_t caps3 = 0U;
  std::uint32_t caps4 = 0U;

  const bool complete =
      ReadU32LittleEndian(bytes, 0U, magic) &&
      ReadU32LittleEndian(bytes, 4U, header_size) &&
      ReadU32LittleEndian(bytes, 8U, flags) &&
      ReadU32LittleEndian(bytes, 12U, height) &&
      ReadU32LittleEndian(bytes, 16U, width) &&
      ReadU32LittleEndian(bytes, 20U, pitch_or_linear_size) &&
      ReadU32LittleEndian(bytes, 24U, depth) &&
      ReadU32LittleEndian(bytes, 28U, mip_count_raw) &&
      ReadU32LittleEndian(bytes, 76U, pixel_format_size) &&
      ReadU32LittleEndian(bytes, 80U, pixel_flags) &&
      ReadU32LittleEndian(bytes, 84U, four_cc) &&
      ReadU32LittleEndian(bytes, 88U, rgb_bit_count) &&
      ReadU32LittleEndian(bytes, 92U, red_mask) &&
      ReadU32LittleEndian(bytes, 96U, green_mask) &&
      ReadU32LittleEndian(bytes, 100U, blue_mask) &&
      ReadU32LittleEndian(bytes, 104U, alpha_mask) &&
      ReadU32LittleEndian(bytes, 108U, caps) &&
      ReadU32LittleEndian(bytes, 112U, caps2) &&
      ReadU32LittleEndian(bytes, 116U, caps3) &&
      ReadU32LittleEndian(bytes, 120U, caps4);
  if (!complete) {
    return Failure(ValidationCode::SIZE_MISMATCH, "source_texture.dds.header",
                   "DDS header little-endian fields are truncated");
  }
  if (magic != kDdsMagic || header_size != kDdsHeaderSize ||
      pixel_format_size != kDdsPixelFormatSize) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "source_texture.dds.header",
                   "DDS magic or fixed legacy structure size is invalid");
  }
  if ((flags & ~kSupportedHeaderFlags) != 0U ||
      (flags & (kDdsdHeight | kDdsdWidth)) !=
          (kDdsdHeight | kDdsdWidth)) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.flags",
                   "DDS uses unsupported flags or omits 2D dimensions");
  }
  if ((caps & ~kSupportedCaps) != 0U || caps3 != 0U || caps4 != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.caps",
                   "DDS uses unsupported legacy surface capabilities");
  }
  if ((caps2 & (kDdsCaps2CubeMap | kDdsCaps2CubeMapFaces)) != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.cube_map",
                   "DDS cube maps are outside the 2D source-texture contract");
  }
  if ((caps2 & kDdsCaps2Volume) != 0U || (flags & kDdsdDepth) != 0U ||
      depth > 1U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.volume",
                   "DDS volume textures are outside the 2D source-texture contract");
  }
  if (caps2 != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.caps2",
                   "DDS uses unsupported secondary surface capabilities");
  }
  if (width == 0U || height == 0U || width > options.maximum_dimension ||
      height > options.maximum_dimension) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "source_texture.dds.dimensions",
                   "DDS dimensions are zero or exceed the configured cap");
  }
  if ((flags & kDdsdPitch) != 0U && (flags & kDdsdLinearSize) != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.pitch",
                   "DDS cannot declare pitch and linear size simultaneously");
  }
  if ((pixel_flags & ~kSupportedPixelFormatFlags) != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.pixel_format.flags",
                   "DDS pixel format contains unsupported flags");
  }

  ParsedDds candidate;
  candidate.width = width;
  candidate.height = height;
  candidate.header_flags = flags;
  candidate.pitch_or_linear_size = pitch_or_linear_size;
  if ((pixel_flags & kDdpfFourCc) != 0U) {
    if ((pixel_flags & kDdpfRgb) != 0U || rgb_bit_count != 0U ||
        red_mask != 0U || green_mask != 0U || blue_mask != 0U ||
        alpha_mask != 0U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.pixel_format",
                     "compressed DDS has an ambiguous RGB layout");
    }
    candidate.block_compressed = true;
    if (four_cc == kFourCcDx10) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.dx10",
                     "DX10 DDS extensions and arrays are not accepted");
    }
    if (four_cc == kFourCcDxt1) {
      candidate.format = Ogre14SourceTextureFormat::BC1_UNORM;
      candidate.block_bytes = 8U;
      candidate.source_has_alpha =
          options.bc1_alpha_mode ==
          Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA;
      if (options.bc1_alpha_mode ==
          Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE) {
        return Failure(ValidationCode::INVALID_ENUM,
                       "source_texture.options.bc1_alpha_mode",
                       "DXT1 requires an explicit opaque or one-bit-alpha interpretation");
      }
    } else if (four_cc == kFourCcDxt3) {
      candidate.format = Ogre14SourceTextureFormat::BC2_UNORM;
      candidate.block_bytes = 16U;
      candidate.source_has_alpha = true;
    } else if (four_cc == kFourCcDxt5) {
      candidate.format = Ogre14SourceTextureFormat::BC3_UNORM;
      candidate.block_bytes = 16U;
      candidate.source_has_alpha = true;
    } else if (four_cc == kFourCcAti1 || four_cc == kFourCcBc4u) {
      candidate.format = Ogre14SourceTextureFormat::BC4_UNORM;
      candidate.block_bytes = 8U;
    } else if (four_cc == kFourCcAti2 || four_cc == kFourCcBc5u) {
      candidate.format = Ogre14SourceTextureFormat::BC5_UNORM;
      candidate.block_bytes = 16U;
    } else {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.four_cc",
                     "DDS FourCC is not a supported unsigned legacy format");
    }
    if (candidate.format != Ogre14SourceTextureFormat::BC1_UNORM &&
        options.bc1_alpha_mode !=
            Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE) {
      return Failure(ValidationCode::INVALID_ENUM,
                     "source_texture.options.bc1_alpha_mode",
                     "BC1 alpha interpretation was supplied for a non-BC1 texture");
    }
    if ((flags & kDdsdPitch) != 0U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.pitch",
                     "block-compressed DDS cannot use an uncompressed row pitch");
    }
  } else if ((pixel_flags & kDdpfRgb) != 0U) {
    if (four_cc != 0U || rgb_bit_count != 32U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.pixel_format",
                     "only explicit legacy 32-bit RGB layouts are accepted");
    }
    const bool has_alpha_flag = (pixel_flags & kDdpfAlphaPixels) != 0U;
    const bool rgba_order = red_mask == 0x000000FFU &&
                            green_mask == 0x0000FF00U &&
                            blue_mask == 0x00FF0000U;
    const bool bgra_order = red_mask == 0x00FF0000U &&
                            green_mask == 0x0000FF00U &&
                            blue_mask == 0x000000FFU;
    if ((!rgba_order && !bgra_order) ||
        (alpha_mask != 0U && alpha_mask != 0xFF000000U) ||
        has_alpha_flag != (alpha_mask == 0xFF000000U)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.pixel_format.masks",
                     "DDS 32-bit channel masks are unsupported or ambiguous");
    }
    candidate.source_has_alpha = has_alpha_flag;
    if (rgba_order) {
      candidate.format = has_alpha_flag
                             ? Ogre14SourceTextureFormat::RGBA8_UNORM
                             : Ogre14SourceTextureFormat::RGBX8_UNORM;
    } else {
      candidate.format = has_alpha_flag
                             ? Ogre14SourceTextureFormat::BGRA8_UNORM
                             : Ogre14SourceTextureFormat::BGRX8_UNORM;
    }
    if (options.bc1_alpha_mode !=
        Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE) {
      return Failure(ValidationCode::INVALID_ENUM,
                     "source_texture.options.bc1_alpha_mode",
                     "BC1 alpha interpretation was supplied for an uncompressed texture");
    }
    if ((flags & kDdsdLinearSize) != 0U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.dds.pitch",
                     "uncompressed DDS cannot use a compressed linear size");
    }
  } else {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.dds.pixel_format",
                   "DDS does not declare a supported FourCC or RGB layout");
  }

  const std::uint32_t mip_count = mip_count_raw == 0U ? 1U : mip_count_raw;
  if (mip_count > options.maximum_mip_levels ||
      mip_count > MaximumGeometricMipCount(width, height)) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "source_texture.dds.mip_count",
                   "DDS mip count exceeds configured or geometric limits");
  }
  candidate.mip_count = mip_count;
  parsed = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildMipSpans(
    const std::vector<std::uint8_t> &bytes,
    const Ogre14SourceTextureDecodeOptions &options, const ParsedDds &parsed,
    std::array<MipSpan, kOgre14SourceTextureHardMaximumMipLevels> &spans) {
  std::uint32_t width = parsed.width;
  std::uint32_t height = parsed.height;
  std::uint64_t source_offset = kDdsHeaderBytes;
  std::uint64_t total_decoded_bytes = 0U;
  for (std::uint32_t level = 0U; level < parsed.mip_count; ++level) {
    MipSpan span;
    span.width = width;
    span.height = height;
    span.source_offset = source_offset;
    std::uint64_t source_bytes = 0U;
    if (parsed.block_compressed) {
      const std::uint64_t blocks_wide =
          (static_cast<std::uint64_t>(width) + 3U) / 4U;
      const std::uint64_t blocks_high =
          (static_cast<std::uint64_t>(height) + 3U) / 4U;
      std::uint64_t block_count = 0U;
      if (!CheckedMultiply(blocks_wide, blocks_high, block_count) ||
          !CheckedMultiply(block_count, parsed.block_bytes, source_bytes)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "source_texture.dds.mip.source_bytes",
                       "DDS compressed mip byte count overflowed");
      }
    } else {
      std::uint64_t texel_count = 0U;
      if (!CheckedMultiply(width, height, texel_count) ||
          !CheckedMultiply(texel_count, 4U, source_bytes)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "source_texture.dds.mip.source_bytes",
                       "DDS uncompressed mip byte count overflowed");
      }
    }
    std::uint64_t row_bytes = 0U;
    if (!CheckedMultiply(width, 4U, row_bytes) ||
        !CheckedMultiply(row_bytes, height, span.decoded_bytes)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "source_texture.dds.mip.decoded_bytes",
                     "canonical RGBA8 mip byte count overflowed");
    }
    span.source_bytes = source_bytes;
    if (level == 0U) {
      if ((parsed.header_flags & kDdsdPitch) != 0U &&
          parsed.pitch_or_linear_size != row_bytes) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.dds.pitch",
                       "DDS top-level row pitch is not tightly packed 32-bit data");
      }
      if ((parsed.header_flags & kDdsdLinearSize) != 0U &&
          parsed.pitch_or_linear_size != source_bytes) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.dds.linear_size",
                       "DDS top-level compressed linear size is inconsistent");
      }
    }
    std::uint64_t next_source_offset = 0U;
    if (!CheckedAdd(source_offset, source_bytes, next_source_offset) ||
        next_source_offset > static_cast<std::uint64_t>(bytes.size())) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.dds.mip.payload",
                     "DDS mip payload is truncated");
    }
    if (!CheckedAdd(total_decoded_bytes, span.decoded_bytes,
                    total_decoded_bytes) ||
        total_decoded_bytes > options.maximum_decoded_bytes ||
        total_decoded_bytes >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "source_texture.dds.decoded_bytes",
                     "canonical RGBA8 mip chain exceeds the decoded-byte cap");
    }
    spans[level] = span;
    source_offset = next_source_offset;
    width = width > 1U ? width / 2U : 1U;
    height = height > 1U ? height / 2U : 1U;
  }
  if (source_offset != static_cast<std::uint64_t>(bytes.size())) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "source_texture.dds.trailing_bytes",
                   "DDS contains bytes after its declared 2D mip chain");
  }
  return ValidationResult::Success();
}

} // namespace

ValidationResult ValidateOgre14SourceTextureDecodeOptions(
    const Ogre14SourceTextureDecodeOptions &options) {
  if (options.version != kOgre14SourceTextureDecodeOptionsVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "source_texture.options.version",
                   "unsupported source-texture decode options version");
  }
  if (!IsKnownColorSemantic(options.color_semantic) ||
      !IsKnownBc1AlphaMode(options.bc1_alpha_mode)) {
    return Failure(ValidationCode::INVALID_ENUM, "source_texture.options",
                   "source texture semantics contain an unknown or unspecified enum");
  }
  if (options.maximum_dimension == 0U ||
      options.maximum_dimension > kOgre14SourceTextureHardMaximumDimension ||
      options.maximum_mip_levels == 0U ||
      options.maximum_mip_levels >
          kOgre14SourceTextureHardMaximumMipLevels ||
      options.maximum_encoded_bytes < kDdsHeaderBytes ||
      options.maximum_encoded_bytes >
          kOgre14SourceTextureHardMaximumEncodedBytes ||
      options.maximum_decoded_bytes == 0U ||
      options.maximum_decoded_bytes >
          kOgre14SourceTextureHardMaximumDecodedBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "source_texture.options.limits",
                   "source-texture limits are zero, unusable, or exceed hard caps");
  }
  return ValidationResult::Success();
}

ValidationResult DecodeOgre14SourceTextureDds(
    const std::vector<std::uint8_t> &encoded_dds,
    const Ogre14SourceTextureDecodeOptions &options,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector) {
  const ValidationResult options_validation =
      ValidateOgre14SourceTextureDecodeOptions(options);
  if (!options_validation) {
    return options_validation;
  }
  if (encoded_dds.size() > options.maximum_encoded_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "source_texture.dds.encoded_bytes",
                   "DDS exceeds the configured encoded-byte cap");
  }

  try {
    ParsedDds parsed;
    const ValidationResult header_validation =
        ParseDdsHeader(encoded_dds, options, parsed);
    if (!header_validation) {
      return header_validation;
    }

    std::array<MipSpan, kOgre14SourceTextureHardMaximumMipLevels> spans{};
    const ValidationResult span_validation =
        BuildMipSpans(encoded_dds, options, parsed, spans);
    if (!span_validation) {
      return span_validation;
    }
    if (fault_injector != nullptr) {
      fault_injector->BeforeDecoderStage(
          Ogre14SourceTextureDecoderFaultStage::AFTER_HEADER_VALIDATION);
    }

    Ogre14DecodedSourceTexture candidate;
    candidate.width = parsed.width;
    candidate.height = parsed.height;
    candidate.source_format = parsed.format;
    candidate.color_semantic = options.color_semantic;
    candidate.bc1_alpha_mode =
        parsed.format == Ogre14SourceTextureFormat::BC1_UNORM
            ? options.bc1_alpha_mode
            : Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
    candidate.source_has_alpha = parsed.source_has_alpha;
    candidate.mip_levels.reserve(parsed.mip_count);
    for (std::uint32_t level = 0U; level < parsed.mip_count; ++level) {
      const MipSpan &span = spans[level];
      Ogre14DecodedSourceTextureMip mip;
      mip.width = span.width;
      mip.height = span.height;
      mip.row_pitch_bytes = static_cast<std::uint64_t>(span.width) * 4U;
      mip.slice_pitch_bytes = span.decoded_bytes;
      mip.rgba8_unorm.resize(static_cast<std::size_t>(span.decoded_bytes));
      if (parsed.block_compressed) {
        DecodeBlockCompressedMip(encoded_dds, span, parsed.format,
                                 options.bc1_alpha_mode, parsed.block_bytes,
                                 mip);
      } else {
        DecodeUncompressedMip(encoded_dds, span, parsed.format, mip);
      }
      candidate.mip_levels.push_back(std::move(mip));
      if (level == 0U && fault_injector != nullptr) {
        fault_injector->BeforeDecoderStage(
            Ogre14SourceTextureDecoderFaultStage::AFTER_FIRST_MIP_DECODE);
      }
    }
    if (fault_injector != nullptr) {
      fault_injector->BeforeDecoderStage(
          Ogre14SourceTextureDecoderFaultStage::BEFORE_COMMIT);
    }

    static_assert(
        std::is_nothrow_move_assignable<Ogre14DecodedSourceTexture>::value,
        "source texture output commit must be noexcept");
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "source_texture.decoder.allocation",
                   "allocation failed before source-texture decode commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "source_texture.decoder.allocation",
                   "source-texture allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "source_texture.decoder.exception",
                   "unexpected exception before source-texture decode commit");
  }
}

} // namespace RoR::Render
