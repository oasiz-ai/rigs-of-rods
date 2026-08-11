/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoMaterialSource.h"

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/render/MaterialDescriptor.h"
#include "gfx/render/Ogre14SourceTextureDecoder.h"
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

// The generic source-image entrypoint is supplied by the renderer-neutral
// decoder milestone. Keeping this declaration local lets this product-wiring
// commit remain independent from that implementation while still compiling
// against the exact shared input/output types. A matching header declaration
// is an ordinary compatible redeclaration once that milestone is composed.
namespace RoR::Render {
[[nodiscard]] ValidationResult DecodeOgre14SourceTexture(
    const std::vector<std::uint8_t> &encoded_source,
    const Ogre14SourceTextureDecodeOptions &options,
    Ogre14DecodedSourceTexture &output,
    IOgre14SourceTextureDecoderFaultInjector *fault_injector);
}

namespace RoR::Gfx::Detail {
namespace {

constexpr char kProjectionTokenDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/Token/v1";
constexpr char kTextureIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/TextureSourceAsset/v1";
constexpr char kSamplerIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/SamplerSourceAsset/v1";
constexpr char kAuthenticatedDecoderPolicy[] =
    "RoR/OgreNextDemo/AuthenticatedSourceDecoder/v1";
constexpr char kMaterialGroup[] = "RoR/OgreNextDemo/ProjectedPbr/v1";
constexpr std::uint32_t kMaximumTextureDimension = 8192U;
constexpr std::uint64_t kMaximumTextureBaseBytes =
    256ULL * 1024ULL * 1024ULL;

constexpr std::uint32_t FourCc(char a, char b, char c, char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

constexpr std::uint32_t kFourCcDxt1 = FourCc('D', 'X', 'T', '1');

struct AuthenticatedTextureProvenance final {
  Render::Ogre14AuthenticatedTextureSourceKind source_kind =
      Render::Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER;
  std::string effective_resource_group;
  std::uint64_t group_generation = 0U;
  std::string archive_identity;
  std::string archive_name;
  std::string archive_type;
  std::string archive_sha256;
  std::string exact_member_name;
  std::string generated_fallback_rule;
  std::uint32_t generated_fallback_rule_version = 0U;
  std::uint64_t byte_count = 0U;
  std::string bytes_sha256;
  std::uint32_t decoder_options_version = 0U;
  std::uint32_t decoded_texture_version = 0U;
  std::uint32_t decoded_mip_version = 0U;
  Render::Ogre14SourceTextureColorSemantic color_semantic =
      Render::Ogre14SourceTextureColorSemantic::UNSPECIFIED;
  Render::Ogre14SourceTextureBc1AlphaMode bc1_alpha_mode =
      Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
};

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

Render::ValidationResult
PreflightTextureIdentity(const Ogre::TexturePtr &native_texture,
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
  if (rgba_row_bytes >
      kMaximumTextureBaseBytes / static_cast<std::uint64_t>(native_height)) {
    return Render::ValidationResult::Success();
  }
  eligible = true;
  return Render::ValidationResult::Success();
}

Render::Ogre14SourceTextureDecodeOptions BuildAuthenticatedDecodeOptions(
    const Render::Ogre14AuthenticatedTextureReceiptMetadata &metadata) {
  Render::Ogre14SourceTextureDecodeOptions options;
  options.color_semantic = Render::Ogre14SourceTextureColorSemantic::SRGB_COLOR;
  options.bc1_alpha_mode =
      metadata.dds.kind == Render::Ogre14SourceDdsHeaderKind::LEGACY &&
              metadata.dds.four_cc == kFourCcDxt1
          ? Render::Ogre14SourceTextureBc1AlphaMode::OPAQUE
          : Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  options.maximum_dimension = kMaximumTextureDimension;
  options.maximum_mip_levels = Render::kOgre14SourceTextureHardMaximumMipLevels;
  options.maximum_encoded_bytes = kMaximumTextureBaseBytes;
  options.maximum_decoded_bytes = kMaximumTextureBaseBytes;
  return options;
}

Render::ValidationResult BuildAuthenticatedTextureProvenance(
    Ogre::Texture &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const Render::Ogre14AuthenticatedTextureResolution &resolution,
    const Render::Ogre14SourceTextureDecodeOptions &options,
    AuthenticatedTextureProvenance &output, std::string &content_decode_key) {
  const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
      resolution.source_receipt();
  const Render::Ogre14AuthenticatedTextureReceiptMetadata *const metadata =
      receipt != nullptr ? receipt->metadata() : nullptr;
  const std::size_t native_state_count = native_texture.getStateCount();
  const std::uint64_t loaded_state_count =
      static_cast<std::uint64_t>(native_state_count);
  if (!resolution.initialized() || receipt == nullptr || metadata == nullptr ||
      !receipt->initialized() || receipt->source_bytes() == nullptr ||
      receipt->source_size() == 0U ||
      static_cast<std::uint64_t>(receipt->source_size()) !=
          metadata->byte_count ||
      metadata->source.effective_resource_group != native_texture.getGroup() ||
      metadata->source.binding.kind !=
          Render::Ogre14AuthenticatedTextureBindingKind::RESOURCE ||
      metadata->source.binding.exact_resource_name !=
          native_texture.getName() ||
      static_cast<std::size_t>(loaded_state_count) != native_state_count ||
      !resolution.MatchesResolver(resolver) ||
      !resolution.MatchesLoadedResourceIdentity(
          reinterpret_cast<std::uintptr_t>(&native_texture),
          static_cast<std::uint64_t>(native_texture.getHandle()),
          native_texture.getGroup(), native_texture.getName(),
          loaded_state_count) ||
      !Render::IsLowercaseOgre14Sha256(metadata->source.archive_sha256) ||
      !Render::IsLowercaseOgre14Sha256(metadata->bytes_sha256)) {
    return Failure(Render::ValidationCode::INVALID_HANDLE,
                   "ogre_next_demo.material.authenticated.provenance",
                   "authenticated source receipt does not own the exact loaded "
                   "texture and bytes");
  }

  AuthenticatedTextureProvenance candidate;
  candidate.source_kind = metadata->source.source_kind;
  candidate.effective_resource_group =
      metadata->source.effective_resource_group;
  candidate.group_generation = metadata->source.group_generation;
  candidate.archive_identity = metadata->source.archive_identity;
  candidate.archive_name = metadata->source.archive_name;
  candidate.archive_type = metadata->source.archive_type;
  candidate.archive_sha256 = metadata->source.archive_sha256;
  candidate.exact_member_name = metadata->source.exact_member_name;
  candidate.generated_fallback_rule = metadata->source.generated_fallback_rule;
  candidate.generated_fallback_rule_version =
      metadata->source.generated_fallback_rule_version;
  candidate.byte_count = metadata->byte_count;
  candidate.bytes_sha256 = metadata->bytes_sha256;
  candidate.decoder_options_version = options.version;
  candidate.decoded_texture_version =
      Render::kOgre14DecodedSourceTextureVersion;
  candidate.decoded_mip_version = Render::kOgre14DecodedSourceTextureMipVersion;
  candidate.color_semantic = options.color_semantic;
  candidate.bc1_alpha_mode = options.bc1_alpha_mode;

  // This key authenticates an immutable decode/cache entry only. It is
  // intentionally never fed into DeriveOgreNextDemoSourceId: public asset IDs
  // remain stable group+name identities, while changed source authority fails
  // the map generation instead of creating a new asset resurrection.
  std::string candidate_key(kAuthenticatedDecoderPolicy);
  AppendNumber(candidate_key,
               static_cast<std::uint64_t>(candidate.source_kind));
  AppendField(candidate_key, candidate.effective_resource_group);
  AppendField(candidate_key, native_texture.getName());
  AppendField(candidate_key, candidate.archive_identity);
  AppendField(candidate_key, candidate.archive_name);
  AppendField(candidate_key, candidate.archive_type);
  AppendField(candidate_key, candidate.archive_sha256);
  AppendField(candidate_key, candidate.exact_member_name);
  AppendField(candidate_key, candidate.generated_fallback_rule);
  AppendNumber(candidate_key, candidate.generated_fallback_rule_version);
  AppendNumber(candidate_key, candidate.byte_count);
  AppendField(candidate_key, candidate.bytes_sha256);
  AppendNumber(candidate_key, candidate.decoder_options_version);
  AppendNumber(candidate_key, candidate.decoded_texture_version);
  AppendNumber(candidate_key, candidate.decoded_mip_version);
  AppendNumber(candidate_key,
               static_cast<std::uint64_t>(candidate.color_semantic));
  AppendNumber(candidate_key,
               static_cast<std::uint64_t>(candidate.bc1_alpha_mode));
  output = std::move(candidate);
  content_decode_key = std::move(candidate_key);
  return Render::ValidationResult::Success();
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

enum class CapturedTextureSource : std::uint8_t {
  AUTHENTICATED_SOURCE_BYTES = 0U,
  UNAUTHENTICATED_GPU_READBACK = 1U,
};

struct CapturedTexture final {
  CapturedTextureSource source =
      CapturedTextureSource::UNAUTHENTICATED_GPU_READBACK;
  Ogre::Texture *native_texture = nullptr;
  const Ogre::HardwarePixelBuffer *native_base_buffer = nullptr;
  Ogre::PixelFormat native_texture_format = Ogre::PF_UNKNOWN;
  Ogre::PixelFormat native_buffer_format = Ogre::PF_UNKNOWN;
  std::size_t native_state_count = 0U;
  std::uint32_t native_width = 0U;
  std::uint32_t native_height = 0U;
  Render::Ogre14AuthenticatedTextureReceipt authenticated_receipt;
  AuthenticatedTextureProvenance authenticated_provenance;
  /// Separate from the stable public source-asset identity. A reload or byte
  /// mutation must reject the frozen map generation, never mint a replacement
  /// asset ID and resurrect its tombstone.
  std::string authenticated_content_decode_key;
  std::uint64_t source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> payload;
};

bool ResolveFrozenAuthenticatedTexture(
    Ogre::Texture &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const CapturedTexture &captured,
    Render::Ogre14AuthenticatedTextureResolution *fresh_output) noexcept {
  try {
    if (captured.source != CapturedTextureSource::AUTHENTICATED_SOURCE_BYTES ||
        !captured.authenticated_receipt.initialized() ||
        captured.authenticated_content_decode_key.empty() ||
        captured.native_texture != nullptr || !native_texture.isLoaded() ||
        captured.native_texture_format != native_texture.getFormat() ||
        captured.native_state_count != native_texture.getStateCount() ||
        captured.native_width != native_texture.getWidth() ||
        captured.native_height != native_texture.getHeight()) {
      return false;
    }
    Render::Ogre14AuthenticatedTextureResolution fresh;
    const Render::ValidationResult resolution =
        resolver.ResolveAuthenticatedTexture(native_texture, fresh);
    const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
        resolution ? fresh.source_receipt() : nullptr;
    const Render::Ogre14AuthenticatedTextureReceiptMetadata *const metadata =
        receipt != nullptr ? receipt->metadata() : nullptr;
    const AuthenticatedTextureProvenance &frozen =
        captured.authenticated_provenance;
    const Render::Ogre14SourceTextureDecodeOptions options =
        metadata != nullptr ? BuildAuthenticatedDecodeOptions(*metadata)
                            : Render::Ogre14SourceTextureDecodeOptions{};
    AuthenticatedTextureProvenance observed;
    std::string observed_key;
    const Render::ValidationResult observation =
        metadata != nullptr
            ? BuildAuthenticatedTextureProvenance(native_texture, resolver,
                                                  fresh, options, observed,
                                                  observed_key)
            : Failure(Render::ValidationCode::MISSING_REFERENCE,
                      "ogre_next_demo.material.authenticated.receipt",
                      "authenticated receipt disappeared");
    const bool matches =
        resolution.ok() && receipt != nullptr && receipt->initialized() &&
        receipt->SharesImmutableStateWith(captured.authenticated_receipt) &&
        observation.ok() &&
        resolver.RevalidateAuthenticatedTexture(native_texture, fresh) &&
        observed_key == captured.authenticated_content_decode_key &&
        observed.source_kind == frozen.source_kind &&
        observed.effective_resource_group == frozen.effective_resource_group &&
        observed.group_generation == frozen.group_generation &&
        observed.archive_identity == frozen.archive_identity &&
        observed.archive_name == frozen.archive_name &&
        observed.archive_type == frozen.archive_type &&
        observed.archive_sha256 == frozen.archive_sha256 &&
        observed.exact_member_name == frozen.exact_member_name &&
        observed.generated_fallback_rule == frozen.generated_fallback_rule &&
        observed.generated_fallback_rule_version ==
            frozen.generated_fallback_rule_version &&
        observed.byte_count == frozen.byte_count &&
        observed.bytes_sha256 == frozen.bytes_sha256 &&
        observed.decoder_options_version == frozen.decoder_options_version &&
        observed.decoded_texture_version == frozen.decoded_texture_version &&
        observed.decoded_mip_version == frozen.decoded_mip_version &&
        observed.color_semantic == frozen.color_semantic &&
        observed.bc1_alpha_mode == frozen.bc1_alpha_mode;
    if (matches && fresh_output != nullptr) {
      *fresh_output = std::move(fresh);
    }
    return matches;
  } catch (...) {
    return false;
  }
}

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

Render::ValidationResult CaptureAuthenticatedTextureSource(
    const Ogre::TexturePtr &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const Render::Ogre14AuthenticatedTextureResolution &resolution,
    std::string_view debug_token, Render::TextureResourceDescriptor &output,
    Render::Ogre14AuthenticatedTextureReceipt &output_receipt,
    AuthenticatedTextureProvenance &output_provenance,
    std::string &output_content_decode_key) {
  if (!native_texture || !resolution.initialized()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.authenticated.resolution",
                   "authenticated source decode requires one exact loaded "
                   "texture resolution");
  }
  const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
      resolution.source_receipt();
  const Render::Ogre14AuthenticatedTextureReceiptMetadata *const metadata =
      receipt != nullptr ? receipt->metadata() : nullptr;
  if (receipt == nullptr || metadata == nullptr ||
      receipt->source_bytes() == nullptr || receipt->source_size() == 0U) {
    return Failure(
        Render::ValidationCode::MISSING_REFERENCE,
        "ogre_next_demo.material.authenticated.receipt",
        "authenticated resolution has no immutable source receipt bytes");
  }

  const Render::Ogre14SourceTextureDecodeOptions options =
      BuildAuthenticatedDecodeOptions(*metadata);
  AuthenticatedTextureProvenance provenance;
  std::string content_decode_key;
  Render::ValidationResult validation = BuildAuthenticatedTextureProvenance(
      *native_texture, resolver, resolution, options, provenance,
      content_decode_key);
  if (!validation) {
    return validation;
  }
  if (receipt->source_size() > options.maximum_encoded_bytes ||
      receipt->source_size() > std::vector<std::uint8_t>{}.max_size()) {
    return Failure(
        Render::ValidationCode::VALUE_OUT_OF_RANGE,
        "ogre_next_demo.material.authenticated.encoded_bytes",
        "authenticated source exceeds the private product decode cap");
  }

  const std::size_t native_state_count = native_texture->getStateCount();
  const std::size_t native_width = native_texture->getWidth();
  const std::size_t native_height = native_texture->getHeight();
  const Ogre::PixelFormat native_format = native_texture->getFormat();
  std::vector<std::uint8_t> encoded_source(receipt->source_bytes(),
                                           receipt->source_bytes() +
                                               receipt->source_size());
  Render::Ogre14DecodedSourceTexture decoded;
  validation = Render::DecodeOgre14SourceTexture(encoded_source, options,
                                                 decoded, nullptr);
  if (!validation) {
    validation.field =
        "ogre_next_demo.material.authenticated." + validation.field;
    return validation;
  }

  Render::TextureResourceDescriptor candidate;
  validation = BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
      std::move(decoded), static_cast<std::uint32_t>(native_width),
      static_cast<std::uint32_t>(native_height),
      "OgreNextDemoPbrTexture/" + std::string(debug_token), candidate);
  if (!validation) {
    return validation;
  }

  // The receipt must remain the exact current ContentManager publication
  // immediately after decode and immediately before the candidate can escape.
  // There is deliberately no native-buffer readback fallback beyond this point.
  if (!native_texture->isLoaded() ||
      native_texture->getStateCount() != native_state_count ||
      native_texture->getWidth() != native_width ||
      native_texture->getHeight() != native_height ||
      native_texture->getFormat() != native_format ||
      !resolver.RevalidateAuthenticatedTexture(*native_texture, resolution)) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.material.authenticated.final_revalidation",
                   "loaded texture or authenticated source authority changed "
                   "during source decode");
  }
  AuthenticatedTextureProvenance final_provenance;
  std::string final_content_decode_key;
  validation = BuildAuthenticatedTextureProvenance(
      *native_texture, resolver, resolution, options, final_provenance,
      final_content_decode_key);
  if (!validation || final_content_decode_key != content_decode_key) {
    return validation
               ? Failure(
                     Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.authenticated.provenance_"
                     "revalidation",
                     "authenticated source provenance changed during decode")
               : validation;
  }

