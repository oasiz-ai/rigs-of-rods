/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetDeltaTransport.h"
#include "SceneSnapshotTransport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

static_assert(!std::is_copy_constructible_v<
              DecodedRenderAssetDeltaTransportMessage>);
static_assert(!std::is_move_constructible_v<
              DecodedRenderAssetDeltaTransportMessage>);
static_assert(std::is_same_v<
              decltype(std::declval<
                           const DecodedRenderAssetDeltaTransportMessage &>()
                           .delta()),
              const std::shared_ptr<const RenderAssetDelta> &>);

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "asset delta transport test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireStatus(RenderTransportStatus actual,
                   RenderTransportStatus expected, const char *message) {
  if (actual != expected) {
    std::cerr << "asset delta transport test failed: " << message
              << " (actual=" << static_cast<unsigned>(actual)
              << ", expected=" << static_cast<unsigned>(expected) << ")\n";
    std::exit(EXIT_FAILURE);
  }
}

RenderAssetId Id(std::uint64_t low) {
  return RenderAssetId::FromWords(0x524F525F41535345ULL, low);
}

RenderAssetReference Ref(RenderAssetKind kind, std::uint64_t low,
                         std::uint64_t revision = 1U) {
  return RenderAssetReference::Create(kind, Id(low), revision);
}

RenderAssetMutation Upsert(const RenderAssetReference &asset,
                           RenderAssetPayload payload) {
  RenderAssetMutation mutation;
  mutation.type = RenderAssetMutationType::UPSERT;
  mutation.asset = asset;
  mutation.payload = std::move(payload);
  return mutation;
}

RenderAssetMutation Destroy(const RenderAssetReference &asset) {
  RenderAssetMutation mutation;
  mutation.type = RenderAssetMutationType::DESTROY;
  mutation.asset = asset;
  return mutation;
}

MeshResourceDescriptor MakeMesh(std::string debug_name = "rich mesh") {
  MeshResourceDescriptor mesh;
  mesh.debug_name = std::move(debug_name);
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.topology_revision = 7U;
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-0.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {{-0.0F, 0.0F, 0.0F},
                    {1.0F, 0.0F, 0.0F},
                    {0.0F, 1.0F, 0.0F}};
  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  mesh.velocities = {{0.0F, 0.0F, 0.0F},
                     {0.5F, 0.0F, 0.0F},
                     {0.0F, 0.5F, 0.0F}};
  mesh.texture_coordinates_0 = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
  mesh.texture_coordinates_1 = {
      {0.25F, 0.25F}, {0.75F, 0.25F}, {0.25F, 0.75F}};
  mesh.colors = {{1.0F, 0.0F, 0.0F, 1.0F},
                 {0.0F, 1.0F, 0.0F, 0.75F},
                 {0.0F, 0.0F, 1.0F, 0.5F}};
  mesh.indices = {0U, 1U, 2U};
  Require(ValidateMeshResourceDescriptor(mesh).ok(),
          "mesh fixture must be valid");
  return mesh;
}

TextureResourceDescriptor MakeTexture(std::string debug_name = "rich tex") {
  TextureResourceDescriptor texture;
  texture.debug_name = std::move(debug_name);
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor base;
  base.width = 2U;
  base.height = 2U;
  base.row_pitch_bytes = 8U;
  base.layer_pitch_bytes = 16U;
  base.bytes = {255U, 0U,   0U,   255U, 0U,   255U, 0U,   255U,
                0U,   0U,   255U, 128U, 255U, 255U, 255U, 64U};
  texture.mip_levels.push_back(base);
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes = {127U, 63U, 31U, 255U};
  texture.mip_levels.push_back(mip);
  Require(ValidateTextureResourceDescriptor(texture).ok(),
          "texture fixture must be valid");
  return texture;
}

SamplerResourceDescriptor MakeSampler(std::string debug_name = "rich samp") {
  SamplerResourceDescriptor sampler;
  sampler.debug_name = std::move(debug_name);
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_v = SamplerAddressMode::MIRRORED_REPEAT;
  sampler.mip_lod_bias = -0.0F;
  sampler.minimum_lod = 0.25F;
  sampler.maximum_lod = 8.0F;
  sampler.anisotropy_enabled = true;
  sampler.maximum_anisotropy = 8.0F;
  sampler.border_color = {0.1F, 0.2F, 0.3F, 1.0F};
  Require(ValidateSamplerResourceDescriptor(sampler).ok(),
          "sampler fixture must be valid");
  return sampler;
}

