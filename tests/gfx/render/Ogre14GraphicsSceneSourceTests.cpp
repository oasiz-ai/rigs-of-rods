/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"
#include "ogrenext/OgreNextN1Policy.h"

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
  return std::fabs(lhs - rhs) <= 1.0e-5F;
}

RoR::Render::Ogre14GraphicsSceneLightCaptureInput MakeDirectionalLight(
    const char *name = "MainLight") {
  RoR::Render::Ogre14GraphicsSceneLightCaptureInput input;
  input.exact_name = name;
  input.kind =
      RoR::Render::Ogre14GraphicsSceneLightKind::DIRECTIONAL;
  input.diffuse_linear = {0.25F, 0.5F, 1.0F};
  input.power_scale = 3.0F;
  input.derived_direction = {0.0F, -1.0F, 0.0F};
  input.attenuation_range = 100000.0F;
  input.spot_falloff = 2.0F;
  return input;
}

void RequireLegacyDiffusePowerRecovered(
    const RoR::Render::Ogre14GraphicsSceneLightCaptureInput &native,
    const RoR::Render::GraphicsSceneLightInput &portable) {
  const float inverse_calibration =
      RoR::Render::kOgreNextRt4LuxToNativePowerScale;
  Require(Near(portable.color_linear.x * portable.intensity *
                   inverse_calibration,
               native.diffuse_linear.x * native.power_scale) &&
              Near(portable.color_linear.y * portable.intensity *
                       inverse_calibration,
                   native.diffuse_linear.y * native.power_scale) &&
              Near(portable.color_linear.z * portable.intensity *
                       inverse_calibration,
                   native.diffuse_linear.z * native.power_scale),
          "legacy diffuse*power was not reproduced at Ogre-Next scale");
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

void TestLightIdentityIsStableExactAndTransactional() {
  using namespace RoR::Render;
  std::uint64_t identity = 77U;
  ValidationResult result =
      DeriveOgre14GraphicsSceneLightId("MainLight", identity);
  Require(result.ok() && identity == 0x9cf8a4437c828d15ULL,
          "domain-separated exact-name identity changed");

  const std::uint64_t accepted = identity;
  result = DeriveOgre14GraphicsSceneLightId({}, identity);
  Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
              identity == accepted,
          "empty light name was accepted or modified identity output");

  std::uint64_t case_variant = 0U;
  Require(DeriveOgre14GraphicsSceneLightId("mainlight", case_variant).ok() &&
              case_variant != accepted,
          "exact name bytes were case-folded");

  Ogre14GraphicsSceneLightIdentityRegistry registry;
  Require(registry.RegisterDerivedIdentity("first", 41U).ok() &&
              registry.size() == 1U,
          "identity registry rejected its first exact mapping");
  result = registry.RegisterDerivedIdentity("second", 41U);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              registry.size() == 1U,
          "stable-ID collision was accepted or changed registry");
  result = registry.RegisterDerivedIdentity("first", 42U);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              registry.size() == 1U,
          "exact name changed identity or changed registry");
}

void TestDirectionalLightCalibrationAndInactiveIdentity() {
  using namespace RoR::Render;
  static_assert(kOgre14LegacyDiffusePowerToCanonicalIntensity *
                    kOgreNextRt4LuxToNativePowerScale ==
                1.0F,
                "OGRE 14 and Ogre-Next compatibility scales diverged");
  static_assert(kOgre14LightCompatibilityCalibrationVersion == 1U,
                "light calibration fixture needs an explicit migration");

  Ogre14GraphicsSceneLightCaptureInput input = MakeDirectionalLight();
  GraphicsSceneLightInput light;
  ValidationResult result = BuildOgre14GraphicsSceneLight(input, light);
  Require(result.ok() && light.type == LightType::DIRECTIONAL &&
              light.position.x == 0.0F && light.position.y == 0.0F &&
              light.position.z == 0.0F && light.range == 0.0F &&
              light.inner_cone_radians == 0.0F &&
              light.outer_cone_radians == 0.0F &&
              light.shadow_flags == LIGHT_SHADOW_DEFAULT_FLAGS,
          "directional light fields were not mapped canonically");
  RequireLegacyDiffusePowerRecovered(input, light);

  const std::uint64_t active_identity = light.source_light_id;
  input.visible = false;
  result = BuildOgre14GraphicsSceneLight(input, light);
  Require(result.ok() && light.source_light_id == active_identity &&
              light.intensity == 0.0F && light.shadow_flags == 0U,
          "inactive light churned identity or retained active output");

  input.visible = true;
  input.casts_shadows = false;
  result = BuildOgre14GraphicsSceneLight(input, light);
  Require(result.ok() && light.intensity > 0.0F &&
              light.shadow_flags == 0U,
          "authored shadow disable was not preserved independently");
}

