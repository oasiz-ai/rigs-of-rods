/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoMaterialSource.h"

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/render/MaterialDescriptor.h"
#include "gfx/render/RenderAssetRegistry.h"
#include "gfx/render/RenderResourceDescriptors.h"

#include <OgreBuildSettings.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "the disposable OgreNext material source is pinned to OGRE 14.5.2");
static_assert(sizeof(Ogre::Real) == sizeof(float),
              "the disposable OgreNext material source requires binary32 Ogre::Real");

namespace RoR::Gfx::Detail {
namespace {

constexpr char kProjectionTokenDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/Token/v1";
constexpr char kTextureIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/TextureSourceAsset/v1";
constexpr char kSamplerIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/SamplerSourceAsset/v1";
constexpr char kMaterialGroup[] = "RoR/OgreNextDemo/ProjectedPbr/v1";
constexpr std::uint32_t kMaximumTextureDimension = 8192U;
constexpr std::uint64_t kMaximumTextureBaseBytes =
    256ULL * 1024ULL * 1024ULL;

Render::ValidationResult Failure(Render::ValidationCode code,
                                 const char *field,
                                 const char *detail) {
  return Render::ValidationResult::Failure(code, field, detail);
}

std::string HexId(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

void AppendField(std::string &key, std::string_view value) {
  key.append(value.data(), value.size());
  key.push_back('\0');
}

void AppendNumber(std::string &key, std::uint64_t value) {
  AppendField(key, std::to_string(value));
}

bool IsIdentityTextureTransform(const Ogre::Matrix4 &matrix) noexcept {
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      const Ogre::Real expected = row == column ? 1.0F : 0.0F;
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

bool HasAuthoredProgram(const Ogre::Pass &pass) noexcept {
  return pass.hasVertexProgram() || pass.hasFragmentProgram() ||
         pass.hasGeometryProgram() || pass.hasTessellationHullProgram() ||
         pass.hasTessellationDomainProgram() || pass.hasComputeProgram();
}

bool IsOpaqueReplacePass(const Ogre::Pass &pass,
                         bool allow_vertex_colour_tracking) noexcept {
  bool red = false;
  bool green = false;
  bool blue = false;
  bool alpha = false;
  pass.getColourWriteEnabled(red, green, blue, alpha);
  const Ogre::ColourValue diffuse = pass.getDiffuse();
  return red && green && blue && alpha && diffuse.a == 1.0F &&
         pass.getLightingEnabled() &&
         pass.getSceneBlendingOperation() == Ogre::SBO_ADD &&
         pass.getSceneBlendingOperationAlpha() == Ogre::SBO_ADD &&
         pass.getSourceBlendFactor() == Ogre::SBF_ONE &&
         pass.getDestBlendFactor() == Ogre::SBF_ZERO &&
         pass.getSourceBlendFactorAlpha() == Ogre::SBF_ONE &&
         pass.getDestBlendFactorAlpha() == Ogre::SBF_ZERO &&
         pass.getAlphaRejectFunction() == Ogre::CMPF_ALWAYS_PASS &&
         !pass.isAlphaToCoverageEnabled() && pass.getDepthCheckEnabled() &&
         pass.getDepthWriteEnabled() &&
         pass.getDepthFunction() == Ogre::CMPF_LESS_EQUAL &&
         pass.getDepthBiasConstant() == 0.0F &&
         pass.getDepthBiasSlopeScale() == 0.0F &&
         pass.getIterationDepthBias() == 0.0F &&
         pass.getManualCullingMode() == Ogre::MANUAL_CULL_BACK &&
         pass.getPolygonMode() == Ogre::PM_SOLID &&
         pass.getPolygonModeOverrideable() &&
         (pass.getVertexColourTracking() == Ogre::TVC_NONE ||
          allow_vertex_colour_tracking) &&
         !pass.getFogOverride() && pass.getPassIterationCount() == 1U &&
         !pass.getIteratePerLight();
}

bool IsExactAlexisDiffuseProjection(
    const Ogre::Technique &technique, const Ogre::Pass &base_pass,
    std::string_view exact_material_name,
    std::string_view exact_diffuse_texture_name) noexcept {
  const std::size_t separator = exact_material_name.find(" (");
  if (separator == std::string_view::npos ||
      technique.getNumPasses() != 2U ||
      base_pass.getName() != "BaseRender" ||
      base_pass.getNumTextureUnitStates() != 1U ||
      HasAuthoredProgram(base_pass)) {
    return false;
  }
  const std::string_view base = exact_material_name.substr(0U, separator);
  std::string_view expected_diffuse;
  std::string_view expected_specular;
  if (base == "SaberChassis" || base == "SaberChassisM") {
    expected_diffuse = "AlexisSaberChassis.png";
    expected_specular = "AlexisSaberChassisSpec.png";
  } else if (base == "SaberWheels") {
    expected_diffuse = "AlexisSaberWheel.png";
    expected_specular = "AlexisSaberWheelSpec.png";
  } else if (base == "SaberGrilles") {
    expected_diffuse = "AlexisSabergrilles.png";
    expected_specular = "alexissabergrillesspec.png";
  } else {
    return false;
  }
  Ogre::TextureUnitState *const diffuse =
      base_pass.getTextureUnitState(0U);
  Ogre::Pass *const specular = technique.getPass(1U);
  if (exact_diffuse_texture_name != expected_diffuse || diffuse == nullptr ||
      diffuse->getName() != "Diffuse_Map" || specular == nullptr ||
      specular->getName() != "SpecularMapping1" ||
      specular->getNumTextureUnitStates() != 2U ||
      HasAuthoredProgram(*specular) ||
      specular->getSourceBlendFactor() != Ogre::SBF_ONE ||
      specular->getDestBlendFactor() != Ogre::SBF_ONE) {
    return false;
  }
  Ogre::TextureUnitState *const specular_texture =
      specular->getTextureUnitState(0U);
  Ogre::TextureUnitState *const environment =
      specular->getTextureUnitState(1U);
  return specular_texture != nullptr && environment != nullptr &&
         specular_texture->getName() == "SpecularMapping1_Tex" &&
         specular_texture->getTextureName() == expected_specular &&
         environment->getName() == "envmap" &&
         environment->getTextureName() == "EnvironmentTexture";
}

bool MapAddressMode(Ogre::TextureAddressingMode native,
                    Render::SamplerAddressMode &portable) noexcept {
  switch (native) {
  case Ogre::TAM_WRAP:
    portable = Render::SamplerAddressMode::REPEAT;
    return true;
  case Ogre::TAM_MIRROR:
    portable = Render::SamplerAddressMode::MIRRORED_REPEAT;
    return true;
  case Ogre::TAM_CLAMP:
    portable = Render::SamplerAddressMode::CLAMP_TO_EDGE;
    return true;
  default:
    return false;
  }
}

struct CapturedTexture final {
  const Ogre::Texture *native_texture = nullptr;
  const Ogre::HardwarePixelBuffer *native_base_buffer = nullptr;
  Ogre::PixelFormat native_texture_format = Ogre::PF_UNKNOWN;
  Ogre::PixelFormat native_buffer_format = Ogre::PF_UNKNOWN;
  std::size_t native_state_count = 0U;
  std::uint32_t native_width = 0U;
  std::uint32_t native_height = 0U;
  std::uint64_t source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> payload;
};

struct CapturedSampler final {
  std::uint64_t source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> payload;
};

struct Projection final {
  std::string exact_name;
  std::string texture_key;
  std::string sampler_key;
  const Ogre::Material *native_material = nullptr;
  const Ogre::Pass *native_pass = nullptr;
  const Ogre::TextureUnitState *native_unit = nullptr;
  const Ogre::Sampler *native_sampler = nullptr;
  std::size_t native_material_state_count = 0U;
  std::array<float, 4U> base_color_factor{};
  float roughness_factor = 1.0F;
  std::array<float, 3U> emissive_factor{};
  std::uint64_t material_source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> placeholder_payload;
  std::shared_ptr<const Render::RenderAssetPayload> material_payload;
};

struct ProjectionDecision final {
  std::string exact_resource_group;
  std::string exact_material_name;
  Render::Ogre14GraphicsSceneMaterialCull exact_cull =
      Render::Ogre14GraphicsSceneMaterialCull::NONE;
  bool projection_candidate = false;
  bool has_authored_uv0 = false;
  bool projected = false;
  std::string projection_key;
};

Render::ValidationResult ValidateNativeBasePixelFormat(
    Ogre::PixelFormat native_format) {
  const bool exact_unsigned_rgb8 =
      native_format == Ogre::PF_R8G8B8 ||
      native_format == Ogre::PF_B8G8R8 ||
      native_format == Ogre::PF_A8R8G8B8 ||
      native_format == Ogre::PF_A8B8G8R8 ||
      native_format == Ogre::PF_B8G8R8A8 ||
      native_format == Ogre::PF_X8R8G8B8 ||
      native_format == Ogre::PF_X8B8G8R8 ||
      native_format == Ogre::PF_R8G8B8A8;
  int bit_depths[4U] = {0, 0, 0, 0};
  Ogre::PixelUtil::getBitDepths(native_format, bit_depths);
  const std::size_t component_count =
      Ogre::PixelUtil::getComponentCount(native_format);
  const std::size_t element_bytes =
      Ogre::PixelUtil::getNumElemBytes(native_format);
  const bool exact_rgb8 = bit_depths[0U] == 8 && bit_depths[1U] == 8 &&
                          bit_depths[2U] == 8 &&
                          (bit_depths[3U] == 0 || bit_depths[3U] == 8);
  if (!exact_unsigned_rgb8 ||
      !Ogre::PixelUtil::isAccessible(native_format) ||
      Ogre::PixelUtil::isCompressed(native_format) ||
      Ogre::PixelUtil::isFloatingPoint(native_format) ||
      Ogre::PixelUtil::isInteger(native_format) ||
      Ogre::PixelUtil::isDepth(native_format) ||
      Ogre::PixelUtil::isLuminance(native_format) ||
      Ogre::PixelUtil::getComponentType(native_format) != Ogre::PCT_BYTE ||
      !exact_rgb8 || (component_count != 3U && component_count != 4U) ||
      (element_bytes != 3U && element_bytes != 4U)) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.texture.native_format",
                   "TUS0 must use an accessible uncompressed normalized RGB8 or RGBA8 native format");
  }
  return Render::ValidationResult::Success();
}

struct TextureBasePreflight final {
  Ogre::HardwarePixelBufferSharedPtr buffer;
  Ogre::PixelFormat texture_format = Ogre::PF_UNKNOWN;
  Ogre::PixelFormat buffer_format = Ogre::PF_UNKNOWN;
  std::size_t state_count = 0U;
  std::size_t width = 0U;
  std::size_t height = 0U;
};

Render::ValidationResult PreflightTextureBase(
    const Ogre::TexturePtr &native_texture,
    TextureBasePreflight &output,
    bool &eligible) {
  eligible = false;
  if (!native_texture || !native_texture->isLoaded() ||
      native_texture->isManuallyLoaded() ||
      native_texture->getTextureType() != Ogre::TEX_TYPE_2D ||
      native_texture->getDepth() != 1U || native_texture->getNumFaces() != 1U ||
      (native_texture->getUsage() & Ogre::TU_RENDERTARGET) != 0U) {
    return Render::ValidationResult::Success();
  }
  const std::size_t native_width = native_texture->getWidth();
  const std::size_t native_height = native_texture->getHeight();
  if (native_width == 0U || native_height == 0U ||
      native_width > kMaximumTextureDimension ||
      native_height > kMaximumTextureDimension ||
      native_width > (std::numeric_limits<std::uint32_t>::max)() ||
      native_height > (std::numeric_limits<std::uint32_t>::max)()) {
    return Render::ValidationResult::Success();
  }
  const std::uint64_t rgba_row_bytes =
      static_cast<std::uint64_t>(native_width) * 4U;
  if (rgba_row_bytes > kMaximumTextureBaseBytes /
                           static_cast<std::uint64_t>(native_height)) {
    return Render::ValidationResult::Success();
  }
  const Ogre::HardwarePixelBufferSharedPtr buffer =
      native_texture->getBuffer(0U, 0U);
  if (!buffer || buffer->getWidth() != native_width ||
      buffer->getHeight() != native_height || buffer->getDepth() != 1U) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.texture.buffer",
                   "TUS0 has no exact base pixel buffer");
  }
  const Ogre::PixelFormat texture_format = native_texture->getFormat();
  const Ogre::PixelFormat buffer_format = buffer->getFormat();
  Render::ValidationResult validation =
      ValidateNativeBasePixelFormat(buffer_format);
  if (!validation) {
    return Render::ValidationResult::Success();
  }
  TextureBasePreflight candidate;
  candidate.buffer = buffer;
  candidate.texture_format = texture_format;
  candidate.buffer_format = buffer_format;
  candidate.state_count = native_texture->getStateCount();
  candidate.width = native_width;
  candidate.height = native_height;
  output = std::move(candidate);
  eligible = true;
  return Render::ValidationResult::Success();
}

