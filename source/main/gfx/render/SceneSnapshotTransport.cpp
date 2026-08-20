/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshotTransport.h"

#include "RenderTransportDetail.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace RoR::Render {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t),
              "scene transport requires IEEE-754 binary32 storage");
static_assert(sizeof(double) == sizeof(std::uint64_t),
              "scene transport requires IEEE-754 binary64 storage");
static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<double>::is_iec559,
              "scene transport requires IEC 559 floating point");
static_assert(kSceneSnapshotVersion == kSceneSnapshotTransportSceneVersion,
              "a new scene schema requires a new transport message kind");
static_assert(kRenderFrameContractVersion ==
                  kSceneSnapshotTransportCameraVersion,
              "a new camera schema requires a new transport message kind");

using TransportDetail::AllocationBudget;
using TransportDetail::WireReader;
using TransportDetail::WireWriter;

constexpr std::size_t kMinimumMeshInstanceBytes = 230U;
constexpr std::size_t kMinimumLightBytes = 89U;
constexpr std::size_t kMinimumReflectionProbeBytes = 202U;
constexpr std::size_t kMinimumDynamicMeshUpdateBytes = 98U;
constexpr std::size_t kMinimumParticleEventBytes = 81U;

bool WriteFloat2(WireWriter &writer, const Float2 &value) {
  return writer.AddFloat(value.x) && writer.AddFloat(value.y);
}

bool WriteFloat3(WireWriter &writer, const Float3 &value) {
  return writer.AddFloat(value.x) && writer.AddFloat(value.y) &&
         writer.AddFloat(value.z);
}

bool WriteFloat4(WireWriter &writer, const Float4 &value) {
  return writer.AddFloat(value.x) && writer.AddFloat(value.y) &&
         writer.AddFloat(value.z) && writer.AddFloat(value.w);
}

bool WriteDouble3(WireWriter &writer, const Double3 &value) {
  return writer.AddDouble(value.x) && writer.AddDouble(value.y) &&
         writer.AddDouble(value.z);
}

bool WriteMatrix(WireWriter &writer, const Matrix4x4 &value) {
  for (const float element : value.elements) {
    if (!writer.AddFloat(element)) {
      return false;
    }
  }
  return true;
}

bool WriteBounds(WireWriter &writer, const Bounds3 &value) {
  return WriteFloat3(writer, value.minimum) &&
         WriteFloat3(writer, value.maximum);
}

bool WriteAssetReference(WireWriter &writer,
                         const RenderAssetReference &reference) {
  return writer.AddByte(static_cast<std::uint8_t>(reference.kind)) &&
         writer.AddU64(reference.id.high()) &&
         writer.AddU64(reference.id.low()) &&
         writer.AddU64(reference.revision);
}

bool WriteEnvironment(WireWriter &writer,
                      const SceneEnvironmentDescriptor &environment) {
  const AnalyticSkyDescriptor &sky = environment.analytic_sky;
  return WriteFloat3(writer, environment.ambient_radiance) &&
         WriteAssetReference(writer, environment.environment_texture) &&
         WriteAssetReference(writer, environment.environment_sampler) &&
         writer.AddFloat(environment.environment_intensity) &&
         writer.AddBool(sky.enabled) && writer.AddU64(sky.sun_light_id) &&
         WriteFloat3(writer, sky.zenith_radiance) &&
         WriteFloat3(writer, sky.horizon_radiance) &&
         WriteFloat3(writer, sky.ground_radiance) &&
         WriteFloat3(writer, sky.sun_disk_radiance) &&
         writer.AddFloat(sky.sun_angular_radius_radians) &&
         writer.AddFloat(sky.cloud_coverage) &&
         WriteFloat3(writer, sky.cloud_radiance) &&
         writer.AddFloat(sky.cloud_phase_radians) &&
         writer.AddFloat(sky.haze_extinction_per_meter) &&
         writer.AddFloat(sky.haze_inverse_scale_height_per_meter) &&
         writer.AddFloat(sky.haze_base_height_meters) &&
         writer.AddFloat(environment.exposure_compensation_ev);
}

