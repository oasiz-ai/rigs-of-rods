/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Backend-neutral lifecycle state for Ogre-Next native interop.

#pragma once

#include "../RendererFrontend.h"

#include <cstdint>
#include <map>
#include <vector>

namespace RoR::Render {

/// A geometry payload whose native buffers are already resolved by the
/// platform adapter. `export_id` must be zero; the lifecycle state assigns a
/// unique identifier only when a caller acquires an immutable lease.
struct OgreNextN2PublishedGeometry {
  NativeGeometryExport geometry;
};

/// Pure state machine shared by native adapters. It owns no API objects and
/// performs no waits; platform code remains responsible for recording and
/// completing real queue synchronization before advancing the corresponding
/// transition here.
class OgreNextN2InteropState final {
public:
  RenderOperationResult Initialize(const NativeContextExport &context,
                                   NativeObjectToken frontend_timeline);

  /// Replaces the sole published frame. A prior revision remains immutable
  /// until every geometry lease and its external-frame lease have ended.
  RenderOperationResult PublishFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      const std::vector<OgreNextN2PublishedGeometry> &geometry);
  [[nodiscard]] RenderOperationResult CanPublishFrame() const;
  RenderOperationResult DiscardPublishedFrame();

  RenderOperationResult
  AcquireGeometry(const NativeGeometryExportRequest &request,
                  NativeGeometryExport &output);
  RenderOperationResult
  BeginExternalFrame(std::uint64_t frame_id, std::uint64_t snapshot_id,
                     NativeFrameSynchronization &output);
  RenderOperationResult
  ArmExternalCompletion(NativeFrameSynchronization &synchronization);
  RenderOperationResult MarkExternalSubmitted(
      const NativeFrameSynchronization &synchronization);
  RenderOperationResult MarkExternalCompleted(
      const NativeFrameSynchronization &synchronization);
  RenderOperationResult
  EndExternalFrame(const NativeFrameSynchronization &synchronization);
  RenderOperationResult AbortExternalFrameBeforeSubmission(
      const NativeFrameSynchronization &synchronization);

  [[nodiscard]] RenderOperationResult
  ValidateGeometryLease(const NativeGeometryExport &geometry) const;
  [[nodiscard]] RenderOperationResult ValidateFrameLease(
      const NativeFrameSynchronization &synchronization) const;
  void ReleaseGeometry(std::uint64_t export_id) noexcept;

  RenderOperationResult RegisterRayTracingBackend();
  RenderOperationResult UnregisterRayTracingBackend();
  [[nodiscard]] RenderOperationResult CanShutdown() const;
  RenderOperationResult Reset();

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] bool has_outstanding_leases() const noexcept;
  [[nodiscard]] bool ray_tracing_backend_registered() const noexcept {
    return ray_tracing_backend_registered_;
  }
  [[nodiscard]] const NativeContextExport &context() const noexcept {
    return context_;
  }

private:
  struct ActiveFrame {
    NativeFrameSynchronization synchronization;
    bool external_armed = false;
    bool external_submitted = false;
    bool external_completed = false;
  };

  [[nodiscard]] RenderOperationResult RequireInitialized() const;

  NativeContextExport context_;
  NativeObjectToken frontend_timeline_;
  std::map<std::uint64_t, NativeGeometryExport> published_geometry_;
  std::map<std::uint64_t, NativeGeometryExport> geometry_leases_;
  ActiveFrame active_frame_;
  std::uint64_t published_frame_id_ = 0U;
  std::uint64_t published_snapshot_id_ = 0U;
  std::uint64_t next_export_id_ = 1U;
  std::uint64_t next_timeline_value_ = 1U;
  bool initialized_ = false;
  bool active_frame_live_ = false;
  bool ray_tracing_backend_registered_ = false;
};

} // namespace RoR::Render
