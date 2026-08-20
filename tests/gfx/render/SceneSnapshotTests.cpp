/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshot.h"
#include "RenderAssetRegistry.h"
#include "RenderResourceDescriptors.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "scene snapshot test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::RenderAssetReference Asset(RoR::Render::RenderAssetKind kind,
                                        std::uint64_t value,
                                        std::uint64_t revision = 1U) {
  return RoR::Render::RenderAssetReference::Create(
      kind, RoR::Render::RenderAssetId::FromWords(0x5CE0EU, value), revision);
}

RoR::Render::SceneSnapshotDescriptor MakeValidDescriptor() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 17U;
  descriptor.asset_registry_id = 31U;
  descriptor.asset_sequence = 7U;
  descriptor.simulation_tick = 2000U;
  descriptor.simulation_time_seconds = 1.0;
  descriptor.environment.environment_texture =
      Asset(RenderAssetKind::TEXTURE, 1U);
  descriptor.environment.environment_sampler =
      Asset(RenderAssetKind::SAMPLER, 4U);
  descriptor.environment.environment_intensity = 1.25F;
  descriptor.environment.exposure_compensation_ev = -0.5F;
  descriptor.environment.analytic_sky.enabled = true;
  descriptor.environment.analytic_sky.sun_light_id = 30U;
  descriptor.environment.analytic_sky.zenith_radiance = {0.08F, 0.12F, 0.2F};
  descriptor.environment.analytic_sky.horizon_radiance = {0.3F, 0.25F, 0.2F};
  descriptor.environment.analytic_sky.ground_radiance = {0.02F, 0.018F,
                                                          0.015F};
  descriptor.environment.analytic_sky.sun_disk_radiance = {9000.0F, 8500.0F,
                                                            7200.0F};
  descriptor.environment.analytic_sky.sun_angular_radius_radians = 0.00465F;
  descriptor.environment.analytic_sky.cloud_coverage = 0.45F;
  descriptor.environment.analytic_sky.cloud_radiance = {0.1675F, 0.169375F,
                                                        0.17125F};
  descriptor.environment.analytic_sky.cloud_phase_radians = 0.4F;
  descriptor.environment.analytic_sky.haze_extinction_per_meter = 9.78e-5F;
  descriptor.environment.analytic_sky.haze_inverse_scale_height_per_meter =
      8.3333333e-4F;
  descriptor.environment.analytic_sky.haze_base_height_meters = 0.0F;

  MeshInstanceDescriptor instance;
  instance.instance_id = 10U;
  instance.mesh = Asset(RenderAssetKind::MESH, 2U);
  instance.material = Asset(RenderAssetKind::MATERIAL, 3U);
  instance.topology_revision = 4U;
  instance.deformation_revision = 9U;
  instance.local_bounds.minimum = {-1.0F, -0.5F, -2.0F};
  instance.local_bounds.maximum = {1.0F, 0.5F, 2.0F};
  descriptor.mesh_instances.push_back(instance);

  LightDescriptor light;
  light.light_id = 20U;
  light.type = LightType::SPOT;
  light.position = {0.0F, 4.0F, 0.0F};
  light.previous_position = light.position;
  light.range = 30.0F;
  light.inner_cone_radians = 0.5F;
  light.outer_cone_radians = 0.75F;
  descriptor.lights.push_back(light);

  LightDescriptor sun;
  sun.light_id = 30U;
  sun.intensity = 110000.0F;
  sun.direction = {0.0F, -0.8F, -0.6F};
  sun.previous_direction = sun.direction;
  descriptor.lights.push_back(sun);

  ReflectionProbeRuntimeDescriptor probe;
  probe.probe_id = 35U;
  probe.absolute_world_position_meters = {1000000012.5, -2000000000.0,
                                          3000000002.0};
  probe.resolution = 32U;
  probe.influence_half_size = {4.0F, 3.0F, 2.0F};
  probe.correction_shape_half_size = {5.0F, 4.0F, 3.0F};
  probe.capture_far_meters = 16.0F;
  descriptor.reflection_probes.push_back(probe);

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
  descriptor.reflection_probes.clear();

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
  Require(created.snapshot->reflection_probes().size() == 1U &&
              created.snapshot->reflection_probe_hash() ==
                  ComputeSceneReflectionProbeHash(MakeValidDescriptor()),
          "snapshot retained mutable reflection probes or lost their hash");
  Require(created.snapshot->lighting_environment_hash() ==
              ComputeSceneLightingEnvironmentHash(
                  MakeValidDescriptor()),
          "snapshot did not retain the exact immutable lighting hash");
}

