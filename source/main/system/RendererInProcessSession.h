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
  UI_OVERLAY_PRESENTED,
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
  FAILED_UI_OVERLAY_PRESENTATION,
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
  /// Render-boundary degrade counters relayed from the dispatcher. Monotonic
  /// for the session lifetime. `dispatch_rejected_frames` counts frames a
  /// prologue validator dropped without poisoning;
  /// `dispatch_recoverable_frame_failures` counts frontend render failures
  /// that carried a verified-clean rollback verdict (counted, not yet
  /// honoured). A degrade nobody can see is not a fix.
  std::uint64_t dispatch_rejected_frames = 0U;
  std::uint64_t dispatch_recoverable_frame_failures = 0U;
  std::uint32_t event_polls = 0U;
  /// Wall-clock nanoseconds this frame spent converting the joined scene on
  /// the CPU, up to and including snapshot production.
  std::uint64_t scene_capture_ns = 0U;
  /// Sub-spans of `scene_capture_ns`: reading the joined OGRE 14 scene,
  /// normalizing and validating it, and producing the neutral snapshot.
  std::uint64_t scene_joined_read_ns = 0U;
  std::uint64_t scene_normalize_ns = 0U;
  /// Sub-spans of `scene_joined_read_ns` reported by the source itself:
  /// reading the OGRE 14 scene, and re-validating what was read.
  std::uint64_t scene_source_read_ns = 0U;
  std::uint64_t scene_source_validate_ns = 0U;
  std::uint64_t scene_produce_ns = 0U;
  /// Wall-clock nanoseconds this frame spent dispatching to the frontend,
  /// rendering, and waiting for completion.
  std::uint64_t scene_dispatch_ns = 0U;
  /// This frame's producer diagnostics, relayed so the host can audit the
  /// retained-section reuse decisions it cannot otherwise observe. Zero on
  /// every frame that did not reach Produce().
  Render::GraphicsSceneSnapshotProduction::Diagnostics producer_diagnostics;
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
  /// Presents one GUI-only frame for an application state that has no world
  /// to capture (the main menu, a settings page, a loading screen). It
  /// CONSUMES the current one-shot simulation grant, exactly like
  /// SkipUpdatedScene(), and replaces that call for those states: the caller
  /// pumped, advanced nothing, and shows its GUI instead of nothing at all.
  /// No asset, snapshot, or frontend-frame identity advances, so it never
  /// interleaves with the scene lineage and stays legal at any point in the
  /// session, including between map generations. PENDING_FRONTEND_SURFACE is
  /// retryable and consumed no identity; the next pump adopts the surface.
  [[nodiscard]] RendererInProcessSessionResult
  PresentUiOverlayFrame(
      const Render::UiOverlayFrameRequest &request) noexcept;
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
  /// Clears a latched terminal state so publication can resume, for the narrow
  /// class of causes that demonstrably committed nothing.
  ///
  /// The governing invariant of this boundary is that a per-frame validation
  /// may reject a frame or an object, but may not end a session and may not
  /// permanently stop publication. `terminal` enforces the first half and
  /// violates the second: `active()` is false forever afterwards, so the host
  /// stops calling PostUpdatedScene and the presenter keeps showing its last
  /// frame for the rest of the process. This is the release valve for exactly
  /// the causes where that is the wrong answer.
  ///
  /// Recovery is granted only when ALL of the following hold, so it can never
  /// paper over a half-committed frame:
  ///   * `terminal_cause` is in the recoverable set (see
  ///     `IsRecoverableRendererInProcessSessionTerminalCause`) -- pre-commit
  ///     content failures only, never device, allocation, pump, or internal
  ///     state;
  ///   * the dispatcher is NOT itself latched. The dispatcher poisons only
  ///     where the frontend may already have committed native work, so its
  ///     latch is the independent, self-verifying proof that recovery is
  ///     unsafe. A session whose dispatcher is dead can never publish again
  ///     regardless of what this flag says.
  /// Any retained production is dropped, because resubmitting the exact
  /// snapshot that was just rejected would reject identically every frame and
  /// stop publication permanently -- the failure mode the invariant forbids.
  ///
  /// Returns READY when publication was resumed, REJECTED_NOT_READY when the
  /// cause is unrecoverable or the session is closed. It never invents a
  /// frame: the caller must obtain a fresh grant and capture again.
  [[nodiscard]] RendererInProcessSessionResult RecoverPublication() noexcept;
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

/// True for the terminal causes that are recoverable in principle: the failure
/// was observed while converting or validating content, strictly before the
/// frontend committed anything, so dropping the frame and capturing again is a
/// complete remedy.
///
/// Everything else is deliberately excluded, because continuing would be
/// meaningless or dishonest rather than merely degraded:
///   * FAILED_FRONTEND_INITIALIZATION -- there is no frontend to present with.
///   * FAILED_SURFACE_UPDATE -- the native drawable could not be described.
///     This is where device loss lands: the session cannot know what it would
///     be rendering to, so every later frame would be a guess.
///   * FAILED_EVENT_PUMP -- window and input events are dead. Continuing
///     leaves an unresponsive window the user cannot even close, which is
///     worse for them than exiting.
///   * FAILED_ALLOCATION -- out of memory; the next capture allocates too.
///   * FAILED_UI_OVERLAY_PRESENTATION / FAILED_FRONTEND_SHUTDOWN /
///     FAILED_INTERNAL -- native state is already unknown by definition.
/// FAILED_DISPATCH is admitted here only in principle; `RecoverPublication`
/// additionally requires the dispatcher's own latch to be clear, which is what
/// actually proves nothing was committed.
[[nodiscard]] bool IsRecoverableRendererInProcessSessionTerminalCause(
    RendererInProcessSessionStatus status) noexcept;

[[nodiscard]] bool IsKnownRendererInProcessSessionStatus(
    RendererInProcessSessionStatus status) noexcept;
[[nodiscard]] const char *
ToString(RendererInProcessSessionStatus status) noexcept;

} // namespace RoR
