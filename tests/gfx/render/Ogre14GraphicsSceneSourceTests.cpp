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
#include <optional>
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

double DoubleFromBits(std::uint64_t bits) {
  double value = 0.0;
  const volatile unsigned char *const source =
      reinterpret_cast<const volatile unsigned char *>(&bits);
  unsigned char *const destination =
      reinterpret_cast<unsigned char *>(&value);
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    destination[index] = source[index];
  }
  return value;
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

RoR::Render::Ogre14GraphicsSceneTerrainPageCaptureInput MakeTerrainPage(
    std::int32_t slot_x = 0, std::int32_t slot_y = 0) {
  using namespace RoR::Render;
  Ogre14GraphicsSceneTerrainPageCaptureInput input;
  input.identity.exact_resource_group = "TerrainCache";
  input.identity.exact_filename_prefix = "cityworld_ogre_140502";
  input.identity.exact_filename_extension = "mapbin";
  input.identity.exact_slot_filename =
      "cityworld_" + std::to_string(slot_x) + "_" +
      std::to_string(slot_y) + ".mapbin";
  input.identity.slot_x = slot_x;
  input.identity.slot_y = slot_y;
  input.size = 5U;
  input.minimum_batch_size = 3U;
  input.maximum_batch_size = 5U;
  input.lod_level_count = 2U;
  input.lod_levels_per_leaf = 2U;
  input.highest_lod_prepared = 0;
  input.highest_lod_loaded = 0;
  input.target_lod_level = 0;
  input.world_size = 4.0F;
  input.skirt_size = 1.0F;
  input.page_world_position = {
      static_cast<float>(slot_x) * input.world_size, 0.0F,
      static_cast<float>(slot_y) * -input.world_size};
  input.material = MakeStaticMaterial("Terrain/FactorOnly");

  const std::int32_t point_stride =
      static_cast<std::int32_t>(input.size - 1U);
  const auto height = [=](std::int32_t x, std::int32_t y) {
    const std::int32_t global_x = slot_x * point_stride + x;
    const std::int32_t global_y = slot_y * point_stride + y;
    return static_cast<float>(global_x + global_y) * 0.125F;
  };
  for (std::uint32_t y = 0U; y < input.size; ++y) {
    for (std::uint32_t x = 0U; x < input.size; ++x) {
      input.height_samples.push_back(
          height(static_cast<std::int32_t>(x),
                 static_cast<std::int32_t>(y)));
    }
  }
  const std::int32_t halo_side =
      static_cast<std::int32_t>(input.size + 2U);
  input.normal_neighbourhood_positions.reserve(
      static_cast<std::size_t>(halo_side * halo_side));
  const float base = input.world_size * -0.5F;
  for (std::int32_t y = -1; y <= static_cast<std::int32_t>(input.size);
       ++y) {
    for (std::int32_t x = -1; x <= static_cast<std::int32_t>(input.size);
         ++x) {
      input.normal_neighbourhood_positions.push_back(
          {static_cast<float>(x) + base, height(x, y),
           static_cast<float>(y) * -1.0F - base});
    }
  }
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
  void CommitOgre14GraphicsSceneCapture() noexcept override { ++commits; }
  void DiscardOgre14GraphicsSceneCapture() noexcept override { ++discards; }

  RoR::Render::Ogre14GraphicsSceneCapture capture = MakeCompleteCapture();
  Behavior behavior = Behavior::RETURN_CAPTURE;
  std::uint32_t calls = 0U;
  std::uint32_t commits = 0U;
  std::uint32_t discards = 0U;
};

