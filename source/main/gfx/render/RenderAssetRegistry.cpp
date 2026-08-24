/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetRegistry.h"

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace RoR::Render {
namespace {

// A revision identifies exact immutable upload bytes. Numeric equality would
// collapse the observably different IEEE encodings of positive and negative
// zero, so catalog replay compares every floating object representation.
static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  (std::numeric_limits<float>::is_iec559),
              "renderer asset payloads require IEEE-754 binary32 floats");

bool EqualFloatBits(const float &lhs, const float &rhs) noexcept {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool EqualFloat2Bits(const Float2 &lhs, const Float2 &rhs) noexcept {
  return EqualFloatBits(lhs.x, rhs.x) && EqualFloatBits(lhs.y, rhs.y);
}

bool EqualFloat3Bits(const Float3 &lhs, const Float3 &rhs) noexcept {
  return EqualFloatBits(lhs.x, rhs.x) && EqualFloatBits(lhs.y, rhs.y) &&
         EqualFloatBits(lhs.z, rhs.z);
}

bool EqualFloat4Bits(const Float4 &lhs, const Float4 &rhs) noexcept {
  return EqualFloatBits(lhs.x, rhs.x) && EqualFloatBits(lhs.y, rhs.y) &&
         EqualFloatBits(lhs.z, rhs.z) && EqualFloatBits(lhs.w, rhs.w);
}

bool EqualBoundsBits(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return EqualFloat3Bits(lhs.minimum, rhs.minimum) &&
         EqualFloat3Bits(lhs.maximum, rhs.maximum);
}

template <typename Value, typename Equal>
bool EqualVector(const std::vector<Value> &lhs, const std::vector<Value> &rhs,
                 Equal equal) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    if (!equal(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

bool EqualMip(const TextureMipLevelDescriptor &lhs,
              const TextureMipLevelDescriptor &rhs) noexcept {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.row_pitch_bytes == rhs.row_pitch_bytes &&
         lhs.layer_pitch_bytes == rhs.layer_pitch_bytes &&
         lhs.bytes == rhs.bytes;
}

bool EqualMesh(const MeshResourceDescriptor &lhs,
               const MeshResourceDescriptor &rhs) noexcept {
  return lhs.debug_name == rhs.debug_name &&
         lhs.topology_revision == rhs.topology_revision &&
         EquivalentMeshResourceContents(lhs, rhs);
}

bool EqualMeshLod(const MeshDistanceLodLevelDescriptor &lhs,
                  const MeshDistanceLodLevelDescriptor &rhs) noexcept {
  return EqualFloatBits(lhs.activation_distance_meters,
                        rhs.activation_distance_meters) &&
         lhs.indices == rhs.indices;
}

bool EqualTexture(const TextureResourceDescriptor &lhs,
                  const TextureResourceDescriptor &rhs) noexcept {
  if (lhs.version != rhs.version || lhs.debug_name != rhs.debug_name ||
      lhs.type != rhs.type || lhs.format != rhs.format ||
      lhs.color_space != rhs.color_space || lhs.width != rhs.width ||
      lhs.height != rhs.height || lhs.array_layers != rhs.array_layers ||
      lhs.mip_levels.size() != rhs.mip_levels.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.mip_levels.size(); ++index) {
    if (!EqualMip(lhs.mip_levels[index], rhs.mip_levels[index])) {
      return false;
    }
  }
  return true;
}

bool EqualSampler(const SamplerResourceDescriptor &lhs,
                  const SamplerResourceDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.debug_name == rhs.debug_name &&
         lhs.minification_filter == rhs.minification_filter &&
         lhs.magnification_filter == rhs.magnification_filter &&
         lhs.mip_filter == rhs.mip_filter && lhs.address_u == rhs.address_u &&
         lhs.address_v == rhs.address_v && lhs.address_w == rhs.address_w &&
         EqualFloatBits(lhs.mip_lod_bias, rhs.mip_lod_bias) &&
         EqualFloatBits(lhs.minimum_lod, rhs.minimum_lod) &&
         EqualFloatBits(lhs.maximum_lod, rhs.maximum_lod) &&
         lhs.anisotropy_enabled == rhs.anisotropy_enabled &&
         EqualFloatBits(lhs.maximum_anisotropy, rhs.maximum_anisotropy) &&
         lhs.compare_enabled == rhs.compare_enabled &&
         lhs.compare_operation == rhs.compare_operation &&
         EqualFloat4Bits(lhs.border_color, rhs.border_color);
}

bool EqualBinding(const TextureBinding &lhs,
                  const TextureBinding &rhs) noexcept {
  return lhs.texture == rhs.texture && lhs.sampler == rhs.sampler &&
         lhs.texture_coordinate_set == rhs.texture_coordinate_set &&
         EqualFloat2Bits(lhs.scale, rhs.scale) &&
         EqualFloat2Bits(lhs.offset, rhs.offset) &&
         EqualFloatBits(lhs.rotation_radians, rhs.rotation_radians);
}

bool EqualDetailLayers(const MaterialDescriptor &lhs,
                       const MaterialDescriptor &rhs) noexcept {
  for (std::size_t layer = 0U; layer < kMaterialDetailMapCount; ++layer) {
    if (!EqualBinding(lhs.detail_textures[layer],
                      rhs.detail_textures[layer]) ||
        !EqualFloatBits(lhs.detail_weights[layer],
                        rhs.detail_weights[layer])) {
      return false;
    }
  }
  return true;
}

bool EqualMaterial(const MaterialDescriptor &lhs,
                   const MaterialDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.debug_name == rhs.debug_name &&
         lhs.model == rhs.model && lhs.blend_mode == rhs.blend_mode &&
         lhs.alpha_test_mode == rhs.alpha_test_mode &&
         lhs.pbr_workflow == rhs.pbr_workflow &&
         lhs.base_color_transfer == rhs.base_color_transfer &&
         lhs.double_sided == rhs.double_sided &&
         lhs.depth_write == rhs.depth_write &&
         EqualFloat4Bits(lhs.base_color_factor, rhs.base_color_factor) &&
         EqualFloatBits(lhs.metallic_factor, rhs.metallic_factor) &&
         EqualFloatBits(lhs.roughness_factor, rhs.roughness_factor) &&
         EqualFloat3Bits(lhs.specular_factor, rhs.specular_factor) &&
         EqualFloatBits(lhs.normal_scale, rhs.normal_scale) &&
         EqualFloatBits(lhs.occlusion_strength, rhs.occlusion_strength) &&
         EqualFloat3Bits(lhs.emissive_factor, rhs.emissive_factor) &&
         EqualFloatBits(lhs.emissive_strength, rhs.emissive_strength) &&
         EqualFloatBits(lhs.alpha_cutoff, rhs.alpha_cutoff) &&
         EqualFloatBits(lhs.index_of_refraction, rhs.index_of_refraction) &&
         lhs.transmission_mode == rhs.transmission_mode &&
         EqualFloatBits(lhs.transmission_factor,
                        rhs.transmission_factor) &&
         EqualFloatBits(lhs.attenuation_color.x,
                        rhs.attenuation_color.x) &&
         EqualFloatBits(lhs.attenuation_color.y,
                        rhs.attenuation_color.y) &&
         EqualFloatBits(lhs.attenuation_color.z,
                        rhs.attenuation_color.z) &&
         EqualFloatBits(lhs.attenuation_distance_m,
                        rhs.attenuation_distance_m) &&
         EqualFloatBits(lhs.slab_thickness_m, rhs.slab_thickness_m) &&
         EqualBinding(lhs.base_color_texture, rhs.base_color_texture) &&
         EqualBinding(lhs.metallic_roughness_texture,
                      rhs.metallic_roughness_texture) &&
         EqualBinding(lhs.normal_texture, rhs.normal_texture) &&
         EqualBinding(lhs.occlusion_texture, rhs.occlusion_texture) &&
         EqualBinding(lhs.emissive_texture, rhs.emissive_texture) &&
         EqualBinding(lhs.specular_texture, rhs.specular_texture) &&
         EqualBinding(lhs.detail_weight_texture, rhs.detail_weight_texture) &&
         EqualDetailLayers(lhs, rhs);
}

ValidationResult ValidatePayload(const RenderAssetMutation &mutation,
                                 std::size_t index) {
  if (mutation.payload.valueless_by_exception()) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "mutations.payload",
        "asset payload variant is valueless after an exception", index);
  }
  const RenderAssetKind payload_kind = RenderAssetPayloadKind(mutation.payload);
  if (mutation.type == RenderAssetMutationType::DESTROY) {
    if (!std::holds_alternative<std::monostate>(mutation.payload)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "mutations.payload",
          "destroy mutations must carry no descriptor", index);
    }
    return ValidationResult::Success();
  }
  if (payload_kind == RenderAssetKind::INVALID) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     "mutations.payload",
                                     "upsert mutations require a descriptor",
                                     index);
  }
  if (payload_kind != mutation.asset.kind) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_ASSET_KIND, "mutations.payload",
        "descriptor kind differs from the asset reference", index);
  }

  ValidationResult validation;
  switch (payload_kind) {
  case RenderAssetKind::MESH:
    validation = ValidateMeshResourceDescriptor(
        std::get<MeshResourceDescriptor>(mutation.payload));
    break;
  case RenderAssetKind::TEXTURE:
    validation = ValidateTextureResourceDescriptor(
        std::get<TextureResourceDescriptor>(mutation.payload));
    break;
  case RenderAssetKind::MATERIAL:
    validation = ValidateMaterialDescriptor(
        std::get<MaterialDescriptor>(mutation.payload));
    break;
  case RenderAssetKind::SAMPLER:
    validation = ValidateSamplerResourceDescriptor(
        std::get<SamplerResourceDescriptor>(mutation.payload));
    break;
  case RenderAssetKind::INVALID:
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     "mutations.payload",
                                     "upsert descriptor is missing", index);
  }
  if (!validation) {
    validation.element_index = index;
  }
  return validation;
}

