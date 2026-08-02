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

RoR::Render::Ogre14ProceduralRoadCapture MakeRoad(
    std::uint64_t id = 1U, std::uint64_t revision = 1U) {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture capture;
  capture.stable_graphics_id = id;
  capture.topology_revision = revision;
  capture.exact_native_mesh_resource_group = "General";
  capture.exact_native_mesh_name =
      "RoadSystem-native-" + std::to_string(id);
  capture.exact_native_entity_name =
      "RoadSystem_Instance-native-" + std::to_string(id);
  capture.positions = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F},
                       {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  capture.exact_render_normals = {{0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F},
                                  {0.0F, 0.0F, 1.0F}};
  capture.texture_coordinates_0 = {{0.0F, 0.0F}, {1.0F, 0.0F},
                                   {1.0F, 1.0F}, {0.0F, 1.0F}};
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

void TestManagerIdentityAllocator() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadIdentityAllocator allocator(7U, 8U);
  Ogre14ProceduralRoadIdentityState first;
  ValidationResult result = allocator.Reserve(first);
  Require(result.ok() && first.stable_graphics_id() == 7U,
          "allocator did not issue configured first nonzero ID");
  result = allocator.FinalizeGeometry(first, "geometry-a");
  Require(result.ok() && first.topology_revision() == 1U &&
              first.lifecycle() ==
                  Ogre14ProceduralRoadIdentityLifecycle::LIVE,
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
  result = BuildOgre14ProceduralRoadGeometryStateKey(presentation_only,
                                                      second_key);
  Require(result.ok() && first_key == second_key,
          "non-geometry road state changed topology key");
  Ogre14ProceduralRoadCapture changed = road;
  changed.positions[2U].x = 1.25F;
  std::string changed_key;
  result = BuildOgre14ProceduralRoadGeometryStateKey(changed, changed_key);
  Require(result.ok() && changed_key != first_key,
          "changed finalized road geometry retained the same exact key");

  std::shared_ptr<const RenderAssetPayload> payload;
  result = BuildOgre14ProceduralRoadMeshPayload(road, payload);
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
  result = BuildOgre14ProceduralRoadMeshPayload(bad_index, payload);
  Require(!result && result.field == "road.indices",
          "invalid promoted uint16 index was accepted");
}

void TestCacheRevisionAndOwnerReuse() {
  using namespace RoR::Render;
  Ogre14ProceduralRoadCapture road = MakeRoad();
  Ogre14ProceduralRoadCacheEntry first;
  ValidationResult result =
      ResolveOgre14ProceduralRoadCacheEntry(road, nullptr, first);
  Require(result.ok() && first.mesh_payload != nullptr,
          "initial road cache entry was rejected");

  Ogre14ProceduralRoadCacheEntry unchanged;
  result = ResolveOgre14ProceduralRoadCacheEntry(road, &first, unchanged);
  Require(result.ok() && SameSharedOwner(first.mesh_payload,
                                         unchanged.mesh_payload),
          "byte-identical road cache lookup did not reuse immutable owner");

  Ogre14ProceduralRoadCapture changed = road;
  changed.positions[2U].x = 1.25F;
  changed.topology_revision = 2U;
  Ogre14ProceduralRoadCacheEntry rebuilt;
  result = ResolveOgre14ProceduralRoadCacheEntry(changed, &first, rebuilt);
  Require(result.ok() && rebuilt.topology_revision == 2U &&
              !SameSharedOwner(first.mesh_payload, rebuilt.mesh_payload),
          "changed road did not create exactly one new immutable revision");

  changed.topology_revision = 1U;
  Ogre14ProceduralRoadCacheEntry sentinel = unchanged;
  const auto sentinel_owner = sentinel.mesh_payload;
  result = ResolveOgre14ProceduralRoadCacheEntry(changed, &first, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              SameSharedOwner(sentinel_owner, sentinel.mesh_payload),
          "bad changed-geometry revision mutated cache output");

  Ogre14ProceduralRoadCapture false_advance = road;
  false_advance.topology_revision = 2U;
  result = ResolveOgre14ProceduralRoadCacheEntry(false_advance, &first,
                                                  sentinel);
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
  ValidationResult result = BuildOgre14ProceduralRoadInventory(
      {second, first}, inventory, sections);
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
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>
      reordered_sections;
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
  result = BuildOgre14ProceduralRoadInventory(
      {first, second}, inventory, sections);
  Require(result.ok() && SameSharedOwner(first_owner,
                                         sections.front().mesh_payload),
          "stable road inventory did not reuse immutable mesh owner");

  Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
  result = BuildOgre14GraphicsSceneStaticInventory(
      sections, static_registry, assets, static_meshes);
  Require(result.ok() && assets.size() == 3U && static_meshes.size() == 2U,
          "road sections did not satisfy generic static inventory contract");

  result = BuildOgre14ProceduralRoadInventory({first}, inventory, sections);
  Require(result.ok() && inventory.live_identity_count() == 1U &&
              inventory.known_identity_count() == 2U,
          "road omission did not create a permanent tombstone");
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> sentinel =
      sections;
  result = BuildOgre14ProceduralRoadInventory(
      {first, second}, inventory, sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              sentinel.size() == sections.size() &&
              inventory.live_identity_count() == 1U,
          "removed road resurrection mutated inventory or output");
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
  ValidationResult result = BuildOgre14ProceduralRoadInventory(
      {textured}, inventory, output);
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

  result = BuildOgre14ProceduralRoadInventory({road, road}, inventory,
                                               output);
  Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "duplicate manager road ID was accepted");

  Ogre14ProceduralRoadInventoryConfiguration limited;
  limited.maximum_live_roads = 1U;
  limited.maximum_lifetime_roads = 1U;
  limited.maximum_payload_bytes = 1024U;
  Ogre14ProceduralRoadInventory limited_inventory(limited);
  result = BuildOgre14ProceduralRoadInventory({road}, limited_inventory,
                                               output);
  Require(result.ok(), "bounded one-road inventory was rejected");
  result = BuildOgre14ProceduralRoadInventory({}, limited_inventory, output);
  Require(result.ok() && limited_inventory.live_identity_count() == 0U,
          "bounded road removal failed");
  result = BuildOgre14ProceduralRoadInventory(
      {MakeRoad(2U)}, limited_inventory, output);
  Require(!result && result.field == "road_inventory.lifetime_roads" &&
              limited_inventory.known_identity_count() == 1U,
          "lifetime identity exhaustion did not fail transactionally");

  Ogre14ProceduralRoadInventoryConfiguration live_limited;
  live_limited.maximum_live_roads = 1U;
  Ogre14ProceduralRoadInventory live_limited_inventory(live_limited);
  result = BuildOgre14ProceduralRoadInventory(
      {road, MakeRoad(2U)}, live_limited_inventory, output);
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
  result = BuildOgre14ProceduralRoadInventory(
      {road}, payload_limited_inventory, output);
  Require(!result && result.field == "road_inventory.payload_bytes" &&
              payload_limited_inventory.known_identity_count() == 0U,
          "aggregate payload bound did not fail transactionally");

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
  TestInventoryFailureGatesAndBounds();
  return EXIT_SUCCESS;
}
