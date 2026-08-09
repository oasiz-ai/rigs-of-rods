/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"

#include <OgreBuildSettings.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "legacy asset capture is pinned to OGRE 14.5.2");
static_assert(std::is_same<Ogre::Real, float>::value,
              "legacy asset capture requires OGRE binary32 Real");
static_assert(
    std::is_nothrow_move_assignable<
        RoR::Render::Ogre14LegacyNativeMaterialCapture>::value,
    "native capture publication must preserve transactional rollback");

namespace RoR::Render {
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
namespace {

thread_local IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector
    *g_native_material_declaration_digest_fault_injector = nullptr;

} // namespace

namespace Testing {

void SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
    IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector *fault_injector)
    noexcept {
  g_native_material_declaration_digest_fault_injector = fault_injector;
}

} // namespace Testing
#endif

namespace {

bool CheckedMultiplyU64(std::uint64_t lhs, std::uint64_t rhs,
                        std::uint64_t &result) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

bool CheckedAddU64(std::uint64_t lhs, std::uint64_t rhs,
                   std::uint64_t &result) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

constexpr std::array<std::uint8_t, 8U> kNativeMaterialDeclarationMagic{{
    'R', 'O', 'R', 'N', 'M', 'D', '1', '\0',
}};

class NativeMaterialDeclarationWriter final {
public:
  NativeMaterialDeclarationWriter() {
    bytes_.reserve(1024U);
    (void)AppendBytes(kNativeMaterialDeclarationMagic.data(),
                      kNativeMaterialDeclarationMagic.size());
    (void)AppendU32(
        kOgre14LegacyNativeMaterialDeclarationSerializationVersion);
  }

  [[nodiscard]] bool AppendU8(std::uint8_t value) {
    return AppendBytes(&value, 1U);
  }

  [[nodiscard]] bool AppendBool(bool value) {
    return AppendU8(value ? 1U : 0U);
  }

  [[nodiscard]] bool AppendU16(std::uint16_t value) {
    std::array<std::uint8_t, 2U> encoded{};
    for (std::uint32_t byte = 0U; byte < encoded.size(); ++byte) {
      encoded[byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
    return AppendBytes(encoded.data(), encoded.size());
  }

  [[nodiscard]] bool AppendU32(std::uint32_t value) {
    std::array<std::uint8_t, 4U> encoded{};
    for (std::uint32_t byte = 0U; byte < encoded.size(); ++byte) {
      encoded[byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
    return AppendBytes(encoded.data(), encoded.size());
  }

  [[nodiscard]] bool AppendFloat(float value) {
    if (!std::isfinite(value)) {
      Fail(ValidationCode::NON_FINITE_VALUE,
           "native_material_declaration.float",
           "native declaration contains a non-finite float32 value");
      return false;
    }
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "native declaration requires binary32 float");
    static_assert(std::numeric_limits<float>::is_iec559,
                  "native declaration requires IEEE-754 float32");
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AppendU32(bits);
  }

  [[nodiscard]] bool AppendString(const std::string &value) {
    if (value.size() >
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "native_material_declaration.string",
           "native declaration string exceeds the canonical uint32 range");
      return false;
    }
    if (!AppendU32(static_cast<std::uint32_t>(value.size()))) {
      return false;
    }
    return AppendBytes(reinterpret_cast<const std::uint8_t *>(value.data()),
                       value.size());
  }

  [[nodiscard]] bool ok() const noexcept { return error_.ok(); }
  [[nodiscard]] const ValidationResult &error() const noexcept { return error_; }
  [[nodiscard]] const std::vector<std::uint8_t> &bytes() const noexcept {
    return bytes_;
  }

private:
  [[nodiscard]] bool AppendBytes(const std::uint8_t *bytes,
                                 std::size_t size) {
    if (!error_) {
      return false;
    }
    if ((bytes == nullptr && size != 0U) ||
        bytes_.size() >
            kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes ||
        size > kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes -
                   bytes_.size()) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "native_material_declaration.canonical_bytes",
           "native declaration exceeds the canonical byte cap");
      return false;
    }
    if (size == 0U) {
      return true;
    }
    bytes_.insert(bytes_.end(), bytes, bytes + size);
    return true;
  }

  void Fail(ValidationCode code, const char *field, const char *detail) {
    if (error_) {
      error_ = ValidationResult::Failure(code, field, detail);
    }
  }

  std::vector<std::uint8_t> bytes_;
  ValidationResult error_;
};

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count) noexcept {
  return (value >> count) | (value << (32U - count));
}

