/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyMaterialSemanticCatalogV2.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{
    {'R', 'O', 'R', 'M', 'A', 'T', '2', '\0'}};
constexpr std::size_t kMaximumExactKeyComponentBytes = 255U;
constexpr std::size_t kMaximumSourceMemberBytes = 4096U;
constexpr std::size_t kMaximumLoweringAlgorithmBytes = 128U;
constexpr std::uint16_t kNoTextureUnit = 0xFFFFU;

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) noexcept {
  return (value >> count) | (value << (32U - count));
}

Ogre14LegacySha256 Sha256(const std::uint8_t *bytes,
                         std::size_t size) noexcept {
  static constexpr std::array<std::uint32_t, 64U> kRoundConstants{{
      0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
      0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
      0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
      0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
      0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
      0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
      0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
      0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
      0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
      0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
      0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
      0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
      0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
      0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
      0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
      0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
  }};
  std::array<std::uint32_t, 8U> state{{
      0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
      0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
  }};

  auto Transform = [&state](const std::uint8_t *block) noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t s0 =
          RotateRight(words[index - 15U], 7U) ^
          RotateRight(words[index - 15U], 18U) ^
          (words[index - 15U] >> 3U);
      const std::uint32_t s1 =
          RotateRight(words[index - 2U], 17U) ^
          RotateRight(words[index - 2U], 19U) ^
          (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }
    std::uint32_t a = state[0U];
    std::uint32_t b = state[1U];
    std::uint32_t c = state[2U];
    std::uint32_t d = state[3U];
    std::uint32_t e = state[4U];
    std::uint32_t f = state[5U];
    std::uint32_t g = state[6U];
    std::uint32_t h = state[7U];
    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t sum_one =
          RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary_one =
          h + sum_one + choose + kRoundConstants[index] + words[index];
      const std::uint32_t sum_zero =
          RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary_two = sum_zero + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary_one;
      d = c;
      c = b;
      b = a;
      a = temporary_one + temporary_two;
    }
    state[0U] += a;
    state[1U] += b;
    state[2U] += c;
    state[3U] += d;
    state[4U] += e;
    state[5U] += f;
    state[6U] += g;
    state[7U] += h;
  };

  std::size_t offset = 0U;
  while (size - offset >= 64U) {
    Transform(bytes + offset);
    offset += 64U;
  }
  std::array<std::uint8_t, 128U> tail{};
  const std::size_t remaining = size - offset;
  if (remaining != 0U) {
    std::memcpy(tail.data(), bytes + offset, remaining);
  }
  tail[remaining] = 0x80U;
  const std::size_t padded = remaining < 56U ? 64U : 128U;
  const std::uint64_t bit_size = static_cast<std::uint64_t>(size) * 8U;
  for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
    tail[padded - 1U - byte] =
        static_cast<std::uint8_t>(bit_size >> (byte * 8U));
  }
  Transform(tail.data());
  if (padded == 128U) {
    Transform(tail.data() + 64U);
  }
  Ogre14LegacySha256 digest{};
  for (std::size_t index = 0U; index < state.size(); ++index) {
    digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
    digest[index * 4U + 1U] =
        static_cast<std::uint8_t>(state[index] >> 16U);
    digest[index * 4U + 2U] =
        static_cast<std::uint8_t>(state[index] >> 8U);
    digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
  }
  return digest;
}