MaterialDescriptor MakeMaterial(std::uint64_t revision = 1U) {
  MaterialDescriptor material;
  material.debug_name = "rich material";
  material.alpha_mode = MaterialAlphaMode::MASK;
  material.double_sided = true;
  material.base_color_factor = {0.8F, 0.7F, 0.6F, 0.5F};
  material.metallic_factor = -0.0F;
  material.roughness_factor = 0.35F;
  material.normal_scale = 1.25F;
  material.occlusion_strength = 0.75F;
  material.emissive_factor = {0.1F, 0.2F, 0.3F};
  material.emissive_strength = 2.0F;
  material.alpha_cutoff = 0.4F;
  material.index_of_refraction = 1.45F;
  material.base_color_texture.texture =
      Ref(RenderAssetKind::TEXTURE, 2U, revision);
  material.base_color_texture.sampler =
      Ref(RenderAssetKind::SAMPLER, 3U, revision);
  material.base_color_texture.texture_coordinate_set = 0U;
  material.base_color_texture.scale = {0.5F, 0.75F};
  material.base_color_texture.offset = {0.125F, -0.25F};
  material.base_color_texture.rotation_radians = -0.125F;
  material.metallic_roughness_texture.scale = {-1.0F, 1.0F};
  material.normal_texture.offset = {0.25F, 0.5F};
  material.occlusion_texture.rotation_radians = 0.25F;
  material.emissive_texture.texture_coordinate_set = 1U;
  Require(ValidateMaterialDescriptor(material).ok(),
          "material fixture must be valid");
  return material;
}

RenderAssetDelta MakeRichDelta() {
  RenderAssetDelta delta;
  delta.registry_id = 77U;
  delta.sequence = 2U;
  delta.full_snapshot = true;
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MESH, 1U), MakeMesh()));
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 2U), MakeTexture()));
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::SAMPLER, 3U), MakeSampler()));
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MATERIAL, 4U), MakeMaterial()));
  delta.mutations.push_back(
      Destroy(Ref(RenderAssetKind::TEXTURE, 5U, 2U)));
  Require(ValidateRenderAssetDelta(delta).ok(),
          "rich delta fixture must be structurally valid");
  RenderAssetRegistry registry(delta.registry_id);
  Require(registry.Apply(delta).ok(),
          "rich delta fixture must be registry-applicable");
  return delta;
}

RenderAssetDelta MakeTextureOnlyDelta() {
  RenderAssetDelta delta;
  delta.registry_id = 91U;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 1U), MakeTexture("t")));
  return delta;
}

RenderAssetDelta MakeMeshOnlyDelta() {
  RenderAssetDelta delta;
  delta.registry_id = 92U;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MESH, 1U), MakeMesh("m")));
  return delta;
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
  const auto digest = ComputeRenderTransportPayloadDigest(
      frame.data() + kRenderTransportEnvelopeHeaderBytes,
      frame.size() - kRenderTransportEnvelopeHeaderBytes);
  std::copy(digest.begin(), digest.end(), frame.begin() + 32U);
}

std::string ToHex(const std::vector<std::uint8_t> &bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint8_t byte : bytes) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void RequireEquivalentDelta(const RenderAssetDelta &actual,
                            const RenderAssetDelta &expected) {
  Require(actual.version == expected.version &&
              actual.registry_id == expected.registry_id &&
              actual.base_sequence == expected.base_sequence &&
              actual.sequence == expected.sequence &&
              actual.full_snapshot == expected.full_snapshot &&
              actual.mutations.size() == expected.mutations.size(),
          "delta transaction fields did not round-trip");
  for (std::size_t index = 0U; index < actual.mutations.size(); ++index) {
    Require(actual.mutations[index].type == expected.mutations[index].type &&
                actual.mutations[index].asset == expected.mutations[index].asset &&
                EquivalentRenderAssetPayload(actual.mutations[index].payload,
                                             expected.mutations[index].payload),
            "asset mutation contents did not round-trip bit-exactly");
  }
}