Ogre14LegacyNativeMaterialDeclarationSha256
Sha256(const std::uint8_t *bytes, std::size_t size) noexcept {
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

  const auto transform = [&state](const std::uint8_t *block) noexcept {
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
    transform(bytes + offset);
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
  transform(tail.data());
  if (padded == 128U) {
    transform(tail.data() + 64U);
  }
  Ogre14LegacyNativeMaterialDeclarationSha256 digest{};
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

Float3 ToFloat3(const Ogre::ColourValue &color) noexcept {
  return {color.r, color.g, color.b};
}

Float4 ToFloat4(const Ogre::ColourValue &color) noexcept {
  return {color.r, color.g, color.b, color.a};
}

ValidationResult SourceRevision(std::size_t state_count,
                                std::uint64_t &revision) {
  const std::uint64_t narrowed_state_count =
      static_cast<std::uint64_t>(state_count);
  if (static_cast<std::size_t>(narrowed_state_count) != state_count ||
      narrowed_state_count == (std::numeric_limits<std::uint64_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "native.state_count",
        "OGRE resource state count cannot advance to a nonzero revision");
  }
  revision = narrowed_state_count + 1U;
  return ValidationResult::Success();
}

Ogre14LegacyBlendFactor ToBlendFactor(Ogre::SceneBlendFactor factor) {
  switch (factor) {
  case Ogre::SBF_ONE:
    return Ogre14LegacyBlendFactor::ONE;
  case Ogre::SBF_ZERO:
    return Ogre14LegacyBlendFactor::ZERO;
  case Ogre::SBF_DEST_COLOUR:
    return Ogre14LegacyBlendFactor::DESTINATION_COLOR;
  case Ogre::SBF_SOURCE_COLOUR:
    return Ogre14LegacyBlendFactor::SOURCE_COLOR;
  case Ogre::SBF_ONE_MINUS_DEST_COLOUR:
    return Ogre14LegacyBlendFactor::ONE_MINUS_DESTINATION_COLOR;
  case Ogre::SBF_ONE_MINUS_SOURCE_COLOUR:
    return Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_COLOR;
  case Ogre::SBF_DEST_ALPHA:
    return Ogre14LegacyBlendFactor::DESTINATION_ALPHA;
  case Ogre::SBF_SOURCE_ALPHA:
    return Ogre14LegacyBlendFactor::SOURCE_ALPHA;
  case Ogre::SBF_ONE_MINUS_DEST_ALPHA:
    return Ogre14LegacyBlendFactor::ONE_MINUS_DESTINATION_ALPHA;
  case Ogre::SBF_ONE_MINUS_SOURCE_ALPHA:
    return Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
  }
  return static_cast<Ogre14LegacyBlendFactor>(255U);
}

Ogre14LegacyBlendOperation
ToBlendOperation(Ogre::SceneBlendOperation operation) {
  switch (operation) {
  case Ogre::SBO_ADD:
    return Ogre14LegacyBlendOperation::ADD;
  case Ogre::SBO_SUBTRACT:
    return Ogre14LegacyBlendOperation::SUBTRACT;
  case Ogre::SBO_REVERSE_SUBTRACT:
    return Ogre14LegacyBlendOperation::REVERSE_SUBTRACT;
  case Ogre::SBO_MIN:
    return Ogre14LegacyBlendOperation::MINIMUM;
  case Ogre::SBO_MAX:
    return Ogre14LegacyBlendOperation::MAXIMUM;
  }
  return static_cast<Ogre14LegacyBlendOperation>(255U);
}

Ogre14LegacyCompareOperation ToCompare(Ogre::CompareFunction operation) {
  switch (operation) {
  case Ogre::CMPF_ALWAYS_FAIL:
    return Ogre14LegacyCompareOperation::ALWAYS_FAIL;
  case Ogre::CMPF_ALWAYS_PASS:
    return Ogre14LegacyCompareOperation::ALWAYS_PASS;
  case Ogre::CMPF_LESS:
    return Ogre14LegacyCompareOperation::LESS;
  case Ogre::CMPF_LESS_EQUAL:
    return Ogre14LegacyCompareOperation::LESS_EQUAL;
  case Ogre::CMPF_EQUAL:
    return Ogre14LegacyCompareOperation::EQUAL;
  case Ogre::CMPF_NOT_EQUAL:
    return Ogre14LegacyCompareOperation::NOT_EQUAL;
  case Ogre::CMPF_GREATER_EQUAL:
    return Ogre14LegacyCompareOperation::GREATER_EQUAL;
  case Ogre::CMPF_GREATER:
    return Ogre14LegacyCompareOperation::GREATER;
  }
  return static_cast<Ogre14LegacyCompareOperation>(255U);
}

Ogre14LegacyCullMode ToCull(Ogre::CullingMode cull) {
  switch (cull) {
  case Ogre::CULL_NONE:
    return Ogre14LegacyCullMode::NONE;
  case Ogre::CULL_CLOCKWISE:
    return Ogre14LegacyCullMode::CLOCKWISE;
  case Ogre::CULL_ANTICLOCKWISE:
    return Ogre14LegacyCullMode::ANTICLOCKWISE;
  }
  return static_cast<Ogre14LegacyCullMode>(255U);
}

Ogre14LegacyManualCullMode ToManualCull(Ogre::ManualCullingMode cull) {
  switch (cull) {
  case Ogre::MANUAL_CULL_NONE:
    return Ogre14LegacyManualCullMode::NONE;
  case Ogre::MANUAL_CULL_BACK:
    return Ogre14LegacyManualCullMode::BACK;
  case Ogre::MANUAL_CULL_FRONT:
    return Ogre14LegacyManualCullMode::FRONT;
  }
  return static_cast<Ogre14LegacyManualCullMode>(255U);
}

Ogre14LegacyFilter ToFilter(Ogre::FilterOptions filter) {
  switch (filter) {
  case Ogre::FO_NONE:
    return Ogre14LegacyFilter::NONE;
  case Ogre::FO_POINT:
    return Ogre14LegacyFilter::POINT;
  case Ogre::FO_LINEAR:
    return Ogre14LegacyFilter::LINEAR;
  case Ogre::FO_ANISOTROPIC:
    return Ogre14LegacyFilter::ANISOTROPIC;
  }
  return static_cast<Ogre14LegacyFilter>(255U);
}

Ogre14LegacyAddressMode ToAddress(Ogre::TextureAddressingMode address) {
  switch (address) {
  case Ogre::TAM_WRAP:
    return Ogre14LegacyAddressMode::WRAP;
  case Ogre::TAM_MIRROR:
    return Ogre14LegacyAddressMode::MIRROR;
  case Ogre::TAM_CLAMP:
    return Ogre14LegacyAddressMode::CLAMP;
  case Ogre::TAM_BORDER:
    return Ogre14LegacyAddressMode::BORDER;
  }
  return static_cast<Ogre14LegacyAddressMode>(255U);
}

Ogre14LegacyTextureType ToTextureType(Ogre::TextureType type) {
  switch (type) {
  case Ogre::TEX_TYPE_1D:
    return Ogre14LegacyTextureType::TEXTURE_1D;
  case Ogre::TEX_TYPE_2D:
    return Ogre14LegacyTextureType::TEXTURE_2D;
  case Ogre::TEX_TYPE_3D:
    return Ogre14LegacyTextureType::TEXTURE_3D;
  case Ogre::TEX_TYPE_CUBE_MAP:
    return Ogre14LegacyTextureType::TEXTURE_CUBE;
  case Ogre::TEX_TYPE_2D_ARRAY:
    return Ogre14LegacyTextureType::TEXTURE_2D_ARRAY;
  case Ogre::TEX_TYPE_2D_MULTISAMPLE:
    return Ogre14LegacyTextureType::TEXTURE_2D_MULTISAMPLE;
  case Ogre::TEX_TYPE_EXTERNAL_OES:
    return Ogre14LegacyTextureType::TEXTURE_EXTERNAL;
  }
  return static_cast<Ogre14LegacyTextureType>(255U);
}

bool IsIdentity(const Ogre::Matrix4 &matrix) noexcept {
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      const float expected = row == column ? 1.0F : 0.0F;
      if (matrix[row][column] != expected) {
        return false;
      }
    }
  }
  return true;
}

bool IsCanonicalModulate(const Ogre::LayerBlendModeEx &blend,
                         Ogre::LayerBlendType expected_type) noexcept {
  return blend.blendType == expected_type &&
         blend.operation == Ogre::LBX_MODULATE &&
         blend.source1 == Ogre::LBS_TEXTURE &&
         blend.source2 == Ogre::LBS_CURRENT;
}

ValidationResult ValidateNativePixelFormat(Ogre::PixelFormat format) {
  int channels[4] = {0, 0, 0, 0};
  Ogre::PixelUtil::getBitDepths(format, channels);
  const bool exact_rgb8 = channels[0] == 8 && channels[1] == 8 &&
                          channels[2] == 8 &&
                          (channels[3] == 0 || channels[3] == 8);
  const std::size_t component_count =
      Ogre::PixelUtil::getComponentCount(format);
  const std::size_t element_bytes = Ogre::PixelUtil::getNumElemBytes(format);
  if (!Ogre::PixelUtil::isAccessible(format) ||
      Ogre::PixelUtil::isCompressed(format) ||
      Ogre::PixelUtil::isFloatingPoint(format) ||
      Ogre::PixelUtil::isInteger(format) || Ogre::PixelUtil::isDepth(format) ||
      Ogre::PixelUtil::isLuminance(format) ||
      Ogre::PixelUtil::getComponentType(format) != Ogre::PCT_BYTE ||
      !exact_rgb8 || (component_count != 3U && component_count != 4U) ||
      (element_bytes != 3U && element_bytes != 4U)) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.pixel_format",
        "native format is not an accessible uncompressed normalized RGB8 or "
        "RGBA8 format");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateNativeTechniqueAndPassState(const Ogre::Material &material,
                                    const Ogre::Technique &technique,
                                    const Ogre::Pass &pass) {
  if (technique.getSchemeName() != Ogre::MaterialManager::DEFAULT_SCHEME_NAME ||
      technique.getLodIndex() != 0U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.technique_state",
        "v1 requires one default-scheme LOD-zero technique");
  }
  if (!technique.getShadowCasterMaterialName().empty() ||
      !technique.getShadowReceiverMaterialName().empty() ||
      technique.getShadowCasterMaterial() ||
      technique.getShadowReceiverMaterial()) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.shadow_materials",
        "custom shadow materials are not representable in v1");
  }
  if (!material.getReceiveShadows() || material.getTransparencyCastsShadows()) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.shadow_policy",
        "v1 requires canonical shadow receiving and casting policy");
  }
  if (!technique.getGPUVendorRules().empty() ||
      !technique.getGPUDeviceNameRules().empty()) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.technique_hardware_rules",
        "hardware-vendor and device-specific technique rules are not portable");
  }
  if (pass.getVertexColourTracking() != Ogre::TVC_NONE) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.vertex_colour_tracking",
        "legacy vertex-colour material tracking is not representable in v1");
  }
  if (pass.getShadingMode() != Ogre::SO_GOURAUD ||
      pass.getMaxSimultaneousLights() != OGRE_MAX_SIMULTANEOUS_LIGHTS ||
      pass.getStartLight() != 0U || pass.getLightMask() != 0xFFFFFFFFU ||
      pass.getIteratePerLight() || pass.getRunOnlyForOneLightType() ||
      pass.getOnlyLightType() != Ogre::Light::LT_POINT ||
      pass.getLightCountPerIteration() != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.lighting_controls",
        "legacy shading, light selection, or iteration is outside the "
        "canonical v1 state");
  }
  if (pass.getFogOverride() || !pass.getPolygonModeOverrideable() ||
      pass.getLightScissoringEnabled() || pass.getLightClipPlanesEnabled() ||
      pass.getIlluminationStage() != Ogre::IS_UNKNOWN) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.scene_overrides",
        "fog, polygon override, light clipping, or illumination staging is not "
        "representable in v1");
  }
  if (!pass.getTransparentSortingEnabled() ||
      pass.getTransparentSortingForced()) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.transparent_sorting",
        "v1 requires canonical conditional transparent sorting");
  }
  if (pass.getLineWidth() != 1.0F || pass.getPointSize() != 1.0F ||
      pass.getPointSpritesEnabled() || pass.isPointAttenuationEnabled() ||
      pass.getPointAttenuationConstant() != 1.0F ||
      pass.getPointAttenuationLinear() != 0.0F ||
      pass.getPointAttenuationQuadratic() != 0.0F ||
      pass.getPointMinSize() != 0.0F || pass.getPointMaxSize() != 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.line_point_raster",
        "nondefault legacy line or point raster state is not representable");
  }
  return ValidationResult::Success();
}

