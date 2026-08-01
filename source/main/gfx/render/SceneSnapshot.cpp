/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshot.h"

#include "MaterialDescriptor.h"
#include "RenderAssetRegistry.h"
#include "RenderResourceDescriptors.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult ValidateAssetReference(const RenderAssetReference &reference,
                                        RenderAssetKind kind,
                                        const char *field,
                                        std::size_t index) {
  if (!reference.valid()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, field,
        "required renderer-neutral asset reference is invalid", index);
  }
  if (reference.kind != kind) {
    return ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND, field,
                                     "asset kind does not match", index);
  }
  return ValidationResult::Success();
}

ValidationResult ValidateIncreasingIdentifier(std::uint64_t identifier,
                                              std::uint64_t previous,
                                              bool has_previous,
                                              const char *field,
                                              std::size_t index) {
  if (identifier == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER, field,
                                     "identifier must be nonzero", index);
  }
  if (has_previous && identifier == previous) {
    return ValidationResult::Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                                     field, "identifier is duplicated", index);
  }
  if (has_previous && identifier < previous) {
    return ValidationResult::Failure(
        ValidationCode::NON_DETERMINISTIC_ORDER, field,
        "identifiers must be strictly increasing", index);
  }
  return ValidationResult::Success();
}

bool EqualBounds(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return lhs.minimum.x == rhs.minimum.x && lhs.minimum.y == rhs.minimum.y &&
         lhs.minimum.z == rhs.minimum.z && lhs.maximum.x == rhs.maximum.x &&
         lhs.maximum.y == rhs.maximum.y && lhs.maximum.z == rhs.maximum.z;
}

ValidationResult
ValidateFiniteVertices(const DynamicMeshUpdateDescriptor &update,
                       std::size_t update_index) {
  for (const Float3 &position : update.positions) {
    if (!IsFinite(position)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.positions",
          "all positions must be finite", update_index);
    }
  }
  for (const Float3 &normal : update.normals) {
    if (!IsFinite(normal)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.normals",
          "all normals must be finite", update_index);
    }
    if (!IsNormalized(normal)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.normals",
          "all supplied normals must have unit length", update_index);
    }
  }
  for (const Float4 &tangent : update.tangents) {
    if (!IsFinite(tangent)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.tangents",
          "all tangents must be finite", update_index);
    }
    if (!IsNormalizedTangent(tangent)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.tangents",
          "tangent xyz must have unit length and handedness must be -1 or 1",
          update_index);
    }
  }
  for (const Float3 &velocity : update.velocities) {
    if (!IsFinite(velocity)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.velocities",
          "all velocities must be finite", update_index);
    }
  }
  return ValidationResult::Success();
}

} // namespace

bool IsKnownLightType(LightType type) noexcept {
  switch (type) {
  case LightType::DIRECTIONAL:
  case LightType::POINT:
  case LightType::SPOT:
    return true;
  }
  return false;
}

bool IsKnownParticleEffect(ParticleEffect effect) noexcept {
  switch (effect) {
  case ParticleEffect::TIRE_SMOKE:
  case ParticleEffect::DUST:
  case ParticleEffect::SPARKS:
  case ParticleEffect::WATER_SPRAY:
  case ParticleEffect::STEAM:
  case ParticleEffect::FIRE:
  case ParticleEffect::DEBRIS:
    return true;
  }
  return false;
}

