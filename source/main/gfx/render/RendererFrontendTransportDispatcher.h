/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed game-to-presentation frontend transport dispatcher.

#pragma once

#include "RenderAssetDeltaTransport.h"
#include "RenderBridgeSessionIdentity.h"
#include "RenderTransportStream.h"
#include "RendererFrontendDirectDispatcher.h"
#include "RendererFrontendPresentationPolicy.h"
#include "SceneGenerationBoundaryTransport.h"
#include "SceneSnapshotTransport.h"

#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kRendererFrontendTransportDispatcherContractVersion =
    4U;

enum class RendererFrontendTransportDispatchStatus : std::uint8_t {
  ASSET_DELTA_SYNCHRONIZED = 0U,
  SCENE_FRAME_COMPLETED,
  SCENE_FRAME_RETIRED,
  SCENE_GENERATION_BOUNDARY_CONSUMED,
  REJECTED_TERMINAL,
  REJECTED_INVALID_SESSION,
  REJECTED_INVALID_FRAME,
  REJECTED_REVERSE_DIRECTION,
  REJECTED_INVALID_PRESENTATION_POLICY,
  FAILED_DECODE,
  FAILED_LINEAGE,
  FAILED_FRONTEND_CAPABILITIES,
  FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
  FAILED_FRONTEND_RENDER,
  FAILED_FRONTEND_WAIT,
  FAILED_FRONTEND_OUTPUT,
  FAILED_RESOURCE_RELEASE,
  FAILED_FRONTEND_SCENE_GENERATION_RESET,
  FAILED_INTERNAL,
};

struct RendererFrontendTransportDispatchResult final {
  std::uint32_t version = kRendererFrontendTransportDispatcherContractVersion;
  RendererFrontendTransportDispatchStatus status =
      RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
  RendererFrontendTransportDispatchStatus terminal_cause =
      RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
  RenderTransportMessageKind kind =
      RenderTransportMessageKind::SCENE_SNAPSHOT_V5_CAMERA_V2;
  std::uint64_t sequence = 0U;
  /// Exact decoded immutable snapshot identity for a successfully completed
  /// scene. Asset results and every rejected/failed result leave this zero.
  std::uint64_t scene_snapshot_id = 0U;
  RenderTransportStatus transport_status =
      RenderTransportStatus::INVALID_ARGUMENT;
  ValidationCode validation_code = ValidationCode::OK;
  RenderOperationCode frontend_code = RenderOperationCode::OK;
  std::uint32_t resources_released = 0U;
  bool terminal = false;

  [[nodiscard]] bool ok() const noexcept {
    return status == RendererFrontendTransportDispatchStatus::
                         ASSET_DELTA_SYNCHRONIZED ||
           status ==
               RendererFrontendTransportDispatchStatus::SCENE_FRAME_COMPLETED ||
           status ==
               RendererFrontendTransportDispatchStatus::SCENE_FRAME_RETIRED ||
           status == RendererFrontendTransportDispatchStatus::
                         SCENE_GENERATION_BOUNDARY_CONSUMED;
  }
  explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] bool IsKnownRendererFrontendTransportDispatchStatus(
    RendererFrontendTransportDispatchStatus status) noexcept;
[[nodiscard]] const char *
ToString(RendererFrontendTransportDispatchStatus status) noexcept;

/// Serial, renderer-neutral consumer for the game-host -> presentation stream.
/// Input must be one complete envelope already emitted by
/// RenderTransportStreamDecoder::TakeFrame(). Both typed decoders share one
/// sequence state, so asset and scene messages form one exact lineage.
///
/// Asset transactions are applied and synchronized before a scene may resolve
/// them. Mixed transport sequence remains the acknowledgement lineage; scenes
/// that actually reach Render receive a separate contiguous frontend frame ID.
/// Every successful Render is drained with an infinite WaitForFrame before each unique
/// transferred attachment handle is released exactly once. Any malformed
/// payload, wrong direction, lineage mismatch, frontend failure, invalid
/// output, or release failure permanently poisons this dispatcher. The
/// referenced frontend must already be initialized, must outlive this object,
/// and is never shut down by it. One caller serializes all calls on the
/// frontend owner thread.
class RendererFrontendTransportDispatcher final {
public:
  RendererFrontendTransportDispatcher(
      IRendererFrontend &frontend,
      const RenderBridgeSessionIdentity &session_id) noexcept;