bool IsValidUtf8WithoutNul(const std::string &value) noexcept {
  std::size_t index = 0U;
  while (index < value.size()) {
    const std::uint8_t first = static_cast<std::uint8_t>(value[index]);
    if (first == 0U) {
      return false;
    }
    if (first <= 0x7FU) {
      ++index;
      continue;
    }
    std::size_t continuation_count = 0U;
    std::uint32_t code_point = 0U;
    if ((first & 0xE0U) == 0xC0U) {
      continuation_count = 1U;
      code_point = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
      continuation_count = 2U;
      code_point = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
      continuation_count = 3U;
      code_point = first & 0x07U;
    } else {
      return false;
    }
    if (index + continuation_count >= value.size()) {
      return false;
    }
    for (std::size_t continuation = 0U; continuation < continuation_count;
         ++continuation) {
      const std::uint8_t byte =
          static_cast<std::uint8_t>(value[index + continuation + 1U]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    if ((continuation_count == 1U && code_point < 0x80U) ||
        (continuation_count == 2U && code_point < 0x800U) ||
        (continuation_count == 3U && code_point < 0x10000U) ||
        code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1U;
  }
  return true;
}

bool IsFiniteFloatBits(std::uint32_t bits) noexcept {
  return (bits & 0x7F800000U) != 0x7F800000U;
}

float FloatFromBits(std::uint32_t bits) noexcept {
  float value = 0.0F;
  static_assert(sizeof(value) == sizeof(bits), "float32 is required");
  static_assert(std::numeric_limits<float>::is_iec559,
                "IEEE-754 float32 is required");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool ByteStringLess(const std::string &lhs, const std::string &rhs) noexcept {
  const std::size_t shared = (std::min)(lhs.size(), rhs.size());
  for (std::size_t index = 0U; index < shared; ++index) {
    const std::uint8_t lhs_byte = static_cast<std::uint8_t>(lhs[index]);
    const std::uint8_t rhs_byte = static_cast<std::uint8_t>(rhs[index]);
    if (lhs_byte != rhs_byte) {
      return lhs_byte < rhs_byte;
    }
  }
  return lhs.size() < rhs.size();
}

class Reader final {
public:
  Reader(const std::uint8_t *begin, std::size_t size,
         std::uint64_t maximum_string_bytes) noexcept
      : begin_(begin), size_(size), maximum_string_bytes_(maximum_string_bytes) {}

  [[nodiscard]] bool ReadU8(std::uint8_t &value) noexcept {
    if (!Require(1U)) {
      return false;
    }
    value = begin_[offset_++];
    return true;
  }

  [[nodiscard]] bool ReadU16(std::uint16_t &value) noexcept {
    if (!Require(2U)) {
      return false;
    }
    value = static_cast<std::uint16_t>(begin_[offset_]) |
            (static_cast<std::uint16_t>(begin_[offset_ + 1U]) << 8U);
    offset_ += 2U;
    return true;
  }

  [[nodiscard]] bool ReadU32(std::uint32_t &value) noexcept {
    if (!Require(4U)) {
      return false;
    }
    value = 0U;
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
      value |= static_cast<std::uint32_t>(begin_[offset_ + byte]) <<
               (byte * 8U);
    }
    offset_ += 4U;
    return true;
  }

  [[nodiscard]] bool ReadU64(std::uint64_t &value) noexcept {
    if (!Require(8U)) {
      return false;
    }
    value = 0U;
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
      value |= static_cast<std::uint64_t>(begin_[offset_ + byte]) <<
               (byte * 8U);
    }
    offset_ += 8U;
    return true;
  }

  [[nodiscard]] bool ReadDigest(Ogre14LegacySha256 &digest) noexcept {
    if (!Require(digest.size())) {
      return false;
    }
    std::copy_n(begin_ + offset_, digest.size(), digest.begin());
    offset_ += digest.size();
    return true;
  }

  [[nodiscard]] bool ReadString(std::string &value,
                                std::size_t maximum_length) {
    std::uint16_t length = 0U;
    if (!ReadU16(length)) {
      return false;
    }
    if (length == 0U || length > maximum_length) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "semantic_catalog.string.length",
           "catalog string length is zero or exceeds its field cap");
      return false;
    }
    if (string_bytes_ >
            (std::numeric_limits<std::uint64_t>::max)() - length ||
        string_bytes_ + length > maximum_string_bytes_) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "semantic_catalog.string_bytes",
           "catalog strings exceed the configured aggregate cap");
      return false;
    }
    if (!Require(length)) {
      return false;
    }
    std::string candidate(reinterpret_cast<const char *>(begin_ + offset_),
                          length);
    if (!IsValidUtf8WithoutNul(candidate)) {
      Fail(ValidationCode::INVALID_IDENTIFIER, "semantic_catalog.string",
           "catalog string is not canonical UTF-8 or contains NUL");
      return false;
    }
    offset_ += length;
    string_bytes_ += length;
    value = std::move(candidate);
    return true;
  }

  [[nodiscard]] bool finished() const noexcept { return offset_ == size_; }
  [[nodiscard]] const ValidationResult &error() const noexcept { return error_; }

  void Invalid(ValidationCode code, const char *field,
               const char *detail) {
    Fail(code, field, detail);
  }

