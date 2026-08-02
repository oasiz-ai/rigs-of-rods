/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "OGRE 14 graphics-scene source test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool Near(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= 1.0e-6F;
}

RoR::Render::Ogre14CameraCaptureInput MakeCameraInput() {
  RoR::Render::Ogre14CameraCaptureInput input;
  input.view_id = 1U;
  input.width = 1280U;
  input.height = 720U;
  input.left = -0.16F;
  input.right = 0.16F;
  input.top = 0.09F;
  input.bottom = -0.09F;
  input.near_plane = 0.1F;
  input.far_plane = 1000.0F;
  return input;
}

RoR::Render::Ogre14GraphicsSceneCapture MakeCompleteCapture() {
  RoR::Render::Ogre14GraphicsSceneCapture capture;
  capture.joined_buffer_epoch = 7U;
  capture.available_fields =
      RoR::Render::kOgre14GraphicsSceneRequiredFields;
  capture.frame.simulation_tick = 81U;
  capture.frame.simulation_time_seconds = 0.0405;
  const RoR::Render::ValidationResult camera_validation =
      RoR::Render::BuildOgre14GraphicsSceneCamera(
          MakeCameraInput(), capture.frame.camera);
  Require(camera_validation.ok(), "fixture camera was rejected");
  return capture;
}

class FixtureProvider final
    : public RoR::Render::IOgre14GraphicsSceneCaptureProvider {
public:
  enum class Behavior { RETURN_CAPTURE, RETURN_FAILURE, THROW_STANDARD,
                        THROW_UNKNOWN };

  [[nodiscard]] RoR::Render::ValidationResult CaptureOgre14GraphicsScene(
      RoR::Render::Ogre14GraphicsSceneCapture &output) override {
    ++calls;
    switch (behavior) {
    case Behavior::RETURN_CAPTURE:
      output = capture;
      return RoR::Render::ValidationResult::Success();
    case Behavior::RETURN_FAILURE:
      return RoR::Render::ValidationResult::Failure(
          RoR::Render::ValidationCode::MISSING_REFERENCE,
          "provider.fixture", "fixture capture is unavailable");
    case Behavior::THROW_STANDARD:
      throw std::runtime_error("fixture");
    case Behavior::THROW_UNKNOWN:
      throw 7;
    }
    return RoR::Render::ValidationResult::Failure(
        RoR::Render::ValidationCode::INVALID_ENUM, "provider.behavior",
        "unknown fixture behavior");
  }

  RoR::Render::Ogre14GraphicsSceneCapture capture = MakeCompleteCapture();
  Behavior behavior = Behavior::RETURN_CAPTURE;
  std::uint32_t calls = 0U;
};

void TestCompleteCaptureFeedsProducerOnce() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x524F525F4F473134ULL;
  GraphicsSceneSnapshotProducer producer(configuration);

  const GraphicsSceneSnapshotProduceResult result =
      producer.ProduceJoinedFrame(source);
  Require(result.ok(), "complete joined capture was rejected");
  Require(provider.calls == 1U, "provider was not called exactly once");
  Require(result.production.scene_snapshot->simulation_tick() == 81U,
          "simulation tick was not preserved");
  Require(result.production.scene_snapshot->mesh_instances().empty() &&
              result.production.scene_snapshot->lights().empty() &&
              result.production.scene_snapshot->reflection_probes().empty(),
          "complete empty fixture gained fabricated scene records");
  Require(result.production.camera.view_id == 1U &&
              result.production.camera.width == 1280U &&
              result.production.camera.height == 720U,
          "main camera identity or dimensions changed");
}

void TestMissingFieldsAreCompleteAndTransactional() {
  using namespace RoR::Render;
  FixtureProvider provider;
  provider.capture.available_fields =
      Ogre14GraphicsSceneCaptureFieldBit(
          Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY) |
      Ogre14GraphicsSceneCaptureFieldBit(
          Ogre14GraphicsSceneCaptureField::SIMULATION_TICK) |
      Ogre14GraphicsSceneCaptureFieldBit(
          Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS) |
      Ogre14GraphicsSceneCaptureFieldBit(
          Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS) |
      Ogre14GraphicsSceneCaptureFieldBit(
          Ogre14GraphicsSceneCaptureField::CAMERA);
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneFrameInput output;
  output.simulation_tick = 999U;
  output.simulation_time_seconds = 3.0;

  const ValidationResult result = source.CaptureJoinedGraphicsFrame(output);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "environment",
          "first missing field was not exact");
  Require(result.detail ==
              "missing required OGRE 14 joined fields: environment, assets, "
              "static_meshes, lights, reflection_probes",
          "complete missing-field detail changed");
  Require(output.simulation_tick == 999U &&
              output.simulation_time_seconds == 3.0,
          "incomplete capture modified caller output");
}

