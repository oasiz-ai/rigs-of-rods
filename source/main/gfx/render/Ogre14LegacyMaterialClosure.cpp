/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14LegacyMaterialClosure.h"

#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <utility>

namespace RoR::Render {
namespace {

constexpr char kSamplerNameSuffix[] = "|pass=0|unit=0|sampler";

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
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

const char *StableKeyKindName(RenderAssetKind kind) noexcept {
  switch (kind) {
  case RenderAssetKind::TEXTURE:
    return "texture";
  case RenderAssetKind::SAMPLER:
    return "sampler";
  case RenderAssetKind::MATERIAL:
    return "material";
  default:
    return nullptr;
  }
}

bool IsClosureAssetKind(RenderAssetKind kind) noexcept {
  return kind == RenderAssetKind::TEXTURE ||
         kind == RenderAssetKind::SAMPLER ||
         kind == RenderAssetKind::MATERIAL;
}

bool CheckedAdd(std::uint64_t value, std::uint64_t &total) noexcept {
  if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += value;
  return true;
}

bool FloatBitsEqual(float lhs, float rhs) noexcept {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  static_assert(sizeof(lhs_bits) == sizeof(lhs), "binary32 is required");
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool Float2BitsEqual(const Float2 &lhs, const Float2 &rhs) noexcept {
  return FloatBitsEqual(lhs.x, rhs.x) && FloatBitsEqual(lhs.y, rhs.y);
}

bool Float3BitsEqual(const Float3 &lhs, const Float3 &rhs) noexcept {
  return FloatBitsEqual(lhs.x, rhs.x) && FloatBitsEqual(lhs.y, rhs.y) &&
         FloatBitsEqual(lhs.z, rhs.z);
}

bool ParseCanonicalSize(const std::string &text, std::size_t &cursor,
                        std::size_t &value) noexcept {
  const std::size_t first = cursor;
  if (first >= text.size() || text[first] < '0' || text[first] > '9') {
    return false;
  }
  std::size_t candidate = 0U;
  while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
    const std::size_t digit =
        static_cast<std::size_t>(text[cursor] - '0');
    if (candidate > ((std::numeric_limits<std::size_t>::max)() - digit) /
                        10U) {
      return false;
    }
    candidate = candidate * 10U + digit;
    ++cursor;
  }
  if (cursor >= text.size() || text[cursor] != ':' ||
      (cursor - first > 1U && text[first] == '0')) {
    return false;
  }
  ++cursor;
  value = candidate;
  return true;
}

ValidationResult ParseStableKey(RenderAssetKind kind,
                                const std::string &stable_key,
                                Ogre14LegacyAssetKey &key) {
  const char *kind_name = StableKeyKindName(kind);
  if (kind_name == nullptr ||
      stable_key.size() > kMaximumOgre14LegacyStableAssetKeyBytes) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key has an unsupported kind or exceeds its bound");
  }
  const std::string prefix = std::string(kind_name) + "|group=";
  if (stable_key.compare(0U, prefix.size(), prefix) != 0) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key kind prefix is not canonical");
  }

  std::size_t cursor = prefix.size();
  std::size_t group_size = 0U;
  if (!ParseCanonicalSize(stable_key, cursor, group_size) ||
      group_size > stable_key.size() - cursor) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key resource-group length is malformed");
  }
  Ogre14LegacyAssetKey candidate;
  candidate.exact_resource_group.assign(stable_key, cursor, group_size);
  cursor += group_size;
  static constexpr char kNamePrefix[] = "|name=";
  if (stable_key.compare(cursor, sizeof(kNamePrefix) - 1U, kNamePrefix) != 0) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key name separator is malformed");
  }
  cursor += sizeof(kNamePrefix) - 1U;
  std::size_t name_size = 0U;
  if (!ParseCanonicalSize(stable_key, cursor, name_size) || name_size == 0U ||
      name_size != stable_key.size() - cursor) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key exact-name length is malformed");
  }
  candidate.exact_name.assign(stable_key, cursor, name_size);
  if (candidate.exact_name.find('\0') != std::string::npos ||
      candidate.exact_resource_group.find('\0') != std::string::npos) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key contains an embedded NUL");
  }

  std::string canonical;
  ValidationResult validation =
      BuildOgre14LegacyStableAssetKey(kind, candidate, canonical);
  if (!validation) {
    return validation;
  }
  if (canonical != stable_key) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                   "stable key is not the canonical length-delimited form");
  }
  std::size_t source_name_size = candidate.exact_name.size();
  if (kind == RenderAssetKind::SAMPLER) {
    const std::size_t suffix_size = sizeof(kSamplerNameSuffix) - 1U;
    if (source_name_size <= suffix_size ||
        candidate.exact_name.compare(source_name_size - suffix_size,
                                     suffix_size, kSamplerNameSuffix) != 0) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "asset.stable_key",
                     "sampler key is not derived from pass zero texture unit zero");
    }
    source_name_size -= suffix_size;
  }
  const std::size_t source_debug_size =
      candidate.exact_resource_group.empty()
          ? source_name_size
          : candidate.exact_resource_group.size() + 1U + source_name_size;
  const std::size_t emitted_debug_size =
      kind == RenderAssetKind::SAMPLER
          ? source_debug_size + (sizeof("/pass0/unit0") - 1U)
          : source_debug_size;
  if (emitted_debug_size > kMaximumResourceDebugNameBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "asset.stable_key",
                   "stable key cannot originate from an admitted debug identity");
  }
  key = std::move(candidate);
  return ValidationResult::Success();
}