private:
  [[nodiscard]] bool Require(std::size_t count) noexcept {
    if (count > size_ - offset_) {
      Fail(ValidationCode::SIZE_MISMATCH, "semantic_catalog.payload",
           "compiled catalog payload ended before a declared field");
      return false;
    }
    return true;
  }

  void Fail(ValidationCode code, const char *field,
            const char *detail) noexcept {
    if (error_) {
      try {
        error_ = Failure(code, field, detail);
      } catch (...) {
        error_.code = code;
      }
    }
  }

  const std::uint8_t *begin_ = nullptr;
  std::size_t size_ = 0U;
  std::size_t offset_ = 0U;
  std::uint64_t string_bytes_ = 0U;
  std::uint64_t maximum_string_bytes_ = 0U;
  ValidationResult error_;
};

template <typename Enum>
bool ReadEnum(Reader &reader, Enum &output, std::uint8_t maximum,
              const char *field) {
  std::uint8_t value = 0U;
  if (!reader.ReadU8(value)) {
    return false;
  }
  if (value > maximum) {
    reader.Invalid(ValidationCode::INVALID_ENUM, field,
                   "compiled catalog contains an unknown enum value");
    return false;
  }
  output = static_cast<Enum>(value);
  return true;
}

bool ReadBool(Reader &reader, bool &output, const char *field) {
  std::uint8_t value = 0U;
  if (!reader.ReadU8(value)) {
    return false;
  }
  if (value > 1U) {
    reader.Invalid(ValidationCode::INVALID_ENUM, field,
                   "compiled catalog boolean is not zero or one");
    return false;
  }
  output = value != 0U;
  return true;
}

bool ReadFloatBits(Reader &reader, std::uint32_t &output,
                   const char *field) {
  if (!reader.ReadU32(output)) {
    return false;
  }
  if (!IsFiniteFloatBits(output)) {
    reader.Invalid(ValidationCode::NON_FINITE_VALUE, field,
                   "compiled catalog contains non-finite float32 bits");
    return false;
  }
  return true;
}

bool ReadPassFacts(Reader &reader, Ogre14LegacyCatalogPassFacts &pass) {
  if (!ReadEnum(reader, pass.source_color, 9U, "semantic_catalog.pass.source_color") ||
      !ReadEnum(reader, pass.destination_color, 9U,
                "semantic_catalog.pass.destination_color") ||
      !ReadEnum(reader, pass.source_alpha, 9U, "semantic_catalog.pass.source_alpha") ||
      !ReadEnum(reader, pass.destination_alpha, 9U,
                "semantic_catalog.pass.destination_alpha") ||
      !ReadEnum(reader, pass.color_operation, 4U,
                "semantic_catalog.pass.color_operation") ||
      !ReadEnum(reader, pass.alpha_operation, 4U,
                "semantic_catalog.pass.alpha_operation") ||
      !reader.ReadU8(pass.color_write_mask) ||
      !ReadBool(reader, pass.depth_check_enabled,
                "semantic_catalog.pass.depth_check_enabled") ||
      !ReadBool(reader, pass.depth_write_enabled,
                "semantic_catalog.pass.depth_write_enabled") ||
      !ReadEnum(reader, pass.depth_compare, 7U,
                "semantic_catalog.pass.depth_compare") ||
      !ReadFloatBits(reader, pass.constant_depth_bias_f32_bits,
                     "semantic_catalog.pass.constant_depth_bias") ||
      !ReadFloatBits(reader, pass.slope_scale_depth_bias_f32_bits,
                     "semantic_catalog.pass.slope_scale_depth_bias") ||
      !ReadFloatBits(reader, pass.iteration_depth_bias_f32_bits,
                     "semantic_catalog.pass.iteration_depth_bias") ||
      !ReadEnum(reader, pass.cull, 2U, "semantic_catalog.pass.cull") ||
      !ReadEnum(reader, pass.manual_cull, 2U,
                "semantic_catalog.pass.manual_cull") ||
      !ReadEnum(reader, pass.alpha_reject, 7U,
                "semantic_catalog.pass.alpha_reject") ||
      !reader.ReadU8(pass.alpha_reject_value) ||
      !ReadBool(reader, pass.alpha_to_coverage,
                "semantic_catalog.pass.alpha_to_coverage") ||
      !ReadBool(reader, pass.solid_fill,
                "semantic_catalog.pass.solid_fill") ||
      !reader.ReadU32(pass.pass_iteration_count)) {
    return false;
  }
  if (pass.color_write_mask == 0U || pass.color_write_mask > 0x0FU ||
      pass.pass_iteration_count == 0U) {
    reader.Invalid(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_catalog.pass",
                   "pass write mask or iteration count is outside its range");
    return false;
  }
  return true;
}