ValidationResult CaptureTexture(Ogre::Texture &native,
                                Ogre14LegacyTextureColorRole color_role,
                                std::uint64_t maximum_decoded_bytes,
                                Ogre14LegacyTextureInput &texture) {
  Ogre14LegacyTextureInput candidate;
  candidate.key.exact_resource_group = native.getGroup();
  candidate.key.exact_name = native.getName();
  ValidationResult validation =
      SourceRevision(native.getStateCount(), candidate.source_revision);
  if (!validation) {
    return validation;
  }
  candidate.type = ToTextureType(native.getTextureType());
  candidate.pixel_encoding = Ogre14LegacyPixelEncoding::RGBA8_BYTES;
  candidate.color_role = color_role;
  candidate.hardware_gamma_enabled = native.isHardwareGammaEnabled();
  candidate.compressed = Ogre::PixelUtil::isCompressed(native.getFormat());
  candidate.render_target = (native.getUsage() & Ogre::TU_RENDERTARGET) != 0;
  candidate.generated = native.isManuallyLoaded();
  candidate.procedural = native.isManuallyLoaded();
  candidate.width = native.getWidth();
  candidate.height = native.getHeight();

  validation = ValidateNativePixelFormat(native.getFormat());
  if (!validation) {
    return validation;
  }
  if (candidate.width == 0U || candidate.height == 0U ||
      candidate.width > kMaximumTextureResourceDimension ||
      candidate.height > kMaximumTextureResourceDimension) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "texture.dimensions",
        "native texture dimensions are outside the portable range");
  }
  if (candidate.type != Ogre14LegacyTextureType::TEXTURE_2D ||
      native.getDepth() != 1U || native.getNumFaces() != 1U ||
      candidate.render_target || candidate.generated) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.source_kind",
        "native texture is not an ordinary loaded 2D sampled resource");
  }

  std::uint32_t maximum_mip_count = 1U;
  for (std::uint32_t width = candidate.width, height = candidate.height;
       width > 1U || height > 1U; ++maximum_mip_count) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
  }
  if (native.getNumMipmaps() >= maximum_mip_count) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "texture.mip_levels",
        "native texture exposes more mip levels than its extent permits");
  }
  const std::uint32_t mip_count = native.getNumMipmaps() + 1U;
  candidate.mip_levels.reserve(mip_count);
  std::uint64_t decoded_texture_bytes = 0U;
  for (std::uint32_t level = 0U; level < mip_count; ++level) {
    const Ogre::HardwarePixelBufferPtr &buffer = native.getBuffer(0U, level);
    if (!buffer || buffer->getDepth() != 1U) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "texture.pixel_buffer",
          "native texture mip has no readable 2D pixel buffer", level);
    }
    validation = ValidateNativePixelFormat(buffer->getFormat());
    if (!validation) {
      validation.element_index = level;
      return validation;
    }
    Ogre14LegacyTextureMipInput mip;
    mip.width = buffer->getWidth();
    mip.height = buffer->getHeight();
    if (mip.width == 0U || mip.height == 0U ||
        mip.width > kMaximumTextureResourceDimension ||
        mip.height > kMaximumTextureResourceDimension) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "texture.pixel_buffer",
          "native mip dimensions are outside the portable range", level);
    }
    std::uint64_t row_bytes = 0U;
    std::uint64_t slice_bytes = 0U;
    std::uint64_t next_decoded_texture_bytes = 0U;
    if (!CheckedMultiplyU64(mip.width, 4U, row_bytes) ||
        !CheckedMultiplyU64(row_bytes, mip.height, slice_bytes) ||
        !CheckedAddU64(decoded_texture_bytes, slice_bytes,
                       next_decoded_texture_bytes)) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "texture.pixel_buffer",
          "canonical native mip byte count overflows", level);
    }
    if (next_decoded_texture_bytes > maximum_decoded_bytes) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.decoded_bytes",
          "native texture exceeds the configured decoded-byte cap", level);
    }
    if (slice_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "texture.pixel_buffer",
          "canonical mip allocation exceeds the host address range", level);
    }
    mip.row_pitch_bytes = row_bytes;
    mip.slice_pitch_bytes = slice_bytes;
    mip.bytes.resize(static_cast<std::size_t>(slice_bytes));
    Ogre::PixelBox destination(mip.width, mip.height, 1U, Ogre::PF_BYTE_RGBA,
                               mip.bytes.data());
    buffer->blitToMemory(destination);
    candidate.mip_levels.push_back(std::move(mip));
    decoded_texture_bytes = next_decoded_texture_bytes;
  }
  validation = ValidateOgre14LegacyTextureInput(candidate);
  if (!validation) {
    return validation;
  }
  texture = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult CapturePipeline(const Ogre::Pass &pass,
                                 Ogre14LegacyPipelineStateInput &state) {
  state.source_color = ToBlendFactor(pass.getSourceBlendFactor());
  state.destination_color = ToBlendFactor(pass.getDestBlendFactor());
  state.source_alpha = ToBlendFactor(pass.getSourceBlendFactorAlpha());
  state.destination_alpha = ToBlendFactor(pass.getDestBlendFactorAlpha());
  state.color_operation = ToBlendOperation(pass.getSceneBlendingOperation());
  state.alpha_operation =
      ToBlendOperation(pass.getSceneBlendingOperationAlpha());
  bool red = false;
  bool green = false;
  bool blue = false;
  bool alpha = false;
  pass.getColourWriteEnabled(red, green, blue, alpha);
  state.color_write_mask =
      static_cast<std::uint8_t>((red ? 1U : 0U) | (green ? 2U : 0U) |
                                (blue ? 4U : 0U) | (alpha ? 8U : 0U));
  state.depth_check_enabled = pass.getDepthCheckEnabled();
  state.depth_write_enabled = pass.getDepthWriteEnabled();
  state.depth_compare = ToCompare(pass.getDepthFunction());
  state.constant_depth_bias = pass.getDepthBiasConstant();
  state.slope_scale_depth_bias = pass.getDepthBiasSlopeScale();
  state.iteration_depth_bias = pass.getIterationDepthBias();
  state.cull = ToCull(pass.getCullingMode());
  state.manual_cull = ToManualCull(pass.getManualCullingMode());
  state.alpha_reject = ToCompare(pass.getAlphaRejectFunction());
  state.alpha_reject_value = pass.getAlphaRejectValue();
  state.alpha_to_coverage = pass.isAlphaToCoverageEnabled();
  state.solid_fill = pass.getPolygonMode() == Ogre::PM_SOLID;
  const std::size_t iterations = pass.getPassIterationCount();
  if (iterations > (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.pipeline.pass_iteration_count",
        "native pass iteration count exceeds the portable integer range");
  }
  state.pass_iteration_count = static_cast<std::uint32_t>(iterations);
  return ValidationResult::Success();
}

