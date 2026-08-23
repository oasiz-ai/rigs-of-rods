/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneSnapshot.h"

#include "MaterialDescriptor.h"
#include "RenderAssetRegistry.h"
#include "RenderResourceDescriptors.h"
#include "ValidatedAssetCompatibilityInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace RoR::Render {
static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24 &&
                  std::numeric_limits<float>::max_exponent == 128,
              "render contract requires IEEE-754 binary32");
static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_exponent == 1024,
              "render contract requires IEEE-754 binary64");
namespace {

constexpr float kHalfPi = 1.57079632679489661923F;

class CanonicalLightingHasher final {
public:
  void AddByte(std::uint8_t value) noexcept {
    hash_ ^= static_cast<std::uint64_t>(value);
    hash_ *= kPrime;
  }

  void AddU32(std::uint32_t value) noexcept {
    for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
      AddByte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void AddU64(std::uint64_t value) noexcept {
    for (std::size_t byte = 0U; byte < sizeof(value); ++byte) {
      AddByte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU));
    }
  }

  void AddFloat(float value) noexcept {
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "lighting hash requires IEEE-754 binary32 storage");
    if (value == 0.0F) {
      value = 0.0F;
    }
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU32(bits);
  }

  void AddDouble(double value) noexcept {
    static_assert(sizeof(double) == sizeof(std::uint64_t),
                  "lighting hash requires IEEE-754 binary64 storage");
    if (value == 0.0) {
      value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU64(bits);
  }

  void AddFloat3(const Float3 &value) noexcept {
    AddFloat(value.x);
    AddFloat(value.y);
    AddFloat(value.z);
  }

  void AddAssetReference(const RenderAssetReference &reference) noexcept {
    AddByte(static_cast<std::uint8_t>(reference.kind));
    AddU64(reference.id.high());
    AddU64(reference.id.low());
    AddU64(reference.revision);
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
  static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  static constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash_ = kOffsetBasis;
};

bool IsCanonicalPointDirection(const Float3 &direction) noexcept {
  return direction.x == 0.0F && direction.y == -1.0F &&
         direction.z == 0.0F;
}

bool IsCanonicalZero(const Float3 &value) noexcept {
  return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

bool NormalizeDirection(const Float3 &input,
                        std::array<double, 3U> &output) noexcept {
  if (!IsFinite(input)) {
    return false;
  }
  const double x = static_cast<double>(input.x);
  const double y = static_cast<double>(input.y);
  const double z = static_cast<double>(input.z);
  const double length_squared = x * x + y * y + z * z;
  if (!std::isfinite(length_squared) || length_squared <= 0.0) {
    return false;
  }
  const double inverse_length = 1.0 / std::sqrt(length_squared);
  if (!std::isfinite(inverse_length)) {
    return false;
  }
  output = {{x * inverse_length, y * inverse_length, z * inverse_length}};
  return std::isfinite(output[0U]) && std::isfinite(output[1U]) &&
         std::isfinite(output[2U]);
}

bool ValidateAssetReference(const RenderAssetReference &reference,
                            RenderAssetKind kind, const char *field,
                            std::size_t index, ValidationResult &failure) {
  if (!reference.valid()) {
    failure = ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, field,
        "required renderer-neutral asset reference is invalid", index);
    return false;
  }
  if (reference.kind != kind) {
    failure = ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND, field,
                                        "asset kind does not match", index);
    return false;
  }
  return true;
}

bool ValidateIncreasingIdentifier(std::uint64_t identifier,
                                  std::uint64_t previous, bool has_previous,
                                  const char *field, std::size_t index,
                                  ValidationResult &failure) {
  if (identifier == 0U) {
    failure = ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                        field, "identifier must be nonzero",
                                        index);
    return false;
  }
  if (has_previous && identifier == previous) {
    failure = ValidationResult::Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                                        field, "identifier is duplicated",
                                        index);
    return false;
  }
  if (has_previous && identifier < previous) {
    failure = ValidationResult::Failure(
        ValidationCode::NON_DETERMINISTIC_ORDER, field,
        "identifiers must be strictly increasing", index);
    return false;
  }
  return true;
}

bool EqualBounds(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return lhs.minimum.x == rhs.minimum.x && lhs.minimum.y == rhs.minimum.y &&
         lhs.minimum.z == rhs.minimum.z && lhs.maximum.x == rhs.maximum.x &&
         lhs.maximum.y == rhs.maximum.y && lhs.maximum.z == rhs.maximum.z;
}

bool ValidateFiniteVertices(const DynamicMeshUpdateDescriptor &update,
                            std::size_t update_index,
                            ValidationResult &failure) {
  for (const Float3 &position : update.positions) {
    if (!IsFinite(position)) {
      failure = ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.positions",
          "all positions must be finite", update_index);
      return false;
    }
  }
  for (const Float3 &normal : update.normals) {
    if (!IsFinite(normal)) {
      failure = ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.normals",
          "all normals must be finite", update_index);
      return false;
    }
    if (!IsNormalized(normal)) {
      failure = ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.normals",
          "all supplied normals must have unit length", update_index);
      return false;
    }
  }
  for (const Float4 &tangent : update.tangents) {
    if (!IsFinite(tangent)) {
      failure = ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.tangents",
          "all tangents must be finite", update_index);
      return false;
    }
    if (!IsNormalizedTangent(tangent)) {
      failure = ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.tangents",
          "tangent xyz must have unit length and handedness must be -1 or 1",
          update_index);
      return false;
    }
  }
  for (const Float3 &velocity : update.velocities) {
    if (!IsFinite(velocity)) {
      failure = ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "dynamic_mesh_updates.velocities",
          "all velocities must be finite", update_index);
      return false;
    }
  }
  return true;
}

} // namespace

double ComputeLinearSrgbRec709D65Luminance(
    const Float3 &color_linear) noexcept {
  return static_cast<double>(color_linear.x) *
             kLinearSrgbRec709D65RedLuminance +
         static_cast<double>(color_linear.y) *
             kLinearSrgbRec709D65GreenLuminance +
         static_cast<double>(color_linear.z) *
             kLinearSrgbRec709D65BlueLuminance;
}

bool IsCanonicalPhotometricColorLinear(const Float3 &color_linear) noexcept {
  if (!IsFinite(color_linear) || !IsNonNegative(color_linear)) {
    return false;
  }
  const double luminance =
      ComputeLinearSrgbRec709D65Luminance(color_linear);
  // Exact midpoints between binary32 1.0 and its adjacent values. Ties round
  // to 1.0 because its significand is even.
  constexpr double kRoundsToOneMinimum = 1.0 - 0x1p-25;
  constexpr double kRoundsToOneMaximum = 1.0 + 0x1p-24;
  return std::isfinite(luminance) && luminance >= kRoundsToOneMinimum &&
         luminance <= kRoundsToOneMaximum;
}

