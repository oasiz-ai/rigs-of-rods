/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "GraphicsSceneSnapshotProducer.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

namespace AllocationProbe {
constexpr std::size_t kLargeAllocationBytes = 512U * 1024U;
bool enabled = false;
std::size_t allocations = 0U;
std::size_t large_allocations = 0U;
} // namespace AllocationProbe

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// GCC's interprocedural warning sees the intentionally replaced global new
// and delete bodies but does not model them as a matched replacement pair.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void *operator new(std::size_t size) {
  if (AllocationProbe::enabled) {
    ++AllocationProbe::allocations;
    if (size >= AllocationProbe::kLargeAllocationBytes) {
      ++AllocationProbe::large_allocations;
    }
  }
  if (void *allocation = std::malloc(size == 0U ? 1U : size)) {
    return allocation;
  }
  throw std::bad_alloc();
}

void *operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void *allocation) noexcept { std::free(allocation); }

void operator delete[](void *allocation) noexcept {
  ::operator delete(allocation);
}

void operator delete(void *allocation, std::size_t) noexcept {
  ::operator delete(allocation);
}

void operator delete[](void *allocation, std::size_t) noexcept {
  ::operator delete[](allocation);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "graphics scene snapshot producer test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::Matrix4x4 Translation(float x, float y = 0.0F,
                                   float z = 0.0F) {
  RoR::Render::Matrix4x4 transform;
  transform.elements[12U] = x;
  transform.elements[13U] = y;
  transform.elements[14U] = z;
  return transform;
}

