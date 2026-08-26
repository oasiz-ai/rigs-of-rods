/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral mesh, texture, and sampler resource descriptions.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR::Render {

struct DynamicMeshUpdateDescriptor;
struct MeshInstanceDescriptor;

constexpr std::uint32_t kMeshResourceDescriptorVersion = 2U;
/// Version 2 adds the block-compressed storage formats (BC4/BC5/BC7). A
/// version-1 reader must refuse a version-2 descriptor outright rather than
/// reinterpret its format byte, because the pitch fields change meaning from
/// bytes-per-texel-row to bytes-per-block-row.
constexpr std::uint32_t kTextureResourceDescriptorVersion = 2U;
constexpr std::uint32_t kSamplerResourceDescriptorVersion = 1U;
constexpr std::size_t kMaximumResourceDebugNameBytes = 255U;
constexpr std::uint32_t kMaximumTextureResourceDimension = 65535U;
constexpr std::uint32_t kMaximumTextureArrayLayers = 2048U;
constexpr float kMaximumSamplerAnisotropy = 16.0F;
constexpr float kMaximumSamplerLod = 32.0F;
constexpr float kMaximumSamplerLodBias = 16.0F;
constexpr std::size_t kMaximumMeshDistanceLodLevels = 15U;

enum class MeshPrimitiveTopology : std::uint8_t {
  TRIANGLE_LIST = 0,
  LINE_LIST = 1,
  POINT_LIST = 2,
};

enum class MeshIndexFormat : std::uint8_t {
  UINT16 = 0,
  UINT32 = 1,
};

/// One additional generated index-only LOD. The activation distance is an
/// object-to-camera distance in meters, not an OGRE-version-specific squared
/// or projected LOD value. Levels are ordered strictly near-to-far. Vertex
/// streams remain shared with the base level, so authored replacement-mesh
/// LODs require a separate future contract.
struct MeshDistanceLodLevelDescriptor {
  float activation_distance_meters = 0.0F;
  std::vector<std::uint32_t> indices;
};

/// Portable indexed mesh payload. All vertex streams use object-local values.
/// UV (0, 0) is the upper-left texel; +U moves right and +V moves down.
/// Tangent xyz is unit length and bitangent is defined exactly as
/// `tangent.w * cross(normal, tangent.xyz)` in canonical right-handed space.
/// Authored tangent.xyz must satisfy `abs(dot(normal, tangent.xyz)) <= 1e-3`;
/// adapters do not choose their own orthogonalization policy.
/// Triangle front faces use counter-clockwise object-space winding when viewed
/// from the front; adapters account for API conventions and mirrored world
/// transforms before culling.
/// Optional streams are empty or contain exactly one element per position.
/// Missing vertex color is exactly linear white (1,1,1,1). Normals, tangents,
/// and UVs are never synthesized: material/mesh compatibility rejects a pair
/// that needs a missing authored stream.
/// Indices are transported as uint32 values; UINT16 requires every vertex and
/// index to remain addressable by the narrower backend representation.
struct MeshResourceDescriptor {
  std::uint32_t version = kMeshResourceDescriptorVersion;
  std::string debug_name;
  MeshPrimitiveTopology topology = MeshPrimitiveTopology::TRIANGLE_LIST;
  MeshIndexFormat index_format = MeshIndexFormat::UINT32;
  std::uint64_t topology_revision = 1U;
  bool dynamic = false;
  Bounds3 local_bounds;
  std::vector<Float3> positions;
  std::vector<Float3> normals;
  std::vector<Float4> tangents;
  /// Optional object-local meters-per-second stream reserved at creation for
  /// deformation/motion updates.
  std::vector<Float3> velocities;
  std::vector<Float2> texture_coordinates_0;
  std::vector<Float2> texture_coordinates_1;
  std::vector<Float4> colors;
  std::vector<std::uint32_t> indices;
  std::vector<MeshDistanceLodLevelDescriptor> distance_lod_levels;
};

enum class TextureResourceType : std::uint8_t {
  TEXTURE_2D = 0,
  TEXTURE_2D_ARRAY = 1,
  TEXTURE_CUBE = 2,
};

/// Uncompressed portable texel formats. Color-space transfer is described
/// separately so storage width and color interpretation cannot be conflated.
enum class TextureResourceFormat : std::uint8_t {
  R8_UNORM = 0,
  RG8_UNORM = 1,
  RGBA8_UNORM = 2,
  R16_FLOAT = 3,
  RG16_FLOAT = 4,
  RGBA16_FLOAT = 5,
  R32_FLOAT = 6,
  RGBA32_FLOAT = 7,
  /// Block-compressed storage. Every BC format packs a fixed 4x4 texel block
  /// into a fixed number of bytes and is uploaded to the GPU still compressed:
  /// there is no CPU decode step and no channel extraction, because a single
  /// channel cannot be read out of a BC block without decoding it.
  ///
  /// BC4 stores one unsigned channel in 8 bytes per block (0.5 bytes/texel).
  /// BC5 stores two unsigned channels in 16 bytes per block (1 byte/texel).
  /// BC7 stores four channels in 16 bytes per block (1 byte/texel) and is the
  /// only BC format here that may carry the sRGB transfer function.
  BC4_UNORM = 8,
  BC5_UNORM = 9,
  BC7_UNORM = 10,
};

/// Storage geometry of one texture format. Uncompressed formats report a 1x1
/// block whose size is the texel size, so every pitch rule below is expressed
/// once, in blocks, and holds for both families.
struct TextureResourceFormatBlockExtent {
  std::uint32_t block_width = 0U;
  std::uint32_t block_height = 0U;
  std::uint32_t block_bytes = 0U;
};

enum class TextureColorSpace : std::uint8_t {
  LINEAR = 0,
  SRGB = 1,
};

/// One mip payload containing every array or cube layer. The first byte row is
/// V=0 (the top row), rows advance toward +V, and texels advance toward +U.
/// Rows and layers may be padded; byte payload size must equal
/// layer_pitch_bytes * layer count.
///
/// `width`/`height` are always in texels. `row_pitch_bytes` is the stride of
/// one BLOCK row, and `layer_pitch_bytes` must cover ceil(height/block_height)
/// such rows. For the uncompressed formats the block is 1x1, so a block row is
/// a texel row and both fields keep their original meaning.
struct TextureMipLevelDescriptor {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t row_pitch_bytes = 0U;
  std::uint64_t layer_pitch_bytes = 0U;
  std::vector<std::uint8_t> bytes;
};

struct TextureResourceDescriptor {
  std::uint32_t version = kTextureResourceDescriptorVersion;
  std::string debug_name;
  TextureResourceType type = TextureResourceType::TEXTURE_2D;
  TextureResourceFormat format = TextureResourceFormat::RGBA8_UNORM;
  TextureColorSpace color_space = TextureColorSpace::LINEAR;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t array_layers = 1U;
  /// A nonempty, contiguous prefix of the full base-to-1x1 mip chain.
  /// Cube layers are ordered +X, -X, +Y, -Y, +Z, -Z. Increasing U/V points:
  /// +X (-Z/-Y), -X (+Z/-Y), +Y (+X/+Z), -Y (+X/-Z), +Z (+X/-Y),
  /// -Z (-X/-Y), where each pair is (right/down) in world-axis notation.
  std::vector<TextureMipLevelDescriptor> mip_levels;
};

enum class SamplerFilter : std::uint8_t {
  NEAREST = 0,
  LINEAR = 1,
};

enum class SamplerAddressMode : std::uint8_t {
  REPEAT = 0,
  MIRRORED_REPEAT = 1,
  CLAMP_TO_EDGE = 2,
  CLAMP_TO_BORDER = 3,
};

enum class SamplerCompareOperation : std::uint8_t {
  NEVER = 0,
  LESS = 1,
  EQUAL = 2,
  LESS_EQUAL = 3,
  GREATER = 4,
  NOT_EQUAL = 5,
  GREATER_EQUAL = 6,
  ALWAYS = 7,
};

struct SamplerResourceDescriptor {
  std::uint32_t version = kSamplerResourceDescriptorVersion;
  std::string debug_name;
  SamplerFilter minification_filter = SamplerFilter::LINEAR;
  SamplerFilter magnification_filter = SamplerFilter::LINEAR;
  SamplerFilter mip_filter = SamplerFilter::LINEAR;
  SamplerAddressMode address_u = SamplerAddressMode::REPEAT;
  SamplerAddressMode address_v = SamplerAddressMode::REPEAT;
  SamplerAddressMode address_w = SamplerAddressMode::REPEAT;
  float mip_lod_bias = 0.0F;
  float minimum_lod = 0.0F;
  float maximum_lod = kMaximumSamplerLod;
  bool anisotropy_enabled = false;
  float maximum_anisotropy = 1.0F;
  bool compare_enabled = false;
  SamplerCompareOperation compare_operation = SamplerCompareOperation::ALWAYS;
  Float4 border_color{};
};

[[nodiscard]] bool
IsKnownMeshPrimitiveTopology(MeshPrimitiveTopology topology) noexcept;
[[nodiscard]] bool IsKnownMeshIndexFormat(MeshIndexFormat format) noexcept;
[[nodiscard]] bool
IsKnownTextureResourceType(TextureResourceType type) noexcept;
[[nodiscard]] bool
IsKnownTextureResourceFormat(TextureResourceFormat format) noexcept;
[[nodiscard]] bool
IsKnownTextureColorSpace(TextureColorSpace color_space) noexcept;
/// Bytes occupied by one texel. Returns zero for every block-compressed
/// format, which has no per-texel byte count; callers that must work for both
/// families use TextureResourceFormatBlockLayout instead.
[[nodiscard]] std::uint32_t
BytesPerTextureResourceTexel(TextureResourceFormat format) noexcept;
/// Block geometry for any known format, or a zeroed extent for an unknown one.
[[nodiscard]] TextureResourceFormatBlockExtent
TextureResourceFormatBlockLayout(TextureResourceFormat format) noexcept;
[[nodiscard]] bool
IsBlockCompressedTextureResourceFormat(TextureResourceFormat format) noexcept;
/// Minimum legal row pitch: one complete row of blocks covering `width`.
[[nodiscard]] std::uint64_t
MinimumTextureRowPitchBytes(TextureResourceFormat format,
                            std::uint32_t width) noexcept;
/// Number of block rows needed to cover `height`.
[[nodiscard]] std::uint32_t
TextureBlockRowCount(TextureResourceFormat format,
                     std::uint32_t height) noexcept;
[[nodiscard]] bool IsKnownSamplerFilter(SamplerFilter filter) noexcept;
[[nodiscard]] bool
IsKnownSamplerAddressMode(SamplerAddressMode address_mode) noexcept;
[[nodiscard]] bool
IsKnownSamplerCompareOperation(SamplerCompareOperation operation) noexcept;

[[nodiscard]] ValidationResult
ValidateMeshResourceDescriptor(const MeshResourceDescriptor &descriptor);
[[nodiscard]] ValidationResult
ValidateTextureResourceDescriptor(const TextureResourceDescriptor &descriptor);
[[nodiscard]] ValidationResult
ValidateSamplerResourceDescriptor(const SamplerResourceDescriptor &descriptor);
/// Validates one already-structural snapshot update against its live base mesh
/// allocation and authored streams before any backend upload occurs.
[[nodiscard]] ValidationResult ValidateDynamicMeshUpdateCompatibility(
    const MeshResourceDescriptor &mesh,
    const DynamicMeshUpdateDescriptor &update);
/// Resolves the one canonical instance-bounds source against the live base mesh
/// or its self-contained deformation update.
[[nodiscard]] ValidationResult ValidateMeshInstanceCompatibility(
    const MeshResourceDescriptor &mesh, const MeshInstanceDescriptor &instance,
    const DynamicMeshUpdateDescriptor *deformation_update);
/// Canonical equirectangular environment resources: a linear floating-point
/// RGBA 2D texture and a non-comparison linear U-repeat/V-clamp sampler.
[[nodiscard]] ValidationResult ValidateEnvironmentTextureCompatibility(
    const TextureResourceDescriptor &texture,
    const SamplerResourceDescriptor &sampler);

} // namespace RoR::Render
