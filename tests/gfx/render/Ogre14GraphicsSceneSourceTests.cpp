/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"
#include "ogrenext/OgreNextN1Policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

template <typename T>
bool SameSharedOwner(const std::shared_ptr<const T> &lhs,
                     const std::shared_ptr<const T> &rhs) noexcept {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

RoR::Render::Matrix4x4 Translation(float x, float y = 0.0F,
                                   float z = 0.0F) {
  RoR::Render::Matrix4x4 transform;
  transform.elements[12U] = x;
  transform.elements[13U] = y;
  transform.elements[14U] = z;
  return transform;
}

RoR::Render::Ogre14GraphicsSceneCpuMeshSectionInput MakeCpuTriangle(
    const char *name = "city.mesh/submesh/0", bool reverse_winding = false) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneCpuMeshSectionInput input;
  input.debug_name = name;
  input.index_format = MeshIndexFormat::UINT16;
  input.topology_revision = 7U;
  input.reverse_winding = reverse_winding;
  input.positions = {{-1.0F, 2.0F, 3.0F}, {4.0F, -5.0F, 6.0F},
                     {0.0F, 1.0F, -2.0F}};
  input.normals = {{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F},
                   {0.0F, 0.0F, 1.0F}};
  input.texture_coordinates_0 = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {0.25F, 1.0F}};
  input.colors = {{1.0F, 0.0F, 0.0F, 1.0F},
                  {0.0F, 1.0F, 0.0F, 0.5F},
                  {0.0F, 0.0F, 1.0F, 0.25F}};
  input.indices = {0U, 1U, 2U};
  return input;
}

RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput MakeStaticMaterial(
    const char *name = "City/Concrete") {
  using namespace RoR::Render;
  Ogre14GraphicsSceneMaterialCaptureInput input;
  input.exact_resource_group = "General";
  input.exact_name = name;
  input.diffuse_linear = {0.25F, 0.5F, 0.75F, 1.0F};
  input.ambient_linear = {0.2F, 0.2F, 0.2F};
  input.specular_linear = {0.1F, 0.1F, 0.1F};
  input.emissive_linear = {0.05F, 0.1F, 0.15F};
  input.shininess = 30.0F;
  return input;
}

RoR::Render::Ogre14GraphicsSceneStaticSectionCaptureInput MakeStaticSection(
    std::uint64_t stable_object_id, std::uint32_t section_index,
    const char *entity_name, const char *material_name = "City/Concrete",
    bool reverse_winding = false) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneStaticSectionCaptureInput input;
  input.stable_object_id = stable_object_id;
  input.section_index = section_index;
  input.exact_entity_name = entity_name;
  input.mesh_identity.exact_resource_group = "General";
  input.mesh_identity.exact_mesh_name = "city.mesh";
  input.mesh_identity.submesh_index = section_index;
  input.mesh_identity.vertex_count = 3U;
  input.mesh_identity.index_count = 3U;
  input.mesh_identity.reverse_winding = reverse_winding;
  input.material = MakeStaticMaterial(material_name);
  input.material.cull =
      reverse_winding ? Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE
                      : Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  std::shared_ptr<const RenderAssetPayload> payload;
  const ValidationResult validation =
      BuildOgre14GraphicsSceneStaticMeshPayload(
          MakeCpuTriangle(entity_name, reverse_winding), payload);
  Require(validation.ok(), "static-section fixture mesh was rejected");
  input.mesh_payload = std::move(payload);
  input.render_from_object =
      Translation(static_cast<float>(stable_object_id), 2.0F, -3.0F);
  return input;
}

