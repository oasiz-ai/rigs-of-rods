/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgre14GameHostSession.h"

#include "render/RenderAssetRegistry.h"
#include "render/RenderBridgeSessionIdentity.h"
#include "render/RenderTransportStream.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace RoR {
namespace {

constexpr std::size_t kMaximumChannelIoChunkBytes = 1024U * 1024U;
constexpr std::size_t kMaximumConfiguredForwardQueueBytes =
    1024U * 1024U * 1024U;
constexpr std::uint32_t kMaximumConfiguredMessages = 65536U;
constexpr std::uint32_t kMaximumIdleWaitMilliseconds = 1000U;

bool IsValidConfig(
    const RendererOgre14GameHostSessionConfig &config) noexcept {
  return config.version == kRendererOgre14GameHostSessionConfigVersion &&
         config.maximum_forward_queue_bytes >=
             Render::kRenderTransportEnvelopeHeaderBytes &&
         config.maximum_forward_queue_bytes <=
             kMaximumConfiguredForwardQueueBytes &&
         config.maximum_forward_messages != 0U &&
         config.maximum_forward_messages <= kMaximumConfiguredMessages &&
         config.maximum_unacknowledged_forward_messages >=
             config.maximum_forward_messages &&
         config.maximum_unacknowledged_forward_messages <=
             kMaximumConfiguredMessages &&
         config.maximum_reverse_messages != 0U &&
         config.maximum_reverse_messages <= kMaximumConfiguredMessages &&
         config.reverse_read_chunk_bytes != 0U &&
         config.reverse_read_chunk_bytes <= kMaximumChannelIoChunkBytes &&
         config.idle_wait_milliseconds != 0U &&
         config.idle_wait_milliseconds <= kMaximumIdleWaitMilliseconds;
}

RendererOgre14GameHostSessionResult MakeResult(
    RendererOgre14GameHostSessionStatus status, bool accepted,
    bool terminal = false) noexcept {
  RendererOgre14GameHostSessionResult result;
  result.status = status;
  result.accepted = accepted;
  result.terminal = terminal;
  return result;
}

} // namespace

struct RendererOgre14GameHostSession::Impl final {
  struct ForwardMessage final {
    Render::RenderTransportMessageKind kind =
        Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
    std::uint64_t sequence = 0U;
    std::uint64_t snapshot_id = 0U;
    std::vector<std::uint8_t> bytes;
    std::size_t offset = 0U;
  };

  struct ForwardLineage final {
    Render::RenderTransportMessageKind kind =
        Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
    std::uint64_t sequence = 0U;
    std::uint64_t snapshot_id = 0U;
  };

  explicit Impl(RendererOgre14GameBridge &owned_bridge) noexcept
      : bridge(&owned_bridge) {}

  RendererOgre14GameHostSessionResult ResultLocked(
      RendererOgre14GameHostSessionStatus operation_status,
      bool accepted) const noexcept {
    RendererOgre14GameHostSessionResult result =
        MakeResult(operation_status, accepted, terminal);
    result.terminal_cause = terminal_cause;
    result.channel_status = observed_channel_status;
    result.surface_revision = surface_state.surface_revision;
    return result;
  }

  void FailLocked(RendererOgre14GameHostSessionStatus cause,
                  Render::RenderTransportStatus transport,
                  RendererBridgeChannelStatus channel_failure) noexcept {
    if (!terminal) {
      terminal = true;
      terminal_cause = cause;
      transport_failure = transport;
      channel_failure_status = channel_failure;
    }
    stop_immediately = true;
    wake.notify_all();
  }

  RendererOgre14GameHostSessionResult TerminalResultLocked() const noexcept {
    RendererOgre14GameHostSessionResult result =
        ResultLocked(terminal ? terminal_cause
                              : RendererOgre14GameHostSessionStatus::CLOSED,
                     false);
    result.transport_status = transport_failure;
    result.channel_status =
        channel_failure_status != RendererBridgeChannelStatus::UNINITIALIZED
            ? channel_failure_status
            : result.channel_status;
    return result;
  }

  bool HasForwardCapacityLocked(std::size_t frame_bytes) const noexcept {
    return frame_bytes <= config.maximum_forward_queue_bytes &&
           queued_forward_bytes <=
               config.maximum_forward_queue_bytes - frame_bytes &&
           forward_queue.size() + forward_messages_in_flight <
               config.maximum_forward_messages &&
           forward_lineage.size() <
               config.maximum_unacknowledged_forward_messages;
  }

  bool ValidateAcknowledgementLocked(
      const Render::RenderBridgeAcknowledgement &acknowledgement) noexcept {
    if (acknowledgement.through_forward_sequence <=
            last_acknowledged_forward ||
        acknowledgement.through_forward_sequence > last_written_forward) {
      return false;
    }
    if (acknowledgement.presented_scene_sequence == 0U) {
      if (acknowledgement.presented_snapshot_id != 0U ||
          last_presented_scene_sequence != 0U) {
        return false;
      }
    } else if (acknowledgement.presented_scene_sequence ==
               last_presented_scene_sequence) {
      if (acknowledgement.presented_snapshot_id !=
          last_presented_snapshot_id) {
        return false;
      }
    } else {
      if (acknowledgement.presented_scene_sequence <
              last_presented_scene_sequence ||
          acknowledgement.presented_scene_sequence >
              acknowledgement.through_forward_sequence) {
        return false;
      }
      const auto found = std::find_if(
          forward_lineage.begin(), forward_lineage.end(),
          [&acknowledgement](const ForwardLineage &candidate) {
            return candidate.sequence ==
                   acknowledgement.presented_scene_sequence;
          });
      if (found == forward_lineage.end() ||
          found->kind != Render::RenderTransportMessageKind::
                             SCENE_SNAPSHOT_V7_CAMERA_V2 ||
          found->snapshot_id != acknowledgement.presented_snapshot_id) {
        return false;
      }
      last_presented_scene_sequence =
          acknowledgement.presented_scene_sequence;
      last_presented_snapshot_id = acknowledgement.presented_snapshot_id;
    }
    last_acknowledged_forward =
        acknowledgement.through_forward_sequence;
    // Receipt/application can lead presentation. Retain every acknowledged
    // scene newer than the latest presented scene so a later cumulative ACK
    // can still prove its exact immutable snapshot identity. Non-scene
    // lineage and scenes that can no longer be presented are retired.
    forward_lineage.erase(
        std::remove_if(
            forward_lineage.begin(), forward_lineage.end(),
            [this](const ForwardLineage &lineage) {
              return lineage.sequence <= last_acknowledged_forward &&
                     (lineage.kind != Render::RenderTransportMessageKind::
                                          SCENE_SNAPSHOT_V7_CAMERA_V2 ||
                      lineage.sequence <= last_presented_scene_sequence);
            }),
        forward_lineage.end());
    return true;
  }

