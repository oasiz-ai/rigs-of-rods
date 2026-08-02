/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshotTransport.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

static_assert(!std::is_copy_constructible_v<
              DecodedSceneSnapshotTransportMessage>);
static_assert(!std::is_move_constructible_v<
              DecodedSceneSnapshotTransportMessage>);
static_assert(std::is_same_v<
              decltype(std::declval<const DecodedSceneSnapshotTransportMessage &>()
                           .scene_snapshot()),
              const std::shared_ptr<const SceneSnapshot> &>);
static_assert(std::is_same_v<
              decltype(std::declval<const DecodedSceneSnapshotTransportMessage &>()
                           .camera()),
              const CameraViewRequest &>);

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "scene snapshot transport test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireStatus(SceneSnapshotTransportStatus actual,
                   SceneSnapshotTransportStatus expected,
                   const char *message) {
  if (actual != expected) {
    std::cerr << "scene snapshot transport test failed: " << message
              << " (actual=" << static_cast<unsigned>(actual)
              << ", expected=" << static_cast<unsigned>(expected) << ")\n";
    std::exit(EXIT_FAILURE);
  }
}

RenderAssetReference Asset(RenderAssetKind kind, std::uint64_t value,
                           std::uint64_t revision = 1U) {
  return RenderAssetReference::Create(
      kind, RenderAssetId::FromWords(0x5CE0EU, value), revision);
}

Matrix4x4 MakePerspectiveProjection(float near_plane = 0.1F,
                                    float far_plane = 10000.0F) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

CameraViewRequest MakeCamera() {
  CameraViewRequest camera;
  camera.view_id = 9U;
  camera.width = 1920U;
  camera.height = 1080U;
  camera.view_from_render.elements[12U] = 2.5F;
  camera.view_from_render.elements[13U] = -3.0F;
  camera.previous_view_from_render.elements[12U] = 2.25F;
  camera.previous_view_from_render.elements[13U] = -3.0F;
  camera.clip_from_view = MakePerspectiveProjection();
  camera.previous_clip_from_view = MakePerspectiveProjection();
  camera.temporal_jitter_pixels = {0.25F, -0.125F};
  camera.exposure = 1.5F;
  camera.visibility_mask = 0x00FFFFFFU;
  Require(ValidateCameraViewRequest(camera).ok(),
          "camera fixture must be valid");
  return camera;
}

SceneSnapshotDescriptor MakeMinimalDescriptor() {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = 2U;
  descriptor.asset_sequence = 3U;
  descriptor.simulation_tick = 4U;
  descriptor.simulation_time_seconds = 0.5;
  descriptor.absolute_world_origin_meters = {10.0, 20.0, 30.0};
  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "minimal scene fixture must be valid");
  return descriptor;
}