bool WriteMeshInstance(WireWriter &writer,
                       const MeshInstanceDescriptor &instance) {
  return writer.AddU64(instance.instance_id) &&
         WriteAssetReference(writer, instance.mesh) &&
         WriteAssetReference(writer, instance.material) &&
         writer.AddU64(instance.topology_revision) &&
         writer.AddU64(instance.deformation_revision) &&
         WriteMatrix(writer, instance.render_from_object) &&
         WriteMatrix(writer, instance.previous_render_from_object) &&
         WriteBounds(writer, instance.local_bounds) &&
         writer.AddU32(instance.visibility_mask) && writer.AddU32(instance.flags);
}

bool WriteLight(WireWriter &writer, const LightDescriptor &light) {
  return writer.AddU64(light.light_id) &&
         writer.AddByte(static_cast<std::uint8_t>(light.type)) &&
         WriteFloat3(writer, light.color_linear) &&
         writer.AddFloat(light.intensity) && WriteFloat3(writer, light.position) &&
         WriteFloat3(writer, light.previous_position) &&
         WriteFloat3(writer, light.direction) &&
         WriteFloat3(writer, light.previous_direction) &&
         writer.AddFloat(light.range) &&
         writer.AddFloat(light.inner_cone_radians) &&
         writer.AddFloat(light.outer_cone_radians) &&
         writer.AddU32(light.shadow_flags);
}

bool WriteReflectionProbe(WireWriter &writer,
                          const ReflectionProbeRuntimeDescriptor &probe) {
  return writer.AddU32(probe.version) && writer.AddU64(probe.probe_id) &&
         writer.AddU64(probe.content_revision) &&
         WriteDouble3(writer, probe.absolute_world_position_meters) &&
         WriteMatrix(writer, probe.world_from_probe_orientation) &&
         WriteFloat3(writer, probe.capture_position_local) &&
         WriteFloat3(writer, probe.influence_center_local) &&
         WriteFloat3(writer, probe.influence_half_size) &&
         WriteFloat3(writer, probe.influence_inner_fraction) &&
         WriteFloat3(writer, probe.correction_shape_center_local) &&
         WriteFloat3(writer, probe.correction_shape_half_size) &&
         writer.AddU16(probe.priority) && writer.AddU16(probe.resolution) &&
         writer.AddFloat(probe.capture_near_meters) &&
         writer.AddFloat(probe.capture_far_meters) &&
         writer.AddU32(probe.visibility_mask) &&
         writer.AddByte(static_cast<std::uint8_t>(probe.update_mode)) &&
         writer.AddU64(probe.update_interval_simulation_ticks) &&
         writer.AddBool(probe.include_dynamic_geometry);
}

bool WriteFloat3Vector(WireWriter &writer,
                       const std::vector<Float3> &values) {
  if (!writer.AddU32(static_cast<std::uint32_t>(values.size()))) {
    return false;
  }
  for (const Float3 &value : values) {
    if (!WriteFloat3(writer, value)) {
      return false;
    }
  }
  return true;
}

bool WriteFloat4Vector(WireWriter &writer,
                       const std::vector<Float4> &values) {
  if (!writer.AddU32(static_cast<std::uint32_t>(values.size()))) {
    return false;
  }
  for (const Float4 &value : values) {
    if (!WriteFloat4(writer, value)) {
      return false;
    }
  }
  return true;
}

bool WriteDynamicMeshUpdate(WireWriter &writer,
                            const DynamicMeshUpdateDescriptor &update) {
  return writer.AddU64(update.update_sequence) &&
         writer.AddU64(update.instance_id) &&
         WriteAssetReference(writer, update.mesh) &&
         writer.AddU64(update.topology_revision) &&
         writer.AddU64(update.deformation_revision) &&
         WriteFloat3Vector(writer, update.positions) &&
         WriteFloat3Vector(writer, update.normals) &&
         WriteFloat4Vector(writer, update.tangents) &&
         WriteFloat3Vector(writer, update.velocities) &&
         writer.AddBool(update.has_updated_bounds) &&
         WriteBounds(writer, update.updated_local_bounds);
}

