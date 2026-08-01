/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Immutable renderer-neutral scene snapshot contract.

#pragma once

#include "RenderAssetId.h"
#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kSceneSnapshotVersion = 3U;
constexpr std::uint32_t kSceneLightingHashVersion = 1U;

class RenderAssetRegistry;

enum class LightType : std::uint8_t {
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2,
};

enum LightShadowFlag : std::uint32_t {
  LIGHT_SHADOW_STATIC_GEOMETRY = 1U << 0U,
  LIGHT_SHADOW_DYNAMIC_GEOMETRY = 1U << 1U,
  LIGHT_SHADOW_DEFAULT_FLAGS = LIGHT_SHADOW_STATIC_GEOMETRY |
                               LIGHT_SHADOW_DYNAMIC_GEOMETRY,
};

enum class ParticleEffect : std::uint8_t {
  TIRE_SMOKE = 0,
  DUST = 1,
  SPARKS = 2,
  WATER_SPRAY = 3,
  STEAM = 4,
  FIRE = 5,
  DEBRIS = 6,
};

enum MeshInstanceFlag : std::uint32_t {
  MESH_INSTANCE_CASTS_SHADOW = 1U << 0U,
  MESH_INSTANCE_RECEIVES_SHADOW = 1U << 1U,
  MESH_INSTANCE_VISIBLE_IN_REFLECTIONS = 1U << 2U,
  MESH_INSTANCE_DEFAULT_FLAGS = MESH_INSTANCE_CASTS_SHADOW |
                                MESH_INSTANCE_RECEIVES_SHADOW |
                                MESH_INSTANCE_VISIBLE_IN_REFLECTIONS,
};

/// Renderer-independent analytic background used in addition to an optional
/// environment texture. Above the horizon, radiance is the linear blend from
/// horizon_radiance at direction.y=0 to zenith_radiance at direction.y=1;
/// below it, ground_radiance is constant. The sun disk adds
/// sun_disk_radiance within sun_angular_radius_radians of the directional light
/// named by sun_light_id. All radiance fields use linear RGB W/(m^2 sr).
struct AnalyticSkyDescriptor {
  bool enabled = false;
  std::uint64_t sun_light_id = 0U;
  Float3 zenith_radiance{};
  Float3 horizon_radiance{};
  Float3 ground_radiance{};
  Float3 sun_disk_radiance{};
  float sun_angular_radius_radians = 0.0F;
};

struct SceneEnvironmentDescriptor {
  /// Linear RGB radiance in W/(m^2 sr). Environment texture RGB uses the same
  /// units and environment_intensity is a dimensionless multiplier applied to
  /// the constant, texture, and analytic-sky radiance.
  Float3 ambient_radiance{0.03F, 0.03F, 0.03F};
  /// Optional equirectangular, linear-color float texture: U maps longitude
  /// [-pi, pi], V maps latitude [+pi/2, -pi/2], with +Z at U=0.5 and
  /// increasing U rotating toward +X when viewed from +Y.
  RenderAssetReference environment_texture;
  /// Required with environment_texture; resolved resources must pass
  /// ValidateEnvironmentTextureCompatibility().
  RenderAssetReference environment_sampler;
  float environment_intensity = 1.0F;
  AnalyticSkyDescriptor analytic_sky;
  /// Scene-level exposure compensation in stops. A frontend combines it with
  /// the view exposure as `view_exposure * exp2(exposure_compensation_ev)`.
  float exposure_compensation_ev = 0.0F;
};

struct MeshInstanceDescriptor {
  std::uint64_t instance_id = 0U;
  RenderAssetReference mesh;
  RenderAssetReference material;
  std::uint64_t topology_revision = 1U;
  /// Starts at one for base mesh contents and advances whenever this
  /// instance's deformable vertex contents change.
  std::uint64_t deformation_revision = 1U;
  /// Object-to-render transform. For its linear part M, adapters transform a
  /// normal as normalize(transpose(inverse(M))*n), transform tangent.xyz by M
  /// then Gram-Schmidt/normalize it against that normal, and multiply tangent.w
  /// by sign(det(M)). det(M) must be finite and abs(det(M)) > 1e-8. The matrix
  /// must have the exact affine bottom row (0,0,0,1). This same rule applies to
  /// the previous transform and mirrored winding/culling.
  Matrix4x4 render_from_object;
  Matrix4x4 previous_render_from_object;
  Bounds3 local_bounds;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  std::uint32_t flags = MESH_INSTANCE_DEFAULT_FLAGS;
};