SceneSnapshotDescriptor MakeRichDescriptor() {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 17U;
  descriptor.asset_registry_id = 31U;
  descriptor.asset_sequence = 7U;
  descriptor.simulation_tick = 2000U;
  descriptor.simulation_time_seconds = 1.25;
  descriptor.absolute_world_origin_meters = {
      1000000000.25, -2000000000.5, 3000000000.75};
  descriptor.environment.ambient_radiance = {0.01F, 0.02F, 0.03F};
  descriptor.environment.environment_texture =
      Asset(RenderAssetKind::TEXTURE, 1U, 2U);
  descriptor.environment.environment_sampler =
      Asset(RenderAssetKind::SAMPLER, 4U, 3U);
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

  MeshInstanceDescriptor instance;
  instance.instance_id = 10U;
  instance.mesh = Asset(RenderAssetKind::MESH, 2U, 4U);
  instance.material = Asset(RenderAssetKind::MATERIAL, 3U, 5U);
  instance.topology_revision = 4U;
  instance.deformation_revision = 9U;
  instance.render_from_object.elements[12U] = 4.0F;
  instance.previous_render_from_object.elements[12U] = 3.75F;
  instance.local_bounds.minimum = {-1.0F, -0.5F, -2.0F};
  instance.local_bounds.maximum = {1.0F, 0.5F, 2.0F};
  instance.visibility_mask = 0x0FFFFFFFU;
  descriptor.mesh_instances.push_back(instance);

  LightDescriptor spot;
  spot.light_id = 20U;
  spot.type = LightType::SPOT;
  spot.intensity = 800.0F;
  spot.position = {0.0F, 4.0F, 0.0F};
  spot.previous_position = {0.0F, 3.9F, 0.0F};
  spot.range = 30.0F;
  spot.inner_cone_radians = 0.5F;
  spot.outer_cone_radians = 0.75F;
  spot.shadow_flags = LIGHT_SHADOW_DYNAMIC_GEOMETRY;
  descriptor.lights.push_back(spot);

  LightDescriptor sun;
  sun.light_id = 30U;
  sun.intensity = 110000.0F;
  sun.direction = {0.0F, -0.8F, -0.6F};
  sun.previous_direction = {0.0F, -0.6F, -0.8F};
  descriptor.lights.push_back(sun);

  ReflectionProbeRuntimeDescriptor probe;
  probe.probe_id = 35U;
  probe.content_revision = 6U;
  probe.absolute_world_position_meters = {1000000012.5, -2000000000.0,
                                          3000000002.0};
  probe.capture_position_local = {0.25F, 0.5F, -0.75F};
  probe.influence_center_local = {0.5F, 0.0F, 0.0F};
  probe.influence_half_size = {4.0F, 3.0F, 2.0F};
  probe.influence_inner_fraction = {0.7F, 0.8F, 0.9F};
  probe.correction_shape_center_local = {0.5F, 0.0F, 0.0F};
  probe.correction_shape_half_size = {5.0F, 4.0F, 3.0F};
  probe.priority = 4U;
  probe.resolution = 32U;
  probe.capture_near_meters = 0.1F;
  probe.capture_far_meters = 16.0F;
  probe.visibility_mask = 0x0000FFFFU;
  probe.update_mode = ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS;
  probe.update_interval_simulation_ticks = 120U;
  probe.include_dynamic_geometry = false;
  descriptor.reflection_probes.push_back(probe);

  DynamicMeshUpdateDescriptor update;
  update.update_sequence = 30U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.topology_revision = instance.topology_revision;
  update.deformation_revision = instance.deformation_revision;
  update.positions = {{-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
  update.normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  update.tangents = {{1.0F, 0.0F, 0.0F, 1.0F},
                     {1.0F, 0.0F, 0.0F, -1.0F}};
  update.velocities = {{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.5F}};
  update.has_updated_bounds = true;
  update.updated_local_bounds = instance.local_bounds;
  descriptor.dynamic_mesh_updates.push_back(update);

  ParticleEvent particle;
  particle.event_id = 40U;
  particle.emitter_id = 50U;
  particle.effect = ParticleEffect::TIRE_SMOKE;
  particle.position = {0.0F, 0.1F, -1.0F};
  particle.velocity = {0.0F, 0.5F, -0.1F};
  particle.color_linear = {0.5F, 0.6F, 0.7F, 0.8F};
  particle.size_meters = 0.3F;
  particle.lifetime_seconds = 1.75F;
  particle.intensity = 2.0F;
  particle.emission_count = 12U;
  particle.random_seed = 0xA55AU;
  descriptor.particle_events.push_back(particle);

  Require(ValidateSceneSnapshotDescriptor(descriptor).ok(),
          "rich scene fixture must be valid");
  return descriptor;
}

std::shared_ptr<const SceneSnapshot>
MakeSnapshot(SceneSnapshotDescriptor descriptor) {
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "scene fixture could not be frozen");
  return result.snapshot;
}

std::uint16_t ReadU16(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(bytes[offset]) |
      (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U));
}