bool ReadTextureUnit(Reader &reader,
                     Ogre14LegacyCatalogTextureUnitFacts &unit,
                     std::uint16_t expected_ordinal) {
  if (!reader.ReadU16(unit.ordinal) ||
      !reader.ReadString(unit.exact_unit_name,
                         kMaximumExactKeyComponentBytes) ||
      !reader.ReadString(unit.texture_key.exact_resource_group,
                         kMaximumExactKeyComponentBytes) ||
      !reader.ReadString(unit.texture_key.exact_name,
                         kMaximumExactKeyComponentBytes) ||
      !ReadEnum(reader, unit.semantic, 6U,
                "semantic_catalog.texture_unit.semantic") ||
      !ReadEnum(reader, unit.color_role, 1U,
                "semantic_catalog.texture_unit.color_role")) {
    return false;
  }
  if (unit.ordinal != expected_ordinal) {
    reader.Invalid(ValidationCode::NON_DETERMINISTIC_ORDER,
                   "semantic_catalog.texture_unit.ordinal",
                   "texture units are not in exact ordinal order");
    return false;
  }
  for (Ogre14LegacyTextureSwizzle &channel : unit.swizzle) {
    if (!ReadEnum(reader, channel, 5U,
                  "semantic_catalog.texture_unit.swizzle")) {
      return false;
    }
  }
  if (!reader.ReadU8(unit.texture_coordinate_set) ||
      !ReadBool(reader, unit.projective,
                "semantic_catalog.texture_unit.projective")) {
    return false;
  }
  for (std::uint32_t &bits : unit.uv_transform_f32_bits) {
    if (!ReadFloatBits(reader, bits,
                       "semantic_catalog.texture_unit.uv_transform")) {
      return false;
    }
  }
  Ogre14LegacyCatalogSamplerFacts &sampler = unit.sampler;
  if (!ReadEnum(reader, sampler.minification, 3U,
                "semantic_catalog.sampler.minification") ||
      !ReadEnum(reader, sampler.magnification, 3U,
                "semantic_catalog.sampler.magnification") ||
      !ReadEnum(reader, sampler.mip, 3U, "semantic_catalog.sampler.mip") ||
      !ReadEnum(reader, sampler.address_u, 3U,
                "semantic_catalog.sampler.address_u") ||
      !ReadEnum(reader, sampler.address_v, 3U,
                "semantic_catalog.sampler.address_v") ||
      !ReadEnum(reader, sampler.address_w, 3U,
                "semantic_catalog.sampler.address_w") ||
      !ReadFloatBits(reader, sampler.mip_lod_bias_f32_bits,
                     "semantic_catalog.sampler.mip_lod_bias") ||
      !ReadFloatBits(reader, sampler.minimum_lod_f32_bits,
                     "semantic_catalog.sampler.minimum_lod") ||
      !ReadFloatBits(reader, sampler.maximum_lod_f32_bits,
                     "semantic_catalog.sampler.maximum_lod") ||
      !reader.ReadU32(sampler.maximum_anisotropy) ||
      !ReadBool(reader, sampler.compare_enabled,
                "semantic_catalog.sampler.compare_enabled") ||
      !ReadEnum(reader, sampler.compare_operation, 7U,
                "semantic_catalog.sampler.compare_operation")) {
    return false;
  }
  for (std::uint32_t &bits : sampler.border_color_f32_bits) {
    if (!ReadFloatBits(reader, bits,
                       "semantic_catalog.sampler.border_color")) {
      return false;
    }
  }
  if (sampler.maximum_anisotropy == 0U ||
      sampler.maximum_anisotropy > 16U ||
      FloatFromBits(sampler.minimum_lod_f32_bits) >
          FloatFromBits(sampler.maximum_lod_f32_bits)) {
    reader.Invalid(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_catalog.sampler",
                   "sampler anisotropy or LOD range is invalid");
    return false;
  }
  Ogre14LegacyCatalogCombineFacts &combine = unit.combine;
  if (!ReadEnum(reader, combine.color_operation, 5U,
                "semantic_catalog.combine.color_operation") ||
      !ReadEnum(reader, combine.color_source_one, 4U,
                "semantic_catalog.combine.color_source_one") ||
      !ReadEnum(reader, combine.color_source_two, 4U,
                "semantic_catalog.combine.color_source_two") ||
      !ReadEnum(reader, combine.alpha_operation, 5U,
                "semantic_catalog.combine.alpha_operation") ||
      !ReadEnum(reader, combine.alpha_source_one, 4U,
                "semantic_catalog.combine.alpha_source_one") ||
      !ReadEnum(reader, combine.alpha_source_two, 4U,
                "semantic_catalog.combine.alpha_source_two")) {
    return false;
  }
  for (std::uint32_t &bits : combine.color_manual_one_f32_bits) {
    if (!ReadFloatBits(reader, bits,
                       "semantic_catalog.combine.color_manual_one")) {
      return false;
    }
  }
  for (std::uint32_t &bits : combine.color_manual_two_f32_bits) {
    if (!ReadFloatBits(reader, bits,
                       "semantic_catalog.combine.color_manual_two")) {
      return false;
    }
  }
  return ReadFloatBits(reader, combine.color_manual_factor_f32_bits,
                       "semantic_catalog.combine.color_manual_factor") &&
         ReadFloatBits(reader, combine.alpha_manual_one_f32_bits,
                       "semantic_catalog.combine.alpha_manual_one") &&
         ReadFloatBits(reader, combine.alpha_manual_two_f32_bits,
                       "semantic_catalog.combine.alpha_manual_two") &&
         ReadFloatBits(reader, combine.alpha_manual_factor_f32_bits,
                       "semantic_catalog.combine.alpha_manual_factor");
}