std::shared_ptr<const RoR::Render::Ogre14GraphicsSceneJoinedDynamicState>
MakeJoinedDynamicState(float x_offset = 0.0F) {
  using namespace RoR::Render;
  auto state = std::make_shared<Ogre14GraphicsSceneJoinedDynamicState>();
  state->topology_revision = 7U;
  state->positions = {{-1.0F + x_offset, 2.0F, 3.0F},
                      {4.0F + x_offset, -5.0F, 6.0F},
                      {x_offset, 1.0F, -2.0F}};
  state->normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  state->updated_local_bounds.minimum = {-1.0F + x_offset, -5.0F, -2.0F};
  state->updated_local_bounds.maximum = {4.0F + x_offset, 2.0F, 6.0F};
  return state;
}

RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput MakeDynamicSection(
    std::int64_t actor_id = 41,
    RoR::Render::Ogre14GraphicsSceneDynamicComponentKind kind =
        RoR::Render::Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY,
    std::uint32_t component_id = 2U, std::uint32_t section_index = 0U,
    float x_offset = 0.0F) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionCaptureInput input;
  input.identity.actor_instance_id = actor_id;
  input.identity.component_kind = kind;
  input.identity.component_id = component_id;
  input.identity.section_index = section_index;
  input.exact_entity_name = "actor-41-flexbody-2";
  input.material = MakeStaticMaterial("Vehicle/Paint");
  Ogre14GraphicsSceneCpuMeshSectionInput mesh =
      MakeCpuTriangle("actor-41-flexbody-2");
  const ValidationResult validation =
      BuildOgre14GraphicsSceneDynamicMeshPayload(mesh, input.mesh_payload);
  Require(validation.ok(), "dynamic-section fixture mesh was rejected");
  input.render_from_object = Translation(10.0F, 2.0F, -3.0F);
  input.state = MakeJoinedDynamicState(x_offset);
  return input;
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
  capture.post_update_scene_epoch = 7U;
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
              "static_meshes, lights, reflection_probes, "
              "post_update_scene_atomicity, dynamic_meshes",
          "complete missing-field detail changed");
  Require(output.simulation_tick == 999U &&
              output.simulation_time_seconds == 3.0,
          "incomplete capture modified caller output");
}

void TestEveryRequiredFieldHasStableDiagnosticIdentity() {
  using namespace RoR::Render;
  const std::array<Ogre14GraphicsSceneCaptureField, 12U> fields{{
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
      Ogre14GraphicsSceneCaptureField::POST_UPDATE_SCENE_ATOMICITY,
      Ogre14GraphicsSceneCaptureField::DYNAMIC_MESHES,
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

  capture = MakeCompleteCapture();
  capture.post_update_scene_epoch = 0U;
  const ValidationResult incomplete_update =
      ValidateOgre14GraphicsSceneCapture(capture);
  Require(!incomplete_update &&
              incomplete_update.code == ValidationCode::SEQUENCE_MISMATCH &&
              incomplete_update.field == "post_update_scene_epoch",
          "missing post-UpdateScene completion epoch was accepted");

  capture = MakeCompleteCapture();
  ++capture.post_update_scene_epoch;
  const ValidationResult mismatched_update =
      ValidateOgre14GraphicsSceneCapture(capture);
  Require(!mismatched_update &&
              mismatched_update.code == ValidationCode::SEQUENCE_MISMATCH &&
              mismatched_update.field == "post_update_scene_epoch",
          "mismatched joined/update epochs were accepted");
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

void TestStaticIdentityDerivationAndCollisionAudit() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneMeshAssetIdentity mesh_identity;
  mesh_identity.exact_resource_group = "General";
  mesh_identity.exact_mesh_name = "city.mesh";
  mesh_identity.submesh_index = 2U;
  mesh_identity.vertex_start = 4U;
  mesh_identity.vertex_count = 3U;
  mesh_identity.index_start = 6U;
  mesh_identity.index_count = 3U;

  std::uint64_t mesh_id = 77U;
  ValidationResult result =
      DeriveOgre14GraphicsSceneMeshAssetId(mesh_identity, mesh_id);
  Require(result.ok() && mesh_id == 0x36b00c07cc5e5fbeULL,
          "exact mesh resource/range identity changed");

  std::uint64_t material_id = 0U;
  result = DeriveOgre14GraphicsSceneMaterialAssetId(
      "General", "City/Glass", material_id);
  Require(result.ok() && material_id == 0xd4863bfd0c205e1fULL,
          "exact material resource identity changed");

  std::uint64_t object_id = 0U;
  result = DeriveOgre14GraphicsSceneStaticSectionId(41U, 2U, object_id);
  Require(result.ok() && object_id == 0x28919f5212bdc362ULL,
          "manager-object/section identity changed");

  const std::uint64_t accepted_mesh_id = mesh_id;
  mesh_identity.exact_mesh_name.clear();
  result = DeriveOgre14GraphicsSceneMeshAssetId(mesh_identity, mesh_id);
  Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
              mesh_id == accepted_mesh_id,
          "invalid mesh identity modified caller output");

  mesh_identity.exact_mesh_name = "city.mesh";
  mesh_identity.reverse_winding = true;
  std::uint64_t reversed_id = 0U;
  Require(DeriveOgre14GraphicsSceneMeshAssetId(mesh_identity, reversed_id)
              .ok() &&
              reversed_id != accepted_mesh_id,
          "winding conversion was omitted from mesh asset identity");

  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  Require(registry.RegisterDerivedAssetIdentity("first", 41U).ok() &&
              registry.asset_identity_count() == 1U,
          "static asset registry rejected first exact mapping");
  result = registry.RegisterDerivedAssetIdentity("second", 41U);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              registry.asset_identity_count() == 1U,
          "static asset ID collision was accepted or mutated registry");
  Require(registry.RegisterDerivedObjectIdentity("object", 51U).ok() &&
              registry.object_identity_count() == 1U,
          "static object registry rejected first exact mapping");
  result = registry.RegisterDerivedObjectIdentity("object", 52U);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              registry.object_identity_count() == 1U,
          "static object exact key changed identity");
}