  output = std::move(candidate);
  output_receipt = *receipt;
  output_provenance = std::move(final_provenance);
  output_content_decode_key = std::move(final_content_decode_key);
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

std::size_t SaturatingAdd(std::size_t lhs, std::size_t rhs) noexcept {
  const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
  return lhs > maximum - rhs ? maximum : lhs + rhs;
}

void AccumulateCounters(const OgreNextDemoMaterialSourceCounters &increment,
                        OgreNextDemoMaterialSourceCounters &total) noexcept {
  total.authenticated_source_decodes =
      SaturatingAdd(total.authenticated_source_decodes,
                    increment.authenticated_source_decodes);
  total.authenticated_gpu_readbacks = SaturatingAdd(
      total.authenticated_gpu_readbacks, increment.authenticated_gpu_readbacks);
  total.unauthenticated_gpu_readbacks =
      SaturatingAdd(total.unauthenticated_gpu_readbacks,
                    increment.unauthenticated_gpu_readbacks);
  total.projections = SaturatingAdd(total.projections, increment.projections);
}

} // namespace

struct MaterialCache final {
  OgreNextDemoIdentityRegistry identities;
  std::map<std::string, CapturedTexture, std::less<>> textures;
  std::map<std::string, CapturedSampler, std::less<>> samplers;
  std::map<std::string, Projection, std::less<>> projections;
  std::map<std::string, ProjectionDecision, std::less<>> decisions;
};