struct LightDescriptor {
  std::uint64_t light_id = 0U;
  LightType type = LightType::DIRECTIONAL;
  /// Linear RGB chromatic multiplier. Directional intensity is illuminance in
  /// lux; point and spot intensity is luminous intensity in candela.
  Float3 color_linear{1.0F, 1.0F, 1.0F};
  float intensity = 1.0F;
  /// Render-relative meters at this snapshot's absolute origin. Directional
  /// lights use exact zero for both current and previous positions.
  Float3 position{};
  Float3 previous_position{};
  /// Unit direction in which emitted rays travel. Surface-to-light for a
  /// directional light is therefore -direction. Point lights use the canonical
  /// direction (0,-1,0) because their orientation is not meaningful.
  Float3 direction{0.0F, -1.0F, 0.0F};
  Float3 previous_direction{0.0F, -1.0F, 0.0F};
  /// Local-light range cutoff in meters. For distance d, attenuation is
  /// `(1 - clamp(d/range, 0, 1)^4)^2 / max(d^2, 0.0001)` and is zero at or
  /// beyond range. Directional lights ignore position and range.
  float range = 0.0F;
  /// Spot half-angles. With c=dot(direction, light_to_point), angular falloff
  /// is smoothstep(cos(outer), cos(inner), c), or a hard step when equal.
  /// Point and directional lights ignore both cone values.
  float inner_cone_radians = 0.0F;
  float outer_cone_radians = 0.0F;
  /// Independent visibility of static and deformable/dynamic geometry to the
  /// light's shadow path. Zero explicitly disables shadow generation.
  std::uint32_t shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
};

/// Self-contained deformable state for one complete live mesh allocation.
///
/// Positions are object-local meters and normals/tangents are object-local
/// directions. Velocities are object-local meters per second (translation is
/// not applied when transformed into render space). Version 1 requires every
/// base vertex position and every stream allocated by the base mesh, plus exact
/// updated bounds. This allows snapshots/revisions to render in any order with
/// no cached partial-update state.
struct DynamicMeshUpdateDescriptor {
  std::uint64_t update_sequence = 0U;
  std::uint64_t instance_id = 0U;
  RenderAssetReference mesh;
  std::uint64_t topology_revision = 1U;
  std::uint64_t deformation_revision = 1U;
  std::vector<Float3> positions;
  std::vector<Float3> normals;
  std::vector<Float4> tangents;
  std::vector<Float3> velocities;
  bool has_updated_bounds = false;
  Bounds3 updated_local_bounds;
};

/// Deterministic producer-global particle emission event. Event IDs are
/// strictly increasing and never reused across newly created snapshots during
/// one producer lifetime. A frontend emits an event once on the first
/// successful submission of its snapshot, never per view or repeated render;
/// Shutdown/Initialize resets frontend consumption state. A repeated snapshot
/// is idempotent, while a new snapshot containing an old event ID is rejected.
struct ParticleEvent {
  std::uint64_t event_id = 0U;
  std::uint64_t emitter_id = 0U;
  ParticleEffect effect = ParticleEffect::DUST;
  /// Render-relative meters at this snapshot's absolute origin.
  Float3 position{};
  /// Render-space meters per second.
  Float3 velocity{};
  Float4 color_linear{1.0F, 1.0F, 1.0F, 1.0F};
  float size_meters = 0.1F;
  float lifetime_seconds = 1.0F;
  float intensity = 1.0F;
  std::uint32_t emission_count = 1U;
  std::uint64_t random_seed = 0U;
};

/// Mutable producer payload accepted only after complete validation.
///
/// Every ID-bearing collection must be strictly increasing. This makes the
/// snapshot order deterministic and prevents backends from inventing their own
/// ordering when building draw lists or acceleration structures.
struct SceneSnapshotDescriptor {
  std::uint32_t version = kSceneSnapshotVersion;
  /// Globally identifies these immutable contents for the producer lifetime.
  /// Newly created snapshots use strictly increasing IDs. The same snapshot
  /// may back many frames, but this ID must never identify different contents.
  std::uint64_t snapshot_id = 0U;
  /// Exact logical asset catalog required by every reference in this scene.
  /// A frontend must have applied this registry sequence before rendering.
  std::uint64_t asset_registry_id = 0U;
  std::uint64_t asset_sequence = 0U;
  std::uint64_t simulation_tick = 0U;
  double simulation_time_seconds = 0.0;
  /// Absolute double-precision simulation coordinate represented by render
  /// coordinate (0, 0, 0). Every transform translation, light/particle
  /// position, and camera value in this snapshot is relative to this origin.
  /// Previous transforms are also rebased to this current origin.
  Double3 absolute_world_origin_meters{};
  SceneEnvironmentDescriptor environment;
  std::vector<MeshInstanceDescriptor> mesh_instances;
  std::vector<LightDescriptor> lights;
  std::vector<DynamicMeshUpdateDescriptor> dynamic_mesh_updates;
  std::vector<ParticleEvent> particle_events;
};