bool WriteParticleEvent(WireWriter &writer, const ParticleEvent &event) {
  return writer.AddU64(event.event_id) && writer.AddU64(event.emitter_id) &&
         writer.AddByte(static_cast<std::uint8_t>(event.effect)) &&
         WriteFloat3(writer, event.position) &&
         WriteFloat3(writer, event.velocity) &&
         WriteFloat4(writer, event.color_linear) &&
         writer.AddFloat(event.size_meters) &&
         writer.AddFloat(event.lifetime_seconds) &&
         writer.AddFloat(event.intensity) &&
         writer.AddU32(event.emission_count) && writer.AddU64(event.random_seed);
}

bool WriteCamera(WireWriter &writer, const CameraViewRequest &camera) {
  return writer.AddU64(camera.view_id) && writer.AddU32(camera.width) &&
         writer.AddU32(camera.height) &&
         WriteMatrix(writer, camera.view_from_render) &&
         WriteMatrix(writer, camera.clip_from_view) &&
         WriteMatrix(writer, camera.previous_view_from_render) &&
         WriteMatrix(writer, camera.previous_clip_from_view) &&
         WriteFloat2(writer, camera.temporal_jitter_pixels) &&
         writer.AddFloat(camera.near_plane) && writer.AddFloat(camera.far_plane) &&
         writer.AddFloat(camera.exposure) && writer.AddU32(camera.visibility_mask);
}

bool SceneCountsWithinLimits(const SceneSnapshot &scene) noexcept {
  if (scene.mesh_instances().size() >
          kSceneSnapshotTransportMaximumMeshInstances ||
      scene.lights().size() > kSceneSnapshotTransportMaximumLights ||
      scene.reflection_probes().size() >
          kSceneSnapshotTransportMaximumReflectionProbes ||
      scene.dynamic_mesh_updates().size() >
          kSceneSnapshotTransportMaximumDynamicMeshUpdates ||
      scene.particle_events().size() >
          kSceneSnapshotTransportMaximumParticleEvents) {
    return false;
  }
  std::uint64_t total_positions = 0U;
  for (const DynamicMeshUpdateDescriptor &update :
       scene.dynamic_mesh_updates()) {
    if (update.positions.size() >
            kSceneSnapshotTransportMaximumVerticesPerUpdate ||
        update.normals.size() >
            kSceneSnapshotTransportMaximumVerticesPerUpdate ||
        update.tangents.size() >
            kSceneSnapshotTransportMaximumVerticesPerUpdate ||
        update.velocities.size() >
            kSceneSnapshotTransportMaximumVerticesPerUpdate) {
      return false;
    }
    total_positions += static_cast<std::uint64_t>(update.positions.size());
    if (total_positions >
        kSceneSnapshotTransportMaximumPositionsPerMessage) {
      return false;
    }
  }
  return true;
}

bool WritePayload(WireWriter &writer, const SceneSnapshot &scene,
                  const CameraViewRequest &camera) {
  if (!writer.AddU32(kSceneSnapshotPayloadVersion) ||
      !writer.AddU32(kSceneSnapshotTransportSceneVersion) ||
      !writer.AddU32(kSceneSnapshotTransportCameraVersion) ||
      !writer.AddU32(0U) ||
      !writer.AddU64(scene.snapshot_id()) ||
      !writer.AddU64(scene.asset_registry_id()) ||
      !writer.AddU64(scene.asset_sequence()) ||
      !writer.AddU64(scene.simulation_tick()) ||
      !writer.AddDouble(scene.simulation_time_seconds()) ||
      !WriteDouble3(writer, scene.absolute_world_origin_meters()) ||
      !WriteEnvironment(writer, scene.environment()) ||
      !writer.AddU32(
          static_cast<std::uint32_t>(scene.mesh_instances().size()))) {
    return false;
  }
  for (const MeshInstanceDescriptor &instance : scene.mesh_instances()) {
    if (!WriteMeshInstance(writer, instance)) {
      return false;
    }
  }
  if (!writer.AddU32(static_cast<std::uint32_t>(scene.lights().size()))) {
    return false;
  }
  for (const LightDescriptor &light : scene.lights()) {
    if (!WriteLight(writer, light)) {
      return false;
    }
  }
  if (!writer.AddU32(
          static_cast<std::uint32_t>(scene.reflection_probes().size()))) {
    return false;
  }
  for (const ReflectionProbeRuntimeDescriptor &probe :
       scene.reflection_probes()) {
    if (!WriteReflectionProbe(writer, probe)) {
      return false;
    }
  }
  if (!writer.AddBool(scene.hud_overlay().enabled) ||
      !WriteAssetReference(writer, scene.hud_overlay().material)) {
    return false;
  }
  if (!writer.AddU32(
          static_cast<std::uint32_t>(scene.dynamic_mesh_updates().size()))) {
    return false;
  }
  for (const DynamicMeshUpdateDescriptor &update :
       scene.dynamic_mesh_updates()) {
    if (!WriteDynamicMeshUpdate(writer, update)) {
      return false;
    }
  }
  if (!writer.AddU32(
          static_cast<std::uint32_t>(scene.particle_events().size()))) {
    return false;
  }
  for (const ParticleEvent &event : scene.particle_events()) {
    if (!WriteParticleEvent(writer, event)) {
      return false;
    }
  }
  return WriteCamera(writer, camera);
}

