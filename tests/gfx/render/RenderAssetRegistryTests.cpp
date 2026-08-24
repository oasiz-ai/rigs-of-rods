/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderAssetRegistry.h"
#include "SceneSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "render asset registry test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::RenderAssetId Id(std::uint64_t low) {
  return RoR::Render::RenderAssetId::FromWords(0x524F525F41535345ULL, low);
}

RoR::Render::RenderAssetReference Ref(RoR::Render::RenderAssetKind kind,
                                      std::uint64_t low,
                                      std::uint64_t revision = 1U) {
  return RoR::Render::RenderAssetReference::Create(kind, Id(low), revision);
}

RoR::Render::MeshResourceDescriptor MakeMesh() {
  using namespace RoR::Render;
  MeshResourceDescriptor mesh;
  mesh.debug_name = "registry triangle";
  mesh.local_bounds.minimum = {0.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {
      {0.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F},
  };
  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

RoR::Render::MeshResourceDescriptor MakeDistanceLodMesh() {
  using namespace RoR::Render;
  MeshResourceDescriptor mesh = MakeMesh();
  mesh.positions.push_back({1.0F, 1.0F, 0.0F});
  mesh.normals.push_back({0.0F, 0.0F, 1.0F});
  mesh.indices = {0U, 1U, 2U, 2U, 1U, 3U};
  MeshDistanceLodLevelDescriptor reduced;
  reduced.activation_distance_meters = 25.0F;
  reduced.indices = {0U, 1U, 2U};
  mesh.distance_lod_levels.push_back(std::move(reduced));
  Require(ValidateMeshResourceDescriptor(mesh).ok(),
          "distance-LOD mesh fixture is invalid");
  return mesh;
}

RoR::Render::TextureResourceDescriptor MakeBaseColorTexture() {
  using namespace RoR::Render;
  TextureResourceDescriptor texture;
  texture.debug_name = "one pixel sRGB";
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes = {255U, 255U, 255U, 255U};
  texture.mip_levels.push_back(mip);
  return texture;
}

RoR::Render::TextureResourceDescriptor MakeEnvironmentTexture() {
  using namespace RoR::Render;
  TextureResourceDescriptor texture;
  texture.debug_name = "linear HDR equirectangular environment";
  texture.format = TextureResourceFormat::RGBA16_FLOAT;
  texture.color_space = TextureColorSpace::LINEAR;
  texture.width = 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 8U;
  mip.layer_pitch_bytes = 8U;
  mip.bytes.resize(8U);
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RoR::Render::RenderAssetMutation Upsert(
    const RoR::Render::RenderAssetReference &asset,
    RoR::Render::RenderAssetPayload payload) {
  RoR::Render::RenderAssetMutation mutation;
  mutation.type = RoR::Render::RenderAssetMutationType::UPSERT;
  mutation.asset = asset;
  mutation.payload = std::move(payload);
  return mutation;
}

RoR::Render::RenderAssetMutation Destroy(
    const RoR::Render::RenderAssetReference &asset) {
  RoR::Render::RenderAssetMutation mutation;
  mutation.type = RoR::Render::RenderAssetMutationType::DESTROY;
  mutation.asset = asset;
  return mutation;
}

RoR::Render::RenderAssetDelta MakeBaseDelta(std::uint64_t registry_id) {
  using namespace RoR::Render;
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  delta.mutations.push_back(Upsert(Ref(RenderAssetKind::MESH, 1U), MakeMesh()));
  MaterialDescriptor material;
  material.debug_name = "plain PBR";
  delta.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MATERIAL, 2U), material));
  return delta;
}

struct ThrowingMeshPayload {
  operator RoR::Render::MeshResourceDescriptor() const {
    throw std::runtime_error("injected payload construction failure");
  }
};

RoR::Render::RenderAssetPayload MakeValuelessPayload() {
  using namespace RoR::Render;
  RenderAssetPayload payload;
  try {
    payload.emplace<MeshResourceDescriptor>(ThrowingMeshPayload{});
  } catch (const std::runtime_error &) {
  }
  Require(payload.valueless_by_exception(),
          "test could not produce a valueless asset payload");
  return payload;
}

