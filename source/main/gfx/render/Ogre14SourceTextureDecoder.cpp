/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

// This exact vendored implementation is intentionally confined to this
// translation unit. OGRE14's Codec_FreeImage exports overlapping PNG/JPEG
// codec symbols, so source-image normalization must not add another global
// codec ABI to the combined process.
//
// Keep this guard before every include. It makes compiler/toolchain -D and
// forced-include configuration visible and fatal before the reviewed private
// implementation policy is established.
#if defined(STBIDEF) || defined(STB_IMAGE_IMPLEMENTATION) || \
    defined(STB_IMAGE_STATIC) || defined(STBI_ASSERT) || \
    defined(STBI_FAILURE_USERMSG) || defined(STBI_FREE) || \
    defined(STBI_HAS_LROTL) || defined(STBI_INCLUDE_STB_IMAGE_H) || \
    defined(STBI_MALLOC) || defined(STBI_MAX_DIMENSIONS) || \
    defined(STBI_MINGW_ENABLE_SSE2) || defined(STBI_NEON) || \
    defined(STBI_NO_BMP) || \
    defined(STBI_NO_FAILURE_STRINGS) || defined(STBI_NO_GIF) || \
    defined(STBI_NO_HDR) || defined(STBI_NO_JPEG) || \
    defined(STBI_NO_LINEAR) || defined(STBI_NO_PIC) || \
    defined(STBI_NO_PNG) || defined(STBI_NO_PNM) || \
    defined(STBI_NO_PSD) || defined(STBI_NO_SIMD) || \
    defined(STBI_NO_STDIO) || defined(STBI_NO_TGA) || \
    defined(STBI_NO_THREAD_LOCALS) || defined(STBI_NO_ZLIB) || \
    defined(STBI_ONLY_BMP) || defined(STBI_ONLY_GIF) || \
    defined(STBI_ONLY_HDR) || defined(STBI_ONLY_JPEG) || \
    defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNG) || \
    defined(STBI_ONLY_PNM) || defined(STBI_ONLY_PSD) || \
    defined(STBI_ONLY_TGA) || defined(STBI_ONLY_ZLIB) || \
    defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED) || \
    defined(STBI_SIMD_ALIGN) || \
    defined(STBI_SSE2) || defined(STBI_SUPPORT_ZLIB) || \
    defined(STBI_THREAD_LOCAL) || defined(STBI_WINDOWS_UTF8) || \
    defined(STBI__X64_TARGET) || defined(STBI__X86_TARGET)
#error "stb_image configuration must not be injected before the reviewed decoder policy"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "third_party/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Ogre14SourceTextureDecoder.h"

#include <array>
#include <limits>
#include <memory>
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

constexpr std::array<std::uint8_t, 8U> kPngSignature = {
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};

constexpr std::uint32_t BigEndianTag(char a, char b, char c,
                                     char d) noexcept {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

constexpr std::uint32_t kPngIhdr = BigEndianTag('I', 'H', 'D', 'R');
constexpr std::uint32_t kPngPlte = BigEndianTag('P', 'L', 'T', 'E');
constexpr std::uint32_t kPngTrns = BigEndianTag('t', 'R', 'N', 'S');
constexpr std::uint32_t kPngIdat = BigEndianTag('I', 'D', 'A', 'T');
constexpr std::uint32_t kPngIend = BigEndianTag('I', 'E', 'N', 'D');
constexpr std::uint32_t kPngChrm = BigEndianTag('c', 'H', 'R', 'M');
constexpr std::uint32_t kPngGama = BigEndianTag('g', 'A', 'M', 'A');
constexpr std::uint32_t kPngIccp = BigEndianTag('i', 'C', 'C', 'P');
constexpr std::uint32_t kPngPhys = BigEndianTag('p', 'H', 'Y', 's');
constexpr std::uint32_t kPngSbit = BigEndianTag('s', 'B', 'I', 'T');
constexpr std::uint32_t kPngSrgb = BigEndianTag('s', 'R', 'G', 'B');
constexpr std::uint32_t kPngText = BigEndianTag('t', 'E', 'X', 't');
constexpr std::uint32_t kPngTime = BigEndianTag('t', 'I', 'M', 'E');
constexpr std::uint32_t kPngBkgd = BigEndianTag('b', 'K', 'G', 'D');
constexpr std::uint32_t kPngItxt = BigEndianTag('i', 'T', 'X', 't');
constexpr std::uint32_t kPngActl = BigEndianTag('a', 'c', 'T', 'L');
constexpr std::uint32_t kPngFctl = BigEndianTag('f', 'c', 'T', 'L');
constexpr std::uint32_t kPngFdat = BigEndianTag('f', 'd', 'A', 'T');
constexpr std::uint32_t kPngZtxt = BigEndianTag('z', 'T', 'X', 't');

constexpr std::uint8_t kJpegMarkerSof0 = 0xC0U;
constexpr std::uint8_t kJpegMarkerSof2 = 0xC2U;
constexpr std::uint8_t kJpegMarkerDht = 0xC4U;
constexpr std::uint8_t kJpegMarkerSoi = 0xD8U;
constexpr std::uint8_t kJpegMarkerEoi = 0xD9U;
constexpr std::uint8_t kJpegMarkerSos = 0xDAU;
constexpr std::uint8_t kJpegMarkerDqt = 0xDBU;
constexpr std::uint8_t kJpegMarkerDri = 0xDDU;
constexpr std::uint8_t kJpegMarkerCom = 0xFEU;

struct ParsedPng final {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  bool source_has_alpha = false;
};

struct ParsedJpeg final {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::array<std::uint8_t, 3U> component_ids{};
  bool progressive = false;
};

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
  /// Stride of one authored block row. Only a block-compressed source sets it.
  std::uint64_t source_row_pitch_bytes = 0U;
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

bool ReadU16BigEndian(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset, std::uint16_t &value) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < 2U) {
    return false;
  }
  value = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
      static_cast<std::uint16_t>(bytes[offset + 1U]));
  return true;
}