bool NormalizePhotometricColorLinear(
    const Float3 &color_linear, Float3 &normalized_color_linear) noexcept {
  if (!IsFinite(color_linear) || !IsNonNegative(color_linear)) {
    return false;
  }
  const double luminance =
      ComputeLinearSrgbRec709D65Luminance(color_linear);
  if (!std::isfinite(luminance) || luminance <= 0.0) {
    return false;
  }
  const Float3 candidate{
      static_cast<float>(static_cast<double>(color_linear.x) / luminance),
      static_cast<float>(static_cast<double>(color_linear.y) / luminance),
      static_cast<float>(static_cast<double>(color_linear.z) / luminance)};
  if (!IsCanonicalPhotometricColorLinear(candidate)) {
    return false;
  }
  normalized_color_linear = candidate;
  return true;
}

bool IsViewDirectionInsideAnalyticSunDisk(
    const Float3 &view_direction, const AnalyticSkyDescriptor &sky,
    const LightDescriptor &sun, LightHistorySample sample) noexcept {
  if (!sky.enabled || sky.sun_light_id == 0U ||
      sky.sun_light_id != sun.light_id || sun.type != LightType::DIRECTIONAL ||
      !IsFinite(sky.sun_angular_radius_radians) ||
      sky.sun_angular_radius_radians <= 0.0F ||
      sky.sun_angular_radius_radians > kHalfPi ||
      (sample != LightHistorySample::CURRENT &&
       sample != LightHistorySample::PREVIOUS)) {
    return false;
  }
  std::array<double, 3U> normalized_view{};
  std::array<double, 3U> normalized_emitted{};
  const Float3 &emitted = sample == LightHistorySample::CURRENT
                              ? sun.direction
                              : sun.previous_direction;
  if (!NormalizeDirection(view_direction, normalized_view) ||
      !NormalizeDirection(emitted, normalized_emitted)) {
    return false;
  }
  const float center_dot_view = static_cast<float>(
      -(normalized_view[0U] * normalized_emitted[0U] +
        normalized_view[1U] * normalized_emitted[1U] +
        normalized_view[2U] * normalized_emitted[2U]));
  const float boundary = static_cast<float>(
      std::cos(static_cast<double>(sky.sun_angular_radius_radians)));
  return center_dot_view >= boundary;
}

ShadowGeometryClass
ClassifyShadowGeometry(const MeshResourceDescriptor &mesh) noexcept {
  return mesh.dynamic ? ShadowGeometryClass::DYNAMIC
                      : ShadowGeometryClass::STATIC;
}

std::uint32_t
ShadowGeometryClassMask(ShadowGeometryClass geometry_class) noexcept {
  switch (geometry_class) {
  case ShadowGeometryClass::STATIC:
    return LIGHT_SHADOW_STATIC_GEOMETRY;
  case ShadowGeometryClass::DYNAMIC:
    return LIGHT_SHADOW_DYNAMIC_GEOMETRY;
  }
  return 0U;
}

bool LightShadowMaskIncludesGeometry(
    const LightDescriptor &light,
    const MeshResourceDescriptor &mesh) noexcept {
  return (light.shadow_flags &
          ShadowGeometryClassMask(ClassifyShadowGeometry(mesh))) != 0U;
}

bool MeshInstanceCastsShadowForLight(
    const LightDescriptor &light, const MeshInstanceDescriptor &instance,
    const MeshResourceDescriptor &mesh) noexcept {
  return (instance.flags & MESH_INSTANCE_CASTS_SHADOW) != 0U &&
         LightShadowMaskIncludesGeometry(light, mesh);
}

bool ComputePortableEffectiveExposure(
    float view_exposure, float scene_exposure_compensation_ev,
    float &effective_exposure) noexcept {
  if (!IsFinite(view_exposure) || view_exposure <= 0.0F ||
      !IsFinite(scene_exposure_compensation_ev)) {
    return false;
  }
  const double value =
      static_cast<double>(view_exposure) *
      std::exp2(static_cast<double>(scene_exposure_compensation_ev));
  constexpr double kMinimumPortableExposure =
      static_cast<double>((std::numeric_limits<float>::min)());
  constexpr double kMaximumPortableExposure =
      static_cast<double>((std::numeric_limits<float>::max)());
  if (!std::isfinite(value) || value < kMinimumPortableExposure ||
      value > kMaximumPortableExposure) {
    return false;
  }
  const float candidate = static_cast<float>(value);
  if (!IsFinite(candidate) || candidate <= 0.0F ||
      std::fpclassify(candidate) != FP_NORMAL) {
    return false;
  }
  effective_exposure = candidate;
  return true;
}

bool IsKnownLightType(LightType type) noexcept {
  switch (type) {
  case LightType::DIRECTIONAL:
  case LightType::POINT:
  case LightType::SPOT:
    return true;
  }
  return false;
}

bool IsKnownParticleEffect(ParticleEffect effect) noexcept {
  switch (effect) {
  case ParticleEffect::TIRE_SMOKE:
  case ParticleEffect::DUST:
  case ParticleEffect::SPARKS:
  case ParticleEffect::WATER_SPRAY:
  case ParticleEffect::STEAM:
  case ParticleEffect::FIRE:
  case ParticleEffect::DEBRIS:
    return true;
  }
  return false;
}

std::uint64_t ComputeSceneLightingEnvironmentHash(
    const SceneSnapshotDescriptor &descriptor) noexcept {
  CanonicalLightingHasher hasher;
  constexpr std::uint8_t kDomain[] = {'R', 'o', 'R', '-', 'l', 'i', 'g', 'h',
                                      't', 'i', 'n', 'g', '-', 'e', 'n', 'v'};
  for (const std::uint8_t byte : kDomain) {
    hasher.AddByte(byte);
  }
  hasher.AddU32(kSceneLightingHashVersion);
  hasher.AddU32(descriptor.version);
  hasher.AddU64(descriptor.asset_registry_id);
  hasher.AddDouble(descriptor.absolute_world_origin_meters.x);
  hasher.AddDouble(descriptor.absolute_world_origin_meters.y);
  hasher.AddDouble(descriptor.absolute_world_origin_meters.z);

  const SceneEnvironmentDescriptor &environment = descriptor.environment;
  hasher.AddFloat3(environment.ambient_radiance);
  hasher.AddAssetReference(environment.environment_texture);
  hasher.AddAssetReference(environment.environment_sampler);
  hasher.AddFloat(environment.environment_intensity);
  hasher.AddByte(environment.analytic_sky.enabled ? 1U : 0U);
  hasher.AddU64(environment.analytic_sky.sun_light_id);
  hasher.AddFloat3(environment.analytic_sky.zenith_radiance);
  hasher.AddFloat3(environment.analytic_sky.horizon_radiance);
  hasher.AddFloat3(environment.analytic_sky.ground_radiance);
  hasher.AddFloat3(environment.analytic_sky.sun_disk_radiance);
  hasher.AddFloat(environment.analytic_sky.sun_angular_radius_radians);
  hasher.AddFloat(environment.analytic_sky.cloud_coverage);
  hasher.AddFloat3(environment.analytic_sky.cloud_radiance);
  hasher.AddFloat(environment.analytic_sky.cloud_phase_radians);
  hasher.AddFloat(environment.analytic_sky.haze_extinction_per_meter);
  hasher.AddFloat(
      environment.analytic_sky.haze_inverse_scale_height_per_meter);
  hasher.AddFloat(environment.analytic_sky.haze_base_height_meters);
  hasher.AddFloat(environment.exposure_compensation_ev);

  hasher.AddU64(static_cast<std::uint64_t>(descriptor.lights.size()));
  for (const LightDescriptor &light : descriptor.lights) {
    hasher.AddU64(light.light_id);
    hasher.AddByte(static_cast<std::uint8_t>(light.type));
    hasher.AddFloat3(light.color_linear);
    hasher.AddFloat(light.intensity);
    hasher.AddFloat3(light.position);
    hasher.AddFloat3(light.previous_position);
    hasher.AddFloat3(light.direction);
    hasher.AddFloat3(light.previous_direction);
    hasher.AddFloat(light.range);
    hasher.AddFloat(light.inner_cone_radians);
    hasher.AddFloat(light.outer_cone_radians);
    hasher.AddU32(light.shadow_flags);
  }
  return hasher.value();
}

