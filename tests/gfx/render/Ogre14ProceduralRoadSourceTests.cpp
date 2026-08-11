/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14ProceduralRoadSource.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "OGRE 14 procedural-road source test failed: " << message
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

RoR::Render::Ogre14ProceduralRoadCapture MakeRoad(std::uint64_t id = 1U,
                                                  std::uint64_t revision = 1U) {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture capture;
  capture.stable_graphics_id = id;
  capture.topology_revision = revision;
  capture.exact_native_mesh_resource_group = "General";
  capture.exact_native_mesh_name = "RoadSystem-native-" + std::to_string(id);
  capture.exact_native_entity_name =
      "RoadSystem_Instance-native-" + std::to_string(id);
  capture.positions = {{0.0F, 0.0F, 0.0F},
                       {1.0F, 0.0F, 0.0F},
                       {1.0F, 1.0F, 0.0F},
                       {0.0F, 1.0F, 0.0F}};
  capture.exact_render_normals = {{0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F}};
  capture.texture_coordinates_0 = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
  capture.indices = {0U, 1U, 2U, 0U, 2U, 3U};
  capture.material.exact_resource_group = "General";
  capture.material.exact_name = "road2";
  capture.material.pass_count = 1U;
  capture.material.texture_unit_count = 0U;
  capture.material.diffuse_linear = {0.5F, 0.5F, 0.5F, 1.0F};
  capture.material.ambient_linear = {0.5F, 0.5F, 0.5F};
  capture.material.specular_linear = {0.1F, 0.1F, 0.1F};
  capture.material.shininess = 16.0F;
  capture.native_material_audit_complete = true;
  capture.finalized = true;
  return capture;
}

RoR::Render::Ogre14ProceduralRoadWindingProof
MakeWindingProof(const RoR::Render::Ogre14ProceduralRoadCapture &capture) {
  using namespace RoR::Render;
  Ogre14ProceduralRoadWindingProof proof;
  proof.exact_native_cull = capture.material.cull;
  proof.reverse_winding =
      capture.material.cull == Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  proof.complete = true;
  return proof;
}

void TestManagerIdentityAllocator() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadIdentityAllocator allocator(7U, 8U);
  Ogre14ProceduralRoadIdentityState first;
  ValidationResult result = allocator.Reserve(first);
  Require(result.ok() && first.stable_graphics_id() == 7U,
          "allocator did not issue configured first nonzero ID");
  result = allocator.FinalizeGeometry(first, "geometry-a");
  Require(result.ok() && first.topology_revision() == 1U &&
              first.lifecycle() == Ogre14ProceduralRoadIdentityLifecycle::LIVE,
          "first finalized geometry did not establish revision one");
  result = allocator.FinalizeGeometry(first, "geometry-a");
  Require(result.ok() && first.topology_revision() == 1U,
          "unchanged finalized geometry advanced topology revision");
  result = allocator.FinalizeGeometry(first, "geometry-b");
  Require(result.ok() && first.topology_revision() == 2U,
          "changed finalized geometry did not advance exactly once");
  result = allocator.Reserve(first);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate live registration did not fail closed");
  result = allocator.Tombstone(first);
  Require(result.ok() && first.lifecycle() ==
                             Ogre14ProceduralRoadIdentityLifecycle::TOMBSTONED,
          "live road was not permanently tombstoned");
  result = allocator.Reserve(first);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH,
          "removed road identity was resurrected");

  Ogre14ProceduralRoadIdentityState second;
  result = allocator.Reserve(second);
  Require(result.ok() && second.stable_graphics_id() == 8U &&
              allocator.next_id_for_diagnostics() == 0U,
          "last identity was not issued exactly once");
  Ogre14ProceduralRoadIdentityState exhausted;
  result = allocator.Reserve(exhausted);
  Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE &&
              exhausted.lifecycle() ==
                  Ogre14ProceduralRoadIdentityLifecycle::NEVER_REGISTERED,
          "identity exhaustion mutated caller state");
}