void TestPointAndSpotGeometryMapping() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneLightCaptureInput point = MakeDirectionalLight("Point A");
  point.kind = Ogre14GraphicsSceneLightKind::POINT;
  point.derived_position = {12.0F, -3.0F, 4.5F};
  point.derived_direction = {1.0F, 0.0F, 0.0F};
  point.attenuation_range = 75.0F;
  GraphicsSceneLightInput light;
  ValidationResult result = BuildOgre14GraphicsSceneLight(point, light);
  Require(result.ok() && light.type == LightType::POINT &&
              light.position.x == 12.0F && light.position.y == -3.0F &&
              light.position.z == 4.5F && light.range == 75.0F &&
              light.direction.x == 0.0F && light.direction.y == -1.0F &&
              light.direction.z == 0.0F &&
              light.inner_cone_radians == 0.0F &&
              light.outer_cone_radians == 0.0F,
          "point-light position/range/canonical direction changed");
  RequireLegacyDiffusePowerRecovered(point, light);

  Ogre14GraphicsSceneLightCaptureInput spot = point;
  spot.exact_name = "Spot A";
  spot.kind = Ogre14GraphicsSceneLightKind::SPOT;
  spot.derived_direction = {0.0F, 0.0F, -1.0F};
  spot.inner_cone_radians = 0.6F;
  spot.outer_cone_radians = 1.2F;
  spot.spot_falloff = 3.0F;
  result = BuildOgre14GraphicsSceneLight(spot, light);
  Require(result.ok() && light.type == LightType::SPOT &&
              light.position.x == spot.derived_position.x &&
              light.direction.z == -1.0F && light.range == 75.0F &&
              Near(light.inner_cone_radians, 0.3F) &&
              Near(light.outer_cone_radians, 0.6F),
          "OGRE full spotlight cones were not mapped to portable half angles");
  RequireLegacyDiffusePowerRecovered(spot, light);
}

void TestLightInventoryIsAtomicAndCanonical() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneLightIdentityRegistry registry;
  std::vector<Ogre14GraphicsSceneLightCaptureInput> inputs{
      MakeDirectionalLight("Zulu"), MakeDirectionalLight("Alpha")};
  std::vector<GraphicsSceneLightInput> lights(1U);
  lights[0U].source_light_id = 999U;
  ValidationResult result =
      BuildOgre14GraphicsSceneLights(inputs, registry, lights);
  Require(result.ok() && registry.size() == 2U && lights.size() == 2U &&
              lights[0U].source_light_id < lights[1U].source_light_id,
          "complete light inventory was not registered and sorted atomically");

  const std::vector<GraphicsSceneLightInput> accepted = lights;
  const std::size_t accepted_registry_size = registry.size();
  inputs.push_back(inputs.front());
  result = BuildOgre14GraphicsSceneLights(inputs, registry, lights);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              result.element_index == 2U &&
              registry.size() == accepted_registry_size &&
              lights.size() == accepted.size() &&
              lights.front().source_light_id ==
                  accepted.front().source_light_id,
          "duplicate inventory modified output or identity registry");
}