std::string DebugName(const Ogre14LegacyAssetKey &key) {
  return key.exact_resource_group.empty()
             ? key.exact_name
             : key.exact_resource_group + "/" + key.exact_name;
}

Ogre14LegacyAssetKey SamplerKey(const Ogre14LegacyAssetKey &material_key) {
  Ogre14LegacyAssetKey key = material_key;
  key.exact_name += kSamplerNameSuffix;
  return key;
}

bool ExactAbsentReference(const RenderAssetReference &reference) noexcept {
  return IsAbsentRenderAssetReference(reference);
}

bool ExactDefaultTransform(const TextureBinding &binding) noexcept {
  return Float2BitsEqual(binding.scale, Float2{1.0F, 1.0F}) &&
         Float2BitsEqual(binding.offset, Float2{}) &&
         FloatBitsEqual(binding.rotation_radians, 0.0F);
}

bool ExactAbsentBinding(const TextureBinding &binding) noexcept {
  return ExactAbsentReference(binding.texture) &&
         ExactAbsentReference(binding.sampler) &&
         binding.texture_coordinate_set == 0U &&
         ExactDefaultTransform(binding);
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

ValidationResult ValidateCanonicalMaterial(
    const Ogre14LegacyTranslatedAsset &asset,
    const Ogre14LegacyAssetKey &parsed_key) {
  const MaterialDescriptor &material =
      std::get<MaterialDescriptor>(*asset.payload);
  ValidationResult validation = ValidateMaterialDescriptor(material);
  if (!validation) {
    return validation;
  }
  if (asset.material_audit == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "material.audit",
                   "translated material requires its immutable pipeline audit");
  }
  validation =
      ValidateOgre14LegacyMaterialPipelineAudit(*asset.material_audit);
  if (!validation) {
    return validation;
  }
  const Ogre14LegacyMaterialPipelineAudit &audit = *asset.material_audit;
  const MaterialModel expected_model =
      audit.base_color_semantic == Ogre14LegacyBaseColorSemantic::UNLIT
          ? MaterialModel::UNLIT
          : MaterialModel::PBR_METALLIC_ROUGHNESS;
  const MaterialAlphaMode expected_alpha =
      IsStraightSourceOver(audit.pipeline)
          ? MaterialAlphaMode::BLEND
          : audit.pipeline.alpha_reject ==
                    Ogre14LegacyCompareOperation::GREATER_EQUAL
                ? MaterialAlphaMode::MASK
                : MaterialAlphaMode::OPAQUE;
  const bool expected_double_sided =
      audit.pipeline.cull == Ogre14LegacyCullMode::NONE;
  const float expected_alpha_cutoff =
      static_cast<float>(audit.pipeline.alpha_reject_value) / 255.0F;

  if (material.debug_name != DebugName(parsed_key) ||
      material.model != expected_model || material.alpha_mode != expected_alpha ||
      material.double_sided != expected_double_sided ||
      !FloatBitsEqual(material.metallic_factor, 0.0F) ||
      !FloatBitsEqual(material.roughness_factor, 1.0F) ||
      !FloatBitsEqual(material.normal_scale, 1.0F) ||
      !FloatBitsEqual(material.occlusion_strength, 1.0F) ||
      !Float3BitsEqual(material.emissive_factor, Float3{}) ||
      !FloatBitsEqual(material.emissive_strength, 1.0F) ||
      !FloatBitsEqual(material.alpha_cutoff, expected_alpha_cutoff) ||
      !FloatBitsEqual(material.index_of_refraction, 1.5F)) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "material.descriptor_semantics",
        "portable material no longer matches the exact translated audit model");
  }
  if (!ExactAbsentReference(material.base_color_texture.texture) ||
      !ExactAbsentReference(material.base_color_texture.sampler) ||
      !ExactDefaultTransform(material.base_color_texture) ||
      material.base_color_texture.texture_coordinate_set > 1U ||
      !ExactAbsentBinding(material.metallic_roughness_texture) ||
      !ExactAbsentBinding(material.normal_texture) ||
      !ExactAbsentBinding(material.occlusion_texture) ||
      !ExactAbsentBinding(material.emissive_texture)) {
    return Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, "material.texture_bindings",
        "translated material must retain only canonical producer-owned binding state");
  }
  if (audit.texture_source_asset_id == 0U &&
      material.base_color_texture.texture_coordinate_set != 0U) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material.base_color_texture.texture_coordinate_set",
        "an untextured translated material must retain canonical UV set zero");
  }
  return ValidationResult::Success();
}