struct PendingAuthenticatedTextureObservation final {
  Ogre::TexturePtr native_texture;
  Render::Ogre14AuthenticatedTextureResolution resolution;
};

struct OgreNextDemoMaterialSource::State final {
  State() : cache(std::make_shared<MaterialCache>()) {}
  explicit State(std::shared_ptr<MaterialCache> retained_cache)
      : cache(std::move(retained_cache)) {}

  bool capture_open = false;
  std::size_t projection_count_before_capture = 0U;
  std::shared_ptr<MaterialCache> cache;
  std::set<std::string, std::less<>> used_projections;
  std::map<std::string, PendingAuthenticatedTextureObservation, std::less<>>
      authenticated_texture_observations;
  OgreNextDemoMaterialSourceCounters counters;
};

OgreNextDemoMaterialSource::OgreNextDemoMaterialSource()
    : committed_(std::make_unique<State>()) {}

OgreNextDemoMaterialSource::~OgreNextDemoMaterialSource() = default;

bool OgreNextDemoMaterialSource::BindAuthenticatedTextureAuthority(
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const Render::IOgre14AuthenticatedTextureAuthorityProvider
        &provider) noexcept {
  if (texture_resolver_ == &resolver &&
      texture_authority_provider_ == &provider) {
    return true;
  }
  if (texture_resolver_ != nullptr || texture_authority_provider_ != nullptr ||
      pending_ != nullptr || committed_ == nullptr || !committed_->cache ||
      !committed_->cache->textures.empty() ||
      !committed_->cache->projections.empty() ||
      !committed_->cache->decisions.empty()) {
    return false;
  }
  texture_resolver_ = &resolver;
  texture_authority_provider_ = &provider;
  return true;
}