class TransactionalDynamicProvider final
    : public RoR::Render::IOgre14GraphicsSceneCaptureProvider {
public:
  [[nodiscard]] RoR::Render::ValidationResult CaptureOgre14GraphicsScene(
      RoR::Render::Ogre14GraphicsSceneCapture &output) override {
    using namespace RoR::Render;
    if (pending_registry.has_value()) {
      return ValidationResult::Failure(
          ValidationCode::SEQUENCE_MISMATCH, "fixture.pending",
          "fixture capture was prepared twice");
    }
    Ogre14GraphicsSceneDynamicIdentityRegistry candidate = registry;
    Ogre14GraphicsSceneCapture capture = MakeCompleteCapture();
    const ValidationResult built = BuildOgre14GraphicsSceneDynamicInventory(
        {input}, candidate, capture.frame.assets,
        capture.frame.dynamic_meshes);
    if (!built) {
      return built;
    }
    if (reject_camera) {
      capture.frame.camera.width = 0U;
    }
    pending_registry.emplace(std::move(candidate));
    output = std::move(capture);
    return ValidationResult::Success();
  }

  void CommitOgre14GraphicsSceneCapture() noexcept override {
    if (!pending_registry.has_value()) {
      return;
    }
    registry = std::move(*pending_registry);
    pending_registry.reset();
    ++commits;
  }
  void DiscardOgre14GraphicsSceneCapture() noexcept override {
    if (pending_registry.has_value()) {
      pending_registry.reset();
      ++discards;
    }
  }

  RoR::Render::Ogre14GraphicsSceneDynamicSectionCaptureInput input =
      MakeDynamicSection();
  RoR::Render::Ogre14GraphicsSceneDynamicIdentityRegistry registry;
  std::optional<RoR::Render::Ogre14GraphicsSceneDynamicIdentityRegistry>
      pending_registry;
  bool reject_camera = true;
  std::uint32_t commits = 0U;
  std::uint32_t discards = 0U;
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
  Require(provider.commits == 1U && provider.discards == 0U,
          "producer acceptance did not commit provider state exactly once");
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

void TestPreparedCaptureRequiresExplicitResolution() {
  using namespace RoR::Render;
  FixtureProvider provider;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneFrameInput first;
  Require(source.CaptureJoinedGraphicsFrame(first).ok(),
          "complete provider frame could not be prepared");
  GraphicsSceneFrameInput second;
  const ValidationResult overlapping =
      source.CaptureJoinedGraphicsFrame(second);
  Require(!overlapping &&
              overlapping.code == ValidationCode::SEQUENCE_MISMATCH &&
              overlapping.field == "joined_graphics_source.pending_capture" &&
              provider.calls == 1U && provider.commits == 0U &&
              provider.discards == 0U,
          "overlapping source preparation was not rejected transactionally");

  source.DiscardJoinedGraphicsFrame();
  Require(provider.discards == 1U,
          "explicit source discard did not reach the provider");
  Require(source.CaptureJoinedGraphicsFrame(second).ok(),
          "source could not prepare after an explicit discard");
  source.CommitJoinedGraphicsFrame();
  Require(provider.calls == 2U && provider.commits == 1U &&
              provider.discards == 1U,
          "explicit source commit did not resolve the prepared provider state");
}

void TestRejectedCaptureDoesNotSkipDeformationRevision() {
  using namespace RoR::Render;
  TransactionalDynamicProvider provider;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x5452414E53414354ULL;
  GraphicsSceneSnapshotProducer producer(configuration);

  const GraphicsSceneSnapshotProduceResult rejected =
      producer.ProduceJoinedFrame(source);
  Require(!rejected && provider.commits == 0U && provider.discards == 1U &&
              provider.registry.object_identity_count() == 0U,
          "rejected producer frame committed dynamic source lineage");

  provider.reject_camera = false;
  provider.input.state = MakeJoinedDynamicState(0.25F);
  const GraphicsSceneSnapshotProduceResult accepted =
      producer.ProduceJoinedFrame(source);
  Require(accepted && provider.commits == 1U && provider.discards == 1U &&
              accepted.production.scene_snapshot->dynamic_mesh_updates()
                      .size() == 1U &&
              accepted.production.scene_snapshot->dynamic_mesh_updates()
                      .front()
                      .deformation_revision == 2U,
          "discarded deformation advanced past the producer's next revision");
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

void TestModernAnalyticSkyPolicyIsLiveMatchedAndTransactional() {
  using namespace RoR::Render;
  static_assert(kOgre14ModernAnalyticSkyPolicyVersion == 3U);
  GraphicsSceneLightInput sun;
  sun.source_light_id = 0xA51U;
  sun.type = LightType::DIRECTIONAL;
  sun.color_linear = {1.0F, 1.0F, 1.0F};
  sun.intensity = kOgre14LegacyDiffusePowerToCanonicalIntensity;
  sun.direction = {0.0F, -0.8F, -0.6F};

  SceneEnvironmentDescriptor environment;
  environment.ambient_radiance = {9.0F, 8.0F, 7.0F};
  ValidationResult result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.2F, 0.25F, 0.3F}, sun, 100.0, environment);
  Require(result.ok() && environment.analytic_sky.enabled &&
              environment.analytic_sky.sun_light_id == sun.source_light_id &&
              Near(environment.analytic_sky.zenith_radiance.x, 0.044F) &&
              Near(environment.analytic_sky.zenith_radiance.y, 0.10125F) &&
              Near(environment.analytic_sky.zenith_radiance.z, 0.2175F) &&
              Near(environment.analytic_sky.horizon_radiance.x, 0.135F) &&
              Near(environment.analytic_sky.horizon_radiance.y, 0.13875F) &&
              Near(environment.analytic_sky.horizon_radiance.z, 0.1425F) &&
              Near(environment.analytic_sky.ground_radiance.x, 0.03F) &&
              Near(environment.analytic_sky.ground_radiance.y, 0.0375F) &&
              Near(environment.analytic_sky.ground_radiance.z, 0.045F) &&
              Near(environment.analytic_sky.sun_disk_radiance.x, 24.0F) &&
              Near(environment.analytic_sky.sun_angular_radius_radians,
                   kOgre14ModernAnalyticSunAngularRadiusRadians) &&
              environment.exposure_compensation_ev ==
                  kOgre14ModernAnalyticSkyExposureCompensationEv,
          "policy-v2 daylight sky did not preserve its reviewed coefficients or live sun identity");
  // Policy v3 clouds: coverage tracks the same daylight term, the cloud
  // radiance is 0.5*horizon + 0.10*native_sun*daylight per channel, and the
  // phase is fmod(simulation_time * rate, 2*pi) - here 100 s * 0.004 = 0.4.
  Require(Near(environment.analytic_sky.cloud_coverage, 0.45F) &&
              Near(environment.analytic_sky.cloud_radiance.x, 0.1675F) &&
              Near(environment.analytic_sky.cloud_radiance.y, 0.169375F) &&
              Near(environment.analytic_sky.cloud_radiance.z, 0.17125F) &&
              Near(environment.analytic_sky.cloud_phase_radians, 0.4F),
          "policy-v3 daylight cloud layer did not preserve its reviewed coefficients");

  Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                          sun.color_linear),
          "smoke sun chromaticity fixture could not be normalized");
  result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.01F, 0.012F, 0.015F}, sun, 100.0, environment);
  Require(result.ok() &&
              environment.analytic_sky.zenith_radiance ==
                  Float3{0.0114296256F, 0.0236894581F,
                         0.0483114645F} &&
              environment.analytic_sky.horizon_radiance ==
                  Float3{0.0323878489F, 0.0372578688F,
                         0.0445614643F} &&
              environment.analytic_sky.ground_radiance ==
                  Float3{0.001500000013038516F, 0.0017999999690800905F,
                         0.0022499999031424522F} &&
              environment.analytic_sky.sun_disk_radiance ==
                  Float3{25.812335968017578F, 23.74734878540039F,
                         21.166114807128906F},
          "RT4 smoke sky no longer matches the exact policy-v2 report oracle");
  Require(Near(environment.analytic_sky.cloud_coverage, 0.45F) &&
              Near(environment.analytic_sky.cloud_radiance.x, 0.1237453F) &&
              Near(environment.analytic_sky.cloud_radiance.y, 0.1175762F) &&
              Near(environment.analytic_sky.cloud_radiance.z, 0.1104729F),
          "chromatic-sun cloud radiance no longer matches the policy-v3 mix");

  const SceneEnvironmentDescriptor accepted = environment;
  sun.type = LightType::POINT;
  result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.2F, 0.25F, 0.3F}, sun, 100.0, environment);
  Require(!result && result.code == ValidationCode::WRONG_RESOURCE_KIND &&
              environment.analytic_sky.sun_light_id ==
                  accepted.analytic_sky.sun_light_id &&
              environment.analytic_sky.zenith_radiance ==
                  accepted.analytic_sky.zenith_radiance,
          "non-directional sky authority was accepted or changed output");

  sun.type = LightType::DIRECTIONAL;
  result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.2F, 0.25F, 0.3F}, sun, -1.0, environment);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              environment.analytic_sky.cloud_phase_radians ==
                  accepted.analytic_sky.cloud_phase_radians,
          "negative simulation time was accepted or changed output");
  result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.2F, 0.25F, 0.3F}, sun,
      std::numeric_limits<double>::quiet_NaN(), environment);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE &&
              environment.analytic_sky.cloud_coverage ==
                  accepted.analytic_sky.cloud_coverage,
          "non-finite simulation time was accepted or changed output");

  sun.direction = {0.0F, 0.8F, -0.6F};
  result = BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
      {0.2F, 0.25F, 0.3F}, sun, 2000.0, environment);
  Require(result.ok() &&
              Near(environment.analytic_sky.zenith_radiance.x, 0.016F) &&
              Near(environment.analytic_sky.zenith_radiance.y, 0.025F) &&
              Near(environment.analytic_sky.zenith_radiance.z, 0.066F) &&
              environment.analytic_sky.sun_disk_radiance == Float3{},
          "policy-v1 below-horizon sun did not produce its exact night sky");
  // Night: coverage collapses to zero with the daylight term while the cloud
  // radiance stays the pure horizon mix; 2000 s * 0.004 = 8 wraps past 2*pi.
  Require(environment.analytic_sky.cloud_coverage == 0.0F &&
              Near(environment.analytic_sky.cloud_radiance.x, 0.012F) &&
              Near(environment.analytic_sky.cloud_radiance.y, 0.0125F) &&
              Near(environment.analytic_sky.cloud_radiance.z, 0.027F) &&
              Near(environment.analytic_sky.cloud_phase_radians,
                   1.71681469F),
          "policy-v3 night cloud state did not collapse deterministically");
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
  static_assert(kOgre14StaticMaterialFallbackVersion == 2U,
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
              material.blend_mode == MaterialBlendMode::REPLACE &&
              material.alpha_test_mode ==
                  MaterialAlphaTestMode::GREATER_EQUAL &&
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
  input.blend =
      Ogre14GraphicsSceneMaterialBlend::LEGACY_STRAIGHT_ALPHA;
  input.alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(result.ok() && material.model == MaterialModel::UNLIT &&
              material.blend_mode ==
                  MaterialBlendMode::LEGACY_STRAIGHT_ALPHA &&
              material.alpha_test_mode ==
                  MaterialAlphaTestMode::DISABLED &&
              material.emissive_factor == Float3{} &&
              material.roughness_factor == 1.0F,
          "unlit straight-alpha fallback changed");

  input.alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::GREATER;
  input.alpha_reject_value = 2U;
  input.depth_write = false;
  result = BuildOgre14GraphicsSceneMaterialFallback(input, material);
  Require(result.ok() &&
              material.blend_mode ==
                  MaterialBlendMode::LEGACY_STRAIGHT_ALPHA &&
              material.alpha_test_mode == MaterialAlphaTestMode::GREATER &&
              material.alpha_cutoff == 2.0F / 255.0F &&
              !material.depth_write,
          "independent legacy blend, GREATER alpha test, and depth-write "
          "state changed");

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
  unsupported.unadapted_deformable = true;
  result = ValidateOgre14GraphicsSceneStaticCoverage(unsupported);
  Require(!result &&
              result.field == "static_meshes.unsupported.deformable",
          "unadapted deformable geometry lacks an exact fail-closed diagnostic");
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

void TestTerrainIdentityAndExactStateKeyAreStable() {
  using namespace RoR::Render;
  static_assert(kOgre14TerrainCpuCaptureVersion == 1U,
                "terrain CPU fixture needs an explicit migration");
  Ogre14GraphicsSceneTerrainPageCaptureInput page = MakeTerrainPage();
  std::uint64_t page_id = 0U;
  ValidationResult result =
      DeriveOgre14GraphicsSceneTerrainPageId(page.identity, page_id);
  Require(result.ok() && page_id == 0xcfeffb04d3810f22ULL,
          "domain-separated exact terrain page identity changed");
  const std::uint64_t accepted_page_id = page_id;
  page.identity.exact_filename_prefix.clear();
  result = DeriveOgre14GraphicsSceneTerrainPageId(page.identity, page_id);
  Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
              page_id == accepted_page_id,
          "invalid terrain identity modified caller output");

  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  Require(registry.RegisterDerivedTerrainPageIdentity("first", 91U).ok() &&
              registry.terrain_page_identity_count() == 1U,
          "terrain page registry rejected its first exact mapping");
  result = registry.RegisterDerivedTerrainPageIdentity("second", 91U);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              registry.terrain_page_identity_count() == 1U,
          "terrain page ID collision was accepted or mutated registry");

  page = MakeTerrainPage();
  std::string state_key = "sentinel";
  result = BuildOgre14GraphicsSceneTerrainGeometryStateKey(page, state_key);
  Require(result.ok() && state_key.size() > page.height_samples.size(),
          "exact terrain geometry state key was not built");
  const std::string accepted_key = state_key;
  std::string equivalent_key;
  Require(BuildOgre14GraphicsSceneTerrainGeometryStateKey(
              MakeTerrainPage(), equivalent_key)
              .ok() &&
              equivalent_key == accepted_key,
          "equivalent terrain geometry changed its exact state key");
  page.height_samples[0U] += 1.0F;
  const std::size_t halo_side = page.size + 2U;
  page.normal_neighbourhood_positions[halo_side + 1U].y += 1.0F;
  std::string changed_key;
  Require(BuildOgre14GraphicsSceneTerrainGeometryStateKey(page, changed_key)
              .ok() &&
              changed_key != accepted_key,
          "changed terrain height reused an immutable geometry key");

  page = MakeTerrainPage();
  page.highest_lod_loaded = 1;
  page.target_lod_level = 1;
  std::string camera_lod_key;
  Require(BuildOgre14GraphicsSceneTerrainGeometryStateKey(
              page, camera_lod_key)
              .ok() &&
              camera_lod_key == accepted_key,
          "camera-selected terrain draw LOD changed canonical CPU identity");
  std::shared_ptr<const RenderAssetPayload> lod0_payload;
  std::shared_ptr<const RenderAssetPayload> camera_lod_payload;
  Require(BuildOgre14GraphicsSceneTerrainMeshPayload(
              MakeTerrainPage(), 1U, lod0_payload)
              .ok() &&
              BuildOgre14GraphicsSceneTerrainMeshPayload(
                  page, 1U, camera_lod_payload)
                  .ok() &&
              EquivalentRenderAssetPayload(*lod0_payload,
                                           *camera_lod_payload),
          "camera-selected terrain draw LOD changed canonical LOD0 payload");

  page.highest_lod_prepared = 1;
  state_key = accepted_key;
  result = BuildOgre14GraphicsSceneTerrainGeometryStateKey(page, state_key);
  Require(!result && result.field == "lod.full_resolution" &&
              state_key == accepted_key,
          "partial terrain LOD was accepted or modified state-key output");
}

