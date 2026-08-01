/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetRegistry.h"

#include <array>
#include <limits>
#include <memory>
#include <utility>

namespace RoR::Render {
namespace {

bool EqualMip(const TextureMipLevelDescriptor &lhs,
              const TextureMipLevelDescriptor &rhs) noexcept {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.row_pitch_bytes == rhs.row_pitch_bytes &&
         lhs.layer_pitch_bytes == rhs.layer_pitch_bytes &&
         lhs.bytes == rhs.bytes;
}

bool EqualMesh(const MeshResourceDescriptor &lhs,
               const MeshResourceDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.debug_name == rhs.debug_name &&
         lhs.topology == rhs.topology && lhs.index_format == rhs.index_format &&
         lhs.topology_revision == rhs.topology_revision &&
         lhs.dynamic == rhs.dynamic && lhs.local_bounds == rhs.local_bounds &&
         lhs.positions == rhs.positions && lhs.normals == rhs.normals &&
         lhs.tangents == rhs.tangents && lhs.velocities == rhs.velocities &&
         lhs.texture_coordinates_0 == rhs.texture_coordinates_0 &&
         lhs.texture_coordinates_1 == rhs.texture_coordinates_1 &&
         lhs.colors == rhs.colors && lhs.indices == rhs.indices;
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
         lhs.mip_lod_bias == rhs.mip_lod_bias &&
         lhs.minimum_lod == rhs.minimum_lod &&
         lhs.maximum_lod == rhs.maximum_lod &&
         lhs.anisotropy_enabled == rhs.anisotropy_enabled &&
         lhs.maximum_anisotropy == rhs.maximum_anisotropy &&
         lhs.compare_enabled == rhs.compare_enabled &&
         lhs.compare_operation == rhs.compare_operation &&
         lhs.border_color == rhs.border_color;
}

bool EqualBinding(const TextureBinding &lhs,
                  const TextureBinding &rhs) noexcept {
  return lhs.texture == rhs.texture && lhs.sampler == rhs.sampler &&
         lhs.texture_coordinate_set == rhs.texture_coordinate_set &&
         lhs.scale == rhs.scale && lhs.offset == rhs.offset &&
         lhs.rotation_radians == rhs.rotation_radians;
}

bool EqualMaterial(const MaterialDescriptor &lhs,
                   const MaterialDescriptor &rhs) noexcept {
  return lhs.version == rhs.version && lhs.debug_name == rhs.debug_name &&
         lhs.model == rhs.model && lhs.alpha_mode == rhs.alpha_mode &&
         lhs.double_sided == rhs.double_sided &&
         lhs.base_color_factor == rhs.base_color_factor &&
         lhs.metallic_factor == rhs.metallic_factor &&
         lhs.roughness_factor == rhs.roughness_factor &&
         lhs.normal_scale == rhs.normal_scale &&
         lhs.occlusion_strength == rhs.occlusion_strength &&
         lhs.emissive_factor == rhs.emissive_factor &&
         lhs.emissive_strength == rhs.emissive_strength &&
         lhs.alpha_cutoff == rhs.alpha_cutoff &&
         lhs.index_of_refraction == rhs.index_of_refraction &&
         EqualBinding(lhs.base_color_texture, rhs.base_color_texture) &&
         EqualBinding(lhs.metallic_roughness_texture,
                      rhs.metallic_roughness_texture) &&
         EqualBinding(lhs.normal_texture, rhs.normal_texture) &&
         EqualBinding(lhs.occlusion_texture, rhs.occlusion_texture) &&
         EqualBinding(lhs.emissive_texture, rhs.emissive_texture);
}

ValidationResult ValidatePayload(const RenderAssetMutation &mutation,
                                 std::size_t index) {
  const RenderAssetKind payload_kind = RenderAssetPayloadKind(mutation.payload);
  if (mutation.type == RenderAssetMutationType::DESTROY) {
    if (payload_kind != RenderAssetKind::INVALID) {
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

std::array<const TextureBinding *, 5U>
MaterialBindings(const MaterialDescriptor &material) noexcept {
  return {{&material.base_color_texture,
           &material.metallic_roughness_texture,
           &material.normal_texture,
           &material.occlusion_texture,
           &material.emissive_texture}};
}

const std::array<MaterialTextureSlot, 5U> kMaterialSlots{{
    MaterialTextureSlot::BASE_COLOR,
    MaterialTextureSlot::METALLIC_ROUGHNESS,
    MaterialTextureSlot::NORMAL,
    MaterialTextureSlot::OCCLUSION,
    MaterialTextureSlot::EMISSIVE,
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

bool EquivalentRenderAssetPayload(const RenderAssetPayload &lhs,
                                  const RenderAssetPayload &rhs) noexcept {
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
  return EqualSampler(std::get<SamplerResourceDescriptor>(lhs),
                      std::get<SamplerResourceDescriptor>(rhs));
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
