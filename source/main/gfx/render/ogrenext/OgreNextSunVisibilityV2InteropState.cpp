/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextSunVisibilityV2InteropState.h"

#include <limits>

namespace RoR::Render {
namespace {

NativeSunVisibilityV2Result Result(NativeSunVisibilityV2Code code,
                                   NativeSunVisibilityV2Stage stage,
                                   std::uint64_t frame_id,
                                   std::uint64_t snapshot_id,
                                   const char *detail) {
  NativeSunVisibilityV2Result result;
  result.code = code;
  result.stage = stage;
  result.frame_id = frame_id;
  result.snapshot_id = snapshot_id;
  result.detail = detail;
  return result;
}

NativeSunVisibilityV2Result Ok(NativeSunVisibilityV2Stage stage,
                               std::uint64_t frame_id,
                               std::uint64_t snapshot_id) {
  return Result(NativeSunVisibilityV2Code::OK, stage, frame_id, snapshot_id,
                "ok");
}

bool SameOwner(const std::shared_ptr<const SceneSnapshot> &lhs,
               const std::shared_ptr<const SceneSnapshot> &rhs) noexcept {
  return lhs && rhs && lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool SameToken(const NativeObjectToken &lhs,
               const NativeObjectToken &rhs) noexcept {
  return lhs.api == rhs.api && lhs.kind == rhs.kind &&
         lhs.context_id == rhs.context_id && lhs.value == rhs.value &&
         lhs.generation == rhs.generation;
}

bool SameBinding(const OgreNextSunVisibilityV2ImageBinding &lhs,
                 const OgreNextSunVisibilityV2ImageBinding &rhs) noexcept {
  return lhs.role == rhs.role && lhs.format == rhs.format &&
         lhs.usage == rhs.usage && SameToken(lhs.image, rhs.image);
}

bool SameImages(const OgreNextSunVisibilityV2ImageSetExport &lhs,
                const OgreNextSunVisibilityV2ImageSetExport &rhs) noexcept {
  return lhs.version == rhs.version && lhs.export_id == rhs.export_id &&
         lhs.frame_id == rhs.frame_id &&
         lhs.snapshot_id == rhs.snapshot_id && lhs.view_id == rhs.view_id &&
         SameOwner(lhs.scene_snapshot, rhs.scene_snapshot) &&
         lhs.view == rhs.view && lhs.width == rhs.width &&
         lhs.height == rhs.height && SameBinding(lhs.base_hdr, rhs.base_hdr) &&
         SameBinding(lhs.sun_direct_hdr, rhs.sun_direct_hdr) &&
         SameBinding(lhs.visibility, rhs.visibility) &&
         SameBinding(lhs.lit_hdr, rhs.lit_hdr);
}

} // namespace

NativeSunVisibilityV2Result OgreNextSunVisibilityV2InteropState::Initialize(
    const NativeContextExport &context) {
  if (initialized_ || !ValidateNativeContextExport(context) ||
      context.native_api != NativeGraphicsApi::METAL) {
    return Result(NativeSunVisibilityV2Code::INVALID_ARGUMENT,
                  NativeSunVisibilityV2Stage::CAPABILITY_GATE, 1U, 1U,
                  "invalid-metal-context");
  }
  context_ = context;
  initialized_ = true;
  return Ok(NativeSunVisibilityV2Stage::CAPABILITY_GATE, 1U, 1U);
}

NativeSunVisibilityV2Result OgreNextSunVisibilityV2InteropState::PreparePublish(
    const OgreNextSunVisibilityV2FrameImageBinding &binding,
    const OgreNextSunVisibilityV2ImageSetExport &converted_images) {
  const std::uint64_t frame = binding.frame_id;
  const std::uint64_t snapshot = binding.snapshot_id;
  if (!initialized_ || prepared_live_ || lease_live_) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, frame, snapshot,
                  "image-set-transaction-busy");
  }
  OgreNextSunVisibilityV2ImageSetRequest request;
  request.frame_id = frame;
  request.snapshot_id = snapshot;
  request.view_id = binding.view_id;
  request.scene_snapshot = binding.scene_snapshot;
  request.view = binding.view;
  request.width = binding.width;
  request.height = binding.height;
  if (binding.version != kOgreNextSunVisibilityV2ImageInteropVersion ||
      binding.ogre_base_hdr_texture == 0U ||
      binding.ogre_sun_direct_hdr_texture == 0U ||
      binding.ogre_visibility_texture == 0U ||
      binding.ogre_lit_hdr_texture == 0U ||
      binding.presentation_continuation == nullptr ||
      !ValidateOgreNextSunVisibilityV2ImageSetExport(
          request, converted_images, context_)) {
    return Result(NativeSunVisibilityV2Code::INVALID_ARGUMENT,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, frame, snapshot,
                  "invalid-image-set-publication");
  }
  prepared_.binding = binding;
  prepared_.images = converted_images;
  prepared_live_ = true;
  return Ok(NativeSunVisibilityV2Stage::IMAGE_EXPORT, frame, snapshot);
}