  bool QueueReverseMessage(RendererOgre14GameHostReverseMessage message) {
    std::lock_guard<std::mutex> lock(mutex);
    if (reverse_queue.size() >= config.maximum_reverse_messages) {
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED,
                 observed_channel_status);
      return false;
    }
    reverse_queue.push_back(std::move(message));
    wake.notify_all();
    return true;
  }

  bool DispatchReverseFrame(
      const Render::RenderTransportStreamFrameResult &frame) {
    try {
      if (frame.kind == Render::RenderTransportMessageKind::
                            INPUT_EVENT_BATCH_V1) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (!peer_ready) {
            FailLocked(
                RendererOgre14GameHostSessionStatus::FAILED_REVERSE_LINEAGE,
                Render::RenderTransportStatus::RECONCILIATION_MISMATCH,
                RendererBridgeChannelStatus::READY);
            return false;
          }
        }
        const Render::InputEventTransportDecodeResult decoded =
            input_decoder->Accept(frame.bytes);
        if (!decoded) {
          std::lock_guard<std::mutex> lock(mutex);
          FailLocked(
              RendererOgre14GameHostSessionStatus::FAILED_REVERSE_DECODE,
              decoded.status, RendererBridgeChannelStatus::READY);
          return false;
        }
        RendererOgre14GameHostReverseMessage message;
        message.kind = frame.kind;
        message.reverse_sequence = frame.sequence;
        message.input = decoded.message;
        const Render::InputTransportBatch &batch = *decoded.message->batch();
        if (!batch.events.empty()) {
          message.issued_first_event_id = batch.events.front().event_id;
          message.issued_last_event_id = batch.events.back().event_id;
        }
        message.resolved_through_event_id =
            batch.reconciliation.through_event_id;
        return QueueReverseMessage(std::move(message));
      }
      if (frame.kind == Render::RenderTransportMessageKind::
                            RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 ||
          frame.kind == Render::RenderTransportMessageKind::
                            RENDER_BRIDGE_CONTROL_V1) {
        const Render::RenderBridgeControlTransportDecodeResult decoded =
            control_decoder->Accept(frame.bytes);
        if (!decoded) {
          std::lock_guard<std::mutex> lock(mutex);
          FailLocked(
              RendererOgre14GameHostSessionStatus::FAILED_REVERSE_DECODE,
              decoded.status, RendererBridgeChannelStatus::READY);
          return false;
        }
        RendererOgre14GameHostReverseMessage message;
        message.kind = frame.kind;
        message.reverse_sequence = frame.sequence;
        if (frame.kind == Render::RenderTransportMessageKind::
                              RENDER_BRIDGE_ACKNOWLEDGEMENT_V1) {
          std::lock_guard<std::mutex> lock(mutex);
          if (!peer_ready ||
              !ValidateAcknowledgementLocked(decoded.acknowledgement)) {
            FailLocked(
                RendererOgre14GameHostSessionStatus::FAILED_REVERSE_LINEAGE,
                Render::RenderTransportStatus::RECONCILIATION_MISMATCH,
                RendererBridgeChannelStatus::READY);
            return false;
          }
          message.acknowledgement = decoded.acknowledgement;
          if (reverse_queue.size() >= config.maximum_reverse_messages) {
            FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                       Render::RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED,
                       RendererBridgeChannelStatus::READY);
            return false;
          }
          reverse_queue.push_back(std::move(message));
          wake.notify_all();
          return true;
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (last_control_command_id ==
                  (std::numeric_limits<std::uint64_t>::max)() ||
              decoded.control.command_id != last_control_command_id + 1U ||
              (last_control_command_id == 0U &&
               decoded.control.kind !=
                   Render::RenderBridgeControlKind::PEER_READY) ||
              (last_control_command_id != 0U &&
               decoded.control.kind ==
                   Render::RenderBridgeControlKind::PEER_READY) ||
              (decoded.control.kind ==
                   Render::RenderBridgeControlKind::SURFACE_CHANGED &&
               (!peer_ready ||
                decoded.control.surface.surface_revision <=
                    surface_state.surface_revision))) {
            FailLocked(
                RendererOgre14GameHostSessionStatus::FAILED_REVERSE_LINEAGE,
                Render::RenderTransportStatus::OUT_OF_ORDER_SEQUENCE,
                RendererBridgeChannelStatus::READY);
            return false;
          }
          if (reverse_queue.size() >= config.maximum_reverse_messages) {
            FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                       Render::RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED,
                       RendererBridgeChannelStatus::READY);
            return false;
          }
          last_control_command_id = decoded.control.command_id;
          if (decoded.control.kind ==
                  Render::RenderBridgeControlKind::PEER_READY ||
              decoded.control.kind ==
                  Render::RenderBridgeControlKind::SURFACE_CHANGED) {
            surface_state = decoded.control.surface;
          }
          if (decoded.control.kind ==
              Render::RenderBridgeControlKind::PEER_READY) {
            peer_ready = true;
          }
          message.control = decoded.control;
          reverse_queue.push_back(std::move(message));
          wake.notify_all();
        }
        return true;
      }
    } catch (const std::bad_alloc &) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::ALLOCATION_FAILURE,
                 RendererBridgeChannelStatus::READY);
      return false;
    } catch (const std::length_error &) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::ALLOCATION_FAILURE,
                 RendererBridgeChannelStatus::READY);
      return false;
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::INVALID_ARGUMENT,
                 RendererBridgeChannelStatus::READY);
      return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    FailLocked(RendererOgre14GameHostSessionStatus::FAILED_REVERSE_DECODE,
               Render::RenderTransportStatus::UNKNOWN_MESSAGE_KIND,
               RendererBridgeChannelStatus::READY);
    return false;
  }

  bool PumpReverse(std::vector<std::uint8_t> &read_buffer,
                   std::vector<std::uint8_t> &pending_bytes,
                   std::size_t &pending_offset, bool &drained) {
    drained = false;
    bool progressed = false;
    for (std::size_t iteration = 0U; iteration < 16U; ++iteration) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (terminal || stop_immediately ||
            reverse_queue.size() >= config.maximum_reverse_messages) {
          return progressed;
        }
      }
      if (reverse_stream->frame_ready()) {
        const Render::RenderTransportStreamFrameResult frame =
            reverse_stream->TakeFrame();
        if (!frame || !DispatchReverseFrame(frame)) {
          return progressed;
        }
        progressed = true;
        continue;
      }
      if (pending_offset < pending_bytes.size()) {
        const Render::RenderTransportStreamResult accepted =
            reverse_stream->Accept(pending_bytes.data() + pending_offset,
                                   pending_bytes.size() - pending_offset);
        pending_offset += accepted.bytes_consumed;
        progressed = progressed || accepted.bytes_consumed != 0U;
        if (accepted.terminal) {
          std::lock_guard<std::mutex> lock(mutex);
          FailLocked(RendererOgre14GameHostSessionStatus::
                         FAILED_REVERSE_STREAM,
                     accepted.transport_status,
                     RendererBridgeChannelStatus::READY);
          return progressed;
        }
        if (pending_offset == pending_bytes.size()) {
          pending_bytes.clear();
          pending_offset = 0U;
        }
        continue;
      }

      const RendererBridgeChannelResult read =
          channel->TryReadSome(read_buffer.data(), read_buffer.size());
      {
        std::lock_guard<std::mutex> lock(mutex);
        observed_channel_status = read.status;
      }
      if (read.status == RendererBridgeChannelStatus::READY) {
        if (read.bytes_transferred == 0U) {
          drained = true;
          return progressed;
        }
        pending_bytes.assign(read_buffer.begin(),
                             read_buffer.begin() +
                                 static_cast<std::ptrdiff_t>(
                                     read.bytes_transferred));
        pending_offset = 0U;
        progressed = true;
        continue;
      }
      if (read.status == RendererBridgeChannelStatus::PEER_CLOSED) {
        const Render::RenderTransportStreamResult finished =
            reverse_stream->Finish();
        std::lock_guard<std::mutex> lock(mutex);
        inbound_closed = true;
        if (finished.terminal) {
          FailLocked(RendererOgre14GameHostSessionStatus::
                         FAILED_REVERSE_STREAM,
                     finished.transport_status, read.status);
        } else if (!outbound_closed) {
          FailLocked(RendererOgre14GameHostSessionStatus::PEER_CLOSED,
                     Render::RenderTransportStatus::OK, read.status);
        }
        drained = true;
        return true;
      }
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_CHANNEL,
                 Render::RenderTransportStatus::OK, read.status);
      return progressed;
    }
    return progressed;
  }

  void WorkerMain() noexcept {
    std::vector<std::uint8_t> read_buffer;
    std::vector<std::uint8_t> pending_bytes;
    std::size_t pending_offset = 0U;
    ForwardMessage active_forward;
    bool have_active_forward = false;
    try {
      read_buffer.resize(config.reverse_read_chunk_bytes);
      for (;;) {
        bool reverse_drained = false;
        bool progressed = PumpReverse(read_buffer, pending_bytes,
                                      pending_offset, reverse_drained);
        {
          std::unique_lock<std::mutex> lock(mutex);
          if (stop_immediately || terminal) {
            break;
          }
          if (inbound_closed && outbound_closed) {
            closed = true;
            break;
          }
          if (reverse_queue.size() >= config.maximum_reverse_messages) {
            wake.wait(lock, [this]() {
              return stop_immediately || terminal ||
                     reverse_queue.size() < config.maximum_reverse_messages;
            });
            continue;
          }
        }
        // A complete zero-wait reverse drain is the forward-send barrier. If
        // the bounded pump exhausted its work budget, loop back and continue
        // draining before writing either pipe direction again.
        if (!reverse_drained) {
          continue;
        }

        bool close_outbound = false;
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (stop_immediately || terminal) {
            break;
          }
          if (!have_active_forward && !forward_queue.empty()) {
            active_forward = std::move(forward_queue.front());
            forward_queue.pop_front();
            ++forward_messages_in_flight;
            have_active_forward = true;
            wake.notify_all();
          } else if (!have_active_forward && finish_forward_requested &&
                     !outbound_closed) {
            close_outbound = true;
          }
        }

        if (have_active_forward) {
          const RendererBridgeChannelResult written =
              channel->TryWriteSome(
                  active_forward.bytes.data() + active_forward.offset,
                  active_forward.bytes.size() - active_forward.offset);
          std::lock_guard<std::mutex> lock(mutex);
          observed_channel_status = written.status;
          if (written.status != RendererBridgeChannelStatus::READY) {
            FailLocked(written.status ==
                                   RendererBridgeChannelStatus::PEER_CLOSED
                               ? RendererOgre14GameHostSessionStatus::
                                     PEER_CLOSED
                               : RendererOgre14GameHostSessionStatus::
                                     FAILED_CHANNEL,
                       Render::RenderTransportStatus::OK, written.status);
          } else {
            const std::size_t remaining =
                active_forward.bytes.size() - active_forward.offset;
            if (written.bytes_transferred > remaining) {
              FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                         Render::RenderTransportStatus::FRAME_SIZE_MISMATCH,
                         written.status);
            } else {
              active_forward.offset += written.bytes_transferred;
              queued_forward_bytes =
                  queued_forward_bytes >= written.bytes_transferred
                      ? queued_forward_bytes - written.bytes_transferred
                      : 0U;
              if (active_forward.offset == active_forward.bytes.size()) {
                if (forward_messages_in_flight != 0U) {
                  --forward_messages_in_flight;
                }
                last_written_forward = active_forward.sequence;
                active_forward = ForwardMessage{};
                have_active_forward = false;
              }
            }
          }
          progressed = progressed || written.bytes_transferred != 0U;
          wake.notify_all();
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (terminal || stop_immediately) {
            break;
          }
        }
        if (close_outbound) {
          const RendererBridgeChannelResult close_result =
              channel->CloseOutbound();
          std::lock_guard<std::mutex> lock(mutex);
          observed_channel_status = close_result.status;
          if (close_result.status != RendererBridgeChannelStatus::READY &&
              close_result.status != RendererBridgeChannelStatus::CLOSED) {
            FailLocked(RendererOgre14GameHostSessionStatus::FAILED_CHANNEL,
                       Render::RenderTransportStatus::OK,
                       close_result.status);
          } else {
            outbound_closed = true;
          }
          progressed = true;
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (terminal || stop_immediately) {
            break;
          }
        }
        {
          std::unique_lock<std::mutex> lock(mutex);
          if (terminal || stop_immediately) {
            break;
          }
          if (!progressed) {
            wake.wait_for(
                lock, std::chrono::milliseconds(config.idle_wait_milliseconds),
                [this, &have_active_forward]() {
                  return terminal || stop_immediately ||
                         (reverse_queue.size() <
                              config.maximum_reverse_messages &&
                          !have_active_forward &&
                           (!forward_queue.empty() ||
                            (finish_forward_requested && !outbound_closed)));
                });
          }
        }
      }
    } catch (const std::bad_alloc &) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::ALLOCATION_FAILURE,
                 RendererBridgeChannelStatus::READY);
    } catch (const std::length_error &) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::ALLOCATION_FAILURE,
                 RendererBridgeChannelStatus::READY);
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex);
      FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                 Render::RenderTransportStatus::INVALID_ARGUMENT,
                 RendererBridgeChannelStatus::READY);
    }

    if (have_active_forward) {
      std::lock_guard<std::mutex> lock(mutex);
      const std::size_t remaining =
          active_forward.bytes.size() - active_forward.offset;
      queued_forward_bytes = queued_forward_bytes >= remaining
                                 ? queued_forward_bytes - remaining
                                 : 0U;
      if (forward_messages_in_flight != 0U) {
        --forward_messages_in_flight;
      }
      wake.notify_all();
    }

    bool close_channel = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      close_channel = terminal || stop_immediately;
    }
    if (close_channel && channel != nullptr) {
      const RendererBridgeChannelResult channel_close = channel->Close();
      std::lock_guard<std::mutex> lock(mutex);
      observed_channel_status = channel_close.status;
    } else if (channel != nullptr && !inbound_closed) {
      const RendererBridgeChannelResult channel_close = channel->CloseInbound();
      std::lock_guard<std::mutex> lock(mutex);
      observed_channel_status = channel_close.status;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      worker_exited = true;
      if (!terminal) {
        closed = true;
      }
      wake.notify_all();
    }
  }

  template <typename Predicate>
  RendererOgre14GameHostPollResult PollMatching(Predicate predicate) {
    std::lock_guard<std::mutex> lock(mutex);
    RendererOgre14GameHostPollResult poll;
    const auto found =
        std::find_if(reverse_queue.begin(), reverse_queue.end(), predicate);
    if (found == reverse_queue.end()) {
      poll.result = terminal
                        ? TerminalResultLocked()
                        : ResultLocked(
                              RendererOgre14GameHostSessionStatus::EMPTY,
                              false);
      return poll;
    }
    poll.message = std::move(*found);
    reverse_queue.erase(found);
    poll.result = ResultLocked(
        RendererOgre14GameHostSessionStatus::REVERSE_MESSAGE_READY, true);
    poll.result.kind = poll.message.kind;
    poll.result.reverse_sequence = poll.message.reverse_sequence;
    wake.notify_all();
    return poll;
  }

  RendererOgre14GameBridge *bridge = nullptr;
  RendererBridgeChannel *channel = nullptr;
  RendererOgre14GameHostSessionConfig config;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;
  std::unique_ptr<Render::RenderAssetRegistry> asset_registry;
  Render::RenderTransportSequenceState reverse_sequence;
  std::unique_ptr<Render::InputEventTransportDecoder> input_decoder;
  std::unique_ptr<Render::RenderBridgeControlTransportDecoder>
      control_decoder;
  std::unique_ptr<Render::RenderTransportStreamDecoder> reverse_stream;
  std::deque<ForwardMessage> forward_queue;
  std::deque<ForwardLineage> forward_lineage;
  std::deque<RendererOgre14GameHostReverseMessage> reverse_queue;
  std::size_t queued_forward_bytes = 0U;
  std::size_t forward_messages_in_flight = 0U;
  std::uint64_t registry_id = 0U;
  std::uint64_t next_forward_sequence = 1U;
  std::uint64_t last_written_forward = 0U;
  std::uint64_t last_acknowledged_forward = 0U;
  std::uint64_t last_presented_scene_sequence = 0U;
  std::uint64_t last_presented_snapshot_id = 0U;
  std::uint64_t last_control_command_id = 0U;
  std::uint64_t last_snapshot_id = 0U;
  std::uint64_t last_simulation_tick = 0U;
  std::uint64_t scene_generation = 1U;
  Render::RenderBridgeSurfaceState surface_state;
  bool has_posted_scene = false;
  bool last_scene_was_empty = false;
  bool simulation_lineage_initialized = false;
  bool peer_ready = false;
  bool started = false;
  bool finish_forward_requested = false;
  bool outbound_closed = false;
  bool inbound_closed = false;
  bool stop_immediately = false;
  bool worker_exited = false;
  bool closed = false;
  bool terminal = false;
  RendererOgre14GameHostSessionStatus terminal_cause =
      RendererOgre14GameHostSessionStatus::FAILED_INTERNAL;
  Render::RenderTransportStatus transport_failure =
      Render::RenderTransportStatus::OK;
  RendererBridgeChannelStatus channel_failure_status =
      RendererBridgeChannelStatus::UNINITIALIZED;
  RendererBridgeChannelStatus observed_channel_status =
      RendererBridgeChannelStatus::UNINITIALIZED;
};

