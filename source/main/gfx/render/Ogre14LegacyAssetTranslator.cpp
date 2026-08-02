/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14LegacyAssetTranslator.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime64 = 1099511628211ULL;

bool FloatBitsEqual(float lhs, float rhs) noexcept {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  static_assert(sizeof(lhs_bits) == sizeof(lhs), "binary32 is required");
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

void HashByte(std::uint64_t &hash, std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime64;
}

void HashU64(std::uint64_t &hash, std::uint64_t value) noexcept {
  for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
    HashByte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

void HashString(std::uint64_t &hash, const std::string &value) noexcept {
  HashU64(hash, static_cast<std::uint64_t>(value.size()));
  for (const unsigned char byte : value) {
    HashByte(hash, byte);
  }
}

bool IsKeyValid(const Ogre14LegacyAssetKey &key) noexcept {
  return !key.exact_name.empty() &&
         key.exact_name.find('\0') == std::string::npos &&
         key.exact_resource_group.find('\0') == std::string::npos;
}

std::string DebugName(const Ogre14LegacyAssetKey &key) {
  return key.exact_resource_group.empty()
             ? key.exact_name
             : key.exact_resource_group + "/" + key.exact_name;
}

const char *KindName(RenderAssetKind kind) noexcept {
  switch (kind) {
  case RenderAssetKind::TEXTURE:
    return "texture";
  case RenderAssetKind::SAMPLER:
    return "sampler";
  case RenderAssetKind::MATERIAL:
    return "material";
  default:
    return "invalid";
  }
}

std::string StableKey(RenderAssetKind kind, const Ogre14LegacyAssetKey &key) {
  return std::string(KindName(kind)) +
         "|group=" + std::to_string(key.exact_resource_group.size()) + ":" +
         key.exact_resource_group +
         "|name=" + std::to_string(key.exact_name.size()) + ":" +
         key.exact_name;
}

Ogre14LegacyAssetKey SamplerKey(const Ogre14LegacyAssetKey &material_key) {
  Ogre14LegacyAssetKey key = material_key;
  key.exact_name += "|pass=0|unit=0|sampler";
  return key;
}

bool IsKnownPixelEncoding(Ogre14LegacyPixelEncoding encoding) noexcept {
  switch (encoding) {
  case Ogre14LegacyPixelEncoding::RGB8_BYTES:
  case Ogre14LegacyPixelEncoding::BGR8_BYTES:
  case Ogre14LegacyPixelEncoding::RGBA8_BYTES:
  case Ogre14LegacyPixelEncoding::BGRA8_BYTES:
  case Ogre14LegacyPixelEncoding::ARGB8_BYTES:
  case Ogre14LegacyPixelEncoding::ABGR8_BYTES:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_LITTLE_ENDIAN:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_BIG_ENDIAN:
    return true;
  }
  return false;
}

std::uint32_t SourceBytesPerPixel(Ogre14LegacyPixelEncoding encoding) noexcept {
  switch (encoding) {
  case Ogre14LegacyPixelEncoding::RGB8_BYTES:
  case Ogre14LegacyPixelEncoding::BGR8_BYTES:
    return 3U;
  case Ogre14LegacyPixelEncoding::RGBA8_BYTES:
  case Ogre14LegacyPixelEncoding::BGRA8_BYTES:
  case Ogre14LegacyPixelEncoding::ARGB8_BYTES:
  case Ogre14LegacyPixelEncoding::ABGR8_BYTES:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_LITTLE_ENDIAN:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_BIG_ENDIAN:
    return 4U;
  }
  return 0U;
}

void DecodePixel(const std::uint8_t *source, Ogre14LegacyPixelEncoding encoding,
                 std::uint8_t *destination) noexcept {
  switch (encoding) {
  case Ogre14LegacyPixelEncoding::RGB8_BYTES:
    destination[0U] = source[0U];
    destination[1U] = source[1U];
    destination[2U] = source[2U];
    destination[3U] = 255U;
    return;
  case Ogre14LegacyPixelEncoding::BGR8_BYTES:
    destination[0U] = source[2U];
    destination[1U] = source[1U];
    destination[2U] = source[0U];
    destination[3U] = 255U;
    return;
  case Ogre14LegacyPixelEncoding::RGBA8_BYTES:
    destination[0U] = source[0U];
    destination[1U] = source[1U];
    destination[2U] = source[2U];
    destination[3U] = source[3U];
    return;
  case Ogre14LegacyPixelEncoding::BGRA8_BYTES:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_LITTLE_ENDIAN:
    destination[0U] = source[2U];
    destination[1U] = source[1U];
    destination[2U] = source[0U];
    destination[3U] = source[3U];
    return;
  case Ogre14LegacyPixelEncoding::ARGB8_BYTES:
  case Ogre14LegacyPixelEncoding::A8R8G8B8_WORD_BIG_ENDIAN:
    destination[0U] = source[1U];
    destination[1U] = source[2U];
    destination[2U] = source[3U];
    destination[3U] = source[0U];
    return;
  case Ogre14LegacyPixelEncoding::ABGR8_BYTES:
    destination[0U] = source[3U];
    destination[1U] = source[2U];
    destination[2U] = source[1U];
    destination[3U] = source[0U];
    return;
  }
}

bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &result) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

bool IsKnownFilter(Ogre14LegacyFilter filter) noexcept {
  switch (filter) {
  case Ogre14LegacyFilter::NONE:
  case Ogre14LegacyFilter::POINT:
  case Ogre14LegacyFilter::LINEAR:
  case Ogre14LegacyFilter::ANISOTROPIC:
    return true;
  }
  return false;
}

bool IsKnownAddress(Ogre14LegacyAddressMode address) noexcept {
  switch (address) {
  case Ogre14LegacyAddressMode::WRAP:
  case Ogre14LegacyAddressMode::MIRROR:
  case Ogre14LegacyAddressMode::CLAMP:
  case Ogre14LegacyAddressMode::BORDER:
    return true;
  }
  return false;
}

bool IsKnownCompare(Ogre14LegacyCompareOperation operation) noexcept {
  switch (operation) {
  case Ogre14LegacyCompareOperation::ALWAYS_FAIL:
  case Ogre14LegacyCompareOperation::ALWAYS_PASS:
  case Ogre14LegacyCompareOperation::LESS:
  case Ogre14LegacyCompareOperation::LESS_EQUAL:
  case Ogre14LegacyCompareOperation::EQUAL:
  case Ogre14LegacyCompareOperation::NOT_EQUAL:
  case Ogre14LegacyCompareOperation::GREATER_EQUAL:
  case Ogre14LegacyCompareOperation::GREATER:
    return true;
  }
  return false;
}

bool IsKnownBlendFactor(Ogre14LegacyBlendFactor factor) noexcept {
  switch (factor) {
  case Ogre14LegacyBlendFactor::ONE:
  case Ogre14LegacyBlendFactor::ZERO:
  case Ogre14LegacyBlendFactor::DESTINATION_COLOR:
  case Ogre14LegacyBlendFactor::SOURCE_COLOR:
  case Ogre14LegacyBlendFactor::ONE_MINUS_DESTINATION_COLOR:
  case Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_COLOR:
  case Ogre14LegacyBlendFactor::DESTINATION_ALPHA:
  case Ogre14LegacyBlendFactor::SOURCE_ALPHA:
  case Ogre14LegacyBlendFactor::ONE_MINUS_DESTINATION_ALPHA:
  case Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA:
    return true;
  }
  return false;
}

bool IsKnownBlendOperation(Ogre14LegacyBlendOperation operation) noexcept {
  switch (operation) {
  case Ogre14LegacyBlendOperation::ADD:
  case Ogre14LegacyBlendOperation::SUBTRACT:
  case Ogre14LegacyBlendOperation::REVERSE_SUBTRACT:
  case Ogre14LegacyBlendOperation::MINIMUM:
  case Ogre14LegacyBlendOperation::MAXIMUM:
    return true;
  }
  return false;
}

bool IsKnownCull(Ogre14LegacyCullMode cull) noexcept {
  switch (cull) {
  case Ogre14LegacyCullMode::NONE:
  case Ogre14LegacyCullMode::CLOCKWISE:
  case Ogre14LegacyCullMode::ANTICLOCKWISE:
    return true;
  }
  return false;
}

bool IsKnownManualCull(Ogre14LegacyManualCullMode cull) noexcept {
  switch (cull) {
  case Ogre14LegacyManualCullMode::NONE:
  case Ogre14LegacyManualCullMode::BACK:
  case Ogre14LegacyManualCullMode::FRONT:
    return true;
  }
  return false;
}

bool IsKnownSemantic(Ogre14LegacyBaseColorSemantic semantic) noexcept {
  switch (semantic) {
  case Ogre14LegacyBaseColorSemantic::UNLIT:
  case Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR:
    return true;
  }
  return false;
}

bool IsReplaceBlend(const Ogre14LegacyPipelineStateInput &state) noexcept {
  return state.source_color == Ogre14LegacyBlendFactor::ONE &&
         state.destination_color == Ogre14LegacyBlendFactor::ZERO &&
         state.source_alpha == Ogre14LegacyBlendFactor::ONE &&
         state.destination_alpha == Ogre14LegacyBlendFactor::ZERO;
}

bool IsStraightSourceOver(
    const Ogre14LegacyPipelineStateInput &state) noexcept {
  return state.source_color == Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
         state.destination_color ==
             Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA &&
         state.source_alpha == Ogre14LegacyBlendFactor::ONE &&
         state.destination_alpha ==
             Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
}

ValidationResult ValidateSamplerInput(const Ogre14LegacySamplerInput &sampler) {
  if (sampler.source_revision == 0U) {
    return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                     "material.sampler.source_revision",
                                     "sampler source revision must be nonzero");
  }
  if (!IsKnownFilter(sampler.minification) ||
      !IsKnownFilter(sampler.magnification) || !IsKnownFilter(sampler.mip) ||
      !IsKnownAddress(sampler.address_u) ||
      !IsKnownAddress(sampler.address_v) ||
      !IsKnownAddress(sampler.address_w) ||
      !IsKnownCompare(sampler.compare_operation)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "material.sampler",
                                     "sampler has an unknown OGRE 14 enum");
  }
  if (sampler.minification == Ogre14LegacyFilter::NONE ||
      sampler.magnification == Ogre14LegacyFilter::NONE ||
      sampler.mip == Ogre14LegacyFilter::ANISOTROPIC) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.sampler.filter",
        "v1 cannot represent this legacy filter combination exactly");
  }
  if (!IsFinite(sampler.mip_lod_bias) || !IsFinite(sampler.minimum_lod) ||
      !IsFinite(sampler.maximum_lod) || !IsFinite(sampler.border_color)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "material.sampler",
                                     "sampler numeric state must be finite");
  }
  if (sampler.minimum_lod != 0.0F || sampler.maximum_lod < 0.0F ||
      sampler.maximum_lod > kMaximumSamplerLod ||
      sampler.mip_lod_bias < -kMaximumSamplerLodBias ||
      sampler.mip_lod_bias > kMaximumSamplerLodBias ||
      !IsNormalizedColor(sampler.border_color)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "material.sampler",
        "sampler LOD, bias, or border state is outside the portable range");
  }
  const bool anisotropic =
      sampler.minification == Ogre14LegacyFilter::ANISOTROPIC ||
      sampler.magnification == Ogre14LegacyFilter::ANISOTROPIC;
  if ((anisotropic && (sampler.maximum_anisotropy <= 1U ||
                       sampler.maximum_anisotropy > 16U)) ||
      (!anisotropic && sampler.maximum_anisotropy != 1U)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.sampler.maximum_anisotropy",
        "anisotropy must be one when disabled or in [2, 16] when enabled");
  }
  if (sampler.compare_enabled ||
      sampler.compare_operation != Ogre14LegacyCompareOperation::ALWAYS_PASS) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.sampler.compare",
        "a base-color texture cannot use legacy comparison sampling");
  }
  return ValidationResult::Success();
}