std::array<const TextureBinding *, 11U>
MaterialBindings(const MaterialDescriptor &material) noexcept {
  return {{&material.base_color_texture,
           &material.metallic_roughness_texture,
           &material.normal_texture,
           &material.occlusion_texture,
           &material.emissive_texture,
           &material.specular_texture,
           &material.detail_weight_texture,
           &material.detail_textures[0],
           &material.detail_textures[1],
           &material.detail_textures[2],
           &material.detail_textures[3]}};
}

const std::array<MaterialTextureSlot, 11U> kMaterialSlots{{
    MaterialTextureSlot::BASE_COLOR,
    MaterialTextureSlot::METALLIC_ROUGHNESS,
    MaterialTextureSlot::NORMAL,
    MaterialTextureSlot::OCCLUSION,
    MaterialTextureSlot::EMISSIVE,
    MaterialTextureSlot::SPECULAR,
    MaterialTextureSlot::DETAIL_WEIGHT,
    MaterialTextureSlot::DETAIL0,
    MaterialTextureSlot::DETAIL1,
    MaterialTextureSlot::DETAIL2,
    MaterialTextureSlot::DETAIL3,
}};

RenderAssetRecord MakeRecord(const RenderAssetMutation &mutation) {
  return RenderAssetRecord{
      mutation.asset,
      std::make_shared<const RenderAssetPayload>(mutation.payload)};
}

} // namespace