std::uint64_t ReadU64(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  std::uint64_t value = 0U;
  for (std::size_t byte = 0U; byte < 8U; ++byte) {
    value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
  }
  return value;
}

void WriteU16(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint16_t value) {
  for (std::size_t byte = 0U; byte < 2U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint32_t value) {
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void WriteU64(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint64_t value) {
  for (std::size_t byte = 0U; byte < 8U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void RefreshPayloadDigest(std::vector<std::uint8_t> &frame) {
  const std::size_t payload_offset = kSceneSnapshotTransportHeaderBytes;
  const auto digest = ComputeSceneSnapshotTransportPayloadDigest(
      frame.data() + payload_offset, frame.size() - payload_offset);
  std::copy(digest.begin(), digest.end(), frame.begin() + 32U);
}

std::string ToHex(const std::uint8_t *bytes, std::size_t size) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0U; index < size; ++index) {
    output << std::setw(2) << static_cast<unsigned>(bytes[index]);
  }
  return output.str();
}

std::string ToHex(const std::vector<std::uint8_t> &bytes) {
  return ToHex(bytes.data(), bytes.size());
}

void TestSha256KnownVectors() {
  static constexpr std::array<std::uint8_t, 3U> kAbc{{'a', 'b', 'c'}};
  const auto empty = ComputeSceneSnapshotTransportPayloadDigest(nullptr, 0U);
  const auto abc = ComputeSceneSnapshotTransportPayloadDigest(kAbc.data(),
                                                               kAbc.size());
  Require(ToHex(empty.data(), empty.size()) ==
              "e3b0c44298fc1c149afbf4c8996fb924"
              "27ae41e4649b934ca495991b7852b855",
          "SHA-256 empty-vector digest changed");
  Require(ToHex(abc.data(), abc.size()) ==
              "ba7816bf8f01cfea414140de5dae2223"
              "b00361a396177a9cb410ff61f20015ad",
          "SHA-256 abc-vector digest changed");
}

void TestGoldenMinimalFrame() {
  const auto snapshot = MakeSnapshot(MakeMinimalDescriptor());
  const CameraViewRequest camera = MakeCamera();
  const SceneSnapshotTransportEncodeResult first =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, camera);
  const SceneSnapshotTransportEncodeResult second =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, camera);
  Require(first.ok() && second.ok() && first.bytes == second.bytes,
          "identical inputs did not produce identical bytes");
  Require(first.bytes.size() == 591U, "minimal frame size changed");
  Require(std::equal(kSceneSnapshotTransportMagic.begin(),
                     kSceneSnapshotTransportMagic.end(), first.bytes.begin()),
          "wire magic changed");
  Require(ReadU16(first.bytes, 8U) == kSceneSnapshotTransportVersion &&
              ReadU16(first.bytes, 10U) ==
                  kSceneSnapshotTransportHeaderBytes &&
              ReadU16(first.bytes, 12U) == 1U &&
              ReadU16(first.bytes, 14U) == 0U &&
              ReadU64(first.bytes, 16U) == 1U &&
              ReadU64(first.bytes, 24U) == 527U,
          "fixed little-endian frame header changed");

  // Filled from the independently exercised encoder only after the structural
  // assertions above pass. This exact fixture pins every byte, including the
  // SHA-256 field, binary floating-point encoding, and reserved zero bytes.
  static const std::string kGoldenHex =
      "524f5253434e3031010040000100000001000000000000000f02000000000000"
      "d5c9ec1c0e35559250cb20c1220e0515e293fbe54bfbd48149f032c2c7055ce1"
      "0100000004000000020000000000000001000000000000000200000000000000"
      "03000000000000000400000000000000000000000000e03f0000000000002440"
      "00000000000034400000000000003e408fc2f53c8fc2f53c8fc2f53c00000000"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "00000000000000000000000000000000803f0000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "00000000000000090000000000000080070000380400000000803f0000000000"
      "00000000000000000000000000803f0000000000000000000000000000000000"
      "00803f0000000000002040000040c0000000000000803f0000803f0000000000"
      "00000000000000000000000000803f0000000000000000000000000000000054"
      "0080bf000080bf000000000000000053cdccbd000000000000803f0000000000"
      "00000000000000000000000000803f0000000000000000000000000000000000"
      "00803f0000000000001040000040c0000000000000803f0000803f0000000000"
      "00000000000000000000000000803f0000000000000000000000000000000054"
      "0080bf000080bf000000000000000053cdccbd000000000000803e000000becd"
      "cccc3d00401c460000c03fffffff00";
  const std::string actual_hex = ToHex(first.bytes);
  Require(actual_hex == kGoldenHex,
          "minimal frame no longer matches the v1 golden bytes");
}