std::uint64_t ComputeSceneReflectionProbeHash(
    const SceneSnapshotDescriptor &descriptor) noexcept {
  CanonicalLightingHasher hasher;
  constexpr std::uint8_t kDomain[] = {'R', 'o', 'R', '-', 'r', 'e', 'f', 'l',
                                      'e', 'c', 't', 'i', 'o', 'n', '-', 'p',
                                      'r', 'o', 'b', 'e'};
  for (const std::uint8_t byte : kDomain) {
    hasher.AddByte(byte);
  }
  hasher.AddU32(kSceneReflectionProbeHashVersion);
  hasher.AddU32(descriptor.version);
  hasher.AddU64(
      static_cast<std::uint64_t>(descriptor.reflection_probes.size()));
  for (const ReflectionProbeRuntimeDescriptor &probe :
       descriptor.reflection_probes) {
    hasher.AddU64(ComputeReflectionProbeDescriptorFingerprint(probe));
  }
  return hasher.value();
}

namespace {

/// Every per-instance rule except the strictly-increasing identity relation,
/// which needs the instance's neighbours rather than the instance alone.
bool ValidateMeshInstanceEntry(const MeshInstanceDescriptor &instance,
                               std::size_t index,
                               ValidationResult &validation) {
  if (!ValidateAssetReference(instance.mesh, RenderAssetKind::MESH,
                              "mesh_instances.mesh", index, validation)) {
    return false;
  }
  if (!ValidateAssetReference(instance.material, RenderAssetKind::MATERIAL,
                              "mesh_instances.material", index, validation)) {
    return false;
  }
  if (instance.topology_revision == 0U ||
      instance.deformation_revision == 0U) {
    validation = ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "mesh_instances.revision",
        "topology and deformation revisions must be nonzero", index);
    return false;
  }
  if (!HasInvertibleAffineTransform(instance.render_from_object) ||
      !HasInvertibleAffineTransform(instance.previous_render_from_object)) {
    validation = ValidationResult::Failure(
        IsFinite(instance.render_from_object) &&
                IsFinite(instance.previous_render_from_object)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "mesh_instances.transform",
        "current and previous transforms must be canonical affine and "
        "invertible",
        index);
    return false;
  }
  if (!IsValid(instance.local_bounds)) {
    validation = ValidationResult::Failure(
        ValidationCode::INVALID_BOUNDS, "mesh_instances.local_bounds",
        "mesh bounds must be finite and ordered", index);
    return false;
  }
  if (instance.visibility_mask == 0U) {
    validation = ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "mesh_instances.visibility_mask",
        "visibility mask must contain at least one bit", index);
    return false;
  }
  constexpr std::uint32_t kKnownMeshFlags =
      MESH_INSTANCE_CASTS_SHADOW | MESH_INSTANCE_RECEIVES_SHADOW |
      MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
  if ((instance.flags & ~kKnownMeshFlags) != 0U) {
    validation = ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "mesh_instances.flags",
        "mesh instance contains unknown flag bits", index);
    return false;
  }
  return true;
}

