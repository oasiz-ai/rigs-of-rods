/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderResourceDescriptors.h"

#include "SceneSnapshot.h"
#include "ValidatedAssetCompatibilityInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace RoR::Render {
namespace {

constexpr float kVectorBasisTolerance = 1.0e-3F;

ValidationResult ValidateDebugName(const std::string &debug_name) {
  if (debug_name.size() > kMaximumResourceDebugNameBytes ||
      debug_name.find('\0') != std::string::npos) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "debug_name",
        "debug name must be at most 255 bytes and contain no NUL");
  }
  return ValidationResult::Success();
}

bool IsUnitVector(const Float3 &value) noexcept {
  if (!IsFinite(value)) {
    return false;
  }
  const float length_squared = LengthSquared(value);
  return IsFinite(length_squared) &&
         std::fabs(length_squared - 1.0F) <= kVectorBasisTolerance;
}

bool BoundsContain(const Bounds3 &bounds, const Float3 &position) noexcept {
  return position.x >= bounds.minimum.x && position.x <= bounds.maximum.x &&
         position.y >= bounds.minimum.y && position.y <= bounds.maximum.y &&
         position.z >= bounds.minimum.z && position.z <= bounds.maximum.z;
}

bool EqualBounds(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return lhs.minimum.x == rhs.minimum.x && lhs.minimum.y == rhs.minimum.y &&
         lhs.minimum.z == rhs.minimum.z && lhs.maximum.x == rhs.maximum.x &&
         lhs.maximum.y == rhs.maximum.y && lhs.maximum.z == rhs.maximum.z;
}

ValidationResult ValidateOptionalStreamSize(std::size_t stream_size,
                                            std::size_t position_count,
                                            const char *field) {
  if (stream_size != 0U && stream_size != position_count) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, field,
        "optional vertex stream must match the position count");
  }
  return ValidationResult::Success();
}

std::uint32_t MaximumMipLevelCount(std::uint32_t width,
                                   std::uint32_t height) noexcept {
  std::uint32_t count = 1U;
  while (width > 1U || height > 1U) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
    ++count;
  }
  return count;
}

} // namespace

bool IsKnownMeshPrimitiveTopology(MeshPrimitiveTopology topology) noexcept {
  switch (topology) {
  case MeshPrimitiveTopology::TRIANGLE_LIST:
  case MeshPrimitiveTopology::LINE_LIST:
  case MeshPrimitiveTopology::POINT_LIST:
    return true;
  }
  return false;
}

bool IsKnownMeshIndexFormat(MeshIndexFormat format) noexcept {
  switch (format) {
  case MeshIndexFormat::UINT16:
  case MeshIndexFormat::UINT32:
    return true;
  }
  return false;
}

bool IsKnownTextureResourceType(TextureResourceType type) noexcept {
  switch (type) {
  case TextureResourceType::TEXTURE_2D:
  case TextureResourceType::TEXTURE_2D_ARRAY:
  case TextureResourceType::TEXTURE_CUBE:
    return true;
  }
  return false;
}

std::uint32_t
BytesPerTextureResourceTexel(TextureResourceFormat format) noexcept {
  switch (format) {
  case TextureResourceFormat::R8_UNORM:
    return 1U;
  case TextureResourceFormat::RG8_UNORM:
  case TextureResourceFormat::R16_FLOAT:
    return 2U;
  case TextureResourceFormat::RGBA8_UNORM:
  case TextureResourceFormat::RG16_FLOAT:
  case TextureResourceFormat::R32_FLOAT:
    return 4U;
  case TextureResourceFormat::RGBA16_FLOAT:
    return 8U;
  case TextureResourceFormat::RGBA32_FLOAT:
    return 16U;
  }
  return 0U;
}

bool IsKnownTextureResourceFormat(TextureResourceFormat format) noexcept {
  return BytesPerTextureResourceTexel(format) != 0U;
}

bool IsKnownTextureColorSpace(TextureColorSpace color_space) noexcept {
  switch (color_space) {
  case TextureColorSpace::LINEAR:
  case TextureColorSpace::SRGB:
    return true;
  }
  return false;
}

bool IsKnownSamplerFilter(SamplerFilter filter) noexcept {
  switch (filter) {
  case SamplerFilter::NEAREST:
  case SamplerFilter::LINEAR:
    return true;
  }
  return false;
}