SamplerFilter ToSamplerFilter(Ogre14LegacyFilter filter) noexcept {
  return filter == Ogre14LegacyFilter::POINT ? SamplerFilter::NEAREST
                                             : SamplerFilter::LINEAR;
}

SamplerAddressMode ToAddressMode(Ogre14LegacyAddressMode address) noexcept {
  switch (address) {
  case Ogre14LegacyAddressMode::WRAP:
    return SamplerAddressMode::REPEAT;
  case Ogre14LegacyAddressMode::MIRROR:
    return SamplerAddressMode::MIRRORED_REPEAT;
  case Ogre14LegacyAddressMode::CLAMP:
    return SamplerAddressMode::CLAMP_TO_EDGE;
  case Ogre14LegacyAddressMode::BORDER:
    return SamplerAddressMode::CLAMP_TO_BORDER;
  }
  return SamplerAddressMode::REPEAT;
}

ValidationResult
BuildSamplerDescriptor(const Ogre14LegacyAssetKey &material_key,
                       const Ogre14LegacySamplerInput &input,
                       SamplerResourceDescriptor &descriptor) {
  ValidationResult validation = ValidateSamplerInput(input);
  if (!validation) {
    return validation;
  }
  SamplerResourceDescriptor candidate;
  candidate.debug_name = DebugName(material_key) + "/pass0/unit0";
  candidate.minification_filter = ToSamplerFilter(input.minification);
  candidate.magnification_filter = ToSamplerFilter(input.magnification);
  candidate.mip_filter = input.mip == Ogre14LegacyFilter::POINT ||
                                 input.mip == Ogre14LegacyFilter::NONE
                             ? SamplerFilter::NEAREST
                             : SamplerFilter::LINEAR;
  candidate.address_u = ToAddressMode(input.address_u);
  candidate.address_v = ToAddressMode(input.address_v);
  candidate.address_w = ToAddressMode(input.address_w);
  candidate.mip_lod_bias = input.mip_lod_bias;
  candidate.minimum_lod = input.minimum_lod;
  candidate.maximum_lod = input.maximum_lod;
  candidate.anisotropy_enabled =
      input.minification == Ogre14LegacyFilter::ANISOTROPIC ||
      input.magnification == Ogre14LegacyFilter::ANISOTROPIC;
  candidate.maximum_anisotropy = static_cast<float>(input.maximum_anisotropy);
  candidate.compare_enabled = false;
  candidate.compare_operation = SamplerCompareOperation::ALWAYS;
  candidate.border_color = input.border_color;
  validation = ValidateSamplerResourceDescriptor(candidate);
  if (!validation) {
    return validation;
  }
  descriptor = std::move(candidate);
  return ValidationResult::Success();
}

