/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace RoR::Render {
namespace {

struct RequiredField final {
  Ogre14GraphicsSceneCaptureField field;
  const char *name;
};

constexpr std::array<RequiredField, 10U> kRequiredFields{{
    {Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY,
     "joined_buffer_atomicity"},
    {Ogre14GraphicsSceneCaptureField::SIMULATION_TICK,
     "simulation_tick"},
    {Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS,
     "simulation_time_seconds"},
    {Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS,
     "absolute_world_origin_meters"},
    {Ogre14GraphicsSceneCaptureField::ENVIRONMENT, "environment"},
    {Ogre14GraphicsSceneCaptureField::ASSETS, "assets"},
    {Ogre14GraphicsSceneCaptureField::STATIC_MESHES, "static_meshes"},
    {Ogre14GraphicsSceneCaptureField::LIGHTS, "lights"},
    {Ogre14GraphicsSceneCaptureField::REFLECTION_PROBES,
     "reflection_probes"},
    {Ogre14GraphicsSceneCaptureField::CAMERA, "camera"},
}};

ValidationResult Failure(ValidationCode code, const char *field,
                         std::string detail) {
  ValidationResult result;
  result.code = code;
  result.field = field != nullptr ? field : "";
  result.detail = std::move(detail);
  return result;
}

bool IsKnownProjection(Ogre14CameraProjectionKind projection) noexcept {
  switch (projection) {
  case Ogre14CameraProjectionKind::PERSPECTIVE:
  case Ogre14CameraProjectionKind::ORTHOGRAPHIC:
    return true;
  }
  return false;
}

bool IsKnownLightKind(Ogre14GraphicsSceneLightKind kind) noexcept {
  switch (kind) {
  case Ogre14GraphicsSceneLightKind::POINT:
  case Ogre14GraphicsSceneLightKind::DIRECTIONAL:
  case Ogre14GraphicsSceneLightKind::SPOT:
  case Ogre14GraphicsSceneLightKind::RECTANGLE:
    return true;
  }
  return false;
}

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
constexpr char kOgre14LightIdentityDomain[] = "ror.ogre14.light.name.v1";

void HashByte(std::uint64_t &hash, std::uint8_t byte) noexcept {
  hash ^= byte;
  hash *= kFnv1a64Prime;
}

} // namespace

const char *ToString(Ogre14GraphicsSceneCaptureField field) noexcept {
  for (const RequiredField &required : kRequiredFields) {
    if (required.field == field) {
      return required.name;
    }
  }
  return "invalid";
}

std::string DescribeMissingOgre14GraphicsSceneFields(
    std::uint32_t available_fields) {
  std::string result;
  for (const RequiredField &required : kRequiredFields) {
    if ((available_fields & Ogre14GraphicsSceneCaptureFieldBit(
                                required.field)) != 0U) {
      continue;
    }
    if (!result.empty()) {
      result += ", ";
    }
    result += required.name;
  }
  return result;
}

ValidationResult ValidateOgre14GraphicsSceneCapture(
    const Ogre14GraphicsSceneCapture &capture) {
  if (capture.version != kOgre14GraphicsSceneSourceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported OGRE 14 graphics-scene source version");
  }
  if ((capture.available_fields &
       ~kOgre14GraphicsSceneRequiredFields) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "available_fields",
        "OGRE 14 capture advertises unknown field bits");
  }
  const std::string missing =
      DescribeMissingOgre14GraphicsSceneFields(capture.available_fields);
  if (!missing.empty()) {
    const auto first_missing = std::find_if(
        kRequiredFields.begin(), kRequiredFields.end(),
        [&capture](const RequiredField &required) {
          return (capture.available_fields &
                  Ogre14GraphicsSceneCaptureFieldBit(required.field)) == 0U;
        });
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        first_missing != kRequiredFields.end() ? first_missing->name
                                               : "available_fields",
        "missing required OGRE 14 joined fields: " + missing);
  }
  if (capture.joined_buffer_epoch == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "joined_buffer_epoch",
        "joined buffer epoch must be nonzero");
  }
  return ValidationResult::Success();
}

Ogre14GraphicsSceneSource::Ogre14GraphicsSceneSource(
    IOgre14GraphicsSceneCaptureProvider &provider) noexcept
    : provider_(provider) {}

ValidationResult Ogre14GraphicsSceneSource::CaptureJoinedGraphicsFrame(
    GraphicsSceneFrameInput &frame) {
  try {
    Ogre14GraphicsSceneCapture capture;
    ValidationResult validation =
        provider_.CaptureOgre14GraphicsScene(capture);
    if (!validation) {
      return validation;
    }
    validation = ValidateOgre14GraphicsSceneCapture(capture);
    if (!validation) {
      return validation;
    }
    frame = std::move(capture.frame);
    return ValidationResult::Success();
  } catch (const std::exception &) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw an exception");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw a non-standard exception");
  }
}