bool IsKnownRenderAssetMutationType(RenderAssetMutationType type) noexcept {
  switch (type) {
  case RenderAssetMutationType::UPSERT:
  case RenderAssetMutationType::DESTROY:
    return true;
  }
  return false;
}

RenderAssetKind
RenderAssetPayloadKind(const RenderAssetPayload &payload) noexcept {
  if (std::holds_alternative<MeshResourceDescriptor>(payload)) {
    return RenderAssetKind::MESH;
  }
  if (std::holds_alternative<TextureResourceDescriptor>(payload)) {
    return RenderAssetKind::TEXTURE;
  }
  if (std::holds_alternative<MaterialDescriptor>(payload)) {
    return RenderAssetKind::MATERIAL;
  }
  if (std::holds_alternative<SamplerResourceDescriptor>(payload)) {
    return RenderAssetKind::SAMPLER;
  }
  return RenderAssetKind::INVALID;
}

bool EquivalentMeshResourceContents(const MeshResourceDescriptor &lhs,
                                    const MeshResourceDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.topology == rhs.topology &&
         lhs.index_format == rhs.index_format && lhs.dynamic == rhs.dynamic &&
         EqualBoundsBits(lhs.local_bounds, rhs.local_bounds) &&
         EqualVector(lhs.positions, rhs.positions, EqualFloat3Bits) &&
         EqualVector(lhs.normals, rhs.normals, EqualFloat3Bits) &&
         EqualVector(lhs.tangents, rhs.tangents, EqualFloat4Bits) &&
         EqualVector(lhs.velocities, rhs.velocities, EqualFloat3Bits) &&
         EqualVector(lhs.texture_coordinates_0, rhs.texture_coordinates_0,
                     EqualFloat2Bits) &&
         EqualVector(lhs.texture_coordinates_1, rhs.texture_coordinates_1,
                     EqualFloat2Bits) &&
         EqualVector(lhs.colors, rhs.colors, EqualFloat4Bits) &&
         lhs.indices == rhs.indices &&
         EqualVector(lhs.distance_lod_levels, rhs.distance_lod_levels,
                     EqualMeshLod);
}