bool ReadU32BigEndian(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset, std::uint32_t &value) noexcept {
  if (offset > bytes.size() || bytes.size() - offset < 4U) {
    return false;
  }
  value = (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
          (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
          (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
          static_cast<std::uint32_t>(bytes[offset + 3U]);
  return true;
}

bool HasPngSignature(const std::vector<std::uint8_t> &bytes) noexcept {
  if (bytes.size() < kPngSignature.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < kPngSignature.size(); ++index) {
    if (bytes[index] != kPngSignature[index]) {
      return false;
    }
  }
  return true;
}

bool HasJpegSignature(const std::vector<std::uint8_t> &bytes) noexcept {
  return bytes.size() >= 2U && bytes[0] == 0xFFU &&
         bytes[1] == kJpegMarkerSoi;
}

bool HasDdsSignature(const std::vector<std::uint8_t> &bytes) noexcept {
  std::uint32_t magic = 0U;
  return ReadU32LittleEndian(bytes, 0U, magic) && magic == kDdsMagic;
}

const std::array<std::uint32_t, 256U> &PngCrc32Table() {
  static const std::array<std::uint32_t, 256U> table = [] {
    std::array<std::uint32_t, 256U> result{};
    for (std::uint32_t byte = 0U; byte < result.size(); ++byte) {
      std::uint32_t remainder = byte;
      for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
        remainder = (remainder & 1U) != 0U
                        ? (remainder >> 1U) ^ 0xEDB88320U
                        : remainder >> 1U;
      }
      result[byte] = remainder;
    }
    return result;
  }();
  return table;
}

std::uint32_t PngChunkCrc32(const std::vector<std::uint8_t> &bytes,
                            std::size_t type_offset,
                            std::uint32_t data_length) noexcept {
  std::uint32_t crc = 0xFFFFFFFFU;
  const std::array<std::uint32_t, 256U> &table = PngCrc32Table();
  const std::size_t bytes_to_hash =
      static_cast<std::size_t>(data_length) + 4U;
  for (std::size_t index = 0U; index < bytes_to_hash; ++index) {
    crc = table[(crc ^ bytes[type_offset + index]) & 0xFFU] ^ (crc >> 8U);
  }
  return crc ^ 0xFFFFFFFFU;
}

bool IsAsciiLetter(std::uint8_t value) noexcept {
  return (value >= static_cast<std::uint8_t>('A') &&
          value <= static_cast<std::uint8_t>('Z')) ||
         (value >= static_cast<std::uint8_t>('a') &&
          value <= static_cast<std::uint8_t>('z'));
}

bool IsAdmittedPngAncillary(std::uint32_t type) noexcept {
  switch (type) {
  case kPngChrm:
  case kPngGama:
  case kPngIccp:
  case kPngPhys:
  case kPngSbit:
  case kPngSrgb:
  case kPngText:
  case kPngTime:
  case kPngBkgd:
  case kPngItxt:
    return true;
  default:
    return false;
  }
}

bool IsPngSingletonAncillary(std::uint32_t type) noexcept {
  return type != kPngText && type != kPngItxt;
}

bool PngAncillaryMustPrecedeIdat(std::uint32_t type) noexcept {
  switch (type) {
  case kPngChrm:
  case kPngGama:
  case kPngIccp:
  case kPngPhys:
  case kPngSbit:
  case kPngSrgb:
  case kPngBkgd:
    return true;
  default:
    return false;
  }
}

bool FindPngNull(const std::vector<std::uint8_t> &bytes,
                 std::size_t begin, std::size_t end,
                 std::size_t &position) noexcept {
  for (std::size_t cursor = begin; cursor < end; ++cursor) {
    if (bytes[cursor] == 0U) {
      position = cursor;
      return true;
    }
  }
  return false;
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

ValidationResult ParsePngContainer(
    const std::vector<std::uint8_t> &bytes,
    const Ogre14SourceTextureDecodeOptions &options, ParsedPng &parsed) {
  if (!HasPngSignature(bytes)) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "source_texture.png.signature",
                   "PNG signature is missing or truncated");
  }

  ParsedPng candidate;
  std::size_t offset = kPngSignature.size();
  std::uint32_t chunk_count = 0U;
  std::uint8_t color_type = 0U;
  std::uint32_t palette_entries = 0U;
  bool seen_ihdr = false;
  bool seen_plte = false;
  bool seen_trns = false;
  bool seen_idat = false;
  bool idat_sequence_closed = false;
  bool seen_iend = false;
  bool seen_iccp = false;
  bool seen_srgb = false;
  std::array<std::uint32_t, 10U> singleton_ancillary{};
  std::size_t singleton_ancillary_count = 0U;

  while (offset < bytes.size()) {
    ++chunk_count;
    if (chunk_count > kOgre14SourceImageCodecMaximumPngChunks) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "source_texture.png.chunk_count",
                     "PNG chunk count exceeds the hard parser cap");
    }
    if (bytes.size() - offset < 12U) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.png.chunk",
                     "PNG chunk header, data, or CRC is truncated");
    }

    std::uint32_t data_length = 0U;
    std::uint32_t type = 0U;
    if (!ReadU32BigEndian(bytes, offset, data_length) ||
        !ReadU32BigEndian(bytes, offset + 4U, type) ||
        data_length > 0x7FFFFFFFU) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "source_texture.png.chunk.length",
                     "PNG chunk length is invalid");
    }
    const std::size_t type_offset = offset + 4U;
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      if (!IsAsciiLetter(bytes[type_offset + byte])) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.chunk.type",
                       "PNG chunk type contains a non-letter byte");
      }
    }
    if (bytes[type_offset + 2U] < static_cast<std::uint8_t>('A') ||
        bytes[type_offset + 2U] > static_cast<std::uint8_t>('Z')) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.png.chunk.reserved_bit",
                     "PNG chunk type uses the reserved lowercase bit");
    }

    std::uint64_t chunk_bytes = 0U;
    std::uint64_t chunk_end = 0U;
    if (!CheckedAdd(static_cast<std::uint64_t>(data_length), 12U,
                    chunk_bytes) ||
        !CheckedAdd(static_cast<std::uint64_t>(offset), chunk_bytes,
                    chunk_end) ||
        chunk_end > static_cast<std::uint64_t>(bytes.size())) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.png.chunk.payload",
                     "PNG chunk extends beyond the encoded payload");
    }
    const std::size_t data_offset = offset + 8U;
    const std::size_t crc_offset =
        data_offset + static_cast<std::size_t>(data_length);
    std::uint32_t encoded_crc = 0U;
    if (!ReadU32BigEndian(bytes, crc_offset, encoded_crc) ||
        encoded_crc != PngChunkCrc32(bytes, type_offset, data_length)) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.png.chunk.crc",
                     "PNG chunk CRC does not match its exact type and data");
    }

    if (!seen_ihdr && (type != kPngIhdr || chunk_count != 1U)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.png.ihdr",
                     "PNG IHDR must be the first and only header chunk");
    }
    if (seen_iend) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.png.trailing_bytes",
                     "PNG contains a chunk after IEND");
    }

    if (type == kPngIhdr) {
      if (seen_ihdr || data_length != 13U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.ihdr",
                       "PNG IHDR is duplicated or has the wrong length");
      }
      std::uint32_t width = 0U;
      std::uint32_t height = 0U;
      if (!ReadU32BigEndian(bytes, data_offset, width) ||
          !ReadU32BigEndian(bytes, data_offset + 4U, height)) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.png.ihdr",
                       "PNG IHDR dimensions are truncated");
      }
      const std::uint8_t bit_depth = bytes[data_offset + 8U];
      color_type = bytes[data_offset + 9U];
      const std::uint8_t compression = bytes[data_offset + 10U];
      const std::uint8_t filter = bytes[data_offset + 11U];
      const std::uint8_t interlace = bytes[data_offset + 12U];
      const std::uint32_t dimension_cap =
          options.maximum_dimension < kOgre14SourceImageCodecMaximumDimension
              ? options.maximum_dimension
              : kOgre14SourceImageCodecMaximumDimension;
      if (width == 0U || height == 0U || width > dimension_cap ||
          height > dimension_cap) {
        return Failure(ValidationCode::INVALID_DIMENSIONS,
                       "source_texture.png.dimensions",
                       "PNG dimensions are zero or exceed the admitted cap");
      }
      if (bit_depth != 8U ||
          (color_type != 2U && color_type != 3U && color_type != 6U)) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.pixel_format",
                       "PNG must be 8-bit RGB, indexed, or RGBA");
      }
      if (compression != 0U || filter != 0U || interlace > 1U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.ihdr.methods",
                       "PNG uses an unsupported compression, filter, or interlace method");
      }
      std::uint64_t texels = 0U;
      std::uint64_t decoded_bytes = 0U;
      if (!CheckedMultiply(width, height, texels) ||
          !CheckedMultiply(texels, 4U, decoded_bytes) ||
          decoded_bytes > options.maximum_decoded_bytes ||
          decoded_bytes > static_cast<std::uint64_t>(
                              (std::numeric_limits<std::size_t>::max)())) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "source_texture.png.decoded_bytes",
                       "canonical PNG RGBA8 output exceeds the decoded-byte cap");
      }
      candidate.width = width;
      candidate.height = height;
      candidate.source_has_alpha = color_type == 6U;
      seen_ihdr = true;
    } else if (type == kPngPlte) {
      if (seen_plte || seen_idat || data_length == 0U ||
          data_length > 768U || (data_length % 3U) != 0U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.plte",
                       "PNG PLTE is duplicated, misplaced, or malformed");
      }
      palette_entries = data_length / 3U;
      seen_plte = true;
    } else if (type == kPngTrns) {
      if (seen_trns || seen_idat || color_type == 6U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.trns",
                       "PNG tRNS is duplicated, misplaced, or conflicts with RGBA");
      }
      if ((color_type == 2U && data_length != 6U) ||
          (color_type == 3U &&
           (!seen_plte || data_length == 0U ||
            data_length > palette_entries))) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.trns",
                       "PNG tRNS length does not match its color type or palette");
      }
      seen_trns = true;
      candidate.source_has_alpha = true;
    } else if (type == kPngIdat) {
      if (idat_sequence_closed || (color_type == 3U && !seen_plte)) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.idat",
                       "PNG IDAT chunks are non-contiguous or precede the palette");
      }
      seen_idat = true;
    } else if (type == kPngIend) {
      if (data_length != 0U || !seen_idat ||
          (color_type == 3U && !seen_plte)) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.iend",
                       "PNG IEND is malformed or precedes required image data");
      }
      seen_iend = true;
    } else {
      if (type == kPngActl || type == kPngFctl || type == kPngFdat) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.apng",
                       "animated PNG chunks are outside the source-texture contract");
      }
      if (type == kPngZtxt) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.ztxt",
                       "compressed PNG text is outside the admitted metadata set");
      }
      if (!IsAdmittedPngAncillary(type)) {
        const bool critical =
            bytes[type_offset] >= static_cast<std::uint8_t>('A') &&
            bytes[type_offset] <= static_cast<std::uint8_t>('Z');
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       critical ? "source_texture.png.critical_chunk"
                                : "source_texture.png.ancillary_chunk",
                       critical
                           ? "PNG contains an unknown critical chunk"
                           : "PNG ancillary chunk is outside the audited content set");
      }
      if (PngAncillaryMustPrecedeIdat(type) && seen_idat) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.ancillary_order",
                       "PNG metadata chunk appears after image data");
      }
      if (IsPngSingletonAncillary(type)) {
        for (std::size_t index = 0U; index < singleton_ancillary_count;
             ++index) {
          if (singleton_ancillary[index] == type) {
            return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                           "source_texture.png.ancillary_duplicate",
                           "PNG singleton metadata chunk is duplicated");
          }
        }
        singleton_ancillary[singleton_ancillary_count++] = type;
      }
      if (type == kPngIccp) {
        std::size_t keyword_end = 0U;
        const std::size_t data_end =
            data_offset + static_cast<std::size_t>(data_length);
        if (seen_srgb || data_length < 4U ||
            !FindPngNull(bytes, data_offset, data_end, keyword_end) ||
            keyword_end == data_offset || keyword_end - data_offset > 79U ||
            keyword_end + 2U >= data_end || bytes[keyword_end + 1U] != 0U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.png.iccp",
                         "PNG iCCP profile name, compression method, or payload is invalid");
        }
        seen_iccp = true;
      } else if (type == kPngSrgb) {
        if (seen_iccp || data_length != 1U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.png.srgb",
                         "PNG sRGB conflicts with iCCP or has the wrong length");
        }
        seen_srgb = true;
      } else if (type == kPngItxt) {
        const std::size_t data_end =
            data_offset + static_cast<std::size_t>(data_length);
        std::size_t keyword_end = 0U;
        if (!FindPngNull(bytes, data_offset, data_end, keyword_end) ||
            keyword_end == data_offset || keyword_end - data_offset > 79U ||
            keyword_end + 3U > data_end || bytes[keyword_end + 1U] != 0U ||
            bytes[keyword_end + 2U] != 0U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.png.itxt",
                         "PNG iTXt must be uncompressed with method zero");
        }
        const std::size_t language_begin = keyword_end + 3U;
        std::size_t language_end = 0U;
        std::size_t translated_end = 0U;
        if (!FindPngNull(bytes, language_begin, data_end, language_end) ||
            !FindPngNull(bytes, language_end + 1U, data_end,
                         translated_end)) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.png.itxt",
                         "PNG iTXt language or translated-keyword separator is missing");
        }
      } else if (type == kPngText) {
        const std::size_t data_end =
            data_offset + static_cast<std::size_t>(data_length);
        std::size_t keyword_end = 0U;
        if (!FindPngNull(bytes, data_offset, data_end, keyword_end) ||
            keyword_end == data_offset || keyword_end - data_offset > 79U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.png.text",
                         "PNG tEXt keyword is missing or outside the admitted length");
        }
      } else if ((type == kPngChrm && data_length != 32U) ||
                 (type == kPngGama && data_length != 4U) ||
                 (type == kPngPhys && data_length != 9U) ||
                 (type == kPngTime && data_length != 7U) ||
                 (type == kPngSbit &&
                  data_length != (color_type == 6U ? 4U : 3U)) ||
                 (type == kPngBkgd &&
                  data_length != (color_type == 3U ? 1U : 6U))) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.ancillary_length",
                       "PNG fixed-size metadata chunk has the wrong length");
      }
      if (type == kPngBkgd && color_type == 3U && !seen_plte) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.png.bkgd",
                       "indexed PNG bKGD must follow PLTE");
      }
    }

    if (seen_idat && type != kPngIdat && type != kPngIend) {
      idat_sequence_closed = true;
    }
    offset = static_cast<std::size_t>(chunk_end);
    if (seen_iend) {
      if (offset != bytes.size()) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.png.trailing_bytes",
                       "PNG contains bytes after its exact IEND chunk");
      }
      break;
    }
  }

  if (!seen_iend || offset != bytes.size()) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "source_texture.png.iend",
                   "PNG does not end at one complete IEND chunk");
  }
  parsed = candidate;
  return ValidationResult::Success();
}