struct SceneSnapshotCreateResult;

/// Stable, non-cryptographic FNV-1a-64 digest of the exact validated lighting
/// and environment payload. The canonical byte encoding is little-endian,
/// folds -0 to +0, contains no structure padding, and includes the absolute
/// render origin plus current/previous light state. Snapshot/time identities,
/// geometry, particles, and camera state are deliberately excluded.
[[nodiscard]] std::uint64_t ComputeSceneLightingEnvironmentHash(
    const SceneSnapshotDescriptor &descriptor) noexcept;

class SceneSnapshot final {
public:
  SceneSnapshot(const SceneSnapshot &) = delete;
  SceneSnapshot &operator=(const SceneSnapshot &) = delete;
  SceneSnapshot(SceneSnapshot &&) = delete;
  SceneSnapshot &operator=(SceneSnapshot &&) = delete;
  ~SceneSnapshot() = default;

  [[nodiscard]] std::uint32_t version() const noexcept {
    return descriptor_.version;
  }
  [[nodiscard]] std::uint64_t snapshot_id() const noexcept {
    return descriptor_.snapshot_id;
  }
  [[nodiscard]] std::uint64_t asset_registry_id() const noexcept {
    return descriptor_.asset_registry_id;
  }
  [[nodiscard]] std::uint64_t asset_sequence() const noexcept {
    return descriptor_.asset_sequence;
  }
  [[nodiscard]] std::uint64_t simulation_tick() const noexcept {
    return descriptor_.simulation_tick;
  }
  [[nodiscard]] double simulation_time_seconds() const noexcept {
    return descriptor_.simulation_time_seconds;
  }
  [[nodiscard]] const Double3 &absolute_world_origin_meters() const noexcept {
    return descriptor_.absolute_world_origin_meters;
  }
  [[nodiscard]] const SceneEnvironmentDescriptor &environment() const noexcept {
    return descriptor_.environment;
  }
  [[nodiscard]] const std::vector<MeshInstanceDescriptor> &
  mesh_instances() const noexcept {
    return descriptor_.mesh_instances;
  }
  [[nodiscard]] const std::vector<LightDescriptor> &lights() const noexcept {
    return descriptor_.lights;
  }
  [[nodiscard]] const std::vector<DynamicMeshUpdateDescriptor> &
  dynamic_mesh_updates() const noexcept {
    return descriptor_.dynamic_mesh_updates;
  }
  [[nodiscard]] const std::vector<ParticleEvent> &
  particle_events() const noexcept {
    return descriptor_.particle_events;
  }
  [[nodiscard]] std::uint64_t lighting_environment_hash() const noexcept {
    return lighting_environment_hash_;
  }

private:
  explicit SceneSnapshot(SceneSnapshotDescriptor &&descriptor)
      : descriptor_(std::move(descriptor)),
        lighting_environment_hash_(
            ComputeSceneLightingEnvironmentHash(descriptor_)) {}

  SceneSnapshotDescriptor descriptor_;
  std::uint64_t lighting_environment_hash_ = 0U;

  friend SceneSnapshotCreateResult
  CreateSceneSnapshot(SceneSnapshotDescriptor descriptor);
  friend ValidationResult ValidateSceneSnapshotAssets(
      const SceneSnapshot &snapshot, const RenderAssetRegistry &registry);
};

struct SceneSnapshotCreateResult {
  std::shared_ptr<const SceneSnapshot> snapshot;
  ValidationResult validation;

  [[nodiscard]] bool ok() const noexcept {
    return snapshot != nullptr && validation.ok();
  }

  explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] bool IsKnownLightType(LightType type) noexcept;
[[nodiscard]] bool IsKnownParticleEffect(ParticleEffect effect) noexcept;
[[nodiscard]] ValidationResult
ValidateSceneSnapshotDescriptor(const SceneSnapshotDescriptor &descriptor);
/// Resolves every portable reference against the exact catalog revision and
/// validates cross-resource compatibility before a backend mutates its scene.
[[nodiscard]] ValidationResult ValidateSceneSnapshotAssets(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry);
[[nodiscard]] ValidationResult
ValidateSceneSnapshotAssets(const SceneSnapshot &snapshot,
                            const RenderAssetRegistry &registry);
[[nodiscard]] SceneSnapshotCreateResult
CreateSceneSnapshot(SceneSnapshotDescriptor descriptor);

} // namespace RoR::Render