void TestGoldenRichDeltaAndRoundTrip() {
  const RenderAssetDelta delta = MakeRichDelta();
  const RenderAssetDeltaTransportEncodeResult first =
      EncodeRenderAssetDeltaTransportFrame(1U, delta);
  const RenderAssetDeltaTransportEncodeResult second =
      EncodeRenderAssetDeltaTransportFrame(1U, delta);
  Require(first.ok() && second.ok() && first.bytes == second.bytes,
          "rich delta encoding was not deterministic");
  Require(std::equal(kRenderTransportEnvelopeMagic.begin(),
                     kRenderTransportEnvelopeMagic.end(), first.bytes.begin()),
          "shared envelope magic changed");
  Require(ReadU16(first.bytes, 8U) == kRenderTransportEnvelopeVersion &&
              ReadU16(first.bytes, 10U) ==
                  kRenderTransportEnvelopeHeaderBytes &&
              ReadU16(first.bytes, 12U) == 2U &&
              ReadU16(first.bytes, 14U) == 0U &&
              ReadU64(first.bytes, 16U) == 1U &&
              ReadU64(first.bytes, 24U) ==
                  first.bytes.size() - kRenderTransportEnvelopeHeaderBytes,
          "asset envelope header is not canonical little-endian v1");

  static const std::string kGoldenHex =
      "524f5253434e303101004000020000000100000000000000a0040000000000007114910b73a0cf612271f8e0a539d357"
      "5f95428d42a60207a0abf408791c78ef010000000100000001000000010000000200000001000000000000004d000000"
      "000000000000000000000000020000000000000001050000000001455353415f524f5201000000000000000100000000"
      "000000015a0100000000000001000000090072696368206d657368000007000000000000000100000080000000000000"
      "00000000803f0000803f00000000030000000000008000000000000000000000803f0000000000000000000000000000"
      "803f000000000300000000000000000000000000803f00000000000000000000803f00000000000000000000803f0300"
      "00000000803f00000000000000000000803f0000803f00000000000000000000803f0000803f00000000000000000000"
      "803f030000000000000000000000000000000000003f0000000000000000000000000000003f00000000030000000000"
      "0000000000000000803f00000000000000000000803f030000000000803e0000803e0000403f0000803e0000803e0000"
      "403f030000000000803f00000000000000000000803f000000000000803f000000000000403f00000000000000000000"
      "803f0000003f030000000000000001000000020000000002455353415f524f5202000000000000000100000000000000"
      "027500000000000000010000000800726963682074657800020102000000020000000100000002000000020000000200"
      "0000080000000000000010000000000000001000000000000000ff0000ff00ff00ff0000ff80ffffff40010000000100"
      "00000400000000000000040000000000000004000000000000007f3f1fff0004455353415f524f520300000000000000"
      "0100000000000000043800000000000000010000000900726963682073616d70010101020100000000800000803e0000"
      "004101000000410007cdcccc3dcdcc4c3e9a99993e0000803f0003455353415f524f5204000000000000000100000000"
      "00000003b101000000000000020000000d0072696368206d6174657269616c000101cdcc4c3f3333333f9a99193f0000"
      "003f000000803333b33e0000a03f0000403fcdcccc3dcdcc4c3e9a99993e00000040cdcccc3e9a99b93f02455353415f"
      "524f520200000000000000010000000000000004455353415f524f520300000000000000010000000000000000000000"
      "3f0000403f0000003e000080be000000be00000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000080bf0000803f0000000000000000000000000000000000000000"
      "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f00"
      "00803f0000803e0000003f00000000000000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000803f0000803f00000000000000000000803e00000000000000000000"
      "00000000000000000000000000000000000000000000000000000000000000000000000000000000010000803f000080"
      "3f0000000000000000000000000102455353415f524f5205000000000000000200000000000000000000000000000000";
  Require(ToHex(first.bytes) == kGoldenHex,
          "rich asset frame no longer matches golden bytes");

  RenderAssetDeltaTransportDecoder decoder(delta.registry_id);
  const RenderAssetDeltaTransportDecodeResult decoded =
      decoder.Accept(first.bytes);
  Require(decoded.ok() && decoded.message->sequence() == 1U &&
              decoded.message->kind() ==
                  RenderTransportMessageKind::RENDER_ASSET_DELTA_V1,
          "rich delta did not decode");
  RequireEquivalentDelta(*decoded.message->delta(), delta);
  Require(decoder.registry().sequence() == 2U &&
              decoder.registry().record_count() == 5U &&
              decoder.registry().live_count() == 4U &&
              decoder.registry().Find(Id(5U)) != nullptr &&
              !decoder.registry().Find(Id(5U))->live(),
          "decoded registry lost catalog or tombstone state");
  const auto reencoded = EncodeRenderAssetDeltaTransportFrame(
      1U, *decoded.message->delta());
  Require(reencoded.ok() && reencoded.bytes == first.bytes,
          "asset decode/re-encode was not byte deterministic");

  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(
          decoded.message->delta()->mutations[0U].payload);
  const SamplerResourceDescriptor &sampler =
      std::get<SamplerResourceDescriptor>(
          decoded.message->delta()->mutations[2U].payload);
  const MaterialDescriptor &material =
      std::get<MaterialDescriptor>(
          decoded.message->delta()->mutations[3U].payload);
  Require(FloatBits(mesh.positions.front().x) == 0x80000000U &&
              FloatBits(sampler.mip_lod_bias) == 0x80000000U &&
              FloatBits(material.metallic_factor) == 0x80000000U,
          "asset signed-zero revision identity was canonicalized or lost");
}