void TestIdentityOrderAndResourceValidation() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  descriptor.version = 3U;
  RequireInvalid(descriptor, ValidationCode::UNSUPPORTED_VERSION,
                 "legacy scene snapshot version three was accepted implicitly");

  descriptor = MakeValidDescriptor();
  descriptor.version = 2U;
  RequireInvalid(descriptor, ValidationCode::UNSUPPORTED_VERSION,
                 "legacy scene snapshot version two was accepted implicitly");

  descriptor = MakeValidDescriptor();
  descriptor.version = 1U;
  RequireInvalid(descriptor, ValidationCode::UNSUPPORTED_VERSION,
                 "legacy scene snapshot version one was accepted implicitly");

  descriptor = MakeValidDescriptor();
  descriptor.snapshot_id = 0U;
  RequireInvalid(descriptor, ValidationCode::INVALID_IDENTIFIER,
                 "zero snapshot identifier was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.asset_registry_id = 0U;
  RequireInvalid(descriptor, ValidationCode::INVALID_IDENTIFIER,
                 "zero asset registry identifier was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.asset_sequence = 0U;
  RequireInvalid(descriptor, ValidationCode::INVALID_IDENTIFIER,
                 "zero asset catalog sequence was accepted");

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
  descriptor.mesh_instances.front().mesh =
      Asset(RenderAssetKind::TEXTURE, 2U);
  RequireInvalid(descriptor, ValidationCode::WRONG_ASSET_KIND,
                 "texture asset was accepted as a mesh");

  descriptor = MakeValidDescriptor();
  descriptor.mesh_instances.front().material = {};
  RequireInvalid(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
                 "missing material asset was accepted");

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
  descriptor.environment.environment_texture = {};
  descriptor.environment.environment_sampler = {};
  descriptor.environment.environment_texture.id =
      RenderAssetId::FromWords(0x5CE0EU, 1U);
  RequireInvalid(
      descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
      "partially populated environment texture was treated as absent");

  descriptor = MakeValidDescriptor();
  descriptor.environment.environment_texture = {};
  descriptor.environment.environment_sampler = {};
  descriptor.environment.environment_sampler.revision = 1U;
  RequireInvalid(
      descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
      "partially populated environment sampler was treated as absent");

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

void TestReflectionProbeSnapshotContract() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "valid reflection-probe snapshot was rejected");
  const std::uint64_t baseline = ComputeSceneReflectionProbeHash(descriptor);
  Require(baseline != 0U,
          "valid reflection-probe snapshot hash was zero");

  SceneSnapshotDescriptor unrelated = descriptor;
  unrelated.snapshot_id += 1U;
  unrelated.simulation_tick += 1U;
  unrelated.simulation_time_seconds += 1.0;
  unrelated.absolute_world_origin_meters = {1000000000.0, -2000000000.0,
                                            3000000000.0};
  Require(ComputeSceneReflectionProbeHash(unrelated) == baseline,
          "reflection-probe hash included frame identity or render origin");

  SceneSnapshotDescriptor changed = descriptor;
  ++changed.reflection_probes.front().content_revision;
  changed.reflection_probes.front().capture_position_local.x = 0.25F;
  Require(ValidateSceneSnapshotDescriptor(changed).ok() &&
              ComputeSceneReflectionProbeHash(changed) != baseline,
          "reflection-probe hash omitted authored revision or geometry");

  SceneSnapshotDescriptor signed_zero = descriptor;
  signed_zero.reflection_probes.front().absolute_world_position_meters.y =
      -0.0;
  descriptor.reflection_probes.front().absolute_world_position_meters.y = 0.0;
  Require(ComputeSceneReflectionProbeHash(signed_zero) ==
              ComputeSceneReflectionProbeHash(descriptor),
          "reflection-probe snapshot hash did not canonicalize signed zero");

  SceneSnapshotDescriptor duplicate = MakeValidDescriptor();
  duplicate.reflection_probes.push_back(duplicate.reflection_probes.front());
  RequireInvalid(duplicate, ValidationCode::DUPLICATE_IDENTIFIER,
                 "duplicate reflection-probe identity was accepted");

  SceneSnapshotDescriptor unsorted = MakeValidDescriptor();
  ReflectionProbeRuntimeDescriptor earlier =
      unsorted.reflection_probes.front();
  earlier.probe_id -= 1U;
  unsorted.reflection_probes.push_back(earlier);
  RequireInvalid(unsorted, ValidationCode::NON_DETERMINISTIC_ORDER,
                 "unsorted reflection-probe set was accepted");

  SceneSnapshotDescriptor malformed = MakeValidDescriptor();
  malformed.reflection_probes.front().resolution = 96U;
  RequireInvalid(malformed, ValidationCode::INVALID_DIMENSIONS,
                 "malformed reflection-probe descriptor was accepted");
}