RoR::Render::Matrix4x4 Perspective(float near_plane = 0.1F,
                                   float far_plane = 1000.0F,
                                   float horizontal_offset = 0.0F) {
  RoR::Render::Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  projection.elements[8U] = horizontal_offset;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

RoR::Render::MeshResourceDescriptor MakeMesh() {
  using namespace RoR::Render;
  MeshResourceDescriptor mesh;
  mesh.debug_name = "joined terrain triangle";
  mesh.local_bounds.minimum = {0.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {
      {0.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F},
  };
  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0 = {
      {0.0F, 0.0F},
      {1.0F, 0.0F},
      {0.0F, 1.0F},
  };
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

RoR::Render::TextureResourceDescriptor MakeTexture(std::uint8_t red = 255U) {
  using namespace RoR::Render;
  TextureResourceDescriptor texture;
  texture.debug_name = "joined base color";
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes = {red, 127U, 63U, 255U};
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

std::uint64_t DescriptorOwnedBytes(
    const RoR::Render::RenderAssetPayload &payload) {
  using namespace RoR::Render;
  if (const auto *mesh = std::get_if<MeshResourceDescriptor>(&payload)) {
    return static_cast<std::uint64_t>(mesh->debug_name.size()) +
           static_cast<std::uint64_t>(mesh->positions.size()) *
               sizeof(Float3) +
           static_cast<std::uint64_t>(mesh->normals.size()) * sizeof(Float3) +
           static_cast<std::uint64_t>(mesh->tangents.size()) * sizeof(Float4) +
           static_cast<std::uint64_t>(mesh->velocities.size()) *
               sizeof(Float3) +
           static_cast<std::uint64_t>(mesh->texture_coordinates_0.size()) *
               sizeof(Float2) +
           static_cast<std::uint64_t>(mesh->texture_coordinates_1.size()) *
               sizeof(Float2) +
           static_cast<std::uint64_t>(mesh->colors.size()) * sizeof(Float4) +
           static_cast<std::uint64_t>(mesh->indices.size()) *
               sizeof(std::uint32_t);
  }
  if (const auto *texture =
          std::get_if<TextureResourceDescriptor>(&payload)) {
    std::uint64_t bytes =
        static_cast<std::uint64_t>(texture->debug_name.size());
    for (const TextureMipLevelDescriptor &mip : texture->mip_levels) {
      bytes += static_cast<std::uint64_t>(mip.bytes.size());
    }
    return bytes;
  }
  if (const auto *material = std::get_if<MaterialDescriptor>(&payload)) {
    return static_cast<std::uint64_t>(material->debug_name.size());
  }
  const auto *sampler = std::get_if<SamplerResourceDescriptor>(&payload);
  Require(sampler != nullptr,
          "descriptor byte fixture requires a live asset payload");
  return static_cast<std::uint64_t>(sampler->debug_name.size());
}

RoR::Render::GraphicsSceneAssetInput MeshAsset() {
  RoR::Render::GraphicsSceneAssetInput input;
  input.source_asset_id = 10U;
  input.payload = std::make_shared<const RoR::Render::RenderAssetPayload>(
      MakeMesh());
  return input;
}

RoR::Render::GraphicsSceneAssetInput MaterialAsset() {
  using namespace RoR::Render;
  GraphicsSceneAssetInput input;
  input.source_asset_id = 20U;
  MaterialDescriptor material;
  material.debug_name = "joined textured PBR";
  material.roughness_factor = 0.6F;
  input.payload = std::make_shared<const RenderAssetPayload>(material);
  input.material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::BASE_COLOR)] = {30U, 40U};
  return input;
}

RoR::Render::GraphicsSceneAssetInput TextureAsset(std::uint8_t red = 255U) {
  RoR::Render::GraphicsSceneAssetInput input;
  input.source_asset_id = 30U;
  input.payload = std::make_shared<const RoR::Render::RenderAssetPayload>(
      MakeTexture(red));
  return input;
}

RoR::Render::GraphicsSceneAssetInput SamplerAsset() {
  RoR::Render::GraphicsSceneAssetInput input;
  input.source_asset_id = 40U;
  RoR::Render::SamplerResourceDescriptor sampler;
  sampler.debug_name = "joined trilinear sampler";
  input.payload =
      std::make_shared<const RoR::Render::RenderAssetPayload>(sampler);
  return input;
}

RoR::Render::GraphicsSceneFrameInput MakeFrame() {
  using namespace RoR::Render;
  GraphicsSceneFrameInput frame;
  frame.simulation_tick = 41U;
  frame.simulation_time_seconds = 1.0;
  // Deliberately reverse source identity order. Output order and ID allocation
  // must not inherit adapter traversal order.
  frame.assets.push_back(SamplerAsset());
  frame.assets.push_back(MaterialAsset());
  frame.assets.push_back(TextureAsset());
  frame.assets.push_back(MeshAsset());

  GraphicsSceneStaticMeshInput second;
  second.source_object_id = 200U;
  second.mesh_source_asset_id = 10U;
  second.material_source_asset_id = 20U;
  second.render_from_object = Translation(5.0F);
  frame.static_meshes.push_back(second);
  GraphicsSceneStaticMeshInput first = second;
  first.source_object_id = 100U;
  first.render_from_object = Translation(1.0F);
  frame.static_meshes.push_back(first);

  frame.camera.view_id = 7U;
  frame.camera.width = 1280U;
  frame.camera.height = 720U;
  frame.camera.clip_from_view = Perspective();
  frame.camera.near_plane = 0.1F;
  frame.camera.far_plane = 1000.0F;
  return frame;
}

RoR::Render::GraphicsSceneSnapshotProducer MakeProducer(
    std::uint64_t registry_id = 0x524F525F5343454EULL) {
  RoR::Render::GraphicsSceneSnapshotProducerConfiguration configuration;
  configuration.registry_id = registry_id;
  return RoR::Render::GraphicsSceneSnapshotProducer(configuration);
}

const RoR::Render::RenderAssetMutation &MutationWithLowId(
    const RoR::Render::RenderAssetDelta &delta, std::uint64_t low) {
  const auto found = std::find_if(
      delta.mutations.begin(), delta.mutations.end(),
      [low](const RoR::Render::RenderAssetMutation &mutation) {
        return mutation.asset.id.low() == low;
      });
  Require(found != delta.mutations.end(), "expected asset mutation is absent");
  return *found;
}

class FixtureJoinedSource final
    : public RoR::Render::IJoinedGraphicsSceneSource {
public:
  [[nodiscard]] RoR::Render::ValidationResult CaptureJoinedGraphicsFrame(
      RoR::Render::GraphicsSceneFrameInput &output) override {
    ++capture_count;
    if (!capture_validation) {
      return capture_validation;
    }
    output = frame;
    return RoR::Render::ValidationResult::Success();
  }

  RoR::Render::GraphicsSceneFrameInput frame = MakeFrame();
  RoR::Render::ValidationResult capture_validation;
  std::uint32_t capture_count = 0U;
};

void TestJoinedSourceInitialSnapshotAndCanonicalOrder() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer();
  Require(!producer.BuildRecoveryAssetSnapshot(),
          "recovery was available before first production");

  FixtureJoinedSource source;
  const GraphicsSceneSnapshotProduceResult produced =
      producer.ProduceJoinedFrame(source);
  Require(produced.ok(), "valid joined source frame was rejected");
  Require(source.capture_count == 1U, "joined source was not captured once");
  Require(produced.production.asset_delta.has_value(),
          "first production omitted its full asset snapshot");
  const RenderAssetDelta &delta = *produced.production.asset_delta;
  Require(delta.full_snapshot && delta.base_sequence == 0U &&
              delta.sequence == 1U && delta.mutations.size() == 4U,
          "initial asset transaction lineage is wrong");
  for (std::size_t index = 0U; index < delta.mutations.size(); ++index) {
    Require(delta.mutations[index].asset.id.high() == producer.registry_id() &&
                delta.mutations[index].asset.id.low() == index + 1U,
            "asset IDs were not allocated in canonical source order");
    if (index != 0U) {
      Require(delta.mutations[index - 1U].asset.id <
                  delta.mutations[index].asset.id,
              "asset delta was not strictly sorted");
    }
  }

  const SceneSnapshot &scene = *produced.production.scene_snapshot;
  Require(scene.snapshot_id() == 1U && scene.asset_sequence() == 1U,
          "initial scene identity or asset sequence is wrong");
  Require(scene.mesh_instances().size() == 2U &&
              scene.mesh_instances()[0U].instance_id == 100U &&
              scene.mesh_instances()[1U].instance_id == 200U,
          "static instances were not canonicalized by source identity");
  Require(scene.mesh_instances()[0U].mesh.id.low() == 1U &&
              scene.mesh_instances()[0U].material.id.low() == 2U,
          "scene references do not use producer-owned stable asset IDs");
  Require(produced.production.camera.previous_view_from_render ==
              produced.production.camera.view_from_render &&
              produced.production.camera.previous_clip_from_view ==
                  produced.production.camera.clip_from_view,
          "first camera did not initialize its temporal history");

  const RenderAssetMutation &material_mutation = MutationWithLowId(delta, 2U);
  const MaterialDescriptor &material =
      std::get<MaterialDescriptor>(material_mutation.payload);
  Require(material.base_color_texture.texture.id.low() == 3U &&
              material.base_color_texture.sampler.id.low() == 4U,
          "source material dependencies were not resolved to portable IDs");

  GraphicsSceneAssetRecoveryResult recovery =
      producer.BuildRecoveryAssetSnapshot();
  Require(recovery.ok() && recovery.full_snapshot.full_snapshot &&
              recovery.full_snapshot.sequence == 1U,
          "producer did not emit a valid recovery catalog");
  RenderAssetRegistry recovered(producer.registry_id());
  Require(recovered.Apply(recovery.full_snapshot).ok() &&
              ValidateSceneSnapshotAssets(scene, recovered).ok(),
          "fresh frontend could not replay the producer recovery catalog");

  source.frame.static_meshes.front().render_from_object = Translation(99.0F);
  Require(scene.mesh_instances()[1U].render_from_object.elements[12U] == 5.0F,
          "immutable scene retained mutable joined-source storage");
}

void TestTransformCameraHistoryAndOriginRebase() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(101U);
  GraphicsSceneFrameInput frame = MakeFrame();
  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  Require(first.ok(), "origin history base frame was rejected");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  frame.absolute_world_origin_meters = {100.0, 0.0, 0.0};
  frame.static_meshes[0U].render_from_object = Translation(-95.0F);
  frame.static_meshes[1U].render_from_object = Translation(-99.0F);
  frame.camera.view_from_render = Translation(100.0F);
  const GraphicsSceneSnapshotProduceResult rebased = producer.Produce(frame);
  Require(rebased.ok(), "origin-rebased joined frame was rejected");
  Require(!rebased.production.asset_delta.has_value() &&
              rebased.production.scene_snapshot->asset_sequence() == 1U,
          "transform-only frame incorrectly advanced asset lineage");
  Require(rebased.production.scene_snapshot->snapshot_id() == 2U,
          "transform-only frame did not advance snapshot identity");
  const auto &instances = rebased.production.scene_snapshot->mesh_instances();
  Require(instances[0U].previous_render_from_object.elements[12U] == -99.0F &&
              instances[1U].previous_render_from_object.elements[12U] ==
                  -95.0F,
          "previous object transforms were not rebased to the current origin");
  Require(rebased.production.camera.previous_view_from_render.elements[12U] ==
              100.0F,
          "previous camera was not rebased to the current origin");

  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  frame.camera.near_plane = 0.2F;
  frame.camera.clip_from_view = Perspective(0.2F, 1000.0F, 0.1F);
  const GraphicsSceneSnapshotProduceResult changed_projection =
      producer.Produce(frame);
  Require(changed_projection.ok() &&
              changed_projection.production.camera.previous_clip_from_view ==
                  frame.camera.clip_from_view,
          "clip-plane transition did not reset unrepresentable projection history");
}