bool IsJpegRestartMarker(std::uint8_t marker) noexcept {
  return marker >= 0xD0U && marker <= 0xD7U;
}

bool IsAdmittedJpegApplicationMarker(std::uint8_t marker) noexcept {
  return marker == 0xE0U || marker == 0xE1U || marker == 0xE2U ||
         marker == 0xECU || marker == 0xEDU || marker == 0xEEU;
}

int JpegComponentIndex(const ParsedJpeg &parsed,
                       std::uint8_t identifier) noexcept {
  for (std::size_t index = 0U; index < parsed.component_ids.size(); ++index) {
    if (parsed.component_ids[index] == identifier) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

ValidationResult ValidateJpegDqt(const std::vector<std::uint8_t> &bytes,
                                 std::size_t begin, std::size_t end) {
  std::size_t cursor = begin;
  while (cursor < end) {
    const std::uint8_t table = bytes[cursor++];
    const std::uint8_t precision = static_cast<std::uint8_t>(table >> 4U);
    const std::uint8_t identifier = static_cast<std::uint8_t>(table & 15U);
    if (precision > 1U || identifier > 3U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.jpeg.dqt",
                     "JPEG quantization table precision or identifier is unsupported");
    }
    const std::size_t coefficients = precision == 0U ? 64U : 128U;
    if (end - cursor < coefficients) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.jpeg.dqt",
                     "JPEG quantization table is truncated");
    }
    cursor += coefficients;
  }
  return cursor == end
             ? ValidationResult::Success()
             : Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.dqt",
                       "JPEG quantization table segment is malformed");
}