ValidationResult AddPayloadBytes(const Ogre14LegacyTranslatedAsset &asset,
                                 std::uint64_t &aggregate) {
  std::uint64_t asset_bytes = 0U;
  if (asset.kind == RenderAssetKind::TEXTURE) {
    const TextureResourceDescriptor &texture =
        std::get<TextureResourceDescriptor>(*asset.payload);
    for (const TextureMipLevelDescriptor &mip : texture.mip_levels) {
      if (!CheckedAdd(mip.bytes.size(), asset_bytes)) {
        return Failure(ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
                       "texture byte accounting overflowed");
      }
    }
  }
  if (asset_bytes > kMaximumOgre14LegacyMaterialClosurePayloadBytes ||
      !CheckedAdd(asset_bytes, aggregate) ||
      aggregate > kMaximumOgre14LegacyMaterialClosurePayloadBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "assets.payload_bytes",
                   "translated inventory exceeds the bounded payload budget");
  }
  return ValidationResult::Success();
}

struct IndexedAsset {
  const Ogre14LegacyTranslatedAsset *asset = nullptr;
  Ogre14LegacyAssetKey parsed_key;
};

using AssetIndex = std::map<std::uint64_t, IndexedAsset>;

ValidationResult ValidateAsset(const Ogre14LegacyTranslatedAsset &asset,
                               Ogre14LegacyAssetKey &parsed_key,
                               std::uint64_t &aggregate_payload_bytes) {
  if (!IsClosureAssetKind(asset.kind)) {
    return Failure(ValidationCode::WRONG_ASSET_KIND, "assets.kind",
                   "translated closure inventory accepts only texture, sampler, and material");
  }
  if (asset.source_asset_id == 0U || asset.source_revision == 0U ||
      asset.translated_revision == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH, "assets.lineage",
                   "translated asset IDs and revisions must be nonzero");
  }
  ValidationResult validation =
      ParseStableKey(asset.kind, asset.stable_key, parsed_key);
  if (!validation) {
    return validation;
  }
  std::uint64_t expected_id = 0U;
  validation =
      DeriveOgre14LegacySourceAssetId(asset.kind, parsed_key, expected_id);
  if (!validation) {
    return validation;
  }
  if (asset.source_asset_id != expected_id) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "assets.source_asset_id",
                   "translated source ID does not match its canonical stable key");
  }
  if (asset.payload == nullptr || asset.payload->valueless_by_exception()) {
    return Failure(ValidationCode::EMPTY_PAYLOAD, "assets.payload",
                   "translated live asset requires an immutable payload");
  }
  if (RenderAssetPayloadKind(*asset.payload) != asset.kind) {
    return Failure(ValidationCode::WRONG_RESOURCE_KIND, "assets.payload",
                   "translated payload kind disagrees with asset metadata");
  }

  switch (asset.kind) {
  case RenderAssetKind::TEXTURE:
    validation = ValidateTextureResourceDescriptor(
        std::get<TextureResourceDescriptor>(*asset.payload));
    if (asset.material_audit != nullptr) {
      return Failure(ValidationCode::WRONG_RESOURCE_KIND, "assets.material_audit",
                     "only a material may carry a pipeline audit");
    }
    if (validation &&
        std::get<TextureResourceDescriptor>(*asset.payload).debug_name !=
            DebugName(parsed_key)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "texture.debug_name",
                     "texture debug identity disagrees with its exact key");
    }
    break;
  case RenderAssetKind::SAMPLER: {
    validation = ValidateSamplerResourceDescriptor(
        std::get<SamplerResourceDescriptor>(*asset.payload));
    if (asset.material_audit != nullptr) {
      return Failure(ValidationCode::WRONG_RESOURCE_KIND, "assets.material_audit",
                     "only a material may carry a pipeline audit");
    }
    const std::size_t suffix_size = sizeof(kSamplerNameSuffix) - 1U;
    if (validation &&
        (parsed_key.exact_name.size() <= suffix_size ||
         parsed_key.exact_name.compare(parsed_key.exact_name.size() - suffix_size,
                                       suffix_size, kSamplerNameSuffix) != 0)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "sampler.stable_key",
                     "sampler key is not derived from pass zero texture unit zero");
    }
    if (validation) {
      Ogre14LegacyAssetKey material_key = parsed_key;
      material_key.exact_name.resize(material_key.exact_name.size() - suffix_size);
      const std::string expected_debug = DebugName(material_key) + "/pass0/unit0";
      if (std::get<SamplerResourceDescriptor>(*asset.payload).debug_name !=
          expected_debug) {
        return Failure(ValidationCode::INVALID_IDENTIFIER, "sampler.debug_name",
                       "sampler debug identity disagrees with its material key");
      }
    }
    break;
  }
  case RenderAssetKind::MATERIAL:
    validation = ValidateCanonicalMaterial(asset, parsed_key);
    break;
  default:
    break;
  }
  if (!validation) {
    return validation;
  }
  return AddPayloadBytes(asset, aggregate_payload_bytes);
}

