/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeRenderAssetPackage.h"

#include "MaterialDescriptor.h"
#include "RenderAssetRegistry.h"
#include "RenderMath.h"
#include "RenderResourceDescriptors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace RoR::Render {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{{
    'R', 'O', 'R', 'N', 'A', 'T', '1', 0U,
}};
constexpr std::array<std::uint8_t, 8U> kTransmissionMagic{{
    'R', 'O', 'R', 'N', 'A', 'T', '2', 0U,
}};
constexpr std::array<std::uint8_t, 8U> kDistanceLodMagic{{
    'R', 'O', 'R', 'N', 'A', 'T', '3', 0U,
}};
constexpr std::uint32_t kRecordManifest = 1U;
constexpr std::uint32_t kRecordMesh = 2U;
constexpr std::uint32_t kRecordTexture = 3U;
constexpr std::uint32_t kRecordMaterial = 4U;
constexpr std::uint32_t kRecordSampler = 5U;
constexpr std::uint32_t kRecordStaticInstance = 6U;
constexpr std::uint32_t kMaximumRecordCount = 32768U;
constexpr std::uint32_t kMaximumAssetCount = 4096U;
constexpr std::uint32_t kMaximumInstanceCount = 16384U;
constexpr std::uint32_t kMaximumStringBytes = 255U;
constexpr std::uint32_t kMaximumManifestBytes = 1024U * 1024U;
constexpr std::uint32_t kMaximumMeshVertices = 4000000U;
constexpr std::uint32_t kMaximumMeshIndices = 12000000U;
constexpr std::uint32_t kMaximumTextureDimension = 16384U;
constexpr std::uint32_t kMaximumTextureMips = 15U;
constexpr std::size_t kMaximumJsonDepth = 64U;
// .rornative v1/v2/v3 material records predate the native-only v6 terrain detail
// profile.  Their binary layout is fixed to the original six core bindings;
// growing the live GraphicsScene array must not make those immutable packages
// appear truncated.  A package carrying detail bindings needs a new package
// version and explicit bytes rather than silently changing v1/v2.
constexpr std::size_t kNativePackageMaterialTextureSlotCount = 6U;
static_assert(static_cast<std::size_t>(MaterialTextureSlot::SPECULAR) + 1U ==
              kNativePackageMaterialTextureSlotCount);
static_assert(kNativePackageMaterialTextureSlotCount <
              kGraphicsSceneMaterialTextureSlotCount);

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

void SetEmergencyFailure(NativeRenderAssetPackageDecodeResult &result,
                         ValidationCode code) noexcept {
  result.package.reset();
  result.validation.code = code;
  result.validation.element_index = ValidationResult::kNoElement;
  result.validation.field.clear();
  result.validation.detail.clear();
}

class Reader final {
public:
  Reader(const std::uint8_t *bytes, std::size_t byte_count) noexcept
      : bytes_(bytes), byte_count_(byte_count) {}

  [[nodiscard]] std::size_t remaining() const noexcept {
    return offset_ <= byte_count_ ? byte_count_ - offset_ : 0U;
  }
  [[nodiscard]] bool empty() const noexcept { return remaining() == 0U; }

  bool ReadU8(std::uint8_t &value) noexcept {
    if (remaining() < 1U) {
      return false;
    }
    value = bytes_[offset_++];
    return true;
  }

  bool ReadU16(std::uint16_t &value) noexcept {
    if (remaining() < 2U) {
      return false;
    }
    value = static_cast<std::uint16_t>(bytes_[offset_]) |
            static_cast<std::uint16_t>(bytes_[offset_ + 1U]) << 8U;
    offset_ += 2U;
    return true;
  }

  bool ReadU32(std::uint32_t &value) noexcept {
    if (remaining() < 4U) {
      return false;
    }
    value = static_cast<std::uint32_t>(bytes_[offset_]) |
            static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U |
            static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U |
            static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U;
    offset_ += 4U;
    return true;
  }

  bool ReadU64(std::uint64_t &value) noexcept {
    if (remaining() < 8U) {
      return false;
    }
    value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
      value |= static_cast<std::uint64_t>(bytes_[offset_ + index])
               << (index * 8U);
    }
    offset_ += 8U;
    return true;
  }

  bool ReadFloat(float &value) noexcept {
    std::uint32_t bits = 0U;
    if (!ReadU32(bits)) {
      return false;
    }
    static_assert(sizeof(bits) == sizeof(value),
                  "binary32 package requires a 32-bit float");
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadRaw(std::uint8_t *destination, std::size_t count) noexcept {
    if (count > remaining() || (count != 0U && destination == nullptr)) {
      return false;
    }
    if (count != 0U) {
      std::memcpy(destination, bytes_ + offset_, count);
    }
    offset_ += count;
    return true;
  }

  bool ReadVector(std::vector<std::uint8_t> &destination,
                  std::size_t count) {
    if (count > remaining()) {
      return false;
    }
    destination.resize(count);
    return ReadRaw(destination.data(), count);
  }

  bool ReadString(std::string &value) {
    std::uint32_t size = 0U;
    if (!ReadU32(size) || size == 0U || size > kMaximumStringBytes ||
        size > remaining()) {
      return false;
    }
    value.assign(reinterpret_cast<const char *>(bytes_ + offset_), size);
    offset_ += size;
    for (const unsigned char character : value) {
      if (character < 0x20U || character > 0x7EU) {
        return false;
      }
    }
    return true;
  }

  bool ReadZeroes(std::size_t count) noexcept {
    if (count > remaining()) {
      return false;
    }
    for (std::size_t index = 0U; index < count; ++index) {
      if (bytes_[offset_ + index] != 0U) {
        return false;
      }
    }
    offset_ += count;
    return true;
  }

  bool Take(std::size_t count, Reader &subreader) noexcept {
    if (count > remaining()) {
      return false;
    }
    subreader = Reader(bytes_ + offset_, count);
    offset_ += count;
    return true;
  }

private:
  const std::uint8_t *bytes_ = nullptr;
  std::size_t byte_count_ = 0U;
  std::size_t offset_ = 0U;
};

bool IsCanonicalFloat(float value) noexcept {
  return IsFinite(value) && !(value == 0.0F && std::signbit(value));
}

bool ReadCanonicalFloat(Reader &reader, float &value) noexcept {
  return reader.ReadFloat(value) && IsCanonicalFloat(value);
}

bool ReadFloat2(Reader &reader, Float2 &value) noexcept {
  return ReadCanonicalFloat(reader, value.x) &&
         ReadCanonicalFloat(reader, value.y);
}

bool ReadFloat3(Reader &reader, Float3 &value) noexcept {
  return ReadCanonicalFloat(reader, value.x) &&
         ReadCanonicalFloat(reader, value.y) &&
         ReadCanonicalFloat(reader, value.z);
}

bool ReadFloat4(Reader &reader, Float4 &value) noexcept {
  return ReadCanonicalFloat(reader, value.x) &&
         ReadCanonicalFloat(reader, value.y) &&
         ReadCanonicalFloat(reader, value.z) &&
         ReadCanonicalFloat(reader, value.w);
}

template <typename Element, typename ReadElement>
bool ReadElements(Reader &reader, std::uint32_t count,
                  std::uint32_t maximum, std::size_t encoded_bytes,
                  std::vector<Element> &values, ReadElement read_element) {
  if (count > maximum || encoded_bytes == 0U ||
      count > reader.remaining() / encoded_bytes) {
    return false;
  }
  values.resize(count);
  for (Element &value : values) {
    if (!read_element(reader, value)) {
      return false;
    }
  }
  return true;
}

Float3 Cross(const Float3 &left, const Float3 &right) noexcept {
  return Float3{
      left.y * right.z - left.z * right.y,
      left.z * right.x - left.x * right.z,
      left.x * right.y - left.y * right.x,
  };
}

bool Normalize(Float3 &value) noexcept {
  const double length_squared =
      static_cast<double>(value.x) * value.x +
      static_cast<double>(value.y) * value.y +
      static_cast<double>(value.z) * value.z;
  if (!std::isfinite(length_squared) || length_squared <= 1.0e-16) {
    return false;
  }
  const float reciprocal =
      static_cast<float>(1.0 / std::sqrt(length_squared));
  value.x *= reciprocal;
  value.y *= reciprocal;
  value.z *= reciprocal;
  return IsFinite(value);
}

ValidationResult ValidateMeshTangentDerivatives(
    const MeshResourceDescriptor &mesh) {
  for (std::size_t triangle = 0U; triangle < mesh.indices.size();
       triangle += 3U) {
    const std::uint32_t a = mesh.indices[triangle];
    const std::uint32_t b = mesh.indices[triangle + 1U];
    const std::uint32_t c = mesh.indices[triangle + 2U];
    const Float3 &p0 = mesh.positions[a];
    const Float3 &p1 = mesh.positions[b];
    const Float3 &p2 = mesh.positions[c];
    const Float2 &uv0 = mesh.texture_coordinates_0[a];
    const Float2 &uv1 = mesh.texture_coordinates_0[b];
    const Float2 &uv2 = mesh.texture_coordinates_0[c];
    const Float3 edge1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    const Float3 edge2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    Float3 face_normal = Cross(edge1, edge2);
    if (!Normalize(face_normal)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "native.mesh.geometry",
                     "triangle geometric face normal is degenerate",
                     triangle / 3U);
    }
    for (const std::uint32_t vertex : {a, b, c}) {
      if (!(Dot(face_normal, mesh.normals[vertex]) > 0.0F)) {
        return Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "native.mesh.winding",
            "geometric face normal must point into every authored vertex-normal hemisphere",
            triangle / 3U);
      }
    }
    const float du1 = uv1.x - uv0.x;
    const float dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x;
    const float dv2 = uv2.y - uv0.y;
    const float determinant = du1 * dv2 - dv1 * du2;
    if (!IsFinite(determinant) || std::fabs(determinant) <= 1.0e-12F) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "native.mesh.uv_derivatives",
                     "triangle UV derivatives are singular", triangle / 3U);
    }
    const float reciprocal = 1.0F / determinant;
    const Float3 u_direction{
        (edge1.x * dv2 - edge2.x * dv1) * reciprocal,
        (edge1.y * dv2 - edge2.y * dv1) * reciprocal,
        (edge1.z * dv2 - edge2.z * dv1) * reciprocal,
    };
    const Float3 v_direction{
        (edge2.x * du1 - edge1.x * du2) * reciprocal,
        (edge2.y * du1 - edge1.y * du2) * reciprocal,
        (edge2.z * du1 - edge1.z * du2) * reciprocal,
    };
    for (const std::uint32_t vertex : {a, b, c}) {
      const Float3 &normal = mesh.normals[vertex];
      const Float4 &authored = mesh.tangents[vertex];
      const Float3 actual_tangent{authored.x, authored.y, authored.z};
      const double normal_length = std::sqrt(
          static_cast<double>(normal.x) * normal.x +
          static_cast<double>(normal.y) * normal.y +
          static_cast<double>(normal.z) * normal.z);
      const double tangent_length = std::sqrt(
          static_cast<double>(actual_tangent.x) * actual_tangent.x +
          static_cast<double>(actual_tangent.y) * actual_tangent.y +
          static_cast<double>(actual_tangent.z) * actual_tangent.z);
      if (std::fabs(normal_length - 1.0) > 1.0e-5 ||
          std::fabs(tangent_length - 1.0) > 1.0e-5 ||
          std::fabs(static_cast<double>(Dot(normal, actual_tangent))) >
              1.0e-5 ||
          (authored.w != -1.0F && authored.w != 1.0F)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "native.mesh.tangent_basis",
                       "normal/tangent basis is not canonical",
                       triangle / 3U);
      }
      const float tangent_projection = Dot(normal, u_direction);
      Float3 expected_tangent{
          u_direction.x - normal.x * tangent_projection,
          u_direction.y - normal.y * tangent_projection,
          u_direction.z - normal.z * tangent_projection,
      };
      if (!Normalize(expected_tangent) ||
          Dot(expected_tangent, actual_tangent) < 0.9999F) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "native.mesh.tangent_u",
                       "tangent does not follow increasing UV0 U",
                       triangle / 3U);
      }
      const float bitangent_projection = Dot(normal, v_direction);
      Float3 expected_bitangent{
          v_direction.x - normal.x * bitangent_projection,
          v_direction.y - normal.y * bitangent_projection,
          v_direction.z - normal.z * bitangent_projection,
      };
      Float3 reconstructed = Cross(normal, actual_tangent);
      reconstructed.x *= authored.w;
      reconstructed.y *= authored.w;
      reconstructed.z *= authored.w;
      if (!Normalize(expected_bitangent) ||
          Dot(expected_bitangent, reconstructed) < 0.9999F) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "native.mesh.tangent_handedness",
                       "tangent w does not reconstruct B=w*cross(N,T)",
                       triangle / 3U);
      }
    }
  }
  return ValidationResult::Success();
}

