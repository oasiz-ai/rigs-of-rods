/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererInProcessSession.h"

#include "render/RendererFrontendDirectDispatcher.h"
#include "render/RendererFrontendPresentationPolicy.h"

#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace RoR {
namespace {

Render::FrontendSurfaceUpdate InitialSurface(
    const Render::FrontendInitializationRequest &request) noexcept {
  Render::FrontendSurfaceUpdate surface;
  surface.surface_revision = request.initial_surface_revision;
  surface.window = request.window;
  surface.pixel_width = request.initial_width;
  surface.pixel_height = request.initial_height;
  surface.content_scale = request.initial_content_scale;
  surface.suspended = false;
  return surface;
}

Render::ValidationResult InvalidObservationVersion() {
  Render::ValidationResult result;
  result.code = Render::ValidationCode::UNSUPPORTED_VERSION;
  return result;
}

void PreserveValidationFailure(
    const Render::ValidationResult &source,
    Render::ValidationResult &destination) noexcept {
  destination.code = source.code;
  destination.element_index = source.element_index;
  try {
    std::string field = source.field;
    std::string detail = source.detail;
    destination.field = std::move(field);
    destination.detail = std::move(detail);
  } catch (...) {
    destination.field.clear();
    destination.detail.clear();
  }
}

class JoinedCaptureGuard final {
public:
  explicit JoinedCaptureGuard(
      Render::IJoinedGraphicsSceneSource &source) noexcept
      : source_(source) {}
  ~JoinedCaptureGuard() {
    if (active_) {
      source_.DiscardJoinedGraphicsFrame();
    }
  }
  void Commit() noexcept {
    source_.CommitJoinedGraphicsFrame();
    active_ = false;
  }
  void Arm() noexcept { active_ = true; }
  void DismissUncaptured() noexcept { active_ = false; }

private:
  Render::IJoinedGraphicsSceneSource &source_;
  bool active_ = false;
};

class FramePolicyCaptureGuard final {
public:
  explicit FramePolicyCaptureGuard(
      IRendererInProcessFramePolicy &policy) noexcept
      : policy_(policy) {}
  ~FramePolicyCaptureGuard() { policy_.EndCapture(); }

  FramePolicyCaptureGuard(const FramePolicyCaptureGuard &) = delete;
  FramePolicyCaptureGuard &
  operator=(const FramePolicyCaptureGuard &) = delete;

private:
  IRendererInProcessFramePolicy &policy_;
};

} // namespace

class RendererInProcessSession::Impl final {
public:
  struct PendingProduction final {
    Render::GraphicsSceneSnapshotProduction production;
    std::uint64_t captured_surface_revision = 0U;
    std::uint32_t captured_width = 0U;
    std::uint32_t captured_height = 0U;
    bool asset_submitted = false;
  };

  Impl(Render::IRendererFrontend &owned_frontend,
       IRendererInProcessEventPump &owned_event_pump,
       IRendererInProcessFramePolicy &owned_frame_policy) noexcept
      : frontend(owned_frontend), event_pump(owned_event_pump),
        frame_policy(owned_frame_policy) {}

  RendererInProcessSessionResult Result(
      RendererInProcessSessionStatus status, bool accepted,
      bool operation_terminal = false, std::uint32_t event_polls = 0U) const
      noexcept {
    RendererInProcessSessionResult result;
    result.status = status;
    result.terminal_cause = terminal_cause;
    result.surface_revision = current_surface.surface_revision;
    result.asset_sequence =
        dispatcher != nullptr ? dispatcher->asset_sequence() : 0U;
    result.frontend_frame_id =
        dispatcher != nullptr ? dispatcher->last_frontend_frame_id() : 0U;
    result.event_polls = event_polls;
    result.pending_frame = pending.has_value();
    result.shutdown_requested = shutdown_requested;
    result.simulation_may_advance = simulation_granted;
    result.accepted = accepted;
    result.terminal = operation_terminal || terminal;
    return result;
  }

  RendererInProcessSessionResult Poison(
      RendererInProcessSessionStatus status,
      const Render::ValidationResult &validation =
          Render::ValidationResult::Success(),
      Render::RenderOperationCode frontend_code =
          Render::RenderOperationCode::OK,
      std::uint32_t event_polls = 0U) noexcept {
    if (!terminal) {
      terminal = true;
      terminal_cause = status;
    }
    RendererInProcessSessionResult result =
        Result(status, false, true, event_polls);
    PreserveValidationFailure(validation, result.validation);
    result.frontend_code = frontend_code;
    return result;
  }