void TestFramingCountsLengthsAndCorruption() {
  const RenderAssetDelta delta = MakeRichDelta();
  const auto encoded = EncodeRenderAssetDeltaTransportFrame(1U, delta);
  Require(encoded.ok(), "framing fixture was not encoded");

  for (std::size_t size = 0U; size < encoded.bytes.size(); ++size) {
    const auto size_offset =
        static_cast<std::vector<std::uint8_t>::difference_type>(size);
    const auto prefix_end = encoded.bytes.begin() + size_offset;
    const std::vector<std::uint8_t> prefix(encoded.bytes.begin(),
                                           prefix_end);
    const RenderTransportStatus expected =
        size < kRenderTransportEnvelopeHeaderBytes
            ? RenderTransportStatus::FRAME_TRUNCATED
            : RenderTransportStatus::FRAME_SIZE_MISMATCH;
    RequireStatus(
        RenderAssetDeltaTransportDecoder(delta.registry_id).Accept(prefix).status,
        expected, "a truncated asset prefix was not rejected");
  }
  for (std::size_t offset = 32U; offset < encoded.bytes.size(); ++offset) {
    std::vector<std::uint8_t> corrupt = encoded.bytes;
    corrupt[offset] ^= 1U;
    RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                      .Accept(corrupt)
                      .status,
                  RenderTransportStatus::PAYLOAD_DIGEST_MISMATCH,
                  "digest or payload corruption was accepted");
  }

  std::vector<std::uint8_t> frame = encoded.bytes;
  WriteU16(frame, 12U, 99U);
  RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                    .Accept(frame)
                    .status,
                RenderTransportStatus::UNKNOWN_MESSAGE_KIND,
                "unknown envelope kind was accepted");
  frame = encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes, 2U);
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                    .Accept(frame)
                    .status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unknown asset payload version was accepted");

  // Prefix versions/registry/base/target/full consume 53 payload bytes.
  frame = encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes + 53U,
           (std::numeric_limits<std::uint32_t>::max)());
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                    .Accept(frame)
                    .status,
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "malicious mutation count reached allocation");

  // Mutation fixed prefix is 35 bytes; its u64 resource length starts at 84.
  frame = encoded.bytes;
  WriteU64(frame, kRenderTransportEnvelopeHeaderBytes + 84U,
           kRenderAssetDeltaTransportMaximumResourceBytes + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                    .Accept(frame)
                    .status,
                RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED,
                "oversized resource declaration reached allocation");

  const auto mesh_frame =
      EncodeRenderAssetDeltaTransportFrame(1U, MakeMeshOnlyDelta());
  Require(mesh_frame.ok(), "mesh malicious-count fixture was not encoded");
  frame = mesh_frame.bytes;
  // Fixed payload 57 + mutation 35 + mesh fields through bounds 42.
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes + 134U,
           kRenderAssetDeltaTransportMaximumMeshPositions + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(92U).Accept(frame).status,
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "oversized mesh vector count reached allocation");

  const auto texture_frame =
      EncodeRenderAssetDeltaTransportFrame(1U, MakeTextureOnlyDelta());
  Require(texture_frame.ok(), "texture malicious-blob fixture was not encoded");
  frame = texture_frame.bytes;
  // Fixed payload 57 + mutation 35 + texture header/mip fields to blob 50.
  WriteU64(frame, kRenderTransportEnvelopeHeaderBytes + 142U,
           kRenderAssetDeltaTransportMaximumBlobBytes + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(91U).Accept(frame).status,
                RenderTransportStatus::BLOB_LIMIT_EXCEEDED,
                "oversized texture blob reached allocation");

  frame = encoded.bytes;
  frame.push_back(0U);
  WriteU64(frame, 24U, ReadU64(frame, 24U) + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(RenderAssetDeltaTransportDecoder(delta.registry_id)
                    .Accept(frame)
                    .status,
                RenderTransportStatus::MALFORMED_PAYLOAD,
                "trailing asset payload byte was accepted");
}