RoR::Render::SceneSnapshotDescriptor MakeScene(std::uint64_t registry_id,
                                               std::uint64_t sequence,
                                               std::uint64_t material_revision) {
  using namespace RoR::Render;
  SceneSnapshotDescriptor scene;
  scene.snapshot_id = 9U;
  scene.asset_registry_id = registry_id;
  scene.asset_sequence = sequence;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Ref(RenderAssetKind::MESH, 1U);
  instance.material =
      Ref(RenderAssetKind::MATERIAL, 2U, material_revision);
  instance.local_bounds = MakeMesh().local_bounds;
  scene.mesh_instances.push_back(instance);
  return scene;
}

RoR::Render::SceneSnapshotDescriptor
MakeDynamicScene(std::uint64_t registry_id) {
  using namespace RoR::Render;
  SceneSnapshotDescriptor scene = MakeScene(registry_id, 1U, 1U);
  MeshInstanceDescriptor &instance = scene.mesh_instances.front();
  instance.deformation_revision = 2U;

  const MeshResourceDescriptor mesh = MakeMesh();
  DynamicMeshUpdateDescriptor update;
  update.update_sequence = 1U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.topology_revision = instance.topology_revision;
  update.deformation_revision = instance.deformation_revision;
  update.positions = mesh.positions;
  update.normals = mesh.normals;
  update.has_updated_bounds = true;
  update.updated_local_bounds = instance.local_bounds;
  scene.dynamic_mesh_updates.push_back(std::move(update));
  return scene;
}

void TestStableIdentity() {
  using namespace RoR::Render;
  static_assert(sizeof(RenderAssetId) == 16U,
                "portable asset identity must retain all 128 bits");
  static_assert(std::is_trivially_copyable_v<RenderAssetId>,
                "asset IDs must be cheap immutable values");
  Require(!RenderAssetId::FromWords(0U, 0U), "all-zero asset ID was accepted");
  Require(Id(1U) < Id(2U), "asset ID ordering is not lexicographic");
  Require(Ref(RenderAssetKind::MESH, 1U).valid(),
          "valid asset reference was rejected");
  Require(!RenderAssetReference::Create(RenderAssetKind::INVALID, Id(1U), 1U),
          "invalid asset kind was accepted");
  Require(!RenderAssetReference::Create(RenderAssetKind::MESH, Id(1U), 0U),
          "zero asset revision was accepted");
  std::unordered_set<RenderAssetId, RenderAssetIdHash> ids;
  ids.insert(Id(7U));
  ids.insert(Id(7U));
  ids.insert(Id(8U));
  Require(ids.size() == 2U, "asset ID hashing changed equality semantics");
}

void TestFloatingPayloadBitIdentity() {
  using namespace RoR::Render;

  RenderAssetPayload positive_zero_mesh = MakeMesh();
  RenderAssetPayload negative_zero_mesh = positive_zero_mesh;
  std::get<MeshResourceDescriptor>(negative_zero_mesh).positions.front().x =
      -0.0F;
  Require(!EquivalentRenderAssetPayload(positive_zero_mesh,
                                        negative_zero_mesh),
          "mesh payload equality collapsed signed-zero vertex bytes");

  RenderAssetPayload positive_zero_material = MaterialDescriptor{};
  RenderAssetPayload negative_zero_material = positive_zero_material;
  std::get<MaterialDescriptor>(negative_zero_material).metallic_factor = -0.0F;
  Require(ValidateMaterialDescriptor(
              std::get<MaterialDescriptor>(negative_zero_material))
              .ok(),
          "legal signed-zero material fixture was rejected");
  Require(!EquivalentRenderAssetPayload(positive_zero_material,
                                        negative_zero_material),
          "material payload equality collapsed signed-zero factor bytes");

  RenderAssetPayload positive_zero_sampler = SamplerResourceDescriptor{};
  RenderAssetPayload negative_zero_sampler = positive_zero_sampler;
  std::get<SamplerResourceDescriptor>(negative_zero_sampler).mip_lod_bias =
      -0.0F;
  Require(ValidateSamplerResourceDescriptor(
              std::get<SamplerResourceDescriptor>(negative_zero_sampler))
              .ok(),
          "legal signed-zero sampler fixture was rejected");
  Require(!EquivalentRenderAssetPayload(positive_zero_sampler,
                                        negative_zero_sampler),
          "sampler payload equality collapsed signed-zero state bytes");
}