RendererOgre14GameHostSession::RendererOgre14GameHostSession(
    RendererOgre14GameBridge &bridge)
    : impl_(new Impl(bridge)) {}

RendererOgre14GameHostSession::~RendererOgre14GameHostSession() {
  (void)Close();
}

RendererOgre14GameHostSessionResult RendererOgre14GameHostSession::Start(
    const RendererOgre14GameHostSessionConfig &config) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->started) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY, false);
  }
  if (!IsValidConfig(config)) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_INVALID_CONFIGURATION,
        false);
  }
  if (impl_->bridge == nullptr || !impl_->bridge->active() ||
      impl_->bridge->endpoint() == nullptr ||
      impl_->bridge->endpoint()->role != RendererBridgeRole::GAME_HOST ||
      impl_->bridge->channel() == nullptr ||
      impl_->bridge->channel()->status() != RendererBridgeChannelStatus::READY) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_INACTIVE_BRIDGE, false);
  }
  const Render::RenderBridgeSessionIdentity session_id =
      impl_->bridge->endpoint()->session_id;
  const std::uint64_t registry_id =
      Render::DeriveRenderAssetRegistryIdFromBridgeSession(session_id);
  if (registry_id == 0U) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID, false);
  }
  try {
    impl_->config = config;
    impl_->registry_id = registry_id;
    impl_->channel = impl_->bridge->channel();
    impl_->observed_channel_status = RendererBridgeChannelStatus::READY;
    const RendererBridgeChannelResult nonblocking_outbound =
        impl_->channel->EnableNonblockingOutbound();
    impl_->observed_channel_status = nonblocking_outbound.status;
    if (nonblocking_outbound.status != RendererBridgeChannelStatus::READY) {
      impl_->FailLocked(RendererOgre14GameHostSessionStatus::FAILED_CHANNEL,
                        Render::RenderTransportStatus::OK,
                        nonblocking_outbound.status);
      (void)impl_->channel->Close();
      return impl_->TerminalResultLocked();
    }
    impl_->asset_registry.reset(new Render::RenderAssetRegistry(registry_id));
    impl_->input_decoder.reset(
        new Render::InputEventTransportDecoder(impl_->reverse_sequence));
    impl_->control_decoder.reset(
        new Render::RenderBridgeControlTransportDecoder(
            registry_id, impl_->reverse_sequence));
    impl_->reverse_stream.reset(new Render::RenderTransportStreamDecoder(
        Render::kRenderTransportStreamInputMaximumPayloadBytes));
    impl_->started = true;
    impl_->worker = std::thread(&Impl::WorkerMain, impl_.get());
  } catch (...) {
    impl_->started = false;
    impl_->FailLocked(RendererOgre14GameHostSessionStatus::FAILED_INTERNAL,
                      Render::RenderTransportStatus::ALLOCATION_FAILURE,
                      RendererBridgeChannelStatus::READY);
    if (impl_->channel != nullptr) {
      const RendererBridgeChannelResult close_result = impl_->channel->Close();
      impl_->observed_channel_status = close_result.status;
    }
    return impl_->TerminalResultLocked();
  }
  return impl_->ResultLocked(RendererOgre14GameHostSessionStatus::READY, true);
}

