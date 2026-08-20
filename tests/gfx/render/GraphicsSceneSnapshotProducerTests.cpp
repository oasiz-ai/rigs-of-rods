/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "GraphicsSceneSnapshotProducer.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <thread>
#include <utility>

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK 1
#endif
#endif
#if !defined(ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK) &&                    \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK 1
#endif
#if !defined(ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK)
#define ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK 0
#endif

#if ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK
#include <sanitizer/allocator_interface.h>
#endif

namespace AllocationProbe {
constexpr std::size_t kLargeAllocationBytes = 512U * 1024U;
std::atomic<bool> enabled{false};
std::atomic<std::size_t> allocations{0U};
std::atomic<std::size_t> large_allocations{0U};

void Record(std::size_t size) noexcept {
  if (!enabled.load(std::memory_order_relaxed)) {
    return;
  }
  allocations.fetch_add(1U, std::memory_order_relaxed);
  if (size >= kLargeAllocationBytes) {
    large_allocations.fetch_add(1U, std::memory_order_relaxed);
  }
}

void Reset() noexcept {
  allocations.store(0U, std::memory_order_relaxed);
  large_allocations.store(0U, std::memory_order_relaxed);
}

void Enable() noexcept { enabled.store(true, std::memory_order_relaxed); }

void Disable() noexcept { enabled.store(false, std::memory_order_relaxed); }

std::size_t Count() noexcept {
  return allocations.load(std::memory_order_relaxed);
}

std::size_t LargeCount() noexcept {
  return large_allocations.load(std::memory_order_relaxed);
}
} // namespace AllocationProbe

#if ROR_ALLOCATION_PROBE_USES_SANITIZER_HOOK

// Sanitizer runtimes own the replaceable global new/delete symbols. Their
// documented allocator hook observes the same allocations without colliding
// with libclang_rt.{a,t}san_cxx at link time.
extern "C" void __sanitizer_malloc_hook(const volatile void *,
                                         std::size_t size) {
  AllocationProbe::Record(size);
}

#else

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// GCC's interprocedural warning sees the intentionally replaced global new
// and delete bodies but does not model them as a matched replacement pair.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void *operator new(std::size_t size) {
  AllocationProbe::Record(size);
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

#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "graphics scene snapshot producer test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
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

RoR::Render::GraphicsSceneAssetInput DynamicMeshAsset(
    std::uint64_t source_asset_id = 50U) {
  using namespace RoR::Render;
  GraphicsSceneAssetInput input;
  input.source_asset_id = source_asset_id;
  MeshResourceDescriptor mesh = MakeMesh();
  mesh.debug_name = "joined deformable triangle";
  mesh.dynamic = true;
  input.payload =
      std::make_shared<const RenderAssetPayload>(std::move(mesh));
  return input;
}

std::shared_ptr<const RoR::Render::GraphicsSceneDynamicMeshState>
DynamicState(std::uint64_t deformation_revision, float x_offset = 0.0F) {
  using namespace RoR::Render;
  auto state = std::make_shared<GraphicsSceneDynamicMeshState>();
  state->deformation_revision = deformation_revision;
  state->positions = {
      {x_offset, 0.0F, 0.0F},
      {1.0F + x_offset, 0.0F, 0.0F},
      {x_offset, 1.0F, 0.0F},
  };
  state->normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  state->updated_local_bounds.minimum = {x_offset, 0.0F, 0.0F};
  state->updated_local_bounds.maximum = {1.0F + x_offset, 1.0F, 0.0F};
  return state;
}

RoR::Render::GraphicsSceneDynamicMeshInput DynamicObject(
    std::uint64_t source_object_id,
    std::shared_ptr<const RoR::Render::GraphicsSceneDynamicMeshState> state) {
  RoR::Render::GraphicsSceneDynamicMeshInput input;
  input.source_object_id = source_object_id;
  input.mesh_source_asset_id = 50U;
  input.material_source_asset_id = 20U;
  input.render_from_object = Translation(3.0F);
  input.state = std::move(state);
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

RoR::Render::ReflectionProbeRuntimeDescriptor
ReflectionProbe(std::uint64_t probe_id, double world_x,
                std::uint16_t priority = 1U) {
  using namespace RoR::Render;
  ReflectionProbeRuntimeDescriptor probe;
  probe.probe_id = probe_id;
  probe.absolute_world_position_meters = {world_x, 20.0, -30.0};
  probe.priority = priority;
  probe.resolution = 32U;
  probe.influence_half_size = {4.0F, 3.0F, 2.0F};
  probe.correction_shape_half_size = {5.0F, 4.0F, 3.0F};
  probe.capture_far_meters = 16.0F;
  return probe;
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

  frame.environment.ambient_radiance = {0.025F, 0.03F, 0.04F};
  frame.environment.environment_intensity = 1.1F;
  frame.environment.exposure_compensation_ev = 0.5F;
  frame.environment.analytic_sky.enabled = true;
  frame.environment.analytic_sky.sun_light_id = 300U;
  frame.environment.analytic_sky.zenith_radiance = {0.06F, 0.09F, 0.16F};
  frame.environment.analytic_sky.horizon_radiance = {0.2F, 0.18F, 0.15F};
  frame.environment.analytic_sky.ground_radiance = {0.01F, 0.009F, 0.008F};
  frame.environment.analytic_sky.sun_disk_radiance = {10000.0F, 9300.0F,
                                                       8200.0F};
  frame.environment.analytic_sky.sun_angular_radius_radians = 0.00465F;

  GraphicsSceneLightInput sun;
  sun.source_light_id = 300U;
  sun.intensity = 105000.0F;
  sun.direction = {0.0F, -0.8F, -0.6F};
  frame.lights.push_back(sun);
  GraphicsSceneLightInput spot;
  spot.source_light_id = 200U;
  spot.type = LightType::SPOT;
  spot.intensity = 1800.0F;
  spot.position = {8.0F, 6.0F, -2.0F};
  spot.direction = {0.0F, -1.0F, 0.0F};
  spot.range = 45.0F;
  spot.inner_cone_radians = 0.35F;
  spot.outer_cone_radians = 0.55F;
  frame.lights.push_back(spot);
  GraphicsSceneLightInput point;
  point.source_light_id = 100U;
  point.type = LightType::POINT;
  point.intensity = 900.0F;
  point.position = {4.0F, 3.0F, 1.0F};
  point.range = 30.0F;
  point.shadow_flags = LIGHT_SHADOW_DYNAMIC_GEOMETRY;
  frame.lights.push_back(point);

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
  void CommitJoinedGraphicsFrame() noexcept override { ++commit_count; }
  void DiscardJoinedGraphicsFrame() noexcept override { ++discard_count; }

  RoR::Render::GraphicsSceneFrameInput frame = MakeFrame();
  RoR::Render::ValidationResult capture_validation;
  std::uint32_t capture_count = 0U;
  std::uint32_t commit_count = 0U;
  std::uint32_t discard_count = 0U;
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
  Require(source.commit_count == 1U && source.discard_count == 0U,
          "successful producer output did not commit its source transaction");
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
  Require(scene.lights().size() == 3U &&
              scene.lights()[0U].light_id == 100U &&
              scene.lights()[1U].light_id == 200U &&
              scene.lights()[2U].light_id == 300U,
          "analytic lights were not canonicalized by stable source identity");
  Require(scene.environment().analytic_sky.sun_light_id == 300U &&
              scene.lighting_environment_hash() != 0U,
          "analytic sky identity or deterministic lighting digest is absent");
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

void TestJoinedSourceProducerRejectionDiscardsAndRetries() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer();
  FixtureJoinedSource source;
  source.frame.version = kGraphicsSceneSnapshotProducerVersion + 1U;

  const GraphicsSceneSnapshotProduceResult rejected =
      producer.ProduceJoinedFrame(source);
  Require(!rejected &&
              rejected.validation.code == ValidationCode::UNSUPPORTED_VERSION &&
              source.capture_count == 1U && source.commit_count == 0U &&
              source.discard_count == 1U,
          "producer rejection did not discard the prepared source frame");

  source.frame.version = kGraphicsSceneSnapshotProducerVersion;
  const GraphicsSceneSnapshotProduceResult accepted =
      producer.ProduceJoinedFrame(source);
  Require(accepted && source.capture_count == 2U &&
              source.commit_count == 1U && source.discard_count == 1U &&
              accepted.production.scene_snapshot->snapshot_id() == 1U,
          "discarded source frame advanced producer lineage or blocked retry");
}

void TestLegacyProducerVersionsRequireExplicitMigration() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer(91U);
  GraphicsSceneFrameInput frame = MakeFrame();
  frame.version = 2U;
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::UNSUPPORTED_VERSION &&
              producer.LoadPublishedSnapshot() == nullptr &&
              !producer.BuildRecoveryAssetSnapshot(),
          "producer input version two was implicitly migrated or changed state");
  frame.version = 1U;
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::UNSUPPORTED_VERSION &&
              producer.LoadPublishedSnapshot() == nullptr &&
              !producer.BuildRecoveryAssetSnapshot(),
          "legacy producer input was implicitly migrated or changed state");
  frame.version = 0U;
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::UNSUPPORTED_VERSION &&
              producer.LoadPublishedSnapshot() == nullptr,
          "unversioned producer input was accepted");
}

