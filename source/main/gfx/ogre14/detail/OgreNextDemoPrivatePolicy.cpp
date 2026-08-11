/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoPrivatePolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace RoR::Gfx::Detail {
namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

Render::ValidationResult Failure(Render::ValidationCode code,
                                 const char *field, const char *detail,
                                 std::size_t index =
                                     Render::ValidationResult::kNoElement) {
  return Render::ValidationResult::Failure(code, field, detail, index);
}

std::uint32_t CompleteMipCount(std::uint32_t width,
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

Render::ValidationResult ValidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &observation) {
  if (!observation.ordinary_texture) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.ordinary",
                   "TUS0 must be a named, single-frame, loaded 2D texture without UAV access");
  }
  if (!observation.uv0_identity) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.uv",
                   "TUS0 must use UV0 with no generation, effects, or transform");
  }
  if (!observation.sampler_identity) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.sampler",
                   "TUS0 must use clamp U/V/W, linear min/mag, nearest mip, and no comparison");
  }
  if (!observation.gamma_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.gamma",
                   "display-domain filtering requires native hardware gamma decode to remain disabled");
  }
  if (!observation.fog_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.fog",
                   "the disposable opaque terrain lowering cannot preserve OGRE scene fog");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult RevalidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &before,
    const OgreNextDemoSamplingObservation &after) {
  Render::ValidationResult validation =
      ValidateOgreNextDemoSampling(before);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgreNextDemoSampling(after);
  if (!validation) {
    return validation;
  }
  if (before.exact_native_state.empty() ||
      after.exact_native_state.empty() ||
      before.exact_native_state != after.exact_native_state) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.readback.revalidation",
                   "terrain, TUS0, sampler, texture, or mip state mutated during readback");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult CompleteOgreNextDemoOpaqueMipChain(
    Render::TextureResourceDescriptor &texture) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != Render::TextureColorSpace::SRGB ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.size() != 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.full_mip_chain",
                   "opaque lowering requires exactly one fresh SRGB RGBA8 2D base level");
  }

  const Render::TextureMipLevelDescriptor &base = texture.mip_levels.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(texture.width) * 4U;
  if (texture.height != 0U &&
      row_bytes > (std::numeric_limits<std::uint64_t>::max)() /
                      texture.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "RGBA8 base-level byte count overflows", 0U);
  }
  const std::uint64_t layer_bytes = row_bytes * texture.height;
  if (layer_bytes > static_cast<std::uint64_t>(
                        (std::numeric_limits<std::size_t>::max)()) ||
      base.width != texture.width || base.height != texture.height ||
      base.row_pitch_bytes != row_bytes ||
      base.layer_pitch_bytes != layer_bytes ||
      base.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "opaque lowering requires an exact tight RGBA8 base layout",
                   0U);
  }

  // Validation above completes before the first write, so malformed input is
  // transactionally unchanged. Only the fourth byte of a native base texel is
  // touched; its RGB triplet remains byte-identical to the fresh readback.
  for (std::size_t alpha = 3U;
       alpha < texture.mip_levels.front().bytes.size(); alpha += 4U) {
    texture.mip_levels.front().bytes[alpha] = 255U;
  }

  while (texture.mip_levels.size() <
         CompleteMipCount(texture.width, texture.height)) {
    const Render::TextureMipLevelDescriptor &source =
        texture.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.terrain.texture.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t sum =
              static_cast<std::uint32_t>(source.bytes[offsets[0U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[1U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[2U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[3U] + channel]);
          // Round to nearest integer with a deterministic half-up rule. This
          // operates on encoded bytes because the material contract filters in
          // display space and decodes only after sampling.
          destination.bytes[output + channel] =
              static_cast<std::uint8_t>((sum + 2U) / 4U);
        }
        destination.bytes[output + 3U] = 255U;
      }
    }
    texture.mip_levels.push_back(std::move(destination));
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult DeriveOgreNextDemoSourceId(
    std::string_view domain, std::string_view exact_key,
    std::uint64_t &source_id) {
  if (domain.empty() || exact_key.empty()) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "source ID domain and exact identity must not be empty");
  }
  std::uint64_t hash = kFnv1a64OffsetBasis;
  const auto append = [&hash](std::string_view bytes) {
    for (const char byte : bytes) {
      hash ^= static_cast<std::uint8_t>(
          static_cast<unsigned char>(byte));
      hash *= kFnv1a64Prime;
    }
  };
  append(domain);
  const char separator = '\0';
  append(std::string_view(&separator, 1U));
  append(exact_key);
  if (hash == 0U) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "domain-separated identity hashed to reserved zero");
  }
  source_id = hash;
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoMatteTangents(
    std::size_t vertex_count, std::vector<Render::Float3> &normals,
    std::vector<Render::Float4> &tangents) {
  if (vertex_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal sanitization requires at least one vertex");
  }
  if (!normals.empty() && normals.size() != vertex_count) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal stream must be absent or complete");
  }

  constexpr Render::Float3 kFallbackNormal{0.0F, 1.0F, 0.0F};
  std::vector<Render::Float3> candidate_normals = normals;
  if (candidate_normals.empty()) {
    candidate_normals.assign(vertex_count, kFallbackNormal);
  }
  std::vector<Render::Float4> candidate_tangents;
  candidate_tangents.reserve(vertex_count);
  for (std::size_t index = 0U; index < vertex_count; ++index) {
    Render::Float3 &normal = candidate_normals[index];
    const float normal_length_squared = normal.x * normal.x +
                                        normal.y * normal.y +
                                        normal.z * normal.z;
    if (std::isfinite(normal.x) && std::isfinite(normal.y) &&
        std::isfinite(normal.z) && std::isfinite(normal_length_squared) &&
        normal_length_squared > 0.0F) {
      const float inverse_length = 1.0F / std::sqrt(normal_length_squared);
      normal = {normal.x * inverse_length, normal.y * inverse_length,
                normal.z * inverse_length};
      const float sanitized_length_squared = normal.x * normal.x +
                                             normal.y * normal.y +
                                             normal.z * normal.z;
      if (!std::isfinite(sanitized_length_squared) ||
          std::fabs(sanitized_length_squared - 1.0F) > 1.0e-3F) {
        normal = kFallbackNormal;
      }
    } else {
      normal = kFallbackNormal;
    }
    // Cross the normal with the least-parallel fixed axis. The tangent has no
    // material-space consumer in the matte path; it only provides the exact,
    // deterministic RT4 vertex layout while staying orthogonal as a FlexBody
    // normal deforms from frame to frame.
    const Render::Float3 axis = std::fabs(normal.z) < 0.875F
                                    ? Render::Float3{0.0F, 0.0F, 1.0F}
                                    : Render::Float3{0.0F, 1.0F, 0.0F};
    const Render::Float3 crossed{
        axis.y * normal.z - axis.z * normal.y,
        axis.z * normal.x - axis.x * normal.z,
        axis.x * normal.y - axis.y * normal.x};
    const float length_squared = crossed.x * crossed.x +
                                 crossed.y * crossed.y +
                                 crossed.z * crossed.z;
    if (!std::isfinite(length_squared) || length_squared <= 0.0F) {
      return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                     "ogre_next_demo.matte_mesh.tangents",
                     "an authored normal cannot produce a finite matte tangent",
                     index);
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    candidate_tangents.push_back(
        {crossed.x * inverse_length, crossed.y * inverse_length,
         crossed.z * inverse_length, 1.0F});
  }
  normals = std::move(candidate_normals);
  tangents = std::move(candidate_tangents);
  return Render::ValidationResult::Success();
}