enum class JsonType : std::uint8_t {
  NULL_VALUE = 0U,
  BOOLEAN = 1U,
  NUMBER = 2U,
  STRING = 3U,
  ARRAY = 4U,
  OBJECT = 5U,
};

struct JsonValue {
  JsonType type = JsonType::NULL_VALUE;
  bool boolean = false;
  std::uint64_t number = 0U;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

class CanonicalJsonReader final {
public:
  CanonicalJsonReader(const std::uint8_t *bytes, std::size_t size) noexcept
      : bytes_(bytes), size_(size) {}

  bool Parse(JsonValue &root) {
    return bytes_ != nullptr && size_ >= 2U &&
           size_ <= kMaximumManifestBytes && ParseValue(0U, root) &&
           offset_ == size_;
  }

private:
  bool Consume(std::uint8_t expected) noexcept {
    if (offset_ >= size_ || bytes_[offset_] != expected) {
      return false;
    }
    ++offset_;
    return true;
  }

  bool ParseValue(std::size_t depth, JsonValue &value) {
    if (depth > kMaximumJsonDepth || offset_ >= size_) {
      return false;
    }
    switch (bytes_[offset_]) {
    case static_cast<std::uint8_t>('{'):
      return ParseObject(depth, value);
    case static_cast<std::uint8_t>('['):
      return ParseArray(depth, value);
    case static_cast<std::uint8_t>('"'):
      value.type = JsonType::STRING;
      return ParseString(value.string);
    case static_cast<std::uint8_t>('t'):
      value.type = JsonType::BOOLEAN;
      value.boolean = true;
      return ParseLiteral("true");
    case static_cast<std::uint8_t>('f'):
      value.type = JsonType::BOOLEAN;
      value.boolean = false;
      return ParseLiteral("false");
    case static_cast<std::uint8_t>('n'):
      value.type = JsonType::NULL_VALUE;
      return ParseLiteral("null");
    default:
      value.type = JsonType::NUMBER;
      return ParseNumber(value.number);
    }
  }

  bool ParseObject(std::size_t depth, JsonValue &value) {
    if (!Consume(static_cast<std::uint8_t>('{'))) {
      return false;
    }
    value.type = JsonType::OBJECT;
    value.object.clear();
    if (Consume(static_cast<std::uint8_t>('}'))) {
      return true;
    }
    std::string previous_key;
    bool first = true;
    while (true) {
      std::string key;
      JsonValue member;
      if (!ParseString(key) || (!first && key <= previous_key) ||
          !Consume(static_cast<std::uint8_t>(':')) ||
          !ParseValue(depth + 1U, member) ||
          !value.object.emplace(key, std::move(member)).second) {
        return false;
      }
      first = false;
      previous_key = std::move(key);
      if (Consume(static_cast<std::uint8_t>('}'))) {
        return true;
      }
      if (!Consume(static_cast<std::uint8_t>(','))) {
        return false;
      }
    }
  }

  bool ParseArray(std::size_t depth, JsonValue &value) {
    if (!Consume(static_cast<std::uint8_t>('['))) {
      return false;
    }
    value.type = JsonType::ARRAY;
    value.array.clear();
    if (Consume(static_cast<std::uint8_t>(']'))) {
      return true;
    }
    while (true) {
      JsonValue element;
      if (!ParseValue(depth + 1U, element)) {
        return false;
      }
      value.array.push_back(std::move(element));
      if (Consume(static_cast<std::uint8_t>(']'))) {
        return true;
      }
      if (!Consume(static_cast<std::uint8_t>(','))) {
        return false;
      }
    }
  }

  bool ParseString(std::string &decoded) {
    if (!Consume(static_cast<std::uint8_t>('"'))) {
      return false;
    }
    decoded.clear();
    while (offset_ < size_) {
      const std::uint8_t value = bytes_[offset_++];
      if (value == static_cast<std::uint8_t>('"')) {
        return true;
      }
      if (value < 0x20U || value > 0x7EU) {
        return false;
      }
      if (value != static_cast<std::uint8_t>('\\')) {
        decoded.push_back(static_cast<char>(value));
        continue;
      }
      if (offset_ >= size_) {
        return false;
      }
      const std::uint8_t escape = bytes_[offset_++];
      if (escape == static_cast<std::uint8_t>('"') ||
          escape == static_cast<std::uint8_t>('\\')) {
        decoded.push_back(static_cast<char>(escape));
        continue;
      }
      return false;
    }
    return false;
  }

  bool ParseLiteral(const char *literal) noexcept {
    for (const char *cursor = literal; *cursor != '\0'; ++cursor) {
      if (!Consume(static_cast<std::uint8_t>(*cursor))) {
        return false;
      }
    }
    return true;
  }

  bool ParseNumber(std::uint64_t &number) noexcept {
    if (offset_ >= size_) {
      return false;
    }
    number = 0U;
    if (bytes_[offset_] == static_cast<std::uint8_t>('0')) {
      ++offset_;
      if (offset_ < size_ && bytes_[offset_] >= static_cast<std::uint8_t>('0') &&
          bytes_[offset_] <= static_cast<std::uint8_t>('9')) {
        return false;
      }
    } else if (bytes_[offset_] >= static_cast<std::uint8_t>('1') &&
               bytes_[offset_] <= static_cast<std::uint8_t>('9')) {
      while (offset_ < size_ &&
             bytes_[offset_] >= static_cast<std::uint8_t>('0') &&
             bytes_[offset_] <= static_cast<std::uint8_t>('9')) {
        const std::uint64_t digit =
            static_cast<std::uint64_t>(bytes_[offset_] -
                                       static_cast<std::uint8_t>('0'));
        if (number > ((std::numeric_limits<std::uint64_t>::max)() - digit) /
                         10U) {
          return false;
        }
        number = number * 10U + digit;
        ++offset_;
      }
    } else {
      return false;
    }
    // The v1 provenance schema contains only non-negative integer counts.
    // Reject other JSON-number spellings so recomputed hostile packages cannot
    // introduce alternate lexical representations.
    return offset_ >= size_ ||
           (bytes_[offset_] != static_cast<std::uint8_t>('.') &&
            bytes_[offset_] != static_cast<std::uint8_t>('e') &&
            bytes_[offset_] != static_cast<std::uint8_t>('E'));
  }

