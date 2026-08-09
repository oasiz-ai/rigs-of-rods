/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyLiveMaterialCoordinator.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult
Failure(ValidationCode code, const char *field, const char *detail,
        std::size_t index = (std::numeric_limits<std::size_t>::max)()) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                std::uint64_t &output) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  output = lhs + rhs;
  return true;
}

bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &output) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  output = lhs * rhs;
  return true;
}

bool SameFloat(float lhs, float rhs) noexcept {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  static_assert(sizeof(lhs_bits) == sizeof(lhs), "binary32 is required");
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool SameFloat3(const Float3 &lhs, const Float3 &rhs) noexcept {
  return SameFloat(lhs.x, rhs.x) && SameFloat(lhs.y, rhs.y) &&
         SameFloat(lhs.z, rhs.z);
}

bool SameFloat4(const Float4 &lhs, const Float4 &rhs) noexcept {
  return SameFloat(lhs.x, rhs.x) && SameFloat(lhs.y, rhs.y) &&
         SameFloat(lhs.z, rhs.z) && SameFloat(lhs.w, rhs.w);
}

bool SamePipeline(const Ogre14LegacyPipelineStateInput &lhs,
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
         SameFloat(lhs.constant_depth_bias, rhs.constant_depth_bias) &&
         SameFloat(lhs.slope_scale_depth_bias, rhs.slope_scale_depth_bias) &&
         SameFloat(lhs.iteration_depth_bias, rhs.iteration_depth_bias) &&
         lhs.cull == rhs.cull && lhs.manual_cull == rhs.manual_cull &&
         lhs.alpha_reject == rhs.alpha_reject &&
         lhs.alpha_reject_value == rhs.alpha_reject_value &&
         lhs.alpha_to_coverage == rhs.alpha_to_coverage &&
         lhs.solid_fill == rhs.solid_fill &&
         lhs.pass_iteration_count == rhs.pass_iteration_count;
}

bool SameSampler(const Ogre14LegacySamplerInput &lhs,
                 const Ogre14LegacySamplerInput &rhs) noexcept {
  return lhs.source_revision == rhs.source_revision &&
         lhs.minification == rhs.minification &&
         lhs.magnification == rhs.magnification && lhs.mip == rhs.mip &&
         lhs.address_u == rhs.address_u && lhs.address_v == rhs.address_v &&
         lhs.address_w == rhs.address_w &&
         SameFloat(lhs.mip_lod_bias, rhs.mip_lod_bias) &&
         SameFloat(lhs.minimum_lod, rhs.minimum_lod) &&
         SameFloat(lhs.maximum_lod, rhs.maximum_lod) &&
         lhs.maximum_anisotropy == rhs.maximum_anisotropy &&
         lhs.compare_enabled == rhs.compare_enabled &&
         lhs.compare_operation == rhs.compare_operation &&
         SameFloat4(lhs.border_color, rhs.border_color);
}

bool SameTextureUnit(const Ogre14LegacyTextureUnitInput &lhs,
                     const Ogre14LegacyTextureUnitInput &rhs) noexcept {
  return lhs.texture_key == rhs.texture_key &&
         SameSampler(lhs.sampler, rhs.sampler) &&
         lhs.texture_coordinate_set == rhs.texture_coordinate_set &&
         lhs.named_content == rhs.named_content &&
         lhs.texture_2d == rhs.texture_2d &&
         lhs.frame_count == rhs.frame_count &&
         lhs.has_animated_or_procedural_effect ==
             rhs.has_animated_or_procedural_effect &&
         lhs.projective == rhs.projective &&
         lhs.environment_mapping == rhs.environment_mapping &&
         lhs.compositor == rhs.compositor &&
         lhs.render_target == rhs.render_target &&
         lhs.canonical_color_modulate == rhs.canonical_color_modulate &&
         lhs.canonical_alpha_modulate == rhs.canonical_alpha_modulate &&
         lhs.identity_texture_transform == rhs.identity_texture_transform;
}

bool SameMaterial(const Ogre14LegacyMaterialInput &lhs,
                  const Ogre14LegacyMaterialInput &rhs) noexcept {
  if (lhs.version != rhs.version || lhs.key != rhs.key ||
      lhs.source_revision != rhs.source_revision ||
      lhs.technique_count != rhs.technique_count ||
      lhs.pass_count != rhs.pass_count ||
      lhs.generated_rtss_program != rhs.generated_rtss_program ||
      lhs.has_vertex_program != rhs.has_vertex_program ||
      lhs.has_fragment_program != rhs.has_fragment_program ||
      lhs.has_geometry_program != rhs.has_geometry_program ||
      lhs.has_tessellation_program != rhs.has_tessellation_program ||
      lhs.has_compute_program != rhs.has_compute_program ||
      lhs.base_color_semantic != rhs.base_color_semantic ||
      lhs.lighting_enabled != rhs.lighting_enabled ||
      !SameFloat4(lhs.diffuse_linear, rhs.diffuse_linear) ||
      !SameFloat3(lhs.ambient_linear, rhs.ambient_linear) ||
      !SameFloat3(lhs.specular_linear, rhs.specular_linear) ||
      !SameFloat3(lhs.emissive_linear, rhs.emissive_linear) ||
      !SameFloat(lhs.shininess, rhs.shininess) ||
      !SamePipeline(lhs.pipeline, rhs.pipeline) ||
      lhs.texture_units.size() != rhs.texture_units.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.texture_units.size(); ++index) {
    if (!SameTextureUnit(lhs.texture_units[index], rhs.texture_units[index])) {
      return false;
    }
  }
  return true;
}

template <typename T>
bool SameExactOwner(const std::shared_ptr<const T> &lhs,
                    const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && lhs.get() == rhs.get() &&
         !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

template <typename T>
bool SharesControlBlock(const std::shared_ptr<const T> &lhs,
                        const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool SameMip(const Ogre14LegacyTextureMipInput &lhs,
             const Ogre14LegacyTextureMipInput &rhs) noexcept {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.row_pitch_bytes == rhs.row_pitch_bytes &&
         lhs.slice_pitch_bytes == rhs.slice_pitch_bytes &&
         lhs.bytes == rhs.bytes;
}

bool SameTexture(const Ogre14LegacyTextureInput &lhs,
                 const Ogre14LegacyTextureInput &rhs) noexcept {
  if (lhs.version != rhs.version || lhs.key != rhs.key ||
      lhs.source_revision != rhs.source_revision || lhs.type != rhs.type ||
      lhs.pixel_encoding != rhs.pixel_encoding ||
      lhs.color_role != rhs.color_role ||
      lhs.hardware_gamma_enabled != rhs.hardware_gamma_enabled ||
      lhs.compressed != rhs.compressed ||
      lhs.render_target != rhs.render_target ||
      lhs.generated != rhs.generated || lhs.procedural != rhs.procedural ||
      lhs.width != rhs.width || lhs.height != rhs.height ||
      lhs.mip_levels.size() != rhs.mip_levels.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.mip_levels.size(); ++index) {
    if (!SameMip(lhs.mip_levels[index], rhs.mip_levels[index])) {
      return false;
    }
  }
  return true;
}

bool SameNativeCapture(const Ogre14LegacyNativeMaterialCapture &lhs,
                       const Ogre14LegacyNativeMaterialCapture &rhs) noexcept {
  if (lhs.version != rhs.version || !SameMaterial(lhs.material, rhs.material) ||
      lhs.textures.size() != rhs.textures.size() ||
      !SameExactOwner(lhs.exact_native_material_audit,
                      rhs.exact_native_material_audit)) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.textures.size(); ++index) {
    if (!SameTexture(lhs.textures[index], rhs.textures[index])) {
      return false;
    }
  }
  return true;
}

ValidationResult ValidateObservation(
    const Ogre14LegacyMaterialObservation &observation,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    std::uint64_t &observed_texture_bytes) {
  if (observation.version != kOgre14LegacyMaterialObservationVersion ||
      observation.native_capture.version !=
          kOgre14LegacyNativeAssetExtractorVersion) {
    return Failure(
        ValidationCode::UNSUPPORTED_VERSION, "material_observations.version",
        "unsupported material observation or native capture version");
  }
  if (observation.material_key != observation.native_capture.material.key) {
    return Failure(
        ValidationCode::REVISION_MISMATCH, "material_observations.material_key",
        "observation and native capture identify different materials");
  }
  if (!observation.native_capture.native_material_audit_receipt.Authenticates(
          observation.native_capture.exact_native_material_audit)) {
    return Failure(
        ValidationCode::MISSING_REFERENCE,
        "material_observations.native_material_audit_owner",
        "native material audit owner is missing, replaced, reboxed, or not "
        "authenticated by its capture receipt");
  }
  Ogre14LegacyMaterialPipelineAudit expected_native_audit;
  ValidationResult validation = DeriveOgre14LegacyMaterialPipelineAudit(
      observation.native_capture.material, expected_native_audit);
  if (!validation) {
    return validation;
  }
  if (!EquivalentOgre14LegacyMaterialPipelineAudit(
          expected_native_audit,
          *observation.native_capture.exact_native_material_audit)) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "material_observations.native_material_audit",
        "authenticated native audit disagrees with captured material, texture, "
        "sampler, or pipeline state");
  }
  Ogre14LegacyMaterialSemanticResolution resolution;
  validation = semantic_registry.Resolve(observation.material_key,
                                         translator_configuration, resolution);
  if (!validation) {
    return validation;
  }
  if (!Ogre14LegacyMaterialSemanticResolutionMatchesKey(
          resolution, observation.material_key) ||
      !Ogre14LegacyMaterialSemanticResolutionMatchesKey(
          observation.semantic_resolution, observation.material_key) ||
      !Ogre14LegacyMaterialSemanticResolutionAuthenticates(
          observation.semantic_resolution, resolution) ||
      observation.native_capture.material.base_color_semantic !=
          resolution.native_declaration.base_color_semantic) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_observations.semantic_resolution",
                   "native material capture disagrees with its exact semantic "
                   "declaration");
  }
  validation =
      ValidateOgre14LegacyMaterialInput(observation.native_capture.material);
  if (!validation) {
    return validation;
  }
  if (observation.native_capture.material.texture_units.size() !=
          observation.native_capture.textures.size() ||
      observation.native_capture.textures.size() > 1U) {
    return Failure(
        ValidationCode::SIZE_MISMATCH, "material_observations.textures",
        "native capture must carry exactly its referenced v1 textures");
  }
  std::uint64_t candidate_observed_texture_bytes = 0U;
  for (std::size_t index = 0U;
       index < observation.native_capture.textures.size(); ++index) {
    const Ogre14LegacyTextureInput &texture =
        observation.native_capture.textures[index];
    validation = ValidateOgre14LegacyTextureInput(texture);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    if (texture.type != Ogre14LegacyTextureType::TEXTURE_2D ||
        texture.pixel_encoding != Ogre14LegacyPixelEncoding::RGBA8_BYTES ||
        texture.compressed || texture.render_target || texture.generated ||
        texture.procedural) {
      return Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "material_observations.texture_payload",
          "live native texture capture must be canonical RGBA8 2D data", index);
    }
    std::uint64_t texture_bytes = 0U;
    for (const Ogre14LegacyTextureMipInput &mip : texture.mip_levels) {
      std::uint64_t tight_row_bytes = 0U;
      std::uint64_t tight_slice_bytes = 0U;
      std::uint64_t next_texture_bytes = 0U;
      if (!CheckedMultiply(static_cast<std::uint64_t>(mip.width), 4U,
                           tight_row_bytes) ||
          !CheckedMultiply(tight_row_bytes,
                           static_cast<std::uint64_t>(mip.height),
                           tight_slice_bytes) ||
          tight_slice_bytes > static_cast<std::uint64_t>(
                                  (std::numeric_limits<std::size_t>::max)()) ||
          mip.row_pitch_bytes != tight_row_bytes ||
          mip.slice_pitch_bytes != tight_slice_bytes ||
          mip.bytes.size() != static_cast<std::size_t>(tight_slice_bytes) ||
          !CheckedAdd(texture_bytes, tight_slice_bytes, next_texture_bytes)) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "material_observations.texture_payload",
                       "live native texture mip bytes are not canonical and "
                       "tightly packed",
                       index);
      }
      texture_bytes = next_texture_bytes;
    }
    if (texture_bytes >
        translator_configuration.maximum_decoded_bytes_per_asset) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material_observations.texture_payload",
          "live native texture exceeds the configured per-asset byte cap",
          index);
    }
    std::uint64_t next_observed_texture_bytes = 0U;
    if (!CheckedAdd(candidate_observed_texture_bytes, texture_bytes,
                    next_observed_texture_bytes)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_observations.texture_payload",
                     "live native texture byte accounting overflowed", index);
    }
    candidate_observed_texture_bytes = next_observed_texture_bytes;
    if (texture.key != observation.native_capture.material.texture_units[index]
                           .texture_key ||
        texture.color_role !=
            resolution.native_declaration.texture_color_role) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "material_observations.texture_semantic",
                     "captured texture reference or color role disagrees with "
                     "explicit semantics",
                     index);
    }
  }
  observed_texture_bytes = candidate_observed_texture_bytes;
  return ValidationResult::Success();
}

} // namespace