ValidationResult
ValidateSceneSnapshotDescriptor(const SceneSnapshotDescriptor &descriptor) {
  if (descriptor.version != kSceneSnapshotVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported scene snapshot version");
  }
  if (descriptor.snapshot_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "snapshot_id",
                                     "snapshot identifier must be nonzero");
  }
  if (descriptor.asset_registry_id == 0U || descriptor.asset_sequence == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        descriptor.asset_registry_id == 0U ? "asset_registry_id"
                                           : "asset_sequence",
        "asset registry identity and sequence must be nonzero");
  }
  if (!IsFinite(descriptor.simulation_time_seconds) ||
      descriptor.simulation_time_seconds < 0.0) {
    return ValidationResult::Failure(
        IsFinite(descriptor.simulation_time_seconds)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "simulation_time_seconds",
        "simulation time must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.absolute_world_origin_meters)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "absolute_world_origin_meters",
                                     "absolute render origin must be finite");
  }
  if (!IsNonNegative(descriptor.environment.ambient_radiance)) {
    return ValidationResult::Failure(
        IsFinite(descriptor.environment.ambient_radiance)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "environment.ambient_radiance",
        "ambient radiance must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.environment.environment_intensity) ||
      descriptor.environment.environment_intensity < 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.environment.environment_intensity)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "environment.environment_intensity",
        "environment intensity must be finite and nonnegative");
  }
  const bool environment_texture_absent = IsAbsentRenderAssetReference(
      descriptor.environment.environment_texture);
  const bool environment_sampler_absent = IsAbsentRenderAssetReference(
      descriptor.environment.environment_sampler);
  if ((!environment_texture_absent &&
       !descriptor.environment.environment_texture.valid()) ||
      (!environment_sampler_absent &&
       !descriptor.environment.environment_sampler.valid())) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, "environment",
        "optional environment references must be canonical absent or fully valid");
  }
  if (!environment_texture_absent &&
      descriptor.environment.environment_texture.kind !=
          RenderAssetKind::TEXTURE) {
    return ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND,
                                     "environment.environment_texture",
                                     "environment resource must be a texture");
  }
  if (!environment_sampler_absent &&
      descriptor.environment.environment_sampler.kind !=
          RenderAssetKind::SAMPLER) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_ASSET_KIND, "environment.environment_sampler",
        "environment sampler resource must be a sampler");
  }
  if (environment_texture_absent != environment_sampler_absent) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "environment",
        "environment texture and explicit sampler must be supplied together");
  }

  std::uint64_t previous_identifier = 0U;
  for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
       ++index) {
    const MeshInstanceDescriptor &instance = descriptor.mesh_instances[index];
    ValidationResult validation = ValidateIncreasingIdentifier(
        instance.instance_id, previous_identifier, index != 0U,
        "mesh_instances.instance_id", index);
    if (!validation) {
      return validation;
    }
    previous_identifier = instance.instance_id;

    validation = ValidateAssetReference(instance.mesh, RenderAssetKind::MESH,
                                        "mesh_instances.mesh", index);
    if (!validation) {
      return validation;
    }
    validation = ValidateAssetReference(instance.material,
                                        RenderAssetKind::MATERIAL,
                                        "mesh_instances.material", index);
    if (!validation) {
      return validation;
    }
    if (instance.topology_revision == 0U ||
        instance.deformation_revision == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "mesh_instances.revision",
          "topology and deformation revisions must be nonzero", index);
    }
    if (!HasInvertibleAffineTransform(instance.render_from_object) ||
        !HasInvertibleAffineTransform(instance.previous_render_from_object)) {
      return ValidationResult::Failure(
          IsFinite(instance.render_from_object) &&
                  IsFinite(instance.previous_render_from_object)
              ? ValidationCode::VALUE_OUT_OF_RANGE
              : ValidationCode::NON_FINITE_VALUE,
          "mesh_instances.transform",
          "current and previous transforms must be canonical affine and "
          "invertible",
          index);
    }
    if (!IsValid(instance.local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS, "mesh_instances.local_bounds",
          "mesh bounds must be finite and ordered", index);
    }
    if (instance.visibility_mask == 0U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "mesh_instances.visibility_mask",
          "visibility mask must contain at least one bit", index);
    }
    constexpr std::uint32_t kKnownMeshFlags =
        MESH_INSTANCE_CASTS_SHADOW | MESH_INSTANCE_RECEIVES_SHADOW |
        MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
    if ((instance.flags & ~kKnownMeshFlags) != 0U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "mesh_instances.flags",
          "mesh instance contains unknown flag bits", index);
    }
  }

  previous_identifier = 0U;
  constexpr float kHalfPi = 1.57079632679489661923F;
  for (std::size_t index = 0U; index < descriptor.lights.size(); ++index) {
    const LightDescriptor &light = descriptor.lights[index];
    ValidationResult validation =
        ValidateIncreasingIdentifier(light.light_id, previous_identifier,
                                     index != 0U, "lights.light_id", index);
    if (!validation) {
      return validation;
    }
    previous_identifier = light.light_id;
    if (!IsKnownLightType(light.type)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "lights.type", "unknown light type",
                                       index);
    }
    if (!IsFinite(light.color_linear) || !IsFinite(light.intensity) ||
        !IsFinite(light.position) || !IsFinite(light.direction) ||
        !IsFinite(light.range) || !IsFinite(light.inner_cone_radians) ||
        !IsFinite(light.outer_cone_radians)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "lights.photometry",
          "all light numeric fields must be finite", index);
    }
    if (!IsNonNegative(light.color_linear) || light.intensity < 0.0F ||
        light.range < 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.photometry",
          "light values must be nonnegative", index);
    }
    if (!IsNormalized(light.direction)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.direction",
          "light direction must have unit length", index);
    }
    if (light.type != LightType::DIRECTIONAL && light.range <= 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.range",
          "local light range must be positive", index);
    }
    if (light.inner_cone_radians < 0.0F ||
        light.outer_cone_radians < light.inner_cone_radians ||
        light.outer_cone_radians > kHalfPi) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.cone",
          "spot cones must satisfy 0 <= inner <= outer <= pi/2", index);
    }
  }

  previous_identifier = 0U;
  std::vector<std::uint64_t> updated_instance_ids;
  for (std::size_t index = 0U; index < descriptor.dynamic_mesh_updates.size();
       ++index) {
    const DynamicMeshUpdateDescriptor &update =
        descriptor.dynamic_mesh_updates[index];
    ValidationResult validation = ValidateIncreasingIdentifier(
        update.update_sequence, previous_identifier, index != 0U,
        "dynamic_mesh_updates.update_sequence", index);
    if (!validation) {
      return validation;
    }
    previous_identifier = update.update_sequence;
    if (update.instance_id == 0U || update.topology_revision == 0U ||
        update.deformation_revision == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          update.instance_id == 0U ? "dynamic_mesh_updates.instance_id"
                                   : "dynamic_mesh_updates.revision",
          "instance and geometry revisions must be nonzero", index);
    }
    validation = ValidateAssetReference(update.mesh, RenderAssetKind::MESH,
                                        "dynamic_mesh_updates.mesh", index);
    if (!validation) {
      return validation;
    }
    const auto instance = std::lower_bound(
        descriptor.mesh_instances.begin(), descriptor.mesh_instances.end(),
        update.instance_id,
        [](const MeshInstanceDescriptor &candidate, std::uint64_t id) {
          return candidate.instance_id < id;
        });
    if (instance == descriptor.mesh_instances.end() ||
        instance->instance_id != update.instance_id) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "dynamic_mesh_updates.instance_id",
          "dynamic mesh update references a missing instance", index);
    }
    if (instance->mesh != update.mesh) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_ASSET_REFERENCE,
          "dynamic_mesh_updates.mesh",
          "dynamic mesh asset differs from the referenced instance", index);
    }
    if (instance->topology_revision != update.topology_revision ||
        instance->deformation_revision != update.deformation_revision) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "dynamic_mesh_updates.revision",
          "dynamic update revisions must match the referenced instance", index);
    }
    if (update.deformation_revision == 1U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "dynamic_mesh_updates.deformation_revision",
          "base deformation revision one uses mesh resource contents", index);
    }
    if (std::find(updated_instance_ids.begin(), updated_instance_ids.end(),
                  update.instance_id) != updated_instance_ids.end()) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "dynamic_mesh_updates.instance_id",
          "version 1 allows exactly one full update per deformed instance",
          index);
    }
    updated_instance_ids.push_back(update.instance_id);
    if (update.positions.empty()) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "dynamic_mesh_updates.positions",
          "dynamic mesh update requires positions", index);
    }
    if (update.positions.size() > (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.positions",
          "dynamic vertex count exceeds 32-bit mesh indexing", index);
    }
    const std::size_t vertex_count = update.positions.size();
    if ((!update.normals.empty() && update.normals.size() != vertex_count) ||
        (!update.tangents.empty() && update.tangents.size() != vertex_count) ||
        (!update.velocities.empty() &&
         update.velocities.size() != vertex_count)) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "dynamic_mesh_updates.vertex_streams",
          "optional vertex streams must match the position count", index);
    }
    validation = ValidateFiniteVertices(update, index);
    if (!validation) {
      return validation;
    }
    if (!update.has_updated_bounds || !IsValid(update.updated_local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS,
          "dynamic_mesh_updates.updated_local_bounds",
          "full deformation state requires finite ordered updated bounds",
          index);
    }
    if (!EqualBounds(update.updated_local_bounds, instance->local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS,
          "dynamic_mesh_updates.updated_local_bounds",
          "full update bounds must exactly equal the instance bounds", index);
    }
  }

  for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
       ++index) {
    const MeshInstanceDescriptor &instance = descriptor.mesh_instances[index];
    const bool has_update =
        std::find(updated_instance_ids.begin(), updated_instance_ids.end(),
                  instance.instance_id) != updated_instance_ids.end();
    if ((instance.deformation_revision > 1U) != has_update) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "mesh_instances.deformation_revision",
          "every non-base deformation revision requires one full update",
          index);
    }
  }

  previous_identifier = 0U;
  for (std::size_t index = 0U; index < descriptor.particle_events.size();
       ++index) {
    const ParticleEvent &event = descriptor.particle_events[index];
    ValidationResult validation = ValidateIncreasingIdentifier(
        event.event_id, previous_identifier, index != 0U,
        "particle_events.event_id", index);
    if (!validation) {
      return validation;
    }
    previous_identifier = event.event_id;
    if (event.emitter_id == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "particle_events.emitter_id",
          "emitter identifier must be nonzero", index);
    }
    if (!IsKnownParticleEffect(event.effect)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "particle_events.effect",
                                       "unknown particle effect", index);
    }
    if (!IsFinite(event.position) || !IsFinite(event.velocity) ||
        !IsFinite(event.color_linear) || !IsFinite(event.size_meters) ||
        !IsFinite(event.lifetime_seconds) || !IsFinite(event.intensity)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "particle_events.payload",
          "all particle numeric fields must be finite", index);
    }
    if (!IsNonNegative(Float3{event.color_linear.x, event.color_linear.y,
                              event.color_linear.z}) ||
        event.color_linear.w < 0.0F || event.color_linear.w > 1.0F ||
        event.size_meters <= 0.0F || event.lifetime_seconds <= 0.0F ||
        event.intensity < 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "particle_events.payload",
          "particle values must be finite and physically nonnegative", index);
    }
    if (event.emission_count == 0U) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "particle_events.emission_count",
          "particle event must emit at least one particle", index);
    }
  }

  return ValidationResult::Success();
}