void TestReflectionProbeLineageAndCanonicalOrder() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer(92U);
  GraphicsSceneFrameInput frame = MakeFrame();
  frame.reflection_probes = {
      ReflectionProbe(2000U, 1000000020.0, 3U),
      ReflectionProbe(1000U, 1000000010.0, 7U),
  };
  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  Require(first.ok(), "valid joined reflection-probe frame was rejected");
  const auto &first_probes =
      first.production.scene_snapshot->reflection_probes();
  Require(first_probes.size() == 2U &&
              first_probes[0U].probe_id == 1000U &&
              first_probes[1U].probe_id == 2000U &&
              first.production.scene_snapshot->reflection_probe_hash() != 0U,
          "joined reflection probes were not canonicalized and hashed");
  const std::shared_ptr<const SceneSnapshot> first_published =
      producer.LoadPublishedSnapshot();
  const std::uint64_t first_hash = first_published->reflection_probe_hash();

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  frame.absolute_world_origin_meters = {1000000000.0, 0.0, 0.0};
  const GraphicsSceneSnapshotProduceResult rebased = producer.Produce(frame);
  Require(rebased.ok() &&
              rebased.production.scene_snapshot->reflection_probe_hash() ==
                  first_hash,
          "render-origin rebase changed authored reflection-probe lineage");

  GraphicsSceneFrameInput unchanged_revision = frame;
  unchanged_revision.simulation_tick = 43U;
  unchanged_revision.simulation_time_seconds = 3.0;
  unchanged_revision.reflection_probes[0U].capture_position_local.x = 0.25F;
  const GraphicsSceneSnapshotProduceResult rejected =
      producer.Produce(unchanged_revision);
  Require(rejected.validation.code == ValidationCode::REVISION_MISMATCH &&
              rejected.validation.element_index == 0U &&
              SameSharedOwner(rebased.production.scene_snapshot,
                              producer.LoadPublishedSnapshot()),
          "changed probe without a revision was accepted or published");

  frame = std::move(unchanged_revision);
  ++frame.reflection_probes[0U].content_revision;
  const GraphicsSceneSnapshotProduceResult revised = producer.Produce(frame);
  Require(revised.ok() &&
              revised.production.scene_snapshot->reflection_probe_hash() !=
                  first_hash,
          "new reflection-probe revision did not publish changed contents");

  frame.simulation_tick = 44U;
  frame.simulation_time_seconds = 4.0;
  frame.reflection_probes.erase(frame.reflection_probes.begin() + 1);
  const GraphicsSceneSnapshotProduceResult removed = producer.Produce(frame);
  Require(removed.ok() &&
              removed.production.scene_snapshot->reflection_probes().size() ==
                  1U,
          "authoritative reflection-probe removal was rejected");
  const std::shared_ptr<const SceneSnapshot> removed_published =
      producer.LoadPublishedSnapshot();

  frame.simulation_tick = 45U;
  frame.simulation_time_seconds = 5.0;
  frame.reflection_probes.push_back(
      ReflectionProbe(1000U, 1000000010.0, 7U));
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(removed_published,
                              producer.LoadPublishedSnapshot()),
          "destroyed reflection-probe identity was reused or published");

  GraphicsSceneSnapshotProducer malformed_producer = MakeProducer(93U);
  GraphicsSceneFrameInput malformed = MakeFrame();
  malformed.reflection_probes = {
      ReflectionProbe(20U, 20.0), ReflectionProbe(10U, 10.0)};
  malformed.reflection_probes[0U].resolution = 96U;
  const GraphicsSceneSnapshotProduceResult malformed_result =
      malformed_producer.Produce(malformed);
  Require(malformed_result.validation.code ==
                  ValidationCode::INVALID_DIMENSIONS &&
              malformed_result.validation.element_index == 0U,
          "malformed probe failure lost its original traversal index");

  malformed.reflection_probes[0U] = malformed.reflection_probes[1U];
  const GraphicsSceneSnapshotProduceResult duplicate_result =
      malformed_producer.Produce(malformed);
  Require(duplicate_result.validation.code ==
                  ValidationCode::DUPLICATE_IDENTIFIER &&
              duplicate_result.validation.element_index == 1U,
          "duplicate probe failure lost the later original traversal index");
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
  frame.lights[1U].position.x = -92.0F;
  frame.lights[2U].position.x = -96.0F;
  frame.lights[0U].direction = {0.0F, -0.6F, -0.8F};
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
  const auto &lights = rebased.production.scene_snapshot->lights();
  Require(lights[0U].light_id == 100U &&
              lights[0U].position.x == -96.0F &&
              lights[0U].previous_position.x == -96.0F &&
              lights[1U].light_id == 200U &&
              lights[1U].position.x == -92.0F &&
              lights[1U].previous_position.x == -92.0F &&
              lights[2U].previous_direction.y == -0.8F &&
              lights[2U].direction.y == -0.6F,
          "light transforms/history were not canonical or origin-rebased");
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

void TestLargeOriginRebaseRollbackAndRetry() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer(131U);
  GraphicsSceneFrameInput frame = MakeFrame();
  frame.absolute_world_origin_meters = {1.0e16, -1.0e16, 1.0e16};
  frame.static_meshes[0U].render_from_object.elements[12U] = 0.25F;
  frame.static_meshes[0U].render_from_object.elements[13U] = -0.5F;
  frame.static_meshes[0U].render_from_object.elements[14U] = 0.75F;
  frame.static_meshes[1U].render_from_object.elements[12U] = -1.25F;
  frame.static_meshes[1U].render_from_object.elements[13U] = 1.5F;
  frame.static_meshes[1U].render_from_object.elements[14U] = -1.75F;
  frame.lights[1U].position = {-1.25F, 1.5F, -1.75F};
  frame.lights[2U].position = {0.25F, -0.5F, 0.75F};

  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  const std::shared_ptr<const SceneSnapshot> first_published =
      producer.LoadPublishedSnapshot();
  Require(first.ok() && SameSharedOwner(first.production.scene_snapshot,
                                         first_published),
          "large-origin base frame was not published");

  GraphicsSceneFrameInput rejected = frame;
  rejected.simulation_tick = 42U;
  rejected.simulation_time_seconds = 2.0;
  rejected.absolute_world_origin_meters.x =
      (std::numeric_limits<double>::max)();
  rejected.lights[2U].position = {99.0F, 98.0F, 97.0F};
  const GraphicsSceneSnapshotProduceResult failed = producer.Produce(rejected);
  Require(failed.validation.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              failed.validation.field == "lights.previous_position" &&
              SameSharedOwner(first_published,
                              producer.LoadPublishedSnapshot()) &&
              producer.asset_sequence() == 1U,
          "failed large-origin rebase mutated publication or lineage");
  Require(first_published->lights()[0U].position ==
              Float3{0.25F, -0.5F, 0.75F},
          "failed rebase mutated previously published light history");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  frame.absolute_world_origin_meters = {1.0e16 + 16.0,
                                        -1.0e16 - 32.0,
                                        1.0e16 + 64.0};
  frame.static_meshes[0U].render_from_object.elements[12U] = 10.0F;
  frame.static_meshes[0U].render_from_object.elements[13U] = 20.0F;
  frame.static_meshes[0U].render_from_object.elements[14U] = 30.0F;
  frame.static_meshes[1U].render_from_object.elements[12U] = 40.0F;
  frame.static_meshes[1U].render_from_object.elements[13U] = 50.0F;
  frame.static_meshes[1U].render_from_object.elements[14U] = 60.0F;
  frame.lights[1U].position = {10.0F, 20.0F, 30.0F};
  frame.lights[2U].position = {40.0F, 50.0F, 60.0F};
  const GraphicsSceneSnapshotProduceResult retried = producer.Produce(frame);
  Require(retried.ok() &&
              retried.production.scene_snapshot->snapshot_id() == 2U &&
              !SameSharedOwner(first_published,
                               producer.LoadPublishedSnapshot()),
          "valid retry after failed rebase did not commit exactly once");

  const auto &objects = retried.production.scene_snapshot->mesh_instances();
  Require(objects[0U].previous_render_from_object.elements[12U] == -17.25F &&
              objects[0U].previous_render_from_object.elements[13U] == 33.5F &&
              objects[0U].previous_render_from_object.elements[14U] ==
                  -65.75F &&
              objects[1U].previous_render_from_object.elements[12U] ==
                  -15.75F &&
              objects[1U].previous_render_from_object.elements[13U] == 31.5F &&
              objects[1U].previous_render_from_object.elements[14U] ==
                  -63.25F,
          "object origin delta lost small relative terms on one or more axes");
  const auto &lights = retried.production.scene_snapshot->lights();
  Require(lights[0U].previous_position ==
              Float3{-15.75F, 31.5F, -63.25F} &&
              lights[1U].previous_position ==
                  Float3{-17.25F, 33.5F, -65.75F},
          "local-light origin delta lost small relative terms after retry");

  const std::shared_ptr<const SceneSnapshot> retry_published =
      producer.LoadPublishedSnapshot();
  GraphicsSceneFrameInput object_rejected = frame;
  object_rejected.simulation_tick = 43U;
  object_rejected.simulation_time_seconds = 3.0;
  object_rejected.absolute_world_origin_meters.x =
      (std::numeric_limits<double>::max)();
  object_rejected.lights.resize(1U);
  const GraphicsSceneSnapshotProduceResult object_failure =
      producer.Produce(object_rejected);
  Require(object_failure.validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              object_failure.validation.field ==
                  "static_meshes.previous_transform" &&
              SameSharedOwner(retry_published,
                              producer.LoadPublishedSnapshot()) &&
              retry_published->lights().size() == 3U,
          "failed object rebase committed light tombstones or publication");

  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  const GraphicsSceneSnapshotProduceResult object_retry =
      producer.Produce(frame);
  Require(object_retry.ok() &&
              object_retry.production.scene_snapshot->snapshot_id() == 3U &&
              object_retry.production.scene_snapshot->lights().size() == 3U,
          "object-rebase retry consumed identity or retained staged history");
}