ValidationResult ValidateJpegDht(const std::vector<std::uint8_t> &bytes,
                                 std::size_t begin, std::size_t end) {
  std::size_t cursor = begin;
  while (cursor < end) {
    if (end - cursor < 17U) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.jpeg.dht",
                     "JPEG Huffman table header is truncated");
    }
    const std::uint8_t table = bytes[cursor++];
    if ((table >> 4U) > 1U || (table & 15U) > 3U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.jpeg.dht",
                     "JPEG Huffman table class or identifier is unsupported");
    }
    std::uint32_t symbol_count = 0U;
    for (std::size_t length = 0U; length < 16U; ++length) {
      symbol_count += bytes[cursor + length];
    }
    cursor += 16U;
    if (symbol_count == 0U || symbol_count > 256U ||
        end - cursor < symbol_count) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.jpeg.dht",
                     "JPEG Huffman symbol table is empty, oversized, or truncated");
    }
    cursor += symbol_count;
  }
  return cursor == end
             ? ValidationResult::Success()
             : Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.dht",
                       "JPEG Huffman table segment is malformed");
}

ValidationResult ParseJpegContainer(
    const std::vector<std::uint8_t> &bytes,
    const Ogre14SourceTextureDecodeOptions &options, ParsedJpeg &parsed) {
  if (!HasJpegSignature(bytes)) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "source_texture.jpeg.signature",
                   "JPEG SOI signature is missing or truncated");
  }

  ParsedJpeg candidate;
  std::size_t offset = 2U;
  std::uint32_t marker_count = 0U;
  std::uint8_t scanned_components = 0U;
  bool seen_sof = false;
  bool seen_sos = false;
  bool seen_eoi = false;
  bool in_entropy_scan = false;

  while (offset < bytes.size()) {
    std::uint8_t marker = 0U;
    if (in_entropy_scan) {
      bool found_marker = false;
      while (offset < bytes.size()) {
        if (bytes[offset++] != 0xFFU) {
          continue;
        }
        while (offset < bytes.size() && bytes[offset] == 0xFFU) {
          ++offset;
        }
        if (offset >= bytes.size()) {
          return Failure(ValidationCode::SIZE_MISMATCH,
                         "source_texture.jpeg.entropy",
                         "JPEG entropy scan ends in a truncated marker");
        }
        marker = bytes[offset++];
        if (marker == 0x00U) {
          continue;
        }
        if (IsJpegRestartMarker(marker)) {
          continue;
        }
        found_marker = true;
        break;
      }
      if (!found_marker) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.eoi",
                       "JPEG entropy data reaches EOF before EOI");
      }
      in_entropy_scan = false;
    } else {
      if (offset >= bytes.size() || bytes[offset++] != 0xFFU) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.marker",
                       "JPEG contains bytes outside a marker or entropy scan");
      }
      while (offset < bytes.size() && bytes[offset] == 0xFFU) {
        ++offset;
      }
      if (offset >= bytes.size()) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.marker",
                       "JPEG marker code is truncated");
      }
      marker = bytes[offset++];
      if (marker == 0x00U) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.marker",
                       "JPEG stuffed zero appears outside entropy data");
      }
    }

    ++marker_count;
    if (marker_count > 1048576U) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "source_texture.jpeg.marker_count",
                     "JPEG marker count exceeds the hard parser cap");
    }
    if (marker == kJpegMarkerEoi) {
      if (!seen_sof || !seen_sos || scanned_components != 0x07U ||
          offset != bytes.size()) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "source_texture.jpeg.eoi",
                       "JPEG EOI is premature or is followed by trailing bytes");
      }
      seen_eoi = true;
      break;
    }
    if (marker == kJpegMarkerSoi || IsJpegRestartMarker(marker) ||
        marker == 0x01U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.jpeg.marker",
                     "JPEG contains an unexpected standalone marker");
    }
    if (offset > bytes.size() || bytes.size() - offset < 2U) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.jpeg.segment",
                     "JPEG segment length is truncated");
    }
    std::uint16_t segment_length = 0U;
    if (!ReadU16BigEndian(bytes, offset, segment_length) ||
        segment_length < 2U ||
        static_cast<std::size_t>(segment_length) > bytes.size() - offset) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "source_texture.jpeg.segment",
                     "JPEG segment length exceeds the encoded payload");
    }
    const std::size_t data_offset = offset + 2U;
    const std::size_t segment_end =
        offset + static_cast<std::size_t>(segment_length);

    if (marker == kJpegMarkerSof0 || marker == kJpegMarkerSof2) {
      if (seen_sof || segment_length != 17U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.sof",
                       "JPEG must contain one three-component SOF0 or SOF2 frame");
      }
      const std::uint8_t precision = bytes[data_offset];
      std::uint16_t height = 0U;
      std::uint16_t width = 0U;
      if (!ReadU16BigEndian(bytes, data_offset + 1U, height) ||
          !ReadU16BigEndian(bytes, data_offset + 3U, width) ||
          precision != 8U || bytes[data_offset + 5U] != 3U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.frame",
                       "JPEG frame must be 8-bit with exactly three components");
      }
      const std::uint32_t dimension_cap =
          options.maximum_dimension < kOgre14SourceImageCodecMaximumDimension
              ? options.maximum_dimension
              : kOgre14SourceImageCodecMaximumDimension;
      if (width == 0U || height == 0U || width > dimension_cap ||
          height > dimension_cap) {
        return Failure(ValidationCode::INVALID_DIMENSIONS,
                       "source_texture.jpeg.dimensions",
                       "JPEG dimensions are zero or exceed the admitted cap");
      }
      for (std::size_t component = 0U; component < 3U; ++component) {
        const std::size_t component_offset =
            data_offset + 6U + component * 3U;
        const std::uint8_t identifier = bytes[component_offset];
        const std::uint8_t sampling = bytes[component_offset + 1U];
        const std::uint8_t horizontal =
            static_cast<std::uint8_t>(sampling >> 4U);
        const std::uint8_t vertical =
            static_cast<std::uint8_t>(sampling & 15U);
        if (identifier == 0U || horizontal == 0U || horizontal > 4U ||
            vertical == 0U || vertical > 4U ||
            bytes[component_offset + 2U] > 3U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.jpeg.components",
                         "JPEG component identifier, sampling, or quantization selector is invalid");
        }
        for (std::size_t prior = 0U; prior < component; ++prior) {
          if (candidate.component_ids[prior] == identifier) {
            return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                           "source_texture.jpeg.components",
                           "JPEG frame contains duplicate component identifiers");
          }
        }
        candidate.component_ids[component] = identifier;
      }
      std::uint64_t texels = 0U;
      std::uint64_t decoded_bytes = 0U;
      if (!CheckedMultiply(width, height, texels) ||
          !CheckedMultiply(texels, 4U, decoded_bytes) ||
          decoded_bytes > options.maximum_decoded_bytes ||
          decoded_bytes > static_cast<std::uint64_t>(
                              (std::numeric_limits<std::size_t>::max)())) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "source_texture.jpeg.decoded_bytes",
                       "canonical JPEG RGBA8 output exceeds the decoded-byte cap");
      }
      candidate.width = width;
      candidate.height = height;
      candidate.progressive = marker == kJpegMarkerSof2;
      seen_sof = true;
    } else if (marker == kJpegMarkerSos) {
      if (!seen_sof || segment_length < 8U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.sos",
                       "JPEG scan appears before a complete admitted frame");
      }
      const std::uint8_t component_count = bytes[data_offset];
      if (component_count == 0U || component_count > 3U ||
          segment_length !=
              static_cast<std::uint16_t>(6U + component_count * 2U)) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.sos",
                       "JPEG scan component count or length is invalid");
      }
      std::uint8_t scan_mask = 0U;
      for (std::uint8_t component = 0U; component < component_count;
           ++component) {
        const std::size_t selector_offset =
            data_offset + 1U + static_cast<std::size_t>(component) * 2U;
        const int component_index =
            JpegComponentIndex(candidate, bytes[selector_offset]);
        const std::uint8_t tables = bytes[selector_offset + 1U];
        if (component_index < 0 || (tables >> 4U) > 3U ||
            (tables & 15U) > 3U ||
            (scan_mask & (1U << static_cast<unsigned>(component_index))) !=
                0U) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "source_texture.jpeg.sos.components",
                         "JPEG scan references an unknown, duplicate, or invalid component table");
        }
        scan_mask = static_cast<std::uint8_t>(
            scan_mask | (1U << static_cast<unsigned>(component_index)));
      }
      const std::size_t spectral_offset =
          data_offset + 1U + static_cast<std::size_t>(component_count) * 2U;
      const std::uint8_t spectral_start = bytes[spectral_offset];
      const std::uint8_t spectral_end = bytes[spectral_offset + 1U];
      const std::uint8_t approximation = bytes[spectral_offset + 2U];
      const std::uint8_t successive_high =
          static_cast<std::uint8_t>(approximation >> 4U);
      const std::uint8_t successive_low =
          static_cast<std::uint8_t>(approximation & 15U);
      if ((!candidate.progressive &&
           (spectral_start != 0U || spectral_end != 63U ||
            approximation != 0U)) ||
          (candidate.progressive &&
           (spectral_start > spectral_end || spectral_end > 63U ||
            successive_high > 13U || successive_low > 13U ||
            (spectral_start == 0U && spectral_end != 0U) ||
            (spectral_start != 0U && component_count != 1U)))) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.sos.progression",
                       "JPEG scan spectral or successive-approximation fields are invalid");
      }
      scanned_components =
          static_cast<std::uint8_t>(scanned_components | scan_mask);
      seen_sos = true;
      in_entropy_scan = true;
    } else if (marker == kJpegMarkerDqt) {
      const ValidationResult validation =
          ValidateJpegDqt(bytes, data_offset, segment_end);
      if (!validation) {
        return validation;
      }
    } else if (marker == kJpegMarkerDht) {
      const ValidationResult validation =
          ValidateJpegDht(bytes, data_offset, segment_end);
      if (!validation) {
        return validation;
      }
    } else if (marker == kJpegMarkerDri) {
      if (segment_length != 4U) {
        return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                       "source_texture.jpeg.dri",
                       "JPEG restart interval segment has the wrong length");
      }
    } else if (!IsAdmittedJpegApplicationMarker(marker) &&
               marker != kJpegMarkerCom) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.jpeg.marker",
                     "JPEG marker is outside the audited SOF0/SOF2 Huffman subset");
    }

    offset = segment_end;
  }

  if (!seen_eoi || offset != bytes.size()) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "source_texture.jpeg.eoi",
                   "JPEG does not end at one exact EOI marker");
  }
  parsed = candidate;
  return ValidationResult::Success();
}