bool ReadRecord(Reader &reader,
                const Ogre14LegacyMaterialSemanticCatalogV2Configuration &configuration,
                Ogre14LegacyMaterialSemanticCatalogV2Record &record) {
  if (!reader.ReadDigest(record.package_archive_sha256) ||
      !reader.ReadString(record.material_key.exact_resource_group,
                         kMaximumExactKeyComponentBytes) ||
      !reader.ReadU64(record.resource_generation) ||
      !reader.ReadString(record.exact_source_script_member,
                         kMaximumSourceMemberBytes) ||
      !reader.ReadDigest(record.source_script_sha256) ||
      !reader.ReadDigest(record.effective_script_sha256) ||
      !reader.ReadU32(record.repair_plan_version) ||
      !reader.ReadString(record.material_key.exact_name,
                         kMaximumExactKeyComponentBytes) ||
      !reader.ReadDigest(record.native_structure_sha256) ||
      !reader.ReadString(record.selected_scheme,
                         kMaximumExactKeyComponentBytes) ||
      !reader.ReadU32(record.selected_lod) ||
      !ReadEnum(reader, record.runtime_generation, 3U,
                "semantic_catalog.runtime_generation") ||
      !ReadEnum(reader, record.base_color_semantic, 1U,
                "semantic_catalog.base_color_semantic") ||
      !ReadEnum(reader, record.registry_texture_color_role, 1U,
                "semantic_catalog.registry_texture_color_role") ||
      !reader.ReadString(record.exact_lowering_algorithm,
                         kMaximumLoweringAlgorithmBytes) ||
      !reader.ReadU32(record.lowering_version) ||
      !reader.ReadU64(record.declaration_revision) ||
      !ReadPassFacts(reader, record.pass) ||
      !ReadEnum(reader, record.environment_augmentation, 3U,
                "semantic_catalog.environment_augmentation") ||
      !reader.ReadU16(record.environment_texture_unit) ||
      !ReadEnum(reader, record.shadow_augmentation, 3U,
                "semantic_catalog.shadow_augmentation") ||
      !ReadEnum(reader, record.shadow_technique, 2U,
                "semantic_catalog.shadow_technique")) {
    return false;
  }
  std::uint16_t texture_unit_count = 0U;
  if (!reader.ReadU16(texture_unit_count)) {
    return false;
  }
  if (texture_unit_count > configuration.maximum_texture_units_per_record) {
    reader.Invalid(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_catalog.texture_units",
                   "texture unit count exceeds the configured cap");
    return false;
  }
  if (record.resource_generation == 0U ||
      record.resource_generation ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      record.repair_plan_version == 0U || record.lowering_version == 0U ||
      record.declaration_revision == 0U ||
      record.declaration_revision ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    reader.Invalid(ValidationCode::REVISION_MISMATCH,
                   "semantic_catalog.revision",
                   "catalog generations and revisions must be usable");
    return false;
  }
  if ((record.runtime_generation ==
           Ogre14LegacyMaterialRuntimeGeneration::AUTHORED &&
       record.source_script_sha256 != record.effective_script_sha256) ||
      (record.runtime_generation ==
           Ogre14LegacyMaterialRuntimeGeneration::REPAIRED_SCRIPT &&
       record.source_script_sha256 == record.effective_script_sha256)) {
    reader.Invalid(ValidationCode::REVISION_MISMATCH,
                   "semantic_catalog.runtime_generation",
                   "runtime generation disagrees with exact script digests");
    return false;
  }
  std::string stable_key;
  const ValidationResult key_validation = BuildOgre14LegacyStableAssetKey(
      RenderAssetKind::MATERIAL, record.material_key, stable_key);
  if (!key_validation) {
    reader.Invalid(key_validation.code, "semantic_catalog.material_key",
                   "catalog material key is not a valid exact legacy key");
    return false;
  }
  record.texture_units.reserve(texture_unit_count);
  std::size_t base_color_count = 0U;
  for (std::uint16_t index = 0U; index < texture_unit_count; ++index) {
    Ogre14LegacyCatalogTextureUnitFacts unit;
    if (!ReadTextureUnit(reader, unit, index)) {
      return false;
    }
    if (unit.semantic == Ogre14LegacyTextureSemantic::BASE_COLOR) {
      ++base_color_count;
      if (unit.color_role != record.registry_texture_color_role) {
        reader.Invalid(ValidationCode::REVISION_MISMATCH,
                       "semantic_catalog.registry_texture_color_role",
                       "explicit registry role differs from base-color unit");
        return false;
      }
    }
    record.texture_units.push_back(std::move(unit));
  }
  if (base_color_count > 1U) {
    reader.Invalid(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_catalog.texture_units.base_color",
                   "v2 lowering declares more than one base-color unit");
    return false;
  }
  const bool has_environment =
      record.environment_augmentation !=
      Ogre14LegacyEnvironmentAugmentation::NONE;
  if ((!has_environment && record.environment_texture_unit != kNoTextureUnit) ||
      (has_environment &&
       record.environment_texture_unit >= record.texture_units.size()) ||
      (has_environment &&
       record.texture_units[record.environment_texture_unit].semantic !=
           Ogre14LegacyTextureSemantic::ENVIRONMENT)) {
    reader.Invalid(ValidationCode::INVALID_ASSET_REFERENCE,
                   "semantic_catalog.environment_texture_unit",
                   "environment augmentation does not reference its exact unit");
    return false;
  }
  const bool has_shadow =
      record.shadow_augmentation != Ogre14LegacyShadowAugmentation::NONE;
  if (has_shadow !=
      (record.shadow_technique != Ogre14LegacyShadowTechnique::NONE)) {
    reader.Invalid(ValidationCode::REVISION_MISMATCH,
                   "semantic_catalog.shadow_augmentation",
                   "shadow augmentation and technique disagree");
    return false;
  }
  return true;
}