  RendererFrontendTransportDispatcher(
      const RendererFrontendTransportDispatcher &) = delete;
  RendererFrontendTransportDispatcher &
  operator=(const RendererFrontendTransportDispatcher &) = delete;
  RendererFrontendTransportDispatcher(RendererFrontendTransportDispatcher &&) =
      delete;
  RendererFrontendTransportDispatcher &
  operator=(RendererFrontendTransportDispatcher &&) = delete;
  ~RendererFrontendTransportDispatcher() = default;

  [[nodiscard]] RendererFrontendTransportDispatchResult Dispatch(
      const RenderTransportStreamFrameResult &frame,
      const RendererFrontendPresentationPolicy &presentation_policy) noexcept;

  [[nodiscard]] std::uint64_t registry_id() const noexcept {
    return registry_id_;
  }
  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return sequence_state_.next_expected_sequence();
  }
  [[nodiscard]] std::uint64_t last_accepted_sequence() const noexcept {
    return sequence_state_.last_accepted_sequence();
  }
  [[nodiscard]] const RenderAssetRegistry &asset_registry() const noexcept {
    return asset_decoder_.registry();
  }
  [[nodiscard]] bool terminal() const noexcept { return terminal_; }
  [[nodiscard]] RendererFrontendTransportDispatchStatus
  terminal_cause() const noexcept {
    return terminal_cause_;
  }

private:
  [[nodiscard]] RendererFrontendTransportDispatchResult
  DispatchAsset(const RenderTransportStreamFrameResult &frame);
  [[nodiscard]] RendererFrontendTransportDispatchResult
  DispatchScene(const RenderTransportStreamFrameResult &frame,
                const RendererFrontendPresentationPolicy &presentation_policy);
  [[nodiscard]] RendererFrontendTransportDispatchResult
  DispatchSceneGenerationBoundary(
      const RenderTransportStreamFrameResult &frame);
  [[nodiscard]] RendererFrontendTransportDispatchResult FailFromDirect(
      const RendererFrontendDirectDispatchResult &direct,
      const RenderTransportStreamFrameResult &frame) noexcept;
  [[nodiscard]] RendererFrontendTransportDispatchResult
  Fail(RendererFrontendTransportDispatchStatus status,
       const RenderTransportStreamFrameResult *frame,
       RenderTransportStatus transport_status, ValidationCode validation_code,
       RenderOperationCode frontend_code,
       std::uint32_t resources_released = 0U) noexcept;
  [[nodiscard]] RendererFrontendTransportDispatchResult
  Success(RendererFrontendTransportDispatchStatus status,
          const RenderTransportStreamFrameResult &frame,
          std::uint32_t resources_released = 0U) const noexcept;

  std::uint64_t registry_id_ = 0U;
  RenderTransportSequenceState sequence_state_;
  RenderAssetDeltaTransportDecoder asset_decoder_;
  SceneSnapshotTransportDecoder scene_decoder_;
  SceneGenerationBoundaryTransportDecoder scene_generation_decoder_;
  RendererFrontendDirectDispatcher direct_dispatcher_;
  RendererFrontendTransportDispatchStatus terminal_cause_ =
      RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
  bool terminal_ = false;
  std::uint64_t scene_generation_ = 1U;
  std::uint64_t last_scene_snapshot_id_ = 0U;
  std::uint64_t last_scene_asset_sequence_ = 0U;
  bool last_scene_was_empty_ = false;
};

} // namespace RoR::Render