void TestLightLifecycleAndAtomicPublication() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(151U);
  Require(producer.LoadPublishedSnapshot() == nullptr,
          "producer published a snapshot before successful production");

  GraphicsSceneFrameInput frame = MakeFrame();
  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  const std::shared_ptr<const SceneSnapshot> first_published =
      producer.LoadPublishedSnapshot();
  Require(first.ok() &&
              SameSharedOwner(first.production.scene_snapshot,
                              first_published),
          "first successful production was not release-published exactly");

  GraphicsSceneFrameInput invalid = frame;
  invalid.simulation_tick = 42U;
  invalid.simulation_time_seconds = 2.0;
  invalid.lights[2U].range = 0.0F;
  const GraphicsSceneSnapshotProduceResult rejected = producer.Produce(invalid);
  Require(rejected.validation.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              rejected.validation.element_index == 2U &&
              SameSharedOwner(first_published,
                              producer.LoadPublishedSnapshot()),
          "rejected light transaction replaced the atomic publication");

  GraphicsSceneFrameInput changed_type = frame;
  changed_type.simulation_tick = 42U;
  changed_type.simulation_time_seconds = 2.0;
  changed_type.lights[2U].type = LightType::SPOT;
  changed_type.lights[2U].inner_cone_radians = 0.2F;
  changed_type.lights[2U].outer_cone_radians = 0.4F;
  Require(producer.Produce(changed_type).validation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(first_published,
                              producer.LoadPublishedSnapshot()),
          "stable light identity changed type or publication atomically");

  GraphicsSceneFrameInput invalid_type = frame;
  invalid_type.simulation_tick = 42U;
  invalid_type.simulation_time_seconds = 2.0;
  invalid_type.lights[2U].type = static_cast<LightType>(255U);
  Require(producer.Produce(invalid_type).validation.code ==
                  ValidationCode::INVALID_ENUM &&
              SameSharedOwner(first_published,
                              producer.LoadPublishedSnapshot()),
          "unknown light enum reached lineage checks or changed publication");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  frame.lights[2U].position.x = 6.0F;
  const GraphicsSceneSnapshotProduceResult moved = producer.Produce(frame);
  const std::shared_ptr<const SceneSnapshot> moved_published =
      producer.LoadPublishedSnapshot();
  Require(moved.ok() &&
              SameSharedOwner(moved.production.scene_snapshot,
                              moved_published) &&
              !SameSharedOwner(first_published, moved_published) &&
              moved_published->lights()[0U].position.x == 6.0F &&
              moved_published->lights()[0U].previous_position.x == 4.0F &&
              first_published->lights()[0U].position.x == 4.0F,
          "successful light motion did not publish immutable temporal state");

  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  frame.lights.erase(frame.lights.begin() + 2);
  const GraphicsSceneSnapshotProduceResult removed = producer.Produce(frame);
  Require(removed.ok() && removed.production.scene_snapshot->lights().size() ==
                              2U,
          "authoritative light removal was rejected");
  const std::shared_ptr<const SceneSnapshot> removed_published =
      producer.LoadPublishedSnapshot();

  frame.simulation_tick = 44U;
  frame.simulation_time_seconds = 4.0;
  GraphicsSceneLightInput reused_point;
  reused_point.source_light_id = 100U;
  reused_point.type = LightType::POINT;
  reused_point.position = {6.0F, 3.0F, 1.0F};
  reused_point.range = 30.0F;
  frame.lights.push_back(reused_point);
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(removed_published,
                              producer.LoadPublishedSnapshot()),
          "destroyed light identity was reused or replaced publication");

  GraphicsSceneSnapshotProducer concurrent = MakeProducer(152U);
  GraphicsSceneFrameInput concurrent_frame = MakeFrame();
  Require(concurrent.Produce(concurrent_frame).ok(),
          "concurrent publication base frame was rejected");
  std::atomic<bool> writer_done{false};
  std::atomic<bool> writer_failed{false};
  std::thread writer([&concurrent, concurrent_frame, &writer_done,
                      &writer_failed]() mutable {
    for (std::uint64_t iteration = 0U; iteration < 500U; ++iteration) {
      concurrent_frame.simulation_tick = 42U + iteration;
      concurrent_frame.simulation_time_seconds = 2.0 +
                                                  static_cast<double>(iteration);
      concurrent_frame.lights[2U].position.x =
          4.0F + static_cast<float>(iteration) * 0.01F;
      if (!concurrent.Produce(concurrent_frame)) {
        writer_failed.store(true, std::memory_order_release);
        break;
      }
    }
    writer_done.store(true, std::memory_order_release);
  });

  std::uint64_t observed_snapshot_id = 0U;
  std::size_t observations = 0U;
  for (;;) {
    const std::shared_ptr<const SceneSnapshot> published =
        concurrent.LoadPublishedSnapshot();
    if (published != nullptr) {
      Require(published->snapshot_id() >= observed_snapshot_id &&
                  published->lights().size() == 3U &&
                  published->lighting_environment_hash() != 0U,
              "atomic reader observed torn or regressing scene state");
      observed_snapshot_id = published->snapshot_id();
      ++observations;
    }
    if (writer_done.load(std::memory_order_acquire)) {
      break;
    }
    std::this_thread::yield();
  }
  writer.join();
  const std::shared_ptr<const SceneSnapshot> final_published =
      concurrent.LoadPublishedSnapshot();
  Require(!writer_failed.load(std::memory_order_acquire) && observations > 0U &&
              final_published != nullptr &&
              final_published->snapshot_id() == 501U,
          "concurrent acquire-load/release-publication stress failed");
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

void TestMeshSignedZeroBytesRequireTopologyRevision() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(203U);
  GraphicsSceneFrameInput frame = MakeFrame();
  Require(producer.Produce(frame).ok(),
          "signed-zero mesh base frame was rejected");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  MeshResourceDescriptor mesh =
      std::get<MeshResourceDescriptor>(*frame.assets[3U].payload);
  mesh.positions.front().x = -0.0F;
  frame.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(mesh);
  const GraphicsSceneSnapshotProduceResult missing_revision =
      producer.Produce(frame);
  Require(missing_revision.validation.code ==
                  ValidationCode::REVISION_MISMATCH &&
              missing_revision.validation.element_index == 3U &&
              producer.asset_sequence() == 1U,
          "signed-zero mesh bytes changed without a topology revision");

  mesh.topology_revision = 2U;
  frame.assets[3U].payload =
      std::make_shared<const RenderAssetPayload>(mesh);
  const GraphicsSceneSnapshotProduceResult accepted = producer.Produce(frame);
  Require(accepted.ok() && accepted.production.asset_delta.has_value() &&
              accepted.production.asset_delta->sequence == 2U &&
              accepted.production.asset_delta->mutations.size() == 1U &&
              accepted.production.scene_snapshot->snapshot_id() == 2U,
          "revised signed-zero mesh bytes did not advance exact lineage");
  const RenderAssetMutation &mesh_mutation =
      MutationWithLowId(*accepted.production.asset_delta, 1U);
  Require(mesh_mutation.asset.revision == 2U &&
              EquivalentRenderAssetPayload(mesh_mutation.payload,
                                           *frame.assets[3U].payload),
          "mesh revision did not retain the exact signed-zero payload");

  const GraphicsSceneAssetRecoveryResult recovery =
      producer.BuildRecoveryAssetSnapshot();
  Require(recovery.ok() &&
              EquivalentRenderAssetPayload(
                  MutationWithLowId(recovery.full_snapshot, 1U).payload,
                  *frame.assets[3U].payload),
          "asset recovery lost revised signed-zero mesh bytes");
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
  AllocationProbe::Reset();
  AllocationProbe::Enable();
  const GraphicsSceneSnapshotProduceResult transformed = producer.Produce(frame);
  AllocationProbe::Disable();
  Require(transformed.ok() && !transformed.production.asset_delta.has_value(),
          "large transform-only frame was rejected or emitted an asset delta");
  Require(AllocationProbe::LargeCount() == 0U,
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
  AllocationProbe::Reset();
  AllocationProbe::Enable();
  const GraphicsSceneSnapshotProduceResult produced = producer.Produce(frame);
  AllocationProbe::Disable();
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
  return AllocationProbe::Count();
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
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) &&                     \
    _ITERATOR_DEBUG_LEVEL != 0
  // MSVC's checked containers allocate one bookkeeping proxy per temporary
  // vector/string even when the logical buffer is empty. The exact scale
  // equality above still catches every per-element allocation; this larger
  // fixed ceiling admits only the platform's constant Debug bookkeeping.
  constexpr std::size_t kStableAllocationBudget = 64U;
#else
  constexpr std::size_t kStableAllocationBudget = 16U;
#endif
  Require(large_count <= kStableAllocationBudget,
          "stable frame exceeded the fixed allocation budget");
}

