/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14ProductSession.h"

#include <chrono>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace RoR {
namespace {

RendererOgre14ProductSessionResult MakeResult(
    RendererOgre14ProductSessionStatus status, bool accepted,
    bool terminal = false) noexcept {
  RendererOgre14ProductSessionResult result;
  result.status = status;
  result.accepted = accepted;
  result.terminal = terminal;
  return result;
}

bool IsBackpressure(RendererOgre14GameHostSessionStatus status) noexcept {
  return status == RendererOgre14GameHostSessionStatus::BACKPRESSURE;
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

private:
  Render::IJoinedGraphicsSceneSource &source_;
  bool active_ = true;
};

} // namespace

RendererOgre14ProductSession::RendererOgre14ProductSession(
    RendererOgre14GameBridge &bridge,
    IRendererOgre14InputTarget &input_target)
    : host_(bridge), input_target_(input_target), input_adapter_(input_target) {}

RendererOgre14ProductSession::~RendererOgre14ProductSession() {
  (void)Shutdown();
}

RendererOgre14ProductSessionResult RendererOgre14ProductSession::Start(
    const RendererOgre14ProductSessionConfig &config) {
  if (started_ || closed_ ||
      config.version != kRendererOgre14ProductSessionContractVersion ||
      config.shutdown_drain_timeout_milliseconds == 0U ||
      config.shutdown_drain_timeout_milliseconds > 30000U) {
    return MakeResult(
        RendererOgre14ProductSessionStatus::REJECTED_CONFIGURATION, false);
  }
  const RendererOgre14GameHostSessionResult started = host_.Start(config.host);
  if (!started) {
    return FailureFromHost(started);
  }
  try {
    config_ = config;
    config_.producer.registry_id = host_.registry_id();
    producer_ = std::make_unique<Render::GraphicsSceneSnapshotProducer>(
        config_.producer);
    if (!input_adapter_.ActivateTarget()) {
      (void)host_.Close();
      closed_ = true;
      RendererOgre14ProductSessionResult result = MakeResult(
          RendererOgre14ProductSessionStatus::FAILED_INPUT, false, true);
      result.host_status = started.status;
      result.input_status = RendererOgre14InputApplyStatus::FAILED_TARGET;
      return result;
    }
    started_ = true;
    RendererOgre14ProductSessionResult result =
        MakeResult(RendererOgre14ProductSessionStatus::READY, true);
    result.host_status = started.status;
    return result;
  } catch (const std::bad_alloc &) {
    (void)host_.Close();
    closed_ = true;
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_INTERNAL,
                      false, true);
  } catch (...) {
    (void)host_.Close();
    closed_ = true;
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_PRODUCER,
                      false, true);
  }
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::FailureFromHost(
    const RendererOgre14GameHostSessionResult &host_result) const noexcept {
  RendererOgre14ProductSessionResult result = MakeResult(
      RendererOgre14ProductSessionStatus::FAILED_HOST, false,
      host_result.terminal);
  result.host_status = host_result.status;
  result.surface_revision = host_result.surface_revision;
  result.validation.code = host_result.validation_code;
  result.pending_frame = pending_.has_value();
  return result;
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::PumpReverse() {
  if (!started_ || closed_) {
    return MakeResult(RendererOgre14ProductSessionStatus::REJECTED_NOT_READY,
                      false);
  }
  RendererOgre14ProductSessionResult result =
      MakeResult(RendererOgre14ProductSessionStatus::REVERSE_DRAINED, true);
  for (;;) {
    const RendererOgre14GameHostPollResult polled = host_.PollReverse();
    if (!polled.available()) {
      if (polled.result.terminal) {
        return FailureFromHost(polled.result);
      }
      break;
    }
    ++result.reverse_messages;
    result.host_status = polled.result.status;
    result.surface_revision = polled.result.surface_revision;
    switch (polled.message.kind) {
    case Render::RenderTransportMessageKind::INPUT_EVENT_BATCH_V1: {
      ++result.input_batches;
      if (polled.message.input == nullptr) {
        result.status = RendererOgre14ProductSessionStatus::FAILED_INPUT;
        result.terminal = true;
        result.accepted = false;
        return result;
      }
      const RendererOgre14InputApplyResult applied =
          input_adapter_.Apply(*polled.message.input);
      result.input_status = applied.status;
      if (!applied) {
        result.status = RendererOgre14ProductSessionStatus::FAILED_INPUT;
        result.terminal = true;
        result.accepted = false;
        return result;
      }
      break;
    }
    case Render::RenderTransportMessageKind::
        RENDER_BRIDGE_ACKNOWLEDGEMENT_V1:
      ++result.acknowledgements;
      break;
    case Render::RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1:
      ++result.controls;
      if (polled.message.control.kind ==
          Render::RenderBridgeControlKind::REQUEST_GRACEFUL_SHUTDOWN) {
        // Control has its own reverse sequence and must not be fabricated as
        // input lineage. Route it through the same idempotent app close sink.
        input_target_.WindowCloseRequested();
      }
      break;
    default:
      result.status = RendererOgre14ProductSessionStatus::FAILED_HOST;
      result.terminal = true;
      result.accepted = false;
      return result;
    }
  }
  result.pending_frame = pending_.has_value();
  return result;
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::TryPending() {
  if (!pending_.has_value()) {
    return MakeResult(RendererOgre14ProductSessionStatus::FRAME_QUEUED, true);
  }
  PendingProduction &pending = *pending_;
  RendererOgre14ProductSessionResult result;
  result.pending_frame = true;
  result.surface_revision = pending.captured_surface_revision;
  result.snapshot_id = pending.production.scene_snapshot != nullptr
                           ? pending.production.scene_snapshot->snapshot_id()
                           : 0U;

  if (!pending.asset_submitted && pending.production.asset_delta.has_value()) {
    const RendererOgre14GameHostSessionResult submitted =
        host_.Submit(*pending.production.asset_delta);
    result.host_status = submitted.status;
    if (!submitted) {
      if (IsBackpressure(submitted.status)) {
        result.status =
            RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE;
        return result;
      }
      return FailureFromHost(submitted);
    }
    pending.asset_submitted = true;
  } else if (!pending.production.asset_delta.has_value()) {
    pending.asset_submitted = true;
  }

  const RendererOgre14GameHostSessionResult posted =
      host_.PostPhysicsCapturedAtSurface(
          *pending.production.scene_snapshot, pending.production.camera,
          pending.captured_surface_revision);
  result.host_status = posted.status;
  if (!posted) {
    if (IsBackpressure(posted.status) ||
        posted.status ==
            RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE) {
      result.status =
          RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE;
      return result;
    }
    return FailureFromHost(posted);
  }
  last_scene_surface_revision_ = pending.captured_surface_revision;
  pending_.reset();
  result.status = RendererOgre14ProductSessionStatus::FRAME_QUEUED;
  result.accepted = true;
  result.pending_frame = false;
  return result;
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::PostUpdatedScene(
    Render::IJoinedGraphicsSceneSource &source) {
  if (!started_ || closed_ || producer_ == nullptr) {
    return MakeResult(RendererOgre14ProductSessionStatus::REJECTED_NOT_READY,
                      false);
  }
  if (host_.terminal()) {
    RendererOgre14GameHostSessionResult host_result;
    host_result.status = host_.terminal_cause();
    host_result.terminal_cause = host_.terminal_cause();
    host_result.terminal = true;
    return FailureFromHost(host_result);
  }
  if (scene_generation_reset_pending_) {
    // The unload requester owns completion of this state machine. Ordinary
    // frame pumping may drain its retained final production, but must never
    // capture a new generation before the ordered reset boundary is consumed.
    return TryPending();
  }
  if (pending_.has_value()) {
    return TryPending();
  }

  const Render::RenderBridgeSurfaceState surface =
      host_.current_surface_state();
  if (!host_.peer_ready() || surface.surface_revision == 0U ||
      surface.suspended || surface.drawable_width == 0U ||
      surface.drawable_height == 0U) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::WAITING_FOR_SURFACE, true);
    result.surface_revision = surface.surface_revision;
    return result;
  }

  Render::GraphicsSceneFrameInput frame;
  const Render::ValidationResult captured =
      source.CaptureJoinedGraphicsFrame(frame);
  if (!captured) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::CAPTURE_REJECTED, false);
    result.validation = captured;
    result.surface_revision = surface.surface_revision;
    return result;
  }
  JoinedCaptureGuard capture_guard(source);
  if (frame.camera.width != surface.drawable_width ||
      frame.camera.height != surface.drawable_height) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::WAITING_FOR_CAMERA_EXTENT, true);
    result.surface_revision = surface.surface_revision;
    return result;
  }

  Render::GraphicsSceneSnapshotProduceResult produced =
      producer_->Produce(frame);
  if (!produced) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::FAILED_PRODUCER, false);
    result.validation = produced.validation;
    result.surface_revision = surface.surface_revision;
    return result;
  }
  capture_guard.Commit();
  PendingProduction pending;
  pending.production = std::move(produced.production);
  pending.captured_surface_revision = surface.surface_revision;
  pending.asset_submitted = !pending.production.asset_delta.has_value();
  pending_ = std::move(pending);
  return TryPending();
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::CompleteSceneGenerationReset() {
  if (!scene_generation_reset_pending_ || pending_.has_value() ||
      finalizing_scene_snapshot_id_ == 0U) {
    return MakeResult(RendererOgre14ProductSessionStatus::REJECTED_NOT_READY,
                      false);
  }
  const RendererOgre14GameHostSessionResult reset =
      host_.CompleteSceneGeneration(finalizing_scene_snapshot_id_);
  if (!reset) {
    if (IsBackpressure(reset.status)) {
      RendererOgre14ProductSessionResult result = MakeResult(
          RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE, false);
      result.host_status = reset.status;
      result.surface_revision = last_scene_surface_revision_;
      result.snapshot_id = finalizing_scene_snapshot_id_;
      return result;
    }
    return FailureFromHost(reset);
  }
  RendererOgre14ProductSessionResult result = MakeResult(
      RendererOgre14ProductSessionStatus::SCENE_GENERATION_RESET, true);
  result.host_status = reset.status;
  result.surface_revision = last_scene_surface_revision_;
  result.snapshot_id = finalizing_scene_snapshot_id_;
  scene_generation_reset_pending_ = false;
  finalizing_scene_snapshot_id_ = 0U;
  return result;
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::ResetSceneGeneration() noexcept {
  try {
    return ResetSceneGenerationImpl();
  } catch (const std::bad_alloc &) {
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_ALLOCATION,
                      false);
  } catch (const std::length_error &) {
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_ALLOCATION,
                      false);
  } catch (...) {
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_INTERNAL,
                      false, true);
  }
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::ResetSceneGenerationImpl() {
  if (!started_ || closed_ || producer_ == nullptr) {
    return MakeResult(RendererOgre14ProductSessionStatus::REJECTED_NOT_READY,
                      false);
  }
  if (host_.terminal()) {
    RendererOgre14GameHostSessionResult host_result;
    host_result.status = host_.terminal_cause();
    host_result.terminal_cause = host_.terminal_cause();
    host_result.terminal = true;
    return FailureFromHost(host_result);
  }

  if (scene_generation_reset_pending_) {
    if (pending_.has_value()) {
      const RendererOgre14ProductSessionResult submitted = TryPending();
      if (submitted.terminal || pending_.has_value()) {
        return submitted;
      }
    }
    return CompleteSceneGenerationReset();
  }

  if (pending_.has_value()) {
    const RendererOgre14ProductSessionResult submitted = TryPending();
    if (submitted.terminal || pending_.has_value()) {
      return submitted;
    }
  }

  if (!producer_->has_open_scene_generation()) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::SCENE_GENERATION_RESET, true);
    result.host_status = RendererOgre14GameHostSessionStatus::READY;
    return result;
  }
  if (last_scene_surface_revision_ == 0U) {
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_INTERNAL,
                      false);
  }

  Render::GraphicsSceneSnapshotProduceResult finalized =
      producer_->FinalizeSceneGeneration();
  if (!finalized) {
    RendererOgre14ProductSessionResult result = MakeResult(
        RendererOgre14ProductSessionStatus::FAILED_PRODUCER, false);
    result.validation = finalized.validation;
    return result;
  }
  // FinalizeSceneGeneration() commits the producer only after every candidate
  // allocation succeeds. From this point through retention, moves must be
  // statically non-throwing: otherwise an exception could strand the producer
  // in a closed generation without the final production needed for retry.
  static_assert(
      std::is_nothrow_move_constructible<
          Render::GraphicsSceneSnapshotProduceResult>::value,
      "producer result retention must not throw after generation commit");
  static_assert(
      std::is_nothrow_move_assignable<std::optional<PendingProduction>>::value,
      "pending generation-finalization retention must not throw");
  PendingProduction pending;
  pending.production = std::move(finalized.production);
  pending.captured_surface_revision = last_scene_surface_revision_;
  pending.asset_submitted = !pending.production.asset_delta.has_value();
  finalizing_scene_snapshot_id_ =
      pending.production.scene_snapshot->snapshot_id();
  scene_generation_reset_pending_ = true;
  pending_ = std::move(pending);

  const RendererOgre14ProductSessionResult submitted = TryPending();
  if (submitted.terminal || pending_.has_value()) {
    return submitted;
  }
  return CompleteSceneGenerationReset();
}