Render::ValidationResult CaptureTextureBase(
    const Ogre::TexturePtr &native_texture,
    const TextureBasePreflight &preflight,
    std::string_view debug_token,
    Render::TextureResourceDescriptor &output,
    Ogre::PixelFormat &output_native_texture_format,
    Ogre::PixelFormat &output_native_buffer_format) {
  if (!native_texture || !preflight.buffer ||
      native_texture->getStateCount() != preflight.state_count ||
      native_texture->getWidth() != preflight.width ||
      native_texture->getHeight() != preflight.height ||
      native_texture->getFormat() != preflight.texture_format ||
      native_texture->getBuffer(0U, 0U).get() != preflight.buffer.get() ||
      preflight.buffer->getFormat() != preflight.buffer_format) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.material.texture.preflight",
                   "TUS0 changed after eligibility preflight");
  }
  const std::size_t native_width = preflight.width;
  const std::size_t native_height = preflight.height;
  const std::uint64_t rgba_row_bytes =
      static_cast<std::uint64_t>(native_width) * 4U;
  if (rgba_row_bytes > kMaximumTextureBaseBytes /
                           static_cast<std::uint64_t>(native_height)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.bytes",
                   "TUS0 base allocation exceeds the private demo cap");
  }
  const std::uint64_t rgba_layer_bytes =
      rgba_row_bytes * static_cast<std::uint64_t>(native_height);
  const std::size_t native_state_count = preflight.state_count;
  const Ogre::HardwarePixelBufferSharedPtr buffer = preflight.buffer;
  const Ogre::PixelFormat texture_format = preflight.texture_format;
  const Ogre::PixelFormat native_format = preflight.buffer_format;
  const std::size_t native_element_bytes =
      Ogre::PixelUtil::getNumElemBytes(native_format);
  const std::uint64_t native_row_bytes =
      static_cast<std::uint64_t>(native_width) * native_element_bytes;
  if (native_row_bytes > kMaximumTextureBaseBytes /
                             static_cast<std::uint64_t>(native_height)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.native_bytes",
                   "native TUS0 base allocation exceeds the private demo cap");
  }
  const std::uint64_t native_layer_bytes =
      native_row_bytes * static_cast<std::uint64_t>(native_height);
  if (native_layer_bytes > static_cast<std::uint64_t>(
                               (std::numeric_limits<std::size_t>::max)()) ||
      rgba_layer_bytes > static_cast<std::uint64_t>(
                             (std::numeric_limits<std::size_t>::max)())) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.native_bytes",
                   "TUS0 base allocation exceeds host address space");
  }

  std::vector<std::uint8_t> native_bytes(
      static_cast<std::size_t>(native_layer_bytes));
  Ogre::PixelBox native_destination(
      static_cast<std::uint32_t>(native_width),
      static_cast<std::uint32_t>(native_height), 1U, native_format,
      native_bytes.data());
  if (native_destination.rowPitch != native_width ||
      native_destination.slicePitch != native_width * native_height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.native_pixel_box",
                   "OGRE did not retain a tight native-format destination");
  }

  // Metal readback is requested exactly once in the storage format owned by
  // the native pixel buffer. Channel conversion is deliberately CPU-only.
  buffer->blitToMemory(native_destination);
  const Ogre::HardwarePixelBufferSharedPtr buffer_after =
      native_texture->getBuffer(0U, 0U);
  if (native_texture->getStateCount() != native_state_count ||
      native_texture->getWidth() != native_width ||
      native_texture->getHeight() != native_height ||
      native_texture->getFormat() != texture_format || !buffer_after ||
      buffer_after.get() != buffer.get() ||
      buffer_after->getWidth() != native_width ||
      buffer_after->getHeight() != native_height ||
      buffer_after->getDepth() != 1U ||
      buffer_after->getFormat() != native_format) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.material.texture.revalidation",
                   "TUS0 texture identity, logical format, or base storage changed during readback");
  }

  Render::TextureResourceDescriptor candidate;
  candidate.debug_name = "OgreNextDemoPbrTexture/" +
                         std::string(debug_token);
  candidate.type = Render::TextureResourceType::TEXTURE_2D;
  candidate.format = Render::TextureResourceFormat::RGBA8_UNORM;
  candidate.color_space = Render::TextureColorSpace::SRGB;
  candidate.width = static_cast<std::uint32_t>(native_width);
  candidate.height = static_cast<std::uint32_t>(native_height);
  candidate.array_layers = 1U;
  Render::TextureMipLevelDescriptor base;
  base.width = candidate.width;
  base.height = candidate.height;
  base.row_pitch_bytes = rgba_row_bytes;
  base.layer_pitch_bytes = rgba_layer_bytes;
  base.bytes.resize(static_cast<std::size_t>(rgba_layer_bytes));
  Ogre::PixelBox destination(base.width, base.height, 1U, Ogre::PF_BYTE_RGBA,
                             base.bytes.data());
  if (destination.rowPitch != base.width ||
      destination.slicePitch !=
          static_cast<std::size_t>(base.width) * base.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.pixel_box",
                   "OGRE did not retain a tight PF_BYTE_RGBA destination");
  }
  Ogre::PixelUtil::bulkPixelConversion(native_destination, destination);
  candidate.mip_levels.push_back(std::move(base));
  Render::ValidationResult validation =
      CompleteOgreNextDemoSrgbPbrMipChain(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material." + validation.field;
    return validation;
  }
  validation = Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material.texture." + validation.field;
    return validation;
  }
  output = std::move(candidate);
  output_native_texture_format = texture_format;
  output_native_buffer_format = native_format;
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildSampler(
    const Ogre::Sampler::UVWAddressingMode &native_address,
    std::size_t mip_count, std::string_view debug_token,
    Render::SamplerResourceDescriptor &output) {
  if (mip_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.material.sampler",
                   "projected sampler requires a complete texture");
  }
  Render::SamplerResourceDescriptor candidate;
  candidate.debug_name = "OgreNextDemoPbrSampler/" +
                         std::string(debug_token);
  candidate.minification_filter = Render::SamplerFilter::LINEAR;
  candidate.magnification_filter = Render::SamplerFilter::LINEAR;
  candidate.mip_filter = Render::SamplerFilter::LINEAR;
  if (!MapAddressMode(native_address.u, candidate.address_u) ||
      !MapAddressMode(native_address.v, candidate.address_v) ||
      !MapAddressMode(native_address.w, candidate.address_w)) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.address",
                   "TUS0 uses a nonportable address mode");
  }
  candidate.mip_lod_bias = 0.0F;
  candidate.minimum_lod = 0.0F;
  candidate.maximum_lod = static_cast<float>(mip_count - 1U);
  candidate.anisotropy_enabled = false;
  candidate.maximum_anisotropy = 1.0F;
  candidate.compare_enabled = false;
  candidate.compare_operation = Render::SamplerCompareOperation::ALWAYS;
  candidate.border_color = {};
  Render::ValidationResult validation =
      Render::ValidateSamplerResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material.sampler." + validation.field;
    return validation;
  }
  output = std::move(candidate);
  return Render::ValidationResult::Success();
}

} // namespace

