/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextLiveSession.h"

#include <array>
#include <limits>

namespace RoR {
namespace {

constexpr std::size_t kReadBufferBytes = 64U * 1024U;

bool IsValidRuntime(
    const RendererOgreNextLiveSessionRuntime &runtime) noexcept {
  return runtime.version == kRendererOgreNextLiveSessionContractVersion &&
         runtime.frontend != nullptr && runtime.context != nullptr &&
         runtime.poll != nullptr;
}

bool AddCounter(std::uint64_t amount, std::uint64_t &value) noexcept {
  if (amount > (std::numeric_limits<std::uint64_t>::max)() - value) {
    return false;
  }
  value += amount;
  return true;
}

enum class ReverseSendOutcome : std::uint8_t {
  SENT = 0U,
  PEER_CLOSED,
  ENCODE_FAILED,
  WRITE_FAILED,
  COUNTER_OVERFLOW,
};

ReverseSendOutcome SendReverseFrame(
    const Render::RenderTransportEnvelopeEncodeResult &encoded,
    RendererBridgeChannel &channel,
    RendererOgreNextLiveSessionResult &result) noexcept {
  result.response_status = encoded.status;
  if (!encoded || encoded.bytes.empty()) {
    return ReverseSendOutcome::ENCODE_FAILED;
  }
  const RendererBridgeChannelResult written =
      channel.WriteAll(encoded.bytes.data(), encoded.bytes.size());
  result.channel_status = written.status;
  if (!AddCounter(static_cast<std::uint64_t>(written.bytes_transferred),
                  result.bytes_written)) {
    return ReverseSendOutcome::COUNTER_OVERFLOW;
  }
  if (written.status == RendererBridgeChannelStatus::PEER_CLOSED) {
    return ReverseSendOutcome::PEER_CLOSED;
  }
  if (!written || written.bytes_transferred != encoded.bytes.size()) {
    return ReverseSendOutcome::WRITE_FAILED;
  }
  if (!AddCounter(1U, result.responses_sent)) {
    return ReverseSendOutcome::COUNTER_OVERFLOW;
  }
  return ReverseSendOutcome::SENT;
}

RendererOgreNextLiveSessionStatus FailureForReverseSend(
    ReverseSendOutcome outcome) noexcept {
  return outcome == ReverseSendOutcome::ENCODE_FAILED
             ? RendererOgreNextLiveSessionStatus::FAILED_RESPONSE_ENCODE
         : outcome == ReverseSendOutcome::WRITE_FAILED
             ? RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_WRITE
             : RendererOgreNextLiveSessionStatus::FAILED_INTERNAL;
}

bool HasReverseSequenceCapacity(std::uint64_t next_sequence,
                                std::uint64_t count) noexcept {
  const std::uint64_t maximum_valid =
      (std::numeric_limits<std::uint64_t>::max)() - 1U;
  return next_sequence != 0U && count != 0U &&
         next_sequence <= maximum_valid &&
         count - 1U <= maximum_valid - next_sequence;
}

RendererOgreNextLiveSessionResult FinishWithClose(
    RendererOgreNextLiveSessionResult result,
    RendererOgreNextLiveSessionStatus status,
    RendererBridgeChannel &channel, bool completed) noexcept {
  const RendererBridgeChannelResult closed = channel.Close();
  result.channel_status = closed.status;
  result.completed = completed;
  if (closed.status == RendererBridgeChannelStatus::FAILED_CLOSE) {
    result.status = RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_CLOSE;
    result.completed = false;
    return result;
  }
  result.status = status;
  return result;
}

} // namespace

RendererOgreNextLiveSessionResult RunRendererOgreNextLiveSession(
    const RendererBridgeEndpoint &endpoint,
    const RendererOgreNextLiveSessionRuntime &runtime) noexcept {
  RendererOgreNextLiveSessionResult result;
  if (!IsValidRendererBridgeEndpoint(endpoint) ||
      endpoint.role != RendererBridgeRole::PRESENTATION_FRONTEND) {
    result.status =
        RendererOgreNextLiveSessionStatus::REJECTED_INVALID_ENDPOINT;
    return result;
  }
  if (!IsValidRuntime(runtime)) {
    result.status =
        RendererOgreNextLiveSessionStatus::REJECTED_INVALID_RUNTIME;
    return result;
  }

  RendererBridgeChannel channel(endpoint);
  try {
    const RendererBridgeChannelResult adopted = channel.Adopt();
    result.channel_status = adopted.status;
    result.channel_adopted = channel.adopted();
    if (!adopted) {
      result.status =
          RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_ADOPTION;
      return FinishWithClose(result, result.status, channel, false);
    }

    Render::RenderTransportStreamDecoder decoder(
        Render::kRenderTransportStreamAbsoluteMaximumPayloadBytes);
    Render::RendererFrontendTransportDispatcher dispatcher(
        *runtime.frontend, endpoint.session_id);
    if (dispatcher.terminal()) {
      result.dispatch_status = dispatcher.terminal_cause();
      return FinishWithClose(
          result, RendererOgreNextLiveSessionStatus::FAILED_DISPATCH, channel,
          false);
    }

    std::uint64_t reverse_sequence = 1U;
    std::uint64_t next_control_command_id = 1U;
    std::uint64_t last_presented_scene_sequence = 0U;
    std::uint64_t last_presented_snapshot_id = 0U;

    Render::RenderBridgeControl ready;
    ready.kind = Render::RenderBridgeControlKind::PEER_READY;
    ready.registry_id = dispatcher.registry_id();
    ready.command_id = next_control_command_id;
    const ReverseSendOutcome ready_sent = SendReverseFrame(
        Render::EncodeRenderBridgeControlFrame(reverse_sequence, ready),
        channel, result);
    if (ready_sent == ReverseSendOutcome::PEER_CLOSED) {
      return FinishWithClose(
          result,
          RendererOgreNextLiveSessionStatus::COMPLETED_PEER_REVERSE_CLOSE,
          channel, true);
    }
    if (ready_sent != ReverseSendOutcome::SENT) {
      return FinishWithClose(result, FailureForReverseSend(ready_sent),
                             channel, false);
    }
    result.last_reverse_sequence = reverse_sequence;
    if (!AddCounter(1U, result.controls_sent)) {
      return FinishWithClose(
          result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL, channel,
          false);
    }
    ++reverse_sequence;
    ++next_control_command_id;

    std::array<std::uint8_t, kReadBufferBytes> bytes{};
    for (;;) {
      const RendererBridgeChannelResult read =
          channel.ReadSome(bytes.data(), bytes.size());
      result.channel_status = read.status;
      if (!AddCounter(static_cast<std::uint64_t>(read.bytes_transferred),
                      result.bytes_read)) {
        return FinishWithClose(
            result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
            channel, false);
      }
      if (read.status == RendererBridgeChannelStatus::PEER_CLOSED) {
        const Render::RenderTransportStreamResult finished = decoder.Finish();
        result.stream_status = finished.status;
        if (finished.status != Render::RenderTransportStreamStatus::CLOSED) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_STREAM,
              channel, false);
        }
        return FinishWithClose(
            result, RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF,
            channel, true);
      }
      if (!read || read.bytes_transferred == 0U) {
        return FinishWithClose(
            result, RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_READ,
            channel, false);
      }

      std::size_t offset = 0U;
      while (offset < read.bytes_transferred) {
        const Render::RenderTransportStreamResult accepted = decoder.Accept(
            bytes.data() + offset, read.bytes_transferred - offset);
        result.stream_status = accepted.status;
        if (accepted.bytes_consumed == 0U) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_STREAM,
              channel, false);
        }
        offset += accepted.bytes_consumed;
        if (accepted.terminal) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_STREAM,
              channel, false);
        }
        if (accepted.status !=
            Render::RenderTransportStreamStatus::FRAME_READY) {
          continue;
        }

        Render::RenderTransportStreamFrameResult frame = decoder.TakeFrame();
        result.stream_status = frame.status;
        if (!frame) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_STREAM,
              channel, false);
        }

        RendererOgreNextLiveSessionObservation observation;
        bool polled = false;
        try {
          polled = runtime.poll(runtime.context, frame.sequence, &observation);
        } catch (...) {
          polled = false;
        }
        if (!polled || observation.version !=
                           kRendererOgreNextLiveSessionContractVersion ||
            (!observation.presentation_suspended &&
             observation.presentation_surface_revision == 0U) ||
            (observation.presentation_suspended &&
             observation.presentation_surface_revision != 0U)) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_EVENT_POLL,
              channel, false);
        }
        if (!HasReverseSequenceCapacity(reverse_sequence, 2U)) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
              channel, false);
        }

        if (observation.window_close_requested) {
          const ReverseSendOutcome input_sent = SendReverseFrame(
              Render::EncodeInputEventTransportFrame(
                  reverse_sequence, observation.response),
              channel, result);
          if (input_sent == ReverseSendOutcome::PEER_CLOSED) {
            return FinishWithClose(
                result,
                RendererOgreNextLiveSessionStatus::
                    COMPLETED_PEER_REVERSE_CLOSE,
                channel, true);
          }
          if (input_sent != ReverseSendOutcome::SENT) {
            return FinishWithClose(result,
                                   FailureForReverseSend(input_sent), channel,
                                   false);
          }
          result.last_reverse_sequence = reverse_sequence;
          if (!AddCounter(1U, result.input_batches_sent)) {
            return FinishWithClose(
                result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
                channel, false);
          }
          ++reverse_sequence;

          Render::RenderBridgeControl shutdown;
          shutdown.kind =
              Render::RenderBridgeControlKind::REQUEST_GRACEFUL_SHUTDOWN;
          shutdown.registry_id = dispatcher.registry_id();
          shutdown.command_id = next_control_command_id;
          const ReverseSendOutcome control_sent = SendReverseFrame(
              Render::EncodeRenderBridgeControlFrame(reverse_sequence,
                                                     shutdown),
              channel, result);
          if (control_sent == ReverseSendOutcome::PEER_CLOSED) {
            return FinishWithClose(
                result,
                RendererOgreNextLiveSessionStatus::
                    COMPLETED_PEER_REVERSE_CLOSE,
                channel, true);
          }
          if (control_sent != ReverseSendOutcome::SENT) {
            return FinishWithClose(result,
                                   FailureForReverseSend(control_sent),
                                   channel, false);
          }
          result.last_reverse_sequence = reverse_sequence;
          if (!AddCounter(1U, result.controls_sent)) {
            return FinishWithClose(
                result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
                channel, false);
          }
          return FinishWithClose(
              result,
              RendererOgreNextLiveSessionStatus::COMPLETED_WINDOW_CLOSE,
              channel, true);
        }

        Render::RendererFrontendPresentationPolicy policy;
        policy.requested_outputs = Render::FrameOutputMask::COLOR;
        policy.color_format = Render::PixelFormat::RGBA8_SRGB;
        policy.presentation_surface_revision =
            observation.presentation_surface_revision;
        policy.present = !observation.presentation_suspended;
        policy.allow_async_compute = false;
        const Render::RendererFrontendTransportDispatchResult dispatched =
            dispatcher.Dispatch(frame, policy);
        result.dispatch_status = dispatched.status;
        if (!dispatched) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_DISPATCH,
              channel, false);
        }
        result.last_forward_sequence = frame.sequence;
        if (frame.kind ==
            Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V1) {
          if (!AddCounter(1U, result.asset_frames)) {
            return FinishWithClose(
                result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
                channel, false);
          }
        } else if (frame.kind ==
                   Render::RenderTransportMessageKind::
                       SCENE_SNAPSHOT_V4_CAMERA_V2) {
          if (!AddCounter(1U, result.scene_frames) ||
              (policy.present &&
               !AddCounter(1U, result.presented_scene_frames))) {
            return FinishWithClose(
                result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
                channel, false);
          }
        } else {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_DISPATCH,
              channel, false);
        }

        const ReverseSendOutcome input_sent = SendReverseFrame(
            Render::EncodeInputEventTransportFrame(
                reverse_sequence, observation.response),
            channel, result);
        if (input_sent == ReverseSendOutcome::PEER_CLOSED) {
          return FinishWithClose(
              result,
              RendererOgreNextLiveSessionStatus::
                  COMPLETED_PEER_REVERSE_CLOSE,
              channel, true);
        }
        if (input_sent != ReverseSendOutcome::SENT) {
          return FinishWithClose(result, FailureForReverseSend(input_sent),
                                 channel, false);
        }
        result.last_reverse_sequence = reverse_sequence;
        if (!AddCounter(1U, result.input_batches_sent)) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
              channel, false);
        }
        ++reverse_sequence;

        std::uint64_t acknowledged_presented_scene_sequence =
            last_presented_scene_sequence;
        std::uint64_t acknowledged_presented_snapshot_id =
            last_presented_snapshot_id;
        if (frame.kind ==
                Render::RenderTransportMessageKind::
                    SCENE_SNAPSHOT_V4_CAMERA_V2 &&
            policy.present) {
          if (dispatched.scene_snapshot_id == 0U) {
            return FinishWithClose(
                result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
                channel, false);
          }
          acknowledged_presented_scene_sequence = frame.sequence;
          acknowledged_presented_snapshot_id = dispatched.scene_snapshot_id;
        }
        Render::RenderBridgeAcknowledgement acknowledgement;
        acknowledgement.registry_id = dispatcher.registry_id();
        acknowledgement.through_forward_sequence = frame.sequence;
        acknowledgement.presented_scene_sequence =
            acknowledged_presented_scene_sequence;
        acknowledgement.presented_snapshot_id =
            acknowledged_presented_snapshot_id;
        const ReverseSendOutcome acknowledgement_sent = SendReverseFrame(
            Render::EncodeRenderBridgeAcknowledgementFrame(reverse_sequence,
                                                           acknowledgement),
            channel, result);
        if (acknowledgement_sent == ReverseSendOutcome::PEER_CLOSED) {
          return FinishWithClose(
              result,
              RendererOgreNextLiveSessionStatus::
                  COMPLETED_PEER_REVERSE_CLOSE,
              channel, true);
        }
        if (acknowledgement_sent != ReverseSendOutcome::SENT) {
          return FinishWithClose(
              result, FailureForReverseSend(acknowledgement_sent), channel,
              false);
        }
        result.last_reverse_sequence = reverse_sequence;
        result.last_acknowledged_forward_sequence = frame.sequence;
        result.last_presented_scene_sequence =
            acknowledged_presented_scene_sequence;
        result.last_presented_snapshot_id =
            acknowledged_presented_snapshot_id;
        last_presented_scene_sequence =
            acknowledged_presented_scene_sequence;
        last_presented_snapshot_id = acknowledged_presented_snapshot_id;
        if (!AddCounter(1U, result.acknowledgements_sent)) {
          return FinishWithClose(
              result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL,
              channel, false);
        }
        ++reverse_sequence;
      }
    }
  } catch (...) {
    return FinishWithClose(
        result, RendererOgreNextLiveSessionStatus::FAILED_INTERNAL, channel,
        false);
  }
}