void TestDynamicMeshValidation() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().instance_id = 999U;
  RequireInvalid(descriptor, ValidationCode::MISSING_REFERENCE,
                 "update referencing a missing instance was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.dynamic_mesh_updates.front().mesh =
      Asset(RenderAssetKind::MESH, 9U);
  RequireInvalid(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
                 "update mesh asset different from instance was accepted");

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
  descriptor.lights.front().color_linear = {};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "black photometric multiplier was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().color_linear = {2.0F, 2.0F, 2.0F};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "unnormalized photometric multiplier was accepted");

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
  descriptor.lights.front().previous_direction = {0.0F, -2.0F, 0.0F};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "non-unit previous light direction was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.back().range = 10.0F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "directional light with noncanonical local range was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.lights.front().shadow_flags = 1U << 31U;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "unknown light shadow flag was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.exposure_compensation_ev = 25.0F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "unbounded scene exposure compensation was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.sun_light_id = 999U;
  RequireInvalid(descriptor, ValidationCode::MISSING_REFERENCE,
                 "analytic sky with a missing sun light was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.sun_light_id =
      descriptor.lights.front().light_id;
  RequireInvalid(descriptor, ValidationCode::WRONG_RESOURCE_KIND,
                 "analytic sky with a spot-light sun was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.enabled = false;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "disabled analytic sky retained noncanonical live fields");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.cloud_coverage = 1.5F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "cloud coverage above one was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.cloud_coverage = -0.25F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "negative cloud coverage was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.cloud_radiance = {0.1F, -0.1F, 0.1F};
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "negative cloud radiance was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.cloud_phase_radians =
      std::numeric_limits<float>::quiet_NaN();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "non-finite cloud phase was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky = {};
  descriptor.environment.analytic_sky.cloud_phase_radians = 0.1F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "disabled analytic sky retained live cloud state");

  // Aerial haze. Zero extinction under an enabled sky is deliberately legal
  // and means exactly no haze; everything outside the reviewed bounds, and
  // any live haze state under a disabled sky, must fail closed.
  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_extinction_per_meter = 0.0F;
  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "zero haze extinction under an enabled sky was rejected");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_extinction_per_meter = -1.0e-6F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "negative haze extinction was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_extinction_per_meter = 1.1e-2F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "haze extinction above the reviewed bound was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_extinction_per_meter =
      std::numeric_limits<float>::quiet_NaN();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "non-finite haze extinction was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_inverse_scale_height_per_meter =
      -1.0e-6F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "negative haze inverse scale height was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_inverse_scale_height_per_meter =
      1.1e-1F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "haze inverse scale height above the reviewed bound was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_inverse_scale_height_per_meter =
      std::numeric_limits<float>::infinity();
  RequireInvalid(descriptor, ValidationCode::NON_FINITE_VALUE,
                 "non-finite haze inverse scale height was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_base_height_meters = 2.0e5F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "haze base height above the reviewed bound was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky.haze_base_height_meters = -2.0e5F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "haze base height below the reviewed bound was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky = {};
  descriptor.environment.analytic_sky.haze_extinction_per_meter = 1.0e-5F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "disabled analytic sky retained live haze extinction");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky = {};
  descriptor.environment.analytic_sky.haze_inverse_scale_height_per_meter =
      1.0e-4F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "disabled analytic sky retained live haze scale height");

  descriptor = MakeValidDescriptor();
  descriptor.environment.analytic_sky = {};
  descriptor.environment.analytic_sky.haze_base_height_meters = 1.0F;
  RequireInvalid(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
                 "disabled analytic sky retained a live haze base height");

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

