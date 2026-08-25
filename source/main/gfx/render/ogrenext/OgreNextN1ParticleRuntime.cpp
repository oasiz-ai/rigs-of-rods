/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextN1ParticleRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool HasAuthoredSourceAlpha(const TextureResourceDescriptor &texture) {
  if (texture.type != TextureResourceType::TEXTURE_2D ||
      texture.format != TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != TextureColorSpace::SRGB ||
      texture.array_layers != 1U) {
    return false;
  }
  for (const TextureMipLevelDescriptor &mip : texture.mip_levels) {
    for (std::uint32_t y = 0U; y < mip.height; ++y) {
      const std::uint64_t row = static_cast<std::uint64_t>(y) *
                                mip.row_pitch_bytes;
      for (std::uint32_t x = 0U; x < mip.width; ++x) {
        const std::uint64_t alpha =
            row + static_cast<std::uint64_t>(x) * 4U + 3U;
        if (alpha >= mip.bytes.size()) {
          return false;
        }
        if (mip.bytes[static_cast<std::size_t>(alpha)] != 255U) {
          return true;
        }
      }
    }
  }
  return false;
}

bool IsExactDustSampler(const SamplerResourceDescriptor &sampler,
                        std::size_t mip_count) noexcept {
  if (mip_count == 0U ||
      sampler.address_u != SamplerAddressMode::CLAMP_TO_EDGE ||
      sampler.address_v != SamplerAddressMode::CLAMP_TO_EDGE ||
      sampler.address_w != SamplerAddressMode::CLAMP_TO_EDGE ||
      sampler.mip_lod_bias != 0.0F || sampler.minimum_lod != 0.0F ||
      sampler.maximum_lod != static_cast<float>(mip_count - 1U) ||
      sampler.compare_enabled) {
    return false;
  }
  if (!sampler.anisotropy_enabled) {
    return sampler.maximum_anisotropy == 1.0F;
  }
  return sampler.minification_filter == SamplerFilter::LINEAR &&
         sampler.magnification_filter == SamplerFilter::LINEAR &&
         sampler.mip_filter == SamplerFilter::LINEAR &&
         sampler.maximum_anisotropy >= 2.0F &&
         sampler.maximum_anisotropy <= 16.0F &&
         std::floor(sampler.maximum_anisotropy) ==
             sampler.maximum_anisotropy;
}

bool IsKnownOperation(Ogre14ParticleLifecycleOperation operation) noexcept {
  switch (operation) {
  case Ogre14ParticleLifecycleOperation::CREATE:
  case Ogre14ParticleLifecycleOperation::UPDATE:
  case Ogre14ParticleLifecycleOperation::STOP:
  case Ogre14ParticleLifecycleOperation::DESTROY:
    return true;
  }
  return false;
}

bool IsUnitDirection(const Float3 &direction) noexcept {
  if (!IsFinite(direction)) {
    return false;
  }
  const double x = direction.x;
  const double y = direction.y;
  const double z = direction.z;
  const double length_squared = x * x + y * y + z * z;
  return std::isfinite(length_squared) &&
         std::fabs(length_squared - 1.0) <= 1.0e-3;
}

ValidationResult ValidateParticleSystemPayload(
    const Ogre14CapturedParticleSystem &system, std::size_t command_index) {
  if (system.particles.size() > 16384U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "continuous_particles.system.particles",
                   "one Dust system exceeds the native batch quota",
                   command_index);
  }
  std::uint64_t previous_particle_id = 0U;
  for (const Ogre14ParticleState &particle : system.particles) {
    if (particle.particle_id == 0U ||
        particle.particle_id <= previous_particle_id) {
      return Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                     "continuous_particles.system.particle_id",
                     "particle IDs must be nonzero, unique, and sorted",
                     command_index);
    }
    if (!IsFinite(particle.position) || !IsUnitDirection(particle.direction) ||
        !IsFinite(particle.velocity) || !IsFinite(particle.color_linear) ||
        !IsFinite(particle.size_meters) ||
        !IsFinite(particle.rotation_radians) ||
        !IsFinite(particle.age_seconds) ||
        !IsFinite(particle.lifetime_seconds)) {
      return Failure(ValidationCode::NON_FINITE_VALUE,
                     "continuous_particles.system.particle_payload",
                     "particle payload contains nonfinite or non-unit state",
                     command_index);
    }
    if (particle.color_linear.x < 0.0F ||
        particle.color_linear.y < 0.0F ||
        particle.color_linear.z < 0.0F ||
        particle.color_linear.w < 0.0F ||
        particle.color_linear.w > 1.0F ||
        particle.size_meters.x <= 0.0F ||
        particle.size_meters.y <= 0.0F || particle.age_seconds < 0.0F ||
        particle.lifetime_seconds <= 0.0F ||
        particle.age_seconds > particle.lifetime_seconds) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "continuous_particles.system.particle_payload",
                     "particle colour, size, age, or lifetime is out of range",
                     command_index);
    }
    previous_particle_id = particle.particle_id;
  }
  return ValidationResult::Success();
}

