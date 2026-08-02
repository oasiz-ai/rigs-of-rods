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

namespace RoR::Render {
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

ValidationResult ValidateNativeTechniqueAndPassState(
    const Ogre::Material &material, const Ogre::Technique &technique,
    const Ogre::Pass &pass) {
  if (technique.getSchemeName() != Ogre::MaterialManager::DEFAULT_SCHEME_NAME ||
      technique.getLodIndex() != 0U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.technique_state",
        "v1 requires one default-scheme LOD-zero technique");
  }
  if (technique.getShadowCasterMaterial() ||
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
      pass.getLightCountPerIteration() != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.pipeline.lighting_controls",
        "legacy shading, light selection, or iteration is outside the "
        "canonical v1 state");
  }
  if (pass.getFogOverride() || !pass.getPolygonModeOverrideable() ||
      pass.getLightScissoringEnabled() ||
      pass.getLightClipPlanesEnabled() ||
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

ValidationResult
CaptureTextureUnit(const Ogre::TextureUnitState &native,
                   const Ogre14LegacyNativeMaterialDeclaration &declaration,
                   std::uint64_t material_revision,
                   Ogre14LegacyTextureUnitInput &unit,
                   Ogre14LegacyTextureInput &texture) {
  Ogre14LegacyTextureUnitInput candidate;
  candidate.texture_key.exact_name = native.getTextureName();
  candidate.sampler.source_revision = material_revision;
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
        ValidationCode::UNSUPPORTED_FEATURE, "material.texture_unit.frame_count",
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

  const Ogre::Sampler::UVWAddressingMode &address =
      native.getTextureAddressingMode();
  candidate.sampler.address_u = ToAddress(address.u);
  candidate.sampler.address_v = ToAddress(address.v);
  candidate.sampler.address_w = ToAddress(address.w);
  candidate.sampler.minification =
      ToFilter(native.getTextureFiltering(Ogre::FT_MIN));
  candidate.sampler.magnification =
      ToFilter(native.getTextureFiltering(Ogre::FT_MAG));
  candidate.sampler.mip = ToFilter(native.getTextureFiltering(Ogre::FT_MIP));
  candidate.sampler.mip_lod_bias = native.getTextureMipmapBias();
  candidate.sampler.minimum_lod = 0.0F;
  const unsigned int maximum_anisotropy = native.getTextureAnisotropy();
  if (static_cast<std::uint64_t>(maximum_anisotropy) >
      (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.sampler.maximum_anisotropy",
        "native anisotropy exceeds the portable integer range");
  }
  candidate.sampler.maximum_anisotropy =
      static_cast<std::uint32_t>(maximum_anisotropy);
  candidate.sampler.compare_enabled = native.getTextureCompareEnabled();
  candidate.sampler.compare_operation =
      ToCompare(native.getTextureCompareFunction());
  candidate.sampler.border_color = ToFloat4(native.getTextureBorderColour());

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
  candidate.texture_key = texture.key;
  candidate.render_target = texture.render_target;
  candidate.sampler.maximum_lod =
      candidate.sampler.mip == Ogre14LegacyFilter::NONE
          ? 0.0F
          : static_cast<float>(texture.mip_levels.size() - 1U);
  unit = std::move(candidate);
  return ValidationResult::Success();
}

} // namespace

ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    Ogre14LegacyNativeMaterialCapture &capture) {
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

    const std::size_t texture_unit_count = pass->getNumTextureUnitStates();
    if (texture_unit_count > 1U) {
      // Validation only needs proof that the count exceeds the v1 limit; do
      // not mirror an attacker-controlled native count into an allocation.
      candidate.material.texture_units.resize(2U);
      return ValidateOgre14LegacyMaterialInput(candidate.material);
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
      validation =
          CaptureTextureUnit(*native_unit, declaration,
                             candidate.material.source_revision, unit, texture);
      if (!validation) {
        return validation;
      }
      candidate.material.texture_units.push_back(std::move(unit));
      candidate.textures.push_back(std::move(texture));
    }
    if (!technique->isSupported()) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "material.technique_state",
          "v1 requires a technique supported by the current render system");
    }
    validation = ValidateOgre14LegacyMaterialInput(candidate.material);
    if (!validation) {
      return validation;
    }
    capture = std::move(candidate);
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

} // namespace RoR::Render
