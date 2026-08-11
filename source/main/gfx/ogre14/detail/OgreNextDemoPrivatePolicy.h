/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Pure policy used only by the disposable OgreNext product demo.

#pragma once

#include "gfx/render/RenderResourceDescriptors.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace RoR::Gfx::Detail {

/// Canonicalized result of observing the exact OGRE terrain TUS0. Native
/// pointer/layout identity is retained in exact_native_state; the booleans
/// admit only the one sampling policy the demo can reproduce honestly.
struct OgreNextDemoSamplingObservation final {
  bool ordinary_texture = true;
  bool uv0_identity = true;
  bool sampler_identity = true;
  bool gamma_disabled = true;
  bool fog_disabled = true;
  std::string exact_native_state;
};

[[nodiscard]] Render::ValidationResult ValidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &observation);

[[nodiscard]] Render::ValidationResult RevalidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &before,
    const OgreNextDemoSamplingObservation &after);

/// Validates one freshly read tight RGBA8 base level, forces only its alpha
/// bytes opaque, then deterministically generates every remaining mip through
/// 1x1. Generated levels use an encoded/display-domain 2x2 integer box filter;
/// RGB bytes in the native base level are never rewritten. Reading native
/// nonzero mips is forbidden because pinned OGRE Metal aliases them to mip 0.
[[nodiscard]] Render::ValidationResult CompleteOgreNextDemoOpaqueMipChain(
    Render::TextureResourceDescriptor &texture);

[[nodiscard]] Render::ValidationResult DeriveOgreNextDemoSourceId(
    std::string_view domain, std::string_view exact_key,
    std::uint64_t &source_id);

/// Canonicalizes an untextured demo-matte mesh to the exact RT4 vertex
/// layout. Authored UV0 is retained, absent UV0 becomes deterministic zero,
/// finite nonzero normals are normalized, unusable or absent normals become
/// deterministic +Y, tangent directions are rebuilt, and streams with no
/// matte consumer are removed. The input is unchanged on failure.
[[nodiscard]] Render::ValidationResult NormalizeOgreNextDemoMatteMesh(
    Render::MeshResourceDescriptor &mesh);

/// Sanitizes the complete normal stream and rebuilds the same matte-only
/// tangent basis for a joined dynamic update. Finite nonzero directions are
/// normalized; absent, zero, or non-finite directions become +Y. Both output
/// streams are unchanged on structural failure.
[[nodiscard]] Render::ValidationResult BuildOgreNextDemoMatteTangents(
    std::size_t vertex_count, std::vector<Render::Float3> &normals,
    std::vector<Render::Float4> &tangents);

/// Builds the smallest camera-centered sphere containing the normalized
/// OgreNext demo far frustum. Source offsets and vertical FOV are retained;
/// target_aspect supplies the child surface aspect.
[[nodiscard]] Render::ValidationResult BuildOgreNextDemoStaticCaptureRadius(
    float left, float right, float top, float bottom, float near_plane,
    float far_plane, float target_aspect, float &radius_meters);

/// Classifies a finite world AABB by closest-point distance to the enclosing
/// demo frustum sphere. The output remains unchanged on failure.
[[nodiscard]] Render::ValidationResult ClassifyOgreNextDemoStaticBounds(
    const Render::Bounds3 &world_bounds,
    const Render::Float3 &camera_position, float radius_meters,
    bool &within_capture_radius);

/// Bidirectional collision audit. Transactions copy this private registry,
/// mutate the candidate, and replace the committed owner only on commit.
class OgreNextDemoIdentityRegistry final {
public:
  [[nodiscard]] Render::ValidationResult Register(std::string exact_key,
                                                  std::uint64_t source_id);
  [[nodiscard]] bool Contains(std::string_view exact_key,
                              std::uint64_t source_id) const;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::map<std::uint64_t, std::string> keys_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> ids_by_key_;
};

[[nodiscard]] bool OgreNextDemoRequiresMatte(
    std::size_t texture_unit_count, bool has_authored_program) noexcept;
[[nodiscard]] bool OgreNextDemoDropsDynamicBlendColors(
    bool has_dynamic_texture_blend) noexcept;
[[nodiscard]] bool OgreNextDemoOmitsInvisibleCab(
    std::string_view exact_material_name, float diffuse_alpha,
    bool depth_write_enabled) noexcept;
[[nodiscard]] bool OgreNextDemoOmitsNonUniformSpeedBump(
    std::string_view exact_mesh_name,
    const Render::Float3 &derived_scale) noexcept;

} // namespace RoR::Gfx::Detail