void TestDynamicMeshLineageOwnershipAndTombstones() {
  using namespace RoR::Render;
  GraphicsSceneSnapshotProducer producer = MakeProducer(701U);
  GraphicsSceneFrameInput frame = MakeFrame();
  frame.assets.push_back(DynamicMeshAsset());
  const auto first_state = DynamicState(2U);
  frame.dynamic_meshes.push_back(DynamicObject(150U, first_state));

  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  Require(first.ok() && first.production.scene_snapshot->mesh_instances().size() ==
                            3U &&
              first.production.scene_snapshot->mesh_instances()[1U]
                      .instance_id == 150U &&
              first.production.scene_snapshot->dynamic_mesh_updates().size() ==
                  1U,
          "initial deformable object was not published in canonical order");
  const DynamicMeshUpdateDescriptor &first_update =
      first.production.scene_snapshot->dynamic_mesh_updates().front();
  Require(first_update.update_sequence == 1U &&
              first_update.deformation_revision == 2U &&
              first_update.positions == first_state->positions &&
              first_update.updated_local_bounds.minimum ==
                  first_state->updated_local_bounds.minimum &&
              first_update.updated_local_bounds.maximum ==
                  first_state->updated_local_bounds.maximum,
          "initial full deformation update lost revision, data, or bounds");

  frame.simulation_tick = 42U;
  frame.simulation_time_seconds = 2.0;
  const GraphicsSceneSnapshotProduceResult stable = producer.Produce(frame);
  Require(stable.ok() && !stable.production.asset_delta.has_value() &&
              stable.production.scene_snapshot->dynamic_mesh_updates().front()
                      .update_sequence == 2U &&
              stable.production.scene_snapshot->dynamic_mesh_updates().front()
                      .deformation_revision == 2U,
          "stable deformable state was not replayed with ordered frame lineage");

  frame.simulation_tick = 43U;
  frame.simulation_time_seconds = 3.0;
  frame.dynamic_meshes[0U].state = DynamicState(2U);
  const GraphicsSceneSnapshotProduceResult equivalent = producer.Produce(frame);
  Require(equivalent.ok() &&
              equivalent.production.scene_snapshot->dynamic_mesh_updates()
                      .front()
                      .update_sequence == 3U,
          "equivalent immutable deformation owner was not canonicalized");

  const std::shared_ptr<const SceneSnapshot> before_rejection =
      producer.LoadPublishedSnapshot();
  frame.simulation_tick = 44U;
  frame.simulation_time_seconds = 4.0;
  frame.dynamic_meshes[0U].state = DynamicState(2U, 0.25F);
  const GraphicsSceneSnapshotProduceResult stale_revision =
      producer.Produce(frame);
  Require(stale_revision.validation.code == ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(before_rejection,
                              producer.LoadPublishedSnapshot()),
          "changed deformation without the next revision mutated publication");

  frame.dynamic_meshes[0U].state = DynamicState(3U, 0.25F);
  const GraphicsSceneSnapshotProduceResult revised = producer.Produce(frame);
  Require(revised.ok() &&
              revised.production.scene_snapshot->dynamic_mesh_updates().front()
                      .update_sequence == 4U &&
              revised.production.scene_snapshot->dynamic_mesh_updates().front()
                      .deformation_revision == 3U,
          "rejected frame consumed update sequence or blocked exact retry");

  frame.simulation_tick = 45U;
  frame.simulation_time_seconds = 5.0;
  auto loose_state =
      std::make_shared<GraphicsSceneDynamicMeshState>(
          *DynamicState(3U, 0.25F));
  loose_state->updated_local_bounds.maximum.x += 1.0F;
  frame.dynamic_meshes[0U].state = loose_state;
  Require(producer.Produce(frame).validation.code ==
                  ValidationCode::INVALID_BOUNDS &&
              SameSharedOwner(revised.production.scene_snapshot,
                              producer.LoadPublishedSnapshot()),
          "non-tight deformation bounds were accepted or published");

  frame.dynamic_meshes[0U].state = DynamicState(3U, 0.25F);
  frame.dynamic_meshes.clear();
  const GraphicsSceneSnapshotProduceResult removed = producer.Produce(frame);
  Require(removed.ok() &&
              removed.production.scene_snapshot->dynamic_mesh_updates().empty(),
          "authoritative deformable removal was rejected");
  frame.simulation_tick = 46U;
  frame.simulation_time_seconds = 6.0;
  frame.dynamic_meshes.push_back(
      DynamicObject(150U, DynamicState(4U, 0.5F)));
  Require(producer.Produce(frame).validation.code ==
              ValidationCode::REVISION_MISMATCH,
          "destroyed deformable identity was resurrected");

  GraphicsSceneSnapshotProducerConfiguration stream_config;
  stream_config.registry_id = 704U;
  stream_config.maximum_dynamic_vertex_count = 3U;
  stream_config.maximum_dynamic_payload_bytes =
      3U * sizeof(Float3) + 3U * sizeof(Float3);
  GraphicsSceneSnapshotProducer stream_bounded(stream_config);
  GraphicsSceneFrameInput stream_frame = MakeFrame();
  stream_frame.assets.push_back(DynamicMeshAsset());
  stream_frame.dynamic_meshes.push_back(
      DynamicObject(350U, DynamicState(2U)));
  const GraphicsSceneSnapshotProduceResult exact_stream_bound =
      stream_bounded.Produce(stream_frame);
  Require(exact_stream_bound.ok(),
          "exact aggregate dynamic vertex/byte bound was rejected");
  const std::shared_ptr<const SceneSnapshot> exact_stream_snapshot =
      stream_bounded.LoadPublishedSnapshot();
  stream_frame.simulation_tick = 42U;
  stream_frame.simulation_time_seconds = 2.0;
  auto oversized_state =
      std::make_shared<GraphicsSceneDynamicMeshState>(*DynamicState(3U, 0.5F));
  oversized_state->velocities.assign(3U, Float3{});
  stream_frame.dynamic_meshes[0U].state = oversized_state;
  Require(stream_bounded.Produce(stream_frame).validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              SameSharedOwner(exact_stream_snapshot,
                              stream_bounded.LoadPublishedSnapshot()),
          "over-bound dynamic stream bytes were accepted or published");

  GraphicsSceneSnapshotProducerConfiguration vertex_config;
  vertex_config.registry_id = 705U;
  vertex_config.maximum_dynamic_vertex_count = 2U;
  GraphicsSceneSnapshotProducer vertex_bounded(vertex_config);
  Require(vertex_bounded.Produce(stream_frame).validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              vertex_bounded.LoadPublishedSnapshot() == nullptr,
          "over-bound aggregate dynamic vertices initialized publication");

  GraphicsSceneSnapshotProducer collision_producer = MakeProducer(702U);
  GraphicsSceneFrameInput collision = MakeFrame();
  collision.assets.push_back(DynamicMeshAsset());
  collision.dynamic_meshes.push_back(
      DynamicObject(collision.static_meshes.front().source_object_id,
                    DynamicState(2U)));
  Require(collision_producer.Produce(collision).validation.code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "static and dynamic objects did not share one identity namespace");

  GraphicsSceneSnapshotProducerConfiguration bounded_config;
  bounded_config.registry_id = 703U;
  bounded_config.maximum_dynamic_mesh_objects = 1U;
  GraphicsSceneSnapshotProducer bounded(bounded_config);
  GraphicsSceneFrameInput bounded_frame = MakeFrame();
  bounded_frame.assets.push_back(DynamicMeshAsset());
  bounded_frame.dynamic_meshes.push_back(
      DynamicObject(300U, DynamicState(2U)));
  Require(bounded.Produce(bounded_frame).ok(),
          "last configured dynamic object record was unavailable");
  bounded_frame.simulation_tick = 42U;
  bounded_frame.simulation_time_seconds = 2.0;
  bounded_frame.dynamic_meshes.clear();
  Require(bounded.Produce(bounded_frame).ok(),
          "bounded deformable retirement was rejected");
  bounded_frame.simulation_tick = 43U;
  bounded_frame.simulation_time_seconds = 3.0;
  bounded_frame.dynamic_meshes.push_back(
      DynamicObject(301U, DynamicState(2U)));
  Require(bounded.Produce(bounded_frame).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "dynamic lifetime bound ignored a tombstoned identity");
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

  GraphicsSceneSnapshotProducerConfiguration light_config;
  light_config.registry_id = 408U;
  light_config.maximum_light_records = 2U;
  GraphicsSceneSnapshotProducer light_producer(light_config);
  Require(light_producer.Produce(MakeFrame()).validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              light_producer.LoadPublishedSnapshot() == nullptr,
          "live light bound failure initialized or published producer state");
  GraphicsSceneFrameInput two_lights = MakeFrame();
  two_lights.lights.pop_back();
  Require(light_producer.Produce(two_lights).ok(),
          "last bounded light histories were not usable");
  two_lights.simulation_tick = 42U;
  two_lights.simulation_time_seconds = 2.0;
  two_lights.lights.erase(two_lights.lights.begin() + 1);
  GraphicsSceneLightInput replacement;
  replacement.source_light_id = 400U;
  replacement.type = LightType::POINT;
  replacement.position = {1.0F, 2.0F, 3.0F};
  replacement.range = 20.0F;
  two_lights.lights.push_back(replacement);
  Require(light_producer.Produce(two_lights).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "lifetime light-history bound ignored a tombstoned identity");

  GraphicsSceneSnapshotProducerConfiguration probe_config;
  probe_config.registry_id = 409U;
  probe_config.maximum_reflection_probe_records = 1U;
  GraphicsSceneSnapshotProducer probe_producer(probe_config);
  GraphicsSceneFrameInput two_probes = MakeFrame();
  two_probes.reflection_probes = {ReflectionProbe(1U, 1.0),
                                  ReflectionProbe(2U, 2.0)};
  Require(probe_producer.Produce(two_probes).validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              probe_producer.LoadPublishedSnapshot() == nullptr,
          "live reflection-probe bound initialized or published state");
  two_probes.reflection_probes.pop_back();
  Require(probe_producer.Produce(two_probes).ok(),
          "last bounded reflection-probe lineage was not usable");
  two_probes.simulation_tick = 42U;
  two_probes.simulation_time_seconds = 2.0;
  two_probes.reflection_probes.clear();
  Require(probe_producer.Produce(two_probes).ok(),
          "bounded reflection-probe retirement was rejected");
  two_probes.simulation_tick = 43U;
  two_probes.simulation_time_seconds = 3.0;
  two_probes.reflection_probes.push_back(ReflectionProbe(2U, 2.0));
  Require(probe_producer.Produce(two_probes).validation.code ==
              ValidationCode::VALUE_OUT_OF_RANGE,
          "lifetime reflection-probe bound ignored a tombstoned identity");
}