bool IsKnownSamplerAddressMode(SamplerAddressMode address_mode) noexcept {
  switch (address_mode) {
  case SamplerAddressMode::REPEAT:
  case SamplerAddressMode::MIRRORED_REPEAT:
  case SamplerAddressMode::CLAMP_TO_EDGE:
  case SamplerAddressMode::CLAMP_TO_BORDER:
    return true;
  }
  return false;
}

bool IsKnownSamplerCompareOperation(
    SamplerCompareOperation operation) noexcept {
  switch (operation) {
  case SamplerCompareOperation::NEVER:
  case SamplerCompareOperation::LESS:
  case SamplerCompareOperation::EQUAL:
  case SamplerCompareOperation::LESS_EQUAL:
  case SamplerCompareOperation::GREATER:
  case SamplerCompareOperation::NOT_EQUAL:
  case SamplerCompareOperation::GREATER_EQUAL:
  case SamplerCompareOperation::ALWAYS:
    return true;
  }
  return false;
}

ValidationResult
ValidateMeshResourceDescriptor(const MeshResourceDescriptor &descriptor) {
  if (descriptor.version != kMeshResourceDescriptorVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported mesh descriptor version");
  }
  ValidationResult validation = ValidateDebugName(descriptor.debug_name);
  if (!validation) {
    return validation;
  }
  if (!IsKnownMeshPrimitiveTopology(descriptor.topology) ||
      !IsKnownMeshIndexFormat(descriptor.index_format)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "mesh_format",
                                     "unknown mesh topology or index format");
  }
  if (descriptor.topology_revision == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "topology_revision",
                                     "topology revision must be nonzero");
  }
  if (!IsValid(descriptor.local_bounds)) {
    return ValidationResult::Failure(ValidationCode::INVALID_BOUNDS,
                                     "local_bounds",
                                     "mesh bounds must be finite and ordered");
  }
  if (descriptor.positions.empty() || descriptor.indices.empty()) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     descriptor.positions.empty() ? "positions"
                                                                  : "indices",
                                     "mesh requires indexed position data");
  }
  if (descriptor.positions.size() >
          (std::numeric_limits<std::uint32_t>::max)() ||
      descriptor.indices.size() > (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "mesh_count",
        "mesh vertex and index counts must fit in 32 bits");
  }
  if (descriptor.index_format == MeshIndexFormat::UINT16 &&
      descriptor.positions.size() > 65536U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "positions",
        "16-bit mesh indices cannot address more than 65536 vertices");
  }

  const std::size_t position_count = descriptor.positions.size();
  const struct {
    std::size_t size;
    const char *field;
  } optional_streams[] = {
      {descriptor.normals.size(), "normals"},
      {descriptor.tangents.size(), "tangents"},
      {descriptor.velocities.size(), "velocities"},
      {descriptor.texture_coordinates_0.size(), "texture_coordinates_0"},
      {descriptor.texture_coordinates_1.size(), "texture_coordinates_1"},
      {descriptor.colors.size(), "colors"},
  };
  for (const auto &stream : optional_streams) {
    validation =
        ValidateOptionalStreamSize(stream.size, position_count, stream.field);
    if (!validation) {
      return validation;
    }
  }
  if (!descriptor.tangents.empty() && descriptor.normals.empty()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "normals",
        "authored tangents require an authored normal stream");
  }

  for (std::size_t index = 0U; index < position_count; ++index) {
    const Float3 &position = descriptor.positions[index];
    if (!IsFinite(position)) {
      return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                       "positions",
                                       "mesh positions must be finite", index);
    }
    if (!BoundsContain(descriptor.local_bounds, position)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS, "local_bounds",
          "mesh bounds must contain every position", index);
    }
    if (!descriptor.normals.empty() &&
        !IsUnitVector(descriptor.normals[index])) {
      return ValidationResult::Failure(
          IsFinite(descriptor.normals[index])
              ? ValidationCode::VALUE_OUT_OF_RANGE
              : ValidationCode::NON_FINITE_VALUE,
          "normals", "mesh normals must be finite unit vectors", index);
    }
    if (!descriptor.tangents.empty()) {
      const Float4 &tangent = descriptor.tangents[index];
      const Float3 tangent_xyz{tangent.x, tangent.y, tangent.z};
      if (!IsFinite(tangent) || !IsUnitVector(tangent_xyz) ||
          (tangent.w != -1.0F && tangent.w != 1.0F)) {
        return ValidationResult::Failure(
            IsFinite(tangent) ? ValidationCode::VALUE_OUT_OF_RANGE
                              : ValidationCode::NON_FINITE_VALUE,
            "tangents",
            "mesh tangents require a unit direction and handedness of -1 or 1",
            index);
      }
      if (std::fabs(Dot(descriptor.normals[index], tangent_xyz)) >
          kVectorBasisTolerance) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "tangents",
            "mesh tangent must be orthogonal to its authored normal", index);
      }
    }
    if (!descriptor.velocities.empty() &&
        !IsFinite(descriptor.velocities[index])) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "velocities",
          "mesh velocities must be finite object-local meters per second",
          index);
    }
    if ((!descriptor.texture_coordinates_0.empty() &&
         !IsFinite(descriptor.texture_coordinates_0[index])) ||
        (!descriptor.texture_coordinates_1.empty() &&
         !IsFinite(descriptor.texture_coordinates_1[index]))) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "texture_coordinates",
          "mesh texture coordinates must be finite", index);
    }
    if (!descriptor.colors.empty() &&
        !IsNormalizedColor(descriptor.colors[index])) {
      return ValidationResult::Failure(
          IsFinite(descriptor.colors[index])
              ? ValidationCode::VALUE_OUT_OF_RANGE
              : ValidationCode::NON_FINITE_VALUE,
          "colors", "mesh vertex colors must be finite and in [0, 1]", index);
    }
  }

  if (descriptor.distance_lod_levels.size() >
      kMaximumMeshDistanceLodLevels) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "distance_lod_levels",
        "mesh generated-LOD count exceeds the portable limit");
  }
  if (!descriptor.distance_lod_levels.empty() &&
      (descriptor.dynamic ||
       descriptor.topology != MeshPrimitiveTopology::TRIANGLE_LIST)) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "distance_lod_levels",
        "index-only distance LODs require an immutable triangle-list mesh");
  }

  std::size_t primitive_width = 1U;
  switch (descriptor.topology) {
  case MeshPrimitiveTopology::TRIANGLE_LIST:
    primitive_width = 3U;
    break;
  case MeshPrimitiveTopology::LINE_LIST:
    primitive_width = 2U;
    break;
  case MeshPrimitiveTopology::POINT_LIST:
    primitive_width = 1U;
    break;
  }
  const auto validate_indices = [&](const std::vector<std::uint32_t> &indices,
                                    const char *field) -> ValidationResult {
    if (indices.size() >
        (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, field,
          "mesh index count must fit in 32 bits");
    }
    if (indices.size() < primitive_width ||
        indices.size() % primitive_width != 0U) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, field,
          "index count must contain complete primitives");
    }
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      const std::uint32_t vertex_index = indices[index];
      if (vertex_index >= position_count ||
          (descriptor.index_format == MeshIndexFormat::UINT16 &&
           vertex_index > 65535U)) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, field,
            "mesh index is outside the addressable vertex range", index);
      }
    }
    for (std::size_t index = 0U; index < indices.size();
         index += primitive_width) {
      if ((primitive_width == 2U && indices[index] == indices[index + 1U]) ||
          (primitive_width == 3U &&
           (indices[index] == indices[index + 1U] ||
            indices[index] == indices[index + 2U] ||
            indices[index + 1U] == indices[index + 2U]))) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, field,
            "line and triangle primitives must not repeat a vertex index",
            index);
      }
    }
    return ValidationResult::Success();
  };
  validation = validate_indices(descriptor.indices, "indices");
  if (!validation) {
    return validation;
  }
  float previous_distance = 0.0F;
  for (std::size_t level_index = 0U;
       level_index < descriptor.distance_lod_levels.size(); ++level_index) {
    const MeshDistanceLodLevelDescriptor &level =
        descriptor.distance_lod_levels[level_index];
    if (!std::isfinite(level.activation_distance_meters) ||
        level.activation_distance_meters <= previous_distance) {
      return ValidationResult::Failure(
          std::isfinite(level.activation_distance_meters)
              ? ValidationCode::NON_DETERMINISTIC_ORDER
              : ValidationCode::NON_FINITE_VALUE,
          "distance_lod_levels.activation_distance_meters",
          "mesh LOD activation distances must be finite, positive, and strictly increasing",
          level_index);
    }
    validation = validate_indices(level.indices, "distance_lod_levels.indices");
    if (!validation) {
      validation.element_index = level_index;
      return validation;
    }
    previous_distance = level.activation_distance_meters;
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateTextureResourceDescriptor(const TextureResourceDescriptor &descriptor) {
  if (descriptor.version != kTextureResourceDescriptorVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported texture descriptor version");
  }
  ValidationResult validation = ValidateDebugName(descriptor.debug_name);
  if (!validation) {
    return validation;
  }
  if (!IsKnownTextureResourceType(descriptor.type) ||
      !IsKnownTextureResourceFormat(descriptor.format) ||
      !IsKnownTextureColorSpace(descriptor.color_space)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "texture_format",
        "unknown texture type, format, or color space");
  }
  if (descriptor.width == 0U || descriptor.height == 0U ||
      descriptor.width > kMaximumTextureResourceDimension ||
      descriptor.height > kMaximumTextureResourceDimension) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "extent",
        "texture dimensions must be in [1, 65535]");
  }
  if (descriptor.array_layers == 0U ||
      descriptor.array_layers > kMaximumTextureArrayLayers) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "array_layers",
        "texture array layer count is outside the portable range");
  }
  if ((descriptor.type == TextureResourceType::TEXTURE_2D &&
       descriptor.array_layers != 1U) ||
      (descriptor.type == TextureResourceType::TEXTURE_CUBE &&
       descriptor.array_layers != 6U)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "array_layers",
        "2D textures require one layer and cube textures require six layers");
  }
  if (descriptor.type == TextureResourceType::TEXTURE_CUBE &&
      descriptor.width != descriptor.height) {
    return ValidationResult::Failure(ValidationCode::INVALID_DIMENSIONS,
                                     "extent",
                                     "cube texture faces must be square");
  }
  if (descriptor.color_space == TextureColorSpace::SRGB &&
      descriptor.format != TextureResourceFormat::RGBA8_UNORM) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "color_space",
        "sRGB transfer is supported only for RGBA8_UNORM storage");
  }
  if (descriptor.mip_levels.empty()) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                     "mip_levels",
                                     "texture requires at least one mip level");
  }
  const std::uint32_t maximum_mips =
      MaximumMipLevelCount(descriptor.width, descriptor.height);
  if (descriptor.mip_levels.size() > maximum_mips) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "mip_levels",
        "texture supplies more mip levels than its base extent permits");
  }

  const std::uint64_t bytes_per_texel =
      BytesPerTextureResourceTexel(descriptor.format);
  std::uint32_t expected_width = descriptor.width;
  std::uint32_t expected_height = descriptor.height;
  for (std::size_t index = 0U; index < descriptor.mip_levels.size(); ++index) {
    const TextureMipLevelDescriptor &mip = descriptor.mip_levels[index];
    if (mip.width != expected_width || mip.height != expected_height) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "mip_levels.extent",
          "mip dimensions must follow the complete halving sequence", index);
    }
    if (mip.bytes.empty()) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "mip_levels.bytes",
          "every supplied mip level requires a payload", index);
    }
    const std::uint64_t minimum_row_pitch =
        static_cast<std::uint64_t>(mip.width) * bytes_per_texel;
    if (mip.row_pitch_bytes < minimum_row_pitch ||
        mip.row_pitch_bytes % bytes_per_texel != 0U) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "mip_levels.row_pitch_bytes",
          "row pitch must contain whole texels and at least one complete row",
          index);
    }
    if (mip.row_pitch_bytes >
        (std::numeric_limits<std::uint64_t>::max)() / mip.height) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "mip_levels.layer_pitch_bytes",
          "row pitch times height overflows", index);
    }
    const std::uint64_t minimum_layer_pitch = mip.row_pitch_bytes * mip.height;
    if (mip.layer_pitch_bytes < minimum_layer_pitch ||
        mip.layer_pitch_bytes > (std::numeric_limits<std::uint64_t>::max)() /
                                    descriptor.array_layers) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "mip_levels.layer_pitch_bytes",
          "layer pitch is too small or total payload size overflows", index);
    }
    const std::uint64_t expected_size =
        mip.layer_pitch_bytes * descriptor.array_layers;
    if (expected_size > (std::numeric_limits<std::size_t>::max)() ||
        expected_size != mip.bytes.size()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "mip_levels.bytes",
          "mip payload must equal layer pitch times layer count", index);
    }
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateSamplerResourceDescriptor(const SamplerResourceDescriptor &descriptor) {
  if (descriptor.version != kSamplerResourceDescriptorVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported sampler descriptor version");
  }
  ValidationResult validation = ValidateDebugName(descriptor.debug_name);
  if (!validation) {
    return validation;
  }
  if (!IsKnownSamplerFilter(descriptor.minification_filter) ||
      !IsKnownSamplerFilter(descriptor.magnification_filter) ||
      !IsKnownSamplerFilter(descriptor.mip_filter) ||
      !IsKnownSamplerAddressMode(descriptor.address_u) ||
      !IsKnownSamplerAddressMode(descriptor.address_v) ||
      !IsKnownSamplerAddressMode(descriptor.address_w) ||
      !IsKnownSamplerCompareOperation(descriptor.compare_operation)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "sampler_state",
        "sampler contains an unknown filter, address, or compare mode");
  }
  if (!IsFinite(descriptor.mip_lod_bias) || !IsFinite(descriptor.minimum_lod) ||
      !IsFinite(descriptor.maximum_lod) ||
      !IsFinite(descriptor.maximum_anisotropy) ||
      !IsFinite(descriptor.border_color)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "sampler_state",
                                     "sampler numeric values must be finite");
  }
  if (descriptor.mip_lod_bias < -kMaximumSamplerLodBias ||
      descriptor.mip_lod_bias > kMaximumSamplerLodBias ||
      descriptor.minimum_lod < 0.0F ||
      descriptor.maximum_lod < descriptor.minimum_lod ||
      descriptor.maximum_lod > kMaximumSamplerLod) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lod",
        "sampler LOD values are outside the portable range");
  }
  if ((descriptor.anisotropy_enabled &&
       (descriptor.maximum_anisotropy <= 1.0F ||
        descriptor.maximum_anisotropy > kMaximumSamplerAnisotropy)) ||
      (!descriptor.anisotropy_enabled &&
       descriptor.maximum_anisotropy != 1.0F)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "maximum_anisotropy",
        "anisotropy must be 1 when disabled or in (1, 16] when enabled");
  }
  if (!descriptor.compare_enabled &&
      descriptor.compare_operation != SamplerCompareOperation::ALWAYS) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "compare_operation",
        "disabled comparison requires the canonical ALWAYS operation");
  }
  if (!IsNormalizedColor(descriptor.border_color)) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "border_color",
                                     "sampler border color must be in [0, 1]");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateDynamicMeshUpdateCompatibility(
    const MeshResourceDescriptor &mesh,
    const DynamicMeshUpdateDescriptor &update) {
  ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }

  return Detail::ValidateDynamicMeshUpdateCompatibilityFromValidatedMesh(
      ValidatedAssetCompatibilityAccess{}, mesh, update);
}

