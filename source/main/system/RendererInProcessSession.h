/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral, single-process game-to-frontend lifecycle.

#pragma once

#include "render/GraphicsSceneSnapshotProducer.h"
#include "render/RendererFrontend.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace RoR {

constexpr std::uint32_t kRendererInProcessSessionContractVersion = 2U;

/// The owner thread pumps native events before simulation and once more before
/// presentation. The second point lets a resize that arrived during a long
/// joined capture retire that immutable frame instead of stretching it.
enum class RendererInProcessEventPollPoint : std::uint8_t {
  BEFORE_SIMULATION = 0U,
  BEFORE_PRESENT,
};

struct RendererInProcessEventObservation final {
  std::uint32_t version = kRendererInProcessSessionContractVersion;
  /// Present only when the native owner has committed a strictly newer
  /// surface state. The session applies it to the frontend transactionally.
  std::optional<Render::FrontendSurfaceUpdate> surface_update;
  /// True only for the exact surface synchronously adopted by Render() or
  /// PresentBootstrapFrame() before returning typed presentation-surface-stale
  /// recovery. The session adopts this observation locally without calling
  /// UpdateSurface a second time. It is invalid without both that pending
  /// recovery and surface_update.
  bool surface_update_already_committed_to_frontend = false;
  bool shutdown_requested = false;
};

/// Renderer-independent native event seam. Implementations translate and
/// apply game input during PollEvents(); no serialized input representation is
/// involved. The referenced pump and its native window outlive the session.
class IRendererInProcessEventPump {
public:
  virtual ~IRendererInProcessEventPump() = default;
  [[nodiscard]] virtual Render::ValidationResult PollEvents(
      RendererInProcessEventPollPoint point,
      RendererInProcessEventObservation &observation) = 0;
  /// Called only after the frontend has released its borrowed native window.
  virtual void ShutdownEventPump() noexcept {}
};

/// Product-owned capture policy. The reusable session owns the source
/// transaction, ordering, and lifetime; camera conventions, clip ranges, and
/// supported lighting are selected by the game integration without importing
/// a renderer SDK here.
class IRendererInProcessFramePolicy {
public:
  virtual ~IRendererInProcessFramePolicy() = default;
  /// Establish product-specific thread-local capture state before the session
  /// calls the joined source. The session pairs every success with EndCapture,
  /// including source exceptions; implementations must not allocate per frame.
  [[nodiscard]] virtual Render::ValidationResult
  BeginCapture(std::uint32_t drawable_width,
               std::uint32_t drawable_height) = 0;
  virtual void EndCapture() noexcept = 0;
  /// Normalize and validate the session-owned successful capture. Returning or
  /// throwing a failure causes the session to discard that source transaction.
  [[nodiscard]] virtual Render::ValidationResult NormalizeAndValidate(
      Render::GraphicsSceneFrameInput &frame,
      std::uint32_t drawable_width, std::uint32_t drawable_height) = 0;
};

struct RendererInProcessSessionConfig final {
  std::uint32_t version = kRendererInProcessSessionContractVersion;
  Render::FrontendInitializationRequest frontend;
  Render::GraphicsSceneSnapshotProducerConfiguration producer;
  Render::FrameOutputMask requested_outputs = Render::FrameOutputMask::COLOR;
  Render::PixelFormat color_format = Render::PixelFormat::RGBA8_SRGB;
  std::uint64_t surface_update_timeout_nanoseconds = 2'000'000'000ULL;
  std::uint64_t shutdown_timeout_nanoseconds = 5'000'000'000ULL;
  bool present_frames = true;
  bool allow_async_compute = false;
};

enum class RendererInProcessSessionStatus : std::uint8_t {
  READY = 0U,
  BOOTSTRAP_PRESENTED,
  EVENTS_PUMPED,
  SIMULATION_SKIPPED,
  WAITING_FOR_SURFACE,
  FRAME_COMPLETED,
  FRAME_RETIRED,
  PENDING_BACKPRESSURE,
  PENDING_FRONTEND_SURFACE,
  CAPTURE_REJECTED,
  SCENE_GENERATION_RESET,
  SHUTDOWN_REQUESTED,
  CLOSED,
  REJECTED_CONFIGURATION,
  REJECTED_NOT_READY,
  FAILED_FRONTEND_INITIALIZATION,
  FAILED_BOOTSTRAP_PRESENTATION,
  FAILED_EVENT_PUMP,
  FAILED_SURFACE_UPDATE,
  FAILED_PRODUCER,
  FAILED_DISPATCH,
  FAILED_FRONTEND_SHUTDOWN,
  FAILED_ALLOCATION,
  FAILED_INTERNAL,
};