void TestRichRoundTripAndSignedZeroNormalization() {
  SceneSnapshotDescriptor descriptor = MakeRichDescriptor();
  descriptor.absolute_world_origin_meters.y = -0.0;
  descriptor.environment.ambient_radiance.x = -0.0F;
  descriptor.mesh_instances.front().render_from_object.elements[13U] = -0.0F;
  descriptor.dynamic_mesh_updates.front().positions.front().y = -0.0F;
  descriptor.particle_events.front().position.x = -0.0F;
  const auto snapshot = MakeSnapshot(std::move(descriptor));
  CameraViewRequest camera = MakeCamera();
  camera.temporal_jitter_pixels.y = -0.0F;

  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(44U, *snapshot, camera);
  Require(encoded.ok(), "rich frame was not encoded");
  SceneSnapshotTransportDecoder decoder(44U);
  const SceneSnapshotTransportDecodeResult decoded = decoder.Accept(encoded.bytes);
  Require(decoded.ok() && decoded.message->sequence() == 44U,
          "rich frame was not decoded");
  Require(decoded.message->kind() ==
              SceneSnapshotTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2,
          "decoded message kind changed");
  const SceneSnapshot &scene = *decoded.message->scene_snapshot();
  Require(scene.version() == kSceneSnapshotVersion &&
              scene.snapshot_id() == 17U && scene.asset_registry_id() == 31U &&
              scene.asset_sequence() == 7U && scene.simulation_tick() == 2000U &&
              scene.simulation_time_seconds() == 1.25,
          "scene identity or simulation state did not round-trip");
  Require(scene.mesh_instances().size() == 1U && scene.lights().size() == 2U &&
              scene.reflection_probes().size() == 1U &&
              scene.dynamic_mesh_updates().size() == 1U &&
              scene.particle_events().size() == 1U,
          "one or more v4 scene collections did not round-trip");
  Require(scene.environment().analytic_sky.enabled &&
              scene.environment().environment_texture.revision == 2U &&
              scene.mesh_instances().front().deformation_revision == 9U &&
              scene.lights().front().type == LightType::SPOT &&
              scene.reflection_probes().front().update_interval_simulation_ticks ==
                  120U &&
              scene.dynamic_mesh_updates().front().tangents.size() == 2U &&
              scene.particle_events().front().random_seed == 0xA55AU,
          "representative v4 scene payload fields did not round-trip");
  Require(decoded.message->camera() == camera,
          "camera contract did not round-trip semantically");

  const SceneSnapshotTransportEncodeResult reencoded =
      EncodeSceneSnapshotTransportFrame(44U, scene, decoded.message->camera());
  Require(reencoded.ok() && reencoded.bytes == encoded.bytes,
          "decode/re-encode was not byte deterministic");

  const auto double_bits = [](double value) {
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  };
  const auto float_bits = [](float value) {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  };
  Require(double_bits(scene.absolute_world_origin_meters().y) == 0U &&
              float_bits(scene.environment().ambient_radiance.x) == 0U &&
              float_bits(scene.mesh_instances()
                             .front()
                             .render_from_object.elements[13U]) == 0U &&
              float_bits(scene.dynamic_mesh_updates()
                             .front()
                             .positions.front()
                             .y) == 0U &&
              float_bits(scene.particle_events().front().position.x) == 0U &&
              float_bits(decoded.message->camera().temporal_jitter_pixels.y) ==
                  0U,
          "signed zero was not normalized to positive zero");
}