struct MaterialCache final {
  OgreNextDemoIdentityRegistry identities;
  std::map<std::string, CapturedTexture, std::less<>> textures;
  std::map<std::string, CapturedSampler, std::less<>> samplers;
  std::map<std::string, Projection, std::less<>> projections;
  std::map<std::string, ProjectionDecision, std::less<>> decisions;
};

struct OgreNextDemoMaterialSource::State final {
  State() : cache(std::make_shared<MaterialCache>()) {}
  explicit State(std::shared_ptr<MaterialCache> retained_cache)
      : cache(std::move(retained_cache)) {}

  bool capture_open = false;
  std::size_t projection_count_before_capture = 0U;
  std::shared_ptr<MaterialCache> cache;
  std::set<std::string, std::less<>> used_projections;
};

OgreNextDemoMaterialSource::OgreNextDemoMaterialSource()
    : committed_(std::make_unique<State>()) {}

OgreNextDemoMaterialSource::~OgreNextDemoMaterialSource() = default;

bool OgreNextDemoMaterialSource::BeginCapture() noexcept {
  if (pending_ != nullptr || committed_ == nullptr) {
    return false;
  }
  try {
    pending_ = std::make_unique<State>(committed_->cache);
    pending_->capture_open = true;
    pending_->projection_count_before_capture =
        pending_->cache->projections.size();
    pending_->used_projections.clear();
    return true;
  } catch (...) {
    pending_.reset();
    return false;
  }
}