bool EquivalentRenderAssetPayload(const RenderAssetPayload &lhs,
                                  const RenderAssetPayload &rhs) noexcept {
  if (lhs.valueless_by_exception() || rhs.valueless_by_exception()) {
    return false;
  }
  if (lhs.index() != rhs.index()) {
    return false;
  }
  if (std::holds_alternative<std::monostate>(lhs)) {
    return true;
  }
  if (const auto *mesh = std::get_if<MeshResourceDescriptor>(&lhs)) {
    return EqualMesh(*mesh, std::get<MeshResourceDescriptor>(rhs));
  }
  if (const auto *texture = std::get_if<TextureResourceDescriptor>(&lhs)) {
    return EqualTexture(*texture, std::get<TextureResourceDescriptor>(rhs));
  }
  if (const auto *material = std::get_if<MaterialDescriptor>(&lhs)) {
    return EqualMaterial(*material, std::get<MaterialDescriptor>(rhs));
  }
  if (const auto *sampler =
          std::get_if<SamplerResourceDescriptor>(&lhs)) {
    return EqualSampler(*sampler,
                        std::get<SamplerResourceDescriptor>(rhs));
  }
  return false;
}

ValidationResult ValidateRenderAssetDelta(const RenderAssetDelta &delta) {
  if (delta.version != kRenderAssetRegistryContractVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported asset delta version");
  }
  if (delta.registry_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "registry_id",
                                     "registry identifier must be nonzero");
  }
  if (delta.sequence == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "sequence",
                                     "asset sequence must be nonzero");
  }
  if (delta.full_snapshot) {
    if (delta.base_sequence != 0U) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "base_sequence",
          "a full asset snapshot must use base sequence zero");
    }
  } else if (delta.base_sequence == (std::numeric_limits<std::uint64_t>::max)() ||
             delta.sequence != delta.base_sequence + 1U) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "sequence",
        "an incremental asset delta must advance its base by exactly one");
  }

  RenderAssetId previous_id;
  bool has_previous = false;
  for (std::size_t index = 0U; index < delta.mutations.size(); ++index) {
    const RenderAssetMutation &mutation = delta.mutations[index];
    if (!IsKnownRenderAssetMutationType(mutation.type)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "mutations.type",
                                       "unknown asset mutation type", index);
    }
    if (!mutation.asset.valid()) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_ASSET_REFERENCE, "mutations.asset",
          "asset ID, kind, and revision must be valid", index);
    }
    if (mutation.asset.revision > delta.sequence) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "mutations.asset.revision",
          "asset revision cannot exceed the transaction sequence", index);
    }
    if (has_previous && !(previous_id < mutation.asset.id)) {
      return ValidationResult::Failure(
          previous_id == mutation.asset.id
              ? ValidationCode::DUPLICATE_IDENTIFIER
              : ValidationCode::NON_DETERMINISTIC_ORDER,
          "mutations.asset.id",
          previous_id == mutation.asset.id
              ? "an asset may be mutated only once per transaction"
              : "asset mutations must be strictly sorted by ID",
          index);
    }
    previous_id = mutation.asset.id;
    has_previous = true;

    const ValidationResult validation = ValidatePayload(mutation, index);
    if (!validation) {
      return validation;
    }
  }
  return ValidationResult::Success();
}

std::size_t RenderAssetRegistry::live_count() const noexcept {
  std::size_t count = 0U;
  for (const auto &entry : records_) {
    if (entry.second.live()) {
      ++count;
    }
  }
  return count;
}

