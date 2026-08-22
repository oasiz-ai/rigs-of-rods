/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoMaterialSource.h"

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.h"
#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.h"
#include "gfx/ogre14/Ogre14SelectedTextureSource.h"
#include "gfx/render/MaterialDescriptor.h"
#include "gfx/render/Ogre14SourceTextureDecoder.h"
#include "gfx/render/RenderAssetRegistry.h"
#include "gfx/render/RenderResourceDescriptors.h"
#include "resources/LegacyMaterialScriptSanitizer.h"

#include <OgreBuildSettings.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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

static_assert(
    OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
        OGRE_VERSION_PATCH == 2,
    "the disposable OgreNext material source is pinned to OGRE 14.5.2");
static_assert(
    sizeof(Ogre::Real) == sizeof(float),
    "the disposable OgreNext material source requires binary32 Ogre::Real");

namespace RoR::Gfx::Detail {
namespace {

constexpr char kProjectionTokenDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/Token/v2";
constexpr char kTextureIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/TextureSourceAsset/v1";
constexpr char kSamplerIdDomain[] =
    "RoR/OgreNextDemo/ProjectedPbr/SamplerSourceAsset/v1";
constexpr char kManagedSpecularTextureIdDomain[] =
    "RoR/OgreNextDemo/ManagedSpecular/TextureSourceAsset/v1";
constexpr char kCuratedCityWorldSpecularTextureIdDomain[] =
    "RoR/OgreNextDemo/CuratedCityWorldAsia/LinearSpecularTextureSourceAsset/v1";
constexpr char kAuthenticatedDecoderPolicy[] =
    "RoR/OgreNextDemo/AuthenticatedSourceDecoder/v2";
constexpr char kOrdinarySelectedDecoderPolicy[] =
    "RoR/OgreNextDemo/OrdinarySelectedSourceDecoder/v2";
constexpr char kModernSourceNormalizationPolicy[] =
    "RoR/OgreNextDemo/ModernSourceNormalization/SrgbOpaqueMipPrefix/v2";
constexpr char kMaterialGroup[] = "RoR/OgreNextDemo/ProjectedPbr/v2";
constexpr char kLossyMaterialNormalizationPolicy[] =
    "RoR/OgreNextDemo/LegacyFixedFunctionToPbrNormalization/v2";
constexpr char kManagedSpecularPbrLoweringPolicy[] =
    "RoR/OgreNextDemo/ManagedSpecularPbrLowering/"
    "LinearRgbSpecularWorkflowDielectricIor1p5F0p04NoMetallicSynthesis/v1";
constexpr char kCuratedCityWorldPbrLoweringPolicy[] =
    "RoR/OgreNextDemo/CuratedCityWorldAsia/ReviewedSpecularWorkflow/v1";
constexpr char kAlexisAuthoredRoughnessPolicy[] =
    "RoR/OgreNextDemo/AlexisAuthoredBodyPaint/ReviewedRoughness/v1";
constexpr char kOgreNextDemoAdditiveEquivalentGlowOverlayPolicy[] =
    "RoR/OgreNextDemo/LegacyOverlay/AdditiveEquivalentGlowAuthoredTexelProof/"
    "v1";
constexpr std::uint32_t kMaximumTextureDimension = 8192U;
constexpr std::uint64_t kMaximumTextureBaseBytes = 256ULL * 1024ULL * 1024ULL;

constexpr std::uint32_t FourCc(char a, char b, char c, char d) noexcept {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24U);
}

constexpr std::uint32_t kFourCcDxt1 = FourCc('D', 'X', 'T', '1');

struct AuthenticatedTextureProvenance final {
  Render::Ogre14AuthenticatedTextureSourceKind source_kind = Render::
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER;
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

struct OrdinaryTextureProvenance final {
  std::string effective_resource_group;
  std::uint64_t group_generation = 0U;
  std::string selected_archive_name;
  std::string selected_archive_type;
  std::uintptr_t selected_archive_pointer_token = 0U;
  std::uintptr_t file_info_archive_pointer_token = 0U;
  std::string exact_member_name;
  std::uint64_t file_info_compressed_size = 0U;
  std::uint64_t file_info_uncompressed_size = 0U;
  std::uintptr_t opened_stream_pointer_token = 0U;
  std::string opened_stream_name;
  std::uint64_t opened_stream_size = 0U;
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

Render::ValidationResult Failure(Render::ValidationCode code, const char *field,
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

void AppendFloatBits(std::string &key, float value) {
  std::uint32_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  AppendNumber(key, bits);
}

void AppendDigest(std::string &key,
                  const Render::RenderPayloadDigest &digest) {
  AppendField(key, std::string_view(
                       reinterpret_cast<const char *>(digest.data()),
                       digest.size()));
}

std::array<float, 4U>
ObserveColourComponents(const Ogre::ColourValue &colour) noexcept {
  return {static_cast<float>(colour.r), static_cast<float>(colour.g),
          static_cast<float>(colour.b), static_cast<float>(colour.a)};
}

Render::ValidationResult
ObserveExactTexture(const Ogre::TextureUnitState &unit,
                    const Ogre::Texture &texture,
                    OgreNextDemoExactTextureObservation &output) {
  if (texture.getNumMipmaps() == (std::numeric_limits<std::uint32_t>::max)()) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.material.texture.native_mip_count",
                   "native additional mip count cannot include a base level");
  }
  OgreNextDemoExactTextureObservation candidate;
  candidate.texture_unit_gamma = static_cast<float>(unit.getGamma());
  candidate.texture_gamma = texture.getGamma();
  candidate.texture_unit_hardware_gamma = unit.isHardwareGammaEnabled();
  candidate.texture_hardware_gamma = texture.isHardwareGammaEnabled();
  candidate.additional_mip_count = texture.getNumMipmaps();
  candidate.actual_mip_count = candidate.additional_mip_count + 1U;
  candidate.mipmaps_hardware_generated = texture.getMipmapsHardwareGenerated();
  candidate.usage_token = static_cast<std::uint32_t>(texture.getUsage());
  candidate.source_width = texture.getSrcWidth();
  candidate.source_height = texture.getSrcHeight();
  candidate.source_depth = texture.getSrcDepth();
  candidate.source_format_token =
      static_cast<std::uint32_t>(texture.getSrcFormat());
  candidate.output_width = texture.getWidth();
  candidate.output_height = texture.getHeight();
  candidate.output_depth = texture.getDepth();
  candidate.output_format_token =
      static_cast<std::uint32_t>(texture.getFormat());
  candidate.face_count = texture.getNumFaces();
  candidate.texture_type_token =
      static_cast<std::uint32_t>(texture.getTextureType());
  Render::ValidationResult validation =
      ValidateOgreNextDemoExactTextureObservation(candidate);
  if (!validation) {
    return validation;
  }
  output = candidate;
  return Render::ValidationResult::Success();
}

void AppendExactTextureObservation(
    std::string &key, const OgreNextDemoExactTextureObservation &observation) {
  AppendField(key, kModernSourceNormalizationPolicy);
  AppendNumber(key, kOgreNextDemoModernSourceNormalizationPolicyVersion);
  AppendFloatBits(key, observation.texture_unit_gamma);
  AppendFloatBits(key, observation.texture_gamma);
  AppendNumber(key, observation.texture_unit_hardware_gamma ? 1U : 0U);
  AppendNumber(key, observation.texture_hardware_gamma ? 1U : 0U);
  AppendNumber(key, observation.additional_mip_count);
  AppendNumber(key, observation.actual_mip_count);
  AppendNumber(key, observation.mipmaps_hardware_generated ? 1U : 0U);
  AppendNumber(key, observation.usage_token);
  AppendNumber(key, observation.source_width);
  AppendNumber(key, observation.source_height);
  AppendNumber(key, observation.source_depth);
  AppendNumber(key, observation.source_format_token);
  AppendNumber(key, observation.output_width);
  AppendNumber(key, observation.output_height);
  AppendNumber(key, observation.output_depth);
  AppendNumber(key, observation.output_format_token);
  AppendNumber(key, observation.face_count);
  AppendNumber(key, observation.texture_type_token);
}

Render::ValidationResult
PreflightTextureIdentity(const Ogre::TexturePtr &native_texture,
                         OgreNextDemoTextureProjectionExclusion &exclusion) {
  OgreNextDemoTextureEligibilityObservation observation;
  observation.source_available = native_texture && native_texture->isLoaded() &&
                                 !native_texture->getName().empty();
  if (native_texture) {
    observation.manually_loaded = native_texture->isManuallyLoaded();
    observation.render_target =
        (native_texture->getUsage() & Ogre::TU_RENDERTARGET) != 0U;
    observation.cube_texture =
        native_texture->getTextureType() == Ogre::TEX_TYPE_CUBE_MAP;
    observation.volume_texture =
        native_texture->getTextureType() == Ogre::TEX_TYPE_3D;
    observation.texture_2d =
        native_texture->getTextureType() == Ogre::TEX_TYPE_2D;
    observation.unit_depth = native_texture->getDepth() == 1U;
    observation.unit_face_count = native_texture->getNumFaces() == 1U;
    const std::size_t native_width = native_texture->getWidth();
    const std::size_t native_height = native_texture->getHeight();
    const std::size_t source_width = native_texture->getSrcWidth();
    const std::size_t source_height = native_texture->getSrcHeight();
    const bool dimensions_fit =
        native_width != 0U && native_height != 0U && source_width != 0U &&
        source_height != 0U && native_width <= kMaximumTextureDimension &&
        native_height <= kMaximumTextureDimension &&
        source_width <= kMaximumTextureDimension &&
        source_height <= kMaximumTextureDimension &&
        native_width <= (std::numeric_limits<std::uint32_t>::max)() &&
        native_height <= (std::numeric_limits<std::uint32_t>::max)() &&
        source_width <= (std::numeric_limits<std::uint32_t>::max)() &&
        source_height <= (std::numeric_limits<std::uint32_t>::max)();
    observation.dimensions_in_range =
        dimensions_fit &&
        static_cast<std::uint64_t>(native_width) * 4U <=
            kMaximumTextureBaseBytes /
                static_cast<std::uint64_t>(native_height) &&
        static_cast<std::uint64_t>(source_width) * 4U <=
            kMaximumTextureBaseBytes /
                static_cast<std::uint64_t>(source_height);
  }
  return ClassifyOgreNextDemoTextureProjectionEligibility(observation,
                                                          exclusion);
}

Render::Ogre14SourceTextureDecodeOptions BuildAuthenticatedDecodeOptions(
    const Render::Ogre14AuthenticatedTextureReceiptMetadata &metadata,
    OgreNextDemoTextureAlphaPolicy alpha_policy) {
  Render::Ogre14SourceTextureDecodeOptions options;
  options.color_semantic = Render::Ogre14SourceTextureColorSemantic::SRGB_COLOR;
  const bool legacy_dxt1 =
      metadata.dds.kind == Render::Ogre14SourceDdsHeaderKind::LEGACY &&
      metadata.dds.four_cc == kFourCcDxt1;
  if (!ResolveOgreNextDemoBc1AlphaMode(legacy_dxt1, alpha_policy, false,
                                      options.bc1_alpha_mode)) {
    options.bc1_alpha_mode =
        Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  }
  options.maximum_dimension = kMaximumTextureDimension;
  options.maximum_mip_levels = Render::kOgre14SourceTextureHardMaximumMipLevels;
  options.maximum_encoded_bytes = kMaximumTextureBaseBytes;
  options.maximum_decoded_bytes = kMaximumTextureBaseBytes;
  return options;
}

bool MapAuthenticatedSourceMode(
    Render::Ogre14AuthenticatedTextureSourceKind source_kind,
    OgreNextDemoTextureSourceMode &mode) noexcept {
  switch (source_kind) {
  case Render::Ogre14AuthenticatedTextureSourceKind::
      AUTHENTICATED_ARCHIVE_MEMBER:
    mode = OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES;
    return true;
  case Render::Ogre14AuthenticatedTextureSourceKind::
      VERSIONED_GENERATED_FALLBACK:
    mode = OgreNextDemoTextureSourceMode::AUTHENTICATED_GENERATED_SOURCE_BYTES;
    return true;
  default:
    return false;
  }
}

std::uint32_t ReadLittleEndianU32(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0U]) |
         (static_cast<std::uint32_t>(bytes[1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3U]) << 24U);
}

bool IsLegacyDxt1Source(const std::uint8_t *bytes,
                        std::size_t size) noexcept {
  return bytes != nullptr && size >= 88U && bytes[0U] == 'D' &&
         bytes[1U] == 'D' && bytes[2U] == 'S' && bytes[3U] == ' ' &&
         ReadLittleEndianU32(bytes + 84U) == kFourCcDxt1;
}

Render::Ogre14SourceTextureDecodeOptions BuildOrdinaryDecodeOptions(
    const Render::Ogre14SelectedTextureSourceReceipt &receipt,
    OgreNextDemoTextureAlphaPolicy alpha_policy) {
  Render::Ogre14SourceTextureDecodeOptions options;
  options.color_semantic = Render::Ogre14SourceTextureColorSemantic::SRGB_COLOR;
  const std::uint8_t *const bytes = receipt.source_bytes();
  const std::size_t size = receipt.source_size();
  // DDS magic is four bytes and DDS_HEADER::ddspf.dwFourCC is at byte 84.
  // This only selects the mandatory BC1 interpretation; the decoder still
  // validates the complete container and rejects every malformed header.
  const bool legacy_dxt1 = IsLegacyDxt1Source(bytes, size);
  if (!ResolveOgreNextDemoBc1AlphaMode(legacy_dxt1, alpha_policy, false,
                                      options.bc1_alpha_mode)) {
    options.bc1_alpha_mode =
        Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  }
  options.maximum_dimension = kMaximumTextureDimension;
  options.maximum_mip_levels = Render::kOgre14SourceTextureHardMaximumMipLevels;
  options.maximum_encoded_bytes = kMaximumTextureBaseBytes;
  options.maximum_decoded_bytes = kMaximumTextureBaseBytes;
  return options;
}

Render::Ogre14SourceTextureDecodeOptions BuildManagedDecodeOptions(
    const Render::ManagedMaterialTextureSourceReceipt &receipt,
    Render::Ogre14SourceTextureColorSemantic color_semantic,
    OgreNextDemoTextureAlphaPolicy alpha_policy) {
  Render::Ogre14SourceTextureDecodeOptions options;
  options.color_semantic = color_semantic;
  const std::uint8_t *const bytes = receipt.source_bytes();
  const std::size_t size = receipt.source_size();
  const bool legacy_dxt1 = IsLegacyDxt1Source(bytes, size);
  if (!ResolveOgreNextDemoBc1AlphaMode(legacy_dxt1, alpha_policy, false,
                                      options.bc1_alpha_mode)) {
    options.bc1_alpha_mode =
        Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  }
  options.maximum_dimension = kMaximumTextureDimension;
  options.maximum_mip_levels = Render::kOgre14SourceTextureHardMaximumMipLevels;
  options.maximum_encoded_bytes = kMaximumTextureBaseBytes;
  options.maximum_decoded_bytes = kMaximumTextureBaseBytes;
  return options;
}

bool ManagedReceiptMatchesNativeTexture(
    const Render::ManagedMaterialTextureSourceReceipt &receipt,
    const Ogre::Texture &native_texture) noexcept {
  const Render::ManagedMaterialTextureSourceIdentity *const identity =
      receipt.identity();
  return receipt.initialized() && identity != nullptr &&
         receipt.source_bytes() != nullptr && receipt.source_size() != 0U &&
         identity->byte_count == receipt.source_size() &&
         identity->effective_resource_group == native_texture.getGroup() &&
         identity->exact_resource_name == native_texture.getName();
}

bool ManagedReceiptBytesEqual(const Render::ManagedMaterialTextureSourceReceipt &managed,
                              const std::uint8_t *bytes,
                              std::size_t size) noexcept {
  return managed.initialized() && managed.source_bytes() != nullptr &&
         bytes != nullptr && managed.source_size() == size &&
         std::equal(managed.source_bytes(), managed.source_bytes() + size,
                    bytes);
}

bool ManagedReceiptOwnsNativeTexture(
    const Render::ManagedMaterialTextureSourceReceipt &managed,
    Ogre::Texture &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const Render::IOgre14SelectedTextureSourceResolver &selected_resolver)
    noexcept {
  try {
    if (!ManagedReceiptMatchesNativeTexture(managed, native_texture)) {
      return false;
    }
    if (authenticated_resolver.RequiresAuthenticatedTextureSource(
            native_texture)) {
      Render::Ogre14AuthenticatedTextureResolution resolution;
      const Render::ValidationResult result =
          authenticated_resolver.ResolveAuthenticatedTexture(native_texture,
                                                               resolution);
      const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
          result ? resolution.source_receipt() : nullptr;
      return receipt != nullptr && receipt->initialized() &&
             ManagedReceiptBytesEqual(managed, receipt->source_bytes(),
                                      receipt->source_size()) &&
             authenticated_resolver.RevalidateAuthenticatedTexture(
                 native_texture, resolution);
    }
    Render::Ogre14SelectedTextureSourceResolution resolution;
    const Render::ValidationResult result =
        selected_resolver.ResolveSelectedTextureSource(native_texture,
                                                       resolution);
    const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
        result ? resolution.source_receipt() : nullptr;
    return receipt != nullptr && receipt->initialized() &&
           ManagedReceiptBytesEqual(managed, receipt->source_bytes(),
                                    receipt->source_size()) &&
           selected_resolver.RevalidateSelectedTextureSource(native_texture,
                                                              resolution);
  } catch (...) {
    return false;
  }
}