bool EquivalentPipeline(const Ogre14LegacyPipelineStateInput &lhs,
                        const Ogre14LegacyPipelineStateInput &rhs) noexcept {
  return lhs.source_color == rhs.source_color &&
         lhs.destination_color == rhs.destination_color &&
         lhs.source_alpha == rhs.source_alpha &&
         lhs.destination_alpha == rhs.destination_alpha &&
         lhs.color_operation == rhs.color_operation &&
         lhs.alpha_operation == rhs.alpha_operation &&
         lhs.color_write_mask == rhs.color_write_mask &&
         lhs.depth_check_enabled == rhs.depth_check_enabled &&
         lhs.depth_write_enabled == rhs.depth_write_enabled &&
         lhs.depth_compare == rhs.depth_compare &&
         FloatBitsEqual(lhs.constant_depth_bias, rhs.constant_depth_bias) &&
         FloatBitsEqual(lhs.slope_scale_depth_bias,
                        rhs.slope_scale_depth_bias) &&
         FloatBitsEqual(lhs.iteration_depth_bias, rhs.iteration_depth_bias) &&
         lhs.cull == rhs.cull && lhs.manual_cull == rhs.manual_cull &&
         lhs.alpha_reject == rhs.alpha_reject &&
         lhs.alpha_reject_value == rhs.alpha_reject_value &&
         lhs.alpha_to_coverage == rhs.alpha_to_coverage &&
         lhs.solid_fill == rhs.solid_fill &&
         lhs.pass_iteration_count == rhs.pass_iteration_count;
}

bool EquivalentAudit(const Ogre14LegacyMaterialPipelineAudit &lhs,
                     const Ogre14LegacyMaterialPipelineAudit &rhs) noexcept {
  return lhs.version == rhs.version &&
         EquivalentPipeline(lhs.pipeline, rhs.pipeline) &&
         lhs.base_color_semantic == rhs.base_color_semantic &&
         lhs.requires_reverse_winding == rhs.requires_reverse_winding &&
         lhs.texture_source_asset_id == rhs.texture_source_asset_id &&
         lhs.sampler_source_asset_id == rhs.sampler_source_asset_id;
}

bool EquivalentAsset(const Ogre14LegacyTranslatedAsset &lhs,
                     const Ogre14LegacyTranslatedAsset &rhs) noexcept {
  if (lhs.kind != rhs.kind || lhs.source_asset_id != rhs.source_asset_id ||
      lhs.stable_key != rhs.stable_key || lhs.payload == nullptr ||
      rhs.payload == nullptr ||
      !EquivalentRenderAssetPayload(*lhs.payload, *rhs.payload)) {
    return false;
  }
  if ((lhs.material_audit == nullptr) != (rhs.material_audit == nullptr)) {
    return false;
  }
  return lhs.material_audit == nullptr ||
         EquivalentAudit(*lhs.material_audit, *rhs.material_audit);
}

std::uint32_t DependencyRank(RenderAssetKind kind) noexcept {
  switch (kind) {
  case RenderAssetKind::TEXTURE:
    return 0U;
  case RenderAssetKind::SAMPLER:
    return 1U;
  case RenderAssetKind::MATERIAL:
    return 2U;
  default:
    return 3U;
  }
}

