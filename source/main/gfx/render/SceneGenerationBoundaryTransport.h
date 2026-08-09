/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Authenticated ordered scene-generation boundary wire contract.

#pragma once

#include "RenderTransportEnvelope.h"

#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kSceneGenerationBoundaryPayloadVersion = 1U;
constexpr std::uint64_t kSceneGenerationBoundaryMaximumPayloadBytes = 128ULL;

/// Ends exactly one map generation after its authoritative empty snapshot has
/// been consumed. Renderer-global transport, asset, snapshot, frame, and input
/// identities remain monotonic; only scene-scoped simulation/temporal lineage
/// opens again at `next_generation`.
struct SceneGenerationBoundary final {
  std::uint32_t version = kSceneGenerationBoundaryPayloadVersion;
  std::uint64_t registry_id = 0U;
  std::uint64_t completed_generation = 0U;
  std::uint64_t next_generation = 0U;
  std::uint64_t asset_sequence = 0U;
  std::uint64_t finalized_snapshot_id = 0U;
};

struct SceneGenerationBoundaryTransportDecodeResult final {
  RenderTransportStatus status = RenderTransportStatus::INVALID_ARGUMENT;
  std::uint64_t sequence = 0U;
  SceneGenerationBoundary boundary;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStatus::OK && sequence != 0U;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Transactional decoder sharing the game-to-presentation sequence with asset
/// and scene decoders. The session-derived registry identity is checked before
/// the shared sequence advances.
class SceneGenerationBoundaryTransportDecoder final {
public:
  SceneGenerationBoundaryTransportDecoder(
      std::uint64_t registry_id,
      RenderTransportSequenceState &shared_sequence_state) noexcept;

  SceneGenerationBoundaryTransportDecoder(
      const SceneGenerationBoundaryTransportDecoder &) = delete;
  SceneGenerationBoundaryTransportDecoder &operator=(
      const SceneGenerationBoundaryTransportDecoder &) = delete;
  SceneGenerationBoundaryTransportDecoder(
      SceneGenerationBoundaryTransportDecoder &&) = delete;
  SceneGenerationBoundaryTransportDecoder &operator=(
      SceneGenerationBoundaryTransportDecoder &&) = delete;
  ~SceneGenerationBoundaryTransportDecoder() = default;

  [[nodiscard]] SceneGenerationBoundaryTransportDecodeResult
  Accept(const std::vector<std::uint8_t> &frame) noexcept;

private:
  std::uint64_t registry_id_ = 0U;
  RenderTransportSequenceState *sequence_state_ = nullptr;
};

[[nodiscard]] bool IsValidSceneGenerationBoundary(
    const SceneGenerationBoundary &boundary) noexcept;

[[nodiscard]] RenderTransportEnvelopeEncodeResult
EncodeSceneGenerationBoundaryFrame(
    std::uint64_t sequence, const SceneGenerationBoundary &boundary);

} // namespace RoR::Render