void TestTerrainCacheResolutionIsExactAndTransactional() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneTerrainPageCaptureInput page = MakeTerrainPage();
  Ogre14GraphicsSceneTerrainPageCacheEntry cache;
  ValidationResult result =
      ResolveOgre14GraphicsSceneTerrainPageCacheEntry(page, nullptr, cache);
  Require(result.ok() && !cache.exact_geometry_state_key.empty() &&
              cache.topology_revision == 1U &&
              cache.mesh_payload != nullptr,
          "new terrain cache entry was not built at revision one");
  const Ogre14GraphicsSceneTerrainPageCacheEntry accepted = cache;

  Ogre14GraphicsSceneTerrainPageCacheEntry stable;
  result = ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
      page, &cache, stable);
  Require(result.ok() && stable.topology_revision == 1U &&
              stable.exact_geometry_state_key ==
                  accepted.exact_geometry_state_key &&
              SameSharedOwner(stable.mesh_payload, accepted.mesh_payload),
          "stable terrain geometry did not reuse its immutable cache owner");

  page.highest_lod_loaded = 1;
  page.target_lod_level = 1;
  Ogre14GraphicsSceneTerrainPageCacheEntry camera_lod;
  result = ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
      page, &stable, camera_lod);
  Require(result.ok() && camera_lod.topology_revision == 1U &&
              SameSharedOwner(camera_lod.mesh_payload,
                              accepted.mesh_payload),
          "camera-selected terrain LOD invalidated the CPU geometry cache");

  page.height_samples[0U] += 0.5F;
  const std::size_t halo_side = page.size + 2U;
  page.normal_neighbourhood_positions[halo_side + 1U].y += 0.5F;
  Ogre14GraphicsSceneTerrainPageCacheEntry changed;
  result = ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
      page, &camera_lod, changed);
  Require(result.ok() && changed.topology_revision == 2U &&
              changed.exact_geometry_state_key !=
                  accepted.exact_geometry_state_key &&
              !SameSharedOwner(changed.mesh_payload,
                               accepted.mesh_payload),
          "changed terrain geometry reused a stale cache revision or owner");

  const Ogre14GraphicsSceneTerrainPageCacheEntry committed = changed;
  page.highest_lod_prepared = 1;
  result = ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
      page, &changed, changed);
  Require(!result && result.field == "lod.full_resolution" &&
              changed.topology_revision == committed.topology_revision &&
              changed.exact_geometry_state_key ==
                  committed.exact_geometry_state_key &&
              SameSharedOwner(changed.mesh_payload,
                              committed.mesh_payload),
          "rejected terrain cache update modified the committed entry");
}