ValidationResult BuildOgre14GraphicsSceneEnvironment(
    const Float3 &native_ambient_linear,
    SceneEnvironmentDescriptor &environment) {
  if (!IsFinite(native_ambient_linear)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "environment.ambient_radiance",
        "OGRE 14 ambient color must be finite");
  }
  if (!IsNonNegative(native_ambient_linear)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "environment.ambient_radiance",
        "OGRE 14 ambient color must be nonnegative");
  }

  SceneEnvironmentDescriptor candidate;
  candidate.ambient_radiance = {
      native_ambient_linear.x * kOgre14AmbientNativeUnitRadiance,
      native_ambient_linear.y * kOgre14AmbientNativeUnitRadiance,
      native_ambient_linear.z * kOgre14AmbientNativeUnitRadiance};
  candidate.environment_intensity = 1.0F;
  candidate.analytic_sky = {};
  candidate.exposure_compensation_ev = 0.0F;
  environment = candidate;
  return ValidationResult::Success();
}

ValidationResult Ogre14GraphicsSceneLightIdentityRegistry::
    RegisterDerivedIdentity(std::string_view exact_name,
                            std::uint64_t stable_id) {
  if (exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }
  if (stable_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.source_light_id",
        "derived OGRE 14 light identity must be nonzero");
  }

  const auto id_match = names_by_id_.find(stable_id);
  if (id_match != names_by_id_.end() && id_match->second != exact_name) {
    return ValidationResult::Failure(
        ValidationCode::DUPLICATE_IDENTIFIER, "lights.source_light_id",
        "distinct exact OGRE 14 light names collided on one stable identity");
  }
  const auto name_match = ids_by_name_.find(exact_name);
  if (name_match != ids_by_name_.end() && name_match->second != stable_id) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "lights.exact_name",
        "an exact OGRE 14 light name changed stable identity");
  }
  if (id_match != names_by_id_.end()) {
    return ValidationResult::Success();
  }

  auto inserted_name = names_by_id_.emplace(stable_id, exact_name);
  try {
    const auto inserted_id = ids_by_name_.emplace(exact_name, stable_id);
    if (!inserted_id.second) {
      names_by_id_.erase(inserted_name.first);
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "lights.exact_name",
          "an exact OGRE 14 light name changed stable identity");
    }
  } catch (...) {
    names_by_id_.erase(inserted_name.first);
    throw;
  }
  return ValidationResult::Success();
}