ValidationResult ValidateSceneSnapshotAssets(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry) {
  ValidationResult validation = ValidateSceneSnapshotDescriptor(descriptor);
  if (!validation) {
    return validation;
  }
  if (descriptor.asset_registry_id != registry.registry_id()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "asset_registry_id",
        "scene references a different renderer-neutral asset registry");
  }
  if (descriptor.asset_sequence != registry.sequence()) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
        "scene requires a different asset registry sequence");
  }

  if (descriptor.environment.environment_texture.valid()) {
    const TextureResourceDescriptor *texture = registry.ResolveTexture(
        descriptor.environment.environment_texture);
    const SamplerResourceDescriptor *sampler = registry.ResolveSampler(
        descriptor.environment.environment_sampler);
    if (texture == nullptr || sampler == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "environment",
          "environment references a missing, stale, or tombstoned asset");
    }
    validation = ValidateEnvironmentTextureCompatibility(*texture, *sampler);
    if (!validation) {
      return validation;
    }
  }

  for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
       ++index) {
    const MeshInstanceDescriptor &instance = descriptor.mesh_instances[index];
    const MeshResourceDescriptor *mesh = registry.ResolveMesh(instance.mesh);
    const MaterialDescriptor *material =
        registry.ResolveMaterial(instance.material);
    if (mesh == nullptr || material == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "mesh_instances.asset",
          "instance references a missing, stale, or tombstoned asset", index);
    }
    validation = ValidateMaterialMeshCompatibility(*material, *mesh);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }

    const auto update = std::find_if(
        descriptor.dynamic_mesh_updates.begin(),
        descriptor.dynamic_mesh_updates.end(),
        [&instance](const DynamicMeshUpdateDescriptor &candidate) {
          return candidate.instance_id == instance.instance_id;
        });
    const DynamicMeshUpdateDescriptor *update_ptr =
        update == descriptor.dynamic_mesh_updates.end() ? nullptr : &*update;
    validation =
        ValidateMeshInstanceCompatibility(*mesh, instance, update_ptr);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
  }

  for (std::size_t index = 0U;
       index < descriptor.dynamic_mesh_updates.size(); ++index) {
    const DynamicMeshUpdateDescriptor &update =
        descriptor.dynamic_mesh_updates[index];
    const MeshResourceDescriptor *mesh = registry.ResolveMesh(update.mesh);
    if (mesh == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "dynamic_mesh_updates.mesh",
          "deformation references a missing, stale, or tombstoned mesh",
          index);
    }
    validation = ValidateDynamicMeshUpdateCompatibility(*mesh, update);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
  }

  return ValidationResult::Success();
}

ValidationResult
ValidateSceneSnapshotAssets(const SceneSnapshot &snapshot,
                            const RenderAssetRegistry &registry) {
  return ValidateSceneSnapshotAssets(snapshot.descriptor_, registry);
}

SceneSnapshotCreateResult
CreateSceneSnapshot(SceneSnapshotDescriptor descriptor) {
  SceneSnapshotCreateResult result;
  result.validation = ValidateSceneSnapshotDescriptor(descriptor);
  if (!result.validation) {
    return result;
  }
  result.snapshot = std::shared_ptr<const SceneSnapshot>(
      new SceneSnapshot(std::move(descriptor)));
  return result;
}

} // namespace RoR::Render