Render::ValidationResult BuildAuthenticatedTextureProvenance(
    Ogre::Texture &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const Render::Ogre14AuthenticatedTextureResolution &resolution,
    const Render::Ogre14SourceTextureDecodeOptions &options,
    const OgreNextDemoExactTextureObservation &exact_texture_observation,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
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
  AppendExactTextureObservation(candidate_key, exact_texture_observation);
  // Opaque retains the exact v2 key prefix; only the new straight-alpha path
  // appends its separate domain, including for PNG/JPEG.
  if (alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    AppendField(candidate_key, kOgreNextDemoStraightAlphaNormalizationPolicy);
    AppendNumber(candidate_key,
                 kOgreNextDemoStraightAlphaNormalizationPolicyVersion);
  }
  output = std::move(candidate);
  content_decode_key = std::move(candidate_key);
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOrdinaryTextureProvenance(
    Ogre::Texture &native_texture,
    const Render::IOgre14SelectedTextureSourceResolver &resolver,
    const Render::Ogre14SelectedTextureSourceResolution &resolution,
    const Render::Ogre14SourceTextureDecodeOptions &options,
    const OgreNextDemoExactTextureObservation &exact_texture_observation,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
    OrdinaryTextureProvenance &output, std::string &content_decode_key) {
  const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
      resolution.source_receipt();
  const Render::Ogre14SelectedTextureSourceReceiptMetadata *const metadata =
      receipt != nullptr ? receipt->metadata() : nullptr;
  const std::size_t native_state_count = native_texture.getStateCount();
  if (!resolution.initialized() || receipt == nullptr || metadata == nullptr ||
      !receipt->initialized() || receipt->source_bytes() == nullptr ||
      receipt->source_size() == 0U ||
      static_cast<std::uint64_t>(receipt->source_size()) !=
          metadata->byte_count ||
      metadata->source.source_kind !=
          Render::Ogre14SelectedTextureSourceKind::
              UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER ||
      metadata->source.effective_resource_group != native_texture.getGroup() ||
      metadata->source.resource_pointer_token !=
          reinterpret_cast<std::uintptr_t>(&native_texture) ||
      metadata->source.resource_handle !=
          static_cast<std::uint64_t>(native_texture.getHandle()) ||
      metadata->source.exact_resource_name != native_texture.getName() ||
      !resolution.MatchesResolver(resolver) ||
      !resolution.MatchesLoadedResourceIdentity(
          reinterpret_cast<std::uintptr_t>(&native_texture),
          static_cast<std::uint64_t>(native_texture.getHandle()),
          native_texture.getGroup(), native_texture.getName(),
          static_cast<std::uint64_t>(native_state_count)) ||
      !Render::IsLowercaseOgre14Sha256(metadata->observed_bytes_sha256)) {
    return Failure(Render::ValidationCode::INVALID_HANDLE,
                   "ogre_next_demo.material.ordinary.provenance",
                   "ordinary selected-source receipt does not own the exact "
                   "loaded texture and bytes");
  }

  OrdinaryTextureProvenance candidate;
  candidate.effective_resource_group =
      metadata->source.effective_resource_group;
  candidate.group_generation = metadata->source.group_generation;
  candidate.selected_archive_name = metadata->source.selected_archive_name;
  candidate.selected_archive_type = metadata->source.selected_archive_type;
  candidate.selected_archive_pointer_token =
      metadata->source.selected_archive_pointer_token;
  candidate.file_info_archive_pointer_token =
      metadata->source.file_info_archive_pointer_token;
  candidate.exact_member_name = metadata->source.exact_member_name;
  candidate.file_info_compressed_size =
      metadata->source.file_info_compressed_size;
  candidate.file_info_uncompressed_size =
      metadata->source.file_info_uncompressed_size;
  candidate.opened_stream_pointer_token =
      metadata->source.opened_stream_pointer_token;
  candidate.opened_stream_name = metadata->source.opened_stream_name;
  candidate.opened_stream_size = metadata->source.opened_stream_size;
  candidate.byte_count = metadata->byte_count;
  candidate.bytes_sha256 = metadata->observed_bytes_sha256;
  candidate.decoder_options_version = options.version;
  candidate.decoded_texture_version =
      Render::kOgre14DecodedSourceTextureVersion;
  candidate.decoded_mip_version = Render::kOgre14DecodedSourceTextureMipVersion;
  candidate.color_semantic = options.color_semantic;
  candidate.bc1_alpha_mode = options.bc1_alpha_mode;

  std::string candidate_key(kOrdinarySelectedDecoderPolicy);
  AppendField(candidate_key, candidate.effective_resource_group);
  AppendField(candidate_key, native_texture.getName());
  AppendNumber(candidate_key, candidate.group_generation);
  AppendField(candidate_key, candidate.selected_archive_name);
  AppendField(candidate_key, candidate.selected_archive_type);
  AppendNumber(candidate_key, candidate.selected_archive_pointer_token);
  AppendNumber(candidate_key, candidate.file_info_archive_pointer_token);
  AppendField(candidate_key, candidate.exact_member_name);
  AppendNumber(candidate_key, candidate.file_info_compressed_size);
  AppendNumber(candidate_key, candidate.file_info_uncompressed_size);
  AppendNumber(candidate_key, candidate.opened_stream_pointer_token);
  AppendField(candidate_key, candidate.opened_stream_name);
  AppendNumber(candidate_key, candidate.opened_stream_size);
  AppendNumber(candidate_key, candidate.byte_count);
  AppendField(candidate_key, candidate.bytes_sha256);
  AppendNumber(candidate_key, candidate.decoder_options_version);
  AppendNumber(candidate_key, candidate.decoded_texture_version);
  AppendNumber(candidate_key, candidate.decoded_mip_version);
  AppendNumber(candidate_key,
               static_cast<std::uint64_t>(candidate.color_semantic));
  AppendNumber(candidate_key,
               static_cast<std::uint64_t>(candidate.bc1_alpha_mode));
  AppendExactTextureObservation(candidate_key, exact_texture_observation);
  if (alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    AppendField(candidate_key, kOgreNextDemoStraightAlphaNormalizationPolicy);
    AppendNumber(candidate_key,
                 kOgreNextDemoStraightAlphaNormalizationPolicyVersion);
  }
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

OgreNextDemoExactSamplerObservation
ObserveExactSampler(const Ogre::Sampler &sampler) noexcept;

bool IsCanonicalTextureUnitSemantic(
    const Ogre::TextureUnitState &unit) noexcept {
  return unit.getNumFrames() == 1U && unit.getTextureCoordSet() == 0U &&
         unit.getProjectiveTexturingFrustum() == nullptr &&
         unit.getEffects().empty() && unit.getUnorderedAccessMipLevel() == -1 &&
         IsIdentityTextureTransform(unit.getTextureTransform()) &&
         IsCanonicalModulate(unit.getColourBlendMode(), Ogre::LBT_COLOUR) &&
         IsCanonicalModulate(unit.getAlphaBlendMode(), Ogre::LBT_ALPHA);
}

bool IsExactCuratedCityWorldSpecularUnit(
    const Ogre::TextureUnitState &unit) noexcept {
  const Ogre::LayerBlendModeEx &colour = unit.getColourBlendMode();
  return unit.getNumFrames() == 1U && unit.getTextureCoordSet() == 0U &&
         unit.getProjectiveTexturingFrustum() == nullptr &&
         unit.getEffects().empty() && unit.getUnorderedAccessMipLevel() == -1 &&
         IsIdentityTextureTransform(unit.getTextureTransform()) &&
         colour.blendType == Ogre::LBT_COLOUR &&
         colour.operation == Ogre::LBX_BLEND_TEXTURE_ALPHA &&
         colour.source1 == Ogre::LBS_TEXTURE &&
         colour.source2 == Ogre::LBS_CURRENT &&
         IsCanonicalModulate(unit.getAlphaBlendMode(), Ogre::LBT_ALPHA);
}

bool IsExactCuratedCityWorldSphericalEnvironmentUnit(
    const Ogre::TextureUnitState &unit) noexcept {
  const Ogre::LayerBlendModeEx &colour = unit.getColourBlendMode();
  const Ogre::TextureUnitState::EffectMap &effects = unit.getEffects();
  if (unit.getNumFrames() != 1U || unit.getTextureCoordSet() != 0U ||
      unit.getProjectiveTexturingFrustum() != nullptr ||
      unit.getUnorderedAccessMipLevel() != -1 ||
      !IsIdentityTextureTransform(unit.getTextureTransform()) ||
      colour.blendType != Ogre::LBT_COLOUR ||
      colour.operation != Ogre::LBX_BLEND_CURRENT_ALPHA ||
      colour.source1 != Ogre::LBS_TEXTURE ||
      colour.source2 != Ogre::LBS_CURRENT ||
      !IsCanonicalModulate(unit.getAlphaBlendMode(), Ogre::LBT_ALPHA) ||
      effects.size() != 1U) {
    return false;
  }
  const auto effect = effects.begin();
  return effect->first == Ogre::TextureUnitState::ET_ENVIRONMENT_MAP &&
         effect->second.type == Ogre::TextureUnitState::ET_ENVIRONMENT_MAP &&
         effect->second.subtype == Ogre::TextureUnitState::ENV_CURVED;
}

/// Bounded classification of one trailing (index >= 1) texture unit of a
/// single-pass legacy material.
///
/// Legacy CityWorld overwhelmingly declares a canonical diffuse on unit 0 and
/// then one or two further layers - an alpha-blended specular map and/or an
/// environment reflection. The projection presents unit 0 only, so none of
/// these layers contributes a single texel and the authenticated-source
/// contract is untouched: they are structurally observed and counted, never
/// sampled. Anything outside this bounded set keeps the material matte.
enum class LegacyUnpresentedLayerKind : std::uint8_t {
  UNSUPPORTED = 0U,
  SPECULAR_ALPHA_BLEND = 1U,
  ENVIRONMENT_REFLECTION = 2U,
};

bool IsLegacyLayerColourBlend(const Ogre::LayerBlendModeEx &colour) noexcept {
  return colour.blendType == Ogre::LBT_COLOUR &&
         colour.source1 == Ogre::LBS_TEXTURE &&
         colour.source2 == Ogre::LBS_CURRENT &&
         (colour.operation == Ogre::LBX_MODULATE ||
          colour.operation == Ogre::LBX_BLEND_CURRENT_ALPHA ||
          colour.operation == Ogre::LBX_BLEND_MANUAL);
}

LegacyUnpresentedLayerKind ClassifyLegacyUnpresentedLayer(
    const Ogre::TextureUnitState &unit) noexcept {
  try {
    if (unit.getNumFrames() != 1U || unit.getTextureCoordSet() != 0U ||
        unit.getProjectiveTexturingFrustum() != nullptr ||
        unit.getUnorderedAccessMipLevel() != -1 ||
        !IsIdentityTextureTransform(unit.getTextureTransform()) ||
        !IsCanonicalModulate(unit.getAlphaBlendMode(), Ogre::LBT_ALPHA)) {
      return LegacyUnpresentedLayerKind::UNSUPPORTED;
    }
    if (IsExactCuratedCityWorldSpecularUnit(unit)) {
      return LegacyUnpresentedLayerKind::SPECULAR_ALPHA_BLEND;
    }
    const Ogre::TextureUnitState::EffectMap &effects = unit.getEffects();
    if (effects.size() != 1U || !IsLegacyLayerColourBlend(
                                    unit.getColourBlendMode())) {
      return LegacyUnpresentedLayerKind::UNSUPPORTED;
    }
    const auto effect = effects.begin();
    // An OGRE environment-map subtype names the same thing for this
    // projection whichever spelling it uses: a trailing layer whose texels
    // come from the reflection probe, not from an authored source file. The
    // projection presents unit 0 only, so the subtype selects a UV derivation
    // this code never evaluates - it cannot change the presented result, only
    // the name of what is being withheld.
    //
    // ENV_REFLECTION is admitted here for a second, stronger reason. The one
    // shape that reaches it is self-inflicted: LegacyMaterialScriptSanitizer
    // rewrites `cubic_texture EnvironmentTexture combinedUVW` / `env_map
    // planar` into `texture EnvironmentTexture cubic ...` / `env_map
    // cubic_reflection` so the declaration matches the cube RTT main.cpp
    // creates. That rewrite turned an ENV_PLANAR layer this predicate already
    // accepted into an ENV_REFLECTION layer it refused, which is a refusal of
    // our own edit rather than of anything the author wrote. Refusing the
    // rewritten spelling while accepting the original one is not a policy, it
    // is a gap.
    //
    // And `EnvironmentTexture` carries nothing to withhold in this runtime:
    // the combined session force-disables the environment-map RTT
    // (gfx_envmap_enabled=false, gfx_envmap_rate=0 in main.cpp) because the
    // hidden producer's envmap passes cost 15.4 ms/frame rendering into
    // textures the presenter never reads. The cube is created and never
    // written, so the authored sample would read undefined contents. Omitting
    // this layer is therefore not an approximation of a reflection - there is
    // no reflection in this runtime to approximate.
    if (effect->first != Ogre::TextureUnitState::ET_ENVIRONMENT_MAP ||
        effect->second.type !=
            Ogre::TextureUnitState::ET_ENVIRONMENT_MAP ||
        // ENV_NORMAL is deliberately still refused: no shipping section
        // declares it, so admitting it would widen the gate past anything this
        // corpus exercises.
        (effect->second.subtype != Ogre::TextureUnitState::ENV_CURVED &&
         effect->second.subtype != Ogre::TextureUnitState::ENV_PLANAR &&
         effect->second.subtype != Ogre::TextureUnitState::ENV_REFLECTION)) {
      return LegacyUnpresentedLayerKind::UNSUPPORTED;
    }
    return LegacyUnpresentedLayerKind::ENVIRONMENT_REFLECTION;
  } catch (...) {
    return LegacyUnpresentedLayerKind::UNSUPPORTED;
  }
}

/// Bounded classification of one trailing (index >= 1) pass of a legacy
/// multi-pass technique.
///
/// The shipping CityWorld multi-pass population is exactly two shapes. An
/// `ADDITIVE` overlay declares `scene_blend add` with an add operation and no
/// raster-state override: whatever it draws is summed onto what pass 0 already
/// wrote, so pass 0's colour is a strict lower bound on the authored result and
/// omitting the overlay can only make the surface dimmer, never a different
/// colour. A `DESTINATION_MODIFYING` overlay - an alpha-blended lit decal, or a
/// modulate darkening layer - replaces or scales that colour instead, so
/// presenting pass 0 alone would show colour the author deliberately covered.
/// Only the first bound is safe to trade, and anything outside both shapes is
/// not classified at all.
enum class LegacyOverlayPassKind : std::uint8_t {
  UNSUPPORTED = 0U,
  ADDITIVE = 1U,
  DESTINATION_MODIFYING = 2U,
  /// A `scene_blend alpha_blend` overlay whose *declared* shape is the
  /// CityWorld glow shape: alpha-rejected, exactly one canonical texture unit,
  /// and a self-illumination term. Only the declaration has been checked here;
  /// the authored texels still have to prove the additive equivalence before
  /// this may be traded for pass 0's base colour.
  ADDITIVE_EQUIVALENT_GLOW_CANDIDATE = 3U,
};

/// Contract for the authored-source proof behind
/// `kOgreNextDemoAdmitsAdditiveEquivalentGlowOverlayPasses`. Kept abstract so
/// the structural predicate stays a free function while the decode, the
/// selected-source receipts, and their per-capture revalidation stay owned by
/// the material source.
class LegacyGlowOverlayContentVerifier {
public:
  virtual ~LegacyGlowOverlayContentVerifier() = default;
  [[nodiscard]] virtual bool VerifyAdditiveEquivalentGlowOverlay(
      const Ogre::Pass &base_pass, const Ogre::Pass &overlay_pass) noexcept = 0;
};

LegacyOverlayPassKind ClassifyLegacyOverlayPass(
    const Ogre::Pass &pass) noexcept;

bool IsRtssGeneratedTechnique(const Ogre::Technique &technique) noexcept {
  try {
    return technique.getSchemeName() == Ogre::MSN_SHADERGEN &&
           technique.getUserObjectBindings()
               .getUserAny("SGTechnique")
               .has_value();
  } catch (...) {
    return false;
  }
}

bool FindCuratedCityWorldSourceTechnique(
    const Ogre::MaterialPtr &material, Ogre::Technique *&output) noexcept {
  try {
    if (!material || material->getNumTechniques() == 0U) {
      return false;
    }
    Ogre::Technique *source = nullptr;
    for (unsigned short index = 0U; index < material->getNumTechniques();
         ++index) {
      Ogre::Technique *const technique = material->getTechnique(index);
      if (technique == nullptr) {
        return false;
      }
      const bool has_rtss_marker =
          technique->getUserObjectBindings()
              .getUserAny("SGTechnique")
              .has_value();
      if (technique->getSchemeName() == Ogre::MSN_DEFAULT &&
          !has_rtss_marker) {
        // Ogre RTSS derives and appends destination techniques from the
        // authored source. Downstream material closure code is deliberately
        // pinned to technique zero, so require that exact topology instead of
        // accepting a reordered clone as source authority.
        if (index != 0U || source != nullptr) {
          return false;
        }
        source = technique;
        continue;
      }
      if (!IsRtssGeneratedTechnique(*technique)) {
        return false;
      }
    }
    if (source == nullptr) {
      return false;
    }
    output = source;
    return true;
  } catch (...) {
    return false;
  }
}

bool HasCuratedCityWorldSphericalFamilyShape(
    const Ogre::MaterialPtr &material) noexcept {
  try {
    Ogre::Technique *technique = nullptr;
    if (!FindCuratedCityWorldSourceTechnique(material, technique)) {
      return false;
    }
    Ogre::Pass *const pass =
        technique != nullptr && technique->getNumPasses() == 1U
            ? technique->getPass(0U)
            : nullptr;
    if (pass == nullptr || pass->getNumTextureUnitStates() != 3U) {
      return false;
    }
    Ogre::TextureUnitState *const base = pass->getTextureUnitState(0U);
    Ogre::TextureUnitState *const specular = pass->getTextureUnitState(1U);
    Ogre::TextureUnitState *const environment = pass->getTextureUnitState(2U);
    return base != nullptr && specular != nullptr && environment != nullptr &&
           IsCanonicalTextureUnitSemantic(*base) &&
           IsExactCuratedCityWorldSpecularUnit(*specular) &&
           IsExactCuratedCityWorldSphericalEnvironmentUnit(*environment);
  } catch (...) {
    return false;
  }
}

bool IsExactManagedSpecularTextureUnitSemantic(
    const Ogre::TextureUnitState &unit) noexcept {
  const Ogre::LayerBlendModeEx &colour = unit.getColourBlendMode();
  const Ogre::LayerBlendModeEx &alpha = unit.getAlphaBlendMode();
  return unit.getNumFrames() == 1U && unit.getTextureCoordSet() == 0U &&
         unit.getProjectiveTexturingFrustum() == nullptr &&
         unit.getEffects().empty() && unit.getUnorderedAccessMipLevel() == -1 &&
         IsIdentityTextureTransform(unit.getTextureTransform()) &&
         colour.blendType == Ogre::LBT_COLOUR &&
         colour.operation == Ogre::LBX_SOURCE2 &&
         colour.source1 == Ogre::LBS_TEXTURE &&
         colour.source2 == Ogre::LBS_TEXTURE &&
         alpha.blendType == Ogre::LBT_ALPHA &&
         alpha.operation == Ogre::LBX_SOURCE1 &&
         alpha.source1 == Ogre::LBS_TEXTURE &&
         alpha.source2 == Ogre::LBS_TEXTURE;
}

bool IsExactManagedEnvironmentUnit(
    const Ogre::TextureUnitState &unit) noexcept {
  const Ogre::TextureUnitState::EffectMap &effects = unit.getEffects();
  const Ogre::SamplerPtr sampler = unit.getSampler();
  if (unit.getName() != "envmap" ||
      unit.getTextureName() != "EnvironmentTexture" ||
      unit.getTextureType() != Ogre::TEX_TYPE_CUBE_MAP ||
      unit.getNumFrames() != 1U || unit.getTextureCoordSet() != 0U ||
      unit.getProjectiveTexturingFrustum() != nullptr ||
      unit.getUnorderedAccessMipLevel() != -1 ||
      !IsIdentityTextureTransform(unit.getTextureTransform()) ||
      !IsCanonicalModulate(unit.getColourBlendMode(), Ogre::LBT_COLOUR) ||
      !IsCanonicalModulate(unit.getAlphaBlendMode(), Ogre::LBT_ALPHA) ||
      effects.size() != 1U || !sampler) {
    return false;
  }
  const auto effect = effects.begin();
  const OgreNextDemoExactSamplerObservation sampler_observation =
      ObserveExactSampler(*sampler);
  return effect->first == Ogre::TextureUnitState::ET_ENVIRONMENT_MAP &&
         effect->second.type == Ogre::TextureUnitState::ET_ENVIRONMENT_MAP &&
         effect->second.subtype == Ogre::TextureUnitState::ENV_REFLECTION &&
         sampler_observation.address_u ==
             OgreNextDemoObservedSamplerAddressMode::WRAP &&
         sampler_observation.address_v ==
             OgreNextDemoObservedSamplerAddressMode::WRAP &&
         sampler_observation.address_w ==
             OgreNextDemoObservedSamplerAddressMode::WRAP;
}

bool HasAvailableNamedTextureSource(
    const Ogre::TextureUnitState &unit) noexcept {
  return unit.getContentType() == Ogre::TextureUnitState::CONTENT_NAMED &&
         !unit.isBlank() && !unit.isTextureLoadFailing();
}

bool HasAuthoredProgram(const Ogre::Pass &pass) noexcept {
  return pass.hasVertexProgram() || pass.hasFragmentProgram() ||
         pass.hasGeometryProgram() || pass.hasTessellationHullProgram() ||
         pass.hasTessellationDomainProgram() || pass.hasComputeProgram();
}

/// True when a trailing overlay pass would shade an identical texel to exactly
/// the same colour pass 0 would, plus some non-negative self-illumination.
///
/// Both passes run the same fixed-function lighting equation, so equal
/// material colours, equal shininess, equal vertex-colour tracking and equal
/// lighting state make the lit term a pure function of the sampled texel. The
/// self-illumination is the only term allowed to differ, and only upward: that
/// difference is precisely the "added light" the additive bound permits us to
/// drop. Alpha is deliberately not compared - the overlay's alpha selects
/// which fragments survive and is verified against the authored texels
/// instead, while pass 0's alpha is governed by its own canonical blend.
bool HasAddedLightOnlyShadingResponse(const Ogre::Pass &base,
                                      const Ogre::Pass &overlay) noexcept {
  const Ogre::ColourValue base_emissive = base.getSelfIllumination();
  const Ogre::ColourValue overlay_emissive = overlay.getSelfIllumination();
  return base.getDiffuse() == overlay.getDiffuse() &&
         base.getAmbient() == overlay.getAmbient() &&
         base.getSpecular() == overlay.getSpecular() &&
         base.getShininess() == overlay.getShininess() &&
         base.getVertexColourTracking() ==
             overlay.getVertexColourTracking() &&
         base.getLightingEnabled() == overlay.getLightingEnabled() &&
         overlay_emissive.r >= base_emissive.r &&
         overlay_emissive.g >= base_emissive.g &&
         overlay_emissive.b >= base_emissive.b;
}

/// Single structural gate for an ordinary (uncurated, non-Alexis) legacy
/// material. No GPU program, pass 0 unit 0 as base colour, every further unit a
/// recognised legacy layer, and every further pass a recognised additive
/// overlay - all of which are observed and counted but never presented. Both
/// the admission decision and its later revalidation call this so the two can
/// never drift apart.
///
/// `glow_verifier` supplies the authored-source proof for an alpha-blended
/// overlay that only *declares* the additive-equivalent glow shape. Admission
/// always passes one. Revalidation passes null and re-checks the declaration
/// only: a projection exists solely because admission already discharged the
/// texel proof, and the proof is bound to immutable authored source bytes
/// whose selected-source receipts the verifier revalidates on every reuse.
bool HasAdmissibleLegacyShape(
    const Ogre::Technique &technique, const Ogre::Pass &pass,
    std::size_t &unpresented_layer_units,
    std::size_t &unpresented_additive_overlay_passes,
    OgreNextDemoTextureProjectionExclusion &exclusion,
    LegacyGlowOverlayContentVerifier *glow_verifier = nullptr) noexcept {
  unpresented_layer_units = 0U;
  unpresented_additive_overlay_passes = 0U;
  if (HasAuthoredProgram(pass)) {
    exclusion = OgreNextDemoTextureProjectionExclusion::
        MATERIAL_AUTHORED_PROGRAM_UNSUPPORTED;
    return false;
  }
  const std::size_t pass_count =
      static_cast<std::size_t>(technique.getNumPasses());
  if (pass_count == 0U) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
    return false;
  }
  if (pass_count > kOgreNextDemoMaximumLegacyTechniquePasses) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_MULTI_PASS_UNSUPPORTED;
    return false;
  }
  // A trailing pass carries visible contribution this projection does not
  // present. Classify what it actually is: an additive overlay bounds the loss
  // to "dimmer than authored" and may be traded for pass 0's base colour under
  // an explicit policy constant, while a destination-modifying overlay would
  // let pass 0 show colour the author covered and is refused under its own
  // name. Anything else is not classified and keeps the generic refusal.
  std::size_t additive_overlay_passes = 0U;
  for (std::size_t index = 1U; index < pass_count; ++index) {
    const Ogre::Pass *const overlay =
        const_cast<Ogre::Technique &>(technique).getPass(
            static_cast<unsigned short>(index));
    const LegacyOverlayPassKind kind =
        overlay == nullptr ? LegacyOverlayPassKind::UNSUPPORTED
                           : ClassifyLegacyOverlayPass(*overlay);
    if (kind == LegacyOverlayPassKind::DESTINATION_MODIFYING) {
      exclusion = OgreNextDemoTextureProjectionExclusion::
          MATERIAL_BLENDED_OVERLAY_PASS_UNSUPPORTED;
      return false;
    }
    if (kind == LegacyOverlayPassKind::ADDITIVE_EQUIVALENT_GLOW_CANDIDATE) {
      // The declaration alone never earns the additive bound. Require that the
      // overlay could only ever add light to an identical texel, then require
      // the authored texels themselves to prove they ARE identical wherever
      // the overlay's own alpha rejection keeps them. A candidate that fails
      // either clause is exactly what the destination-modifying refusal is
      // for: presenting pass 0 alone would show colour the author covered.
      if (!HasAddedLightOnlyShadingResponse(pass, *overlay) ||
          (glow_verifier != nullptr &&
           !glow_verifier->VerifyAdditiveEquivalentGlowOverlay(pass,
                                                               *overlay))) {
        exclusion = OgreNextDemoTextureProjectionExclusion::
            MATERIAL_BLENDED_OVERLAY_PASS_UNSUPPORTED;
        return false;
      }
      ++additive_overlay_passes;
      continue;
    }
    if (kind != LegacyOverlayPassKind::ADDITIVE) {
      exclusion = OgreNextDemoTextureProjectionExclusion::
          MATERIAL_MULTI_PASS_UNSUPPORTED;
      return false;
    }
    ++additive_overlay_passes;
  }
  if (additive_overlay_passes != 0U) {
    if constexpr (!kOgreNextDemoAdmitsLegacyAdditiveOverlayPasses) {
      exclusion = OgreNextDemoTextureProjectionExclusion::
          MATERIAL_ADDITIVE_OVERLAY_PASS_UNSUPPORTED;
      return false;
    }
    if constexpr (!kOgreNextDemoAdmitsAlphaTestedLegacyAdditiveOverlayMaterials) {
      if (pass.getAlphaRejectFunction() != Ogre::CMPF_ALWAYS_PASS) {
        exclusion = OgreNextDemoTextureProjectionExclusion::
            MATERIAL_ALPHA_TESTED_OVERLAY_PASS_UNSUPPORTED;
        return false;
      }
    }
    unpresented_additive_overlay_passes = additive_overlay_passes;
  }
  const std::size_t unit_count =
      static_cast<std::size_t>(pass.getNumTextureUnitStates());
  if (unit_count == 0U) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
    return false;
  }
  if (unit_count > kOgreNextDemoMaximumLegacyLayeredTextureUnits) {
    exclusion = OgreNextDemoTextureProjectionExclusion::
        MATERIAL_TEXTURE_UNIT_LAYER_UNSUPPORTED;
    return false;
  }
  for (std::size_t index = 1U; index < unit_count; ++index) {
    const Ogre::TextureUnitState *const layer =
        const_cast<Ogre::Pass &>(pass).getTextureUnitState(
            static_cast<unsigned short>(index));
    if (layer == nullptr ||
        ClassifyLegacyUnpresentedLayer(*layer) ==
            LegacyUnpresentedLayerKind::UNSUPPORTED) {
      exclusion = OgreNextDemoTextureProjectionExclusion::
          MATERIAL_TEXTURE_UNIT_LAYER_UNSUPPORTED;
      return false;
    }
  }
  unpresented_layer_units = unit_count - 1U;
  return true;
}

struct ExactPassObservation final {
  std::array<float, 4U> diffuse{};
  std::array<float, 4U> ambient{};
  std::array<float, 4U> specular{};
  std::array<float, 4U> emissive{};
  float shininess = 0.0F;
  std::uint8_t source_color = 0U;
  std::uint8_t destination_color = 0U;
  std::uint8_t source_alpha = 0U;
  std::uint8_t destination_alpha = 0U;
  std::uint8_t color_operation = 0U;
  std::uint8_t alpha_operation = 0U;
  std::uint8_t alpha_reject = 0U;
  std::uint8_t alpha_reject_value = 0U;
  std::uint8_t depth_function = 0U;
  std::uint8_t cull_mode = 0U;
  std::uint8_t manual_cull_mode = 0U;
  std::uint8_t polygon_mode = 0U;
  std::uint8_t vertex_colour_tracking = 0U;
  std::uint64_t pass_iteration_count = 0U;
  float depth_bias_constant = 0.0F;
  float depth_bias_slope_scale = 0.0F;
  float iteration_depth_bias = 0.0F;
  bool write_red = false;
  bool write_green = false;
  bool write_blue = false;
  bool write_alpha = false;
  bool lighting_enabled = false;
  bool alpha_to_coverage = false;
  bool depth_check = false;
  bool depth_write = false;
  bool polygon_mode_overrideable = false;
  bool fog_override = false;
  bool iterate_per_light = false;
};

void AppendExactPassObservation(std::string &key,
                                const ExactPassObservation &observation);

ExactPassObservation ObserveExactPass(const Ogre::Pass &pass) noexcept {
  ExactPassObservation observation;
  observation.diffuse = ObserveColourComponents(pass.getDiffuse());
  observation.ambient = ObserveColourComponents(pass.getAmbient());
  observation.specular = ObserveColourComponents(pass.getSpecular());
  observation.emissive =
      ObserveColourComponents(pass.getSelfIllumination());
  observation.shininess = static_cast<float>(pass.getShininess());
  observation.source_color =
      static_cast<std::uint8_t>(pass.getSourceBlendFactor());
  observation.destination_color =
      static_cast<std::uint8_t>(pass.getDestBlendFactor());
  observation.source_alpha =
      static_cast<std::uint8_t>(pass.getSourceBlendFactorAlpha());
  observation.destination_alpha =
      static_cast<std::uint8_t>(pass.getDestBlendFactorAlpha());
  observation.color_operation =
      static_cast<std::uint8_t>(pass.getSceneBlendingOperation());
  observation.alpha_operation =
      static_cast<std::uint8_t>(pass.getSceneBlendingOperationAlpha());
  observation.alpha_reject =
      static_cast<std::uint8_t>(pass.getAlphaRejectFunction());
  observation.alpha_reject_value = pass.getAlphaRejectValue();
  observation.depth_function =
      static_cast<std::uint8_t>(pass.getDepthFunction());
  observation.cull_mode = static_cast<std::uint8_t>(pass.getCullingMode());
  observation.manual_cull_mode =
      static_cast<std::uint8_t>(pass.getManualCullingMode());
  observation.polygon_mode =
      static_cast<std::uint8_t>(pass.getPolygonMode());
  observation.vertex_colour_tracking =
      static_cast<std::uint8_t>(pass.getVertexColourTracking());
  observation.pass_iteration_count =
      static_cast<std::uint64_t>(pass.getPassIterationCount());
  observation.depth_bias_constant = pass.getDepthBiasConstant();
  observation.depth_bias_slope_scale = pass.getDepthBiasSlopeScale();
  observation.iteration_depth_bias = pass.getIterationDepthBias();
  pass.getColourWriteEnabled(observation.write_red, observation.write_green,
                             observation.write_blue,
                             observation.write_alpha);
  observation.lighting_enabled = pass.getLightingEnabled();
  observation.alpha_to_coverage = pass.isAlphaToCoverageEnabled();
  observation.depth_check = pass.getDepthCheckEnabled();
  observation.depth_write = pass.getDepthWriteEnabled();
  observation.polygon_mode_overrideable = pass.getPolygonModeOverrideable();
  observation.fog_override = pass.getFogOverride();
  observation.iterate_per_light = pass.getIteratePerLight();
  return observation;
}

LegacyOverlayPassKind ClassifyLegacyOverlayPass(
    const Ogre::Pass &pass) noexcept {
  try {
    if (HasAuthoredProgram(pass) ||
        pass.getNumTextureUnitStates() >
            kOgreNextDemoMaximumLegacyLayeredTextureUnits) {
      return LegacyOverlayPassKind::UNSUPPORTED;
    }
    const ExactPassObservation observation = ObserveExactPass(pass);
    // Shared floor for either overlay shape. Everything here is state that
    // would let a trailing pass affect the frame beyond compositing its own
    // fragments onto pass 0's - a depth-bias offset, a masked colour write, a
    // raster mode override, per-light or multi-iteration replay, or a fog
    // override - and is refused rather than reasoned about.
    if (observation.alpha_reject != Ogre::CMPF_ALWAYS_PASS &&
        observation.alpha_reject != Ogre::CMPF_GREATER &&
        observation.alpha_reject != Ogre::CMPF_GREATER_EQUAL) {
      return LegacyOverlayPassKind::UNSUPPORTED;
    }
    if (!observation.write_red || !observation.write_green ||
        !observation.write_blue || !observation.write_alpha ||
        observation.alpha_to_coverage || !observation.depth_check ||
        observation.depth_function != Ogre::CMPF_LESS_EQUAL ||
        observation.depth_bias_constant != 0.0F ||
        observation.depth_bias_slope_scale != 0.0F ||
        observation.iteration_depth_bias != 0.0F ||
        observation.polygon_mode != Ogre::PM_SOLID ||
        !observation.polygon_mode_overrideable || observation.fog_override ||
        observation.pass_iteration_count != 1U ||
        observation.iterate_per_light) {
      return LegacyOverlayPassKind::UNSUPPORTED;
    }
    if (observation.color_operation != Ogre::SBO_ADD ||
        observation.alpha_operation != Ogre::SBO_ADD) {
      return LegacyOverlayPassKind::UNSUPPORTED;
    }
    // `scene_blend add`: destination = destination + source, on colour and on
    // alpha. This is the only shape whose omission is bounded.
    if (observation.source_color == Ogre::SBF_ONE &&
        observation.destination_color == Ogre::SBF_ONE &&
        observation.source_alpha == Ogre::SBF_ONE &&
        observation.destination_alpha == Ogre::SBF_ONE) {
      return LegacyOverlayPassKind::ADDITIVE;
    }
    // The two destination-modifying legacy overlay blends: `scene_blend
    // alpha_blend` (a lit decal, which CityWorld declares on ~60 facade
    // materials) and `scene_blend zero src_colour` (a modulate darkening
    // layer). Naming them separates a deliberate refusal from an unrecognised
    // topology in the live census.
    const bool alpha_blend_overlay =
        observation.source_color == Ogre::SBF_SOURCE_ALPHA &&
        observation.destination_color == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
        observation.source_alpha == Ogre::SBF_SOURCE_ALPHA &&
        observation.destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    // Both spellings of a modulate darkening layer: OGRE's `scene_blend
    // modulate` shorthand and the explicit `scene_blend zero src_colour` the
    // NeoQ2.0 ground material declares.
    const bool modulate_overlay =
        (observation.source_color == Ogre::SBF_ZERO &&
         observation.destination_color == Ogre::SBF_SOURCE_COLOUR &&
         observation.source_alpha == Ogre::SBF_ZERO &&
         observation.destination_alpha == Ogre::SBF_SOURCE_COLOUR) ||
        (observation.source_color == Ogre::SBF_DEST_COLOUR &&
         observation.destination_color == Ogre::SBF_ZERO &&
         observation.source_alpha == Ogre::SBF_DEST_COLOUR &&
         observation.destination_alpha == Ogre::SBF_ZERO);
    if (alpha_blend_overlay) {
      // Separate the declared CityWorld glow shape from every other
      // alpha-blended decal. A glow overlay rejects its own transparent
      // texels rather than compositing them, carries exactly one canonical
      // UV0 texture unit (a second unit is how the `ventanas` sky-reflection
      // decals are built), declares self-illumination it means to add, and
      // leaves lighting and vertex colour alone. Everything that survives is
      // still only a candidate: `HasAdmissibleLegacyShape` must compare it
      // against pass 0 and then prove the texels.
      Ogre::Pass &mutable_overlay = const_cast<Ogre::Pass &>(pass);
      const Ogre::TextureUnitState *const overlay_unit =
          mutable_overlay.getNumTextureUnitStates() == 1U
              ? mutable_overlay.getTextureUnitState(0U)
              : nullptr;
      const bool declares_added_light =
          observation.emissive[0U] > 0.0F || observation.emissive[1U] > 0.0F ||
          observation.emissive[2U] > 0.0F;
      if (kOgreNextDemoAdmitsAdditiveEquivalentGlowOverlayPasses &&
          observation.alpha_reject != Ogre::CMPF_ALWAYS_PASS &&
          overlay_unit != nullptr && declares_added_light &&
          observation.lighting_enabled &&
          observation.vertex_colour_tracking == Ogre::TVC_NONE &&
          observation.depth_check &&
          IsCanonicalTextureUnitSemantic(*overlay_unit) &&
          HasAvailableNamedTextureSource(*overlay_unit)) {
        return LegacyOverlayPassKind::ADDITIVE_EQUIVALENT_GLOW_CANDIDATE;
      }
      return LegacyOverlayPassKind::DESTINATION_MODIFYING;
    }
    if (modulate_overlay) {
      return LegacyOverlayPassKind::DESTINATION_MODIFYING;
    }
    return LegacyOverlayPassKind::UNSUPPORTED;
  } catch (...) {
    return LegacyOverlayPassKind::UNSUPPORTED;
  }
}

/// True when `observation` is the managed `Texture/managed/SpecularMapping`
/// overlay pass exactly as the shipped templates declare it.
///
/// `expected_depth_write` is the one field the managed family genuinely
/// authors two ways. The opaque templates leave the overlay's depth writes at
/// OGRE's default (on, matching their own base pass); the transparent
/// templates override BOTH their base pass and this overlay with `depth_write
/// off`. Passing the base pass's own depth-write state in keeps that pairing
/// exact instead of admitting either spelling for either family: a transparent
/// declaration whose overlay still wrote depth, or an opaque one whose overlay
/// did not, is a shape no shipped template produces and stays refused.
bool IsExactManagedSpecularPass(const ExactPassObservation &observation,
                                bool expected_depth_write) noexcept {
  return observation.depth_write == expected_depth_write &&
         observation.diffuse == std::array<float, 4U>{1.0F, 1.0F, 1.0F,
                                                       1.0F} &&
         observation.ambient == std::array<float, 4U>{1.0F, 1.0F, 1.0F,
                                                       1.0F} &&
         observation.specular == std::array<float, 4U>{0.0F, 0.0F, 0.0F,
                                                        1.0F} &&
         observation.emissive == std::array<float, 4U>{0.0F, 0.0F, 0.0F,
                                                        1.0F} &&
         observation.shininess == 0.0F &&
         observation.source_color == Ogre::SBF_ONE &&
         observation.destination_color == Ogre::SBF_ONE &&
         observation.source_alpha == Ogre::SBF_ONE &&
         observation.destination_alpha == Ogre::SBF_ONE &&
         observation.color_operation == Ogre::SBO_ADD &&
         observation.alpha_operation == Ogre::SBO_ADD &&
         observation.alpha_reject == Ogre::CMPF_ALWAYS_PASS &&
         observation.write_red && observation.write_green &&
         observation.write_blue && observation.write_alpha &&
         observation.lighting_enabled && !observation.alpha_to_coverage &&
         observation.depth_check &&
         observation.depth_function == Ogre::CMPF_LESS_EQUAL &&
         observation.depth_bias_constant == 0.0F &&
         observation.depth_bias_slope_scale == 0.0F &&
         observation.iteration_depth_bias == 0.0F &&
         observation.cull_mode == Ogre::CULL_CLOCKWISE &&
         observation.manual_cull_mode == Ogre::MANUAL_CULL_BACK &&
         observation.polygon_mode == Ogre::PM_SOLID &&
         observation.polygon_mode_overrideable &&
         observation.vertex_colour_tracking == Ogre::TVC_NONE &&
         !observation.fog_override && observation.pass_iteration_count == 1U &&
         !observation.iterate_per_light;
}

bool MatchExactPassObservation(const ExactPassObservation &left,
                               const ExactPassObservation &right) noexcept {
  try {
    std::string left_key;
    std::string right_key;
    AppendExactPassObservation(left_key, left);
    AppendExactPassObservation(right_key, right);
    return left_key == right_key;
  } catch (...) {
    return false;
  }
}

void AppendExactPassObservation(std::string &key,
                                const ExactPassObservation &observation) {
  for (const auto &colour : {observation.diffuse, observation.ambient,
                             observation.specular, observation.emissive}) {
    for (const float component : colour) {
      AppendFloatBits(key, component);
    }
  }
  AppendFloatBits(key, observation.shininess);
  AppendNumber(key, observation.source_color);
  AppendNumber(key, observation.destination_color);
  AppendNumber(key, observation.source_alpha);
  AppendNumber(key, observation.destination_alpha);
  AppendNumber(key, observation.color_operation);
  AppendNumber(key, observation.alpha_operation);
  AppendNumber(key, observation.alpha_reject);
  AppendNumber(key, observation.alpha_reject_value);
  AppendNumber(key, observation.depth_function);
  AppendNumber(key, observation.cull_mode);
  AppendNumber(key, observation.manual_cull_mode);
  AppendNumber(key, observation.polygon_mode);
  AppendNumber(key, observation.vertex_colour_tracking);
  AppendNumber(key, observation.pass_iteration_count);
  AppendFloatBits(key, observation.depth_bias_constant);
  AppendFloatBits(key, observation.depth_bias_slope_scale);
  AppendFloatBits(key, observation.iteration_depth_bias);
  AppendNumber(key, observation.write_red ? 1U : 0U);
  AppendNumber(key, observation.write_green ? 1U : 0U);
  AppendNumber(key, observation.write_blue ? 1U : 0U);
  AppendNumber(key, observation.write_alpha ? 1U : 0U);
  AppendNumber(key, observation.lighting_enabled ? 1U : 0U);
  AppendNumber(key, observation.alpha_to_coverage ? 1U : 0U);
  AppendNumber(key, observation.depth_check ? 1U : 0U);
  AppendNumber(key, observation.depth_write ? 1U : 0U);
  AppendNumber(key, observation.polygon_mode_overrideable ? 1U : 0U);
  AppendNumber(key, observation.fog_override ? 1U : 0U);
  AppendNumber(key, observation.iterate_per_light ? 1U : 0U);
}