void TestTerrainLod0MeshPreservesGridSkirtsNormalsAndUv() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneTerrainPageCaptureInput page = MakeTerrainPage();
  std::shared_ptr<const RenderAssetPayload> payload;
  ValidationResult result = BuildOgre14GraphicsSceneTerrainMeshPayload(
      page, 4U, payload);
  Require(result.ok() && payload != nullptr,
          "valid full-resolution terrain page was rejected");
  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(*payload);
  Require(mesh.positions.size() == 45U && mesh.indices.size() == 192U &&
              mesh.index_format == MeshIndexFormat::UINT16 &&
              mesh.topology_revision == 4U && !mesh.dynamic,
          "terrain LOD0 grid or perimeter-skirt topology changed");
  Require(mesh.positions.front() == Float3{-2.0F, 0.0F, 2.0F} &&
              mesh.texture_coordinates_0.front() == Float2{0.0F, 1.0F} &&
              mesh.texture_coordinates_0[24U] == Float2{1.0F, 0.0F} &&
              Near(mesh.positions[25U].y,
                   mesh.positions.front().y - page.skirt_size),
          "terrain basis, upper-left UV, or skirt displacement changed");
  Require(mesh.local_bounds.minimum.x == -2.0F &&
              Near(mesh.local_bounds.minimum.y, -1.0F) &&
              mesh.local_bounds.minimum.z == -2.0F &&
              mesh.local_bounds.maximum.x == 2.0F &&
              Near(mesh.local_bounds.maximum.y, 1.0F) &&
              mesh.local_bounds.maximum.z == 2.0F,
          "terrain tight local bounds changed");
  for (std::size_t index = 0U; index < 25U; ++index) {
    Require(Near(mesh.normals[index].x,
                 -mesh.normals[index].y * 0.125F) &&
                Near(mesh.normals[index].z,
                     -mesh.normals[index].x) &&
                Near(mesh.tangents[index].w, -1.0F),
            "terrain eight-face normal or UV tangent frame changed");
  }
  for (const std::uint32_t index : mesh.indices) {
    Require(index < mesh.positions.size(),
            "terrain strip conversion emitted an out-of-range index");
  }

  Ogre14GraphicsSceneTerrainPageCaptureInput isolated = MakeTerrainPage();
  isolated.normal_neighbourhood_positions.clear();
  const float isolated_base = isolated.world_size * -0.5F;
  for (std::int32_t y = -1;
       y <= static_cast<std::int32_t>(isolated.size); ++y) {
    for (std::int32_t x = -1;
         x <= static_cast<std::int32_t>(isolated.size); ++x) {
      const std::int32_t clamped_x = (std::max)(
          0, (std::min)(x, static_cast<std::int32_t>(isolated.size) - 1));
      const std::int32_t clamped_y = (std::max)(
          0, (std::min)(y, static_cast<std::int32_t>(isolated.size) - 1));
      isolated.normal_neighbourhood_positions.push_back(
          {static_cast<float>(clamped_x) + isolated_base,
           static_cast<float>(clamped_x + clamped_y) * 0.125F,
           static_cast<float>(clamped_y) * -1.0F - isolated_base});
    }
  }
  result = BuildOgre14GraphicsSceneTerrainMeshPayload(
      isolated, 1U, payload);
  Require(result.ok() && payload != nullptr &&
              std::get<MeshResourceDescriptor>(*payload).positions.size() ==
                  45U,
          "isolated terrain page rejected OGRE-clamped boundary normals");

  const std::shared_ptr<const RenderAssetPayload> accepted = payload;
  page.derived_data_update_in_progress = true;
  result = BuildOgre14GraphicsSceneTerrainMeshPayload(page, 5U, payload);
  Require(!result && result.field == "derived_data" &&
              SameSharedOwner(payload, accepted),
          "concurrent terrain derived update was accepted or changed output");
  page.derived_data_update_in_progress = false;
  page.has_holes = true;
  result = BuildOgre14GraphicsSceneTerrainMeshPayload(page, 5U, payload);
  Require(!result && result.field == "holes" &&
              SameSharedOwner(payload, accepted),
          "terrain holes were silently filled or changed output");

  const auto make_aligned_page = [](Ogre14GraphicsSceneTerrainAlignment alignment) {
    Ogre14GraphicsSceneTerrainPageCaptureInput aligned = MakeTerrainPage();
    aligned.alignment = alignment;
    aligned.normal_neighbourhood_positions.clear();
    const float base = aligned.world_size * -0.5F;
    for (std::int32_t y = -1;
         y <= static_cast<std::int32_t>(aligned.size); ++y) {
      for (std::int32_t x = -1;
           x <= static_cast<std::int32_t>(aligned.size); ++x) {
        const float height = static_cast<float>(x + y) * 0.125F;
        switch (alignment) {
        case Ogre14GraphicsSceneTerrainAlignment::X_Z:
          aligned.normal_neighbourhood_positions.push_back(
              {static_cast<float>(x) + base, height,
               static_cast<float>(y) * -1.0F - base});
          break;
        case Ogre14GraphicsSceneTerrainAlignment::X_Y:
          aligned.normal_neighbourhood_positions.push_back(
              {static_cast<float>(x) + base,
               static_cast<float>(y) + base, height});
          break;
        case Ogre14GraphicsSceneTerrainAlignment::Y_Z:
          aligned.normal_neighbourhood_positions.push_back(
              {height, static_cast<float>(y) + base,
               static_cast<float>(x) * -1.0F - base});
          break;
        }
      }
    }
    return aligned;
  };
  Ogre14GraphicsSceneTerrainPageCaptureInput aligned = make_aligned_page(
      Ogre14GraphicsSceneTerrainAlignment::X_Y);
  result = BuildOgre14GraphicsSceneTerrainMeshPayload(aligned, 1U, payload);
  Require(result.ok() &&
              std::get<MeshResourceDescriptor>(*payload).positions.front() ==
                  Float3{-2.0F, -2.0F, 0.0F} &&
              Near(std::get<MeshResourceDescriptor>(*payload)
                       .positions[25U]
                       .z,
                   -aligned.skirt_size),
          "X/Y-aligned terrain basis or skirt direction changed");
  aligned = make_aligned_page(Ogre14GraphicsSceneTerrainAlignment::Y_Z);
  result = BuildOgre14GraphicsSceneTerrainMeshPayload(aligned, 1U, payload);
  Require(result.ok() &&
              std::get<MeshResourceDescriptor>(*payload).positions.front() ==
                  Float3{0.0F, -2.0F, 2.0F} &&
              Near(std::get<MeshResourceDescriptor>(*payload)
                       .positions[25U]
                       .x,
                   -aligned.skirt_size),
          "Y/Z-aligned terrain basis or skirt direction changed");
}