void TestEveryRequiredFieldHasStableDiagnosticIdentity() {
  using namespace RoR::Render;
  const std::array<Ogre14GraphicsSceneCaptureField, 10U> fields{{
      Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY,
      Ogre14GraphicsSceneCaptureField::SIMULATION_TICK,
      Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS,
      Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS,
      Ogre14GraphicsSceneCaptureField::ENVIRONMENT,
      Ogre14GraphicsSceneCaptureField::ASSETS,
      Ogre14GraphicsSceneCaptureField::STATIC_MESHES,
      Ogre14GraphicsSceneCaptureField::LIGHTS,
      Ogre14GraphicsSceneCaptureField::REFLECTION_PROBES,
      Ogre14GraphicsSceneCaptureField::CAMERA,
  }};
  for (const Ogre14GraphicsSceneCaptureField field : fields) {
    Ogre14GraphicsSceneCapture capture = MakeCompleteCapture();
    capture.available_fields &=
        ~Ogre14GraphicsSceneCaptureFieldBit(field);
    const ValidationResult result =
        ValidateOgre14GraphicsSceneCapture(capture);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
                result.field == ToString(field) &&
                result.detail.find(ToString(field)) != std::string::npos,
            "required field lacks a stable exact diagnostic");
  }
  Require(std::string(ToString(
              static_cast<Ogre14GraphicsSceneCaptureField>(1U << 31U))) ==
              "invalid",
          "unknown field acquired a diagnostic identity");
}

void TestMalformedMetadataFailsClosed() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneCapture capture = MakeCompleteCapture();
  capture.version = kOgre14GraphicsSceneSourceVersion + 1U;
  Require(ValidateOgre14GraphicsSceneCapture(capture).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "unsupported adapter version was accepted");

  capture = MakeCompleteCapture();
  capture.available_fields |= 1U << 31U;
  Require(ValidateOgre14GraphicsSceneCapture(capture).code ==
              ValidationCode::INVALID_ENUM,
          "unknown available-field bit was accepted");

  capture = MakeCompleteCapture();
  capture.joined_buffer_epoch = 0U;
  const ValidationResult epoch =
      ValidateOgre14GraphicsSceneCapture(capture);
  Require(!epoch && epoch.code == ValidationCode::INVALID_IDENTIFIER &&
              epoch.field == "joined_buffer_epoch",
          "zero joined-buffer epoch was accepted");
}

void TestProviderFailuresAndExceptionsDoNotEscape() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneFrameInput output;
  output.simulation_tick = 41U;

  provider.behavior = FixtureProvider::Behavior::RETURN_FAILURE;
  ValidationResult result = source.CaptureJoinedGraphicsFrame(output);
  Require(!result && result.field == "provider.fixture" &&
              output.simulation_tick == 41U,
          "provider failure was not propagated transactionally");

  provider.behavior = FixtureProvider::Behavior::THROW_STANDARD;
  result = source.CaptureJoinedGraphicsFrame(output);
  Require(!result && result.field == "joined_graphics_source" &&
              result.detail ==
                  "OGRE 14 capture provider threw an exception",
          "standard provider exception escaped or changed identity");

  provider.behavior = FixtureProvider::Behavior::THROW_UNKNOWN;
  result = source.CaptureJoinedGraphicsFrame(output);
  Require(!result && result.field == "joined_graphics_source" &&
              result.detail ==
                  "OGRE 14 capture provider threw a non-standard exception",
          "unknown provider exception escaped or changed identity");
}

