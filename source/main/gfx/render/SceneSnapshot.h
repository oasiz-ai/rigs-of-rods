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
#include "ReflectionProbeRuntime.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kSceneSnapshotVersion = 6U;
constexpr std::uint32_t kSceneLightingHashVersion = 3U;
constexpr std::uint32_t kSceneReflectionProbeHashVersion = 1U;

class RenderAssetRegistry;
struct MeshResourceDescriptor;

/// IEC 61966-2-1 linear-sRGB / ITU-R BT.709 luminance coefficients for the
/// D65 white point. These exact decimal coefficients, encoded as binary64,
/// define the renderer-neutral photometric contract.
constexpr double kLinearSrgbRec709D65RedLuminance = 0.2126;
constexpr double kLinearSrgbRec709D65GreenLuminance = 0.7152;
constexpr double kLinearSrgbRec709D65BlueLuminance = 0.0722;

enum class LightType : std::uint8_t {
  DIRECTIONAL = 0,
  POINT = 1,
  SPOT = 2,
};

enum class LightHistorySample : std::uint8_t {
  CURRENT = 0,
  PREVIOUS = 1,
};

enum class ShadowGeometryClass : std::uint8_t {
  STATIC = 0,
  DYNAMIC = 1,
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
///
/// The optional deterministic cloud layer is described, not rendered, here: a
/// backend derives its cloud pattern purely from these three values, so equal
/// descriptors must always reproduce identical cloud geometry. cloud_coverage
/// is the covered sky fraction in [0, 1]; cloud_phase_radians rotates the
/// pattern in longitude and is produced as fmod(time, 2*pi), which native
/// admission relies on. All three stay canonical zero while the layer (or
/// the whole sky) is disabled.
struct AnalyticSkyDescriptor {
  bool enabled = false;
  std::uint64_t sun_light_id = 0U;
  Float3 zenith_radiance{};
  Float3 horizon_radiance{};
  Float3 ground_radiance{};
  Float3 sun_disk_radiance{};
  float sun_angular_radius_radians = 0.0F;
  float cloud_coverage = 0.0F;
  Float3 cloud_radiance{};
  float cloud_phase_radians = 0.0F;
};

/// Optional transported menu/HUD overlay. The material is an ordinary
/// MaterialModel::UNLIT display-domain material
/// (SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE) whose base-color texture carries
/// display-referred, premultiplied RGBA GUI pixels with union coverage in
/// alpha; MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER is required so a
/// presenter composites it after tone mapping as one fullscreen
/// (ONE, ONE_MINUS_SRC_ALPHA) quad. Disabled state keeps the canonical
/// absent material reference. The HUD is deliberately a texture reference,
/// not a geometry domain, so its content flows through the existing
/// per-frame asset delta and revision machinery.
struct HudOverlayDescriptor {
  bool enabled = false;
  RenderAssetReference material;
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
  /// the view exposure as `view_exposure * exp2(exposure_compensation_ev)`;
  /// validation requires a positive normal binary32 effective result.
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
  /// Linear-sRGB Rec.709 D65 chromatic multiplier with canonical photopic
  /// luminance one. Directional intensity is illuminance in lux; point and
  /// spot intensity is luminous intensity in candela. Consequently intensity,
  /// rather than an arbitrary RGB magnitude, is the light's scalar photometry.
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
  /// Strictly increasing absolute-world reflection probes. Expensive capture
  /// scheduling remains frontend/runtime state; the immutable snapshot carries
  /// the complete authored set required to derive that schedule.
  std::vector<ReflectionProbeRuntimeDescriptor> reflection_probes;
  /// Version 6: optional transported menu/HUD overlay reference. It is not
  /// lighting state; the canonical lighting/reflection hash encodings remain
  /// untouched by this field.
  HudOverlayDescriptor hud_overlay;
  std::vector<DynamicMeshUpdateDescriptor> dynamic_mesh_updates;
  std::vector<ParticleEvent> particle_events;
};

struct SceneSnapshotCreateResult;

/// Returns the Rec.709/D65 photopic luminance of a linear-sRGB triplet.
[[nodiscard]] double
ComputeLinearSrgbRec709D65Luminance(const Float3 &color_linear) noexcept;

/// Converts a finite, nonnegative, non-black linear-sRGB color to the
/// canonical unit-luminance binary32 representation. The output is unchanged
/// on failure.
[[nodiscard]] bool NormalizePhotometricColorLinear(
    const Float3 &color_linear, Float3 &normalized_color_linear) noexcept;

/// True only for a finite, nonnegative, non-black binary32 triplet whose
/// Rec.709/D65 luminance lies in the exact round-to-binary32-one interval.
[[nodiscard]] bool
IsCanonicalPhotometricColorLinear(const Float3 &color_linear) noexcept;

/// Tests whether a renderer-space view ray intersects the analytic sun disk.
/// Both inputs are normalized internally. The disk center is the negated
/// emitted-ray direction; normalized dot and cosine are rounded to binary32
/// before the inclusive comparison. Invalid sky/light/sample state or a
/// zero/nonfinite input vector returns false.
[[nodiscard]] bool IsViewDirectionInsideAnalyticSunDisk(
    const Float3 &view_direction, const AnalyticSkyDescriptor &sky,
    const LightDescriptor &sun,
    LightHistorySample sample = LightHistorySample::CURRENT) noexcept;

/// Shadow geometry is authored resource state. It depends only on
/// MeshResourceDescriptor::dynamic, never instance motion, deformation
/// revision, or the presence of a DynamicMeshUpdateDescriptor.
[[nodiscard]] ShadowGeometryClass
ClassifyShadowGeometry(const MeshResourceDescriptor &mesh) noexcept;
[[nodiscard]] std::uint32_t
ShadowGeometryClassMask(ShadowGeometryClass geometry_class) noexcept;
[[nodiscard]] bool LightShadowMaskIncludesGeometry(
    const LightDescriptor &light, const MeshResourceDescriptor &mesh) noexcept;
[[nodiscard]] bool MeshInstanceCastsShadowForLight(
    const LightDescriptor &light, const MeshInstanceDescriptor &instance,
    const MeshResourceDescriptor &mesh) noexcept;

/// Computes `view_exposure * exp2(scene_ev)` and accepts only a positive,
/// finite, normal IEEE-754 binary32 result. Excluding subnormals makes the
/// policy stable on FTZ and non-FTZ backends. The output is unchanged on
/// failure.
[[nodiscard]] bool ComputePortableEffectiveExposure(
    float view_exposure, float scene_exposure_compensation_ev,
    float &effective_exposure) noexcept;

/// Stable, non-cryptographic FNV-1a-64 digest of the exact validated lighting
/// and environment payload. The canonical byte encoding is little-endian,
/// folds -0 to +0, contains no structure padding, and includes the asset
/// registry identity, absolute render origin, and current/previous light state.
/// Snapshot/time identities, asset sequence, geometry, particles, and camera
/// state are deliberately excluded.
[[nodiscard]] std::uint64_t ComputeSceneLightingEnvironmentHash(
    const SceneSnapshotDescriptor &descriptor) noexcept;
/// Stable FNV-1a-64 digest of the ordered absolute-world reflection-probe set.
/// Render-origin, snapshot/time identities, and capture generations are
/// excluded; authored revision/geometry/update policy are included.
[[nodiscard]] std::uint64_t ComputeSceneReflectionProbeHash(
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
  [[nodiscard]] const std::vector<ReflectionProbeRuntimeDescriptor> &
  reflection_probes() const noexcept {
    return descriptor_.reflection_probes;
  }
  [[nodiscard]] const HudOverlayDescriptor &hud_overlay() const noexcept {
    return descriptor_.hud_overlay;
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
  [[nodiscard]] std::uint64_t reflection_probe_hash() const noexcept {
    return reflection_probe_hash_;
  }

private:
  explicit SceneSnapshot(SceneSnapshotDescriptor &&descriptor)
      : descriptor_(std::move(descriptor)),
        lighting_environment_hash_(
            ComputeSceneLightingEnvironmentHash(descriptor_)),
        reflection_probe_hash_(ComputeSceneReflectionProbeHash(descriptor_)) {}

  SceneSnapshotDescriptor descriptor_;
  std::uint64_t lighting_environment_hash_ = 0U;
  std::uint64_t reflection_probe_hash_ = 0U;

  friend SceneSnapshotCreateResult
  CreateSceneSnapshot(SceneSnapshotDescriptor descriptor);
  friend SceneSnapshotCreateResult CreateSceneSnapshotWithRetainedBlock(
      SceneSnapshotDescriptor descriptor,
      const std::shared_ptr<const SceneSnapshot> &previous,
      const std::vector<std::uint32_t> &patched_indices);
  friend ValidationResult ValidateSceneSnapshotAssets(
      const SceneSnapshot &snapshot, const RenderAssetRegistry &registry);
  friend ValidationResult ValidateSceneSnapshotAssetsScoped(
      const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
      const std::vector<std::uint64_t> &instance_ids);
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
/// Registry compatibility for only `instance_ids` (nonzero, strictly
/// increasing) and their dynamic updates, using the same Detail validators as
/// the full pass so no compatibility rule is forked. It deliberately does not
/// re-run ValidateSceneSnapshotDescriptor: the snapshot already carries that
/// proof. A caller must have validated every other instance against this exact
/// registry revision on a previously accepted frame; an identity absent from
/// the snapshot fails closed rather than being skipped.
[[nodiscard]] ValidationResult ValidateSceneSnapshotAssetsScoped(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry,
    const std::vector<std::uint64_t> &instance_ids);
[[nodiscard]] ValidationResult ValidateSceneSnapshotAssetsScoped(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const std::vector<std::uint64_t> &instance_ids);
[[nodiscard]] SceneSnapshotCreateResult
CreateSceneSnapshot(SceneSnapshotDescriptor descriptor);
/// Creates a snapshot whose mesh_instances claim byte-identity with
/// `previous` everywhere except `patched_indices` (in range, strictly
/// increasing). The claim is verified here by segmented memcmp against
/// previous->mesh_instances(); MeshInstanceDescriptor is trivially copyable,
/// so a caller must fill the unpatched region by copying those exact bytes
/// rather than reconstructing equal values. Per-entry instance validation
/// then runs for the patched entries and the ordering seams around them, and
/// in full for every non-instance section. Any mismatch fails closed, so
/// nothing enters a snapshot without either fresh validation or byte-level
/// proof against an already-validated immutable snapshot.
[[nodiscard]] SceneSnapshotCreateResult CreateSceneSnapshotWithRetainedBlock(
    SceneSnapshotDescriptor descriptor,
    const std::shared_ptr<const SceneSnapshot> &previous,
    const std::vector<std::uint32_t> &patched_indices);

} // namespace RoR::Render