bool SharedOwnerAndPointer(
    const std::shared_ptr<const RenderAssetPayload> &lhs,
    const std::shared_ptr<const RenderAssetPayload> &rhs) noexcept {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool SharedOwnerAndPointer(
    const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> &lhs,
    const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> &rhs) noexcept {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool StrictAssetOrder(const Ogre14LegacyTranslatedAsset &previous,
                      const Ogre14LegacyTranslatedAsset &current) noexcept {
  const std::uint32_t previous_rank = DependencyRank(previous.kind);
  const std::uint32_t current_rank = DependencyRank(current.kind);
  return previous_rank < current_rank ||
         (previous_rank == current_rank &&
          previous.source_asset_id < current.source_asset_id);
}

bool StrictMutationOrder(const Ogre14LegacyAssetMutation &previous,
                         const Ogre14LegacyAssetMutation &current) noexcept {
  if (previous.type != current.type) {
    return previous.type == Ogre14LegacyAssetMutationType::DESTROY &&
           current.type == Ogre14LegacyAssetMutationType::UPSERT;
  }
  const std::uint32_t previous_rank = DependencyRank(previous.kind);
  const std::uint32_t current_rank = DependencyRank(current.kind);
  if (previous_rank != current_rank) {
    return previous.type == Ogre14LegacyAssetMutationType::DESTROY
               ? previous_rank > current_rank
               : previous_rank < current_rank;
  }
  return previous.source_asset_id < current.source_asset_id;
}

ValidationResult ValidateMaterialDependencies(const AssetIndex &assets,
                                              std::set<std::uint64_t> &samplers) {
  for (const auto &entry : assets) {
    const IndexedAsset &indexed = entry.second;
    const Ogre14LegacyTranslatedAsset &asset = *indexed.asset;
    if (asset.kind != RenderAssetKind::MATERIAL) {
      continue;
    }
    const Ogre14LegacyMaterialPipelineAudit &audit = *asset.material_audit;
    if (audit.texture_source_asset_id == 0U) {
      continue;
    }
    const auto texture = assets.find(audit.texture_source_asset_id);
    const auto sampler = assets.find(audit.sampler_source_asset_id);
    if (texture == assets.end() || sampler == assets.end()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material.audit.dependencies",
                     "audited base-color dependency is absent from the full snapshot");
    }
    if (texture->second.asset->kind != RenderAssetKind::TEXTURE ||
        sampler->second.asset->kind != RenderAssetKind::SAMPLER) {
      return Failure(ValidationCode::WRONG_ASSET_KIND,
                     "material.audit.dependencies",
                     "audited base-color IDs do not identify texture then sampler");
    }
    Ogre14LegacyAssetKey expected_sampler_key = SamplerKey(indexed.parsed_key);
    std::uint64_t expected_sampler_id = 0U;
    std::string expected_sampler_stable_key;
    ValidationResult validation = DeriveOgre14LegacySourceAssetId(
        RenderAssetKind::SAMPLER, expected_sampler_key, expected_sampler_id);
    if (validation) {
      validation = BuildOgre14LegacyStableAssetKey(
          RenderAssetKind::SAMPLER, expected_sampler_key,
          expected_sampler_stable_key);
    }
    if (!validation) {
      return validation;
    }
    if (audit.sampler_source_asset_id != expected_sampler_id ||
        sampler->second.asset->stable_key != expected_sampler_stable_key) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "material.audit.sampler_source_asset_id",
                     "audited sampler is not exactly derived from its material key");
    }
    if (!samplers.insert(audit.sampler_source_asset_id).second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "material.audit.sampler_source_asset_id",
                     "one material-owned sampler was referenced more than once");
    }
    validation = ValidateMaterialTextureCompatibility(
        MaterialTextureSlot::BASE_COLOR,
        std::get<TextureResourceDescriptor>(*texture->second.asset->payload),
        std::get<SamplerResourceDescriptor>(*sampler->second.asset->payload));
    if (!validation) {
      return validation;
    }
  }
  std::size_t sampler_count = 0U;
  for (const auto &entry : assets) {
    if (entry.second.asset->kind == RenderAssetKind::SAMPLER) {
      ++sampler_count;
    }
  }
  if (sampler_count != samplers.size()) {
    return Failure(ValidationCode::MISSING_REFERENCE, "assets.samplers",
                   "full snapshot contains an orphan material-owned sampler");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateCompleteFrame(const Ogre14LegacyTranslatedFrame &frame,
                                       AssetIndex &assets) {
  if (frame.version != kOgre14LegacyTranslatedFrameVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION, "frame.version",
                   "unsupported translated legacy frame version");
  }
  if (!frame.full_snapshot) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH, "frame.full_snapshot",
                   "material closure requires an authoritative full snapshot");
  }
  if (frame.source_sequence == 0U || frame.catalog_sequence == 0U ||
      frame.catalog_sequence > frame.source_sequence) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH, "frame.sequence",
                   "full snapshot requires valid nonzero source/catalog lineage");
  }
  if (frame.live_assets.size() >
          kMaximumOgre14LegacyMaterialClosureLiveAssets ||
      frame.mutations.size() >
          kMaximumOgre14LegacyMaterialClosureMutations ||
      frame.live_assets.size() > frame.mutations.size()) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "frame.asset_counts",
                   "full snapshot exceeds closure caps or omits live upserts");
  }

  std::set<std::string, std::less<>> live_stable_keys;
  std::uint64_t aggregate_payload_bytes = 0U;
  for (std::size_t index = 0U; index < frame.live_assets.size(); ++index) {
    const Ogre14LegacyTranslatedAsset &asset = frame.live_assets[index];
    if (index != 0U && !StrictAssetOrder(frame.live_assets[index - 1U], asset)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "frame.live_assets",
                     "live inventory is not in strict dependency/ID order", index);
    }
    Ogre14LegacyAssetKey parsed_key;
    ValidationResult validation =
        ValidateAsset(asset, parsed_key, aggregate_payload_bytes);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    if (!live_stable_keys.insert(asset.stable_key).second ||
        !assets.emplace(asset.source_asset_id,
                        IndexedAsset{&asset, std::move(parsed_key)})
             .second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "frame.live_assets",
                     "live inventory duplicates a stable key or source ID", index);
    }
  }

  std::set<std::uint64_t> mutation_ids;
  std::set<std::string, std::less<>> mutation_stable_keys;
  std::set<std::uint64_t> live_upserts;
  for (std::size_t index = 0U; index < frame.mutations.size(); ++index) {
    const Ogre14LegacyAssetMutation &mutation = frame.mutations[index];
    if (mutation.type != Ogre14LegacyAssetMutationType::UPSERT &&
        mutation.type != Ogre14LegacyAssetMutationType::DESTROY) {
      return Failure(ValidationCode::INVALID_ENUM, "frame.mutations.type",
                     "full snapshot mutation has an unknown type", index);
    }
    if (!IsClosureAssetKind(mutation.kind) || mutation.source_asset_id == 0U ||
        mutation.translated_revision == 0U) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "frame.mutations.lineage",
                     "full snapshot mutation has invalid kind, ID, or revision",
                     index);
    }
    if (index != 0U &&
        !StrictMutationOrder(frame.mutations[index - 1U], mutation)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER, "frame.mutations",
                     "mutations are not in strict transaction order", index);
    }
    Ogre14LegacyAssetKey parsed_key;
    ValidationResult validation =
        ParseStableKey(mutation.kind, mutation.stable_key, parsed_key);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    std::uint64_t expected_id = 0U;
    validation = DeriveOgre14LegacySourceAssetId(mutation.kind, parsed_key,
                                                 expected_id);
    if (!validation || expected_id != mutation.source_asset_id) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "frame.mutations.source_asset_id",
                     "mutation source ID disagrees with its canonical stable key",
                     index);
    }
    if (!mutation_ids.insert(mutation.source_asset_id).second ||
        !mutation_stable_keys.insert(mutation.stable_key).second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER, "frame.mutations",
                     "full snapshot duplicates a mutation ID or stable key", index);
    }

    if (mutation.type == Ogre14LegacyAssetMutationType::DESTROY) {
      if (mutation.payload != nullptr || mutation.material_audit != nullptr) {
        return Failure(ValidationCode::EMPTY_PAYLOAD, "frame.mutations.destroy",
                       "destroy mutation must not retain payload or audit owners",
                       index);
      }
      if (assets.find(mutation.source_asset_id) != assets.end()) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "frame.mutations.destroy",
                       "a live source ID cannot also be a full-snapshot tombstone",
                       index);
      }
      continue;
    }

    const auto live = assets.find(mutation.source_asset_id);
    if (live == assets.end()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "frame.mutations.upsert",
                     "full-snapshot upsert has no matching live asset", index);
    }
    const Ogre14LegacyTranslatedAsset &asset = *live->second.asset;
    if (mutation.kind != asset.kind ||
        mutation.translated_revision != asset.translated_revision ||
        mutation.stable_key != asset.stable_key ||
        !SharedOwnerAndPointer(mutation.payload, asset.payload) ||
        !SharedOwnerAndPointer(mutation.material_audit, asset.material_audit)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "frame.mutations.upsert",
                     "upsert does not share exact metadata and immutable owners",
                     index);
    }
    live_upserts.insert(mutation.source_asset_id);
  }
  if (live_upserts.size() != assets.size()) {
    return Failure(ValidationCode::MISSING_REFERENCE, "frame.mutations.upsert",
                   "full snapshot omits an upsert for a live asset");
  }
  std::set<std::uint64_t> material_samplers;
  return ValidateMaterialDependencies(assets, material_samplers);
}