void TestDistanceLodPayloadIdentity() {
  using namespace RoR::Render;

  const MeshResourceDescriptor authoritative = MakeDistanceLodMesh();
  MeshResourceDescriptor identical = authoritative;
  Require(EquivalentMeshResourceContents(authoritative, identical) &&
              EquivalentRenderAssetPayload(authoritative, identical),
          "identical distance-LOD ladder changed mesh identity");

  MeshResourceDescriptor changed_distance = authoritative;
  changed_distance.distance_lod_levels.front().activation_distance_meters =
      30.0F;
  Require(ValidateMeshResourceDescriptor(changed_distance).ok() &&
              !EquivalentMeshResourceContents(authoritative,
                                              changed_distance) &&
              !EquivalentRenderAssetPayload(authoritative,
                                            changed_distance),
          "distance-LOD activation bytes were omitted from mesh identity");

  MeshResourceDescriptor changed_indices = authoritative;
  changed_indices.distance_lod_levels.front().indices = {2U, 1U, 3U};
  Require(ValidateMeshResourceDescriptor(changed_indices).ok() &&
              !EquivalentMeshResourceContents(authoritative,
                                              changed_indices) &&
              !EquivalentRenderAssetPayload(authoritative, changed_indices),
          "distance-LOD index bytes were omitted from mesh identity");

  constexpr std::uint64_t kRegistry = 43U;
  RenderAssetDelta snapshot = MakeBaseDelta(kRegistry);
  snapshot.mutations.front().payload = authoritative;
  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(snapshot).ok(),
          "registry rejected authoritative distance-LOD snapshot");

  RenderAssetDelta conflicting_replay = snapshot;
  conflicting_replay.mutations.front().payload = changed_indices;
  Require(registry.Apply(conflicting_replay).code ==
              ValidationCode::REVISION_MISMATCH,
          "same-sequence replay changed immutable distance-LOD contents");
  const MeshResourceDescriptor *retained =
      registry.ResolveMesh(Ref(RenderAssetKind::MESH, 1U));
  Require(retained != nullptr &&
              EquivalentMeshResourceContents(*retained, authoritative),
          "conflicting distance-LOD replay mutated retained authority");
}

void TestOneSceneCanFeedTwoFrontendCatalogs() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 44U;
  RenderAssetRegistry producer(kRegistry);
  const RenderAssetDelta base = MakeBaseDelta(kRegistry);
  Require(producer.Apply(base).ok(), "producer rejected valid base catalog");
  Require(producer.live_count() == 2U && producer.sequence() == 1U,
          "producer catalog state is wrong");

  const RenderAssetDelta snapshot = producer.BuildFullSnapshot();
  Require(ValidateRenderAssetDelta(snapshot).ok(),
          "generated full snapshot is not replayable");
  RenderAssetRegistry ogre14_catalog(kRegistry);
  RenderAssetRegistry ogre_next_catalog(kRegistry);
  Require(ogre14_catalog.Apply(snapshot).ok(),
          "OGRE 1.14 mirror rejected neutral catalog");
  Require(ogre_next_catalog.Apply(snapshot).ok(),
          "Ogre-Next mirror rejected neutral catalog");

  const SceneSnapshotDescriptor scene = MakeScene(kRegistry, 1U, 1U);
  Require(ValidateSceneSnapshotAssets(scene, ogre14_catalog).ok(),
          "scene did not resolve in the first frontend catalog");
  Require(ValidateSceneSnapshotAssets(scene, ogre_next_catalog).ok(),
          "same scene did not resolve in the second frontend catalog");

  Require(ogre_next_catalog.Apply(snapshot).ok(),
          "identical same-sequence recovery replay was rejected");
  RenderAssetDelta conflicting = snapshot;
  std::get<MaterialDescriptor>(conflicting.mutations[1U].payload)
      .roughness_factor = 0.25F;
  Require(ogre_next_catalog.Apply(conflicting).code ==
              ValidationCode::REVISION_MISMATCH,
          "same sequence was allowed to identify different contents");

  RenderAssetDelta signed_zero_conflict = snapshot;
  MeshResourceDescriptor &signed_zero_mesh =
      std::get<MeshResourceDescriptor>(
          signed_zero_conflict.mutations.front().payload);
  signed_zero_mesh.positions.front().x = -0.0F;
  Require(ogre_next_catalog.Apply(signed_zero_conflict).code ==
              ValidationCode::REVISION_MISMATCH,
          "same asset revision accepted a different signed-zero bit pattern");
}