ValidationResult RenderAssetRegistry::Apply(const RenderAssetDelta &delta) {
  ValidationResult validation = ValidateRenderAssetDelta(delta);
  if (!validation) {
    return validation;
  }
  if (registry_id_ == 0U || delta.registry_id != registry_id_) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "registry_id",
        "asset delta belongs to a different or invalid registry");
  }

  RecordMap candidate;
  if (delta.full_snapshot) {
    if (delta.sequence < sequence_) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "sequence",
          "a full asset snapshot may not move the registry backwards");
    }
    const std::uint64_t sequence_gap = delta.sequence - sequence_;
    for (const RenderAssetMutation &mutation : delta.mutations) {
      if (mutation.type == RenderAssetMutationType::DESTROY &&
          mutation.asset.revision < 2U) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "mutations.asset.revision",
            "a full-snapshot tombstone must follow a live revision");
      }
      candidate.emplace(mutation.asset.id, MakeRecord(mutation));
    }
    if (sequence_ != 0U) {
      for (const auto &prior_entry : records_) {
        const auto current = candidate.find(prior_entry.first);
        if (current == candidate.end()) {
          return ValidationResult::Failure(
              ValidationCode::SEQUENCE_MISMATCH, "mutations.asset.id",
              "a full snapshot omitted an existing asset or tombstone");
        }
        if (current->second.asset.kind != prior_entry.second.asset.kind) {
          return ValidationResult::Failure(
              ValidationCode::WRONG_ASSET_KIND, "mutations.asset.kind",
              "a full snapshot changed an existing asset kind");
        }
        if (current->second.asset.revision <
            prior_entry.second.asset.revision) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH,
              "mutations.asset.revision",
              "a full snapshot moved an asset revision backwards");
        }
        const std::uint64_t revision_gap = current->second.asset.revision -
                                           prior_entry.second.asset.revision;
        if (revision_gap > sequence_gap) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH,
              "mutations.asset.revision",
              "asset revision advanced more times than the snapshot sequence");
        }
        if (!prior_entry.second.live() &&
            (current->second.live() || current->second.asset.revision !=
                                           prior_entry.second.asset.revision)) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH, "mutations.asset.id",
              "a full snapshot changed or resurrected a terminal tombstone");
        }
        if (current->second.asset.revision ==
                prior_entry.second.asset.revision &&
            !EquivalentRenderAssetPayload(*current->second.payload,
                                          *prior_entry.second.payload)) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH,
              "mutations.asset.revision",
              "one asset revision identified different contents");
        }
      }
    }
    for (const auto &candidate_entry : candidate) {
      if (records_.find(candidate_entry.first) == records_.end() &&
          candidate_entry.second.asset.revision > sequence_gap) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "mutations.asset.revision",
            "new asset revision exceeds elapsed snapshot sequences");
      }
    }
  } else {
    if (delta.base_sequence != sequence_) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "base_sequence",
          "incremental asset delta does not continue this registry");
    }
    candidate = records_;
    for (std::size_t index = 0U; index < delta.mutations.size(); ++index) {
      const RenderAssetMutation &mutation = delta.mutations[index];
      const auto current = candidate.find(mutation.asset.id);
      if (current == candidate.end()) {
        if (mutation.type != RenderAssetMutationType::UPSERT ||
            mutation.asset.revision != 1U) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH,
              "mutations.asset.revision",
              "a new asset must be created by revision one", index);
        }
        candidate.emplace(mutation.asset.id, MakeRecord(mutation));
        continue;
      }
      if (current->second.asset.kind != mutation.asset.kind) {
        return ValidationResult::Failure(
            ValidationCode::WRONG_ASSET_KIND, "mutations.asset.kind",
            "an asset ID cannot change kind during a registry lifetime",
            index);
      }
      if (!current->second.live()) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "mutations.asset.id",
            "a tombstoned asset ID may never be reused", index);
      }
      if (current->second.asset.revision ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          mutation.asset.revision != current->second.asset.revision + 1U) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "mutations.asset.revision",
            "an asset update must advance its live revision by exactly one",
            index);
      }
      current->second = MakeRecord(mutation);
    }
  }

  validation = ValidateResolvedDependencies(candidate);
  if (!validation) {
    return validation;
  }

  if (delta.full_snapshot && delta.sequence == sequence_) {
    if (candidate.size() != records_.size()) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "sequence",
          "same-sequence full snapshot changed the asset catalog");
    }
    auto lhs = candidate.begin();
    auto rhs = records_.begin();
    for (; lhs != candidate.end(); ++lhs, ++rhs) {
      if (lhs->first != rhs->first || lhs->second.asset != rhs->second.asset ||
          !EquivalentRenderAssetPayload(*lhs->second.payload,
                                        *rhs->second.payload)) {
        return ValidationResult::Failure(
            ValidationCode::SEQUENCE_MISMATCH, "sequence",
            "same-sequence full snapshot changed asset contents");
      }
    }
  }

  records_ = std::move(candidate);
  sequence_ = delta.sequence;
  return ValidationResult::Success();
}