ValidationResult ValidateClosure(
    const Ogre14ParticleMaterialClosureReceipt &closure,
    const RenderAssetRegistry &registry, std::uint64_t source_sequence,
    std::size_t index) {
  const MaterialDescriptor *const material =
      registry.ResolveMaterial(closure.material);
  const TextureResourceDescriptor *const source_texture =
      registry.ResolveTexture(closure.source_texture);
  const SamplerResourceDescriptor *const sampler =
      registry.ResolveSampler(closure.sampler);
  if (closure.version != kOgre14ParticleMaterialClosureReceiptVersion ||
      closure.material_catalog_registry_id != registry.registry_id() ||
      closure.material_catalog_sequence == 0U ||
      closure.material_catalog_sequence > registry.sequence() ||
      closure.translation_source_sequence == 0U ||
      closure.translation_source_sequence > source_sequence ||
      material == nullptr ||
      source_texture == nullptr ||
      sampler == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "continuous_particles.material_closure",
                   "particle closure does not resolve against the live catalog",
                   index);
  }
  if (material->base_color_texture.texture != closure.source_texture ||
      material->base_color_texture.sampler != closure.sampler ||
      material->model != MaterialModel::PBR_METALLIC_ROUGHNESS ||
      material->pbr_workflow != MaterialPbrWorkflow::METALLIC_ROUGHNESS ||
      material->blend_mode != MaterialBlendMode::LEGACY_STRAIGHT_ALPHA ||
      material->alpha_test_mode != MaterialAlphaTestMode::GREATER ||
      material->depth_write || material->double_sided ||
      material->base_color_transfer !=
          BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER ||
      material->base_color_factor != Float4{1.0F, 1.0F, 1.0F, 1.0F} ||
      material->alpha_cutoff != (2.0F / 255.0F) ||
      !closure.depth_check || closure.depth_write ||
      closure.lighting_enabled || closure.receives_shadows ||
      closure.casts_shadows || !closure.vertex_color_modulation ||
      !closure.source_backed_texture || closure.gpu_readback_used ||
      closure.blend !=
          ContinuousParticleBlendMode::LEGACY_STRAIGHT_ALPHA ||
      closure.alpha_reject != ContinuousParticleAlphaReject::GREATER ||
      closure.alpha_reject_threshold != (2.0F / 255.0F) ||
      closure.sort_policy !=
          ContinuousParticleSortPolicy::STABLE_PARTICLE_ID ||
      !IsExactDustSampler(*sampler, source_texture->mip_levels.size()) ||
      !HasAuthoredSourceAlpha(*source_texture)) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "continuous_particles.material_closure",
        "N1 v1 admits only source-backed tracks/Dust legacy straight-alpha GREATER 2/255, depth-write-off, stable-order state",
        index);
  }
  return ValidationResult::Success();
}

} // namespace

std::array<Float2, 4U>
BuildOgre14ParticleTextureCoordinateQuad(float rotation_radians) noexcept {
  const float cosine = std::cos(rotation_radians);
  const float sine = std::sin(rotation_radians);
  constexpr float width = 0.5F;
  constexpr float height = 0.5F;
  constexpr float middle_u = 0.5F;
  constexpr float middle_v = 0.5F;
  const float cosine_width = cosine * width;
  const float cosine_height = cosine * height;
  const float sine_width = sine * width;
  const float sine_height = sine * height;

  // Pinned OgreBillboardSet::genQuadVertices emits LT, RT, LB, RB. N1's
  // quad order is LT, LB, RB, RT, so preserve the same values in that order.
  return {{{middle_u - cosine_width + sine_height,
            middle_v - sine_width - cosine_height},
           {middle_u - cosine_width - sine_height,
            middle_v - sine_width + cosine_height},
           {middle_u + cosine_width - sine_height,
            middle_v + sine_width + cosine_height},
           {middle_u + cosine_width + sine_height,
            middle_v + sine_width - cosine_height}}};
}