void TestLightConversionRejectsUnrepresentableState() {
  using namespace RoR::Render;
  GraphicsSceneLightInput output;
  output.source_light_id = 987U;
  Ogre14GraphicsSceneLightCaptureInput input = MakeDirectionalLight();

  input.kind = Ogre14GraphicsSceneLightKind::RECTANGLE;
  ValidationResult result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              output.source_light_id == 987U,
          "rectangle light was accepted or modified output");

  input = MakeDirectionalLight();
  input.kind = static_cast<Ogre14GraphicsSceneLightKind>(255U);
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.code == ValidationCode::INVALID_ENUM,
          "unknown OGRE light kind was accepted");

  input = MakeDirectionalLight();
  input.diffuse_linear = {};
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.diffuse_linear",
          "black diffuse light acquired invented chromaticity");

  input = MakeDirectionalLight();
  input.power_scale = -1.0F;
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.native_state",
          "negative OGRE power was accepted");

  input = MakeDirectionalLight();
  input.power_scale = std::numeric_limits<float>::infinity();
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE,
          "non-finite OGRE power was accepted");

  input = MakeDirectionalLight();
  input.specular_linear.y =
      std::numeric_limits<float>::quiet_NaN();
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE,
          "non-finite omitted OGRE specular state was ignored");

  input = MakeDirectionalLight();
  input.derived_direction = {0.0F, -2.0F, 0.0F};
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.direction",
          "non-unit directional light was accepted");

  input = MakeDirectionalLight();
  input.kind = Ogre14GraphicsSceneLightKind::POINT;
  input.attenuation_range = 0.0F;
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.range",
          "zero local-light range was accepted");

  input = MakeDirectionalLight();
  input.kind = Ogre14GraphicsSceneLightKind::POINT;
  input.attenuation_range = 10.0F;
  input.attenuation_constant = 0.0F;
  input.attenuation_linear = 0.0F;
  input.attenuation_quadratic = 0.0F;
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.range",
          "zero OGRE attenuation denominator was accepted");

  input = MakeDirectionalLight();
  input.kind = Ogre14GraphicsSceneLightKind::SPOT;
  input.attenuation_range = 10.0F;
  input.inner_cone_radians = 1.0F;
  input.outer_cone_radians = 0.5F;
  result = BuildOgre14GraphicsSceneLight(input, output);
  Require(!result && result.field == "lights.cone" &&
              output.source_light_id == 987U,
          "reversed full spot cones were accepted or modified output");
}

void TestConvertedLightsFeedProducer() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneLightIdentityRegistry registry;
  std::vector<Ogre14GraphicsSceneLightCaptureInput> native_lights{
      MakeDirectionalLight("Sun"), MakeDirectionalLight("Fill")};
  native_lights[1U].visible = false;
  Require(BuildOgre14GraphicsSceneLights(
              native_lights, registry, provider.capture.frame.lights)
              .ok(),
          "producer fixture lights failed conversion");

  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x4C494748545F4F47ULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult result =
      producer.ProduceJoinedFrame(source);
  Require(result.ok() && result.production.scene_snapshot->lights().size() ==
                             2U,
          "converted complete light inventory was rejected by producer");
  const auto &published = result.production.scene_snapshot->lights();
  Require(published[0U].light_id < published[1U].light_id &&
              (published[0U].intensity == 0.0F ||
               published[1U].intensity == 0.0F),
          "producer changed canonical ordering or inactive light state");
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
  TestLightIdentityIsStableExactAndTransactional();
  TestDirectionalLightCalibrationAndInactiveIdentity();
  TestPointAndSpotGeometryMapping();
  TestLightInventoryIsAtomicAndCanonical();
  TestLightConversionRejectsUnrepresentableState();
  TestConvertedLightsFeedProducer();
  TestPerspectiveAndOrthographicCameraConversion();
  TestCameraConversionRejectsGuesswork();
  return EXIT_SUCCESS;
}