  const std::uint8_t *bytes_ = nullptr;
  std::size_t size_ = 0U;
  std::size_t offset_ = 0U;
};

const JsonValue *Member(const JsonValue &object, const char *key) noexcept {
  if (object.type != JsonType::OBJECT) {
    return nullptr;
  }
  const auto found = object.object.find(key);
  return found == object.object.end() ? nullptr : &found->second;
}

bool ExactObject(const JsonValue &value,
                 std::initializer_list<const char *> keys) {
  if (value.type != JsonType::OBJECT || value.object.size() != keys.size()) {
    return false;
  }
  for (const char *key : keys) {
    if (value.object.find(key) == value.object.end()) {
      return false;
    }
  }
  return true;
}

bool StringValue(const JsonValue *value, const std::string &expected) noexcept {
  return value != nullptr && value->type == JsonType::STRING &&
         value->string == expected;
}

bool BooleanValue(const JsonValue *value, bool expected) noexcept {
  return value != nullptr && value->type == JsonType::BOOLEAN &&
         value->boolean == expected;
}

bool BoundedString(const JsonValue *value, std::size_t maximum) noexcept {
  return value != nullptr && value->type == JsonType::STRING &&
         !value->string.empty() && value->string.size() <= maximum;
}

bool CanonicalIdentifier(const std::string &value) noexcept {
  if (value.size() < 7U || value.size() > kMaximumStringBytes ||
      value.compare(0U, 6U, "rorng_") != 0) {
    return false;
  }
  for (const unsigned char character : value) {
    if (!((character >= static_cast<unsigned char>('a') &&
           character <= static_cast<unsigned char>('z')) ||
          (character >= static_cast<unsigned char>('0') &&
           character <= static_cast<unsigned char>('9')) ||
          character == static_cast<unsigned char>('_'))) {
      return false;
    }
  }
  return true;
}

bool PortablePath(const std::string &value, const char *suffix) noexcept {
  if (value.empty() || value.front() == '/' ||
      value.find('\\') != std::string::npos) {
    return false;
  }
  std::size_t segment_start = 0U;
  while (segment_start <= value.size()) {
    const std::size_t separator = value.find('/', segment_start);
    const std::size_t segment_end =
        separator == std::string::npos ? value.size() : separator;
    const std::size_t segment_size = segment_end - segment_start;
    if (segment_size == 0U ||
        (segment_size == 1U && value[segment_start] == '.') ||
        (segment_size == 2U && value[segment_start] == '.' &&
         value[segment_start + 1U] == '.')) {
      return false;
    }
    if (separator == std::string::npos) {
      break;
    }
    segment_start = separator + 1U;
  }
  const std::size_t required_suffix_size = std::strlen(suffix);
  return value.size() >= required_suffix_size &&
         value.compare(value.size() - required_suffix_size,
                       required_suffix_size, suffix) == 0;
}

bool ParseLowerHex(const std::string &value, std::uint64_t &parsed) noexcept {
  if (value.size() != 16U) {
    return false;
  }
  parsed = 0U;
  for (const unsigned char character : value) {
    std::uint8_t digit = 0U;
    if (character >= static_cast<unsigned char>('0') &&
        character <= static_cast<unsigned char>('9')) {
      digit = static_cast<std::uint8_t>(character -
                                        static_cast<unsigned char>('0'));
    } else if (character >= static_cast<unsigned char>('a') &&
               character <= static_cast<unsigned char>('f')) {
      digit = static_cast<std::uint8_t>(character -
                                            static_cast<unsigned char>('a') +
                                        10U);
    } else {
      return false;
    }
    parsed = (parsed << 4U) | digit;
  }
  return parsed != 0U;
}

bool ParseDigest(const JsonValue *value, RenderPayloadDigest &digest) noexcept {
  if (value == nullptr || value->type != JsonType::STRING ||
      value->string.size() != digest.size() * 2U) {
    return false;
  }
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    std::uint8_t byte = 0U;
    for (std::size_t nibble = 0U; nibble < 2U; ++nibble) {
      const unsigned char character =
          static_cast<unsigned char>(value->string[index * 2U + nibble]);
      std::uint8_t digit = 0U;
      if (character >= static_cast<unsigned char>('0') &&
          character <= static_cast<unsigned char>('9')) {
        digit = static_cast<std::uint8_t>(character -
                                          static_cast<unsigned char>('0'));
      } else if (character >= static_cast<unsigned char>('a') &&
                 character <= static_cast<unsigned char>('f')) {
        digit = static_cast<std::uint8_t>(character -
                                              static_cast<unsigned char>('a') +
                                          10U);
      } else {
        return false;
      }
      byte = static_cast<std::uint8_t>((byte << 4U) | digit);
    }
    digest[index] = byte;
  }
  return true;
}

std::uint64_t SourceIdentity(const std::string &package_id,
                             const std::string &kind,
                             const std::string &logical_id) {
  std::string input("ror-native-render-source-id-v1");
  input.push_back('\0');
  input.append(package_id);
  input.push_back('\0');
  input.append(kind);
  input.push_back('\0');
  input.append(logical_id);
  const RenderPayloadDigest digest = ComputeRenderPayloadDigest(
      reinterpret_cast<const std::uint8_t *>(input.data()), input.size());
  std::uint64_t result = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    result = (result << 8U) | digest[index];
  }
  return result;
}

struct ManifestAsset {
  std::uint32_t record_type = 0U;
  RenderAssetKind kind = RenderAssetKind::INVALID;
  std::uint64_t source_id = 0U;
  std::string kind_name;
  std::string logical_id;
};

struct ManifestInstance {
  std::uint64_t source_id = 0U;
  std::uint32_t flags = 0U;
  std::string logical_id;
};

struct ManifestCounts {
  std::uint64_t assets = 0U;
  std::uint64_t indices = 0U;
  std::uint64_t instances = 0U;
  std::uint64_t lod_indices = 0U;
  std::uint64_t lod_levels = 0U;
  std::uint64_t materials = 0U;
  std::uint64_t meshes = 0U;
  std::uint64_t samplers = 0U;
  std::uint64_t texture_bytes = 0U;
  std::uint64_t textures = 0U;
  std::uint64_t triangles = 0U;
  std::uint64_t vertices = 0U;
};

struct ParsedManifest {
  std::string package_id;
  std::string origin_class;
  RenderPayloadDigest compiler_sha256{};
  RenderPayloadDigest generator_sha256{};
  RenderPayloadDigest glb_sha256{};
  RenderPayloadDigest composition_sha256{};
  RenderPayloadDigest source_manifest_sha256{};
  bool includes_distance_lods = false;
  ManifestCounts counts;
  std::vector<ManifestAsset> assets;
  std::vector<ManifestInstance> instances;
};

bool ParseFileRecord(const JsonValue *value, const char *suffix,
                     RenderPayloadDigest &digest) {
  if (value == nullptr || !ExactObject(*value, {"path", "sha256"})) {
    return false;
  }
  const JsonValue *path = Member(*value, "path");
  return path != nullptr && path->type == JsonType::STRING &&
         PortablePath(path->string, suffix) &&
         ParseDigest(Member(*value, "sha256"), digest);
}

