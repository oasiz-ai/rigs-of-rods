/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Real RoR-Ogre14 product lifecycle for an Ogre-Next child session.

#pragma once

#include "RendererOgre14GameHostSession.h"
#include "RendererOgre14InputAdapter.h"

#include "render/GraphicsSceneSnapshotProducer.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace RoR {

constexpr std::uint32_t kRendererOgre14ProductSessionContractVersion = 2U;

struct RendererOgre14ProductSessionConfig final {
  std::uint32_t version = kRendererOgre14ProductSessionContractVersion;
  RendererOgre14GameHostSessionConfig host;
  Render::GraphicsSceneSnapshotProducerConfiguration producer;
  std::uint32_t shutdown_drain_timeout_milliseconds = 2000U;
};

enum class RendererOgre14ProductSessionStatus : std::uint8_t {
  READY = 0U,
  REVERSE_DRAINED,
  SCENE_GENERATION_RESET,
  WAITING_FOR_SURFACE,
  WAITING_FOR_CAMERA_EXTENT,
  FRAME_QUEUED,
  PENDING_BACKPRESSURE,
  CAPTURE_REJECTED,
  CLOSED,
  REJECTED_CONFIGURATION,
  REJECTED_NOT_READY,
  FAILED_HOST,
  FAILED_INPUT,
  FAILED_PRODUCER,
  FAILED_ALLOCATION,
  FAILED_SHUTDOWN_TIMEOUT,
  FAILED_INTERNAL,
};

struct RendererOgre14ProductSessionResult final {
  std::uint32_t version = kRendererOgre14ProductSessionContractVersion;
  RendererOgre14ProductSessionStatus status =
      RendererOgre14ProductSessionStatus::FAILED_INTERNAL;
  RendererOgre14GameHostSessionStatus host_status =
      RendererOgre14GameHostSessionStatus::FAILED_INTERNAL;
  RendererOgre14InputApplyStatus input_status =
      RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE;
  Render::ValidationResult validation;
  std::uint64_t surface_revision = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t reverse_messages = 0U;
  std::uint64_t input_batches = 0U;
  std::uint64_t acknowledgements = 0U;
  std::uint64_t controls = 0U;
  bool pending_frame = false;
  bool accepted = false;
  bool terminal = false;

  [[nodiscard]] bool ok() const noexcept { return accepted && !terminal; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Owns exactly one host stream, producer, and reverse input adapter. A
/// successful producer output remains deep-owned here until its optional asset
/// delta and scene are both accepted. No newer source capture occurs while it
/// is pending, so bounded backpressure cannot skip snapshot/tick lineage.
class RendererOgre14ProductSession final {
public:
  RendererOgre14ProductSession(RendererOgre14GameBridge &bridge,
                               IRendererGameInputTarget &input_target);
  ~RendererOgre14ProductSession();

  RendererOgre14ProductSession(const RendererOgre14ProductSession &) = delete;
  RendererOgre14ProductSession &operator=(
      const RendererOgre14ProductSession &) = delete;

  [[nodiscard]] RendererOgre14ProductSessionResult Start(
      const RendererOgre14ProductSessionConfig &config = {});
  /// Drain every reverse message currently published by the bounded worker.
  [[nodiscard]] RendererOgre14ProductSessionResult PumpReverse();
  /// Call only after GfxScene::UpdateScene() has consumed the completed
  /// BufferSimulationData() copy and joined flex/wheel work for this frame.
  [[nodiscard]] RendererOgre14ProductSessionResult PostUpdatedScene(
      Render::IJoinedGraphicsSceneSource &source);
  /// Finalizes the current map as an empty scene, queues its authenticated
  /// boundary for ordered child consumption, and resets only scene-scoped
  /// producer/host lineage. Bounded backpressure retains the final production;
  /// callers retry this method before destroying OGRE scene resources. The
  /// host stream, child process, surface, and input adapter remain active.
  /// Allocation or unexpected producer failures are converted to explicit
  /// results so the terrain-unload caller always reaches ordered teardown.
  [[nodiscard]] RendererOgre14ProductSessionResult
  ResetSceneGeneration() noexcept;
  /// Drain pending output, half-close game -> child, consume final reverse
  /// traffic through peer EOF, then join. Bounded by the configured deadline.
  [[nodiscard]] RendererOgre14ProductSessionResult Shutdown() noexcept;

  [[nodiscard]] bool active() const noexcept { return started_ && !closed_; }
  [[nodiscard]] bool has_pending_frame() const noexcept {
    return pending_.has_value();
  }
  [[nodiscard]] RendererOgre14GameHostSession &host() noexcept {
    return host_;
  }

private:
  struct PendingProduction final {
    Render::GraphicsSceneSnapshotProduction production;
    std::uint64_t captured_surface_revision = 0U;
    bool asset_submitted = false;
  };

  [[nodiscard]] RendererOgre14ProductSessionResult TryPending();
  [[nodiscard]] RendererOgre14ProductSessionResult
  CompleteSceneGenerationReset();
  [[nodiscard]] RendererOgre14ProductSessionResult
  ResetSceneGenerationImpl();
  [[nodiscard]] RendererOgre14ProductSessionResult FailureFromHost(
      const RendererOgre14GameHostSessionResult &host_result) const noexcept;

  RendererOgre14GameHostSession host_;
  IRendererGameInputTarget &input_target_;
  RendererOgre14InputAdapter input_adapter_;
  std::unique_ptr<Render::GraphicsSceneSnapshotProducer> producer_;
  std::optional<PendingProduction> pending_;
  RendererOgre14ProductSessionConfig config_;
  std::uint64_t last_scene_surface_revision_ = 0U;
  std::uint64_t finalizing_scene_snapshot_id_ = 0U;
  bool scene_generation_reset_pending_ = false;
  bool started_ = false;
  bool closed_ = false;
};

[[nodiscard]] bool IsKnownRendererOgre14ProductSessionStatus(
    RendererOgre14ProductSessionStatus status) noexcept;
[[nodiscard]] const char *ToString(
    RendererOgre14ProductSessionStatus status) noexcept;

} // namespace RoR