/// `scoped_instance_indices`, when present, names the only instance entries
/// this call may assume nothing about. Every other entry has already been
/// proven byte-identical to an entry of a previously validated snapshot at
/// the same position, so its own rules and its internal ordering carry over;
/// only the ordering seams around the scoped entries still need checking.
/// Every non-instance section is validated in full either way.
ValidationResult ValidateSceneSnapshotDescriptorInternal(
    const SceneSnapshotDescriptor &descriptor,
    const std::vector<std::uint32_t> *scoped_instance_indices) {
  if (descriptor.version != kSceneSnapshotVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported scene snapshot version");
  }
  if (descriptor.snapshot_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "snapshot_id",
                                     "snapshot identifier must be nonzero");
  }
  if (descriptor.asset_registry_id == 0U || descriptor.asset_sequence == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        descriptor.asset_registry_id == 0U ? "asset_registry_id"
                                           : "asset_sequence",
        "asset registry identity and sequence must be nonzero");
  }
  if (!IsFinite(descriptor.simulation_time_seconds) ||
      descriptor.simulation_time_seconds < 0.0) {
    return ValidationResult::Failure(
        IsFinite(descriptor.simulation_time_seconds)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "simulation_time_seconds",
        "simulation time must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.absolute_world_origin_meters)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "absolute_world_origin_meters",
                                     "absolute render origin must be finite");
  }
  if (!IsNonNegative(descriptor.environment.ambient_radiance)) {
    return ValidationResult::Failure(
        IsFinite(descriptor.environment.ambient_radiance)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "environment.ambient_radiance",
        "ambient radiance must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.environment.environment_intensity) ||
      descriptor.environment.environment_intensity < 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.environment.environment_intensity)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "environment.environment_intensity",
        "environment intensity must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.environment.exposure_compensation_ev) ||
      descriptor.environment.exposure_compensation_ev < -24.0F ||
      descriptor.environment.exposure_compensation_ev > 24.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.environment.exposure_compensation_ev)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "environment.exposure_compensation_ev",
        "exposure compensation must be finite and within [-24, 24] EV");
  }
  const AnalyticSkyDescriptor &sky = descriptor.environment.analytic_sky;
  if (!IsFinite(sky.zenith_radiance) ||
      !IsFinite(sky.horizon_radiance) ||
      !IsFinite(sky.ground_radiance) ||
      !IsFinite(sky.sun_disk_radiance) ||
      !IsFinite(sky.sun_angular_radius_radians) ||
      !IsFinite(sky.cloud_coverage) || !IsFinite(sky.cloud_radiance) ||
      !IsFinite(sky.cloud_phase_radians) ||
      !IsFinite(sky.haze_extinction_per_meter) ||
      !IsFinite(sky.haze_inverse_scale_height_per_meter) ||
      !IsFinite(sky.haze_base_height_meters)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "environment.analytic_sky",
        "all analytic sky numeric fields must be finite");
  }
  if (!sky.enabled) {
    if (sky.sun_light_id != 0U || !IsCanonicalZero(sky.zenith_radiance) ||
        !IsCanonicalZero(sky.horizon_radiance) ||
        !IsCanonicalZero(sky.ground_radiance) ||
        !IsCanonicalZero(sky.sun_disk_radiance) ||
        sky.sun_angular_radius_radians != 0.0F ||
        sky.cloud_coverage != 0.0F ||
        !IsCanonicalZero(sky.cloud_radiance) ||
        sky.cloud_phase_radians != 0.0F ||
        sky.haze_extinction_per_meter != 0.0F ||
        sky.haze_inverse_scale_height_per_meter != 0.0F ||
        sky.haze_base_height_meters != 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "environment.analytic_sky",
          "disabled analytic sky state must use its canonical zero payload");
    }
  } else if (sky.sun_light_id == 0U ||
             !IsNonNegative(sky.zenith_radiance) ||
             !IsNonNegative(sky.horizon_radiance) ||
             !IsNonNegative(sky.ground_radiance) ||
             !IsNonNegative(sky.sun_disk_radiance) ||
             sky.sun_angular_radius_radians <= 0.0F ||
             sky.sun_angular_radius_radians > kHalfPi ||
             sky.cloud_coverage < 0.0F || sky.cloud_coverage > 1.0F ||
             !IsNonNegative(sky.cloud_radiance) ||
             // Aerial haze bounds. Zero extinction is deliberately legal and
             // means exactly no haze; the upper bounds are two decades above
             // any physical atmosphere (1e-2 /m is ~400 m visibility, 1e-1 /m
             // a 10 m scale height) so an out-of-range payload can only be
             // corruption and fails closed here rather than saturating a
             // backend's exponential.
             sky.haze_extinction_per_meter < 0.0F ||
             sky.haze_extinction_per_meter > 1.0e-2F ||
             sky.haze_inverse_scale_height_per_meter < 0.0F ||
             sky.haze_inverse_scale_height_per_meter > 1.0e-1F ||
             sky.haze_base_height_meters < -1.0e5F ||
             sky.haze_base_height_meters > 1.0e5F) {
    return ValidationResult::Failure(
        sky.sun_light_id == 0U ? ValidationCode::INVALID_IDENTIFIER
                               : ValidationCode::VALUE_OUT_OF_RANGE,
        "environment.analytic_sky",
        "enabled sky requires a sun identity, nonnegative radiance, a sun "
        "half-angle in (0, pi/2], cloud coverage in [0, 1], haze extinction "
        "in [0, 1e-2] /m, haze inverse scale height in [0, 1e-1] /m, and a "
        "haze base height within +/-1e5 m");
  }
  const bool environment_texture_absent = IsAbsentRenderAssetReference(
      descriptor.environment.environment_texture);
  const bool environment_sampler_absent = IsAbsentRenderAssetReference(
      descriptor.environment.environment_sampler);
  if ((!environment_texture_absent &&
       !descriptor.environment.environment_texture.valid()) ||
      (!environment_sampler_absent &&
       !descriptor.environment.environment_sampler.valid())) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, "environment",
        "optional environment references must be canonical absent or fully valid");
  }
  if (!environment_texture_absent &&
      descriptor.environment.environment_texture.kind !=
          RenderAssetKind::TEXTURE) {
    return ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND,
                                     "environment.environment_texture",
                                     "environment resource must be a texture");
  }
  if (!environment_sampler_absent &&
      descriptor.environment.environment_sampler.kind !=
          RenderAssetKind::SAMPLER) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_ASSET_KIND, "environment.environment_sampler",
        "environment sampler resource must be a sampler");
  }
  if (environment_texture_absent != environment_sampler_absent) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "environment",
        "environment texture and explicit sampler must be supplied together");
  }
  if (descriptor.hud_overlay.enabled) {
    if (!descriptor.hud_overlay.material.valid()) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_ASSET_REFERENCE, "hud_overlay.material",
          "enabled HUD overlay requires a valid material reference");
    }
    if (descriptor.hud_overlay.material.kind != RenderAssetKind::MATERIAL) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_ASSET_KIND, "hud_overlay.material",
          "HUD overlay reference must identify a material");
    }
  } else if (!IsAbsentRenderAssetReference(descriptor.hud_overlay.material)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, "hud_overlay.material",
        "disabled HUD overlay must keep the canonical absent material");
  }

  // Reuse one failure carrier throughout the collection walks. In an MSVC
  // checked-iterator Debug build, constructing a successful ValidationResult
  // would otherwise allocate two std::string proxies for every scene element.
  ValidationResult validation;
  std::uint64_t previous_identifier = 0U;
  if (scoped_instance_indices == nullptr) {
    for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
         ++index) {
      const MeshInstanceDescriptor &instance =
          descriptor.mesh_instances[index];
      if (!ValidateIncreasingIdentifier(
              instance.instance_id, previous_identifier, index != 0U,
              "mesh_instances.instance_id", index, validation)) {
        return validation;
      }
      previous_identifier = instance.instance_id;
      if (!ValidateMeshInstanceEntry(instance, index, validation)) {
        return validation;
      }
    }
  } else {
    for (const std::uint32_t scoped : *scoped_instance_indices) {
      const std::size_t index = static_cast<std::size_t>(scoped);
      if (index >= descriptor.mesh_instances.size()) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE,
            "retained_block.patched_indices",
            "patched instance index is outside this snapshot", index);
      }
      const MeshInstanceDescriptor &instance =
          descriptor.mesh_instances[index];
      if (!ValidateIncreasingIdentifier(instance.instance_id, 0U, false,
                                        "mesh_instances.instance_id", index,
                                        validation)) {
        return validation;
      }
      // Both seams: the run before and the run after are internally ordered
      // by the byte-identity proof, so ordering can only break here.
      if ((index != 0U &&
           descriptor.mesh_instances[index - 1U].instance_id >=
               instance.instance_id) ||
          (index + 1U < descriptor.mesh_instances.size() &&
           instance.instance_id >=
               descriptor.mesh_instances[index + 1U].instance_id)) {
        return ValidationResult::Failure(
            ValidationCode::SEQUENCE_MISMATCH, "mesh_instances.instance_id",
            "identifiers must be nonzero and strictly increasing", index);
      }
      if (!ValidateMeshInstanceEntry(instance, index, validation)) {
        return validation;
      }
    }
  }

  previous_identifier = 0U;
  for (std::size_t index = 0U; index < descriptor.lights.size(); ++index) {
    const LightDescriptor &light = descriptor.lights[index];
    if (!ValidateIncreasingIdentifier(light.light_id, previous_identifier,
                                      index != 0U, "lights.light_id", index,
                                      validation)) {
      return validation;
    }
    previous_identifier = light.light_id;
    if (!IsKnownLightType(light.type)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "lights.type", "unknown light type",
                                       index);
    }
    if (!IsFinite(light.color_linear) || !IsFinite(light.intensity) ||
        !IsFinite(light.position) || !IsFinite(light.previous_position) ||
        !IsFinite(light.direction) || !IsFinite(light.previous_direction) ||
        !IsFinite(light.range) || !IsFinite(light.inner_cone_radians) ||
        !IsFinite(light.outer_cone_radians)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "lights.photometry",
          "all light numeric fields must be finite", index);
    }
    if (!IsCanonicalPhotometricColorLinear(light.color_linear) ||
        light.intensity < 0.0F ||
        light.range < 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.photometry",
          "light color must be canonical unit-luminance Rec.709/D65 and "
          "scalar photometry must be nonnegative",
          index);
    }
    if (!IsNormalized(light.direction) ||
        !IsNormalized(light.previous_direction)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.direction",
          "current and previous light directions must have unit length", index);
    }
    constexpr std::uint32_t kKnownShadowFlags =
        LIGHT_SHADOW_STATIC_GEOMETRY | LIGHT_SHADOW_DYNAMIC_GEOMETRY;
    if ((light.shadow_flags & ~kKnownShadowFlags) != 0U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.shadow_flags",
          "light contains unknown shadow flag bits", index);
    }
    if (light.type == LightType::DIRECTIONAL) {
      if (!IsCanonicalZero(light.position) ||
          !IsCanonicalZero(light.previous_position) || light.range != 0.0F ||
          light.inner_cone_radians != 0.0F ||
          light.outer_cone_radians != 0.0F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "lights.directional",
            "directional lights require canonical zero local-light fields",
            index);
      }
    } else if (light.type == LightType::POINT) {
      if (light.range <= 0.0F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "lights.range",
            "local light range must be positive", index);
      }
      if (!IsCanonicalPointDirection(light.direction) ||
          !IsCanonicalPointDirection(light.previous_direction) ||
          light.inner_cone_radians != 0.0F ||
          light.outer_cone_radians != 0.0F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "lights.point",
            "point lights require canonical orientation and zero cone fields",
            index);
      }
    } else if (light.range <= 0.0F || light.inner_cone_radians < 0.0F ||
               light.outer_cone_radians < light.inner_cone_radians ||
               light.outer_cone_radians > kHalfPi) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.cone",
          "spot range must be positive and cones must satisfy 0 <= inner <= "
          "outer <= pi/2",
          index);
    }
  }

  if (sky.enabled) {
    const auto sun = std::lower_bound(
        descriptor.lights.begin(), descriptor.lights.end(), sky.sun_light_id,
        [](const LightDescriptor &candidate, std::uint64_t light_id) {
          return candidate.light_id < light_id;
        });
    if (sun == descriptor.lights.end() || sun->light_id != sky.sun_light_id) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "environment.analytic_sky.sun_light_id",
          "analytic sky references a missing directional light");
    }
    if (sun->type != LightType::DIRECTIONAL) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_RESOURCE_KIND,
          "environment.analytic_sky.sun_light_id",
          "analytic sky sun identity must name a directional light");
    }
  }

  validation =
      ValidateReflectionProbeRuntimeSet(descriptor.reflection_probes);
  if (!validation) {
    validation.field = "reflection_probes." + validation.field;
    return validation;
  }

  previous_identifier = 0U;
  std::vector<std::uint64_t> updated_instance_ids;
  for (std::size_t index = 0U; index < descriptor.dynamic_mesh_updates.size();
       ++index) {
    const DynamicMeshUpdateDescriptor &update =
        descriptor.dynamic_mesh_updates[index];
    if (!ValidateIncreasingIdentifier(
            update.update_sequence, previous_identifier, index != 0U,
            "dynamic_mesh_updates.update_sequence", index, validation)) {
      return validation;
    }
    previous_identifier = update.update_sequence;
    if (update.instance_id == 0U || update.topology_revision == 0U ||
        update.deformation_revision == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          update.instance_id == 0U ? "dynamic_mesh_updates.instance_id"
                                   : "dynamic_mesh_updates.revision",
          "instance and geometry revisions must be nonzero", index);
    }
    if (!ValidateAssetReference(update.mesh, RenderAssetKind::MESH,
                                "dynamic_mesh_updates.mesh", index,
                                validation)) {
      return validation;
    }
    const auto instance = std::lower_bound(
        descriptor.mesh_instances.begin(), descriptor.mesh_instances.end(),
        update.instance_id,
        [](const MeshInstanceDescriptor &candidate, std::uint64_t id) {
          return candidate.instance_id < id;
        });
    if (instance == descriptor.mesh_instances.end() ||
        instance->instance_id != update.instance_id) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "dynamic_mesh_updates.instance_id",
          "dynamic mesh update references a missing instance", index);
    }
    if (instance->mesh != update.mesh) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_ASSET_REFERENCE,
          "dynamic_mesh_updates.mesh",
          "dynamic mesh asset differs from the referenced instance", index);
    }
    if (instance->topology_revision != update.topology_revision ||
        instance->deformation_revision != update.deformation_revision) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "dynamic_mesh_updates.revision",
          "dynamic update revisions must match the referenced instance", index);
    }
    if (update.deformation_revision == 1U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "dynamic_mesh_updates.deformation_revision",
          "base deformation revision one uses mesh resource contents", index);
    }
    if (std::find(updated_instance_ids.begin(), updated_instance_ids.end(),
                  update.instance_id) != updated_instance_ids.end()) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "dynamic_mesh_updates.instance_id",
          "version 1 allows exactly one full update per deformed instance",
          index);
    }
    updated_instance_ids.push_back(update.instance_id);
    if (update.positions.empty()) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "dynamic_mesh_updates.positions",
          "dynamic mesh update requires positions", index);
    }
    if (update.positions.size() > (std::numeric_limits<std::uint32_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_mesh_updates.positions",
          "dynamic vertex count exceeds 32-bit mesh indexing", index);
    }
    const std::size_t vertex_count = update.positions.size();
    if ((!update.normals.empty() && update.normals.size() != vertex_count) ||
        (!update.tangents.empty() && update.tangents.size() != vertex_count) ||
        (!update.velocities.empty() &&
         update.velocities.size() != vertex_count)) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "dynamic_mesh_updates.vertex_streams",
          "optional vertex streams must match the position count", index);
    }
    if (!ValidateFiniteVertices(update, index, validation)) {
      return validation;
    }
    if (!update.has_updated_bounds || !IsValid(update.updated_local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS,
          "dynamic_mesh_updates.updated_local_bounds",
          "full deformation state requires finite ordered updated bounds",
          index);
    }
    if (!EqualBounds(update.updated_local_bounds, instance->local_bounds)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS,
          "dynamic_mesh_updates.updated_local_bounds",
          "full update bounds must exactly equal the instance bounds", index);
    }
  }

  for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
       ++index) {
    const MeshInstanceDescriptor &instance = descriptor.mesh_instances[index];
    const bool has_update =
        std::find(updated_instance_ids.begin(), updated_instance_ids.end(),
                  instance.instance_id) != updated_instance_ids.end();
    if ((instance.deformation_revision > 1U) != has_update) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "mesh_instances.deformation_revision",
          "every non-base deformation revision requires one full update",
          index);
    }
  }

  previous_identifier = 0U;
  for (std::size_t index = 0U; index < descriptor.particle_events.size();
       ++index) {
    const ParticleEvent &event = descriptor.particle_events[index];
    if (!ValidateIncreasingIdentifier(
            event.event_id, previous_identifier, index != 0U,
            "particle_events.event_id", index, validation)) {
      return validation;
    }
    previous_identifier = event.event_id;
    if (event.emitter_id == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "particle_events.emitter_id",
          "emitter identifier must be nonzero", index);
    }
    if (!IsKnownParticleEffect(event.effect)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "particle_events.effect",
                                       "unknown particle effect", index);
    }
    if (!IsFinite(event.position) || !IsFinite(event.velocity) ||
        !IsFinite(event.color_linear) || !IsFinite(event.size_meters) ||
        !IsFinite(event.lifetime_seconds) || !IsFinite(event.intensity)) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "particle_events.payload",
          "all particle numeric fields must be finite", index);
    }
    if (!IsNonNegative(Float3{event.color_linear.x, event.color_linear.y,
                              event.color_linear.z}) ||
        event.color_linear.w < 0.0F || event.color_linear.w > 1.0F ||
        event.size_meters <= 0.0F || event.lifetime_seconds <= 0.0F ||
        event.intensity < 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "particle_events.payload",
          "particle values must be finite and physically nonnegative", index);
    }
    if (event.emission_count == 0U) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "particle_events.emission_count",
          "particle event must emit at least one particle", index);
    }
  }

  return ValidationResult::Success();
}

} // namespace