// Preserve the exact pre-alpha-slice opaque-v2 projection identity. The
// larger v4 pass fingerprint is still captured and revalidated, but adding
// newly represented canonical pass fields to this hash would churn every
// existing opaque material source ID despite byte-identical normalization.
void AppendLegacyOpaqueV2PassIdentity(
    std::string &key, const ExactPassObservation &observation) {
  for (const auto &colour : {observation.diffuse, observation.ambient,
                             observation.specular, observation.emissive}) {
    for (const float component : colour) {
      AppendFloatBits(key, component);
    }
  }
  AppendFloatBits(key, observation.shininess);
  AppendNumber(key, observation.vertex_colour_tracking);
}

bool ClassifyCanonicalPass(
    const ExactPassObservation &observation,
    bool allow_vertex_colour_tracking, Render::MaterialBlendMode &blend_mode,
    Render::MaterialAlphaTestMode &alpha_test_mode) noexcept {
  const bool replace =
      observation.source_color == Ogre::SBF_ONE &&
      observation.destination_color == Ogre::SBF_ZERO &&
      observation.source_alpha == Ogre::SBF_ONE &&
      observation.destination_alpha == Ogre::SBF_ZERO;
  const bool legacy_straight_alpha =
      observation.source_color == Ogre::SBF_SOURCE_ALPHA &&
      observation.destination_color == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
      observation.source_alpha == Ogre::SBF_SOURCE_ALPHA &&
      observation.destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
  const bool straight_source_over =
      observation.source_color == Ogre::SBF_SOURCE_ALPHA &&
      observation.destination_color == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
      observation.source_alpha == Ogre::SBF_ONE &&
      observation.destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
  const bool disabled_alpha_test =
      observation.alpha_reject == Ogre::CMPF_ALWAYS_PASS;
  const bool greater_alpha_test =
      observation.alpha_reject == Ogre::CMPF_GREATER;
  const bool greater_equal_alpha_test =
      observation.alpha_reject == Ogre::CMPF_GREATER_EQUAL;
  const bool alpha_factor_is_unit = observation.diffuse[3U] == 1.0F;
  if (!std::all_of(observation.diffuse.begin(), observation.diffuse.end(),
                   [](float value) { return std::isfinite(value); }) ||
      !std::all_of(observation.ambient.begin(), observation.ambient.end(),
                   [](float value) { return std::isfinite(value); }) ||
      !std::all_of(observation.specular.begin(), observation.specular.end(),
                   [](float value) { return std::isfinite(value); }) ||
      !std::all_of(observation.emissive.begin(), observation.emissive.end(),
                   [](float value) { return std::isfinite(value); }) ||
      !std::isfinite(observation.shininess) || !observation.write_red ||
      !observation.write_green || !observation.write_blue ||
      !observation.write_alpha || !observation.lighting_enabled ||
      observation.color_operation != Ogre::SBO_ADD ||
      observation.alpha_operation != Ogre::SBO_ADD ||
      (!replace && !legacy_straight_alpha && !straight_source_over) ||
      (!disabled_alpha_test && !greater_alpha_test &&
       !greater_equal_alpha_test) ||
      ((legacy_straight_alpha || straight_source_over) &&
       !alpha_factor_is_unit) ||
      ((greater_alpha_test || greater_equal_alpha_test) &&
       !alpha_factor_is_unit) ||
      (replace && disabled_alpha_test && !alpha_factor_is_unit) ||
      observation.alpha_to_coverage || !observation.depth_check ||
      observation.depth_function != Ogre::CMPF_LESS_EQUAL ||
      observation.depth_bias_constant != 0.0F ||
      observation.depth_bias_slope_scale != 0.0F ||
      observation.iteration_depth_bias != 0.0F ||
      // `cull_software` (Pass::getManualCullingMode) is deliberately NOT gated
      // here. Manual culling is a CPU-side hint that OGRE 14 consults only
      // when it processes geometry in software - shadow-volume extrusion and
      // the BSP/patch scene managers. It never reaches a rasterizer state, and
      // this projection carries no field for it: the capture input this
      // classification feeds (Ogre14GraphicsSceneMaterialCaptureInput) declares
      // `cull` (the hardware winding, which IS gated, observed, and lowered)
      // and no manual-cull member at all. Admitting `cull_software none` means
      // ignoring a token that had no consumer, not approximating a raster state
      // we cannot reproduce - the presented pixels are bit-identical either
      // way.
      //
      // The token stays part of the frozen identity key regardless
      // (AppendExactPassObservation appends manual_cull_mode), so two passes
      // that differ only in software culling still hash apart and cannot share
      // a projection.
      observation.polygon_mode != Ogre::PM_SOLID ||
      !observation.polygon_mode_overrideable ||
      (observation.vertex_colour_tracking != Ogre::TVC_NONE &&
       !allow_vertex_colour_tracking) ||
      observation.fog_override || observation.pass_iteration_count != 1U ||
      observation.iterate_per_light) {
    return false;
  }
  blend_mode =
      legacy_straight_alpha
          ? Render::MaterialBlendMode::LEGACY_STRAIGHT_ALPHA
          : straight_source_over
                ? Render::MaterialBlendMode::STRAIGHT_SOURCE_OVER
                : Render::MaterialBlendMode::REPLACE;
  alpha_test_mode =
      greater_alpha_test
          ? Render::MaterialAlphaTestMode::GREATER
          : greater_equal_alpha_test
                ? Render::MaterialAlphaTestMode::GREATER_EQUAL
                : Render::MaterialAlphaTestMode::DISABLED;
  return true;
}

bool IsCanonicalPass(const Ogre::Pass &pass,
                     bool allow_vertex_colour_tracking) noexcept {
  Render::MaterialBlendMode blend_mode = Render::MaterialBlendMode::REPLACE;
  Render::MaterialAlphaTestMode alpha_test_mode =
      Render::MaterialAlphaTestMode::DISABLED;
  return ClassifyCanonicalPass(ObserveExactPass(pass),
                               allow_vertex_colour_tracking, blend_mode,
                               alpha_test_mode);
}

bool IsExactContinuousDustPass(const Ogre::Technique &technique,
                               const Ogre::Pass &pass,
                               const Ogre::Material &material) noexcept {
  const ExactPassObservation observation = ObserveExactPass(pass);
  return material.getName() == "tracks/SmokeMat" &&
         !material.getReceiveShadows() && technique.getNumPasses() == 1U &&
         pass.getNumTextureUnitStates() == 1U && !HasAuthoredProgram(pass) &&
         observation.diffuse ==
             std::array<float, 4U>{1.0F, 1.0F, 1.0F, 1.0F} &&
         observation.source_color == Ogre::SBF_SOURCE_ALPHA &&
         observation.destination_color ==
             Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
         observation.source_alpha == Ogre::SBF_SOURCE_ALPHA &&
         observation.destination_alpha ==
             Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
         observation.color_operation == Ogre::SBO_ADD &&
         observation.alpha_operation == Ogre::SBO_ADD &&
         observation.alpha_reject == Ogre::CMPF_GREATER &&
         observation.alpha_reject_value == 2U && observation.write_red &&
         observation.write_green && observation.write_blue &&
         observation.write_alpha && !observation.lighting_enabled &&
         !observation.alpha_to_coverage && observation.depth_check &&
         !observation.depth_write &&
         observation.depth_function == Ogre::CMPF_LESS_EQUAL &&
         observation.depth_bias_constant == 0.0F &&
         observation.depth_bias_slope_scale == 0.0F &&
         observation.iteration_depth_bias == 0.0F &&
         observation.cull_mode == Ogre::CULL_CLOCKWISE &&
         observation.manual_cull_mode == Ogre::MANUAL_CULL_BACK &&
         observation.polygon_mode == Ogre::PM_SOLID &&
         observation.polygon_mode_overrideable &&
         observation.vertex_colour_tracking == Ogre::TVC_NONE &&
         !observation.fog_override && observation.pass_iteration_count == 1U &&
         !observation.iterate_per_light;
}

bool IsExactContinuousDustSampler(
    const OgreNextDemoExactSamplerObservation &observation) noexcept {
  return observation.address_u ==
             OgreNextDemoObservedSamplerAddressMode::CLAMP &&
         observation.address_v ==
             OgreNextDemoObservedSamplerAddressMode::CLAMP &&
         observation.address_w ==
             OgreNextDemoObservedSamplerAddressMode::CLAMP;
}

bool UsesTextureAlphaCombine(const Ogre::TextureUnitState &unit) noexcept {
  return unit.getColourBlendMode().operation ==
             Ogre::LBX_BLEND_TEXTURE_ALPHA ||
         unit.getAlphaBlendMode().operation == Ogre::LBX_BLEND_TEXTURE_ALPHA;
}

bool IsManagedTransparentType(
    Render::ManagedMaterialSemanticType type) noexcept {
  return type == Render::ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT ||
         type == Render::ManagedMaterialSemanticType::MESH_TRANSPARENT;
}

/// The single roughness derivation for one projected legacy material.
///
/// This must stay the only implementation. A capture stores the value it
/// derives, and every later capture that reuses the cached projection derives
/// it again and requires the two to be equal; two copies of the rule are two
/// chances to disagree, and a disagreement reads as "projected native material
/// authority changed" and stops the scene presenting for the rest of the run.
float ResolveOgreNextDemoRoughnessFactor(
    const Ogre::Material &native_material, const Ogre::Pass &pass,
    const OgreNextDemoCuratedCityWorldMaterial *curated_policy) noexcept {
  if (curated_policy != nullptr) {
    return curated_policy->roughness_factor;
  }
  float authored_roughness = 0.0F;
  if (OgreNextDemoResolveAlexisAuthoredRoughness(native_material.getGroup(),
                                                 native_material.getName(),
                                                 authored_roughness)) {
    return authored_roughness;
  }
  return static_cast<float>(
      std::sqrt(2.0 / (static_cast<double>(pass.getShininess()) + 2.0)));
}

bool IsExactAlexisDiffuseProjection(
    const Ogre::Technique &technique, const Ogre::Pass &base_pass,
    std::string_view exact_material_name,
    std::string_view exact_diffuse_texture_name) noexcept {
  const std::size_t separator = exact_material_name.find(" (");
  if (separator == std::string_view::npos || technique.getNumPasses() != 2U ||
      base_pass.getName() != "BaseRender" ||
      base_pass.getNumTextureUnitStates() != 1U ||
      HasAuthoredProgram(base_pass)) {
    return false;
  }
  const std::string_view base = exact_material_name.substr(0U, separator);
  std::string_view expected_diffuse;
  std::string_view expected_specular;
  // The two managed families this predicate accepts differ in exactly three
  // authored pass states, and they differ in all three together: the
  // `managed/*_transparent/*` templates declare `scene_blend alpha_blend`,
  // `alpha_rejection greater 0` and `depth_write off` on their base pass (and
  // repeat the depth-write override on the specular overlay), while the
  // `managed/*_standard/*` templates leave all three at OGRE's opaque
  // defaults. Deciding the family from the material name and then requiring
  // the whole triple keeps a half-transparent declaration - one this runtime
  // has never seen and could not lower faithfully - refused.
  bool transparent_family = false;
  if (base == "SaberChassis" || base == "SaberChassisM") {
    expected_diffuse = "AlexisSaberChassis.png";
    expected_specular = "AlexisSaberChassisSpec.png";
  } else if (base == "SaberWheels") {
    expected_diffuse = "AlexisSaberWheel.png";
    expected_specular = "AlexisSaberWheelSpec.png";
  } else if (base == "SaberGrilles") {
    expected_diffuse = "AlexisSabergrilles.png";
    expected_specular = "alexissabergrillesspec.png";
  } else if (base == "SaberBody") {
    // The body paint is the one Alexis base whose texture names move at
    // runtime: AlexisSaber.skin replaces both the base colour and its paired
    // specular member when a colour skin is selected, and the replacement
    // rewrites the texture unit names this predicate reads. Enumerating the
    // authored pairs keeps the gate exact - an unpaired or unknown pair is
    // still refused - while letting all six skins project.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6U>
        kAuthoredBodyPaint{{{"bodytemp.png", "bodytempspec.png"},
                            {"body_black.png", "body_blackspec.png"},
                            {"body_blue.png", "body_bluespec.png"},
                            {"body_green.png", "body_greenspec.png"},
                            {"body_purple.png", "body_purplespec.png"},
                            {"body_white.png", "body_whitespec.png"}}};
    for (const auto &[authored_diffuse, authored_specular] :
         kAuthoredBodyPaint) {
      if (exact_diffuse_texture_name == authored_diffuse) {
        expected_diffuse = authored_diffuse;
        expected_specular = authored_specular;
        break;
      }
    }
    if (expected_diffuse.empty()) {
      return false;
    }
  } else if (base == "SaberWinds" || base == "SaberWinds_int") {
    // The outer and inner window shells. Both declare
    // `mesh_transparent AlexisSaberWinds2.png AlexisSaberWindss.png`, which
    // the spawner instantiates as `managed/mesh_transparent/specular` - the
    // same two-pass BaseRender + SpecularMapping1 shape the opaque bases use,
    // with the transparent triple applied.
    expected_diffuse = "AlexisSaberWinds2.png";
    expected_specular = "AlexisSaberWindss.png";
    transparent_family = true;
  } else {
    return false;
  }
  if (base_pass.getDepthWriteEnabled() == transparent_family ||
      base_pass.getSourceBlendFactor() !=
          (transparent_family ? Ogre::SBF_SOURCE_ALPHA : Ogre::SBF_ONE) ||
      base_pass.getDestBlendFactor() !=
          (transparent_family ? Ogre::SBF_ONE_MINUS_SOURCE_ALPHA
                              : Ogre::SBF_ZERO) ||
      base_pass.getAlphaRejectFunction() !=
          (transparent_family ? Ogre::CMPF_GREATER : Ogre::CMPF_ALWAYS_PASS) ||
      (transparent_family && base_pass.getAlphaRejectValue() != 0U)) {
    return false;
  }
  Ogre::TextureUnitState *const diffuse = base_pass.getTextureUnitState(0U);
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
  Ogre::TextureUnitState *const environment = specular->getTextureUnitState(1U);
  return specular_texture != nullptr && environment != nullptr &&
         specular_texture->getName() == "SpecularMapping1_Tex" &&
         specular_texture->getTextureName() == expected_specular &&
         IsExactManagedSpecularPass(ObserveExactPass(*specular),
                                    !transparent_family) &&
         IsExactManagedSpecularTextureUnitSemantic(*specular_texture) &&
         IsExactManagedEnvironmentUnit(*environment);
}

OgreNextDemoObservedSamplerFilter
ObserveFilter(Ogre::FilterOptions native) noexcept {
  switch (native) {
  case Ogre::FO_POINT:
    return OgreNextDemoObservedSamplerFilter::POINT;
  case Ogre::FO_LINEAR:
    return OgreNextDemoObservedSamplerFilter::LINEAR;
  case Ogre::FO_ANISOTROPIC:
    return OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  default:
    return OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  }
}

bool IsCanonicalAnisotropicSampler(
    const OgreNextDemoExactSamplerObservation &observation) noexcept {
  return observation.minification_filter ==
             OgreNextDemoObservedSamplerFilter::ANISOTROPIC &&
         observation.magnification_filter ==
             OgreNextDemoObservedSamplerFilter::ANISOTROPIC &&
         observation.mip_filter == OgreNextDemoObservedSamplerFilter::LINEAR;
}

OgreNextDemoObservedSamplerAddressMode
ObserveAddressMode(Ogre::TextureAddressingMode native) noexcept {
  switch (native) {
  case Ogre::TAM_WRAP:
    return OgreNextDemoObservedSamplerAddressMode::WRAP;
  case Ogre::TAM_MIRROR:
    return OgreNextDemoObservedSamplerAddressMode::MIRROR;
  case Ogre::TAM_CLAMP:
    return OgreNextDemoObservedSamplerAddressMode::CLAMP;
  default:
    return OgreNextDemoObservedSamplerAddressMode::UNSUPPORTED;
  }
}

OgreNextDemoExactSamplerObservation
ObserveExactSampler(const Ogre::Sampler &native) noexcept {
  OgreNextDemoExactSamplerObservation observation;
  observation.minification_filter =
      ObserveFilter(native.getFiltering(Ogre::FT_MIN));
  observation.magnification_filter =
      ObserveFilter(native.getFiltering(Ogre::FT_MAG));
  observation.mip_filter = ObserveFilter(native.getFiltering(Ogre::FT_MIP));
  const Ogre::Sampler::UVWAddressingMode address = native.getAddressingMode();
  observation.address_u = ObserveAddressMode(address.u);
  observation.address_v = ObserveAddressMode(address.v);
  observation.address_w = ObserveAddressMode(address.w);
  observation.mip_lod_bias = native.getMipmapBias();
  observation.maximum_anisotropy = native.getAnisotropy();
  observation.compare_enabled = native.getCompareEnabled();
  observation.compare_function_token =
      static_cast<std::uint8_t>(native.getCompareFunction());
  const Ogre::ColourValue border = native.getBorderColour();
  observation.border_color = {
      static_cast<float>(border.r), static_cast<float>(border.g),
      static_cast<float>(border.b), static_cast<float>(border.a)};
  return observation;
}

void AppendExactSamplerObservation(
    std::string &key, const OgreNextDemoExactSamplerObservation &observation) {
  AppendNumber(key,
               static_cast<std::uint64_t>(observation.minification_filter));
  AppendNumber(key,
               static_cast<std::uint64_t>(observation.magnification_filter));
  AppendNumber(key, static_cast<std::uint64_t>(observation.mip_filter));
  AppendNumber(key, static_cast<std::uint64_t>(observation.address_u));
  AppendNumber(key, static_cast<std::uint64_t>(observation.address_v));
  AppendNumber(key, static_cast<std::uint64_t>(observation.address_w));
  AppendFloatBits(key, observation.mip_lod_bias);
  AppendNumber(key, observation.maximum_anisotropy);
  AppendNumber(key, observation.compare_enabled ? 1U : 0U);
  AppendNumber(key, observation.compare_function_token);
  for (float component : observation.border_color) {
    AppendFloatBits(key, component);
  }
}

struct CapturedTexture final {
  OgreNextDemoTextureSourceMode source =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
  std::size_t native_state_count = 0U;
  OgreNextDemoExactTextureObservation exact_texture_observation;
  OgreNextDemoTextureNormalizationObservation normalization_observation;
  OgreNextDemoTextureAlphaPolicy alpha_policy =
      OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE;
  Render::Ogre14AuthenticatedTextureReceipt authenticated_receipt;
  AuthenticatedTextureProvenance authenticated_provenance;
  /// Separate from the stable public source-asset identity. A reload or byte
  /// mutation must reject the frozen map generation, never mint a replacement
  /// asset ID and resurrect its tombstone.
  std::string authenticated_content_decode_key;
  Render::Ogre14SelectedTextureSourceReceipt ordinary_receipt;
  OrdinaryTextureProvenance ordinary_provenance;
  std::string ordinary_content_decode_key;
  std::uint64_t source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> payload;
};

bool ManagedReceiptMatchesCapturedTexture(
    const Render::ManagedMaterialTextureSourceReceipt &managed,
    const CapturedTexture &captured) noexcept {
  if (IsOgreNextDemoAuthenticatedTextureSourceMode(captured.source)) {
    return ManagedReceiptBytesEqual(
        managed, captured.authenticated_receipt.source_bytes(),
        captured.authenticated_receipt.source_size());
  }
  if (captured.source ==
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES) {
    return ManagedReceiptBytesEqual(managed,
                                    captured.ordinary_receipt.source_bytes(),
                                    captured.ordinary_receipt.source_size());
  }
  return false;
}