struct RendererInProcessSessionResult final {
  std::uint32_t version = kRendererInProcessSessionContractVersion;
  RendererInProcessSessionStatus status =
      RendererInProcessSessionStatus::FAILED_INTERNAL;
  RendererInProcessSessionStatus terminal_cause =
      RendererInProcessSessionStatus::FAILED_INTERNAL;
  Render::ValidationResult validation;
  Render::RenderOperationCode frontend_code = Render::RenderOperationCode::OK;
  /// The backend's own message for `frontend_code`, relayed in a bounded
  /// buffer so it survives the `noexcept` result path.
  Render::RenderOperationDetail frontend_detail;
  std::uint64_t surface_revision = 0U;
  std::uint64_t asset_sequence = 0U;
  std::uint64_t scene_snapshot_id = 0U;
  std::uint64_t frontend_frame_id = 0U;
  std::uint32_t event_polls = 0U;
  bool pending_frame = false;
  bool shutdown_requested = false;
  bool simulation_may_advance = false;
  bool accepted = false;
  bool terminal = false;

  [[nodiscard]] bool ok() const noexcept { return accepted && !terminal; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Owns one freshly initialized frontend's complete direct-dispatch lifetime.
/// Joined game state is captured transactionally, converted by the canonical
/// scene producer, and submitted as typed asset then scene objects on the same
/// thread. A surface timeout retains that exact immutable production for retry;
/// no newer capture can overtake it. Map reset preserves process-lifetime asset,
/// snapshot, and frontend-frame identities. If the first Render synchronously
/// adopts a newer show surface, that typed recovery retains the old-extent
/// production only long enough to retire it; the next grant captures a fresh
/// camera at the adopted extent without skipping a frontend frame ID.
class RendererInProcessSession final {
public:
  RendererInProcessSession(Render::IRendererFrontend &frontend,
                           IRendererInProcessEventPump &event_pump,
                           IRendererInProcessFramePolicy &frame_policy);
  ~RendererInProcessSession();

  RendererInProcessSession(const RendererInProcessSession &) = delete;
  RendererInProcessSession &operator=(const RendererInProcessSession &) =
      delete;
  RendererInProcessSession(RendererInProcessSession &&) = delete;
  RendererInProcessSession &operator=(RendererInProcessSession &&) = delete;

  [[nodiscard]] RendererInProcessSessionResult
  Start(const RendererInProcessSessionConfig &config);
  /// Pumps authoritative native events and presents at most one clear-only
  /// frontend frame for each active surface revision before any portable scene
  /// state has been consumed. This never grants simulation and never advances
  /// asset, snapshot, particle, or frontend-frame identities. A show callback
  /// surface change is adopted and retried once within the same bounded call
  /// when its presenter notification is immediately available; otherwise the
  /// operation remains PENDING_FRONTEND_SURFACE without another swap until a
  /// later call adopts that exact notification.
  [[nodiscard]] RendererInProcessSessionResult
  PresentBootstrapFrame() noexcept;
  /// Pump and apply native input/surface state before the simulation advances.
  /// If an older immutable production is retained, it is drained first. A
  /// successful result with simulation_may_advance=true grants exactly one
  /// subsequent PostUpdatedScene() or SkipUpdatedScene() call.
  [[nodiscard]] RendererInProcessSessionResult
  PumpEventsBeforeSimulation() noexcept;
  /// Consume the current one-shot simulation grant when the application has
  /// intentionally not advanced or copied simulation state. No source is
  /// captured and no asset, snapshot, or frontend-frame identity advances.
  /// The next loop iteration must pump events to obtain a fresh grant.
  [[nodiscard]] RendererInProcessSessionResult SkipUpdatedScene() noexcept;
  /// Call after the simulation-to-graphics copy and all joined deformation
  /// work complete. The caller must have obtained the current one-shot
  /// simulation grant above before advancing the simulation; otherwise this
  /// method rejects without pumping late input. A retained production is
  /// drained by the next pre-simulation pump, so `source` is never recaptured.
  [[nodiscard]] RendererInProcessSessionResult
  PostUpdatedScene(Render::IJoinedGraphicsSceneSource &source) noexcept;
  /// Submit the authoritative final empty scene and open the next map-scoped
  /// generation without reinitializing the frontend or resetting global IDs.
  [[nodiscard]] RendererInProcessSessionResult
  ResetSceneGeneration() noexcept;
  /// Drains any retained typed production, releases frontend window borrows,
  /// then shuts down the event pump. TIMEOUT leaves the session retryable.
  [[nodiscard]] RendererInProcessSessionResult Shutdown() noexcept;

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool has_pending_frame() const noexcept;
  [[nodiscard]] bool terminal() const noexcept;
  [[nodiscard]] std::uint64_t registry_id() const noexcept;
  [[nodiscard]] std::uint64_t asset_sequence() const noexcept;
  [[nodiscard]] std::uint64_t scene_generation() const noexcept;
  [[nodiscard]] std::uint64_t last_consumed_scene_snapshot_id() const noexcept;
  [[nodiscard]] std::uint64_t last_frontend_frame_id() const noexcept;
  [[nodiscard]] Render::FrontendSurfaceUpdate current_surface() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool IsKnownRendererInProcessSessionStatus(
    RendererInProcessSessionStatus status) noexcept;
[[nodiscard]] const char *
ToString(RendererInProcessSessionStatus status) noexcept;

} // namespace RoR