void TestMalformedPayloadsCannotMintRegistryTrust() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 45U;
  RenderAssetRegistry registry(kRegistry);

  RenderAssetDelta malformed_mesh = MakeBaseDelta(kRegistry);
  std::get<MeshResourceDescriptor>(malformed_mesh.mutations.front().payload)
      .positions.front()
      .x = std::numeric_limits<float>::infinity();
  Require(registry.Apply(malformed_mesh).code ==
              ValidationCode::NON_FINITE_VALUE,
          "registry admitted a mesh that bypassed standalone validation");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "failed malformed-mesh transaction minted registry state");

  RenderAssetDelta malformed_material = MakeBaseDelta(kRegistry);
  std::get<MaterialDescriptor>(malformed_material.mutations.back().payload)
      .roughness_factor = std::numeric_limits<float>::quiet_NaN();
  Require(registry.Apply(malformed_material).code ==
              ValidationCode::NON_FINITE_VALUE,
          "registry admitted a material that bypassed standalone validation");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "failed malformed-material transaction minted registry state");

  RenderAssetDelta malformed_texture;
  malformed_texture.registry_id = kRegistry;
  malformed_texture.sequence = 1U;
  malformed_texture.full_snapshot = true;
  TextureResourceDescriptor texture = MakeBaseColorTexture();
  texture.version = kTextureResourceDescriptorVersion + 1U;
  malformed_texture.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 3U), std::move(texture)));
  Require(registry.Apply(malformed_texture).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "registry admitted a malformed texture payload");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "failed malformed-texture transaction minted registry state");

  RenderAssetDelta malformed_sampler;
  malformed_sampler.registry_id = kRegistry;
  malformed_sampler.sequence = 1U;
  malformed_sampler.full_snapshot = true;
  SamplerResourceDescriptor sampler;
  sampler.version = kSamplerResourceDescriptorVersion + 1U;
  malformed_sampler.mutations.push_back(
      Upsert(Ref(RenderAssetKind::SAMPLER, 4U), sampler));
  Require(registry.Apply(malformed_sampler).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "registry admitted a malformed sampler payload");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "failed malformed-sampler transaction minted registry state");
}

void TestRegistryResolvedSceneRelationshipsRemainHostile() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 46U;
  RenderAssetDelta base = MakeBaseDelta(kRegistry);
  std::get<MeshResourceDescriptor>(base.mutations.front().payload).dynamic =
      true;
  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(base).ok(),
          "registry relationship fixture rejected its valid catalog");

  const SceneSnapshotDescriptor dynamic = MakeDynamicScene(kRegistry);
  Require(ValidateSceneSnapshotDescriptor(dynamic).ok() &&
              ValidateSceneSnapshotAssets(dynamic, registry).ok(),
          "registry relationship fixture rejected a valid dynamic scene");

  SceneSnapshotDescriptor stale = dynamic;
  stale.mesh_instances.front().mesh = Ref(RenderAssetKind::MESH, 1U, 2U);
  stale.dynamic_mesh_updates.front().mesh = stale.mesh_instances.front().mesh;
  Require(ValidateSceneSnapshotDescriptor(stale).ok() &&
              ValidateSceneSnapshotAssets(stale, registry).code ==
                  ValidationCode::MISSING_REFERENCE,
          "registry fast path accepted a stale mesh revision");

  SceneSnapshotDescriptor topology = dynamic;
  topology.mesh_instances.front().topology_revision = 2U;
  topology.dynamic_mesh_updates.front().topology_revision = 2U;
  Require(ValidateSceneSnapshotDescriptor(topology).ok() &&
              ValidateSceneSnapshotAssets(topology, registry).code ==
                  ValidationCode::MISSING_REFERENCE,
          "registry fast path accepted a stale topology relation");

  SceneSnapshotDescriptor bounds = MakeScene(kRegistry, 1U, 1U);
  bounds.mesh_instances.front().local_bounds.maximum.x = 2.0F;
  Require(ValidateSceneSnapshotDescriptor(bounds).ok() &&
              ValidateSceneSnapshotAssets(bounds, registry).code ==
                  ValidationCode::INVALID_BOUNDS,
          "registry fast path accepted base instance bounds unlike its mesh");

  SceneSnapshotDescriptor short_update = dynamic;
  short_update.dynamic_mesh_updates.front().positions.pop_back();
  short_update.dynamic_mesh_updates.front().normals.pop_back();
  Require(ValidateSceneSnapshotDescriptor(short_update).ok() &&
              ValidateSceneSnapshotAssets(short_update, registry).code ==
                  ValidationCode::SIZE_MISMATCH,
          "registry fast path accepted an update shorter than its live mesh");

  SceneSnapshotDescriptor missing_stream = dynamic;
  missing_stream.dynamic_mesh_updates.front().normals.clear();
  Require(ValidateSceneSnapshotDescriptor(missing_stream).ok() &&
              ValidateSceneSnapshotAssets(missing_stream, registry).code ==
                  ValidationCode::MISSING_REFERENCE,
          "registry fast path accepted an update missing a live mesh stream");
}