void TestConstantEnvironmentConversionIsExactAndTransactional() {
  using namespace RoR::Render;
  SceneEnvironmentDescriptor environment;
  environment.ambient_radiance = {9.0F, 8.0F, 7.0F};
  environment.environment_intensity = 3.0F;
  environment.exposure_compensation_ev = 2.0F;

  ValidationResult result = BuildOgre14GraphicsSceneEnvironment(
      Float3{0.125F, 0.25F, 0.5F}, environment);
  Require(result.ok() &&
              environment.ambient_radiance.x == 0.125F &&
              environment.ambient_radiance.y == 0.25F &&
              environment.ambient_radiance.z == 0.5F &&
              environment.environment_intensity == 1.0F &&
              !environment.environment_texture.valid() &&
              !environment.environment_sampler.valid() &&
              !environment.analytic_sky.enabled &&
              environment.exposure_compensation_ev == 0.0F,
          "constant OGRE 14 ambient state was not converted exactly");

  const SceneEnvironmentDescriptor accepted = environment;
  result = BuildOgre14GraphicsSceneEnvironment(
      Float3{-0.1F, 0.2F, 0.3F}, environment);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "environment.ambient_radiance" &&
              environment.ambient_radiance.x ==
                  accepted.ambient_radiance.x,
          "negative ambient input was accepted or modified output");

  result = BuildOgre14GraphicsSceneEnvironment(
      Float3{0.1F, std::numeric_limits<float>::infinity(), 0.3F},
      environment);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE &&
              environment.ambient_radiance.y ==
                  accepted.ambient_radiance.y,
          "non-finite ambient input was accepted or modified output");
}

void TestPerspectiveAndOrthographicCameraConversion() {
  using namespace RoR::Render;
  Ogre14CameraCaptureInput input = MakeCameraInput();
  GraphicsSceneCameraInput camera;
  ValidationResult result = BuildOgre14GraphicsSceneCamera(input, camera);
  Require(result.ok(), "perspective camera conversion failed");
  Require(Near(camera.clip_from_view.elements[0U], 0.625F) &&
              Near(camera.clip_from_view.elements[5U],
                   1.111111164093017578125F) &&
              camera.clip_from_view.elements[8U] == 0.0F &&
              camera.clip_from_view.elements[9U] == 0.0F &&
              camera.clip_from_view.elements[11U] == -1.0F &&
              camera.clip_from_view.elements[15U] == 0.0F,
          "perspective matrix does not use canonical RH [0,1] depth");

  input.projection = Ogre14CameraProjectionKind::ORTHOGRAPHIC;
  input.left = -8.0F;
  input.right = 8.0F;
  input.bottom = -4.5F;
  input.top = 4.5F;
  result = BuildOgre14GraphicsSceneCamera(input, camera);
  Require(result.ok(), "orthographic camera conversion failed");
  Require(Near(camera.clip_from_view.elements[0U], 0.125F) &&
              Near(camera.clip_from_view.elements[5U],
                   0.22222222387790679931640625F) &&
              camera.clip_from_view.elements[12U] == 0.0F &&
              camera.clip_from_view.elements[13U] == 0.0F &&
              camera.clip_from_view.elements[15U] == 1.0F,
          "orthographic matrix does not use canonical RH [0,1] depth");
}

void TestCameraConversionRejectsGuesswork() {
  using namespace RoR::Render;
  Ogre14CameraCaptureInput input = MakeCameraInput();
  GraphicsSceneCameraInput output;
  output.view_id = 77U;

  input.far_plane = 0.0F; // OGRE's unlimited sentinel has no finite contract.
  ValidationResult result = BuildOgre14GraphicsSceneCamera(input, output);
  Require(!result && output.view_id == 77U,
          "unlimited far-plane sentinel was guessed or modified output");

  input = MakeCameraInput();
  input.left = input.right;
  result = BuildOgre14GraphicsSceneCamera(input, output);
  Require(!result && result.field == "camera.frustum_extents",
          "degenerate frustum was accepted");

  input = MakeCameraInput();
  input.view_from_render.elements[0U] = 2.0F;
  result = BuildOgre14GraphicsSceneCamera(input, output);
  Require(!result && result.field == "views.view_from_render",
          "non-rigid OGRE view transform was accepted");

  input = MakeCameraInput();
  input.projection = static_cast<Ogre14CameraProjectionKind>(255U);
  result = BuildOgre14GraphicsSceneCamera(input, output);
  Require(!result && result.code == ValidationCode::INVALID_ENUM,
          "unknown OGRE projection kind was accepted");
}

} // namespace

int main() {
  TestCompleteCaptureFeedsProducerOnce();
  TestMissingFieldsAreCompleteAndTransactional();
  TestEveryRequiredFieldHasStableDiagnosticIdentity();
  TestMalformedMetadataFailsClosed();
  TestProviderFailuresAndExceptionsDoNotEscape();
  TestConstantEnvironmentConversionIsExactAndTransactional();
  TestPerspectiveAndOrthographicCameraConversion();
  TestCameraConversionRejectsGuesswork();
  return EXIT_SUCCESS;
}
