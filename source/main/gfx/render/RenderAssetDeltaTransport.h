/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic cross-process wire contract for asset transactions.

#pragma once

#include "RenderAssetRegistry.h"
#include "RenderTransportEnvelope.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderAssetDeltaTransportPayloadVersion = 2U;
constexpr std::uint32_t kRenderAssetDeltaTransportRegistryVersion = 1U;
constexpr std::uint32_t kRenderAssetDeltaTransportMeshVersion = 1U;
constexpr std::uint32_t kRenderAssetDeltaTransportTextureVersion = 1U;
constexpr std::uint32_t kRenderAssetDeltaTransportMaterialVersion = 4U;
constexpr std::uint32_t kRenderAssetDeltaTransportSamplerVersion = 1U;

constexpr std::uint64_t kRenderAssetDeltaTransportMaximumPayloadBytes =
    640ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t
    kRenderAssetDeltaTransportMaximumDecodedAllocationBytes =
        768ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderAssetDeltaTransportMaximumResourceBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderAssetDeltaTransportMaximumBlobBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderAssetDeltaTransportMaximumTotalBlobBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kRenderAssetDeltaTransportMaximumMutations = 65536U;
constexpr std::uint32_t kRenderAssetDeltaTransportMaximumMeshPositions =
    44739242U;
constexpr std::uint32_t kRenderAssetDeltaTransportMaximumMeshIndices =
    134217728U;
constexpr std::uint32_t kRenderAssetDeltaTransportMaximumTextureMipLevels =
    16U;

using RenderAssetDeltaTransportEncodeResult =
    RenderTransportEnvelopeEncodeResult;

class DecodedRenderAssetDeltaTransportMessage final {
public:
  DecodedRenderAssetDeltaTransportMessage(
      const DecodedRenderAssetDeltaTransportMessage &) = delete;
  DecodedRenderAssetDeltaTransportMessage &operator=(
      const DecodedRenderAssetDeltaTransportMessage &) = delete;
  DecodedRenderAssetDeltaTransportMessage(
      DecodedRenderAssetDeltaTransportMessage &&) = delete;
  DecodedRenderAssetDeltaTransportMessage &operator=(
      DecodedRenderAssetDeltaTransportMessage &&) = delete;
  ~DecodedRenderAssetDeltaTransportMessage() = default;

  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
  [[nodiscard]] RenderTransportMessageKind kind() const noexcept {
    return RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
  }
  [[nodiscard]] const std::shared_ptr<const RenderAssetDelta> &
  delta() const noexcept {
    return delta_;
  }

private:
  DecodedRenderAssetDeltaTransportMessage(
      std::uint64_t sequence,
      std::shared_ptr<const RenderAssetDelta> delta) noexcept
      : sequence_(sequence), delta_(std::move(delta)) {}

  std::uint64_t sequence_ = 0U;
  std::shared_ptr<const RenderAssetDelta> delta_;

  friend class RenderAssetDeltaTransportDecoder;
};

struct RenderAssetDeltaTransportDecodeResult {
  std::shared_ptr<const DecodedRenderAssetDeltaTransportMessage> message;
  RenderTransportStatus status = RenderTransportStatus::INVALID_ARGUMENT;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStatus::OK && message != nullptr;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Decodes, validates, and applies asset transactions to one private catalog.
/// The catalog, published owner, and envelope sequence advance together only
/// after complete framing, payload, lineage, dependency, and registry checks.
/// Asset floating-point storage is finite and bit-exact: unlike scene values,
/// negative zero is preserved because it participates in revision identity.
class RenderAssetDeltaTransportDecoder final {
public:
  explicit RenderAssetDeltaTransportDecoder(
      std::uint64_t registry_id,
      std::uint64_t first_expected_sequence = 1U) noexcept;
  RenderAssetDeltaTransportDecoder(
      std::uint64_t registry_id,
      RenderTransportSequenceState &shared_sequence_state) noexcept;

  RenderAssetDeltaTransportDecoder(
      const RenderAssetDeltaTransportDecoder &) = delete;
  RenderAssetDeltaTransportDecoder &operator=(
      const RenderAssetDeltaTransportDecoder &) = delete;
  RenderAssetDeltaTransportDecoder(RenderAssetDeltaTransportDecoder &&) =
      delete;
  RenderAssetDeltaTransportDecoder &operator=(
      RenderAssetDeltaTransportDecoder &&) = delete;
  ~RenderAssetDeltaTransportDecoder() = default;

  [[nodiscard]] RenderAssetDeltaTransportDecodeResult
  Accept(const std::vector<std::uint8_t> &frame);

  [[nodiscard]] const RenderAssetRegistry &registry() const noexcept {
    return registry_;
  }
  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return sequence_state_->next_expected_sequence();
  }
  [[nodiscard]] std::uint64_t last_accepted_sequence() const noexcept {
    return sequence_state_->last_accepted_sequence();
  }
  [[nodiscard]] const std::shared_ptr<
      const DecodedRenderAssetDeltaTransportMessage> &
  published() const noexcept {
    return published_;
  }

private:
  RenderAssetRegistry registry_;
  RenderTransportSequenceState owned_sequence_state_;
  RenderTransportSequenceState *sequence_state_ = &owned_sequence_state_;
  std::shared_ptr<const DecodedRenderAssetDeltaTransportMessage> published_;
};

[[nodiscard]] RenderAssetDeltaTransportEncodeResult
EncodeRenderAssetDeltaTransportFrame(std::uint64_t sequence,
                                     const RenderAssetDelta &delta);

} // namespace RoR::Render
