/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoMaterialSource.h"

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.h"
#include "gfx/ogre14/Ogre14SelectedTextureSource.h"
#include "gfx/render/MaterialDescriptor.h"
#include "gfx/render/Ogre14SourceTextureDecoder.h"
#include "gfx/render/RenderAssetRegistry.h"
#include "gfx/render/RenderResourceDescriptors.h"

#include <OgreBuildSettings.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>

#include <algorithm>
#include <array>
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
  std::uint32_t pass_iteration_count = 0U;
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
  observation.pass_iteration_count = pass.getPassIterationCount();
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

bool IsExactManagedSpecularPass(
    const ExactPassObservation &observation) noexcept {
  return observation.diffuse == std::array<float, 4U>{1.0F, 1.0F, 1.0F,
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
         observation.depth_check && observation.depth_write &&
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
      observation.manual_cull_mode != Ogre::MANUAL_CULL_BACK ||
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
         IsExactManagedSpecularPass(ObserveExactPass(*specular)) &&
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

struct Projection final {
  std::string exact_name;
  std::string texture_key;
  std::string sampler_key;
  std::string managed_specular_texture_key;
  std::string managed_specular_sampler_key;
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

struct MaterialCache final {
  OgreNextDemoIdentityRegistry identities;
  std::map<std::string, CapturedTexture, std::less<>> textures;
  std::map<std::string, CapturedSampler, std::less<>> samplers;
  std::map<std::string, CapturedManagedSpecularTexture, std::less<>>
      managed_specular_textures;
  std::map<std::string, Projection, std::less<>> projections;
  std::map<std::string, ProjectionDecision, std::less<>> decisions;
};

struct PendingNativeTextureOwner final {
  Ogre::MaterialPtr native_material;
  std::uintptr_t native_pass_pointer_token = 0U;
  std::uintptr_t native_unit_pointer_token = 0U;
  std::uintptr_t native_sampler_pointer_token = 0U;
  OgreNextDemoExactSamplerObservation sampler_observation;
  OgreNextDemoExactTextureObservation texture_observation;
  ExactPassObservation pass_observation;
  bool allow_alexis_approximation = false;
  std::size_t technique_pass_count = 0U;
  std::size_t pass_texture_unit_count = 0U;
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
        !IsCanonicalPass(*pass, owner.allow_alexis_approximation) ||
        !HasAvailableNamedTextureSource(*unit) ||
        !IsCanonicalTextureUnitSemantic(*unit) ||
        (!owner.allow_alexis_approximation &&
         (technique->getNumPasses() != 1U ||
          pass->getNumTextureUnitStates() != 1U ||
          HasAuthoredProgram(*pass))) ||
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
    const Render::Ogre14ManagedMaterialDeclarationBinding *managed_binding,
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
  if (pass == nullptr || pass->getNumTextureUnitStates() == 0U) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
    return false;
  }
  const ExactPassObservation pass_observation = ObserveExactPass(*pass);
  Render::MaterialBlendMode blend_mode = Render::MaterialBlendMode::REPLACE;
  Render::MaterialAlphaTestMode alpha_test_mode =
      Render::MaterialAlphaTestMode::DISABLED;
  if (!ClassifyCanonicalPass(pass_observation, allow_alexis_approximation,
                             blend_mode, alpha_test_mode)) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STATE_UNSUPPORTED;
    return false;
  }
  if (!allow_alexis_approximation &&
      (technique->getNumPasses() != 1U ||
       pass->getNumTextureUnitStates() != 1U || HasAuthoredProgram(*pass))) {
    exclusion =
        OgreNextDemoTextureProjectionExclusion::MATERIAL_STRUCTURE_UNSUPPORTED;
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
  if (managed_binding != nullptr) {
    if (!managed_binding->initialized() ||
        !managed_binding->MatchesExactMaterial(native_material) ||
        texture_resolver_ == nullptr ||
        ordinary_texture_source_resolver_ == nullptr ||
        !managed_binding->Revalidate(*texture_resolver_,
                                     *ordinary_texture_source_resolver_)) {
      failure = Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.material.managed.authority",
          "managed declaration binding is not current for the exact material");
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
      if (!ObserveManagedSpecularNativeFacts(
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
    owner.technique_pass_count = technique->getNumPasses();
    owner.pass_texture_unit_count = pass->getNumTextureUnitStates();
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
      const Render::ManagedMaterialTextureSourceIdentity *const identity =
          managed_specular->identity();
      if (identity == nullptr || !managed_specular->initialized() ||
          managed_specular->source_bytes() == nullptr ||
          managed_specular->source_size() == 0U ||
          managed_binding == nullptr ||
          !managed_binding->Revalidate(*texture_resolver_,
                                       *ordinary_texture_source_resolver_)) {
        failure = Failure(
            Render::ValidationCode::REVISION_MISMATCH,
            "ogre_next_demo.material.managed.specular_authority",
            "managed specular source authority changed before decode");
        return false;
      }
      AppendField(managed_specular_texture_key,
                  kManagedSpecularTextureIdDomain);
      AppendDigest(managed_specular_texture_key,
                   managed_specular->canonical_identity_sha256());
      auto specular_texture = pending_->cache->managed_specular_textures.find(
          managed_specular_texture_key);
      if (specular_texture ==
          pending_->cache->managed_specular_textures.end()) {
        CapturedManagedSpecularTexture captured_specular;
        Render::ValidationResult managed_validation =
            DeriveOgreNextDemoSourceId(
                kManagedSpecularTextureIdDomain,
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
            !managed_binding->Revalidate(
                *texture_resolver_, *ordinary_texture_source_resolver_)) {
          failure = managed_validation
                        ? Failure(Render::ValidationCode::REVISION_MISMATCH,
                                  "ogre_next_demo.material.managed.specular_"
                                  "final_authority",
                                  "managed specular source authority changed "
                                  "during decode")
                        : std::move(managed_validation);
          return false;
        }
        std::string specular_identity(kManagedSpecularTextureIdDomain);
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
    captured.exact_name = preserves_opaque_v2_identity
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
      if (managed_specular != nullptr) {
        captured.managed_native_material_owner = native_material;
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
      }
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
      material.specular_factor = {1.0F, 1.0F, 1.0F};
      material.specular_texture.texture_coordinate_set = 0U;
    }
    captured.roughness_factor = static_cast<float>(
        std::sqrt(2.0 / (static_cast<double>(pass->getShininess()) + 2.0)));
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
    const float roughness_factor = static_cast<float>(
        std::sqrt(2.0 / (static_cast<double>(pass->getShininess()) + 2.0)));
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
      failure = Failure(Render::ValidationCode::REVISION_MISMATCH,
                        "ogre_next_demo.material.projection.native",
                        "projected native material authority changed");
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

    std::string decision_key;
    AppendField(decision_key, exact_section_key);
    const auto record_candidate_outcome =
        [&](bool selected, OgreNextDemoTextureProjectionExclusion reason)
        -> Render::ValidationResult {
      if (!selected) {
        if (reason == OgreNextDemoTextureProjectionExclusion::NONE) {
          return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                         "ogre_next_demo.material.decision.exclusion",
                         "candidate matte decision has no stable named reason");
        }
        return RecordOgreNextDemoTextureProjectionExclusion(reason,
                                                            pending_->counters);
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
                    ORDINARY_SELECTED_SOURCE_UNAVAILABLE;
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
              native_material, has_authored_uv0, managed_binding, input,
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
      if (!TryProjectCurrent(native_material, has_authored_uv0,
                             managed_binding, input, current_projection_key,
                             false, current_exclusion, current_failure) ||
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
          native_material, has_authored_uv0, managed_binding, input,
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
      input.source_mode = texture.second.source;
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

    class SourcePublicationBatchValidator final
        : public IOgreNextDemoTexturePublicationBatchValidator {
    public:
      SourcePublicationBatchValidator(
          State &pending,
          const Render::IOgre14AuthenticatedTextureResolver *resolver,
          const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider,
          const Render::IOgre14SelectedTextureSourceResolver *ordinary_resolver)
          : pending_(pending), resolver_(resolver), provider_(provider),
            ordinary_resolver_(ordinary_resolver) {}

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
      State &pending_;
      const Render::IOgre14AuthenticatedTextureResolver *resolver_;
      const Render::IOgre14AuthenticatedTextureAuthorityProvider *provider_;
      const Render::IOgre14SelectedTextureSourceResolver *ordinary_resolver_;
    } batch_validator(*pending_, texture_resolver_, texture_authority_provider_,
                      ordinary_texture_source_resolver_);

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
      if (projection->second.managed_binding.initialized() &&
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
      auto material = std::find_if(
          candidate.begin(), candidate.end(), [&](const auto &asset) {
            return asset.source_asset_id ==
                   projection->second.material_source_id;
          });
      if (material == candidate.end()) {
        if (!asset_ids.insert(projected_material.source_asset_id).second) {
          return Failure(
              Render::ValidationCode::DUPLICATE_IDENTIFIER,
              "ogre_next_demo.material.material_collision",
              "projected material ID is occupied without an input asset");
        }
        candidate.push_back(std::move(projected_material));
      } else {
        if (!material->payload || !projection->second.placeholder_payload ||
            !projection->second.material_payload ||
            material->payload->valueless_by_exception() ||
            Render::RenderAssetPayloadKind(*material->payload) !=
                Render::RenderAssetKind::MATERIAL) {
          return Failure(
              Render::ValidationCode::DUPLICATE_IDENTIFIER,
              "ogre_next_demo.material.material_collision",
              "projected material ID collides with a nonmaterial asset");
        }
        const bool exact_placeholder =
            Render::EquivalentRenderAssetPayload(
                *material->payload, *projection->second.placeholder_payload) &&
            material->material_bindings ==
                Render::GraphicsSceneAssetInput{}.material_bindings;
        const bool exact_projected =
            Render::EquivalentRenderAssetPayload(
                *material->payload, *projection->second.material_payload) &&
            material->material_bindings == projected_material.material_bindings;
        if (!exact_placeholder && !exact_projected) {
          return Failure(
              Render::ValidationCode::DUPLICATE_IDENTIFIER,
              "ogre_next_demo.material.material_collision",
              "projected material ID collides with a different material");
        }
        *material = std::move(projected_material);
      }

      Render::ValidationResult validation =
          append_dependency(texture->second.source_id, texture->second.payload,
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

OgreNextDemoMaterialSourceCounters
OgreNextDemoMaterialSource::LifetimeCounters() const noexcept {
  return lifetime_counters_;
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
