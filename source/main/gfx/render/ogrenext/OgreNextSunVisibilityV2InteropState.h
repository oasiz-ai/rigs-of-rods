/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Transactional state for the additive four-image V2 Metal lease.

#pragma once

#include "OgreNextSunVisibilityV2Interop.h"

#include <cstdint>

namespace RoR::Render {

class OgreNextSunVisibilityV2InteropState final {
public:
  [[nodiscard]] NativeSunVisibilityV2Result
  Initialize(const NativeContextExport &context);

  [[nodiscard]] NativeSunVisibilityV2Result PreparePublish(
      const OgreNextSunVisibilityV2FrameImageBinding &binding,
      const OgreNextSunVisibilityV2ImageSetExport &converted_images);
  [[nodiscard]] bool CanCommitPrepared(
      std::uint64_t frame_id,
      std::uint64_t snapshot_id) const noexcept;
  void CommitPrepared() noexcept;
  void AbortPrepared() noexcept;
  [[nodiscard]] NativeSunVisibilityV2Result DiscardPublished();

  [[nodiscard]] NativeSunVisibilityV2Result Acquire(
      const OgreNextSunVisibilityV2ImageSetRequest &request,
      OgreNextSunVisibilityV2ImageSetExport &output);
  [[nodiscard]] NativeSunVisibilityV2Result ValidateLease(
      const OgreNextSunVisibilityV2ImageSetExport &images) const;
  void ObserveExternalFrameBegun(
      const NativeFrameSynchronization &synchronization) noexcept;
  void ObserveExternalFrameEnded(
      const NativeFrameSynchronization &synchronization) noexcept;
  [[nodiscard]] NativeSunVisibilityV2Result ContinuePresentation(
      const OgreNextSunVisibilityV2ImageSetExport &images,
      const NativeFrameSynchronization &synchronization);
  [[nodiscard]] NativeSunVisibilityV2Result AbortBeforeSubmission(
      const OgreNextSunVisibilityV2ImageSetExport &images,
      const NativeFrameSynchronization &synchronization,
      const NativeSunVisibilityV2Result &failure);
  void Release(std::uint64_t export_id) noexcept;
  void Reset() noexcept;

  [[nodiscard]] bool HasOutstandingLease() const noexcept;

private:
  struct Publication final {
    OgreNextSunVisibilityV2FrameImageBinding binding;
    OgreNextSunVisibilityV2ImageSetExport images;
  };

  NativeContextExport context_;
  Publication prepared_;
  Publication published_;
  OgreNextSunVisibilityV2ImageSetExport lease_;
  std::uint64_t next_export_id_ = 1U;
  bool initialized_ = false;
  bool prepared_live_ = false;
  bool published_live_ = false;
  bool lease_live_ = false;
  bool external_frame_begun_ = false;
  bool external_frame_ended_ = false;
  bool presentation_continued_ = false;
  bool aborted_ = false;
};

} // namespace RoR::Render