struct StbiPixelsDeleter final {
  void operator()(stbi_uc *pixels) const noexcept {
    stbi_image_free(pixels);
  }
};

ValidationResult DecodeStbRgba(
    const std::vector<std::uint8_t> &bytes,
    const Ogre14SourceTextureDecodeOptions &options, std::uint32_t width,
    std::uint32_t height, bool source_has_alpha, bool require_rgb_source,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector) {
  static_assert(STBI_MAX_DIMENSIONS ==
                    kOgre14SourceImageCodecMaximumDimension,
                "stb and public source-image dimension caps must agree");
  if (fault_injector != nullptr) {
    fault_injector->BeforeDecoderStage(
        Ogre14SourceTextureDecoderFaultStage::AFTER_HEADER_VALIDATION);
  }
  if (bytes.size() >
      static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "source_texture.image.encoded_bytes",
                   "source image exceeds stb's signed input-length domain");
  }

  int decoded_width = 0;
  int decoded_height = 0;
  int source_channels = 0;
  std::unique_ptr<stbi_uc, StbiPixelsDeleter> pixels(stbi_load_from_memory(
      bytes.data(), static_cast<int>(bytes.size()), &decoded_width,
      &decoded_height, &source_channels, 4));
  if (!pixels) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "source_texture.image.compressed_payload",
                   "PNG/JPEG payload failed the pinned bounded decoder");
  }
  if (decoded_width != static_cast<int>(width) ||
      decoded_height != static_cast<int>(height) ||
      source_channels < 3 || source_channels > 4 ||
      (require_rgb_source && source_channels != 3)) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "source_texture.image.decoder_metadata",
                   "decoded image metadata differs from strict container preflight");
  }

  std::uint64_t row_pitch = 0U;
  std::uint64_t decoded_bytes = 0U;
  if (!CheckedMultiply(width, 4U, row_pitch) ||
      !CheckedMultiply(row_pitch, height, decoded_bytes) ||
      decoded_bytes > options.maximum_decoded_bytes ||
      decoded_bytes > static_cast<std::uint64_t>(
                          (std::numeric_limits<std::size_t>::max)())) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "source_texture.image.decoded_bytes",
                   "canonical image output exceeds the decoded-byte cap");
  }

  Ogre14DecodedSourceTexture candidate;
  candidate.width = width;
  candidate.height = height;
  candidate.source_format =
      source_has_alpha ? Ogre14SourceTextureFormat::RGBA8_UNORM
                       : Ogre14SourceTextureFormat::RGBX8_UNORM;
  candidate.color_semantic = options.color_semantic;
  candidate.bc1_alpha_mode =
      Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  candidate.source_has_alpha = source_has_alpha;
  candidate.mip_levels.reserve(1U);
  Ogre14DecodedSourceTextureMip mip;
  mip.width = width;
  mip.height = height;
  mip.row_pitch_bytes = row_pitch;
  mip.slice_pitch_bytes = decoded_bytes;
  const std::size_t decoded_size = static_cast<std::size_t>(decoded_bytes);
  mip.rgba8_unorm.assign(pixels.get(), pixels.get() + decoded_size);
  candidate.mip_levels.push_back(std::move(mip));
  if (fault_injector != nullptr) {
    fault_injector->BeforeDecoderStage(
        Ogre14SourceTextureDecoderFaultStage::AFTER_FIRST_MIP_DECODE);
    fault_injector->BeforeDecoderStage(
        Ogre14SourceTextureDecoderFaultStage::BEFORE_COMMIT);
  }

  static_assert(
      std::is_nothrow_move_assignable<Ogre14DecodedSourceTexture>::value,
      "source texture output commit must be noexcept");
  output = std::move(candidate);
  return ValidationResult::Success();
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
  case Ogre14SourceTextureBc1AlphaMode::OPAQUE_COLOR:
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
  // A pass-through decode holds the authored block payload instead of an RGBA8
  // expansion, so the budget must charge for the bytes actually retained.
  const bool preserve_blocks =
      options.preserve_block_compression && parsed.block_compressed;
  std::uint64_t total_retained_bytes = 0U;
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
      // One block row divides the block-count product just accepted above, so
      // it cannot overflow on its own.
      span.source_row_pitch_bytes = blocks_wide * parsed.block_bytes;
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
    const std::uint64_t retained_bytes =
        preserve_blocks ? span.source_bytes : span.decoded_bytes;
    if (!CheckedAdd(total_retained_bytes, retained_bytes,
                    total_retained_bytes) ||
        total_retained_bytes > options.maximum_decoded_bytes ||
        total_retained_bytes >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)())) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          preserve_blocks ? "source_texture.dds.block_bytes"
                          : "source_texture.dds.decoded_bytes",
          preserve_blocks
              ? "authored block mip chain exceeds the decoded-byte cap"
              : "canonical RGBA8 mip chain exceeds the decoded-byte cap");
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