bool AssetOrder(const Ogre14LegacyTranslatedAsset &lhs,
                const Ogre14LegacyTranslatedAsset &rhs) noexcept {
  const std::uint32_t lhs_rank = DependencyRank(lhs.kind);
  const std::uint32_t rhs_rank = DependencyRank(rhs.kind);
  return lhs_rank != rhs_rank ? lhs_rank < rhs_rank
                              : lhs.source_asset_id < rhs.source_asset_id;
}

bool MutationOrder(const Ogre14LegacyAssetMutation &lhs,
                   const Ogre14LegacyAssetMutation &rhs) noexcept {
  if (lhs.type != rhs.type) {
    return lhs.type == Ogre14LegacyAssetMutationType::DESTROY;
  }
  const std::uint32_t lhs_rank = DependencyRank(lhs.kind);
  const std::uint32_t rhs_rank = DependencyRank(rhs.kind);
  if (lhs_rank != rhs_rank) {
    return lhs.type == Ogre14LegacyAssetMutationType::DESTROY
               ? lhs_rank > rhs_rank
               : lhs_rank < rhs_rank;
  }
  return lhs.source_asset_id < rhs.source_asset_id;
}

Ogre14LegacyAssetMutation
UpsertMutation(const Ogre14LegacyTranslatedAsset &asset) {
  Ogre14LegacyAssetMutation mutation;
  mutation.type = Ogre14LegacyAssetMutationType::UPSERT;
  mutation.kind = asset.kind;
  mutation.source_asset_id = asset.source_asset_id;
  mutation.translated_revision = asset.translated_revision;
  mutation.stable_key = asset.stable_key;
  mutation.payload = asset.payload;
  mutation.material_audit = asset.material_audit;
  return mutation;
}

} // namespace

struct Ogre14LegacyAssetTranslator::State {
  struct Record {
    Ogre14LegacyTranslatedAsset asset;
    bool live = false;
  };

  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  std::map<std::string, Record, std::less<>> records;
  std::map<std::uint64_t, std::string> stable_keys_by_id;
};

ValidationResult
ValidateOgre14LegacyTextureInput(const Ogre14LegacyTextureInput &input) {
  if (input.version != kOgre14LegacyTextureInputVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "texture.version",
        "unsupported OGRE 14 texture input version");
  }
  if (!IsKeyValid(input.key)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "texture.key",
        "texture requires an exact nonempty NUL-free name");
  }
  if (DebugName(input.key).size() > kMaximumResourceDebugNameBytes) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "texture.key",
        "combined texture debug identity exceeds 255 bytes");
  }
  if (input.source_revision == 0U) {
    return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                     "texture.source_revision",
                                     "texture source revision must be nonzero");
  }
  if (input.type != Ogre14LegacyTextureType::TEXTURE_2D) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.type",
        "v1 accepts only ordinary 2D legacy textures");
  }
  if (!IsKnownPixelEncoding(input.pixel_encoding)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "texture.pixel_encoding",
                                     "unknown legacy pixel encoding");
  }
  if (input.color_role != Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB &&
      input.color_role != Ogre14LegacyTextureColorRole::LINEAR_DATA) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "texture.color_role",
                                     "unknown explicit texture color role");
  }
  const bool expects_gamma =
      input.color_role == Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  if (input.hardware_gamma_enabled != expects_gamma) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.color_transform",
        "hardware gamma state disagrees with the explicit color role");
  }
  if (input.compressed || input.render_target || input.generated ||
      input.procedural) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "texture.source_kind",
        "compressed, render-target, generated, or procedural textures are not "
        "representable in v1");
  }
  if (input.width == 0U || input.height == 0U ||
      input.width > kMaximumTextureResourceDimension ||
      input.height > kMaximumTextureResourceDimension) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "texture.dimensions",
        "texture dimensions are outside the portable range");
  }
  if (input.mip_levels.empty()) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     "texture.mip_levels",
                                     "texture requires a nonempty mip prefix");
  }
  std::uint32_t maximum_mip_count = 1U;
  for (std::uint32_t width = input.width, height = input.height;
       width > 1U || height > 1U; ++maximum_mip_count) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
  }
  if (input.mip_levels.size() > maximum_mip_count) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "texture.mip_levels",
        "texture supplies more mip levels than its base extent permits");
  }
  const std::uint32_t bytes_per_pixel =
      SourceBytesPerPixel(input.pixel_encoding);
  std::uint32_t expected_width = input.width;
  std::uint32_t expected_height = input.height;
  for (std::size_t index = 0U; index < input.mip_levels.size(); ++index) {
    const Ogre14LegacyTextureMipInput &mip = input.mip_levels[index];
    if (mip.width != expected_width || mip.height != expected_height) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "texture.mip_levels",
          "mip dimensions must be the exact contiguous halving chain", index);
    }
    std::uint64_t required_row = 0U;
    std::uint64_t required_slice = 0U;
    if (!CheckedMultiply(mip.width, bytes_per_pixel, required_row) ||
        mip.row_pitch_bytes < required_row ||
        !CheckedMultiply(mip.row_pitch_bytes, mip.height, required_slice) ||
        mip.slice_pitch_bytes < required_slice ||
        mip.slice_pitch_bytes != mip.bytes.size()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "texture.mip_layout",
          "mip row/slice pitches and payload size are inconsistent", index);
    }
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }
  return ValidationResult::Success();
}