void TestStaticMeshPayloadPreservesBasisUvAndTightBounds() {
  using namespace RoR::Render;
  const Ogre14GraphicsSceneCpuMeshSectionInput input = MakeCpuTriangle();
  std::shared_ptr<const RenderAssetPayload> payload;
  ValidationResult result =
      BuildOgre14GraphicsSceneStaticMeshPayload(input, payload);
  Require(result.ok() && payload != nullptr &&
              RenderAssetPayloadKind(*payload) == RenderAssetKind::MESH,
          "valid CPU static mesh was rejected");
  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(*payload);
  Require(mesh.index_format == MeshIndexFormat::UINT16 && !mesh.dynamic &&
              mesh.topology_revision == 7U &&
              mesh.local_bounds.minimum.x == -1.0F &&
              mesh.local_bounds.minimum.y == -5.0F &&
              mesh.local_bounds.minimum.z == -2.0F &&
              mesh.local_bounds.maximum.x == 4.0F &&
              mesh.local_bounds.maximum.y == 2.0F &&
              mesh.local_bounds.maximum.z == 6.0F,
          "CPU mesh format or tight local bounds changed");
  Require(mesh.positions == input.positions &&
              mesh.normals == input.normals &&
              mesh.texture_coordinates_0 == input.texture_coordinates_0 &&
              mesh.colors == input.colors &&
              mesh.indices == input.indices,
          "canonical basis, upper-left UV, color, or CCW stream changed");

  std::shared_ptr<const RenderAssetPayload> reversed;
  result = BuildOgre14GraphicsSceneStaticMeshPayload(
      MakeCpuTriangle("city.mesh/reversed", true), reversed);
  Require(result.ok() &&
              std::get<MeshResourceDescriptor>(*reversed).indices ==
                  std::vector<std::uint32_t>({0U, 2U, 1U}),
          "explicit reverse-winding conversion changed or was omitted");

  const std::shared_ptr<const RenderAssetPayload> accepted = payload;
  Ogre14GraphicsSceneCpuMeshSectionInput malformed = input;
  malformed.indices[2U] = 3U;
  result = BuildOgre14GraphicsSceneStaticMeshPayload(malformed, payload);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              SameSharedOwner(payload, accepted),
          "out-of-range CPU index was accepted or modified output owner");
}