void TestDeterministicAcrossAdapterTraversalOrders() {
  using namespace RoR::Render;
  GraphicsSceneFrameInput lhs_frame = MakeFrame();
  GraphicsSceneFrameInput rhs_frame = lhs_frame;
  std::reverse(rhs_frame.assets.begin(), rhs_frame.assets.end());
  std::reverse(rhs_frame.static_meshes.begin(), rhs_frame.static_meshes.end());
  std::reverse(rhs_frame.lights.begin(), rhs_frame.lights.end());
  lhs_frame.reflection_probes = {ReflectionProbe(10U, 10.0),
                                 ReflectionProbe(20U, 20.0)};
  rhs_frame.reflection_probes = lhs_frame.reflection_probes;
  std::reverse(rhs_frame.reflection_probes.begin(),
               rhs_frame.reflection_probes.end());
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
  const auto &lhs_lights = lhs_output.production.scene_snapshot->lights();
  const auto &rhs_lights = rhs_output.production.scene_snapshot->lights();
  Require(lhs_lights.size() == rhs_lights.size(),
          "adapter traversal order changed light count");
  for (std::size_t index = 0U; index < lhs_lights.size(); ++index) {
    Require(lhs_lights[index].light_id == rhs_lights[index].light_id &&
                lhs_lights[index].type == rhs_lights[index].type &&
                lhs_lights[index].position == rhs_lights[index].position &&
                lhs_lights[index].direction == rhs_lights[index].direction,
            "adapter traversal order changed canonical light state");
  }
  Require(lhs_output.production.scene_snapshot
                  ->lighting_environment_hash() ==
              rhs_output.production.scene_snapshot
                  ->lighting_environment_hash(),
          "adapter traversal order changed lighting/environment digest");
  Require(lhs_output.production.scene_snapshot->reflection_probe_hash() ==
              rhs_output.production.scene_snapshot->reflection_probe_hash() &&
              lhs_output.production.scene_snapshot->reflection_probes().size() ==
                  2U &&
              lhs_output.production.scene_snapshot->reflection_probes()[0U]
                      .probe_id == 10U,
          "adapter traversal order changed reflection-probe state or digest");
}

void TestSceneGenerationFinalizationAndTickReset() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer(0x47454E4552415445ULL);
  const GraphicsSceneSnapshotProduceResult before_first =
      producer.FinalizeSceneGeneration();
  Require(!before_first &&
              before_first.validation.code ==
                  ValidationCode::MISSING_REFERENCE &&
              producer.LoadPublishedSnapshot() == nullptr &&
              !producer.has_open_scene_generation() &&
              producer.asset_sequence() == 0U,
          "pre-publication scene finalization changed producer state");

  GraphicsSceneFrameInput first_frame = MakeFrame();
  const GraphicsSceneSnapshotProduceResult first =
      producer.Produce(first_frame);
  Require(first.ok() && producer.has_open_scene_generation() &&
              first.production.asset_delta.has_value(),
          "first map generation was not published");
  const RenderAssetReference first_mesh =
      first.production.scene_snapshot->mesh_instances().front().mesh;
  const std::uint64_t first_asset_sequence = producer.asset_sequence();

  const GraphicsSceneSnapshotProduceResult finalized =
      producer.FinalizeSceneGeneration();
  Require(finalized.ok() &&
              finalized.production.scene_snapshot->snapshot_id() == 2U &&
              finalized.production.scene_snapshot->simulation_tick() ==
                  first_frame.simulation_tick &&
              finalized.production.scene_snapshot->mesh_instances().empty() &&
              finalized.production.scene_snapshot->lights().empty() &&
              finalized.production.asset_delta.has_value() &&
              finalized.production.asset_delta->base_sequence ==
                  first_asset_sequence &&
              finalized.production.asset_delta->mutations.size() ==
                  first_frame.assets.size() &&
              !producer.has_open_scene_generation(),
          "scene generation did not finalize as one empty tombstone transaction");
  for (const RenderAssetMutation &mutation :
       finalized.production.asset_delta->mutations) {
    Require(mutation.type == RenderAssetMutationType::DESTROY,
            "scene finalization retained a live old-generation asset");
  }

  GraphicsSceneFrameInput reloaded = MakeFrame();
  reloaded.simulation_tick = 0U;
  reloaded.simulation_time_seconds = 0.0;
  const GraphicsSceneSnapshotProduceResult second = producer.Produce(reloaded);
  Require(second.ok() && second.production.scene_snapshot->snapshot_id() == 3U &&
              second.production.scene_snapshot->simulation_tick() == 0U &&
              second.production.asset_delta.has_value() &&
              !second.production.asset_delta->full_snapshot &&
              second.production.scene_snapshot->mesh_instances().front().mesh !=
                  first_mesh &&
              second.production.scene_snapshot->mesh_instances().front()
                      .mesh.id.low() > first_mesh.id.low(),
          "reload tick zero reused retired renderer identity or regressed global lineage");
  const GraphicsSceneAssetRecoveryResult recovery =
      producer.BuildRecoveryAssetSnapshot();
  Require(recovery.ok() &&
              recovery.full_snapshot.mutations.size() ==
                  first_frame.assets.size() * 2U,
          "generation reset recovery omitted retired tombstones or new assets");

  GraphicsSceneSnapshotProducerConfiguration lifetime_config;
  lifetime_config.registry_id = 0x4C49464554494D45ULL;
  lifetime_config.maximum_asset_records = first_frame.assets.size();
  GraphicsSceneSnapshotProducer lifetime_bounded(lifetime_config);
  Require(lifetime_bounded.Produce(MakeFrame()).ok() &&
              lifetime_bounded.FinalizeSceneGeneration().ok(),
          "lifetime-cap fixture could not close its first generation");
  const std::shared_ptr<const SceneSnapshot> lifetime_sentinel =
      lifetime_bounded.LoadPublishedSnapshot();
  const std::uint64_t lifetime_sequence =
      lifetime_bounded.asset_sequence();
  GraphicsSceneFrameInput lifetime_reload = MakeFrame();
  lifetime_reload.simulation_tick = 0U;
  lifetime_reload.simulation_time_seconds = 0.0;
  const GraphicsSceneSnapshotProduceResult lifetime_rejected =
      lifetime_bounded.Produce(lifetime_reload);
  Require(!lifetime_rejected &&
              lifetime_rejected.validation.code ==
                  ValidationCode::VALUE_OUT_OF_RANGE &&
              lifetime_rejected.validation.field == "assets" &&
              SameSharedOwner(lifetime_sentinel,
                              lifetime_bounded.LoadPublishedSnapshot()) &&
              lifetime_bounded.asset_sequence() == lifetime_sequence &&
              !lifetime_bounded.has_open_scene_generation(),
          "generation-scoped source IDs bypassed the process-global lifetime "
          "asset-record cap or changed the final sentinel");

  GraphicsSceneSnapshotProducer empty_producer = MakeProducer(0x454D50545947454EULL);
  GraphicsSceneFrameInput empty_frame;
  empty_frame.simulation_tick = 9U;
  empty_frame.simulation_time_seconds = 0.5;
  empty_frame.camera.view_id = 1U;
  empty_frame.camera.width = 1280U;
  empty_frame.camera.height = 720U;
  empty_frame.camera.clip_from_view = Perspective();
  empty_frame.camera.far_plane = 1000.0F;
  const GraphicsSceneSnapshotProduceResult empty_first =
      empty_producer.Produce(empty_frame);
  const GraphicsSceneSnapshotProduceResult empty_final =
      empty_producer.FinalizeSceneGeneration();
  empty_frame.simulation_tick = 0U;
  empty_frame.simulation_time_seconds = 0.0;
  const GraphicsSceneSnapshotProduceResult empty_reload =
      empty_producer.Produce(empty_frame);
  Require(empty_first.ok(), "initially empty scene was rejected");
  Require(empty_final.ok(), "initially empty scene could not be finalized");
  Require(!empty_final.production.asset_delta.has_value(),
          "empty finalization emitted a spurious asset delta");
  Require(empty_reload.ok(), "empty reload tick zero was rejected");
  Require(empty_reload.production.scene_snapshot->snapshot_id() == 3U,
          "empty reload did not preserve global snapshot identity");

  GraphicsSceneSnapshotProducerConfiguration exhausted_config;
  exhausted_config.registry_id = 0x4641494C47454E31ULL;
  exhausted_config.first_snapshot_id =
      (std::numeric_limits<std::uint64_t>::max)();
  GraphicsSceneSnapshotProducer exhausted(exhausted_config);
  const GraphicsSceneSnapshotProduceResult last = exhausted.Produce(MakeFrame());
  const std::shared_ptr<const SceneSnapshot> sentinel =
      exhausted.LoadPublishedSnapshot();
  const std::uint64_t sentinel_sequence = exhausted.asset_sequence();
  const GraphicsSceneSnapshotProduceResult rejected =
      exhausted.FinalizeSceneGeneration();
  Require(last.ok() && !rejected &&
              rejected.validation.field == "snapshot_id" &&
              SameSharedOwner(sentinel, exhausted.LoadPublishedSnapshot()) &&
              exhausted.asset_sequence() == sentinel_sequence &&
              exhausted.has_open_scene_generation(),
          "failed generation finalization changed the published sentinel");
}