void TestAssetUpdatesDependencyRevisionAndDestroyRecovery() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(202U);
  GraphicsSceneFrameInput frame = MakeFrame();
  Require(producer.Produce(frame).ok(), "asset update base frame was rejected");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  MeshResourceDescriptor mesh =
      std::get<MeshResourceDescriptor>(*frame.assets[3U].payload);
  mesh.positions[1U].x = 2.0F;
  mesh.local_bounds.maximum.x = 2.0F;
  mesh.topology_revision = 2U;
  frame.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(mesh));
  frame.assets[2U] = TextureAsset(32U);
  const GraphicsSceneSnapshotProduceResult updated = producer.Produce(frame);
  Require(updated.ok() && updated.production.asset_delta.has_value(),
          "valid asset updates were rejected or omitted");
  const RenderAssetDelta &delta = *updated.production.asset_delta;
  Require(!delta.full_snapshot && delta.base_sequence == 1U &&
              delta.sequence == 2U && delta.mutations.size() == 3U,
          "incremental update lineage or mutation set is wrong");
  Require(MutationWithLowId(delta, 1U).asset.revision == 2U &&
              MutationWithLowId(delta, 2U).asset.revision == 2U &&
              MutationWithLowId(delta, 3U).asset.revision == 2U,
          "mesh, dependent material, and texture revisions did not advance");
  const MaterialDescriptor &material = std::get<MaterialDescriptor>(
      MutationWithLowId(delta, 2U).payload);
  Require(material.base_color_texture.texture.revision == 2U,
          "material did not inherit its updated texture revision");
  Require(updated.production.scene_snapshot->mesh_instances()[0U]
                  .topology_revision == 2U &&
              updated.production.scene_snapshot->mesh_instances()[0U]
                      .material.revision == 2U,
          "updated scene did not reference exact asset/topology revisions");

  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  frame.assets.clear();
  frame.static_meshes.clear();
  const GraphicsSceneSnapshotProduceResult destroyed = producer.Produce(frame);
  Require(destroyed.ok() && destroyed.production.asset_delta.has_value() &&
              destroyed.production.scene_snapshot->mesh_instances().empty(),
          "authoritative removal did not destroy the static scene");
  const RenderAssetDelta &destroy_delta =
      *destroyed.production.asset_delta;
  Require(destroy_delta.sequence == 3U &&
              destroy_delta.mutations.size() == 4U,
          "destroy delta did not cover every removed live asset");
  for (const RenderAssetMutation &mutation : destroy_delta.mutations) {
    Require(mutation.type == RenderAssetMutationType::DESTROY &&
                std::holds_alternative<std::monostate>(mutation.payload),
            "removed asset did not become a payload-free tombstone");
  }

  const GraphicsSceneAssetRecoveryResult recovery =
      producer.BuildRecoveryAssetSnapshot();
  Require(recovery.ok() && recovery.full_snapshot.mutations.size() == 4U,
          "recovery catalog omitted permanent tombstones");
  RenderAssetRegistry recovered(producer.registry_id());
  Require(recovered.Apply(recovery.full_snapshot).ok() &&
              recovered.live_count() == 0U &&
              recovered.record_count() == 4U,
          "fresh frontend did not recover exact tombstone state");

  GraphicsSceneFrameInput reused = frame;
  reused.simulation_tick = 44U;
  reused.simulation_time_seconds = 4.0;
  reused.assets.push_back(MeshAsset());
  Require(producer.Produce(reused).validation.code ==
              ValidationCode::REVISION_MISMATCH,
          "destroyed source asset identity was reusable");
  Require(producer.asset_sequence() == 3U,
          "rejected reuse advanced the asset sequence");

  frame.simulation_tick = 44U;
  frame.simulation_time_seconds = 4.0;
  const GraphicsSceneSnapshotProduceResult after_rejection =
      producer.Produce(frame);
  Require(after_rejection.ok() &&
              after_rejection.production.scene_snapshot->snapshot_id() == 4U &&
              !after_rejection.production.asset_delta.has_value(),
          "rejected reuse consumed snapshot identity or catalog state");
}