bool TryMapOgre14SourceTextureFormatToTransport(
    Ogre14SourceTextureFormat format,
    TextureResourceFormat &out) noexcept {
  switch (format) {
  case Ogre14SourceTextureFormat::BC1_UNORM:
    out = TextureResourceFormat::BC1_UNORM;
    return true;
  case Ogre14SourceTextureFormat::BC3_UNORM:
    out = TextureResourceFormat::BC3_UNORM;
    return true;
  case Ogre14SourceTextureFormat::BC4_UNORM:
    out = TextureResourceFormat::BC4_UNORM;
    return true;
  case Ogre14SourceTextureFormat::BC5_UNORM:
    out = TextureResourceFormat::BC5_UNORM;
    return true;
  case Ogre14SourceTextureFormat::BC2_UNORM:
  case Ogre14SourceTextureFormat::RGBA8_UNORM:
  case Ogre14SourceTextureFormat::RGBX8_UNORM:
  case Ogre14SourceTextureFormat::BGRA8_UNORM:
  case Ogre14SourceTextureFormat::BGRX8_UNORM:
    return false;
  }
  return false;
}

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

ValidationResult DecodeOgre14SourceTexture(
    const std::vector<std::uint8_t> &encoded_source,
    const Ogre14SourceTextureDecodeOptions &options,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector) {
  const ValidationResult options_validation =
      ValidateOgre14SourceTextureDecodeOptions(options);
  if (!options_validation) {
    return options_validation;
  }
  if (encoded_source.size() > options.maximum_encoded_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "source_texture.encoded_bytes",
                   "source texture exceeds the configured encoded-byte cap");
  }

  try {
    if (HasDdsSignature(encoded_source)) {
      return DecodeOgre14SourceTextureDds(encoded_source, options, output,
                                          fault_injector);
    }
    if (!HasPngSignature(encoded_source) &&
        !HasJpegSignature(encoded_source)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "source_texture.container",
                     "source texture is not an admitted DDS, PNG, or JPEG container");
    }
    if (options.bc1_alpha_mode !=
        Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE) {
      return Failure(ValidationCode::INVALID_ENUM,
                     "source_texture.options.bc1_alpha_mode",
                     "BC1 alpha interpretation cannot be supplied for PNG or JPEG");
    }
    if (HasPngSignature(encoded_source)) {
      ParsedPng parsed;
      const ValidationResult validation =
          ParsePngContainer(encoded_source, options, parsed);
      if (!validation) {
        return validation;
      }
      return DecodeStbRgba(encoded_source, options, parsed.width,
                            parsed.height, parsed.source_has_alpha, false,
                            output, fault_injector);
    }

    ParsedJpeg parsed;
    const ValidationResult validation =
        ParseJpegContainer(encoded_source, options, parsed);
    if (!validation) {
      return validation;
    }
    return DecodeStbRgba(encoded_source, options, parsed.width, parsed.height,
                          false, true, output, fault_injector);
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
    // Pass-through is only meaningful for a block-compressed container. Asking
    // for it on any other source is a legitimate request for whatever the
    // source can offer, which is the canonical RGBA8 decode.
    const bool preserve_blocks =
        options.preserve_block_compression && parsed.block_compressed;
    candidate.block_compressed = preserve_blocks;
    candidate.mip_levels.reserve(parsed.mip_count);
    for (std::uint32_t level = 0U; level < parsed.mip_count; ++level) {
      const MipSpan &span = spans[level];
      Ogre14DecodedSourceTextureMip mip;
      mip.width = span.width;
      mip.height = span.height;
      if (preserve_blocks) {
        mip.row_pitch_bytes = span.source_row_pitch_bytes;
        mip.slice_pitch_bytes = span.source_bytes;
        const std::uint8_t *const source =
            encoded_dds.data() + static_cast<std::size_t>(span.source_offset);
        mip.block_bytes.assign(
            source, source + static_cast<std::size_t>(span.source_bytes));
      } else {
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