  RendererInProcessSessionResult Failure(
      RendererInProcessSessionStatus status,
      const Render::ValidationResult &validation =
          Render::ValidationResult::Success(),
      Render::RenderOperationCode frontend_code =
          Render::RenderOperationCode::OK,
      std::uint32_t event_polls = 0U) const noexcept {
    RendererInProcessSessionResult result =
        Result(status, false, false, event_polls);
    PreserveValidationFailure(validation, result.validation);
    result.frontend_code = frontend_code;
    return result;
  }

  RendererInProcessSessionResult FailureFromDispatch(
      const Render::RendererFrontendDirectDispatchResult &dispatch,
      std::uint32_t event_polls) noexcept {
    Render::ValidationResult validation;
    validation.code = dispatch.validation_code;
    RendererInProcessSessionResult result = Poison(
        RendererInProcessSessionStatus::FAILED_DISPATCH, validation,
        dispatch.frontend_code, event_polls);
    result.asset_sequence = dispatch.asset_sequence;
    result.scene_snapshot_id = dispatch.scene_snapshot_id;
    result.frontend_frame_id = dispatch.frontend_frame_id;
    return result;
  }

  void QuiesceEventPump() noexcept {
    if (!event_pump_quiesced) {
      event_pump.ShutdownEventPump();
      event_pump_quiesced = true;
    }
  }

  Render::RenderOperationResult StopFrontendForClosure(
      std::uint64_t timeout_nanoseconds) noexcept {
    if (!frontend_initialized) {
      return Render::RenderOperationResult::Success();
    }
    Render::RenderOperationResult stopped;
    try {
      stopped = frontend.Shutdown(timeout_nanoseconds);
    } catch (...) {
      return Render::RenderOperationResult::Failure(
          Render::RenderOperationCode::BACKEND_FAILURE,
          "frontend shutdown threw during in-process session closure");
    }
    // A failed Initialize is required to have rolled itself back. NOT_INITIALIZED
    // from the defensive Shutdown below therefore confirms there is no
    // remaining native-window borrow.
    if (!stopped &&
        stopped.code != Render::RenderOperationCode::NOT_INITIALIZED) {
      return stopped;
    }
    frontend_initialized = false;
    return Render::RenderOperationResult::Success();
  }

  bool CloseFailedStart(std::uint64_t timeout_nanoseconds) noexcept {
    const Render::RenderOperationResult stopped =
        StopFrontendForClosure(timeout_nanoseconds);
    if (!stopped) {
      return false;
    }
    QuiesceEventPump();
    return true;
  }

  bool ConfigValid(const RendererInProcessSessionConfig &candidate,
                   Render::ValidationResult &validation) const {
    if (candidate.version != kRendererInProcessSessionContractVersion ||
        candidate.producer.registry_id == 0U ||
        candidate.surface_update_timeout_nanoseconds == 0U ||
        candidate.shutdown_timeout_nanoseconds == 0U ||
        (candidate.frontend.headless && candidate.present_frames)) {
      validation = Render::ValidationResult::Failure(
          Render::ValidationCode::VALUE_OUT_OF_RANGE,
          "in_process_session.config",
          "invalid version, registry, timeout, or headless presentation mode");
      return false;
    }
    validation =
        Render::ValidateFrontendInitializationRequest(candidate.frontend);
    if (!validation) {
      return false;
    }
    Render::RendererFrontendPresentationPolicy policy;
    policy.requested_outputs = candidate.requested_outputs;
    policy.color_format = candidate.color_format;
    policy.present = false;
    policy.allow_async_compute = candidate.allow_async_compute;
    validation = Render::ValidateRendererFrontendPresentationPolicy(policy);
    return validation.ok();
  }