void TestStaticMaterialFallbackIsExplicitAndTransactional() {
  using namespace RoR::Render;
  static_assert(kOgre14StaticMaterialFallbackVersion == 1U,
                "material compatibility fixture needs an explicit migration");
  Ogre14GraphicsSceneMaterialCaptureInput input =
      MakeStaticMaterial("City/Glass");
  input.alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL;
  input.alpha_reject_value = 128U;
  input.cull = Ogre14GraphicsSceneMaterialCull::NONE;

  MaterialDescriptor material;
  ValidationResult result =
      BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(result.ok() &&
              material.model == MaterialModel::PBR_METALLIC_ROUGHNESS &&
              material.alpha_mode == MaterialAlphaMode::MASK &&
              material.double_sided &&
              material.base_color_factor == input.diffuse_linear &&
              material.emissive_factor == input.emissive_linear &&
              Near(material.roughness_factor, 0.25F) &&
              Near(material.alpha_cutoff, 128.0F / 255.0F) &&
              !material.base_color_texture.texture.valid() &&
              !material.normal_texture.texture.valid(),
          "versioned factor-only material fallback changed");

  const MaterialDescriptor factor_only = material;
  input.pass_count = 2U;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "material.pass_count" &&
              material.debug_name == factor_only.debug_name,
          "additional authored pass was silently dropped or modified output");
  input.pass_count = 1U;
  input.texture_unit_count = 3U;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(!result && result.field == "material.texture_units" &&
              material.debug_name == factor_only.debug_name,
          "authored texture units were silently dropped or modified output");
  input.texture_unit_count = 0U;
  input.has_vertex_program = true;
  input.has_fragment_program = true;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(!result && result.field == "material.programs" &&
              material.debug_name == factor_only.debug_name,
          "authored shader programs were silently dropped or modified output");

  input = MakeStaticMaterial("City/Glass");
  input.lighting_enabled = false;
  input.blend = Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA;
  input.alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(result.ok() && material.model == MaterialModel::UNLIT &&
              material.alpha_mode == MaterialAlphaMode::BLEND &&
              material.emissive_factor == Float3{} &&
              material.roughness_factor == 1.0F,
          "unlit straight-alpha fallback changed");

  const MaterialDescriptor accepted = material;
  input.alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              material.debug_name == accepted.debug_name &&
              material.alpha_mode == accepted.alpha_mode,
          "unrepresentable blend/test combination modified output");

  input = MakeStaticMaterial();
  input.shininess = std::numeric_limits<float>::infinity();
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE,
          "non-finite legacy material factor was accepted");
}

void TestUnsupportedStaticGeometryFailsClosedInStableOrder() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneUnsupportedGeometry unsupported;
  unsupported.terrain = true;
  unsupported.procedural = true;
  ValidationResult result =
      ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result &&
              result.field == "static_meshes.unsupported.terrain",
          "terrain did not win deterministic unsupported-coverage order");

  unsupported = {};
  unsupported.procedural = true;
  result = ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result &&
              result.field == "static_meshes.unsupported.procedural",
          "procedural geometry lacks an exact fail-closed diagnostic");
  unsupported = {};
  unsupported.deformable = true;
  result = ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result &&
              result.field == "static_meshes.unsupported.deformable",
          "deformable geometry lacks an exact fail-closed diagnostic");
  unsupported = {};
  unsupported.paged = true;
  result = ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result && result.field == "static_meshes.unsupported.paged",
          "paged geometry lacks an exact fail-closed diagnostic");
  unsupported = {};
  unsupported.animated = true;
  result = ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result &&
              result.field == "static_meshes.unsupported.animated",
          "animated geometry lacks an exact fail-closed diagnostic");
  Require(ValidateOgre14GraphicsSceneStaticCoverage({}).ok(),
          "fully supported static coverage was rejected");
}