ValidationResult
ValidateSceneSnapshotDescriptor(const SceneSnapshotDescriptor &descriptor) {
  return ValidateSceneSnapshotDescriptorInternal(descriptor, nullptr);
}

namespace {

/// One update per deformed instance, and far fewer instances, so
/// an identity-ordered index turns the per-instance lookup from a linear scan
/// of every update into a binary search. Descriptor validation guarantees at
/// most one update per instance but orders the vector by update sequence, so
/// the identity order is established here rather than assumed.
std::vector<const DynamicMeshUpdateDescriptor *>
BuildDynamicMeshUpdateIndex(const SceneSnapshotDescriptor &descriptor) {
  std::vector<const DynamicMeshUpdateDescriptor *> index;
  index.reserve(descriptor.dynamic_mesh_updates.size());
  for (const DynamicMeshUpdateDescriptor &update :
       descriptor.dynamic_mesh_updates) {
    index.push_back(&update);
  }
  std::sort(index.begin(), index.end(),
            [](const DynamicMeshUpdateDescriptor *lhs,
               const DynamicMeshUpdateDescriptor *rhs) {
              return lhs->instance_id < rhs->instance_id;
            });
  return index;
}

const DynamicMeshUpdateDescriptor *FindDynamicMeshUpdate(
    const std::vector<const DynamicMeshUpdateDescriptor *> &index,
    std::uint64_t instance_id) noexcept {
  const auto found = std::lower_bound(
      index.begin(), index.end(), instance_id,
      [](const DynamicMeshUpdateDescriptor *candidate, std::uint64_t id) {
        return candidate->instance_id < id;
      });
  return found == index.end() || (*found)->instance_id != instance_id
             ? nullptr
             : *found;
}

/// Exact, invocation-local memoization for repeated mesh/material pairs.
///
/// Large imported scenes commonly instantiate the same immutable section pair
/// thousands of times. Resolving both references through the ordered registry
/// and rechecking the six material bindings for every instance is redundant.
/// This direct-mapped cache is deliberately only an optimization: a collision
/// is an exact-reference miss and runs the full resolver/compatibility path.
/// Its borrowed descriptor pointers cannot outlive this validation call.
class ValidatedSceneAssetPairCache final {
public:
  explicit ValidatedSceneAssetPairCache(
      const ValidatedAssetCompatibilityAccess &validated_assets) noexcept
      : validated_assets_(validated_assets) {}