bool OgreNextSunVisibilityV2InteropState::CanCommitPrepared(
    std::uint64_t frame_id, std::uint64_t snapshot_id) const noexcept {
  return initialized_ && prepared_live_ && !lease_live_ &&
         prepared_.binding.frame_id == frame_id &&
         prepared_.binding.snapshot_id == snapshot_id;
}

void OgreNextSunVisibilityV2InteropState::CommitPrepared() noexcept {
  if (!prepared_live_ || lease_live_) {
    return;
  }
  published_ = std::move(prepared_);
  prepared_ = {};
  prepared_live_ = false;
  published_live_ = true;
}

void OgreNextSunVisibilityV2InteropState::AbortPrepared() noexcept {
  prepared_ = {};
  prepared_live_ = false;
}

NativeSunVisibilityV2Result
OgreNextSunVisibilityV2InteropState::DiscardPublished() {
  const std::uint64_t frame = published_.binding.frame_id;
  const std::uint64_t snapshot = published_.binding.snapshot_id;
  if (!initialized_ || lease_live_ || prepared_live_) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT,
                  frame == 0U ? 1U : frame, snapshot == 0U ? 1U : snapshot,
                  "published-image-set-busy");
  }
  published_ = {};
  published_live_ = false;
  return Ok(NativeSunVisibilityV2Stage::IMAGE_EXPORT,
            frame == 0U ? 1U : frame, snapshot == 0U ? 1U : snapshot);
}

NativeSunVisibilityV2Result OgreNextSunVisibilityV2InteropState::Acquire(
    const OgreNextSunVisibilityV2ImageSetRequest &request,
    OgreNextSunVisibilityV2ImageSetExport &output) {
  if (!initialized_ || !published_live_ || lease_live_ || prepared_live_) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, request.frame_id,
                  request.snapshot_id, "image-set-not-acquirable");
  }
  OgreNextSunVisibilityV2ImageSetExport candidate = published_.images;
  if (next_export_id_ == 0U ||
      next_export_id_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return Result(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, request.frame_id,
                  request.snapshot_id, "image-export-id-exhausted");
  }
  candidate.export_id = next_export_id_++;
  if (!ValidateOgreNextSunVisibilityV2ImageSetExport(request, candidate,
                                                      context_)) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, request.frame_id,
                  request.snapshot_id, "published-image-set-stale");
  }
  lease_ = candidate;
  lease_live_ = true;
  external_frame_begun_ = false;
  external_frame_ended_ = false;
  presentation_attempted_ = false;
  presentation_continued_ = false;
  aborted_ = false;
  output = candidate;
  return Ok(NativeSunVisibilityV2Stage::IMAGE_EXPORT, request.frame_id,
            request.snapshot_id);
}

NativeSunVisibilityV2Result OgreNextSunVisibilityV2InteropState::ValidateLease(
    const OgreNextSunVisibilityV2ImageSetExport &images) const {
  if (!initialized_ || !lease_live_ || !SameImages(images, lease_)) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, images.frame_id,
                  images.snapshot_id, "image-set-lease-stale");
  }
  return Ok(NativeSunVisibilityV2Stage::IMAGE_EXPORT, images.frame_id,
            images.snapshot_id);
}

void OgreNextSunVisibilityV2InteropState::ObserveExternalFrameBegun(
    const NativeFrameSynchronization &synchronization) noexcept {
  if (lease_live_ && synchronization.frame_id == lease_.frame_id &&
      synchronization.snapshot_id == lease_.snapshot_id) {
    external_frame_begun_ = true;
  }
}