void TestStaticInventorySplitsDeduplicatesAndReusesOwners() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections{
      MakeStaticSection(9U, 1U, "CityBuilding"),
      MakeStaticSection(9U, 0U, "CityBuilding")};
  sections[0U].visible = false;
  sections[0U].casts_shadows = false;
  sections[0U].receives_shadows = false;
  sections[0U].visible_in_reflections = false;
  sections[1U].visibility_mask = 0x12345678U;

  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> instances;
  ValidationResult result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, instances);
  Require(result.ok() && assets.size() == 3U && instances.size() == 2U &&
              registry.asset_identity_count() == 3U &&
              registry.object_identity_count() == 2U,
          "multi-submesh inventory was not split/deduplicated exactly");
  Require(assets[0U].source_asset_id < assets[1U].source_asset_id &&
              assets[1U].source_asset_id < assets[2U].source_asset_id &&
              instances[0U].source_object_id <
                  instances[1U].source_object_id,
          "static assets or section instances lack canonical ordering");
  bool saw_hidden = false;
  bool saw_visible = false;
  for (const GraphicsSceneStaticMeshInput &instance : instances) {
    if (instance.visibility_mask == 0U) {
      saw_hidden = instance.flags == 0U;
    } else if (instance.visibility_mask == 0x12345678U) {
      saw_visible =
          instance.flags == MESH_INSTANCE_DEFAULT_FLAGS &&
          instance.render_from_object.elements[12U] == 9.0F;
    }
  }
  Require(saw_hidden && saw_visible,
          "visibility, shadow/reflection flags, or transform changed");

  const std::vector<GraphicsSceneAssetInput> first_assets = assets;
  sections = {MakeStaticSection(9U, 0U, "CityBuilding"),
              MakeStaticSection(9U, 1U, "CityBuilding")};
  result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, instances);
  Require(result.ok(), "equivalent reordered static frame was rejected");
  for (const GraphicsSceneAssetInput &asset : assets) {
    const auto prior = std::find_if(
        first_assets.begin(), first_assets.end(),
        [&asset](const GraphicsSceneAssetInput &candidate) {
          return candidate.source_asset_id == asset.source_asset_id;
        });
    Require(prior != first_assets.end() &&
                SameSharedOwner(prior->payload, asset.payload),
            "equivalent static asset did not reuse immutable owner");
  }
}

void TestStaticInventoryFailureAndLifecycleAreAtomic() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections{
      MakeStaticSection(17U, 0U, "Warehouse")};
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> instances;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              sections, registry, assets, instances)
              .ok(),
          "static lifecycle fixture was rejected");
  const std::vector<GraphicsSceneAssetInput> accepted_assets = assets;
  const std::vector<GraphicsSceneStaticMeshInput> accepted_instances =
      instances;
  const std::size_t accepted_asset_ids = registry.asset_identity_count();
  const std::size_t accepted_object_ids = registry.object_identity_count();

  Ogre14GraphicsSceneStaticSectionCaptureInput conflicting = sections[0U];
  std::shared_ptr<const RenderAssetPayload> conflicting_payload;
  Ogre14GraphicsSceneCpuMeshSectionInput changed = MakeCpuTriangle();
  changed.positions[0U].x = -2.0F;
  Require(BuildOgre14GraphicsSceneStaticMeshPayload(
              changed, conflicting_payload)
              .ok(),
          "conflicting static fixture payload was invalid");
  conflicting.mesh_payload = std::move(conflicting_payload);
  sections.push_back(std::move(conflicting));
  ValidationResult result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, instances);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              registry.asset_identity_count() == accepted_asset_ids &&
              registry.object_identity_count() == accepted_object_ids &&
              assets.size() == accepted_assets.size() &&
              instances.size() == accepted_instances.size() &&
              SameSharedOwner(assets.front().payload,
                              accepted_assets.front().payload),
          "conflicting same-frame asset changed committed inventory state");

  sections.resize(1U);
  sections.front().render_from_object.elements[0U] = -1.0F;
  result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, instances);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field ==
                  "static_meshes.render_from_object.mirrored" &&
              assets.size() == accepted_assets.size() &&
              instances.size() == accepted_instances.size(),
          "mirrored static transform was guessed or modified inventory");

  sections.front().render_from_object.elements[0U] = 1.0F;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {}, registry, assets, instances)
              .ok() &&
              assets.empty() && instances.empty(),
          "static removal did not commit an empty complete inventory");
  result = BuildOgre14GraphicsSceneStaticInventory(
      sections, registry, assets, instances);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              result.field.find("source_asset_id") != std::string::npos &&
              assets.empty() && instances.empty(),
          "removed static identity resurrected or modified empty inventory");
}

