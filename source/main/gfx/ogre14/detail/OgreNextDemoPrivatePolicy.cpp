/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoPrivatePolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace RoR::Gfx::Detail {
namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

Render::ValidationResult
Failure(Render::ValidationCode code, const char *field, const char *detail,
        std::size_t index = Render::ValidationResult::kNoElement) {
  return Render::ValidationResult::Failure(code, field, detail, index);
}

std::uint32_t CompleteMipCount(std::uint32_t width,
                               std::uint32_t height) noexcept {
  std::uint32_t count = 1U;
  while (width > 1U || height > 1U) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
    ++count;
  }
  return count;
}

double DecodeSrgbByte(std::uint8_t encoded_byte) {
  const double encoded = static_cast<double>(encoded_byte) / 255.0;
  if (encoded <= 0.04045) {
    return encoded / 12.92;
  }
  return std::pow((encoded + 0.055) / 1.055, 2.4);
}

std::uint8_t EncodeLinearSrgbByte(double linear) {
  const double encoded = linear <= 0.0031308
                             ? linear * 12.92
                             : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
  const double scaled = (std::clamp)(encoded, 0.0, 1.0) * 255.0;
  // All inputs are finite decoded bytes, so floor(x + 0.5) is an exact,
  // deterministic round-to-nearest rule with ties resolved upward.
  return static_cast<std::uint8_t>(std::floor(scaled + 0.5));
}

std::size_t SaturatingAdd(std::size_t lhs, std::size_t rhs) noexcept {
  const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
  return lhs > maximum - rhs ? maximum : lhs + rhs;
}

bool HasZeroGpuReadbacks(
    const OgreNextDemoTextureSourceCounters &counters) noexcept {
  return counters.gpu_readbacks == 0U &&
         counters.authenticated_gpu_readbacks == 0U &&
         counters.unauthenticated_gpu_readbacks == 0U;
}

} // namespace

bool IsOgreNextDemoAuthenticatedTextureSourceMode(
    OgreNextDemoTextureSourceMode mode) noexcept {
  return mode == OgreNextDemoTextureSourceMode::
                     AUTHENTICATED_ARCHIVE_SOURCE_BYTES ||
         mode == OgreNextDemoTextureSourceMode::
                     AUTHENTICATED_GENERATED_SOURCE_BYTES;
}

Render::ValidationResult RecordOgreNextDemoTextureSourceDecode(
    OgreNextDemoTextureSourceMode mode,
    OgreNextDemoTextureSourceCounters &counters) {
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source-byte accounting cannot follow a GPU readback");
  }
  OgreNextDemoTextureSourceCounters candidate = counters;
  switch (mode) {
  case OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES:
    candidate.authenticated_archive_source_decodes =
        SaturatingAdd(candidate.authenticated_archive_source_decodes, 1U);
    candidate.authenticated_source_decodes =
        SaturatingAdd(candidate.authenticated_source_decodes, 1U);
    break;
  case OgreNextDemoTextureSourceMode::AUTHENTICATED_GENERATED_SOURCE_BYTES:
    candidate.authenticated_generated_source_decodes =
        SaturatingAdd(candidate.authenticated_generated_source_decodes, 1U);
    candidate.authenticated_source_decodes =
        SaturatingAdd(candidate.authenticated_source_decodes, 1U);
    break;
  case OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES:
    candidate.ordinary_observed_source_decodes =
        SaturatingAdd(candidate.ordinary_observed_source_decodes, 1U);
    break;
  default:
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.source_accounting.mode",
                   "texture source mode is invalid");
  }
  counters = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult RecordOgreNextDemoTextureSourceCacheHit(
    OgreNextDemoTextureSourceCounters &counters) {
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source cache accounting cannot follow a GPU readback");
  }
  counters.source_cache_hits = SaturatingAdd(counters.source_cache_hits, 1U);
  return Render::ValidationResult::Success();
}

