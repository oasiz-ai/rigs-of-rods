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
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyMaterialClosureVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialClosureRequestVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialClosureBatchVersion = 1U;
/// A resolver accepts no inventory larger than the translator can produce.
constexpr std::size_t kMaximumOgre14LegacyMaterialClosureLiveAssets =
    kDefaultOgre14LegacyMaximumLiveAssetsPerFrame;
constexpr std::size_t kMaximumOgre14LegacyMaterialClosureMutations =
    kDefaultOgre14LegacyMaximumLifetimeAssetRecords;
constexpr std::uint64_t kMaximumOgre14LegacyMaterialClosurePayloadBytes =
    kDefaultOgre14LegacyMaximumDecodedBytesPerFrame;
constexpr std::size_t kMaximumOgre14LegacyMaterialClosureRequests =
    kDefaultOgre14LegacyMaximumMaterialInputsPerFrame;

/// One material plus its exact base-color dependencies, ready for the joined
/// graphics transaction. `assets` contains either material alone or texture,
/// sampler, material in that dependency order. The material's portable
/// descriptor retains canonical absent RenderAssetReferences; the exact audit
/// IDs are applied to GraphicsSceneAssetInput::material_bindings[BASE_COLOR],
/// which is the producer-owned binding seam.
struct Ogre14LegacyMaterialClosure {
  std::uint32_t version = kOgre14LegacyMaterialClosureVersion;
  Ogre14LegacyCatalogIdentityReceipt catalog_identity;
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  std::uint64_t material_source_asset_id = 0U;
  bool requires_reverse_winding = false;
  /// Exact immutable translated companion. Consumers compare its value
  /// bit-for-bit with independently captured native state; pointer identity is
  /// never used as proof of agreement.
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> material_audit;
  /// Exact dependency-ordered keys parallel to `assets`. These let detached
  /// consumers rederive every source ID and canonical debug identity rather
  /// than trusting an ID/payload pairing supplied by a caller.
  std::vector<Ogre14LegacyAssetKey> asset_keys;
  std::vector<GraphicsSceneAssetInput> assets;
};

/// Caller-owned exact request receipt. Copying identity from a translated
/// frame is permitted; minting a nonempty identity is not. Batch resolution
/// rejects a receipt from another translator or another numeric frame.
struct Ogre14LegacyMaterialClosureRequest {
  std::uint32_t version = kOgre14LegacyMaterialClosureRequestVersion;
  Ogre14LegacyCatalogIdentityReceipt catalog_identity;
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  Ogre14LegacyAssetKey material_key;
};

/// Canonically source-ID-ordered closure set produced by one validation and
/// one indexed traversal of an authoritative full translated frame.
struct Ogre14LegacyMaterialClosureBatch {
  std::uint32_t version = kOgre14LegacyMaterialClosureBatchVersion;
  Ogre14LegacyCatalogIdentityReceipt catalog_identity;
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;
  std::vector<Ogre14LegacyMaterialClosure> closures;
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
  virtual void AtFaultPoint(Ogre14LegacyMaterialClosureFaultPoint point) = 0;
};

/// Revalidates a detached closure against its exact material key. This checks
/// lineage, dependency order and kinds, immutable payloads, producer-owned
/// bindings, translated audit semantics, and the material source identity.
[[nodiscard]] ValidationResult
ValidateOgre14LegacyMaterialClosure(const Ogre14LegacyMaterialClosure &closure,
                                    const Ogre14LegacyAssetKey &material_key);

/// Adds pointer-exact catalog identity and numeric freshness checks against an
/// authoritative frame before applying detached structural validation.
[[nodiscard]] ValidationResult ValidateOgre14LegacyMaterialClosureForFrame(
    const Ogre14LegacyTranslatedFrame &frame,
    const Ogre14LegacyMaterialClosure &closure,
    const Ogre14LegacyAssetKey &material_key);

/// Creates an exact request receipt without validating or resolving the full
/// frame. Allocation/exception failure leaves `output` untouched.
[[nodiscard]] ValidationResult MakeOgre14LegacyMaterialClosureRequest(
    const Ogre14LegacyTranslatedFrame &frame,
    const Ogre14LegacyAssetKey &material_key,
    Ogre14LegacyMaterialClosureRequest &output);

/// Validates `frame` exactly once, indexes it once, rejects duplicate,
/// foreign, stale, missing, and forged identities, then resolves the complete
/// requested material set with the frame's canonical immutable owners.
/// Every failure leaves `output` and all of its deep owners untouched.
[[nodiscard]] ValidationResult ResolveOgre14LegacyMaterialClosureBatch(
    const Ogre14LegacyTranslatedFrame &frame,
    const std::vector<Ogre14LegacyMaterialClosureRequest> &requests,
    Ogre14LegacyMaterialClosureBatch &output,
    IOgre14LegacyMaterialClosureFaultInjector *fault_injector = nullptr);

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