  RendererInProcessSessionResult Start(
      const RendererInProcessSessionConfig &candidate) {
    if (started || closed || terminal || frontend_initialized) {
      return Failure(RendererInProcessSessionStatus::REJECTED_CONFIGURATION);
    }
    Render::ValidationResult validation;
    if (!ConfigValid(candidate, validation)) {
      return Failure(RendererInProcessSessionStatus::REJECTED_CONFIGURATION,
                     validation);
    }
    closure_shutdown_timeout_nanoseconds =
        candidate.shutdown_timeout_nanoseconds;

    try {
      std::unique_ptr<Render::GraphicsSceneSnapshotProducer> next_producer =
          std::make_unique<Render::GraphicsSceneSnapshotProducer>(
              candidate.producer);
      // Conservatively require an explicit shutdown from the instant Initialize
      // is entered. NOT_INITIALIZED confirms a failed Initialize already rolled
      // its native state back.
      frontend_initialized = true;
      const Render::RenderOperationResult initialized =
          frontend.Initialize(candidate.frontend);
      if (!initialized) {
        terminal = true;
        terminal_cause =
            RendererInProcessSessionStatus::FAILED_FRONTEND_INITIALIZATION;
        closed = CloseFailedStart(candidate.shutdown_timeout_nanoseconds);
        RendererInProcessSessionResult result = Poison(
            RendererInProcessSessionStatus::FAILED_FRONTEND_INITIALIZATION,
            Render::ValidationResult::Success(), initialized.code);
        return result;
      }
      std::unique_ptr<Render::RendererFrontendDirectDispatcher>
          next_dispatcher =
              std::make_unique<Render::RendererFrontendDirectDispatcher>(
                  frontend, candidate.producer.registry_id);
      if (next_dispatcher->terminal()) {
        const Render::RendererFrontendDirectDispatchResult rejected =
            next_dispatcher->SynchronizeAssets(Render::RenderAssetDelta{});
        closed = CloseFailedStart(candidate.shutdown_timeout_nanoseconds);
        return FailureFromDispatch(rejected, 0U);
      }

      config = candidate;
      current_surface = InitialSurface(candidate.frontend);
      producer = std::move(next_producer);
      dispatcher = std::move(next_dispatcher);
      started = true;
      return Result(RendererInProcessSessionStatus::READY, true);
    } catch (const std::bad_alloc &) {
      closed = CloseFailedStart(candidate.shutdown_timeout_nanoseconds);
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY);
    } catch (const std::length_error &) {
      closed = CloseFailedStart(candidate.shutdown_timeout_nanoseconds);
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY);
    } catch (...) {
      closed = CloseFailedStart(candidate.shutdown_timeout_nanoseconds);
      return Poison(RendererInProcessSessionStatus::FAILED_INTERNAL,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE);
    }
  }

  RendererInProcessSessionResult ApplyPendingSurface(
      std::uint32_t event_polls) noexcept {
    if (!pending_surface.has_value()) {
      return Result(RendererInProcessSessionStatus::READY, true, false,
                    event_polls);
    }
    try {
      const Render::RenderOperationResult updated = frontend.UpdateSurface(
          *pending_surface, config.frontend.headless,
          config.surface_update_timeout_nanoseconds);
      if (!updated) {
        if (updated.code == Render::RenderOperationCode::TIMEOUT) {
          return Failure(
              RendererInProcessSessionStatus::PENDING_BACKPRESSURE,
              Render::ValidationResult::Success(), updated.code, event_polls);
        }
        return Poison(RendererInProcessSessionStatus::FAILED_SURFACE_UPDATE,
                      Render::ValidationResult::Success(), updated.code,
                      event_polls);
      }
      current_surface = *pending_surface;
      pending_surface.reset();
      awaiting_frontend_surface_update = false;
      return Result(RendererInProcessSessionStatus::READY, true, false,
                    event_polls);
    } catch (const std::bad_alloc &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (const std::length_error &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_SURFACE_UPDATE,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
  }

  RendererInProcessSessionResult PumpEvents(
      RendererInProcessEventPollPoint point,
      std::uint32_t &event_polls) noexcept {
    if (pending_surface.has_value()) {
      return ApplyPendingSurface(event_polls);
    }
    RendererInProcessEventObservation observation;
    Render::ValidationResult polled;
    try {
      polled = event_pump.PollEvents(point, observation);
      ++event_polls;
    } catch (const std::bad_alloc &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (const std::length_error &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_EVENT_PUMP,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
    if (!polled) {
      return Poison(RendererInProcessSessionStatus::FAILED_EVENT_PUMP, polled,
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
    if (observation.version != kRendererInProcessSessionContractVersion) {
      return Poison(RendererInProcessSessionStatus::FAILED_EVENT_PUMP,
                    InvalidObservationVersion(),
                    Render::RenderOperationCode::INVALID_ARGUMENT,
                    event_polls);
    }
    if (observation.surface_update_already_committed_to_frontend &&
        (!awaiting_frontend_surface_update ||
         !observation.surface_update.has_value())) {
      return Poison(
          RendererInProcessSessionStatus::FAILED_EVENT_PUMP,
          Render::ValidationResult::Failure(
              Render::ValidationCode::MISSING_REFERENCE,
              "in_process_session.committed_surface",
              "an already-committed surface requires the matching pending "
              "frontend recovery"),
          Render::RenderOperationCode::INVALID_ARGUMENT, event_polls);
    }
    shutdown_requested =
        shutdown_requested || observation.shutdown_requested;
    if (!observation.surface_update.has_value()) {
      return Result(RendererInProcessSessionStatus::READY, true, false,
                    event_polls);
    }
    Render::ValidationResult transition;
    try {
      transition = Render::ValidateFrontendSurfaceTransition(
          current_surface, *observation.surface_update,
          config.frontend.headless, true);
    } catch (const std::bad_alloc &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (const std::length_error &) {
      return Poison(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_EVENT_PUMP,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
    if (!transition) {
      return Poison(RendererInProcessSessionStatus::FAILED_EVENT_PUMP,
                    transition, Render::RenderOperationCode::INVALID_ARGUMENT,
                    event_polls);
    }
    if (observation.surface_update_already_committed_to_frontend) {
      current_surface = *observation.surface_update;
      awaiting_frontend_surface_update = false;
      return Result(RendererInProcessSessionStatus::READY, true, false,
                    event_polls);
    }
    pending_surface = std::move(observation.surface_update);
    return ApplyPendingSurface(event_polls);
  }

  RendererInProcessSessionResult TryPending(
      std::uint32_t &event_polls) noexcept {
    if (!pending.has_value() || dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::NOT_INITIALIZED,
                     event_polls);
    }
    RendererInProcessSessionResult events =
        PumpEvents(RendererInProcessEventPollPoint::BEFORE_PRESENT,
                   event_polls);
    if (!events || events.status ==
                       RendererInProcessSessionStatus::PENDING_BACKPRESSURE) {
      events.pending_frame = true;
      return events;
    }
    if (awaiting_frontend_surface_update) {
      RendererInProcessSessionResult waiting = Failure(
          RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE,
          Render::ValidationResult::Success(),
          Render::RenderOperationCode::RESOURCE_STALE, event_polls);
      waiting.pending_frame = true;
      return waiting;
    }

    PendingProduction &retained = *pending;
    if (!retained.asset_submitted &&
        retained.production.asset_delta.has_value()) {
      const Render::RendererFrontendDirectDispatchResult synchronized =
          dispatcher->SynchronizeAssets(*retained.production.asset_delta);
      if (!synchronized) {
        return FailureFromDispatch(synchronized, event_polls);
      }
      retained.asset_submitted = true;
    } else if (!retained.production.asset_delta.has_value()) {
      retained.asset_submitted = true;
    }

    const bool stale_surface =
        retained.captured_surface_revision !=
            current_surface.surface_revision ||
        retained.captured_width != current_surface.pixel_width ||
        retained.captured_height != current_surface.pixel_height;
    Render::RendererFrontendPresentationPolicy policy;
    policy.requested_outputs = config.requested_outputs;
    policy.color_format = config.color_format;
    policy.allow_async_compute = config.allow_async_compute;
    policy.retire_scene_without_render =
        stale_surface || current_surface.suspended || shutdown_requested;
    policy.present = config.present_frames &&
                     !policy.retire_scene_without_render;
    if (policy.present) {
      policy.presentation_surface_revision =
          current_surface.surface_revision;
      policy.presentation_drawable_width = current_surface.pixel_width;
      policy.presentation_drawable_height = current_surface.pixel_height;
      policy.retire_scene_on_presentation_extent_mismatch = true;
    }

    const Render::RendererFrontendDirectDispatchResult dispatched =
        dispatcher->RenderScene(retained.production.scene_snapshot,
                                retained.production.camera, policy);
    if (dispatched.status ==
        Render::RendererFrontendDirectDispatchStatus::
            SCENE_FRAME_PRESENTATION_SURFACE_STALE) {
      awaiting_frontend_surface_update = true;
      RendererInProcessSessionResult waiting = Failure(
          RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE,
          Render::ValidationResult::Success(), dispatched.frontend_code,
          event_polls);
      waiting.asset_sequence = dispatched.asset_sequence;
      waiting.scene_snapshot_id = dispatched.scene_snapshot_id;
      waiting.frontend_frame_id = dispatched.frontend_frame_id;
      waiting.pending_frame = true;
      return waiting;
    }
    if (!dispatched) {
      return FailureFromDispatch(dispatched, event_polls);
    }
    last_scene_surface_revision = retained.captured_surface_revision;
    last_scene_width = retained.captured_width;
    last_scene_height = retained.captured_height;
    pending.reset();

    RendererInProcessSessionResult result = Result(
        dispatched.status == Render::RendererFrontendDirectDispatchStatus::
                                 SCENE_FRAME_RETIRED
            ? RendererInProcessSessionStatus::FRAME_RETIRED
            : RendererInProcessSessionStatus::FRAME_COMPLETED,
        true, false, event_polls);
    result.asset_sequence = dispatched.asset_sequence;
    result.scene_snapshot_id = dispatched.scene_snapshot_id;
    result.frontend_frame_id = dispatched.frontend_frame_id;
    return result;
  }

  RendererInProcessSessionResult PumpEventsBeforeSimulation() noexcept {
    if (!started || closed || terminal || producer == nullptr ||
        dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY);
    }
    if (simulation_granted) {
      RendererInProcessSessionResult result = Result(
          RendererInProcessSessionStatus::EVENTS_PUMPED, true);
      result.simulation_may_advance = true;
      return result;
    }
    if (scene_generation_reset_pending) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::INVALID_ARGUMENT);
    }

    std::uint32_t event_polls = 0U;
    RendererInProcessSessionResult retained_result;
    const bool had_pending = pending.has_value();
    if (had_pending) {
      retained_result = TryPending(event_polls);
      if (retained_result.terminal || pending.has_value()) {
        return retained_result;
      }
    }

    RendererInProcessSessionResult events =
        PumpEvents(RendererInProcessEventPollPoint::BEFORE_SIMULATION,
                   event_polls);
    if (!events || events.status ==
                       RendererInProcessSessionStatus::PENDING_BACKPRESSURE) {
      return events;
    }
    if (shutdown_requested) {
      return Result(RendererInProcessSessionStatus::SHUTDOWN_REQUESTED, true,
                    false, event_polls);
    }
    simulation_granted = true;
    RendererInProcessSessionResult result =
        had_pending
            ? retained_result
            : Result(RendererInProcessSessionStatus::EVENTS_PUMPED, true,
                     false, event_polls);
    result.surface_revision = current_surface.surface_revision;
    result.event_polls = event_polls;
    result.pending_frame = false;
    result.shutdown_requested = shutdown_requested;
    result.simulation_may_advance = true;
    return result;
  }

  RendererInProcessSessionResult SkipUpdatedScene() noexcept {
    if (!started || closed || terminal || producer == nullptr ||
        dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY);
    }
    if (!simulation_granted || pending.has_value() ||
        scene_generation_reset_pending) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::INVALID_ARGUMENT);
    }
    simulation_granted = false;
    return Result(RendererInProcessSessionStatus::SIMULATION_SKIPPED, true);
  }

  RendererInProcessSessionResult PostUpdatedScene(
      Render::IJoinedGraphicsSceneSource &source) noexcept {
    if (!started || closed || terminal || producer == nullptr ||
        dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY);
    }
    if (!simulation_granted || pending.has_value() ||
        scene_generation_reset_pending) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::INVALID_ARGUMENT);
    }
    simulation_granted = false;
    std::uint32_t event_polls = 0U;
    if (shutdown_requested) {
      return Result(RendererInProcessSessionStatus::SHUTDOWN_REQUESTED, true,
                    false, event_polls);
    }
    if (current_surface.suspended) {
      return Result(RendererInProcessSessionStatus::WAITING_FOR_SURFACE, true,
                    false, event_polls);
    }

    try {
      Render::GraphicsSceneFrameInput frame;
      JoinedCaptureGuard capture_guard(source);
      {
        const Render::ValidationResult scope_started =
            frame_policy.BeginCapture(current_surface.pixel_width,
                                      current_surface.pixel_height);
        if (!scope_started) {
          return Failure(RendererInProcessSessionStatus::CAPTURE_REJECTED,
                         scope_started,
                         Render::RenderOperationCode::INVALID_ARGUMENT,
                         event_polls);
        }
        FramePolicyCaptureGuard policy_guard(frame_policy);
        capture_guard.Arm();
        const Render::ValidationResult captured =
            source.CaptureJoinedGraphicsFrame(frame);
        if (!captured) {
          capture_guard.DismissUncaptured();
          return Failure(
              RendererInProcessSessionStatus::CAPTURE_REJECTED, captured,
              Render::RenderOperationCode::INVALID_ARGUMENT, event_polls);
        }
      }
      const Render::ValidationResult normalized =
          frame_policy.NormalizeAndValidate(
              frame, current_surface.pixel_width,
              current_surface.pixel_height);
      if (!normalized) {
        return Failure(RendererInProcessSessionStatus::CAPTURE_REJECTED,
                       normalized,
                       Render::RenderOperationCode::INVALID_ARGUMENT,
                       event_polls);
      }

      Render::GraphicsSceneSnapshotProduceResult produced =
          producer->Produce(frame);
      if (!produced) {
        return Failure(RendererInProcessSessionStatus::FAILED_PRODUCER,
                       produced.validation,
                       Render::RenderOperationCode::INVALID_ARGUMENT,
                       event_polls);
      }
      static_assert(
          std::is_nothrow_move_constructible<
              Render::GraphicsSceneSnapshotProduceResult>::value,
          "committed producer output retention must not throw");
      static_assert(
          std::is_nothrow_move_assignable<
              std::optional<PendingProduction>>::value,
          "pending direct production retention must not throw");
      PendingProduction retained;
      retained.production = std::move(produced.production);
      retained.captured_surface_revision = current_surface.surface_revision;
      retained.captured_width = current_surface.pixel_width;
      retained.captured_height = current_surface.pixel_height;
      retained.asset_submitted =
          !retained.production.asset_delta.has_value();
      pending = std::move(retained);
      capture_guard.Commit();
      return TryPending(event_polls);
    } catch (const std::bad_alloc &) {
      return Failure(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (const std::length_error &) {
      return Failure(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_INTERNAL,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
  }

  RendererInProcessSessionResult CompleteSceneGenerationReset(
      std::uint32_t event_polls) noexcept {
    if (!scene_generation_reset_pending || pending.has_value() ||
        finalizing_scene_snapshot_id == 0U || dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::INVALID_ARGUMENT,
                     event_polls);
    }
    const std::uint64_t finalized_snapshot_id =
        finalizing_scene_snapshot_id;
    const Render::RendererFrontendDirectDispatchResult reset =
        dispatcher->ResetSceneGeneration();
    if (!reset) {
      return FailureFromDispatch(reset, event_polls);
    }
    scene_generation_reset_pending = false;
    finalizing_scene_snapshot_id = 0U;
    RendererInProcessSessionResult result = Result(
        RendererInProcessSessionStatus::SCENE_GENERATION_RESET, true, false,
        event_polls);
    result.scene_snapshot_id = finalized_snapshot_id;
    result.asset_sequence = reset.asset_sequence;
    result.frontend_frame_id = reset.frontend_frame_id;
    return result;
  }

  RendererInProcessSessionResult ResetSceneGeneration() noexcept {
    if (!started || closed || terminal || producer == nullptr ||
        dispatcher == nullptr) {
      return Failure(RendererInProcessSessionStatus::REJECTED_NOT_READY);
    }
    simulation_granted = false;
    std::uint32_t event_polls = 0U;
    try {
      if (scene_generation_reset_pending) {
        if (pending.has_value()) {
          const RendererInProcessSessionResult submitted =
              TryPending(event_polls);
          if (submitted.terminal || pending.has_value()) {
            return submitted;
          }
        }
        return CompleteSceneGenerationReset(event_polls);
      }
      if (pending.has_value()) {
        const RendererInProcessSessionResult submitted =
            TryPending(event_polls);
        if (submitted.terminal || pending.has_value()) {
          return submitted;
        }
      }
      if (!producer->has_open_scene_generation()) {
        return Result(
            RendererInProcessSessionStatus::SCENE_GENERATION_RESET, true,
            false, event_polls);
      }
      if (last_scene_surface_revision == 0U || last_scene_width == 0U ||
          last_scene_height == 0U) {
        return Failure(RendererInProcessSessionStatus::FAILED_INTERNAL,
                       Render::ValidationResult::Success(),
                       Render::RenderOperationCode::INVALID_ARGUMENT,
                       event_polls);
      }

      Render::GraphicsSceneSnapshotProduceResult finalized =
          producer->FinalizeSceneGeneration();
      if (!finalized) {
        return Failure(RendererInProcessSessionStatus::FAILED_PRODUCER,
                       finalized.validation,
                       Render::RenderOperationCode::INVALID_ARGUMENT,
                       event_polls);
      }
      static_assert(
          std::is_nothrow_move_assignable<
              std::optional<PendingProduction>>::value,
          "generation-finalization retention must not throw");
      PendingProduction retained;
      retained.production = std::move(finalized.production);
      retained.captured_surface_revision = last_scene_surface_revision;
      retained.captured_width = last_scene_width;
      retained.captured_height = last_scene_height;
      retained.asset_submitted =
          !retained.production.asset_delta.has_value();
      finalizing_scene_snapshot_id =
          retained.production.scene_snapshot->snapshot_id();
      pending = std::move(retained);
      scene_generation_reset_pending = true;

      const RendererInProcessSessionResult submitted =
          TryPending(event_polls);
      if (submitted.terminal || pending.has_value()) {
        return submitted;
      }
      return CompleteSceneGenerationReset(event_polls);
    } catch (const std::bad_alloc &) {
      return Failure(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (const std::length_error &) {
      return Failure(RendererInProcessSessionStatus::FAILED_ALLOCATION,
                     Render::ValidationResult::Success(),
                     Render::RenderOperationCode::OUT_OF_MEMORY, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_INTERNAL,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
  }

  RendererInProcessSessionResult Shutdown() noexcept {
    if (closed) {
      return Result(RendererInProcessSessionStatus::CLOSED, true);
    }
    if (!started) {
      const Render::RenderOperationResult stopped =
          StopFrontendForClosure(closure_shutdown_timeout_nanoseconds);
      if (!stopped) {
        return Failure(
            RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN,
            Render::ValidationResult::Success(), stopped.code);
      }
      QuiesceEventPump();
      closed = true;
      return Result(RendererInProcessSessionStatus::CLOSED, !terminal,
                    terminal);
    }

    shutdown_requested = true;
    simulation_granted = false;
    std::uint32_t event_polls = 0U;
    try {
      if (pending.has_value() && !terminal) {
        const RendererInProcessSessionResult submitted =
            TryPending(event_polls);
        if (pending.has_value() && !submitted.terminal) {
          return submitted;
        }
      }
      if (scene_generation_reset_pending && !pending.has_value() &&
          !terminal) {
        const RendererInProcessSessionResult reset =
            CompleteSceneGenerationReset(event_polls);
        if (reset.terminal) {
          // Continue into frontend teardown; the caller still receives the
          // stable terminal cause through the closed result.
        }
      }

      const Render::RenderOperationResult stopped = StopFrontendForClosure(
          config.shutdown_timeout_nanoseconds);
      if (!stopped) {
        if (stopped.code == Render::RenderOperationCode::TIMEOUT) {
          return Failure(
              RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN,
              Render::ValidationResult::Success(), stopped.code,
              event_polls);
        }
        return Poison(
            RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN,
            Render::ValidationResult::Success(), stopped.code, event_polls);
      }
      QuiesceEventPump();
      closed = true;
      started = false;
      return Result(RendererInProcessSessionStatus::CLOSED, !terminal,
                    terminal, event_polls);
    } catch (...) {
      return Poison(RendererInProcessSessionStatus::FAILED_INTERNAL,
                    Render::ValidationResult::Success(),
                    Render::RenderOperationCode::BACKEND_FAILURE,
                    event_polls);
    }
  }

  Render::IRendererFrontend &frontend;
  IRendererInProcessEventPump &event_pump;
  IRendererInProcessFramePolicy &frame_policy;
  RendererInProcessSessionConfig config;
  Render::FrontendSurfaceUpdate current_surface;
  std::unique_ptr<Render::GraphicsSceneSnapshotProducer> producer;
  std::unique_ptr<Render::RendererFrontendDirectDispatcher> dispatcher;
  std::optional<PendingProduction> pending;
  std::optional<Render::FrontendSurfaceUpdate> pending_surface;
  RendererInProcessSessionStatus terminal_cause =
      RendererInProcessSessionStatus::FAILED_INTERNAL;
  std::uint64_t last_scene_surface_revision = 0U;
  std::uint32_t last_scene_width = 0U;
  std::uint32_t last_scene_height = 0U;
  std::uint64_t finalizing_scene_snapshot_id = 0U;
  std::uint64_t closure_shutdown_timeout_nanoseconds = 5'000'000'000ULL;
  bool scene_generation_reset_pending = false;
  bool frontend_initialized = false;
  bool event_pump_quiesced = false;
  bool shutdown_requested = false;
  bool awaiting_frontend_surface_update = false;
  bool simulation_granted = false;
  bool started = false;
  bool closed = false;
  bool terminal = false;
};

RendererInProcessSession::RendererInProcessSession(
    Render::IRendererFrontend &frontend,
    IRendererInProcessEventPump &event_pump,
    IRendererInProcessFramePolicy &frame_policy)
    : impl_(std::make_unique<Impl>(frontend, event_pump, frame_policy)) {}

RendererInProcessSession::~RendererInProcessSession() {
  if (impl_ != nullptr) {
    (void)impl_->Shutdown();
  }
}

RendererInProcessSessionResult RendererInProcessSession::Start(
    const RendererInProcessSessionConfig &config) {
  return impl_->Start(config);
}

RendererInProcessSessionResult
RendererInProcessSession::PumpEventsBeforeSimulation() noexcept {
  return impl_->PumpEventsBeforeSimulation();
}

RendererInProcessSessionResult
RendererInProcessSession::SkipUpdatedScene() noexcept {
  return impl_->SkipUpdatedScene();
}

RendererInProcessSessionResult RendererInProcessSession::PostUpdatedScene(
    Render::IJoinedGraphicsSceneSource &source) noexcept {
  return impl_->PostUpdatedScene(source);
}

RendererInProcessSessionResult
RendererInProcessSession::ResetSceneGeneration() noexcept {
  return impl_->ResetSceneGeneration();
}

RendererInProcessSessionResult RendererInProcessSession::Shutdown() noexcept {
  return impl_->Shutdown();
}

bool RendererInProcessSession::active() const noexcept {
  return impl_ != nullptr && impl_->started && !impl_->closed &&
         !impl_->terminal;
}

bool RendererInProcessSession::has_pending_frame() const noexcept {
  return impl_ != nullptr && impl_->pending.has_value();
}

bool RendererInProcessSession::terminal() const noexcept {
  return impl_ != nullptr && impl_->terminal;
}

std::uint64_t RendererInProcessSession::registry_id() const noexcept {
  return impl_ != nullptr && impl_->dispatcher != nullptr
             ? impl_->dispatcher->registry_id()
             : 0U;
}

std::uint64_t RendererInProcessSession::asset_sequence() const noexcept {
  return impl_ != nullptr && impl_->dispatcher != nullptr
             ? impl_->dispatcher->asset_sequence()
             : 0U;
}

std::uint64_t RendererInProcessSession::scene_generation() const noexcept {
  return impl_ != nullptr && impl_->dispatcher != nullptr
             ? impl_->dispatcher->scene_generation()
             : 0U;
}

std::uint64_t
RendererInProcessSession::last_consumed_scene_snapshot_id() const noexcept {
  return impl_ != nullptr && impl_->dispatcher != nullptr
             ? impl_->dispatcher->last_consumed_scene_snapshot_id()
             : 0U;
}

std::uint64_t
RendererInProcessSession::last_frontend_frame_id() const noexcept {
  return impl_ != nullptr && impl_->dispatcher != nullptr
             ? impl_->dispatcher->last_frontend_frame_id()
             : 0U;
}

Render::FrontendSurfaceUpdate
RendererInProcessSession::current_surface() const noexcept {
  return impl_ != nullptr ? impl_->current_surface
                          : Render::FrontendSurfaceUpdate{};
}

bool IsKnownRendererInProcessSessionStatus(
    RendererInProcessSessionStatus status) noexcept {
  switch (status) {
  case RendererInProcessSessionStatus::READY:
  case RendererInProcessSessionStatus::EVENTS_PUMPED:
  case RendererInProcessSessionStatus::SIMULATION_SKIPPED:
  case RendererInProcessSessionStatus::WAITING_FOR_SURFACE:
  case RendererInProcessSessionStatus::FRAME_COMPLETED:
  case RendererInProcessSessionStatus::FRAME_RETIRED:
  case RendererInProcessSessionStatus::PENDING_BACKPRESSURE:
  case RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE:
  case RendererInProcessSessionStatus::CAPTURE_REJECTED:
  case RendererInProcessSessionStatus::SCENE_GENERATION_RESET:
  case RendererInProcessSessionStatus::SHUTDOWN_REQUESTED:
  case RendererInProcessSessionStatus::CLOSED:
  case RendererInProcessSessionStatus::REJECTED_CONFIGURATION:
  case RendererInProcessSessionStatus::REJECTED_NOT_READY:
  case RendererInProcessSessionStatus::FAILED_FRONTEND_INITIALIZATION:
  case RendererInProcessSessionStatus::FAILED_EVENT_PUMP:
  case RendererInProcessSessionStatus::FAILED_SURFACE_UPDATE:
  case RendererInProcessSessionStatus::FAILED_PRODUCER:
  case RendererInProcessSessionStatus::FAILED_DISPATCH:
  case RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN:
  case RendererInProcessSessionStatus::FAILED_ALLOCATION:
  case RendererInProcessSessionStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererInProcessSessionStatus status) noexcept {
  switch (status) {
  case RendererInProcessSessionStatus::READY: return "ready";
  case RendererInProcessSessionStatus::EVENTS_PUMPED:
    return "events_pumped";
  case RendererInProcessSessionStatus::SIMULATION_SKIPPED:
    return "simulation_skipped";
  case RendererInProcessSessionStatus::WAITING_FOR_SURFACE:
    return "waiting_for_surface";
  case RendererInProcessSessionStatus::FRAME_COMPLETED:
    return "frame_completed";
  case RendererInProcessSessionStatus::FRAME_RETIRED:
    return "frame_retired";
  case RendererInProcessSessionStatus::PENDING_BACKPRESSURE:
    return "pending_backpressure";
  case RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE:
    return "pending_frontend_surface";
  case RendererInProcessSessionStatus::CAPTURE_REJECTED:
    return "capture_rejected";
  case RendererInProcessSessionStatus::SCENE_GENERATION_RESET:
    return "scene_generation_reset";
  case RendererInProcessSessionStatus::SHUTDOWN_REQUESTED:
    return "shutdown_requested";
  case RendererInProcessSessionStatus::CLOSED: return "closed";
  case RendererInProcessSessionStatus::REJECTED_CONFIGURATION:
    return "rejected_configuration";
  case RendererInProcessSessionStatus::REJECTED_NOT_READY:
    return "rejected_not_ready";
  case RendererInProcessSessionStatus::FAILED_FRONTEND_INITIALIZATION:
    return "failed_frontend_initialization";
  case RendererInProcessSessionStatus::FAILED_EVENT_PUMP:
    return "failed_event_pump";
  case RendererInProcessSessionStatus::FAILED_SURFACE_UPDATE:
    return "failed_surface_update";
  case RendererInProcessSessionStatus::FAILED_PRODUCER:
    return "failed_producer";
  case RendererInProcessSessionStatus::FAILED_DISPATCH:
    return "failed_dispatch";
  case RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN:
    return "failed_frontend_shutdown";
  case RendererInProcessSessionStatus::FAILED_ALLOCATION:
    return "failed_allocation";
  case RendererInProcessSessionStatus::FAILED_INTERNAL:
    return "failed_internal";
  }
  return "invalid";
}

} // namespace RoR