bool MaterialKeyLess(const Ogre14LegacyMaterialSemanticCatalogV2Record &lhs,
                     const Ogre14LegacyMaterialSemanticCatalogV2Record &rhs) {
  if (lhs.material_key.exact_resource_group !=
      rhs.material_key.exact_resource_group) {
    return ByteStringLess(lhs.material_key.exact_resource_group,
                          rhs.material_key.exact_resource_group);
  }
  return ByteStringLess(lhs.material_key.exact_name,
                        rhs.material_key.exact_name);
}

bool MaterialKeyEqual(const Ogre14LegacyMaterialSemanticCatalogV2Record &lhs,
                      const Ogre14LegacyMaterialSemanticCatalogV2Record &rhs) {
  return !MaterialKeyLess(lhs, rhs) && !MaterialKeyLess(rhs, lhs);
}

} // namespace

struct Ogre14LegacyMaterialSemanticCatalogV2::State final {
  Ogre14LegacyMaterialSemanticCatalogV2Configuration configuration;
  Ogre14LegacySha256 content_sha256{};
  std::vector<Ogre14LegacyMaterialSemanticCatalogV2Record> records;
};

Ogre14LegacyMaterialSemanticCatalogV2::
    Ogre14LegacyMaterialSemanticCatalogV2(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyMaterialSemanticCatalogV2::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t Ogre14LegacyMaterialSemanticCatalogV2::size() const noexcept {
  return state_ != nullptr ? state_->records.size() : 0U;
}

const Ogre14LegacySha256 &
Ogre14LegacyMaterialSemanticCatalogV2::content_sha256() const noexcept {
  static const Ogre14LegacySha256 kEmpty{};
  return state_ != nullptr ? state_->content_sha256 : kEmpty;
}

const Ogre14LegacyMaterialSemanticCatalogV2Record *
Ogre14LegacyMaterialSemanticCatalogV2::FindExact(
    const Ogre14LegacyAssetKey &material_key) const noexcept {
  if (state_ == nullptr) {
    return nullptr;
  }
  const auto found = std::lower_bound(
      state_->records.begin(), state_->records.end(), material_key,
      [](const Ogre14LegacyMaterialSemanticCatalogV2Record &record,
         const Ogre14LegacyAssetKey &key) {
        if (record.material_key.exact_resource_group !=
            key.exact_resource_group) {
          return ByteStringLess(record.material_key.exact_resource_group,
                                key.exact_resource_group);
        }
        return ByteStringLess(record.material_key.exact_name, key.exact_name);
      });
  if (found == state_->records.end() ||
      found->material_key != material_key) {
    return nullptr;
  }
  return &*found;
}

bool Ogre14LegacyMaterialSemanticCatalogV2::SharesImmutableStateWith(
    const Ogre14LegacyMaterialSemanticCatalogV2 &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(
    const Ogre14LegacyMaterialSemanticCatalogV2Configuration &configuration) {
  if (configuration.version !=
      kOgre14LegacyMaterialSemanticCatalogV2Version) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "semantic_catalog.configuration.version",
                   "unsupported semantic catalog configuration version");
  }
  if (configuration.maximum_catalog_bytes <
          kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes ||
      configuration.maximum_catalog_bytes >
          kOgre14LegacyMaterialSemanticCatalogV2MaximumBytes ||
      configuration.maximum_records == 0U ||
      configuration.maximum_records >
          kOgre14LegacyMaterialSemanticCatalogV2MaximumRecords ||
      configuration.maximum_texture_units_per_record == 0U ||
      configuration.maximum_texture_units_per_record >
          kOgre14LegacyMaterialSemanticCatalogV2MaximumTextureUnitsPerRecord ||
      configuration.maximum_total_string_bytes == 0U ||
      configuration.maximum_total_string_bytes >
          kOgre14LegacyMaterialSemanticCatalogV2MaximumStringBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_catalog.configuration.limits",
                   "semantic catalog limits are zero or exceed hard caps");
  }
  return ValidationResult::Success();
}