ValidationResult CaptureNativeSamplerState(
    const Ogre::TextureUnitState &native, std::uint64_t source_revision,
    std::uint32_t mip_level_count, Ogre14LegacySamplerInput &sampler) {
  if (!native.getSampler()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "material.texture_unit.sampler",
        "native texture unit has no sampler object");
  }
  if (mip_level_count == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS,
        "material.texture_unit.mip_level_count",
        "native texture unit cannot derive sampler LOD from an empty mip inventory");
  }
  Ogre14LegacySamplerInput candidate;
  candidate.source_revision = source_revision;
  const Ogre::Sampler::UVWAddressingMode &address =
      native.getTextureAddressingMode();
  candidate.address_u = ToAddress(address.u);
  candidate.address_v = ToAddress(address.v);
  candidate.address_w = ToAddress(address.w);
  candidate.minification = ToFilter(native.getTextureFiltering(Ogre::FT_MIN));
  candidate.magnification =
      ToFilter(native.getTextureFiltering(Ogre::FT_MAG));
  candidate.mip = ToFilter(native.getTextureFiltering(Ogre::FT_MIP));
  candidate.mip_lod_bias = native.getTextureMipmapBias();
  candidate.minimum_lod = 0.0F;
  const unsigned int maximum_anisotropy = native.getTextureAnisotropy();
  if (static_cast<std::uint64_t>(maximum_anisotropy) >
      (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.sampler.maximum_anisotropy",
        "native anisotropy exceeds the portable integer range");
  }
  candidate.maximum_anisotropy =
      static_cast<std::uint32_t>(maximum_anisotropy);
  candidate.compare_enabled = native.getTextureCompareEnabled();
  candidate.compare_operation = ToCompare(native.getTextureCompareFunction());
  candidate.border_color = ToFloat4(native.getTextureBorderColour());
  candidate.maximum_lod =
      candidate.mip == Ogre14LegacyFilter::NONE
          ? 0.0F
          : static_cast<float>(mip_level_count - 1U);
  sampler = candidate;
  return ValidationResult::Success();
}

template <typename Enum>
bool AppendEnum(NativeMaterialDeclarationWriter &writer, Enum value) {
  const auto numeric = static_cast<std::int64_t>(value);
  return numeric >= 0 &&
         numeric <= (std::numeric_limits<std::uint8_t>::max)() &&
         writer.AppendU8(static_cast<std::uint8_t>(numeric));
}

bool AppendFloat3(NativeMaterialDeclarationWriter &writer,
                  const Float3 &value) {
  return writer.AppendFloat(value.x) && writer.AppendFloat(value.y) &&
         writer.AppendFloat(value.z);
}

bool AppendFloat4(NativeMaterialDeclarationWriter &writer,
                  const Float4 &value) {
  return writer.AppendFloat(value.x) && writer.AppendFloat(value.y) &&
         writer.AppendFloat(value.z) && writer.AppendFloat(value.w);
}

bool AppendPipeline(NativeMaterialDeclarationWriter &writer,
                    const Ogre14LegacyPipelineStateInput &pipeline) {
  return AppendEnum(writer, pipeline.source_color) &&
         AppendEnum(writer, pipeline.destination_color) &&
         AppendEnum(writer, pipeline.source_alpha) &&
         AppendEnum(writer, pipeline.destination_alpha) &&
         AppendEnum(writer, pipeline.color_operation) &&
         AppendEnum(writer, pipeline.alpha_operation) &&
         writer.AppendU8(pipeline.color_write_mask) &&
         writer.AppendBool(pipeline.depth_check_enabled) &&
         writer.AppendBool(pipeline.depth_write_enabled) &&
         AppendEnum(writer, pipeline.depth_compare) &&
         writer.AppendFloat(pipeline.constant_depth_bias) &&
         writer.AppendFloat(pipeline.slope_scale_depth_bias) &&
         writer.AppendFloat(pipeline.iteration_depth_bias) &&
         AppendEnum(writer, pipeline.cull) &&
         AppendEnum(writer, pipeline.manual_cull) &&
         AppendEnum(writer, pipeline.alpha_reject) &&
         writer.AppendU8(pipeline.alpha_reject_value) &&
         writer.AppendBool(pipeline.alpha_to_coverage) &&
         writer.AppendBool(pipeline.solid_fill) &&
         writer.AppendU32(pipeline.pass_iteration_count);
}

bool AppendSampler(NativeMaterialDeclarationWriter &writer,
                   const Ogre14LegacySamplerInput &sampler) {
  return AppendEnum(writer, sampler.minification) &&
         AppendEnum(writer, sampler.magnification) &&
         AppendEnum(writer, sampler.mip) &&
         AppendEnum(writer, sampler.address_u) &&
         AppendEnum(writer, sampler.address_v) &&
         AppendEnum(writer, sampler.address_w) &&
         writer.AppendFloat(sampler.mip_lod_bias) &&
         writer.AppendFloat(sampler.minimum_lod) &&
         writer.AppendFloat(sampler.maximum_lod) &&
         writer.AppendU32(sampler.maximum_anisotropy) &&
         writer.AppendBool(sampler.compare_enabled) &&
         AppendEnum(writer, sampler.compare_operation) &&
         AppendFloat4(writer, sampler.border_color);
}

bool AppendLayerBlendMode(NativeMaterialDeclarationWriter &writer,
                          const Ogre::LayerBlendModeEx &blend) {
  return AppendEnum(writer, blend.blendType) &&
         AppendEnum(writer, blend.operation) &&
         AppendEnum(writer, blend.source1) && AppendEnum(writer, blend.source2) &&
         writer.AppendBool(blend.source1 == Ogre::LBS_MANUAL) &&
         writer.AppendBool(blend.source2 == Ogre::LBS_MANUAL) &&
         writer.AppendBool(blend.operation == Ogre::LBX_BLEND_MANUAL);
}

#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
void BeforeNativeMaterialDeclarationDigestStage(
    Ogre14LegacyNativeMaterialDeclarationDigestStage stage) {
  if (g_native_material_declaration_digest_fault_injector != nullptr) {
    g_native_material_declaration_digest_fault_injector
        ->BeforeNativeMaterialDeclarationDigestStage(stage);
  }
}
#endif