ValidationResult
DecodeOgre14LegacyTexture(const Ogre14LegacyTextureInput &input,
                          TextureResourceDescriptor &descriptor) {
  ValidationResult validation = ValidateOgre14LegacyTextureInput(input);
  if (!validation) {
    return validation;
  }

  try {
    TextureResourceDescriptor candidate;
    candidate.debug_name = DebugName(input.key);
    candidate.type = TextureResourceType::TEXTURE_2D;
    candidate.format = TextureResourceFormat::RGBA8_UNORM;
    candidate.color_space =
        input.color_role == Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB
            ? TextureColorSpace::SRGB
            : TextureColorSpace::LINEAR;
    candidate.width = input.width;
    candidate.height = input.height;
    candidate.array_layers = 1U;
    candidate.mip_levels.reserve(input.mip_levels.size());
    const std::uint32_t source_stride =
        SourceBytesPerPixel(input.pixel_encoding);
    for (const Ogre14LegacyTextureMipInput &source_mip : input.mip_levels) {
      TextureMipLevelDescriptor mip;
      mip.width = source_mip.width;
      mip.height = source_mip.height;
      mip.row_pitch_bytes = static_cast<std::uint64_t>(mip.width) * 4U;
      mip.layer_pitch_bytes = mip.row_pitch_bytes * mip.height;
      mip.bytes.resize(static_cast<std::size_t>(mip.layer_pitch_bytes));
      for (std::uint32_t row = 0U; row < mip.height; ++row) {
        const std::uint8_t *source_row =
            source_mip.bytes.data() +
            static_cast<std::size_t>(row * source_mip.row_pitch_bytes);
        std::uint8_t *destination_row =
            mip.bytes.data() +
            static_cast<std::size_t>(row * mip.row_pitch_bytes);
        for (std::uint32_t column = 0U; column < mip.width; ++column) {
          DecodePixel(source_row + column * source_stride, input.pixel_encoding,
                      destination_row + column * 4U);
        }
      }
      candidate.mip_levels.push_back(std::move(mip));
    }
    validation = ValidateTextureResourceDescriptor(candidate);
    if (!validation) {
      return validation;
    }
    descriptor = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "texture.allocation",
        "allocation failed while decoding the canonical texture");
  }
}

ValidationResult
ValidateOgre14LegacyMaterialInput(const Ogre14LegacyMaterialInput &input) {
  if (input.version != kOgre14LegacyMaterialInputVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "material.version",
        "unsupported OGRE 14 material input version");
  }
  if (!IsKeyValid(input.key)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "material.key",
        "material requires an exact nonempty NUL-free name");
  }
  if (DebugName(input.key).size() > kMaximumMaterialDebugNameBytes) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "material.key",
        "combined material debug identity exceeds 255 bytes");
  }
  if (input.source_revision == 0U) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "material.source_revision",
        "material source revision must be nonzero");
  }
  if (input.technique_count != 1U || input.pass_count != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.pass_structure",
        "v1 requires exactly one technique containing exactly one pass");
  }
  if (input.generated_rtss_program || input.has_vertex_program ||
      input.has_fragment_program || input.has_geometry_program ||
      input.has_tessellation_program || input.has_compute_program) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE,
                                     "material.programs",
                                     "generated RTSS and authored GPU programs "
                                     "are not legacy base-color semantics");
  }
  if (!IsKnownSemantic(input.base_color_semantic)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "material.base_color_semantic",
                                     "unknown explicit base-color semantic");
  }
  if ((input.base_color_semantic == Ogre14LegacyBaseColorSemantic::UNLIT) !=
      !input.lighting_enabled) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.lighting",
        "declared portable semantic disagrees with legacy lighting state");
  }
  if (!IsNormalizedColor(input.diffuse_linear) ||
      !IsFinite(input.ambient_linear) || !IsFinite(input.specular_linear) ||
      !IsFinite(input.emissive_linear) || !IsFinite(input.shininess)) {
    return ValidationResult::Failure(
        IsFinite(input.diffuse_linear) && IsFinite(input.ambient_linear) &&
                IsFinite(input.specular_linear) &&
                IsFinite(input.emissive_linear) && IsFinite(input.shininess)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "material.fixed_function_lobes",
        "legacy material factors must be finite and diffuse must be "
        "normalized");
  }
  if (input.ambient_linear != Float3{} || input.specular_linear != Float3{} ||
      input.emissive_linear != Float3{} || input.shininess != 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.fixed_function_lobes",
        "v1 rejects ambient, specular, emissive, and shininess instead of "
        "guessing PBR roles");
  }
  if (input.texture_units.size() > 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.texture_units",
        "v1 accepts at most one base-color texture unit");
  }

  const Ogre14LegacyPipelineStateInput &state = input.pipeline;
  if (!IsKnownBlendFactor(state.source_color) ||
      !IsKnownBlendFactor(state.destination_color) ||
      !IsKnownBlendFactor(state.source_alpha) ||
      !IsKnownBlendFactor(state.destination_alpha) ||
      !IsKnownBlendOperation(state.color_operation) ||
      !IsKnownBlendOperation(state.alpha_operation) ||
      !IsKnownCompare(state.depth_compare) ||
      !IsKnownCompare(state.alpha_reject) || !IsKnownCull(state.cull) ||
      !IsKnownManualCull(state.manual_cull)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "material.pipeline",
        "pipeline contains an unknown OGRE 14 enum");
  }
  if (!IsFinite(state.constant_depth_bias) ||
      !IsFinite(state.slope_scale_depth_bias) ||
      !IsFinite(state.iteration_depth_bias)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "material.pipeline.depth_bias",
                                     "depth bias must be finite");
  }
  const bool replace = IsReplaceBlend(state);
  const bool source_over = IsStraightSourceOver(state);
  if ((!replace && !source_over) ||
      state.color_operation != Ogre14LegacyBlendOperation::ADD ||
      state.alpha_operation != Ogre14LegacyBlendOperation::ADD) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.pipeline.blend",
        "v1 accepts only replace or exact straight-alpha source-over blending");
  }
  if (state.color_write_mask != 0x0FU || !state.depth_check_enabled ||
      state.depth_compare != Ogre14LegacyCompareOperation::LESS_EQUAL ||
      state.constant_depth_bias != 0.0F ||
      state.slope_scale_depth_bias != 0.0F ||
      state.iteration_depth_bias != 0.0F ||
      state.manual_cull != Ogre14LegacyManualCullMode::BACK ||
      state.alpha_to_coverage || !state.solid_fill ||
      state.pass_iteration_count != 1U ||
      (replace && !state.depth_write_enabled) ||
      (source_over && state.depth_write_enabled)) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.pipeline.depth_raster",
        "color mask, depth, cull, fill, coverage, or iteration state is "
        "outside the exact v1 subset");
  }
  const bool alpha_pass =
      state.alpha_reject == Ogre14LegacyCompareOperation::ALWAYS_PASS;
  const bool alpha_mask =
      state.alpha_reject == Ogre14LegacyCompareOperation::GREATER_EQUAL;
  if ((!alpha_pass && !alpha_mask) || (source_over && alpha_mask)) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.pipeline.alpha_reject",
        "v1 accepts always-pass or >= alpha rejection and never combines "
        "rejection with blending");
  }
  if (input.texture_units.empty()) {
    return ValidationResult::Success();
  }
  const Ogre14LegacyTextureUnitInput &unit = input.texture_units.front();
  if (!IsKeyValid(unit.texture_key)) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "material.texture.key",
                                     "base-color texture identity is invalid");
  }
  if (!unit.named_content || !unit.texture_2d || unit.frame_count != 1U ||
      unit.has_animated_or_procedural_effect || unit.projective ||
      unit.environment_mapping || unit.compositor || unit.render_target ||
      !unit.canonical_color_modulate || !unit.canonical_alpha_modulate ||
      !unit.identity_texture_transform) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.texture_unit",
        "animated, procedural, projective, cube, compositor, render-target, "
        "transformed, or non-modulate texture units are not representable in "
        "v1");
  }
  if (unit.texture_coordinate_set > 1U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.texture_unit.texture_coordinate_set",
        "portable materials expose only UV set zero or one");
  }
  return ValidateSamplerInput(unit.sampler);
}