void TestTerrainPageSetRequiresCompleteMatchingSharedEdges() {
  using namespace RoR::Render;
  std::vector<Ogre14GraphicsSceneTerrainPageCaptureInput> pages{
      MakeTerrainPage(1, 0), MakeTerrainPage(0, 0)};
  ValidationResult result =
      ValidateOgre14GraphicsSceneTerrainPageSet(pages);
  Require(result.ok(),
          "complete adjacent terrain pages with shared LOD0 edge were rejected");

  pages.push_back(pages.front());
  result = ValidateOgre14GraphicsSceneTerrainPageSet(pages);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              result.field == "terrain.pages.identity",
          "duplicate TerrainGroup slot was accepted");
  pages.pop_back();

  const std::size_t halo_side = pages[0U].size + 2U;
  for (std::uint32_t y = 0U; y < pages[0U].size; ++y) {
    pages[0U].height_samples[static_cast<std::size_t>(y) * pages[0U].size] +=
        0.25F;
    pages[0U]
        .normal_neighbourhood_positions[
            static_cast<std::size_t>(y + 1U) * halo_side + 1U]
        .y += 0.25F;
  }
  result = ValidateOgre14GraphicsSceneTerrainPageSet(pages);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              result.field == "terrain.pages.shared_edge",
          "mismatched adjacent terrain edges were silently stitched");
}