RendererOgre14GameHostSessionResult RendererOgre14GameHostSession::Submit(
    const Render::RenderAssetDelta &delta) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->terminal) {
    return impl_->TerminalResultLocked();
  }
  if (!impl_->started || impl_->closed || impl_->finish_forward_requested ||
      impl_->asset_registry == nullptr) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY, false);
  }
  if (delta.registry_id != impl_->registry_id) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID, false);
    result.validation_code = Render::ValidationCode::INVALID_IDENTIFIER;
    return result;
  }
  if (impl_->next_forward_sequence ==
      (std::numeric_limits<std::uint64_t>::max)()) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_ASSET_LINEAGE, false);
    result.validation_code = Render::ValidationCode::VALUE_OUT_OF_RANGE;
    return result;
  }
  const std::uint64_t sequence = impl_->next_forward_sequence;
  Render::RenderAssetDeltaTransportEncodeResult encoded =
      Render::EncodeRenderAssetDeltaTransportFrame(sequence, delta);
  if (!encoded) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_FORWARD_ENCODING, false);
    result.kind = Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
    result.forward_sequence = sequence;
    result.transport_status = encoded.status;
    return result;
  }
  const std::size_t frame_bytes = encoded.bytes.size();
  if (!impl_->HasForwardCapacityLocked(frame_bytes)) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::BACKPRESSURE, false);
  }
  try {
    impl_->forward_lineage.push_back(
        {Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2, sequence,
         0U});
    Impl::ForwardMessage queued;
    queued.kind = Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
    queued.sequence = sequence;
    queued.bytes = std::move(encoded.bytes);
    impl_->forward_queue.push_back(std::move(queued));
    const Render::ValidationResult applied = impl_->asset_registry->Apply(delta);
    if (!applied) {
      impl_->forward_queue.pop_back();
      impl_->forward_lineage.pop_back();
      RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
          RendererOgre14GameHostSessionStatus::REJECTED_ASSET_LINEAGE, false);
      result.validation_code = applied.code;
      return result;
    }
  } catch (...) {
    if (!impl_->forward_queue.empty() &&
        impl_->forward_queue.back().sequence == sequence) {
      impl_->forward_queue.pop_back();
    }
    if (!impl_->forward_lineage.empty() &&
        impl_->forward_lineage.back().sequence == sequence) {
      impl_->forward_lineage.pop_back();
    }
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_INTERNAL, false);
  }
  impl_->queued_forward_bytes += frame_bytes;
  ++impl_->next_forward_sequence;
  impl_->wake.notify_all();
  RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
      RendererOgre14GameHostSessionStatus::ASSET_DELTA_QUEUED, true);
  result.kind = Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2;
  result.forward_sequence = sequence;
  return result;
}