ValidationResult
DeriveOgre14LegacySourceAssetId(RenderAssetKind kind,
                                const Ogre14LegacyAssetKey &key,
                                std::uint64_t &source_asset_id) {
  if ((kind != RenderAssetKind::TEXTURE && kind != RenderAssetKind::SAMPLER &&
       kind != RenderAssetKind::MATERIAL) ||
      !IsKeyValid(key)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "asset.key",
        "stable legacy asset identity requires a supported kind and exact key");
  }
  std::uint64_t hash = kFnvOffset64;
  static constexpr char kDomain[] = "RoR.OGRE14.LegacyAsset.v1";
  for (std::size_t index = 0U; index + 1U < sizeof(kDomain); ++index) {
    HashByte(hash, static_cast<std::uint8_t>(kDomain[index]));
  }
  HashByte(hash, static_cast<std::uint8_t>(kind));
  HashString(hash, key.exact_resource_group);
  HashString(hash, key.exact_name);
  if (hash == 0U) {
    hash = 1U;
  }
  source_asset_id = hash;
  return ValidationResult::Success();
}

Ogre14LegacyAssetTranslator::Ogre14LegacyAssetTranslator(
    IOgre14LegacyAssetTranslatorFaultInjector *fault_injector)
    : state_(std::make_unique<State>()), fault_injector_(fault_injector) {}

Ogre14LegacyAssetTranslator::~Ogre14LegacyAssetTranslator() = default;
Ogre14LegacyAssetTranslator::Ogre14LegacyAssetTranslator(
    Ogre14LegacyAssetTranslator &&) noexcept = default;
Ogre14LegacyAssetTranslator &Ogre14LegacyAssetTranslator::operator=(
    Ogre14LegacyAssetTranslator &&) noexcept = default;

std::uint64_t Ogre14LegacyAssetTranslator::source_sequence() const noexcept {
  return state_ != nullptr ? state_->source_sequence : 0U;
}

std::uint64_t Ogre14LegacyAssetTranslator::catalog_sequence() const noexcept {
  return state_ != nullptr ? state_->catalog_sequence : 0U;
}

