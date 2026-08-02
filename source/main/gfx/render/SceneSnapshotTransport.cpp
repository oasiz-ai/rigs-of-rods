/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshotTransport.h"

#include <algorithm>
#include <array>
#include <cstring>
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

constexpr std::uint16_t kHeaderFlags = 0U;
constexpr std::size_t kPayloadDigestOffset = 32U;
constexpr std::size_t kMinimumMeshInstanceBytes = 230U;
constexpr std::size_t kMinimumLightBytes = 89U;
constexpr std::size_t kMinimumReflectionProbeBytes = 202U;
constexpr std::size_t kMinimumDynamicMeshUpdateBytes = 98U;
constexpr std::size_t kMinimumParticleEventBytes = 81U;

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

constexpr std::uint32_t RotateRight(std::uint32_t value,
                                    std::uint32_t amount) noexcept {
  return (value >> amount) | (value << (32U - amount));
}

class Sha256 final {
public:
  void Update(const std::uint8_t *bytes, std::size_t size) noexcept {
    for (std::size_t index = 0U; index < size; ++index) {
      block_[block_size_++] = bytes[index];
      if (block_size_ == block_.size()) {
        Transform();
        total_bytes_ += block_.size();
        block_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32U> Final() noexcept {
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(total_bytes_ + block_size_) * 8ULL;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0U;
      }
      Transform();
      block_size_ = 0U;
    }
    while (block_size_ < 56U) {
      block_[block_size_++] = 0U;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_count >> (index * 8U));
    }
    Transform();

    std::array<std::uint8_t, 32U> digest{};
    for (std::size_t word = 0U; word < state_.size(); ++word) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        digest[word * 4U + byte] = static_cast<std::uint8_t>(
            state_[word] >> ((3U - byte) * 8U));
      }
    }
    return digest;
  }

private:
  void Transform() noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block_[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t before = words[index - 15U];
      const std::uint32_t after = words[index - 2U];
      const std::uint32_t sigma0 = RotateRight(before, 7U) ^
                                   RotateRight(before, 18U) ^ (before >> 3U);
      const std::uint32_t sigma1 = RotateRight(after, 17U) ^
                                   RotateRight(after, 19U) ^ (after >> 10U);
      words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];

    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^
                                 RotateRight(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary1 =
          h + sum1 + choose + kSha256RoundConstants[index] + words[index];
      const std::uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^
                                 RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
  }

  std::array<std::uint32_t, 8U> state_{{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  }};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_ = 0U;
  std::size_t total_bytes_ = 0U;
};

class WireWriter final {
public:
  WireWriter(std::vector<std::uint8_t> *output,
             std::uint64_t maximum_bytes) noexcept
      : output_(output), maximum_bytes_(maximum_bytes) {}

  bool AddByte(std::uint8_t value) {
    if (!Advance(1U)) {
      return false;
    }
    if (output_ != nullptr) {
      output_->push_back(value);
    }
    return true;
  }

  bool AddBytes(const std::uint8_t *bytes, std::size_t size) {
    if (!Advance(size)) {
      return false;
    }
    if (output_ != nullptr && size != 0U) {
      output_->insert(output_->end(), bytes, bytes + size);
    }
    return true;
  }

  bool AddU16(std::uint16_t value) {
    return AddByte(static_cast<std::uint8_t>(value)) &&
           AddByte(static_cast<std::uint8_t>(value >> 8U));
  }

