/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetDeltaTransport.h"

#include "RenderTransportDetail.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace RoR::Render {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  std::numeric_limits<float>::is_iec559,
              "asset transport requires IEC 559 binary32 floats");
static_assert(kRenderAssetRegistryContractVersion ==
                  kRenderAssetDeltaTransportRegistryVersion,
              "a new registry schema requires a new transport message kind");
static_assert(kMeshResourceDescriptorVersion ==
                  kRenderAssetDeltaTransportMeshVersion,
              "a new mesh schema requires a new transport message kind");
static_assert(kTextureResourceDescriptorVersion ==
                  kRenderAssetDeltaTransportTextureVersion,
              "a new texture schema requires a new transport message kind");
static_assert(kMaterialDescriptorVersion ==
                  kRenderAssetDeltaTransportMaterialVersion,
              "a new material schema requires a new transport message kind");
static_assert(kSamplerResourceDescriptorVersion ==
                  kRenderAssetDeltaTransportSamplerVersion,
              "a new sampler schema requires a new transport message kind");

using TransportDetail::AllocationBudget;
using TransportDetail::WireReader;
using TransportDetail::WireWriter;

constexpr std::size_t kMinimumMutationBytes = 35U;
constexpr std::size_t kMinimumTextureMipBytes = 32U;

bool WriteFloat2(WireWriter &writer, const Float2 &value) {
  return writer.AddFloatExact(value.x) && writer.AddFloatExact(value.y);
}

bool WriteFloat3(WireWriter &writer, const Float3 &value) {
  return writer.AddFloatExact(value.x) && writer.AddFloatExact(value.y) &&
         writer.AddFloatExact(value.z);
}