void TestGeometryKeyAndPayloadAudit() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture road = MakeRoad();
  std::string first_key = "sentinel";
  ValidationResult result =
      BuildOgre14ProceduralRoadGeometryStateKey(road, first_key);
  Require(result.ok() && !first_key.empty(),
          "valid road geometry state key was rejected");
  Ogre14ProceduralRoadCapture presentation_only = road;
  presentation_only.render_from_object.elements[12U] = 9.0F;
  presentation_only.visible = false;
  presentation_only.material.diffuse_linear.x = 0.25F;
  std::string second_key;
  result =
      BuildOgre14ProceduralRoadGeometryStateKey(presentation_only, second_key);
  Require(result.ok() && first_key == second_key,
          "non-geometry road state changed topology key");
  Ogre14ProceduralRoadCapture changed = road;
  changed.positions[2U].x = 1.25F;
  std::string changed_key;
  result = BuildOgre14ProceduralRoadGeometryStateKey(changed, changed_key);
  Require(result.ok() && changed_key != first_key,
          "changed finalized road geometry retained the same exact key");

  std::shared_ptr<const RenderAssetPayload> payload;
  result = BuildOgre14ProceduralRoadMeshPayload(road, MakeWindingProof(road),
                                                payload);
  Require(result.ok() && payload != nullptr,
          "valid road mesh payload was rejected");
  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(*payload);
  Require(mesh.index_format == MeshIndexFormat::UINT16 &&
              mesh.indices == road.indices &&
              mesh.positions == road.positions &&
              mesh.normals == road.exact_render_normals &&
              mesh.texture_coordinates_0 == road.texture_coordinates_0,
          "road mesh payload did not preserve exact promoted source streams");
  Require(mesh.local_bounds.minimum == Float3{0.0F, 0.0F, 0.0F} &&
              mesh.local_bounds.maximum == Float3{1.0F, 1.0F, 0.0F},
          "road mesh payload did not compute tight bounds");

  Ogre14ProceduralRoadCapture bad_winding = road;
  std::swap(bad_winding.indices[1U], bad_winding.indices[2U]);
  std::string untouched = "untouched";
  result = BuildOgre14ProceduralRoadGeometryStateKey(bad_winding, untouched);
  Require(!result && result.field == "road.triangle_winding" &&
              untouched == "untouched",
          "normal/winding mismatch was not transactional");
  Ogre14ProceduralRoadCapture bad_index = road;
  bad_index.indices[2U] = 65536U;
  result = BuildOgre14ProceduralRoadMeshPayload(
      bad_index, MakeWindingProof(bad_index), payload);
  Require(!result && result.field == "road.indices",
          "invalid promoted uint16 index was accepted");
}