ValidationResult BuildNativeMaterialDeclarationDigest(
    const Ogre::Material &material, const Ogre::Technique &technique,
    const Ogre::Pass &pass,
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
    bool invoke_fault_stages,
#endif
    std::uint32_t &serialization_version,
    Ogre14LegacyNativeMaterialDeclarationSha256 &sha256) {
  const std::size_t technique_count = material.getNumTechniques();
  const std::size_t pass_count = technique.getNumPasses();
  const std::size_t texture_unit_count = pass.getNumTextureUnitStates();
  const std::size_t gpu_vendor_rule_count =
      technique.getGPUVendorRules().size();
  const std::size_t gpu_device_rule_count =
      technique.getGPUDeviceNameRules().size();
  if (technique_count != 1U || pass_count != 1U || texture_unit_count > 1U ||
      material.getTechnique(0U) != &technique || technique.getPass(0U) != &pass ||
      technique_count >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)()) ||
      pass_count >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)()) ||
      texture_unit_count >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)()) ||
      gpu_vendor_rule_count >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)()) ||
      gpu_device_rule_count >
          static_cast<std::size_t>(
              (std::numeric_limits<std::uint32_t>::max)())) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH,
        "native_material_declaration.material_structure",
        "native material topology is not the complete canonical v1 graph");
  }
  Ogre14LegacyPipelineStateInput pipeline;
  ValidationResult validation = CapturePipeline(pass, pipeline);
  if (!validation) {
    return validation;
  }
  if (!std::isfinite(pipeline.constant_depth_bias) ||
      !std::isfinite(pipeline.slope_scale_depth_bias) ||
      !std::isfinite(pipeline.iteration_depth_bias)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "material.pipeline.depth_bias",
                                     "depth bias must be finite");
  }
  NativeMaterialDeclarationWriter writer;
  if (!writer.ok() || !writer.AppendString(material.getGroup()) ||
      !writer.AppendString(material.getName()) ||
      !writer.AppendBool(material.getReceiveShadows()) ||
      !writer.AppendBool(material.getTransparencyCastsShadows()) ||
      !writer.AppendU32(static_cast<std::uint32_t>(technique_count)) ||
      !writer.AppendU32(0U) ||
      !writer.AppendString(technique.getSchemeName()) ||
      !writer.AppendU16(technique.getLodIndex()) ||
      !writer.AppendBool(technique.isSupported()) ||
      !writer.AppendU32(static_cast<std::uint32_t>(gpu_vendor_rule_count)) ||
      !writer.AppendU32(static_cast<std::uint32_t>(gpu_device_rule_count)) ||
      !writer.AppendString(technique.getShadowCasterMaterialName()) ||
      !writer.AppendString(technique.getShadowReceiverMaterialName()) ||
      !writer.AppendBool(technique.getShadowCasterMaterial() != nullptr) ||
      !writer.AppendBool(technique.getShadowReceiverMaterial() != nullptr) ||
      !writer.AppendU32(static_cast<std::uint32_t>(pass_count))) {
    return writer.ok()
               ? ValidationResult::Failure(
                     ValidationCode::VALUE_OUT_OF_RANGE,
                     "native_material_declaration.material_structure",
                     "native material identity or technique state is outside the canonical format")
               : writer.error();
  }
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
  if (invoke_fault_stages) {
    BeforeNativeMaterialDeclarationDigestStage(
        Ogre14LegacyNativeMaterialDeclarationDigestStage::
            AFTER_MATERIAL_IDENTITY);
  }
#endif

  if (!writer.AppendU32(0U) ||
      !writer.AppendBool(pass.hasVertexProgram()) ||
      !writer.AppendBool(pass.hasFragmentProgram()) ||
      !writer.AppendBool(pass.hasGeometryProgram()) ||
      !writer.AppendBool(pass.hasTessellationHullProgram()) ||
      !writer.AppendBool(pass.hasTessellationDomainProgram()) ||
      !writer.AppendBool(pass.hasComputeProgram()) ||
      !writer.AppendBool(pass.getLightingEnabled()) ||
      !AppendFloat4(writer, ToFloat4(pass.getDiffuse())) ||
      !AppendFloat3(writer, ToFloat3(pass.getAmbient())) ||
      !AppendFloat3(writer, ToFloat3(pass.getSpecular())) ||
      !AppendFloat3(writer, ToFloat3(pass.getEmissive())) ||
      !writer.AppendFloat(pass.getShininess()) ||
      !AppendPipeline(writer, pipeline) ||
      !AppendEnum(writer, pass.getVertexColourTracking()) ||
      !AppendEnum(writer, pass.getShadingMode()) ||
      !writer.AppendU16(pass.getMaxSimultaneousLights()) ||
      !writer.AppendU16(pass.getStartLight()) ||
      !writer.AppendU32(pass.getLightMask()) ||
      !writer.AppendBool(pass.getIteratePerLight()) ||
      !writer.AppendBool(pass.getRunOnlyForOneLightType()) ||
      !AppendEnum(writer, pass.getOnlyLightType()) ||
      !writer.AppendU16(pass.getLightCountPerIteration()) ||
      !writer.AppendBool(pass.getFogOverride()) ||
      !writer.AppendBool(pass.getPolygonModeOverrideable()) ||
      !writer.AppendBool(pass.getLightScissoringEnabled()) ||
      !writer.AppendBool(pass.getLightClipPlanesEnabled()) ||
      !AppendEnum(writer, pass.getIlluminationStage()) ||
      !writer.AppendBool(pass.getTransparentSortingEnabled()) ||
      !writer.AppendBool(pass.getTransparentSortingForced()) ||
      !writer.AppendFloat(pass.getLineWidth()) ||
      !writer.AppendFloat(pass.getPointSize()) ||
      !writer.AppendBool(pass.getPointSpritesEnabled()) ||
      !writer.AppendBool(pass.isPointAttenuationEnabled()) ||
      !writer.AppendFloat(pass.getPointAttenuationConstant()) ||
      !writer.AppendFloat(pass.getPointAttenuationLinear()) ||
      !writer.AppendFloat(pass.getPointAttenuationQuadratic()) ||
      !writer.AppendFloat(pass.getPointMinSize()) ||
      !writer.AppendFloat(pass.getPointMaxSize()) ||
      !writer.AppendU32(static_cast<std::uint32_t>(texture_unit_count))) {
    return writer.ok()
               ? ValidationResult::Failure(
                     ValidationCode::VALUE_OUT_OF_RANGE,
                     "native_material_declaration.pass_state",
                     "native pass state cannot be represented canonically")
               : writer.error();
  }
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
  if (invoke_fault_stages) {
    BeforeNativeMaterialDeclarationDigestStage(
        Ogre14LegacyNativeMaterialDeclarationDigestStage::AFTER_PASS_STATE);
  }
