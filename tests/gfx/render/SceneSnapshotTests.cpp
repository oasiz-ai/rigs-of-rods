/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "scene snapshot test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::ResourceHandle Handle(RoR::Render::ResourceKind kind,
                                   std::uint32_t slot) {
  return RoR::Render::ResourceHandle::Create(kind, 1U, slot, 1U);
}

RoR::Render::SceneSnapshotDescriptor MakeValidDescriptor() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 17U;
  descriptor.simulation_tick = 2000U;
  descriptor.simulation_time_seconds = 1.0;
  descriptor.environment.environment_texture =
      Handle(ResourceKind::TEXTURE, 0U);
  descriptor.environment.environment_sampler =
      Handle(ResourceKind::SAMPLER, 3U);

  MeshInstanceDescriptor instance;
  instance.instance_id = 10U;
  instance.mesh = Handle(ResourceKind::MESH, 1U);
  instance.material = Handle(ResourceKind::MATERIAL, 2U);
  instance.topology_revision = 4U;
  instance.deformation_revision = 9U;
  instance.local_bounds.minimum = {-1.0F, -0.5F, -2.0F};
  instance.local_bounds.maximum = {1.0F, 0.5F, 2.0F};
  descriptor.mesh_instances.push_back(instance);

  LightDescriptor light;
  light.light_id = 20U;
  light.type = LightType::SPOT;
  light.position = {0.0F, 4.0F, 0.0F};
  descriptor.lights.push_back(light);

  DynamicMeshUpdateDescriptor update;
  update.update_sequence = 30U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.topology_revision = 4U;
  update.deformation_revision = 9U;
  update.positions = {{-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
  update.normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  update.velocities = {{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}};
  update.has_updated_bounds = true;
  update.updated_local_bounds = instance.local_bounds;
  descriptor.dynamic_mesh_updates.push_back(update);

  ParticleEvent particle;
  particle.event_id = 40U;
  particle.emitter_id = 50U;
  particle.effect = ParticleEffect::TIRE_SMOKE;
  particle.position = {0.0F, 0.1F, -1.0F};
  particle.velocity = {0.0F, 0.5F, -0.1F};
  particle.emission_count = 12U;
  particle.random_seed = 0xA55AU;
  descriptor.particle_events.push_back(particle);
  return descriptor;
}

void RequireInvalid(const RoR::Render::SceneSnapshotDescriptor &descriptor,
                    RoR::Render::ValidationCode expected, const char *message) {
  const RoR::Render::ValidationResult validation =
      RoR::Render::ValidateSceneSnapshotDescriptor(descriptor);
  Require(!validation && validation.code == expected, message);
  const RoR::Render::SceneSnapshotCreateResult created =
      RoR::Render::CreateSceneSnapshot(descriptor);
  Require(!created && created.snapshot == nullptr &&
              created.validation.code == expected,
          "factory accepted a descriptor rejected by validation");
}

void TestValidSnapshotIsDeepCopiedAndImmutable() {
  using namespace RoR::Render;

  static_assert(!std::is_copy_constructible_v<SceneSnapshot>,
                "immutable snapshots must not be copied into mutable values");
  static_assert(!std::is_move_assignable_v<SceneSnapshot>,
                "immutable snapshots must not be reassigned");
  static_assert(std::is_same_v<decltype(SceneSnapshotCreateResult{}.snapshot),
                               std::shared_ptr<const SceneSnapshot>>,
                "snapshot factory must expose shared const ownership");

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "valid snapshot descriptor was rejected");
  const SceneSnapshotCreateResult created = CreateSceneSnapshot(descriptor);
  Require(created.ok(), "valid snapshot was not created");

  descriptor.snapshot_id = 99U;
  descriptor.mesh_instances.front().instance_id = 999U;
  descriptor.dynamic_mesh_updates.front().positions.front().x = 1234.0F;
  descriptor.particle_events.clear();

  Require(created.snapshot->snapshot_id() == 17U,
          "snapshot retained mutable descriptor identity");
  Require(created.snapshot->mesh_instances().front().instance_id == 10U,
          "snapshot retained mutable instance storage");
  Require(
      created.snapshot->dynamic_mesh_updates().front().positions.front().x ==
          -1.0F,
      "snapshot retained mutable dynamic vertices");
  Require(created.snapshot->particle_events().size() == 1U,
          "snapshot retained mutable particle storage");
}