void TestCacheRevisionAndOwnerReuse() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture road = MakeRoad();
  const Ogre14ProceduralRoadWindingProof road_winding = MakeWindingProof(road);
  Ogre14ProceduralRoadCacheEntry first;
  ValidationResult result =
      ResolveOgre14ProceduralRoadCacheEntry(road, road_winding, nullptr, first);
  Require(result.ok() && first.mesh_payload != nullptr,
          "initial road cache entry was rejected");

  Ogre14ProceduralRoadCacheEntry unchanged;
  result = ResolveOgre14ProceduralRoadCacheEntry(road, road_winding, &first,
                                                 unchanged);
  Require(result.ok() &&
              SameSharedOwner(first.mesh_payload, unchanged.mesh_payload),
          "byte-identical road cache lookup did not reuse immutable owner");

  Ogre14ProceduralRoadCapture no_cull = road;
  no_cull.material.cull = Ogre14GraphicsSceneMaterialCull::NONE;
  Ogre14ProceduralRoadCacheEntry no_cull_entry;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      no_cull, MakeWindingProof(no_cull), &first, no_cull_entry);
  Require(result.ok() &&
              no_cull_entry.exact_winding_proof.exact_native_cull ==
                  Ogre14GraphicsSceneMaterialCull::NONE &&
              SameSharedOwner(first.mesh_payload, no_cull_entry.mesh_payload),
          "same unreversed bytes did not survive a NONE cull proof update");
  Ogre14ProceduralRoadCacheEntry clockwise_after_none;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      road, road_winding, &no_cull_entry, clockwise_after_none);
  Require(result.ok() &&
              clockwise_after_none.exact_winding_proof.exact_native_cull ==
                  Ogre14GraphicsSceneMaterialCull::CLOCKWISE &&
              SameSharedOwner(first.mesh_payload,
                              clockwise_after_none.mesh_payload),
          "NONE-to-CLOCKWISE proof update replaced identical payload bytes");

  Ogre14ProceduralRoadCapture reversed = road;
  reversed.material.cull = Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  const Ogre14ProceduralRoadWindingProof reversed_winding =
      MakeWindingProof(reversed);
  Ogre14ProceduralRoadCacheEntry reversed_entry;
  result = ResolveOgre14ProceduralRoadCacheEntry(reversed, reversed_winding,
                                                 &first, reversed_entry);
  Require(result.ok() && reversed_entry.mesh_payload != nullptr,
          "same-geometry cull flip was rejected");
  const MeshResourceDescriptor &reversed_mesh =
      std::get<MeshResourceDescriptor>(*reversed_entry.mesh_payload);
  Require(
      reversed_entry.topology_revision == 1U &&
          !SameSharedOwner(first.mesh_payload, reversed_entry.mesh_payload) &&
          reversed_mesh.indices ==
              std::vector<std::uint32_t>{0U, 2U, 1U, 0U, 3U, 2U},
      "same-geometry cull flip reused oppositely wound payload bytes");

  Ogre14ProceduralRoadCacheEntry repeated_reversed;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      reversed, reversed_winding, &reversed_entry, repeated_reversed);
  Require(result.ok() && SameSharedOwner(reversed_entry.mesh_payload,
                                         repeated_reversed.mesh_payload),
          "unchanged reverse-winding proof did not reuse immutable owner");

  Ogre14ProceduralRoadCacheEntry proof_sentinel = unchanged;
  const auto proof_sentinel_owner = proof_sentinel.mesh_payload;
  Ogre14ProceduralRoadWindingProof incomplete_proof;
  result = ResolveOgre14ProceduralRoadCacheEntry(road, incomplete_proof, &first,
                                                 proof_sentinel);
  Require(
      !result && result.code == ValidationCode::MISSING_REFERENCE &&
          SameSharedOwner(proof_sentinel_owner, proof_sentinel.mesh_payload),
      "absent winding proof mutated cache output");

  Ogre14ProceduralRoadWindingProof inconsistent_proof = road_winding;
  inconsistent_proof.reverse_winding = true;
  result = ResolveOgre14ProceduralRoadCacheEntry(road, inconsistent_proof,
                                                 &first, proof_sentinel);
  Require(
      !result && result.code == ValidationCode::REVISION_MISMATCH &&
          SameSharedOwner(proof_sentinel_owner, proof_sentinel.mesh_payload),
      "inconsistent winding proof mutated cache output");

  Ogre14ProceduralRoadCacheEntry forged_previous = first;
  forged_previous.exact_winding_proof = reversed_winding;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      reversed, reversed_winding, &forged_previous, proof_sentinel);
  Require(
      !result && result.code == ValidationCode::REVISION_MISMATCH &&
          result.field == "road.cache.mesh_payload.reverse_winding" &&
          SameSharedOwner(proof_sentinel_owner, proof_sentinel.mesh_payload),
      "forged cache proof/payload mismatch was reused or published");

  Ogre14ProceduralRoadCapture changed = road;
  changed.positions[2U].x = 1.25F;
  changed.topology_revision = 2U;
  Ogre14ProceduralRoadCacheEntry rebuilt;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      changed, MakeWindingProof(changed), &first, rebuilt);
  Require(result.ok() && rebuilt.topology_revision == 2U &&
              !SameSharedOwner(first.mesh_payload, rebuilt.mesh_payload),
          "changed road did not create exactly one new immutable revision");

  changed.topology_revision = 1U;
  Ogre14ProceduralRoadCacheEntry sentinel = unchanged;
  const auto sentinel_owner = sentinel.mesh_payload;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      changed, MakeWindingProof(changed), &first, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(sentinel_owner, sentinel.mesh_payload),
          "bad changed-geometry revision mutated cache output");

  Ogre14ProceduralRoadCapture false_advance = road;
  false_advance.topology_revision = 2U;
  result = ResolveOgre14ProceduralRoadCacheEntry(
      false_advance, MakeWindingProof(false_advance), &first, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH,
          "unchanged geometry was allowed to advance revision");
}