struct Ogre14LegacyPreparedMaterialFrame::State final {
  std::uint32_t version = kOgre14LegacyPreparedMaterialFrameVersion;
  std::shared_ptr<const Ogre14LegacyTranslatedFrame> translated_frame;
  std::vector<Ogre14LegacyPreparedMaterial> materials;
};

Ogre14LegacyPreparedMaterialFrame::Ogre14LegacyPreparedMaterialFrame(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyPreparedMaterialFrame::initialized() const noexcept {
  return state_ != nullptr && state_->translated_frame != nullptr;
}

std::uint32_t Ogre14LegacyPreparedMaterialFrame::version() const noexcept {
  return state_ != nullptr ? state_->version : 0U;
}

const Ogre14LegacyTranslatedFrame *
Ogre14LegacyPreparedMaterialFrame::translated_frame() const noexcept {
  return state_ != nullptr ? state_->translated_frame.get() : nullptr;
}

const std::vector<Ogre14LegacyPreparedMaterial> &
Ogre14LegacyPreparedMaterialFrame::materials() const noexcept {
  static const std::vector<Ogre14LegacyPreparedMaterial> empty;
  return state_ != nullptr ? state_->materials : empty;
}

bool Ogre14LegacyPreparedMaterialFrame::SharesImmutableStateWith(
    const Ogre14LegacyPreparedMaterialFrame &other) const noexcept {
  return state_ != nullptr && state_.get() == other.state_.get();
}

struct Ogre14LegacyLiveMaterialCoordinator::PendingFrame final {
  Ogre14LegacyAssetTranslatorCommittableTransaction transaction;
  Ogre14LegacyPreparedMaterialFrame prepared;
};

Ogre14LegacyLiveMaterialCoordinator::~Ogre14LegacyLiveMaterialCoordinator() =
    default;

Ogre14LegacyLiveMaterialCoordinator::Ogre14LegacyLiveMaterialCoordinator(
    Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration,
    Ogre14LegacyMaterialSemanticRegistry semantic_registry,
    std::unique_ptr<Ogre14LegacyAssetTranslator> translator) noexcept
    : configuration_(std::move(configuration)),
      semantic_registry_(std::move(semantic_registry)),
      translator_(std::move(translator)) {}

std::uint64_t
Ogre14LegacyLiveMaterialCoordinator::source_sequence() const noexcept {
  return translator_ != nullptr ? translator_->source_sequence() : 0U;
}

std::uint64_t
Ogre14LegacyLiveMaterialCoordinator::catalog_sequence() const noexcept {
  return translator_ != nullptr ? translator_->catalog_sequence() : 0U;
}

bool Ogre14LegacyLiveMaterialCoordinator::has_pending_frame() const noexcept {
  return pending_ != nullptr;
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::ResolveMaterialSemantics(
    const Ogre14LegacyAssetKey &material_key,
    Ogre14LegacyMaterialSemanticResolution &output) const {
  if (translator_ == nullptr || !semantic_registry_.initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.state",
                   "coordinator has no translator or semantic registry");
  }
  return semantic_registry_.Resolve(material_key, configuration_.translator,
                                    output);
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::PrepareFrame(
    std::uint64_t source_sequence,
    const std::vector<Ogre14LegacyMaterialObservation> &observations,
    Ogre14LegacyPreparedMaterialFrame &output,
    IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector) try {
  if (pending_ != nullptr) {
    return Failure(
        ValidationCode::SEQUENCE_MISMATCH, "material_coordinator.pending",
        "the preceding material frame must be committed or discarded");
  }
  if (translator_ == nullptr || !semantic_registry_.initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.state",
                   "coordinator has no translator or semantic registry");
  }
  if (observations.size() > configuration_.maximum_material_observations) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_observations.count",
                   "material observation count exceeds the configured cap");
  }
  if (source_sequence == 0U ||
      translator_->source_sequence() ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      source_sequence != translator_->source_sequence() + 1U) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "material_coordinator.source_sequence",
                   "material frame source sequence must advance exactly once");
  }

  ValidationResult validation = ValidationResult::Success();

  struct IndexedObservation final {
    std::string material_stable_key;
    const Ogre14LegacyMaterialObservation *observation = nullptr;
  };
  std::vector<IndexedObservation> ordered;
  ordered.reserve(observations.size());
  std::uint64_t observed_texture_bytes = 0U;
  for (std::size_t index = 0U; index < observations.size(); ++index) {
    std::uint64_t observation_texture_bytes = 0U;
    validation = ValidateObservation(observations[index], semantic_registry_,
                                     configuration_.translator,
                                     observation_texture_bytes);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    std::uint64_t next_observed_texture_bytes = 0U;
    if (!CheckedAdd(observed_texture_bytes, observation_texture_bytes,
                    next_observed_texture_bytes) ||
        next_observed_texture_bytes >
            configuration_.translator.maximum_decoded_bytes_per_frame) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material_observations.texture_bytes",
          "observed native texture bytes exceed the configured frame cap",
          index);
    }
    // Charge every caller-supplied payload before canonicalization. Sharing one
    // authenticated audit owner does not make copied mip vectors disappear from
    // caller memory or waive the frame's resource-admission ceiling.
    observed_texture_bytes = next_observed_texture_bytes;
    IndexedObservation indexed;
    indexed.observation = &observations[index];
    validation = BuildOgre14LegacyStableAssetKey(
        RenderAssetKind::MATERIAL, observations[index].material_key,
        indexed.material_stable_key);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    ordered.push_back(std::move(indexed));
    if (index == 0U && fault_injector != nullptr) {
      fault_injector->AtFaultPoint(
          Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
              AFTER_FIRST_OBSERVATION);
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const IndexedObservation &lhs,
               const IndexedObservation &rhs) noexcept {
              return lhs.material_stable_key < rhs.material_stable_key;
            });
  std::vector<IndexedObservation> canonical_observations;
  canonical_observations.reserve(ordered.size());
  using NativeAuditOwner =
      std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>;
  std::map<NativeAuditOwner, std::string, std::owner_less<NativeAuditOwner>>
      material_key_by_native_audit_owner;
  for (std::size_t index = 0U; index < ordered.size(); ++index) {
    const IndexedObservation &indexed = ordered[index];
    if (indexed.observation == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_observations.shared_material",
                     "ordered material observation is missing", index);
    }
    const NativeAuditOwner &native_audit =
        indexed.observation->native_capture.exact_native_material_audit;
    const auto owner = material_key_by_native_audit_owner.find(native_audit);
    if (owner != material_key_by_native_audit_owner.end() &&
        owner->second != indexed.material_stable_key) {
      return Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "material_observations.native_material_audit_owner",
          "one authenticated native audit owner cannot identify distinct exact "
          "materials",
          index);
    }
    if (owner == material_key_by_native_audit_owner.end()) {
      material_key_by_native_audit_owner.emplace(native_audit,
                                                 indexed.material_stable_key);
    }
    if (!canonical_observations.empty() &&
        canonical_observations.back().material_stable_key ==
            indexed.material_stable_key) {
      const Ogre14LegacyNativeMaterialCapture &canonical_capture =
          canonical_observations.back().observation->native_capture;
      if (!SameNativeCapture(canonical_capture,
                             indexed.observation->native_capture)) {
        return Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "material_observations.shared_material",
            "repeated exact material observations must share one bit-exact "
            "authenticated native audit owner",
            index);
      }
      continue;
    }
    canonical_observations.push_back(indexed);
  }
  ordered = std::move(canonical_observations);

  std::map<std::string, const Ogre14LegacyTextureInput *, std::less<>> textures;
  std::vector<const Ogre14LegacyMaterialInput *> identity_materials;
  identity_materials.reserve(ordered.size());
  std::uint64_t sampler_count = 0U;
  for (const IndexedObservation &indexed : ordered) {
    const Ogre14LegacyNativeMaterialCapture &capture =
        indexed.observation->native_capture;
    identity_materials.push_back(&capture.material);
    std::uint64_t next_sampler_count = 0U;
    if (!CheckedAdd(
            sampler_count,
            static_cast<std::uint64_t>(capture.material.texture_units.size()),
            next_sampler_count)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_observations.sampler_count",
                     "derived native sampler count overflowed");
    }
    sampler_count = next_sampler_count;
    for (const Ogre14LegacyTextureInput &texture : capture.textures) {
      std::string stable_key;
      validation = BuildOgre14LegacyStableAssetKey(RenderAssetKind::TEXTURE,
                                                   texture.key, stable_key);
      if (!validation) {
        return validation;
      }
      const auto existing = textures.find(stable_key);
      if (existing != textures.end()) {
        if (existing->second == nullptr ||
            !SameTexture(*existing->second, texture)) {
          return Failure(
              ValidationCode::REVISION_MISMATCH,
              "material_observations.shared_texture",
              "one exact texture key has conflicting captured state");
        }
      } else {
        if (textures.size() >=
            configuration_.translator.maximum_texture_inputs_per_frame) {
          return Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              "material_observations.texture_count",
              "unique native texture count exceeds the configured cap");
        }
        textures.emplace(std::move(stable_key), &texture);
      }
    }
  }
  std::uint64_t derived_live_asset_count = 0U;
  if (!CheckedAdd(static_cast<std::uint64_t>(textures.size()), sampler_count,
                  derived_live_asset_count) ||
      !CheckedAdd(derived_live_asset_count,
                  static_cast<std::uint64_t>(ordered.size()),
                  derived_live_asset_count) ||
      derived_live_asset_count >
          configuration_.translator.maximum_live_assets_per_frame) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_observations.live_asset_count",
                   "derived native asset count exceeds the configured cap");
  }
  std::vector<const Ogre14LegacyTextureInput *> identity_textures;
  identity_textures.reserve(textures.size());
  for (const auto &entry : textures) {
    if (entry.second == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_observations.textures",
                     "canonical native texture index contains no input");
    }
    identity_textures.push_back(entry.second);
  }

  Ogre14LegacyAssetIdentityFrameView identity_view;
  identity_view.texture_inputs =
      identity_textures.empty() ? nullptr : identity_textures.data();
  identity_view.texture_input_count = identity_textures.size();
  identity_view.material_inputs =
      identity_materials.empty() ? nullptr : identity_materials.data();
  identity_view.material_input_count = identity_materials.size();
  validation = translator_->PreflightLifetimeAdmission(identity_view);
  if (!validation) {
    return validation;
  }

  Ogre14LegacyAssetFrameInput frame_input;
  frame_input.source_sequence = source_sequence;
  frame_input.materials.reserve(identity_materials.size());
  for (const Ogre14LegacyMaterialInput *material : identity_materials) {
    if (material == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_observations.materials",
                     "canonical native material index contains no input");
    }
    frame_input.materials.push_back(*material);
  }
  frame_input.textures.reserve(identity_textures.size());
  for (const Ogre14LegacyTextureInput *texture : identity_textures) {
    if (texture == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_observations.textures",
                     "canonical native texture index contains no input");
    }
    frame_input.textures.push_back(*texture);
  }

  auto candidate_pending = std::make_unique<PendingFrame>();
  validation =
      translator_->BeginCommittableTransaction(candidate_pending->transaction);
  if (!validation) {
    return validation;
  }

  Ogre14LegacyTranslatedFrame translated;
  validation = candidate_pending->transaction.candidate()->Translate(
      frame_input, translated);
  if (!validation) {
    return validation;
  }
  validation =
      candidate_pending->transaction.candidate()->BuildFullSnapshot(translated);
  if (!validation) {
    return validation;
  }

  std::vector<Ogre14LegacyMaterialClosureRequest> requests;
  requests.reserve(ordered.size());
  for (const IndexedObservation &indexed : ordered) {
    Ogre14LegacyMaterialClosureRequest request;
    validation = MakeOgre14LegacyMaterialClosureRequest(
        translated, indexed.observation->material_key, request);
    if (!validation) {
      return validation;
    }
    requests.push_back(std::move(request));
  }
  Ogre14LegacyMaterialClosureBatch batch;
  validation =
      ResolveOgre14LegacyMaterialClosureBatch(translated, requests, batch);
  if (!validation) {
    return validation;
  }

  auto prepared_state =
      std::make_shared<Ogre14LegacyPreparedMaterialFrame::State>();
  prepared_state->translated_frame =
      std::make_shared<const Ogre14LegacyTranslatedFrame>(
          std::move(translated));
  prepared_state->materials.reserve(batch.closures.size());
  for (Ogre14LegacyMaterialClosure &closure : batch.closures) {
    if (closure.asset_keys.empty()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_closures.material_key",
                     "resolved closure omitted its exact material key");
    }
    std::string material_stable_key;
    validation = BuildOgre14LegacyStableAssetKey(RenderAssetKind::MATERIAL,
                                                 closure.asset_keys.back(),
                                                 material_stable_key);
    if (!validation) {
      return validation;
    }
    const auto observation = std::lower_bound(
        ordered.begin(), ordered.end(), material_stable_key,
        [](const IndexedObservation &lhs, const std::string &rhs) noexcept {
          return lhs.material_stable_key < rhs;
        });
    if (observation == ordered.end() ||
        observation->material_stable_key != material_stable_key ||
        observation->observation == nullptr ||
        closure.material_audit == nullptr ||
        observation->observation->native_capture.exact_native_material_audit ==
            nullptr) {
      return Failure(
          ValidationCode::MISSING_REFERENCE,
          "material_closures.native_material_audit",
          "resolved closure has no corresponding independently captured native "
          "audit owner");
    }
    const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>
        &native_audit = observation->observation->native_capture
                            .exact_native_material_audit;
    if (!EquivalentOgre14LegacyMaterialPipelineAudit(*native_audit,
                                                     *closure.material_audit)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "material_closures.native_material_audit",
                     "translated closure audit disagrees bit-exactly with "
                     "native material, "
                     "texture, sampler, or pipeline capture");
    }
    if (SharesControlBlock(native_audit, closure.material_audit)) {
      return Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "material_closures.native_material_audit_owner",
          "translated closure audit owner was laundered as native capture");
    }
    if (fault_injector != nullptr) {
      fault_injector->AtFaultPoint(
          Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
              AFTER_NATIVE_AUDIT_MATCH);
    }
    Ogre14LegacyPreparedMaterial material;
    material.material_key = closure.asset_keys.back();
    material.native_material_audit = native_audit;
    material.closure =
        std::make_shared<const Ogre14LegacyMaterialClosure>(std::move(closure));
    prepared_state->materials.push_back(std::move(material));
  }
  Ogre14LegacyPreparedMaterialFrame prepared(
      std::shared_ptr<const Ogre14LegacyPreparedMaterialFrame::State>(
          std::move(prepared_state)));
  if (fault_injector != nullptr) {
    fault_injector->AtFaultPoint(Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
                                     BEFORE_PREPARED_FRAME_PUBLISH);
  }
  static_assert(std::is_nothrow_move_assignable<decltype(pending_)>::value,
                "publishing the pending material transaction must not throw");
  candidate_pending->prepared = prepared;
  output = std::move(prepared);
  pending_ = std::move(candidate_pending);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.allocation",
                 "allocation failed before the material frame was published");
} catch (...) {
  return Failure(
      ValidationCode::UNSUPPORTED_FEATURE, "material_coordinator.exception",
      "unexpected exception before the material frame was published");
}