ValidationResult
Ogre14LegacyAssetTranslator::Translate(const Ogre14LegacyAssetFrameInput &input,
                                       Ogre14LegacyTranslatedFrame &output) {
  if (state_ == nullptr) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     "translator.state",
                                     "moved-from translator has no state");
  }
  if (input.version != kOgre14LegacyAssetTranslatorVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "frame.version",
        "unsupported OGRE 14 legacy frame version");
  }
  const std::uint64_t expected_source_sequence =
      state_->source_sequence == 0U ? 1U : state_->source_sequence + 1U;
  if (state_->source_sequence == (std::numeric_limits<std::uint64_t>::max)() ||
      input.source_sequence != expected_source_sequence) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "frame.source_sequence",
        "source sequence must start at one and advance exactly once");
  }

  try {
    State candidate = *state_;
    std::map<std::string, Ogre14LegacyTranslatedAsset, std::less<>> proposed;
    std::map<std::string, const Ogre14LegacyTextureInput *, std::less<>>
        textures_by_key;

    for (std::size_t index = 0U; index < input.textures.size(); ++index) {
      const Ogre14LegacyTextureInput &texture = input.textures[index];
      ValidationResult validation = ValidateOgre14LegacyTextureInput(texture);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      const std::string key = StableKey(RenderAssetKind::TEXTURE, texture.key);
      if (!textures_by_key.emplace(key, &texture).second) {
        return ValidationResult::Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "textures.key",
            "texture key is duplicated in one authoritative frame", index);
      }
      TextureResourceDescriptor descriptor;
      validation = DecodeOgre14LegacyTexture(texture, descriptor);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      std::uint64_t source_id = 0U;
      validation = DeriveOgre14LegacySourceAssetId(RenderAssetKind::TEXTURE,
                                                   texture.key, source_id);
      if (!validation) {
        return validation;
      }
      Ogre14LegacyTranslatedAsset asset;
      asset.kind = RenderAssetKind::TEXTURE;
      asset.source_asset_id = source_id;
      asset.source_revision = texture.source_revision;
      asset.stable_key = key;
      asset.payload =
          std::make_shared<const RenderAssetPayload>(std::move(descriptor));
      proposed.emplace(key, std::move(asset));
    }

    std::set<std::string, std::less<>> material_keys;
    for (std::size_t index = 0U; index < input.materials.size(); ++index) {
      const Ogre14LegacyMaterialInput &material_input = input.materials[index];
      ValidationResult validation =
          ValidateOgre14LegacyMaterialInput(material_input);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      const std::string material_stable_key =
          StableKey(RenderAssetKind::MATERIAL, material_input.key);
      if (!material_keys.insert(material_stable_key).second) {
        return ValidationResult::Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "materials.key",
            "material key is duplicated in one authoritative frame", index);
      }

      std::uint64_t texture_id = 0U;
      std::uint64_t sampler_id = 0U;
      if (!material_input.texture_units.empty()) {
        const Ogre14LegacyTextureUnitInput &unit =
            material_input.texture_units.front();
        const std::string texture_key =
            StableKey(RenderAssetKind::TEXTURE, unit.texture_key);
        const auto texture = textures_by_key.find(texture_key);
        if (texture == textures_by_key.end()) {
          return ValidationResult::Failure(
              ValidationCode::MISSING_REFERENCE, "material.texture_key",
              "material base-color texture is absent from the same frame",
              index);
        }
        if (texture->second->color_role !=
            Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB) {
          return ValidationResult::Failure(
              ValidationCode::UNSUPPORTED_FEATURE,
              "material.texture_color_role",
              "v1 base-color binding requires an explicit sRGB role", index);
        }
        const float exact_maximum_lod =
            unit.sampler.mip == Ogre14LegacyFilter::NONE
                ? 0.0F
                : static_cast<float>(texture->second->mip_levels.size() - 1U);
        if (!FloatBitsEqual(unit.sampler.maximum_lod, exact_maximum_lod)) {
          return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                           "material.sampler.maximum_lod",
                                           "sampler maximum LOD must equal the "
                                           "exact effective texture mip bound",
                                           index);
        }
        validation = DeriveOgre14LegacySourceAssetId(
            RenderAssetKind::TEXTURE, unit.texture_key, texture_id);
        if (!validation) {
          return validation;
        }
        const Ogre14LegacyAssetKey sampler_key = SamplerKey(material_input.key);
        const std::string sampler_stable_key =
            StableKey(RenderAssetKind::SAMPLER, sampler_key);
        SamplerResourceDescriptor sampler;
        validation =
            BuildSamplerDescriptor(material_input.key, unit.sampler, sampler);
        if (!validation) {
          validation.element_index = index;
          return validation;
        }
        validation = DeriveOgre14LegacySourceAssetId(RenderAssetKind::SAMPLER,
                                                     sampler_key, sampler_id);
        if (!validation) {
          return validation;
        }
        Ogre14LegacyTranslatedAsset sampler_asset;
        sampler_asset.kind = RenderAssetKind::SAMPLER;
        sampler_asset.source_asset_id = sampler_id;
        sampler_asset.source_revision = unit.sampler.source_revision;
        sampler_asset.stable_key = sampler_stable_key;
        sampler_asset.payload =
            std::make_shared<const RenderAssetPayload>(std::move(sampler));
        if (!proposed.emplace(sampler_stable_key, std::move(sampler_asset))
                 .second) {
          return ValidationResult::Failure(
              ValidationCode::DUPLICATE_IDENTIFIER, "materials.sampler_key",
              "derived material sampler identity is duplicated", index);
        }
      }

      MaterialDescriptor material;
      material.debug_name = DebugName(material_input.key);
      material.model = material_input.base_color_semantic ==
                               Ogre14LegacyBaseColorSemantic::UNLIT
                           ? MaterialModel::UNLIT
                           : MaterialModel::PBR_METALLIC_ROUGHNESS;
      material.alpha_mode = IsStraightSourceOver(material_input.pipeline)
                                ? MaterialAlphaMode::BLEND
                            : material_input.pipeline.alpha_reject ==
                                    Ogre14LegacyCompareOperation::GREATER_EQUAL
                                ? MaterialAlphaMode::MASK
                                : MaterialAlphaMode::OPAQUE;
      material.double_sided =
          material_input.pipeline.cull == Ogre14LegacyCullMode::NONE;
      material.base_color_factor = material_input.diffuse_linear;
      material.metallic_factor = 0.0F;
      material.roughness_factor = 1.0F;
      material.emissive_factor = {};
      material.alpha_cutoff =
          static_cast<float>(material_input.pipeline.alpha_reject_value) /
          255.0F;
      if (!material_input.texture_units.empty()) {
        material.base_color_texture.texture_coordinate_set =
            material_input.texture_units.front().texture_coordinate_set;
      }
      validation = ValidateMaterialDescriptor(material);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }

      auto audit = std::make_shared<Ogre14LegacyMaterialPipelineAudit>();
      audit->pipeline = material_input.pipeline;
      audit->base_color_semantic = material_input.base_color_semantic;
      audit->requires_reverse_winding =
          material_input.pipeline.cull == Ogre14LegacyCullMode::ANTICLOCKWISE;
      audit->texture_source_asset_id = texture_id;
      audit->sampler_source_asset_id = sampler_id;

      std::uint64_t material_id = 0U;
      validation = DeriveOgre14LegacySourceAssetId(
          RenderAssetKind::MATERIAL, material_input.key, material_id);
      if (!validation) {
        return validation;
      }
      Ogre14LegacyTranslatedAsset material_asset;
      material_asset.kind = RenderAssetKind::MATERIAL;
      material_asset.source_asset_id = material_id;
      material_asset.source_revision = material_input.source_revision;
      material_asset.stable_key = material_stable_key;
      material_asset.payload =
          std::make_shared<const RenderAssetPayload>(std::move(material));
      material_asset.material_audit = std::move(audit);
      proposed.emplace(material_stable_key, std::move(material_asset));
    }

    for (const auto &entry : proposed) {
      const Ogre14LegacyTranslatedAsset &asset = entry.second;
      const auto collision =
          candidate.stable_keys_by_id.find(asset.source_asset_id);
      if (collision != candidate.stable_keys_by_id.end() &&
          collision->second != entry.first) {
        return ValidationResult::Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "assets.source_asset_id",
            "distinct exact legacy keys collided on one stable source ID");
      }
      candidate.stable_keys_by_id[asset.source_asset_id] = entry.first;
    }

    std::vector<Ogre14LegacyAssetMutation> mutations;
    for (auto &entry : proposed) {
      Ogre14LegacyTranslatedAsset &asset = entry.second;
      const auto previous = candidate.records.find(entry.first);
      if (previous == candidate.records.end()) {
        asset.translated_revision = 1U;
        State::Record record;
        record.asset = asset;
        record.live = true;
        candidate.records.emplace(entry.first, std::move(record));
        mutations.push_back(UpsertMutation(asset));
        continue;
      }
      if (!previous->second.live) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "assets.stable_key",
            "a permanently tombstoned legacy identity may never return");
      }
      const Ogre14LegacyTranslatedAsset &prior = previous->second.asset;
      if (asset.kind != prior.kind ||
          asset.source_asset_id != prior.source_asset_id) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "assets.identity",
            "a stable legacy key changed kind or source ID");
      }
      if (asset.source_revision < prior.source_revision) {
        return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                         "assets.source_revision",
                                         "legacy source revision regressed");
      }
      const bool equivalent = EquivalentAsset(asset, prior);
      if (!equivalent && asset.source_revision == prior.source_revision) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "assets.source_revision",
            "semantic bytes changed without a source revision advance");
      }
      if (equivalent) {
        asset.translated_revision = prior.translated_revision;
        asset.payload = prior.payload;
        asset.material_audit = prior.material_audit;
      } else {
        if (prior.translated_revision ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                           "assets.translated_revision",
                                           "translated revision exhausted");
        }
        asset.translated_revision = prior.translated_revision + 1U;
        mutations.push_back(UpsertMutation(asset));
      }
      previous->second.asset = asset;
    }

    for (auto &entry : candidate.records) {
      if (!entry.second.live || proposed.find(entry.first) != proposed.end()) {
        continue;
      }
      if (entry.second.asset.translated_revision ==
          (std::numeric_limits<std::uint64_t>::max)()) {
        return ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                         "assets.translated_revision",
                                         "tombstone revision exhausted");
      }
      ++entry.second.asset.translated_revision;
      entry.second.live = false;
      Ogre14LegacyAssetMutation mutation;
      mutation.type = Ogre14LegacyAssetMutationType::DESTROY;
      mutation.kind = entry.second.asset.kind;
      mutation.source_asset_id = entry.second.asset.source_asset_id;
      mutation.translated_revision = entry.second.asset.translated_revision;
      mutation.stable_key = entry.second.asset.stable_key;
      mutations.push_back(std::move(mutation));
    }

    std::sort(mutations.begin(), mutations.end(), MutationOrder);
    candidate.source_sequence = input.source_sequence;
    const bool initializes_catalog = state_->source_sequence == 0U;
    if (initializes_catalog || !mutations.empty()) {
      if (candidate.catalog_sequence ==
          (std::numeric_limits<std::uint64_t>::max)()) {
        return ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH,
                                         "frame.catalog_sequence",
                                         "catalog sequence exhausted");
      }
      ++candidate.catalog_sequence;
    }

    Ogre14LegacyTranslatedFrame frame;
    frame.source_sequence = input.source_sequence;
    frame.catalog_sequence = candidate.catalog_sequence;
    frame.full_snapshot = initializes_catalog;
    frame.mutations = std::move(mutations);
    for (const auto &entry : candidate.records) {
      if (entry.second.live) {
        frame.live_assets.push_back(entry.second.asset);
      }
    }
    std::sort(frame.live_assets.begin(), frame.live_assets.end(), AssetOrder);

    if (fault_injector_ != nullptr) {
      const ValidationResult injected = fault_injector_->BeforeCommit();
      if (!injected) {
        return injected;
      }
    }
    *state_ = std::move(candidate);
    output = std::move(frame);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "translator.allocation",
        "allocation failed before the translation transaction committed");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "translator.exception",
        "unexpected capture exception before transaction commit");
  }
}