void TestTerrainMaterialGateAndSectionAreTransactional() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneTerrainPageCaptureInput page = MakeTerrainPage();
  std::shared_ptr<const RenderAssetPayload> payload;
  Require(BuildOgre14GraphicsSceneTerrainMeshPayload(page, 1U, payload).ok(),
          "terrain section fixture mesh was rejected");
  Ogre14GraphicsSceneStaticSectionCaptureInput section;
  section.stable_object_id = 999U;
  ValidationResult result = BuildOgre14GraphicsSceneTerrainSection(
      page, payload, section);
  Require(result.ok() && section.stable_object_id != 0U &&
              section.section_index == 0U &&
              !section.exact_terrain_page_key.empty() &&
              section.mesh_payload == payload &&
              section.render_from_object.elements[12U] ==
                  page.page_world_position.x,
          "terrain page was not bound to an exact section identity");
  const std::uint64_t accepted_id = section.stable_object_id;

  page.material_audit.layer_count = 1U;
  page.material_audit.sampler_count = 2U;
  page.material_audit.layer_world_sizes = {12.0F};
  page.material_audit.layer_texture_names = {"d.png", "n.png"};
  result = ValidateOgre14GraphicsSceneTerrainPageSet({page});
  Require(!result && result.field == "terrain.pages.material.layers",
          "complete-page validation did not gate terrain layers before meshing");
  result = BuildOgre14GraphicsSceneTerrainSection(page, payload, section);
  Require(!result && result.field == "material.layers" &&
              section.stable_object_id == accepted_id,
          "terrain layers were silently dropped or modified output section");

  page = MakeTerrainPage();
  page.material_audit.has_composite_map = true;
  page.material_audit.exact_composite_map_name = "Terrain/Composite";
  result = BuildOgre14GraphicsSceneTerrainSection(page, payload, section);
  Require(!result && result.field == "material.composite_map" &&
              section.stable_object_id == accepted_id,
          "terrain composite map was silently dropped");
}

void TestTerrainInventoryReusesPayloadAndFeedsProducer() {
  using namespace RoR::Render;
  Ogre14GraphicsSceneTerrainPageCaptureInput page = MakeTerrainPage();
  std::shared_ptr<const RenderAssetPayload> payload;
  Require(BuildOgre14GraphicsSceneTerrainMeshPayload(page, 1U, payload).ok(),
          "terrain inventory fixture mesh was rejected");
  Ogre14GraphicsSceneStaticSectionCaptureInput section;
  Require(BuildOgre14GraphicsSceneTerrainSection(page, payload, section).ok(),
          "terrain inventory fixture section was rejected");

  Ogre14GraphicsSceneStaticIdentityRegistry registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> instances;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {section}, registry, assets, instances)
              .ok() &&
              registry.terrain_page_identity_count() == 1U &&
              assets.size() == 2U && instances.size() == 1U,
          "terrain page identities/assets were not committed atomically");
  const std::vector<GraphicsSceneAssetInput> accepted_assets = assets;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {section}, registry, assets, instances)
              .ok(),
          "stable terrain inventory was rejected");
  for (const GraphicsSceneAssetInput &asset : assets) {
    const auto prior = std::find_if(
        accepted_assets.begin(), accepted_assets.end(),
        [&asset](const GraphicsSceneAssetInput &candidate) {
          return candidate.source_asset_id == asset.source_asset_id;
        });
    Require(prior != accepted_assets.end() &&
                SameSharedOwner(prior->payload, asset.payload),
            "stable terrain asset did not reuse its immutable owner");
  }

  Require(BuildOgre14GraphicsSceneStaticInventory(
              {}, registry, assets, instances)
              .ok() &&
              assets.empty() && instances.empty(),
          "terrain omission did not atomically tombstone the live page");
  const ValidationResult resurrection =
      BuildOgre14GraphicsSceneStaticInventory(
          {section}, registry, assets, instances);
  Require(!resurrection && assets.empty() && instances.empty() &&
              registry.terrain_page_identity_count() == 1U,
          "removed terrain page identity was resurrected or mutated output");

  Ogre14GraphicsSceneStaticIdentityRegistry producer_registry;
  Require(BuildOgre14GraphicsSceneStaticInventory(
              {section}, producer_registry, assets, instances)
              .ok(),
          "fresh terrain producer inventory fixture was rejected");

  FixtureProvider provider;
  provider.capture.frame.assets = assets;
  provider.capture.frame.static_meshes = instances;
  Ogre14GraphicsSceneSource source(provider);
  GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = 0x5445525241494E31ULL;
  GraphicsSceneSnapshotProducer producer(configuration);
  const GraphicsSceneSnapshotProduceResult produced =
      producer.ProduceJoinedFrame(source);
  Require(produced.ok() &&
              produced.production.scene_snapshot->mesh_instances().size() ==
                  1U &&
              produced.production.asset_delta.has_value() &&
              produced.production.asset_delta->mutations.size() == 2U,
          "canonical terrain page was rejected by the scene producer");
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

  inputs[0U].visible = false;
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  Require(result.ok() && meshes.size() == 1U &&
              meshes.front().visibility_mask == 0U &&
              SameSharedOwner(first_state, meshes.front().state),
          "hidden actor section was tombstoned or changed deformation state");
  inputs[0U].visible = true;
  result = BuildOgre14GraphicsSceneDynamicInventory(inputs, registry, assets,
                                                     meshes);
  Require(result.ok() && meshes.size() == 1U &&
              meshes.front().visibility_mask != 0U &&
              SameSharedOwner(first_state, meshes.front().state),
          "unhidden actor section did not preserve identity and lineage");

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

RoR::Render::GraphicsSceneAssetInput MakeMergeMaterialAsset(
    std::uint64_t source_asset_id, float red = 1.0F) {
  using namespace RoR::Render;
  MaterialDescriptor material;
  material.debug_name = "merge/material";
  material.base_color_factor = {red, 0.5F, 0.25F, 1.0F};
  GraphicsSceneAssetInput asset;
  asset.source_asset_id = source_asset_id;
  asset.payload =
      std::make_shared<const RenderAssetPayload>(std::move(material));
  return asset;
}