void TestInventoryLifecycleOrderingAndCompatibility() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture first = MakeRoad(1U);
  Ogre14ProceduralRoadCapture second = MakeRoad(2U);
  second.render_from_object.elements[12U] = 20.0F;

  Ogre14ProceduralRoadInventory inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sections;
  ValidationResult result =
      BuildOgre14ProceduralRoadInventory({second, first}, inventory, sections);
  Require(result.ok() && sections.size() == 2U &&
              inventory.live_identity_count() == 2U &&
              inventory.known_identity_count() == 2U &&
              inventory.cached_mesh_count() == 2U,
          "valid complete road inventory was rejected");
  Require(std::is_sorted(sections.begin(), sections.end(),
                         [](const auto &lhs, const auto &rhs) {
                           return lhs.stable_object_id < rhs.stable_object_id;
                         }),
          "road section output was not canonically ordered");

  Ogre14ProceduralRoadInventory reordered_inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> reordered_sections;
  result = BuildOgre14ProceduralRoadInventory(
      {first, second}, reordered_inventory, reordered_sections);
  Require(result.ok() && reordered_sections.size() == sections.size(),
          "reordered source inventory was rejected");
  for (std::size_t index = 0U; index < sections.size(); ++index) {
    Require(reordered_sections[index].stable_object_id ==
                    sections[index].stable_object_id &&
                reordered_sections[index].exact_entity_name ==
                    sections[index].exact_entity_name,
            "source order changed canonical road section output");
  }

  const auto first_owner = sections.front().mesh_payload;
  result =
      BuildOgre14ProceduralRoadInventory({first, second}, inventory, sections);
  Require(result.ok() &&
              SameSharedOwner(first_owner, sections.front().mesh_payload),
          "stable road inventory did not reuse immutable mesh owner");

  Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
  result = BuildOgre14GraphicsSceneStaticInventory(sections, static_registry,
                                                   assets, static_meshes);
  Require(result.ok() && assets.size() == 3U && static_meshes.size() == 2U,
          "road sections did not satisfy generic static inventory contract");

  result = BuildOgre14ProceduralRoadInventory({first}, inventory, sections);
  Require(result.ok() && inventory.live_identity_count() == 1U &&
              inventory.known_identity_count() == 2U,
          "road omission did not create a permanent tombstone");
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sentinel = sections;
  result =
      BuildOgre14ProceduralRoadInventory({first, second}, inventory, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              sentinel.size() == sections.size() &&
              inventory.live_identity_count() == 1U,
          "removed road resurrection mutated inventory or output");
}