void TestRegistryResolvedCrossAssetRelationshipsRemainHostile() {
  using namespace RoR::Render;

  constexpr std::uint64_t kMaterialRegistry = 47U;
  RenderAssetDelta material_base = MakeBaseDelta(kMaterialRegistry);
  std::get<MeshResourceDescriptor>(material_base.mutations.front().payload)
      .normals.clear();
  RenderAssetRegistry material_registry(kMaterialRegistry);
  Require(material_registry.Apply(material_base).ok(),
          "cross-asset material fixture rejected valid individual assets");
  const SceneSnapshotDescriptor material_scene =
      MakeScene(kMaterialRegistry, 1U, 1U);
  Require(ValidateSceneSnapshotDescriptor(material_scene).ok() &&
              ValidateSceneSnapshotAssets(material_scene, material_registry)
                      .code == ValidationCode::MISSING_REFERENCE,
          "registry fast path accepted a PBR mesh without authored normals");

  constexpr std::uint64_t kEnvironmentRegistry = 48U;
  RenderAssetDelta environment_base;
  environment_base.registry_id = kEnvironmentRegistry;
  environment_base.sequence = 1U;
  environment_base.full_snapshot = true;
  environment_base.mutations.push_back(Upsert(
      Ref(RenderAssetKind::TEXTURE, 3U), MakeEnvironmentTexture()));
  environment_base.mutations.push_back(Upsert(
      Ref(RenderAssetKind::SAMPLER, 4U), SamplerResourceDescriptor{}));
  RenderAssetRegistry environment_registry(kEnvironmentRegistry);
  Require(environment_registry.Apply(environment_base).ok(),
          "cross-asset environment fixture rejected valid individual assets");
  SceneSnapshotDescriptor environment_scene;
  environment_scene.snapshot_id = 10U;
  environment_scene.asset_registry_id = kEnvironmentRegistry;
  environment_scene.asset_sequence = 1U;
  environment_scene.environment.environment_texture =
      Ref(RenderAssetKind::TEXTURE, 3U);
  environment_scene.environment.environment_sampler =
      Ref(RenderAssetKind::SAMPLER, 4U);
  Require(ValidateSceneSnapshotDescriptor(environment_scene).ok() &&
              ValidateSceneSnapshotAssets(environment_scene,
                                          environment_registry)
                      .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "registry fast path accepted an incompatible environment sampler");
}

