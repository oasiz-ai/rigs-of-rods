/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Typed in-process renderer frontend dispatch without transport,
/// serialization, channels, or a child process.

#pragma once

#include "RendererFrontend.h"
#include "RendererFrontendPresentationPolicy.h"

#include <cstdint>
#include <memory>

namespace RoR::Render {

constexpr std::uint32_t kRendererFrontendDirectDispatcherContractVersion = 2U;

enum class RendererFrontendDirectDispatchStatus : std::uint8_t {
  ASSET_DELTA_SYNCHRONIZED = 0U,
  SCENE_FRAME_COMPLETED,
  SCENE_FRAME_RETIRED,
  /// Render() committed a newer presentation surface but consumed neither the
  /// scene snapshot nor frontend frame identity. The caller must observe that
  /// exact surface, retire this stale immutable scene, and submit a fresh one.
  SCENE_FRAME_PRESENTATION_SURFACE_STALE,
  SCENE_GENERATION_RESET,
  REJECTED_TERMINAL,
  REJECTED_INVALID_REGISTRY,
  REJECTED_INVALID_PRESENTATION_POLICY,
  FAILED_ASSET_VALIDATION,
  FAILED_FRONTEND_ASSET_SYNCHRONIZATION,
  FAILED_SCENE_VALIDATION,
  FAILED_FRONTEND_CAPABILITIES,
  FAILED_FRONTEND_RENDER,
  FAILED_FRONTEND_FRAME_RETIREMENT,
  FAILED_FRONTEND_WAIT,
  FAILED_FRONTEND_OUTPUT,
  FAILED_RESOURCE_RELEASE,
  FAILED_FRONTEND_SCENE_GENERATION_RESET,
  FAILED_ALLOCATION,
  FAILED_INTERNAL,
};

struct RendererFrontendDirectDispatchResult final {
  std::uint32_t version =
      kRendererFrontendDirectDispatcherContractVersion;
  RendererFrontendDirectDispatchStatus status =
      RendererFrontendDirectDispatchStatus::FAILED_INTERNAL;
  RendererFrontendDirectDispatchStatus terminal_cause =
      RendererFrontendDirectDispatchStatus::FAILED_INTERNAL;
  ValidationCode validation_code = ValidationCode::OK;
  RenderOperationCode frontend_code = RenderOperationCode::OK;
  /// The backend's own message for `frontend_code`. Empty when the failure
  /// came from validation rather than the frontend.
  RenderOperationDetail frontend_detail;
  std::uint64_t asset_sequence = 0U;
  std::uint64_t scene_snapshot_id = 0U;
  std::uint64_t frontend_frame_id = 0U;
  std::uint32_t resources_released = 0U;
  /// Process-lifetime count of frames this dispatcher declined without
  /// poisoning itself. Surfaced on every result -- success or failure -- so a
  /// heartbeat can observe the degrade without polling a second interface.
  /// A silent degrade trades a crash for a wrong picture; this is how the
  /// wrong picture becomes visible.
  std::uint64_t rejected_frames = 0U;
  /// Process-lifetime count of frontend render failures that arrived carrying
  /// RenderOperationRecovery::RETRY_NEXT_FRAME -- the frontend's own verdict
  /// that its reverse-abort walk left nothing committed.
  ///
  /// Counted AND honoured: such a frame is routed to `Reject()` and dropped,
  /// so it no longer poisons and publication resumes on the next frame. The
  /// counter remains the named degrade this boundary is required to expose --
  /// it now measures how often the picture silently skipped a frame.
  ///
  /// The frontend's verdict alone does not decide this. It is honoured only
  /// when the dispatcher's own lineage confirms the frame advanced nothing;
  /// a verdict contradicted by that check is treated as a possible partial
  /// commit and still poisons, because silent corruption is strictly worse
  /// than the session kill it would replace.
  std::uint64_t recoverable_frame_failures = 0U;
  bool terminal = false;