  [[nodiscard]] ValidationResult Resolve(
      const RenderAssetRegistry &registry,
      const RenderAssetReference &mesh_reference,
      const RenderAssetReference &material_reference,
      const MeshResourceDescriptor *&mesh,
      const MaterialDescriptor *&material) noexcept {
    Entry &entry = entries_[Index(mesh_reference, material_reference)];
    if (entry.occupied && entry.mesh_reference == mesh_reference &&
        entry.material_reference == material_reference) {
      mesh = entry.mesh;
      material = entry.material;
      return ValidationResult::Success();
    }

    mesh = registry.ResolveMesh(mesh_reference);
    material = registry.ResolveMaterial(material_reference);
    if (mesh == nullptr || material == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "mesh_instances.asset",
          "instance references a missing, stale, or tombstoned asset");
    }
    ValidationResult validation =
        Detail::ValidateMaterialMeshCompatibilityFromValidatedAssets(
            validated_assets_, *material, *mesh);
    if (!validation) {
      return validation;
    }

    entry.occupied = true;
    entry.mesh_reference = mesh_reference;
    entry.material_reference = material_reference;
    entry.mesh = mesh;
    entry.material = material;
    return ValidationResult::Success();
  }

private:
  static constexpr std::size_t kEntryCount = 512U;
  static_assert((kEntryCount & (kEntryCount - 1U)) == 0U,
                "scene asset pair cache size must be a power of two");

  struct Entry final {
    bool occupied = false;
    RenderAssetReference mesh_reference;
    RenderAssetReference material_reference;
    const MeshResourceDescriptor *mesh = nullptr;
    const MaterialDescriptor *material = nullptr;
  };

  [[nodiscard]] static std::size_t
  Index(const RenderAssetReference &mesh,
        const RenderAssetReference &material) noexcept {
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    const auto add = [&hash](std::uint64_t value) noexcept {
      hash ^= value;
      hash *= kPrime;
    };
    add(mesh.id.high());
    add(mesh.id.low());
    add(mesh.revision);
    add(material.id.high());
    add(material.id.low());
    add(material.revision);
    return static_cast<std::size_t>(hash) & (kEntryCount - 1U);
  }

  const ValidatedAssetCompatibilityAccess &validated_assets_;
  std::array<Entry, kEntryCount> entries_{};
};

} // namespace