void RequireMergeSentinelUnchanged(
    const std::vector<RoR::Render::GraphicsSceneAssetInput> &assets,
    const std::shared_ptr<const RoR::Render::RenderAssetPayload> &owner,
    const char *message) {
  using namespace RoR::Render;
  Require(assets.size() == 1U && assets.front().source_asset_id == 991U &&
              SameSharedOwner(assets.front().payload, owner) &&
              assets.front()
                      .material_bindings[static_cast<std::size_t>(
                          MaterialTextureSlot::EMISSIVE)] ==
                  GraphicsSceneAssetBinding{771U, 772U},
          message);
}

void TestCrossDomainAssetMergeAuditsPayloadsBindingsAndOwners() {
  using namespace RoR::Render;
  GraphicsSceneAssetInput static_asset = MakeMergeMaterialAsset(20U);
  static_asset.material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::EMISSIVE)] = {101U, 102U};
  GraphicsSceneAssetInput exact_duplicate = static_asset;
  exact_duplicate.payload = std::make_shared<const RenderAssetPayload>(
      *static_asset.payload);
  const GraphicsSceneAssetInput road_asset = MakeMergeMaterialAsset(10U);

  std::vector<GraphicsSceneAssetInput> merged;
  ValidationResult result = MergeOgre14GraphicsSceneAssets(
      {static_asset}, {exact_duplicate}, {road_asset}, merged);
  Require(result.ok() && merged.size() == 2U &&
              merged[0U].source_asset_id == 10U &&
              merged[1U].source_asset_id == 20U &&
              SameSharedOwner(merged[1U].payload, static_asset.payload) &&
              !SameSharedOwner(merged[1U].payload,
                               exact_duplicate.payload) &&
              merged[1U].material_bindings == static_asset.material_bindings,
          "exact cross-domain duplicates were not sorted or did not preserve "
          "the first owner");

  GraphicsSceneAssetInput binding_conflict = exact_duplicate;
  binding_conflict.material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::EMISSIVE)] = {101U, 999U};
  GraphicsSceneAssetInput sentinel = MakeMergeMaterialAsset(991U);
  sentinel.material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::EMISSIVE)] = {771U, 772U};
  const auto sentinel_owner = sentinel.payload;
  merged = {sentinel};
  result = MergeOgre14GraphicsSceneAssets(
      {static_asset}, {binding_conflict}, {}, merged);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              result.field == "assets.merge.source_asset_id",
          "cross-domain material-binding conflict was accepted");
  RequireMergeSentinelUnchanged(
      merged, sentinel_owner,
      "binding-conflict failure changed the populated merge sentinel");

  GraphicsSceneAssetInput payload_conflict = MakeMergeMaterialAsset(20U, 0.5F);
  payload_conflict.material_bindings = static_asset.material_bindings;
  result = MergeOgre14GraphicsSceneAssets(
      {static_asset}, {}, {payload_conflict}, merged);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH,
          "cross-domain payload conflict was accepted");
  RequireMergeSentinelUnchanged(
      merged, sentinel_owner,
      "payload-conflict failure changed the populated merge sentinel");
}

class ThrowingAssetMergeFault final
    : public RoR::Render::IOgre14GraphicsSceneAssetMergeFaultInjector {
public:
  explicit ThrowingAssetMergeFault(bool allocation) noexcept
      : allocation_(allocation) {}

  void AtFaultPoint(
      RoR::Render::Ogre14GraphicsSceneAssetMergeFaultPoint) override {
    if (allocation_) {
      throw std::bad_alloc{};
    }
    throw std::runtime_error("deterministic joined-asset merge fault");
  }

private:
  bool allocation_ = false;
};

void TestCrossDomainAssetMergeCapAndExceptionRollback() {
  using namespace RoR::Render;
  GraphicsSceneAssetInput sentinel = MakeMergeMaterialAsset(991U);
  sentinel.material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::EMISSIVE)] = {771U, 772U};
  const auto sentinel_owner = sentinel.payload;
  std::vector<GraphicsSceneAssetInput> merged{sentinel};

  const GraphicsSceneAssetInput repeated = MakeMergeMaterialAsset(1U);
  std::vector<GraphicsSceneAssetInput> over_cap(
      kMaximumOgre14GraphicsSceneMergedAssets + 1U, repeated);
  ValidationResult result = MergeOgre14GraphicsSceneAssets(
      over_cap, {}, {}, merged);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              result.field == "assets.merge.aggregate_count",
          "aggregate asset cap+1 was not rejected before merge allocation");
  RequireMergeSentinelUnchanged(
      merged, sentinel_owner,
      "aggregate cap failure changed the populated merge sentinel");

  ThrowingAssetMergeFault allocation(true);
  result = MergeOgre14GraphicsSceneAssets(
      {repeated}, {}, {}, merged, &allocation);
  Require(!result && result.code == ValidationCode::EMPTY_PAYLOAD &&
              result.field == "assets.merge.allocation",
          "injected merge allocation failure escaped or changed code");
  RequireMergeSentinelUnchanged(
      merged, sentinel_owner,
      "allocation exception changed the populated merge sentinel owner");

  ThrowingAssetMergeFault unexpected(false);
  result = MergeOgre14GraphicsSceneAssets(
      {repeated}, {}, {}, merged, &unexpected);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "assets.merge.exception",
          "injected unexpected merge exception escaped or changed code");
  RequireMergeSentinelUnchanged(
      merged, sentinel_owner,
      "unexpected exception changed the populated merge sentinel owner");
}

RoR::Render::GraphicsSceneStaticMeshInput AutomaticProbeStatic(
    std::uint64_t source_object_id) {
  RoR::Render::GraphicsSceneStaticMeshInput instance;
  instance.source_object_id = source_object_id;
  return instance;
}