RendererOgre14GameHostSessionResult RendererOgre14GameHostSession::PostPhysics(
    const Render::SceneSnapshot &snapshot,
    const Render::CameraViewRequest &camera) {
  return PostPhysicsImpl(snapshot, camera, 0U, false);
}

RendererOgre14GameHostSessionResult
RendererOgre14GameHostSession::PostPhysicsCapturedAtSurface(
    const Render::SceneSnapshot &snapshot,
    const Render::CameraViewRequest &camera,
    std::uint64_t captured_surface_revision) {
  return PostPhysicsImpl(snapshot, camera, captured_surface_revision, true);
}

RendererOgre14GameHostSessionResult
RendererOgre14GameHostSession::PostPhysicsImpl(
    const Render::SceneSnapshot &snapshot,
    const Render::CameraViewRequest &camera,
    std::uint64_t captured_surface_revision,
    bool captured_revision_provided) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->terminal) {
    return impl_->TerminalResultLocked();
  }
  if (!impl_->started || impl_->closed || impl_->finish_forward_requested ||
      impl_->asset_registry == nullptr) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY, false);
  }
  const bool captured_current_surface =
      !captured_revision_provided ||
      captured_surface_revision == impl_->surface_state.surface_revision;
  const bool invalid_captured_revision =
      captured_revision_provided &&
      (captured_surface_revision == 0U ||
       captured_surface_revision > impl_->surface_state.surface_revision);
  const bool invalid_current_surface =
      captured_current_surface &&
      (impl_->surface_state.suspended ||
       camera.width != impl_->surface_state.drawable_width ||
       camera.height != impl_->surface_state.drawable_height);
  if (!impl_->peer_ready || invalid_captured_revision ||
      invalid_current_surface) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE, false);
    result.validation_code = impl_->peer_ready
                                 ? Render::ValidationCode::VALUE_OUT_OF_RANGE
                                 : Render::ValidationCode::MISSING_REFERENCE;
    return result;
  }
  if (snapshot.asset_registry_id() != impl_->registry_id) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID, false);
    result.validation_code = Render::ValidationCode::INVALID_IDENTIFIER;
    return result;
  }
  const Render::ValidationResult scene_assets =
      Render::ValidateSceneSnapshotAssets(snapshot, *impl_->asset_registry);
  if (!scene_assets) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_SCENE_LINEAGE, false);
    result.validation_code = scene_assets.code;
    return result;
  }
  if (impl_->asset_registry->sequence() == 0U ||
      (impl_->has_posted_scene &&
       snapshot.snapshot_id() <= impl_->last_snapshot_id) ||
      (impl_->simulation_lineage_initialized &&
       snapshot.simulation_tick() < impl_->last_simulation_tick) ||
      impl_->next_forward_sequence ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_SCENE_LINEAGE, false);
    result.validation_code =
        impl_->next_forward_sequence ==
                (std::numeric_limits<std::uint64_t>::max)()
            ? Render::ValidationCode::VALUE_OUT_OF_RANGE
            : Render::ValidationCode::SEQUENCE_MISMATCH;
    return result;
  }
  const std::uint64_t sequence = impl_->next_forward_sequence;
  Render::SceneSnapshotTransportEncodeResult encoded =
      Render::EncodeSceneSnapshotTransportFrame(sequence, snapshot, camera);
  if (!encoded) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_FORWARD_ENCODING, false);
    result.kind =
        Render::RenderTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2;
    result.forward_sequence = sequence;
    result.transport_status = encoded.status;
    return result;
  }
  const std::size_t frame_bytes = encoded.bytes.size();
  if (!impl_->HasForwardCapacityLocked(frame_bytes)) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::BACKPRESSURE, false);
  }
  try {
    impl_->forward_lineage.push_back(
        {Render::RenderTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2,
         sequence, snapshot.snapshot_id()});
    Impl::ForwardMessage queued;
    queued.kind =
        Render::RenderTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2;
    queued.sequence = sequence;
    queued.snapshot_id = snapshot.snapshot_id();
    queued.bytes = std::move(encoded.bytes);
    impl_->forward_queue.push_back(std::move(queued));
  } catch (...) {
    if (!impl_->forward_queue.empty() &&
        impl_->forward_queue.back().sequence == sequence) {
      impl_->forward_queue.pop_back();
    }
    if (!impl_->forward_lineage.empty() &&
        impl_->forward_lineage.back().sequence == sequence) {
      impl_->forward_lineage.pop_back();
    }
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_INTERNAL, false);
  }
  impl_->queued_forward_bytes += frame_bytes;
  impl_->last_snapshot_id = snapshot.snapshot_id();
  impl_->last_simulation_tick = snapshot.simulation_tick();
  impl_->has_posted_scene = true;
  impl_->last_scene_was_empty = snapshot.mesh_instances().empty() &&
                                snapshot.lights().empty() &&
                                snapshot.reflection_probes().empty() &&
                                snapshot.dynamic_mesh_updates().empty() &&
                                snapshot.particle_events().empty();
  impl_->simulation_lineage_initialized = true;
  ++impl_->next_forward_sequence;
  impl_->wake.notify_all();
  RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
      RendererOgre14GameHostSessionStatus::SCENE_SNAPSHOT_QUEUED, true);
  result.kind =
      Render::RenderTransportMessageKind::SCENE_SNAPSHOT_V7_CAMERA_V2;
  result.forward_sequence = sequence;
  return result;
}