// Splits MakeFrame()'s static domain into an immutable retained owner pair,
// leaving only the deformable domain and its assets in the flat vectors.
void SplitFrameIntoRetainedOwners(
    RoR::Render::GraphicsSceneFrameInput &frame) {
  using namespace RoR::Render;
  auto owner_assets =
      std::make_shared<std::vector<GraphicsSceneAssetInput>>(frame.assets);
  std::sort(owner_assets->begin(), owner_assets->end(),
            [](const GraphicsSceneAssetInput &lhs,
               const GraphicsSceneAssetInput &rhs) {
              return lhs.source_asset_id < rhs.source_asset_id;
            });
  auto owner_meshes =
      std::make_shared<std::vector<GraphicsSceneStaticMeshInput>>(
          frame.static_meshes);
  std::sort(owner_meshes->begin(), owner_meshes->end(),
            [](const GraphicsSceneStaticMeshInput &lhs,
               const GraphicsSceneStaticMeshInput &rhs) {
              return lhs.source_object_id < rhs.source_object_id;
            });
  frame.assets.clear();
  frame.static_meshes.clear();
  frame.retained_static_assets = std::move(owner_assets);
  frame.retained_static_meshes = std::move(owner_meshes);
}

void AdvanceFrameTime(RoR::Render::GraphicsSceneFrameInput &frame) {
  ++frame.simulation_tick;
  frame.simulation_time_seconds += 1.0;
}

void TestRetainedStaticSectionReuseIsByteStableAndFailsClosed() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer(901U);
  GraphicsSceneFrameInput frame = MakeFrame();
  SplitFrameIntoRetainedOwners(frame);

  const GraphicsSceneSnapshotProduceResult adopted = producer.Produce(frame);
  Require(adopted.ok(), "retained-owner adoption frame was rejected");
  Require(adopted.production.diagnostics.retained_static_adoptions == 1U &&
              adopted.production.diagnostics.retained_static_block_reuses ==
                  0U,
          "first sight of a retained owner did not report an adoption");
  Require(adopted.production.scene_snapshot->mesh_instances().size() == 2U,
          "retained owners did not join the authoritative instance set");
  Require(adopted.production.asset_delta.has_value(),
          "retained owner assets produced no first catalog transaction");

  // Second frame with the exact same owners: still an adoption, because the
  // block's transform history only settles once previous == current.
  AdvanceFrameTime(frame);
  const GraphicsSceneSnapshotProduceResult settled = producer.Produce(frame);
  Require(settled.ok() && !settled.production.asset_delta.has_value(),
          "settling frame was rejected or rebuilt the catalog");

  AdvanceFrameTime(frame);
  const GraphicsSceneSnapshotProduceResult reused = producer.Produce(frame);
  Require(reused.ok(), "stable retained-owner frame was rejected");
  Require(reused.production.diagnostics.retained_static_block_reuses == 1U &&
              reused.production.diagnostics.retained_static_adoptions == 0U &&
              reused.production.diagnostics.retained_static_instances_reused ==
                  2U,
          "stable retained-owner frame did not reuse the instance block");
  Require(reused.production.diagnostics.asset_payload_full_validations == 0U &&
              reused.production.diagnostics
                      .asset_payload_fallback_comparisons == 0U,
          "reusing frame rescanned retained payloads");
  // The section is smaller than either window, so one frame audits all of it.
  Require(reused.production.diagnostics.retained_static_window_verifications ==
              static_cast<std::uint64_t>(
                  frame.retained_static_meshes->size() +
                  frame.retained_static_assets->size()),
          "reusing frame did not run the rotating drift audit");
  const std::vector<MeshInstanceDescriptor> &settled_instances =
      settled.production.scene_snapshot->mesh_instances();
  const std::vector<MeshInstanceDescriptor> &reused_instances =
      reused.production.scene_snapshot->mesh_instances();
  Require(settled_instances.size() == reused_instances.size(),
          "reused block changed the instance count");
  Require(std::memcmp(settled_instances.data(), reused_instances.data(),
                      settled_instances.size() *
                          sizeof(MeshInstanceDescriptor)) == 0,
          "republished retained block is not byte-identical");

  // A live deformable rides the reuse path: its own entry is recanonicalized
  // while the statics stay byte-identical, and compatibility narrows to it.
  GraphicsSceneFrameInput dynamic_frame = frame;
  dynamic_frame.assets.push_back(DynamicMeshAsset());
  dynamic_frame.dynamic_meshes.push_back(
      DynamicObject(150U, DynamicState(2U)));
  AdvanceFrameTime(dynamic_frame);
  const GraphicsSceneSnapshotProduceResult spawned =
      producer.Produce(dynamic_frame);
  Require(spawned.ok() &&
              spawned.production.scene_snapshot->mesh_instances().size() == 3U,
          "deformable spawn beside a retained owner was rejected");
  AdvanceFrameTime(dynamic_frame);
  const GraphicsSceneSnapshotProduceResult driving =
      producer.Produce(dynamic_frame);
  Require(driving.ok() &&
              driving.production.diagnostics.retained_static_block_reuses ==
                  1U,
          "stable frame with a live deformable did not reuse the block");
  Require(driving.production.diagnostics
                      .scene_asset_compatibility_full_validations == 0U &&
              driving.production.diagnostics
                      .scene_asset_compatibility_scoped_validations == 1U,
          "a live deformable still forced a full compatibility pass");
  Require(driving.production.scene_snapshot->dynamic_mesh_updates().size() ==
              1U,
          "reusing frame dropped the complete deformation update");

  // Removing the deformable is a signalled change: reuse is refused for that
  // frame and the identity is tombstoned exactly as on the full path.
  GraphicsSceneFrameInput despawned = frame;
  despawned.assets.push_back(DynamicMeshAsset());
  AdvanceFrameTime(despawned);
  despawned.simulation_tick += 2U;
  despawned.simulation_time_seconds += 2.0;
  const GraphicsSceneSnapshotProduceResult removed =
      producer.Produce(despawned);
  Require(removed.ok() &&
              removed.production.diagnostics
                      .retained_static_precondition_misses == 1U &&
              removed.production.scene_snapshot->mesh_instances().size() == 2U,
          "deformable removal did not refuse reuse and retire the instance");

  // An identity present in both the owner and the residue is a duplicate.
  GraphicsSceneFrameInput colliding = frame;
  colliding.assets.push_back(MaterialAsset());
  AdvanceFrameTime(colliding);
  colliding.simulation_tick += 4U;
  colliding.simulation_time_seconds += 4.0;
  Require(producer.Produce(colliding).validation.code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "an identity in both the owner and the residue was accepted");

  // An unsorted owner is a contract violation, not a case to canonicalize.
  GraphicsSceneFrameInput unsorted = frame;
  auto reversed = std::make_shared<std::vector<GraphicsSceneStaticMeshInput>>(
      *frame.retained_static_meshes);
  std::reverse(reversed->begin(), reversed->end());
  unsorted.retained_static_meshes = std::move(reversed);
  AdvanceFrameTime(unsorted);
  unsorted.simulation_tick += 6U;
  unsorted.simulation_time_seconds += 6.0;
  const GraphicsSceneSnapshotProduceResult misordered =
      producer.Produce(unsorted);
  Require(misordered.validation.code == ValidationCode::SEQUENCE_MISMATCH &&
              misordered.validation.field == "retained_static.order",
          "an unsorted retained owner was canonicalized instead of refused");

  // Test-only seam: mutate a published owner in place, which the adapter
  // contract forbids and nothing else can observe. The rotating audit must
  // reject the frame rather than republish a block that no longer describes
  // its inputs.
  GraphicsSceneFrameInput mutated = frame;
  AdvanceFrameTime(mutated);
  mutated.simulation_tick += 8U;
  mutated.simulation_time_seconds += 8.0;
  const auto &published_owner = *mutated.retained_static_meshes;
  const_cast<GraphicsSceneStaticMeshInput &>(published_owner.front())
      .render_from_object = Translation(77.0F);
  const GraphicsSceneSnapshotProduceResult drifted = producer.Produce(mutated);
  Require(drifted.validation.code == ValidationCode::REVISION_MISMATCH &&
              drifted.validation.field == "retained_static.window",
          "an in-place owner mutation was republished from the stale block");

  // The next frame rebuilds cleanly from the mutated bytes under the ordinary
  // rules, from the exact same owner: the drift surfaced once, it did not
  // wedge the producer into rejecting that owner forever.
  GraphicsSceneFrameInput recovered = mutated;
  AdvanceFrameTime(recovered);
  const GraphicsSceneSnapshotProduceResult rebuilt =
      producer.Produce(recovered);
  Require(rebuilt.ok() &&
              rebuilt.production.diagnostics.retained_static_adoptions == 1U,
          "the frame after a window rejection did not readopt cleanly");
  Require(rebuilt.production.scene_snapshot->mesh_instances()
                  .front()
                  .render_from_object.elements[12U] == 77.0F,
          "readoption did not pick up the changed static transform");

  // Dropping the owners is an omission: every retained identity is destroyed.
  GraphicsSceneFrameInput emptied = recovered;
  emptied.retained_static_assets.reset();
  emptied.retained_static_meshes.reset();
  AdvanceFrameTime(emptied);
  const GraphicsSceneSnapshotProduceResult tombstoned =
      producer.Produce(emptied);
  Require(tombstoned.ok() &&
              tombstoned.production.scene_snapshot->mesh_instances().empty() &&
              tombstoned.production.asset_delta.has_value(),
          "omitting the retained owners did not destroy their content");
  bool destroyed_any = false;
  for (const RenderAssetMutation &mutation :
       tombstoned.production.asset_delta->mutations) {
    destroyed_any = destroyed_any ||
                    mutation.type == RenderAssetMutationType::DESTROY;
  }
  Require(destroyed_any, "omitted retained assets were not tombstoned");
}

