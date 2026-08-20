/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic cross-process wire contract for immutable scenes.

#pragma once

#include "RenderFrame.h"
#include "RenderTransportEnvelope.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint16_t kSceneSnapshotTransportVersion =
    kRenderTransportEnvelopeVersion;
constexpr std::uint32_t kSceneSnapshotPayloadVersion = 1U;
constexpr std::uint32_t kSceneSnapshotTransportSceneVersion = 7U;
constexpr std::uint32_t kSceneSnapshotTransportCameraVersion = 2U;
constexpr std::size_t kSceneSnapshotTransportHeaderBytes =
    kRenderTransportEnvelopeHeaderBytes;
constexpr std::uint64_t kSceneSnapshotTransportMaximumPayloadBytes =
    64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kSceneSnapshotTransportMaximumDecodedAllocationBytes =
    128ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kSceneSnapshotTransportMaximumMeshInstances = 65536U;
constexpr std::uint32_t kSceneSnapshotTransportMaximumLights = 4096U;
constexpr std::uint32_t kSceneSnapshotTransportMaximumReflectionProbes = 256U;
constexpr std::uint32_t kSceneSnapshotTransportMaximumDynamicMeshUpdates =
    4096U;
constexpr std::uint32_t kSceneSnapshotTransportMaximumParticleEvents = 65536U;
constexpr std::uint32_t kSceneSnapshotTransportMaximumVerticesPerUpdate =
    1048576U;
constexpr std::uint64_t kSceneSnapshotTransportMaximumPositionsPerMessage =
    4194304ULL;

inline constexpr auto kSceneSnapshotTransportMagic =
    kRenderTransportEnvelopeMagic;

using SceneSnapshotTransportMessageKind = RenderTransportMessageKind;
using SceneSnapshotTransportStatus = RenderTransportStatus;

[[nodiscard]] bool IsKnownSceneSnapshotTransportMessageKind(
    SceneSnapshotTransportMessageKind kind) noexcept;

/// Standalone SHA-256 for the exact payload bytes. It is exposed so a framed
/// byte-stream adapter can integrity-check or construct a complete frame
/// without importing a serialization or crypto dependency.
[[nodiscard]] std::array<std::uint8_t, 32U>
ComputeSceneSnapshotTransportPayloadDigest(const std::uint8_t *payload,
                                           std::size_t payload_size) noexcept;

using SceneSnapshotTransportEncodeResult =
    RenderTransportEnvelopeEncodeResult;

/// Immutable decoded ownership. The scene is a validated deep-owned snapshot;
/// camera state is accessible only through this const message owner.
class DecodedSceneSnapshotTransportMessage final {
public:
  DecodedSceneSnapshotTransportMessage(
      const DecodedSceneSnapshotTransportMessage &) = delete;
  DecodedSceneSnapshotTransportMessage &operator=(
      const DecodedSceneSnapshotTransportMessage &) = delete;
  DecodedSceneSnapshotTransportMessage(
      DecodedSceneSnapshotTransportMessage &&) = delete;
  DecodedSceneSnapshotTransportMessage &operator=(
      DecodedSceneSnapshotTransportMessage &&) = delete;
  ~DecodedSceneSnapshotTransportMessage() = default;

  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
  [[nodiscard]] SceneSnapshotTransportMessageKind kind() const noexcept {
    return kind_;
  }
  [[nodiscard]] const std::shared_ptr<const SceneSnapshot> &
  scene_snapshot() const noexcept {
    return scene_snapshot_;
  }
  [[nodiscard]] const CameraViewRequest &camera() const noexcept {
    return camera_;
  }

private:
  DecodedSceneSnapshotTransportMessage(
      std::uint64_t sequence, SceneSnapshotTransportMessageKind kind,
      std::shared_ptr<const SceneSnapshot> scene_snapshot,
      CameraViewRequest camera) noexcept;

  std::uint64_t sequence_ = 0U;
  SceneSnapshotTransportMessageKind kind_ =
      SceneSnapshotTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2;
  std::shared_ptr<const SceneSnapshot> scene_snapshot_;
  CameraViewRequest camera_;

  friend class SceneSnapshotTransportDecoder;
};

struct SceneSnapshotTransportDecodeResult {
  std::shared_ptr<const DecodedSceneSnapshotTransportMessage> message;
  SceneSnapshotTransportStatus status =
      SceneSnapshotTransportStatus::INVALID_ARGUMENT;

  [[nodiscard]] bool ok() const noexcept {
    return status == SceneSnapshotTransportStatus::OK && message != nullptr;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Transactional ordered receiver. A frame is decoded and fully validated in
/// candidate storage before publication or sequence state changes. One caller
/// serializes Accept(); immutable owners already returned remain valid.
class SceneSnapshotTransportDecoder final {
public:
  explicit SceneSnapshotTransportDecoder(
      std::uint64_t first_expected_sequence = 1U) noexcept;
  explicit SceneSnapshotTransportDecoder(
      RenderTransportSequenceState &shared_sequence_state) noexcept;

  SceneSnapshotTransportDecoder(const SceneSnapshotTransportDecoder &) =
      delete;
  SceneSnapshotTransportDecoder &operator=(
      const SceneSnapshotTransportDecoder &) = delete;
  SceneSnapshotTransportDecoder(SceneSnapshotTransportDecoder &&) = delete;
  SceneSnapshotTransportDecoder &operator=(
      SceneSnapshotTransportDecoder &&) = delete;
  ~SceneSnapshotTransportDecoder() = default;

  [[nodiscard]] SceneSnapshotTransportDecodeResult
  Accept(const std::vector<std::uint8_t> &frame);

  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return sequence_state_->next_expected_sequence();
  }
  [[nodiscard]] std::uint64_t last_accepted_sequence() const noexcept {
    return sequence_state_->last_accepted_sequence();
  }
  [[nodiscard]] const std::shared_ptr<
      const DecodedSceneSnapshotTransportMessage> &
  published() const noexcept {
    return published_;
  }

private:
  RenderTransportSequenceState owned_sequence_state_;
  RenderTransportSequenceState *sequence_state_ = &owned_sequence_state_;
  std::shared_ptr<const DecodedSceneSnapshotTransportMessage> published_;
};

/// Encodes exactly one current SceneSnapshot v7 and CameraViewRequest under
/// the render-frame v2 camera contract. Every admitted signed zero becomes
/// positive zero; every other finite IEEE-754 bit pattern is preserved.
[[nodiscard]] SceneSnapshotTransportEncodeResult
EncodeSceneSnapshotTransportFrame(std::uint64_t sequence,
                                  const SceneSnapshot &scene_snapshot,
                                  const CameraViewRequest &camera);

} // namespace RoR::Render