RenderAssetDelta RenderAssetRegistry::BuildFullSnapshot() const {
  RenderAssetDelta snapshot;
  snapshot.registry_id = registry_id_;
  snapshot.sequence = sequence_;
  snapshot.full_snapshot = true;
  snapshot.mutations.reserve(records_.size());
  for (const auto &entry : records_) {
    RenderAssetMutation mutation;
    mutation.type = entry.second.live() ? RenderAssetMutationType::UPSERT
                                        : RenderAssetMutationType::DESTROY;
    mutation.asset = entry.second.asset;
    mutation.payload = *entry.second.payload;
    snapshot.mutations.push_back(std::move(mutation));
  }
  return snapshot;
}

const RenderAssetRecord *
RenderAssetRegistry::Find(RenderAssetId id) const noexcept {
  const auto found = records_.find(id);
  return found == records_.end() ? nullptr : &found->second;
}

const RenderAssetRecord *RenderAssetRegistry::Resolve(
    const RenderAssetReference &reference) const noexcept {
  if (!reference.valid()) {
    return nullptr;
  }
  const RenderAssetRecord *record = Find(reference.id);
  return record != nullptr && record->live() && record->asset == reference
             ? record
             : nullptr;
}

const MeshResourceDescriptor *RenderAssetRegistry::ResolveMesh(
    const RenderAssetReference &reference) const noexcept {
  const RenderAssetRecord *record = Resolve(reference);
  return record != nullptr
             ? std::get_if<MeshResourceDescriptor>(record->payload.get())
             : nullptr;
}

const TextureResourceDescriptor *RenderAssetRegistry::ResolveTexture(
    const RenderAssetReference &reference) const noexcept {
  const RenderAssetRecord *record = Resolve(reference);
  return record != nullptr
             ? std::get_if<TextureResourceDescriptor>(record->payload.get())
             : nullptr;
}

const MaterialDescriptor *RenderAssetRegistry::ResolveMaterial(
    const RenderAssetReference &reference) const noexcept {
  const RenderAssetRecord *record = Resolve(reference);
  return record != nullptr
             ? std::get_if<MaterialDescriptor>(record->payload.get())
             : nullptr;
}

const SamplerResourceDescriptor *RenderAssetRegistry::ResolveSampler(
    const RenderAssetReference &reference) const noexcept {
  const RenderAssetRecord *record = Resolve(reference);
  return record != nullptr
             ? std::get_if<SamplerResourceDescriptor>(record->payload.get())
             : nullptr;
}

ValidationResult RenderAssetRegistry::ValidateResolvedDependencies(
    const RecordMap &records) const {
  const auto resolve = [&records](const RenderAssetReference &reference)
      -> const RenderAssetRecord * {
    const auto found = records.find(reference.id);
    return found != records.end() && found->second.live() &&
                   found->second.asset == reference
               ? &found->second
               : nullptr;
  };

  for (const auto &entry : records) {
    const MaterialDescriptor *material =
        std::get_if<MaterialDescriptor>(entry.second.payload.get());
    if (material == nullptr) {
      continue;
    }
    const auto bindings = MaterialBindings(*material);
    for (std::size_t index = 0U; index < bindings.size(); ++index) {
      const TextureBinding &binding = *bindings[index];
      if (!binding.texture.valid()) {
        continue;
      }
      const RenderAssetRecord *texture_record = resolve(binding.texture);
      const RenderAssetRecord *sampler_record = resolve(binding.sampler);
      if (texture_record == nullptr || sampler_record == nullptr) {
        return ValidationResult::Failure(
            ValidationCode::MISSING_REFERENCE, "material.texture_binding",
            "material references a missing, stale, or tombstoned asset",
            index);
      }
      const auto *texture =
          std::get_if<TextureResourceDescriptor>(texture_record->payload.get());
      const auto *sampler =
          std::get_if<SamplerResourceDescriptor>(sampler_record->payload.get());
      if (texture == nullptr || sampler == nullptr) {
        return ValidationResult::Failure(
            ValidationCode::WRONG_ASSET_KIND, "material.texture_binding",
            "material dependency payload has the wrong asset kind", index);
      }
      ValidationResult compatibility = ValidateMaterialTextureCompatibility(
          kMaterialSlots[index], *texture, *sampler);
      if (!compatibility) {
        compatibility.element_index = index;
        return compatibility;
      }
    }
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