void TestRetainedReuseAcrossEveryLiveAssetCategory() {
  using namespace RoR::Render;

  // The combined runtime's asset set is a union of four things: the retained
  // static section (authored objects, terrain pages, procedural road), the
  // per-frame deformable domain, the producer-synthesized HUD overlay, and
  // the environment. A fixture that only spans the first two proves nothing
  // about whether the union those four form still validates on a reuse frame.
  const auto mesh_asset = [](std::uint64_t identity, bool dynamic_mesh) {
    GraphicsSceneAssetInput input;
    input.source_asset_id = identity;
    MeshResourceDescriptor mesh = MakeMesh();
    mesh.dynamic = dynamic_mesh;
    input.payload =
        std::make_shared<const RenderAssetPayload>(std::move(mesh));
    return input;
  };
  const auto material_asset = [](std::uint64_t identity) {
    GraphicsSceneAssetInput input;
    input.source_asset_id = identity;
    MaterialDescriptor material;
    material.debug_name = "retained union material";
    input.payload = std::make_shared<const RenderAssetPayload>(material);
    // Every material in the section depends on the one texture and sampler
    // the section also owns, so dependency resolution crosses the union.
    input.material_bindings[static_cast<std::size_t>(
        MaterialTextureSlot::BASE_COLOR)] = {30U, 40U};
    return input;
  };
  const auto static_object = [](std::uint64_t identity, std::uint64_t mesh,
                                std::uint64_t material, float offset) {
    GraphicsSceneStaticMeshInput input;
    input.source_object_id = identity;
    input.mesh_source_asset_id = mesh;
    input.material_source_asset_id = material;
    input.render_from_object = Translation(offset);
    return input;
  };

  GraphicsSceneFrameInput frame = MakeFrame();
  // Terrain-page and procedural-road style content, which on a live map lives
  // inside the retained owners beside the authored static objects.
  frame.assets.push_back(mesh_asset(60U, false));
  frame.assets.push_back(material_asset(61U));
  frame.assets.push_back(mesh_asset(70U, false));
  frame.assets.push_back(material_asset(71U));
  frame.static_meshes.push_back(static_object(600U, 60U, 61U, 11.0F));
  frame.static_meshes.push_back(static_object(700U, 70U, 71U, 13.0F));
  SplitFrameIntoRetainedOwners(frame);
  Require(frame.retained_static_assets->size() == 8U &&
              frame.retained_static_meshes->size() == 4U,
          "the retained section fixture does not span authored, terrain, and "
          "road style content");

  // Everything below stays in the per-frame vectors, exactly as the live
  // adapter hands them beside the section.
  frame.assets.push_back(mesh_asset(50U, true));
  frame.dynamic_meshes.push_back(DynamicObject(150U, DynamicState(2U)));
  GraphicsSceneHudOverlayInput hud;
  hud.width = 2U;
  hud.height = 2U;
  hud.content_hash = 0xC0FFEEU;
  hud.rgba8_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(16U, 0x20U);
  frame.hud_overlay = hud;

  GraphicsSceneSnapshotProducer producer = MakeProducer(903U);
  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  Require(first.ok(),
          "the adoption frame over the complete live asset union was "
          "rejected");
  Require(first.production.scene_snapshot->mesh_instances().size() == 5U &&
              first.production.scene_snapshot->hud_overlay().enabled,
          "the adoption frame lost a category of the asset union");

  // Two more frames: the second settles the transform history, the third is
  // the one that actually republishes the retained block. Both must validate
  // against the same union - this is the frame the live run rejected.
  for (std::size_t repeat = 0U; repeat < 2U; ++repeat) {
    AdvanceFrameTime(frame);
    const GraphicsSceneSnapshotProduceResult produced = producer.Produce(frame);
    Require(produced.ok(),
            "a frame reusing the retained section beside the live asset union "
            "was rejected");
    Require(produced.production.scene_snapshot->mesh_instances().size() == 5U,
            "a reusing frame lost an instance from the union");
    Require(produced.production.scene_snapshot->dynamic_mesh_updates().size() ==
                1U,
            "a reusing frame lost the deformable update");
    Require(produced.production.scene_snapshot->hud_overlay().enabled,
            "a reusing frame dropped the synthesized HUD overlay");
    Require(!produced.production.asset_delta.has_value(),
            "a stable frame over the complete union rebuilt the catalog");
  }
  Require(producer.LoadPublishedSnapshot()
                  ->mesh_instances()
                  .size() == 5U,
          "the published union lost content across the reuse frames");
}

void TestRetainedBlockSurvivesDeformableInterleaving() {
  using namespace RoR::Render;

  // The cached deformable positions are indices into a block whose entries
  // merge two identity domains. Identities below, between, and above the
  // retained static range each move those positions differently, so drive
  // every arrangement through spawn, steady state, and despawn.
  GraphicsSceneSnapshotProducer producer = MakeProducer(902U);
  GraphicsSceneFrameInput base = MakeFrame();
  base.assets.push_back(DynamicMeshAsset());
  SplitFrameIntoRetainedOwners(base);
  Require(base.retained_static_meshes->size() == 2U &&
              base.retained_static_meshes->front().source_object_id == 100U &&
              base.retained_static_meshes->back().source_object_id == 200U,
          "interleaving fixture does not straddle the static identity range");

  // Below the range, between the two statics, and above the range.
  const std::uint64_t kArrangements[][2U] = {
      {50U, 0U}, {150U, 0U}, {250U, 0U}, {50U, 150U},
      {150U, 250U}, {50U, 250U}, {0U, 0U},
  };
  std::map<std::uint64_t, std::uint64_t> live_revisions;
  // Simulation time may never move backwards, so it accumulates across every
  // arrangement rather than restarting from the fixture.
  std::uint64_t tick = base.simulation_tick;
  double seconds = base.simulation_time_seconds;
  const auto advance = [&](GraphicsSceneFrameInput &target) {
    ++tick;
    seconds += 1.0;
    target.simulation_tick = tick;
    target.simulation_time_seconds = seconds;
  };
  for (const auto &arrangement : kArrangements) {
    // Identities are permanent, so an arrangement may only introduce ones
    // never used before; each pass therefore adds a fresh generation.
    static std::uint64_t identity_generation = 0U;
    ++identity_generation;
    GraphicsSceneFrameInput frame = base;
    for (const std::uint64_t slot : arrangement) {
      if (slot == 0U) {
        continue;
      }
      const std::uint64_t identity = slot + (identity_generation * 1000U);
      const std::uint64_t revision = ++live_revisions[identity] + 1U;
      frame.dynamic_meshes.push_back(
          DynamicObject(identity, DynamicState(revision)));
    }
    std::size_t expected_dynamics = frame.dynamic_meshes.size();
    for (std::size_t repeat = 0U; repeat < 3U; ++repeat) {
      advance(frame);
      const GraphicsSceneSnapshotProduceResult produced =
          producer.Produce(frame);
      Require(produced.ok(),
              "a deformable arrangement around the retained range was "
              "rejected");
      Require(produced.production.scene_snapshot->mesh_instances().size() ==
                  2U + expected_dynamics,
              "interleaved deformables lost or duplicated an instance");
      Require(produced.production.scene_snapshot->dynamic_mesh_updates()
                      .size() == expected_dynamics,
              "interleaved deformables lost a complete update");
      const std::vector<MeshInstanceDescriptor> &instances =
          produced.production.scene_snapshot->mesh_instances();
      for (std::size_t index = 1U; index < instances.size(); ++index) {
        Require(instances[index - 1U].instance_id <
                    instances[index].instance_id,
                "republished block is not strictly ordered by identity");
      }
      // Every static entry must still carry its own transform, whichever
      // slots the deformables took around it.
      for (const MeshInstanceDescriptor &instance : instances) {
        if (instance.instance_id != 100U && instance.instance_id != 200U) {
          continue;
        }
        Require(instance.render_from_object.elements[12U] ==
                    (instance.instance_id == 100U ? 1.0F : 5.0F),
                "a retained static entry was patched by a deformable");
      }
    }
    // Retire the arrangement so the next one starts from the static-only set.
    frame.dynamic_meshes.clear();
    expected_dynamics = 0U;
    advance(frame);
    Require(producer.Produce(frame).ok(),
            "retiring an interleaved deformable arrangement was rejected");
  }
}