void TestConvertedStaticInventoryFeedsProducer() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections{
      MakeStaticSection(23U, 0U, "Bridge")};
  Require(BuildOgre14GraphicsSceneStaticInventory(
              sections, registry, provider.capture.frame.assets,
              provider.capture.frame.static_meshes)
              .ok(),
          "producer static inventory fixture failed conversion");

  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x5354415449434F47ULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult result =
      producer.ProduceJoinedFrame(source);
  Require(result.ok() &&
              result.production.scene_snapshot->mesh_instances().size() ==
                  1U &&
              result.production.asset_delta.has_value() &&
              result.production.asset_delta->mutations.size() == 2U,
          "converted complete static inventory was rejected by producer");
}

void TestDynamicIdentityPayloadRevisionAndLifecycle() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionIdentity identity;
  identity.actor_instance_id = 41;
  identity.component_kind = Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY;
  identity.component_id = 2U;
  identity.section_index = 1U;
  std::uint64_t mesh_id = 0U;
  std::uint64_t object_id = 0U;
  Require(DeriveOgre14GraphicsSceneDynamicMeshAssetId(identity, mesh_id).ok() &&
              DeriveOgre14GraphicsSceneDynamicSectionId(identity, object_id)
                  .ok() &&
              mesh_id != 0U && object_id != 0U && mesh_id != object_id,
          "dynamic mesh/object identities were not stable domain-separated IDs");
  const std::uint64_t flexbody_mesh_id = mesh_id;
  identity.component_kind =
      Ogre14GraphicsSceneDynamicComponentKind::FLEXMESH_WHEEL;
  Require(DeriveOgre14GraphicsSceneDynamicMeshAssetId(identity, mesh_id).ok() &&
              mesh_id != flexbody_mesh_id,
          "dynamic component kind was omitted from stable identity");
  identity.actor_instance_id = -1;
  Require(DeriveOgre14GraphicsSceneDynamicSectionId(identity, object_id).code ==
              ValidationCode::INVALID_IDENTIFIER,
          "negative actor identity was accepted");

  std::shared_ptr<const RenderAssetPayload> payload;
  Require(BuildOgre14GraphicsSceneDynamicMeshPayload(MakeCpuTriangle(), payload)
              .ok() &&
              std::get<MeshResourceDescriptor>(*payload).dynamic,
          "dynamic base payload did not preserve validated topology storage");

  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> inputs{
      MakeDynamicSection()};
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneDynamicMeshInput> meshes;
  ValidationResult result = BuildOgre14GraphicsSceneDynamicInventory(
      inputs, registry, assets, meshes);
  Require(result.ok() && registry.asset_identity_count() == 2U &&
              registry.object_identity_count() == 1U && assets.size() == 2U &&
              meshes.size() == 1U &&
              meshes.front().state->deformation_revision == 2U,
          "initial dynamic inventory did not own base assets and revision two");
  const std::shared_ptr<const GraphicsSceneDynamicMeshState> first_state =
      meshes.front().state;
  const auto first_mesh_asset = std::find_if(
      assets.begin(), assets.end(), [](const GraphicsSceneAssetInput &asset) {
        return RenderAssetPayloadKind(*asset.payload) == RenderAssetKind::MESH;
      });
  Require(first_mesh_asset != assets.end(),
          "dynamic inventory omitted its base mesh asset");
  const std::shared_ptr<const RenderAssetPayload> first_mesh_owner =
      first_mesh_asset->payload;

  inputs[0U].state = MakeJoinedDynamicState();
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  const auto stable_mesh_asset = std::find_if(
      assets.begin(), assets.end(), [](const GraphicsSceneAssetInput &asset) {
        return RenderAssetPayloadKind(*asset.payload) == RenderAssetKind::MESH;
      });
  Require(result.ok() && SameSharedOwner(first_state, meshes.front().state) &&
              stable_mesh_asset != assets.end() &&
              SameSharedOwner(first_mesh_owner, stable_mesh_asset->payload),
          "equivalent dynamic staging did not reuse immutable owners");

  inputs[0U].state = MakeJoinedDynamicState(0.25F);
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  Require(result.ok() && !SameSharedOwner(first_state, meshes.front().state) &&
              meshes.front().state->deformation_revision == 3U,
          "changed joined staging did not advance semantic revision exactly");
  const std::vector<GraphicsSceneAssetInput> accepted_assets = assets;
  const std::vector<GraphicsSceneDynamicMeshInput> accepted_meshes = meshes;
  const std::size_t accepted_object_count = registry.object_identity_count();

  inputs[0U].has_dynamic_vertex_colors = true;
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              registry.object_identity_count() == accepted_object_count &&
              assets.size() == accepted_assets.size() &&
              SameSharedOwner(meshes.front().state,
                              accepted_meshes.front().state),
          "unsupported dynamic colors mutated identity or output state");
  inputs[0U].has_dynamic_vertex_colors = false;

  result = BuildOgre14GraphicsSceneDynamicInventory({}, registry, assets,
                                                     meshes);
  Require(result.ok() && meshes.empty() && assets.size() == 2U,
          "dynamic removal did not tombstone object while retaining base assets");
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              meshes.empty() && assets.size() == 2U,
          "removed dynamic identity returned or modified retained output");
}