ValidationResult ValidateSceneSnapshotAssets(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry) {
  ValidationResult validation = ValidateSceneSnapshotDescriptor(descriptor);
  if (!validation) {
    return validation;
  }
  if (descriptor.asset_registry_id != registry.registry_id()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "asset_registry_id",
        "scene references a different renderer-neutral asset registry");
  }
  if (descriptor.asset_sequence != registry.sequence()) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
        "scene requires a different asset registry sequence");
  }

  const ValidatedAssetCompatibilityAccess validated_assets;
  ValidatedSceneAssetPairCache asset_pair_cache(validated_assets);
  if (descriptor.environment.environment_texture.valid()) {
    const TextureResourceDescriptor *texture = registry.ResolveTexture(
        descriptor.environment.environment_texture);
    const SamplerResourceDescriptor *sampler = registry.ResolveSampler(
        descriptor.environment.environment_sampler);
    if (texture == nullptr || sampler == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "environment",
          "environment references a missing, stale, or tombstoned asset");
    }
    validation =
        Detail::ValidateEnvironmentTextureCompatibilityFromValidatedAssets(
            validated_assets, *texture, *sampler);
    if (!validation) {
      return validation;
    }
  }

  if (descriptor.hud_overlay.enabled) {
    const MaterialDescriptor *material =
        registry.ResolveMaterial(descriptor.hud_overlay.material);
    if (material == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "hud_overlay.material",
          "HUD overlay references a missing, stale, or tombstoned material");
    }
    if (material->model != MaterialModel::UNLIT ||
        material->base_color_transfer !=
            BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE ||
        material->blend_mode !=
            MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER ||
        material->alpha_test_mode != MaterialAlphaTestMode::DISABLED ||
        material->depth_write) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "hud_overlay.material",
          "HUD overlay requires an UNLIT display-domain premultiplied "
          "source-over material without alpha test or depth writes");
    }
    if (!material->base_color_texture.texture.valid() ||
        !material->base_color_texture.sampler.valid()) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "hud_overlay.material.base_color_texture",
          "HUD overlay material requires a bound base texture and sampler");
    }
    const TextureResourceDescriptor *texture =
        registry.ResolveTexture(material->base_color_texture.texture);
    const SamplerResourceDescriptor *sampler =
        registry.ResolveSampler(material->base_color_texture.sampler);
    if (texture == nullptr || sampler == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "hud_overlay.material.base_color_texture",
          "HUD overlay texture or sampler is missing, stale, or tombstoned");
    }
    if (texture->mip_levels.size() != 1U) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "hud_overlay.material.base_color_texture",
          "HUD overlay texture must carry exactly one mip level");
    }
  }

  const std::vector<const DynamicMeshUpdateDescriptor *> update_index =
      BuildDynamicMeshUpdateIndex(descriptor);
  for (std::size_t index = 0U; index < descriptor.mesh_instances.size();
       ++index) {
    const MeshInstanceDescriptor &instance = descriptor.mesh_instances[index];
    const MeshResourceDescriptor *mesh = nullptr;
    const MaterialDescriptor *material = nullptr;
    validation = asset_pair_cache.Resolve(registry, instance.mesh,
                                          instance.material, mesh, material);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }

    validation = Detail::ValidateMeshInstanceCompatibilityFromValidatedMesh(
        validated_assets, *mesh, instance,
        FindDynamicMeshUpdate(update_index, instance.instance_id));
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
  }

  return ValidationResult::Success();
}

ValidationResult
ValidateSceneSnapshotAssets(const SceneSnapshot &snapshot,
                            const RenderAssetRegistry &registry) {
  return ValidateSceneSnapshotAssets(snapshot.descriptor_, registry);
}

ValidationResult ValidateSceneSnapshotRetainedAssets(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    std::uint64_t expected_predecessor_snapshot_id) {
  const SceneSnapshotDescriptor &descriptor = snapshot.descriptor_;
  if (!snapshot.has_retained_instance_block_proof() ||
      expected_predecessor_snapshot_id == 0U ||
      snapshot.retained_instance_predecessor_snapshot_id_ !=
          expected_predecessor_snapshot_id ||
      snapshot.retained_instance_patched_indices_.size() !=
          snapshot.retained_instance_predecessor_ids_.size()) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "retained_block.predecessor",
        "retained asset validation requires the exact accepted predecessor");
  }
  if (descriptor.asset_registry_id != registry.registry_id()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "asset_registry_id",
        "scene references a different renderer-neutral asset registry");
  }
  if (descriptor.asset_sequence != registry.sequence()) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
        "scene requires a different asset registry sequence");
  }

  const ValidatedAssetCompatibilityAccess validated_assets;
  if (descriptor.environment.environment_texture.valid()) {
    const TextureResourceDescriptor *texture = registry.ResolveTexture(
        descriptor.environment.environment_texture);
    const SamplerResourceDescriptor *sampler = registry.ResolveSampler(
        descriptor.environment.environment_sampler);
    if (texture == nullptr || sampler == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "environment",
          "environment references a missing, stale, or tombstoned asset");
    }
    ValidationResult validation =
        Detail::ValidateEnvironmentTextureCompatibilityFromValidatedAssets(
            validated_assets, *texture, *sampler);
    if (!validation) {
      return validation;
    }
  }
  if (descriptor.hud_overlay.enabled) {
    const MaterialDescriptor *material =
        registry.ResolveMaterial(descriptor.hud_overlay.material);
    if (material == nullptr || material->model != MaterialModel::UNLIT ||
        material->base_color_transfer !=
            BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE ||
        material->blend_mode !=
            MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER ||
        material->alpha_test_mode != MaterialAlphaTestMode::DISABLED ||
        material->depth_write ||
        !material->base_color_texture.texture.valid() ||
        !material->base_color_texture.sampler.valid()) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "hud_overlay.material",
          "HUD overlay retained validation requires the exact admitted display-domain material");
    }
    const TextureResourceDescriptor *texture =
        registry.ResolveTexture(material->base_color_texture.texture);
    const SamplerResourceDescriptor *sampler =
        registry.ResolveSampler(material->base_color_texture.sampler);
    if (texture == nullptr || sampler == nullptr ||
        texture->mip_levels.size() != 1U) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "hud_overlay.material.base_color_texture",
          "HUD overlay texture/sampler retained state is stale or noncanonical");
    }
  }

  const std::vector<std::uint32_t> &patched =
      snapshot.retained_instance_patched_indices_;
  for (std::size_t patch = 0U; patch < patched.size(); ++patch) {
    if (patched[patch] >= descriptor.mesh_instances.size()) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "retained_block.patched_indices",
          "retained patch index is outside the immutable instance block",
          patch);
    }
  }
  for (const DynamicMeshUpdateDescriptor &update :
       descriptor.dynamic_mesh_updates) {
    const auto found = std::lower_bound(
        patched.begin(), patched.end(), update.instance_id,
        [&descriptor](std::uint32_t index, std::uint64_t instance_id) {
          return descriptor.mesh_instances[index].instance_id < instance_id;
        });
    if (found == patched.end() ||
        descriptor.mesh_instances[*found].instance_id != update.instance_id) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "retained_block.dynamic_mesh_updates",
          "every deformable update must be named by the retained patch proof");
    }
  }

  ValidatedSceneAssetPairCache asset_pair_cache(validated_assets);
  const std::vector<const DynamicMeshUpdateDescriptor *> update_index =
      BuildDynamicMeshUpdateIndex(descriptor);
  for (std::size_t patch = 0U; patch < patched.size(); ++patch) {
    const std::uint32_t instance_index = patched[patch];
    const MeshInstanceDescriptor &instance =
        descriptor.mesh_instances[instance_index];
    const MeshResourceDescriptor *mesh = nullptr;
    const MaterialDescriptor *material = nullptr;
    ValidationResult validation = asset_pair_cache.Resolve(
        registry, instance.mesh, instance.material, mesh, material);
    if (!validation) {
      validation.element_index = instance_index;
      return validation;
    }
    validation = Detail::ValidateMeshInstanceCompatibilityFromValidatedMesh(
        validated_assets, *mesh, instance,
        FindDynamicMeshUpdate(update_index, instance.instance_id));
    if (!validation) {
      validation.element_index = instance_index;
      return validation;
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateSceneSnapshotAssetsScoped(
    const SceneSnapshotDescriptor &descriptor,
    const RenderAssetRegistry &registry,
    const std::vector<std::uint64_t> &instance_ids) {
  if (descriptor.asset_registry_id != registry.registry_id()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "asset_registry_id",
        "scene references a different renderer-neutral asset registry");
  }
  if (descriptor.asset_sequence != registry.sequence()) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
        "scene requires a different asset registry sequence");
  }

  const ValidatedAssetCompatibilityAccess validated_assets;
  ValidatedSceneAssetPairCache asset_pair_cache(validated_assets);
  const std::vector<const DynamicMeshUpdateDescriptor *> update_index =
      BuildDynamicMeshUpdateIndex(descriptor);
  std::uint64_t previous_identifier = 0U;
  std::size_t instance_scan = 0U;
  for (std::size_t index = 0U; index < instance_ids.size(); ++index) {
    const std::uint64_t instance_id = instance_ids[index];
    if (instance_id == 0U ||
        (index != 0U && instance_id <= previous_identifier)) {
      return ValidationResult::Failure(
          instance_id == 0U ? ValidationCode::INVALID_IDENTIFIER
                            : ValidationCode::SEQUENCE_MISMATCH,
          "scoped_instances.instance_id",
          "scoped instance identities must be nonzero and strictly increasing",
          index);
    }
    previous_identifier = instance_id;
    // mesh_instances is strictly increasing by identity, so one shared cursor
    // resolves the whole sorted scope in a single pass.
    while (instance_scan < descriptor.mesh_instances.size() &&
           descriptor.mesh_instances[instance_scan].instance_id <
               instance_id) {
      ++instance_scan;
    }
    if (instance_scan >= descriptor.mesh_instances.size() ||
        descriptor.mesh_instances[instance_scan].instance_id != instance_id) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "scoped_instances.instance_id",
          "scoped instance identity is absent from this snapshot", index);
    }
    const MeshInstanceDescriptor &instance =
        descriptor.mesh_instances[instance_scan];
    const MeshResourceDescriptor *mesh = nullptr;
    const MaterialDescriptor *material = nullptr;
    ValidationResult validation = asset_pair_cache.Resolve(
        registry, instance.mesh, instance.material, mesh, material);
    if (!validation) {
      validation.element_index = instance_scan;
      return validation;
    }
    validation = Detail::ValidateMeshInstanceCompatibilityFromValidatedMesh(
        validated_assets, *mesh, instance,
        FindDynamicMeshUpdate(update_index, instance_id));
    if (!validation) {
      validation.element_index = instance_scan;
      return validation;
    }
  }

  return ValidationResult::Success();
}