#endif

  if (texture_unit_count == 1U) {
    const Ogre::TextureUnitState *native_unit = pass.getTextureUnitState(0U);
    if (native_unit == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "native_material_declaration.texture_unit",
          "native texture unit disappeared before declaration serialization");
    }
    if (!native_unit->getSampler()) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "material.texture_unit.sampler",
          "native texture unit has no sampler object");
    }
    const unsigned int texture_coordinate_set = native_unit->getTextureCoordSet();
    if (texture_coordinate_set > 1U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material.texture_unit.texture_coordinate_set",
          "native texture coordinate set exceeds the portable UV range");
    }
    if (native_unit->getUnorderedAccessMipLevel() != -1) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "material.texture_unit.unordered_access",
          "unordered-access texture bindings are not representable in v1");
    }
    if (!native_unit->_getTexturePtr()) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "native_material_declaration.texture_unit",
          "native texture disappeared before declaration serialization");
    }
    const Ogre::TexturePtr &native_texture = native_unit->_getTexturePtr();
    const unsigned int native_mipmaps = native_texture->getNumMipmaps();
    if (native_mipmaps ==
        (std::numeric_limits<unsigned int>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "native_material_declaration.texture_mip_count",
          "native texture mip count cannot be represented canonically");
    }
    Ogre14LegacySamplerInput sampler;
    validation = CaptureNativeSamplerState(
        *native_unit, 0U, static_cast<std::uint32_t>(native_mipmaps + 1U),
        sampler);
    if (!validation) {
      return validation;
    }
    const Ogre::Matrix4 &transform = native_unit->getTextureTransform();
    const std::size_t effect_count = native_unit->getEffects().size();
    if (effect_count > (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "native_material_declaration.texture_effects",
          "native texture effect count exceeds the canonical range");
    }
    if (!writer.AppendU32(0U) ||
        !writer.AppendString(native_unit->getName()) ||
        !writer.AppendString(native_texture->getGroup()) ||
        !writer.AppendString(native_texture->getName()) ||
        !AppendEnum(writer, native_unit->getContentType()) ||
        !AppendEnum(writer, native_unit->getTextureType()) ||
        !writer.AppendU8(static_cast<std::uint8_t>(texture_coordinate_set)) ||
        !writer.AppendU32(native_unit->getNumFrames()) ||
        !writer.AppendU32(native_unit->getCurrentFrame()) ||
        !writer.AppendBool(native_unit->getProjectiveTexturingFrustum() !=
                           nullptr) ||
        !writer.AppendU32(static_cast<std::uint32_t>(effect_count)) ||
        !writer.AppendBool(
            native_unit->getEffects().find(
                Ogre::TextureUnitState::ET_ENVIRONMENT_MAP) !=
            native_unit->getEffects().end()) ||
        !writer.AppendU32(
            static_cast<std::uint32_t>(native_unit->getUnorderedAccessMipLevel())) ||
        !writer.AppendFloat(native_unit->getGamma())) {
      return writer.ok()
                 ? ValidationResult::Failure(
                       ValidationCode::VALUE_OUT_OF_RANGE,
                       "native_material_declaration.texture_unit",
                       "native texture-unit structure is outside the canonical format")
                 : writer.error();
    }
    for (std::size_t row = 0U; row < 4U; ++row) {
      for (std::size_t column = 0U; column < 4U; ++column) {
        if (!writer.AppendFloat(transform[row][column])) {
          return writer.error();
        }
      }
    }
    if (!AppendLayerBlendMode(writer, native_unit->getColourBlendMode()) ||
        !AppendLayerBlendMode(writer, native_unit->getAlphaBlendMode()) ||
        !AppendSampler(writer, sampler) ||
        !writer.AppendBool((native_texture->getUsage() & Ogre::TU_RENDERTARGET) !=
                           0)) {
      return writer.ok()
                 ? ValidationResult::Failure(
                       ValidationCode::VALUE_OUT_OF_RANGE,
                       "native_material_declaration.texture_combine",
                       "native texture combine or sampler state cannot be represented canonically")
                 : writer.error();
    }
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
    if (invoke_fault_stages) {
      BeforeNativeMaterialDeclarationDigestStage(
          Ogre14LegacyNativeMaterialDeclarationDigestStage::AFTER_TEXTURE_UNIT);
    }
#endif
  }

#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
  if (invoke_fault_stages) {
    BeforeNativeMaterialDeclarationDigestStage(
        Ogre14LegacyNativeMaterialDeclarationDigestStage::BEFORE_DIGEST_COMMIT);
  }
#endif
  if (!writer.ok() || writer.bytes().empty() ||
      writer.bytes().size() >
          kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes) {
    return writer.ok()
               ? ValidationResult::Failure(
                     ValidationCode::EMPTY_PAYLOAD,
                     "native_material_declaration.canonical_bytes",
                     "canonical native material declaration is empty or oversized")
               : writer.error();
  }
  const Ogre14LegacyNativeMaterialDeclarationSha256 candidate_sha256 =
      Sha256(writer.bytes().data(), writer.bytes().size());
  serialization_version =
      kOgre14LegacyNativeMaterialDeclarationSerializationVersion;
  sha256 = candidate_sha256;
  return ValidationResult::Success();
}

ValidationResult
CaptureTextureUnit(const Ogre::TextureUnitState &native,
                   const Ogre14LegacyNativeMaterialDeclaration &declaration,
                   std::uint64_t material_revision,
                   Ogre14LegacyTextureUnitInput &unit,
                   Ogre14LegacyTextureInput &texture,
                   const IOgre14AuthenticatedTextureResolver *texture_resolver,
                   Ogre14AuthenticatedTextureResolution &texture_resolution) {
  if (!native.getSampler()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "material.texture_unit.sampler",
        "native texture unit has no sampler object");
  }
  Ogre14LegacyTextureUnitInput candidate;
  candidate.exact_unit_name = native.getName();
  candidate.texture_key.exact_name = native.getTextureName();
  const unsigned int texture_coordinate_set = native.getTextureCoordSet();
  if (texture_coordinate_set > 1U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.texture_unit.texture_coordinate_set",
        "native texture coordinate set exceeds the portable UV range");
  }
  candidate.texture_coordinate_set =
      static_cast<std::uint8_t>(texture_coordinate_set);
  candidate.named_content =
      native.getContentType() == Ogre::TextureUnitState::CONTENT_NAMED;
  candidate.texture_2d = native.getTextureType() == Ogre::TEX_TYPE_2D;
  const unsigned int frame_count = native.getNumFrames();
  if (frame_count != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.texture_unit.frame_count",
        "v1 requires exactly one native texture frame");
  }
  candidate.frame_count = static_cast<std::uint32_t>(frame_count);
  candidate.compositor =
      native.getContentType() == Ogre::TextureUnitState::CONTENT_COMPOSITOR;
  candidate.projective = native.getProjectiveTexturingFrustum() != nullptr;
  candidate.has_animated_or_procedural_effect = !native.getEffects().empty();
  candidate.environment_mapping =
      native.getEffects().find(Ogre::TextureUnitState::ET_ENVIRONMENT_MAP) !=
      native.getEffects().end();
  if (native.getUnorderedAccessMipLevel() != -1) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.texture_unit.unordered_access",
        "unordered-access texture bindings are not representable in v1");
  }
  candidate.identity_texture_transform =
      IsIdentity(native.getTextureTransform());
  candidate.canonical_color_modulate =
      IsCanonicalModulate(native.getColourBlendMode(), Ogre::LBT_COLOUR);
  candidate.canonical_alpha_modulate =
      IsCanonicalModulate(native.getAlphaBlendMode(), Ogre::LBT_ALPHA);

  if (native.getGamma() != 1.0F) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.color_transform",
        "non-identity legacy gamma transforms are not decoded in v1");
  }
  if (native.isBlank() || native.isTextureLoadFailing() ||
      !native._getTexturePtr()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "material.texture_unit",
        "legacy texture unit has no successfully loaded texture");
  }
  const Ogre::TexturePtr &native_texture = native._getTexturePtr();
  ValidationResult validation = CaptureTexture(
      *native_texture, declaration.texture_color_role,
      declaration.translator_configuration.maximum_decoded_bytes_per_asset,
      texture);
  if (!validation) {
    return validation;
  }
  validation = CaptureNativeSamplerState(
      native, material_revision,
      static_cast<std::uint32_t>(texture.mip_levels.size()),
      candidate.sampler);
  if (!validation) {
    return validation;
  }
  if (texture_resolver != nullptr) {
    Ogre14AuthenticatedTextureResolution resolution;
    validation = texture_resolver->ResolveAuthenticatedTexture(
        *native_texture, resolution);
    if (!validation) {
      return validation;
    }
    const std::size_t native_state_count = native_texture->getStateCount();
    const std::uint64_t loaded_state_count =
        static_cast<std::uint64_t>(native_state_count);
    if (static_cast<std::size_t>(loaded_state_count) != native_state_count ||
        !native_texture->isLoaded() ||
        !resolution.MatchesResolver(*texture_resolver) ||
        !resolution.MatchesLoadedResourceIdentity(
            reinterpret_cast<std::uintptr_t>(native_texture.get()),
            static_cast<std::uint64_t>(native_texture->getHandle()),
            native_texture->getGroup(), native_texture->getName(),
            loaded_state_count) ||
        texture.source_revision != loaded_state_count + 1U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_HANDLE,
          "texture_resolution.resolve_identity",
          "resolver did not authenticate the exact texture read back by the native extractor");
    }
    texture_resolution = std::move(resolution);
  }
  candidate.texture_key = texture.key;
  candidate.render_target = texture.render_target;
  unit = std::move(candidate);
  return ValidationResult::Success();
}

} // namespace