bool ResolveFrozenAuthenticatedTexture(
    Ogre::Texture &native_texture,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const CapturedTexture &captured,
    const OgreNextDemoExactTextureObservation &fresh_texture_observation,
    Render::Ogre14AuthenticatedTextureResolution *fresh_output) noexcept {
  try {
    if (!IsOgreNextDemoAuthenticatedTextureSourceMode(captured.source) ||
        !captured.authenticated_receipt.initialized() ||
        captured.authenticated_content_decode_key.empty() ||
        !native_texture.isLoaded() ||
        captured.native_state_count != native_texture.getStateCount() ||
        !MatchOgreNextDemoExactTextureObservation(
            captured.exact_texture_observation, fresh_texture_observation)) {
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
        metadata != nullptr
            ? BuildAuthenticatedDecodeOptions(*metadata,
                                              captured.alpha_policy)
                            : Render::Ogre14SourceTextureDecodeOptions{};
    AuthenticatedTextureProvenance observed;
    std::string observed_key;
    const Render::ValidationResult observation =
        metadata != nullptr
            ? BuildAuthenticatedTextureProvenance(
                  native_texture, resolver, fresh, options,
                  fresh_texture_observation, captured.alpha_policy, observed,
                  observed_key)
            : Failure(Render::ValidationCode::MISSING_REFERENCE,
                      "ogre_next_demo.material.authenticated.receipt",
                      "authenticated receipt disappeared");
    OgreNextDemoTextureSourceMode observed_mode =
        OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
    const bool matches =
        resolution.ok() && receipt != nullptr && receipt->initialized() &&
        receipt->SharesImmutableStateWith(captured.authenticated_receipt) &&
        observation.ok() &&
        MapAuthenticatedSourceMode(observed.source_kind, observed_mode) &&
        observed_mode == captured.source &&
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

bool ResolveFrozenOrdinaryTexture(
    Ogre::Texture &native_texture,
    const Render::IOgre14SelectedTextureSourceResolver &resolver,
    const CapturedTexture &captured,
    const OgreNextDemoExactTextureObservation &fresh_texture_observation,
    Render::Ogre14SelectedTextureSourceResolution *fresh_output) noexcept {
  try {
    if (captured.source !=
            OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES ||
        !captured.ordinary_receipt.initialized() ||
        captured.ordinary_content_decode_key.empty() ||
        !native_texture.isLoaded() ||
        captured.native_state_count != native_texture.getStateCount() ||
        !MatchOgreNextDemoExactTextureObservation(
            captured.exact_texture_observation, fresh_texture_observation)) {
      return false;
    }
    Render::Ogre14SelectedTextureSourceResolution fresh;
    const Render::ValidationResult resolution =
        resolver.ResolveSelectedTextureSource(native_texture, fresh);
    const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
        resolution ? fresh.source_receipt() : nullptr;
    const Render::Ogre14SourceTextureDecodeOptions options =
        receipt != nullptr
            ? BuildOrdinaryDecodeOptions(*receipt, captured.alpha_policy)
                           : Render::Ogre14SourceTextureDecodeOptions{};
    OrdinaryTextureProvenance observed;
    std::string observed_key;
    const Render::ValidationResult observation =
        receipt != nullptr
            ? BuildOrdinaryTextureProvenance(
                  native_texture, resolver, fresh, options,
                  fresh_texture_observation, captured.alpha_policy, observed,
                  observed_key)
            : Failure(Render::ValidationCode::MISSING_REFERENCE,
                      "ogre_next_demo.material.ordinary.receipt",
                      "ordinary selected-source receipt disappeared");
    const bool matches =
        resolution.ok() && receipt != nullptr && receipt->initialized() &&
        receipt->SharesImmutableStateWith(captured.ordinary_receipt) &&
        observation.ok() &&
        resolver.RevalidateSelectedTextureSource(native_texture, fresh) &&
        observed_key == captured.ordinary_content_decode_key;
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

struct CuratedCityWorldNativeFacts final {
  const OgreNextDemoCuratedCityWorldMaterial *policy = nullptr;
  Ogre::MaterialPtr material;
  std::uint64_t material_state_count = 0U;
  Ogre::Pass *pass = nullptr;
  std::array<Ogre::TextureUnitState *, 3U> units{};
  std::array<Ogre::SamplerPtr, 3U> samplers{};
  std::array<Ogre::TexturePtr, 3U> textures{};
  ExactPassObservation pass_observation;
  std::array<OgreNextDemoExactSamplerObservation, 3U>
      sampler_observations{};
  std::array<OgreNextDemoExactTextureObservation, 3U>
      texture_observations{};
  Render::Ogre14AuthenticatedMaterialScriptResolution script_resolution;
  std::array<Render::Ogre14AuthenticatedTextureResolution, 3U>
      texture_resolutions{};
  Render::ManagedMaterialTextureSourceReceipt specular_receipt;
  Render::Ogre14ManagedMaterialSourceAuthorityBinding specular_binding;
};

bool IsReviewedCuratedCityWorldSampler(
    const OgreNextDemoExactSamplerObservation &observation) noexcept {
  return observation.minification_filter ==
             OgreNextDemoObservedSamplerFilter::ANISOTROPIC &&
         observation.magnification_filter ==
             OgreNextDemoObservedSamplerFilter::ANISOTROPIC &&
         observation.mip_filter ==
             OgreNextDemoObservedSamplerFilter::LINEAR &&
         observation.address_u ==
             OgreNextDemoObservedSamplerAddressMode::WRAP &&
         observation.address_v ==
             OgreNextDemoObservedSamplerAddressMode::WRAP &&
         observation.address_w ==
             OgreNextDemoObservedSamplerAddressMode::WRAP &&
         observation.mip_lod_bias == 0.0F &&
         observation.maximum_anisotropy == 4U &&
         !observation.compare_enabled &&
         observation.compare_function_token ==
             static_cast<std::uint8_t>(Ogre::CMPF_GREATER_EQUAL) &&
         observation.border_color ==
             std::array<float, 4U>{{0.0F, 0.0F, 0.0F, 1.0F}};
}

bool ResolveReviewedCuratedCityWorldTexture(
    const Ogre::TexturePtr &texture, std::string_view expected_name,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    Render::Ogre14AuthenticatedTextureResolution &output,
    bool &temporarily_unavailable) noexcept {
  try {
    if (!texture || !texture->isLoaded()) {
      temporarily_unavailable = true;
      return false;
    }
    if (texture->getName() != expected_name ||
        !resolver.RequiresAuthenticatedTextureSource(*texture)) {
      return false;
    }
    Render::Ogre14AuthenticatedTextureResolution resolution;
    const Render::ValidationResult result =
        resolver.ResolveAuthenticatedTexture(*texture, resolution);
    if (!result &&
        (result.code == Render::ValidationCode::MISSING_REFERENCE ||
         result.code == Render::ValidationCode::SEQUENCE_MISMATCH)) {
      temporarily_unavailable = true;
      return false;
    }
    const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
        result ? resolution.source_receipt() : nullptr;
    const Render::Ogre14AuthenticatedTextureReceiptMetadata *const metadata =
        receipt != nullptr ? receipt->metadata() : nullptr;
    const std::size_t state_count = texture->getStateCount();
    if (!result || !resolution.initialized() || receipt == nullptr ||
        metadata == nullptr || !receipt->initialized() ||
        receipt->source_bytes() == nullptr || receipt->source_size() == 0U ||
        metadata->source.source_kind !=
            Render::Ogre14AuthenticatedTextureSourceKind::
                AUTHENTICATED_ARCHIVE_MEMBER ||
        metadata->source.archive_sha256 !=
            kOgreNextDemoCuratedCityWorldArchiveSha256 ||
        metadata->source.exact_member_name != expected_name ||
        metadata->source.effective_resource_group != texture->getGroup() ||
        metadata->source.binding.kind !=
            Render::Ogre14AuthenticatedTextureBindingKind::RESOURCE ||
        metadata->source.binding.exact_resource_name != texture->getName() ||
        metadata->byte_count != receipt->source_size() ||
        !resolution.MatchesResolver(resolver) ||
        !resolution.MatchesLoadedResourceIdentity(
            reinterpret_cast<std::uintptr_t>(texture.get()),
            static_cast<std::uint64_t>(texture->getHandle()),
            texture->getGroup(), texture->getName(),
            static_cast<std::uint64_t>(state_count)) ||
        !resolver.RevalidateAuthenticatedTexture(*texture, resolution)) {
      return false;
    }
    output = std::move(resolution);
    return true;
  } catch (...) {
    return false;
  }
}

bool ResolveReviewedCuratedCityWorldScript(
    const Ogre::MaterialPtr &material,
    const OgreNextDemoCuratedCityWorldMaterial &policy,
    const Render::IOgre14AuthenticatedMaterialScriptResolver &resolver,
    Render::Ogre14AuthenticatedMaterialScriptResolution &output,
    bool &temporarily_unavailable) noexcept {
  try {
    if (!material) {
      return false;
    }
    Render::Ogre14AuthenticatedMaterialScriptResolution resolution;
    const Render::ValidationResult result =
        resolver.ResolveAuthenticatedMaterialScript(*material, resolution);
    if (!result &&
        (result.code == Render::ValidationCode::MISSING_REFERENCE ||
         result.code == Render::ValidationCode::SEQUENCE_MISMATCH)) {
      temporarily_unavailable = true;
      return false;
    }
    const Render::Ogre14AuthenticatedMaterialScriptReceipt *const receipt =
        result ? resolution.receipt() : nullptr;
    const Render::Ogre14AuthenticatedMaterialScriptSourceMetadata *const
        source = receipt != nullptr ? receipt->source_metadata() : nullptr;
    const Render::Ogre14AuthenticatedMaterialScriptBindingMetadata *const
        binding = receipt != nullptr ? receipt->binding_metadata() : nullptr;
    if (!result || !resolution.initialized() || receipt == nullptr ||
        !receipt->initialized() || source == nullptr || binding == nullptr ||
        receipt->source_count() != 1U ||
        receipt->primary_source_index() != 0U ||
        source->source_role !=
            Render::Ogre14MaterialScriptSourceRole::ROOT_SCRIPT ||
        source->archive_sha256 !=
            kOgreNextDemoCuratedCityWorldArchiveSha256 ||
        source->exact_member_name !=
            kOgreNextDemoCuratedCityWorldScriptMember ||
        source->original_sha256 !=
            kOgreNextDemoCuratedCityWorldScriptSha256 ||
        source->original_byte_count != receipt->original_size() ||
        source->effective_byte_count != receipt->effective_size() ||
        receipt->original_bytes() == nullptr ||
        receipt->effective_bytes() == nullptr ||
        binding->material_pointer_token !=
            reinterpret_cast<std::uintptr_t>(material.get()) ||
        binding->material_handle !=
            static_cast<std::uint64_t>(material->getHandle()) ||
        binding->exact_material_name != material->getName() ||
        binding->exact_group != material->getGroup()) {
      return false;
    }
    // Independently recompute the reviewed repair-plan digest from the
    // source-controlled plan table. The receipt's own digest must equal it, so
    // a repaired script is admitted only when the repair is exactly the
    // reviewed one, and never merely because a repair was declared.
    std::string reviewed_repair_plan_sha256;
    const bool repair_applied =
        source->repair_state ==
        Render::Ogre14MaterialScriptRepairState::APPLIED;
    const std::string archive_sha256(source->archive_sha256);
    const std::string exact_member_name(source->exact_member_name);
    const std::string original_sha256(source->original_sha256);
    const LegacyMaterialScriptEditPlan *const reviewed_plan =
        FindLegacyMaterialScriptEditPlan(archive_sha256, exact_member_name);
    if (repair_applied) {
      if (reviewed_plan == nullptr ||
          !ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
              *reviewed_plan, exact_member_name, original_sha256,
              reviewed_repair_plan_sha256)) {
        return false;
      }
    } else if (!ComputeLegacyMaterialScriptNoRepairPlanSha256(
                   archive_sha256, exact_member_name, original_sha256,
                   reviewed_repair_plan_sha256)) {
      return false;
    }
    OgreNextDemoCuratedCityWorldScriptRepairObservation repair;
    repair.repair_applied = repair_applied;
    repair.applied_edit_count = source->applied_edit_count;
    repair.repair_plan_version = source->repair_plan_version;
    repair.repair_plan_sha256 = source->repair_plan_sha256;
    repair.reviewed_repair_plan_sha256 = reviewed_repair_plan_sha256;
    repair.original_sha256 = source->original_sha256;
    repair.effective_sha256 = source->effective_sha256;
    repair.effective_bytes_equal_original =
        receipt->original_size() == receipt->effective_size() &&
        std::memcmp(receipt->original_bytes(), receipt->effective_bytes(),
                    receipt->original_size()) == 0;
    if (!AuthenticateOgreNextDemoCuratedCityWorldScriptRepair(repair)) {
      return false;
    }

    // The reviewed declaration span stays pinned to the original archive
    // bytes: that is the text the row was reviewed against, and the receipt
    // retains it verbatim beside the repaired effective bytes.
    const OgreNextDemoCuratedCityWorldSourceObservation observation{
        source->archive_sha256, source->exact_member_name,
        source->original_sha256, receipt->original_bytes(),
        receipt->original_size()};
    if (!AuthenticateOgreNextDemoCuratedCityWorldMaterial(policy,
                                                           observation) ||
        !resolver.RevalidateAuthenticatedMaterialScript(*material,
                                                        resolution)) {
      return false;
    }
    output = std::move(resolution);
    return true;
  } catch (...) {
    return false;
  }
}

bool SharesCuratedCityWorldTextureReceipt(
    const Render::Ogre14AuthenticatedTextureResolution &first,
    const Render::Ogre14AuthenticatedTextureResolution &second) noexcept {
  const Render::Ogre14AuthenticatedTextureReceipt *const first_receipt =
      first.source_receipt();
  const Render::Ogre14AuthenticatedTextureReceipt *const second_receipt =
      second.source_receipt();
  return first.initialized() && second.initialized() &&
         first_receipt != nullptr && second_receipt != nullptr &&
         first_receipt->initialized() && second_receipt->initialized() &&
         first_receipt->SharesImmutableStateWith(*second_receipt);
}

bool SharesCuratedCityWorldScriptReceipt(
    const Render::Ogre14AuthenticatedMaterialScriptResolution &first,
    const Render::Ogre14AuthenticatedMaterialScriptResolution &second)
    noexcept {
  const Render::Ogre14AuthenticatedMaterialScriptReceipt *const
      first_receipt = first.receipt();
  const Render::Ogre14AuthenticatedMaterialScriptReceipt *const
      second_receipt = second.receipt();
  return first.initialized() && second.initialized() &&
         first_receipt != nullptr && second_receipt != nullptr &&
         first_receipt->initialized() && second_receipt->initialized() &&
         first_receipt->SharesImmutableStateWith(*second_receipt);
}

bool ObserveCuratedCityWorldNativeFacts(
    const Ogre::MaterialPtr &material,
    const OgreNextDemoCuratedCityWorldMaterial &policy,
    Render::Ogre14GraphicsSceneMaterialCull section_cull,
    const Render::IOgre14AuthenticatedMaterialScriptResolver &script_resolver,
    const Render::IOgre14AuthenticatedTextureResolver &texture_resolver,
    const Render::IOgre14SelectedTextureSourceResolver &selected_resolver,
    CuratedCityWorldNativeFacts &output,
    bool *temporarily_unavailable = nullptr,
    const Render::ManagedMaterialTextureSourceReceipt
        *reusable_specular_receipt = nullptr) noexcept {
  try {
    bool ignored_temporarily_unavailable = false;
    if (temporarily_unavailable != nullptr) {
      *temporarily_unavailable = false;
    }
    bool &source_unavailable = temporarily_unavailable != nullptr
                                   ? *temporarily_unavailable
                                   : ignored_temporarily_unavailable;
    if (!material || material->getName() != policy.exact_material_name ||
        !material->getReceiveShadows() ||
        section_cull !=
            Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE ||
        policy.workflow != OgreNextDemoCuratedCityWorldWorkflow::SPECULAR ||
        policy.alpha_policy !=
            OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE ||
        !policy.depth_write || !policy.clockwise_cull ||
        policy.sampler_policy !=
            OgreNextDemoCuratedCityWorldSamplerPolicy::
                REVIEWED_CONFIGURED_ANISOTROPIC4_V1 ||
        policy.environment_policy !=
            OgreNextDemoCuratedCityWorldEnvironmentPolicy::
                SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED) {
      return false;
    }
    Ogre::Technique *technique = nullptr;
    if (!FindCuratedCityWorldSourceTechnique(material, technique)) {
      return false;
    }
    Ogre::Pass *const pass =
        technique != nullptr && technique->getNumPasses() == 1U
            ? technique->getPass(0U)
            : nullptr;
    if (pass == nullptr || pass->getNumTextureUnitStates() != 3U ||
        HasAuthoredProgram(*pass) ||
        !HasCuratedCityWorldSphericalFamilyShape(material)) {
      return false;
    }
    const ExactPassObservation pass_observation = ObserveExactPass(*pass);
    Render::MaterialBlendMode blend = Render::MaterialBlendMode::REPLACE;
    Render::MaterialAlphaTestMode alpha =
        Render::MaterialAlphaTestMode::DISABLED;
    if (!ClassifyCanonicalPass(pass_observation, false, blend, alpha) ||
        blend != Render::MaterialBlendMode::REPLACE ||
        alpha != Render::MaterialAlphaTestMode::DISABLED ||
        !pass_observation.depth_write ||
        pass_observation.cull_mode != Ogre::CULL_CLOCKWISE) {
      return false;
    }

    CuratedCityWorldNativeFacts candidate;
    candidate.policy = &policy;
    candidate.material = material;
    const std::size_t material_state_count = material->getStateCount();
    candidate.material_state_count =
        static_cast<std::uint64_t>(material_state_count);
    if (static_cast<std::size_t>(candidate.material_state_count) !=
        material_state_count) {
      return false;
    }
    candidate.pass = pass;
    candidate.pass_observation = pass_observation;
    const std::array<std::string_view, 3U> expected_names{{
        policy.base_color_texture_name, policy.linear_specular_texture_name,
        policy.spherical_environment_texture_name}};
    for (std::size_t index = 0U; index < expected_names.size(); ++index) {
      Ogre::TextureUnitState *const unit = pass->getTextureUnitState(index);
      const Ogre::SamplerPtr sampler =
          unit != nullptr ? unit->getSampler() : Ogre::SamplerPtr{};
      const Ogre::TexturePtr texture =
          unit != nullptr ? unit->_getTexturePtr() : Ogre::TexturePtr{};
      if (unit == nullptr || !sampler ||
          unit->getTextureName() != expected_names[index] ||
          unit->getTextureType() != Ogre::TEX_TYPE_2D ||
          (texture && texture->getName() != expected_names[index]) ||
          (texture && texture->getGroup() != material->getGroup())) {
        return false;
      }
      if (!texture || !texture->isLoaded()) {
        if (temporarily_unavailable != nullptr) {
          *temporarily_unavailable = true;
        }
        return false;
      }
      if (!HasAvailableNamedTextureSource(*unit)) {
        return false;
      }
      const OgreNextDemoExactSamplerObservation sampler_observation =
          ObserveExactSampler(*sampler);
      if (!IsReviewedCuratedCityWorldSampler(sampler_observation)) {
        return false;
      }
      Render::SamplerResourceDescriptor sampler_preflight;
      if (!BuildOgreNextDemoSamplerDescriptor(
              sampler_observation, 1U, "curated-cityworld-preflight",
              sampler_preflight)) {
        return false;
      }
      OgreNextDemoTextureProjectionExclusion texture_exclusion =
          OgreNextDemoTextureProjectionExclusion::NONE;
      if (!PreflightTextureIdentity(texture, texture_exclusion) ||
          texture_exclusion != OgreNextDemoTextureProjectionExclusion::NONE) {
        return false;
      }
      OgreNextDemoExactTextureObservation texture_observation;
      if (!ObserveExactTexture(*unit, *texture, texture_observation) ||
          !ResolveReviewedCuratedCityWorldTexture(
              texture, expected_names[index], texture_resolver,
              candidate.texture_resolutions[index],
              source_unavailable)) {
        return false;
      }
      candidate.units[index] = unit;
      candidate.samplers[index] = sampler;
      candidate.textures[index] = texture;
      candidate.sampler_observations[index] = sampler_observation;
      candidate.texture_observations[index] = texture_observation;
    }
    if (!ResolveReviewedCuratedCityWorldScript(
            material, policy, script_resolver,
            candidate.script_resolution,
            source_unavailable)) {
      return false;
    }
    const Render::ValidationResult specular_receipt =
        Render::Ogre14ManagedMaterialSourceAdapter::BuildAuthenticated(
            candidate.textures[1U], texture_resolver,
            candidate.texture_resolutions[1U],
            Render::ManagedMaterialDeclarationRegistryConfiguration{},
            candidate.specular_receipt, candidate.specular_binding,
            reusable_specular_receipt);
    if (!specular_receipt || !candidate.specular_receipt.initialized() ||
        !candidate.specular_binding.initialized() ||
        !candidate.specular_binding.Revalidate(texture_resolver,
                                               selected_resolver)) {
      return false;
    }
    output = std::move(candidate);
    return true;
  } catch (...) {
    return false;
  }
}

bool MatchCuratedCityWorldNativeFacts(
    const CuratedCityWorldNativeFacts &expected,
    const CuratedCityWorldNativeFacts &observed) noexcept {
  try {
    if (expected.policy == nullptr || observed.policy == nullptr ||
        expected.policy->review_identity_sha256 !=
            observed.policy->review_identity_sha256 ||
        expected.material.get() != observed.material.get() ||
        expected.material_state_count != observed.material_state_count ||
        expected.pass != observed.pass ||
        !MatchExactPassObservation(expected.pass_observation,
                                   observed.pass_observation) ||
        !SharesCuratedCityWorldScriptReceipt(observed.script_resolution,
                                             expected.script_resolution) ||
        !observed.specular_receipt.SharesImmutableStateWith(
            expected.specular_receipt)) {
      return false;
    }
    for (std::size_t index = 0U; index < expected.units.size(); ++index) {
      if (expected.units[index] != observed.units[index] ||
          expected.samplers[index].get() != observed.samplers[index].get() ||
          expected.textures[index].get() != observed.textures[index].get() ||
          !SharesCuratedCityWorldTextureReceipt(
              observed.texture_resolutions[index],
              expected.texture_resolutions[index]) ||
          !MatchOgreNextDemoExactSamplerObservation(
              expected.sampler_observations[index],
              observed.sampler_observations[index]) ||
          !MatchOgreNextDemoExactTextureObservation(
              expected.texture_observations[index],
              observed.texture_observations[index])) {
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

struct Projection final {
  std::string exact_name;
  std::string texture_key;
  std::string sampler_key;
  std::string managed_specular_texture_key;
  std::string managed_specular_sampler_key;
  bool curated_cityworld = false;
  std::string curated_review_identity_sha256;
  Ogre::MaterialPtr curated_native_material_owner;
  std::uint64_t curated_material_state_count = 0U;
  std::uintptr_t curated_pass_pointer_token = 0U;
  std::array<std::uintptr_t, 3U> curated_unit_pointer_tokens{};
  std::array<std::uintptr_t, 3U> curated_sampler_pointer_tokens{};
  std::array<std::uintptr_t, 3U> curated_texture_pointer_tokens{};
  std::array<OgreNextDemoExactSamplerObservation, 3U>
      curated_sampler_observations{};
  std::array<OgreNextDemoExactTextureObservation, 3U>
      curated_texture_observations{};
  Render::Ogre14AuthenticatedMaterialScriptResolution
      curated_script_resolution;
  std::array<Render::Ogre14AuthenticatedTextureResolution, 3U>
      curated_texture_resolutions{};
  Render::ManagedMaterialTextureSourceReceipt curated_specular_receipt;
  Render::Ogre14ManagedMaterialSourceAuthorityBinding
      curated_specular_binding;
  Render::Ogre14ManagedMaterialDeclarationBinding managed_binding;
  Render::RenderPayloadDigest managed_declaration_digest{};
  std::uintptr_t native_material_pointer_token = 0U;
  std::uintptr_t native_pass_pointer_token = 0U;
  std::uintptr_t native_unit_pointer_token = 0U;
  std::uintptr_t native_sampler_pointer_token = 0U;
  Ogre::MaterialPtr managed_native_material_owner;
  std::uintptr_t managed_specular_pass_pointer_token = 0U;
  std::uintptr_t managed_specular_unit_pointer_token = 0U;
  std::uintptr_t managed_specular_sampler_pointer_token = 0U;
  std::uintptr_t managed_specular_texture_pointer_token = 0U;
  OgreNextDemoExactSamplerObservation sampler_observation;
  OgreNextDemoExactSamplerObservation managed_specular_sampler_observation;
  OgreNextDemoExactTextureObservation managed_specular_texture_observation;
  ExactPassObservation pass_observation;
  ExactPassObservation managed_specular_pass_observation;
  std::array<float, 4U> base_color_factor{};
  std::array<float, 4U> discarded_ambient{};
  std::array<float, 4U> discarded_specular{};
  float roughness_factor = 1.0F;
  std::array<float, 3U> emissive_factor{};
  float discarded_emissive_alpha = 1.0F;
  std::uint8_t vertex_colour_tracking_token = 0U;
  std::uint64_t material_source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> placeholder_payload;
  std::shared_ptr<const Render::RenderAssetPayload> material_payload;
};

struct ManagedSpecularNativeFacts final {
  Ogre::Pass *pass = nullptr;
  Ogre::TextureUnitState *unit = nullptr;
  Ogre::SamplerPtr sampler;
  Ogre::TexturePtr texture;
  ExactPassObservation pass_observation;
  OgreNextDemoExactSamplerObservation sampler_observation;
  OgreNextDemoExactTextureObservation texture_observation;
};

bool ObserveManagedSpecularNativeFacts(
    const Ogre::MaterialPtr &material,
    const Render::ManagedMaterialTextureSourceReceipt &receipt,
    const Render::IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const Render::IOgre14SelectedTextureSourceResolver &selected_resolver,
    ManagedSpecularNativeFacts &output) noexcept {
  try {
    if (!material || material->getNumTechniques() != 1U) {
      return false;
    }
    Ogre::Technique *const technique = material->getTechnique(0U);
    Ogre::Pass *const base =
        technique != nullptr && technique->getNumPasses() != 0U
            ? technique->getPass(0U)
            : nullptr;
    Ogre::TextureUnitState *const diffuse =
        base != nullptr && base->getNumTextureUnitStates() != 0U
            ? base->getTextureUnitState(0U)
            : nullptr;
    if (technique == nullptr || base == nullptr || diffuse == nullptr ||
        !IsExactAlexisDiffuseProjection(*technique, *base,
                                        material->getName(),
                                        diffuse->getTextureName())) {
      return false;
    }
    Ogre::Pass *const pass = technique->getPass(1U);
    Ogre::TextureUnitState *const unit =
        pass != nullptr && pass->getNumTextureUnitStates() != 0U
            ? pass->getTextureUnitState(0U)
            : nullptr;
    const Ogre::SamplerPtr sampler =
        unit != nullptr ? unit->getSampler() : Ogre::SamplerPtr{};
    const Ogre::TexturePtr texture =
        unit != nullptr ? unit->_getTexturePtr() : Ogre::TexturePtr{};
    if (pass == nullptr || unit == nullptr || !sampler || !texture ||
        !texture->isLoaded() || !HasAvailableNamedTextureSource(*unit) ||
        !IsExactManagedSpecularTextureUnitSemantic(*unit) ||
        !ManagedReceiptOwnsNativeTexture(receipt, *texture,
                                         authenticated_resolver,
                                         selected_resolver)) {
      return false;
    }
    ManagedSpecularNativeFacts candidate;
    candidate.pass = pass;
    candidate.unit = unit;
    candidate.sampler = sampler;
    candidate.texture = texture;
    candidate.pass_observation = ObserveExactPass(*pass);
    candidate.sampler_observation = ObserveExactSampler(*sampler);
    if (!ObserveExactTexture(*unit, *texture,
                             candidate.texture_observation)) {
      return false;
    }
    output = std::move(candidate);
    return true;
  } catch (...) {
    return false;
  }
}

bool RevalidateManagedSpecularNativeProjection(
    const Projection &projection,
    const Render::IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const Render::IOgre14SelectedTextureSourceResolver &selected_resolver)
    noexcept {
  const Render::ManagedMaterialDeclaration *const declaration =
      projection.managed_binding.declaration();
  const Render::ManagedMaterialTextureSourceReceipt *const receipt =
      declaration != nullptr
          ? declaration->source_receipt(
                Render::ManagedMaterialTextureSlot::SPECULAR)
          : nullptr;
  ManagedSpecularNativeFacts fresh;
  return receipt != nullptr && projection.managed_native_material_owner &&
         projection.managed_binding.Revalidate(authenticated_resolver,
                                               selected_resolver) &&
         ObserveManagedSpecularNativeFacts(
             projection.managed_native_material_owner, *receipt,
             authenticated_resolver, selected_resolver, fresh) &&
         reinterpret_cast<std::uintptr_t>(fresh.pass) ==
             projection.managed_specular_pass_pointer_token &&
         reinterpret_cast<std::uintptr_t>(fresh.unit) ==
             projection.managed_specular_unit_pointer_token &&
         reinterpret_cast<std::uintptr_t>(fresh.sampler.get()) ==
             projection.managed_specular_sampler_pointer_token &&
         reinterpret_cast<std::uintptr_t>(fresh.texture.get()) ==
             projection.managed_specular_texture_pointer_token &&
         MatchExactPassObservation(
             projection.managed_specular_pass_observation,
             fresh.pass_observation) &&
         MatchOgreNextDemoExactSamplerObservation(
             projection.managed_specular_sampler_observation,
             fresh.sampler_observation) &&
         MatchOgreNextDemoExactTextureObservation(
             projection.managed_specular_texture_observation,
             fresh.texture_observation);
}

bool RevalidateCuratedCityWorldProjection(
    const Projection &projection,
    const Render::IOgre14AuthenticatedMaterialScriptResolver &script_resolver,
    const Render::IOgre14AuthenticatedTextureResolver &texture_resolver,
    const Render::IOgre14SelectedTextureSourceResolver &selected_resolver)
    noexcept {
  try {
    const OgreNextDemoCuratedCityWorldMaterial *const policy =
        FindOgreNextDemoCuratedCityWorldMaterial(
            projection.curated_native_material_owner
                ? projection.curated_native_material_owner->getName()
                : std::string{});
    if (!projection.curated_cityworld || policy == nullptr ||
        policy->review_identity_sha256 !=
            projection.curated_review_identity_sha256 ||
        !projection.curated_native_material_owner ||
        static_cast<std::uint64_t>(
            projection.curated_native_material_owner->getStateCount()) !=
            projection.curated_material_state_count ||
        !projection.curated_script_resolution.initialized() ||
        !projection.curated_specular_receipt.initialized() ||
        !projection.curated_specular_binding.initialized()) {
      return false;
    }
    CuratedCityWorldNativeFacts fresh;
    const bool observed =
        ObserveCuratedCityWorldNativeFacts(
            projection.curated_native_material_owner, *policy,
            Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE,
            script_resolver, texture_resolver, selected_resolver, fresh,
            nullptr, &projection.curated_specular_receipt);
    const bool pass_pointer = reinterpret_cast<std::uintptr_t>(fresh.pass) ==
                              projection.curated_pass_pointer_token;
    const bool pass_observation = MatchExactPassObservation(
        projection.pass_observation, fresh.pass_observation);
    const bool script_receipt = SharesCuratedCityWorldScriptReceipt(
        fresh.script_resolution, projection.curated_script_resolution);
    const bool specular_receipt =
        fresh.specular_receipt.SharesImmutableStateWith(
            projection.curated_specular_receipt);
    const bool stored_binding = projection.curated_specular_binding.Revalidate(
        texture_resolver, selected_resolver);
    if (!observed || !pass_pointer || !pass_observation || !script_receipt ||
        !specular_receipt || !stored_binding) {
      return false;
    }
    for (std::size_t index = 0U; index < fresh.units.size(); ++index) {
      if (reinterpret_cast<std::uintptr_t>(fresh.units[index]) !=
              projection.curated_unit_pointer_tokens[index] ||
          reinterpret_cast<std::uintptr_t>(fresh.samplers[index].get()) !=
              projection.curated_sampler_pointer_tokens[index] ||
          reinterpret_cast<std::uintptr_t>(fresh.textures[index].get()) !=
              projection.curated_texture_pointer_tokens[index] ||
          !SharesCuratedCityWorldTextureReceipt(
              fresh.texture_resolutions[index],
              projection.curated_texture_resolutions[index]) ||
          !MatchOgreNextDemoExactSamplerObservation(
              projection.curated_sampler_observations[index],
              fresh.sampler_observations[index]) ||
          !MatchOgreNextDemoExactTextureObservation(
              projection.curated_texture_observations[index],
              fresh.texture_observations[index])) {
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

struct ProjectionDecision final {
  std::string exact_resource_group;
  std::string exact_material_name;
  Render::Ogre14GraphicsSceneMaterialCull exact_cull =
      Render::Ogre14GraphicsSceneMaterialCull::NONE;
  bool projection_candidate = false;
  bool has_authored_uv0 = false;
  bool projected = false;
  OgreNextDemoTextureProjectionExclusion exclusion =
      OgreNextDemoTextureProjectionExclusion::NONE;
  std::string projection_key;
};

Render::ValidationResult CaptureAuthenticatedTextureSource(
    const Ogre::TexturePtr &native_texture,
    const Ogre::TextureUnitState &native_unit,
    const OgreNextDemoExactTextureObservation &initial_texture_observation,
    const Render::IOgre14AuthenticatedTextureResolver &resolver,
    const Render::Ogre14AuthenticatedTextureResolution &resolution,
    std::string_view debug_token, Render::TextureResourceDescriptor &output,
    Render::Ogre14AuthenticatedTextureReceipt &output_receipt,
    AuthenticatedTextureProvenance &output_provenance,
    std::string &output_content_decode_key,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
    OgreNextDemoTextureNormalizationObservation &output_normalization) {
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

  Render::Ogre14SourceTextureBc1AlphaMode authorized_bc1_mode =
      Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  Render::ValidationResult validation = ResolveOgreNextDemoBc1AlphaMode(
      metadata->dds.kind == Render::Ogre14SourceDdsHeaderKind::LEGACY &&
          metadata->dds.four_cc == kFourCcDxt1,
      alpha_policy, false, authorized_bc1_mode);
  if (!validation) {
    validation.field = "ogre_next_demo.material.authenticated." +
                       validation.field;
    return validation;
  }

  const Render::Ogre14SourceTextureDecodeOptions options =
      BuildAuthenticatedDecodeOptions(*metadata, alpha_policy);
  AuthenticatedTextureProvenance provenance;
  std::string content_decode_key;
  validation = BuildAuthenticatedTextureProvenance(
      *native_texture, resolver, resolution, options,
      initial_texture_observation, alpha_policy, provenance,
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
  OgreNextDemoTextureNormalizationObservation normalization;
  validation = BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
      std::move(decoded), initial_texture_observation.source_width,
      initial_texture_observation.source_height,
      "OgreNextDemoPbrTexture/" + std::string(debug_token), alpha_policy,
      candidate,
      &normalization);
  if (!validation) {
    return validation;
  }

  // The receipt must remain the exact current ContentManager publication
  // immediately after decode and immediately before the candidate can escape.
  // There is deliberately no native-buffer readback fallback beyond this point.
  OgreNextDemoExactTextureObservation final_texture_observation;
  validation = ObserveExactTexture(native_unit, *native_texture,
                                   final_texture_observation);
  if (!validation || !native_texture->isLoaded() ||
      native_texture->getStateCount() != native_state_count ||
      !MatchOgreNextDemoExactTextureObservation(initial_texture_observation,
                                                final_texture_observation) ||
      !resolver.RevalidateAuthenticatedTexture(*native_texture, resolution)) {
    return validation
               ? Failure(Render::ValidationCode::REVISION_MISMATCH,
                         "ogre_next_demo.material.authenticated.final_"
                         "revalidation",
                         "loaded texture or authenticated source authority "
                         "changed during source decode")
               : validation;
  }
  AuthenticatedTextureProvenance final_provenance;
  std::string final_content_decode_key;
  validation = BuildAuthenticatedTextureProvenance(
      *native_texture, resolver, resolution, options, final_texture_observation,
      alpha_policy, final_provenance, final_content_decode_key);
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
  output_normalization = normalization;
  return Render::ValidationResult::Success();
}

Render::ValidationResult TryCaptureOrdinaryTextureSource(
    const Ogre::TexturePtr &native_texture,
    const Ogre::TextureUnitState &native_unit,
    const OgreNextDemoExactTextureObservation &initial_texture_observation,
    const Render::IOgre14SelectedTextureSourceResolver &resolver,
    const Render::Ogre14SelectedTextureSourceResolution &resolution,
    std::string_view debug_token, Render::TextureResourceDescriptor &output,
    Render::Ogre14SelectedTextureSourceReceipt &output_receipt,
    OrdinaryTextureProvenance &output_provenance,
    std::string &output_content_decode_key, bool &captured,
    OgreNextDemoTextureProjectionExclusion &exclusion,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
    OgreNextDemoTextureNormalizationObservation &output_normalization) {
  captured = false;
  exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
  if (!native_texture || !resolution.initialized()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.ordinary.resolution",
                   "ordinary source decode requires one exact loaded texture "
                   "resolution");
  }
  const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
      resolution.source_receipt();
  if (receipt == nullptr || receipt->metadata() == nullptr ||
      receipt->source_bytes() == nullptr || receipt->source_size() == 0U) {
    return Failure(
        Render::ValidationCode::MISSING_REFERENCE,
        "ogre_next_demo.material.ordinary.receipt",
        "ordinary resolution has no immutable selected source bytes");
  }

  Render::Ogre14SourceTextureBc1AlphaMode authorized_bc1_mode =
      Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  const Render::ValidationResult bc1_validation =
      ResolveOgreNextDemoBc1AlphaMode(
          IsLegacyDxt1Source(receipt->source_bytes(), receipt->source_size()),
          alpha_policy, false, authorized_bc1_mode);
  if (!bc1_validation) {
    exclusion = OgreNextDemoTextureProjectionExclusion::
        AMBIGUOUS_BC1_ALPHA_SEMANTIC;
    return Render::ValidationResult::Success();
  }

  const Render::Ogre14SourceTextureDecodeOptions options =
      BuildOrdinaryDecodeOptions(*receipt, alpha_policy);
  OrdinaryTextureProvenance provenance;
  std::string content_decode_key;
  Render::ValidationResult validation = BuildOrdinaryTextureProvenance(
      *native_texture, resolver, resolution, options,
      initial_texture_observation, alpha_policy, provenance,
      content_decode_key);
  if (!validation) {
    return validation;
  }
  if (receipt->source_size() > options.maximum_encoded_bytes ||
      receipt->source_size() > std::vector<std::uint8_t>{}.max_size()) {
    exclusion = OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED;
    return Render::ValidationResult::Success();
  }

  const std::size_t native_state_count = native_texture->getStateCount();
  std::vector<std::uint8_t> encoded_source(receipt->source_bytes(),
                                           receipt->source_bytes() +
                                               receipt->source_size());
  Render::Ogre14DecodedSourceTexture decoded;
  validation = Render::DecodeOgre14SourceTexture(encoded_source, options,
                                                 decoded, nullptr);
  if (!validation) {
    if (validation.field == "source_texture.decoder.allocation") {
      validation.field = "ogre_next_demo.material.ordinary." + validation.field;
      return validation;
    }
    exclusion =
        validation.field == "source_texture.container"
            ? OgreNextDemoTextureProjectionExclusion::
                  UNSUPPORTED_SOURCE_CONTAINER
            : OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED;
    return Render::ValidationResult::Success();
  }

  Render::TextureResourceDescriptor candidate;
  OgreNextDemoTextureNormalizationObservation normalization;
  validation = BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
      std::move(decoded), initial_texture_observation.source_width,
      initial_texture_observation.source_height,
      "OgreNextDemoPbrTexture/" + std::string(debug_token), alpha_policy,
      candidate,
      &normalization);
  if (!validation) {
    exclusion = OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED;
    return Render::ValidationResult::Success();
  }

  OgreNextDemoExactTextureObservation final_texture_observation;
  validation = ObserveExactTexture(native_unit, *native_texture,
                                   final_texture_observation);
  if (!validation || !native_texture->isLoaded() ||
      native_texture->getStateCount() != native_state_count ||
      !MatchOgreNextDemoExactTextureObservation(initial_texture_observation,
                                                final_texture_observation) ||
      !resolver.RevalidateSelectedTextureSource(*native_texture, resolution)) {
    return validation
               ? Failure(Render::ValidationCode::REVISION_MISMATCH,
                         "ogre_next_demo.material.ordinary.final_revalidation",
                         "loaded texture or ordinary selected-source authority "
                         "changed during source decode")
               : validation;
  }
  OrdinaryTextureProvenance final_provenance;
  std::string final_content_decode_key;
  validation = BuildOrdinaryTextureProvenance(
      *native_texture, resolver, resolution, options, final_texture_observation,
      alpha_policy, final_provenance, final_content_decode_key);
  if (!validation || final_content_decode_key != content_decode_key) {
    return validation
               ? Failure(Render::ValidationCode::REVISION_MISMATCH,
                         "ogre_next_demo.material.ordinary.provenance_"
                         "revalidation",
                         "ordinary selected-source provenance changed during "
                         "decode")
               : validation;
  }

  output = std::move(candidate);
  output_receipt = *receipt;
  output_provenance = std::move(final_provenance);
  output_content_decode_key = std::move(final_content_decode_key);
  output_normalization = normalization;
  captured = true;
  return Render::ValidationResult::Success();
}

Render::ValidationResult CaptureManagedSpecularTextureSource(
    const Render::ManagedMaterialTextureSourceReceipt &receipt,
    std::string_view debug_token, Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation &output_normalization) {
  const Render::ManagedMaterialTextureSourceIdentity *const identity =
      receipt.identity();
  if (!receipt.initialized() || identity == nullptr ||
      receipt.source_bytes() == nullptr || receipt.source_size() == 0U ||
      identity->byte_count != receipt.source_size()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.managed.specular_receipt",
                   "managed specular declaration has no immutable source bytes");
  }
  const Render::Ogre14SourceTextureDecodeOptions options =
      BuildManagedDecodeOptions(
          receipt, Render::Ogre14SourceTextureColorSemantic::LINEAR_DATA,
          OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE);
  if (receipt.source_size() > options.maximum_encoded_bytes ||
      receipt.source_size() > std::vector<std::uint8_t>{}.max_size()) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.material.managed.specular_bytes",
                   "managed specular source exceeds the product decode cap");
  }
  std::vector<std::uint8_t> encoded(receipt.source_bytes(),
                                    receipt.source_bytes() +
                                        receipt.source_size());
  Render::Ogre14DecodedSourceTexture decoded;
  Render::ValidationResult validation =
      Render::DecodeOgre14SourceTexture(encoded, options, decoded, nullptr);
  if (!validation) {
    validation.field = "ogre_next_demo.material.managed.specular." +
                       validation.field;
    return validation;
  }
  const std::uint32_t width = decoded.width;
  const std::uint32_t height = decoded.height;
  return BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
      std::move(decoded), width, height,
      "OgreNextDemoLinearSpecular/" + std::string(debug_token), output,
      &output_normalization);
}

} // namespace

struct CapturedManagedSpecularTexture final {
  Render::ManagedMaterialTextureSourceReceipt receipt;
  std::uint64_t source_id = 0U;
  std::shared_ptr<const Render::RenderAssetPayload> payload;
  OgreNextDemoTextureNormalizationObservation normalization_observation;
};

/// One texture's authored bytes, resolved through whichever domain actually
/// owns them. CityWorld - the content this widening exists for - ships as an
/// authenticated package, so restricting the proof to the ordinary
/// selected-source domain would silently refuse every candidate it targets.
struct GlowOverlayResolvedSource final {
  bool authenticated = false;
  Render::Ogre14AuthenticatedTextureResolution authenticated_resolution;
  Render::Ogre14SelectedTextureSourceResolution ordinary_resolution;
};

/// Memoised outcome of one authored-texel glow proof. `verified` is only ever
/// stored for a pair that passed; the two resolutions are retained so a later
/// reuse can re-assert that the very bytes the proof ran over are still the
/// bytes the resolver would hand out today.
struct GlowOverlayContentVerdict final {
  bool verified = false;
  GlowOverlayResolvedSource base_source;
  GlowOverlayResolvedSource overlay_source;
};

struct MaterialCache final {
  OgreNextDemoIdentityRegistry identities;
  std::map<std::string, CapturedTexture, std::less<>> textures;
  std::map<std::string, CapturedSampler, std::less<>> samplers;
  std::map<std::string, CapturedManagedSpecularTexture, std::less<>>
      managed_specular_textures;
  std::map<std::string, Projection, std::less<>> projections;
  std::map<std::string, ProjectionDecision, std::less<>> decisions;
  std::map<std::string, GlowOverlayContentVerdict, std::less<>>
      glow_overlay_verdicts;
  // Apply-derived data is retained only while the structural cache above is
  // byte-for-byte unchanged.  Authority is deliberately not cached: every
  // reachable source is revalidated against the current frame immediately
  // before its retained owners are published.
  bool retained_publication_valid = false;
  std::vector<std::string> retained_used_projection_keys;
  OgreNextDemoCachedProjectionPublicationTransaction retained_publication;
  bool retained_owner_assets_valid = false;
  std::vector<Render::GraphicsSceneAssetInput> retained_owner_assets;
  std::size_t retained_owner_asset_count = 0U;
};

/// Per-texel discharge of clauses 1 and 3 of the additive-equivalence
/// argument. `reject_function`/`reject_value` are the overlay pass's own
/// authored alpha rejection, so this walks exactly the fragments the overlay
/// would have drawn and ignores the ones it discards.
///
/// Only mip 0 is compared, and only at equal dimensions. Both sources are
/// decoded to canonical tightly packed RGBA8, so equal dimensions make the
/// texel correspondence exact and remove any resampling judgement from the
/// proof. Every further mip either side would use is generated from the mip 0
/// proved here by the same box filter, and a filter that is a convex
/// combination cannot widen a bound its inputs already satisfy.
bool AuthoredTexelsProveAddedLightOnly(
    const Render::Ogre14DecodedSourceTexture &base,
    const Render::Ogre14DecodedSourceTexture &overlay,
    Ogre::CompareFunction reject_function,
    std::uint8_t reject_value) noexcept {
  try {
    if (reject_function != Ogre::CMPF_GREATER &&
        reject_function != Ogre::CMPF_GREATER_EQUAL) {
      return false;
    }
    if (base.mip_levels.empty() || overlay.mip_levels.empty()) {
      return false;
    }
    const Render::Ogre14DecodedSourceTextureMip &base_mip = base.mip_levels[0U];
    const Render::Ogre14DecodedSourceTextureMip &overlay_mip =
        overlay.mip_levels[0U];
    if (base_mip.width == 0U || base_mip.height == 0U ||
        base_mip.width != overlay_mip.width ||
        base_mip.height != overlay_mip.height) {
      return false;
    }
    const std::uint64_t required = static_cast<std::uint64_t>(base_mip.width) *
                                   4U;
    if (base_mip.row_pitch_bytes < required ||
        overlay_mip.row_pitch_bytes < required) {
      return false;
    }
    const std::uint64_t base_span =
        base_mip.row_pitch_bytes * static_cast<std::uint64_t>(base_mip.height);
    const std::uint64_t overlay_span =
        overlay_mip.row_pitch_bytes *
        static_cast<std::uint64_t>(overlay_mip.height);
    if (base_mip.rgba8_unorm.size() < base_span ||
        overlay_mip.rgba8_unorm.size() < overlay_span) {
      return false;
    }
    const int limit =
        static_cast<int>(kOgreNextDemoGlowOverlayMaximumKeptTexelDelta);
    for (std::uint32_t row = 0U; row < base_mip.height; ++row) {
      const std::uint8_t *const base_row =
          base_mip.rgba8_unorm.data() +
          static_cast<std::size_t>(row * base_mip.row_pitch_bytes);
      const std::uint8_t *const overlay_row =
          overlay_mip.rgba8_unorm.data() +
          static_cast<std::size_t>(row * overlay_mip.row_pitch_bytes);
      for (std::uint32_t column = 0U; column < base_mip.width; ++column) {
        const std::size_t texel = static_cast<std::size_t>(column) * 4U;
        const std::uint8_t alpha = overlay_row[texel + 3U];
        const bool kept = reject_function == Ogre::CMPF_GREATER
                              ? alpha > reject_value
                              : alpha >= reject_value;
        if (!kept) {
          continue;
        }
        // Clause 1: a surviving fragment must be fully opaque, or
        // `src*a + dst*(1-a)` is a real composite and not a cutout replace.
        if (overlay_row[texel + 3U] != 255U) {
          return false;
        }
        // Clause 3: the surviving artwork must be pass 0's own.
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const int delta = static_cast<int>(overlay_row[texel + channel]) -
                            static_cast<int>(base_row[texel + channel]);
          if (delta > limit || delta < -limit) {
            return false;
          }
        }
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

struct PendingNativeTextureOwner final {
  Ogre::MaterialPtr native_material;
  std::uintptr_t native_pass_pointer_token = 0U;
  std::uintptr_t native_unit_pointer_token = 0U;
  std::uintptr_t native_sampler_pointer_token = 0U;
  OgreNextDemoExactSamplerObservation sampler_observation;
  OgreNextDemoExactTextureObservation texture_observation;
  ExactPassObservation pass_observation;
  bool allow_alexis_approximation = false;
  bool exact_continuous_dust = false;
  bool curated_cityworld = false;
  std::size_t technique_pass_count = 0U;
  std::size_t pass_texture_unit_count = 0U;
  std::size_t unpresented_layer_units = 0U;
  std::size_t unpresented_additive_overlay_passes = 0U;
  std::array<float, 4U> diffuse{};
  std::array<float, 4U> ambient{};
  std::array<float, 4U> specular{};
  std::array<float, 4U> emissive{};
  float shininess = 0.0F;
  std::uint8_t vertex_colour_tracking_token = 0U;
};

struct PendingAuthenticatedTextureObservation final {
  Ogre::TexturePtr native_texture;
  Render::Ogre14AuthenticatedTextureResolution resolution;
  std::vector<PendingNativeTextureOwner> native_owners;
};

struct PendingOrdinaryTextureObservation final {
  Ogre::TexturePtr native_texture;
  Render::Ogre14SelectedTextureSourceResolution resolution;
  std::vector<PendingNativeTextureOwner> native_owners;
};

Render::ValidationResult RevalidatePendingNativeTextureOwners(
    const Ogre::TexturePtr &native_texture,
    const std::vector<PendingNativeTextureOwner> &owners,
    OgreNextDemoExactTextureObservation &fresh_texture_observation) {
  if (!native_texture || owners.empty()) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.pending_native_owner",
                   "reachable texture has no strong native TUS owner");
  }
  bool observed = false;
  OgreNextDemoExactTextureObservation common_observation;
  for (const PendingNativeTextureOwner &owner : owners) {
    if (!owner.native_material ||
        owner.native_material->getNumTechniques() == 0U) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.pending_native_owner",
                     "native material owner disappeared before publication");
    }
    Ogre::Technique *const technique = owner.native_material->getTechnique(0U);
    Ogre::Pass *const pass =
        technique != nullptr && technique->getNumPasses() != 0U
            ? technique->getPass(0U)
            : nullptr;
    Ogre::TextureUnitState *const unit =
        pass != nullptr && pass->getNumTextureUnitStates() != 0U
            ? pass->getTextureUnitState(0U)
            : nullptr;
    const Ogre::SamplerPtr sampler =
        unit != nullptr ? unit->getSampler() : Ogre::SamplerPtr{};
    const bool observed_continuous_dust =
        technique != nullptr && pass != nullptr &&
        IsExactContinuousDustPass(*technique, *pass,
                                  *owner.native_material);
    const bool observed_curated_cityworld =
        owner.curated_cityworld &&
        HasCuratedCityWorldSphericalFamilyShape(owner.native_material);
    // Revalidate the ordinary structural shape through the exact predicate the
    // admission decision used, so the two can never drift apart.
    std::size_t observed_unpresented_layer_units = 0U;
    std::size_t observed_unpresented_additive_overlay_passes = 0U;
    OgreNextDemoTextureProjectionExclusion observed_shape_exclusion =
        OgreNextDemoTextureProjectionExclusion::NONE;
    const bool observed_admissible_legacy_shape =
        technique != nullptr && pass != nullptr &&
        HasAdmissibleLegacyShape(*technique, *pass,
                                 observed_unpresented_layer_units,
                                 observed_unpresented_additive_overlay_passes,
                                 observed_shape_exclusion);
    if (pass == nullptr || unit == nullptr || !sampler ||
        reinterpret_cast<std::uintptr_t>(pass) !=
            owner.native_pass_pointer_token ||
        reinterpret_cast<std::uintptr_t>(unit) !=
            owner.native_unit_pointer_token ||
        reinterpret_cast<std::uintptr_t>(sampler.get()) !=
            owner.native_sampler_pointer_token ||
        unit->_getTexturePtr().get() != native_texture.get() ||
        technique->getNumPasses() != owner.technique_pass_count ||
        pass->getNumTextureUnitStates() != owner.pass_texture_unit_count ||
        owner.exact_continuous_dust != observed_continuous_dust ||
        (!owner.exact_continuous_dust &&
         !IsCanonicalPass(*pass, owner.allow_alexis_approximation)) ||
        !HasAvailableNamedTextureSource(*unit) ||
        !IsCanonicalTextureUnitSemantic(*unit) ||
        (owner.exact_continuous_dust &&
         (native_texture->getName() != "smoke.dds" ||
          !IsExactContinuousDustSampler(ObserveExactSampler(*sampler)))) ||
        (!owner.curated_cityworld && !owner.allow_alexis_approximation &&
         (!observed_admissible_legacy_shape ||
          observed_unpresented_layer_units !=
              owner.unpresented_layer_units ||
          observed_unpresented_additive_overlay_passes !=
              owner.unpresented_additive_overlay_passes)) ||
        (owner.curated_cityworld && !observed_curated_cityworld) ||
        (owner.allow_alexis_approximation &&
         !IsExactAlexisDiffuseProjection(*technique, *pass,
                                         owner.native_material->getName(),
                                         native_texture->getName())) ||
        !MatchExactPassObservation(owner.pass_observation,
                                   ObserveExactPass(*pass)) ||
        !MatchOgreNextDemoExactSamplerObservation(
            owner.sampler_observation, ObserveExactSampler(*sampler))) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.pending_native_owner",
                     "native pass, TUS0, sampler, or texture owner changed "
                     "before publication");
    }
    OgreNextDemoExactTextureObservation current;
    Render::ValidationResult validation =
        ObserveExactTexture(*unit, *native_texture, current);
    if (!validation) {
      return validation;
    }
    if (!MatchOgreNextDemoExactTextureObservation(owner.texture_observation,
                                                  current) ||
        (observed && !MatchOgreNextDemoExactTextureObservation(
                         common_observation, current))) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.pending_native_texture",
                     "native TUS/texture provenance changed before "
                     "publication");
    }
    common_observation = current;
    observed = true;
  }
  fresh_texture_observation = common_observation;
  return Render::ValidationResult::Success();
}

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
  std::map<std::string, PendingOrdinaryTextureObservation, std::less<>>
      ordinary_texture_observations;
  std::set<std::string, std::less<>> source_cache_hits;
  std::set<std::string, std::less<>> eligible_texture_keys;
  std::set<std::string, std::less<>> projected_texture_keys;
  std::map<std::string, OgreNextDemoExactTextureObservation, std::less<>>
      active_native_texture_observations;
  std::map<std::string, OgreNextDemoTextureNormalizationObservation,
           std::less<>>
      active_normalization_observations;
  std::set<std::string, std::less<>> curated_cityworld_observed;
  std::set<std::string, std::less<>> curated_cityworld_admitted;
  std::set<std::string, std::less<>> curated_cityworld_matte;
  std::set<std::string, std::less<>> uncurated_spherical_family_matte;
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