  [[nodiscard]] bool ok() const noexcept {
    return status == RendererFrontendDirectDispatchStatus::
                         ASSET_DELTA_SYNCHRONIZED ||
           status ==
               RendererFrontendDirectDispatchStatus::SCENE_FRAME_COMPLETED ||
           status ==
               RendererFrontendDirectDispatchStatus::SCENE_FRAME_RETIRED ||
           status ==
               RendererFrontendDirectDispatchStatus::SCENE_GENERATION_RESET;
  }
  explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] bool IsKnownRendererFrontendDirectDispatchStatus(
    RendererFrontendDirectDispatchStatus status) noexcept;
[[nodiscard]] const char *
ToString(RendererFrontendDirectDispatchStatus status) noexcept;

/// Renderer-neutral, same-thread consumer for a producer-owned typed catalog
/// and scene. Unlike RendererFrontendTransportDispatcher this class performs
/// no encoding, byte copying, channel I/O, or decoding. It retains a shallow
/// immutable logical registry solely to validate exact scene dependencies
/// before the frontend receives them.
///
/// The referenced freshly initialized and otherwise unused frontend must
/// outlive the dispatcher; this object then spans that frontend's complete
/// asset, scene-generation, and frame-identity lifetime. One caller serializes
/// all methods on the frontend owner thread.
///
/// GOVERNING INVARIANT for every validation at this boundary:
///
///   A per-frame validation may reject a frame or an object, but may not end
///   a session and may not permanently stop publication. Terminal is reserved
///   for load-time-unrecoverable state, or a rollback that demonstrably
///   failed. Every degrade increments a named counter -- a silent degrade
///   trades a crash for a wrong picture.
///
/// Rejection and poisoning are therefore two distinct outcomes here:
///   * `Reject()` -- the failure was observed strictly before this dispatcher
///     mutated any of its own lineage (`last_consumed_scene_snapshot_id_`,
///     `last_frontend_frame_id_`, the registry) and strictly before the first
///     `frontend_->` call of the operation, so nothing has committed. The
///     frame is dropped, `rejected_frames` increments, and the NEXT frame is
///     accepted normally. All five `RenderSceneImpl` prologue validators use
///     this path.
///   * `Fail()` -- the frontend may already have committed an asset
///     transaction or native work, so the dispatcher latches terminal. This
///     is the only path that ends a session.
/// The explicitly typed presentation-surface recovery above is a third,
/// narrower shape: it advances no dispatcher scene/frame identity and leaves
/// the exact scene eligible for retirement.
class RendererFrontendDirectDispatcher final {
public:
  RendererFrontendDirectDispatcher(IRendererFrontend &frontend,
                                   std::uint64_t registry_id) noexcept;

  RendererFrontendDirectDispatcher(
      const RendererFrontendDirectDispatcher &) = delete;
  RendererFrontendDirectDispatcher &
  operator=(const RendererFrontendDirectDispatcher &) = delete;
  RendererFrontendDirectDispatcher(RendererFrontendDirectDispatcher &&) =
      delete;
  RendererFrontendDirectDispatcher &
  operator=(RendererFrontendDirectDispatcher &&) = delete;
  ~RendererFrontendDirectDispatcher() = default;

  [[nodiscard]] RendererFrontendDirectDispatchResult
  SynchronizeAssets(const RenderAssetDelta &delta) noexcept;
  [[nodiscard]] RendererFrontendDirectDispatchResult RenderScene(
      std::shared_ptr<const SceneSnapshot> scene,
      const CameraViewRequest &camera,
      const RendererFrontendPresentationPolicy &presentation_policy,
      std::shared_ptr<const Ogre14ParticleCapturedFrame>
          continuous_particles = nullptr) noexcept;
  /// Opens the next map-scoped generation after the most recently accepted
  /// scene was authoritative and empty and the registry contains no live
  /// assets. Process-lifetime asset, snapshot, and frontend-frame identities
  /// remain monotonic.
  [[nodiscard]] RendererFrontendDirectDispatchResult
  ResetSceneGeneration() noexcept;

