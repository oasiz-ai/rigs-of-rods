/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed OGRE 14 joined-scene adapter for the render contract.

#pragma once

#include "GraphicsSceneSnapshotProducer.h"

#include <cstdint>
#include <string>

namespace RoR::Render {

constexpr std::uint32_t kOgre14GraphicsSceneSourceVersion = 1U;

/// Every bit names state which must come from the same completed
/// GfxScene::BufferSimulationData() boundary. An adapter may expose a partial
/// capture for diagnostics, but IJoinedGraphicsSceneSource publishes only when
/// every required bit is present.
enum class Ogre14GraphicsSceneCaptureField : std::uint32_t {
  JOINED_BUFFER_ATOMICITY = 1U << 0U,
  SIMULATION_TICK = 1U << 1U,
  SIMULATION_TIME_SECONDS = 1U << 2U,
  ABSOLUTE_WORLD_ORIGIN_METERS = 1U << 3U,
  ENVIRONMENT = 1U << 4U,
  ASSETS = 1U << 5U,
  STATIC_MESHES = 1U << 6U,
  LIGHTS = 1U << 7U,
  REFLECTION_PROBES = 1U << 8U,
  CAMERA = 1U << 9U,
};

constexpr std::uint32_t Ogre14GraphicsSceneCaptureFieldBit(
    Ogre14GraphicsSceneCaptureField field) noexcept {
  return static_cast<std::uint32_t>(field);
}

constexpr std::uint32_t kOgre14GraphicsSceneRequiredFields =
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::SIMULATION_TICK) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ENVIRONMENT) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ASSETS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::STATIC_MESHES) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::LIGHTS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::REFLECTION_PROBES) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::CAMERA);

struct Ogre14GraphicsSceneCapture {
  std::uint32_t version = kOgre14GraphicsSceneSourceVersion;
  /// Nonzero generation of the completed BufferSimulationData() call.
  std::uint64_t joined_buffer_epoch = 0U;
  std::uint32_t available_fields = 0U;
  GraphicsSceneFrameInput frame;
};

/// Narrow production seam implemented by GfxScene. It creates one candidate
/// from graphics-owned state and copied simulation buffers only. Partial
/// candidates return success so the source can report every unavailable field
/// without inventing default scene content.
class IOgre14GraphicsSceneCaptureProvider {
public:
  virtual ~IOgre14GraphicsSceneCaptureProvider() = default;
  [[nodiscard]] virtual ValidationResult CaptureOgre14GraphicsScene(
      Ogre14GraphicsSceneCapture &capture) = 0;
};

/// Validates adapter metadata and reports the first missing field plus the
/// complete ordered missing-field list. It does not duplicate the semantic
/// GraphicsSceneFrameInput validation owned by GraphicsSceneSnapshotProducer.
[[nodiscard]] ValidationResult ValidateOgre14GraphicsSceneCapture(
    const Ogre14GraphicsSceneCapture &capture);
[[nodiscard]] std::string DescribeMissingOgre14GraphicsSceneFields(
    std::uint32_t available_fields);
[[nodiscard]] const char *ToString(
    Ogre14GraphicsSceneCaptureField field) noexcept;

/// Transactional IJoinedGraphicsSceneSource binding. Provider exceptions,
/// malformed metadata, and incomplete field sets leave `frame` untouched.
class Ogre14GraphicsSceneSource final : public IJoinedGraphicsSceneSource {
public:
  explicit Ogre14GraphicsSceneSource(
      IOgre14GraphicsSceneCaptureProvider &provider) noexcept;

  [[nodiscard]] ValidationResult CaptureJoinedGraphicsFrame(
      GraphicsSceneFrameInput &frame) override;

private:
  IOgre14GraphicsSceneCaptureProvider &provider_;
};

enum class Ogre14CameraProjectionKind : std::uint8_t {
  PERSPECTIVE = 0U,
  ORTHOGRAPHIC = 1U,
};

/// Renderer-neutral values read from one OGRE 14 Camera/Viewport. Perspective
/// extents are measured on the near plane; orthographic extents are world
/// units. Custom OGRE projection matrices are rejected by the live provider.
struct Ogre14CameraCaptureInput {
  std::uint64_t view_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  Matrix4x4 view_from_render;
  Ogre14CameraProjectionKind projection =
      Ogre14CameraProjectionKind::PERSPECTIVE;
  float left = 0.0F;
  float right = 0.0F;
  float top = 0.0F;
  float bottom = 0.0F;
  float near_plane = 0.0F;
  float far_plane = 0.0F;
  float exposure = 1.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
};

/// OGRE 14's ambient scene color is already consumed as a renderer-linear
/// multiplier. The bridge defines one native ambient unit as one canonical
/// radiance unit so Ogre-Next receives the same numeric linear RGB without a
/// display-gamma round trip or an unaudited exposure multiplier.
constexpr float kOgre14AmbientNativeUnitRadiance = 1.0F;

/// Converts the complete constant-ambient state supported by OGRE 14. The
/// legacy bridge has no compatible authored linear-float equirectangular
/// environment asset or scene-level exposure value; those optional fields
/// remain canonically absent and identity-valued. Visual sky geometry remains
/// part of the separate static asset/instance inventory. Failure leaves
/// `environment` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneEnvironment(
    const Float3 &native_ambient_linear,
    SceneEnvironmentDescriptor &environment);

/// Builds the canonical right-handed, [0,1]-depth camera contract without
/// consuming an API-specific OGRE projection matrix. Failure leaves `camera`
/// untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneCamera(
    const Ogre14CameraCaptureInput &input,
    GraphicsSceneCameraInput &camera);

} // namespace RoR::Render