bool OgreNextDemoMaterialSource::BindOrdinarySelectedTextureSourceResolver(
    const Render::IOgre14SelectedTextureSourceResolver &resolver) noexcept {
  if (ordinary_texture_source_resolver_ == &resolver) {
    return true;
  }
  if (ordinary_texture_source_resolver_ != nullptr || pending_ != nullptr ||
      committed_ == nullptr || !committed_->cache ||
      !committed_->cache->textures.empty() ||
      !committed_->cache->projections.empty() ||
      !committed_->cache->decisions.empty()) {
    return false;
  }
  ordinary_texture_source_resolver_ = &resolver;
  return true;
}

bool OgreNextDemoMaterialSource::BindAuthenticatedMaterialScriptResolver(
    const Render::IOgre14AuthenticatedMaterialScriptResolver &resolver)
    noexcept {
  if (material_script_resolver_ == &resolver) {
    return true;
  }
  if (material_script_resolver_ != nullptr || pending_ != nullptr ||
      committed_ == nullptr || !committed_->cache ||
      !committed_->cache->textures.empty() ||
      !committed_->cache->projections.empty() ||
      !committed_->cache->decisions.empty()) {
    return false;
  }
  material_script_resolver_ = &resolver;
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
    pending_->ordinary_texture_observations.clear();
    pending_->source_cache_hits.clear();
    pending_->eligible_texture_keys.clear();
    pending_->projected_texture_keys.clear();
    pending_->active_native_texture_observations.clear();
    pending_->active_normalization_observations.clear();
    pending_->curated_cityworld_observed.clear();
    pending_->curated_cityworld_admitted.clear();
    pending_->curated_cityworld_matte.clear();
    pending_->uncurated_spherical_family_matte.clear();
    pending_->counters = {};
    return true;
  } catch (...) {
    pending_.reset();
    return false;
  }
}

bool OgreNextDemoMaterialSource::VerifyAdditiveEquivalentGlowOverlayContent(
    const Ogre::Pass &base_pass, const Ogre::Pass &overlay_pass) noexcept {
  try {
    if (pending_ == nullptr || !pending_->capture_open || !pending_->cache ||
        ordinary_texture_source_resolver_ == nullptr ||
        texture_resolver_ == nullptr) {
      return false;
    }
    Ogre::Pass &mutable_base = const_cast<Ogre::Pass &>(base_pass);
    Ogre::Pass &mutable_overlay = const_cast<Ogre::Pass &>(overlay_pass);
    if (mutable_base.getNumTextureUnitStates() == 0U ||
        mutable_overlay.getNumTextureUnitStates() != 1U) {
      return false;
    }
    const Ogre::TextureUnitState *const base_unit =
        mutable_base.getTextureUnitState(0U);
    const Ogre::TextureUnitState *const overlay_unit =
        mutable_overlay.getTextureUnitState(0U);
    if (base_unit == nullptr || overlay_unit == nullptr) {
      return false;
    }
    const Ogre::TexturePtr base_texture = base_unit->_getTexturePtr();
    const Ogre::TexturePtr overlay_texture = overlay_unit->_getTexturePtr();
    if (!base_texture || !overlay_texture ||
        base_texture->getName().empty() ||
        overlay_texture->getName().empty()) {
      return false;
    }
    // Same UV0 texel for the same fragment is what makes a texel-by-texel
    // comparison meaningful at all. The canonical-semantic gate already fixed
    // both units to coordinate set 0 with an identity transform; matching the
    // sampler addressing closes the remaining way two identical UVs could
    // still fetch different texels outside the unit square.
    const Ogre::SamplerPtr base_sampler = base_unit->getSampler();
    const Ogre::SamplerPtr overlay_sampler = overlay_unit->getSampler();
    if (!base_sampler || !overlay_sampler ||
        !MatchOgreNextDemoExactSamplerObservation(
            ObserveExactSampler(*base_sampler),
            ObserveExactSampler(*overlay_sampler))) {
      return false;
    }
    const Ogre::CompareFunction reject_function =
        overlay_pass.getAlphaRejectFunction();
    const std::uint8_t reject_value = overlay_pass.getAlphaRejectValue();

    std::string verdict_key;
    AppendField(verdict_key, kOgreNextDemoAdditiveEquivalentGlowOverlayPolicy);
    AppendNumber(verdict_key,
                 kOgreNextDemoGlowOverlayMaximumKeptTexelDelta);
    AppendField(verdict_key, base_texture->getGroup());
    AppendField(verdict_key, base_texture->getName());
    AppendField(verdict_key, overlay_texture->getGroup());
    AppendField(verdict_key, overlay_texture->getName());
    AppendNumber(verdict_key, static_cast<std::uint64_t>(reject_function));
    AppendNumber(verdict_key, static_cast<std::uint64_t>(reject_value));

    const auto cached = pending_->cache->glow_overlay_verdicts.find(verdict_key);
    if (cached != pending_->cache->glow_overlay_verdicts.end()) {
      // A stored refusal is never retried. It can only ever be
      // over-conservative - the material stays matte under its truthful
      // reason - and re-decoding a pair that already failed once per capture
      // would be the single most expensive thing this projection does.
      if (!cached->second.verified) {
        return false;
      }
      // A stored proof is only as good as the bytes it ran over, so re-assert
      // that the owning resolver still hands out those exact sources.
      const auto revalidate =
          [this](const Ogre::TexturePtr &texture,
                 const GlowOverlayResolvedSource &source) {
            return source.authenticated
                       ? texture_resolver_->RevalidateAuthenticatedTexture(
                             *texture, source.authenticated_resolution)
                       : ordinary_texture_source_resolver_
                             ->RevalidateSelectedTextureSource(
                                 *texture, source.ordinary_resolution);
          };
      return revalidate(base_texture, cached->second.base_source) &&
             revalidate(overlay_texture, cached->second.overlay_source);
    }

    if (!base_texture->isLoaded() || !overlay_texture->isLoaded()) {
      // Transient, exactly like every other source-unavailable refusal: say no
      // for this capture but store nothing, so a later capture that finds both
      // sources loaded still gets to run the proof.
      return false;
    }

    GlowOverlayContentVerdict verdict;
    bool sources_available = true;
    // PRESERVE_STRAIGHT, never the FORCE_OPAQUE policy an opaque projection
    // would use: the overlay's authored alpha IS the mask this proof is about,
    // and forcing it opaque would destroy the very evidence being weighed.
    constexpr OgreNextDemoTextureAlphaPolicy kProofAlphaPolicy =
        OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT;
    const auto decode_bytes =
        [](const std::uint8_t *bytes, std::size_t size,
           const Render::Ogre14SourceTextureDecodeOptions &options,
           Render::Ogre14DecodedSourceTexture &decoded) {
          if (bytes == nullptr || size == 0U ||
              size > options.maximum_encoded_bytes) {
            return false;
          }
          try {
            const std::vector<std::uint8_t> encoded(bytes, bytes + size);
            return static_cast<bool>(Render::DecodeOgre14SourceTexture(
                encoded, options, decoded, nullptr));
          } catch (...) {
            return false;
          }
        };
    // Read whichever domain owns each texture's authored bytes. Both retain
    // exactly those bytes under their own receipt, and the proof is a
    // statement about the authored content, not about which registry holds it.
    const auto resolve =
        [this, &sources_available, &decode_bytes](
            const Ogre::TexturePtr &texture,
            GlowOverlayResolvedSource &resolved,
            Render::Ogre14DecodedSourceTexture &decoded) {
          if (texture_resolver_->RequiresAuthenticatedTextureSource(*texture)) {
            resolved.authenticated = true;
            if (!texture_resolver_->ResolveAuthenticatedTexture(
                    *texture, resolved.authenticated_resolution) ||
                !resolved.authenticated_resolution.initialized()) {
              sources_available = false;
              return false;
            }
            const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
                resolved.authenticated_resolution.source_receipt();
            const Render::Ogre14AuthenticatedTextureReceiptMetadata *const
                metadata = receipt != nullptr ? receipt->metadata() : nullptr;
            if (receipt == nullptr || !receipt->initialized() ||
                metadata == nullptr || receipt->source_bytes() == nullptr ||
                receipt->source_size() == 0U) {
              sources_available = false;
              return false;
            }
            return decode_bytes(
                receipt->source_bytes(), receipt->source_size(),
                BuildAuthenticatedDecodeOptions(*metadata, kProofAlphaPolicy),
                decoded);
          }
          resolved.authenticated = false;
          if (!ordinary_texture_source_resolver_->ResolveSelectedTextureSource(
                  *texture, resolved.ordinary_resolution) ||
              !resolved.ordinary_resolution.initialized()) {
            sources_available = false;
            return false;
          }
          const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
              resolved.ordinary_resolution.source_receipt();
          if (receipt == nullptr || !receipt->initialized() ||
              receipt->metadata() == nullptr ||
              receipt->source_bytes() == nullptr ||
              receipt->source_size() == 0U) {
            sources_available = false;
            return false;
          }
          return decode_bytes(
              receipt->source_bytes(), receipt->source_size(),
              BuildOrdinaryDecodeOptions(*receipt, kProofAlphaPolicy), decoded);
        };

    Render::Ogre14DecodedSourceTexture base_decoded;
    Render::Ogre14DecodedSourceTexture overlay_decoded;
    verdict.verified =
        resolve(base_texture, verdict.base_source, base_decoded) &&
        resolve(overlay_texture, verdict.overlay_source, overlay_decoded) &&
        // An overlay with no alpha channel at all keeps every texel, so this
        // still requires it to be pass 0's artwork before admitting it.
        AuthoredTexelsProveAddedLightOnly(base_decoded, overlay_decoded,
                                          reject_function, reject_value);
    const bool result = verdict.verified;
    // Only a DEFINITIVE outcome is memoised. A verdict reached because both
    // authored sources were read and compared is permanent - the bytes decide
    // it and the bytes are immutable - but one reached because a source could
    // not be resolved yet is transient, and caching that would freeze a
    // material matte over a resolver state that later recovers.
    if (!result && !sources_available) {
      return false;
    }
    EnsurePendingCacheWritable();
    pending_->cache->glow_overlay_verdicts.emplace(std::move(verdict_key),
                                                   std::move(verdict));
    return result;
  } catch (...) {
    return false;
  }
}

void OgreNextDemoMaterialSource::EnsurePendingCachePrivateForDerivedState() {
  if (pending_ == nullptr || !pending_->capture_open || !pending_->cache) {
    throw std::logic_error("material projection has no writable transaction");
  }
  if (!pending_->cache.unique()) {
    pending_->cache = std::make_shared<MaterialCache>(*pending_->cache);
  }
}

void OgreNextDemoMaterialSource::EnsurePendingCacheWritable() {
  EnsurePendingCachePrivateForDerivedState();
  pending_->cache->retained_publication_valid = false;
  pending_->cache->retained_used_projection_keys.clear();
  pending_->cache->retained_publication = {};
  pending_->cache->retained_owner_assets_valid = false;
  pending_->cache->retained_owner_assets.clear();
  pending_->cache->retained_owner_asset_count = 0U;
}