void TestMalformedFramesAreAtomicAndFailClosed() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(303U);
  GraphicsSceneFrameInput frame = MakeFrame();
  Require(producer.Produce(frame).ok(), "atomicity base frame was rejected");

  GraphicsSceneFrameInput duplicate = frame;
  duplicate.simulation_tick = 42U;
  duplicate.simulation_time_seconds = 2.0;
  duplicate.assets.push_back(duplicate.assets.front());
  Require(producer.Produce(duplicate).validation.code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate source asset was accepted");

  GraphicsSceneFrameInput bad_topology = frame;
  bad_topology.simulation_tick = 42U;
  bad_topology.simulation_time_seconds = 2.0;
  MeshResourceDescriptor bad_mesh =
      std::get<MeshResourceDescriptor>(*bad_topology.assets[3U].payload);
  bad_mesh.positions[1U].x = 2.0F;
  bad_mesh.local_bounds.maximum.x = 2.0F;
  bad_topology.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(bad_mesh));
  Require(producer.Produce(bad_topology).validation.code ==
              ValidationCode::REVISION_MISMATCH,
          "changed topology without revision was accepted");

  GraphicsSceneFrameInput malformed = frame;
  malformed.simulation_tick = 42U;
  malformed.simulation_time_seconds = 2.0;
  malformed.assets[2U] = TextureAsset(11U);
  malformed.static_meshes.front().render_from_object.elements[0U] = 0.0F;
  Require(!producer.Produce(malformed),
          "singular transform with pending asset updates was accepted");
  Require(producer.asset_sequence() == 1U,
          "late scene failure committed a candidate asset update");

  GraphicsSceneFrameInput missing_dependency = frame;
  missing_dependency.simulation_tick = 42U;
  missing_dependency.simulation_time_seconds = 2.0;
  missing_dependency.assets.erase(missing_dependency.assets.begin() + 2);
  Require(producer.Produce(missing_dependency).validation.code ==
              ValidationCode::MISSING_REFERENCE,
          "material with missing texture dependency was accepted");

  GraphicsSceneFrameInput bad_camera = frame;
  bad_camera.simulation_tick = 42U;
  bad_camera.simulation_time_seconds = 2.0;
  bad_camera.assets[2U] = TextureAsset(11U);
  bad_camera.static_meshes[0U].render_from_object = Translation(77.0F);
  bad_camera.camera.view_from_render = Translation(50.0F);
  bad_camera.camera.width = 0U;
  const GraphicsSceneSnapshotProduceResult bad_camera_result =
      producer.Produce(bad_camera);
  Require(bad_camera_result.validation.code ==
                  ValidationCode::INVALID_DIMENSIONS &&
              bad_camera_result.production.diagnostics
                      .scene_asset_compatibility_full_validations == 1U,
          "invalid main camera with pending asset updates was accepted");
  Require(producer.asset_sequence() == 1U,
          "late camera failure committed a candidate asset update");

  GraphicsSceneFrameInput valid_update = frame;
  valid_update.simulation_tick = 42U;
  valid_update.simulation_time_seconds = 2.0;
  valid_update.assets[2U] = TextureAsset(11U);
  valid_update.static_meshes[0U].render_from_object = Translation(8.0F);
  valid_update.camera.view_from_render = Translation(2.0F);
  const GraphicsSceneSnapshotProduceResult accepted =
      producer.Produce(valid_update);
  Require(accepted.ok() && accepted.production.asset_delta.has_value() &&
              accepted.production.asset_delta->sequence == 2U &&
              accepted.production.scene_snapshot->snapshot_id() == 2U &&
              MutationWithLowId(*accepted.production.asset_delta, 3U)
                      .asset.revision == 2U &&
              accepted.production.scene_snapshot->mesh_instances()[1U]
                      .previous_render_from_object.elements[12U] == 5.0F &&
              accepted.production.camera.previous_view_from_render
                      .elements[12U] == 0.0F &&
              accepted.production.diagnostics
                      .scene_asset_compatibility_full_validations == 1U,
          "rejected frame changed IDs, revisions, or temporal history");

  GraphicsSceneFrameInput rejected_identity_refresh = valid_update;
  rejected_identity_refresh.simulation_tick = 43U;
  rejected_identity_refresh.simulation_time_seconds = 3.0;
  rejected_identity_refresh.assets[2U].payload =
      std::make_shared<const RenderAssetPayload>(
          *valid_update.assets[2U].payload);
  rejected_identity_refresh.camera.width = 0U;
  const GraphicsSceneSnapshotProduceResult refresh_failure =
      producer.Produce(rejected_identity_refresh);
  Require(refresh_failure.validation.code ==
                  ValidationCode::INVALID_DIMENSIONS &&
              refresh_failure.production.diagnostics
                      .asset_payload_full_validations == 1U &&
              refresh_failure.production.diagnostics
                      .asset_payload_fallback_comparisons == 1U &&
              producer.asset_sequence() == 2U,
          "late failure did not preserve staged identity-refresh diagnostics");
  rejected_identity_refresh.camera.width = valid_update.camera.width;
  const GraphicsSceneSnapshotProduceResult refresh_retry =
      producer.Produce(rejected_identity_refresh);
  Require(refresh_retry.ok() &&
              !refresh_retry.production.asset_delta.has_value() &&
              refresh_retry.production.scene_snapshot->snapshot_id() == 3U &&
              refresh_retry.production.diagnostics
                      .asset_payload_full_validations == 1U &&
              refresh_retry.production.diagnostics
                      .asset_payload_fallback_comparisons == 1U &&
              producer.asset_sequence() == 2U,
          "failed identity refresh leaked into producer state or sequence");

  GraphicsSceneFrameInput regressed = valid_update;
  regressed.simulation_tick = 41U;
  Require(producer.Produce(regressed).validation.code ==
              ValidationCode::SEQUENCE_MISMATCH,
          "backwards joined simulation tick was accepted");

  FixtureJoinedSource source;
  source.capture_validation = ValidationResult::Failure(
      ValidationCode::UNSUPPORTED_FEATURE, "fixture", "capture unavailable");
  Require(producer.ProduceJoinedFrame(source).validation.code ==
              ValidationCode::UNSUPPORTED_FEATURE &&
              producer.asset_sequence() == 2U,
          "joined-source failure mutated producer state");
}

