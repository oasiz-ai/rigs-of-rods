/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"

#include "gfx/render/RenderPayloadDigest.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace RoR::Render {
namespace {

constexpr std::size_t kMaximumCaptureProjectionBytes = 256U * 1024U;
constexpr std::array<std::uint8_t, 8U> kCaptureProjectionMagic{{
    'R', 'O', 'R', 'N', 'C', 'P', '1', '\0',
}};

class CaptureProjectionWriter final {
public:
  CaptureProjectionWriter() {
    bytes_.reserve(1024U);
    (void)AppendBytes(kCaptureProjectionMagic.data(),
                      kCaptureProjectionMagic.size());
    (void)AppendU32(kOgre14LegacyNativeMaterialCaptureSerializationVersion);
  }

  [[nodiscard]] bool AppendU8(std::uint8_t value) {
    return AppendBytes(&value, 1U);
  }

  [[nodiscard]] bool AppendBool(bool value) {
    return AppendU8(value ? 1U : 0U);
  }

  [[nodiscard]] bool AppendU32(std::uint32_t value) {
    std::array<std::uint8_t, 4U> encoded{};
    for (std::size_t byte = 0U; byte < encoded.size(); ++byte) {
      encoded[byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
    return AppendBytes(encoded.data(), encoded.size());
  }

  [[nodiscard]] bool AppendU64(std::uint64_t value) {
    std::array<std::uint8_t, 8U> encoded{};
    for (std::size_t byte = 0U; byte < encoded.size(); ++byte) {
      encoded[byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
    return AppendBytes(encoded.data(), encoded.size());
  }

  [[nodiscard]] bool AppendFloat(float value) {
    if (!std::isfinite(value)) {
      Fail(ValidationCode::NON_FINITE_VALUE,
           "native_capture_projection.float",
           "native capture projection contains a non-finite float32 value");
      return false;
    }
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "native capture projection requires binary32 float");
    static_assert(std::numeric_limits<float>::is_iec559,
                  "native capture projection requires IEEE-754 float32");
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AppendU32(bits);
  }

  [[nodiscard]] bool AppendString(const std::string &value) {
    if (value.size() >
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "native_capture_projection.string",
           "native capture string exceeds the canonical uint32 range");
      return false;
    }
    return AppendU32(static_cast<std::uint32_t>(value.size())) &&
           AppendBytes(
               reinterpret_cast<const std::uint8_t *>(value.data()),
               value.size());
  }

  [[nodiscard]] bool AppendDigest(
      const std::array<std::uint8_t, 32U> &digest) {
    return AppendBytes(digest.data(), digest.size());
  }

  [[nodiscard]] bool ok() const noexcept { return error_.ok(); }
  [[nodiscard]] const ValidationResult &error() const noexcept {
    return error_;
  }
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
        bytes_.size() > kMaximumCaptureProjectionBytes ||
        size > kMaximumCaptureProjectionBytes - bytes_.size()) {
      Fail(ValidationCode::VALUE_OUT_OF_RANGE,
           "native_capture_projection.canonical_bytes",
           "native capture projection exceeds its canonical byte cap");
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

template <typename Enum>
bool AppendEnum(CaptureProjectionWriter &writer, Enum value) {
  const auto numeric = static_cast<std::int64_t>(value);
  return numeric >= 0 &&
         numeric <= (std::numeric_limits<std::uint8_t>::max)() &&
         writer.AppendU8(static_cast<std::uint8_t>(numeric));
}

bool AppendCount(CaptureProjectionWriter &writer, std::size_t count) {
  return count <=
             static_cast<std::size_t>(
                 (std::numeric_limits<std::uint32_t>::max)()) &&
         writer.AppendU32(static_cast<std::uint32_t>(count));
}

bool AppendFloat3(CaptureProjectionWriter &writer, const Float3 &value) {
  return writer.AppendFloat(value.x) && writer.AppendFloat(value.y) &&
         writer.AppendFloat(value.z);
}

bool AppendFloat4(CaptureProjectionWriter &writer, const Float4 &value) {
  return writer.AppendFloat(value.x) && writer.AppendFloat(value.y) &&
         writer.AppendFloat(value.z) && writer.AppendFloat(value.w);
}

bool AppendKey(CaptureProjectionWriter &writer,
               const Ogre14LegacyAssetKey &key) {
  return writer.AppendString(key.exact_resource_group) &&
         writer.AppendString(key.exact_name);
}

bool AppendPipeline(CaptureProjectionWriter &writer,
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

bool AppendSampler(CaptureProjectionWriter &writer,
                   const Ogre14LegacySamplerInput &sampler) {
  return writer.AppendU64(sampler.source_revision) &&
         AppendEnum(writer, sampler.minification) &&
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

bool AppendTextureUnit(CaptureProjectionWriter &writer,
                       const Ogre14LegacyTextureUnitInput &unit) {
  return AppendKey(writer, unit.texture_key) &&
         AppendSampler(writer, unit.sampler) &&
         writer.AppendU8(unit.texture_coordinate_set) &&
         writer.AppendBool(unit.named_content) &&
         writer.AppendBool(unit.texture_2d) &&
         writer.AppendU32(unit.frame_count) &&
         writer.AppendBool(unit.has_animated_or_procedural_effect) &&
         writer.AppendBool(unit.projective) &&
         writer.AppendBool(unit.environment_mapping) &&
         writer.AppendBool(unit.compositor) &&
         writer.AppendBool(unit.render_target) &&
         writer.AppendBool(unit.canonical_color_modulate) &&
         writer.AppendBool(unit.canonical_alpha_modulate) &&
         writer.AppendBool(unit.identity_texture_transform);
}

bool AppendMaterial(CaptureProjectionWriter &writer,
                    const Ogre14LegacyMaterialInput &material) {
  if (!writer.AppendU32(material.version) ||
      !AppendKey(writer, material.key) ||
      !writer.AppendU64(material.source_revision) ||
      !writer.AppendU32(material.technique_count) ||
      !writer.AppendU32(material.pass_count) ||
      !writer.AppendBool(material.generated_rtss_program) ||
      !writer.AppendBool(material.has_vertex_program) ||
      !writer.AppendBool(material.has_fragment_program) ||
      !writer.AppendBool(material.has_geometry_program) ||
      !writer.AppendBool(material.has_tessellation_program) ||
      !writer.AppendBool(material.has_compute_program) ||
      !AppendEnum(writer, material.base_color_semantic) ||
      !writer.AppendBool(material.lighting_enabled) ||
      !AppendFloat4(writer, material.diffuse_linear) ||
      !AppendFloat3(writer, material.ambient_linear) ||
      !AppendFloat3(writer, material.specular_linear) ||
      !AppendFloat3(writer, material.emissive_linear) ||
      !writer.AppendFloat(material.shininess) ||
      !AppendPipeline(writer, material.pipeline) ||
      !AppendCount(writer, material.texture_units.size())) {
    return false;
  }
  for (const Ogre14LegacyTextureUnitInput &unit : material.texture_units) {
    if (!AppendTextureUnit(writer, unit)) {
      return false;
    }
  }
  return true;
}

bool AppendTexture(CaptureProjectionWriter &writer,
                   const Ogre14LegacyTextureInput &texture) {
  if (!writer.AppendU32(texture.version) || !AppendKey(writer, texture.key) ||
      !writer.AppendU64(texture.source_revision) ||
      !AppendEnum(writer, texture.type) ||
      !AppendEnum(writer, texture.pixel_encoding) ||
      !AppendEnum(writer, texture.color_role) ||
      !writer.AppendBool(texture.hardware_gamma_enabled) ||
      !writer.AppendBool(texture.compressed) ||
      !writer.AppendBool(texture.render_target) ||
      !writer.AppendBool(texture.generated) ||
      !writer.AppendBool(texture.procedural) ||
      !writer.AppendU32(texture.width) ||
      !writer.AppendU32(texture.height) ||
      !AppendCount(writer, texture.mip_levels.size())) {
    return false;
  }
  for (const Ogre14LegacyTextureMipInput &mip : texture.mip_levels) {
    const auto byte_sha256 = ComputeRenderPayloadDigest(
        mip.bytes.data(), mip.bytes.size());
    if (!writer.AppendU32(mip.width) || !writer.AppendU32(mip.height) ||
        !writer.AppendU64(mip.row_pitch_bytes) ||
        !writer.AppendU64(mip.slice_pitch_bytes) ||
        !writer.AppendU64(static_cast<std::uint64_t>(mip.bytes.size())) ||
        !writer.AppendDigest(byte_sha256)) {
      return false;
    }
  }
  return true;
}

} // namespace

ValidationResult ComputeOgre14LegacyNativeMaterialCaptureSha256(
    const Ogre14LegacyNativeMaterialCapture &capture,
    Ogre14LegacyNativeMaterialCaptureSha256 &sha256) noexcept {
  try {
    CaptureProjectionWriter writer;
    if (!writer.ok() || !writer.AppendU32(capture.version) ||
        !AppendMaterial(writer, capture.material) ||
        !AppendCount(writer, capture.textures.size())) {
      return writer.ok()
                 ? ValidationResult::Failure(
                       ValidationCode::VALUE_OUT_OF_RANGE,
                       "native_capture_projection.material",
                       "native public material capture is outside the canonical projection")
                 : writer.error();
    }
    for (const Ogre14LegacyTextureInput &texture : capture.textures) {
      if (!AppendTexture(writer, texture)) {
        return writer.ok()
                   ? ValidationResult::Failure(
                         ValidationCode::VALUE_OUT_OF_RANGE,
                         "native_capture_projection.texture",
                         "native public texture capture is outside the canonical projection")
                   : writer.error();
      }
    }
    if (!AppendCount(writer,
                     capture.authenticated_texture_resolutions.size()) ||
        !writer.AppendU32(
            capture.native_material_declaration_serialization_version) ||
        !writer.AppendDigest(capture.native_material_declaration_sha256)) {
      return writer.ok()
                 ? ValidationResult::Failure(
                       ValidationCode::VALUE_OUT_OF_RANGE,
                       "native_capture_projection.authority",
                       "native public authority fields are outside the canonical projection")
                 : writer.error();
    }
    const Ogre14LegacyNativeMaterialCaptureSha256 candidate =
        ComputeRenderPayloadDigest(writer.bytes().data(), writer.bytes().size());
    sha256 = candidate;
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD,
        "native_capture_projection.allocation",
        "allocation failed before native capture projection committed");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "native_capture_projection.exception",
        "unexpected exception before native capture projection committed");
  }
}

bool Ogre14LegacyNativeMaterialAuditReceipt::Authenticates(
    const Ogre14LegacyNativeMaterialCapture &capture) const noexcept {
  if (version_ != kOgre14LegacyNativeMaterialAuditReceiptVersion ||
      declaration_serialization_version_ !=
          kOgre14LegacyNativeMaterialDeclarationSerializationVersion ||
      capture.native_material_declaration_serialization_version !=
          declaration_serialization_version_ ||
      capture.native_material_declaration_sha256 != declaration_sha256_ ||
      owner_ == nullptr || capture.exact_native_material_audit == nullptr ||
      owner_.get() != capture.exact_native_material_audit.get() ||
      owner_.owner_before(capture.exact_native_material_audit) ||
      capture.exact_native_material_audit.owner_before(owner_) ||
      capture.authenticated_texture_resolutions.size() !=
          (has_authenticated_texture_resolution_ ? 1U : 0U) ||
      (has_authenticated_texture_resolution_ &&
       !authenticated_texture_resolution_.SharesLoadedResourceAuthorityWith(
           capture.authenticated_texture_resolutions.front()))) {
    return false;
  }
  Ogre14LegacyNativeMaterialCaptureSha256 capture_sha256;
  const ValidationResult validation =
      ComputeOgre14LegacyNativeMaterialCaptureSha256(capture, capture_sha256);
  return validation.ok() && capture_sha256 == capture_sha256_;
}

} // namespace RoR::Render