bool OgreNextDemoMaterialSource::TryProjectCurrent(
    const Ogre::MaterialPtr &native_material, bool has_authored_uv0,
    const Render::Ogre14ManagedMaterialDeclarationBinding *managed_binding,
    bool allow_continuous_dust,
    Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
    std::string &selected_projection_key, bool allow_new_projection,
    OgreNextDemoTextureProjectionExclusion &exclusion,
    Render::ValidationResult &failure) {
  selected_projection_key.clear();
  exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
  failure = Render::ValidationResult::Success();
  if (pending_ == nullptr || !pending_->capture_open) {
    failure = Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                      "ogre_next_demo.material.pending",
                      "material projection has no open capture transaction");
    return false;
  }
  if (!has_authored_uv0) {
    exclusion = OgreNextDemoTextureProjectionExclusion::MISSING_AUTHORED_UV0;
    return false;
  }
  if (!native_material || native_material->getName().empty() ||
      native_material->getNumTechniques() == 0U) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
    return false;
  }
  Ogre::Technique *const technique = native_material->getTechnique(0U);
  Ogre::Pass *const pass =
      technique != nullptr && technique->getNumPasses() != 0U
          ? technique->getPass(0U)
          : nullptr;
  const bool allow_alexis_approximation =
      OgreNextDemoAllowsAlexisTUS0Approximation(native_material->getGroup(),
                                                native_material->getName());
  const OgreNextDemoCuratedCityWorldMaterial *const curated_policy =
      FindOgreNextDemoCuratedCityWorldMaterial(native_material->getName());
  const bool allow_curated_cityworld = curated_policy != nullptr;
  CuratedCityWorldNativeFacts curated_native;
  bool curated_source_temporarily_unavailable = false;
  if (allow_curated_cityworld &&
      (managed_binding != nullptr || material_script_resolver_ == nullptr ||
       texture_resolver_ == nullptr ||
       ordinary_texture_source_resolver_ == nullptr ||
       !ObserveCuratedCityWorldNativeFacts(
           native_material, *curated_policy, input.cull,
           *material_script_resolver_, *texture_resolver_,
           *ordinary_texture_source_resolver_, curated_native,
           &curated_source_temporarily_unavailable))) {
    exclusion = curated_source_temporarily_unavailable
                    ? OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE
                    : OgreNextDemoTextureProjectionExclusion::
                          MATERIAL_STATE_UNSUPPORTED;
    return false;
  }
  if (pass == nullptr || pass->getNumTextureUnitStates() == 0U) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
    return false;
  }
  const ExactPassObservation pass_observation = ObserveExactPass(*pass);
  const bool exact_continuous_dust =
      allow_continuous_dust &&
      IsExactContinuousDustPass(*technique, *pass, *native_material);
  Render::MaterialBlendMode blend_mode = Render::MaterialBlendMode::REPLACE;
  Render::MaterialAlphaTestMode alpha_test_mode =
      Render::MaterialAlphaTestMode::DISABLED;
  if (allow_continuous_dust && !exact_continuous_dust) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STATE_UNSUPPORTED;
    return false;
  }
  if (exact_continuous_dust) {
    blend_mode = Render::MaterialBlendMode::LEGACY_STRAIGHT_ALPHA;
    alpha_test_mode = Render::MaterialAlphaTestMode::GREATER;
  } else if (!ClassifyCanonicalPass(pass_observation,
                                    allow_alexis_approximation, blend_mode,
                                    alpha_test_mode)) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STATE_UNSUPPORTED;
    return false;
  }
  std::size_t unpresented_layer_units = 0U;
  std::size_t unpresented_additive_overlay_passes = 0U;
  // Admission - and only admission - carries the authored-texel proof for an
  // alpha-blended overlay that merely declares the additive-equivalent glow
  // shape.
  struct AdmissionGlowVerifier final : LegacyGlowOverlayContentVerifier {
    OgreNextDemoMaterialSource *owner = nullptr;
    bool VerifyAdditiveEquivalentGlowOverlay(
        const Ogre::Pass &base_pass,
        const Ogre::Pass &overlay_pass) noexcept override {
      return owner != nullptr &&
             owner->VerifyAdditiveEquivalentGlowOverlayContent(base_pass,
                                                               overlay_pass);
    }
  };
  AdmissionGlowVerifier glow_verifier;
  glow_verifier.owner = this;
  if (!allow_alexis_approximation && !allow_curated_cityworld &&
      !HasAdmissibleLegacyShape(*technique, *pass, unpresented_layer_units,
                                unpresented_additive_overlay_passes, exclusion,
                                &glow_verifier)) {
    return false;
  }
  Ogre::TextureUnitState *const unit = pass->getTextureUnitState(0U);
  if (unit == nullptr) {
    exclusion = OgreNextDemoTextureProjectionExclusion::
        TEXTURE_UNIT_STRUCTURE_UNSUPPORTED;
    return false;
  }
  if (!HasAvailableNamedTextureSource(*unit) || !unit->_getTexturePtr()) {
    exclusion = OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
    return false;
  }
  if (!IsCanonicalTextureUnitSemantic(*unit)) {
    exclusion = UsesTextureAlphaCombine(*unit)
                    ? OgreNextDemoTextureProjectionExclusion::
                          TEXTURE_ALPHA_COMBINE_UNSUPPORTED
                    : OgreNextDemoTextureProjectionExclusion::
                          TEXTURE_UNIT_SEMANTIC_UNSUPPORTED;
    return false;
  }
  const Ogre::SamplerPtr native_sampler = unit->getSampler();
  if (!native_sampler) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::SAMPLER_STATE_UNSUPPORTED;
    return false;
  }
  const OgreNextDemoExactSamplerObservation sampler_observation =
      ObserveExactSampler(*native_sampler);
  if (exact_continuous_dust &&
      !IsExactContinuousDustSampler(sampler_observation)) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::SAMPLER_STATE_UNSUPPORTED;
    return false;
  }
  Render::SamplerResourceDescriptor sampler_preflight;
  if (!BuildOgreNextDemoSamplerDescriptor(sampler_observation, 1U, "preflight",
                                          sampler_preflight)) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::SAMPLER_STATE_UNSUPPORTED;
    return false;
  }
  const Ogre::TexturePtr native_texture = unit->_getTexturePtr();
  if (!native_texture || native_texture->getName().empty()) {
    exclusion = OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
    return false;
  }
  if (exact_continuous_dust &&
      (native_texture->getName() != "smoke.dds" ||
       managed_binding != nullptr)) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STATE_UNSUPPORTED;
    return false;
  }
  if (allow_alexis_approximation &&
      (native_texture->getGroup() != native_material->getGroup() ||
       !IsExactAlexisDiffuseProjection(*technique, *pass,
                                       native_material->getName(),
                                       native_texture->getName()))) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::ALEXIS_APPROXIMATION_UNSAFE;
    return false;
  }
  Render::ValidationResult texture_preflight_validation =
      PreflightTextureIdentity(native_texture, exclusion);
  if (!texture_preflight_validation) {
    failure = std::move(texture_preflight_validation);
    return false;
  }
  if (exclusion != OgreNextDemoTextureProjectionExclusion::NONE) {
    return false;
  }
  OgreNextDemoExactTextureObservation exact_texture_observation;
  Render::ValidationResult exact_texture_validation =
      ObserveExactTexture(*unit, *native_texture, exact_texture_observation);
  if (!exact_texture_validation) {
    failure = std::move(exact_texture_validation);
    return false;
  }
  const OgreNextDemoTextureAlphaPolicy alpha_policy =
      blend_mode != Render::MaterialBlendMode::REPLACE ||
              alpha_test_mode != Render::MaterialAlphaTestMode::DISABLED
          ? OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT
          : OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE;
  const Render::ManagedMaterialDeclaration *managed_declaration = nullptr;
  const Render::ManagedMaterialDeclarationMetadata *managed_metadata = nullptr;
  const Render::ManagedMaterialTextureSourceReceipt *managed_diffuse = nullptr;
  const Render::ManagedMaterialTextureSourceReceipt *managed_specular = nullptr;
  const Render::ManagedMaterialTextureSourceReceipt *managed_damaged = nullptr;
  ManagedSpecularNativeFacts managed_specular_native;
  if (allow_curated_cityworld) {
    managed_specular = &curated_native.specular_receipt;
    managed_specular_native.pass = curated_native.pass;
    managed_specular_native.unit = curated_native.units[1U];
    managed_specular_native.sampler = curated_native.samplers[1U];
    managed_specular_native.texture = curated_native.textures[1U];
    managed_specular_native.pass_observation =
        curated_native.pass_observation;
    managed_specular_native.sampler_observation =
        curated_native.sampler_observations[1U];
    managed_specular_native.texture_observation =
        curated_native.texture_observations[1U];
  }
  if (managed_binding != nullptr) {
    if (!managed_binding->initialized() ||
        !managed_binding->ReferencesExactMaterial(native_material)) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.managed.binding",
          "managed declaration binding does not own the exact material");
      return false;
    }
    managed_declaration = managed_binding->declaration();
    managed_metadata = managed_declaration != nullptr
                           ? managed_declaration->metadata()
                           : nullptr;
    managed_diffuse = managed_declaration != nullptr
                          ? managed_declaration->source_receipt(
                                Render::ManagedMaterialTextureSlot::DIFFUSE)
                          : nullptr;
    managed_specular = managed_declaration != nullptr
                           ? managed_declaration->source_receipt(
                                 Render::ManagedMaterialTextureSlot::SPECULAR)
                           : nullptr;
    managed_damaged = managed_declaration != nullptr
                          ? managed_declaration->source_receipt(
                                Render::ManagedMaterialTextureSlot::
                                    DAMAGED_DIFFUSE)
                          : nullptr;
    const bool resolved_transparent =
        managed_metadata != nullptr &&
        IsManagedTransparentType(managed_metadata->resolved_type);
    const bool declared_transparent =
        managed_metadata != nullptr &&
        IsManagedTransparentType(managed_metadata->declared_type);
    const bool native_transparent =
        blend_mode != Render::MaterialBlendMode::REPLACE;
    const bool transparent_state_matches =
        resolved_transparent
            ? native_transparent && !pass_observation.depth_write
            : !native_transparent &&
                  alpha_test_mode == Render::MaterialAlphaTestMode::DISABLED &&
                  pass_observation.depth_write;
    if (managed_metadata == nullptr || managed_metadata->removed_by_tuneup ||
        managed_diffuse == nullptr || !managed_diffuse->initialized() ||
        managed_metadata->textures[static_cast<std::size_t>(
            Render::ManagedMaterialTextureSlot::DAMAGED_DIFFUSE)]
            .configured ||
        managed_damaged != nullptr ||
        (!managed_metadata->type_overridden_by_tuneup &&
         declared_transparent != resolved_transparent) ||
        !transparent_state_matches ||
        (managed_specular != nullptr && !allow_alexis_approximation) ||
        managed_metadata->double_sided !=
            (input.cull ==
             Render::Ogre14GraphicsSceneMaterialCull::NONE)) {
      exclusion = OgreNextDemoTextureProjectionExclusion::
          MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED;
      return false;
    }
    if (managed_specular != nullptr) {
      if (texture_resolver_ == nullptr ||
          ordinary_texture_source_resolver_ == nullptr ||
          !ObserveManagedSpecularNativeFacts(
              native_material, *managed_specular, *texture_resolver_,
              *ordinary_texture_source_resolver_, managed_specular_native)) {
        exclusion = OgreNextDemoTextureProjectionExclusion::
            MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED;
        return false;
      }
      OgreNextDemoTextureProjectionExclusion specular_exclusion =
          OgreNextDemoTextureProjectionExclusion::NONE;
      const Render::ValidationResult preflight = PreflightTextureIdentity(
          managed_specular_native.texture, specular_exclusion);
      Render::SamplerResourceDescriptor specular_sampler_preflight;
      if (!preflight ||
          specular_exclusion != OgreNextDemoTextureProjectionExclusion::NONE ||
          !BuildOgreNextDemoSamplerDescriptor(
              managed_specular_native.sampler_observation, 1U,
              "managed-specular-preflight", specular_sampler_preflight)) {
        exclusion = OgreNextDemoTextureProjectionExclusion::
            MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED;
        return false;
      }
    }
  }
  const auto curated_authority_is_current = [&]() noexcept {
    if (!allow_curated_cityworld) {
      return true;
    }
    CuratedCityWorldNativeFacts fresh;
    return curated_native.specular_binding.Revalidate(
               *texture_resolver_, *ordinary_texture_source_resolver_) &&
           ObserveCuratedCityWorldNativeFacts(
               native_material, *curated_policy, input.cull,
               *material_script_resolver_, *texture_resolver_,
               *ordinary_texture_source_resolver_, fresh, nullptr,
               &curated_native.specular_receipt) &&
           MatchCuratedCityWorldNativeFacts(curated_native, fresh);
  };
  std::string texture_key;
  AppendField(texture_key, native_texture->getGroup());
  AppendField(texture_key, native_texture->getName());
  if (alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    AppendField(texture_key, kOgreNextDemoStraightAlphaNormalizationPolicy);
    AppendNumber(texture_key,
                 kOgreNextDemoStraightAlphaNormalizationPolicyVersion);
  }
  pending_->eligible_texture_keys.insert(texture_key);
  const auto active_native =
      pending_->active_native_texture_observations.emplace(
          texture_key, exact_texture_observation);
  if (!active_native.second &&
      !MatchOgreNextDemoExactTextureObservation(active_native.first->second,
                                                exact_texture_observation)) {
    failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                      "ogre_next_demo.material.texture.active_observation",
                      "one capture observed conflicting native state for one "
                      "texture identity");
    return false;
  }
  std::string sampler_key = texture_key;
  AppendExactSamplerObservation(sampler_key, sampler_observation);
  std::string projection_key;
  AppendField(projection_key, kLossyMaterialNormalizationPolicy);
  AppendField(projection_key, native_material->getGroup());
  AppendField(projection_key, native_material->getName());
  AppendField(projection_key, texture_key);
  AppendField(projection_key, sampler_key);
  AppendExactTextureObservation(projection_key, exact_texture_observation);
  const bool preserves_opaque_v2_identity =
      alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE &&
      managed_specular == nullptr;
  if (preserves_opaque_v2_identity) {
    AppendLegacyOpaqueV2PassIdentity(projection_key, pass_observation);
  } else {
    AppendExactPassObservation(projection_key, pass_observation);
  }
  AppendNumber(projection_key, static_cast<std::uint64_t>(input.cull));
  // A managed declaration with no authored specular output does not change
  // the portable material. Retain the exact opaque-v2 ID/name and keep its
  // declaration receipt as revalidated authority only. The versioned managed
  // lowering domain enters identity only when it changes the output workflow.
  if (managed_metadata != nullptr && managed_specular != nullptr) {
    AppendField(projection_key, kManagedSpecularPbrLoweringPolicy);
    AppendDigest(projection_key,
                 managed_metadata->canonical_identity_sha256);
    if (managed_specular != nullptr) {
      AppendExactPassObservation(
          projection_key, managed_specular_native.pass_observation);
      AppendExactSamplerObservation(
          projection_key, managed_specular_native.sampler_observation);
      AppendExactTextureObservation(
          projection_key, managed_specular_native.texture_observation);
    }
  }
  // The reviewed body-paint roughness replaces the shininess derivation, so
  // it enters identity the same way the curated CityWorld roughness does:
  // changing the constant must invalidate every projection it produced.
  float alexis_roughness_identity = 0.0F;
  if (OgreNextDemoResolveAlexisAuthoredRoughness(
          native_material->getGroup(), native_material->getName(),
          alexis_roughness_identity)) {
    AppendField(projection_key, kAlexisAuthoredRoughnessPolicy);
    AppendFloatBits(projection_key, alexis_roughness_identity);
  }
  if (allow_curated_cityworld) {
    AppendField(projection_key, kCuratedCityWorldPbrLoweringPolicy);
    AppendNumber(projection_key,
                 kOgreNextDemoCuratedCityWorldAsiaPolicyVersion);
    AppendField(projection_key, curated_policy->review_identity_sha256);
    AppendField(projection_key,
                kOgreNextDemoCuratedCityWorldEnvironmentPolicy);
    AppendField(projection_key,
                kOgreNextDemoCuratedCityWorldSamplerProfile);
    AppendField(
        projection_key,
        kOgreNextDemoCuratedCityWorldAcceptanceConfigSha256);
    AppendNumber(projection_key, curated_native.material_state_count);
    AppendFloatBits(projection_key, curated_policy->roughness_factor);
    for (const float factor : curated_policy->specular_factor) {
      AppendFloatBits(projection_key, factor);
    }
    AppendFloatBits(projection_key, curated_policy->index_of_refraction);
    for (std::size_t index = 0U;
         index < curated_native.texture_resolutions.size(); ++index) {
      const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
          curated_native.texture_resolutions[index].source_receipt();
      const Render::Ogre14AuthenticatedTextureReceiptMetadata *const
          metadata = receipt != nullptr ? receipt->metadata() : nullptr;
      if (metadata == nullptr) {
        failure = Failure(
            Render::ValidationCode::MISSING_REFERENCE,
            "ogre_next_demo.material.curated_cityworld.texture_receipt",
            "reviewed CityWorld texture authority disappeared before identity");
        return false;
      }
      AppendField(projection_key, metadata->source.exact_member_name);
      AppendField(projection_key, metadata->bytes_sha256);
      AppendExactSamplerObservation(
          projection_key, curated_native.sampler_observations[index]);
      AppendExactTextureObservation(
          projection_key, curated_native.texture_observations[index]);
    }
  }

  const auto make_pending_native_owner = [&]() {
    PendingNativeTextureOwner owner;
    owner.native_material = native_material;
    owner.native_pass_pointer_token = reinterpret_cast<std::uintptr_t>(pass);
    owner.native_unit_pointer_token = reinterpret_cast<std::uintptr_t>(unit);
    owner.native_sampler_pointer_token =
        reinterpret_cast<std::uintptr_t>(native_sampler.get());
    owner.sampler_observation = sampler_observation;
    owner.texture_observation = exact_texture_observation;
    owner.pass_observation = pass_observation;
    owner.allow_alexis_approximation = allow_alexis_approximation;
    owner.exact_continuous_dust = exact_continuous_dust;
    owner.curated_cityworld = allow_curated_cityworld;
    owner.technique_pass_count = technique->getNumPasses();
    owner.pass_texture_unit_count = pass->getNumTextureUnitStates();
    owner.unpresented_layer_units = unpresented_layer_units;
    owner.unpresented_additive_overlay_passes =
        unpresented_additive_overlay_passes;
    owner.diffuse = ObserveColourComponents(pass->getDiffuse());
    owner.ambient = ObserveColourComponents(pass->getAmbient());
    owner.specular = ObserveColourComponents(pass->getSpecular());
    owner.emissive = ObserveColourComponents(pass->getSelfIllumination());
    owner.shininess = static_cast<float>(pass->getShininess());
    owner.vertex_colour_tracking_token =
        static_cast<std::uint8_t>(pass->getVertexColourTracking());
    return owner;
  };

  const auto append_pending_native_owner =
      [&](std::vector<PendingNativeTextureOwner> &owners) {
        const std::uintptr_t unit_token =
            reinterpret_cast<std::uintptr_t>(unit);
        const auto existing = std::find_if(
            owners.begin(), owners.end(), [unit_token](const auto &owner) {
              return owner.native_unit_pointer_token == unit_token;
            });
        if (existing == owners.end()) {
          owners.push_back(make_pending_native_owner());
        } else {
          *existing = make_pending_native_owner();
        }
      };

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
                                          captured, exact_texture_observation,
                                          &fresh);
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
            captured.source, true, authenticated_source_required,
            authenticated_source_required, fresh_result,
            immutable_receipt_matches);
    if (!authority) {
      return authority;
    }
    PendingAuthenticatedTextureObservation observation;
    observation.native_texture = native_texture;
    observation.resolution = std::move(fresh);
    observation.native_owners.push_back(make_pending_native_owner());
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
      existing->second.resolution = std::move(observation.resolution);
      append_pending_native_owner(existing->second.native_owners);
    }
    return Render::ValidationResult::Success();
  };

  const auto record_ordinary_observation =
      [&](const CapturedTexture &captured) -> Render::ValidationResult {
    if (texture_resolver_ == nullptr ||
        ordinary_texture_source_resolver_ == nullptr) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.ordinary.resolver",
                     "ordinary cache has no bound selected-source resolver");
    }
    const bool ordinary_source_required =
        !texture_resolver_->RequiresAuthenticatedTextureSource(*native_texture);
    Render::Ogre14SelectedTextureSourceResolution fresh;
    const bool immutable_receipt_matches =
        ordinary_source_required &&
        ResolveFrozenOrdinaryTexture(
            *native_texture, *ordinary_texture_source_resolver_, captured,
            exact_texture_observation, &fresh);
    const Render::ValidationResult fresh_result =
        immutable_receipt_matches
            ? Render::ValidationResult::Success()
            : Failure(Render::ValidationCode::REVISION_MISMATCH,
                      "ogre_next_demo.material.ordinary.cache_revalidation",
                      "cached ordinary selected-source provenance or authority "
                      "changed");
    Render::ValidationResult authority =
        ValidateOgreNextDemoCachedTextureSourceAuthority(
            captured.source, true, ordinary_source_required,
            ordinary_source_required, fresh_result, immutable_receipt_matches);
    if (!authority) {
      return authority;
    }
    PendingOrdinaryTextureObservation observation;
    observation.native_texture = native_texture;
    observation.resolution = std::move(fresh);
    observation.native_owners.push_back(make_pending_native_owner());
    auto existing = pending_->ordinary_texture_observations.find(texture_key);
    if (existing == pending_->ordinary_texture_observations.end()) {
      pending_->ordinary_texture_observations.emplace(texture_key,
                                                      std::move(observation));
    } else {
      if (existing->second.native_texture.get() != native_texture.get()) {
        return Failure(Render::ValidationCode::REVISION_MISMATCH,
                       "ogre_next_demo.material.ordinary.pending_texture",
                       "one capture observed two native textures for the "
                       "frozen ordinary source identity");
      }
      existing->second.resolution = std::move(observation.resolution);
      append_pending_native_owner(existing->second.native_owners);
    }
    return Render::ValidationResult::Success();
  };

  const auto revalidate_cached_texture =
      [&](const CapturedTexture &captured) -> Render::ValidationResult {
    if (captured.native_state_count != native_texture->getStateCount() ||
        !MatchOgreNextDemoExactTextureObservation(
            captured.exact_texture_observation, exact_texture_observation)) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.material.texture.cache",
                     "cached TUS0 loaded-texture identity changed");
    }
    Render::ValidationResult authority =
        IsOgreNextDemoAuthenticatedTextureSourceMode(captured.source)
            ? record_authenticated_observation(captured)
        : captured.source ==
                OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES
            ? record_ordinary_observation(captured)
            : Failure(Render::ValidationCode::INVALID_ENUM,
                      "ogre_next_demo.material.texture.cache_mode",
                      "cached texture has an invalid source mode");
    if (!authority) {
      return authority;
    }
    if (pending_->source_cache_hits.emplace(texture_key).second) {
      authority = RecordOgreNextDemoTextureSourceCacheHit(pending_->counters);
    }
    return authority;
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
      Render::Ogre14SelectedTextureSourceResolution ordinary_resolution;
      const bool authenticated_source_required =
          texture_resolver_->RequiresAuthenticatedTextureSource(
              *native_texture);
      const bool authenticated_resolution_attempted =
          authenticated_source_required;
      Render::ValidationResult authenticated_resolution_validation =
          Render::ValidationResult::Success();
      OgreNextDemoTextureSourceMode authenticated_resolution_mode =
          OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
      if (authenticated_resolution_attempted) {
        authenticated_resolution_validation =
            texture_resolver_->ResolveAuthenticatedTexture(
                *native_texture, authenticated_resolution);
        const Render::Ogre14AuthenticatedTextureReceipt *const receipt =
            authenticated_resolution_validation
                ? authenticated_resolution.source_receipt()
                : nullptr;
        const Render::Ogre14AuthenticatedTextureReceiptMetadata *const
            metadata = receipt != nullptr ? receipt->metadata() : nullptr;
        if (metadata != nullptr) {
          (void)MapAuthenticatedSourceMode(metadata->source.source_kind,
                                           authenticated_resolution_mode);
        }
      }
      const bool ordinary_resolution_attempted =
          !authenticated_source_required &&
          ordinary_texture_source_resolver_ != nullptr;
      Render::ValidationResult ordinary_resolution_validation =
          Render::ValidationResult::Success();
      if (ordinary_resolution_attempted) {
        ordinary_resolution_validation =
            ordinary_texture_source_resolver_->ResolveSelectedTextureSource(
                *native_texture, ordinary_resolution);
        const Render::Ogre14SelectedTextureSourceReceipt *const receipt =
            ordinary_resolution_validation
                ? ordinary_resolution.source_receipt()
                : nullptr;
        if (ordinary_resolution_validation &&
            (!ordinary_resolution.initialized() || receipt == nullptr ||
             !receipt->initialized() || receipt->metadata() == nullptr ||
             receipt->source_bytes() == nullptr ||
             receipt->source_size() == 0U)) {
          ordinary_resolution_validation = Failure(
              Render::ValidationCode::MISSING_REFERENCE,
              "ogre_next_demo.material.ordinary.selected_source",
              "ordinary resolver returned no usable selected-source receipt");
        }
      }
      OgreNextDemoTextureSourceSelection source_selection;
      Render::ValidationResult source_selection_validation =
          SelectOgreNextDemoTextureSourceMode(
              authenticated_source_required, authenticated_resolution_attempted,
              authenticated_resolution_validation,
              authenticated_resolution_mode, ordinary_resolution_attempted,
              ordinary_resolution_validation, source_selection);
      if (!source_selection_validation) {
        source_selection_validation.field =
            "ogre_next_demo.material.source_selection." +
            source_selection_validation.field;
        failure = std::move(source_selection_validation);
        return false;
      }
      if (!source_selection.selected) {
        exclusion = source_selection.exclusion;
        return false;
      }

      CapturedTexture captured;
      captured.source = source_selection.mode;
      captured.alpha_policy = alpha_policy;
      Render::ValidationResult validation = DeriveOgreNextDemoSourceId(
          kTextureIdDomain, texture_key, captured.source_id);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      Render::TextureResourceDescriptor descriptor;
      if (IsOgreNextDemoAuthenticatedTextureSourceMode(captured.source)) {
        validation = CaptureAuthenticatedTextureSource(
            native_texture, *unit, exact_texture_observation,
            *texture_resolver_, authenticated_resolution,
            HexId(captured.source_id), descriptor,
            captured.authenticated_receipt, captured.authenticated_provenance,
            captured.authenticated_content_decode_key,
            alpha_policy,
            captured.normalization_observation);
        if (!validation) {
          // Successful resolution selected the authenticated path. Decode or
          // authority failure is terminal; native readback is forbidden.
          failure = std::move(validation);
          return false;
        }
        if (unit->_getTexturePtr().get() != native_texture.get()) {
          failure =
              Failure(Render::ValidationCode::REVISION_MISMATCH,
                      "ogre_next_demo.material.authenticated.texture_unit",
                      "TUS0 changed texture owner after authenticated decode");
          return false;
        }
      } else {
        bool ordinary_captured = false;
        validation = TryCaptureOrdinaryTextureSource(
            native_texture, *unit, exact_texture_observation,
            *ordinary_texture_source_resolver_, ordinary_resolution,
            HexId(captured.source_id), descriptor, captured.ordinary_receipt,
            captured.ordinary_provenance, captured.ordinary_content_decode_key,
            ordinary_captured, exclusion, alpha_policy,
            captured.normalization_observation);
        if (!validation) {
          failure = std::move(validation);
          return false;
        }
        if (!ordinary_captured) {
          return false;
        }
        if (unit->_getTexturePtr().get() != native_texture.get()) {
          failure = Failure(
              Render::ValidationCode::REVISION_MISMATCH,
              "ogre_next_demo.material.ordinary.texture_unit",
              "TUS0 changed texture owner after ordinary source decode");
          return false;
        }
      }
      captured.native_state_count = native_texture->getStateCount();
      captured.exact_texture_observation = exact_texture_observation;
      validation = IsOgreNextDemoAuthenticatedTextureSourceMode(captured.source)
                       ? record_authenticated_observation(captured)
                       : record_ordinary_observation(captured);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      validation = RecordOgreNextDemoTextureSourceDecode(captured.source,
                                                         pending_->counters);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      pending_->counters.modern_source_normalizations += 1U;
      if (alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
        pending_->counters.straight_alpha_source_normalizations += 1U;
      } else {
        pending_->counters.opaque_source_normalizations += 1U;
      }
      pending_->counters.authored_mip_prefix_levels +=
          captured.normalization_observation.authored_mip_prefix_levels;
      pending_->counters.generated_mip_tail_levels +=
          captured.normalization_observation.generated_mip_tail_levels;
      pending_->counters.normalized_output_mip_levels +=
          captured.normalization_observation.authored_mip_prefix_levels +
          captured.normalization_observation.generated_mip_tail_levels;
      pending_->counters.legacy_native_additional_mip_levels +=
          captured.exact_texture_observation.additional_mip_count;
      if (captured.exact_texture_observation.texture_unit_gamma != 1.0F) {
        pending_->counters.legacy_texture_unit_gamma_nonunit_observations += 1U;
      }
      if (captured.exact_texture_observation.texture_gamma != 1.0F) {
        pending_->counters.legacy_texture_gamma_nonunit_observations += 1U;
      }
      if (!captured.exact_texture_observation.texture_unit_hardware_gamma) {
        pending_->counters
            .legacy_texture_unit_hardware_gamma_off_observations += 1U;
      }
      if (!captured.exact_texture_observation.texture_hardware_gamma) {
        pending_->counters.legacy_hardware_gamma_off_observations += 1U;
      }
      if ((captured.exact_texture_observation.usage_token &
           static_cast<std::uint32_t>(Ogre::TU_AUTOMIPMAP)) != 0U) {
        pending_->counters.legacy_automipmap_observations += 1U;
      }
      std::string identity(kTextureIdDomain);
      identity.push_back('\0');
      identity.append(texture_key);
      validation = pending_->cache->identities.Register(std::move(identity),
                                                        captured.source_id);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      captured.payload = std::make_shared<const Render::RenderAssetPayload>(
          std::move(descriptor));
      texture =
          pending_->cache->textures.emplace(texture_key, std::move(captured))
              .first;
    } else {
      Render::ValidationResult cache_validation =
          revalidate_cached_texture(texture->second);
      if (!cache_validation) {
        failure = std::move(cache_validation);
        return false;
      }
    }
    if (managed_binding != nullptr &&
        (texture_resolver_ == nullptr ||
         ordinary_texture_source_resolver_ == nullptr ||
         !managed_binding->MatchesExactMaterial(native_material) ||
         !managed_binding->Revalidate(
             *texture_resolver_, *ordinary_texture_source_resolver_))) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.managed.authority",
          "managed declaration binding is not current for a selected source");
      return false;
    }
    if (managed_diffuse != nullptr &&
        (!ManagedReceiptMatchesNativeTexture(*managed_diffuse,
                                             *native_texture) ||
         !ManagedReceiptMatchesCapturedTexture(*managed_diffuse,
                                               texture->second))) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.managed.diffuse_authority",
          "managed diffuse declaration does not own the exact decoded TUS0 bytes");
      return false;
    }
    if (allow_curated_cityworld) {
      const Render::Ogre14AuthenticatedTextureReceipt *const reviewed_base =
          curated_native.texture_resolutions[0U].source_receipt();
      if (reviewed_base == nullptr ||
          !IsOgreNextDemoAuthenticatedTextureSourceMode(
              texture->second.source) ||
          !texture->second.authenticated_receipt.SharesImmutableStateWith(
              *reviewed_base) ||
          !curated_authority_is_current()) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.curated_cityworld.base_authority",
            "reviewed CityWorld TUS0 or declaration authority changed during decode");
        return false;
      }
    }
    const auto active_normalization =
        pending_->active_normalization_observations.emplace(
            texture_key, texture->second.normalization_observation);
    if (!active_normalization.second &&
        (active_normalization.first->second.policy !=
             texture->second.normalization_observation.policy ||
         active_normalization.first->second.policy_version !=
             texture->second.normalization_observation.policy_version ||
         active_normalization.first->second.authored_mip_prefix_levels !=
             texture->second.normalization_observation
                 .authored_mip_prefix_levels ||
         active_normalization.first->second.generated_mip_tail_levels !=
             texture->second.normalization_observation
                 .generated_mip_tail_levels)) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.texture.active_normalization",
          "one capture observed conflicting normalization for one texture "
          "identity");
      return false;
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
      validation = pending_->cache->identities.Register(std::move(identity),
                                                        captured.source_id);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      const auto &texture_descriptor =
          std::get<Render::TextureResourceDescriptor>(*texture->second.payload);
      Render::SamplerResourceDescriptor descriptor;
      validation = BuildOgreNextDemoSamplerDescriptor(
          sampler_observation, texture_descriptor.mip_levels.size(),
          HexId(captured.source_id), descriptor);
      if (!validation) {
        failure = std::move(validation);
        return false;
      }
      captured.payload = std::make_shared<const Render::RenderAssetPayload>(
          std::move(descriptor));
      sampler =
          pending_->cache->samplers.emplace(sampler_key, std::move(captured))
              .first;
    }

    std::string managed_specular_texture_key;
    std::string managed_specular_sampler_key;
    if (managed_specular != nullptr) {
      const char *const specular_texture_id_domain =
          allow_curated_cityworld
              ? kCuratedCityWorldSpecularTextureIdDomain
              : kManagedSpecularTextureIdDomain;
      const Render::ManagedMaterialTextureSourceIdentity *const identity =
          managed_specular->identity();
      if (identity == nullptr || !managed_specular->initialized() ||
          managed_specular->source_bytes() == nullptr ||
          managed_specular->source_size() == 0U ||
          (allow_curated_cityworld
               ? !curated_authority_is_current()
               : managed_binding == nullptr ||
                     !managed_binding->Revalidate(
                         *texture_resolver_,
                         *ordinary_texture_source_resolver_))) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.specular_authority",
            "managed specular source authority changed before decode");
        return false;
      }
      AppendField(managed_specular_texture_key,
                  specular_texture_id_domain);
      if (allow_curated_cityworld) {
        AppendField(managed_specular_texture_key,
                    curated_policy->review_identity_sha256);
      }
      AppendDigest(managed_specular_texture_key,
                   managed_specular->canonical_identity_sha256());
      auto specular_texture = pending_->cache->managed_specular_textures.find(
          managed_specular_texture_key);
      if (specular_texture ==
          pending_->cache->managed_specular_textures.end()) {
        CapturedManagedSpecularTexture captured_specular;
        Render::ValidationResult managed_validation =
            DeriveOgreNextDemoSourceId(
                specular_texture_id_domain,
                managed_specular_texture_key, captured_specular.source_id);
        if (!managed_validation) {
          failure = std::move(managed_validation);
          return false;
        }
        Render::TextureResourceDescriptor descriptor;
        managed_validation = CaptureManagedSpecularTextureSource(
            *managed_specular, HexId(captured_specular.source_id), descriptor,
            captured_specular.normalization_observation);
        if (!managed_validation ||
            (allow_curated_cityworld
                 ? !curated_authority_is_current()
                 : !managed_binding->Revalidate(
                       *texture_resolver_,
                       *ordinary_texture_source_resolver_))) {
          failure = managed_validation
                        ? Failure(Render::ValidationCode::REVISION_MISMATCH,
                                  "ogre_next_demo.material.managed.specular_"
                                  "final_authority",
                                  "managed specular source authority changed "
                                  "during decode")
                        : std::move(managed_validation);
          return false;
        }
        std::string specular_identity(specular_texture_id_domain);
        specular_identity.push_back('\0');
        specular_identity.append(managed_specular_texture_key);
        managed_validation = pending_->cache->identities.Register(
            std::move(specular_identity), captured_specular.source_id);
        if (!managed_validation) {
          failure = std::move(managed_validation);
          return false;
        }
        captured_specular.receipt = *managed_specular;
        captured_specular.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(descriptor));
        const OgreNextDemoTextureNormalizationObservation
            specular_normalization =
                captured_specular.normalization_observation;
        specular_texture =
            pending_->cache->managed_specular_textures
                .emplace(managed_specular_texture_key,
                         std::move(captured_specular))
                .first;
        pending_->counters.authored_specular_source_decodes += 1U;
        pending_->counters.linear_specular_source_normalizations += 1U;
        pending_->counters.authored_specular_mip_prefix_levels +=
            specular_normalization.authored_mip_prefix_levels;
        pending_->counters.generated_specular_mip_tail_levels +=
            specular_normalization.generated_mip_tail_levels;
        pending_->counters.normalized_specular_output_mip_levels +=
            specular_normalization.authored_mip_prefix_levels +
            specular_normalization.generated_mip_tail_levels;
        pending_->counters.modern_source_normalizations += 1U;
        pending_->counters.authored_mip_prefix_levels +=
            specular_normalization.authored_mip_prefix_levels;
        pending_->counters.generated_mip_tail_levels +=
            specular_normalization.generated_mip_tail_levels;
        pending_->counters.normalized_output_mip_levels +=
            specular_normalization.authored_mip_prefix_levels +
            specular_normalization.generated_mip_tail_levels;
      } else if (!specular_texture->second.receipt.SharesImmutableStateWith(
                     *managed_specular)) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.specular_cache",
            "managed specular source receipt changed within the map generation");
        return false;
      }
      AppendField(managed_specular_sampler_key,
                  managed_specular_native.texture->getGroup());
      AppendField(managed_specular_sampler_key,
                  managed_specular_native.texture->getName());
      AppendExactSamplerObservation(
          managed_specular_sampler_key,
          managed_specular_native.sampler_observation);
      auto specular_sampler =
          pending_->cache->samplers.find(managed_specular_sampler_key);
      if (specular_sampler == pending_->cache->samplers.end()) {
        CapturedSampler captured_specular_sampler;
        Render::ValidationResult sampler_validation =
            DeriveOgreNextDemoSourceId(
                kSamplerIdDomain, managed_specular_sampler_key,
                captured_specular_sampler.source_id);
        if (!sampler_validation) {
          failure = std::move(sampler_validation);
          return false;
        }
        std::string sampler_identity(kSamplerIdDomain);
        sampler_identity.push_back('\0');
        sampler_identity.append(managed_specular_sampler_key);
        sampler_validation = pending_->cache->identities.Register(
            std::move(sampler_identity),
            captured_specular_sampler.source_id);
        if (!sampler_validation) {
          failure = std::move(sampler_validation);
          return false;
        }
        const auto &specular_descriptor =
            std::get<Render::TextureResourceDescriptor>(
                *specular_texture->second.payload);
        Render::SamplerResourceDescriptor descriptor;
        sampler_validation = BuildOgreNextDemoSamplerDescriptor(
            managed_specular_native.sampler_observation,
            specular_descriptor.mip_levels.size(),
            HexId(captured_specular_sampler.source_id), descriptor);
        if (!sampler_validation) {
          failure = std::move(sampler_validation);
          return false;
        }
        captured_specular_sampler.payload =
            std::make_shared<const Render::RenderAssetPayload>(
                std::move(descriptor));
        specular_sampler =
            pending_->cache->samplers
                .emplace(managed_specular_sampler_key,
                         std::move(captured_specular_sampler))
                .first;
      }
      const Render::ValidationResult compatibility =
          Render::ValidateMaterialTextureCompatibility(
              Render::MaterialTextureSlot::SPECULAR,
              std::get<Render::TextureResourceDescriptor>(
                  *specular_texture->second.payload),
              std::get<Render::SamplerResourceDescriptor>(
                  *specular_sampler->second.payload));
      if (!compatibility) {
        failure = compatibility;
        return false;
      }
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
    validation =
        pending_->cache->identities.Register(std::move(token_identity), token);
    if (!validation) {
      failure = std::move(validation);
      return false;
    }
    Projection captured;
    captured.exact_name = allow_curated_cityworld
                              ? "CuratedCityWorldAsia/" + HexId(token) +
                                    "/v1"
                          : preserves_opaque_v2_identity
                              ? "OpaqueTUS0/" + HexId(token) + "/v1"
                              : "AutomaticTUS0/" + HexId(token) + "/v3";
    captured.texture_key = texture_key;
    captured.sampler_key = sampler_key;
    captured.managed_specular_texture_key = managed_specular_texture_key;
    captured.managed_specular_sampler_key = managed_specular_sampler_key;
    if (managed_binding != nullptr && managed_metadata != nullptr) {
      captured.managed_binding = *managed_binding;
      captured.managed_declaration_digest =
          managed_metadata->canonical_identity_sha256;
    }
    if (managed_specular != nullptr) {
      captured.managed_specular_pass_pointer_token =
          reinterpret_cast<std::uintptr_t>(managed_specular_native.pass);
      captured.managed_specular_unit_pointer_token =
          reinterpret_cast<std::uintptr_t>(managed_specular_native.unit);
      captured.managed_specular_sampler_pointer_token =
          reinterpret_cast<std::uintptr_t>(
              managed_specular_native.sampler.get());
      captured.managed_specular_texture_pointer_token =
          reinterpret_cast<std::uintptr_t>(
              managed_specular_native.texture.get());
      captured.managed_specular_pass_observation =
          managed_specular_native.pass_observation;
      captured.managed_specular_sampler_observation =
          managed_specular_native.sampler_observation;
      captured.managed_specular_texture_observation =
          managed_specular_native.texture_observation;
      if (!allow_curated_cityworld) {
        captured.managed_native_material_owner = native_material;
      }
    }
    if (allow_curated_cityworld) {
      captured.curated_cityworld = true;
      captured.curated_review_identity_sha256 =
          std::string(curated_policy->review_identity_sha256);
      captured.curated_native_material_owner = native_material;
      captured.curated_material_state_count =
          curated_native.material_state_count;
      captured.curated_pass_pointer_token =
          reinterpret_cast<std::uintptr_t>(curated_native.pass);
      for (std::size_t index = 0U; index < curated_native.units.size();
           ++index) {
        captured.curated_unit_pointer_tokens[index] =
            reinterpret_cast<std::uintptr_t>(curated_native.units[index]);
        captured.curated_sampler_pointer_tokens[index] =
            reinterpret_cast<std::uintptr_t>(
                curated_native.samplers[index].get());
        captured.curated_texture_pointer_tokens[index] =
            reinterpret_cast<std::uintptr_t>(
                curated_native.textures[index].get());
      }
      captured.curated_sampler_observations =
          curated_native.sampler_observations;
      captured.curated_texture_observations =
          curated_native.texture_observations;
      captured.curated_script_resolution =
          curated_native.script_resolution;
      captured.curated_texture_resolutions =
          curated_native.texture_resolutions;
      captured.curated_specular_receipt =
          curated_native.specular_receipt;
      captured.curated_specular_binding =
          curated_native.specular_binding;
    }
    captured.native_material_pointer_token =
        reinterpret_cast<std::uintptr_t>(native_material.get());
    captured.native_pass_pointer_token = reinterpret_cast<std::uintptr_t>(pass);
    captured.native_unit_pointer_token = reinterpret_cast<std::uintptr_t>(unit);
    captured.native_sampler_pointer_token =
        reinterpret_cast<std::uintptr_t>(native_sampler.get());
    captured.sampler_observation = sampler_observation;
    captured.pass_observation = pass_observation;
    validation = Render::DeriveOgre14GraphicsSceneMaterialAssetId(
        kMaterialGroup, captured.exact_name, captured.material_source_id);
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
    material.blend_mode = blend_mode;
    material.alpha_test_mode = alpha_test_mode;
    material.base_color_transfer =
        Render::BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
    material.double_sided =
        input.cull == Render::Ogre14GraphicsSceneMaterialCull::NONE;
    material.depth_write = pass_observation.depth_write;
    material.alpha_cutoff =
        alpha_test_mode == Render::MaterialAlphaTestMode::DISABLED
            ? 0.5F
            : static_cast<float>(pass_observation.alpha_reject_value) /
                  255.0F;
    const Ogre::ColourValue native_diffuse = pass->getDiffuse();
    captured.base_color_factor = {static_cast<float>(native_diffuse.r),
                                  static_cast<float>(native_diffuse.g),
                                  static_cast<float>(native_diffuse.b),
                                  static_cast<float>(native_diffuse.a)};
    const Ogre::ColourValue native_ambient = pass->getAmbient();
    captured.discarded_ambient = {static_cast<float>(native_ambient.r),
                                  static_cast<float>(native_ambient.g),
                                  static_cast<float>(native_ambient.b),
                                  static_cast<float>(native_ambient.a)};
    const Ogre::ColourValue native_specular = pass->getSpecular();
    captured.discarded_specular = {static_cast<float>(native_specular.r),
                                   static_cast<float>(native_specular.g),
                                   static_cast<float>(native_specular.b),
                                   static_cast<float>(native_specular.a)};
    material.base_color_factor = {
        captured.base_color_factor[0U], captured.base_color_factor[1U],
        captured.base_color_factor[2U], captured.base_color_factor[3U]};
    material.metallic_factor = 0.0F;
    if (!managed_specular_texture_key.empty()) {
      material.pbr_workflow = Render::MaterialPbrWorkflow::SPECULAR;
      material.specular_factor =
          allow_curated_cityworld
              ? Render::Float3{curated_policy->specular_factor[0U],
                               curated_policy->specular_factor[1U],
                               curated_policy->specular_factor[2U]}
              : Render::Float3{1.0F, 1.0F, 1.0F};
      if (allow_curated_cityworld) {
        material.index_of_refraction =
            curated_policy->index_of_refraction;
      }
      material.specular_texture.texture_coordinate_set = 0U;
    }
    captured.roughness_factor = ResolveOgreNextDemoRoughnessFactor(
        *native_material, *pass,
        allow_curated_cityworld ? curated_policy : nullptr);
    material.roughness_factor = captured.roughness_factor;
    const Ogre::ColourValue native_emissive = pass->getSelfIllumination();
    captured.emissive_factor = {static_cast<float>(native_emissive.r),
                                static_cast<float>(native_emissive.g),
                                static_cast<float>(native_emissive.b)};
    captured.discarded_emissive_alpha = static_cast<float>(native_emissive.a);
    captured.vertex_colour_tracking_token =
        static_cast<std::uint8_t>(pass->getVertexColourTracking());
    material.emissive_factor = {captured.emissive_factor[0U],
                                captured.emissive_factor[1U],
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
        std::make_shared<const Render::RenderAssetPayload>(std::move(material));
    if (alpha_test_mode != Render::MaterialAlphaTestMode::DISABLED) {
      pending_->counters.alpha_test_material_projections += 1U;
    }
    if (blend_mode == Render::MaterialBlendMode::LEGACY_STRAIGHT_ALPHA) {
      pending_->counters.legacy_straight_alpha_material_projections += 1U;
    } else if (blend_mode == Render::MaterialBlendMode::STRAIGHT_SOURCE_OVER) {
      pending_->counters.straight_source_over_material_projections += 1U;
    }
    if (!managed_specular_texture_key.empty()) {
      pending_->counters.specular_workflow_projections += 1U;
    }
    if (unpresented_layer_units != 0U) {
      pending_->counters.layered_legacy_material_projections += 1U;
      pending_->counters.unpresented_legacy_layer_units +=
          unpresented_layer_units;
    }
    if (unpresented_additive_overlay_passes != 0U) {
      pending_->counters.additive_overlay_legacy_material_projections += 1U;
      pending_->counters.unpresented_legacy_additive_overlay_passes +=
          unpresented_additive_overlay_passes;
    }
    if (IsCanonicalAnisotropicSampler(sampler_observation) ||
        (managed_specular != nullptr &&
         IsCanonicalAnisotropicSampler(
             managed_specular_native.sampler_observation))) {
      pending_->counters.anisotropic_sampler_projections += 1U;
    }
    projection = pending_->cache->projections
                     .emplace(projection_key, std::move(captured))
                     .first;
  } else {
    if (projection->second.curated_cityworld != allow_curated_cityworld) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.curated_cityworld.cache_kind",
          "cached projection changed curated CityWorld authority class");
      return false;
    }
    if (allow_curated_cityworld) {
      const auto specular_texture =
          pending_->cache->managed_specular_textures.find(
              projection->second.managed_specular_texture_key);
      const auto specular_sampler = pending_->cache->samplers.find(
          projection->second.managed_specular_sampler_key);
      if (projection->second.curated_review_identity_sha256 !=
              curated_policy->review_identity_sha256 ||
          specular_texture ==
              pending_->cache->managed_specular_textures.end() ||
          specular_sampler == pending_->cache->samplers.end() ||
          managed_specular == nullptr ||
          !specular_texture->second.receipt.SharesImmutableStateWith(
              projection->second.curated_specular_receipt) ||
          !RevalidateCuratedCityWorldProjection(
              projection->second, *material_script_resolver_,
              *texture_resolver_, *ordinary_texture_source_resolver_)) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.curated_cityworld.cache_authority",
            "cached reviewed CityWorld declaration, TUS, sampler, or source authority changed");
        return false;
      }
    }
    if (!projection->second.managed_binding.initialized() &&
        managed_binding != nullptr && managed_metadata != nullptr) {
      // Diffuse-only managed authority is output-equivalent to the existing
      // opaque-v2 projection, so it deliberately retains that asset ID. The
      // pending COW cache must nevertheless adopt the exact declaration owner
      // before publication; otherwise an unbound->managed transition could
      // bypass final authority revalidation.
      if (managed_specular != nullptr ||
          !managed_binding->Revalidate(
              *texture_resolver_, *ordinary_texture_source_resolver_)) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.cache_promotion",
            "output-equivalent managed authority could not be adopted by the "
            "opaque-v2 cache");
        return false;
      }
      EnsurePendingCacheWritable();
      projection = pending_->cache->projections.find(projection_key);
      if (projection == pending_->cache->projections.end() ||
          projection->second.managed_binding.initialized()) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.cache_promotion_transaction",
            "the output-equivalent managed projection changed while making "
            "its transactional cache writable");
        return false;
      }
      projection->second.managed_binding = *managed_binding;
      projection->second.managed_declaration_digest =
          managed_metadata->canonical_identity_sha256;
    }
    if (projection->second.managed_binding.initialized()) {
      const auto specular_texture =
          pending_->cache->managed_specular_textures.find(
              projection->second.managed_specular_texture_key);
      const auto specular_sampler = pending_->cache->samplers.find(
          projection->second.managed_specular_sampler_key);
      if (managed_binding == nullptr || managed_metadata == nullptr ||
          !projection->second.managed_binding.SharesImmutableStateWith(
              *managed_binding) ||
          projection->second.managed_declaration_digest !=
              managed_metadata->canonical_identity_sha256 ||
          !managed_binding->Revalidate(
              *texture_resolver_, *ordinary_texture_source_resolver_) ||
          (!projection->second.managed_specular_texture_key.empty() &&
           (specular_texture ==
                pending_->cache->managed_specular_textures.end() ||
            specular_sampler == pending_->cache->samplers.end() ||
            managed_specular == nullptr ||
            !specular_texture->second.receipt.SharesImmutableStateWith(
                *managed_specular) ||
            !RevalidateManagedSpecularNativeProjection(
                projection->second, *texture_resolver_,
                *ordinary_texture_source_resolver_)))) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.cache_authority",
            "cached managed declaration or source authority changed");
        return false;
      }
    }
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
    const auto active_normalization =
        pending_->active_normalization_observations.emplace(
            projection->second.texture_key,
            texture->second.normalization_observation);
    if (!active_normalization.second &&
        (active_normalization.first->second.policy !=
             texture->second.normalization_observation.policy ||
         active_normalization.first->second.policy_version !=
             texture->second.normalization_observation.policy_version ||
         active_normalization.first->second.authored_mip_prefix_levels !=
             texture->second.normalization_observation
                 .authored_mip_prefix_levels ||
         active_normalization.first->second.generated_mip_tail_levels !=
             texture->second.normalization_observation
                 .generated_mip_tail_levels)) {
      failure =
          Failure(Render::ValidationCode::REVISION_MISMATCH,
                  "ogre_next_demo.material.texture.active_normalization",
                  "cached projection normalization changed within one capture");
      return false;
    }
    const Ogre::ColourValue native_diffuse = pass->getDiffuse();
    const std::array<float, 4U> base_color_factor{
        {static_cast<float>(native_diffuse.r),
         static_cast<float>(native_diffuse.g),
         static_cast<float>(native_diffuse.b),
         static_cast<float>(native_diffuse.a)}};
    const Ogre::ColourValue native_ambient = pass->getAmbient();
    const std::array<float, 4U> discarded_ambient{
        {static_cast<float>(native_ambient.r),
         static_cast<float>(native_ambient.g),
         static_cast<float>(native_ambient.b),
         static_cast<float>(native_ambient.a)}};
    const Ogre::ColourValue native_specular = pass->getSpecular();
    const std::array<float, 4U> discarded_specular{
        {static_cast<float>(native_specular.r),
         static_cast<float>(native_specular.g),
         static_cast<float>(native_specular.b),
         static_cast<float>(native_specular.a)}};
    const float roughness_factor = ResolveOgreNextDemoRoughnessFactor(
        *native_material, *pass,
        allow_curated_cityworld ? curated_policy : nullptr);
    const Ogre::ColourValue native_emissive = pass->getSelfIllumination();
    const std::array<float, 3U> emissive_factor{
        {static_cast<float>(native_emissive.r),
         static_cast<float>(native_emissive.g),
         static_cast<float>(native_emissive.b)}};
    if (projection->second.native_material_pointer_token !=
            reinterpret_cast<std::uintptr_t>(native_material.get()) ||
        projection->second.native_pass_pointer_token !=
            reinterpret_cast<std::uintptr_t>(pass) ||
        projection->second.native_unit_pointer_token !=
            reinterpret_cast<std::uintptr_t>(unit) ||
        projection->second.native_sampler_pointer_token !=
            reinterpret_cast<std::uintptr_t>(native_sampler.get()) ||
        !MatchOgreNextDemoExactSamplerObservation(
            projection->second.sampler_observation, sampler_observation) ||
        !MatchExactPassObservation(projection->second.pass_observation,
                                   pass_observation) ||
        projection->second.base_color_factor != base_color_factor ||
        projection->second.discarded_ambient != discarded_ambient ||
        projection->second.discarded_specular != discarded_specular ||
        projection->second.roughness_factor != roughness_factor ||
        projection->second.emissive_factor != emissive_factor ||
        projection->second.discarded_emissive_alpha !=
            static_cast<float>(native_emissive.a) ||
        projection->second.vertex_colour_tracking_token !=
            static_cast<std::uint8_t>(pass->getVertexColourTracking())) {
      // The comparison above is authoritative and stays exact; what changed
      // is the blast radius. A live disagreement between the stored and the
      // re-derived native authority (a roughness rule revision, an edited
      // pass) costs exactly this object: it goes matte under
      // PROJECTION_AUTHORITY_CHANGED for this capture, the cached projection
      // stays unused, and the object re-admits on a later capture once both
      // derivations agree again. It must never become a terminal snapshot
      // rejection - that would stop the whole session's publication.
      exclusion = OgreNextDemoTextureProjectionExclusion::
          PROJECTION_AUTHORITY_CHANGED;
      return false;
    }
  }

  pending_->used_projections.insert(projection_key);
  pending_->projected_texture_keys.insert(texture_key);
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
        Render::BuildOgre14GraphicsSceneMaterialFallback(input, placeholder);
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
    const Ogre::MaterialPtr &native_material, bool projection_candidate,
    bool has_authored_uv0,
    Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
    bool &projected) noexcept {
  return TryProject(exact_section_key, native_material, projection_candidate,
                    has_authored_uv0, nullptr, input, projected);
}

