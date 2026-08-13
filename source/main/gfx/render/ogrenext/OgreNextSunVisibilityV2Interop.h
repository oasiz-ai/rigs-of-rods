/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Additive GPU-only image lease required by sun visibility V2.

#pragma once

#include "NativeSunVisibilityV2Contract.h"
#include "OgreNextN1NativeInterop.h"

#include <cstdint>
#include <memory>

namespace RoR::Render {

constexpr std::uint32_t kOgreNextSunVisibilityV2ImageInteropVersion = 2U;

/// Roles are explicit so no implementation can overload the V1 COLOR lease
/// and pretend that one texture represents four different lighting stages.
enum class OgreNextSunVisibilityV2ImageRole : std::uint8_t {
  INVALID = 0,
  BASE_HDR_RGBA16 = 1,
  SUN_DIRECT_HDR_RGBA16 = 2,
  VISIBILITY_R16 = 3,
  LIT_HDR_RGBA16 = 4,
};

/// Private V2 formats are not added to the frozen portable FrameAttachment
/// enum. In particular, PixelFormat has no R16_FLOAT value and therefore
/// cannot honestly describe the visibility texture.
enum class OgreNextSunVisibilityV2ImageFormat : std::uint8_t {
  INVALID = 0,
  RGBA16_FLOAT = 1,
  R16_FLOAT = 2,
};

struct OgreNextSunVisibilityV2ImageSetRequest final {
  std::uint32_t version = kOgreNextSunVisibilityV2ImageInteropVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  CameraViewRequest view;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
};

struct OgreNextSunVisibilityV2ImageBinding final {
  OgreNextSunVisibilityV2ImageRole role =
      OgreNextSunVisibilityV2ImageRole::INVALID;
  OgreNextSunVisibilityV2ImageFormat format =
      OgreNextSunVisibilityV2ImageFormat::INVALID;
  /// The frozen portable enum has one read/write image usage and includes a
  /// copy-source capability. V2 production code must not exercise that
  /// capability; the frame contract requires exact zero content readbacks.
  NativeImageUsage usage = NativeImageUsage::INVALID;
  NativeObjectToken image;
};

/// One lease owns all four frontend textures and their exact immutable frame
/// lineage. It is GPU-only: there is intentionally no byte vector or CPU
/// FrameAttachment. All tokens share the live Ogre context generation.
struct OgreNextSunVisibilityV2ImageSetExport final {
  std::uint32_t version = kOgreNextSunVisibilityV2ImageInteropVersion;
  std::uint64_t export_id = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  CameraViewRequest view;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  OgreNextSunVisibilityV2ImageBinding base_hdr;
  OgreNextSunVisibilityV2ImageBinding sun_direct_hdr;
  OgreNextSunVisibilityV2ImageBinding visibility;
  OgreNextSunVisibilityV2ImageBinding lit_hdr;
};

/// Frontend-owned continuation invoked only after the external Metal command
/// completed and Ogre accepted EndExternalFrame(). The implementation queues
/// tone mapping/presentation from the exact Ogre LitHdr texture; it never
/// presents a native texture independently of Ogre's compositor.
class OgreNextSunVisibilityV2PresentationContinuation {
public:
  virtual ~OgreNextSunVisibilityV2PresentationContinuation() = default;
  virtual NativeSunVisibilityV2Result ContinueFromLitHdr(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      std::uint64_t view_id, std::uintptr_t ogre_lit_hdr_texture) = 0;
};

/// Raw Ogre publication payload. Only the compile-isolated Metal adapter may
/// decode these TextureGpu identities. The frontend retains every texture,
/// snapshot, and continuation until the prepared/published transaction and
/// any acquired image-set lease have been released.
struct OgreNextSunVisibilityV2FrameImageBinding final {
  std::uint32_t version = kOgreNextSunVisibilityV2ImageInteropVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  CameraViewRequest view;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uintptr_t ogre_base_hdr_texture = 0U;
  std::uintptr_t ogre_sun_direct_hdr_texture = 0U;
  std::uintptr_t ogre_visibility_texture = 0U;
  std::uintptr_t ogre_lit_hdr_texture = 0U;
  OgreNextSunVisibilityV2PresentationContinuation *presentation_continuation =
      nullptr;
};

[[nodiscard]] ValidationResult
ValidateOgreNextSunVisibilityV2ImageSetRequest(
    const OgreNextSunVisibilityV2ImageSetRequest &request);
[[nodiscard]] ValidationResult ValidateOgreNextSunVisibilityV2ImageSetExport(
    const OgreNextSunVisibilityV2ImageSetRequest &request,
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeContextExport &context);
[[nodiscard]] ValidationResult
ValidateOgreNextSunVisibilityV2FrameTransaction(
    const OgreNextSunVisibilityV2ImageSetRequest &request,
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeContextExport &context,
    const NativeFrameSynchronization &synchronization,
    bool require_external_completion);

/// Additive private interface for the lighting frontend/backend integration.
/// Implementations also implement OgreNextN1NativeInteropBridge, so geometry,
/// these four images, and the existing NativeFrameSynchronization all use the
/// same Ogre device, queue, and timeline transaction.
class OgreNextSunVisibilityV2NativeInterop {
public:
  virtual ~OgreNextSunVisibilityV2NativeInterop() = default;