bool IsKnownRendererOgreNextLiveSessionStatus(
    RendererOgreNextLiveSessionStatus status) noexcept {
  switch (status) {
  case RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF:
  case RendererOgreNextLiveSessionStatus::COMPLETED_WINDOW_CLOSE:
  case RendererOgreNextLiveSessionStatus::COMPLETED_PEER_REVERSE_CLOSE:
  case RendererOgreNextLiveSessionStatus::REJECTED_INVALID_ENDPOINT:
  case RendererOgreNextLiveSessionStatus::REJECTED_INVALID_RUNTIME:
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_ADOPTION:
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_READ:
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_WRITE:
  case RendererOgreNextLiveSessionStatus::FAILED_STREAM:
  case RendererOgreNextLiveSessionStatus::FAILED_DISPATCH:
  case RendererOgreNextLiveSessionStatus::FAILED_EVENT_POLL:
  case RendererOgreNextLiveSessionStatus::FAILED_RESPONSE_ENCODE:
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_CLOSE:
  case RendererOgreNextLiveSessionStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererOgreNextLiveSessionStatus status) noexcept {
  switch (status) {
  case RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF:
    return "completed-peer-eof";
  case RendererOgreNextLiveSessionStatus::COMPLETED_WINDOW_CLOSE:
    return "completed-window-close";
  case RendererOgreNextLiveSessionStatus::COMPLETED_PEER_REVERSE_CLOSE:
    return "completed-peer-reverse-close";
  case RendererOgreNextLiveSessionStatus::REJECTED_INVALID_ENDPOINT:
    return "rejected-invalid-endpoint";
  case RendererOgreNextLiveSessionStatus::REJECTED_INVALID_RUNTIME:
    return "rejected-invalid-runtime";
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_ADOPTION:
    return "failed-channel-adoption";
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_READ:
    return "failed-channel-read";
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_WRITE:
    return "failed-channel-write";
  case RendererOgreNextLiveSessionStatus::FAILED_STREAM:
    return "failed-stream";
  case RendererOgreNextLiveSessionStatus::FAILED_DISPATCH:
    return "failed-dispatch";
  case RendererOgreNextLiveSessionStatus::FAILED_EVENT_POLL:
    return "failed-event-poll";
  case RendererOgreNextLiveSessionStatus::FAILED_RESPONSE_ENCODE:
    return "failed-response-encode";
  case RendererOgreNextLiveSessionStatus::FAILED_CHANNEL_CLOSE:
    return "failed-channel-close";
  case RendererOgreNextLiveSessionStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