void TestPhotometricColorAndExposureContracts() {
  using namespace RoR::Render;

  Require(ComputeLinearSrgbRec709D65Luminance({1.0F, 1.0F, 1.0F}) ==
              1.0,
          "linear-sRGB white did not have exact unit luminance");
  const Float3 inputs[] = {{1.0F, 0.0F, 0.0F},
                           {0.0F, 1.0F, 0.0F},
                           {0.0F, 0.0F, 1.0F},
                           {1.0F, 0.5F, 0.25F}};
  for (const Float3 &input : inputs) {
    Float3 normalized{-1.0F, -1.0F, -1.0F};
    Require(NormalizePhotometricColorLinear(input, normalized) &&
                IsCanonicalPhotometricColorLinear(normalized),
            "white/saturated/tinted color did not normalize canonically");
  }
  Require(IsCanonicalPhotometricColorLinear({1.0F, 1.0F, 1.0F}),
          "canonical white photometry was rejected");
  Require(!IsCanonicalPhotometricColorLinear({}) &&
              !IsCanonicalPhotometricColorLinear({2.0F, 2.0F, 2.0F}) &&
              !IsCanonicalPhotometricColorLinear({-1.0F, 1.0F, 1.0F}) &&
              !IsCanonicalPhotometricColorLinear(
                  {std::numeric_limits<float>::infinity(), 1.0F, 1.0F}),
          "zero, unnormalized, or nonfinite photometry was accepted");
  Float3 unchanged{7.0F, 8.0F, 9.0F};
  Require(!NormalizePhotometricColorLinear({}, unchanged) &&
              unchanged == Float3{7.0F, 8.0F, 9.0F},
          "failed photometric normalization modified its output");

  float effective = -1.0F;
  Require(ComputePortableEffectiveExposure(1.0F, 1.0F, effective) &&
              effective == 2.0F,
          "portable effective exposure rejected exact normal binary32");
  const float preserved = effective;
  Require(!ComputePortableEffectiveExposure(
              (std::numeric_limits<float>::max)(), 24.0F, effective) &&
              effective == preserved,
          "overflowing effective exposure was accepted or modified output");
  Require(!ComputePortableEffectiveExposure(
              (std::numeric_limits<float>::min)(), -24.0F, effective) &&
              effective == preserved,
          "subnormal effective exposure was accepted or modified output");
}