GraphicsSceneAssetInput ToGraphicsAsset(
    const Ogre14LegacyTranslatedAsset &asset) {
  GraphicsSceneAssetInput input;
  input.source_asset_id = asset.source_asset_id;
  input.payload = asset.payload;
  return input;
}

} // namespace

ValidationResult ValidateOgre14LegacyMaterialClosure(
    const Ogre14LegacyMaterialClosure &closure,
    const Ogre14LegacyAssetKey &material_key) {
  if (closure.version != kOgre14LegacyMaterialClosureVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "material_closure.version",
                   "unsupported material closure version");
  }
  if (closure.source_sequence == 0U || closure.catalog_sequence == 0U ||
      closure.catalog_sequence > closure.source_sequence) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "material_closure.sequence",
                   "material closure requires valid nonzero source/catalog lineage");
  }
  std::uint64_t expected_material_id = 0U;
  ValidationResult validation = DeriveOgre14LegacySourceAssetId(
      RenderAssetKind::MATERIAL, material_key, expected_material_id);
  if (!validation) {
    return validation;
  }
  if (closure.material_source_asset_id != expected_material_id) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "material_closure.material_source_asset_id",
                   "closure material ID disagrees with its exact key");
  }
  if (closure.material_audit == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_closure.material_audit",
                   "resolved material requires its immutable translated audit");
  }
  validation =
      ValidateOgre14LegacyMaterialPipelineAudit(*closure.material_audit);
  if (!validation) {
    return validation;
  }
  if (closure.requires_reverse_winding !=
      closure.material_audit->requires_reverse_winding) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_closure.requires_reverse_winding",
                   "closure winding disagrees with its translated audit");
  }

  const bool textured =
      closure.material_audit->texture_source_asset_id != 0U;
  if ((textured && closure.assets.size() != 3U) ||
      (!textured && closure.assets.size() != 1U)) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "material_closure.assets",
                   "closure must contain material alone or texture, sampler, material");
  }
  if (closure.asset_keys.size() != closure.assets.size()) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "material_closure.asset_keys",
                   "closure requires one exact key for every dependency-ordered asset");
  }
  if (closure.assets.size() >
      kMaximumOgre14LegacyMaterialClosureLiveAssets) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_closure.assets",
                   "closure dependency count exceeds the translator cap");
  }

  std::set<std::uint64_t> source_ids;
  std::uint64_t aggregate_payload_bytes = 0U;
  for (std::size_t index = 0U; index < closure.assets.size(); ++index) {
    const GraphicsSceneAssetInput &asset = closure.assets[index];
    if (asset.source_asset_id == 0U ||
        !source_ids.insert(asset.source_asset_id).second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "material_closure.assets.source_asset_id",
                     "closure asset IDs must be nonzero and unique", index);
    }
    if (asset.payload == nullptr || asset.payload->valueless_by_exception()) {
      return Failure(ValidationCode::EMPTY_PAYLOAD,
                     "material_closure.assets.payload",
                     "closure assets require immutable payload owners", index);
    }
    const RenderAssetKind kind = RenderAssetPayloadKind(*asset.payload);
    const RenderAssetKind expected_kind =
        textured
            ? (index == 0U ? RenderAssetKind::TEXTURE
                           : index == 1U ? RenderAssetKind::SAMPLER
                                         : RenderAssetKind::MATERIAL)
            : RenderAssetKind::MATERIAL;
    if (kind != expected_kind) {
      return Failure(ValidationCode::WRONG_ASSET_KIND,
                     "material_closure.assets.kind",
                     "closure dependencies are not texture, sampler, material ordered",
                     index);
    }
    if (kind != RenderAssetKind::MATERIAL) {
      for (const GraphicsSceneAssetBinding &binding :
           asset.material_bindings) {
        if (binding.texture_source_asset_id != 0U ||
            binding.sampler_source_asset_id != 0U) {
          return Failure(ValidationCode::WRONG_ASSET_KIND,
                         "material_closure.assets.material_bindings",
                         "only the material may carry producer-owned bindings",
                         index);
        }
      }
    }
    std::string stable_key;
    validation = BuildOgre14LegacyStableAssetKey(
        kind, closure.asset_keys[index], stable_key);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    Ogre14LegacyTranslatedAsset translated_asset;
    translated_asset.kind = kind;
    translated_asset.source_asset_id = asset.source_asset_id;
    translated_asset.source_revision = 1U;
    translated_asset.translated_revision = 1U;
    translated_asset.stable_key = std::move(stable_key);
    translated_asset.payload = asset.payload;
    if (kind == RenderAssetKind::MATERIAL) {
      translated_asset.material_audit = closure.material_audit;
    }
    Ogre14LegacyAssetKey parsed_key;
    validation = ValidateAsset(translated_asset, parsed_key,
                               aggregate_payload_bytes);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    if (parsed_key != closure.asset_keys[index]) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "material_closure.asset_keys",
                     "closure key is not the canonical identity used for its payload",
                     index);
    }
  }
  if (aggregate_payload_bytes >
      kMaximumOgre14LegacyMaterialClosurePayloadBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_closure.assets.payload_bytes",
                   "closure texture bytes exceed the translator cap");
  }

  const GraphicsSceneAssetInput &material = closure.assets.back();
  if (closure.asset_keys.back() != material_key) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "material_closure.asset_keys.material",
                   "material-last key disagrees with the requested exact identity");
  }
  if (material.source_asset_id != closure.material_source_asset_id) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "material_closure.assets.material",
                   "material-last asset ID disagrees with the closure identity");
  }
  const std::size_t base_color_slot =
      static_cast<std::size_t>(MaterialTextureSlot::BASE_COLOR);
  for (std::size_t slot = 0U; slot < material.material_bindings.size(); ++slot) {
    const GraphicsSceneAssetBinding &binding =
        material.material_bindings[slot];
    if (slot == base_color_slot && textured) {
      if (binding.texture_source_asset_id !=
              closure.material_audit->texture_source_asset_id ||
          binding.sampler_source_asset_id !=
              closure.material_audit->sampler_source_asset_id) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "material_closure.assets.material_bindings",
                       "base-color binding disagrees with the translated audit");
      }
    } else if (binding.texture_source_asset_id != 0U ||
               binding.sampler_source_asset_id != 0U) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "material_closure.assets.material_bindings",
                     "closure carries an unaudited material binding");
    }
  }
  if (textured) {
    if (closure.assets[0U].source_asset_id !=
            closure.material_audit->texture_source_asset_id ||
        closure.assets[1U].source_asset_id !=
            closure.material_audit->sampler_source_asset_id) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "material_closure.assets.dependencies",
                     "dependency IDs disagree with the translated audit");
    }
    Ogre14LegacyAssetKey sampler_key = SamplerKey(material_key);
    std::uint64_t expected_sampler_id = 0U;
    validation = DeriveOgre14LegacySourceAssetId(
        RenderAssetKind::SAMPLER, sampler_key, expected_sampler_id);
    if (!validation) {
      return validation;
    }
    if (closure.assets[1U].source_asset_id != expected_sampler_id) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "material_closure.assets.sampler_source_asset_id",
                     "sampler ID is not exactly derived from its material key");
    }
    if (closure.asset_keys[1U] != sampler_key) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "material_closure.asset_keys.sampler",
                     "sampler key is not exactly derived from its material key");
    }
    validation = ValidateMaterialTextureCompatibility(
        MaterialTextureSlot::BASE_COLOR,
        std::get<TextureResourceDescriptor>(*closure.assets[0U].payload),
        std::get<SamplerResourceDescriptor>(*closure.assets[1U].payload));
    if (!validation) {
      return validation;
    }
  } else if (closure.material_audit->sampler_source_asset_id != 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_closure.assets.dependencies",
                   "untextured material cannot retain a sampler dependency");
  }
  return ValidationResult::Success();
}