ValidationResult
Detail::ValidateDynamicMeshUpdateCompatibilityFromValidatedMesh(
    const ValidatedAssetCompatibilityAccess &,
    const MeshResourceDescriptor &mesh,
    const DynamicMeshUpdateDescriptor &update) {
  if (!mesh.dynamic) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "mesh.dynamic",
        "dynamic updates require a mesh created with dynamic storage");
  }
  if (mesh.topology_revision != update.topology_revision) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "update.topology_revision",
        "dynamic update topology revision differs from the live mesh");
  }
  if (update.positions.empty()) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "update.positions",
        "dynamic update requires a nonempty position range");
  }
  if (update.positions.size() != mesh.positions.size()) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "update.positions",
        "version 1 update must contain every live mesh position");
  }
  if (update.normals.empty() != mesh.normals.empty() ||
      update.tangents.empty() != mesh.tangents.empty() ||
      update.velocities.empty() != mesh.velocities.empty()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "update.vertex_streams",
        "full update must reproduce every stream allocated at mesh creation");
  }
  if (!update.has_updated_bounds || !IsValid(update.updated_local_bounds)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_BOUNDS, "update.updated_local_bounds",
        "full update requires finite ordered authored bounds");
  }
  for (std::size_t index = 0U; index < update.positions.size(); ++index) {
    if (!BoundsContain(update.updated_local_bounds, update.positions[index])) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS, "update.updated_local_bounds",
          "updated bounds must contain every full-state position", index);
    }
    if (!update.tangents.empty()) {
      const Float4 &tangent = update.tangents[index];
      if (std::fabs(Dot(update.normals[index],
                        Float3{tangent.x, tangent.y, tangent.z})) >
          kVectorBasisTolerance) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "update.tangents",
            "updated tangent must be orthogonal to its updated normal", index);
      }
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateMeshInstanceCompatibility(
    const MeshResourceDescriptor &mesh, const MeshInstanceDescriptor &instance,
    const DynamicMeshUpdateDescriptor *deformation_update) {
  ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }

  return Detail::ValidateMeshInstanceCompatibilityFromValidatedMesh(
      ValidatedAssetCompatibilityAccess{}, mesh, instance, deformation_update);
}