Render::ValidationResult RecordOgreNextDemoTextureProjectionExclusion(
    OgreNextDemoTextureProjectionExclusion exclusion,
    OgreNextDemoTextureSourceCounters &counters) {
  const std::size_t index = static_cast<std::size_t>(exclusion);
  if (exclusion == OgreNextDemoTextureProjectionExclusion::NONE ||
      index >= kOgreNextDemoTextureProjectionExclusionCount) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.source_accounting.exclusion",
                   "texture projection exclusion is invalid or empty");
  }
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source exclusion accounting cannot follow a GPU readback");
  }
  OgreNextDemoTextureSourceCounters candidate = counters;
  candidate.source_exclusions = SaturatingAdd(candidate.source_exclusions, 1U);
  candidate.exclusions_by_reason[index] =
      SaturatingAdd(candidate.exclusions_by_reason[index], 1U);
  if (exclusion ==
          OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED ||
      exclusion ==
          OgreNextDemoTextureProjectionExclusion::UNSUPPORTED_SOURCE_CONTAINER) {
    candidate.source_decode_rejections =
        SaturatingAdd(candidate.source_decode_rejections, 1U);
  }
  counters = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult AccumulateOgreNextDemoTextureSourceCounters(
    const OgreNextDemoTextureSourceCounters &increment,
    OgreNextDemoTextureSourceCounters &total) {
  if (!HasZeroGpuReadbacks(increment) || !HasZeroGpuReadbacks(total)) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.source_accounting.gpu_readbacks",
        "material texture capture observed a forbidden GPU readback");
  }
  OgreNextDemoTextureSourceCounters candidate = total;
  candidate.authenticated_archive_source_decodes =
      SaturatingAdd(candidate.authenticated_archive_source_decodes,
                    increment.authenticated_archive_source_decodes);
  candidate.authenticated_generated_source_decodes =
      SaturatingAdd(candidate.authenticated_generated_source_decodes,
                    increment.authenticated_generated_source_decodes);
  candidate.ordinary_observed_source_decodes =
      SaturatingAdd(candidate.ordinary_observed_source_decodes,
                    increment.ordinary_observed_source_decodes);
  candidate.source_cache_hits =
      SaturatingAdd(candidate.source_cache_hits, increment.source_cache_hits);
  candidate.source_decode_rejections = SaturatingAdd(
      candidate.source_decode_rejections, increment.source_decode_rejections);
  candidate.source_exclusions =
      SaturatingAdd(candidate.source_exclusions, increment.source_exclusions);
  for (std::size_t index = 0U; index < candidate.exclusions_by_reason.size();
       ++index) {
    candidate.exclusions_by_reason[index] =
        SaturatingAdd(candidate.exclusions_by_reason[index],
                      increment.exclusions_by_reason[index]);
  }
  candidate.authenticated_source_decodes =
      SaturatingAdd(candidate.authenticated_source_decodes,
                    increment.authenticated_source_decodes);
  candidate.projections =
      SaturatingAdd(candidate.projections, increment.projections);
  total = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult ClassifyOgreNextDemoTextureProjectionEligibility(
    const OgreNextDemoTextureEligibilityObservation &observation,
    OgreNextDemoTextureProjectionExclusion &output) {
  OgreNextDemoTextureProjectionExclusion candidate =
      OgreNextDemoTextureProjectionExclusion::NONE;
  if (!observation.source_available) {
    candidate = OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
  } else if (observation.manually_loaded) {
    candidate = OgreNextDemoTextureProjectionExclusion::MANUAL_OR_PROCEDURAL;
  } else if (observation.render_target) {
    candidate = OgreNextDemoTextureProjectionExclusion::RENDER_TARGET;
  } else if (!observation.texture_2d) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_2D;
  } else if (!observation.unit_depth) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_UNIT_DEPTH;
  } else if (!observation.unit_face_count) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_UNIT_FACE_COUNT;
  } else if (!observation.dimensions_in_range) {
    candidate = OgreNextDemoTextureProjectionExclusion::DIMENSION_OUT_OF_RANGE;
  }
  output = candidate;
  return Render::ValidationResult::Success();
}