bool ParseManifest(const std::uint8_t *bytes, std::size_t size,
                   std::uint32_t package_version, ParsedManifest &parsed) {
  const bool includes_distance_lods =
      package_version == kNativeRenderAssetPackageDistanceLodVersion;
  const char *expected_manifest_format =
      includes_distance_lods
          ? "ror-native-render-package-manifest-v3"
          : (package_version == kNativeRenderAssetPackageTransmissionVersion
                 ? "ror-native-render-package-manifest-v2"
                 : "ror-native-render-package-manifest-v1");
  JsonValue root;
  CanonicalJsonReader reader(bytes, size);
  if (bytes == nullptr || size < 2U ||
      bytes[0U] != static_cast<std::uint8_t>('{') ||
      bytes[size - 1U] != static_cast<std::uint8_t>('}') ||
      !reader.Parse(root) ||
      !ExactObject(root, {"assets", "claims", "compiler", "counts", "format",
                          "instances", "package", "source"}) ||
      !StringValue(Member(root, "format"), expected_manifest_format)) {
    return false;
  }
  parsed.includes_distance_lods = includes_distance_lods;

  const JsonValue *claims = Member(root, "claims");
  if (claims == nullptr ||
      !ExactObject(*claims, {"ambient_occlusion", "collision", "lods",
                             "native_terrain", "visual_only"}) ||
      !BooleanValue(Member(*claims, "ambient_occlusion"), false) ||
      !BooleanValue(Member(*claims, "collision"), false) ||
      !BooleanValue(Member(*claims, "lods"), includes_distance_lods) ||
      !BooleanValue(Member(*claims, "native_terrain"), false) ||
      !BooleanValue(Member(*claims, "visual_only"), true)) {
    return false;
  }

  const JsonValue *package = Member(root, "package");
  if (package == nullptr ||
      !ExactObject(*package, {"author", "creation_attestation", "id", "license",
                              "modified", "origin_class", "source_revision",
                              "source_uri"}) ||
      !BoundedString(Member(*package, "author"), 1024U) ||
      !BoundedString(Member(*package, "creation_attestation"), 4096U) ||
      !BoundedString(Member(*package, "license"), 128U) ||
      !BoundedString(Member(*package, "source_revision"), 256U) ||
      !BoundedString(Member(*package, "source_uri"), 2048U) ||
      Member(*package, "modified") == nullptr ||
      Member(*package, "modified")->type != JsonType::BOOLEAN) {
    return false;
  }
  const JsonValue *package_id = Member(*package, "id");
  const JsonValue *origin = Member(*package, "origin_class");
  if (package_id == nullptr || package_id->type != JsonType::STRING ||
      !CanonicalIdentifier(package_id->string) || origin == nullptr ||
      origin->type != JsonType::STRING ||
      (origin->string != "project_original" &&
       origin->string != "clean_room_recreation" &&
       origin->string != "rights_cleared_derivative" &&
       origin->string != "legacy_compat_conversion")) {
    return false;
  }
  parsed.package_id = package_id->string;
  parsed.origin_class = origin->string;

  const JsonValue *compiler = Member(root, "compiler");
  const char *expected_compiler_format =
      includes_distance_lods ? "ror-native-render-compiler-v3"
                             : "ror-native-render-compiler-v1";
  const char *expected_compiler_path =
      includes_distance_lods ? "tools/compile_native_render_asset_v3.py"
                             : "tools/compile_native_render_asset.py";
  if (compiler == nullptr ||
      !ExactObject(*compiler, {"dependencies", "format", "path", "sha256"}) ||
      !StringValue(Member(*compiler, "format"), expected_compiler_format) ||
      !StringValue(Member(*compiler, "path"), expected_compiler_path) ||
      !ParseDigest(Member(*compiler, "sha256"), parsed.compiler_sha256)) {
    return false;
  }
  const JsonValue *dependencies = Member(*compiler, "dependencies");
  constexpr std::array<const char *, 2U> kLegacyDependencyPaths{{
      "tools/validate_cityworld_asset.py",
      "tools/validate_native_render_asset.py",
  }};
  constexpr std::array<const char *, 4U> kDistanceLodDependencyPaths{{
      "tools/validate_cityworld_asset.py",
      "tools/validate_native_render_asset.py",
      "tools/validate_native_render_asset_v3.py",
      "tools/compile_native_render_asset.py",
  }};
  const std::size_t expected_dependency_count =
      includes_distance_lods ? kDistanceLodDependencyPaths.size()
                             : kLegacyDependencyPaths.size();
  if (dependencies == nullptr || dependencies->type != JsonType::ARRAY ||
      dependencies->array.size() != expected_dependency_count) {
    return false;
  }
  for (std::size_t index = 0U; index < dependencies->array.size(); ++index) {
    const JsonValue &dependency = dependencies->array[index];
    RenderPayloadDigest dependency_digest{};
    const char *expected_dependency_path =
        includes_distance_lods ? kDistanceLodDependencyPaths[index]
                               : kLegacyDependencyPaths[index];
    if (!ExactObject(dependency, {"path", "sha256"}) ||
        !StringValue(Member(dependency, "path"), expected_dependency_path) ||
        !ParseDigest(Member(dependency, "sha256"), dependency_digest)) {
      return false;
    }
  }

  const JsonValue *source = Member(root, "source");
  if (source == nullptr ||
      !ExactObject(*source, {"composition", "generator", "glb", "manifest_path",
                             "manifest_sha256"}) ||
      !ParseFileRecord(Member(*source, "composition"), ".composition.json",
                       parsed.composition_sha256) ||
      !ParseFileRecord(Member(*source, "generator"), ".py",
                       parsed.generator_sha256) ||
      !ParseFileRecord(Member(*source, "glb"), ".glb", parsed.glb_sha256) ||
      !ParseDigest(Member(*source, "manifest_sha256"),
                   parsed.source_manifest_sha256)) {
    return false;
  }
  const JsonValue *manifest_path = Member(*source, "manifest_path");
  if (manifest_path == nullptr || manifest_path->type != JsonType::STRING ||
      !PortablePath(manifest_path->string, ".native.json")) {
    return false;
  }

  const JsonValue *counts = Member(root, "counts");
  const bool counts_are_exact =
      counts != nullptr &&
      (includes_distance_lods
           ? ExactObject(*counts,
                         {"assets", "indices", "instances", "lod_indices",
                          "lod_levels", "materials", "meshes", "samplers",
                          "texture_bytes", "textures", "triangles", "vertices"})
           : ExactObject(*counts,
                         {"assets", "indices", "instances", "materials",
                          "meshes", "samplers", "texture_bytes", "textures",
                          "triangles", "vertices"}));
  if (!counts_are_exact) {
    return false;
  }
  const auto read_count = [counts](const char *name,
                                   std::uint64_t &output) noexcept {
    const JsonValue *value = Member(*counts, name);
    if (value == nullptr || value->type != JsonType::NUMBER) {
      return false;
    }
    output = value->number;
    return true;
  };
  if (!read_count("assets", parsed.counts.assets) ||
      !read_count("indices", parsed.counts.indices) ||
      !read_count("instances", parsed.counts.instances) ||
      (includes_distance_lods &&
       (!read_count("lod_indices", parsed.counts.lod_indices) ||
        !read_count("lod_levels", parsed.counts.lod_levels))) ||
      !read_count("materials", parsed.counts.materials) ||
      !read_count("meshes", parsed.counts.meshes) ||
      !read_count("samplers", parsed.counts.samplers) ||
      !read_count("texture_bytes", parsed.counts.texture_bytes) ||
      !read_count("textures", parsed.counts.textures) ||
      !read_count("triangles", parsed.counts.triangles) ||
      !read_count("vertices", parsed.counts.vertices) ||
      parsed.counts.assets == 0U ||
      parsed.counts.assets > kMaximumAssetCount ||
      parsed.counts.instances > kMaximumInstanceCount ||
      (includes_distance_lods &&
       (parsed.counts.lod_levels == 0U || parsed.counts.lod_indices == 0U)) ||
      parsed.counts.lod_levels >
          static_cast<std::uint64_t>(kMaximumAssetCount) *
              static_cast<std::uint64_t>(kMaximumMeshDistanceLodLevels) ||
      parsed.counts.lod_indices >
          kMaximumNativeRenderAssetPackageBytes / sizeof(std::uint32_t) ||
      parsed.counts.vertices > kMaximumMeshVertices ||
      parsed.counts.indices > kMaximumMeshIndices ||
      parsed.counts.texture_bytes > kMaximumNativeRenderAssetPackageBytes) {
    return false;
  }

  const JsonValue *assets = Member(root, "assets");
  if (assets == nullptr || assets->type != JsonType::ARRAY ||
      assets->array.size() != parsed.counts.assets) {
    return false;
  }
  std::uint64_t previous_asset_id = 0U;
  for (const JsonValue &entry : assets->array) {
    if (!ExactObject(entry, {"kind", "logical_id", "source_id_hex"})) {
      return false;
    }
    const JsonValue *kind = Member(entry, "kind");
    const JsonValue *logical_id = Member(entry, "logical_id");
    const JsonValue *source_id = Member(entry, "source_id_hex");
    ManifestAsset asset;
    if (kind == nullptr || kind->type != JsonType::STRING ||
        logical_id == nullptr || logical_id->type != JsonType::STRING ||
        !CanonicalIdentifier(logical_id->string) || source_id == nullptr ||
        source_id->type != JsonType::STRING ||
        !ParseLowerHex(source_id->string, asset.source_id)) {
      return false;
    }
    asset.kind_name = kind->string;
    asset.logical_id = logical_id->string;
    if (asset.kind_name == "mesh") {
      asset.kind = RenderAssetKind::MESH;
      asset.record_type = kRecordMesh;
    } else if (asset.kind_name == "texture") {
      asset.kind = RenderAssetKind::TEXTURE;
      asset.record_type = kRecordTexture;
    } else if (asset.kind_name == "material") {
      asset.kind = RenderAssetKind::MATERIAL;
      asset.record_type = kRecordMaterial;
    } else if (asset.kind_name == "sampler") {
      asset.kind = RenderAssetKind::SAMPLER;
      asset.record_type = kRecordSampler;
    } else {
      return false;
    }
    if (asset.source_id <= previous_asset_id ||
        asset.source_id != SourceIdentity(parsed.package_id, asset.kind_name,
                                          asset.logical_id)) {
      return false;
    }
    previous_asset_id = asset.source_id;
    parsed.assets.push_back(std::move(asset));
  }

  const JsonValue *instances = Member(root, "instances");
  if (instances == nullptr || instances->type != JsonType::ARRAY ||
      instances->array.size() != parsed.counts.instances) {
    return false;
  }
  std::uint64_t previous_instance_id = 0U;
  for (const JsonValue &entry : instances->array) {
    if (!ExactObject(entry, {"flags", "logical_id", "source_id_hex"})) {
      return false;
    }
    const JsonValue *logical_id = Member(entry, "logical_id");
    const JsonValue *source_id = Member(entry, "source_id_hex");
    const JsonValue *flags = Member(entry, "flags");
    ManifestInstance instance;
    if (logical_id == nullptr || logical_id->type != JsonType::STRING ||
        !CanonicalIdentifier(logical_id->string) || source_id == nullptr ||
        source_id->type != JsonType::STRING ||
        !ParseLowerHex(source_id->string, instance.source_id) ||
        flags == nullptr || flags->type != JsonType::ARRAY) {
      return false;
    }
    instance.logical_id = logical_id->string;
    std::string previous_flag;
    for (const JsonValue &flag : flags->array) {
      if (flag.type != JsonType::STRING ||
          (!previous_flag.empty() && flag.string <= previous_flag)) {
        return false;
      }
      previous_flag = flag.string;
      if (flag.string == "casts_shadow") {
        instance.flags |= MESH_INSTANCE_CASTS_SHADOW;
      } else if (flag.string == "receives_shadow") {
        instance.flags |= MESH_INSTANCE_RECEIVES_SHADOW;
      } else if (flag.string == "visible_in_reflections") {
        instance.flags |= MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
      } else {
        return false;
      }
    }
    if (instance.source_id <= previous_instance_id ||
        instance.source_id !=
            SourceIdentity(parsed.package_id, "object", instance.logical_id)) {
      return false;
    }
    previous_instance_id = instance.source_id;
    parsed.instances.push_back(std::move(instance));
  }
  return true;
}

bool ReadRecordHeader(Reader &reader, std::uint32_t &type,
                      std::uint64_t &source_id,
                      std::uint64_t &payload_size) noexcept {
  std::uint32_t flags = 0U;
  return reader.ReadU32(type) && reader.ReadU32(flags) && flags == 0U &&
         reader.ReadU64(source_id) && reader.ReadU64(payload_size) &&
         payload_size <= reader.remaining();
}

bool IsOrderedTriangleSubsequence(
    const std::vector<std::uint32_t> &parent,
    const std::vector<std::uint32_t> &candidate) noexcept {
  if (parent.size() % 3U != 0U || candidate.size() % 3U != 0U ||
      candidate.empty() || candidate.size() >= parent.size()) {
    return false;
  }
  std::size_t parent_offset = 0U;
  for (std::size_t candidate_offset = 0U;
       candidate_offset < candidate.size(); candidate_offset += 3U) {
    while (parent_offset < parent.size() &&
           (parent[parent_offset] != candidate[candidate_offset] ||
            parent[parent_offset + 1U] != candidate[candidate_offset + 1U] ||
            parent[parent_offset + 2U] != candidate[candidate_offset + 2U])) {
      parent_offset += 3U;
    }
    if (parent_offset == parent.size()) {
      return false;
    }
    parent_offset += 3U;
  }
  return true;
}