ValidationResult ResolveOgre14LegacyMaterialClosure(
    const Ogre14LegacyTranslatedFrame &frame,
    const Ogre14LegacyAssetKey &material_key,
    Ogre14LegacyMaterialClosure &output,
    IOgre14LegacyMaterialClosureFaultInjector *fault_injector) {
  if (frame.live_assets.size() >
          kMaximumOgre14LegacyMaterialClosureLiveAssets ||
      frame.mutations.size() >
          kMaximumOgre14LegacyMaterialClosureMutations) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "frame.asset_counts",
                   "translated frame exceeds fixed closure count caps");
  }
  try {
    if (material_key.exact_name.size() > kMaximumMaterialDebugNameBytes ||
        (!material_key.exact_resource_group.empty() &&
         (material_key.exact_name.size() >= kMaximumMaterialDebugNameBytes ||
          material_key.exact_resource_group.size() >
              kMaximumMaterialDebugNameBytes - 1U -
                  material_key.exact_name.size()))) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "material.key",
                     "requested material identity exceeds translator admission");
    }
    std::string material_stable_key;
    ValidationResult validation = BuildOgre14LegacyStableAssetKey(
        RenderAssetKind::MATERIAL, material_key, material_stable_key);
    if (!validation) {
      return validation;
    }
    std::uint64_t material_id = 0U;
    validation = DeriveOgre14LegacySourceAssetId(
        RenderAssetKind::MATERIAL, material_key, material_id);
    if (!validation) {
      return validation;
    }

    if (fault_injector != nullptr) {
      fault_injector->AtFaultPoint(
          Ogre14LegacyMaterialClosureFaultPoint::BEFORE_INDEX_CONSTRUCTION);
    }
    AssetIndex assets;
    validation = ValidateCompleteFrame(frame, assets);
    if (!validation) {
      return validation;
    }
    const auto material = assets.find(material_id);
    if (material == assets.end() ||
        material->second.asset->stable_key != material_stable_key) {
      return Failure(ValidationCode::MISSING_REFERENCE, "material.key",
                     "exact material key is absent from the full snapshot");
    }
    if (material->second.asset->kind != RenderAssetKind::MATERIAL) {
      return Failure(ValidationCode::WRONG_ASSET_KIND, "material.key",
                     "exact material identity resolved to the wrong kind");
    }

    const Ogre14LegacyTranslatedAsset &material_asset =
        *material->second.asset;
    const Ogre14LegacyMaterialPipelineAudit &audit =
        *material_asset.material_audit;
    Ogre14LegacyMaterialClosure candidate;
    candidate.source_sequence = frame.source_sequence;
    candidate.catalog_sequence = frame.catalog_sequence;
    candidate.material_source_asset_id = material_asset.source_asset_id;
    candidate.requires_reverse_winding = audit.requires_reverse_winding;
    candidate.material_audit = material_asset.material_audit;
    candidate.asset_keys.reserve(audit.texture_source_asset_id == 0U ? 1U
                                                                     : 3U);
    candidate.assets.reserve(audit.texture_source_asset_id == 0U ? 1U : 3U);

    if (audit.texture_source_asset_id != 0U) {
      const auto texture = assets.find(audit.texture_source_asset_id);
      const auto sampler = assets.find(audit.sampler_source_asset_id);
      if (texture == assets.end() || sampler == assets.end()) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "material.audit.dependencies",
                       "validated dependencies disappeared during closure build");
      }
      candidate.asset_keys.push_back(texture->second.parsed_key);
      candidate.assets.push_back(ToGraphicsAsset(*texture->second.asset));
      if (fault_injector != nullptr) {
        fault_injector->AtFaultPoint(
            Ogre14LegacyMaterialClosureFaultPoint::DURING_DEPENDENCY_ASSEMBLY);
      }
      candidate.asset_keys.push_back(sampler->second.parsed_key);
      candidate.assets.push_back(ToGraphicsAsset(*sampler->second.asset));
    }

    GraphicsSceneAssetInput material_input = ToGraphicsAsset(material_asset);
    if (audit.texture_source_asset_id != 0U) {
      GraphicsSceneAssetBinding &binding =
          material_input.material_bindings[static_cast<std::size_t>(
              MaterialTextureSlot::BASE_COLOR)];
      binding.texture_source_asset_id = audit.texture_source_asset_id;
      binding.sampler_source_asset_id = audit.sampler_source_asset_id;
    }
    candidate.asset_keys.push_back(material->second.parsed_key);
    candidate.assets.push_back(std::move(material_input));
    validation = ValidateOgre14LegacyMaterialClosure(candidate, material_key);
    if (!validation) {
      return validation;
    }
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD, "material_closure.allocation",
                   "allocation failed before the material closure was published");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_closure.exception",
                   "unexpected exception before the material closure was published");
  }
}

} // namespace RoR::Render