Render::ValidationResult
BuildOgreNextDemoCachedProjectionPublicationTransaction(
    const std::vector<OgreNextDemoCachedProjectionPublicationInput>
        &projections,
    const std::vector<OgreNextDemoCachedTexturePublicationInput> &textures,
    const std::vector<OgreNextDemoCachedSamplerPublicationInput> &samplers,
    const std::vector<std::string> &used_projection_keys,
    IOgreNextDemoTexturePublicationBatchValidator &validator,
    OgreNextDemoCachedProjectionPublicationTransaction &output) {
  std::map<std::string, const OgreNextDemoCachedProjectionPublicationInput *,
           std::less<>>
      catalog_by_key;
  std::map<std::uint64_t, std::string> projection_keys_by_material_id;
  for (std::size_t index = 0U; index < projections.size(); ++index) {
    const OgreNextDemoCachedProjectionPublicationInput &input =
        projections[index];
    if (input.projection_key.empty() || input.texture_key.empty() ||
        input.sampler_key.empty() || input.material_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.projection",
                     "cached projection publication identity is empty or zero",
                     index);
    }
    if (!catalog_by_key.emplace(input.projection_key, &input).second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.projection_key",
                     "cached projection key is duplicated", index);
    }
    const auto material_identity = projection_keys_by_material_id.emplace(
        input.material_source_id, input.projection_key);
    if (!material_identity.second &&
        material_identity.first->second != input.projection_key) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.material_id",
                     "distinct cached projections share one material source ID",
                     index);
    }
  }

  std::map<std::string, const OgreNextDemoCachedTexturePublicationInput *,
           std::less<>>
      textures_by_key;
  std::map<std::uint64_t, std::string> texture_keys_by_id;
  for (std::size_t index = 0U; index < textures.size(); ++index) {
    const OgreNextDemoCachedTexturePublicationInput &input = textures[index];
    if (input.texture_key.empty() || input.texture_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.texture",
                     "cached texture publication identity is empty or zero",
                     index);
    }
    if (!IsOgreNextDemoAuthenticatedTextureSourceMode(input.source_mode) &&
        input.source_mode !=
            OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES) {
      return Failure(Render::ValidationCode::INVALID_ENUM,
                     "ogre_next_demo.material.publication.texture_mode",
                     "cached texture source mode is invalid", index);
    }
    if (!textures_by_key.emplace(input.texture_key, &input).second ||
        !texture_keys_by_id.emplace(input.texture_source_id, input.texture_key)
             .second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.texture",
                     "cached texture key or source ID is duplicated", index);
    }
  }

  std::map<std::string, const OgreNextDemoCachedSamplerPublicationInput *,
           std::less<>>
      samplers_by_key;
  std::map<std::uint64_t, std::string> sampler_keys_by_id;
  for (std::size_t index = 0U; index < samplers.size(); ++index) {
    const OgreNextDemoCachedSamplerPublicationInput &input = samplers[index];
    if (input.sampler_key.empty() || input.sampler_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.sampler",
                     "cached sampler publication identity is empty or zero",
                     index);
    }
    if (!samplers_by_key.emplace(input.sampler_key, &input).second ||
        !sampler_keys_by_id.emplace(input.sampler_source_id, input.sampler_key)
             .second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.sampler",
                     "cached sampler key or source ID is duplicated", index);
    }
  }

  for (std::size_t index = 0U; index < projections.size(); ++index) {
    const OgreNextDemoCachedProjectionPublicationInput &input =
        projections[index];
    if (textures_by_key.find(input.texture_key) == textures_by_key.end() ||
        samplers_by_key.find(input.sampler_key) == samplers_by_key.end()) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.dependency",
                     "cached projection texture or sampler owner is absent",
                     index);
    }
  }

  std::map<std::string, bool, std::less<>> used_keys;
  for (std::size_t index = 0U; index < used_projection_keys.size(); ++index) {
    const std::string &key = used_projection_keys[index];
    if (key.empty() || catalog_by_key.find(key) == catalog_by_key.end()) {
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.publication.used_projection",
          "frame-reachable projection is absent from the frozen cache", index);
    }
    if (!used_keys.emplace(key, true).second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.used_projection",
                     "frame-reachable projection key is duplicated", index);
    }
  }

  OgreNextDemoCachedProjectionPublicationTransaction candidate;
  candidate.owner_catalog.reserve(projections.size());
  candidate.frame_root_material_source_ids.reserve(used_keys.size());
  std::map<std::string, bool, std::less<>> observed_authenticated_textures;
  std::map<std::string, bool, std::less<>> observed_ordinary_textures;
  for (const OgreNextDemoCachedProjectionPublicationInput &input :
       projections) {
    const bool frame_reachable =
        used_keys.find(input.projection_key) != used_keys.end();
    const OgreNextDemoCachedTexturePublicationInput &texture =
        *textures_by_key.find(input.texture_key)->second;
    const OgreNextDemoCachedSamplerPublicationInput &sampler =
        *samplers_by_key.find(input.sampler_key)->second;
    OgreNextDemoCachedProjectionPublicationOwner owner;
    owner.projection_key = input.projection_key;
    owner.material_source_id = input.material_source_id;
    owner.texture_source_id = texture.texture_source_id;
    owner.sampler_source_id = sampler.sampler_source_id;
    owner.frame_reachable = frame_reachable;
    candidate.owner_catalog.push_back(std::move(owner));
    if (!frame_reachable) {
      continue;
    }
    candidate.frame_root_material_source_ids.push_back(
        input.material_source_id);
    if (IsOgreNextDemoAuthenticatedTextureSourceMode(texture.source_mode)) {
      if (observed_authenticated_textures.emplace(input.texture_key, true)
              .second) {
        candidate.authenticated_texture_keys.push_back(input.texture_key);
      }
    } else if (observed_ordinary_textures.emplace(input.texture_key, true)
                   .second) {
      candidate.ordinary_texture_keys.push_back(input.texture_key);
    }
  }
  if (!candidate.authenticated_texture_keys.empty()) {
    Render::ValidationResult validation =
        validator.ValidateReachableAuthenticatedTextureBatch(
            candidate.authenticated_texture_keys);
    if (!validation) {
      validation.field =
          "ogre_next_demo.material.publication." + validation.field;
      return validation;
    }
  }
  if (!candidate.ordinary_texture_keys.empty()) {
    Render::ValidationResult validation =
        validator.ValidateReachableOrdinaryTextureBatch(
            candidate.ordinary_texture_keys);
    if (!validation) {
      validation.field =
          "ogre_next_demo.material.publication." + validation.field;
      return validation;
    }
  }
  output = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult SelectOgreNextDemoTextureSourceMode(
    bool authenticated_source_required, bool authenticated_resolution_attempted,
    const Render::ValidationResult &authenticated_resolution_result,
    OgreNextDemoTextureSourceMode authenticated_resolution_mode,
    bool ordinary_resolution_attempted,
    const Render::ValidationResult &ordinary_resolution_result,
    OgreNextDemoTextureSourceSelection &output) {
  if (authenticated_source_required) {
    if (ordinary_resolution_attempted || !ordinary_resolution_result) {
      return Failure(
          Render::ValidationCode::SEQUENCE_MISMATCH,
          "ogre_next_demo.material.authenticated.ordinary_resolution",
          "authenticated-required texture probed ordinary source authority");
    }
    if (!authenticated_resolution_attempted) {
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.authenticated.resolution",
          "authenticated-required texture has no source resolution attempt");
    }
    if (!authenticated_resolution_result) {
      return authenticated_resolution_result;
    }
    if (!IsOgreNextDemoAuthenticatedTextureSourceMode(
            authenticated_resolution_mode)) {
      return Failure(Render::ValidationCode::INVALID_ENUM,
                     "ogre_next_demo.material.authenticated.source_kind",
                     "authenticated resolution has no exact archive/generated "
                     "source kind");
    }
    OgreNextDemoTextureSourceSelection candidate;
    candidate.selected = true;
    candidate.mode = authenticated_resolution_mode;
    candidate.exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
    output = candidate;
    return Render::ValidationResult::Success();
  }
  if (authenticated_resolution_attempted) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.unauthenticated.resolution",
        "ordinary texture must not probe authenticated source authority");
  }
  if (!authenticated_resolution_result) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.unauthenticated.resolution_result",
        "ordinary texture received a stale source-resolution failure");
  }
  OgreNextDemoTextureSourceSelection candidate;
  candidate.mode =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
  if (!ordinary_resolution_attempted || !ordinary_resolution_result) {
    candidate.selected = false;
    candidate.exclusion = OgreNextDemoTextureProjectionExclusion::
        ORDINARY_SELECTED_SOURCE_UNAVAILABLE;
    output = candidate;
    return Render::ValidationResult::Success();
  }
  candidate.selected = true;
  candidate.exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
  output = candidate;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ValidateOgreNextDemoCachedTextureSourceAuthority(
    OgreNextDemoTextureSourceMode frozen_mode, bool frame_reachable,
    bool source_classification_matches, bool fresh_resolution_attempted,
    const Render::ValidationResult &fresh_resolution_result,
    bool immutable_receipt_matches) {
  if (!IsOgreNextDemoAuthenticatedTextureSourceMode(frozen_mode) &&
      frozen_mode !=
          OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.cached.source_mode",
                   "cached texture source mode is invalid");
  }
  if (!frame_reachable) {
    if (fresh_resolution_attempted || !fresh_resolution_result ||
        immutable_receipt_matches) {
      return Failure(
          Render::ValidationCode::SEQUENCE_MISMATCH,
          "ogre_next_demo.material.cached.unreachable",
          "unreachable anti-tombstone owner probed live texture authority");
    }
    return Render::ValidationResult::Success();
  }

  if (!source_classification_matches) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.material.cached.source_classification",
        "cached texture changed selected source authority classification");
  }
  if (!fresh_resolution_attempted) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.cached.fresh_resolution",
                   "reachable source-byte texture has no fresh resolution");
  }
  if (!fresh_resolution_result) {
    return fresh_resolution_result;
  }
  if (!immutable_receipt_matches) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.material.cached.immutable_receipt",
        "fresh source receipt differs from the frozen immutable state");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult ValidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &observation) {
  if (!observation.ordinary_texture) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.ordinary",
                   "TUS0 must be a named, single-frame, loaded 2D texture "
                   "without UAV access");
  }
  if (!observation.uv0_identity) {
    return Failure(
        Render::ValidationCode::UNSUPPORTED_FEATURE,
        "ogre_next_demo.terrain.sampling.uv",
        "TUS0 must use UV0 with no generation, effects, or transform");
  }
  if (!observation.sampler_identity) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.sampler",
                   "TUS0 must use clamp U/V/W, linear min/mag, nearest mip, "
                   "and no comparison");
  }
  if (!observation.gamma_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.gamma",
                   "display-domain filtering requires native hardware gamma "
                   "decode to remain disabled");
  }
  if (!observation.fog_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.fog",
                   "the disposable opaque terrain lowering cannot preserve "
                   "OGRE scene fog");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult
RevalidateOgreNextDemoSampling(const OgreNextDemoSamplingObservation &before,
                               const OgreNextDemoSamplingObservation &after) {
  Render::ValidationResult validation = ValidateOgreNextDemoSampling(before);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgreNextDemoSampling(after);
  if (!validation) {
    return validation;
  }
  if (before.exact_native_state.empty() || after.exact_native_state.empty() ||
      before.exact_native_state != after.exact_native_state) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.readback.revalidation",
                   "terrain, TUS0, sampler, texture, or mip state mutated "
                   "during readback");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult
CompleteOgreNextDemoOpaqueMipChain(Render::TextureResourceDescriptor &texture) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != Render::TextureColorSpace::SRGB ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.size() != 1U) {
    return Failure(
        Render::ValidationCode::SIZE_MISMATCH,
        "ogre_next_demo.terrain.texture.full_mip_chain",
        "opaque lowering requires exactly one fresh SRGB RGBA8 2D base level");
  }

  const Render::TextureMipLevelDescriptor &base = texture.mip_levels.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(texture.width) * 4U;
  if (texture.height != 0U &&
      row_bytes >
          (std::numeric_limits<std::uint64_t>::max)() / texture.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "RGBA8 base-level byte count overflows", 0U);
  }
  const std::uint64_t layer_bytes = row_bytes * texture.height;
  if (layer_bytes > static_cast<std::uint64_t>(
                        (std::numeric_limits<std::size_t>::max)()) ||
      base.width != texture.width || base.height != texture.height ||
      base.row_pitch_bytes != row_bytes ||
      base.layer_pitch_bytes != layer_bytes ||
      base.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "opaque lowering requires an exact tight RGBA8 base layout",
                   0U);
  }

  // Validation above completes before the first write, so malformed input is
  // transactionally unchanged. Only the fourth byte of a native base texel is
  // touched; its RGB triplet remains byte-identical to the fresh readback.
  for (std::size_t alpha = 3U; alpha < texture.mip_levels.front().bytes.size();
       alpha += 4U) {
    texture.mip_levels.front().bytes[alpha] = 255U;
  }

  while (texture.mip_levels.size() <
         CompleteMipCount(texture.width, texture.height)) {
    const Render::TextureMipLevelDescriptor &source = texture.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.terrain.texture.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t sum =
              static_cast<std::uint32_t>(source.bytes[offsets[0U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[1U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[2U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[3U] + channel]);
          // Round to nearest integer with a deterministic half-up rule. This
          // operates on encoded bytes because the material contract filters in
          // display space and decodes only after sampling.
          destination.bytes[output + channel] =
              static_cast<std::uint8_t>((sum + 2U) / 4U);
        }
        destination.bytes[output + 3U] = 255U;
      }
    }
    texture.mip_levels.push_back(std::move(destination));
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult CompleteOgreNextDemoSrgbPbrMipChain(
    Render::TextureResourceDescriptor &texture) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != Render::TextureColorSpace::SRGB ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.size() != 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.full_mip_chain",
                   "sRGB PBR lowering requires exactly one fresh SRGB RGBA8 2D "
                   "base level");
  }

  const Render::TextureMipLevelDescriptor &base = texture.mip_levels.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(texture.width) * 4U;
  if (texture.height != 0U &&
      row_bytes >
          (std::numeric_limits<std::uint64_t>::max)() / texture.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.mip_layout",
                   "RGBA8 base-level byte count overflows", 0U);
  }
  const std::uint64_t layer_bytes = row_bytes * texture.height;
  if (layer_bytes > static_cast<std::uint64_t>(
                        (std::numeric_limits<std::size_t>::max)()) ||
      base.width != texture.width || base.height != texture.height ||
      base.row_pitch_bytes != row_bytes ||
      base.layer_pitch_bytes != layer_bytes ||
      base.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
    return Failure(
        Render::ValidationCode::SIZE_MISMATCH,
        "ogre_next_demo.material.texture.mip_layout",
        "sRGB PBR lowering requires an exact tight RGBA8 base layout", 0U);
  }

  // Work on a complete candidate so every validation failure leaves the
  // caller's freshly read native base byte-for-byte unchanged.
  Render::TextureResourceDescriptor candidate = texture;
  for (std::size_t alpha = 3U;
       alpha < candidate.mip_levels.front().bytes.size(); alpha += 4U) {
    candidate.mip_levels.front().bytes[alpha] = 255U;
  }

  while (candidate.mip_levels.size() <
         CompleteMipCount(candidate.width, candidate.height)) {
    const Render::TextureMipLevelDescriptor &source =
        candidate.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.material.texture.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const double linear_average =
              (DecodeSrgbByte(source.bytes[offsets[0U] + channel]) +
               DecodeSrgbByte(source.bytes[offsets[1U] + channel]) +
               DecodeSrgbByte(source.bytes[offsets[2U] + channel]) +
               DecodeSrgbByte(source.bytes[offsets[3U] + channel])) /
              4.0;
          destination.bytes[output + channel] =
              EncodeLinearSrgbByte(linear_average);
        }
        destination.bytes[output + 3U] = 255U;
      }
    }
    candidate.mip_levels.push_back(std::move(destination));
  }

  Render::ValidationResult validation =
      Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material.texture." + validation.field;
    return validation;
  }
  texture = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, Render::TextureResourceDescriptor &output) {
  if (decoded.version != Render::kOgre14DecodedSourceTextureVersion ||
      decoded.width == 0U || decoded.height == 0U ||
      decoded.width != expected_native_width ||
      decoded.height != expected_native_height || debug_name.empty() ||
      decoded.color_semantic !=
          Render::Ogre14SourceTextureColorSemantic::SRGB_COLOR ||
      decoded.mip_levels.empty() ||
      decoded.mip_levels.size() >
          CompleteMipCount(decoded.width, decoded.height)) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.material.authenticated.decoded_identity",
                   "decoded source schema, dimensions, semantic, or mip count "
                   "disagrees with the loaded texture");
  }
  if ((decoded.source_format == Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode !=
           Render::Ogre14SourceTextureBc1AlphaMode::OPAQUE) ||
      (decoded.source_format != Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode !=
           Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE)) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.authenticated.bc1_alpha_mode",
                   "opaque product projection requires the frozen BC1 opaque "
                   "interpretation only for BC1 sources");
  }

  std::uint32_t mip_width = decoded.width;
  std::uint32_t mip_height = decoded.height;
  for (std::size_t level = 0U; level < decoded.mip_levels.size(); ++level) {
    const Render::Ogre14DecodedSourceTextureMip &mip =
        decoded.mip_levels[level];
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(mip_width) * 4U;
    const std::uint64_t slice_bytes = row_bytes * mip_height;
    if (mip.version != Render::kOgre14DecodedSourceTextureMipVersion ||
        mip.width != mip_width || mip.height != mip_height ||
        mip.row_pitch_bytes != row_bytes ||
        mip.slice_pitch_bytes != slice_bytes ||
        slice_bytes > static_cast<std::uint64_t>(
                          (std::numeric_limits<std::size_t>::max)()) ||
        mip.rgba8_unorm.size() != static_cast<std::size_t>(slice_bytes)) {
      return Failure(
          Render::ValidationCode::SIZE_MISMATCH,
          "ogre_next_demo.material.authenticated.decoded_mip",
          "decoded source mip prefix is not canonical tight RGBA8 geometry",
          level);
    }
    mip_width = (std::max)(1U, mip_width / 2U);
    mip_height = (std::max)(1U, mip_height / 2U);
  }

  Render::Ogre14DecodedSourceTextureMip &decoded_base =
      decoded.mip_levels.front();
  Render::TextureMipLevelDescriptor base;
  base.width = decoded_base.width;
  base.height = decoded_base.height;
  base.row_pitch_bytes = decoded_base.row_pitch_bytes;
  base.layer_pitch_bytes = decoded_base.slice_pitch_bytes;
  base.bytes = std::move(decoded_base.rgba8_unorm);

  Render::TextureResourceDescriptor candidate;
  candidate.debug_name.assign(debug_name.data(), debug_name.size());
  candidate.type = Render::TextureResourceType::TEXTURE_2D;
  candidate.format = Render::TextureResourceFormat::RGBA8_UNORM;
  candidate.color_space = Render::TextureColorSpace::SRGB;
  candidate.width = decoded.width;
  candidate.height = decoded.height;
  candidate.array_layers = 1U;
  candidate.mip_levels.push_back(std::move(base));

  // The full decoded prefix above is authoritative validation input. Only the
  // base is product input; authored nonzero mips cannot change established
  // CityWorld/Alexis deterministic PBR filtering semantics.
  Render::ValidationResult validation =
      CompleteOgreNextDemoSrgbPbrMipChain(candidate);
  if (!validation) {
    return validation;
  }
  validation = Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field =
        "ogre_next_demo.material.authenticated.texture." + validation.field;
    return validation;
  }
  output = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult DeriveOgreNextDemoSourceId(std::string_view domain,
                                                    std::string_view exact_key,
                                                    std::uint64_t &source_id) {
  if (domain.empty() || exact_key.empty()) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "source ID domain and exact identity must not be empty");
  }
  std::uint64_t hash = kFnv1a64OffsetBasis;
  const auto append = [&hash](std::string_view bytes) {
    for (const char byte : bytes) {
      hash ^= static_cast<std::uint8_t>(static_cast<unsigned char>(byte));
      hash *= kFnv1a64Prime;
    }
  };
  append(domain);
  const char separator = '\0';
  append(std::string_view(&separator, 1U));
  append(exact_key);
  if (hash == 0U) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "domain-separated identity hashed to reserved zero");
  }
  source_id = hash;
  return Render::ValidationResult::Success();
}