ValidationResult OgreNextN1ParticleRuntime::Prepare(
    std::uint64_t frame_id,
    const std::shared_ptr<const Ogre14ParticleCapturedFrame> &frame,
    const RenderAssetRegistry &registry, std::uint64_t simulation_tick,
    const Double3 &absolute_world_origin_meters) {
  if (frame_id == 0U || has_prepared_) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "continuous_particles.prepare",
                   "one nonzero frame may be prepared at a time");
  }
  prepared_.clear();
  prepared_tombstones_.clear();
  prepared_systems_.clear();
  prepared_audit_ = audit_;
  prepared_frame_id_ = 0U;
  prepared_highest_event_id_ = highest_event_id_;
  prepared_highest_system_id_ = highest_system_id_;
  try {
    std::map<std::uint64_t, Record> candidate = committed_;
    std::set<std::uint64_t> candidate_tombstones =
        committed_tombstones_;
    std::vector<std::shared_ptr<const Ogre14CapturedParticleSystem>>
        candidate_systems;
    std::uint64_t candidate_highest_event_id = highest_event_id_;
    std::uint64_t candidate_highest_system_id = highest_system_id_;
    OgreNextN1ParticleRuntimeAudit candidate_audit = audit_;
    if (frame != nullptr) {
      const bool source_sequence_is_contiguous =
          audit_.committed_source_sequence == 0U ||
          frame->source_sequence == audit_.committed_source_sequence + 1U;
      const bool authoritative_generation_close =
          frame->finalizes_scene_generation && registry.live_count() == 0U &&
          (audit_.committed_source_sequence == 0U ||
           frame->source_sequence > audit_.committed_source_sequence);
      if (frame->version != kOgre14ParticleCapturedFrameVersion ||
          frame->source_sequence == 0U ||
          frame->material_catalog_registry_id != registry.registry_id() ||
          frame->material_catalog_sequence != registry.sequence() ||
          frame->simulation_tick != simulation_tick ||
          frame->joined_buffer_epoch == 0U ||
          !IsFinite(frame->simulation_time_seconds) ||
          frame->simulation_time_seconds < 0.0 ||
          frame->commands.size() > 65536U ||
          frame->absolute_world_origin_meters !=
              absolute_world_origin_meters ||
          (!source_sequence_is_contiguous &&
           !authoritative_generation_close)) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "continuous_particles.lineage",
                       "particle delta does not continue the exact scene/catalog lineage");
      }
      candidate_audit.prepared_source_sequence = frame->source_sequence;
      std::set<std::uint64_t> commanded_systems;
      for (std::size_t index = 0U; index < frame->commands.size(); ++index) {
        const Ogre14CapturedParticleCommand &command = frame->commands[index];
        if (!IsKnownOperation(command.operation)) {
          return Failure(ValidationCode::INVALID_ENUM,
                         "continuous_particles.commands.operation",
                         "particle command operation is unknown", index);
        }
        if (command.event_id <= candidate_highest_event_id ||
            command.system_id == 0U ||
            !commanded_systems.insert(command.system_id).second) {
          return Failure(ValidationCode::SEQUENCE_MISMATCH,
                         "continuous_particles.commands.event_id",
                         "particle events must be globally monotonic with one command per system",
                         index);
        }
        candidate_highest_event_id = command.event_id;
        const auto current = candidate.find(command.system_id);
        if (command.operation == Ogre14ParticleLifecycleOperation::DESTROY) {
          if (command.system != nullptr ||
              (current == candidate.end() &&
               !authoritative_generation_close)) {
            return Failure(ValidationCode::REVISION_MISMATCH,
                           "continuous_particles.commands.destroy",
                           "DESTROY requires one existing system and no payload",
                           index);
          }
          if (candidate_tombstones.size() >= 65536U ||
              !candidate_tombstones.insert(command.system_id).second) {
            return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                           "continuous_particles.commands.destroy",
                           "particle system tombstone quota was exhausted",
                           index);
          }
          if (current != candidate.end()) {
            candidate.erase(current);
          }
          ++candidate_audit.destroy_commands;
          continue;
        }
        if (authoritative_generation_close) {
          return Failure(
              ValidationCode::REVISION_MISMATCH,
              "continuous_particles.commands.generation_close",
              "a scene-generation close may contain only DESTROY commands",
              index);
        }
        if (command.system == nullptr ||
            command.system->system_id != command.system_id ||
            command.system->effect != ParticleEffect::DUST ||
            command.system->billboard_mode !=
                Ogre14ParticleBillboardMode::CAMERA_FACING_POINT ||
            command.system->billboard_rotation_mode !=
                Ogre14ParticleBillboardRotationMode::TEXTURE_COORDINATES) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "continuous_particles.commands.system",
                         "N1 v1 requires realized camera-facing Dust with "
                         "texture-coordinate rotation",
                         index);
        }
        ValidationResult closure = ValidateClosure(
            command.system->material_closure, registry,
            frame->source_sequence, index);
        if (!closure) {
          return closure;
        }
        ValidationResult payload =
            ValidateParticleSystemPayload(*command.system, index);
        if (!payload) {
          return payload;
        }
        if ((command.operation == Ogre14ParticleLifecycleOperation::CREATE) !=
            (current == candidate.end())) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "continuous_particles.commands.operation",
                         "CREATE must be new and UPDATE/STOP must already exist",
                         index);
        }
        if (command.operation == Ogre14ParticleLifecycleOperation::CREATE &&
            (candidate_tombstones.find(command.system_id) !=
                 candidate_tombstones.end() ||
             command.system_id <= candidate_highest_system_id ||
             candidate.size() + candidate_tombstones.size() >= 65536U)) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "continuous_particles.commands.system_id",
                         "particle system identities are monotonic and tombstoned permanently",
                         index);
        }
        if (command.operation == Ogre14ParticleLifecycleOperation::STOP &&
            (command.system->emitting || current == candidate.end() ||
             current->second.stopped)) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "continuous_particles.commands.stop",
                         "STOP is only the live-to-stopped transition and retains emission-disabled realized particles",
                         index);
        }
        if (command.operation == Ogre14ParticleLifecycleOperation::UPDATE &&
            current != candidate.end() && !current->second.stopped &&
            !command.system->emitting) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "continuous_particles.commands.update",
                         "the live-to-stopped transition requires STOP",
                         index);
        }
        Record record;
        record.system = command.system;
        record.stopped = !command.system->emitting;
        candidate[command.system_id] = std::move(record);
        if (command.operation == Ogre14ParticleLifecycleOperation::CREATE) {
          candidate_highest_system_id = command.system_id;
          ++candidate_audit.create_commands;
        } else if (command.operation ==
                   Ogre14ParticleLifecycleOperation::UPDATE) {
          ++candidate_audit.update_commands;
        } else {
          ++candidate_audit.stop_commands;
        }
      }
      if (frame->finalizes_scene_generation) {
        if (!authoritative_generation_close) {
          return Failure(
              ValidationCode::SEQUENCE_MISMATCH,
              "continuous_particles.generation_close",
              "a scene-generation close requires a newer source sequence and the exact empty material catalog");
        }
        // Finalization is a complete empty boundary, not an incremental
        // particle delta. A recoverably rejected visual frame may have hidden
        // a CREATE or DESTROY from this runtime, so reconcile both sides: all
        // locally committed systems retire, while source-only DESTROY IDs are
        // retained as tombstones for the duration of this final transaction.
        for (const auto &entry : candidate) {
          if (candidate_tombstones.size() >= 65536U ||
              !candidate_tombstones.insert(entry.first).second) {
            return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                           "continuous_particles.generation_close",
                           "final particle tombstone quota was exhausted");
          }
        }
        candidate.clear();
      }
    }

    std::uint64_t particle_count = 0U;
    std::vector<RenderAssetReference> distinct_source_textures;
    candidate_systems.reserve(candidate.size());
    distinct_source_textures.reserve(candidate.size());
    for (const auto &entry : candidate) {
      if (entry.second.system == nullptr ||
          entry.second.system->particles.size() > 16384U ||
          particle_count > 65536U - entry.second.system->particles.size()) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "continuous_particles.limits",
                       "N1 particle system or frame quota exceeded");
      }
      const std::uint64_t validation_source_sequence =
          frame != nullptr ? frame->source_sequence
                           : audit_.committed_source_sequence;
      ValidationResult closure = ValidateClosure(
          entry.second.system->material_closure, registry,
          validation_source_sequence, ValidationResult::kNoElement);
      if (!closure) {
        return closure;
      }
      particle_count += entry.second.system->particles.size();
      candidate_systems.push_back(entry.second.system);
      const RenderAssetReference &source_texture =
          entry.second.system->material_closure.source_texture;
      if (std::find(distinct_source_textures.begin(),
                    distinct_source_textures.end(),
                    source_texture) == distinct_source_textures.end()) {
        distinct_source_textures.push_back(source_texture);
      }
    }
    candidate_audit.live_systems = candidate.size();
    if (candidate_audit.live_systems > 4096U) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "continuous_particles.live_systems",
                     "N1 live particle system quota exceeded");
    }
    candidate_audit.live_particles = particle_count;
    candidate_audit.lifetime_max_live_systems =
        (std::max)(candidate_audit.lifetime_max_live_systems,
                   candidate_audit.live_systems);
    candidate_audit.lifetime_max_live_particles =
        (std::max)(candidate_audit.lifetime_max_live_particles,
                   candidate_audit.live_particles);
    candidate_audit.source_backed_textures = distinct_source_textures.size();
    candidate_audit.source_alpha_textures = distinct_source_textures.size();
    candidate_audit.lifetime_max_source_backed_textures =
        (std::max)(candidate_audit.lifetime_max_source_backed_textures,
                   candidate_audit.source_backed_textures);
    candidate_audit.lifetime_max_source_alpha_textures =
        (std::max)(candidate_audit.lifetime_max_source_alpha_textures,
                   candidate_audit.source_alpha_textures);
    candidate_audit.gpu_readbacks = 0U;
    prepared_ = std::move(candidate);
    prepared_tombstones_ = std::move(candidate_tombstones);
    prepared_systems_ = std::move(candidate_systems);
    prepared_highest_event_id_ = candidate_highest_event_id;
    prepared_highest_system_id_ = candidate_highest_system_id;
    prepared_audit_ = candidate_audit;
    prepared_frame_id_ = frame_id;
    has_prepared_ = true;
    return ValidationResult::Success();
  } catch (...) {
    prepared_.clear();
    prepared_tombstones_.clear();
    prepared_systems_.clear();
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "continuous_particles.allocation",
                   "particle runtime candidate allocation failed");
  }
}