void TestAnalyticSunMembership() {
  using namespace RoR::Render;

  AnalyticSkyDescriptor sky;
  sky.enabled = true;
  sky.sun_light_id = 99U;
  sky.sun_angular_radius_radians = 0.25F;
  LightDescriptor sun;
  sun.light_id = sky.sun_light_id;
  sun.direction = {0.0F, -1.0F, 0.0F};
  sun.previous_direction = {0.0F, 0.0F, 1.0F};

  Require(IsViewDirectionInsideAnalyticSunDisk({0.0F, 1.0F, 0.0F}, sky,
                                                sun),
          "analytic sun center was outside its disk");
  Require(!IsViewDirectionInsideAnalyticSunDisk({0.0F, -1.0F, 0.0F}, sky,
                                                 sun),
          "direction opposite the analytic sun was inside its disk");
  const float boundary = static_cast<float>(
      std::cos(static_cast<double>(sky.sun_angular_radius_radians)));
  const float boundary_x = static_cast<float>(
      std::sin(static_cast<double>(sky.sun_angular_radius_radians)));
  const double boundary_length = std::sqrt(
      static_cast<double>(boundary_x) * boundary_x +
      static_cast<double>(boundary) * boundary);
  const float normalized_boundary_dot =
      static_cast<float>(static_cast<double>(boundary) / boundary_length);
  Require(normalized_boundary_dot == boundary &&
              IsViewDirectionInsideAnalyticSunDisk(
              {boundary_x, boundary, 0.0F}, sky, sun),
          "inclusive analytic sun boundary was rejected");
  Require(IsViewDirectionInsideAnalyticSunDisk(
              {boundary_x, boundary + 0.001F, 0.0F}, sky, sun) &&
              !IsViewDirectionInsideAnalyticSunDisk(
                  {boundary_x, boundary - 0.001F, 0.0F}, sky, sun),
          "analytic sun just-inside/just-outside membership is wrong");
  sun.direction = {0.0F, -3.0F, 0.0F};
  Require(IsViewDirectionInsideAnalyticSunDisk({0.0F, 9.0F, 0.0F}, sky,
                                                sun),
          "approximately scaled view/emitted directions were not normalized");
  Require(!IsViewDirectionInsideAnalyticSunDisk(
              {0.0F, 1.0F, 0.0F}, sky, sun, LightHistorySample::PREVIOUS) &&
              IsViewDirectionInsideAnalyticSunDisk(
                  {0.0F, 0.0F, -2.0F}, sky, sun,
                  LightHistorySample::PREVIOUS),
          "current and previous sun direction samples were conflated");
}

void TestShadowGeometryClassificationAndMasks() {
  using namespace RoR::Render;

  MeshResourceDescriptor static_mesh;
  MeshResourceDescriptor dynamic_mesh;
  dynamic_mesh.dynamic = true;
  MeshInstanceDescriptor moving_base_instance;
  moving_base_instance.deformation_revision = 1U;
  moving_base_instance.render_from_object.elements[12U] = 100.0F;
  moving_base_instance.previous_render_from_object.elements[12U] = -100.0F;
  LightDescriptor light;

  Require(ClassifyShadowGeometry(static_mesh) == ShadowGeometryClass::STATIC &&
              ClassifyShadowGeometry(dynamic_mesh) ==
                  ShadowGeometryClass::DYNAMIC,
          "mesh resource dynamic bit did not define shadow class");
  Require(ClassifyShadowGeometry(static_mesh) == ShadowGeometryClass::STATIC &&
              ClassifyShadowGeometry(dynamic_mesh) ==
                  ShadowGeometryClass::DYNAMIC &&
              moving_base_instance.deformation_revision == 1U &&
              moving_base_instance.render_from_object !=
                  moving_base_instance.previous_render_from_object,
          "motion or base deformation revision changed authored shadow class");

  light.shadow_flags = 0U;
  Require(!LightShadowMaskIncludesGeometry(light, static_mesh) &&
              !LightShadowMaskIncludesGeometry(light, dynamic_mesh),
          "zero shadow mask included geometry");
  light.shadow_flags = LIGHT_SHADOW_STATIC_GEOMETRY;
  Require(LightShadowMaskIncludesGeometry(light, static_mesh) &&
              !LightShadowMaskIncludesGeometry(light, dynamic_mesh),
          "static-only shadow mask classification is wrong");
  light.shadow_flags = LIGHT_SHADOW_DYNAMIC_GEOMETRY;
  Require(!LightShadowMaskIncludesGeometry(light, static_mesh) &&
              LightShadowMaskIncludesGeometry(light, dynamic_mesh),
          "dynamic-only shadow mask classification is wrong");
  light.shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
  Require(LightShadowMaskIncludesGeometry(light, static_mesh) &&
              LightShadowMaskIncludesGeometry(light, dynamic_mesh) &&
              MeshInstanceCastsShadowForLight(light, moving_base_instance,
                                              static_mesh),
          "default shadow mask did not include both authored classes");
  moving_base_instance.flags &= ~MESH_INSTANCE_CASTS_SHADOW;
  Require(!MeshInstanceCastsShadowForLight(light, moving_base_instance,
                                           static_mesh),
          "instance casts-shadow flag was ignored");
}