ValidationResult DecodeMesh(Reader &reader, RenderAssetPayload &payload,
                            std::uint32_t package_version) {
  MeshResourceDescriptor mesh;
  // Package v1/v2 mesh record 1 has no portable LOD bytes. Package v3 record 2
  // adds one explicit distance/index ladder after the immutable base streams.
  constexpr std::uint32_t kBaseMeshRecordVersion = 1U;
  constexpr std::uint32_t kDistanceLodMeshRecordVersion = 2U;
  const bool includes_distance_lods =
      package_version == kNativeRenderAssetPackageDistanceLodVersion;
  const std::uint32_t expected_mesh_record_version =
      includes_distance_lods ? kDistanceLodMeshRecordVersion
                             : kBaseMeshRecordVersion;
  std::uint32_t version = 0U;
  std::uint8_t topology = 0U;
  std::uint8_t index_format = 0U;
  std::uint8_t dynamic = 0U;
  std::uint8_t reserved = 0U;
  if (!reader.ReadU32(version) ||
      version != expected_mesh_record_version ||
      !reader.ReadString(mesh.debug_name) || !reader.ReadU8(topology) ||
      !reader.ReadU8(index_format) || !reader.ReadU8(dynamic) ||
      !reader.ReadU8(reserved) || reserved != 0U || dynamic != 0U ||
      !reader.ReadU64(mesh.topology_revision) ||
      mesh.topology_revision != 1U ||
      !ReadFloat3(reader, mesh.local_bounds.minimum) ||
      !ReadFloat3(reader, mesh.local_bounds.maximum)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh",
                   "mesh header is truncated or non-canonical");
  }
  mesh.version = kMeshResourceDescriptorVersion;
  mesh.topology = static_cast<MeshPrimitiveTopology>(topology);
  mesh.index_format = static_cast<MeshIndexFormat>(index_format);
  mesh.dynamic = false;
  if (mesh.topology != MeshPrimitiveTopology::TRIANGLE_LIST) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "native.mesh.topology",
                   "v1 native packages accept triangle lists only");
  }
  std::array<std::uint32_t, 8U> counts{};
  for (std::uint32_t &count : counts) {
    if (!reader.ReadU32(count)) {
      return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh.counts",
                     "mesh stream counts are truncated");
    }
  }
  if (counts[0U] == 0U || counts[0U] > kMaximumMeshVertices ||
      counts[1U] != counts[0U] || counts[2U] != counts[0U] ||
      counts[3U] != 0U || counts[4U] != counts[0U] || counts[5U] != 0U ||
      counts[6U] != 0U ||
      counts[7U] == 0U || counts[7U] > kMaximumMeshIndices ||
      counts[7U] % 3U != 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "native.mesh.counts",
                   "mesh vertex/index counts exceed the v1 profile");
  }
  const MeshIndexFormat canonical_index_format =
      counts[0U] <= 65535U ? MeshIndexFormat::UINT16
                           : MeshIndexFormat::UINT32;
  if (mesh.index_format != canonical_index_format) {
    return Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                   "native.mesh.index_format",
                   "mesh must use the smallest canonical index format");
  }
  const auto read_float2 = [](Reader &source, Float2 &value) {
    return ReadFloat2(source, value);
  };
  const auto read_float3 = [](Reader &source, Float3 &value) {
    return ReadFloat3(source, value);
  };
  const auto read_float4 = [](Reader &source, Float4 &value) {
    return ReadFloat4(source, value);
  };
  if (!ReadElements(reader, counts[0U], kMaximumMeshVertices, 12U,
                    mesh.positions,
                    read_float3) ||
      !ReadElements(reader, counts[1U], kMaximumMeshVertices, 12U,
                    mesh.normals,
                    read_float3) ||
      !ReadElements(reader, counts[2U], kMaximumMeshVertices, 16U,
                    mesh.tangents,
                    read_float4) ||
      !ReadElements(reader, counts[3U], kMaximumMeshVertices, 12U,
                    mesh.velocities,
                    read_float3) ||
      !ReadElements(reader, counts[4U], kMaximumMeshVertices,
                    8U,
                    mesh.texture_coordinates_0, read_float2) ||
      !ReadElements(reader, counts[5U], kMaximumMeshVertices,
                    8U,
                    mesh.texture_coordinates_1, read_float2) ||
      !ReadElements(reader, counts[6U], kMaximumMeshVertices, 16U,
                    mesh.colors,
                    read_float4)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh.streams",
                   "mesh stream payload is truncated or non-canonical");
  }
  if (counts[7U] > reader.remaining() / sizeof(std::uint32_t)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh.indices",
                   "mesh index count exceeds the record payload");
  }
  mesh.indices.resize(counts[7U]);
  for (std::uint32_t &index : mesh.indices) {
    if (!reader.ReadU32(index)) {
      return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh.indices",
                     "mesh index payload is truncated");
    }
  }
  if (includes_distance_lods) {
    std::uint32_t level_count = 0U;
    if (!reader.ReadU32(level_count) ||
        level_count > kMaximumMeshDistanceLodLevels) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "native.mesh.distance_lod_levels",
                     "mesh LOD level count is truncated or exceeds the portable limit");
    }
    mesh.distance_lod_levels.reserve(level_count);
    const std::vector<std::uint32_t> *previous_indices = &mesh.indices;
    float previous_distance = 0.0F;
    for (std::uint32_t level_index = 0U; level_index < level_count;
         ++level_index) {
      MeshDistanceLodLevelDescriptor level;
      std::uint32_t index_count = 0U;
      if (!ReadCanonicalFloat(reader, level.activation_distance_meters) ||
          !reader.ReadU32(index_count)) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "native.mesh.distance_lod_levels",
                       "mesh LOD header is truncated", level_index);
      }
      if (level.activation_distance_meters <= previous_distance ||
          index_count == 0U || index_count % 3U != 0U ||
          index_count >= previous_indices->size() ||
          index_count > kMaximumMeshIndices ||
          index_count > reader.remaining() / sizeof(std::uint32_t)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "native.mesh.distance_lod_levels",
                       "mesh LOD distance/count is not strictly ordered and reduced",
                       level_index);
      }
      level.indices.resize(index_count);
      for (std::uint32_t &index : level.indices) {
        if (!reader.ReadU32(index)) {
          return Failure(ValidationCode::SIZE_MISMATCH,
                         "native.mesh.distance_lod_levels.indices",
                         "mesh LOD index payload is truncated", level_index);
        }
      }
      if (!IsOrderedTriangleSubsequence(*previous_indices, level.indices)) {
        return Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                       "native.mesh.distance_lod_levels.indices",
                       "mesh LOD triangles are not an ordered subset of the preceding level",
                       level_index);
      }
      previous_distance = level.activation_distance_meters;
      mesh.distance_lod_levels.push_back(std::move(level));
      previous_indices = &mesh.distance_lod_levels.back().indices;
    }
  }
  if (!reader.empty()) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.mesh",
                   "mesh record has trailing bytes");
  }
  const ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }
  const ValidationResult tangent_validation =
      ValidateMeshTangentDerivatives(mesh);
  if (!tangent_validation) {
    return tangent_validation;
  }
  Bounds3 exact_bounds{mesh.positions.front(), mesh.positions.front()};
  for (const Float3 &position : mesh.positions) {
    exact_bounds.minimum.x = (std::min)(exact_bounds.minimum.x, position.x);
    exact_bounds.minimum.y = (std::min)(exact_bounds.minimum.y, position.y);
    exact_bounds.minimum.z = (std::min)(exact_bounds.minimum.z, position.z);
    exact_bounds.maximum.x = (std::max)(exact_bounds.maximum.x, position.x);
    exact_bounds.maximum.y = (std::max)(exact_bounds.maximum.y, position.y);
    exact_bounds.maximum.z = (std::max)(exact_bounds.maximum.z, position.z);
  }
  if (mesh.local_bounds.minimum != exact_bounds.minimum ||
      mesh.local_bounds.maximum != exact_bounds.maximum) {
    return Failure(ValidationCode::INVALID_BOUNDS, "native.mesh.bounds",
                   "mesh bounds must be the exact position bounds");
  }
  payload = std::move(mesh);
  return ValidationResult::Success();
}

ValidationResult DecodeTexture(Reader &reader, RenderAssetPayload &payload) {
  TextureResourceDescriptor texture;
  // Checked native package v1/v2 artifacts serialize RGBA8 texture records
  // with the immutable version-1 row-pitch contract.  The live descriptor
  // later moved to version 2 for block-compressed storage, so decode either
  // explicit wire contract without reinterpreting one as the other.  Both
  // publish the current in-memory descriptor, just like the mesh upgrade
  // above; unknown record versions remain fail-closed.
  constexpr std::uint32_t kLegacyNativePackageTextureRecordVersion = 1U;
  std::uint32_t version = 0U;
  std::uint8_t type = 0U;
  std::uint8_t format = 0U;
  std::uint8_t color_space = 0U;
  std::uint8_t reserved = 0U;
  std::uint32_t mip_count = 0U;
  if (!reader.ReadU32(version) ||
      (version != kLegacyNativePackageTextureRecordVersion &&
       version != kTextureResourceDescriptorVersion) ||
      !reader.ReadString(texture.debug_name) || !reader.ReadU8(type) ||
      !reader.ReadU8(format) || !reader.ReadU8(color_space) ||
      !reader.ReadU8(reserved) || reserved != 0U ||
      !reader.ReadU32(texture.width) || !reader.ReadU32(texture.height) ||
      !reader.ReadU32(texture.array_layers) || !reader.ReadU32(mip_count)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.texture",
                   "texture header is truncated or non-canonical");
  }
  texture.version = kTextureResourceDescriptorVersion;
  texture.type = static_cast<TextureResourceType>(type);
  texture.format = static_cast<TextureResourceFormat>(format);
  texture.color_space = static_cast<TextureColorSpace>(color_space);
  const bool legacy_rgba8 =
      version == kLegacyNativePackageTextureRecordVersion &&
      texture.format == TextureResourceFormat::RGBA8_UNORM;
  const bool current_format =
      version == kTextureResourceDescriptorVersion &&
      (texture.format == TextureResourceFormat::RGBA8_UNORM ||
       texture.format == TextureResourceFormat::BC1_UNORM ||
       texture.format == TextureResourceFormat::BC3_UNORM ||
       texture.format == TextureResourceFormat::BC4_UNORM ||
       texture.format == TextureResourceFormat::BC5_UNORM ||
       texture.format == TextureResourceFormat::BC7_UNORM);
  if (texture.type != TextureResourceType::TEXTURE_2D ||
      (!legacy_rgba8 && !current_format) ||
      texture.width == 0U || texture.width > kMaximumTextureDimension ||
      texture.height == 0U || texture.height > kMaximumTextureDimension ||
      texture.array_layers != 1U || mip_count == 0U ||
      mip_count > kMaximumTextureMips) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE, "native.texture",
                   "texture record version/format pairing is unsupported");
  }
  std::uint32_t expected_width = texture.width;
  std::uint32_t expected_height = texture.height;
  texture.mip_levels.reserve(mip_count);
  for (std::uint32_t index = 0U; index < mip_count; ++index) {
    TextureMipLevelDescriptor mip;
    std::uint64_t bytes = 0U;
    if (!reader.ReadU32(mip.width) || !reader.ReadU32(mip.height) ||
        !reader.ReadU64(mip.row_pitch_bytes) ||
        !reader.ReadU64(mip.layer_pitch_bytes) || reader.ReadU64(bytes) == false ||
        mip.width != expected_width || mip.height != expected_height ||
        mip.row_pitch_bytes !=
            MinimumTextureRowPitchBytes(texture.format, mip.width) ||
        mip.layer_pitch_bytes !=
            mip.row_pitch_bytes *
                TextureBlockRowCount(texture.format, mip.height) ||
        bytes != mip.layer_pitch_bytes || bytes > reader.remaining() ||
        bytes > kMaximumNativeRenderAssetPackageBytes ||
        !reader.ReadVector(mip.bytes, static_cast<std::size_t>(bytes))) {
      return Failure(ValidationCode::SIZE_MISMATCH, "native.texture.mips",
                     "texture mip chain is truncated or non-canonical", index);
    }
    texture.mip_levels.push_back(std::move(mip));
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }
  if (texture.mip_levels.back().width != 1U ||
      texture.mip_levels.back().height != 1U || !reader.empty()) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.texture.mips",
                   "texture requires a complete chain ending at 1x1");
  }
  const ValidationResult validation = ValidateTextureResourceDescriptor(texture);
  if (!validation) {
    return validation;
  }
  payload = std::move(texture);
  return ValidationResult::Success();
}