RendererOgre14GameHostSessionResult
RendererOgre14GameHostSession::CompleteSceneGeneration(
    std::uint64_t finalized_snapshot_id) noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->terminal) {
    return impl_->TerminalResultLocked();
  }
  if (!impl_->started || impl_->closed || impl_->finish_forward_requested ||
      !impl_->has_posted_scene || finalized_snapshot_id == 0U ||
      finalized_snapshot_id != impl_->last_snapshot_id ||
      !impl_->last_scene_was_empty || impl_->asset_registry == nullptr ||
      impl_->asset_registry->live_count() != 0U ||
      impl_->next_forward_sequence ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      impl_->scene_generation >=
          (std::numeric_limits<std::uint64_t>::max)() - 1U) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_SCENE_LINEAGE, false);
    result.validation_code = Render::ValidationCode::SEQUENCE_MISMATCH;
    return result;
  }

  Render::SceneGenerationBoundary boundary;
  boundary.registry_id = impl_->registry_id;
  boundary.completed_generation = impl_->scene_generation;
  boundary.next_generation = impl_->scene_generation + 1U;
  boundary.asset_sequence = impl_->asset_registry->sequence();
  boundary.finalized_snapshot_id = finalized_snapshot_id;
  const std::uint64_t sequence = impl_->next_forward_sequence;
  Render::RenderTransportEnvelopeEncodeResult encoded =
      Render::EncodeSceneGenerationBoundaryFrame(sequence, boundary);
  if (!encoded) {
    RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_FORWARD_ENCODING, false);
    result.kind =
        Render::RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1;
    result.forward_sequence = sequence;
    result.transport_status = encoded.status;
    return result;
  }
  const std::size_t frame_bytes = encoded.bytes.size();
  if (!impl_->HasForwardCapacityLocked(frame_bytes)) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::BACKPRESSURE, false);
  }
  try {
    impl_->forward_lineage.push_back(
        {Render::RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1,
         sequence, finalized_snapshot_id});
    Impl::ForwardMessage queued;
    queued.kind =
        Render::RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1;
    queued.sequence = sequence;
    queued.snapshot_id = finalized_snapshot_id;
    queued.bytes = std::move(encoded.bytes);
    impl_->forward_queue.push_back(std::move(queued));
  } catch (...) {
    if (!impl_->forward_queue.empty() &&
        impl_->forward_queue.back().sequence == sequence) {
      impl_->forward_queue.pop_back();
    }
    if (!impl_->forward_lineage.empty() &&
        impl_->forward_lineage.back().sequence == sequence) {
      impl_->forward_lineage.pop_back();
    }
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::FAILED_INTERNAL, false);
  }
  impl_->queued_forward_bytes += frame_bytes;
  ++impl_->next_forward_sequence;
  ++impl_->scene_generation;
  impl_->simulation_lineage_initialized = false;
  impl_->last_simulation_tick = 0U;
  impl_->wake.notify_all();
  RendererOgre14GameHostSessionResult result = impl_->ResultLocked(
      RendererOgre14GameHostSessionStatus::
          SCENE_GENERATION_BOUNDARY_QUEUED,
      true);
  result.kind =
      Render::RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1;
  result.forward_sequence = sequence;
  return result;
}