void TestRegistryTransactionsOrderDependenciesAndTombstones() {
  const RenderAssetDelta base = MakeRichDelta();
  const auto base_frame = EncodeRenderAssetDeltaTransportFrame(1U, base);
  RenderAssetDeltaTransportDecoder decoder(base.registry_id);
  const auto accepted_base = decoder.Accept(base_frame.bytes);
  Require(accepted_base.ok(), "base catalog frame was rejected");
  const auto published_base = decoder.published();

  RenderAssetDelta unsafe;
  unsafe.registry_id = base.registry_id;
  unsafe.base_sequence = 2U;
  unsafe.sequence = 3U;
  unsafe.mutations.push_back(
      Destroy(Ref(RenderAssetKind::TEXTURE, 2U, 2U)));
  const auto unsafe_frame =
      EncodeRenderAssetDeltaTransportFrame(2U, unsafe);
  Require(unsafe_frame.ok(), "unsafe dependency delta was not encodable");
  RequireStatus(decoder.Accept(unsafe_frame.bytes).status,
                RenderTransportStatus::REGISTRY_VALIDATION_FAILED,
                "live material dependency was tombstoned");
  Require(decoder.registry().sequence() == 2U &&
              decoder.next_expected_sequence() == 2U &&
              decoder.published() == published_base,
          "rejected dependency transaction mutated state");

  RenderAssetDelta safe = unsafe;
  MaterialDescriptor untextured = MakeMaterial();
  untextured.base_color_texture = {};
  safe.mutations.push_back(Upsert(Ref(RenderAssetKind::MATERIAL, 4U, 2U),
                                  untextured));
  const auto safe_frame = EncodeRenderAssetDeltaTransportFrame(2U, safe);
  Require(safe_frame.ok() && decoder.Accept(safe_frame.bytes).ok(),
          "same-transaction unbind and tombstone was rejected");
  Require(decoder.registry().sequence() == 3U &&
              decoder.registry().Find(Id(2U)) != nullptr &&
              !decoder.registry().Find(Id(2U))->live() &&
              decoder.next_expected_sequence() == 3U,
          "accepted tombstone did not preserve terminal lineage");
  const auto published_safe = decoder.published();

  RequireStatus(decoder.Accept(safe_frame.bytes).status,
                RenderTransportStatus::REPLAYED_SEQUENCE,
                "asset envelope replay was accepted");
  const auto gap_frame = EncodeRenderAssetDeltaTransportFrame(4U, safe);
  RequireStatus(decoder.Accept(gap_frame.bytes).status,
                RenderTransportStatus::OUT_OF_ORDER_SEQUENCE,
                "asset envelope gap was accepted");

  RenderAssetDelta resurrection;
  resurrection.registry_id = base.registry_id;
  resurrection.base_sequence = 3U;
  resurrection.sequence = 4U;
  resurrection.mutations.push_back(Upsert(
      Ref(RenderAssetKind::TEXTURE, 2U, 3U), MakeTexture("resurrection")));
  const auto resurrection_frame =
      EncodeRenderAssetDeltaTransportFrame(3U, resurrection);
  Require(resurrection_frame.ok(), "resurrection fixture was not encodable");
  RequireStatus(decoder.Accept(resurrection_frame.bytes).status,
                RenderTransportStatus::REGISTRY_VALIDATION_FAILED,
                "terminal tombstone was resurrected");
  Require(decoder.registry().sequence() == 3U &&
              decoder.next_expected_sequence() == 3U &&
              decoder.published() == published_safe,
          "rejected resurrection mutated catalog or transport lineage");

  RenderAssetDelta reversed = base;
  std::swap(reversed.mutations[0U], reversed.mutations[1U]);
  RequireStatus(EncodeRenderAssetDeltaTransportFrame(1U, reversed).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "encoder admitted noncanonical mutation order");
  std::vector<std::uint8_t> reordered = base_frame.bytes;
  // First mutation low ID starts at payload offset 67.
  WriteU64(reordered, kRenderTransportEnvelopeHeaderBytes + 67U, 99U);
  RefreshPayloadDigest(reordered);
  RequireStatus(RenderAssetDeltaTransportDecoder(base.registry_id)
                    .Accept(reordered)
                    .status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "decoder admitted noncanonical mutation order");
}