namespace {

ValidationResult RevalidateAuthenticatedTextureForPublication(
    const Ogre::Material &material,
    const IOgre14AuthenticatedTextureResolver &texture_resolver,
    const Ogre14LegacyNativeMaterialCapture &candidate) {
  if (candidate.textures.empty()) {
    return candidate.authenticated_texture_resolutions.empty()
               ? ValidationResult::Success()
               : ValidationResult::Failure(
                     ValidationCode::SIZE_MISMATCH,
                     "texture_resolution.alignment",
                     "untextured material unexpectedly carried a texture resolution");
  }
  if (candidate.textures.size() != 1U ||
      candidate.authenticated_texture_resolutions.size() != 1U) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "texture_resolution.alignment",
        "authenticated texture resolutions are not aligned with native textures");
  }

  try {
    std::uint64_t current_material_revision = 0U;
    ValidationResult revision =
        SourceRevision(material.getStateCount(), current_material_revision);
    if (!revision || !material.isLoaded() ||
        current_material_revision != candidate.material.source_revision ||
        material.getNumTechniques() != 1U) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "texture_resolution.material_revalidation",
          "material changed while its authenticated texture was captured");
    }
    const Ogre::Technique *current_technique = material.getTechnique(0U);
    const Ogre::Pass *current_pass =
        current_technique != nullptr &&
                current_technique->getNumPasses() == 1U
            ? current_technique->getPass(0U)
            : nullptr;
    const Ogre::TextureUnitState *current_unit =
        current_pass != nullptr &&
                current_pass->getNumTextureUnitStates() == 1U
            ? current_pass->getTextureUnitState(0U)
            : nullptr;
    if (current_unit == nullptr || current_unit->isBlank() ||
        current_unit->isTextureLoadFailing() ||
        !current_unit->_getTexturePtr()) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "texture_resolution.texture_unit_revalidation",
          "texture unit changed before authenticated capture publication");
    }
    const Ogre::TexturePtr &current_texture = current_unit->_getTexturePtr();
    const std::size_t native_state_count = current_texture->getStateCount();
    const std::uint64_t loaded_state_count =
        static_cast<std::uint64_t>(native_state_count);
    const Ogre14AuthenticatedTextureResolution &resolution =
        candidate.authenticated_texture_resolutions.front();
    if (static_cast<std::size_t>(loaded_state_count) != native_state_count ||
        loaded_state_count ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        !current_texture->isLoaded() ||
        candidate.textures.front().source_revision !=
            loaded_state_count + 1U ||
        candidate.textures.front().key.exact_resource_group !=
            current_texture->getGroup() ||
        candidate.textures.front().key.exact_name !=
            current_texture->getName() ||
        !resolution.MatchesResolver(texture_resolver) ||
        !resolution.MatchesLoadedResourceIdentity(
            reinterpret_cast<std::uintptr_t>(current_texture.get()),
            static_cast<std::uint64_t>(current_texture->getHandle()),
            current_texture->getGroup(), current_texture->getName(),
            loaded_state_count) ||
        !texture_resolver.RevalidateAuthenticatedTexture(*current_texture,
                                                         resolution)) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "texture_resolution.final_revalidation",
          "authenticated texture identity or registry authority changed before publication");
    }
    return ValidationResult::Success();
  } catch (const Ogre::Exception &) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE,
        "texture_resolution.revalidation_ogre_exception",
        "OGRE failed while reacquiring the exact texture before publication");
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD,
        "texture_resolution.revalidation_allocation",
        "allocation failed before authenticated capture publication");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "texture_resolution.revalidation_exception",
        "unexpected exception before authenticated capture publication");
  }
}

ValidationResult CaptureOgre14LegacyNativeMaterialCandidate(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    const IOgre14AuthenticatedTextureResolver *texture_resolver,
    Ogre14LegacyNativeMaterialCapture &candidate_output,
    Ogre14LegacyNativeMaterialCaptureSha256 &capture_sha256_output) {
  if (declaration.version != kOgre14LegacyNativeAssetExtractorVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "declaration.version",
        "unsupported native material declaration version");
  }
  ValidationResult configuration_validation =
      ValidateOgre14LegacyAssetTranslatorConfiguration(
          declaration.translator_configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (!material.isLoaded()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "material.loaded",
        "native material must be completely loaded before capture");
  }

  try {
    Ogre14LegacyNativeMaterialCapture candidate;
    candidate.material.key.exact_resource_group = material.getGroup();
    candidate.material.key.exact_name = material.getName();
    ValidationResult validation = SourceRevision(
        material.getStateCount(), candidate.material.source_revision);
    if (!validation) {
      return validation;
    }
    const std::size_t technique_count = material.getNumTechniques();
    if (technique_count > (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "material.technique_count",
          "native technique count exceeds the contract range");
    }
    candidate.material.technique_count =
        static_cast<std::uint32_t>(technique_count);
    if (technique_count != 1U) {
      return ValidateOgre14LegacyMaterialInput(candidate.material);
    }
    const Ogre::Technique *technique = material.getTechnique(0U);
    if (technique == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "material.technique",
                                       "native material technique is absent");
    }
    const std::size_t pass_count = technique->getNumPasses();
    if (pass_count > (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "material.pass_count",
          "native pass count exceeds the contract range");
    }
    candidate.material.pass_count = static_cast<std::uint32_t>(pass_count);
    if (pass_count != 1U) {
      return ValidateOgre14LegacyMaterialInput(candidate.material);
    }
    const Ogre::Pass *pass = technique->getPass(0U);
    if (pass == nullptr) {
      return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                       "material.pass",
                                       "native material pass is absent");
    }
    validation =
        ValidateNativeTechniqueAndPassState(material, *technique, *pass);
    if (!validation) {
      return validation;
    }
    if (!technique->isSupported()) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "material.technique_state",
          "v1 requires a technique supported by the current render system");
    }
    const std::size_t texture_unit_count = pass->getNumTextureUnitStates();
    if (texture_unit_count > 1U) {
      // Validation only needs proof that the count exceeds the v1 limit; do
      // not mirror an attacker-controlled native count into an allocation.
      candidate.material.texture_units.resize(2U);
      return ValidateOgre14LegacyMaterialInput(candidate.material);
    }
    std::uint32_t before_declaration_version = 0U;
    Ogre14LegacyNativeMaterialDeclarationSha256 before_declaration_sha256{};
    validation = BuildNativeMaterialDeclarationDigest(
        material, *technique, *pass,
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
        true,
#endif
        before_declaration_version,
        before_declaration_sha256);
    if (!validation) {
      return validation;
    }
    candidate.material.has_vertex_program = pass->hasVertexProgram();
    candidate.material.has_fragment_program = pass->hasFragmentProgram();
    candidate.material.has_geometry_program = pass->hasGeometryProgram();
    candidate.material.has_tessellation_program =
        pass->hasTessellationHullProgram() ||
        pass->hasTessellationDomainProgram();
    candidate.material.has_compute_program = pass->hasComputeProgram();
    const std::string scheme = technique->getSchemeName();
    candidate.material.generated_rtss_program =
        scheme.find("ShaderGenerator") != std::string::npos;
    candidate.material.base_color_semantic = declaration.base_color_semantic;
    candidate.material.lighting_enabled = pass->getLightingEnabled();
    candidate.material.diffuse_linear = ToFloat4(pass->getDiffuse());
    candidate.material.ambient_linear = ToFloat3(pass->getAmbient());
    candidate.material.specular_linear = ToFloat3(pass->getSpecular());
    candidate.material.emissive_linear = ToFloat3(pass->getEmissive());
    candidate.material.shininess = pass->getShininess();
    validation = CapturePipeline(*pass, candidate.material.pipeline);
    if (!validation) {
      return validation;
    }

    if (texture_unit_count == 1U) {
      const Ogre::TextureUnitState *native_unit = pass->getTextureUnitState(0U);
      if (native_unit == nullptr) {
        return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                         "material.texture_unit",
                                         "native texture unit is absent");
      }
      Ogre14LegacyTextureUnitInput unit;
      Ogre14LegacyTextureInput texture;
      Ogre14AuthenticatedTextureResolution texture_resolution;
      validation =
          CaptureTextureUnit(*native_unit, declaration,
                             candidate.material.source_revision, unit, texture,
                             texture_resolver, texture_resolution);
      if (!validation) {
        return validation;
      }
      candidate.material.texture_units.push_back(std::move(unit));
      candidate.textures.push_back(std::move(texture));
      if (texture_resolver != nullptr) {
        candidate.authenticated_texture_resolutions.push_back(
            std::move(texture_resolution));
      }
    }
    validation = ValidateOgre14LegacyMaterialInput(candidate.material);
    if (!validation) {
      return validation;
    }
    Ogre14LegacyMaterialPipelineAudit native_audit_value;
    validation = DeriveOgre14LegacyMaterialPipelineAudit(candidate.material,
                                                         native_audit_value);
    if (!validation) {
      return validation;
    }
    auto native_audit =
        std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
            std::move(native_audit_value));
    candidate.exact_native_material_audit = native_audit;
    if (texture_resolver != nullptr &&
        candidate.authenticated_texture_resolutions.size() !=
            candidate.textures.size()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH,
          "texture_resolution.alignment",
          "authenticated source resolutions are not aligned with captured textures");
    }
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
    BeforeNativeMaterialDeclarationDigestStage(
        Ogre14LegacyNativeMaterialDeclarationDigestStage::
            BEFORE_FRESHNESS_REVALIDATION);
