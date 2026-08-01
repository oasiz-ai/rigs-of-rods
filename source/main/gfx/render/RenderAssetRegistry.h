/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Transactional renderer-neutral asset registry and replay deltas.

#pragma once

#include "MaterialDescriptor.h"
#include "RenderAssetId.h"
#include "RenderResourceDescriptors.h"
#include "RenderValidation.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderAssetRegistryContractVersion = 1U;

using RenderAssetPayload =
    std::variant<std::monostate, MeshResourceDescriptor,
                 TextureResourceDescriptor, MaterialDescriptor,
                 SamplerResourceDescriptor>;

enum class RenderAssetMutationType : std::uint8_t {
  UPSERT = 0,
  DESTROY = 1,
};

/// One current asset state in a delta. UPSERT carries a descriptor; DESTROY
/// carries monostate and is a permanent tombstone for this registry lifetime.
struct RenderAssetMutation {
  RenderAssetMutationType type = RenderAssetMutationType::UPSERT;
  RenderAssetReference asset;
  RenderAssetPayload payload;
};

/// Versioned transaction consumed identically by every frontend.
///
/// Incremental deltas use base_sequence=N and sequence=N+1. A full snapshot
/// uses base_sequence=0 and contains every current live asset and tombstone,
/// allowing a fresh or device-recovered frontend to reconstruct the exact
/// catalog without replaying history. Mutations are strictly sorted by ID.
struct RenderAssetDelta {
  std::uint32_t version = kRenderAssetRegistryContractVersion;
  std::uint64_t registry_id = 0U;
  std::uint64_t base_sequence = 0U;
  std::uint64_t sequence = 0U;
  bool full_snapshot = false;
  std::vector<RenderAssetMutation> mutations;
};

struct RenderAssetRecord {
  RenderAssetReference asset;
  /// Immutable sharing makes an incremental transaction O(changed asset
  /// bytes), not O(total catalog bytes). A candidate map shallow-copies these
  /// owners and allocates payload storage only for mutations.
  std::shared_ptr<const RenderAssetPayload> payload;

  [[nodiscard]] bool live() const noexcept {
    return payload != nullptr && !payload->valueless_by_exception() &&
           !std::holds_alternative<std::monostate>(*payload);
  }
};

[[nodiscard]] bool
IsKnownRenderAssetMutationType(RenderAssetMutationType type) noexcept;
[[nodiscard]] RenderAssetKind
RenderAssetPayloadKind(const RenderAssetPayload &payload) noexcept;
/// Bit-exact mesh allocation contents, excluding the diagnostic name and the
/// source-owned topology lineage counter. A producer uses this to decide
/// whether an unchanged topology revision still identifies the same immutable
/// upload bytes.
[[nodiscard]] bool EquivalentMeshResourceContents(
    const MeshResourceDescriptor &lhs,
    const MeshResourceDescriptor &rhs) noexcept;
[[nodiscard]] bool EquivalentRenderAssetPayload(
    const RenderAssetPayload &lhs, const RenderAssetPayload &rhs) noexcept;
[[nodiscard]] ValidationResult
ValidateRenderAssetDelta(const RenderAssetDelta &delta);

/// Portable logical catalog. It owns immutable descriptor payloads, never
/// native objects or frontend handles. Apply() is transactional: any validation,
/// revision, sequence, or dependency failure leaves the prior catalog intact.
class RenderAssetRegistry final {
public:
  explicit RenderAssetRegistry(std::uint64_t registry_id) noexcept
      : registry_id_(registry_id) {}

  [[nodiscard]] std::uint64_t registry_id() const noexcept {
    return registry_id_;
  }
  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
  [[nodiscard]] std::size_t record_count() const noexcept {
    return records_.size();
  }
  [[nodiscard]] std::size_t live_count() const noexcept;

  [[nodiscard]] ValidationResult Apply(const RenderAssetDelta &delta);
  /// Produces a replayable authoritative catalog at the current sequence.
  /// An uninitialized sequence-zero registry cannot produce a valid snapshot.
  [[nodiscard]] RenderAssetDelta BuildFullSnapshot() const;

  /// Visits each immutable record in stable asset-ID order without copying
  /// descriptor payload bytes or exposing the registry's container type.
  /// Returning a failure stops visitation and forwards that exact result.
  /// The record reference is borrowed only for that callback invocation and
  /// must not escape it; registry mutation or destruction invalidates it.
  template <typename Visitor>
  [[nodiscard]] ValidationResult VisitRecords(Visitor &&visitor) const {
    for (const auto &entry : records_) {
      const ValidationResult result = visitor(entry.second);
      if (!result) {
        return result;
      }
    }
    return ValidationResult::Success();
  }

  [[nodiscard]] const RenderAssetRecord *
  Find(RenderAssetId id) const noexcept;
  [[nodiscard]] const RenderAssetRecord *
  Resolve(const RenderAssetReference &reference) const noexcept;
  [[nodiscard]] const MeshResourceDescriptor *
  ResolveMesh(const RenderAssetReference &reference) const noexcept;
  [[nodiscard]] const TextureResourceDescriptor *
  ResolveTexture(const RenderAssetReference &reference) const noexcept;
  [[nodiscard]] const MaterialDescriptor *
  ResolveMaterial(const RenderAssetReference &reference) const noexcept;
  [[nodiscard]] const SamplerResourceDescriptor *
  ResolveSampler(const RenderAssetReference &reference) const noexcept;

private:
  using RecordMap = std::map<RenderAssetId, RenderAssetRecord>;

  [[nodiscard]] ValidationResult
  ValidateResolvedDependencies(const RecordMap &records) const;

  std::uint64_t registry_id_ = 0U;
  std::uint64_t sequence_ = 0U;
  RecordMap records_;
};

} // namespace RoR::Render