Render::ValidationResult NormalizeOgreNextDemoMatteMesh(
    Render::MeshResourceDescriptor &mesh) {
  Render::MeshResourceDescriptor candidate = mesh;
  if (candidate.texture_coordinates_0.empty()) {
    candidate.texture_coordinates_0.assign(candidate.positions.size(), {});
  }
  candidate.texture_coordinates_1.clear();
  candidate.colors.clear();
  candidate.velocities.clear();
  Render::ValidationResult validation = BuildOgreNextDemoMatteTangents(
      candidate.positions.size(), candidate.normals, candidate.tangents);
  if (!validation) {
    return validation;
  }

  validation = Render::ValidateMeshResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.matte_mesh." + validation.field;
    return validation;
  }
  mesh = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoStaticCaptureRadius(
    float left, float right, float top, float bottom, float near_plane,
    float far_plane, float target_aspect, float &radius_meters) {
  if (!std::isfinite(left) || !std::isfinite(right) ||
      !std::isfinite(top) || !std::isfinite(bottom) ||
      !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
      !std::isfinite(target_aspect) || !(left < right) || !(bottom < top) ||
      !(near_plane > 0.0F) || !(far_plane > near_plane) ||
      !(target_aspect > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.frustum",
                   "static capture requires finite ordered perspective extents, clip distances, and target aspect");
  }

  const double horizontal_span =
      static_cast<double>(right) - static_cast<double>(left);
  const double vertical_span =
      static_cast<double>(top) - static_cast<double>(bottom);
  const double horizontal_offset = std::fabs(
      (static_cast<double>(right) + static_cast<double>(left)) /
      horizontal_span);
  const double vertical_offset = std::fabs(
      (static_cast<double>(top) + static_cast<double>(bottom)) /
      vertical_span);
  const double half_vertical_slope =
      vertical_span / (2.0 * static_cast<double>(near_plane));
  const double half_horizontal_slope =
      half_vertical_slope * static_cast<double>(target_aspect);
  const double maximum_horizontal_slope =
      half_horizontal_slope * (1.0 + horizontal_offset);
  const double maximum_vertical_slope =
      half_vertical_slope * (1.0 + vertical_offset);
  const double candidate = static_cast<double>(far_plane) *
      std::sqrt(1.0 + maximum_horizontal_slope * maximum_horizontal_slope +
                maximum_vertical_slope * maximum_vertical_slope);
  if (!std::isfinite(candidate) || !(candidate > 0.0) ||
      candidate > static_cast<double>((std::numeric_limits<float>::max)())) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.radius",
                   "normalized far-frustum enclosing radius is not representable");
  }
  const float conservative = std::nextafter(
      static_cast<float>(candidate),
      (std::numeric_limits<float>::infinity)());
  if (!std::isfinite(conservative) || !(conservative > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.radius",
                   "normalized far-frustum enclosing radius overflowed conservative rounding");
  }
  radius_meters = conservative;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ClassifyOgreNextDemoStaticBounds(
    const Render::Bounds3 &world_bounds,
    const Render::Float3 &camera_position, float radius_meters,
    bool &within_capture_radius) {
  const auto finite = [](float value) { return std::isfinite(value); };
  if (!finite(world_bounds.minimum.x) ||
      !finite(world_bounds.minimum.y) ||
      !finite(world_bounds.minimum.z) ||
      !finite(world_bounds.maximum.x) ||
      !finite(world_bounds.maximum.y) ||
      !finite(world_bounds.maximum.z) ||
      !finite(camera_position.x) || !finite(camera_position.y) ||
      !finite(camera_position.z) || !finite(radius_meters) ||
      world_bounds.minimum.x > world_bounds.maximum.x ||
      world_bounds.minimum.y > world_bounds.maximum.y ||
      world_bounds.minimum.z > world_bounds.maximum.z ||
      !(radius_meters > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.bounds",
                   "static capture requires a finite ordered world AABB, camera, and positive radius");
  }

  const auto separation = [](double value, double minimum,
                             double maximum) {
    if (value < minimum) {
      return minimum - value;
    }
    if (value > maximum) {
      return value - maximum;
    }
    return 0.0;
  };
  const double dx = separation(camera_position.x, world_bounds.minimum.x,
                               world_bounds.maximum.x);
  const double dy = separation(camera_position.y, world_bounds.minimum.y,
                               world_bounds.maximum.y);
  const double dz = separation(camera_position.z, world_bounds.minimum.z,
                               world_bounds.maximum.z);
  const double radius = radius_meters;
  within_capture_radius =
      dx * dx + dy * dy + dz * dz <= radius * radius;
  return Render::ValidationResult::Success();
}