Render::ValidationResult
BuildOgreNextDemoMatteTangents(std::size_t vertex_count,
                               std::vector<Render::Float3> &normals,
                               std::vector<Render::Float4> &tangents) {
  if (vertex_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal sanitization requires at least one vertex");
  }
  if (!normals.empty() && normals.size() != vertex_count) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal stream must be absent or complete");
  }

  constexpr Render::Float3 kFallbackNormal{0.0F, 1.0F, 0.0F};
  std::vector<Render::Float3> candidate_normals = normals;
  if (candidate_normals.empty()) {
    candidate_normals.assign(vertex_count, kFallbackNormal);
  }
  std::vector<Render::Float4> candidate_tangents;
  candidate_tangents.reserve(vertex_count);
  for (std::size_t index = 0U; index < vertex_count; ++index) {
    Render::Float3 &normal = candidate_normals[index];
    const float normal_length_squared =
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (std::isfinite(normal.x) && std::isfinite(normal.y) &&
        std::isfinite(normal.z) && std::isfinite(normal_length_squared) &&
        normal_length_squared > 0.0F) {
      const float inverse_length = 1.0F / std::sqrt(normal_length_squared);
      normal = {normal.x * inverse_length, normal.y * inverse_length,
                normal.z * inverse_length};
      const float sanitized_length_squared =
          normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
      if (!std::isfinite(sanitized_length_squared) ||
          std::fabs(sanitized_length_squared - 1.0F) > 1.0e-3F) {
        normal = kFallbackNormal;
      }
    } else {
      normal = kFallbackNormal;
    }
    // Cross the normal with the least-parallel fixed axis. The tangent has no
    // material-space consumer in the matte path; it only provides the exact,
    // deterministic RT4 vertex layout while staying orthogonal as a FlexBody
    // normal deforms from frame to frame.
    const Render::Float3 axis = std::fabs(normal.z) < 0.875F
                                    ? Render::Float3{0.0F, 0.0F, 1.0F}
                                    : Render::Float3{0.0F, 1.0F, 0.0F};
    const Render::Float3 crossed{axis.y * normal.z - axis.z * normal.y,
                                 axis.z * normal.x - axis.x * normal.z,
                                 axis.x * normal.y - axis.y * normal.x};
    const float length_squared =
        crossed.x * crossed.x + crossed.y * crossed.y + crossed.z * crossed.z;
    if (!std::isfinite(length_squared) || length_squared <= 0.0F) {
      return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                     "ogre_next_demo.matte_mesh.tangents",
                     "an authored normal cannot produce a finite matte tangent",
                     index);
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    candidate_tangents.push_back({crossed.x * inverse_length,
                                  crossed.y * inverse_length,
                                  crossed.z * inverse_length, 1.0F});
  }
  normals = std::move(candidate_normals);
  tangents = std::move(candidate_tangents);
  return Render::ValidationResult::Success();
}