void TestCanonicalSortingPreservesOriginalFailureIndices() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(304U);
  const GraphicsSceneFrameInput base_frame = MakeFrame();
  Require(producer.Produce(base_frame).ok(),
          "original-index base frame was rejected");

  GraphicsSceneFrameInput wrong_kind = base_frame;
  wrong_kind.simulation_tick = 42U;
  wrong_kind.simulation_time_seconds = 2.0;
  wrong_kind.assets[3U].payload = TextureAsset().payload;
  const GraphicsSceneSnapshotProduceResult wrong_kind_result =
      producer.Produce(wrong_kind);
  Require(wrong_kind_result.validation.code ==
                  ValidationCode::WRONG_ASSET_KIND &&
              wrong_kind_result.validation.element_index == 3U,
          "asset kind failure lost its original input index");

  GraphicsSceneFrameInput malformed_material = base_frame;
  malformed_material.simulation_tick = 42U;
  malformed_material.simulation_time_seconds = 2.0;
  std::reverse(malformed_material.assets.begin(),
               malformed_material.assets.end());
  MaterialDescriptor invalid_material = std::get<MaterialDescriptor>(
      *malformed_material.assets[2U].payload);
  invalid_material.roughness_factor = 2.0F;
  malformed_material.assets[2U].payload =
      std::make_shared<const RenderAssetPayload>(invalid_material);
  const GraphicsSceneSnapshotProduceResult malformed_material_result =
      producer.Produce(malformed_material);
  Require(malformed_material_result.validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              malformed_material_result.validation.element_index == 2U,
          "payload validation failure reported canonical asset rank");

  GraphicsSceneFrameInput missing_dependency = base_frame;
  missing_dependency.simulation_tick = 42U;
  missing_dependency.simulation_time_seconds = 2.0;
  std::reverse(missing_dependency.assets.begin(),
               missing_dependency.assets.end());
  missing_dependency.assets[2U].material_bindings[static_cast<std::size_t>(
      MaterialTextureSlot::BASE_COLOR)] = {999U, 40U};
  const GraphicsSceneSnapshotProduceResult dependency_result =
      producer.Produce(missing_dependency);
  Require(dependency_result.validation.code ==
                  ValidationCode::MISSING_REFERENCE &&
              dependency_result.validation.element_index == 2U,
          "material dependency failure reported canonical asset rank");

  GraphicsSceneFrameInput malformed_object = base_frame;
  malformed_object.simulation_tick = 42U;
  malformed_object.simulation_time_seconds = 2.0;
  malformed_object.static_meshes[0U].render_from_object.elements[0U] = 0.0F;
  const GraphicsSceneSnapshotProduceResult malformed_object_result =
      producer.Produce(malformed_object);
  Require(malformed_object_result.validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              malformed_object_result.validation.element_index == 0U,
          "snapshot validation failure reported canonical object rank");

  GraphicsSceneFrameInput missing_object_asset = base_frame;
  missing_object_asset.simulation_tick = 42U;
  missing_object_asset.simulation_time_seconds = 2.0;
  missing_object_asset.static_meshes[0U].mesh_source_asset_id = 999U;
  const GraphicsSceneSnapshotProduceResult missing_object_asset_result =
      producer.Produce(missing_object_asset);
  Require(missing_object_asset_result.validation.code ==
                  ValidationCode::MISSING_REFERENCE &&
              missing_object_asset_result.validation.element_index == 0U,
          "object reference failure reported canonical object rank");

  GraphicsSceneFrameInput duplicate_asset = base_frame;
  duplicate_asset.simulation_tick = 42U;
  duplicate_asset.simulation_time_seconds = 2.0;
  duplicate_asset.assets.push_back(duplicate_asset.assets.front());
  const GraphicsSceneSnapshotProduceResult duplicate_asset_result =
      producer.Produce(duplicate_asset);
  Require(duplicate_asset_result.validation.code ==
                  ValidationCode::DUPLICATE_IDENTIFIER &&
              duplicate_asset_result.validation.element_index == 4U,
          "duplicate asset failure lost the later original input index");

  GraphicsSceneFrameInput duplicate_object = base_frame;
  duplicate_object.simulation_tick = 42U;
  duplicate_object.simulation_time_seconds = 2.0;
  duplicate_object.static_meshes.push_back(
      duplicate_object.static_meshes.front());
  const GraphicsSceneSnapshotProduceResult duplicate_object_result =
      producer.Produce(duplicate_object);
  Require(duplicate_object_result.validation.code ==
                  ValidationCode::DUPLICATE_IDENTIFIER &&
              duplicate_object_result.validation.element_index == 2U,
          "duplicate object failure lost the later original input index");

  GraphicsSceneFrameInput destroy_identity = base_frame;
  destroy_identity.simulation_tick = 42U;
  destroy_identity.simulation_time_seconds = 2.0;
  destroy_identity.static_meshes.clear();
  destroy_identity.assets.pop_back();
  Require(producer.Produce(destroy_identity).ok(),
          "asset identity destruction fixture was rejected");
  GraphicsSceneFrameInput reuse_identity = destroy_identity;
  reuse_identity.simulation_tick = 43U;
  reuse_identity.simulation_time_seconds = 3.0;
  reuse_identity.assets.push_back(MeshAsset());
  const GraphicsSceneSnapshotProduceResult reuse_identity_result =
      producer.Produce(reuse_identity);
  Require(reuse_identity_result.validation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              reuse_identity_result.validation.element_index == 3U,
          "destroyed asset identity failure reported canonical asset rank");
}