bool OgreNextDemoMaterialSource::BeginCapture() noexcept {
  if (pending_ != nullptr || committed_ == nullptr ||
      texture_resolver_ == nullptr || texture_authority_provider_ == nullptr) {
    return false;
  }
  try {
    pending_ = std::make_unique<State>(committed_->cache);
    pending_->capture_open = true;
    pending_->projection_count_before_capture =
        pending_->cache->projections.size();
    pending_->used_projections.clear();
    pending_->authenticated_texture_observations.clear();
    pending_->counters = {};
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
    bool texture_eligible = false;
    Render::ValidationResult texture_preflight_validation =
        PreflightTextureIdentity(native_texture, texture_eligible);
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

  const auto record_authenticated_observation =
      [&](const CapturedTexture &captured) -> Render::ValidationResult {
    if (texture_resolver_ == nullptr) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.authenticated.resolver",
                     "authenticated cache has no bound resolver");
    }
    const bool authenticated_source_required =
        texture_resolver_->RequiresAuthenticatedTextureSource(*native_texture);
    Render::Ogre14AuthenticatedTextureResolution fresh;
    const bool immutable_receipt_matches =
        authenticated_source_required &&
        ResolveFrozenAuthenticatedTexture(*native_texture, *texture_resolver_,
                                          captured, &fresh);
    const Render::ValidationResult fresh_result =
        immutable_receipt_matches
            ? Render::ValidationResult::Success()
            : Failure(
                  Render::ValidationCode::REVISION_MISMATCH,
                  "ogre_next_demo.material.authenticated.cache_revalidation",
                  "cached authenticated source provenance or authority "
                  "changed");
    Render::ValidationResult authority =
        ValidateOgreNextDemoCachedTextureSourceAuthority(
            OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES, true,
            authenticated_source_required, authenticated_source_required,
            fresh_result, immutable_receipt_matches);
    if (!authority) {
      return authority;
    }
    PendingAuthenticatedTextureObservation observation;
    observation.native_texture = native_texture;
    observation.resolution = std::move(fresh);
    auto existing =
        pending_->authenticated_texture_observations.find(texture_key);
    if (existing == pending_->authenticated_texture_observations.end()) {
      pending_->authenticated_texture_observations.emplace(
          texture_key, std::move(observation));
    } else {
      if (existing->second.native_texture.get() != native_texture.get()) {
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.material.authenticated.pending_texture",
                       "one capture observed two native textures for the "
                       "frozen source identity");
      }
      existing->second = std::move(observation);
    }
    return Render::ValidationResult::Success();
  };

  const auto revalidate_cached_texture =
      [&](const CapturedTexture &captured) -> Render::ValidationResult {
    if (captured.native_texture_format != native_texture->getFormat() ||
        captured.native_state_count != native_texture->getStateCount() ||
        captured.native_width != native_texture->getWidth() ||
        captured.native_height != native_texture->getHeight()) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.texture.cache",
                     "cached TUS0 loaded-texture identity changed");
    }
    if (captured.source == CapturedTextureSource::AUTHENTICATED_SOURCE_BYTES) {
      return record_authenticated_observation(captured);
    }

    if (captured.native_texture != native_texture.get()) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.texture.cache",
                     "cached unauthenticated native texture pointer changed");
    }
    if (texture_resolver_ == nullptr) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.texture.resolver",
                     "cached texture has no bound source authority");
    }
    Render::ValidationResult source_authority =
        ValidateOgreNextDemoCachedTextureSourceAuthority(
            OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK, true,
            texture_resolver_->RequiresAuthenticatedTextureSource(
                *native_texture),
            false, Render::ValidationResult::Success(), false);
    if (!source_authority) {
      return source_authority;
    }

    TextureBasePreflight current_preflight;
    bool current_eligible = false;
    Render::ValidationResult validation = PreflightTextureBase(
        native_texture, current_preflight, current_eligible);
    if (!validation) {
      return validation;
    }
    if (!current_eligible || !current_preflight.buffer ||
        captured.native_base_buffer != current_preflight.buffer.get() ||
        captured.native_buffer_format !=
            current_preflight.buffer->getFormat()) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.texture.cache",
                     "cached unauthenticated GPU texture authority changed");
    }
    return Render::ValidationResult::Success();
  };

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
      Render::Ogre14AuthenticatedTextureResolution authenticated_resolution;
      const bool authenticated_source_required =
          texture_resolver_->RequiresAuthenticatedTextureSource(
              *native_texture);
      bool resolution_attempted = false;
      Render::ValidationResult resolution_validation =
          Render::ValidationResult::Success();
      if (authenticated_source_required) {
        resolution_attempted = true;
        resolution_validation = texture_resolver_->ResolveAuthenticatedTexture(
            *native_texture, authenticated_resolution);
      }
      OgreNextDemoTextureSourceMode source_mode =
          OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES;
      Render::ValidationResult source_selection =
          SelectOgreNextDemoTextureSourceMode(
              authenticated_source_required, resolution_attempted,
              resolution_validation, source_mode);
      if (!source_selection) {
        source_selection.field = "ogre_next_demo.material.source_selection." +
                                 source_selection.field;
        failure = std::move(source_selection);
        return false;
      }
      const bool authenticated_source =
          source_mode ==
          OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES;

      TextureBasePreflight unauthenticated_preflight;
      if (!authenticated_source) {
        bool unauthenticated_eligible = false;
        Render::ValidationResult validation =
            PreflightTextureBase(native_texture, unauthenticated_preflight,
                                 unauthenticated_eligible);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        if (!unauthenticated_eligible) {
          return false;
        }
      }

        CapturedTexture captured;
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
        if (authenticated_source) {
          captured.source = CapturedTextureSource::AUTHENTICATED_SOURCE_BYTES;
          validation = CaptureAuthenticatedTextureSource(
              native_texture, *texture_resolver_, authenticated_resolution,
              HexId(captured.source_id), descriptor,
              captured.authenticated_receipt,
              captured.authenticated_provenance,
              captured.authenticated_content_decode_key);
          if (!validation) {
            // Successful resolution selected the authenticated path. Decode or
            // authority failure is terminal; native readback is forbidden.
            failure = std::move(validation);
            return false;
          }
          if (unit->_getTexturePtr().get() != native_texture.get()) {
            failure = Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "ogre_next_demo.material.authenticated.texture_unit",
                "TUS0 changed texture owner after authenticated decode");
            return false;
          }
        } else {
          captured.source =
              CapturedTextureSource::UNAUTHENTICATED_GPU_READBACK;
          captured.native_texture = native_texture.get();
          Ogre::PixelFormat captured_texture_format = Ogre::PF_UNKNOWN;
          Ogre::PixelFormat captured_buffer_format = Ogre::PF_UNKNOWN;
          validation = CaptureTextureBase(
              native_texture, unauthenticated_preflight,
              HexId(captured.source_id), descriptor,
              captured_texture_format, captured_buffer_format);
          if (!validation) {
            failure = std::move(validation);
            return false;
          }
          if (unit->_getTexturePtr().get() != native_texture.get()) {
            failure = Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "ogre_next_demo.material.texture_unit",
                "TUS0 changed texture owner during unauthenticated GPU readback");
            return false;
          }
          const Ogre::HardwarePixelBufferSharedPtr native_base =
              unauthenticated_preflight.buffer;
          if (!native_base ||
              native_texture->getFormat() != captured_texture_format ||
              native_base->getFormat() != captured_buffer_format) {
            failure = Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "ogre_next_demo.material.texture.buffer",
                "TUS0 base storage changed after unauthenticated GPU readback");
            return false;
          }
          captured.native_base_buffer = native_base.get();
          captured.native_buffer_format = captured_buffer_format;
          pending_->counters.unauthenticated_gpu_readbacks = SaturatingAdd(
              pending_->counters.unauthenticated_gpu_readbacks, 1U);
        }
        // Both branches freeze the loaded-resource identity; only the
        // unauthenticated branch ever retains a native pixel-buffer pointer.
        captured.native_texture_format = native_texture->getFormat();
        captured.native_state_count = native_texture->getStateCount();
        captured.native_width =
            static_cast<std::uint32_t>(native_texture->getWidth());
        captured.native_height =
            static_cast<std::uint32_t>(native_texture->getHeight());
        if (authenticated_source) {
          validation = record_authenticated_observation(captured);
          if (!validation) {
            failure = std::move(validation);
            return false;
          }
          pending_->counters.authenticated_source_decodes = SaturatingAdd(
              pending_->counters.authenticated_source_decodes, 1U);
        }
        captured.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(descriptor));
        texture = pending_->cache->textures.emplace(
            texture_key, std::move(captured)).first;
      } else {
        Render::ValidationResult cache_validation =
            revalidate_cached_texture(texture->second);
        if (!cache_validation) {
          failure = std::move(cache_validation);
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
      if (texture == pending_->cache->textures.end() ||
          sampler == pending_->cache->samplers.end()) {
        failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                          "ogre_next_demo.material.projection.texture",
                          "projected texture or sampler cache disappeared");
        return false;
      }
      Render::ValidationResult cache_validation =
          revalidate_cached_texture(texture->second);
      if (!cache_validation) {
        failure = std::move(cache_validation);
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

    std::vector<OgreNextDemoCachedProjectionPublicationInput>
        cached_projection_publications;
    std::vector<OgreNextDemoCachedTexturePublicationInput>
        cached_texture_publications;
    std::vector<OgreNextDemoCachedSamplerPublicationInput>
        cached_sampler_publications;
    std::vector<std::string> used_projection_keys;
    cached_projection_publications.reserve(pending_->cache->projections.size());
    cached_texture_publications.reserve(pending_->cache->textures.size());
    cached_sampler_publications.reserve(pending_->cache->samplers.size());
    used_projection_keys.reserve(pending_->used_projections.size());
    for (const auto &projection : pending_->cache->projections) {
      OgreNextDemoCachedProjectionPublicationInput input;
      input.projection_key = projection.first;
      input.texture_key = projection.second.texture_key;
      input.sampler_key = projection.second.sampler_key;
      input.material_source_id = projection.second.material_source_id;
      cached_projection_publications.push_back(std::move(input));
    }
    for (const auto &texture : pending_->cache->textures) {
      OgreNextDemoCachedTexturePublicationInput input;
      input.texture_key = texture.first;
      input.texture_source_id = texture.second.source_id;
      input.source_mode =
          texture.second.source ==
                  CapturedTextureSource::AUTHENTICATED_SOURCE_BYTES
              ? OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES
              : OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK;
      cached_texture_publications.push_back(std::move(input));
    }
    for (const auto &sampler : pending_->cache->samplers) {
      OgreNextDemoCachedSamplerPublicationInput input;
      input.sampler_key = sampler.first;
      input.sampler_source_id = sampler.second.source_id;
      cached_sampler_publications.push_back(std::move(input));
    }
    used_projection_keys.assign(pending_->used_projections.begin(),
                                pending_->used_projections.end());

    class AuthenticatedPublicationBatchValidator final
        : public IOgreNextDemoAuthenticatedTexturePublicationBatchValidator {
    public:
      AuthenticatedPublicationBatchValidator(
          State &pending,
          const Render::IOgre14AuthenticatedTextureResolver *resolver,
          const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider)
          : pending_(pending), resolver_(resolver), provider_(provider) {}

      Render::ValidationResult ValidateReachableAuthenticatedTextureBatch(
          const std::vector<std::string> &texture_keys) override {
        if (texture_keys.empty()) {
          return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                         "authenticated.batch",
                         "empty authenticated publication batch was invoked");
        }
        if (resolver_ == nullptr || provider_ == nullptr) {
          return Failure(
              Render::ValidationCode::MISSING_REFERENCE,
              "authenticated.batch_authority",
              "authenticated publication has no bound resolver/provider pair");
        }

        std::vector<PendingAuthenticatedTextureObservation> observations;
        observations.reserve(texture_keys.size());
        for (const std::string &texture_key : texture_keys) {
          const auto texture = pending_.cache->textures.find(texture_key);
          const auto pending_observation =
              pending_.authenticated_texture_observations.find(texture_key);
          if (texture == pending_.cache->textures.end() ||
              pending_observation ==
                  pending_.authenticated_texture_observations.end() ||
              !pending_observation->second.native_texture) {
            return Failure(Render::ValidationCode::MISSING_REFERENCE,
                           "authenticated.pending_observation",
                           "reachable authenticated texture has no strong "
                           "pending observation");
          }
          Ogre::TexturePtr native_texture =
              pending_observation->second.native_texture;
          Render::Ogre14AuthenticatedTextureResolution fresh;
          if (!resolver_->RequiresAuthenticatedTextureSource(*native_texture) ||
              !ResolveFrozenAuthenticatedTexture(*native_texture, *resolver_,
                                                 texture->second, &fresh)) {
            return Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "authenticated.batch_revalidation",
                "reachable authenticated source changed before publication");
          }
          PendingAuthenticatedTextureObservation observation;
          observation.native_texture = std::move(native_texture);
          observation.resolution = std::move(fresh);
          observations.push_back(std::move(observation));
        }

        Render::Ogre14AuthenticatedTextureAuthoritySnapshot final_authority;
        Render::ValidationResult authority_validation =
            provider_->CaptureAuthenticatedTextureAuthoritySnapshot(
                final_authority);
        if (!authority_validation) {
          return authority_validation;
        }
        for (const PendingAuthenticatedTextureObservation &observation :
             observations) {
          if (!observation.native_texture ||
              !final_authority.Authenticates(observation.resolution) ||
              !resolver_->RevalidateAuthenticatedTexture(
                  *observation.native_texture, observation.resolution)) {
            return Failure(Render::ValidationCode::REVISION_MISMATCH,
                           "authenticated.final_authority",
                           "one common final snapshot did not authenticate the "
                           "complete reachable texture batch");
          }
        }
        return Render::ValidationResult::Success();
      }

    private:
      State &pending_;
      const Render::IOgre14AuthenticatedTextureResolver *resolver_;
      const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider_;
    } batch_validator(*pending_, texture_resolver_,
                      texture_authority_provider_);

    OgreNextDemoCachedProjectionPublicationTransaction publication_transaction;
    Render::ValidationResult publication_validation =
        BuildOgreNextDemoCachedProjectionPublicationTransaction(
            cached_projection_publications, cached_texture_publications,
            cached_sampler_publications, used_projection_keys, batch_validator,
            publication_transaction);
    if (!publication_validation) {
      return publication_validation;
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
    // would be forbidden asset resurrection. Entries not in used_projections
    // are byte-identical, unreachable anti-tombstone owners only: no current
    // instance/environment closure may reference them. Every frame-reachable
    // projection must have entered used_projections through TryProjectCurrent
    // and was authenticated in one common batch above. Full-scene teardown
    // resets this whole cache after empty-scene acceptance. A same-map bundle
    // reload may revoke an unused owner, but any later reachability attempt
    // must first fresh-resolve the exact immutable receipt and will fail closed
    // below.
    for (const OgreNextDemoCachedProjectionPublicationOwner &owner :
         publication_transaction.owner_catalog) {
      const auto projection =
          pending_->cache->projections.find(owner.projection_key);
      if (projection == pending_->cache->projections.end()) {
        return Failure(Render::ValidationCode::MISSING_REFERENCE,
                       "ogre_next_demo.material.dependencies",
                       "publication-plan projection disappeared");
      }
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
    if (pending_->counters.authenticated_gpu_readbacks != 0U) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.material.authenticated_gpu_readbacks",
                     "authenticated texture publication observed a forbidden "
                     "GPU readback");
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

OgreNextDemoMaterialSourceCounters
OgreNextDemoMaterialSource::CurrentCaptureCounters() const noexcept {
  if (pending_ == nullptr || !pending_->capture_open) {
    return {};
  }
  OgreNextDemoMaterialSourceCounters counters = pending_->counters;
  counters.projections = pending_->used_projections.size();
  return counters;
}

OgreNextDemoMaterialSourceCounters
OgreNextDemoMaterialSource::LifetimeCounters() const noexcept {
  return lifetime_counters_;
}

void OgreNextDemoMaterialSource::Commit() noexcept {
  if (pending_ != nullptr && pending_->capture_open) {
    pending_->counters.projections = pending_->used_projections.size();
    AccumulateCounters(pending_->counters, lifetime_counters_);
    pending_->capture_open = false;
    pending_->used_projections.clear();
    pending_->authenticated_texture_observations.clear();
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