Render::ValidationResult
NormalizeOgreNextDemoMatteMesh(Render::MeshResourceDescriptor &mesh) {
  Render::MeshResourceDescriptor candidate = mesh;
  if (candidate.texture_coordinates_0.empty()) {
    candidate.texture_coordinates_0.assign(candidate.positions.size(), {});
  }
  candidate.texture_coordinates_1.clear();
  candidate.colors.clear();
  candidate.velocities.clear();
  Render::ValidationResult validation = BuildOgreNextDemoMatteTangents(
      candidate.positions.size(), candidate.normals, candidate.tangents);
  if (!validation) {
    return validation;
  }

  validation = Render::ValidateMeshResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.matte_mesh." + validation.field;
    return validation;
  }
  mesh = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoStaticCaptureRadius(
    float left, float right, float top, float bottom, float near_plane,
    float far_plane, float target_aspect, float &radius_meters) {
  if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) ||
      !std::isfinite(bottom) || !std::isfinite(near_plane) ||
      !std::isfinite(far_plane) || !std::isfinite(target_aspect) ||
      !(left < right) || !(bottom < top) || !(near_plane > 0.0F) ||
      !(far_plane > near_plane) || !(target_aspect > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.frustum",
                   "static capture requires finite ordered perspective "
                   "extents, clip distances, and target aspect");
  }

  const double horizontal_span =
      static_cast<double>(right) - static_cast<double>(left);
  const double vertical_span =
      static_cast<double>(top) - static_cast<double>(bottom);
  const double horizontal_offset =
      std::fabs((static_cast<double>(right) + static_cast<double>(left)) /
                horizontal_span);
  const double vertical_offset = std::fabs(
      (static_cast<double>(top) + static_cast<double>(bottom)) / vertical_span);
  const double half_vertical_slope =
      vertical_span / (2.0 * static_cast<double>(near_plane));
  const double half_horizontal_slope =
      half_vertical_slope * static_cast<double>(target_aspect);
  const double maximum_horizontal_slope =
      half_horizontal_slope * (1.0 + horizontal_offset);
  const double maximum_vertical_slope =
      half_vertical_slope * (1.0 + vertical_offset);
  const double candidate =
      static_cast<double>(far_plane) *
      std::sqrt(1.0 + maximum_horizontal_slope * maximum_horizontal_slope +
                maximum_vertical_slope * maximum_vertical_slope);
  if (!std::isfinite(candidate) || !(candidate > 0.0) ||
      candidate > static_cast<double>((std::numeric_limits<float>::max)())) {
    return Failure(
        Render::ValidationCode::VALUE_OUT_OF_RANGE,
        "ogre_next_demo.static_capture.radius",
        "normalized far-frustum enclosing radius is not representable");
  }
  const float conservative = std::nextafter(
      static_cast<float>(candidate), (std::numeric_limits<float>::infinity)());
  if (!std::isfinite(conservative) || !(conservative > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.radius",
                   "normalized far-frustum enclosing radius overflowed "
                   "conservative rounding");
  }
  radius_meters = conservative;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ClassifyOgreNextDemoStaticBounds(
    const Render::Bounds3 &world_bounds, const Render::Float3 &camera_position,
    float radius_meters, bool &within_capture_radius) {
  const auto finite = [](float value) { return std::isfinite(value); };
  if (!finite(world_bounds.minimum.x) || !finite(world_bounds.minimum.y) ||
      !finite(world_bounds.minimum.z) || !finite(world_bounds.maximum.x) ||
      !finite(world_bounds.maximum.y) || !finite(world_bounds.maximum.z) ||
      !finite(camera_position.x) || !finite(camera_position.y) ||
      !finite(camera_position.z) || !finite(radius_meters) ||
      world_bounds.minimum.x > world_bounds.maximum.x ||
      world_bounds.minimum.y > world_bounds.maximum.y ||
      world_bounds.minimum.z > world_bounds.maximum.z ||
      !(radius_meters > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.bounds",
                   "static capture requires a finite ordered world AABB, "
                   "camera, and positive radius");
  }

  const auto separation = [](double value, double minimum, double maximum) {
    if (value < minimum) {
      return minimum - value;
    }
    if (value > maximum) {
      return value - maximum;
    }
    return 0.0;
  };
  const double dx = separation(camera_position.x, world_bounds.minimum.x,
                               world_bounds.maximum.x);
  const double dy = separation(camera_position.y, world_bounds.minimum.y,
                               world_bounds.maximum.y);
  const double dz = separation(camera_position.z, world_bounds.minimum.z,
                               world_bounds.maximum.z);
  const double radius = radius_meters;
  within_capture_radius = dx * dx + dy * dy + dz * dz <= radius * radius;
  return Render::ValidationResult::Success();
}