void TestStableFrameAvoidsLargePayloadCopiesAndComparisons() {
  using namespace RoR::Render;
  GraphicsSceneFrameInput frame = MakeFrame();
  MeshResourceDescriptor mesh =
      std::get<MeshResourceDescriptor>(*frame.assets[3U].payload);
  constexpr std::size_t kVertexCount = 120000U;
  mesh.positions.assign(kVertexCount, Float3{0.0F, 0.0F, 0.0F});
  mesh.normals.assign(kVertexCount, Float3{0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0.assign(kVertexCount, Float2{});
  mesh.indices.resize(kVertexCount);
  for (std::size_t index = 0U; index < kVertexCount; ++index) {
    mesh.indices[index] = static_cast<std::uint32_t>(index);
  }
  mesh.local_bounds = {};
  frame.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(mesh));
  TextureResourceDescriptor texture =
      std::get<TextureResourceDescriptor>(*frame.assets[2U].payload);
  constexpr std::uint32_t kTextureDimension = 1024U;
  texture.width = kTextureDimension;
  texture.height = kTextureDimension;
  texture.mip_levels.front().width = kTextureDimension;
  texture.mip_levels.front().height = kTextureDimension;
  texture.mip_levels.front().row_pitch_bytes = kTextureDimension * 4U;
  texture.mip_levels.front().layer_pitch_bytes =
      static_cast<std::uint64_t>(kTextureDimension) * kTextureDimension * 4U;
  texture.mip_levels.front().bytes.assign(
      static_cast<std::size_t>(
          texture.mip_levels.front().layer_pitch_bytes),
      127U);
  frame.assets[2U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(texture));

  GraphicsSceneSnapshotProducer producer = MakeProducer(606U);
  std::uint64_t base_candidate_bytes = 0U;
  for (const GraphicsSceneAssetInput &asset : frame.assets) {
    base_candidate_bytes += DescriptorOwnedBytes(*asset.payload);
  }
  const GraphicsSceneSnapshotProduceResult base = producer.Produce(frame);
  Require(base.ok() &&
              base.production.diagnostics.asset_payload_full_validations ==
                  static_cast<std::uint64_t>(frame.assets.size()) &&
              base.production.diagnostics
                      .asset_payload_candidate_bytes_validated ==
                  base_candidate_bytes &&
              base.production.diagnostics
                      .scene_asset_compatibility_full_validations == 1U,
          "large immutable asset base validation telemetry is not exact");
  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  frame.static_meshes.front().render_from_object = Translation(6.0F);
  AllocationProbe::large_allocations = 0U;
  AllocationProbe::allocations = 0U;
  AllocationProbe::enabled = true;
  const GraphicsSceneSnapshotProduceResult transformed = producer.Produce(frame);
  AllocationProbe::enabled = false;
  Require(transformed.ok() && !transformed.production.asset_delta.has_value(),
          "large transform-only frame was rejected or emitted an asset delta");
  Require(AllocationProbe::large_allocations == 0U,
          "transform-only transaction deep-copied immutable mesh/texture bytes");
  Require(transformed.production.diagnostics
                  .asset_payload_full_validations == 0U &&
              transformed.production.diagnostics
                      .asset_payload_candidate_bytes_validated == 0U &&
              transformed.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U &&
              transformed.production.diagnostics
                  .asset_payload_fallback_comparisons == 0U &&
              transformed.production.diagnostics
                      .asset_payload_candidate_bytes_compared == 0U,
          "same-owner stable frame performed a deep payload comparison");

  const std::uint64_t expected_candidate_bytes =
      DescriptorOwnedBytes(*frame.assets[2U].payload) +
      DescriptorOwnedBytes(*frame.assets[3U].payload);
  frame.assets[2U].payload =
      std::make_shared<const RenderAssetPayload>(*frame.assets[2U].payload);
  frame.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(*frame.assets[3U].payload);
  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  const GraphicsSceneSnapshotProduceResult identity_refreshed =
      producer.Produce(frame);
  Require(identity_refreshed.ok() &&
              !identity_refreshed.production.asset_delta.has_value() &&
              producer.asset_sequence() == 1U,
          "equivalent replacement owners advanced the asset registry");
  Require(identity_refreshed.production.diagnostics
                      .asset_payload_full_validations == 2U &&
              identity_refreshed.production.diagnostics
                      .asset_payload_candidate_bytes_validated ==
                  expected_candidate_bytes &&
              identity_refreshed.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U &&
              identity_refreshed.production.diagnostics
                      .asset_payload_fallback_comparisons == 2U &&
              identity_refreshed.production.diagnostics
                      .asset_payload_candidate_bytes_compared ==
                  expected_candidate_bytes,
          "replacement-owner fallback comparison telemetry is not exact");

  frame.simulation_tick = 44U;
  frame.simulation_time_seconds = 4.0;
  const GraphicsSceneSnapshotProduceResult refreshed_stable =
      producer.Produce(frame);
  Require(refreshed_stable.ok() &&
              refreshed_stable.production.diagnostics
                      .asset_payload_full_validations == 0U &&
              refreshed_stable.production.diagnostics
                      .asset_payload_candidate_bytes_validated == 0U &&
              refreshed_stable.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U &&
              refreshed_stable.production.diagnostics
                      .asset_payload_fallback_comparisons == 0U &&
              refreshed_stable.production.diagnostics
                      .asset_payload_candidate_bytes_compared == 0U,
          "committed source-identity refresh did not restore the zero-scan path");

  TextureResourceDescriptor changed_texture =
      std::get<TextureResourceDescriptor>(*frame.assets[2U].payload);
  std::uint8_t &last_texel_byte =
      changed_texture.mip_levels.front().bytes.back();
  last_texel_byte = static_cast<std::uint8_t>(last_texel_byte ^ 1U);
  frame.assets[2U].payload =
      std::make_shared<const RenderAssetPayload>(std::move(changed_texture));
  const std::uint64_t changed_texture_bytes =
      DescriptorOwnedBytes(*frame.assets[2U].payload);
  frame.simulation_tick = 45U;
  frame.simulation_time_seconds = 5.0;
  const GraphicsSceneSnapshotProduceResult changed = producer.Produce(frame);
  Require(changed.ok() && changed.production.asset_delta.has_value() &&
              changed.production.asset_delta->sequence == 2U &&
              changed.production.diagnostics.asset_payload_full_validations ==
                  1U &&
              changed.production.diagnostics
                      .asset_payload_candidate_bytes_validated ==
                  changed_texture_bytes &&
              changed.production.diagnostics
                      .asset_payload_fallback_comparisons == 1U &&
              changed.production.diagnostics
                      .asset_payload_candidate_bytes_compared ==
                  changed_texture_bytes &&
              changed.production.diagnostics
                      .scene_asset_compatibility_full_validations == 1U,
          "changed texture was not validated and compared exactly once");

  frame.simulation_tick = 46U;
  frame.simulation_time_seconds = 6.0;
  const GraphicsSceneSnapshotProduceResult changed_stable =
      producer.Produce(frame);
  Require(changed_stable.ok() &&
              !changed_stable.production.asset_delta.has_value() &&
              changed_stable.production.diagnostics
                      .asset_payload_full_validations == 0U &&
              changed_stable.production.diagnostics
                      .asset_payload_candidate_bytes_validated == 0U &&
              changed_stable.production.diagnostics
                      .asset_payload_fallback_comparisons == 0U &&
              changed_stable.production.diagnostics
                      .asset_payload_candidate_bytes_compared == 0U &&
              changed_stable.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U,
          "stable frame after a logical asset update re-scanned payloads");
}

std::size_t StableFrameAllocationCount(
    RoR::Render::GraphicsSceneFrameInput frame, std::uint64_t registry_id) {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(registry_id);
  Require(producer.Produce(frame).ok(),
          "allocation-count base frame was rejected");
  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  AllocationProbe::allocations = 0U;
  AllocationProbe::large_allocations = 0U;
  AllocationProbe::enabled = true;
  const GraphicsSceneSnapshotProduceResult produced = producer.Produce(frame);
  AllocationProbe::enabled = false;
  Require(produced.ok() && !produced.production.asset_delta.has_value(),
          "stable allocation-count frame was rejected or rebuilt assets");
  Require(produced.production.diagnostics
                      .asset_payload_full_validations == 0U &&
              produced.production.diagnostics
                      .asset_payload_candidate_bytes_validated == 0U &&
              produced.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U &&
              produced.production.diagnostics
                      .asset_payload_fallback_comparisons == 0U &&
              produced.production.diagnostics
                      .asset_payload_candidate_bytes_compared == 0U,
          "stable allocation-count frame fell back to payload comparison");
  return AllocationProbe::allocations;
}

void TestStableCatalogAllocationCountDoesNotScalePerElement() {
  using namespace RoR::Render;
  const auto make_scaled_frame = [](std::size_t element_count) {
    GraphicsSceneFrameInput frame = MakeFrame();
    Require(element_count >= frame.assets.size(),
            "allocation scale requires all fixture assets");
    frame.assets.reserve(element_count);
    const std::shared_ptr<const RenderAssetPayload> shared_sampler =
        SamplerAsset().payload;
    for (std::size_t index = frame.assets.size(); index < element_count;
         ++index) {
      GraphicsSceneAssetInput asset;
      asset.source_asset_id = 1000U + index;
      asset.payload = shared_sampler;
      frame.assets.push_back(std::move(asset));
    }
    frame.static_meshes.clear();
    frame.static_meshes.reserve(element_count);
    for (std::size_t index = 0U; index < element_count; ++index) {
      GraphicsSceneStaticMeshInput object;
      object.source_object_id = 1U + index;
      object.mesh_source_asset_id = 10U;
      object.material_source_asset_id = 20U;
      object.render_from_object =
          Translation(static_cast<float>(index % 1024U));
      frame.static_meshes.push_back(object);
    }
    return frame;
  };

  constexpr std::size_t kSmallStableElementCount = 64U;
  constexpr std::size_t kLargeStableElementCount = 8192U;
  const std::size_t small_count = StableFrameAllocationCount(
      make_scaled_frame(kSmallStableElementCount), 607U);
  const std::size_t large_count = StableFrameAllocationCount(
      make_scaled_frame(kLargeStableElementCount), 608U);
  // Contiguous buffers grow in bytes, but the number of allocation calls must
  // remain exactly constant: no map/set node may be allocated per element.
  Require(large_count == small_count,
          "stable catalog allocation count scaled with asset/object elements");
  Require(large_count <= 16U,
          "stable frame exceeded the fixed allocation budget");
}

void TestExhaustionAndBoundsFailClosed() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducerConfiguration snapshot_config;
  snapshot_config.registry_id = 404U;
  snapshot_config.first_snapshot_id =
      (std::numeric_limits<std::uint64_t>::max)();
  GraphicsSceneSnapshotProducer snapshot_producer(snapshot_config);
  GraphicsSceneFrameInput frame = MakeFrame();
  const GraphicsSceneSnapshotProduceResult last_snapshot =
      snapshot_producer.Produce(frame);
  Require(last_snapshot.ok() &&
              last_snapshot.production.scene_snapshot->snapshot_id() ==
                  (std::numeric_limits<std::uint64_t>::max)(),
          "last representable snapshot identity was not usable");
  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  Require(snapshot_producer.Produce(frame).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "snapshot identity overflow was not rejected");

  GraphicsSceneSnapshotProducerConfiguration asset_config;
  asset_config.registry_id = 405U;
  asset_config.first_asset_ordinal =
      (std::numeric_limits<std::uint64_t>::max)();
  GraphicsSceneSnapshotProducer asset_producer(asset_config);
  Require(asset_producer.Produce(MakeFrame()).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE &&
              asset_producer.asset_sequence() == 0U,
          "asset identity overflow was not atomic");
  GraphicsSceneFrameInput one_asset = MakeFrame();
  one_asset.assets.clear();
  one_asset.assets.push_back(MeshAsset());
  one_asset.static_meshes.clear();
  const GraphicsSceneSnapshotProduceResult last_asset =
      asset_producer.Produce(one_asset);
  Require(last_asset.ok() && last_asset.production.asset_delta.has_value() &&
              last_asset.production.asset_delta->mutations.front()
                      .asset.id.low() ==
                  (std::numeric_limits<std::uint64_t>::max)(),
          "failed multi-allocation consumed the final asset identity");

  GraphicsSceneSnapshotProducerConfiguration bounded_config;
  bounded_config.registry_id = 406U;
  bounded_config.maximum_asset_records = 3U;
  GraphicsSceneSnapshotProducer bounded_producer(bounded_config);
  Require(bounded_producer.Produce(MakeFrame()).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE &&
              !bounded_producer.BuildRecoveryAssetSnapshot(),
          "asset record bound failure initialized producer state");

  GraphicsSceneSnapshotProducerConfiguration byte_config;
  byte_config.registry_id = 407U;
  byte_config.maximum_asset_payload_bytes = 1U;
  GraphicsSceneSnapshotProducer byte_producer(byte_config);
  Require(byte_producer.Produce(MakeFrame()).validation.code ==
              ValidationCode::SIZE_MISMATCH,
          "asset payload byte bound was not enforced");
}