bool ReadFloat2(WireReader &reader, Float2 &value) {
  return reader.ReadFloat(value.x) && reader.ReadFloat(value.y);
}

bool ReadFloat3(WireReader &reader, Float3 &value) {
  return reader.ReadFloat(value.x) && reader.ReadFloat(value.y) &&
         reader.ReadFloat(value.z);
}

bool ReadFloat4(WireReader &reader, Float4 &value) {
  return reader.ReadFloat(value.x) && reader.ReadFloat(value.y) &&
         reader.ReadFloat(value.z) && reader.ReadFloat(value.w);
}

bool ReadDouble3(WireReader &reader, Double3 &value) {
  return reader.ReadDouble(value.x) && reader.ReadDouble(value.y) &&
         reader.ReadDouble(value.z);
}

bool ReadMatrix(WireReader &reader, Matrix4x4 &value) {
  for (float &element : value.elements) {
    if (!reader.ReadFloat(element)) {
      return false;
    }
  }
  return true;
}

bool ReadBounds(WireReader &reader, Bounds3 &value) {
  return ReadFloat3(reader, value.minimum) &&
         ReadFloat3(reader, value.maximum);
}

bool ReadAssetReference(WireReader &reader, RenderAssetReference &reference) {
  std::uint8_t kind = 0U;
  std::uint64_t high = 0U;
  std::uint64_t low = 0U;
  std::uint64_t revision = 0U;
  if (!reader.ReadByte(kind) || !reader.ReadU64(high) ||
      !reader.ReadU64(low) || !reader.ReadU64(revision)) {
    return false;
  }
  reference.id = RenderAssetId::FromWords(high, low);
  reference.kind = static_cast<RenderAssetKind>(kind);
  reference.revision = revision;
  return true;
}

bool ReadEnvironment(WireReader &reader,
                     SceneEnvironmentDescriptor &environment) {
  AnalyticSkyDescriptor &sky = environment.analytic_sky;
  return ReadFloat3(reader, environment.ambient_radiance) &&
         ReadAssetReference(reader, environment.environment_texture) &&
         ReadAssetReference(reader, environment.environment_sampler) &&
         reader.ReadFloat(environment.environment_intensity) &&
         reader.ReadBool(sky.enabled) && reader.ReadU64(sky.sun_light_id) &&
         ReadFloat3(reader, sky.zenith_radiance) &&
         ReadFloat3(reader, sky.horizon_radiance) &&
         ReadFloat3(reader, sky.ground_radiance) &&
         ReadFloat3(reader, sky.sun_disk_radiance) &&
         reader.ReadFloat(sky.sun_angular_radius_radians) &&
         reader.ReadFloat(sky.cloud_coverage) &&
         ReadFloat3(reader, sky.cloud_radiance) &&
         reader.ReadFloat(sky.cloud_phase_radians) &&
         reader.ReadFloat(sky.haze_extinction_per_meter) &&
         reader.ReadFloat(sky.haze_inverse_scale_height_per_meter) &&
         reader.ReadFloat(sky.haze_base_height_meters) &&
         reader.ReadFloat(environment.exposure_compensation_ev);
}