Ogre14LegacyPreparedMaterialCommitResult
Ogre14LegacyLiveMaterialCoordinator::CommitPreparedFrameAfterAcceptedExposure(
    const Ogre14LegacyPreparedMaterialFrame &accepted_frame) noexcept {
  if (pending_ == nullptr) {
    return Ogre14LegacyPreparedMaterialCommitResult::NO_PENDING_FRAME;
  }
  if (!pending_->prepared.SharesImmutableStateWith(accepted_frame)) {
    return Ogre14LegacyPreparedMaterialCommitResult::PREPARED_FRAME_MISMATCH;
  }
  const Ogre14LegacyAssetTranslatorExclusiveCommitResult result =
      pending_->transaction.CommitAfterAcceptedExposure();
  pending_.reset();
  return result == Ogre14LegacyAssetTranslatorExclusiveCommitResult::COMMITTED
             ? Ogre14LegacyPreparedMaterialCommitResult::COMMITTED
             : Ogre14LegacyPreparedMaterialCommitResult::
                   TRANSLATOR_INVARIANT_BROKEN;
}

void Ogre14LegacyLiveMaterialCoordinator::DiscardPreparedFrame() noexcept {
  pending_.reset();
}

ValidationResult ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration) {
  if (configuration.version != kOgre14LegacyLiveMaterialCoordinatorVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "material_coordinator.configuration.version",
                   "unsupported live material coordinator version");
  }
  ValidationResult validation =
      ValidateOgre14LegacyAssetTranslatorConfiguration(
          configuration.translator);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgre14LegacyAssetTranslatorTransactionConfiguration(
      configuration.transaction);
  if (!validation) {
    return validation;
  }
  if (configuration.maximum_material_observations == 0U ||
      configuration.maximum_material_observations >
          configuration.translator.maximum_material_inputs_per_frame ||
      configuration.maximum_material_observations >
          kDefaultOgre14LegacyMaximumMaterialObservations) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material_coordinator.configuration.observations",
        "material observation cap is zero or exceeds translator limits");
  }
  return ValidationResult::Success();
}

ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector) try {
  ValidationResult validation =
      ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  if (!semantic_registry.initialized()) {
    return Failure(
        ValidationCode::MISSING_REFERENCE,
        "material_coordinator.semantic_registry",
        "live coordinator requires an initialized semantic registry");
  }
  auto translator = std::make_unique<Ogre14LegacyAssetTranslator>(
      configuration.translator, configuration.transaction,
      translator_fault_injector);
  auto candidate = std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator>(
      new Ogre14LegacyLiveMaterialCoordinator(configuration, semantic_registry,
                                              std::move(translator)));
  output = std::move(candidate);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.allocation",
                 "allocation failed before the coordinator was published");
} catch (...) {
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "material_coordinator.exception",
                 "unexpected exception before the coordinator was published");
}