void TestRevisionSequenceAndRecovery() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 55U;
  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(MakeBaseDelta(kRegistry)).ok(),
          "base catalog was rejected");
  const RenderAssetRecord *initial_mesh = registry.Find(Id(1U));
  Require(initial_mesh != nullptr && initial_mesh->payload != nullptr,
          "base mesh payload was not retained");
  const RenderAssetPayload *initial_mesh_payload = initial_mesh->payload.get();

  RenderAssetDelta update;
  update.registry_id = kRegistry;
  update.base_sequence = 1U;
  update.sequence = 2U;
  MaterialDescriptor material;
  material.debug_name = "roughness revision two";
  material.roughness_factor = 0.35F;
  update.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MATERIAL, 2U, 2U), material));
  Require(registry.Apply(update).ok(), "valid asset revision was rejected");
  Require(registry.Find(Id(1U))->payload.get() == initial_mesh_payload,
          "incremental update deep-copied an unchanged large asset payload");
  Require(registry.ResolveMaterial(Ref(RenderAssetKind::MATERIAL, 2U)) ==
              nullptr,
          "stale material revision remained resolvable");
  Require(registry.ResolveMaterial(
              Ref(RenderAssetKind::MATERIAL, 2U, 2U)) != nullptr,
          "current material revision was not resolvable");

  RenderAssetDelta gap = update;
  gap.base_sequence = 2U;
  gap.sequence = 4U;
  gap.mutations.front().asset.revision = 3U;
  Require(ValidateRenderAssetDelta(gap).code ==
              ValidationCode::SEQUENCE_MISMATCH,
          "asset sequence gap was accepted");

  RenderAssetDelta future_revision = update;
  future_revision.base_sequence = 2U;
  future_revision.sequence = 3U;
  future_revision.mutations.front().asset.revision = 4U;
  Require(ValidateRenderAssetDelta(future_revision).code ==
              ValidationCode::REVISION_MISMATCH,
          "asset revision newer than its transaction sequence was accepted");

  RenderAssetDelta stale_revision = update;
  stale_revision.base_sequence = 2U;
  stale_revision.sequence = 3U;
  Require(registry.Apply(stale_revision).code ==
              ValidationCode::REVISION_MISMATCH,
          "reused asset revision was accepted");

  const RenderAssetDelta recovery = registry.BuildFullSnapshot();
  RenderAssetRegistry recovered(kRegistry);
  Require(recovered.Apply(recovery).ok(),
          "fresh frontend could not recover from current full snapshot");
  Require(recovered.sequence() == 2U && recovered.live_count() == 2U,
          "recovered catalog differs from producer catalog");
  Require(ValidateSceneSnapshotAssets(MakeScene(kRegistry, 2U, 2U), recovered)
              .ok(),
          "current scene did not resolve after full recovery");
  Require(ValidateSceneSnapshotAssets(MakeScene(kRegistry, 1U, 1U), recovered)
                  .code == ValidationCode::SEQUENCE_MISMATCH,
          "stale scene catalog sequence was accepted");

  RenderAssetDelta rollback = registry.BuildFullSnapshot();
  rollback.sequence = 3U;
  rollback.mutations[1U].asset.revision = 1U;
  Require(registry.Apply(rollback).code == ValidationCode::REVISION_MISMATCH,
          "newer full snapshot rolled an asset revision backwards");
  RenderAssetDelta omission = registry.BuildFullSnapshot();
  omission.sequence = 3U;
  omission.mutations.pop_back();
  Require(registry.Apply(omission).code == ValidationCode::SEQUENCE_MISMATCH,
          "newer full snapshot omitted an existing asset");
  RenderAssetRegistry valid_forward(kRegistry);
  Require(valid_forward.Apply(registry.BuildFullSnapshot()).ok(),
          "revision-gap test registry could not recover current state");
  RenderAssetDelta valid_revision_gap = registry.BuildFullSnapshot();
  valid_revision_gap.sequence = 3U;
  valid_revision_gap.mutations.front().asset.revision = 2U;
  Require(valid_forward.Apply(valid_revision_gap).ok(),
          "full snapshot rejected a revision advance equal to sequence advance");
  RenderAssetDelta impossible_revision_gap = registry.BuildFullSnapshot();
  impossible_revision_gap.sequence = 3U;
  impossible_revision_gap.mutations.front().asset.revision = 3U;
  Require(registry.Apply(impossible_revision_gap).code ==
              ValidationCode::REVISION_MISMATCH,
          "full snapshot accepted more asset revisions than elapsed sequences");
  RenderAssetRegistry valid_new_asset(kRegistry);
  Require(valid_new_asset.Apply(registry.BuildFullSnapshot()).ok(),
          "new-asset lineage test could not recover current state");
  RenderAssetDelta valid_new_asset_snapshot = registry.BuildFullSnapshot();
  valid_new_asset_snapshot.sequence = 3U;
  valid_new_asset_snapshot.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MESH, 3U), MakeMesh()));
  Require(valid_new_asset.Apply(valid_new_asset_snapshot).ok(),
          "full snapshot rejected a new revision-one asset after one sequence");
  RenderAssetDelta impossible_new_asset = registry.BuildFullSnapshot();
  impossible_new_asset.sequence = 3U;
  impossible_new_asset.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MESH, 3U, 2U), MakeMesh()));
  Require(registry.Apply(impossible_new_asset).code ==
              ValidationCode::REVISION_MISMATCH,
          "full snapshot accepted a new high-revision asset without history");
  RenderAssetDelta kind_change = registry.BuildFullSnapshot();
  kind_change.sequence = 3U;
  kind_change.mutations.front().asset.kind = RenderAssetKind::TEXTURE;
  kind_change.mutations.front().payload = MakeBaseColorTexture();
  Require(registry.Apply(kind_change).code == ValidationCode::WRONG_ASSET_KIND,
          "newer full snapshot changed an existing asset kind");
  RenderAssetDelta future_full_revision = registry.BuildFullSnapshot();
  future_full_revision.sequence = 3U;
  future_full_revision.mutations.back().asset.revision = 4U;
  Require(ValidateRenderAssetDelta(future_full_revision).code ==
              ValidationCode::REVISION_MISMATCH,
          "full snapshot carried an asset revision newer than its sequence");
}