Matrix4x4 MakePerspectiveProjection(float near_plane = 0.1F,
                                    float far_plane = 1000.0F) {
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
  camera.view_id = 1U;
  camera.width = 1280U;
  camera.height = 720U;
  camera.far_plane = 1000.0F;
  camera.clip_from_view = MakePerspectiveProjection();
  camera.previous_clip_from_view = camera.clip_from_view;
  Require(ValidateCameraViewRequest(camera).ok(),
          "interleaving camera fixture must be valid");
  return camera;
}

std::shared_ptr<const SceneSnapshot> MakeScene(std::uint64_t snapshot_id,
                                               std::uint64_t asset_sequence,
                                               std::uint64_t material_revision) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = 77U;
  descriptor.asset_sequence = asset_sequence;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material =
      Ref(RenderAssetKind::MATERIAL, 4U, material_revision);
  instance.topology_revision = MakeMesh().topology_revision;
  instance.local_bounds = MakeMesh().local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(result.ok(), "interleaving scene fixture must be valid");
  return result.snapshot;
}

void TestSharedSequenceSceneAssetInterleaving() {
  RenderTransportSequenceState shared_sequence;
  RenderAssetDeltaTransportDecoder assets(77U, shared_sequence);
  SceneSnapshotTransportDecoder scenes(shared_sequence);

  const RenderAssetDelta base = MakeRichDelta();
  const auto asset_one = EncodeRenderAssetDeltaTransportFrame(1U, base);
  const auto scene_one = EncodeSceneSnapshotTransportFrame(
      2U, *MakeScene(1U, 2U, 1U), MakeCamera());
  Require(asset_one.ok() && scene_one.ok(),
          "interleaving fixtures were not encoded");
  Require(assets.Accept(asset_one.bytes).ok() &&
              shared_sequence.next_expected_sequence() == 2U,
          "asset did not start shared envelope lineage");
  RequireStatus(assets.Accept(scene_one.bytes).status,
                RenderTransportStatus::UNKNOWN_MESSAGE_KIND,
                "asset decoder accepted a scene kind");
  Require(shared_sequence.next_expected_sequence() == 2U,
          "wrong-kind route advanced shared lineage");
  const auto decoded_scene_one = scenes.Accept(scene_one.bytes);
  Require(decoded_scene_one.ok() &&
              shared_sequence.next_expected_sequence() == 3U &&
              ValidateSceneSnapshotAssets(
                  *decoded_scene_one.message->scene_snapshot(),
                  assets.registry())
                  .ok(),
          "scene did not resolve after its preceding asset transaction");

  RenderAssetDelta safe;
  safe.registry_id = 77U;
  safe.base_sequence = 2U;
  safe.sequence = 3U;
  safe.mutations.push_back(
      Destroy(Ref(RenderAssetKind::TEXTURE, 2U, 2U)));
  MaterialDescriptor untextured = MakeMaterial();
  untextured.base_color_texture = {};
  safe.mutations.push_back(Upsert(Ref(RenderAssetKind::MATERIAL, 4U, 2U),
                                  untextured));
  const auto asset_two = EncodeRenderAssetDeltaTransportFrame(3U, safe);
  const auto scene_two = EncodeSceneSnapshotTransportFrame(
      4U, *MakeScene(2U, 3U, 2U), MakeCamera());
  Require(asset_two.ok() && scene_two.ok() &&
              assets.Accept(asset_two.bytes).ok() &&
              scenes.Accept(scene_two.bytes).ok() &&
              shared_sequence.next_expected_sequence() == 5U &&
              ValidateSceneSnapshotAssets(
                  *scenes.published()->scene_snapshot(), assets.registry())
                  .ok(),
          "asset/scene/asset/scene interleaving lost catalog ordering");
}

void TestEncoderFailClosed() {
  RenderAssetDelta invalid = MakeRichDelta();
  invalid.registry_id = 0U;
  RequireStatus(EncodeRenderAssetDeltaTransportFrame(1U, invalid).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "invalid delta was encoded");
  RequireStatus(EncodeRenderAssetDeltaTransportFrame(0U, MakeRichDelta()).status,
                RenderTransportStatus::INVALID_ARGUMENT,
                "zero envelope sequence was encoded");
}

} // namespace

int main() {
  TestGoldenRichDeltaAndRoundTrip();
  TestFramingCountsLengthsAndCorruption();
  TestRegistryTransactionsOrderDependenciesAndTombstones();
  TestSharedSequenceSceneAssetInterleaving();
  TestEncoderFailClosed();
  return EXIT_SUCCESS;
}