void TestHudOverlayContract() {
  using namespace RoR::Render;

  // Disabled default: the canonical absent reference is the only valid
  // disabled payload.
  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  Require(!descriptor.hud_overlay.enabled &&
              ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "default-disabled HUD overlay was rejected");
  descriptor.hud_overlay.material = Asset(RenderAssetKind::MATERIAL, 60U);
  RequireInvalid(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
                 "disabled HUD overlay with a bound material was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.hud_overlay.enabled = true;
  RequireInvalid(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
                 "enabled HUD overlay without a material was accepted");

  descriptor = MakeValidDescriptor();
  descriptor.hud_overlay.enabled = true;
  descriptor.hud_overlay.material = Asset(RenderAssetKind::TEXTURE, 60U);
  RequireInvalid(descriptor, ValidationCode::WRONG_ASSET_KIND,
                 "texture asset was accepted as the HUD overlay material");

  // Registry-resolved profile requirements. A minimal scene carries only the
  // HUD reference so the walk exercises exactly the HUD resolution rules.
  const RenderAssetReference hud_texture =
      Asset(RenderAssetKind::TEXTURE, 70U);
  const RenderAssetReference hud_sampler =
      Asset(RenderAssetKind::SAMPLER, 71U);
  const RenderAssetReference hud_material =
      Asset(RenderAssetKind::MATERIAL, 72U);

  TextureResourceDescriptor texture;
  texture.debug_name = "HudTex";
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor mip;
  mip.width = 2U;
  mip.height = 2U;
  mip.row_pitch_bytes = 8U;
  mip.layer_pitch_bytes = 16U;
  mip.bytes.assign(16U, 0x80U);
  texture.mip_levels.push_back(mip);

  SamplerResourceDescriptor sampler;
  sampler.debug_name = "HudSampler";
  sampler.mip_filter = SamplerFilter::NEAREST;
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_w = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.maximum_lod = 0.0F;

  MaterialDescriptor material;
  material.debug_name = "HudMaterial";
  material.model = MaterialModel::UNLIT;
  material.blend_mode = MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER;
  material.base_color_transfer =
      BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
  material.depth_write = false;
  material.base_color_texture.texture = hud_texture;
  material.base_color_texture.sampler = hud_sampler;

  const auto make_registry = [&](const MaterialDescriptor &hud_material_state,
                                 const TextureResourceDescriptor
                                     &hud_texture_state) {
    auto registry = std::make_unique<RenderAssetRegistry>(31U);
    RenderAssetDelta delta;
    delta.registry_id = 31U;
    delta.base_sequence = 0U;
    delta.sequence = 1U;
    delta.full_snapshot = true;
    RenderAssetMutation texture_mutation;
    texture_mutation.asset = hud_texture;
    texture_mutation.payload = hud_texture_state;
    delta.mutations.push_back(std::move(texture_mutation));
    RenderAssetMutation sampler_mutation;
    sampler_mutation.asset = hud_sampler;
    sampler_mutation.payload = sampler;
    delta.mutations.push_back(std::move(sampler_mutation));
    RenderAssetMutation material_mutation;
    material_mutation.asset = hud_material;
    material_mutation.payload = hud_material_state;
    delta.mutations.push_back(std::move(material_mutation));
    Require(registry->Apply(delta).ok(),
            "HUD overlay registry fixture could not be applied");
    return registry;
  };

  SceneSnapshotDescriptor hud_scene;
  hud_scene.snapshot_id = 1U;
  hud_scene.asset_registry_id = 31U;
  hud_scene.asset_sequence = 1U;
  hud_scene.hud_overlay.enabled = true;
  hud_scene.hud_overlay.material = hud_material;
  Require(ValidateSceneSnapshotDescriptor(hud_scene).ok(),
          "enabled HUD overlay scene fixture must be valid");

  {
    const auto registry = make_registry(material, texture);
    Require(ValidateSceneSnapshotAssets(hud_scene, *registry).ok(),
            "canonical HUD overlay material profile was rejected");
  }
  {
    auto registry = std::make_unique<RenderAssetRegistry>(31U);
    RenderAssetDelta delta;
    delta.registry_id = 31U;
    delta.base_sequence = 0U;
    delta.sequence = 1U;
    delta.full_snapshot = true;
    Require(registry->Apply(delta).ok(),
            "empty HUD registry fixture could not be applied");
    const ValidationResult missing =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!missing && missing.code == ValidationCode::MISSING_REFERENCE,
            "HUD overlay with a missing material was accepted");
  }
  {
    MaterialDescriptor wrong_model = material;
    wrong_model.model = MaterialModel::PBR_METALLIC_ROUGHNESS;
    wrong_model.base_color_transfer =
        BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
    wrong_model.blend_mode = MaterialBlendMode::STRAIGHT_SOURCE_OVER;
    const auto registry = make_registry(wrong_model, texture);
    const ValidationResult rejected =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!rejected &&
                rejected.code == ValidationCode::UNSUPPORTED_FEATURE,
            "non-UNLIT HUD overlay material was accepted");
  }
  {
    MaterialDescriptor wrong_blend = material;
    wrong_blend.blend_mode = MaterialBlendMode::STRAIGHT_SOURCE_OVER;
    const auto registry = make_registry(wrong_blend, texture);
    const ValidationResult rejected =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!rejected &&
                rejected.code == ValidationCode::UNSUPPORTED_FEATURE,
            "non-premultiplied HUD overlay blend was accepted");
  }
  {
    MaterialDescriptor depth_writer = material;
    depth_writer.depth_write = true;
    const auto registry = make_registry(depth_writer, texture);
    const ValidationResult rejected =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!rejected &&
                rejected.code == ValidationCode::UNSUPPORTED_FEATURE,
            "depth-writing HUD overlay material was accepted");
  }
  {
    TextureResourceDescriptor two_mips = texture;
    TextureMipLevelDescriptor tail;
    tail.width = 1U;
    tail.height = 1U;
    tail.row_pitch_bytes = 4U;
    tail.layer_pitch_bytes = 4U;
    tail.bytes.assign(4U, 0x80U);
    two_mips.mip_levels.push_back(tail);
    const auto registry = make_registry(material, two_mips);
    const ValidationResult rejected =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!rejected &&
                rejected.code == ValidationCode::VALUE_OUT_OF_RANGE,
            "multi-mip HUD overlay texture was accepted");
  }
  {
    MaterialDescriptor unbound = material;
    unbound.base_color_texture = {};
    const auto registry = make_registry(unbound, texture);
    const ValidationResult rejected =
        ValidateSceneSnapshotAssets(hud_scene, *registry);
    Require(!rejected &&
                rejected.code == ValidationCode::MISSING_REFERENCE,
            "texture-free HUD overlay material was accepted");
  }
}