const Ogre14LegacyPreparedMaterial *FindOgre14LegacyPreparedMaterial(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept {
  try {
    const Ogre14LegacyTranslatedFrame *translated_frame =
        frame.translated_frame();
    if (frame.version() != kOgre14LegacyPreparedMaterialFrameVersion ||
        translated_frame == nullptr ||
        material_key.exact_resource_group.empty() ||
        material_key.exact_name.empty()) {
      return nullptr;
    }
    for (const Ogre14LegacyPreparedMaterial &material : frame.materials()) {
      if (material.material_key == material_key &&
          material.native_material_audit != nullptr &&
          material.closure != nullptr &&
          material.closure->material_audit != nullptr &&
          !SharesControlBlock(material.native_material_audit,
                              material.closure->material_audit) &&
          ValidateOgre14LegacyMaterialPipelineAudit(
              *material.native_material_audit)
              .ok() &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              *material.native_material_audit,
              *material.closure->material_audit) &&
          ValidateOgre14LegacyMaterialClosureForFrame(
              *translated_frame, *material.closure, material_key)
              .ok()) {
        return &material;
      }
    }
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

const Ogre14LegacyMaterialClosure *FindOgre14LegacyPreparedMaterialClosure(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept {
  const Ogre14LegacyPreparedMaterial *material =
      FindOgre14LegacyPreparedMaterial(frame, material_key);
  return material != nullptr ? material->closure.get() : nullptr;
}

static_assert(std::is_nothrow_destructible<
                  Ogre14LegacyAssetTranslatorCommittableTransaction>::value,
              "discarding an unaccepted material transaction must not throw");
static_assert(
    std::is_nothrow_move_assignable<Ogre14LegacyPreparedMaterialFrame>::value,
    "publishing a prepared material frame must not throw");
static_assert(
    std::is_nothrow_copy_assignable<Ogre14LegacyPreparedMaterialFrame>::value,
    "retaining the exact accepted material frame must not throw");
static_assert(
    std::is_nothrow_default_constructible<
        std::vector<Ogre14LegacyPreparedMaterial>>::value,
    "the immutable empty material view must not throw during construction");
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
              "material byte accounting requires size_t to fit in uint64_t");

} // namespace RoR::Render