void TestDeterministicAcrossAdapterTraversalOrders() {
  using namespace RoR::Render;
  GraphicsSceneFrameInput lhs_frame = MakeFrame();
  GraphicsSceneFrameInput rhs_frame = lhs_frame;
  std::reverse(rhs_frame.assets.begin(), rhs_frame.assets.end());
  std::reverse(rhs_frame.static_meshes.begin(), rhs_frame.static_meshes.end());
  GraphicsSceneSnapshotProducer lhs = MakeProducer(505U);
  GraphicsSceneSnapshotProducer rhs = MakeProducer(505U);
  const GraphicsSceneSnapshotProduceResult lhs_output = lhs.Produce(lhs_frame);
  const GraphicsSceneSnapshotProduceResult rhs_output = rhs.Produce(rhs_frame);
  Require(lhs_output.ok() && rhs_output.ok(),
          "determinism fixtures were rejected");
  Require(lhs_output.production.asset_delta->mutations.size() ==
              rhs_output.production.asset_delta->mutations.size(),
          "adapter traversal order changed mutation count");
  for (std::size_t index = 0U;
       index < lhs_output.production.asset_delta->mutations.size(); ++index) {
    const RenderAssetMutation &lhs_mutation =
        lhs_output.production.asset_delta->mutations[index];
    const RenderAssetMutation &rhs_mutation =
        rhs_output.production.asset_delta->mutations[index];
    Require(lhs_mutation.asset == rhs_mutation.asset &&
                EquivalentRenderAssetPayload(lhs_mutation.payload,
                                             rhs_mutation.payload),
            "adapter traversal order changed asset IDs or contents");
  }
  const auto &lhs_instances =
      lhs_output.production.scene_snapshot->mesh_instances();
  const auto &rhs_instances =
      rhs_output.production.scene_snapshot->mesh_instances();
  Require(lhs_instances.size() == rhs_instances.size(),
          "adapter traversal order changed instance count");
  for (std::size_t index = 0U; index < lhs_instances.size(); ++index) {
    Require(lhs_instances[index].instance_id ==
                rhs_instances[index].instance_id &&
                lhs_instances[index].mesh == rhs_instances[index].mesh &&
                lhs_instances[index].material ==
                    rhs_instances[index].material,
            "adapter traversal order changed scene ordering or references");
  }
}

} // namespace

int main() {
  TestJoinedSourceInitialSnapshotAndCanonicalOrder();
  TestTransformCameraHistoryAndOriginRebase();
  TestAssetUpdatesDependencyRevisionAndDestroyRecovery();
  TestMalformedFramesAreAtomicAndFailClosed();
  TestCanonicalSortingPreservesOriginalFailureIndices();
  TestStableFrameAvoidsLargePayloadCopiesAndComparisons();
  TestStableCatalogAllocationCountDoesNotScalePerElement();
  TestExhaustionAndBoundsFailClosed();
  TestDeterministicAcrossAdapterTraversalOrders();
  return EXIT_SUCCESS;
}