Render::ValidationResult OgreNextDemoMaterialSource::TryProject(
    std::string_view exact_section_key,
    const Ogre::MaterialPtr &native_material, bool projection_candidate,
    bool has_authored_uv0,
    const Render::Ogre14ManagedMaterialDeclarationBinding *managed_binding,
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
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.native",
          "material projection requires one named section and native owner");
    }

    const OgreNextDemoCuratedCityWorldMaterial *const curated_policy =
        FindOgreNextDemoCuratedCityWorldMaterial(native_material->getName());
    if (curated_policy != nullptr) {
      pending_->curated_cityworld_observed.insert(
          std::string(curated_policy->exact_material_name));
      if (pending_->curated_cityworld_admitted.find(
              std::string(curated_policy->exact_material_name)) ==
          pending_->curated_cityworld_admitted.end()) {
        pending_->curated_cityworld_matte.insert(
            std::string(curated_policy->exact_material_name));
      }
    } else if (HasCuratedCityWorldSphericalFamilyShape(native_material)) {
      pending_->uncurated_spherical_family_matte.insert(
          native_material->getName());
    }

    std::string decision_key;
    AppendField(decision_key, exact_section_key);
    const bool allow_continuous_dust =
        exact_section_key == "particle/tracks/Dust";
    const auto record_candidate_outcome =
        [&](bool selected, OgreNextDemoTextureProjectionExclusion reason)
        -> Render::ValidationResult {
      // Diagnostic only. This mirrors the aggregate accounting into a
      // per-material split so a matte section can be attributed back to the
      // script that declared it; it never gates anything. Bounded by the
      // number of distinct material names in the map.
      const auto record_census =
          [this, &native_material](
              bool census_selected,
              OgreNextDemoTextureProjectionExclusion census_reason) noexcept {
            try {
              auto entry =
                  material_section_census_.find(native_material->getName());
              if (entry == material_section_census_.end()) {
                entry = material_section_census_
                            .emplace(native_material->getName(),
                                     MaterialSectionCensusEntry{})
                            .first;
              }
              if (census_selected) {
                ++entry->second.projected_sections;
                return;
              }
              ++entry->second.matte_sections;
              entry->second.last_exclusion = census_reason;
            } catch (...) {
              // An observation-only ledger must never be able to fail a
              // capture, so a bad_alloc here is swallowed deliberately.
            }
          };
      record_census(selected, reason);
      if (!selected) {
        if (reason == OgreNextDemoTextureProjectionExclusion::NONE) {
          return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                         "ogre_next_demo.material.decision.exclusion",
                         "candidate matte decision has no stable named reason");
        }
        return RecordOgreNextDemoTextureProjectionExclusion(reason,
                                                            pending_->counters);
      }
      if (curated_policy != nullptr) {
        pending_->curated_cityworld_admitted.insert(
            std::string(curated_policy->exact_material_name));
        pending_->curated_cityworld_matte.erase(
            std::string(curated_policy->exact_material_name));
      } else {
        // The spherical-family census reports what is still matte. Once the
        // layered shape is admitted the material leaves that set, so the
        // counter keeps meaning exactly what its name says.
        pending_->uncurated_spherical_family_matte.erase(
            native_material->getName());
      }
      const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
      if (pending_->counters.candidate_sections != maximum) {
        ++pending_->counters.candidate_sections;
      }
      if (pending_->counters.projected_sections != maximum) {
        ++pending_->counters.projected_sections;
      }
      if (pending_->counters.lossy_material_normalizations != maximum) {
        ++pending_->counters.lossy_material_normalizations;
      }
      return Render::ValidationResult::Success();
    };

    auto decision = pending_->cache->decisions.find(decision_key);
    if (decision != pending_->cache->decisions.end()) {
      if (decision->second.exact_resource_group !=
              native_material->getGroup() ||
          decision->second.exact_material_name != native_material->getName() ||
          decision->second.projection_candidate != projection_candidate ||
          decision->second.has_authored_uv0 != has_authored_uv0 ||
          decision->second.exact_cull != input.cull) {
        return Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.decision.identity",
            "the frozen section changed material, UV, or culling identity");
      }
      if (!decision->second.projection_candidate) {
        return Render::ValidationResult::Success();
      }
      if (!decision->second.projected) {
        const bool retryable =
            decision->second.exclusion ==
                OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE ||
            decision->second.exclusion ==
                OgreNextDemoTextureProjectionExclusion::
                    ORDINARY_SELECTED_SOURCE_UNAVAILABLE ||
            decision->second.exclusion ==
                OgreNextDemoTextureProjectionExclusion::
                    PROJECTION_AUTHORITY_CHANGED;
        if (retryable) {
          EnsurePendingCacheWritable();
          decision = pending_->cache->decisions.find(decision_key);
          if (decision == pending_->cache->decisions.end()) {
            return Failure(Render::ValidationCode::MISSING_REFERENCE,
                           "ogre_next_demo.material.decision.retry",
                           "retryable frozen decision disappeared");
          }
          std::string current_projection_key;
          OgreNextDemoTextureProjectionExclusion current_exclusion =
              OgreNextDemoTextureProjectionExclusion::NONE;
          Render::ValidationResult current_failure =
              Render::ValidationResult::Success();
          const bool current_projected = TryProjectCurrent(
              native_material, has_authored_uv0, managed_binding,
              allow_continuous_dust, input,
              current_projection_key, true, current_exclusion,
              current_failure);
          if (!current_failure) {
            return current_failure;
          }
          decision->second.projected = current_projected;
          decision->second.projection_key =
              current_projected ? std::move(current_projection_key)
                                : std::string{};
          decision->second.exclusion =
              current_projected ? OgreNextDemoTextureProjectionExclusion::NONE
                                : current_exclusion;
        }
        Render::ValidationResult accounting = record_candidate_outcome(
            decision->second.projected, decision->second.exclusion);
        if (!accounting) {
          return accounting;
        }
        projected = decision->second.projected;
        return Render::ValidationResult::Success();
      }
      std::string current_projection_key;
      OgreNextDemoTextureProjectionExclusion current_exclusion =
          OgreNextDemoTextureProjectionExclusion::NONE;
      Render::ValidationResult current_failure =
          Render::ValidationResult::Success();
      const auto cached_projection = pending_->cache->projections.find(
          decision->second.projection_key);
      if (managed_binding != nullptr &&
          cached_projection != pending_->cache->projections.end() &&
          !cached_projection->second.managed_binding.initialized()) {
        // TryProjectCurrent may adopt output-equivalent diffuse-only managed
        // authority without changing the opaque-v2 projection key. Make the
        // transaction private first and reacquire the decision iterator: a
        // failed Apply or Discard must leave the committed cache unbound.
        EnsurePendingCacheWritable();
        decision = pending_->cache->decisions.find(decision_key);
        if (decision == pending_->cache->decisions.end()) {
          return Failure(Render::ValidationCode::MISSING_REFERENCE,
                         "ogre_next_demo.material.decision.cache_promotion",
                         "frozen decision disappeared while making managed "
                         "cache promotion transactional");
        }
      }
      const bool current_projected = TryProjectCurrent(
          native_material, has_authored_uv0, managed_binding,
          allow_continuous_dust, input, current_projection_key, false,
          current_exclusion, current_failure);
      if (!current_projected && current_failure &&
          current_exclusion == OgreNextDemoTextureProjectionExclusion::
                                   PROJECTION_AUTHORITY_CHANGED) {
        // Per-object refusal: the frozen decision stays projected so the
        // object re-admits automatically once the disagreement clears; this
        // capture presents the section matte under its own named reason and
        // every other object is untouched.
        Render::ValidationResult authority_accounting =
            record_candidate_outcome(false, current_exclusion);
        if (!authority_accounting) {
          return authority_accounting;
        }
        return Render::ValidationResult::Success();
      }
      if (!current_projected ||
          current_projection_key != decision->second.projection_key) {
        if (!current_failure) {
          return current_failure;
        }
        return Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.decision.projection",
            "the frozen projected material changed native authority");
      }
      projected = true;
      return record_candidate_outcome(
          true, OgreNextDemoTextureProjectionExclusion::NONE);
    }

    EnsurePendingCacheWritable();
    const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
    if (pending_->counters.new_frozen_material_decisions != maximum) {
      ++pending_->counters.new_frozen_material_decisions;
    }
    ProjectionDecision new_decision;
    new_decision.exact_resource_group = native_material->getGroup();
    new_decision.exact_material_name = native_material->getName();
    new_decision.exact_cull = input.cull;
    new_decision.projection_candidate = projection_candidate;
    new_decision.has_authored_uv0 = has_authored_uv0;
    if (projection_candidate) {
      OgreNextDemoTextureProjectionExclusion current_exclusion =
          OgreNextDemoTextureProjectionExclusion::NONE;
      Render::ValidationResult current_failure =
          Render::ValidationResult::Success();
      new_decision.projected = TryProjectCurrent(
          native_material, has_authored_uv0, managed_binding,
          allow_continuous_dust, input,
          new_decision.projection_key, true, current_exclusion,
          current_failure);
      if (!current_failure) {
        return current_failure;
      }
      new_decision.exclusion =
          new_decision.projected ? OgreNextDemoTextureProjectionExclusion::NONE
                                 : current_exclusion;
      Render::ValidationResult accounting = record_candidate_outcome(
          new_decision.projected, new_decision.exclusion);
      if (!accounting) {
        return accounting;
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
    return Failure(
        Render::ValidationCode::EMPTY_PAYLOAD,
        "ogre_next_demo.material.decision.allocation",
        "allocation failed before the material decision was published");
  } catch (...) {
    return Failure(
        Render::ValidationCode::UNSUPPORTED_FEATURE,
        "ogre_next_demo.material.decision.exception",
        "unexpected failure before the material decision was published");
  }
}

