/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14SourceTextureDecoder.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using RoR::Render::DecodeOgre14SourceTextureDds;
using RoR::Render::IOgre14SourceTextureDecoderFaultInjector;
using RoR::Render::Ogre14DecodedSourceTexture;
using RoR::Render::Ogre14DecodedSourceTextureMip;
using RoR::Render::Ogre14SourceTextureBc1AlphaMode;
using RoR::Render::Ogre14SourceTextureColorSemantic;
using RoR::Render::Ogre14SourceTextureDecodeOptions;
using RoR::Render::Ogre14SourceTextureDecoderFaultStage;
using RoR::Render::Ogre14SourceTextureFormat;

constexpr std::uint32_t kDdpfAlphaPixels = 0x00000001U;
constexpr std::uint32_t kDdpfFourCc = 0x00000004U;
constexpr std::uint32_t kDdpfRgb = 0x00000040U;

constexpr std::uint32_t FourCc(char a, char b, char c, char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void WriteU32LittleEndian(std::vector<std::uint8_t> &bytes,
                          std::size_t offset, std::uint32_t value) {
  Require(offset <= bytes.size() && bytes.size() - offset >= 4U,
          "test attempted an out-of-range little-endian write");
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void AppendU16LittleEndian(std::vector<std::uint8_t> &bytes,
                           std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32LittleEndian(std::vector<std::uint8_t> &bytes,
                           std::uint32_t value) {
  for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
    bytes.push_back(
        static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

void AppendU64LittleEndian(std::vector<std::uint8_t> &bytes,
                           std::uint64_t value) {
  for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
    bytes.push_back(
        static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
  }
}

std::uint32_t BlockBytes(std::uint32_t width, std::uint32_t height,
                         std::uint32_t bytes_per_block) {
  return ((width + 3U) / 4U) * ((height + 3U) / 4U) * bytes_per_block;
}

std::vector<std::uint8_t>
MakeCompressedDds(std::uint32_t four_cc, std::uint32_t width,
                  std::uint32_t height, std::uint32_t mip_count,
                  const std::vector<std::uint8_t> &payload,
                  std::uint32_t bytes_per_block,
                  std::uint32_t pixel_flags = kDdpfFourCc) {
  std::vector<std::uint8_t> bytes(128U, 0U);
  WriteU32LittleEndian(bytes, 0U, 0x20534444U);
  WriteU32LittleEndian(bytes, 4U, 124U);
  std::uint32_t header_flags =
      0x00000001U | 0x00000002U | 0x00000004U | 0x00001000U |
      0x00080000U;
  if (mip_count > 1U) {
    header_flags |= 0x00020000U;
  }
  WriteU32LittleEndian(bytes, 8U, header_flags);
  WriteU32LittleEndian(bytes, 12U, height);
  WriteU32LittleEndian(bytes, 16U, width);
  WriteU32LittleEndian(bytes, 20U, BlockBytes(width, height, bytes_per_block));
  WriteU32LittleEndian(bytes, 28U, mip_count);
  WriteU32LittleEndian(bytes, 76U, 32U);
  WriteU32LittleEndian(bytes, 80U, pixel_flags);
  WriteU32LittleEndian(bytes, 84U, four_cc);
  std::uint32_t caps = 0x00001000U;
  if (mip_count > 1U) {
    caps |= 0x00000008U | 0x00400000U;
  }
  WriteU32LittleEndian(bytes, 108U, caps);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

std::vector<std::uint8_t>
MakeUncompressedDds(std::uint32_t width, std::uint32_t height,
                    std::uint32_t mip_count,
                    const std::vector<std::uint8_t> &payload,
                    std::uint32_t red_mask, std::uint32_t green_mask,
                    std::uint32_t blue_mask, std::uint32_t alpha_mask) {
  std::vector<std::uint8_t> bytes(128U, 0U);
  WriteU32LittleEndian(bytes, 0U, 0x20534444U);
  WriteU32LittleEndian(bytes, 4U, 124U);
  std::uint32_t header_flags =
      0x00000001U | 0x00000002U | 0x00000004U | 0x00000008U |
      0x00001000U;
  if (mip_count > 1U) {
    header_flags |= 0x00020000U;
  }
  WriteU32LittleEndian(bytes, 8U, header_flags);
  WriteU32LittleEndian(bytes, 12U, height);
  WriteU32LittleEndian(bytes, 16U, width);
  WriteU32LittleEndian(bytes, 20U, width * 4U);
  WriteU32LittleEndian(bytes, 28U, mip_count);
  WriteU32LittleEndian(bytes, 76U, 32U);
  WriteU32LittleEndian(bytes, 80U,
                       kDdpfRgb | (alpha_mask != 0U ? kDdpfAlphaPixels : 0U));
  WriteU32LittleEndian(bytes, 88U, 32U);
  WriteU32LittleEndian(bytes, 92U, red_mask);
  WriteU32LittleEndian(bytes, 96U, green_mask);
  WriteU32LittleEndian(bytes, 100U, blue_mask);
  WriteU32LittleEndian(bytes, 104U, alpha_mask);
  std::uint32_t caps = 0x00001000U;
  if (mip_count > 1U) {
    caps |= 0x00000008U | 0x00400000U;
  }
  WriteU32LittleEndian(bytes, 108U, caps);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

Ogre14SourceTextureDecodeOptions LinearOptions() {
  Ogre14SourceTextureDecodeOptions options;
  options.color_semantic = Ogre14SourceTextureColorSemantic::LINEAR_DATA;
  return options;
}

Ogre14SourceTextureDecodeOptions Bc1Options(
    Ogre14SourceTextureBc1AlphaMode alpha_mode) {
  Ogre14SourceTextureDecodeOptions options = LinearOptions();
  options.bc1_alpha_mode = alpha_mode;
  return options;
}

std::uint32_t PackTwoBitIndices(
    const std::array<std::uint8_t, 16U> &indices) {
  std::uint32_t packed = 0U;
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    packed |= static_cast<std::uint32_t>(indices[texel] & 3U)
              << (texel * 2U);
  }
  return packed;
}

std::uint64_t PackThreeBitIndices(
    const std::array<std::uint8_t, 16U> &indices) {
  std::uint64_t packed = 0U;
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    packed |= static_cast<std::uint64_t>(indices[texel] & 7U)
              << (texel * 3U);
  }
  return packed;
}

std::vector<std::uint8_t>
MakeColorBlock(std::uint16_t color_zero, std::uint16_t color_one,
               const std::array<std::uint8_t, 16U> &indices) {
  std::vector<std::uint8_t> block;
  AppendU16LittleEndian(block, color_zero);
  AppendU16LittleEndian(block, color_one);
  AppendU32LittleEndian(block, PackTwoBitIndices(indices));
  return block;
}

std::vector<std::uint8_t>
MakeInterpolatedChannelBlock(std::uint8_t endpoint_zero,
                             std::uint8_t endpoint_one,
                             const std::array<std::uint8_t, 16U> &indices) {
  std::vector<std::uint8_t> block = {endpoint_zero, endpoint_one};
  const std::uint64_t packed = PackThreeBitIndices(indices);
  for (std::uint32_t byte = 0U; byte < 6U; ++byte) {
    block.push_back(
        static_cast<std::uint8_t>((packed >> (byte * 8U)) & 0xFFU));
  }
  return block;
}

std::array<std::uint8_t, 16U> RepeatingIndices(std::uint8_t modulus) {
  std::array<std::uint8_t, 16U> indices{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    indices[index] = static_cast<std::uint8_t>(index % modulus);
  }
  return indices;
}

void RequirePixel(const Ogre14DecodedSourceTextureMip &mip,
                  std::size_t texel, std::array<std::uint8_t, 4U> expected,
                  const char *message) {
  const std::size_t offset = texel * 4U;
  Require(offset <= mip.rgba8_unorm.size() &&
              mip.rgba8_unorm.size() - offset >= 4U,
          "test expected a decoded texel outside the mip payload");
  for (std::size_t channel = 0U; channel < 4U; ++channel) {
    if (mip.rgba8_unorm[offset + channel] != expected[channel]) {
      std::cerr << "FAIL: " << message << " channel " << channel
                << " expected " << static_cast<unsigned>(expected[channel])
                << " got "
                << static_cast<unsigned>(mip.rgba8_unorm[offset + channel])
                << '\n';
      std::exit(EXIT_FAILURE);
    }
  }
}

Ogre14DecodedSourceTexture SentinelOutput() {
  Ogre14DecodedSourceTexture output;
  output.width = 77U;
  output.height = 33U;
  output.source_format = Ogre14SourceTextureFormat::BC5_UNORM;
  output.color_semantic = Ogre14SourceTextureColorSemantic::SRGB_COLOR;
  output.bc1_alpha_mode = Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA;
  output.source_has_alpha = true;
  Ogre14DecodedSourceTextureMip mip;
  mip.width = 9U;
  mip.height = 8U;
  mip.row_pitch_bytes = 36U;
  mip.slice_pitch_bytes = 288U;
  mip.rgba8_unorm = {1U, 7U, 9U, 13U};
  output.mip_levels.push_back(std::move(mip));
  return output;
}

bool IsSentinel(const Ogre14DecodedSourceTexture &output) {
  return output.width == 77U && output.height == 33U &&
         output.source_format == Ogre14SourceTextureFormat::BC5_UNORM &&
         output.color_semantic ==
             Ogre14SourceTextureColorSemantic::SRGB_COLOR &&
         output.bc1_alpha_mode ==
             Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA &&
         output.source_has_alpha && output.mip_levels.size() == 1U &&
         output.mip_levels[0].width == 9U &&
         output.mip_levels[0].rgba8_unorm ==
             std::vector<std::uint8_t>({1U, 7U, 9U, 13U});
}

void ExpectFailureUnchanged(const std::vector<std::uint8_t> &bytes,
                            const Ogre14SourceTextureDecodeOptions &options,
                            const char *message) {
  Ogre14DecodedSourceTexture output = SentinelOutput();
  const RoR::Render::ValidationResult result =
      DecodeOgre14SourceTextureDds(bytes, options, output);
  Require(!result, message);
  Require(IsSentinel(output), "failure changed transactional decoder output");
}

void TestUncompressedLayoutsAndExternalSemantics() {
  struct Fixture final {
    std::uint32_t red_mask;
    std::uint32_t green_mask;
    std::uint32_t blue_mask;
    std::uint32_t alpha_mask;
    std::array<std::uint8_t, 4U> encoded;
    Ogre14SourceTextureFormat format;
  };
  const std::array<Fixture, 4U> fixtures = {{
      {0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0xFF000000U,
       {1U, 2U, 3U, 4U}, Ogre14SourceTextureFormat::RGBA8_UNORM},
      {0x000000FFU, 0x0000FF00U, 0x00FF0000U, 0U,
       {1U, 2U, 3U, 99U}, Ogre14SourceTextureFormat::RGBX8_UNORM},
      {0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0xFF000000U,
       {3U, 2U, 1U, 4U}, Ogre14SourceTextureFormat::BGRA8_UNORM},
      {0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0U,
       {3U, 2U, 1U, 99U}, Ogre14SourceTextureFormat::BGRX8_UNORM},
  }};
  for (const Fixture &fixture : fixtures) {
    const std::vector<std::uint8_t> payload(fixture.encoded.begin(),
                                            fixture.encoded.end());
    const std::vector<std::uint8_t> dds = MakeUncompressedDds(
        1U, 1U, 1U, payload, fixture.red_mask, fixture.green_mask,
        fixture.blue_mask, fixture.alpha_mask);
    Ogre14SourceTextureDecodeOptions options = LinearOptions();
    Ogre14DecodedSourceTexture output;
    Require(DecodeOgre14SourceTextureDds(dds, options, output).ok(),
            "supported uncompressed 32-bit DDS layout failed");
    Require(output.source_format == fixture.format,
            "uncompressed source layout classification changed");
    Require(output.mip_levels.size() == 1U,
            "uncompressed DDS did not produce one mip");
    const std::uint8_t expected_alpha =
        fixture.alpha_mask == 0U ? 255U : 4U;
    RequirePixel(output.mip_levels[0], 0U,
                 {1U, 2U, 3U, expected_alpha},
                 "uncompressed DDS channel mapping changed");

    options.color_semantic = Ogre14SourceTextureColorSemantic::SRGB_COLOR;
    Ogre14DecodedSourceTexture srgb;
    Require(DecodeOgre14SourceTextureDds(dds, options, srgb).ok(),
            "explicit sRGB semantic failed");
    Require(srgb.color_semantic ==
                Ogre14SourceTextureColorSemantic::SRGB_COLOR &&
                srgb.mip_levels[0].rgba8_unorm ==
                    output.mip_levels[0].rgba8_unorm,
            "decoder guessed or transformed the external color semantic");
  }
}

void TestFullUncompressedMipChain() {
  std::vector<std::uint8_t> payload(4U * 2U * 4U, 11U);
  payload.insert(payload.end(), 2U * 1U * 4U, 22U);
  payload.insert(payload.end(), 1U * 1U * 4U, 33U);
  const std::vector<std::uint8_t> dds = MakeUncompressedDds(
      4U, 2U, 3U, payload, 0x000000FFU, 0x0000FF00U, 0x00FF0000U,
      0xFF000000U);
  Ogre14DecodedSourceTexture output;
  Require(DecodeOgre14SourceTextureDds(dds, LinearOptions(), output).ok(),
          "full uncompressed mip chain failed");
  Require(output.mip_levels.size() == 3U,
          "declared mip chain was not decoded completely");
  const std::array<std::uint32_t, 3U> widths = {4U, 2U, 1U};
  const std::array<std::uint32_t, 3U> heights = {2U, 1U, 1U};
  const std::array<std::uint8_t, 3U> values = {11U, 22U, 33U};
  for (std::size_t level = 0U; level < 3U; ++level) {
    const Ogre14DecodedSourceTextureMip &mip = output.mip_levels[level];
    Require(mip.width == widths[level] && mip.height == heights[level],
            "decoded mip dimensions changed");
    Require(mip.row_pitch_bytes == widths[level] * 4U &&
                mip.slice_pitch_bytes ==
                    static_cast<std::uint64_t>(widths[level]) *
                        heights[level] * 4U &&
                mip.rgba8_unorm.size() == mip.slice_pitch_bytes,
            "decoded mip is not canonical tightly packed RGBA8");
    RequirePixel(mip, 0U,
                 {values[level], values[level], values[level], values[level]},
                 "decoded mip payload order changed");
  }
}

void TestBc1ModesAndEdgeClipping() {
  const std::array<std::uint8_t, 16U> indices = {
      0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U,
      0U, 1U, 2U, 3U, 0U, 1U, 2U, 3U};
  std::vector<std::uint8_t> block = MakeColorBlock(0xF800U, 0x001FU, indices);
  std::vector<std::uint8_t> dds =
      MakeCompressedDds(FourCc('D', 'X', 'T', '1'), 3U, 2U, 1U, block, 8U);
  Ogre14DecodedSourceTexture output;
  Require(DecodeOgre14SourceTextureDds(
              dds, Bc1Options(Ogre14SourceTextureBc1AlphaMode::OPAQUE),
              output)
              .ok(),
          "opaque BC1 decode failed");
  Require(output.mip_levels[0].rgba8_unorm.size() == 3U * 2U * 4U,
          "BC1 edge blocks were not clipped to virtual dimensions");
  RequirePixel(output.mip_levels[0], 0U, {255U, 0U, 0U, 255U},
               "BC1 endpoint zero changed");
  RequirePixel(output.mip_levels[0], 1U, {0U, 0U, 255U, 255U},
               "BC1 endpoint one changed");
  RequirePixel(output.mip_levels[0], 2U, {170U, 0U, 85U, 255U},
               "BC1 integer third interpolation changed");

  block = MakeColorBlock(0x001FU, 0xF800U, indices);
  dds = MakeCompressedDds(FourCc('D', 'X', 'T', '1'), 4U, 1U, 1U, block,
                          8U, kDdpfFourCc | kDdpfAlphaPixels);
  Require(DecodeOgre14SourceTextureDds(
              dds, Bc1Options(Ogre14SourceTextureBc1AlphaMode::OPAQUE),
              output)
              .ok(),
          "BC1 forced opaque endpoint-order mode failed");
  RequirePixel(output.mip_levels[0], 3U, {170U, 0U, 85U, 255U},
               "BC1 opaque mode incorrectly produced transparency");
  Require(DecodeOgre14SourceTextureDds(
              dds,
              Bc1Options(Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA),
              output)
              .ok(),
          "BC1 one-bit-alpha mode failed");
  RequirePixel(output.mip_levels[0], 2U, {127U, 0U, 127U, 255U},
               "BC1 one-bit-alpha midpoint changed");
  RequirePixel(output.mip_levels[0], 3U, {0U, 0U, 0U, 0U},
               "BC1 one-bit transparent selector changed");
}

void TestBc2ExplicitAlpha() {
  std::uint64_t alpha = 0U;
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    alpha |= static_cast<std::uint64_t>(texel) << (texel * 4U);
  }
  std::vector<std::uint8_t> block;
  AppendU64LittleEndian(block, alpha);
  const std::vector<std::uint8_t> colors =
      MakeColorBlock(0x0000U, 0xFFFFU, RepeatingIndices(4U));
  block.insert(block.end(), colors.begin(), colors.end());
  const std::vector<std::uint8_t> dds = MakeCompressedDds(
      FourCc('D', 'X', 'T', '3'), 4U, 4U, 1U, block, 16U);
  Ogre14DecodedSourceTexture output;
  Require(DecodeOgre14SourceTextureDds(dds, LinearOptions(), output).ok(),
          "BC2/DXT3 decode failed");
  for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
    const std::size_t alpha_offset = texel * 4U + 3U;
    Require(output.mip_levels[0].rgba8_unorm[alpha_offset] == texel * 17U,
            "BC2 four-bit alpha expansion changed");
  }
  RequirePixel(output.mip_levels[0], 3U, {170U, 170U, 170U, 51U},
               "BC2 color block incorrectly entered BC1 transparent mode");
}

void TestBc3BothAlphaInterpolationModes() {
  const std::array<std::uint8_t, 16U> alpha_indices = RepeatingIndices(8U);
  const std::vector<std::uint8_t> color =
      MakeColorBlock(0xFFFFU, 0xFFFFU, RepeatingIndices(1U));
  struct Fixture final {
    std::uint8_t endpoint_zero;
    std::uint8_t endpoint_one;
    std::array<std::uint8_t, 8U> expected;
  };
  const std::array<Fixture, 2U> fixtures = {{
      {255U, 0U, {255U, 0U, 218U, 182U, 145U, 109U, 72U, 36U}},
      {0U, 255U, {0U, 255U, 51U, 102U, 153U, 204U, 0U, 255U}},
  }};
  for (const Fixture &fixture : fixtures) {
    std::vector<std::uint8_t> block = MakeInterpolatedChannelBlock(
        fixture.endpoint_zero, fixture.endpoint_one, alpha_indices);
    block.insert(block.end(), color.begin(), color.end());
    const std::vector<std::uint8_t> dds = MakeCompressedDds(
        FourCc('D', 'X', 'T', '5'), 4U, 4U, 1U, block, 16U);
    Ogre14DecodedSourceTexture output;
    Require(DecodeOgre14SourceTextureDds(dds, LinearOptions(), output).ok(),
            "BC3/DXT5 alpha mode failed");
    for (std::size_t texel = 0U; texel < fixture.expected.size(); ++texel) {
      Require(output.mip_levels[0].rgba8_unorm[texel * 4U + 3U] ==
                  fixture.expected[texel],
              "BC3 integer alpha interpolation changed");
    }
  }
}

void TestBc4Bc5AndLegacyAliases() {
  const std::array<std::uint8_t, 16U> indices = RepeatingIndices(8U);
  const std::vector<std::uint8_t> channel =
      MakeInterpolatedChannelBlock(10U, 20U, indices);
  for (const std::uint32_t four_cc :
       {FourCc('A', 'T', 'I', '1'), FourCc('B', 'C', '4', 'U')}) {
    const std::vector<std::uint8_t> dds =
        MakeCompressedDds(four_cc, 4U, 4U, 1U, channel, 8U);
    Ogre14DecodedSourceTexture output;
    Require(DecodeOgre14SourceTextureDds(dds, LinearOptions(), output).ok(),
            "BC4/ATI1 unsigned decode failed");
    Require(output.source_format == Ogre14SourceTextureFormat::BC4_UNORM,
            "BC4 alias did not canonicalize to BC4_UNORM");
    RequirePixel(output.mip_levels[0], 0U, {10U, 0U, 0U, 255U},
                 "BC4 channel mapping changed");
    RequirePixel(output.mip_levels[0], 1U, {20U, 0U, 0U, 255U},
                 "BC4 endpoint one mapping changed");
    RequirePixel(output.mip_levels[0], 2U, {12U, 0U, 0U, 255U},
                 "BC4 five-step integer interpolation changed");
  }

  const std::vector<std::uint8_t> red = MakeInterpolatedChannelBlock(
      255U, 0U, RepeatingIndices(1U));
  const std::vector<std::uint8_t> green = MakeInterpolatedChannelBlock(
      0U, 255U, std::array<std::uint8_t, 16U>{1U, 1U, 1U, 1U, 1U, 1U,
                                              1U, 1U, 1U, 1U, 1U, 1U,
                                              1U, 1U, 1U, 1U});
  std::vector<std::uint8_t> block = red;
  block.insert(block.end(), green.begin(), green.end());
  for (const std::uint32_t four_cc :
       {FourCc('A', 'T', 'I', '2'), FourCc('B', 'C', '5', 'U')}) {
    const std::vector<std::uint8_t> dds =
        MakeCompressedDds(four_cc, 4U, 4U, 1U, block, 16U);
    Ogre14DecodedSourceTexture output;
    Require(DecodeOgre14SourceTextureDds(dds, LinearOptions(), output).ok(),
            "BC5/ATI2 unsigned decode failed");
    Require(output.source_format == Ogre14SourceTextureFormat::BC5_UNORM,
            "BC5 alias did not canonicalize to BC5_UNORM");
    RequirePixel(output.mip_levels[0], 0U, {255U, 255U, 0U, 255U},
                 "BC5 RG channel mapping changed");
  }
}

void TestCompressedMipChain() {
  const std::vector<std::uint8_t> red = MakeColorBlock(
      0xF800U, 0xF800U, RepeatingIndices(1U));
  const std::vector<std::uint8_t> blue = MakeColorBlock(
      0x001FU, 0x001FU, RepeatingIndices(1U));
  const std::vector<std::uint8_t> green = MakeColorBlock(
      0x07E0U, 0x07E0U, RepeatingIndices(1U));
  std::vector<std::uint8_t> payload;
  payload.insert(payload.end(), red.begin(), red.end());
  payload.insert(payload.end(), red.begin(), red.end());
  payload.insert(payload.end(), blue.begin(), blue.end());
  payload.insert(payload.end(), green.begin(), green.end());
  const std::vector<std::uint8_t> dds = MakeCompressedDds(
      FourCc('D', 'X', 'T', '1'), 5U, 3U, 3U, payload, 8U);
  Ogre14DecodedSourceTexture output;
  Require(DecodeOgre14SourceTextureDds(
              dds, Bc1Options(Ogre14SourceTextureBc1AlphaMode::OPAQUE),
              output)
              .ok(),
          "compressed edge-clipped mip chain failed");
  Require(output.mip_levels.size() == 3U,
          "compressed mip chain was not decoded completely");
  Require(output.mip_levels[0].rgba8_unorm.size() == 5U * 3U * 4U &&
              output.mip_levels[1].rgba8_unorm.size() == 2U * 1U * 4U &&
              output.mip_levels[2].rgba8_unorm.size() == 1U * 1U * 4U,
          "compressed mip outputs are not tightly edge-clipped");
  RequirePixel(output.mip_levels[0], 0U, {255U, 0U, 0U, 255U},
               "compressed top mip changed");
  RequirePixel(output.mip_levels[1], 0U, {0U, 0U, 255U, 255U},
               "compressed second mip offset changed");
  RequirePixel(output.mip_levels[2], 0U, {0U, 255U, 0U, 255U},
               "compressed final mip offset changed");
}

void TestStrictContainerAndLimitRejection() {
  const std::vector<std::uint8_t> block = MakeColorBlock(
      0xFFFFU, 0x0000U, RepeatingIndices(4U));
  const std::vector<std::uint8_t> valid = MakeCompressedDds(
      FourCc('D', 'X', 'T', '1'), 4U, 4U, 1U, block, 8U);
  const Ogre14SourceTextureDecodeOptions bc1 =
      Bc1Options(Ogre14SourceTextureBc1AlphaMode::OPAQUE);

  Ogre14SourceTextureDecodeOptions options = bc1;
  options.version = 99U;
  ExpectFailureUnchanged(valid, options, "options version was accepted");
  options = bc1;
  options.color_semantic = Ogre14SourceTextureColorSemantic::UNSPECIFIED;
  ExpectFailureUnchanged(valid, options,
                         "unspecified color semantic was accepted");
  options = bc1;
  options.color_semantic =
      static_cast<Ogre14SourceTextureColorSemantic>(255U);
  ExpectFailureUnchanged(valid, options,
                         "unknown color semantic was accepted");
  options = bc1;
  options.bc1_alpha_mode =
      static_cast<Ogre14SourceTextureBc1AlphaMode>(255U);
  ExpectFailureUnchanged(valid, options, "unknown BC1 mode was accepted");
  options = bc1;
  options.maximum_dimension = 3U;
  ExpectFailureUnchanged(valid, options, "dimension cap was bypassed");
  options = bc1;
  options.maximum_mip_levels = 0U;
  ExpectFailureUnchanged(valid, options, "zero mip cap was accepted");
  options = bc1;
  options.maximum_encoded_bytes = 128U;
  ExpectFailureUnchanged(valid, options, "encoded-byte cap was bypassed");
  options = bc1;
  options.maximum_decoded_bytes = 63U;
  ExpectFailureUnchanged(valid, options, "decoded-byte cap was bypassed");

  std::vector<std::uint8_t> changed = valid;
  changed.resize(127U);
  ExpectFailureUnchanged(changed, bc1, "truncated header was accepted");
  changed = valid;
  changed.pop_back();
  ExpectFailureUnchanged(changed, bc1, "truncated mip payload was accepted");
  changed = valid;
  changed.push_back(0U);
  ExpectFailureUnchanged(changed, bc1, "trailing DDS byte was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 0U, 0U);
  ExpectFailureUnchanged(changed, bc1, "bad DDS magic was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 4U, 123U);
  ExpectFailureUnchanged(changed, bc1, "bad DDS_HEADER size was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 76U, 31U);
  ExpectFailureUnchanged(changed, bc1,
                         "bad DDS_PIXELFORMAT size was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 8U,
                       0x00000001U | 0x00000002U | 0x00001000U |
                           0x00080000U);
  ExpectFailureUnchanged(changed, bc1, "missing width flag was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 8U,
                       0x00000001U | 0x00000002U | 0x00000004U |
                           0x00001000U | 0x00080000U | 0x40000000U);
  ExpectFailureUnchanged(changed, bc1,
                         "unknown DDS header flag was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 16U, 0U);
  ExpectFailureUnchanged(changed, bc1, "zero width was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 16U,
                       (std::numeric_limits<std::uint32_t>::max)());
  ExpectFailureUnchanged(changed, bc1, "overflow-scale width was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 28U, 99U);
  ExpectFailureUnchanged(changed, bc1,
                         "impossible geometric mip count was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 8U,
                       0x00000001U | 0x00000002U | 0x00000004U |
                           0x00000008U | 0x00001000U | 0x00080000U);
  ExpectFailureUnchanged(changed, bc1,
                         "simultaneous pitch and linear size was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 20U, 7U);
  ExpectFailureUnchanged(changed, bc1,
                         "inconsistent compressed linear size was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 112U, 0x00000200U | 0x00000400U);
  ExpectFailureUnchanged(changed, bc1, "cube map was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 112U, 0x00200000U);
  ExpectFailureUnchanged(changed, bc1, "volume caps were accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 8U,
                       0x00000001U | 0x00000002U | 0x00000004U |
                           0x00001000U | 0x00080000U | 0x00800000U);
  WriteU32LittleEndian(changed, 24U, 2U);
  ExpectFailureUnchanged(changed, bc1, "volume depth was accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 112U, 0x00000001U);
  ExpectFailureUnchanged(changed, bc1,
                         "unknown secondary caps were accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 108U, 0x80001000U);
  ExpectFailureUnchanged(changed, bc1, "unknown primary caps were accepted");
  changed = valid;
  WriteU32LittleEndian(changed, 84U, FourCc('D', 'X', '1', '0'));
  ExpectFailureUnchanged(changed, bc1,
                         "DX10 extension/array container was accepted");
  for (const std::uint32_t unsupported :
       {FourCc('D', 'X', 'T', '2'), FourCc('D', 'X', 'T', '4'),
        FourCc('B', 'C', '4', 'S'), FourCc('B', 'C', '5', 'S')}) {
    changed = valid;
    WriteU32LittleEndian(changed, 84U, unsupported);
    ExpectFailureUnchanged(changed, bc1,
                           "unsupported or signed FourCC was accepted");
  }
  changed = valid;
  WriteU32LittleEndian(changed, 92U, 0xFFU);
  ExpectFailureUnchanged(changed, bc1,
                         "ambiguous compressed RGB masks were accepted");

  options = LinearOptions();
  ExpectFailureUnchanged(valid, options,
                         "BC1 without explicit alpha mode was accepted");
  const std::vector<std::uint8_t> bc3 = MakeCompressedDds(
      FourCc('D', 'X', 'T', '5'), 4U, 4U, 1U,
      std::vector<std::uint8_t>(16U, 0U), 16U);
  ExpectFailureUnchanged(
      bc3, bc1, "BC1 alpha interpretation leaked into a non-BC1 format");

  const std::vector<std::uint8_t> rgba = MakeUncompressedDds(
      1U, 1U, 1U, {1U, 2U, 3U, 4U}, 0x000000FFU, 0x0000FF00U,
      0x00FF0000U, 0xFF000000U);
  changed = rgba;
  WriteU32LittleEndian(changed, 88U, 24U);
  ExpectFailureUnchanged(changed, LinearOptions(),
                         "uncompressed 24-bit DDS was accepted");
  changed = rgba;
  WriteU32LittleEndian(changed, 92U, 0x0000FF00U);
  ExpectFailureUnchanged(changed, LinearOptions(),
                         "overlapping channel masks were accepted");
  changed = rgba;
  WriteU32LittleEndian(changed, 20U, 5U);
  ExpectFailureUnchanged(changed, LinearOptions(),
                         "inconsistent uncompressed pitch was accepted");
  changed = rgba;
  WriteU32LittleEndian(changed, 80U, kDdpfRgb);
  ExpectFailureUnchanged(changed, LinearOptions(),
                         "alpha mask without alpha flag was accepted");
}

class ThrowingFaultInjector final
    : public IOgre14SourceTextureDecoderFaultInjector {
public:
  Ogre14SourceTextureDecoderFaultStage stage =
      Ogre14SourceTextureDecoderFaultStage::AFTER_HEADER_VALIDATION;
  bool bad_alloc = false;

  void BeforeDecoderStage(
      Ogre14SourceTextureDecoderFaultStage current) override {
    if (current != stage) {
      return;
    }
    if (bad_alloc) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void TestTransactionalExceptionRollback() {
  const std::vector<std::uint8_t> block = MakeColorBlock(
      0xFFFFU, 0x0000U, RepeatingIndices(4U));
  const std::vector<std::uint8_t> valid = MakeCompressedDds(
      FourCc('D', 'X', 'T', '1'), 4U, 4U, 1U, block, 8U);
  const Ogre14SourceTextureDecodeOptions options =
      Bc1Options(Ogre14SourceTextureBc1AlphaMode::OPAQUE);
  for (const Ogre14SourceTextureDecoderFaultStage stage :
       {Ogre14SourceTextureDecoderFaultStage::AFTER_HEADER_VALIDATION,
        Ogre14SourceTextureDecoderFaultStage::AFTER_FIRST_MIP_DECODE,
        Ogre14SourceTextureDecoderFaultStage::BEFORE_COMMIT}) {
    for (const bool bad_alloc : {false, true}) {
      ThrowingFaultInjector fault;
      fault.stage = stage;
      fault.bad_alloc = bad_alloc;
      Ogre14DecodedSourceTexture output = SentinelOutput();
      const RoR::Render::ValidationResult result =
          DecodeOgre14SourceTextureDds(valid, options, output, &fault);
      Require(!result, "fault-injected source texture decode succeeded");
      Require(IsSentinel(output),
              "bad_alloc/unexpected exception changed decoder output");
    }
  }

  Ogre14DecodedSourceTexture output = SentinelOutput();
  Require(DecodeOgre14SourceTextureDds(valid, options, output).ok(),
          "valid decode did not replace the prior output");
  Require(!IsSentinel(output) && output.width == 4U,
          "successful source texture commit did not publish atomically");
}

} // namespace

int main() {
  static_assert(
      std::is_nothrow_move_assignable<Ogre14DecodedSourceTexture>::value,
      "decoder output must have an infallible commit operation");
  TestUncompressedLayoutsAndExternalSemantics();
  TestFullUncompressedMipChain();
  TestBc1ModesAndEdgeClipping();
  TestBc2ExplicitAlpha();
  TestBc3BothAlphaInterpolationModes();
  TestBc4Bc5AndLegacyAliases();
  TestCompressedMipChain();
  TestStrictContainerAndLimitRejection();
  TestTransactionalExceptionRollback();
  std::cout << "OGRE14 source texture decoder tests passed\n";
  return EXIT_SUCCESS;
}