RendererOgre14GameHostPollResult
RendererOgre14GameHostSession::PollReverse() {
  return impl_->PollMatching([](const auto &) { return true; });
}

RendererOgre14GameHostPollResult RendererOgre14GameHostSession::PollInput() {
  return impl_->PollMatching([](const auto &message) {
    return message.kind ==
           Render::RenderTransportMessageKind::INPUT_EVENT_BATCH_V1;
  });
}

RendererOgre14GameHostPollResult
RendererOgre14GameHostSession::PollAcknowledgement() {
  return impl_->PollMatching([](const auto &message) {
    return message.kind == Render::RenderTransportMessageKind::
                               RENDER_BRIDGE_ACKNOWLEDGEMENT_V1;
  });
}

RendererOgre14GameHostPollResult RendererOgre14GameHostSession::PollControl() {
  return impl_->PollMatching([](const auto &message) {
    return message.kind ==
           Render::RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1;
  });
}

RendererOgre14GameHostSessionResult
RendererOgre14GameHostSession::FinishForward() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->terminal) {
    return impl_->TerminalResultLocked();
  }
  if (!impl_->started || impl_->closed || impl_->finish_forward_requested) {
    return impl_->ResultLocked(
        RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY, false);
  }
  impl_->finish_forward_requested = true;
  impl_->wake.notify_all();
  return impl_->ResultLocked(
      RendererOgre14GameHostSessionStatus::OUTBOUND_HALF_CLOSE_REQUESTED,
      true);
}

RendererOgre14GameHostSessionResult RendererOgre14GameHostSession::Close() {
  if (impl_ == nullptr) {
    return MakeResult(RendererOgre14GameHostSessionStatus::CLOSED, true);
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->started) {
      impl_->closed = true;
      return impl_->terminal
                 ? impl_->TerminalResultLocked()
                 : impl_->ResultLocked(
                       RendererOgre14GameHostSessionStatus::CLOSED, true);
    }
    if (!impl_->worker_exited) {
      impl_->stop_immediately = true;
      impl_->wake.notify_all();
    }
    std::size_t cleared_bytes = 0U;
    for (const Impl::ForwardMessage &message : impl_->forward_queue) {
      cleared_bytes += message.bytes.size() - message.offset;
    }
    impl_->forward_queue.clear();
    impl_->queued_forward_bytes =
        impl_->queued_forward_bytes >= cleared_bytes
            ? impl_->queued_forward_bytes - cleared_bytes
            : 0U;
  }
  if (impl_->worker.joinable() &&
      impl_->worker.get_id() != std::this_thread::get_id()) {
    impl_->worker.join();
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->closed = true;
  return impl_->terminal
             ? impl_->TerminalResultLocked()
             : impl_->ResultLocked(RendererOgre14GameHostSessionStatus::CLOSED,
                                   true);
}