void TestAutomaticReflectionProbeIsTransactionalAndMapScoped() {
  using namespace RoR::Render;
  Ogre14AutomaticReflectionProbeState committed;
  Ogre14AutomaticReflectionProbeState candidate;
  std::vector<ReflectionProbeRuntimeDescriptor> probes;
  ValidationResult result = BuildOgre14AutomaticReflectionProbe(
      {10.0, 20.0, 30.0}, {}, committed, candidate, probes);
  Require(result.ok() && !candidate.initialized && probes.empty(),
          "empty pre-content scene unexpectedly authored a reflection probe");

  const std::vector<GraphicsSceneStaticMeshInput> first_static = {
      AutomaticProbeStatic(10U), AutomaticProbeStatic(20U)};
  result = BuildOgre14AutomaticReflectionProbe(
      {10.0, 20.0, 30.0}, first_static, committed, candidate, probes);
  Require(result.ok() && candidate.initialized &&
              candidate.content_revision == 1U &&
              candidate.static_object_ids ==
                  std::vector<std::uint64_t>({10U, 20U}) &&
              probes.size() == 1U,
          "first static inventory did not author one automatic probe");
  const ReflectionProbeRuntimeDescriptor &first = probes.front();
  Require(first.probe_id == kOgre14AutomaticReflectionProbeId &&
              first.absolute_world_position_meters.x == 10.0 &&
              first.absolute_world_position_meters.y == 20.0 &&
              first.absolute_world_position_meters.z == 30.0 &&
              first.content_revision == 1U && first.resolution == 256U &&
              first.update_mode ==
                  ReflectionProbeUpdateMode::PERIODIC_SIMULATION_TICKS &&
              first.update_interval_simulation_ticks ==
                  kOgre14AutomaticReflectionProbeUpdateIntervalSimulationTicks &&
              !first.include_dynamic_geometry &&
              ValidateReflectionProbeRuntimeDescriptor(first).ok(),
          "automatic probe lost its exact PCC policy");

  committed = candidate;
  result = BuildOgre14AutomaticReflectionProbe(
      {100.0, 200.0, 300.0}, first_static, committed, candidate, probes);
  Require(result.ok() && candidate.content_revision == 1U &&
              candidate.absolute_world_position_meters.x == 10.0 &&
              probes.size() == 1U &&
              probes.front().absolute_world_position_meters.x == 10.0,
          "stable static inventory moved or invalidated the frozen probe");

  committed = candidate;
  const std::vector<GraphicsSceneStaticMeshInput> grown_static = {
      AutomaticProbeStatic(5U), AutomaticProbeStatic(10U),
      AutomaticProbeStatic(20U), AutomaticProbeStatic(30U)};
  result = BuildOgre14AutomaticReflectionProbe(
      {-1.0, -2.0, -3.0}, grown_static, committed, candidate, probes);
  Require(result.ok() && candidate.content_revision == 2U &&
              probes.size() == 1U &&
              probes.front().content_revision == 2U &&
              probes.front().absolute_world_position_meters.x == 10.0,
          "grown static inventory did not revise the fixed probe exactly once");

  const Ogre14AutomaticReflectionProbeState sentinel_state = candidate;
  const std::vector<ReflectionProbeRuntimeDescriptor> sentinel_probes = probes;
  result = BuildOgre14AutomaticReflectionProbe(
      {0.0, 0.0, 0.0},
      {AutomaticProbeStatic(10U), AutomaticProbeStatic(10U)},
      committed, candidate, probes);
  Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
              candidate.static_object_ids ==
                  sentinel_state.static_object_ids &&
              probes.front().content_revision ==
                  sentinel_probes.front().content_revision,
          "duplicate static identity changed automatic probe outputs");
  result = BuildOgre14AutomaticReflectionProbe(
      {0.0, 0.0, 0.0}, {AutomaticProbeStatic(10U)}, committed,
      candidate, probes);
  Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
              candidate.static_object_ids ==
                  sentinel_state.static_object_ids,
          "shrinking static inventory bypassed map-generation reset");

  Double3 nonfinite_camera{};
  nonfinite_camera.y = DoubleFromBits(UINT64_C(0x7ff8000000000000));
  result = BuildOgre14AutomaticReflectionProbe(
      nonfinite_camera, grown_static, committed, candidate, probes);
  Require(!result && result.code == ValidationCode::NON_FINITE_VALUE &&
              candidate.static_object_ids ==
                  sentinel_state.static_object_ids,
          "non-finite probe camera changed automatic probe outputs");

  Ogre14AutomaticReflectionProbeState exhausted = committed;
  exhausted.content_revision =
      (std::numeric_limits<std::uint64_t>::max)();
  result = BuildOgre14AutomaticReflectionProbe(
      {0.0, 0.0, 0.0}, grown_static, exhausted, candidate, probes);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              candidate.static_object_ids ==
                  sentinel_state.static_object_ids,
          "exhausted probe revision changed automatic probe outputs");
}

} // namespace

int main() {
  TestCompleteCaptureFeedsProducerOnce();
  TestPreparedCaptureRequiresExplicitResolution();
  TestRejectedCaptureDoesNotSkipDeformationRevision();
  TestMissingFieldsAreCompleteAndTransactional();
  TestEveryRequiredFieldHasStableDiagnosticIdentity();
  TestMalformedMetadataFailsClosed();
  TestProviderFailuresAndExceptionsDoNotEscape();
  TestConstantEnvironmentConversionIsExactAndTransactional();
  TestModernAnalyticSkyPolicyIsLiveMatchedAndTransactional();
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
  TestTerrainIdentityAndExactStateKeyAreStable();
  TestTerrainCacheResolutionIsExactAndTransactional();
  TestTerrainLod0MeshPreservesGridSkirtsNormalsAndUv();
  TestTerrainPageSetRequiresCompleteMatchingSharedEdges();
  TestTerrainMaterialGateAndSectionAreTransactional();
  TestTerrainInventoryReusesPayloadAndFeedsProducer();
  TestDynamicIdentityPayloadRevisionAndLifecycle();
  TestDynamicSectionsPreserveTopologyAndMaterialBindings();
  TestConvertedDynamicInventoryFeedsProducer();
  TestPerspectiveAndOrthographicCameraConversion();
  TestCameraConversionRejectsGuesswork();
  TestCrossDomainAssetMergeAuditsPayloadsBindingsAndOwners();
  TestCrossDomainAssetMergeCapAndExceptionRollback();
  TestAutomaticReflectionProbeIsTransactionalAndMapScoped();
  return EXIT_SUCCESS;
}