void OgreNextDemoMaterialSource::EnsurePendingCacheWritable() {
  if (pending_ == nullptr || !pending_->capture_open || !pending_->cache) {
    throw std::logic_error("material projection has no writable transaction");
  }
  if (!pending_->cache.unique()) {
    pending_->cache = std::make_shared<MaterialCache>(*pending_->cache);
  }
}

bool OgreNextDemoMaterialSource::TryProjectCurrent(
    const Ogre::MaterialPtr &native_material, bool has_authored_uv0,
    Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
    std::string &selected_projection_key,
    bool allow_new_projection,
    Render::ValidationResult &failure) {
    selected_projection_key.clear();
    failure = Render::ValidationResult::Success();
    if (pending_ == nullptr || !pending_->capture_open || !has_authored_uv0 ||
        !native_material || native_material->getName().empty() ||
        native_material->getNumTechniques() == 0U) {
      return false;
    }
    Ogre::Technique *const technique = native_material->getTechnique(0U);
    Ogre::Pass *const pass =
        technique != nullptr && technique->getNumPasses() != 0U
            ? technique->getPass(0U)
            : nullptr;
    const bool allow_alexis_approximation =
        OgreNextDemoAllowsAlexisTUS0Approximation(
            native_material->getGroup(), native_material->getName());
    if (pass == nullptr ||
        !IsOpaqueReplacePass(*pass, allow_alexis_approximation) ||
        pass->getNumTextureUnitStates() == 0U) {
      return false;
    }
    if (!allow_alexis_approximation &&
        (technique->getNumPasses() != 1U ||
         pass->getNumTextureUnitStates() != 1U ||
         HasAuthoredProgram(*pass))) {
      return false;
    }
    Ogre::TextureUnitState *const unit = pass->getTextureUnitState(0U);
    if (unit == nullptr || unit->getContentType() !=
                               Ogre::TextureUnitState::CONTENT_NAMED ||
        unit->getTextureType() != Ogre::TEX_TYPE_2D ||
        unit->getNumFrames() != 1U || unit->getTextureCoordSet() != 0U ||
        unit->getProjectiveTexturingFrustum() != nullptr ||
        !unit->getEffects().empty() || unit->getUnorderedAccessMipLevel() != -1 ||
        unit->getGamma() != 1.0F || unit->isBlank() ||
        unit->isTextureLoadFailing() ||
        !IsIdentityTextureTransform(unit->getTextureTransform()) ||
        !IsCanonicalModulate(unit->getColourBlendMode(), Ogre::LBT_COLOUR) ||
        !IsCanonicalModulate(unit->getAlphaBlendMode(), Ogre::LBT_ALPHA) ||
        !unit->_getTexturePtr()) {
      return false;
    }
    const Ogre::SamplerPtr native_sampler = unit->getSampler();
    if (!native_sampler || native_sampler->getCompareEnabled()) {
      return false;
    }
    const Ogre::TexturePtr native_texture = unit->_getTexturePtr();
    if (!native_texture || native_texture->getName().empty()) {
      return false;
    }
    if (allow_alexis_approximation &&
        (native_texture->getGroup() != native_material->getGroup() ||
         !IsExactAlexisDiffuseProjection(
             *technique, *pass, native_material->getName(),
             native_texture->getName()))) {
      return false;
    }
    const Ogre::Sampler::UVWAddressingMode native_address =
        native_sampler->getAddressingMode();
    Render::SamplerAddressMode portable_address =
        Render::SamplerAddressMode::REPEAT;
    TextureBasePreflight texture_preflight;
    bool texture_eligible = false;
    Render::ValidationResult texture_preflight_validation =
        PreflightTextureBase(native_texture, texture_preflight,
                             texture_eligible);
    if (!texture_preflight_validation) {
      failure = std::move(texture_preflight_validation);
      return false;
    }
    if (!texture_eligible ||
        !MapAddressMode(native_address.u, portable_address) ||
        !MapAddressMode(native_address.v, portable_address) ||
        !MapAddressMode(native_address.w, portable_address)) {
      return false;
    }

    std::string texture_key;
    AppendField(texture_key, native_texture->getGroup());
    AppendField(texture_key, native_texture->getName());
    std::string sampler_key = texture_key;
    AppendNumber(sampler_key, static_cast<std::uint64_t>(native_address.u));
    AppendNumber(sampler_key, static_cast<std::uint64_t>(native_address.v));
    AppendNumber(sampler_key, static_cast<std::uint64_t>(native_address.w));
    std::string projection_key;
    AppendField(projection_key, native_material->getGroup());
    AppendField(projection_key, native_material->getName());
    AppendField(projection_key, texture_key);
    AppendField(projection_key, sampler_key);
    AppendNumber(projection_key,
                 static_cast<std::uint64_t>(input.cull));

    auto projection = pending_->cache->projections.find(projection_key);
    if (projection == pending_->cache->projections.end()) {
      if (!allow_new_projection) {
        failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                          "ogre_next_demo.material.projection_key",
                          "the frozen projection key changed");
        return false;
      }
      auto texture = pending_->cache->textures.find(texture_key);
      if (texture == pending_->cache->textures.end()) {
        CapturedTexture captured;
        captured.native_texture = native_texture.get();
        Render::ValidationResult validation = DeriveOgreNextDemoSourceId(
            kTextureIdDomain, texture_key, captured.source_id);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        std::string identity(kTextureIdDomain);
        identity.push_back('\0');
        identity.append(texture_key);
        validation = pending_->cache->identities.Register(
            std::move(identity), captured.source_id);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        Render::TextureResourceDescriptor descriptor;
        Ogre::PixelFormat captured_texture_format = Ogre::PF_UNKNOWN;
        Ogre::PixelFormat captured_buffer_format = Ogre::PF_UNKNOWN;
        validation = CaptureTextureBase(native_texture, texture_preflight,
                                        HexId(captured.source_id), descriptor,
                                        captured_texture_format,
                                        captured_buffer_format);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        if (unit->_getTexturePtr().get() != native_texture.get()) {
          failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                            "ogre_next_demo.material.texture_unit",
                            "TUS0 changed texture owner during readback");
          return false;
        }
        const Ogre::HardwarePixelBufferSharedPtr native_base =
            texture_preflight.buffer;
        if (!native_base ||
            native_texture->getFormat() != captured_texture_format ||
            native_base->getFormat() != captured_buffer_format) {
          failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                            "ogre_next_demo.material.texture.buffer",
                            "TUS0 base storage changed after readback");
          return false;
        }
        captured.native_base_buffer = native_base.get();
        captured.native_texture_format = captured_texture_format;
        captured.native_buffer_format = captured_buffer_format;
        captured.native_state_count = native_texture->getStateCount();
        captured.native_width =
            static_cast<std::uint32_t>(native_texture->getWidth());
        captured.native_height =
            static_cast<std::uint32_t>(native_texture->getHeight());
        captured.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(descriptor));
        texture = pending_->cache->textures.emplace(
            texture_key, std::move(captured)).first;
      } else {
        const Ogre::HardwarePixelBufferSharedPtr native_base =
            texture_preflight.buffer;
        if (texture->second.native_texture != native_texture.get() ||
            !native_base ||
            texture->second.native_base_buffer != native_base.get() ||
            texture->second.native_texture_format !=
                native_texture->getFormat() ||
            texture->second.native_buffer_format != native_base->getFormat() ||
            texture->second.native_state_count !=
                native_texture->getStateCount() ||
            texture->second.native_width != native_texture->getWidth() ||
            texture->second.native_height != native_texture->getHeight()) {
          failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                            "ogre_next_demo.material.texture.cache",
                            "cached TUS0 texture authority changed");
          return false;
        }
      }

      auto sampler = pending_->cache->samplers.find(sampler_key);
      if (sampler == pending_->cache->samplers.end()) {
        CapturedSampler captured;
        Render::ValidationResult validation = DeriveOgreNextDemoSourceId(
            kSamplerIdDomain, sampler_key, captured.source_id);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        std::string identity(kSamplerIdDomain);
        identity.push_back('\0');
        identity.append(sampler_key);
        validation = pending_->cache->identities.Register(
            std::move(identity), captured.source_id);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        const auto &texture_descriptor =
            std::get<Render::TextureResourceDescriptor>(
                *texture->second.payload);
        Render::SamplerResourceDescriptor descriptor;
        validation = BuildSampler(native_address,
                                  texture_descriptor.mip_levels.size(),
                                  HexId(captured.source_id), descriptor);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        captured.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(descriptor));
        sampler = pending_->cache->samplers.emplace(
            sampler_key, std::move(captured)).first;
      }

      std::uint64_t token = 0U;
      Render::ValidationResult validation = DeriveOgreNextDemoSourceId(
          kProjectionTokenDomain, projection_key, token);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      std::string token_identity(kProjectionTokenDomain);
      token_identity.push_back('\0');
      token_identity.append(projection_key);
      validation = pending_->cache->identities.Register(
          std::move(token_identity), token);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      Projection captured;
      captured.exact_name = "OpaqueTUS0/" + HexId(token) + "/v1";
      captured.texture_key = texture_key;
      captured.sampler_key = sampler_key;
      captured.native_material = native_material.get();
      captured.native_pass = pass;
      captured.native_unit = unit;
      captured.native_sampler = native_sampler.get();
      captured.native_material_state_count = native_material->getStateCount();
      validation = Render::DeriveOgre14GraphicsSceneMaterialAssetId(
          kMaterialGroup, captured.exact_name,
          captured.material_source_id);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      std::string material_identity(kMaterialGroup);
      material_identity.push_back('\0');
      material_identity.append(captured.exact_name);
      validation = pending_->cache->identities.Register(
          std::move(material_identity), captured.material_source_id);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }

      Render::MaterialDescriptor material;
      material.debug_name = "OgreNextDemoPbrMaterial/" + HexId(token);
      material.model = Render::MaterialModel::PBR_METALLIC_ROUGHNESS;
      material.alpha_mode = Render::MaterialAlphaMode::OPAQUE;
      material.base_color_transfer =
          Render::BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
      material.double_sided =
          input.cull == Render::Ogre14GraphicsSceneMaterialCull::NONE;
      const Ogre::ColourValue native_diffuse = pass->getDiffuse();
      captured.base_color_factor = {
          static_cast<float>(native_diffuse.r),
          static_cast<float>(native_diffuse.g),
          static_cast<float>(native_diffuse.b), 1.0F};
      material.base_color_factor = {
          captured.base_color_factor[0U], captured.base_color_factor[1U],
          captured.base_color_factor[2U], captured.base_color_factor[3U]};
      material.metallic_factor = 0.0F;
      captured.roughness_factor = static_cast<float>(
          std::sqrt(2.0 / (static_cast<double>(pass->getShininess()) + 2.0)));
      material.roughness_factor = captured.roughness_factor;
      const Ogre::ColourValue native_emissive = pass->getSelfIllumination();
      captured.emissive_factor = {
          static_cast<float>(native_emissive.r),
          static_cast<float>(native_emissive.g),
          static_cast<float>(native_emissive.b)};
      material.emissive_factor = {
          captured.emissive_factor[0U], captured.emissive_factor[1U],
          captured.emissive_factor[2U]};
      material.emissive_strength = 1.0F;
      material.base_color_texture.texture_coordinate_set = 0U;
      validation = Render::ValidateMaterialDescriptor(material);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      validation = Render::ValidateMaterialTextureCompatibility(
          Render::MaterialTextureSlot::BASE_COLOR,
          std::get<Render::TextureResourceDescriptor>(*texture->second.payload),
          std::get<Render::SamplerResourceDescriptor>(*sampler->second.payload));
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      captured.material_payload =
          std::make_shared<const Render::RenderAssetPayload>(
              std::move(material));
      projection = pending_->cache->projections.emplace(
          projection_key, std::move(captured)).first;
    } else {
      const auto texture =
          pending_->cache->textures.find(projection->second.texture_key);
      const auto sampler =
          pending_->cache->samplers.find(projection->second.sampler_key);
      const Ogre::HardwarePixelBufferSharedPtr native_base =
          texture_preflight.buffer;
      if (texture == pending_->cache->textures.end() ||
          sampler == pending_->cache->samplers.end() || !native_base ||
          texture->second.native_texture != native_texture.get() ||
          texture->second.native_base_buffer != native_base.get() ||
          texture->second.native_texture_format !=
              native_texture->getFormat() ||
          texture->second.native_buffer_format != native_base->getFormat() ||
          texture->second.native_state_count !=
              native_texture->getStateCount() ||
          texture->second.native_width != native_texture->getWidth() ||
          texture->second.native_height != native_texture->getHeight()) {
        failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                          "ogre_next_demo.material.projection.texture",
                          "projected texture authority changed");
        return false;
      }
      const Ogre::ColourValue native_diffuse = pass->getDiffuse();
      const std::array<float, 4U> base_color_factor{{
          static_cast<float>(native_diffuse.r),
          static_cast<float>(native_diffuse.g),
          static_cast<float>(native_diffuse.b), 1.0F}};
      const float roughness_factor = static_cast<float>(
          std::sqrt(2.0 / (static_cast<double>(pass->getShininess()) + 2.0)));
      const Ogre::ColourValue native_emissive = pass->getSelfIllumination();
      const std::array<float, 3U> emissive_factor{{
          static_cast<float>(native_emissive.r),
          static_cast<float>(native_emissive.g),
          static_cast<float>(native_emissive.b)}};
      if (projection->second.native_material != native_material.get() ||
          projection->second.native_pass != pass ||
          projection->second.native_unit != unit ||
          projection->second.native_sampler != native_sampler.get() ||
          projection->second.native_material_state_count !=
              native_material->getStateCount() ||
          projection->second.base_color_factor != base_color_factor ||
          projection->second.roughness_factor != roughness_factor ||
          projection->second.emissive_factor != emissive_factor) {
        failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                          "ogre_next_demo.material.projection.native",
                          "projected native material authority changed");
        return false;
      }
    }

    pending_->used_projections.insert(projection_key);
    input.exact_resource_group = kMaterialGroup;
    input.exact_name = projection->second.exact_name;
    input.pass_count = 1U;
    input.texture_unit_count = 0U;
    input.has_vertex_program = false;
    input.has_fragment_program = false;
    input.lighting_enabled = true;
    input.diffuse_linear = {1.0F, 1.0F, 1.0F, 1.0F};
    input.ambient_linear = {1.0F, 1.0F, 1.0F};
    input.specular_linear = {};
    input.emissive_linear = {};
    input.shininess = 0.0F;
    input.blend = Render::Ogre14GraphicsSceneMaterialBlend::REPLACE;
    input.alpha_reject =
        Render::Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
    input.alpha_reject_value = 0U;
    if (projection->second.placeholder_payload == nullptr) {
      Render::MaterialDescriptor placeholder;
      Render::ValidationResult placeholder_validation =
          Render::BuildOgre14GraphicsSceneMaterialFallback(input,
                                                           placeholder);
      if (!placeholder_validation) {
        failure = std::move(placeholder_validation);
        return false;
      }
      projection->second.placeholder_payload =
          std::make_shared<const Render::RenderAssetPayload>(
              std::move(placeholder));
    }
    selected_projection_key = projection_key;
    return true;
}