  virtual NativeSunVisibilityV2Result PreparePublishSunVisibilityV2ImageSet(
      const OgreNextSunVisibilityV2FrameImageBinding &binding) = 0;
  [[nodiscard]] virtual bool CanCommitPreparedSunVisibilityV2ImageSet(
      std::uint64_t frame_id,
      std::uint64_t snapshot_id) const noexcept = 0;
  virtual void CommitPreparedSunVisibilityV2ImageSet() noexcept = 0;
  virtual void AbortPreparedSunVisibilityV2ImageSet() noexcept = 0;

  virtual NativeSunVisibilityV2Result AcquireSunVisibilityV2ImageSet(
      const OgreNextSunVisibilityV2ImageSetRequest &request,
      OgreNextSunVisibilityV2ImageSetExport &output) = 0;
  [[nodiscard]] virtual NativeSunVisibilityV2Result
  ValidateSunVisibilityV2ImageSetLease(
      const OgreNextSunVisibilityV2ImageSetExport &images) const = 0;

  /// Called only after the backend signaled external completion and the base
  /// bridge accepted EndExternalFrame(). This consumes the LitHdr role as the
  /// source of the frontend's tone-map/present continuation on Ogre's queue;
  /// BaseHdr, SunDirectHdr, and Visibility can then be recycled. No CPU copy or
  /// second presentation owner is introduced.
  virtual NativeSunVisibilityV2Result
  ContinuePresentationFromSunVisibilityV2LitHdr(
      const OgreNextSunVisibilityV2ImageSetExport &images,
      const NativeFrameSynchronization &synchronization) = 0;

  /// Transactional pre-submit unwind. Success returns the original failure
  /// code, stage, lineage, and stable detail token unchanged after restoring
  /// frontend ownership; it must never relabel the failure as ROLLBACK.
  virtual NativeSunVisibilityV2Result
  AbortSunVisibilityV2ImageSetBeforeSubmission(
      const OgreNextSunVisibilityV2ImageSetExport &images,
      const NativeFrameSynchronization &synchronization,
      const NativeSunVisibilityV2Result &failure) = 0;

  /// Releases all four roles atomically. A submitted fault must first use the
  /// base bridge's abandonment path; an unsubmitted failure uses its rollback
  /// path, preserving the exact NativeSunVisibilityV2Result stage/detail.
  virtual void ReleaseSunVisibilityV2ImageSet(
      std::uint64_t export_id) noexcept = 0;
};

} // namespace RoR::Render