ValidationResult ValidateSceneSnapshotAssetsScoped(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const std::vector<std::uint64_t> &instance_ids) {
  return ValidateSceneSnapshotAssetsScoped(snapshot.descriptor_, registry,
                                           instance_ids);
}

SceneSnapshotCreateResult
CreateSceneSnapshot(SceneSnapshotDescriptor descriptor) {
  SceneSnapshotCreateResult result;
  result.validation = ValidateSceneSnapshotDescriptor(descriptor);
  if (!result.validation) {
    return result;
  }
  result.snapshot = std::shared_ptr<const SceneSnapshot>(
      new SceneSnapshot(std::move(descriptor)));
  return result;
}

SceneSnapshotCreateResult CreateSceneSnapshotWithRetainedBlock(
    SceneSnapshotDescriptor descriptor,
    const std::shared_ptr<const SceneSnapshot> &previous,
    const std::vector<std::uint32_t> &patched_indices) {
  static_assert(std::is_trivially_copyable<MeshInstanceDescriptor>::value,
                "byte-identity attestation requires a trivially copyable "
                "instance descriptor");
  SceneSnapshotCreateResult result;
  if (previous == nullptr) {
    result.validation = ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "retained_block.previous",
        "a retained instance block requires the validated snapshot whose "
        "bytes it claims");
    return result;
  }
  const std::vector<MeshInstanceDescriptor> &prior =
      previous->mesh_instances();
  if (prior.size() != descriptor.mesh_instances.size()) {
    result.validation = ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "retained_block.mesh_instances",
        "a retained instance block must keep the previous instance count");
    return result;
  }

  // Prove the claim before anything trusts it: every entry outside
  // patched_indices must be byte-identical to the same position of an
  // already-validated immutable snapshot. Nothing can therefore enter a
  // snapshot without either fresh validation or byte-level proof.
  std::uint32_t previous_patched = 0U;
  std::size_t cursor = 0U;
  const auto attest = [&](std::size_t begin, std::size_t end) {
    return begin >= end ||
           std::memcmp(prior.data() + begin,
                       descriptor.mesh_instances.data() + begin,
                       (end - begin) * sizeof(MeshInstanceDescriptor)) == 0;
  };
  for (std::size_t entry = 0U; entry < patched_indices.size(); ++entry) {
    const std::uint32_t patched = patched_indices[entry];
    if (static_cast<std::size_t>(patched) >=
            descriptor.mesh_instances.size() ||
        (entry != 0U && patched <= previous_patched)) {
      result.validation = ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "retained_block.patched_indices",
          "patched instance indices must be in range and strictly increasing",
          entry);
      return result;
    }
    previous_patched = patched;
    if (!attest(cursor, static_cast<std::size_t>(patched))) {
      result.validation = ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "retained_block.attestation",
          "retained instance entries are not byte-identical to the snapshot "
          "they claim");
      return result;
    }
    cursor = static_cast<std::size_t>(patched) + 1U;
  }
  if (!attest(cursor, prior.size())) {
    result.validation = ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "retained_block.attestation",
        "retained instance entries are not byte-identical to the snapshot "
        "they claim");
    return result;
  }

  result.validation =
      ValidateSceneSnapshotDescriptorInternal(descriptor, &patched_indices);
  if (!result.validation) {
    return result;
  }
  std::vector<std::uint64_t> predecessor_ids;
  predecessor_ids.reserve(patched_indices.size());
  for (const std::uint32_t patched : patched_indices) {
    predecessor_ids.push_back(prior[patched].instance_id);
  }
  result.snapshot = std::shared_ptr<const SceneSnapshot>(
      new SceneSnapshot(std::move(descriptor), previous->snapshot_id(),
                        patched_indices, std::move(predecessor_ids)));
  return result;
}

} // namespace RoR::Render