ValidationResult DecodeSampler(Reader &reader, RenderAssetPayload &payload) {
  SamplerResourceDescriptor sampler;
  std::uint32_t version = 0U;
  std::uint8_t minification = 0U;
  std::uint8_t magnification = 0U;
  std::uint8_t mip = 0U;
  std::uint8_t address_u = 0U;
  std::uint8_t address_v = 0U;
  std::uint8_t address_w = 0U;
  std::uint16_t reserved16 = 0U;
  std::uint8_t anisotropy = 0U;
  std::uint8_t comparison = 0U;
  std::uint8_t compare_operation = 0U;
  if (!reader.ReadU32(version) ||
      version != kSamplerResourceDescriptorVersion ||
      !reader.ReadString(sampler.debug_name) ||
      !reader.ReadU8(minification) || !reader.ReadU8(magnification) ||
      !reader.ReadU8(mip) || !reader.ReadU8(address_u) ||
      !reader.ReadU8(address_v) || !reader.ReadU8(address_w) ||
      !reader.ReadU16(reserved16) || reserved16 != 0U ||
      !ReadCanonicalFloat(reader, sampler.mip_lod_bias) ||
      !ReadCanonicalFloat(reader, sampler.minimum_lod) ||
      !ReadCanonicalFloat(reader, sampler.maximum_lod) ||
      !reader.ReadU8(anisotropy) || anisotropy > 1U ||
      !reader.ReadZeroes(3U) ||
      !ReadCanonicalFloat(reader, sampler.maximum_anisotropy) ||
      !reader.ReadU8(comparison) || comparison > 1U ||
      !reader.ReadU8(compare_operation) || !reader.ReadU16(reserved16) ||
      reserved16 != 0U || !ReadFloat4(reader, sampler.border_color) ||
      !reader.empty()) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.sampler",
                   "sampler record is truncated or non-canonical");
  }
  sampler.version = version;
  sampler.minification_filter = static_cast<SamplerFilter>(minification);
  sampler.magnification_filter = static_cast<SamplerFilter>(magnification);
  sampler.mip_filter = static_cast<SamplerFilter>(mip);
  sampler.address_u = static_cast<SamplerAddressMode>(address_u);
  sampler.address_v = static_cast<SamplerAddressMode>(address_v);
  sampler.address_w = static_cast<SamplerAddressMode>(address_w);
  sampler.anisotropy_enabled = anisotropy != 0U;
  sampler.compare_enabled = comparison != 0U;
  sampler.compare_operation =
      static_cast<SamplerCompareOperation>(compare_operation);
  const ValidationResult validation = ValidateSamplerResourceDescriptor(sampler);
  if (!validation) {
    return validation;
  }
  payload = std::move(sampler);
  return ValidationResult::Success();
}

TextureBinding &BindingAt(MaterialDescriptor &material,
                          std::size_t slot) noexcept {
  switch (static_cast<MaterialTextureSlot>(slot)) {
  case MaterialTextureSlot::BASE_COLOR:
    return material.base_color_texture;
  case MaterialTextureSlot::METALLIC_ROUGHNESS:
    return material.metallic_roughness_texture;
  case MaterialTextureSlot::NORMAL:
    return material.normal_texture;
  case MaterialTextureSlot::OCCLUSION:
    return material.occlusion_texture;
  case MaterialTextureSlot::EMISSIVE:
    return material.emissive_texture;
  case MaterialTextureSlot::SPECULAR:
    return material.specular_texture;
  case MaterialTextureSlot::DETAIL_WEIGHT:
    return material.detail_weight_texture;
  case MaterialTextureSlot::DETAIL0:
    return material.detail_textures[0];
  case MaterialTextureSlot::DETAIL1:
    return material.detail_textures[1];
  case MaterialTextureSlot::DETAIL2:
    return material.detail_textures[2];
  case MaterialTextureSlot::DETAIL3:
    return material.detail_textures[3];
  case MaterialTextureSlot::DETAIL0_NM:
    return material.detail_normal_textures[0];
  case MaterialTextureSlot::DETAIL1_NM:
    return material.detail_normal_textures[1];
  case MaterialTextureSlot::DETAIL2_NM:
    return material.detail_normal_textures[2];
  case MaterialTextureSlot::DETAIL3_NM:
    return material.detail_normal_textures[3];
  }
  return material.base_color_texture;
}

const TextureBinding &BindingAt(const MaterialDescriptor &material,
                                std::size_t slot) noexcept {
  switch (static_cast<MaterialTextureSlot>(slot)) {
  case MaterialTextureSlot::BASE_COLOR:
    return material.base_color_texture;
  case MaterialTextureSlot::METALLIC_ROUGHNESS:
    return material.metallic_roughness_texture;
  case MaterialTextureSlot::NORMAL:
    return material.normal_texture;
  case MaterialTextureSlot::OCCLUSION:
    return material.occlusion_texture;
  case MaterialTextureSlot::EMISSIVE:
    return material.emissive_texture;
  case MaterialTextureSlot::SPECULAR:
    return material.specular_texture;
  case MaterialTextureSlot::DETAIL_WEIGHT:
    return material.detail_weight_texture;
  case MaterialTextureSlot::DETAIL0:
    return material.detail_textures[0];
  case MaterialTextureSlot::DETAIL1:
    return material.detail_textures[1];
  case MaterialTextureSlot::DETAIL2:
    return material.detail_textures[2];
  case MaterialTextureSlot::DETAIL3:
    return material.detail_textures[3];
  case MaterialTextureSlot::DETAIL0_NM:
    return material.detail_normal_textures[0];
  case MaterialTextureSlot::DETAIL1_NM:
    return material.detail_normal_textures[1];
  case MaterialTextureSlot::DETAIL2_NM:
    return material.detail_normal_textures[2];
  case MaterialTextureSlot::DETAIL3_NM:
    return material.detail_normal_textures[3];
  }
  return material.base_color_texture;
}

ValidationResult DecodeMaterial(
    Reader &reader, RenderAssetPayload &payload,
    std::array<GraphicsSceneAssetBinding,
               kGraphicsSceneMaterialTextureSlotCount> &source_bindings,
    std::uint32_t package_version) {
  MaterialDescriptor material;
  const bool package_has_transmission =
      package_version == kNativeRenderAssetPackageTransmissionVersion ||
      package_version == kNativeRenderAssetPackageDistanceLodVersion;
  std::uint32_t version = 0U;
  std::array<std::uint8_t, 8U> state{};
  const std::uint32_t expected_material_version =
      package_has_transmission ? kMaterialDescriptorTransmissionVersion
                               : kMaterialDescriptorVersion;
  if (!reader.ReadU32(version) || version != expected_material_version ||
      !reader.ReadString(material.debug_name)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.material",
                   "material header is truncated");
  }
  for (std::uint8_t &value : state) {
    if (!reader.ReadU8(value)) {
      return Failure(ValidationCode::SIZE_MISMATCH, "native.material.state",
                     "material state is truncated");
    }
  }
  if (state[5U] > 1U || state[6U] > 1U ||
      (package_version == kNativeRenderAssetPackageVersion &&
       state[7U] != 0U) ||
      (package_has_transmission &&
       state[7U] > static_cast<std::uint8_t>(
                        MaterialTransmissionMode::THIN_PARALLEL_SLAB))) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "native.material.state",
                   "material booleans/reserved byte are non-canonical");
  }
  material.version = version;
  material.model = static_cast<MaterialModel>(state[0U]);
  material.pbr_workflow = static_cast<MaterialPbrWorkflow>(state[1U]);
  material.blend_mode = static_cast<MaterialBlendMode>(state[2U]);
  material.alpha_test_mode = static_cast<MaterialAlphaTestMode>(state[3U]);
  material.base_color_transfer = static_cast<BaseColorTransfer>(state[4U]);
  material.double_sided = state[5U] != 0U;
  material.depth_write = state[6U] != 0U;
  material.transmission_mode =
      static_cast<MaterialTransmissionMode>(state[7U]);
  if (!ReadFloat4(reader, material.base_color_factor) ||
      !ReadCanonicalFloat(reader, material.metallic_factor) ||
      !ReadCanonicalFloat(reader, material.roughness_factor) ||
      !ReadFloat3(reader, material.specular_factor) ||
      !ReadCanonicalFloat(reader, material.normal_scale) ||
      !ReadCanonicalFloat(reader, material.occlusion_strength) ||
      !ReadFloat3(reader, material.emissive_factor) ||
      !ReadCanonicalFloat(reader, material.emissive_strength) ||
      !ReadCanonicalFloat(reader, material.alpha_cutoff) ||
      !ReadCanonicalFloat(reader, material.index_of_refraction)) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "native.material.factors",
                   "material factor payload is truncated or non-canonical");
  }
  if (package_has_transmission &&
      (!ReadCanonicalFloat(reader, material.transmission_factor) ||
       !ReadFloat3(reader, material.attenuation_color) ||
       !ReadCanonicalFloat(reader, material.attenuation_distance_m) ||
       !ReadCanonicalFloat(reader, material.slab_thickness_m))) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "native.material.transmission",
                   "material transmission payload is truncated or non-canonical");
  }
  for (std::size_t slot = 0U;
       slot < kNativePackageMaterialTextureSlotCount; ++slot) {
    std::uint64_t texture_source_id = 0U;
    std::uint64_t sampler_source_id = 0U;
    std::uint8_t texture_coordinate_set = 0U;
    Float2 scale{};
    Float2 offset{};
    float rotation = 0.0F;
    if (!reader.ReadU64(texture_source_id) ||
        !reader.ReadU64(sampler_source_id) ||
        !reader.ReadU8(texture_coordinate_set) || !reader.ReadZeroes(7U) ||
        !ReadFloat2(reader, scale) || !ReadFloat2(reader, offset) ||
        !ReadCanonicalFloat(reader, rotation)) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "native.material.bindings",
                     "material binding payload is truncated", slot);
    }
    if ((texture_source_id == 0U) != (sampler_source_id == 0U)) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "native.material.bindings",
                     "texture and sampler source IDs must be absent together",
                     slot);
    }
    if (texture_source_id == 0U) {
      if (texture_coordinate_set != 0U || scale != Float2{} ||
          offset != Float2{} || rotation != 0.0F) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "native.material.bindings",
                       "absent binding requires exact zero package fields",
                       slot);
      }
      continue;
    }
    TextureBinding &binding = BindingAt(material, slot);
    binding.texture_coordinate_set = texture_coordinate_set;
    binding.scale = scale;
    binding.offset = offset;
    binding.rotation_radians = rotation;
    source_bindings[slot] =
        GraphicsSceneAssetBinding{texture_source_id, sampler_source_id};
  }
  if (!reader.empty()) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.material",
                   "material record has trailing bytes");
  }
  const ValidationResult validation = ValidateMaterialDescriptor(material);
  if (!validation) {
    return validation;
  }
  payload = std::move(material);
  return ValidationResult::Success();
}

ValidationResult DecodeInstance(Reader &reader, std::uint64_t source_id,
                                GraphicsSceneStaticMeshInput &instance) {
  std::uint32_t version = 0U;
  if (!reader.ReadU32(version) || version != 1U ||
      !reader.ReadU64(instance.mesh_source_asset_id) ||
      !reader.ReadU64(instance.material_source_asset_id)) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.instance",
                   "static instance header is truncated");
  }
  instance.source_object_id = source_id;
  for (float &value : instance.render_from_object.elements) {
    if (!ReadCanonicalFloat(reader, value)) {
      return Failure(ValidationCode::NON_FINITE_VALUE,
                     "native.instance.transform",
                     "static instance transform is non-canonical");
    }
  }
  if (!reader.ReadU32(instance.visibility_mask) ||
      !reader.ReadU32(instance.flags) || !reader.empty()) {
    return Failure(ValidationCode::SIZE_MISMATCH, "native.instance",
                   "static instance record is truncated or has trailing bytes");
  }
  constexpr std::uint32_t kKnownFlags = MESH_INSTANCE_CASTS_SHADOW |
                                        MESH_INSTANCE_RECEIVES_SHADOW |
                                        MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
  if (instance.mesh_source_asset_id == 0U ||
      instance.material_source_asset_id == 0U ||
      instance.visibility_mask != 0xFFFFFFFFU ||
      (instance.flags & ~kKnownFlags) != 0U ||
      !HasInvertibleAffineTransform(instance.render_from_object)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "native.instance",
                   "static instance values are invalid");
  }
  return ValidationResult::Success();
}

