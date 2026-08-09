/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact portable closure for one translated OGRE 14 material.

#pragma once

#include "GraphicsSceneSnapshotProducer.h"
#include "Ogre14LegacyAssetTranslator.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyMaterialClosureVersion = 1U;
/// A resolver accepts no inventory larger than the translator can produce.
constexpr std::size_t kMaximumOgre14LegacyMaterialClosureLiveAssets =
    kDefaultOgre14LegacyMaximumLiveAssetsPerFrame;
constexpr std::size_t kMaximumOgre14LegacyMaterialClosureMutations =
    kDefaultOgre14LegacyMaximumLifetimeAssetRecords;
constexpr std::uint64_t kMaximumOgre14LegacyMaterialClosurePayloadBytes =
    kDefaultOgre14LegacyMaximumDecodedBytesPerFrame;

/// One material plus its exact base-color dependencies, ready for the joined
/// graphics transaction. `assets` contains either material alone or texture,
/// sampler, material in that dependency order. The material's portable
/// descriptor retains canonical absent RenderAssetReferences; the exact audit
/// IDs are applied to GraphicsSceneAssetInput::material_bindings[BASE_COLOR],
/// which is the producer-owned binding seam.
struct Ogre14LegacyMaterialClosure {
  std::uint32_t version = kOgre14LegacyMaterialClosureVersion;
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  std::uint64_t material_source_asset_id = 0U;
  bool requires_reverse_winding = false;
  std::vector<GraphicsSceneAssetInput> assets;
};

enum class Ogre14LegacyMaterialClosureFaultPoint : std::uint8_t {
  BEFORE_INDEX_CONSTRUCTION = 0U,
  DURING_DEPENDENCY_ASSEMBLY = 1U,
};

/// Optional borrowed test seam. Implementations may throw so focused tests can
/// prove that allocation and arbitrary exceptions cannot publish a partially
/// assembled closure. Production callers leave this null.
class IOgre14LegacyMaterialClosureFaultInjector {
public:
  virtual ~IOgre14LegacyMaterialClosureFaultInjector() = default;
  virtual void AtFaultPoint(
      Ogre14LegacyMaterialClosureFaultPoint point) = 0;
};

/// Resolves an exact material identity from an authoritative full translated
/// snapshot. The whole snapshot is revalidated before any closure is exposed.
/// Any validation, allocation, or unexpected exception leaves `output`
/// untouched and returns a fail-closed diagnostic.
[[nodiscard]] ValidationResult ResolveOgre14LegacyMaterialClosure(
    const Ogre14LegacyTranslatedFrame &frame,
    const Ogre14LegacyAssetKey &material_key,
    Ogre14LegacyMaterialClosure &output,
    IOgre14LegacyMaterialClosureFaultInjector *fault_injector = nullptr);

} // namespace RoR::Render