Render::ValidationResult OgreNextDemoMaterialSource::TryProject(
    std::string_view exact_section_key,
    const Ogre::MaterialPtr &native_material,
    bool projection_candidate, bool has_authored_uv0,
    Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
    bool &projected) noexcept {
  projected = false;
  try {
    if (pending_ == nullptr || !pending_->capture_open) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.material.pending",
                     "material projection has no open capture transaction");
    }
    if (exact_section_key.empty() ||
        exact_section_key.find('\0') != std::string_view::npos ||
        !native_material || native_material->getGroup().empty() ||
        native_material->getName().empty()) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.native",
                     "material projection requires one named section and native owner");
    }

    std::string decision_key;
    AppendField(decision_key, exact_section_key);
    const auto decision = pending_->cache->decisions.find(decision_key);
    if (decision != pending_->cache->decisions.end()) {
      if (decision->second.exact_resource_group !=
              native_material->getGroup() ||
          decision->second.exact_material_name !=
              native_material->getName() ||
          decision->second.projection_candidate != projection_candidate ||
          decision->second.has_authored_uv0 != has_authored_uv0 ||
          decision->second.exact_cull != input.cull) {
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.material.decision.identity",
                       "the frozen section changed material, UV, or culling identity");
      }
      if (!decision->second.projection_candidate ||
          !decision->second.projected) {
        return Render::ValidationResult::Success();
      }
      std::string current_projection_key;
      Render::ValidationResult current_failure =
          Render::ValidationResult::Success();
      if (!TryProjectCurrent(native_material, has_authored_uv0, input,
                             current_projection_key, false,
                             current_failure) ||
          current_projection_key != decision->second.projection_key) {
        if (!current_failure) {
          return current_failure;
        }
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.material.decision.projection",
                       "the frozen projected material changed native authority");
      }
      projected = true;
      return Render::ValidationResult::Success();
    }

    EnsurePendingCacheWritable();
    ProjectionDecision new_decision;
    new_decision.exact_resource_group = native_material->getGroup();
    new_decision.exact_material_name = native_material->getName();
    new_decision.exact_cull = input.cull;
    new_decision.projection_candidate = projection_candidate;
    new_decision.has_authored_uv0 = has_authored_uv0;
    if (projection_candidate) {
      Render::ValidationResult current_failure =
          Render::ValidationResult::Success();
      new_decision.projected = TryProjectCurrent(
          native_material, has_authored_uv0, input,
          new_decision.projection_key, true, current_failure);
      if (!current_failure) {
        return current_failure;
      }
    }
    const auto inserted = pending_->cache->decisions.emplace(
        std::move(decision_key), new_decision);
    if (!inserted.second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.decision",
                     "material decision identity collided during capture");
    }
    projected = new_decision.projected;
    return Render::ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.material.decision.allocation",
                   "allocation failed before the material decision was published");
  } catch (...) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.decision.exception",
                   "unexpected failure before the material decision was published");
  }
}