  bool AddU32(std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      if (!AddByte(static_cast<std::uint8_t>(value >> (byte * 8U)))) {
        return false;
      }
    }
    return true;
  }

  bool AddU64(std::uint64_t value) {
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
      if (!AddByte(static_cast<std::uint8_t>(value >> (byte * 8U)))) {
        return false;
      }
    }
    return true;
  }

  bool AddBool(bool value) { return AddByte(value ? 1U : 0U); }

  bool AddFloat(float value) {
    if (value == 0.0F) {
      value = 0.0F;
    }
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AddU32(bits);
  }

  bool AddDouble(double value) {
    if (value == 0.0) {
      value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return AddU64(bits);
  }

  [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
  [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
  bool Advance(std::size_t amount) noexcept {
    if (!ok_ || static_cast<std::uint64_t>(amount) > maximum_bytes_ - size_) {
      ok_ = false;
      return false;
    }
    size_ += static_cast<std::uint64_t>(amount);
    return true;
  }

  std::vector<std::uint8_t> *output_ = nullptr;
  std::uint64_t maximum_bytes_ = 0U;
  std::uint64_t size_ = 0U;
  bool ok_ = true;
};

class AllocationBudget final {
public:
  bool Charge(std::uint64_t count, std::size_t item_size) noexcept {
    if (count != 0U && static_cast<std::uint64_t>(item_size) >
                           kSceneSnapshotTransportMaximumDecodedAllocationBytes /
                               count) {
      return false;
    }
    const std::uint64_t bytes = count * static_cast<std::uint64_t>(item_size);
    if (bytes >
        kSceneSnapshotTransportMaximumDecodedAllocationBytes - used_bytes_) {
      return false;
    }
    used_bytes_ += bytes;
    return true;
  }

private:
  std::uint64_t used_bytes_ = 0U;
};

class WireReader final {
public:
  WireReader(const std::uint8_t *bytes, std::size_t size,
             AllocationBudget &allocation_budget) noexcept
      : bytes_(bytes), size_(size), allocation_budget_(allocation_budget) {}

  bool ReadByte(std::uint8_t &value) noexcept {
    if (remaining() < 1U) {
      Fail(SceneSnapshotTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    value = bytes_[offset_++];
    return true;
  }

  bool ReadU16(std::uint16_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 2U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint16_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadU32(std::uint32_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadU64(std::uint64_t &value) noexcept {
    value = 0U;
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
      std::uint8_t part = 0U;
      if (!ReadByte(part)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(part) << (byte * 8U);
    }
    return true;
  }

  bool ReadBool(bool &value) noexcept {
    std::uint8_t encoded = 0U;
    if (!ReadByte(encoded)) {
      return false;
    }
    if (encoded > 1U) {
      Fail(SceneSnapshotTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    value = encoded != 0U;
    return true;
  }

  bool ReadFloat(float &value) noexcept {
    std::uint32_t bits = 0U;
    if (!ReadU32(bits)) {
      return false;
    }
    if (bits == 0x80000000U || (bits & 0x7f800000U) == 0x7f800000U) {
      Fail(SceneSnapshotTransportStatus::NON_CANONICAL_FLOAT);
      return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadDouble(double &value) noexcept {
    std::uint64_t bits = 0U;
    if (!ReadU64(bits)) {
      return false;
    }
    if (bits == 0x8000000000000000ULL ||
        (bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) {
      Fail(SceneSnapshotTransportStatus::NON_CANONICAL_FLOAT);
      return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
  }

  bool ReadCount(std::uint32_t maximum, std::size_t minimum_item_bytes,
                 std::uint32_t &count) noexcept {
    if (!ReadU32(count)) {
      return false;
    }
    if (count > maximum) {
      Fail(SceneSnapshotTransportStatus::COUNT_LIMIT_EXCEEDED);
      return false;
    }
    if (minimum_item_bytes != 0U &&
        static_cast<std::uint64_t>(count) >
            static_cast<std::uint64_t>(remaining() / minimum_item_bytes)) {
      Fail(SceneSnapshotTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    return true;
  }

  template <typename Value>
  bool Reserve(std::vector<Value> &values, std::uint32_t count) {
    if (!allocation_budget_.Charge(count, sizeof(Value))) {
      Fail(SceneSnapshotTransportStatus::DECODED_ALLOCATION_LIMIT_EXCEEDED);
      return false;
    }
    values.reserve(count);
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return size_ - offset_;
  }
  [[nodiscard]] bool consumed() const noexcept { return offset_ == size_; }
  [[nodiscard]] SceneSnapshotTransportStatus status() const noexcept {
    return status_;
  }

  void Fail(SceneSnapshotTransportStatus status) noexcept {
    if (status_ == SceneSnapshotTransportStatus::OK) {
      status_ = status;
    }
  }

private:
  const std::uint8_t *bytes_ = nullptr;
  std::size_t size_ = 0U;
  std::size_t offset_ = 0U;
  AllocationBudget &allocation_budget_;
  SceneSnapshotTransportStatus status_ = SceneSnapshotTransportStatus::OK;
};

std::uint16_t ReadHeaderU16(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0U]) |
         (static_cast<std::uint16_t>(bytes[1U]) << 8U);
}

std::uint64_t ReadHeaderU64(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

bool DigestsEqual(const std::uint8_t *encoded,
                  const std::array<std::uint8_t, 32U> &computed) noexcept {
  std::uint8_t difference = 0U;
  for (std::size_t index = 0U; index < computed.size(); ++index) {
    difference |= static_cast<std::uint8_t>(encoded[index] ^ computed[index]);
  }
  return difference == 0U;
}

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
  AllocationBudget allocation_budget;
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
         SceneSnapshotTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2;
}

std::array<std::uint8_t, 32U>
ComputeSceneSnapshotTransportPayloadDigest(const std::uint8_t *payload,
                                           std::size_t payload_size) noexcept {
  if (payload == nullptr && payload_size != 0U) {
    return {};
  }
  Sha256 hasher;
  if (payload_size != 0U) {
    hasher.Update(payload, payload_size);
  }
  return hasher.Final();
}

DecodedSceneSnapshotTransportMessage::DecodedSceneSnapshotTransportMessage(
    std::uint64_t sequence, SceneSnapshotTransportMessageKind kind,
    std::shared_ptr<const SceneSnapshot> scene_snapshot,
    CameraViewRequest camera) noexcept
    : sequence_(sequence), kind_(kind),
      scene_snapshot_(std::move(scene_snapshot)), camera_(std::move(camera)) {}

SceneSnapshotTransportDecoder::SceneSnapshotTransportDecoder(
    std::uint64_t first_expected_sequence) noexcept
    : next_expected_sequence_(first_expected_sequence) {}

SceneSnapshotTransportDecodeResult SceneSnapshotTransportDecoder::Accept(
    const std::vector<std::uint8_t> &frame) {
  if (next_expected_sequence_ == 0U ||
      next_expected_sequence_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(SceneSnapshotTransportStatus::INVALID_SEQUENCE);
  }
  if (frame.size() < kSceneSnapshotTransportHeaderBytes) {
    return Failure(SceneSnapshotTransportStatus::FRAME_TRUNCATED);
  }
  if (!std::equal(kSceneSnapshotTransportMagic.begin(),
                  kSceneSnapshotTransportMagic.end(), frame.begin())) {
    return Failure(SceneSnapshotTransportStatus::INVALID_MAGIC);
  }
  const std::uint16_t transport_version = ReadHeaderU16(frame.data() + 8U);
  const std::uint16_t header_bytes = ReadHeaderU16(frame.data() + 10U);
  const auto kind = static_cast<SceneSnapshotTransportMessageKind>(
      ReadHeaderU16(frame.data() + 12U));
  const std::uint16_t flags = ReadHeaderU16(frame.data() + 14U);
  const std::uint64_t sequence = ReadHeaderU64(frame.data() + 16U);
  const std::uint64_t payload_size = ReadHeaderU64(frame.data() + 24U);
  if (transport_version != kSceneSnapshotTransportVersion) {
    return Failure(
        SceneSnapshotTransportStatus::UNSUPPORTED_TRANSPORT_VERSION);
  }
  if (header_bytes != kSceneSnapshotTransportHeaderBytes ||
      flags != kHeaderFlags) {
    return Failure(SceneSnapshotTransportStatus::INVALID_HEADER);
  }
  if (!IsKnownSceneSnapshotTransportMessageKind(kind)) {
    return Failure(SceneSnapshotTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(SceneSnapshotTransportStatus::INVALID_SEQUENCE);
  }
  if (payload_size > kSceneSnapshotTransportMaximumPayloadBytes) {
    return Failure(SceneSnapshotTransportStatus::PAYLOAD_LIMIT_EXCEEDED);
  }
  if (payload_size != frame.size() - kSceneSnapshotTransportHeaderBytes) {
    return Failure(SceneSnapshotTransportStatus::FRAME_SIZE_MISMATCH);
  }
  if (sequence < next_expected_sequence_) {
    return Failure(SceneSnapshotTransportStatus::REPLAYED_SEQUENCE);
  }
  if (sequence > next_expected_sequence_) {
    return Failure(SceneSnapshotTransportStatus::OUT_OF_ORDER_SEQUENCE);
  }

  const std::uint8_t *payload =
      frame.data() + kSceneSnapshotTransportHeaderBytes;
  const auto digest = ComputeSceneSnapshotTransportPayloadDigest(
      payload, static_cast<std::size_t>(payload_size));
  if (!DigestsEqual(frame.data() + kPayloadDigestOffset, digest)) {
    return Failure(SceneSnapshotTransportStatus::PAYLOAD_DIGEST_MISMATCH);
  }

  try {
    DecodedPayload decoded;
    SceneSnapshotTransportStatus status =
        SceneSnapshotTransportStatus::MALFORMED_PAYLOAD;
    if (!ReadPayload(payload, static_cast<std::size_t>(payload_size), decoded,
                     status)) {
      return Failure(status);
    }
    std::shared_ptr<const DecodedSceneSnapshotTransportMessage> candidate(
        new DecodedSceneSnapshotTransportMessage(
            sequence, kind, std::move(decoded.scene),
            std::move(decoded.camera)));
    published_ = candidate;
    last_accepted_sequence_ = sequence;
    next_expected_sequence_ = sequence + 1U;
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
    const auto digest = ComputeSceneSnapshotTransportPayloadDigest(
        payload.data(), payload.size());

    const std::uint64_t frame_size =
        kSceneSnapshotTransportHeaderBytes + sizer.size();
    result.bytes.reserve(static_cast<std::size_t>(frame_size));
    WireWriter frame_writer(&result.bytes, frame_size);
    if (!frame_writer.AddBytes(kSceneSnapshotTransportMagic.data(),
                               kSceneSnapshotTransportMagic.size()) ||
        !frame_writer.AddU16(kSceneSnapshotTransportVersion) ||
        !frame_writer.AddU16(
            static_cast<std::uint16_t>(kSceneSnapshotTransportHeaderBytes)) ||
        !frame_writer.AddU16(static_cast<std::uint16_t>(
            SceneSnapshotTransportMessageKind::
                SCENE_SNAPSHOT_V4_CAMERA_V2)) ||
        !frame_writer.AddU16(kHeaderFlags) ||
        !frame_writer.AddU64(sequence) ||
        !frame_writer.AddU64(static_cast<std::uint64_t>(payload.size())) ||
        !frame_writer.AddBytes(digest.data(), digest.size()) ||
        !frame_writer.AddBytes(payload.data(), payload.size()) ||
        frame_writer.size() != frame_size) {
      result.bytes.clear();
      result.status = SceneSnapshotTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    result.status = SceneSnapshotTransportStatus::OK;
    return result;
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
