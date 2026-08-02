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
