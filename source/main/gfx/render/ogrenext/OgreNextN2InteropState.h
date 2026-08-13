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

/// Native image identity resolved by the platform adapter before publication.
/// As with geometry, the lifecycle state assigns export_id on acquisition.
struct OgreNextN3PublishedImage {
  NativeImageExport image;
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
      const std::vector<OgreNextN2PublishedGeometry> &geometry,
      const std::vector<OgreNextN3PublishedImage> &images = {});
  /// Validates and owns a complete replacement without making it observable.
  /// This is the only allocating phase used by the enclosing render
  /// transaction.
  RenderOperationResult PreparePublishFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      const std::vector<OgreNextN2PublishedGeometry> &geometry,
      const std::vector<OgreNextN3PublishedImage> &images = {});
  [[nodiscard]] bool CanCommitPreparedFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id) const noexcept;
  /// Atomically swaps the prepared replacement into the public state. Callers
  /// must prove CanCommitPreparedFrame immediately before this no-fail step.
  void CommitPreparedFrame() noexcept;
  void AbortPreparedFrame() noexcept;
  [[nodiscard]] RenderOperationResult CanPublishFrame() const;
  RenderOperationResult DiscardPublishedFrame();

  RenderOperationResult
  AcquireGeometry(const NativeGeometryExportRequest &request,
                  NativeGeometryExport &output);
  RenderOperationResult AcquireImage(const NativeImageExportRequest &request,
                                     NativeImageExport &output);
  RenderOperationResult
  BeginExternalFrame(std::uint64_t frame_id, std::uint64_t snapshot_id,
                     NativeFrameSynchronization &output,
                     bool has_additive_image_lease = false);
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
  [[nodiscard]] RenderOperationResult
  ValidateImageLease(const NativeImageExport &image) const;
  [[nodiscard]] RenderOperationResult ValidateFrameLease(
      const NativeFrameSynchronization &synchronization) const;
  void ReleaseGeometry(std::uint64_t export_id) noexcept;
  void ReleaseImage(std::uint64_t export_id) noexcept;

  RenderOperationResult RegisterRayTracingBackend();
  RenderOperationResult UnregisterRayTracingBackend();
  /// Fault-only teardown escape hatch. The native adapter must latch itself
  /// unusable before calling this method, because submitted GPU work may still
  /// retain the old native allocations after the logical leases are revoked.
  RenderOperationResult AbandonRayTracingBackendAfterFault();
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
  std::map<std::uint64_t, NativeGeometryExport> prepared_geometry_;
  std::map<std::uint64_t, NativeGeometryExport> geometry_leases_;
  std::map<std::uint64_t, NativeImageExport> published_images_;
  std::map<std::uint64_t, NativeImageExport> prepared_images_;
  std::map<std::uint64_t, NativeImageExport> image_leases_;
  ActiveFrame active_frame_;
  std::uint64_t published_frame_id_ = 0U;
  std::uint64_t published_snapshot_id_ = 0U;
  std::uint64_t prepared_frame_id_ = 0U;
  std::uint64_t prepared_snapshot_id_ = 0U;
  std::uint64_t next_export_id_ = 1U;
  std::uint64_t next_timeline_value_ = 1U;
  bool initialized_ = false;
  bool prepared_frame_live_ = false;
  bool active_frame_live_ = false;
  bool ray_tracing_backend_registered_ = false;
};

} // namespace RoR::Render