bool WriteFloat4(WireWriter &writer, const Float4 &value) {
  return writer.AddFloatExact(value.x) && writer.AddFloatExact(value.y) &&
         writer.AddFloatExact(value.z) && writer.AddFloatExact(value.w);
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

bool WriteString(WireWriter &writer, const std::string &value) {
  return value.size() <= kMaximumResourceDebugNameBytes &&
         writer.AddU16(static_cast<std::uint16_t>(value.size())) &&
         writer.AddBytes(reinterpret_cast<const std::uint8_t *>(value.data()),
                         value.size());
}

template <typename Value, typename WriteValue>
bool WriteVector(WireWriter &writer, const std::vector<Value> &values,
                 WriteValue write_value) {
  if (!writer.AddU32(static_cast<std::uint32_t>(values.size()))) {
    return false;
  }
  for (const Value &value : values) {
    if (!write_value(writer, value)) {
      return false;
    }
  }
  return true;
}

bool WriteFloat2Value(WireWriter &writer, const Float2 &value) {
  return WriteFloat2(writer, value);
}

bool WriteFloat3Value(WireWriter &writer, const Float3 &value) {
  return WriteFloat3(writer, value);
}

bool WriteFloat4Value(WireWriter &writer, const Float4 &value) {
  return WriteFloat4(writer, value);
}

bool WriteIndex(WireWriter &writer, std::uint32_t value) {
  return writer.AddU32(value);
}

bool WriteMesh(WireWriter &writer, const MeshResourceDescriptor &mesh) {
  return writer.AddU32(mesh.version) && WriteString(writer, mesh.debug_name) &&
         writer.AddByte(static_cast<std::uint8_t>(mesh.topology)) &&
         writer.AddByte(static_cast<std::uint8_t>(mesh.index_format)) &&
         writer.AddU64(mesh.topology_revision) && writer.AddBool(mesh.dynamic) &&
         WriteBounds(writer, mesh.local_bounds) &&
         WriteVector(writer, mesh.positions, WriteFloat3Value) &&
         WriteVector(writer, mesh.normals, WriteFloat3Value) &&
         WriteVector(writer, mesh.tangents, WriteFloat4Value) &&
         WriteVector(writer, mesh.velocities, WriteFloat3Value) &&
         WriteVector(writer, mesh.texture_coordinates_0, WriteFloat2Value) &&
         WriteVector(writer, mesh.texture_coordinates_1, WriteFloat2Value) &&
         WriteVector(writer, mesh.colors, WriteFloat4Value) &&
         WriteVector(writer, mesh.indices, WriteIndex);
}

bool WriteTexture(WireWriter &writer,
                  const TextureResourceDescriptor &texture) {
  if (!writer.AddU32(texture.version) ||
      !WriteString(writer, texture.debug_name) ||
      !writer.AddByte(static_cast<std::uint8_t>(texture.type)) ||
      !writer.AddByte(static_cast<std::uint8_t>(texture.format)) ||
      !writer.AddByte(static_cast<std::uint8_t>(texture.color_space)) ||
      !writer.AddU32(texture.width) || !writer.AddU32(texture.height) ||
      !writer.AddU32(texture.array_layers) ||
      !writer.AddU32(static_cast<std::uint32_t>(texture.mip_levels.size()))) {
    return false;
  }
  for (const TextureMipLevelDescriptor &mip : texture.mip_levels) {
    if (!writer.AddU32(mip.width) || !writer.AddU32(mip.height) ||
        !writer.AddU64(mip.row_pitch_bytes) ||
        !writer.AddU64(mip.layer_pitch_bytes) ||
        !writer.AddU64(static_cast<std::uint64_t>(mip.bytes.size())) ||
        !writer.AddBytes(mip.bytes.data(), mip.bytes.size())) {
      return false;
    }
  }
  return true;
}

bool WriteTextureBinding(WireWriter &writer, const TextureBinding &binding) {
  return WriteAssetReference(writer, binding.texture) &&
         WriteAssetReference(writer, binding.sampler) &&
         writer.AddByte(binding.texture_coordinate_set) &&
         WriteFloat2(writer, binding.scale) && WriteFloat2(writer, binding.offset) &&
         writer.AddFloatExact(binding.rotation_radians);
}

bool WriteMaterial(WireWriter &writer, const MaterialDescriptor &material) {
  return writer.AddU32(material.version) &&
         WriteString(writer, material.debug_name) &&
         writer.AddByte(static_cast<std::uint8_t>(material.model)) &&
         writer.AddByte(static_cast<std::uint8_t>(material.alpha_mode)) &&
         writer.AddBool(material.double_sided) &&
         WriteFloat4(writer, material.base_color_factor) &&
         writer.AddFloatExact(material.metallic_factor) &&
         writer.AddFloatExact(material.roughness_factor) &&
         writer.AddFloatExact(material.normal_scale) &&
         writer.AddFloatExact(material.occlusion_strength) &&
         WriteFloat3(writer, material.emissive_factor) &&
         writer.AddFloatExact(material.emissive_strength) &&
         writer.AddFloatExact(material.alpha_cutoff) &&
         writer.AddFloatExact(material.index_of_refraction) &&
         WriteTextureBinding(writer, material.base_color_texture) &&
         WriteTextureBinding(writer, material.metallic_roughness_texture) &&
         WriteTextureBinding(writer, material.normal_texture) &&
         WriteTextureBinding(writer, material.occlusion_texture) &&
         WriteTextureBinding(writer, material.emissive_texture);
}

bool WriteSampler(WireWriter &writer,
                  const SamplerResourceDescriptor &sampler) {
  return writer.AddU32(sampler.version) &&
         WriteString(writer, sampler.debug_name) &&
         writer.AddByte(
             static_cast<std::uint8_t>(sampler.minification_filter)) &&
         writer.AddByte(
             static_cast<std::uint8_t>(sampler.magnification_filter)) &&
         writer.AddByte(static_cast<std::uint8_t>(sampler.mip_filter)) &&
         writer.AddByte(static_cast<std::uint8_t>(sampler.address_u)) &&
         writer.AddByte(static_cast<std::uint8_t>(sampler.address_v)) &&
         writer.AddByte(static_cast<std::uint8_t>(sampler.address_w)) &&
         writer.AddFloatExact(sampler.mip_lod_bias) &&
         writer.AddFloatExact(sampler.minimum_lod) &&
         writer.AddFloatExact(sampler.maximum_lod) &&
         writer.AddBool(sampler.anisotropy_enabled) &&
         writer.AddFloatExact(sampler.maximum_anisotropy) &&
         writer.AddBool(sampler.compare_enabled) &&
         writer.AddByte(static_cast<std::uint8_t>(sampler.compare_operation)) &&
         WriteFloat4(writer, sampler.border_color);
}

bool WriteResource(WireWriter &writer, const RenderAssetPayload &payload) {
  if (const auto *mesh = std::get_if<MeshResourceDescriptor>(&payload)) {
    return WriteMesh(writer, *mesh);
  }
  if (const auto *texture =
          std::get_if<TextureResourceDescriptor>(&payload)) {
    return WriteTexture(writer, *texture);
  }
  if (const auto *material = std::get_if<MaterialDescriptor>(&payload)) {
    return WriteMaterial(writer, *material);
  }
  if (const auto *sampler =
          std::get_if<SamplerResourceDescriptor>(&payload)) {
    return WriteSampler(writer, *sampler);
  }
  return false;
}

RenderTransportStatus ValidateTransportCaps(const RenderAssetDelta &delta) {
  if (delta.mutations.size() >
      kRenderAssetDeltaTransportMaximumMutations) {
    return RenderTransportStatus::COUNT_LIMIT_EXCEEDED;
  }
  std::uint64_t total_blob_bytes = 0U;
  for (const RenderAssetMutation &mutation : delta.mutations) {
    if (const auto *mesh =
            std::get_if<MeshResourceDescriptor>(&mutation.payload)) {
      const auto stream_too_large = [](std::size_t size) {
        return size > kRenderAssetDeltaTransportMaximumMeshPositions;
      };
      if (stream_too_large(mesh->positions.size()) ||
          stream_too_large(mesh->normals.size()) ||
          stream_too_large(mesh->tangents.size()) ||
          stream_too_large(mesh->velocities.size()) ||
          stream_too_large(mesh->texture_coordinates_0.size()) ||
          stream_too_large(mesh->texture_coordinates_1.size()) ||
          stream_too_large(mesh->colors.size()) ||
          mesh->indices.size() >
              kRenderAssetDeltaTransportMaximumMeshIndices) {
        return RenderTransportStatus::COUNT_LIMIT_EXCEEDED;
      }
    }
    if (const auto *texture =
            std::get_if<TextureResourceDescriptor>(&mutation.payload)) {
      if (texture->mip_levels.size() >
          kRenderAssetDeltaTransportMaximumTextureMipLevels) {
        return RenderTransportStatus::COUNT_LIMIT_EXCEEDED;
      }
      for (const TextureMipLevelDescriptor &mip : texture->mip_levels) {
        if (mip.bytes.size() >
            kRenderAssetDeltaTransportMaximumBlobBytes) {
          return RenderTransportStatus::BLOB_LIMIT_EXCEEDED;
        }
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(mip.bytes.size());
        if (total_blob_bytes >
                kRenderAssetDeltaTransportMaximumTotalBlobBytes ||
            bytes > kRenderAssetDeltaTransportMaximumTotalBlobBytes -
                        total_blob_bytes) {
          return RenderTransportStatus::BLOB_LIMIT_EXCEEDED;
        }
        total_blob_bytes += bytes;
      }
    }
  }
  return RenderTransportStatus::OK;
}

RenderTransportStatus PrepareResourceSizes(
    const RenderAssetDelta &delta, std::vector<std::uint64_t> &sizes) {
  sizes.clear();
  sizes.reserve(delta.mutations.size());
  for (const RenderAssetMutation &mutation : delta.mutations) {
    if (mutation.type == RenderAssetMutationType::DESTROY) {
      sizes.push_back(0U);
      continue;
    }
    WireWriter sizer(nullptr,
                     kRenderAssetDeltaTransportMaximumResourceBytes);
    if (!WriteResource(sizer, mutation.payload) || !sizer.ok()) {
      return RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED;
    }
    sizes.push_back(sizer.size());
  }
  return RenderTransportStatus::OK;
}

bool WritePayload(WireWriter &writer, const RenderAssetDelta &delta,
                  const std::vector<std::uint64_t> &resource_sizes) {
  if (resource_sizes.size() != delta.mutations.size() ||
      !writer.AddU32(kRenderAssetDeltaTransportPayloadVersion) ||
      !writer.AddU32(kRenderAssetDeltaTransportRegistryVersion) ||
      !writer.AddU32(kRenderAssetDeltaTransportMeshVersion) ||
      !writer.AddU32(kRenderAssetDeltaTransportTextureVersion) ||
      !writer.AddU32(kRenderAssetDeltaTransportMaterialVersion) ||
      !writer.AddU32(kRenderAssetDeltaTransportSamplerVersion) ||
      !writer.AddU32(0U) || !writer.AddU64(delta.registry_id) ||
      !writer.AddU64(delta.base_sequence) || !writer.AddU64(delta.sequence) ||
      !writer.AddBool(delta.full_snapshot) ||
      !writer.AddU32(static_cast<std::uint32_t>(delta.mutations.size()))) {
    return false;
  }

  for (std::size_t index = 0U; index < delta.mutations.size(); ++index) {
    const RenderAssetMutation &mutation = delta.mutations[index];
    const RenderAssetKind payload_kind =
        mutation.type == RenderAssetMutationType::DESTROY
            ? RenderAssetKind::INVALID
            : RenderAssetPayloadKind(mutation.payload);
    if (!writer.AddByte(static_cast<std::uint8_t>(mutation.type)) ||
        !WriteAssetReference(writer, mutation.asset) ||
        !writer.AddByte(static_cast<std::uint8_t>(payload_kind)) ||
        !writer.AddU64(resource_sizes[index])) {
      return false;
    }
    if (mutation.type == RenderAssetMutationType::UPSERT) {
      const std::uint64_t before = writer.size();
      if (!WriteResource(writer, mutation.payload) ||
          writer.size() - before != resource_sizes[index]) {
        return false;
      }
    }
  }
  return true;
}

bool ReadFloat2(WireReader &reader, Float2 &value) {
  return reader.ReadFloatExact(value.x) && reader.ReadFloatExact(value.y);
}

bool ReadFloat3(WireReader &reader, Float3 &value) {
  return reader.ReadFloatExact(value.x) && reader.ReadFloatExact(value.y) &&
         reader.ReadFloatExact(value.z);
}

bool ReadFloat4(WireReader &reader, Float4 &value) {
  return reader.ReadFloatExact(value.x) && reader.ReadFloatExact(value.y) &&
         reader.ReadFloatExact(value.z) && reader.ReadFloatExact(value.w);
}

bool ReadBounds(WireReader &reader, Bounds3 &value) {
  return ReadFloat3(reader, value.minimum) &&
         ReadFloat3(reader, value.maximum);
}

bool ReadAssetReference(WireReader &reader,
                        RenderAssetReference &reference) {
  std::uint8_t kind = 0U;
  std::uint64_t high = 0U;
  std::uint64_t low = 0U;
  if (!reader.ReadByte(kind) || !reader.ReadU64(high) ||
      !reader.ReadU64(low) || !reader.ReadU64(reference.revision)) {
    return false;
  }
  reference.id = RenderAssetId::FromWords(high, low);
  reference.kind = static_cast<RenderAssetKind>(kind);
  return true;
}

bool ReadString(WireReader &reader, std::string &value) {
  std::uint16_t size = 0U;
  if (!reader.ReadU16(size)) {
    return false;
  }
  if (size > kMaximumResourceDebugNameBytes) {
    reader.Fail(RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED);
    return false;
  }
  if (!reader.ChargeAllocation(size, sizeof(char))) {
    return false;
  }
  const std::uint8_t *bytes = nullptr;
  if (!reader.ReadView(size, bytes)) {
    return false;
  }
  value.assign(reinterpret_cast<const char *>(bytes), size);
  return true;
}

template <typename Value, typename ReadValue>
bool ReadVector(WireReader &reader, std::uint32_t maximum,
                std::size_t minimum_item_bytes, std::vector<Value> &values,
                ReadValue read_value) {
  std::uint32_t count = 0U;
  if (!reader.ReadCount(maximum, minimum_item_bytes, count) ||
      !reader.Reserve(values, count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < count; ++index) {
    Value value;
    if (!read_value(reader, value)) {
      return false;
    }
    values.push_back(value);
  }
  return true;
}

bool ReadFloat2Value(WireReader &reader, Float2 &value) {
  return ReadFloat2(reader, value);
}

bool ReadFloat3Value(WireReader &reader, Float3 &value) {
  return ReadFloat3(reader, value);
}

bool ReadFloat4Value(WireReader &reader, Float4 &value) {
  return ReadFloat4(reader, value);
}

bool ReadIndex(WireReader &reader, std::uint32_t &value) {
  return reader.ReadU32(value);
}

bool ReadMesh(WireReader &reader, MeshResourceDescriptor &mesh) {
  std::uint8_t topology = 0U;
  std::uint8_t index_format = 0U;
  if (!reader.ReadU32(mesh.version) || !ReadString(reader, mesh.debug_name) ||
      !reader.ReadByte(topology) || !reader.ReadByte(index_format) ||
      !reader.ReadU64(mesh.topology_revision) ||
      !reader.ReadBool(mesh.dynamic) || !ReadBounds(reader, mesh.local_bounds)) {
    return false;
  }
  mesh.topology = static_cast<MeshPrimitiveTopology>(topology);
  mesh.index_format = static_cast<MeshIndexFormat>(index_format);
  return ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 3U, mesh.positions, ReadFloat3Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 3U, mesh.normals, ReadFloat3Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 4U, mesh.tangents, ReadFloat4Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 3U, mesh.velocities, ReadFloat3Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 2U, mesh.texture_coordinates_0,
                    ReadFloat2Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 2U, mesh.texture_coordinates_1,
                    ReadFloat2Value) &&
         ReadVector(reader,
                    kRenderAssetDeltaTransportMaximumMeshPositions,
                    sizeof(float) * 4U, mesh.colors, ReadFloat4Value) &&
         ReadVector(reader, kRenderAssetDeltaTransportMaximumMeshIndices,
                    sizeof(std::uint32_t), mesh.indices, ReadIndex);
}

bool ReadTexture(WireReader &reader, TextureResourceDescriptor &texture,
                 std::uint64_t &total_blob_bytes) {
  std::uint8_t type = 0U;
  std::uint8_t format = 0U;
  std::uint8_t color_space = 0U;
  if (!reader.ReadU32(texture.version) ||
      !ReadString(reader, texture.debug_name) || !reader.ReadByte(type) ||
      !reader.ReadByte(format) || !reader.ReadByte(color_space) ||
      !reader.ReadU32(texture.width) || !reader.ReadU32(texture.height) ||
      !reader.ReadU32(texture.array_layers)) {
    return false;
  }
  texture.type = static_cast<TextureResourceType>(type);
  texture.format = static_cast<TextureResourceFormat>(format);
  texture.color_space = static_cast<TextureColorSpace>(color_space);

  std::uint32_t mip_count = 0U;
  if (!reader.ReadCount(kRenderAssetDeltaTransportMaximumTextureMipLevels,
                        kMinimumTextureMipBytes, mip_count) ||
      !reader.Reserve(texture.mip_levels, mip_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < mip_count; ++index) {
    TextureMipLevelDescriptor mip;
    std::uint64_t blob_size = 0U;
    if (!reader.ReadU32(mip.width) || !reader.ReadU32(mip.height) ||
        !reader.ReadU64(mip.row_pitch_bytes) ||
        !reader.ReadU64(mip.layer_pitch_bytes) ||
        !reader.ReadU64(blob_size)) {
      return false;
    }
    if (blob_size > kRenderAssetDeltaTransportMaximumBlobBytes ||
        total_blob_bytes >
            kRenderAssetDeltaTransportMaximumTotalBlobBytes ||
        blob_size > kRenderAssetDeltaTransportMaximumTotalBlobBytes -
                        total_blob_bytes) {
      reader.Fail(RenderTransportStatus::BLOB_LIMIT_EXCEEDED);
      return false;
    }
    if (blob_size > reader.remaining()) {
      reader.Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
      return false;
    }
    if (!reader.ChargeAllocation(blob_size, sizeof(std::uint8_t))) {
      return false;
    }
    const std::uint8_t *blob = nullptr;
    if (!reader.ReadView(static_cast<std::size_t>(blob_size), blob)) {
      return false;
    }
    mip.bytes.reserve(static_cast<std::size_t>(blob_size));
    mip.bytes.insert(mip.bytes.end(), blob,
                     blob + static_cast<std::size_t>(blob_size));
    total_blob_bytes += blob_size;
    texture.mip_levels.push_back(std::move(mip));
  }
  return true;
}

bool ReadTextureBinding(WireReader &reader, TextureBinding &binding) {
  return ReadAssetReference(reader, binding.texture) &&
         ReadAssetReference(reader, binding.sampler) &&
         reader.ReadByte(binding.texture_coordinate_set) &&
         ReadFloat2(reader, binding.scale) && ReadFloat2(reader, binding.offset) &&
         reader.ReadFloatExact(binding.rotation_radians);
}

bool ReadMaterial(WireReader &reader, MaterialDescriptor &material) {
  std::uint8_t model = 0U;
  std::uint8_t alpha_mode = 0U;
  if (!reader.ReadU32(material.version) ||
      !ReadString(reader, material.debug_name) || !reader.ReadByte(model) ||
      !reader.ReadByte(alpha_mode) || !reader.ReadBool(material.double_sided) ||
      !ReadFloat4(reader, material.base_color_factor) ||
      !reader.ReadFloatExact(material.metallic_factor) ||
      !reader.ReadFloatExact(material.roughness_factor) ||
      !reader.ReadFloatExact(material.normal_scale) ||
      !reader.ReadFloatExact(material.occlusion_strength) ||
      !ReadFloat3(reader, material.emissive_factor) ||
      !reader.ReadFloatExact(material.emissive_strength) ||
      !reader.ReadFloatExact(material.alpha_cutoff) ||
      !reader.ReadFloatExact(material.index_of_refraction)) {
    return false;
  }
  material.model = static_cast<MaterialModel>(model);
  material.alpha_mode = static_cast<MaterialAlphaMode>(alpha_mode);
  return ReadTextureBinding(reader, material.base_color_texture) &&
         ReadTextureBinding(reader, material.metallic_roughness_texture) &&
         ReadTextureBinding(reader, material.normal_texture) &&
         ReadTextureBinding(reader, material.occlusion_texture) &&
         ReadTextureBinding(reader, material.emissive_texture);
}

bool ReadSampler(WireReader &reader, SamplerResourceDescriptor &sampler) {
  std::uint8_t minification_filter = 0U;
  std::uint8_t magnification_filter = 0U;
  std::uint8_t mip_filter = 0U;
  std::uint8_t address_u = 0U;
  std::uint8_t address_v = 0U;
  std::uint8_t address_w = 0U;
  std::uint8_t compare_operation = 0U;
  if (!reader.ReadU32(sampler.version) ||
      !ReadString(reader, sampler.debug_name) ||
      !reader.ReadByte(minification_filter) ||
      !reader.ReadByte(magnification_filter) ||
      !reader.ReadByte(mip_filter) || !reader.ReadByte(address_u) ||
      !reader.ReadByte(address_v) || !reader.ReadByte(address_w) ||
      !reader.ReadFloatExact(sampler.mip_lod_bias) ||
      !reader.ReadFloatExact(sampler.minimum_lod) ||
      !reader.ReadFloatExact(sampler.maximum_lod) ||
      !reader.ReadBool(sampler.anisotropy_enabled) ||
      !reader.ReadFloatExact(sampler.maximum_anisotropy) ||
      !reader.ReadBool(sampler.compare_enabled) ||
      !reader.ReadByte(compare_operation) ||
      !ReadFloat4(reader, sampler.border_color)) {
    return false;
  }
  sampler.minification_filter = static_cast<SamplerFilter>(minification_filter);
  sampler.magnification_filter =
      static_cast<SamplerFilter>(magnification_filter);
  sampler.mip_filter = static_cast<SamplerFilter>(mip_filter);
  sampler.address_u = static_cast<SamplerAddressMode>(address_u);
  sampler.address_v = static_cast<SamplerAddressMode>(address_v);
  sampler.address_w = static_cast<SamplerAddressMode>(address_w);
  sampler.compare_operation =
      static_cast<SamplerCompareOperation>(compare_operation);
  return true;
}

bool ReadResource(WireReader &reader, RenderAssetKind kind,
                  RenderAssetPayload &payload,
                  std::uint64_t &total_blob_bytes) {
  switch (kind) {
  case RenderAssetKind::MESH: {
    MeshResourceDescriptor mesh;
    if (!ReadMesh(reader, mesh)) {
      return false;
    }
    payload = std::move(mesh);
    return true;
  }
  case RenderAssetKind::TEXTURE: {
    TextureResourceDescriptor texture;
    if (!ReadTexture(reader, texture, total_blob_bytes)) {
      return false;
    }
    payload = std::move(texture);
    return true;
  }
  case RenderAssetKind::MATERIAL: {
    MaterialDescriptor material;
    if (!ReadMaterial(reader, material)) {
      return false;
    }
    payload = std::move(material);
    return true;
  }
  case RenderAssetKind::SAMPLER: {
    SamplerResourceDescriptor sampler;
    if (!ReadSampler(reader, sampler)) {
      return false;
    }
    payload = std::move(sampler);
    return true;
  }
  case RenderAssetKind::INVALID:
    reader.Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
    return false;
  }
  reader.Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
  return false;
}

bool ReadPayload(const std::uint8_t *payload, std::size_t payload_size,
                 std::shared_ptr<const RenderAssetDelta> &decoded,
                 RenderTransportStatus &status) {
  AllocationBudget allocation_budget(
      kRenderAssetDeltaTransportMaximumDecodedAllocationBytes);
  WireReader reader(payload, payload_size, allocation_budget);
  RenderAssetDelta delta;
  std::uint32_t payload_version = 0U;
  std::uint32_t registry_version = 0U;
  std::uint32_t mesh_version = 0U;
  std::uint32_t texture_version = 0U;
  std::uint32_t material_version = 0U;
  std::uint32_t sampler_version = 0U;
  std::uint32_t reserved = 0U;
  if (!reader.ReadU32(payload_version) ||
      !reader.ReadU32(registry_version) || !reader.ReadU32(mesh_version) ||
      !reader.ReadU32(texture_version) ||
      !reader.ReadU32(material_version) ||
      !reader.ReadU32(sampler_version) || !reader.ReadU32(reserved)) {
    status = reader.status();
    return false;
  }
  if (payload_version != kRenderAssetDeltaTransportPayloadVersion ||
      registry_version != kRenderAssetDeltaTransportRegistryVersion ||
      mesh_version != kRenderAssetDeltaTransportMeshVersion ||
      texture_version != kRenderAssetDeltaTransportTextureVersion ||
      material_version != kRenderAssetDeltaTransportMaterialVersion ||
      sampler_version != kRenderAssetDeltaTransportSamplerVersion ||
      reserved != 0U) {
    status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return false;
  }
  delta.version = registry_version;
  if (!reader.ReadU64(delta.registry_id) ||
      !reader.ReadU64(delta.base_sequence) ||
      !reader.ReadU64(delta.sequence) ||
      !reader.ReadBool(delta.full_snapshot)) {
    status = reader.status();
    return false;
  }

  std::uint32_t mutation_count = 0U;
  if (!reader.ReadCount(kRenderAssetDeltaTransportMaximumMutations,
                        kMinimumMutationBytes, mutation_count) ||
      !reader.Reserve(delta.mutations, mutation_count)) {
    status = reader.status();
    return false;
  }
  std::uint64_t total_blob_bytes = 0U;
  for (std::uint32_t index = 0U; index < mutation_count; ++index) {
    RenderAssetMutation mutation;
    std::uint8_t mutation_type = 0U;
    std::uint8_t payload_kind = 0U;
    std::uint64_t resource_size = 0U;
    if (!reader.ReadByte(mutation_type) ||
        !ReadAssetReference(reader, mutation.asset) ||
        !reader.ReadByte(payload_kind) || !reader.ReadU64(resource_size)) {
      status = reader.status();
      return false;
    }
    mutation.type = static_cast<RenderAssetMutationType>(mutation_type);
    const RenderAssetKind kind = static_cast<RenderAssetKind>(payload_kind);
    if (resource_size > kRenderAssetDeltaTransportMaximumResourceBytes) {
      status = RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED;
      return false;
    }
    if ((mutation.type == RenderAssetMutationType::DESTROY &&
         (kind != RenderAssetKind::INVALID || resource_size != 0U)) ||
        (mutation.type == RenderAssetMutationType::UPSERT &&
         (!IsKnownRenderAssetKind(kind) || kind != mutation.asset.kind ||
          resource_size == 0U))) {
      status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
      return false;
    }
    const std::uint8_t *resource_bytes = nullptr;
    if (!reader.ReadView(static_cast<std::size_t>(resource_size),
                         resource_bytes)) {
      status = reader.status();
      return false;
    }
    if (mutation.type == RenderAssetMutationType::UPSERT) {
      WireReader resource_reader(resource_bytes,
                                 static_cast<std::size_t>(resource_size),
                                 allocation_budget);
      if (!ReadResource(resource_reader, kind, mutation.payload,
                        total_blob_bytes)) {
        status = resource_reader.status();
        return false;
      }
      if (!resource_reader.consumed()) {
        status = RenderTransportStatus::MALFORMED_PAYLOAD;
        return false;
      }
    }
    delta.mutations.push_back(std::move(mutation));
  }
  if (!reader.consumed()) {
    status = RenderTransportStatus::MALFORMED_PAYLOAD;
    return false;
  }
  if (!ValidateRenderAssetDelta(delta).ok()) {
    status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return false;
  }
  decoded = std::make_shared<const RenderAssetDelta>(std::move(delta));
  status = RenderTransportStatus::OK;
  return true;
}

RenderAssetDeltaTransportDecodeResult Failure(RenderTransportStatus status) {
  RenderAssetDeltaTransportDecodeResult result;
  result.status = status;
  return result;
}

} // namespace

RenderAssetDeltaTransportDecoder::RenderAssetDeltaTransportDecoder(
    std::uint64_t registry_id,
    std::uint64_t first_expected_sequence) noexcept
    : registry_(registry_id),
      owned_sequence_state_(first_expected_sequence),
      sequence_state_(&owned_sequence_state_) {}

RenderAssetDeltaTransportDecoder::RenderAssetDeltaTransportDecoder(
    std::uint64_t registry_id,
    RenderTransportSequenceState &shared_sequence_state) noexcept
    : registry_(registry_id), owned_sequence_state_(1U),
      sequence_state_(&shared_sequence_state) {}

RenderAssetDeltaTransportDecodeResult RenderAssetDeltaTransportDecoder::Accept(
    const std::vector<std::uint8_t> &frame) {
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus envelope_status = DecodeRenderTransportEnvelope(
      frame, kRenderAssetDeltaTransportMaximumPayloadBytes, envelope);
  if (envelope_status != RenderTransportStatus::OK) {
    return Failure(envelope_status);
  }
  if (envelope.kind != RenderTransportMessageKind::RENDER_ASSET_DELTA_V1) {
    return Failure(RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  const RenderTransportStatus sequence_status =
      sequence_state_->ValidateCandidate(envelope.sequence);
  if (sequence_status != RenderTransportStatus::OK) {
    return Failure(sequence_status);
  }

  try {
    std::shared_ptr<const RenderAssetDelta> delta;
    RenderTransportStatus status = RenderTransportStatus::MALFORMED_PAYLOAD;
    if (!ReadPayload(envelope.payload, envelope.payload_size, delta, status)) {
      return Failure(status);
    }
    std::shared_ptr<const DecodedRenderAssetDeltaTransportMessage> candidate(
        new DecodedRenderAssetDeltaTransportMessage(envelope.sequence,
                                                    delta));
    const ValidationResult applied = registry_.Apply(*delta);
    if (!applied) {
      return Failure(RenderTransportStatus::REGISTRY_VALIDATION_FAILED);
    }
    if (!sequence_state_->CommitAccepted(envelope.sequence)) {
      return Failure(RenderTransportStatus::INVALID_SEQUENCE);
    }
    published_ = candidate;
    return RenderAssetDeltaTransportDecodeResult{
        std::move(candidate), RenderTransportStatus::OK};
  } catch (const std::bad_alloc &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  }
}

RenderAssetDeltaTransportEncodeResult EncodeRenderAssetDeltaTransportFrame(
    std::uint64_t sequence, const RenderAssetDelta &delta) {
  RenderAssetDeltaTransportEncodeResult result;
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    result.status = RenderTransportStatus::INVALID_ARGUMENT;
    return result;
  }
  if (!ValidateRenderAssetDelta(delta).ok()) {
    result.status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return result;
  }
  result.status = ValidateTransportCaps(delta);
  if (result.status != RenderTransportStatus::OK) {
    return result;
  }

  try {
    std::vector<std::uint64_t> resource_sizes;
    result.status = PrepareResourceSizes(delta, resource_sizes);
    if (result.status != RenderTransportStatus::OK) {
      return result;
    }
    WireWriter sizer(nullptr,
                     kRenderAssetDeltaTransportMaximumPayloadBytes);
    if (!WritePayload(sizer, delta, resource_sizes) || !sizer.ok()) {
      result.status = RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED;
      return result;
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(sizer.size()));
    WireWriter writer(&payload,
                      kRenderAssetDeltaTransportMaximumPayloadBytes);
    if (!WritePayload(writer, delta, resource_sizes) || !writer.ok() ||
        writer.size() != sizer.size()) {
      result.status = RenderTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    return EncodeRenderTransportEnvelope(
        RenderTransportMessageKind::RENDER_ASSET_DELTA_V1, sequence, payload,
        kRenderAssetDeltaTransportMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  } catch (const std::length_error &) {
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  }
}

} // namespace RoR::Render