  [[nodiscard]] std::uint64_t registry_id() const noexcept {
    return registry_.registry_id();
  }
  [[nodiscard]] std::uint64_t asset_sequence() const noexcept {
    return registry_.sequence();
  }
  [[nodiscard]] std::uint64_t scene_generation() const noexcept {
    return scene_generation_;
  }
  [[nodiscard]] std::uint64_t last_frontend_frame_id() const noexcept {
    return last_frontend_frame_id_;
  }
  [[nodiscard]] std::uint64_t last_consumed_scene_snapshot_id() const
      noexcept {
    return last_consumed_scene_snapshot_id_;
  }
  [[nodiscard]] std::uint64_t rejected_frames() const noexcept {
    return rejected_frames_;
  }
  [[nodiscard]] std::uint64_t recoverable_frame_failures() const noexcept {
    return recoverable_frame_failures_;
  }
  [[nodiscard]] bool terminal() const noexcept { return terminal_; }
  [[nodiscard]] RendererFrontendDirectDispatchStatus terminal_cause() const
      noexcept {
    return terminal_cause_;
  }

private:
  struct ResourceReleaseResult final {
    RenderOperationCode first_failure = RenderOperationCode::OK;
    std::uint32_t released = 0U;
  };

  [[nodiscard]] RendererFrontendDirectDispatchResult Success(
      RendererFrontendDirectDispatchStatus status,
      std::uint64_t scene_snapshot_id = 0U,
      std::uint64_t frontend_frame_id = 0U,
      std::uint32_t resources_released = 0U) const noexcept;
  [[nodiscard]] RendererFrontendDirectDispatchResult Fail(
      RendererFrontendDirectDispatchStatus status,
      ValidationCode validation_code = ValidationCode::OK,
      RenderOperationCode frontend_code = RenderOperationCode::OK,
      std::uint32_t resources_released = 0U,
      const char *frontend_detail = nullptr) noexcept;
  /// Declines this frame without latching `terminal_`. Legal only where the
  /// dispatcher has provably committed nothing yet -- see the invariant above.
  /// Carries any pre-existing latch through `result.terminal`, exactly as
  /// `Success()` does, so an already-poisoned dispatcher never reports clean.
  [[nodiscard]] RendererFrontendDirectDispatchResult Reject(
      RendererFrontendDirectDispatchStatus status,
      ValidationCode validation_code = ValidationCode::OK,
      RenderOperationCode frontend_code = RenderOperationCode::OK) noexcept;
  [[nodiscard]] RendererFrontendDirectDispatchResult
  RetryablePresentationSurfaceStale(
      std::uint64_t scene_snapshot_id,
      std::uint32_t resources_released) const noexcept;
  [[nodiscard]] ResourceReleaseResult
  ReleaseTransferredResources(const RenderFrameOutput &output) noexcept;
  [[nodiscard]] RendererFrontendDirectDispatchResult
  SynchronizeAssetsImpl(const RenderAssetDelta &delta);
  [[nodiscard]] RendererFrontendDirectDispatchResult RenderSceneImpl(
      std::shared_ptr<const SceneSnapshot> scene,
      const CameraViewRequest &camera,
      const RendererFrontendPresentationPolicy &presentation_policy,
      std::shared_ptr<const Ogre14ParticleCapturedFrame>
          continuous_particles);
  [[nodiscard]] RendererFrontendDirectDispatchResult
  ResetSceneGenerationImpl();

  IRendererFrontend *frontend_ = nullptr;
  RenderAssetRegistry registry_;
  RendererFrontendDirectDispatchStatus terminal_cause_ =
      RendererFrontendDirectDispatchStatus::FAILED_INTERNAL;
  std::uint64_t scene_generation_ = 1U;
  std::uint64_t last_frontend_frame_id_ = 0U;
  /// Process-lifetime replay watermark. Unlike the current generation's final
  /// snapshot identity below, this is deliberately never reset.
  std::uint64_t last_consumed_scene_snapshot_id_ = 0U;
  std::uint64_t last_scene_snapshot_id_ = 0U;
  std::uint64_t last_scene_asset_sequence_ = 0U;
  /// Named degrade counters for the invariant above. Never reset.
  std::uint64_t rejected_frames_ = 0U;
  std::uint64_t recoverable_frame_failures_ = 0U;
  bool last_scene_was_empty_ = false;
  bool terminal_ = false;
};

} // namespace RoR::Render