void TestDynamicSectionsPreserveTopologyAndMaterialBindings() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneDynamicSectionCaptureInput first =
      MakeDynamicSection(41, Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY,
                         2U, 0U);
  Ogre14GraphicsSceneDynamicSectionCaptureInput second =
      MakeDynamicSection(41, Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY,
                         2U, 1U);
  second.material = MakeStaticMaterial("Vehicle/Tire");
  const Ogre14GraphicsSceneCpuMeshSectionInput reversed =
      MakeCpuTriangle("actor-41-flexbody-2/section-1", true);
  Require(BuildOgre14GraphicsSceneDynamicMeshPayload(
              reversed, second.mesh_payload)
              .ok(),
          "second dynamic section topology fixture was rejected");

  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneDynamicMeshInput> meshes;
  Require(BuildOgre14GraphicsSceneDynamicInventory(
              {first, second}, registry, assets, meshes)
              .ok() &&
              meshes.size() == 2U && assets.size() == 4U,
          "multi-section deformable inventory was not preserved exactly");

  std::uint64_t first_object_id = 0U;
  std::uint64_t second_object_id = 0U;
  std::uint64_t first_mesh_id = 0U;
  std::uint64_t second_mesh_id = 0U;
  std::uint64_t first_material_id = 0U;
  std::uint64_t second_material_id = 0U;
  Require(DeriveOgre14GraphicsSceneDynamicSectionId(
              first.identity, first_object_id)
                  .ok() &&
              DeriveOgre14GraphicsSceneDynamicSectionId(
                  second.identity, second_object_id)
                  .ok() &&
              DeriveOgre14GraphicsSceneDynamicMeshAssetId(
                  first.identity, first_mesh_id)
                  .ok() &&
              DeriveOgre14GraphicsSceneDynamicMeshAssetId(
                  second.identity, second_mesh_id)
                  .ok() &&
              DeriveOgre14GraphicsSceneMaterialAssetId(
                  first.material.exact_resource_group,
                  first.material.exact_name, first_material_id)
                  .ok() &&
              DeriveOgre14GraphicsSceneMaterialAssetId(
                  second.material.exact_resource_group,
                  second.material.exact_name, second_material_id)
                  .ok(),
          "multi-section expected identities could not be derived");

  const auto find_object = [&meshes](std::uint64_t object_id) {
    return std::find_if(
        meshes.begin(), meshes.end(),
        [object_id](const GraphicsSceneDynamicMeshInput &mesh) {
          return mesh.source_object_id == object_id;
        });
  };
  const auto first_object = find_object(first_object_id);
  const auto second_object = find_object(second_object_id);
  const auto find_asset = [&assets](std::uint64_t asset_id) {
    return std::find_if(
        assets.begin(), assets.end(),
        [asset_id](const GraphicsSceneAssetInput &asset) {
          return asset.source_asset_id == asset_id;
        });
  };
  const auto first_asset = find_asset(first_mesh_id);
  const auto second_asset = find_asset(second_mesh_id);
  Require(first_object != meshes.end() && second_object != meshes.end() &&
              first_object->mesh_source_asset_id == first_mesh_id &&
              second_object->mesh_source_asset_id == second_mesh_id &&
              first_object->material_source_asset_id == first_material_id &&
              second_object->material_source_asset_id == second_material_id &&
              first_material_id != second_material_id &&
              first_asset != assets.end() && second_asset != assets.end() &&
              std::get<MeshResourceDescriptor>(*first_asset->payload).indices ==
                  std::vector<std::uint32_t>({0U, 1U, 2U}) &&
              std::get<MeshResourceDescriptor>(*second_asset->payload).indices ==
                  std::vector<std::uint32_t>({0U, 2U, 1U}),
          "dynamic sections lost an exact topology or material binding");
}