Render::ValidationResult OgreNextDemoIdentityRegistry::Register(
    std::string exact_key, std::uint64_t source_id) {
  if (exact_key.empty() || source_id == 0U) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "registered source ID and exact identity must be nonzero and nonempty");
  }
  const auto id_match = keys_by_id_.find(source_id);
  if (id_match != keys_by_id_.end() && id_match->second != exact_key) {
    return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "distinct domain-separated identities collided");
  }
  const auto key_match = ids_by_key_.find(exact_key);
  if (key_match != ids_by_key_.end() && key_match->second != source_id) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.source_id",
                   "one exact identity changed its source ID");
  }
  keys_by_id_[source_id] = exact_key;
  ids_by_key_[std::move(exact_key)] = source_id;
  return Render::ValidationResult::Success();
}

bool OgreNextDemoIdentityRegistry::Contains(std::string_view exact_key,
                                            std::uint64_t source_id) const {
  const auto match = keys_by_id_.find(source_id);
  return match != keys_by_id_.end() && match->second == exact_key;
}

std::size_t OgreNextDemoIdentityRegistry::size() const noexcept {
  return keys_by_id_.size();
}

bool OgreNextDemoRequiresMatte(std::size_t texture_unit_count,
                              bool has_authored_program) noexcept {
  return texture_unit_count != 0U || has_authored_program;
}

bool OgreNextDemoDropsDynamicBlendColors(
    bool has_dynamic_texture_blend) noexcept {
  return has_dynamic_texture_blend;
}

bool OgreNextDemoOmitsInvisibleCab(std::string_view exact_material_name,
                                   float diffuse_alpha,
                                   bool depth_write_enabled) noexcept {
  return exact_material_name == "invisible" && diffuse_alpha == 0.0F &&
         !depth_write_enabled;
}

bool OgreNextDemoOmitsNonUniformSpeedBump(
    std::string_view exact_mesh_name,
    const Render::Float3 &derived_scale) noexcept {
  return exact_mesh_name == "topeQr.mesh" && derived_scale.x == 1.0F &&
         derived_scale.y == 0.5F && derived_scale.z == 0.5F;
}

} // namespace RoR::Gfx::Detail