bool ReadMeshInstance(WireReader &reader, MeshInstanceDescriptor &instance) {
  return reader.ReadU64(instance.instance_id) &&
         ReadAssetReference(reader, instance.mesh) &&
         ReadAssetReference(reader, instance.material) &&
         reader.ReadU64(instance.topology_revision) &&
         reader.ReadU64(instance.deformation_revision) &&
         ReadMatrix(reader, instance.render_from_object) &&
         ReadMatrix(reader, instance.previous_render_from_object) &&
         ReadBounds(reader, instance.local_bounds) &&
         reader.ReadU32(instance.visibility_mask) &&
         reader.ReadU32(instance.flags);
}

bool ReadLight(WireReader &reader, LightDescriptor &light) {
  std::uint8_t type = 0U;
  if (!reader.ReadU64(light.light_id) || !reader.ReadByte(type)) {
    return false;
  }
  light.type = static_cast<LightType>(type);
  return ReadFloat3(reader, light.color_linear) &&
         reader.ReadFloat(light.intensity) &&
         ReadFloat3(reader, light.position) &&
         ReadFloat3(reader, light.previous_position) &&
         ReadFloat3(reader, light.direction) &&
         ReadFloat3(reader, light.previous_direction) &&
         reader.ReadFloat(light.range) &&
         reader.ReadFloat(light.inner_cone_radians) &&
         reader.ReadFloat(light.outer_cone_radians) &&
         reader.ReadU32(light.shadow_flags);
}

bool ReadReflectionProbe(WireReader &reader,
                         ReflectionProbeRuntimeDescriptor &probe) {
  std::uint8_t update_mode = 0U;
  if (!reader.ReadU32(probe.version) || !reader.ReadU64(probe.probe_id) ||
      !reader.ReadU64(probe.content_revision) ||
      !ReadDouble3(reader, probe.absolute_world_position_meters) ||
      !ReadMatrix(reader, probe.world_from_probe_orientation) ||
      !ReadFloat3(reader, probe.capture_position_local) ||
      !ReadFloat3(reader, probe.influence_center_local) ||
      !ReadFloat3(reader, probe.influence_half_size) ||
      !ReadFloat3(reader, probe.influence_inner_fraction) ||
      !ReadFloat3(reader, probe.correction_shape_center_local) ||
      !ReadFloat3(reader, probe.correction_shape_half_size) ||
      !reader.ReadU16(probe.priority) || !reader.ReadU16(probe.resolution) ||
      !reader.ReadFloat(probe.capture_near_meters) ||
      !reader.ReadFloat(probe.capture_far_meters) ||
      !reader.ReadU32(probe.visibility_mask) ||
      !reader.ReadByte(update_mode) ||
      !reader.ReadU64(probe.update_interval_simulation_ticks) ||
      !reader.ReadBool(probe.include_dynamic_geometry)) {
    return false;
  }
  probe.update_mode = static_cast<ReflectionProbeUpdateMode>(update_mode);
  return true;
}