ValidationResult Ogre14LegacyAssetTranslator::BuildFullSnapshot(
    Ogre14LegacyTranslatedFrame &output) const {
  if (state_ == nullptr || state_->source_sequence == 0U ||
      state_->catalog_sequence == 0U) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "frame.catalog_sequence",
        "an uninitialized translator cannot produce a full snapshot");
  }
  try {
    Ogre14LegacyTranslatedFrame frame;
    frame.source_sequence = state_->source_sequence;
    frame.catalog_sequence = state_->catalog_sequence;
    frame.full_snapshot = true;
    for (const auto &entry : state_->records) {
      const State::Record &record = entry.second;
      if (record.live) {
        frame.live_assets.push_back(record.asset);
        frame.mutations.push_back(UpsertMutation(record.asset));
      } else {
        Ogre14LegacyAssetMutation mutation;
        mutation.type = Ogre14LegacyAssetMutationType::DESTROY;
        mutation.kind = record.asset.kind;
        mutation.source_asset_id = record.asset.source_asset_id;
        mutation.translated_revision = record.asset.translated_revision;
        mutation.stable_key = record.asset.stable_key;
        frame.mutations.push_back(std::move(mutation));
      }
    }
    std::sort(frame.live_assets.begin(), frame.live_assets.end(), AssetOrder);
    std::sort(frame.mutations.begin(), frame.mutations.end(), MutationOrder);
    output = std::move(frame);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "translator.allocation",
        "allocation failed while building the full snapshot");
  }
}

} // namespace RoR::Render