void TestHudOverlayAssetLifecycleAndRevisions() {
  using namespace RoR::Render;

  GraphicsSceneSnapshotProducer producer = MakeProducer();
  GraphicsSceneFrameInput frame = MakeFrame();
  GraphicsSceneHudOverlayInput hud;
  hud.width = 2U;
  hud.height = 2U;
  hud.content_hash = 0xF00DU;
  hud.rgba8_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(16U, 0x40U);
  frame.hud_overlay = hud;

  const GraphicsSceneSnapshotProduceResult first = producer.Produce(frame);
  Require(first.ok(), "first HUD overlay frame was rejected");
  Require(first.production.asset_delta.has_value(),
          "first HUD overlay frame carried no asset delta");
  const SceneSnapshot &first_scene = *first.production.scene_snapshot;
  Require(first_scene.hud_overlay().enabled,
          "the produced snapshot did not enable the HUD overlay");
  const RenderAssetReference first_material = first_scene.hud_overlay().material;
  Require(first_material.kind == RenderAssetKind::MATERIAL &&
              first_material.revision == 1U,
          "HUD overlay material reference is not a fresh material");
  std::size_t hud_texture_upserts = 0U;
  std::size_t hud_sampler_upserts = 0U;
  std::size_t hud_material_upserts = 0U;
  RenderAssetReference hud_texture_reference;
  for (const RenderAssetMutation &mutation :
       first.production.asset_delta->mutations) {
    if (const auto *texture =
            std::get_if<TextureResourceDescriptor>(&mutation.payload)) {
      if (texture->debug_name == "RoRHudOverlayTexture") {
        ++hud_texture_upserts;
        hud_texture_reference = mutation.asset;
        Require(texture->width == 2U && texture->height == 2U &&
                    texture->mip_levels.size() == 1U &&
                    texture->color_space == TextureColorSpace::SRGB &&
                    texture->mip_levels.front().bytes ==
                        *hud.rgba8_bytes,
                "HUD overlay texture payload was not published exactly");
      }
    } else if (const auto *sampler =
                   std::get_if<SamplerResourceDescriptor>(
                       &mutation.payload)) {
      if (sampler->debug_name == "RoRHudOverlaySampler") {
        ++hud_sampler_upserts;
        Require(sampler->mip_filter == SamplerFilter::NEAREST &&
                    sampler->address_u == SamplerAddressMode::CLAMP_TO_EDGE &&
                    sampler->maximum_lod == 0.0F,
                "HUD overlay sampler profile changed");
      }
    } else if (const auto *material =
                   std::get_if<MaterialDescriptor>(&mutation.payload)) {
      if (material->debug_name == "RoRHudOverlayMaterial") {
        ++hud_material_upserts;
        Require(mutation.asset == first_material,
                "descriptor HUD material differs from the delta mutation");
        Require(material->model == MaterialModel::UNLIT &&
                    material->blend_mode ==
                        MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER &&
                    material->base_color_transfer ==
                        BaseColorTransfer::
                            SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE &&
                    !material->depth_write &&
                    material->base_color_texture.texture.valid() &&
                    material->base_color_texture.sampler.valid(),
                "HUD overlay material profile changed");
      }
    }
  }
  Require(hud_texture_upserts == 1U && hud_sampler_upserts == 1U &&
              hud_material_upserts == 1U,
          "first HUD delta must publish texture, sampler, and material once");
  Require(hud_texture_reference.revision == 1U,
          "fresh HUD texture must start at revision one");

  // Unchanged content hash: no delta, unchanged references.
  frame.simulation_tick += 1U;
  frame.simulation_time_seconds += 0.016;
  const GraphicsSceneSnapshotProduceResult second = producer.Produce(frame);
  Require(second.ok() && !second.production.asset_delta.has_value(),
          "unchanged HUD content shipped an asset delta");
  Require(second.production.scene_snapshot->hud_overlay().material ==
              first_material,
          "unchanged HUD content changed its material reference");

  // Changed content hash: texture revision bump plus the material re-UPSERT
  // embedding the new texture revision; the sampler stays untouched.
  GraphicsSceneHudOverlayInput changed = hud;
  changed.content_hash = 0xBEEFU;
  changed.rgba8_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(16U, 0x80U);
  frame.hud_overlay = changed;
  frame.simulation_tick += 1U;
  const GraphicsSceneSnapshotProduceResult third = producer.Produce(frame);
  Require(third.ok() && third.production.asset_delta.has_value(),
          "changed HUD content shipped no asset delta");
  std::size_t changed_mutations = 0U;
  for (const RenderAssetMutation &mutation :
       third.production.asset_delta->mutations) {
    ++changed_mutations;
    Require(mutation.type == RenderAssetMutationType::UPSERT,
            "changed HUD content destroyed an asset");
    if (const auto *texture =
            std::get_if<TextureResourceDescriptor>(&mutation.payload)) {
      Require(texture->debug_name == "RoRHudOverlayTexture" &&
                  mutation.asset.id == hud_texture_reference.id &&
                  mutation.asset.revision == 2U &&
                  texture->mip_levels.front().bytes == *changed.rgba8_bytes,
              "changed HUD content did not bump exactly the texture revision");
    } else {
      const auto *material = std::get_if<MaterialDescriptor>(&mutation.payload);
      Require(material != nullptr &&
                  material->debug_name == "RoRHudOverlayMaterial" &&
                  mutation.asset.id == first_material.id &&
                  mutation.asset.revision == 2U &&
                  material->base_color_texture.texture.revision == 2U,
              "HUD material did not follow its texture revision");
    }
  }
  Require(changed_mutations == 2U,
          "changed HUD content must mutate exactly texture and material");
  Require(third.production.scene_snapshot->hud_overlay().material.revision ==
              2U,
          "descriptor HUD material did not follow the revision bump");

  // Absent HUD input tombstones the synthesized assets and disables the field.
  frame.hud_overlay.reset();
  frame.simulation_tick += 1U;
  const GraphicsSceneSnapshotProduceResult fourth = producer.Produce(frame);
  Require(fourth.ok() && fourth.production.asset_delta.has_value(),
          "HUD retirement shipped no asset delta");
  Require(!fourth.production.scene_snapshot->hud_overlay().enabled,
          "HUD retirement left the descriptor enabled");
  std::size_t destroys = 0U;
  for (const RenderAssetMutation &mutation :
       fourth.production.asset_delta->mutations) {
    Require(mutation.type == RenderAssetMutationType::DESTROY,
            "HUD retirement produced a non-destroy mutation");
    ++destroys;
  }
  Require(destroys == 3U,
          "HUD retirement must tombstone texture, sampler, and material");

  // Malformed inputs fail closed before any state advances.
  GraphicsSceneSnapshotProducer strict = MakeProducer();
  GraphicsSceneFrameInput malformed = MakeFrame();
  GraphicsSceneHudOverlayInput bad = hud;
  bad.width = 0U;
  malformed.hud_overlay = bad;
  Require(strict.Produce(malformed).validation.code ==
              ValidationCode::INVALID_DIMENSIONS,
          "zero HUD extent was accepted");
  bad = hud;
  bad.content_hash = 0U;
  malformed.hud_overlay = bad;
  Require(strict.Produce(malformed).validation.code ==
              ValidationCode::INVALID_IDENTIFIER,
          "zero HUD content hash was accepted");
  bad = hud;
  bad.rgba8_bytes = nullptr;
  malformed.hud_overlay = bad;
  Require(strict.Produce(malformed).validation.code ==
              ValidationCode::EMPTY_PAYLOAD,
          "HUD input without a byte owner was accepted");
  bad = hud;
  bad.rgba8_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(15U, 0x40U);
  malformed.hud_overlay = bad;
  Require(strict.Produce(malformed).validation.code ==
              ValidationCode::SIZE_MISMATCH,
          "mis-sized HUD byte payload was accepted");

  // The reserved source identities may never be minted by an adapter.
  GraphicsSceneSnapshotProducer collision_producer = MakeProducer();
  GraphicsSceneFrameInput collision = MakeFrame();
  GraphicsSceneAssetInput colliding_sampler = SamplerAsset();
  colliding_sampler.source_asset_id =
      kGraphicsSceneHudOverlayTextureSourceAssetId;
  collision.assets.push_back(colliding_sampler);
  collision.hud_overlay = hud;
  Require(collision_producer.Produce(collision).validation.code ==
              ValidationCode::DUPLICATE_IDENTIFIER,
          "a reserved HUD source identity collision was accepted");
}

} // namespace

int main() {
  TestJoinedSourceInitialSnapshotAndCanonicalOrder();
  TestJoinedSourceProducerRejectionDiscardsAndRetries();
  TestLegacyProducerVersionsRequireExplicitMigration();
  TestReflectionProbeLineageAndCanonicalOrder();
  TestTransformCameraHistoryAndOriginRebase();
  TestLargeOriginRebaseRollbackAndRetry();
  TestLightLifecycleAndAtomicPublication();
  TestAssetUpdatesDependencyRevisionAndDestroyRecovery();
  TestMeshSignedZeroBytesRequireTopologyRevision();
  TestMalformedFramesAreAtomicAndFailClosed();
  TestCanonicalSortingPreservesOriginalFailureIndices();
  TestStableFrameAvoidsLargePayloadCopiesAndComparisons();
  TestStableCatalogAllocationCountDoesNotScalePerElement();
  TestDynamicMeshLineageOwnershipAndTombstones();
  TestExhaustionAndBoundsFailClosed();
  TestDeterministicAcrossAdapterTraversalOrders();
  TestSceneGenerationFinalizationAndTickReset();
  TestRetainedStaticSectionReuseIsByteStableAndFailsClosed();
  TestRetainedReuseAcrossEveryLiveAssetCategory();
  TestRetainedBlockSurvivesDeformableInterleaving();
  TestHudOverlayAssetLifecycleAndRevisions();
  return EXIT_SUCCESS;
}