std::uint64_t RendererOgre14GameHostSession::registry_id() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->registry_id;
}

std::uint64_t
RendererOgre14GameHostSession::next_forward_sequence() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->next_forward_sequence;
}

std::uint64_t
RendererOgre14GameHostSession::last_written_forward_sequence() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_written_forward;
}

std::uint64_t RendererOgre14GameHostSession::
    last_acknowledged_forward_sequence() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->last_acknowledged_forward;
}

std::size_t
RendererOgre14GameHostSession::queued_forward_bytes() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->queued_forward_bytes;
}

std::size_t
RendererOgre14GameHostSession::queued_reverse_messages() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->reverse_queue.size();
}

Render::RenderBridgeSurfaceState
RendererOgre14GameHostSession::current_surface_state() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->surface_state;
}

bool RendererOgre14GameHostSession::peer_ready() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->peer_ready;
}

bool RendererOgre14GameHostSession::shutdown_complete() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->worker_exited;
}

bool RendererOgre14GameHostSession::terminal() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->terminal;
}

RendererOgre14GameHostSessionStatus
RendererOgre14GameHostSession::terminal_cause() const noexcept {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->terminal_cause;
}

bool IsKnownRendererOgre14GameHostSessionStatus(
    RendererOgre14GameHostSessionStatus status) noexcept {
  switch (status) {
  case RendererOgre14GameHostSessionStatus::READY:
  case RendererOgre14GameHostSessionStatus::ASSET_DELTA_QUEUED:
  case RendererOgre14GameHostSessionStatus::SCENE_SNAPSHOT_QUEUED:
  case RendererOgre14GameHostSessionStatus::
      SCENE_GENERATION_BOUNDARY_QUEUED:
  case RendererOgre14GameHostSessionStatus::REVERSE_MESSAGE_READY:
  case RendererOgre14GameHostSessionStatus::OUTBOUND_HALF_CLOSE_REQUESTED:
  case RendererOgre14GameHostSessionStatus::CLOSED:
  case RendererOgre14GameHostSessionStatus::EMPTY:
  case RendererOgre14GameHostSessionStatus::BACKPRESSURE:
  case RendererOgre14GameHostSessionStatus::REJECTED_INVALID_CONFIGURATION:
  case RendererOgre14GameHostSessionStatus::REJECTED_INACTIVE_BRIDGE:
  case RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY:
  case RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID:
  case RendererOgre14GameHostSessionStatus::REJECTED_ASSET_LINEAGE:
  case RendererOgre14GameHostSessionStatus::REJECTED_SCENE_LINEAGE:
  case RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE:
  case RendererOgre14GameHostSessionStatus::FAILED_FORWARD_ENCODING:
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_STREAM:
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_DECODE:
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_LINEAGE:
  case RendererOgre14GameHostSessionStatus::PEER_CLOSED:
  case RendererOgre14GameHostSessionStatus::FAILED_CHANNEL:
  case RendererOgre14GameHostSessionStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererOgre14GameHostSessionStatus status) noexcept {
  switch (status) {
  case RendererOgre14GameHostSessionStatus::READY:
    return "ready";
  case RendererOgre14GameHostSessionStatus::ASSET_DELTA_QUEUED:
    return "asset-delta-queued";
  case RendererOgre14GameHostSessionStatus::SCENE_SNAPSHOT_QUEUED:
    return "scene-snapshot-queued";
  case RendererOgre14GameHostSessionStatus::
      SCENE_GENERATION_BOUNDARY_QUEUED:
    return "scene-generation-boundary-queued";
  case RendererOgre14GameHostSessionStatus::REVERSE_MESSAGE_READY:
    return "reverse-message-ready";
  case RendererOgre14GameHostSessionStatus::OUTBOUND_HALF_CLOSE_REQUESTED:
    return "outbound-half-close-requested";
  case RendererOgre14GameHostSessionStatus::CLOSED:
    return "closed";
  case RendererOgre14GameHostSessionStatus::EMPTY:
    return "empty";
  case RendererOgre14GameHostSessionStatus::BACKPRESSURE:
    return "backpressure";
  case RendererOgre14GameHostSessionStatus::REJECTED_INVALID_CONFIGURATION:
    return "rejected-invalid-configuration";
  case RendererOgre14GameHostSessionStatus::REJECTED_INACTIVE_BRIDGE:
    return "rejected-inactive-bridge";
  case RendererOgre14GameHostSessionStatus::REJECTED_NOT_READY:
    return "rejected-not-ready";
  case RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID:
    return "rejected-registry-id";
  case RendererOgre14GameHostSessionStatus::REJECTED_ASSET_LINEAGE:
    return "rejected-asset-lineage";
  case RendererOgre14GameHostSessionStatus::REJECTED_SCENE_LINEAGE:
    return "rejected-scene-lineage";
  case RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE:
    return "rejected-surface-state";
  case RendererOgre14GameHostSessionStatus::FAILED_FORWARD_ENCODING:
    return "failed-forward-encoding";
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_STREAM:
    return "failed-reverse-stream";
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_DECODE:
    return "failed-reverse-decode";
  case RendererOgre14GameHostSessionStatus::FAILED_REVERSE_LINEAGE:
    return "failed-reverse-lineage";
  case RendererOgre14GameHostSessionStatus::PEER_CLOSED:
    return "peer-closed";
  case RendererOgre14GameHostSessionStatus::FAILED_CHANNEL:
    return "failed-channel";
  case RendererOgre14GameHostSessionStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