bool ReadFloat3Vector(WireReader &reader, std::vector<Float3> &values,
                      std::uint64_t *message_position_count) {
  std::uint32_t count = 0U;
  if (!reader.ReadCount(kSceneSnapshotTransportMaximumVerticesPerUpdate,
                        3U * sizeof(float), count)) {
    return false;
  }
  if (message_position_count != nullptr) {
    if (count > kSceneSnapshotTransportMaximumPositionsPerMessage -
                    *message_position_count) {
      reader.Fail(SceneSnapshotTransportStatus::COUNT_LIMIT_EXCEEDED);
      return false;
    }
    *message_position_count += count;
  }
  if (!reader.Reserve(values, count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    Float3 value;
    if (!ReadFloat3(reader, value)) {
      return false;
    }
    values.push_back(value);
  }
  return true;
}

bool ReadFloat4Vector(WireReader &reader, std::vector<Float4> &values) {
  std::uint32_t count = 0U;
  if (!reader.ReadCount(kSceneSnapshotTransportMaximumVerticesPerUpdate,
                        4U * sizeof(float), count) ||
      !reader.Reserve(values, count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    Float4 value;
    if (!ReadFloat4(reader, value)) {
      return false;
    }
    values.push_back(value);
  }
  return true;
}

bool ReadDynamicMeshUpdate(WireReader &reader,
                           DynamicMeshUpdateDescriptor &update,
                           std::uint64_t &message_position_count) {
  return reader.ReadU64(update.update_sequence) &&
         reader.ReadU64(update.instance_id) &&
         ReadAssetReference(reader, update.mesh) &&
         reader.ReadU64(update.topology_revision) &&
         reader.ReadU64(update.deformation_revision) &&
         ReadFloat3Vector(reader, update.positions, &message_position_count) &&
         ReadFloat3Vector(reader, update.normals, nullptr) &&
         ReadFloat4Vector(reader, update.tangents) &&
         ReadFloat3Vector(reader, update.velocities, nullptr) &&
         reader.ReadBool(update.has_updated_bounds) &&
         ReadBounds(reader, update.updated_local_bounds);
}

bool ReadParticleEvent(WireReader &reader, ParticleEvent &event) {
  std::uint8_t effect = 0U;
  if (!reader.ReadU64(event.event_id) || !reader.ReadU64(event.emitter_id) ||
      !reader.ReadByte(effect)) {
    return false;
  }
  event.effect = static_cast<ParticleEffect>(effect);
  return ReadFloat3(reader, event.position) &&
         ReadFloat3(reader, event.velocity) &&
         ReadFloat4(reader, event.color_linear) &&
         reader.ReadFloat(event.size_meters) &&
         reader.ReadFloat(event.lifetime_seconds) &&
         reader.ReadFloat(event.intensity) &&
         reader.ReadU32(event.emission_count) &&
         reader.ReadU64(event.random_seed);
}

bool ReadCamera(WireReader &reader, CameraViewRequest &camera) {
  return reader.ReadU64(camera.view_id) && reader.ReadU32(camera.width) &&
         reader.ReadU32(camera.height) &&
         ReadMatrix(reader, camera.view_from_render) &&
         ReadMatrix(reader, camera.clip_from_view) &&
         ReadMatrix(reader, camera.previous_view_from_render) &&
         ReadMatrix(reader, camera.previous_clip_from_view) &&
         ReadFloat2(reader, camera.temporal_jitter_pixels) &&
         reader.ReadFloat(camera.near_plane) &&
         reader.ReadFloat(camera.far_plane) &&
         reader.ReadFloat(camera.exposure) &&
         reader.ReadU32(camera.visibility_mask);
}

struct DecodedPayload {
  std::shared_ptr<const SceneSnapshot> scene;
  CameraViewRequest camera;
};

bool ReadPayload(const std::uint8_t *payload, std::size_t payload_size,
                 DecodedPayload &decoded,
                 SceneSnapshotTransportStatus &status) {
  AllocationBudget allocation_budget(
      kSceneSnapshotTransportMaximumDecodedAllocationBytes);
  WireReader reader(payload, payload_size, allocation_budget);
  SceneSnapshotDescriptor descriptor;
  std::uint32_t payload_version = 0U;
  std::uint32_t camera_version = 0U;
  std::uint32_t reserved = 0U;
  if (!reader.ReadU32(payload_version) ||
      !reader.ReadU32(descriptor.version) ||
      !reader.ReadU32(camera_version) || !reader.ReadU32(reserved)) {
    status = reader.status();
    return false;
  }
  if (payload_version != kSceneSnapshotPayloadVersion ||
      descriptor.version != kSceneSnapshotTransportSceneVersion ||
      camera_version != kSceneSnapshotTransportCameraVersion ||
      reserved != 0U) {
    status = SceneSnapshotTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return false;
  }
  if (!reader.ReadU64(descriptor.snapshot_id) ||
      !reader.ReadU64(descriptor.asset_registry_id) ||
      !reader.ReadU64(descriptor.asset_sequence) ||
      !reader.ReadU64(descriptor.simulation_tick) ||
      !reader.ReadDouble(descriptor.simulation_time_seconds) ||
      !ReadDouble3(reader, descriptor.absolute_world_origin_meters) ||
      !ReadEnvironment(reader, descriptor.environment)) {
    status = reader.status();
    return false;
  }

  std::uint32_t count = 0U;
  if (!reader.ReadCount(kSceneSnapshotTransportMaximumMeshInstances,
                        kMinimumMeshInstanceBytes, count) ||
      !reader.Reserve(descriptor.mesh_instances, count)) {
    status = reader.status();
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    MeshInstanceDescriptor instance;
    if (!ReadMeshInstance(reader, instance)) {
      status = reader.status();
      return false;
    }
    descriptor.mesh_instances.push_back(instance);
  }

  if (!reader.ReadCount(kSceneSnapshotTransportMaximumLights,
                        kMinimumLightBytes, count) ||
      !reader.Reserve(descriptor.lights, count)) {
    status = reader.status();
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    LightDescriptor light;
    if (!ReadLight(reader, light)) {
      status = reader.status();
      return false;
    }
    descriptor.lights.push_back(light);
  }

  if (!reader.ReadCount(kSceneSnapshotTransportMaximumReflectionProbes,
                        kMinimumReflectionProbeBytes, count) ||
      !reader.Reserve(descriptor.reflection_probes, count)) {
    status = reader.status();
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    ReflectionProbeRuntimeDescriptor probe;
    if (!ReadReflectionProbe(reader, probe)) {
      status = reader.status();
      return false;
    }
    descriptor.reflection_probes.push_back(probe);
  }

  if (!reader.ReadBool(descriptor.hud_overlay.enabled) ||
      !ReadAssetReference(reader, descriptor.hud_overlay.material)) {
    status = reader.status();
    return false;
  }

  if (!reader.ReadCount(kSceneSnapshotTransportMaximumDynamicMeshUpdates,
                        kMinimumDynamicMeshUpdateBytes, count) ||
      !reader.Reserve(descriptor.dynamic_mesh_updates, count)) {
    status = reader.status();
    return false;
  }
  std::uint64_t message_position_count = 0U;
  for (std::uint32_t index = 0U; index < count; ++index) {
    DynamicMeshUpdateDescriptor update;
    if (!ReadDynamicMeshUpdate(reader, update, message_position_count)) {
      status = reader.status();
      return false;
    }
    descriptor.dynamic_mesh_updates.push_back(std::move(update));
  }

  if (!reader.ReadCount(kSceneSnapshotTransportMaximumParticleEvents,
                        kMinimumParticleEventBytes, count) ||
      !reader.Reserve(descriptor.particle_events, count)) {
    status = reader.status();
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    ParticleEvent event;
    if (!ReadParticleEvent(reader, event)) {
      status = reader.status();
      return false;
    }
    descriptor.particle_events.push_back(event);
  }

  if (!ReadCamera(reader, decoded.camera)) {
    status = reader.status();
    return false;
  }
  if (!reader.consumed()) {
    status = SceneSnapshotTransportStatus::MALFORMED_PAYLOAD;
    return false;
  }

  SceneSnapshotCreateResult scene = CreateSceneSnapshot(std::move(descriptor));
  if (!scene || !ValidateCameraViewRequest(decoded.camera).ok()) {
    status = SceneSnapshotTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return false;
  }
  decoded.scene = std::move(scene.snapshot);
  status = SceneSnapshotTransportStatus::OK;
  return true;
}

SceneSnapshotTransportDecodeResult Failure(
    SceneSnapshotTransportStatus status) {
  SceneSnapshotTransportDecodeResult result;
  result.status = status;
  return result;
}

} // namespace

bool IsKnownSceneSnapshotTransportMessageKind(
    SceneSnapshotTransportMessageKind kind) noexcept {
  return kind ==
         SceneSnapshotTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2;
}

std::array<std::uint8_t, 32U>
ComputeSceneSnapshotTransportPayloadDigest(const std::uint8_t *payload,
                                           std::size_t payload_size) noexcept {
  return ComputeRenderTransportPayloadDigest(payload, payload_size);
}

DecodedSceneSnapshotTransportMessage::DecodedSceneSnapshotTransportMessage(
    std::uint64_t sequence, SceneSnapshotTransportMessageKind kind,
    std::shared_ptr<const SceneSnapshot> scene_snapshot,
    CameraViewRequest camera) noexcept
    : sequence_(sequence), kind_(kind),
      scene_snapshot_(std::move(scene_snapshot)), camera_(std::move(camera)) {}

SceneSnapshotTransportDecoder::SceneSnapshotTransportDecoder(
    std::uint64_t first_expected_sequence) noexcept
    : owned_sequence_state_(first_expected_sequence),
      sequence_state_(&owned_sequence_state_) {}

SceneSnapshotTransportDecoder::SceneSnapshotTransportDecoder(
    RenderTransportSequenceState &shared_sequence_state) noexcept
    : owned_sequence_state_(1U), sequence_state_(&shared_sequence_state) {}

SceneSnapshotTransportDecodeResult SceneSnapshotTransportDecoder::Accept(
    const std::vector<std::uint8_t> &frame) {
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus envelope_status = DecodeRenderTransportEnvelope(
      frame, kSceneSnapshotTransportMaximumPayloadBytes, envelope);
  if (envelope_status != RenderTransportStatus::OK) {
    return Failure(envelope_status);
  }
  if (!IsKnownSceneSnapshotTransportMessageKind(envelope.kind)) {
    return Failure(SceneSnapshotTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  const RenderTransportStatus sequence_status =
      sequence_state_->ValidateCandidate(envelope.sequence);
  if (sequence_status != RenderTransportStatus::OK) {
    return Failure(sequence_status);
  }

  try {
    DecodedPayload decoded;
    SceneSnapshotTransportStatus status =
        SceneSnapshotTransportStatus::MALFORMED_PAYLOAD;
    if (!ReadPayload(envelope.payload, envelope.payload_size, decoded, status)) {
      return Failure(status);
    }
    std::shared_ptr<const DecodedSceneSnapshotTransportMessage> candidate(
        new DecodedSceneSnapshotTransportMessage(
            envelope.sequence, envelope.kind, std::move(decoded.scene),
            std::move(decoded.camera)));
    if (!sequence_state_->CommitAccepted(envelope.sequence)) {
      return Failure(SceneSnapshotTransportStatus::INVALID_SEQUENCE);
    }
    published_ = candidate;
    return SceneSnapshotTransportDecodeResult{
        std::move(candidate), SceneSnapshotTransportStatus::OK};
  } catch (const std::bad_alloc &) {
    return Failure(SceneSnapshotTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(SceneSnapshotTransportStatus::ALLOCATION_FAILURE);
  }
}

SceneSnapshotTransportEncodeResult EncodeSceneSnapshotTransportFrame(
    std::uint64_t sequence, const SceneSnapshot &scene_snapshot,
    const CameraViewRequest &camera) {
  SceneSnapshotTransportEncodeResult result;
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)() ||
      scene_snapshot.version() != kSceneSnapshotTransportSceneVersion ||
      !ValidateCameraViewRequest(camera).ok()) {
    result.status = SceneSnapshotTransportStatus::INVALID_ARGUMENT;
    return result;
  }
  if (!SceneCountsWithinLimits(scene_snapshot)) {
    result.status = SceneSnapshotTransportStatus::COUNT_LIMIT_EXCEEDED;
    return result;
  }

  try {
    WireWriter sizer(nullptr, kSceneSnapshotTransportMaximumPayloadBytes);
    if (!WritePayload(sizer, scene_snapshot, camera) || !sizer.ok()) {
      result.status = SceneSnapshotTransportStatus::PAYLOAD_LIMIT_EXCEEDED;
      return result;
    }
    const std::size_t payload_size = static_cast<std::size_t>(sizer.size());
    std::vector<std::uint8_t> payload;
    payload.reserve(payload_size);
    WireWriter payload_writer(&payload,
                              kSceneSnapshotTransportMaximumPayloadBytes);
    if (!WritePayload(payload_writer, scene_snapshot, camera) ||
        !payload_writer.ok() || payload_writer.size() != payload_size) {
      result.status = SceneSnapshotTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    return EncodeRenderTransportEnvelope(
        SceneSnapshotTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2,
        sequence, payload, kSceneSnapshotTransportMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    result.bytes.clear();
    result.status = SceneSnapshotTransportStatus::ALLOCATION_FAILURE;
    return result;
  } catch (const std::length_error &) {
    result.bytes.clear();
    result.status = SceneSnapshotTransportStatus::ALLOCATION_FAILURE;
    return result;
  }
}

} // namespace RoR::Render