#endif
    validation =
        ValidateNativeTechniqueAndPassState(material, *technique, *pass);
    if (!validation || !technique->isSupported()) {
      return validation
                 ? ValidationResult::Failure(
                       ValidationCode::REVISION_MISMATCH,
                       "native_material_declaration.freshness",
                       "native technique support changed before capture publication")
                 : validation;
    }
    std::uint32_t after_declaration_version = 0U;
    Ogre14LegacyNativeMaterialDeclarationSha256 after_declaration_sha256{};
    validation = BuildNativeMaterialDeclarationDigest(
        material, *technique, *pass,
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
        true,
#endif
        after_declaration_version,
        after_declaration_sha256);
    if (!validation) {
      return validation;
    }
    std::uint32_t verified_declaration_version = 0U;
    Ogre14LegacyNativeMaterialDeclarationSha256
        verified_declaration_sha256{};
    validation = BuildNativeMaterialDeclarationDigest(
        material, *technique, *pass,
#if defined(ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING)
        false,
#endif
        verified_declaration_version,
        verified_declaration_sha256);
    if (!validation) {
      return validation;
    }
    if (before_declaration_version != after_declaration_version ||
        before_declaration_sha256 != after_declaration_sha256 ||
        after_declaration_version != verified_declaration_version ||
        after_declaration_sha256 != verified_declaration_sha256) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "native_material_declaration.freshness",
          "native material declaration changed during atomic capture");
    }
    std::uint64_t current_material_revision = 0U;
    validation =
        SourceRevision(material.getStateCount(), current_material_revision);
    if (!validation || !material.isLoaded() ||
        current_material_revision != candidate.material.source_revision) {
      return validation
                 ? ValidationResult::Failure(
                       ValidationCode::REVISION_MISMATCH,
                       "native_material_declaration.freshness",
                       "native material revision changed during atomic capture")
                 : validation;
    }
    if (!candidate.textures.empty()) {
      const Ogre::TextureUnitState *current_unit =
          pass->getNumTextureUnitStates() == 1U
              ? pass->getTextureUnitState(0U)
              : nullptr;
      const Ogre::TexturePtr current_texture =
          current_unit != nullptr ? current_unit->_getTexturePtr()
                                  : Ogre::TexturePtr{};
      std::uint64_t current_texture_revision = 0U;
      validation = current_texture
                       ? SourceRevision(current_texture->getStateCount(),
                                        current_texture_revision)
                       : ValidationResult::Failure(
                             ValidationCode::MISSING_REFERENCE,
                             "native_material_declaration.freshness",
                             "native texture disappeared during atomic capture");
      if (!validation || !current_texture->isLoaded() ||
          current_texture_revision !=
              candidate.textures.front().source_revision ||
          current_texture->getGroup() !=
              candidate.textures.front().key.exact_resource_group ||
          current_texture->getName() !=
              candidate.textures.front().key.exact_name) {
        return validation
                   ? ValidationResult::Failure(
                         ValidationCode::REVISION_MISMATCH,
                         "native_material_declaration.freshness",
                         "native texture identity or revision changed during atomic capture")
                   : validation;
      }
    }
    candidate.native_material_declaration_serialization_version =
        after_declaration_version;
    candidate.native_material_declaration_sha256 =
        after_declaration_sha256;
    Ogre14LegacyNativeMaterialCaptureSha256 candidate_capture_sha256{};
    validation = ComputeOgre14LegacyNativeMaterialCaptureSha256(
        candidate, candidate_capture_sha256);
    if (!validation) {
      return validation;
    }
    if (texture_resolver != nullptr) {
      validation = RevalidateAuthenticatedTextureForPublication(
          material, *texture_resolver, candidate);
      if (!validation) {
        return validation;
      }
    }

    candidate_output = std::move(candidate);
    capture_sha256_output = candidate_capture_sha256;
    return ValidationResult::Success();
  } catch (const Ogre::Exception &) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "native.ogre_exception",
        "OGRE failed while capturing immutable legacy material state");
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "native.allocation",
        "allocation failed before native capture committed");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "native.exception",
        "unexpected native exception before capture commit");
  }
}

} // namespace

ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    Ogre14LegacyNativeMaterialCapture &capture) {
  Ogre14LegacyNativeMaterialCapture candidate;
  Ogre14LegacyNativeMaterialCaptureSha256 candidate_capture_sha256{};
  ValidationResult validation = CaptureOgre14LegacyNativeMaterialCandidate(
      material, declaration, nullptr, candidate, candidate_capture_sha256);
  if (!validation) {
    return validation;
  }
  candidate.native_material_audit_receipt =
      Ogre14LegacyNativeMaterialAuditReceipt(
          candidate.exact_native_material_audit,
          candidate.native_material_declaration_serialization_version,
          candidate.native_material_declaration_sha256,
          candidate_capture_sha256, nullptr);
  capture = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    const IOgre14AuthenticatedTextureResolver &texture_resolver,
    Ogre14LegacyNativeMaterialCapture &capture) {
  Ogre14LegacyNativeMaterialCapture candidate;
  Ogre14LegacyNativeMaterialCaptureSha256 candidate_capture_sha256{};
  ValidationResult validation = CaptureOgre14LegacyNativeMaterialCandidate(
      material, declaration, &texture_resolver, candidate,
      candidate_capture_sha256);
  if (!validation) {
    return validation;
  }
  candidate.native_material_audit_receipt =
      Ogre14LegacyNativeMaterialAuditReceipt(
          candidate.exact_native_material_audit,
          candidate.native_material_declaration_serialization_version,
          candidate.native_material_declaration_sha256,
          candidate_capture_sha256,
          candidate.authenticated_texture_resolutions.empty()
              ? nullptr
              : &candidate.authenticated_texture_resolutions.front());
  capture = std::move(candidate);
  return ValidationResult::Success();
}

} // namespace RoR::Render