struct ResolvedAsset {
  RenderAssetKind kind = RenderAssetKind::INVALID;
  const GraphicsSceneAssetInput *input = nullptr;
};

ValidationResult ValidateDependencies(NativeRenderAssetPackage &package) {
  std::map<std::uint64_t, ResolvedAsset> assets;
  for (const GraphicsSceneAssetInput &input : package.assets) {
    if (input.source_asset_id == 0U || input.payload == nullptr ||
        input.payload->valueless_by_exception()) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "native.assets",
                     "decoded asset is missing identity or payload");
    }
    const RenderAssetKind kind = RenderAssetPayloadKind(*input.payload);
    if (!IsKnownRenderAssetKind(kind) ||
        !assets.emplace(input.source_asset_id, ResolvedAsset{kind, &input})
             .second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER, "native.assets",
                     "decoded asset identity is duplicate or invalid");
    }
  }
  for (std::size_t asset_index = 0U; asset_index < package.assets.size();
       ++asset_index) {
    const GraphicsSceneAssetInput &input = package.assets[asset_index];
    const auto *material =
        std::get_if<MaterialDescriptor>(input.payload.get());
    if (material == nullptr) {
      for (const GraphicsSceneAssetBinding &binding : input.material_bindings) {
        if (binding != GraphicsSceneAssetBinding{}) {
          return Failure(ValidationCode::WRONG_ASSET_KIND,
                         "native.assets.bindings",
                         "non-material asset contains source bindings",
                         asset_index);
        }
      }
      continue;
    }
    for (std::size_t slot = 0U;
         slot < kGraphicsSceneMaterialTextureSlotCount; ++slot) {
      const GraphicsSceneAssetBinding &binding = input.material_bindings[slot];
      if ((binding.texture_source_asset_id == 0U) !=
          (binding.sampler_source_asset_id == 0U)) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "native.material.bindings",
                       "material source binding is half absent", asset_index);
      }
      if (binding.texture_source_asset_id == 0U) {
        continue;
      }
      const auto texture_entry = assets.find(binding.texture_source_asset_id);
      const auto sampler_entry = assets.find(binding.sampler_source_asset_id);
      if (texture_entry == assets.end() || sampler_entry == assets.end()) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "native.material.bindings",
                       "material source binding references an absent asset",
                       asset_index);
      }
      if (texture_entry->second.kind != RenderAssetKind::TEXTURE ||
          sampler_entry->second.kind != RenderAssetKind::SAMPLER) {
        return Failure(ValidationCode::WRONG_ASSET_KIND,
                       "native.material.bindings",
                       "material source binding references the wrong asset kind",
                       asset_index);
      }
      const auto *texture = std::get_if<TextureResourceDescriptor>(
          texture_entry->second.input->payload.get());
      const auto *sampler = std::get_if<SamplerResourceDescriptor>(
          sampler_entry->second.input->payload.get());
      if (texture == nullptr || sampler == nullptr) {
        return Failure(ValidationCode::WRONG_ASSET_KIND,
                       "native.material.bindings",
                       "material source binding payload kind is inconsistent",
                       asset_index);
      }
      const ValidationResult validation = ValidateMaterialTextureCompatibility(
          static_cast<MaterialTextureSlot>(slot), *texture, *sampler);
      if (!validation) {
        return validation;
      }
    }
    const bool metallic_roughness_bound =
        input.material_bindings[static_cast<std::size_t>(
            MaterialTextureSlot::METALLIC_ROUGHNESS)]
            .texture_source_asset_id != 0U;
    const bool specular_bound =
        input.material_bindings[static_cast<std::size_t>(
            MaterialTextureSlot::SPECULAR)]
            .texture_source_asset_id != 0U;
    if ((material->pbr_workflow ==
             MaterialPbrWorkflow::METALLIC_ROUGHNESS &&
         specular_bound) ||
        (material->pbr_workflow == MaterialPbrWorkflow::SPECULAR &&
         metallic_roughness_bound)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "native.material.workflow",
                     "source binding conflicts with the material PBR workflow",
                     asset_index);
    }
    if (material->alpha_test_mode != MaterialAlphaTestMode::DISABLED &&
        input.material_bindings[static_cast<std::size_t>(
            MaterialTextureSlot::BASE_COLOR)]
                .texture_source_asset_id == 0U) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "native.material.alpha",
                     "alpha testing requires a bound base-color source",
                     asset_index);
    }
  }
  for (std::size_t instance_index = 0U;
       instance_index < package.static_meshes.size(); ++instance_index) {
    const GraphicsSceneStaticMeshInput &instance =
        package.static_meshes[instance_index];
    const auto mesh_entry = assets.find(instance.mesh_source_asset_id);
    const auto material_entry = assets.find(instance.material_source_asset_id);
    if (mesh_entry == assets.end() || material_entry == assets.end()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "native.instances.assets",
                     "static instance references an absent asset",
                     instance_index);
    }
    if (mesh_entry->second.kind != RenderAssetKind::MESH ||
        material_entry->second.kind != RenderAssetKind::MATERIAL) {
      return Failure(ValidationCode::WRONG_ASSET_KIND,
                     "native.instances.assets",
                     "static instance references the wrong asset kind",
                     instance_index);
    }
    const auto *mesh = std::get_if<MeshResourceDescriptor>(
        mesh_entry->second.input->payload.get());
    const auto *material = std::get_if<MaterialDescriptor>(
        material_entry->second.input->payload.get());
    if (mesh == nullptr || material == nullptr) {
      return Failure(ValidationCode::WRONG_ASSET_KIND,
                     "native.instances.assets",
                     "static instance payload kind is inconsistent",
                     instance_index);
    }
    const ValidationResult validation =
        ValidateMaterialMeshCompatibility(*material, *mesh);
    if (!validation) {
      return validation;
    }
    const GraphicsSceneAssetInput &material_input =
        *material_entry->second.input;
    for (std::size_t slot = 0U;
         slot < kGraphicsSceneMaterialTextureSlotCount; ++slot) {
      if (material_input.material_bindings[slot]
              .texture_source_asset_id == 0U) {
        continue;
      }
      const TextureBinding &texture_binding = BindingAt(*material, slot);
      const bool has_coordinates =
          texture_binding.texture_coordinate_set == 0U
              ? !mesh->texture_coordinates_0.empty()
              : !mesh->texture_coordinates_1.empty();
      if (!has_coordinates) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "native.instances.mesh_uv",
                       "bound source texture requires an authored UV stream",
                       instance_index);
      }
      if (slot == static_cast<std::size_t>(MaterialTextureSlot::NORMAL) &&
          mesh->tangents.empty()) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "native.instances.mesh_tangents",
                       "bound normal source requires authored tangents",
                       instance_index);
      }
    }
  }
  return ValidationResult::Success();
}

const std::string *PayloadDebugName(const RenderAssetPayload &payload) {
  return std::visit(
      [](const auto &value) -> const std::string * {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::monostate>) {
          return nullptr;
        } else {
          return &value.debug_name;
        }
      },
      payload);
}

ValidationResult ValidateManifestBinding(
    const NativeRenderAssetPackage &package,
    const ParsedManifest &manifest) {
  if (package.assets.size() != manifest.assets.size() ||
      package.static_meshes.size() != manifest.instances.size()) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "native.package.manifest.counts",
                   "manifest record counts do not match decoded records");
  }

  ManifestCounts actual;
  actual.assets = package.assets.size();
  actual.instances = package.static_meshes.size();
  for (std::size_t index = 0U; index < package.assets.size(); ++index) {
    const GraphicsSceneAssetInput &input = package.assets[index];
    const ManifestAsset &expected = manifest.assets[index];
    if (input.payload == nullptr ||
        input.source_asset_id != expected.source_id ||
        RenderAssetPayloadKind(*input.payload) != expected.kind) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "native.package.manifest.assets",
                     "manifest asset ID/type does not match decoded record",
                     index);
    }
    const std::string *debug_name = PayloadDebugName(*input.payload);
    if (debug_name == nullptr || *debug_name != expected.logical_id) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "native.package.manifest.assets.logical_id",
                     "manifest logical name does not match decoded payload",
                     index);
    }
    if (const auto *mesh =
            std::get_if<MeshResourceDescriptor>(input.payload.get())) {
      ++actual.meshes;
      actual.vertices += mesh->positions.size();
      actual.indices += mesh->indices.size();
      actual.lod_levels += mesh->distance_lod_levels.size();
      for (const MeshDistanceLodLevelDescriptor &level :
           mesh->distance_lod_levels) {
        actual.lod_indices += level.indices.size();
      }
    } else if (const auto *texture =
                   std::get_if<TextureResourceDescriptor>(input.payload.get())) {
      ++actual.textures;
      for (const TextureMipLevelDescriptor &mip : texture->mip_levels) {
        actual.texture_bytes += mip.bytes.size();
      }
    } else if (std::holds_alternative<MaterialDescriptor>(*input.payload)) {
      ++actual.materials;
    } else if (std::holds_alternative<SamplerResourceDescriptor>(
                   *input.payload)) {
      ++actual.samplers;
    }
  }
  actual.triangles = actual.indices / 3U;
  for (std::size_t index = 0U; index < package.static_meshes.size(); ++index) {
    const GraphicsSceneStaticMeshInput &instance = package.static_meshes[index];
    const ManifestInstance &expected = manifest.instances[index];
    if (instance.source_object_id != expected.source_id ||
        instance.flags != expected.flags) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "native.package.manifest.instances",
                     "manifest instance ID/flags do not match decoded record",
                     index);
    }
  }
  if (actual.assets != manifest.counts.assets ||
      actual.indices != manifest.counts.indices ||
      actual.instances != manifest.counts.instances ||
      (manifest.includes_distance_lods &&
       (actual.lod_indices != manifest.counts.lod_indices ||
        actual.lod_levels != manifest.counts.lod_levels)) ||
      actual.materials != manifest.counts.materials ||
      actual.meshes != manifest.counts.meshes ||
      actual.samplers != manifest.counts.samplers ||
      actual.texture_bytes != manifest.counts.texture_bytes ||
      actual.textures != manifest.counts.textures ||
      actual.triangles != manifest.counts.triangles ||
      actual.vertices != manifest.counts.vertices) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "native.package.manifest.counts",
                   "manifest statistics do not match decoded payloads");
  }
  return ValidationResult::Success();
}

} // namespace

