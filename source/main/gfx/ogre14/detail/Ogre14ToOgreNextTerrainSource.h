/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Disposable, product-private CityWorld terrain source for OgreNext.

#pragma once

#include "gfx/render/GraphicsSceneSnapshotProducer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ogre {
class TerrainGroup;
}

namespace RoR::Gfx::Detail {

/// Geometry is produced by the existing exact TerrainGroup CPU extraction.
/// This private record supplies only the generic owner and native page locator
/// needed to add the display-domain composite material. It is deliberately not
/// a public terrain receipt, digest, audit, or generalized renderer API.
struct OgreNextDemoTerrainPageMesh final {
  std::int32_t slot_x = 0;
  std::int32_t slot_y = 0;
  std::string exact_page_key;
  std::shared_ptr<const Render::RenderAssetPayload> mesh_payload;
  Render::Float3 page_world_position{};
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  bool visible = true;
};

struct OgreNextDemoTerrainCapture final {
  std::vector<Render::GraphicsSceneAssetInput> assets;
  std::vector<Render::GraphicsSceneStaticMeshInput> static_meshes;
};

/// One-use migration source for the first playable OgreNext demo. It reads an
/// OGRE 14 terrain composite afresh on every capture and publishes only normal
/// renderer assets. Candidate identity and immutable payload owners remain
/// pending until the enclosing joined-scene transaction is committed.
class Ogre14ToOgreNextTerrainSource final {
public:
  Ogre14ToOgreNextTerrainSource();
  ~Ogre14ToOgreNextTerrainSource();

  Ogre14ToOgreNextTerrainSource(
      const Ogre14ToOgreNextTerrainSource &) = delete;
  Ogre14ToOgreNextTerrainSource &
  operator=(const Ogre14ToOgreNextTerrainSource &) = delete;

  [[nodiscard]] Render::ValidationResult Capture(
      Ogre::TerrainGroup *terrain_group,
      const std::vector<OgreNextDemoTerrainPageMesh> &pages,
      OgreNextDemoTerrainCapture &capture);
  void Commit() noexcept;
  void Discard() noexcept;
  void Reset() noexcept;

private:
  struct State;
  std::unique_ptr<State> committed_;
  std::unique_ptr<State> pending_;
};

} // namespace RoR::Gfx::Detail