void TestFramingAndPayloadRejections() {
  const auto snapshot = MakeSnapshot(MakeMinimalDescriptor());
  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, MakeCamera());
  Require(encoded.ok(), "rejection fixture was not encoded");

  SceneSnapshotTransportDecoder decoder;
  RequireStatus(decoder.Accept({}).status,
                SceneSnapshotTransportStatus::FRAME_TRUNCATED,
                "empty frame was not rejected as truncated");
  std::vector<std::uint8_t> frame(encoded.bytes.begin(),
                                  encoded.bytes.begin() + 63U);
  RequireStatus(decoder.Accept(frame).status,
                SceneSnapshotTransportStatus::FRAME_TRUNCATED,
                "short header was not rejected as truncated");

  frame = encoded.bytes;
  frame[0U] ^= 1U;
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::INVALID_MAGIC,
                "bad magic was accepted");
  frame = encoded.bytes;
  WriteU16(frame, 8U, 2U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::UNSUPPORTED_TRANSPORT_VERSION,
                "unknown transport schema was accepted");
  frame = encoded.bytes;
  WriteU16(frame, 10U, 63U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::INVALID_HEADER,
                "noncanonical header size was accepted");
  frame = encoded.bytes;
  WriteU16(frame, 14U, 1U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::INVALID_HEADER,
                "unknown header flag was accepted");
  frame = encoded.bytes;
  WriteU16(frame, 12U, 99U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::UNKNOWN_MESSAGE_KIND,
                "unknown message kind was accepted");
  frame = encoded.bytes;
  WriteU64(frame, 16U, 0U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::INVALID_SEQUENCE,
                "zero sequence was accepted");
  frame = encoded.bytes;
  WriteU64(frame, 16U, (std::numeric_limits<std::uint64_t>::max)());
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::INVALID_SEQUENCE,
                "maximum sequence was accepted");
  frame = encoded.bytes;
  WriteU64(frame, 24U, kSceneSnapshotTransportMaximumPayloadBytes + 1U);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::PAYLOAD_LIMIT_EXCEEDED,
                "oversized payload declaration was accepted");
  frame = encoded.bytes;
  frame.pop_back();
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::FRAME_SIZE_MISMATCH,
                "truncated payload was not rejected before decode");
  frame = encoded.bytes;
  frame.back() ^= 1U;
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::PAYLOAD_DIGEST_MISMATCH,
                "corrupt payload passed SHA-256");

  frame = encoded.bytes;
  frame.push_back(0U);
  WriteU64(frame, 24U, ReadU64(frame, 24U) + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::MALFORMED_PAYLOAD,
                "valid payload plus trailing byte was accepted");

  frame = encoded.bytes;
  WriteU32(frame, kSceneSnapshotTransportHeaderBytes, 2U);
  RefreshPayloadDigest(frame);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unknown payload version was accepted");
  frame = encoded.bytes;
  WriteU32(frame, kSceneSnapshotTransportHeaderBytes + 4U, 3U);
  RefreshPayloadDigest(frame);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "legacy scene version was accepted implicitly");

  // Prefix (16), identities (32), then simulation_time_seconds.
  frame = encoded.bytes;
  WriteU64(frame, kSceneSnapshotTransportHeaderBytes + 48U,
           0x8000000000000000ULL);
  RefreshPayloadDigest(frame);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::NON_CANONICAL_FLOAT,
                "negative zero payload encoding was accepted");

  // Prefix/identity/time/origin/environment consume 211 payload bytes.
  frame = encoded.bytes;
  WriteU32(frame, kSceneSnapshotTransportHeaderBytes + 211U,
           (std::numeric_limits<std::uint32_t>::max)());
  RefreshPayloadDigest(frame);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(frame).status,
                SceneSnapshotTransportStatus::COUNT_LIMIT_EXCEEDED,
                "malicious mesh count reached allocation");
}