void TestCombinedAuthoritativeStaticTransaction() {
  using namespace RoR::Render;
  const Ogre14ProceduralRoadCapture road = MakeRoad(7U);

  Ogre14ProceduralRoadInventory durable_road_inventory;
  Ogre14ProceduralRoadInventory candidate_road_inventory =
      durable_road_inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> road_sections;
  ValidationResult result = BuildOgre14ProceduralRoadInventory(
      {road}, candidate_road_inventory, road_sections);
  Require(result.ok() && road_sections.size() == 1U &&
              durable_road_inventory.known_identity_count() == 0U,
          "road preparation mutated the durable inventory before commit");

  Ogre14GraphicsSceneStaticSectionCaptureInput terrain_section;
  terrain_section.stable_object_id = road_sections.front().stable_object_id;
  terrain_section.section_index = 0U;
  terrain_section.exact_entity_name = "terrain/page/0";
  terrain_section.mesh_identity.exact_resource_group = "General";
  terrain_section.mesh_identity.exact_mesh_name = "terrain/page/0/lod0";
  terrain_section.mesh_identity.vertex_count =
      static_cast<std::uint32_t>(road.positions.size());
  terrain_section.mesh_identity.index_count =
      static_cast<std::uint32_t>(road.indices.size());
  terrain_section.mesh_payload = road_sections.front().mesh_payload;
  terrain_section.material = road.material;

  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> combined{
      terrain_section, road_sections.front()};
  Ogre14GraphicsSceneStaticIdentityRegistry durable_static_registry;
  Ogre14GraphicsSceneStaticIdentityRegistry candidate_static_registry =
      durable_static_registry;
  std::vector<GraphicsSceneAssetInput> assets{{}};
  assets.front().source_asset_id = 99U;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes{{}};
  static_meshes.front().source_object_id = 99U;
  result = BuildOgre14GraphicsSceneStaticInventory(
      combined, candidate_static_registry, assets, static_meshes);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
              assets.size() == 1U && assets.front().source_asset_id == 99U &&
              static_meshes.size() == 1U &&
              static_meshes.front().source_object_id == 99U &&
              candidate_static_registry.asset_identity_count() == 0U &&
              candidate_static_registry.object_identity_count() == 0U &&
              durable_road_inventory.known_identity_count() == 0U,
          "combined static collision committed road or generic output state");

  terrain_section.stable_object_id =
      road_sections.front().stable_object_id ^ (1ULL << 63U);
  if (terrain_section.stable_object_id == 0U) {
    terrain_section.stable_object_id = 1U;
  }
  combined = {road_sections.front(), terrain_section};
  candidate_static_registry = durable_static_registry;
  result = BuildOgre14GraphicsSceneStaticInventory(
      combined, candidate_static_registry, assets, static_meshes);
  Require(result.ok() && static_meshes.size() == 2U &&
              std::is_sorted(static_meshes.begin(), static_meshes.end(),
                             [](const auto &lhs, const auto &rhs) {
                               return lhs.source_object_id <
                                      rhs.source_object_id;
                             }),
          "road and terrain sections did not form one ordered inventory");

  durable_road_inventory = std::move(candidate_road_inventory);
  durable_static_registry = std::move(candidate_static_registry);
  Require(durable_road_inventory.known_identity_count() == 1U &&
              durable_road_inventory.live_identity_count() == 1U,
          "successful combined static transaction did not commit road state");

  const auto prior_road_owner = road_sections.front().mesh_payload;
  candidate_road_inventory = durable_road_inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> repeated_roads;
  result = BuildOgre14ProceduralRoadInventory({road}, candidate_road_inventory,
                                              repeated_roads);
  Require(result.ok() && repeated_roads.size() == 1U &&
              SameSharedOwner(prior_road_owner,
                              repeated_roads.front().mesh_payload),
          "committed combined inventory did not reuse the road mesh owner");
}