ValidationResult ParseOgre14LegacyMaterialSemanticCatalogV2(
    const Ogre14LegacyMaterialSemanticCatalogV2Configuration &configuration,
    const std::vector<std::uint8_t> &bytes,
    Ogre14LegacyMaterialSemanticCatalogV2 &output,
    IOgre14LegacyMaterialSemanticCatalogV2FaultInjector *fault_injector) {
  const ValidationResult configuration_validation =
      ValidateOgre14LegacyMaterialSemanticCatalogV2Configuration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (bytes.size() < kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes ||
      bytes.size() > configuration.maximum_catalog_bytes) {
    return Failure(ValidationCode::SIZE_MISMATCH, "semantic_catalog.bytes",
                   "compiled catalog is empty, truncated, or exceeds its cap");
  }
  try {
    Reader header(bytes.data(), kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes,
                  configuration.maximum_total_string_bytes);
    std::array<std::uint8_t, 8U> magic{};
    for (std::uint8_t &byte : magic) {
      if (!header.ReadU8(byte)) {
        return header.error();
      }
    }
    std::uint16_t version = 0U;
    std::uint16_t header_bytes = 0U;
    std::uint32_t flags = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t payload_bytes = 0U;
    Ogre14LegacySha256 expected_digest{};
    std::uint64_t reserved = 0U;
    if (!header.ReadU16(version) || !header.ReadU16(header_bytes) ||
        !header.ReadU32(flags) || !header.ReadU32(record_count) ||
        !header.ReadU32(payload_bytes) ||
        !header.ReadDigest(expected_digest) || !header.ReadU64(reserved) ||
        !header.finished()) {
      return header.error();
    }
    if (magic != kMagic || version != 2U ||
        header_bytes != kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes) {
      return Failure(ValidationCode::UNSUPPORTED_VERSION,
                     "semantic_catalog.header",
                     "catalog magic, version, or header size is unsupported");
    }
    if (flags != 0U || reserved != 0U) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "semantic_catalog.header.extensions",
                     "unknown catalog flags or reserved fields are nonzero");
    }
    if (record_count == 0U || record_count > configuration.maximum_records) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "semantic_catalog.record_count",
                     "catalog record count is zero or exceeds its cap");
    }
    if (payload_bytes !=
            bytes.size() - kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "semantic_catalog.payload_bytes",
                     "payload size rejects truncation and trailing fields");
    }
    const std::uint8_t *payload =
        bytes.data() + kOgre14LegacyMaterialSemanticCatalogV2HeaderBytes;
    const Ogre14LegacySha256 actual_digest = Sha256(payload, payload_bytes);
    if (actual_digest != expected_digest) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_catalog.content_sha256",
                     "catalog payload does not match its stable SHA-256");
    }
    if (fault_injector != nullptr) {
      fault_injector->BeforeCatalogParseStage(
          Ogre14LegacyMaterialSemanticCatalogV2ParseStage::AFTER_HEADER);
    }

    auto candidate =
        std::make_shared<Ogre14LegacyMaterialSemanticCatalogV2::State>();
    candidate->configuration = configuration;
    candidate->content_sha256 = actual_digest;
    candidate->records.reserve(record_count);
    Reader reader(payload, payload_bytes,
                  configuration.maximum_total_string_bytes);
    for (std::uint32_t index = 0U; index < record_count; ++index) {
      Ogre14LegacyMaterialSemanticCatalogV2Record record;
      if (!ReadRecord(reader, configuration, record)) {
        return reader.error();
      }
      if (!candidate->records.empty()) {
        const auto &previous = candidate->records.back();
        if (MaterialKeyEqual(previous, record)) {
          return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                         "semantic_catalog.material_key",
                         "duplicate exact material identity is rejected");
        }
        if (!MaterialKeyLess(previous, record)) {
          return Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                         "semantic_catalog.records",
                         "catalog records are not in canonical key order");
        }
      }
      candidate->records.push_back(std::move(record));
      if (index == 0U && fault_injector != nullptr) {
        fault_injector->BeforeCatalogParseStage(
            Ogre14LegacyMaterialSemanticCatalogV2ParseStage::AFTER_FIRST_RECORD);
      }
    }
    if (!reader.finished()) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "semantic_catalog.payload.trailing",
                     "catalog payload contains unknown trailing fields");
    }
    if (fault_injector != nullptr) {
      fault_injector->BeforeCatalogParseStage(
          Ogre14LegacyMaterialSemanticCatalogV2ParseStage::BEFORE_COMMIT);
    }
    output = Ogre14LegacyMaterialSemanticCatalogV2(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_catalog.allocation",
                   "allocation failed before semantic catalog commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_catalog.allocation",
                   "semantic catalog exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_catalog.exception",
                   "unexpected exception before semantic catalog commit");
  }
}

