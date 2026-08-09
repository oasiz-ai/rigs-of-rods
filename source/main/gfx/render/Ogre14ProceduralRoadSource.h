/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact OGRE 14 procedural-road CPU capture and portable inventory.

#pragma once

#include "Ogre14GraphicsSceneSource.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace RoR::Render {

class Ogre14ProceduralRoadInventoryTransaction;

constexpr std::uint32_t kOgre14ProceduralRoadCaptureVersion = 1U;
constexpr std::size_t kOgre14ProceduralRoadMaximumVertices = 50000U;
constexpr std::size_t kOgre14ProceduralRoadMaximumIndices = 150000U;

/// Copy-only post-finish snapshot of the exact arrays uploaded by
/// ProceduralRoad::createMesh(). The native source index stream is uint16;
/// indices are widened without reinterpretation so the portable contract can
/// audit every value before selecting UINT16 storage again.
struct Ogre14ProceduralRoadCapture {
  std::uint32_t version = kOgre14ProceduralRoadCaptureVersion;
  std::uint64_t stable_graphics_id = 0U;
  std::uint64_t topology_revision = 0U;
  std::string exact_native_mesh_resource_group;
  std::string exact_native_mesh_name;
  std::string exact_native_entity_name;
  std::uint32_t source_index_width_bits = 16U;
  std::vector<Float3> positions;
  std::vector<Float3> exact_render_normals;
  std::vector<Float2> texture_coordinates_0;
  std::vector<std::uint32_t> indices;
  Ogre14GraphicsSceneMaterialCaptureInput material;
  bool native_material_audit_complete = false;
  /// Required only by the exact translated-material overload. The owner is
  /// immutable, but admission compares its value bit-for-bit with the
  /// translated audit rather than trusting pointer identity.
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>
      exact_native_material_audit;
  Matrix4x4 render_from_object;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  bool visible = true;
  bool casts_shadows = true;
  bool receives_shadows = true;
  bool visible_in_reflections = true;
  bool finalized = false;
};

enum class Ogre14ProceduralRoadIdentityLifecycle : std::uint8_t {
  NEVER_REGISTERED = 0U,
  RESERVED = 1U,
  LIVE = 2U,
  TOMBSTONED = 3U,
};

/// State embedded in one ProceduralObject but writable only through the
/// manager-owned allocator. It survives mesh replacement and removal so the
/// same object can never be silently resurrected with a fresh identity.
class Ogre14ProceduralRoadIdentityState final {
public:
  Ogre14ProceduralRoadIdentityState() = default;
  Ogre14ProceduralRoadIdentityState(
      const Ogre14ProceduralRoadIdentityState &) = default;
  Ogre14ProceduralRoadIdentityState &operator=(
      const Ogre14ProceduralRoadIdentityState &) = default;

  [[nodiscard]] std::uint64_t stable_graphics_id() const noexcept {
    return stable_graphics_id_;
  }
  [[nodiscard]] std::uint64_t topology_revision() const noexcept {
    return topology_revision_;
  }
  [[nodiscard]] Ogre14ProceduralRoadIdentityLifecycle lifecycle() const
      noexcept {
    return lifecycle_;
  }
  [[nodiscard]] const std::string &exact_geometry_state_key() const noexcept {
    return exact_geometry_state_key_;
  }

private:
  friend class Ogre14ProceduralRoadIdentityAllocator;

  std::uint64_t stable_graphics_id_ = 0U;
  std::uint64_t topology_revision_ = 0U;
  Ogre14ProceduralRoadIdentityLifecycle lifecycle_ =
      Ogre14ProceduralRoadIdentityLifecycle::NEVER_REGISTERED;
  std::string exact_geometry_state_key_;
};

/// Monotonic source identity allocator owned by ProceduralManager. IDs are
/// independent of vector position and display text; zero, reuse, overflow,
/// duplicate registration, and tombstone resurrection all fail closed.
class Ogre14ProceduralRoadIdentityAllocator final {
public:
  explicit Ogre14ProceduralRoadIdentityAllocator(
      std::uint64_t first_id = 1U,
      std::uint64_t maximum_id =
          (std::numeric_limits<std::uint64_t>::max)()) noexcept;

  [[nodiscard]] ValidationResult Reserve(
      Ogre14ProceduralRoadIdentityState &state);
  [[nodiscard]] ValidationResult FinalizeGeometry(
      Ogre14ProceduralRoadIdentityState &state,
      std::string exact_geometry_state_key) const;
  [[nodiscard]] ValidationResult Tombstone(
      Ogre14ProceduralRoadIdentityState &state) const;

  [[nodiscard]] std::uint64_t next_id_for_diagnostics() const noexcept {
    return next_id_;
  }

private:
  std::uint64_t next_id_ = 1U;
  std::uint64_t maximum_id_ =
      (std::numeric_limits<std::uint64_t>::max)();
};

/// Immutable per-road mesh cache. The binary state key carries every source
/// stream byte, while topology_revision cross-checks manager lineage.
struct Ogre14ProceduralRoadCacheEntry {
  std::string exact_geometry_state_key;
  std::uint64_t topology_revision = 0U;
  std::shared_ptr<const RenderAssetPayload> mesh_payload;
};