void TestDependencySafeTombstones() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 66U;
  RenderAssetDelta base;
  base.registry_id = kRegistry;
  base.sequence = 1U;
  base.full_snapshot = true;
  base.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 1U), MakeBaseColorTexture()));
  base.mutations.push_back(Upsert(Ref(RenderAssetKind::SAMPLER, 2U),
                                  SamplerResourceDescriptor{}));
  MaterialDescriptor textured;
  textured.base_color_texture.texture = Ref(RenderAssetKind::TEXTURE, 1U);
  textured.base_color_texture.sampler = Ref(RenderAssetKind::SAMPLER, 2U);
  base.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MATERIAL, 3U), textured));

  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(base).ok(),
          "valid material dependency graph was rejected");

  RenderAssetDelta unsafe_destroy;
  unsafe_destroy.registry_id = kRegistry;
  unsafe_destroy.base_sequence = 1U;
  unsafe_destroy.sequence = 2U;
  unsafe_destroy.mutations.push_back(
      Destroy(Ref(RenderAssetKind::TEXTURE, 1U, 2U)));
  Require(registry.Apply(unsafe_destroy).code ==
              ValidationCode::MISSING_REFERENCE,
          "texture still referenced by a live material was destroyed");
  Require(registry.sequence() == 1U && registry.live_count() == 3U,
          "failed dependency transaction mutated the registry");

  RenderAssetDelta safe_destroy = unsafe_destroy;
  MaterialDescriptor untextured;
  safe_destroy.mutations.push_back(
      Upsert(Ref(RenderAssetKind::MATERIAL, 3U, 2U), untextured));
  Require(registry.Apply(safe_destroy).ok(),
          "same-transaction dependency removal and tombstone failed");
  Require(registry.live_count() == 2U && registry.record_count() == 3U,
          "tombstone did not preserve identity history");

  RenderAssetDelta reuse;
  reuse.registry_id = kRegistry;
  reuse.base_sequence = 2U;
  reuse.sequence = 3U;
  reuse.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 1U, 3U), MakeBaseColorTexture()));
  Require(registry.Apply(reuse).code == ValidationCode::REVISION_MISMATCH,
          "tombstoned asset ID was reused");

  RenderAssetRegistry recovered(kRegistry);
  Require(recovered.Apply(registry.BuildFullSnapshot()).ok(),
          "full recovery lost a permanent tombstone");
  Require(recovered.Find(Id(1U)) != nullptr &&
              !recovered.Find(Id(1U))->live(),
          "recovered registry resurrected a tombstoned asset");

  RenderAssetDelta resurrection = registry.BuildFullSnapshot();
  resurrection.sequence = 3U;
  resurrection.mutations.front().type = RenderAssetMutationType::UPSERT;
  resurrection.mutations.front().asset.revision = 3U;
  resurrection.mutations.front().payload = MakeBaseColorTexture();
  Require(registry.Apply(resurrection).code ==
              ValidationCode::REVISION_MISMATCH,
          "authoritative full snapshot resurrected a tombstone");
}