void TestTransactionalSequenceLineage() {
  const auto snapshot = MakeSnapshot(MakeMinimalDescriptor());
  const CameraViewRequest camera = MakeCamera();
  const auto sequence_one =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, camera);
  const auto sequence_two =
      EncodeSceneSnapshotTransportFrame(2U, *snapshot, camera);
  const auto sequence_three =
      EncodeSceneSnapshotTransportFrame(3U, *snapshot, camera);
  Require(sequence_one.ok() && sequence_two.ok() && sequence_three.ok(),
          "sequence fixtures were not encoded");

  SceneSnapshotTransportDecoder decoder;
  const SceneSnapshotTransportDecodeResult accepted_one =
      decoder.Accept(sequence_one.bytes);
  Require(accepted_one.ok() && decoder.last_accepted_sequence() == 1U &&
              decoder.next_expected_sequence() == 2U &&
              decoder.published() == accepted_one.message,
          "first transaction did not publish atomically");
  const auto published_one = decoder.published();

  RequireStatus(decoder.Accept(sequence_one.bytes).status,
                SceneSnapshotTransportStatus::REPLAYED_SEQUENCE,
                "replayed sequence was accepted");
  RequireStatus(decoder.Accept(sequence_three.bytes).status,
                SceneSnapshotTransportStatus::OUT_OF_ORDER_SEQUENCE,
                "out-of-order sequence was accepted");
  std::vector<std::uint8_t> corrupt_two = sequence_two.bytes;
  corrupt_two.back() ^= 1U;
  RequireStatus(decoder.Accept(corrupt_two).status,
                SceneSnapshotTransportStatus::PAYLOAD_DIGEST_MISMATCH,
                "corrupt expected transaction was accepted");
  Require(decoder.published() == published_one &&
              decoder.last_accepted_sequence() == 1U &&
              decoder.next_expected_sequence() == 2U,
          "rejected candidate mutated published state");

  const SceneSnapshotTransportDecodeResult accepted_two =
      decoder.Accept(sequence_two.bytes);
  Require(accepted_two.ok() && decoder.published() == accepted_two.message &&
              decoder.published() != published_one &&
              decoder.last_accepted_sequence() == 2U &&
              decoder.next_expected_sequence() == 3U,
          "valid second transaction did not replace published state");
  Require(published_one->sequence() == 1U &&
              published_one->scene_snapshot()->snapshot_id() == 1U,
          "previous immutable owner was invalidated after publication");

  SceneSnapshotTransportDecoder invalid_start(0U);
  RequireStatus(invalid_start.Accept(sequence_one.bytes).status,
                SceneSnapshotTransportStatus::INVALID_SEQUENCE,
                "zero expected sequence state was usable");
  SceneSnapshotTransportDecoder exhausted(
      (std::numeric_limits<std::uint64_t>::max)());
  RequireStatus(exhausted.Accept(sequence_one.bytes).status,
                SceneSnapshotTransportStatus::INVALID_SEQUENCE,
                "exhausted sequence state was usable");
}