void TestInventoryFailureGatesAndBounds() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture road = MakeRoad();
  Ogre14ProceduralRoadInventory inventory;
  Ogre14GraphicsSceneStaticSectionCaptureInput marker;
  marker.stable_object_id = 99U;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> output{marker};

  Ogre14ProceduralRoadCapture textured = road;
  textured.material.texture_unit_count = 1U;
  ValidationResult result =
      BuildOgre14ProceduralRoadInventory({textured}, inventory, output);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
              result.field == "road.material.texture_units" &&
              output.size() == 1U && output.front().stable_object_id == 99U &&
              inventory.known_identity_count() == 0U,
          "textured road2 did not fail closed transactionally");

  Ogre14ProceduralRoadCapture renamed = road;
  renamed.material.exact_name = "guessed-road-pbr";
  result = BuildOgre14ProceduralRoadInventory({renamed}, inventory, output);
  Require(!result && result.field == "road.material.exact_name",
          "non-road2 material identity was accepted");

  Ogre14ProceduralRoadCapture mirrored = road;
  mirrored.render_from_object.elements[0U] = -1.0F;
  result = BuildOgre14ProceduralRoadInventory({mirrored}, inventory, output);
  Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE,
          "mirrored road transform was accepted without rebasing");

  result = BuildOgre14ProceduralRoadInventory({road, road}, inventory, output);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate manager road ID was accepted");

  Ogre14ProceduralRoadInventoryConfiguration limited;
  limited.maximum_live_roads = 1U;
  limited.maximum_lifetime_roads = 1U;
  limited.maximum_payload_bytes = 1024U;
  Ogre14ProceduralRoadInventory limited_inventory(limited);
  result =
      BuildOgre14ProceduralRoadInventory({road}, limited_inventory, output);
  Require(result.ok(), "bounded one-road inventory was rejected");
  result = BuildOgre14ProceduralRoadInventory({}, limited_inventory, output);
  Require(result.ok() && limited_inventory.live_identity_count() == 0U,
          "bounded road removal failed");
  result = BuildOgre14ProceduralRoadInventory({MakeRoad(2U)}, limited_inventory,
                                              output);
  Require(!result && result.field == "road_inventory.lifetime_roads" &&
              limited_inventory.known_identity_count() == 1U,
          "lifetime identity exhaustion did not fail transactionally");

  Ogre14ProceduralRoadInventoryConfiguration live_limited;
  live_limited.maximum_live_roads = 1U;
  Ogre14ProceduralRoadInventory live_limited_inventory(live_limited);
  result = BuildOgre14ProceduralRoadInventory({road, MakeRoad(2U)},
                                              live_limited_inventory, output);
  Require(!result && result.field == "road_inventory.live_roads" &&
              live_limited_inventory.known_identity_count() == 0U,
          "live road bound did not fail transactionally");

  Ogre14ProceduralRoadInventoryConfiguration geometry_limited;
  geometry_limited.maximum_vertices_per_road = 3U;
  Ogre14ProceduralRoadInventory geometry_limited_inventory(geometry_limited);
  result = BuildOgre14ProceduralRoadInventory(
      {road}, geometry_limited_inventory, output);
  Require(!result && result.field == "road_inventory.geometry_count" &&
              geometry_limited_inventory.known_identity_count() == 0U,
          "per-road geometry bound did not fail transactionally");

  Ogre14ProceduralRoadInventoryConfiguration payload_limited;
  payload_limited.maximum_payload_bytes = 151U;
  Ogre14ProceduralRoadInventory payload_limited_inventory(payload_limited);
  result = BuildOgre14ProceduralRoadInventory({road}, payload_limited_inventory,
                                              output);
  Require(!result && result.field == "road_inventory.payload_bytes" &&
              payload_limited_inventory.known_identity_count() == 0U,
          "aggregate payload bound did not fail transactionally");

  Ogre14ProceduralRoadInventoryConfiguration replacement_limited;
  replacement_limited.maximum_live_roads = 1U;
  replacement_limited.maximum_lifetime_roads = 1U;
  replacement_limited.maximum_payload_bytes = 152U;
  Ogre14ProceduralRoadInventory replacement_limited_inventory(
      replacement_limited);
  result = BuildOgre14ProceduralRoadInventory(
      {road}, replacement_limited_inventory, output);
  Require(result.ok() && output.size() == 1U,
          "exact-bound initial road payload was rejected");
  const auto unreversed_owner = output.front().mesh_payload;
  Ogre14ProceduralRoadCapture reversed_road = road;
  reversed_road.material.cull = Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  result = BuildOgre14ProceduralRoadInventory(
      {reversed_road}, replacement_limited_inventory, output);
  Require(
      result.ok() && output.size() == 1U &&
          replacement_limited_inventory.known_identity_count() == 1U &&
          replacement_limited_inventory.live_identity_count() == 1U &&
          !SameSharedOwner(unreversed_owner, output.front().mesh_payload),
      "winding-only replacement double-counted the payload or lifetime cap");

  Ogre14ProceduralRoadInventory collision_inventory;
  result = collision_inventory.RegisterDerivedIdentityForAudit(1U, 77U);
  Require(result.ok(), "first collision-audit identity was rejected");
  result = collision_inventory.RegisterDerivedIdentityForAudit(2U, 77U);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "derived procedural-road identity collision was accepted");
  result = collision_inventory.RegisterDerivedIdentityForAudit(1U, 88U);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH,
          "manager road identity was remapped");
}

} // namespace

int main() {
  TestManagerIdentityAllocator();
  TestGeometryKeyAndPayloadAudit();
  TestCacheRevisionAndOwnerReuse();
  TestInventoryLifecycleOrderingAndCompatibility();
  TestCombinedAuthoritativeStaticTransaction();
  TestInventoryFailureGatesAndBounds();
  return EXIT_SUCCESS;
}