void TestConvertedDynamicInventoryFeedsProducer() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> inputs{
      MakeDynamicSection()};
  Require(BuildOgre14GraphicsSceneDynamicInventory(
              inputs, registry, provider.capture.frame.assets,
              provider.capture.frame.dynamic_meshes)
              .ok(),
          "producer dynamic inventory fixture failed conversion");

  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x44594E414D4F4731ULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult produced =
      producer.ProduceJoinedFrame(source);
  Require(produced.ok() &&
              produced.production.scene_snapshot->mesh_instances().size() ==
                  1U &&
              produced.production.scene_snapshot->dynamic_mesh_updates().size() ==
                  1U &&
              produced.production.scene_snapshot->dynamic_mesh_updates()
                      .front()
                      .deformation_revision == 2U,
          "converted dynamic inventory was rejected by scene producer");
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
  TestStaticIdentityDerivationAndCollisionAudit();
  TestStaticMeshPayloadPreservesBasisUvAndTightBounds();
  TestStaticMaterialFallbackIsExplicitAndTransactional();
  TestUnsupportedStaticGeometryFailsClosedInStableOrder();
  TestStaticInventorySplitsDeduplicatesAndReusesOwners();
  TestStaticInventoryFailureAndLifecycleAreAtomic();
  TestConvertedStaticInventoryFeedsProducer();
  TestDynamicIdentityPayloadRevisionAndLifecycle();
  TestDynamicSectionsPreserveTopologyAndMaterialBindings();
  TestConvertedDynamicInventoryFeedsProducer();
  TestPerspectiveAndOrthographicCameraConversion();
  TestCameraConversionRejectsGuesswork();
  return EXIT_SUCCESS;
}