ValidationResult Detail::ValidateMeshInstanceCompatibilityFromValidatedMesh(
    const ValidatedAssetCompatibilityAccess &access,
    const MeshResourceDescriptor &mesh, const MeshInstanceDescriptor &instance,
    const DynamicMeshUpdateDescriptor *deformation_update) {
  if (instance.topology_revision != mesh.topology_revision) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "instance.topology_revision",
        "instance topology revision differs from the live mesh");
  }
  if (instance.deformation_revision == 1U) {
    if (deformation_update != nullptr) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "deformation_update",
          "base deformation revision cannot carry a dynamic update");
    }
    if (!EqualBounds(instance.local_bounds, mesh.local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS, "instance.local_bounds",
          "base instance bounds must exactly equal live mesh bounds");
    }
    return ValidationResult::Success();
  }
  if (deformation_update == nullptr) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "deformation_update",
        "non-base instance revision requires its full deformation update");
  }
  ValidationResult validation =
      ValidateDynamicMeshUpdateCompatibilityFromValidatedMesh(
          access, mesh, *deformation_update);
  if (!validation) {
    return validation;
  }
  if (deformation_update->instance_id != instance.instance_id ||
      deformation_update->mesh != instance.mesh ||
      deformation_update->topology_revision != instance.topology_revision ||
      deformation_update->deformation_revision !=
          instance.deformation_revision) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "deformation_update.identity",
        "deformation update does not identify this exact instance revision");
  }
  if (!EqualBounds(instance.local_bounds,
                   deformation_update->updated_local_bounds)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_BOUNDS, "instance.local_bounds",
        "deformed instance and full update bounds must exactly match");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateEnvironmentTextureCompatibility(
    const TextureResourceDescriptor &texture,
    const SamplerResourceDescriptor &sampler) {
  ValidationResult validation = ValidateTextureResourceDescriptor(texture);
  if (!validation) {
    return validation;
  }
  validation = ValidateSamplerResourceDescriptor(sampler);
  if (!validation) {
    return validation;
  }

  return Detail::ValidateEnvironmentTextureCompatibilityFromValidatedAssets(
      ValidatedAssetCompatibilityAccess{}, texture, sampler);
}

ValidationResult
Detail::ValidateEnvironmentTextureCompatibilityFromValidatedAssets(
    const ValidatedAssetCompatibilityAccess &,
    const TextureResourceDescriptor &texture,
    const SamplerResourceDescriptor &sampler) {
  const bool floating_rgba =
      texture.format == TextureResourceFormat::RGBA16_FLOAT ||
      texture.format == TextureResourceFormat::RGBA32_FLOAT;
  if (texture.type != TextureResourceType::TEXTURE_2D ||
      texture.array_layers != 1U || !floating_rgba ||
      texture.color_space != TextureColorSpace::LINEAR) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "environment.texture",
        "environment requires a linear floating-point RGBA 2D texture");
  }
  if (sampler.compare_enabled ||
      sampler.minification_filter != SamplerFilter::LINEAR ||
      sampler.magnification_filter != SamplerFilter::LINEAR ||
      sampler.mip_filter != SamplerFilter::LINEAR ||
      sampler.address_u != SamplerAddressMode::REPEAT ||
      sampler.address_v != SamplerAddressMode::CLAMP_TO_EDGE) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "environment.sampler",
        "environment sampler must be linear, U-repeat, V-clamp, non-compare");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