Render::ValidationResult OgreNextDemoMaterialSource::Apply(
    std::vector<Render::GraphicsSceneAssetInput> &assets,
    OgreNextDemoMaterialApplyTiming *timing) noexcept {
  try {
    if (pending_ == nullptr || !pending_->capture_open) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.material.pending",
                     "material projection has no open capture transaction");
    }
    OgreNextDemoMaterialApplyTiming candidate_timing;
    const auto input_index_started = std::chrono::steady_clock::now();
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
    candidate_timing.input_index_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - input_index_started)
            .count());

    const auto publication_plan_started = std::chrono::steady_clock::now();
    std::vector<OgreNextDemoCachedProjectionPublicationInput>
        cached_projection_publications;
    std::vector<OgreNextDemoCachedTexturePublicationInput>
        cached_texture_publications;
    std::vector<OgreNextDemoCachedSamplerPublicationInput>
        cached_sampler_publications;
    std::vector<std::string> used_projection_keys;
    used_projection_keys.reserve(pending_->used_projections.size());
    used_projection_keys.assign(pending_->used_projections.begin(),
                                pending_->used_projections.end());
    const bool retained_publication_available =
        pending_->cache->retained_publication_valid &&
        pending_->cache->retained_used_projection_keys == used_projection_keys;
    if (!retained_publication_available) {
      cached_projection_publications.reserve(
          pending_->cache->projections.size());
      cached_texture_publications.reserve(pending_->cache->textures.size());
      cached_sampler_publications.reserve(pending_->cache->samplers.size());
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
        input.source_mode = texture.second.source;
        cached_texture_publications.push_back(std::move(input));
      }
      for (const auto &sampler : pending_->cache->samplers) {
        OgreNextDemoCachedSamplerPublicationInput input;
        input.sampler_key = sampler.first;
        input.sampler_source_id = sampler.second.source_id;
        cached_sampler_publications.push_back(std::move(input));
      }
    }

    class SourcePublicationBatchValidator final
        : public IOgreNextDemoTexturePublicationBatchValidator {
    public:
      SourcePublicationBatchValidator(
          State &pending,
          const Render::IOgre14AuthenticatedTextureResolver *resolver,
          const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider,
          const Render::IOgre14SelectedTextureSourceResolver *ordinary_resolver,
          std::uint64_t &authority_validation_ns)
          : pending_(pending), resolver_(resolver), provider_(provider),
            ordinary_resolver_(ordinary_resolver),
            authority_validation_ns_(authority_validation_ns) {}

      Render::ValidationResult ValidateReachableAuthenticatedTextureBatch(
          const std::vector<std::string> &texture_keys) override {
        AuthorityTimer timer(authority_validation_ns_);
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
          OgreNextDemoExactTextureObservation fresh_texture_observation;
          Render::ValidationResult native_validation =
              RevalidatePendingNativeTextureOwners(
                  native_texture, pending_observation->second.native_owners,
                  fresh_texture_observation);
          if (!native_validation) {
            return native_validation;
          }
          Render::Ogre14AuthenticatedTextureResolution fresh;
          if (!resolver_->RequiresAuthenticatedTextureSource(*native_texture) ||
              !ResolveFrozenAuthenticatedTexture(
                  *native_texture, *resolver_, texture->second,
                  fresh_texture_observation, &fresh)) {
            return Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "authenticated.batch_revalidation",
                "reachable authenticated source changed before publication");
          }
          PendingAuthenticatedTextureObservation observation;
          observation.native_texture = std::move(native_texture);
          observation.resolution = std::move(fresh);
          observation.native_owners = pending_observation->second.native_owners;
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

      Render::ValidationResult ValidateReachableOrdinaryTextureBatch(
          const std::vector<std::string> &texture_keys) override {
        AuthorityTimer timer(authority_validation_ns_);
        if (texture_keys.empty()) {
          return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                         "ordinary.batch",
                         "empty ordinary publication batch was invoked");
        }
        if (resolver_ == nullptr || ordinary_resolver_ == nullptr) {
          return Failure(
              Render::ValidationCode::MISSING_REFERENCE,
              "ordinary.batch_authority",
              "ordinary publication has no bound selected-source resolver");
        }
        for (const std::string &texture_key : texture_keys) {
          const auto texture = pending_.cache->textures.find(texture_key);
          const auto pending_observation =
              pending_.ordinary_texture_observations.find(texture_key);
          if (texture == pending_.cache->textures.end() ||
              pending_observation ==
                  pending_.ordinary_texture_observations.end() ||
              !pending_observation->second.native_texture ||
              !pending_observation->second.resolution.initialized()) {
            return Failure(Render::ValidationCode::MISSING_REFERENCE,
                           "ordinary.pending_observation",
                           "reachable ordinary texture has no strong pending "
                           "selected-source observation");
          }
          Ogre::TexturePtr native_texture =
              pending_observation->second.native_texture;
          OgreNextDemoExactTextureObservation fresh_texture_observation;
          Render::ValidationResult native_validation =
              RevalidatePendingNativeTextureOwners(
                  native_texture, pending_observation->second.native_owners,
                  fresh_texture_observation);
          if (!native_validation) {
            return native_validation;
          }
          Render::Ogre14SelectedTextureSourceResolution fresh;
          if (resolver_->RequiresAuthenticatedTextureSource(*native_texture) ||
              !ResolveFrozenOrdinaryTexture(
                  *native_texture, *ordinary_resolver_, texture->second,
                  fresh_texture_observation, &fresh)) {
            return Failure(Render::ValidationCode::REVISION_MISMATCH,
                           "ordinary.batch_revalidation",
                           "reachable ordinary selected source changed before "
                           "publication");
          }
          const Render::Ogre14SelectedTextureSourceReceipt *const
              pending_receipt =
                  pending_observation->second.resolution.source_receipt();
          const Render::Ogre14SelectedTextureSourceReceipt *const
              fresh_receipt = fresh.source_receipt();
          if (pending_receipt == nullptr || fresh_receipt == nullptr ||
              !fresh_receipt->SharesImmutableStateWith(*pending_receipt) ||
              !ordinary_resolver_->RevalidateSelectedTextureSource(
                  *native_texture, fresh)) {
            return Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "ordinary.final_authority",
                "fresh ordinary receipt did not share the pending immutable "
                "source immediately before publication");
          }
        }
        return Render::ValidationResult::Success();
      }

    private:
      class AuthorityTimer final {
      public:
        explicit AuthorityTimer(std::uint64_t &total) noexcept
            : total_(total), started_(std::chrono::steady_clock::now()) {}
        ~AuthorityTimer() {
          const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - started_)
                                   .count();
          if (elapsed <= 0) {
            return;
          }
          const std::uint64_t value = static_cast<std::uint64_t>(elapsed);
          total_ = value > (std::numeric_limits<std::uint64_t>::max)() - total_
                       ? (std::numeric_limits<std::uint64_t>::max)()
                       : total_ + value;
        }

      private:
        std::uint64_t &total_;
        std::chrono::steady_clock::time_point started_;
      };

      State &pending_;
      const Render::IOgre14AuthenticatedTextureResolver *resolver_;
      const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider_;
      const Render::IOgre14SelectedTextureSourceResolver *ordinary_resolver_;
      std::uint64_t &authority_validation_ns_;
    } batch_validator(*pending_, texture_resolver_, texture_authority_provider_,
                      ordinary_texture_source_resolver_,
                      candidate_timing.authority_validation_ns);

    OgreNextDemoCachedProjectionPublicationTransaction publication_transaction;
    Render::ValidationResult publication_validation =
        Render::ValidationResult::Success();
    if (retained_publication_available) {
      publication_transaction = pending_->cache->retained_publication;
      if (!publication_transaction.authenticated_texture_keys.empty()) {
        publication_validation =
            batch_validator.ValidateReachableAuthenticatedTextureBatch(
                publication_transaction.authenticated_texture_keys);
      }
      if (publication_validation &&
          !publication_transaction.ordinary_texture_keys.empty()) {
        publication_validation =
            batch_validator.ValidateReachableOrdinaryTextureBatch(
                publication_transaction.ordinary_texture_keys);
      }
      if (!publication_validation) {
        publication_validation.field =
            "ogre_next_demo.material.publication." +
            publication_validation.field;
      }
      candidate_timing.retained_authority_plan_reused =
          static_cast<bool>(publication_validation);
    } else {
      publication_validation =
          BuildOgreNextDemoCachedProjectionPublicationTransaction(
              cached_projection_publications, cached_texture_publications,
              cached_sampler_publications, used_projection_keys,
              batch_validator, publication_transaction);
    }
    if (!publication_validation) {
      return publication_validation;
    }
    const std::uint64_t publication_total_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - publication_plan_started)
            .count());
    candidate_timing.publication_plan_ns =
        publication_total_ns > candidate_timing.authority_validation_ns
            ? publication_total_ns - candidate_timing.authority_validation_ns
            : 0U;
    if (!retained_publication_available) {
      EnsurePendingCachePrivateForDerivedState();
      pending_->cache->retained_used_projection_keys = used_projection_keys;
      pending_->cache->retained_publication = publication_transaction;
      pending_->cache->retained_publication_valid = true;
      pending_->cache->retained_owner_assets_valid = false;
      pending_->cache->retained_owner_assets.clear();
      pending_->cache->retained_owner_asset_count = 0U;
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
      const auto existing =
          std::find_if(candidate.begin(), candidate.end(),
                       [source_asset_id](const auto &asset) {
                         return asset.source_asset_id == source_asset_id;
                       });
      if (existing == candidate.end() || !existing->payload || !payload ||
          existing->payload->valueless_by_exception() ||
          payload->valueless_by_exception() ||
          !Render::EquivalentRenderAssetPayload(*existing->payload, *payload) ||
          existing->material_bindings != dependency.material_bindings) {
        return Failure(
            Render::ValidationCode::DUPLICATE_IDENTIFIER, field,
            "projected dependency ID collides with a different input asset");
      }
      return Render::ValidationResult::Success();
    };
    const auto append_projected_material =
        [&](const Projection &projection,
            const Render::GraphicsSceneAssetInput &projected_material)
        -> Render::ValidationResult {
      auto material = std::find_if(
          candidate.begin(), candidate.end(), [&](const auto &asset) {
            return asset.source_asset_id == projection.material_source_id;
          });
      if (material == candidate.end()) {
        if (!asset_ids.insert(projected_material.source_asset_id).second) {
          return Failure(
              Render::ValidationCode::DUPLICATE_IDENTIFIER,
              "ogre_next_demo.material.material_collision",
              "projected material ID is occupied without an input asset");
        }
        candidate.push_back(projected_material);
        return Render::ValidationResult::Success();
      }
      if (!material->payload || !projection.placeholder_payload ||
          !projection.material_payload ||
          material->payload->valueless_by_exception() ||
          Render::RenderAssetPayloadKind(*material->payload) !=
              Render::RenderAssetKind::MATERIAL) {
        return Failure(
            Render::ValidationCode::DUPLICATE_IDENTIFIER,
            "ogre_next_demo.material.material_collision",
            "projected material ID collides with a nonmaterial asset");
      }
      const bool exact_placeholder =
          Render::EquivalentRenderAssetPayload(*material->payload,
                                               *projection.placeholder_payload) &&
          material->material_bindings ==
              Render::GraphicsSceneAssetInput{}.material_bindings;
      const bool exact_projected =
          Render::EquivalentRenderAssetPayload(*material->payload,
                                               *projection.material_payload) &&
          material->material_bindings == projected_material.material_bindings;
      if (!exact_placeholder && !exact_projected) {
        return Failure(
            Render::ValidationCode::DUPLICATE_IDENTIFIER,
            "ogre_next_demo.material.material_collision",
            "projected material ID collides with a different material");
      }
      *material = projected_material;
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
    const auto owner_publication_started = std::chrono::steady_clock::now();
    const std::uint64_t authority_before_owner =
        candidate_timing.authority_validation_ns;
    const bool retained_owner_assets_available =
        candidate_timing.retained_authority_plan_reused &&
        pending_->cache->retained_owner_assets_valid;
    candidate_timing.retained_owner_publication_reused =
        retained_owner_assets_available;
    std::set<std::uint64_t> retained_owner_asset_ids;
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
      retained_owner_asset_ids.insert(owner.material_source_id);
      retained_owner_asset_ids.insert(owner.texture_source_id);
      retained_owner_asset_ids.insert(owner.sampler_source_id);
      const auto specular_texture =
          projection->second.managed_specular_texture_key.empty()
              ? pending_->cache->managed_specular_textures.end()
              : pending_->cache->managed_specular_textures.find(
                    projection->second.managed_specular_texture_key);
      const auto specular_sampler =
          projection->second.managed_specular_sampler_key.empty()
              ? pending_->cache->samplers.end()
              : pending_->cache->samplers.find(
                    projection->second.managed_specular_sampler_key);
      const bool projection_reachable =
          pending_->used_projections.find(owner.projection_key) !=
          pending_->used_projections.end();
      const auto owner_authority_started = std::chrono::steady_clock::now();
      if (projection_reachable && projection->second.curated_cityworld &&
          (material_script_resolver_ == nullptr ||
           texture_resolver_ == nullptr ||
           ordinary_texture_source_resolver_ == nullptr ||
           !RevalidateCuratedCityWorldProjection(
               projection->second, *material_script_resolver_,
               *texture_resolver_, *ordinary_texture_source_resolver_))) {
        return Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.curated_cityworld.final_authority",
            "reviewed CityWorld declaration, TUS2 pending environment, or texture authority changed before publication");
      }
      if (projection_reachable &&
          projection->second.managed_binding.initialized() &&
          (texture_resolver_ == nullptr ||
           ordinary_texture_source_resolver_ == nullptr ||
           !projection->second.managed_binding.Revalidate(
               *texture_resolver_, *ordinary_texture_source_resolver_) ||
           (!projection->second.managed_specular_texture_key.empty() &&
            (specular_texture ==
                 pending_->cache->managed_specular_textures.end() ||
             specular_sampler == pending_->cache->samplers.end() ||
             !RevalidateManagedSpecularNativeProjection(
                 projection->second, *texture_resolver_,
                 *ordinary_texture_source_resolver_))))) {
        return Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.final_authority",
            "managed diffuse/specular source authority changed before publication");
      }
      candidate_timing.authority_validation_ns +=
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - owner_authority_started)
                  .count());
      if (specular_texture !=
          pending_->cache->managed_specular_textures.end()) {
        retained_owner_asset_ids.insert(specular_texture->second.source_id);
        retained_owner_asset_ids.insert(specular_sampler->second.source_id);
      }
      if (retained_owner_assets_available) {
        continue;
      }
      Render::GraphicsSceneAssetInput projected_material;
      projected_material.source_asset_id =
          projection->second.material_source_id;
      projected_material.payload = projection->second.material_payload;
      projected_material.material_bindings[static_cast<std::size_t>(
          Render::MaterialTextureSlot::BASE_COLOR)] = {
          texture->second.source_id, sampler->second.source_id};
      if (specular_texture !=
          pending_->cache->managed_specular_textures.end()) {
        projected_material.material_bindings[static_cast<std::size_t>(
            Render::MaterialTextureSlot::SPECULAR)] = {
            specular_texture->second.source_id,
            specular_sampler->second.source_id};
      }
      Render::ValidationResult validation = append_projected_material(
          projection->second, projected_material);
      if (!validation) {
        return validation;
      }

      validation = append_dependency(
          texture->second.source_id, texture->second.payload,
          "ogre_next_demo.material.texture_collision");
      if (!validation) {
        return validation;
      }
      validation =
          append_dependency(sampler->second.source_id, sampler->second.payload,
                            "ogre_next_demo.material.sampler_collision");
      if (!validation) {
        return validation;
      }
      if (specular_texture !=
          pending_->cache->managed_specular_textures.end()) {
        validation = append_dependency(
            specular_texture->second.source_id,
            specular_texture->second.payload,
            "ogre_next_demo.material.specular_texture_collision");
        if (!validation) {
          return validation;
        }
        validation = append_dependency(
            specular_sampler->second.source_id,
            specular_sampler->second.payload,
            "ogre_next_demo.material.specular_sampler_collision");
        if (!validation) {
          return validation;
        }
      }
    }
    if (retained_owner_assets_available) {
      for (const Render::GraphicsSceneAssetInput &asset :
           pending_->cache->retained_owner_assets) {
        const auto projection = std::find_if(
            pending_->cache->projections.begin(),
            pending_->cache->projections.end(), [&](const auto &entry) {
              return entry.second.material_source_id == asset.source_asset_id;
            });
        Render::ValidationResult validation =
            projection != pending_->cache->projections.end()
                ? append_projected_material(projection->second, asset)
                : append_dependency(asset.source_asset_id, asset.payload,
                                    "ogre_next_demo.material.retained_owner_collision");
        if (!validation) {
          return validation;
        }
      }
      candidate_timing.retained_owner_asset_count =
          pending_->cache->retained_owner_asset_count;
    } else {
      EnsurePendingCachePrivateForDerivedState();
      pending_->cache->retained_owner_assets.clear();
      pending_->cache->retained_owner_assets.reserve(
          retained_owner_asset_ids.size());
      for (const Render::GraphicsSceneAssetInput &asset : candidate) {
        if (retained_owner_asset_ids.find(asset.source_asset_id) !=
            retained_owner_asset_ids.end()) {
          pending_->cache->retained_owner_assets.push_back(asset);
        }
      }
      pending_->cache->retained_owner_asset_count =
          retained_owner_asset_ids.size();
      pending_->cache->retained_owner_assets_valid = true;
      candidate_timing.retained_owner_asset_count =
          retained_owner_asset_ids.size();
    }
    const std::uint64_t owner_total_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - owner_publication_started)
            .count());
    const std::uint64_t owner_authority_ns =
        candidate_timing.authority_validation_ns - authority_before_owner;
    candidate_timing.owner_publication_ns =
        owner_total_ns > owner_authority_ns ? owner_total_ns - owner_authority_ns
                                             : 0U;
    const auto accounting_and_sort_started = std::chrono::steady_clock::now();
    if (pending_->counters.gpu_readbacks != 0U ||
        pending_->counters.authenticated_gpu_readbacks != 0U ||
        pending_->counters.unauthenticated_gpu_readbacks != 0U) {
      return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                     "ogre_next_demo.material.gpu_readbacks",
                     "material texture publication observed a forbidden GPU "
                     "readback");
    }
    OgreNextDemoTextureSourceCounters accounting_audit;
    const Render::ValidationResult accounting_validation =
        AccumulateOgreNextDemoTextureSourceCounters(CurrentCaptureCounters(),
                                                    accounting_audit);
    if (!accounting_validation) {
      return accounting_validation;
    }
    std::sort(candidate.begin(), candidate.end(),
              [](const auto &first, const auto &second) {
                return first.source_asset_id < second.source_asset_id;
              });
    assets = std::move(candidate);
    candidate_timing.accounting_and_sort_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - accounting_and_sort_started)
            .count());
    if (timing != nullptr) {
      *timing = candidate_timing;
    }
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
  for (const std::string &projection_key : pending_->used_projections) {
    const auto projection = pending_->cache->projections.find(projection_key);
    if (projection == pending_->cache->projections.end() ||
        !projection->second.material_payload) {
      continue;
    }
    const auto *const material =
        std::get_if<Render::MaterialDescriptor>(
            projection->second.material_payload.get());
    if (material == nullptr) {
      continue;
    }
    switch (material->blend_mode) {
    case Render::MaterialBlendMode::REPLACE:
      ++counters.active_replace_material_projections;
      break;
    case Render::MaterialBlendMode::STRAIGHT_SOURCE_OVER:
      ++counters.active_straight_source_over_material_projections;
      break;
    case Render::MaterialBlendMode::LEGACY_STRAIGHT_ALPHA:
      ++counters.active_legacy_straight_alpha_material_projections;
      break;
    case Render::MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER:
      ++counters.active_premultiplied_source_over_material_projections;
      break;
    }
    switch (material->alpha_test_mode) {
    case Render::MaterialAlphaTestMode::DISABLED:
      ++counters.active_alpha_test_disabled_material_projections;
      break;
    case Render::MaterialAlphaTestMode::GREATER:
      ++counters.active_alpha_test_greater_material_projections;
      break;
    case Render::MaterialAlphaTestMode::GREATER_EQUAL:
      ++counters.active_alpha_test_greater_equal_material_projections;
      break;
    }
    switch (material->pbr_workflow) {
    case Render::MaterialPbrWorkflow::METALLIC_ROUGHNESS:
      ++counters.active_metallic_roughness_workflow_projections;
      break;
    case Render::MaterialPbrWorkflow::SPECULAR:
      ++counters.active_specular_workflow_projections;
      break;
    }
    if (IsCanonicalAnisotropicSampler(
            projection->second.sampler_observation) ||
        (!projection->second.managed_specular_sampler_key.empty() &&
         IsCanonicalAnisotropicSampler(
             projection->second.managed_specular_sampler_observation))) {
      ++counters.active_anisotropic_sampler_projections;
    }
  }
  counters.distinct_eligible_texture_keys =
      pending_->eligible_texture_keys.size();
  counters.distinct_projected_texture_keys =
      pending_->projected_texture_keys.size();
  counters.distinct_matte_only_texture_keys = 0U;
  for (const std::string &texture_key : pending_->eligible_texture_keys) {
    if (pending_->projected_texture_keys.find(texture_key) ==
        pending_->projected_texture_keys.end()) {
      ++counters.distinct_matte_only_texture_keys;
    }
  }
  counters.active_texture_state_observations =
      pending_->active_native_texture_observations.size();
  for (const auto &entry : pending_->active_native_texture_observations) {
    const OgreNextDemoExactTextureObservation &observation = entry.second;
    counters.active_legacy_native_additional_mip_levels +=
        observation.additional_mip_count;
    counters.active_legacy_texture_unit_gamma_nonunit_observations +=
        observation.texture_unit_gamma != 1.0F ? 1U : 0U;
    counters.active_legacy_texture_gamma_nonunit_observations +=
        observation.texture_gamma != 1.0F ? 1U : 0U;
    counters.active_legacy_texture_unit_hardware_gamma_off_observations +=
        !observation.texture_unit_hardware_gamma ? 1U : 0U;
    counters.active_legacy_hardware_gamma_off_observations +=
        !observation.texture_hardware_gamma ? 1U : 0U;
    counters.active_legacy_automipmap_observations +=
        (observation.usage_token &
         static_cast<std::uint32_t>(Ogre::TU_AUTOMIPMAP)) != 0U
            ? 1U
            : 0U;
  }
  for (const auto &entry : pending_->active_normalization_observations) {
    ++counters.active_normalized_texture_observations;
    switch (entry.second.policy) {
    case OgreNextDemoTextureNormalizationObservation::Policy::SRGB_OPAQUE_V2:
      ++counters.active_opaque_texture_normalizations;
      break;
    case OgreNextDemoTextureNormalizationObservation::Policy::
        SRGB_STRAIGHT_ALPHA_V1:
      ++counters.active_straight_alpha_texture_normalizations;
      break;
    case OgreNextDemoTextureNormalizationObservation::Policy::
        LINEAR_SPECULAR_V1:
      ++counters.active_linear_specular_texture_normalizations;
      break;
    }
    counters.active_authored_mip_prefix_levels +=
        entry.second.authored_mip_prefix_levels;
    counters.active_generated_mip_tail_levels +=
        entry.second.generated_mip_tail_levels;
    counters.active_normalized_output_mip_levels +=
        entry.second.authored_mip_prefix_levels +
        entry.second.generated_mip_tail_levels;
  }
  for (auto current = pending_->used_projections.begin();
       current != pending_->used_projections.end(); ++current) {
    const auto projection = pending_->cache->projections.find(*current);
    if (projection == pending_->cache->projections.end() ||
        projection->second.managed_specular_texture_key.empty()) {
      continue;
    }
    bool already_counted = false;
    for (auto previous = pending_->used_projections.begin();
         previous != current; ++previous) {
      const auto prior_projection =
          pending_->cache->projections.find(*previous);
      if (prior_projection != pending_->cache->projections.end() &&
          prior_projection->second.managed_specular_texture_key ==
              projection->second.managed_specular_texture_key) {
        already_counted = true;
        break;
      }
    }
    if (already_counted) {
      continue;
    }
    const auto specular = pending_->cache->managed_specular_textures.find(
        projection->second.managed_specular_texture_key);
    if (specular == pending_->cache->managed_specular_textures.end()) {
      continue;
    }
    const OgreNextDemoTextureNormalizationObservation &observation =
        specular->second.normalization_observation;
    ++counters.active_normalized_texture_observations;
    if (observation.policy == OgreNextDemoTextureNormalizationObservation::
                                  Policy::LINEAR_SPECULAR_V1) {
      ++counters.active_linear_specular_texture_normalizations;
    }
    counters.active_authored_mip_prefix_levels +=
        observation.authored_mip_prefix_levels;
    counters.active_generated_mip_tail_levels +=
        observation.generated_mip_tail_levels;
    counters.active_normalized_output_mip_levels +=
        observation.authored_mip_prefix_levels +
        observation.generated_mip_tail_levels;
  }
  return counters;
}

OgreNextDemoCuratedCityWorldCoverage
OgreNextDemoMaterialSource::CurrentCuratedCityWorldCoverage() const noexcept {
  OgreNextDemoCuratedCityWorldCoverage coverage;
  if (pending_ == nullptr || !pending_->capture_open) {
    return coverage;
  }
  coverage.observed_entries = pending_->curated_cityworld_observed.size();
  coverage.admitted_entries = pending_->curated_cityworld_admitted.size();
  coverage.matte_entries = pending_->curated_cityworld_matte.size();
  // Every admitted row deliberately retains its authenticated spherical TUS2
  // as pending authority. No environment binding is published in this slice.
  coverage.environment_pending_entries = coverage.admitted_entries;
  coverage.uncurated_spherical_family_matte_materials =
      pending_->uncurated_spherical_family_matte.size();
  return coverage;
}

OgreNextDemoMaterialSourceCounters
OgreNextDemoMaterialSource::LifetimeCounters() const noexcept {
  return lifetime_counters_;
}

std::size_t
OgreNextDemoMaterialSource::MaterialSectionCensusSize() const noexcept {
  return material_section_census_.size();
}

std::string
OgreNextDemoMaterialSource::FormatMaterialSectionCensus() const noexcept {
  try {
    std::ostringstream stream;
    for (const auto &entry : material_section_census_) {
      // One record per material: name, how many of its sections reached the
      // projected path, how many stayed matte, and the reason the last matte
      // one carried. `reason` is `none` exactly when nothing was refused.
      stream << "material=" << entry.first
             << " projected=" << entry.second.projected_sections
             << " matte=" << entry.second.matte_sections << " reason="
             << OgreNextDemoTextureProjectionExclusionName(
                    entry.second.last_exclusion)
             << '\n';
    }
    return stream.str();
  } catch (...) {
    return std::string{};
  }
}

void OgreNextDemoMaterialSource::Commit() noexcept {
  if (pending_ != nullptr && pending_->capture_open) {
    pending_->counters = CurrentCaptureCounters();
    if (!AccumulateOgreNextDemoTextureSourceCounters(pending_->counters,
                                                     lifetime_counters_)) {
      pending_.reset();
      return;
    }
    pending_->capture_open = false;
    pending_->used_projections.clear();
    pending_->authenticated_texture_observations.clear();
    pending_->ordinary_texture_observations.clear();
    pending_->source_cache_hits.clear();
    pending_->eligible_texture_keys.clear();
    pending_->projected_texture_keys.clear();
    pending_->active_native_texture_observations.clear();
    pending_->active_normalization_observations.clear();
    pending_->curated_cityworld_observed.clear();
    pending_->curated_cityworld_admitted.clear();
    pending_->curated_cityworld_matte.clear();
    pending_->uncurated_spherical_family_matte.clear();
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