void TestEveryTruncationAndIntegrityByteFailsClosed() {
  const auto snapshot = MakeSnapshot(MakeMinimalDescriptor());
  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, MakeCamera());
  Require(encoded.ok(), "exhaustive framing fixture was not encoded");

  for (std::size_t size = 0U; size < encoded.bytes.size(); ++size) {
    const auto size_offset =
        static_cast<std::vector<std::uint8_t>::difference_type>(size);
    const auto prefix_end = encoded.bytes.begin() + size_offset;
    const std::vector<std::uint8_t> prefix(encoded.bytes.begin(),
                                           prefix_end);
    const SceneSnapshotTransportStatus expected =
        size < kSceneSnapshotTransportHeaderBytes
            ? SceneSnapshotTransportStatus::FRAME_TRUNCATED
            : SceneSnapshotTransportStatus::FRAME_SIZE_MISMATCH;
    RequireStatus(SceneSnapshotTransportDecoder{}.Accept(prefix).status,
                  expected, "a truncated prefix was not rejected");
  }

  for (std::size_t offset = 32U;
       offset < 32U + 32U; ++offset) {
    std::vector<std::uint8_t> corrupt_digest = encoded.bytes;
    corrupt_digest[offset] ^= 1U;
    RequireStatus(
        SceneSnapshotTransportDecoder{}.Accept(corrupt_digest).status,
        SceneSnapshotTransportStatus::PAYLOAD_DIGEST_MISMATCH,
        "a corrupt digest byte was accepted");
  }
  for (std::size_t offset = kSceneSnapshotTransportHeaderBytes;
       offset < encoded.bytes.size(); ++offset) {
    std::vector<std::uint8_t> corrupt_payload = encoded.bytes;
    corrupt_payload[offset] ^= 1U;
    RequireStatus(
        SceneSnapshotTransportDecoder{}.Accept(corrupt_payload).status,
        SceneSnapshotTransportStatus::PAYLOAD_DIGEST_MISMATCH,
        "a corrupt payload byte was accepted");
  }
}

void TestCanonicalCollectionOrderIsRevalidated() {
  const auto snapshot = MakeSnapshot(MakeRichDescriptor());
  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(1U, *snapshot, MakeCamera());
  Require(encoded.ok(), "canonical-order fixture was not encoded");

  // The fixed prefix/environment consumes 211 bytes; one mesh count plus its
  // 234-byte record and the light count put the first light ID at offset 453.
  // Raising it above the following ID creates wire order [40, 30].
  std::vector<std::uint8_t> reordered = encoded.bytes;
  WriteU64(reordered, kSceneSnapshotTransportHeaderBytes + 453U, 40U);
  RefreshPayloadDigest(reordered);
  RequireStatus(SceneSnapshotTransportDecoder{}.Accept(reordered).status,
                SceneSnapshotTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "noncanonical collection order was accepted");
}

void TestEncoderFailClosed() {
  const auto snapshot = MakeSnapshot(MakeMinimalDescriptor());
  CameraViewRequest invalid_camera = MakeCamera();
  invalid_camera.width = 0U;
  RequireStatus(EncodeSceneSnapshotTransportFrame(1U, *snapshot,
                                                  invalid_camera)
                    .status,
                SceneSnapshotTransportStatus::INVALID_ARGUMENT,
                "invalid camera was encoded");
  RequireStatus(EncodeSceneSnapshotTransportFrame(0U, *snapshot, MakeCamera())
                    .status,
                SceneSnapshotTransportStatus::INVALID_ARGUMENT,
                "zero sequence was encoded");
  RequireStatus(EncodeSceneSnapshotTransportFrame(
                    (std::numeric_limits<std::uint64_t>::max)(), *snapshot,
                    MakeCamera())
                    .status,
                SceneSnapshotTransportStatus::INVALID_ARGUMENT,
                "maximum sequence was encoded");
}

} // namespace

int main() {
  TestSha256KnownVectors();
  TestGoldenMinimalFrame();
  TestRichRoundTripAndSignedZeroNormalization();
  TestFramingAndPayloadRejections();
  TestTransactionalSequenceLineage();
  TestEveryTruncationAndIntegrityByteFailsClosed();
  TestCanonicalCollectionOrderIsRevalidated();
  TestEncoderFailClosed();
  return EXIT_SUCCESS;
}