void TestZeroCopyStableRecordVisitation() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 75U;
  RenderAssetDelta base = MakeBaseDelta(kRegistry);
  base.mutations.push_back(
      Upsert(Ref(RenderAssetKind::TEXTURE, 3U), MakeBaseColorTexture()));
  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(base).ok(),
          "record visitation fixture could not apply its base catalog");

  RenderAssetDelta tombstone;
  tombstone.registry_id = kRegistry;
  tombstone.base_sequence = 1U;
  tombstone.sequence = 2U;
  tombstone.mutations.push_back(
      Destroy(Ref(RenderAssetKind::TEXTURE, 3U, 2U)));
  Require(registry.Apply(tombstone).ok(),
          "record visitation fixture could not create a tombstone");

  std::vector<std::uint64_t> visited_ids;
  std::vector<const RenderAssetPayload *> visited_payloads;
  const ValidationResult visited = registry.VisitRecords(
      [&](const RenderAssetRecord &record) {
        visited_ids.push_back(record.asset.id.low());
        visited_payloads.push_back(record.payload.get());
        const RenderAssetRecord *stored = registry.Find(record.asset.id);
        Require(stored != nullptr && stored->payload.get() == record.payload.get(),
                "record visitation copied or substituted an immutable payload");
        return ValidationResult::Success();
      });
  Require(visited.ok() &&
              visited_ids == std::vector<std::uint64_t>({1U, 2U, 3U}),
          "record visitation was not stable asset-ID order");
  Require(visited_payloads.size() == 3U &&
              visited_payloads[0U] != nullptr &&
              visited_payloads[1U] != nullptr &&
              visited_payloads[2U] != nullptr &&
              std::holds_alternative<std::monostate>(*visited_payloads[2U]) &&
              !registry.Find(Id(3U))->live(),
          "record visitation omitted or materialized a tombstone payload");

  std::size_t calls = 0U;
  const ValidationResult stopped = registry.VisitRecords(
      [&](const RenderAssetRecord &) {
        ++calls;
        if (calls == 2U) {
          return ValidationResult::Failure(
              ValidationCode::VALUE_OUT_OF_RANGE, "visitor.stop",
              "intentional early-stop sentinel", 17U);
        }
        return ValidationResult::Success();
      });
  Require(calls == 2U && stopped.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              stopped.field == "visitor.stop" && stopped.element_index == 17U,
          "record visitation did not forward and stop on visitor failure");
}

void TestDeterministicOrderingAndRegistryIsolation() {
  using namespace RoR::Render;
  RenderAssetDelta reversed = MakeBaseDelta(77U);
  std::swap(reversed.mutations[0U], reversed.mutations[1U]);
  Require(ValidateRenderAssetDelta(reversed).code ==
              ValidationCode::NON_DETERMINISTIC_ORDER,
          "nondeterministically ordered asset delta was accepted");

  RenderAssetRegistry registry(77U);
  RenderAssetDelta foreign = MakeBaseDelta(78U);
  Require(registry.Apply(foreign).code == ValidationCode::INVALID_IDENTIFIER,
          "foreign registry delta was accepted");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "foreign transaction mutated the registry");
}

void TestValuelessPayloadFailsClosed() {
  using namespace RoR::Render;
  constexpr std::uint64_t kRegistry = 88U;

  RenderAssetPayload first_valueless = MakeValuelessPayload();
  RenderAssetPayload second_valueless = MakeValuelessPayload();
  Require(!EquivalentRenderAssetPayload(first_valueless, second_valueless),
          "valueless payloads were treated as equivalent contents");

  RenderAssetRecord hostile_record;
  hostile_record.asset = Ref(RenderAssetKind::MESH, 1U, 2U);
  hostile_record.payload = std::make_shared<const RenderAssetPayload>(
      std::move(first_valueless));
  Require(!hostile_record.live(),
          "valueless payload was reported as a live asset record");

  RenderAssetMutation destroy = Destroy(Ref(RenderAssetKind::MESH, 1U, 2U));
  destroy.payload = std::move(second_valueless);
  RenderAssetDelta delta;
  delta.registry_id = kRegistry;
  delta.sequence = 2U;
  delta.full_snapshot = true;
  delta.mutations.push_back(std::move(destroy));
  Require(ValidateRenderAssetDelta(delta).code == ValidationCode::EMPTY_PAYLOAD,
          "valueless destroy payload passed structural validation");

  RenderAssetRegistry registry(kRegistry);
  Require(registry.Apply(delta).code == ValidationCode::EMPTY_PAYLOAD,
          "registry accepted a valueless destroy payload");
  Require(registry.sequence() == 0U && registry.record_count() == 0U,
          "failed valueless transaction mutated the registry");
}

} // namespace

int main() {
  TestStableIdentity();
  TestFloatingPayloadBitIdentity();
  TestDistanceLodPayloadIdentity();
  TestOneSceneCanFeedTwoFrontendCatalogs();
  TestMalformedPayloadsCannotMintRegistryTrust();
  TestRegistryResolvedSceneRelationshipsRemainHostile();
  TestRegistryResolvedCrossAssetRelationshipsRemainHostile();
  TestRevisionSequenceAndRecovery();
  TestDependencySafeTombstones();
  TestZeroCopyStableRecordVisitation();
  TestDeterministicOrderingAndRegistryIsolation();
  TestValuelessPayloadFailsClosed();
  std::cout << "render asset registry tests passed\n";
  return EXIT_SUCCESS;
}