void TestCanonicalLightingEnvironmentHash() {
  using namespace RoR::Render;

  SceneSnapshotDescriptor descriptor = MakeValidDescriptor();
  const std::uint64_t baseline =
      ComputeSceneLightingEnvironmentHash(descriptor);
  Require(baseline == 2759767521327219205ULL,
          "canonical lighting hash fixture drifted");

  SceneSnapshotDescriptor unrelated = descriptor;
  unrelated.snapshot_id += 1U;
  unrelated.simulation_tick += 1U;
  unrelated.simulation_time_seconds += 1.0;
  unrelated.mesh_instances.front().visibility_mask = 0x7FFFFFFFU;
  unrelated.particle_events.front().random_seed += 1U;
  // The HUD overlay is deliberately not lighting state.
  unrelated.hud_overlay.enabled = true;
  unrelated.hud_overlay.material = Asset(RenderAssetKind::MATERIAL, 60U);
  Require(ComputeSceneLightingEnvironmentHash(unrelated) == baseline,
          "lighting digest included unrelated frame or geometry state");

  SceneSnapshotDescriptor changed_registry = descriptor;
  changed_registry.asset_registry_id += 1U;
  Require(ComputeSceneLightingEnvironmentHash(changed_registry) != baseline,
          "lighting digest omitted asset registry identity");
  SceneSnapshotDescriptor changed_sequence = descriptor;
  changed_sequence.asset_sequence += 1U;
  Require(ComputeSceneLightingEnvironmentHash(changed_sequence) == baseline,
          "lighting digest included asset sequence churn");

  SceneSnapshotDescriptor signed_zero = descriptor;
  signed_zero.lights.front().position.x = -0.0F;
  signed_zero.lights.front().previous_position.z = -0.0F;
  signed_zero.absolute_world_origin_meters.y = -0.0;
  Require(ValidateSceneSnapshotDescriptor(signed_zero).ok() &&
              ComputeSceneLightingEnvironmentHash(signed_zero) == baseline,
          "lighting digest did not canonicalize signed zero");

  SceneSnapshotDescriptor changed_history = descriptor;
  changed_history.lights.front().previous_position.x = 0.25F;
  Require(ComputeSceneLightingEnvironmentHash(changed_history) != baseline,
          "lighting digest omitted temporal transform history");

  SceneSnapshotDescriptor changed_sky = descriptor;
  changed_sky.environment.analytic_sky.zenith_radiance.x += 0.01F;
  Require(ComputeSceneLightingEnvironmentHash(changed_sky) != baseline,
          "lighting digest omitted analytic environment state");

  SceneSnapshotDescriptor changed_cloud = descriptor;
  changed_cloud.environment.analytic_sky.cloud_coverage += 0.01F;
  Require(ComputeSceneLightingEnvironmentHash(changed_cloud) != baseline,
          "lighting digest omitted cloud coverage");
  SceneSnapshotDescriptor changed_cloud_phase = descriptor;
  changed_cloud_phase.environment.analytic_sky.cloud_phase_radians += 0.01F;
  Require(ComputeSceneLightingEnvironmentHash(changed_cloud_phase) !=
              baseline,
          "lighting digest omitted cloud phase");

  SceneSnapshotDescriptor changed_haze_extinction = descriptor;
  changed_haze_extinction.environment.analytic_sky
      .haze_extinction_per_meter += 1.0e-5F;
  Require(ComputeSceneLightingEnvironmentHash(changed_haze_extinction) !=
              baseline,
          "lighting digest omitted aerial haze extinction");
  SceneSnapshotDescriptor changed_haze_scale_height = descriptor;
  changed_haze_scale_height.environment.analytic_sky
      .haze_inverse_scale_height_per_meter += 1.0e-5F;
  Require(ComputeSceneLightingEnvironmentHash(changed_haze_scale_height) !=
              baseline,
          "lighting digest omitted aerial haze scale height");
  SceneSnapshotDescriptor changed_haze_base_height = descriptor;
  changed_haze_base_height.environment.analytic_sky
      .haze_base_height_meters += 5.0F;
  Require(ComputeSceneLightingEnvironmentHash(changed_haze_base_height) !=
              baseline,
          "lighting digest omitted aerial haze base height");

  SceneSnapshotDescriptor reordered = descriptor;
  std::swap(reordered.lights[0U], reordered.lights[1U]);
  RequireInvalid(reordered, ValidationCode::NON_DETERMINISTIC_ORDER,
                 "noncanonical light traversal order was accepted");
}

} // namespace

int main() {
  TestValidSnapshotIsDeepCopiedAndImmutable();
  TestIdentityOrderAndResourceValidation();
  TestDynamicMeshValidation();
  TestWorldLightAndParticleValidation();
  TestReflectionProbeSnapshotContract();
  TestPhotometricColorAndExposureContracts();
  TestAnalyticSunMembership();
  TestShadowGeometryClassificationAndMasks();
  TestHudOverlayContract();
  TestCanonicalLightingEnvironmentHash();
  std::cout << "scene snapshot tests passed\n";
  return EXIT_SUCCESS;
}
