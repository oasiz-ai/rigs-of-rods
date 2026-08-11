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

constexpr std::uint32_t kRendererFrontendDirectDispatcherContractVersion = 1U;

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
  std::uint64_t asset_sequence = 0U;
  std::uint64_t scene_snapshot_id = 0U;
  std::uint64_t frontend_frame_id = 0U;
  std::uint32_t resources_released = 0U;
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
/// all methods on the frontend owner thread. Any validation or frontend failure
/// permanently poisons this object because the frontend may already have
/// committed an asset transaction or native work, except the explicitly typed
/// presentation-surface recovery above. That result advances no dispatcher
/// scene/frame identity and leaves the exact scene eligible for retirement.
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
      const RendererFrontendPresentationPolicy &presentation_policy) noexcept;
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
      std::uint32_t resources_released = 0U) noexcept;
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
      const RendererFrontendPresentationPolicy &presentation_policy);
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
  bool last_scene_was_empty_ = false;
  bool terminal_ = false;
};

} // namespace RoR::Render