Render::ValidationResult
OgreNextDemoIdentityRegistry::Register(std::string exact_key,
                                       std::uint64_t source_id) {
  if (exact_key.empty() || source_id == 0U) {
    return Failure(
        Render::ValidationCode::INVALID_IDENTIFIER, "ogre_next_demo.source_id",
        "registered source ID and exact identity must be nonzero and nonempty");
  }
  const auto id_match = keys_by_id_.find(source_id);
  if (id_match != keys_by_id_.end() && id_match->second != exact_key) {
    return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "distinct domain-separated identities collided");
  }
  const auto key_match = ids_by_key_.find(exact_key);
  if (key_match != ids_by_key_.end() && key_match->second != source_id) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.source_id",
                   "one exact identity changed its source ID");
  }
  keys_by_id_[source_id] = exact_key;
  ids_by_key_[std::move(exact_key)] = source_id;
  return Render::ValidationResult::Success();
}

bool OgreNextDemoIdentityRegistry::Contains(std::string_view exact_key,
                                            std::uint64_t source_id) const {
  const auto match = keys_by_id_.find(source_id);
  return match != keys_by_id_.end() && match->second == exact_key;
}

std::size_t OgreNextDemoIdentityRegistry::size() const noexcept {
  return keys_by_id_.size();
}