ValidationResult BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
    const Ogre14LegacyMaterialSemanticCatalogV2 &catalog,
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration,
    Ogre14LegacyMaterialSemanticRegistry &output,
    IOgre14LegacyMaterialSemanticRegistryFaultInjector *fault_injector) {
  if (catalog.state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "semantic_catalog",
                   "semantic catalog has not been initialized");
  }
  try {
    std::vector<Ogre14LegacyMaterialSemanticDeclaration> declarations;
    declarations.reserve(catalog.state_->records.size());
    for (const Ogre14LegacyMaterialSemanticCatalogV2Record &record :
         catalog.state_->records) {
      Ogre14LegacyMaterialSemanticDeclaration declaration;
      declaration.material_key = record.material_key;
      declaration.source =
          Ogre14LegacyMaterialSemanticSource::VERSIONED_COMPATIBILITY_TABLE;
      declaration.source_revision = record.declaration_revision;
      declaration.base_color_semantic = record.base_color_semantic;
      declaration.texture_color_role = record.registry_texture_color_role;
      declarations.push_back(std::move(declaration));
    }
    return BuildOgre14LegacyMaterialSemanticRegistry(
        configuration, declarations, output, fault_injector);
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_catalog.registry.allocation",
                   "allocation failed before semantic registry build");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_catalog.registry.allocation",
                   "semantic declaration list exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_catalog.registry.exception",
                   "unexpected exception before semantic registry build");
  }
}

} // namespace RoR::Render