Render::ValidationResult OgreNextDemoMaterialSource::Apply(
    std::vector<Render::GraphicsSceneAssetInput> &assets) noexcept {
  try {
    if (pending_ == nullptr || !pending_->capture_open) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.material.pending",
                     "material projection has no open capture transaction");
    }
    std::vector<Render::GraphicsSceneAssetInput> candidate = assets;
    std::set<std::uint64_t> asset_ids;
    for (const Render::GraphicsSceneAssetInput &asset : candidate) {
      if (asset.source_asset_id == 0U ||
          !asset_ids.insert(asset.source_asset_id).second) {
        return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                       "ogre_next_demo.material.assets",
                       "input asset IDs are zero or duplicated");
      }
    }

    const auto append_dependency =
        [&](std::uint64_t source_asset_id,
            const std::shared_ptr<const Render::RenderAssetPayload> &payload,
            const char *field) -> Render::ValidationResult {
      Render::GraphicsSceneAssetInput dependency;
      dependency.source_asset_id = source_asset_id;
      dependency.payload = payload;
      if (asset_ids.insert(source_asset_id).second) {
        candidate.push_back(std::move(dependency));
        return Render::ValidationResult::Success();
      }
      const auto existing = std::find_if(
          candidate.begin(), candidate.end(),
          [source_asset_id](const auto &asset) {
            return asset.source_asset_id == source_asset_id;
          });
      if (existing == candidate.end() || !existing->payload || !payload ||
          existing->payload->valueless_by_exception() ||
          payload->valueless_by_exception() ||
          !Render::EquivalentRenderAssetPayload(*existing->payload, *payload) ||
          existing->material_bindings != dependency.material_bindings) {
        return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER, field,
                       "projected dependency ID collides with a different input asset");
      }
      return Render::ValidationResult::Success();
    };

    // Dynamic inventory tombstones retain their immutable material owner. Keep
    // every already-published projection and dependency alive for the entire
    // map generation as well; dropping and later recreating those source IDs
    // would be forbidden asset resurrection.
    for (const auto &projection_entry : pending_->cache->projections) {
      const auto projection = pending_->cache->projections.find(
          projection_entry.first);
      const auto texture =
          pending_->cache->textures.find(projection->second.texture_key);
      const auto sampler =
          pending_->cache->samplers.find(projection->second.sampler_key);
      if (texture == pending_->cache->textures.end() ||
          sampler == pending_->cache->samplers.end()) {
        return Failure(Render::ValidationCode::MISSING_REFERENCE,
                       "ogre_next_demo.material.dependencies",
                       "projected texture or sampler disappeared");
      }
      Render::GraphicsSceneAssetInput projected_material;
      projected_material.source_asset_id =
          projection->second.material_source_id;
      projected_material.payload = projection->second.material_payload;
      projected_material.material_bindings[static_cast<std::size_t>(
          Render::MaterialTextureSlot::BASE_COLOR)] = {
          texture->second.source_id, sampler->second.source_id};
      auto material = std::find_if(
          candidate.begin(), candidate.end(), [&](const auto &asset) {
            return asset.source_asset_id ==
                   projection->second.material_source_id;
          });
      if (material == candidate.end()) {
        if (!asset_ids.insert(projected_material.source_asset_id).second) {
          return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                         "ogre_next_demo.material.material_collision",
                         "projected material ID is occupied without an input asset");
        }
        candidate.push_back(std::move(projected_material));
      } else {
        if (!material->payload ||
            !projection->second.placeholder_payload ||
            !projection->second.material_payload ||
            material->payload->valueless_by_exception() ||
            Render::RenderAssetPayloadKind(*material->payload) !=
                Render::RenderAssetKind::MATERIAL) {
          return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                         "ogre_next_demo.material.material_collision",
                         "projected material ID collides with a nonmaterial asset");
        }
        const bool exact_placeholder =
            Render::EquivalentRenderAssetPayload(
                *material->payload,
                *projection->second.placeholder_payload) &&
            material->material_bindings ==
                Render::GraphicsSceneAssetInput{}.material_bindings;
        const bool exact_projected =
            Render::EquivalentRenderAssetPayload(
                *material->payload,
                *projection->second.material_payload) &&
            material->material_bindings ==
                projected_material.material_bindings;
        if (!exact_placeholder && !exact_projected) {
          return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                         "ogre_next_demo.material.material_collision",
                         "projected material ID collides with a different material");
        }
        *material = std::move(projected_material);
      }

      Render::ValidationResult validation = append_dependency(
          texture->second.source_id, texture->second.payload,
          "ogre_next_demo.material.texture_collision");
      if (!validation) {
        return validation;
      }
      validation = append_dependency(
          sampler->second.source_id, sampler->second.payload,
          "ogre_next_demo.material.sampler_collision");
      if (!validation) {
        return validation;
      }
    }
    std::sort(candidate.begin(), candidate.end(),
              [](const auto &first, const auto &second) {
                return first.source_asset_id < second.source_asset_id;
              });
    assets = std::move(candidate);
    return Render::ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.material.allocation",
                   "allocation failed before projected assets were published");
  } catch (...) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.exception",
                   "unexpected failure before projected assets were published");
  }
}

std::size_t OgreNextDemoMaterialSource::NewProjectionCount() const noexcept {
  if (pending_ == nullptr || !pending_->capture_open ||
      pending_->cache->projections.size() <
          pending_->projection_count_before_capture) {
    return 0U;
  }
  return pending_->cache->projections.size() -
         pending_->projection_count_before_capture;
}

std::size_t OgreNextDemoMaterialSource::UsedProjectionCount() const noexcept {
  return pending_ != nullptr && pending_->capture_open
             ? pending_->used_projections.size()
             : 0U;
}

void OgreNextDemoMaterialSource::Commit() noexcept {
  if (pending_ != nullptr && pending_->capture_open) {
    pending_->capture_open = false;
    pending_->used_projections.clear();
    committed_.swap(pending_);
    pending_.reset();
  }
}

void OgreNextDemoMaterialSource::Discard() noexcept { pending_.reset(); }

void OgreNextDemoMaterialSource::Reset() noexcept {
  pending_.reset();
  try {
    committed_ = std::make_unique<State>();
  } catch (...) {
    committed_.reset();
  }
}

} // namespace RoR::Gfx::Detail