NativeRenderAssetPackageDecodeResult DecodeNativeRenderAssetPackage(
    const std::uint8_t *bytes, std::size_t byte_count,
    const RenderPayloadDigest &expected_package_sha256) noexcept {
  NativeRenderAssetPackageDecodeResult result;
  try {
    if (bytes == nullptr || byte_count < kNativeRenderAssetPackageHeaderBytes) {
      result.validation = Failure(ValidationCode::EMPTY_PAYLOAD,
                                  "native.package",
                                  "package is null or truncated");
      return result;
    }
    if (byte_count > kMaximumNativeRenderAssetPackageBytes) {
      result.validation = Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                  "native.package",
                                  "package exceeds the v1 byte limit");
      return result;
    }
    const RenderPayloadDigest actual_package_sha256 =
        ComputeRenderPayloadDigest(bytes, byte_count);
    if (actual_package_sha256 != expected_package_sha256) {
      result.validation = Failure(ValidationCode::REVISION_MISMATCH,
                                  "native.package.sha256",
                                  "package does not match its trusted SHA-256");
      return result;
    }
    Reader header(bytes, kNativeRenderAssetPackageHeaderBytes);
    std::array<std::uint8_t, 8U> magic{};
    std::uint32_t version = 0U;
    std::uint32_t header_bytes = 0U;
    std::uint32_t flags = 0U;
    std::uint32_t record_count = 0U;
    std::uint32_t asset_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint64_t declared_bytes = 0U;
    RenderPayloadDigest expected_digest{};
    if (!header.ReadRaw(magic.data(), magic.size()) ||
        !header.ReadU32(version) ||
        !((version == kNativeRenderAssetPackageVersion && magic == kMagic) ||
          (version == kNativeRenderAssetPackageTransmissionVersion &&
           magic == kTransmissionMagic) ||
          (version == kNativeRenderAssetPackageDistanceLodVersion &&
           magic == kDistanceLodMagic)) ||
        !header.ReadU32(header_bytes) ||
        header_bytes != kNativeRenderAssetPackageHeaderBytes ||
        !header.ReadU32(flags) || flags != 0U ||
        !header.ReadU32(record_count) || !header.ReadU32(asset_count) ||
        !header.ReadU32(instance_count) ||
        !header.ReadU64(declared_bytes) || declared_bytes != byte_count ||
        !header.ReadRaw(expected_digest.data(), expected_digest.size()) ||
        !header.ReadZeroes(8U) || !header.empty()) {
      result.validation = Failure(ValidationCode::UNSUPPORTED_VERSION,
                                  "native.package.header",
                                  "package header is invalid or non-canonical");
      return result;
    }
    if (record_count == 0U || record_count > kMaximumRecordCount ||
        asset_count == 0U || asset_count > kMaximumAssetCount ||
        instance_count > kMaximumInstanceCount ||
        record_count != 1U + asset_count + instance_count) {
      result.validation = Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                  "native.package.counts",
                                  "package record counts are inconsistent");
      return result;
    }
    const RenderPayloadDigest actual_digest = ComputeRenderPayloadDigest(
        bytes + kNativeRenderAssetPackageHeaderBytes,
        byte_count - kNativeRenderAssetPackageHeaderBytes);
    if (actual_digest != expected_digest) {
      result.validation = Failure(ValidationCode::REVISION_MISMATCH,
                                  "native.package.body_sha256",
                                  "package body does not match its SHA-256");
      return result;
    }

    auto candidate = std::make_shared<NativeRenderAssetPackage>();
    candidate->version = version;
    candidate->package_sha256 = actual_package_sha256;
    candidate->body_sha256 = expected_digest;
    candidate->assets.reserve(asset_count);
    candidate->static_meshes.reserve(instance_count);
    Reader records(bytes + kNativeRenderAssetPackageHeaderBytes,
                   byte_count - kNativeRenderAssetPackageHeaderBytes);
    std::uint32_t record_type = 0U;
    std::uint64_t source_id = 0U;
    std::uint64_t payload_size = 0U;
    if (!ReadRecordHeader(records, record_type, source_id, payload_size) ||
        record_type != kRecordManifest || source_id != 0U ||
        payload_size == 0U || payload_size > kMaximumManifestBytes) {
      result.validation = Failure(ValidationCode::SIZE_MISMATCH,
                                  "native.package.manifest",
                                  "first record is not a bounded manifest");
      return result;
    }
    Reader manifest(nullptr, 0U);
    if (!records.Take(static_cast<std::size_t>(payload_size), manifest)) {
      result.validation = Failure(ValidationCode::SIZE_MISMATCH,
                                  "native.package.manifest",
                                  "manifest payload is truncated");
      return result;
    }
    std::vector<std::uint8_t> manifest_bytes;
    ParsedManifest parsed_manifest;
    if (!manifest.ReadVector(manifest_bytes,
                             static_cast<std::size_t>(payload_size)) ||
        !manifest.empty() ||
        !ParseManifest(manifest_bytes.data(), manifest_bytes.size(), version,
                       parsed_manifest)) {
      result.validation = Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                                  "native.package.manifest",
                                  "embedded manifest is not canonical ASCII JSON");
      return result;
    }
    if (parsed_manifest.counts.assets != asset_count ||
        parsed_manifest.counts.instances != instance_count) {
      result.validation = Failure(ValidationCode::REVISION_MISMATCH,
                                  "native.package.manifest.counts",
                                  "manifest/header record counts disagree");
      return result;
    }
    candidate->package_id = parsed_manifest.package_id;
    candidate->origin_class = parsed_manifest.origin_class;
    candidate->compiler_sha256 = parsed_manifest.compiler_sha256;
    candidate->generator_sha256 = parsed_manifest.generator_sha256;
    candidate->glb_sha256 = parsed_manifest.glb_sha256;
    candidate->composition_sha256 = parsed_manifest.composition_sha256;
    candidate->source_manifest_sha256 =
        parsed_manifest.source_manifest_sha256;
    candidate->provenance_manifest_json.assign(
        reinterpret_cast<const char *>(manifest_bytes.data()),
        manifest_bytes.size());

    std::uint64_t previous_asset_id = 0U;
    for (std::uint32_t record_index = 0U; record_index < asset_count;
         ++record_index) {
      if (!ReadRecordHeader(records, record_type, source_id, payload_size) ||
          record_type < kRecordMesh || record_type > kRecordSampler ||
          source_id == 0U ||
          (record_index != 0U && source_id <= previous_asset_id) ||
          payload_size == 0U || payload_size > records.remaining()) {
        result.validation = Failure(
            ValidationCode::NON_DETERMINISTIC_ORDER, "native.package.assets",
            "asset record header/order is invalid", record_index);
        return result;
      }
      const ManifestAsset &manifest_asset = parsed_manifest.assets[record_index];
      if (record_type != manifest_asset.record_type ||
          source_id != manifest_asset.source_id) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "native.package.manifest.assets",
            "manifest asset ID/type does not match record header", record_index);
        return result;
      }
      previous_asset_id = source_id;
      Reader payload_reader(nullptr, 0U);
      if (!records.Take(static_cast<std::size_t>(payload_size),
                        payload_reader)) {
        result.validation = Failure(ValidationCode::SIZE_MISMATCH,
                                    "native.package.assets",
                                    "asset record payload is truncated",
                                    record_index);
        return result;
      }
      GraphicsSceneAssetInput input;
      input.source_asset_id = source_id;
      RenderAssetPayload payload;
      ValidationResult validation;
      switch (record_type) {
      case kRecordMesh:
        validation = DecodeMesh(payload_reader, payload, version);
        break;
      case kRecordTexture:
        validation = DecodeTexture(payload_reader, payload);
        break;
      case kRecordMaterial:
        validation = DecodeMaterial(payload_reader, payload,
                                    input.material_bindings, version);
        break;
      case kRecordSampler:
        validation = DecodeSampler(payload_reader, payload);
        break;
      default:
        validation = Failure(ValidationCode::INVALID_ENUM,
                             "native.package.record_type",
                             "unknown asset record type");
        break;
      }
      if (!validation) {
        validation.element_index = record_index;
        result.validation = std::move(validation);
        return result;
      }
      input.payload =
          std::make_shared<const RenderAssetPayload>(std::move(payload));
      candidate->assets.push_back(std::move(input));
    }

    std::uint64_t previous_object_id = 0U;
    for (std::uint32_t record_index = 0U; record_index < instance_count;
         ++record_index) {
      if (!ReadRecordHeader(records, record_type, source_id, payload_size) ||
          record_type != kRecordStaticInstance || source_id == 0U ||
          (record_index != 0U && source_id <= previous_object_id) ||
          payload_size == 0U || payload_size > records.remaining()) {
        result.validation = Failure(
            ValidationCode::NON_DETERMINISTIC_ORDER,
            "native.package.instances",
            "static instance record header/order is invalid", record_index);
        return result;
      }
      const ManifestInstance &manifest_instance =
          parsed_manifest.instances[record_index];
      if (source_id != manifest_instance.source_id) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "native.package.manifest.instances",
            "manifest instance ID does not match record header", record_index);
        return result;
      }
      previous_object_id = source_id;
      Reader payload_reader(nullptr, 0U);
      if (!records.Take(static_cast<std::size_t>(payload_size),
                        payload_reader)) {
        result.validation = Failure(ValidationCode::SIZE_MISMATCH,
                                    "native.package.instances",
                                    "static instance payload is truncated",
                                    record_index);
        return result;
      }
      GraphicsSceneStaticMeshInput instance;
      ValidationResult validation =
          DecodeInstance(payload_reader, source_id, instance);
      if (!validation) {
        validation.element_index = record_index;
        result.validation = std::move(validation);
        return result;
      }
      candidate->static_meshes.push_back(std::move(instance));
    }
    if (!records.empty()) {
      result.validation = Failure(ValidationCode::SIZE_MISMATCH,
                                  "native.package",
                                  "package contains trailing bytes");
      return result;
    }
    result.validation = ValidateDependencies(*candidate);
    if (!result.validation) {
      return result;
    }
    result.validation = ValidateManifestBinding(*candidate, parsed_manifest);
    if (!result.validation) {
      return result;
    }
    result.package = std::move(candidate);
    return result;
  } catch (const std::bad_alloc &) {
    SetEmergencyFailure(result, ValidationCode::VALUE_OUT_OF_RANGE);
    return result;
  } catch (...) {
    SetEmergencyFailure(result, ValidationCode::UNSUPPORTED_FEATURE);
    return result;
  }
}

} // namespace RoR::Render