void OgreNextSunVisibilityV2InteropState::ObserveExternalFrameEnded(
    const NativeFrameSynchronization &synchronization) noexcept {
  if (lease_live_ && synchronization.frame_id == lease_.frame_id &&
      synchronization.snapshot_id == lease_.snapshot_id) {
    external_frame_ended_ = true;
  }
}

NativeSunVisibilityV2Result
OgreNextSunVisibilityV2InteropState::ContinuePresentation(
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeFrameSynchronization &synchronization) {
  const NativeSunVisibilityV2Result lease = ValidateLease(images);
  if (lease.code != NativeSunVisibilityV2Code::OK ||
      !external_frame_begun_ || !external_frame_ended_ ||
      presentation_attempted_ || aborted_ ||
      synchronization.frame_id != images.frame_id ||
      synchronization.snapshot_id != images.snapshot_id) {
    return Result(NativeSunVisibilityV2Code::RESOURCE_STALE,
                  NativeSunVisibilityV2Stage::PRESENT_CONTINUATION,
                  images.frame_id, images.snapshot_id,
                  "lit-hdr-continuation-out-of-order");
  }
  // Consume the one-shot continuation before entering frontend code. A valid
  // failure can be reported after that callback has already submitted work,
  // so retrying the same lease would risk a duplicate presentation.
  presentation_attempted_ = true;
  NativeSunVisibilityV2Result result =
      published_.binding.presentation_continuation->ContinueFromLitHdr(
          images.frame_id, images.snapshot_id, images.view_id,
          published_.binding.ogre_lit_hdr_texture);
  if (!ValidateNativeSunVisibilityV2Result(result) ||
      result.frame_id != images.frame_id ||
      result.snapshot_id != images.snapshot_id) {
    return Result(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                  NativeSunVisibilityV2Stage::PRESENT_CONTINUATION,
                  images.frame_id, images.snapshot_id,
                  "lit-hdr-continuation-invalid-result");
  }
  if (result.code == NativeSunVisibilityV2Code::OK) {
    presentation_continued_ = true;
  }
  return result;
}

NativeSunVisibilityV2Result
OgreNextSunVisibilityV2InteropState::AbortBeforeSubmission(
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeFrameSynchronization &synchronization,
    const NativeSunVisibilityV2Result &failure) {
  const NativeSunVisibilityV2Result lease = ValidateLease(images);
  if (lease.code != NativeSunVisibilityV2Code::OK ||
      !ValidateNativeSunVisibilityV2Result(failure) ||
      failure.code == NativeSunVisibilityV2Code::OK ||
      failure.frame_id != images.frame_id ||
      failure.snapshot_id != images.snapshot_id ||
      synchronization.frame_id != images.frame_id ||
      synchronization.snapshot_id != images.snapshot_id ||
      external_frame_ended_ || presentation_attempted_) {
    return Result(NativeSunVisibilityV2Code::BACKEND_FAILURE,
                  NativeSunVisibilityV2Stage::IMAGE_EXPORT, images.frame_id,
                  images.snapshot_id, "image-set-rollback-invalid");
  }
  aborted_ = true;
  return failure;
}

void OgreNextSunVisibilityV2InteropState::Release(
    std::uint64_t export_id) noexcept {
  if (!lease_live_ || lease_.export_id != export_id ||
      (!presentation_attempted_ && !aborted_ && external_frame_begun_)) {
    return;
  }
  lease_ = {};
  lease_live_ = false;
  external_frame_begun_ = false;
  external_frame_ended_ = false;
  presentation_attempted_ = false;
  presentation_continued_ = false;
  aborted_ = false;
}

void OgreNextSunVisibilityV2InteropState::Reset() noexcept {
  prepared_ = {};
  published_ = {};
  lease_ = {};
  context_ = {};
  next_export_id_ = 1U;
  initialized_ = false;
  prepared_live_ = false;
  published_live_ = false;
  lease_live_ = false;
  external_frame_begun_ = false;
  external_frame_ended_ = false;
  presentation_attempted_ = false;
  presentation_continued_ = false;
  aborted_ = false;
}

bool OgreNextSunVisibilityV2InteropState::HasOutstandingLease() const noexcept {
  return lease_live_;
}

} // namespace RoR::Render