const std::vector<std::shared_ptr<const Ogre14CapturedParticleSystem>> &
OgreNextN1ParticleRuntime::prepared_systems() const noexcept {
  return prepared_systems_;
}

bool OgreNextN1ParticleRuntime::CanCommit(
    std::uint64_t frame_id) const noexcept {
  return has_prepared_ && frame_id == prepared_frame_id_;
}

bool OgreNextN1ParticleRuntime::Commit(std::uint64_t frame_id) noexcept {
  if (!has_prepared_ || frame_id != prepared_frame_id_) {
    return false;
  }
  committed_.swap(prepared_);
  committed_tombstones_.swap(prepared_tombstones_);
  highest_event_id_ = prepared_highest_event_id_;
  highest_system_id_ = prepared_highest_system_id_;
  audit_ = prepared_audit_;
  if (audit_.prepared_source_sequence != 0U) {
    audit_.committed_source_sequence = audit_.prepared_source_sequence;
  }
  prepared_.clear();
  prepared_tombstones_.clear();
  prepared_systems_.clear();
  prepared_frame_id_ = 0U;
  has_prepared_ = false;
  return true;
}

bool OgreNextN1ParticleRuntime::AdvanceDroppedFrame(
    std::uint64_t frame_id) noexcept {
  if (!has_prepared_ || frame_id != prepared_frame_id_ ||
      prepared_audit_.prepared_source_sequence ==
          audit_.committed_source_sequence ||
      prepared_audit_.dropped_source_frames ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return false;
  }
  ++prepared_audit_.dropped_source_frames;
  return Commit(frame_id);
}

void OgreNextN1ParticleRuntime::Abort(std::uint64_t frame_id) noexcept {
  if (!has_prepared_ || frame_id != prepared_frame_id_) {
    return;
  }
  prepared_.clear();
  prepared_tombstones_.clear();
  prepared_systems_.clear();
  prepared_frame_id_ = 0U;
  prepared_highest_event_id_ = highest_event_id_;
  prepared_highest_system_id_ = highest_system_id_;
  prepared_audit_ = audit_;
  has_prepared_ = false;
}

void OgreNextN1ParticleRuntime::Reset() noexcept {
  committed_.clear();
  prepared_.clear();
  committed_tombstones_.clear();
  prepared_tombstones_.clear();
  prepared_systems_.clear();
  audit_ = {};
  prepared_audit_ = {};
  prepared_frame_id_ = 0U;
  highest_event_id_ = 0U;
  prepared_highest_event_id_ = 0U;
  highest_system_id_ = 0U;
  prepared_highest_system_id_ = 0U;
  has_prepared_ = false;
}

OgreNextN1ParticleRuntimeAudit
OgreNextN1ParticleRuntime::audit() const noexcept {
  return audit_;
}

} // namespace RoR::Render