RendererOgre14ProductSessionResult
RendererOgre14ProductSession::Shutdown() noexcept {
  if (closed_) {
    return MakeResult(RendererOgre14ProductSessionStatus::CLOSED, true);
  }
  if (!started_) {
    closed_ = true;
    (void)host_.Close();
    return MakeResult(RendererOgre14ProductSessionStatus::CLOSED, true);
  }

  try {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            config_.shutdown_drain_timeout_milliseconds);
    while (pending_.has_value() &&
           std::chrono::steady_clock::now() < deadline &&
           !host_.terminal()) {
      const RendererOgre14ProductSessionResult reverse = PumpReverse();
      if (reverse.terminal)
        break;
      const RendererOgre14ProductSessionResult submitted = TryPending();
      if (submitted.terminal)
        break;
      if (pending_.has_value())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (pending_.has_value()) {
      (void)host_.Close();
      closed_ = true;
      RendererOgre14ProductSessionResult result = MakeResult(
          RendererOgre14ProductSessionStatus::FAILED_SHUTDOWN_TIMEOUT, false,
          true);
      result.pending_frame = true;
      return result;
    }

    const RendererOgre14GameHostSessionResult finishing =
        host_.FinishForward();
    if (!finishing && !finishing.terminal) {
      (void)host_.Close();
      closed_ = true;
      return FailureFromHost(finishing);
    }
    while (!host_.shutdown_complete() && !host_.terminal() &&
           std::chrono::steady_clock::now() < deadline) {
      const RendererOgre14ProductSessionResult reverse = PumpReverse();
      if (reverse.terminal)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool graceful = host_.shutdown_complete() && !host_.terminal();
    const RendererOgre14GameHostSessionResult closed = host_.Close();
    closed_ = true;
    if (!graceful) {
      RendererOgre14ProductSessionResult result =
          closed.terminal
              ? FailureFromHost(closed)
              : MakeResult(RendererOgre14ProductSessionStatus::
                               FAILED_SHUTDOWN_TIMEOUT,
                           false, true);
      return result;
    }
    RendererOgre14ProductSessionResult result =
        MakeResult(RendererOgre14ProductSessionStatus::CLOSED, true);
    result.host_status = closed.status;
    return result;
  } catch (...) {
    (void)host_.Close();
    closed_ = true;
    return MakeResult(RendererOgre14ProductSessionStatus::FAILED_INTERNAL,
                      false, true);
  }
}

bool IsKnownRendererOgre14ProductSessionStatus(
    RendererOgre14ProductSessionStatus status) noexcept {
  switch (status) {
  case RendererOgre14ProductSessionStatus::READY:
  case RendererOgre14ProductSessionStatus::REVERSE_DRAINED:
  case RendererOgre14ProductSessionStatus::SCENE_GENERATION_RESET:
  case RendererOgre14ProductSessionStatus::WAITING_FOR_SURFACE:
  case RendererOgre14ProductSessionStatus::WAITING_FOR_CAMERA_EXTENT:
  case RendererOgre14ProductSessionStatus::FRAME_QUEUED:
  case RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE:
  case RendererOgre14ProductSessionStatus::CAPTURE_REJECTED:
  case RendererOgre14ProductSessionStatus::CLOSED:
  case RendererOgre14ProductSessionStatus::REJECTED_CONFIGURATION:
  case RendererOgre14ProductSessionStatus::REJECTED_NOT_READY:
  case RendererOgre14ProductSessionStatus::FAILED_HOST:
  case RendererOgre14ProductSessionStatus::FAILED_INPUT:
  case RendererOgre14ProductSessionStatus::FAILED_PRODUCER:
  case RendererOgre14ProductSessionStatus::FAILED_ALLOCATION:
  case RendererOgre14ProductSessionStatus::FAILED_SHUTDOWN_TIMEOUT:
  case RendererOgre14ProductSessionStatus::FAILED_INTERNAL: return true;
  }
  return false;
}

const char *ToString(RendererOgre14ProductSessionStatus status) noexcept {
  switch (status) {
  case RendererOgre14ProductSessionStatus::READY: return "ready";
  case RendererOgre14ProductSessionStatus::REVERSE_DRAINED:
    return "reverse_drained";
  case RendererOgre14ProductSessionStatus::SCENE_GENERATION_RESET:
    return "scene_generation_reset";
  case RendererOgre14ProductSessionStatus::WAITING_FOR_SURFACE:
    return "waiting_for_surface";
  case RendererOgre14ProductSessionStatus::WAITING_FOR_CAMERA_EXTENT:
    return "waiting_for_camera_extent";
  case RendererOgre14ProductSessionStatus::FRAME_QUEUED:
    return "frame_queued";
  case RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE:
    return "pending_backpressure";
  case RendererOgre14ProductSessionStatus::CAPTURE_REJECTED:
    return "capture_rejected";
  case RendererOgre14ProductSessionStatus::CLOSED: return "closed";
  case RendererOgre14ProductSessionStatus::REJECTED_CONFIGURATION:
    return "rejected_configuration";
  case RendererOgre14ProductSessionStatus::REJECTED_NOT_READY:
    return "rejected_not_ready";
  case RendererOgre14ProductSessionStatus::FAILED_HOST: return "failed_host";
  case RendererOgre14ProductSessionStatus::FAILED_INPUT:
    return "failed_input";
  case RendererOgre14ProductSessionStatus::FAILED_PRODUCER:
    return "failed_producer";
  case RendererOgre14ProductSessionStatus::FAILED_ALLOCATION:
    return "failed_allocation";
  case RendererOgre14ProductSessionStatus::FAILED_SHUTDOWN_TIMEOUT:
    return "failed_shutdown_timeout";
  case RendererOgre14ProductSessionStatus::FAILED_INTERNAL:
    return "failed_internal";
  }
  return "invalid";
}

} // namespace RoR