bool OgreNextDemoRequiresMatte(std::size_t texture_unit_count,
                               bool has_authored_program) noexcept {
  return texture_unit_count != 0U || has_authored_program;
}

bool OgreNextDemoDropsDynamicBlendColors(
    bool has_dynamic_texture_blend) noexcept {
  return has_dynamic_texture_blend;
}

bool OgreNextDemoOmitsInvisibleCab(std::string_view exact_material_name,
                                   float diffuse_alpha,
                                   bool depth_write_enabled) noexcept {
  return exact_material_name == "invisible" && diffuse_alpha == 0.0F &&
         !depth_write_enabled;
}

bool OgreNextDemoOmitsNonUniformSpeedBump(
    std::string_view exact_mesh_name,
    const Render::Float3 &derived_scale) noexcept {
  return exact_mesh_name == "topeQr.mesh" && derived_scale.x == 1.0F &&
         derived_scale.y == 0.5F && derived_scale.z == 0.5F;
}

bool OgreNextDemoAllowsAlexisTUS0Approximation(
    std::string_view exact_resource_group,
    std::string_view exact_material_name) noexcept {
  if (exact_resource_group != "{bundle USER:/mods/AlexisSaber.zip}") {
    return false;
  }
  constexpr std::array<std::string_view, 4U> kOpaqueManagedNames{
      {"SaberChassis", "SaberChassisM", "SaberWheels", "SaberGrilles"}};
  constexpr std::string_view kSuffixPrefix =
      " (AlexisSaber.truck [Instance ID ";
  constexpr std::string_view kSuffixEnd = "])";
  for (const std::string_view base : kOpaqueManagedNames) {
    if (exact_material_name.size() <=
            base.size() + kSuffixPrefix.size() + kSuffixEnd.size() ||
        exact_material_name.substr(0U, base.size()) != base ||
        exact_material_name.substr(base.size(), kSuffixPrefix.size()) !=
            kSuffixPrefix ||
        exact_material_name.substr(exact_material_name.size() -
                                   kSuffixEnd.size()) != kSuffixEnd) {
      continue;
    }
    const std::string_view instance = exact_material_name.substr(
        base.size() + kSuffixPrefix.size(),
        exact_material_name.size() - base.size() - kSuffixPrefix.size() -
            kSuffixEnd.size());
    if (!instance.empty() &&
        std::all_of(instance.begin(), instance.end(),
                    [](char value) { return value >= '0' && value <= '9'; })) {
      return true;
    }
  }
  return false;
}

} // namespace RoR::Gfx::Detail