void TestIdentityOrderAndResourceValidation() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  descriptor.snapshot_id = 0U;
  RequireInvalid(descriptor, ValidationCode::INVALID_IDENTIFIER,
                 "zero snapshot identifier was accepted");

  descriptor = MakeValidDescriptor();
  MeshInstanceDescriptor duplicate = descriptor.mesh_instances.front();
  descriptor.mesh_instances.push_back(duplicate);
  RequireInvalid(descriptor, ValidationCode::DUPLICATE_IDENTIFIER,
                 "duplicate instance identifier was accepted");

  descriptor = MakeValidDescriptor();
  MeshInstanceDescriptor earlier = descriptor.mesh_instances.front();
  earlier.instance_id = 5U;
  descriptor.mesh_instances.push_back(earlier);
  RequireInvalid(descriptor, ValidationCode::NON_DETERMINISTIC_ORDER,
                 "nondeterministic instance order was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().mesh = Handle(ResourceKind::TEXTURE, 1U);
  RequireInvalid(descriptor, ValidationCode::WRONG_RESOURCE_KIND,
                 "texture handle was accepted as a mesh");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().material = {};
  RequireInvalid(descriptor, ValidationCode::INVALID_HANDLE,
                 "missing material handle was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.environment_sampler = {};
  RequireInvalid(
      descriptor, ValidationCode::MISSING_REFERENCE,
      "environment texture without an explicit sampler was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.environment_texture = {};
  RequireInvalid(descriptor, ValidationCode::MISSING_REFERENCE,
                 "environment sampler without a texture was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().flags = 1U << 31U;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "unknown mesh instance flag was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().deformation_revision = 0U;
  RequireInvalid(descriptor, ValidationCode::INVALID_IDENTIFIER,
                 "zero instance deformation revision was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().render_from_object.elements[0U] = 0.0F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "singular object transform was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().render_from_object.elements[3U] = 0.01F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "projective object transform was accepted as affine");

  descriptor = MakeValidDescriptor();
  const float huge = (std::numeric_limits<float>::max)();
  descriptor.mesh_instances.front().render_from_object.elements[0U] = huge;
  descriptor.mesh_instances.front().render_from_object.elements[5U] = huge;
  descriptor.mesh_instances.front().render_from_object.elements[10U] = huge;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "transform with an overflowing determinant was accepted");
}

void TestDynamicMeshValidation() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().instance_id = 999U;
  RequireInvalid(descriptor, ValidationCode::MISSING_REFERENCE,
                 "update referencing a missing instance was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().mesh = Handle(ResourceKind::MESH, 9U);
  RequireInvalid(descriptor, ValidationCode::INVALID_HANDLE,
                 "update mesh different from instance mesh was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().deformation_revision = 10U;
  RequireInvalid(descriptor, ValidationCode::MISSING_REFERENCE,
                 "update with a different deformation revision was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().positions.clear();
  RequireInvalid(descriptor, ValidationCode::EMPTY_PAYLOAD,
                 "empty deformation update was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.clear();
  RequireInvalid(
      descriptor, ValidationCode::MISSING_REFERENCE,
      "non-base deformation revision without full state was accepted");

  descriptor = MakeValidDescriptor();
  DynamicMeshUpdateDescriptor duplicate =
      descriptor.dynamic_mesh_updates.front();
  duplicate.update_sequence += 1U;
  descriptor.dynamic_mesh_updates.push_back(duplicate);
  RequireInvalid(descriptor, ValidationCode::DUPLICATE_IDENTIFIER,
                 "multiple partial updates for one instance were accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().has_updated_bounds = false;
  RequireInvalid(descriptor, ValidationCode::INVALID_BOUNDS,
                 "full deformation without authored bounds was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().updated_local_bounds.maximum.x = 2.0F;
  RequireInvalid(descriptor, ValidationCode::INVALID_BOUNDS,
                 "deformation and instance bounds disagreed");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().deformation_revision = 1U;
  descriptor.dynamic_mesh_updates.clear();
  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "base deformation revision without an update was rejected");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().normals.pop_back();
  RequireInvalid(descriptor, ValidationCode::SIZE_MISMATCH,
                 "mismatched vertex stream was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().positions.front().x =
      std::numeric_limits<float>::infinity();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "infinite dynamic vertex was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().normals.front() = {0.0F, 2.0F, 0.0F};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "non-unit dynamic normal was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().tangents = {{1.0F, 0.0F, 0.0F, 0.0F},
                                                      {1.0F, 0.0F, 0.0F, 1.0F}};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "tangent without unit handedness was accepted");
}

void TestWorldLightAndParticleValidation() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  descriptor.simulation_time_seconds = std::numeric_limits<double>::quiet_NaN();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "NaN simulation time was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.absolute_world_origin_meters.x =
      std::numeric_limits<double>::infinity();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "infinite absolute render origin was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().local_bounds.minimum.x = 2.0F;
  RequireInvalid(descriptor, ValidationCode::INVALID_BOUNDS,
                 "inverted mesh bounds were accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().type = static_cast<LightType>(255U);
  RequireInvalid(descriptor, ValidationCode::INVALID_ENUM,
                 "unknown light type was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().outer_cone_radians = 0.25F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "inverted spot cone was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().range = std::numeric_limits<float>::quiet_NaN();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "NaN light range was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().direction = {0.0F, -2.0F, 0.0F};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "non-unit light direction was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.particle_events.front().effect = static_cast<ParticleEffect>(255U);
  RequireInvalid(descriptor, ValidationCode::INVALID_ENUM,
                 "unknown particle effect was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.particle_events.front().emission_count = 0U;
  RequireInvalid(descriptor, ValidationCode::EMPTY_PAYLOAD,
                 "zero-count particle event was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.particle_events.front().color_linear.w = 1.1F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "particle alpha above one was accepted");
}

} // namespace

int main() {
  TestValidSnapshotIsDeepCopiedAndImmutable();
  TestIdentityOrderAndResourceValidation();
  TestDynamicMeshValidation();
  TestWorldLightAndParticleValidation();
  std::cout << "scene snapshot tests passed\n";
  return EXIT_SUCCESS;
}