ValidationResult DeriveOgre14GraphicsSceneLightId(
    std::string_view exact_name, std::uint64_t &stable_id) {
  if (exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }

  std::uint64_t candidate = kFnv1a64OffsetBasis;
  for (std::size_t index = 0U;
       index + 1U < sizeof(kOgre14LightIdentityDomain); ++index) {
    HashByte(candidate,
             static_cast<std::uint8_t>(kOgre14LightIdentityDomain[index]));
  }
  HashByte(candidate, 0U);
  for (const char byte : exact_name) {
    HashByte(candidate,
             static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
  }
  if (candidate == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.source_light_id",
        "exact OGRE 14 light name hashed to the reserved zero identity");
  }
  stable_id = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneLight(
    const Ogre14GraphicsSceneLightCaptureInput &input,
    GraphicsSceneLightInput &light) {
  if (input.exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }
  if (!IsKnownLightKind(input.kind)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "lights.type",
        "unknown OGRE 14 light type");
  }
  if (input.kind == Ogre14GraphicsSceneLightKind::RECTANGLE) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "lights.type",
        "OGRE 14 rectangle lights have no portable scene-schema type");
  }
  if (!IsFinite(input.diffuse_linear) ||
      !IsFinite(input.specular_linear) || !IsFinite(input.power_scale) ||
      !IsFinite(input.derived_position) ||
      !IsFinite(input.derived_direction) ||
      !IsFinite(input.attenuation_range) ||
      !IsFinite(input.attenuation_constant) ||
      !IsFinite(input.attenuation_linear) ||
      !IsFinite(input.attenuation_quadratic) ||
      !IsFinite(input.inner_cone_radians) ||
      !IsFinite(input.outer_cone_radians) ||
      !IsFinite(input.spot_falloff)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "lights.native_state",
        "all captured OGRE 14 light values must be finite");
  }
  if (!IsNonNegative(input.diffuse_linear) ||
      !IsNonNegative(input.specular_linear) || input.power_scale < 0.0F ||
      input.attenuation_range < 0.0F ||
      input.attenuation_constant < 0.0F ||
      input.attenuation_linear < 0.0F ||
      input.attenuation_quadratic < 0.0F || input.spot_falloff < 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.native_state",
        "OGRE 14 light photometry and attenuation must be nonnegative");
  }

  GraphicsSceneLightInput candidate;
  ValidationResult identity = DeriveOgre14GraphicsSceneLightId(
      input.exact_name, candidate.source_light_id);
  if (!identity) {
    return identity;
  }
  if (!NormalizePhotometricColorLinear(input.diffuse_linear,
                                       candidate.color_linear)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.diffuse_linear",
        "OGRE 14 diffuse RGB must have positive finite Rec.709 luminance");
  }

  const double native_luminance =
      ComputeLinearSrgbRec709D65Luminance(input.diffuse_linear);
  const double calibrated_intensity =
      native_luminance * static_cast<double>(input.power_scale) *
      static_cast<double>(
          kOgre14LegacyDiffusePowerToCanonicalIntensity);
  if (!std::isfinite(calibrated_intensity) ||
      calibrated_intensity >
          static_cast<double>((std::numeric_limits<float>::max)())) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.intensity",
        "calibrated OGRE 14 light intensity is not representable");
  }
  const float active_intensity = static_cast<float>(calibrated_intensity);
  if (calibrated_intensity > 0.0 && active_intensity == 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.intensity",
        "calibrated OGRE 14 light intensity underflows binary32");
  }
  candidate.intensity = input.visible ? active_intensity : 0.0F;
  candidate.shadow_flags = input.visible && input.casts_shadows
                               ? LIGHT_SHADOW_DEFAULT_FLAGS
                               : 0U;

  switch (input.kind) {
  case Ogre14GraphicsSceneLightKind::DIRECTIONAL:
    if (!IsNormalized(input.derived_direction)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.direction",
          "OGRE 14 directional-light direction must be unit length");
    }
    candidate.type = LightType::DIRECTIONAL;
    candidate.position = {};
    candidate.direction = input.derived_direction;
    candidate.range = 0.0F;
    candidate.inner_cone_radians = 0.0F;
    candidate.outer_cone_radians = 0.0F;
    break;
  case Ogre14GraphicsSceneLightKind::POINT:
    if (!(input.attenuation_range > 0.0F) ||
        !(input.attenuation_constant > 0.0F ||
          input.attenuation_linear > 0.0F ||
          input.attenuation_quadratic > 0.0F)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.range",
          "OGRE 14 local-light range and attenuation denominator must be "
          "positive");
    }
    candidate.type = LightType::POINT;
    candidate.position = input.derived_position;
    candidate.direction = {0.0F, -1.0F, 0.0F};
    candidate.range = input.attenuation_range;
    candidate.inner_cone_radians = 0.0F;
    candidate.outer_cone_radians = 0.0F;
    break;
  case Ogre14GraphicsSceneLightKind::SPOT: {
    constexpr float kPi = 3.14159265358979323846F;
    if (!(input.attenuation_range > 0.0F) ||
        !(input.attenuation_constant > 0.0F ||
          input.attenuation_linear > 0.0F ||
          input.attenuation_quadratic > 0.0F) ||
        !IsNormalized(input.derived_direction) ||
        input.inner_cone_radians < 0.0F ||
        input.outer_cone_radians < input.inner_cone_radians ||
        input.outer_cone_radians > kPi) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.cone",
          "OGRE 14 spot range/direction/full cones are invalid");
    }
    candidate.type = LightType::SPOT;
    candidate.position = input.derived_position;
    candidate.direction = input.derived_direction;
    candidate.range = input.attenuation_range;
    candidate.inner_cone_radians = input.inner_cone_radians * 0.5F;
    candidate.outer_cone_radians = input.outer_cone_radians * 0.5F;
    break;
  }
  case Ogre14GraphicsSceneLightKind::RECTANGLE:
    break;
  }

  light = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneLights(
    const std::vector<Ogre14GraphicsSceneLightCaptureInput> &inputs,
    Ogre14GraphicsSceneLightIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneLightInput> &lights) {
  Ogre14GraphicsSceneLightIdentityRegistry candidate_registry =
      identity_registry;
  std::vector<GraphicsSceneLightInput> candidate_lights;
  candidate_lights.reserve(inputs.size());
  std::set<std::string, std::less<>> current_names;

  for (std::size_t index = 0U; index < inputs.size(); ++index) {
    const Ogre14GraphicsSceneLightCaptureInput &input = inputs[index];
    if (!current_names.emplace(input.exact_name).second) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER, "lights.exact_name",
          "complete OGRE 14 light inventory contains a duplicate exact name",
          index);
    }
    GraphicsSceneLightInput converted;
    ValidationResult validation =
        BuildOgre14GraphicsSceneLight(input, converted);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    validation = candidate_registry.RegisterDerivedIdentity(
        input.exact_name, converted.source_light_id);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    candidate_lights.push_back(converted);
  }

  std::sort(candidate_lights.begin(), candidate_lights.end(),
            [](const GraphicsSceneLightInput &lhs,
               const GraphicsSceneLightInput &rhs) {
              return lhs.source_light_id < rhs.source_light_id;
            });
  identity_registry = std::move(candidate_registry);
  lights = std::move(candidate_lights);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneCamera(
    const Ogre14CameraCaptureInput &input,
    GraphicsSceneCameraInput &camera) {
  if (!IsKnownProjection(input.projection)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "camera.projection",
        "unknown OGRE 14 camera projection kind");
  }
  if (input.view_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "camera.view_id",
        "main camera view identity must be nonzero");
  }
  if (input.width == 0U || input.height == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "camera.dimensions",
        "main camera viewport dimensions must be nonzero");
  }
  if (!IsFinite(input.view_from_render) || !IsFinite(input.left) ||
      !IsFinite(input.right) || !IsFinite(input.top) ||
      !IsFinite(input.bottom) || !IsFinite(input.near_plane) ||
      !IsFinite(input.far_plane) || !IsFinite(input.exposure)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "camera",
        "OGRE 14 camera values must be finite");
  }
  if (!(input.left < input.right) || !(input.bottom < input.top)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "camera.frustum_extents",
        "camera frustum extents must be strictly ordered");
  }
  if (!(input.near_plane > 0.0F) ||
      !(input.far_plane > input.near_plane) ||
      !(input.exposure > 0.0F) || input.visibility_mask == 0U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "camera",
        "camera clipping, exposure, and visibility must be positive");
  }

  GraphicsSceneCameraInput candidate;
  candidate.view_id = input.view_id;
  candidate.width = input.width;
  candidate.height = input.height;
  candidate.view_from_render = input.view_from_render;
  candidate.near_plane = input.near_plane;
  candidate.far_plane = input.far_plane;
  candidate.exposure = input.exposure;
  candidate.visibility_mask = input.visibility_mask;
  candidate.clip_from_view.elements.fill(0.0F);

  const float width = input.right - input.left;
  const float height = input.top - input.bottom;
  if (input.projection == Ogre14CameraProjectionKind::PERSPECTIVE) {
    candidate.clip_from_view.elements[0U] =
        2.0F * input.near_plane / width;
    candidate.clip_from_view.elements[5U] =
        2.0F * input.near_plane / height;
    candidate.clip_from_view.elements[8U] =
        (input.right + input.left) / width;
    candidate.clip_from_view.elements[9U] =
        (input.top + input.bottom) / height;
    candidate.clip_from_view.elements[10U] =
        input.far_plane / (input.near_plane - input.far_plane);
    candidate.clip_from_view.elements[11U] = -1.0F;
    candidate.clip_from_view.elements[14U] =
        input.near_plane * candidate.clip_from_view.elements[10U];
  } else {
    candidate.clip_from_view.elements[0U] = 2.0F / width;
    candidate.clip_from_view.elements[5U] = 2.0F / height;
    candidate.clip_from_view.elements[10U] =
        1.0F / (input.near_plane - input.far_plane);
    candidate.clip_from_view.elements[12U] =
        -(input.right + input.left) / width;
    candidate.clip_from_view.elements[13U] =
        -(input.top + input.bottom) / height;
    candidate.clip_from_view.elements[14U] =
        input.near_plane * candidate.clip_from_view.elements[10U];
    candidate.clip_from_view.elements[15U] = 1.0F;
  }

  CameraViewRequest validation_view;
  validation_view.view_id = candidate.view_id;
  validation_view.width = candidate.width;
  validation_view.height = candidate.height;
  validation_view.view_from_render = candidate.view_from_render;
  validation_view.clip_from_view = candidate.clip_from_view;
  validation_view.previous_view_from_render = candidate.view_from_render;
  validation_view.previous_clip_from_view = candidate.clip_from_view;
  validation_view.temporal_jitter_pixels =
      candidate.temporal_jitter_pixels;
  validation_view.near_plane = candidate.near_plane;
  validation_view.far_plane = candidate.far_plane;
  validation_view.exposure = candidate.exposure;
  validation_view.visibility_mask = candidate.visibility_mask;
  const ValidationResult validation =
      ValidateCameraViewRequest(validation_view);
  if (!validation) {
    return validation;
  }
  camera = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