struct Ogre14ProceduralRoadInventoryConfiguration {
  std::size_t maximum_live_roads = 4096U;
  std::size_t maximum_lifetime_roads = 65536U;
  std::size_t maximum_vertices_per_road =
      kOgre14ProceduralRoadMaximumVertices;
  std::size_t maximum_indices_per_road =
      kOgre14ProceduralRoadMaximumIndices;
  std::uint64_t maximum_payload_bytes = 256U * 1024U * 1024U;
};

/// Authoritative procedural-road lifecycle and immutable payload cache. A
/// complete successful inventory commits atomically. Omitted source IDs are
/// permanent tombstones, and every derived road ID is checked as a bijection
/// in a domain separate from terrain/static/deformable identities.
class Ogre14ProceduralRoadInventory final {
public:
  explicit Ogre14ProceduralRoadInventory(
      Ogre14ProceduralRoadInventoryConfiguration configuration = {});

  [[nodiscard]] ValidationResult RegisterDerivedIdentityForAudit(
      std::uint64_t manager_graphics_id, std::uint64_t derived_object_id);

  [[nodiscard]] std::size_t known_identity_count() const noexcept {
    return known_manager_ids_.size();
  }
  [[nodiscard]] std::size_t live_identity_count() const noexcept {
    return live_manager_ids_.size();
  }
  [[nodiscard]] std::size_t cached_mesh_count() const noexcept {
    return cache_by_manager_id_.size();
  }

private:
  friend class Ogre14ProceduralRoadInventoryTransaction;
  friend ValidationResult BuildOgre14ProceduralRoadInventory(
      const std::vector<Ogre14ProceduralRoadCapture> &,
      Ogre14ProceduralRoadInventory &,
      std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &);
  friend ValidationResult BuildOgre14ProceduralRoadInventory(
      const std::vector<Ogre14ProceduralRoadCapture> &,
      const Ogre14LegacyTranslatedFrame &,
      Ogre14ProceduralRoadInventory &,
      std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &);

  Ogre14ProceduralRoadInventoryConfiguration configuration_;
  std::map<std::uint64_t, std::uint64_t> derived_id_by_manager_id_;
  std::map<std::uint64_t, std::uint64_t> manager_id_by_derived_id_;
  std::set<std::uint64_t> known_manager_ids_;
  std::set<std::uint64_t> live_manager_ids_;
  std::map<std::uint64_t, Ogre14ProceduralRoadCacheEntry>
      cache_by_manager_id_;
};

/// Domain-separated deterministic identity for one manager-owned road. The
/// inventory registers the exact manager-id/derived-id bijection before use.
[[nodiscard]] ValidationResult DeriveOgre14ProceduralRoadObjectId(
    std::uint64_t manager_graphics_id, std::uint64_t &derived_object_id);

/// Builds a collision-free binary key from exact finalized geometry bytes.
/// Identity, transform, visibility and material state are deliberately
/// excluded: only changes to finalized geometry advance topology revision.
[[nodiscard]] ValidationResult BuildOgre14ProceduralRoadGeometryStateKey(
    const Ogre14ProceduralRoadCapture &capture, std::string &key);

/// Validates exact streams, source-width promotion, normals and winding, then
/// creates one immutable tight-bounds payload. Failure leaves `payload`
/// untouched.
[[nodiscard]] ValidationResult BuildOgre14ProceduralRoadMeshPayload(
    const Ogre14ProceduralRoadCapture &capture,
    std::shared_ptr<const RenderAssetPayload> &payload);

/// Reuses the prior immutable owner only for byte-identical geometry with the
/// same revision. Changed geometry requires exactly revision+1.
[[nodiscard]] ValidationResult ResolveOgre14ProceduralRoadCacheEntry(
    const Ogre14ProceduralRoadCapture &capture,
    const Ogre14ProceduralRoadCacheEntry *previous,
    Ogre14ProceduralRoadCacheEntry &entry);

/// Converts one complete authoritative road inventory into static sections.
/// Textured `road2` remains intentionally unsupported until an exact legacy
/// texture/sampler translator exists. Failure leaves state/output unchanged.
[[nodiscard]] ValidationResult BuildOgre14ProceduralRoadInventory(
    const std::vector<Ogre14ProceduralRoadCapture> &captures,
    Ogre14ProceduralRoadInventory &inventory,
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections);

/// Exact activation-gated path. Every road material is resolved from this one
/// authoritative full translated frame and must carry an independently
/// captured, bit-exact native pipeline audit. No missing audit is promoted to
/// resolved state and no factor fallback is attempted.
[[nodiscard]] ValidationResult BuildOgre14ProceduralRoadInventory(
    const std::vector<Ogre14ProceduralRoadCapture> &captures,
    const Ogre14LegacyTranslatedFrame &authoritative_material_frame,
    Ogre14ProceduralRoadInventory &inventory,
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections);

} // namespace RoR::Render
