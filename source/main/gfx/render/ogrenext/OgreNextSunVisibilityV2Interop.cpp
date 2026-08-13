/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextSunVisibilityV2Interop.h"

#include <array>
#include <cstddef>

namespace RoR::Render {
namespace {

ValidationResult Invalid(ValidationCode code, const char *field,
                         const char *detail, std::size_t index = 0U) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool SameOwner(const std::shared_ptr<const SceneSnapshot> &lhs,
               const std::shared_ptr<const SceneSnapshot> &rhs) noexcept {
  return lhs && rhs && lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool HasExpectedImageBinding(
    const OgreNextSunVisibilityV2ImageBinding &binding,
    OgreNextSunVisibilityV2ImageRole role,
    OgreNextSunVisibilityV2ImageFormat format,
    const NativeContextExport &context) noexcept {
  return binding.role == role && binding.format == format &&
         binding.usage ==
             NativeImageUsage::COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE &&
         binding.image.valid() &&
         binding.image.api == NativeGraphicsApi::METAL &&
         binding.image.kind == NativeObjectKind::IMAGE &&
         binding.image.context_id == context.context_id;
}

} // namespace

ValidationResult ValidateOgreNextSunVisibilityV2ImageSetRequest(
    const OgreNextSunVisibilityV2ImageSetRequest &request) {
  if (request.version != kOgreNextSunVisibilityV2ImageInteropVersion) {
    return Invalid(ValidationCode::UNSUPPORTED_VERSION, "request.version",
                   "unsupported sun-visibility V2 image-set version");
  }
  if (request.frame_id == 0U || request.snapshot_id == 0U ||
      request.view_id == 0U || !request.scene_snapshot ||
      request.scene_snapshot->snapshot_id() != request.snapshot_id) {
    return Invalid(ValidationCode::INVALID_IDENTIFIER, "request.lineage",
                   "sun-visibility image-set lineage is incomplete");
  }
  if (request.width == 0U || request.height == 0U ||
      request.width > kMaximumRenderDimension ||
      request.height > kMaximumRenderDimension ||
      request.view.view_id != request.view_id ||
      request.view.width != request.width ||
      request.view.height != request.height) {
    return Invalid(ValidationCode::INVALID_DIMENSIONS, "request.extent",
                   "sun-visibility image-set view and extent disagree");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgreNextSunVisibilityV2ImageSetExport(
    const OgreNextSunVisibilityV2ImageSetRequest &request,
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeContextExport &context) {
  const ValidationResult request_result =
      ValidateOgreNextSunVisibilityV2ImageSetRequest(request);
  if (!request_result) {
    return request_result;
  }
  if (images.version != kOgreNextSunVisibilityV2ImageInteropVersion ||
      context.version != kRendererFrontendContractVersion) {
    return Invalid(ValidationCode::UNSUPPORTED_VERSION, "images.version",
                   "sun-visibility image set or native context version is unsupported");
  }
  if (context.native_api != NativeGraphicsApi::METAL ||
      context.context_id == 0U || !context.device.valid() ||
      !context.graphics_queue.valid() ||
      context.device.api != NativeGraphicsApi::METAL ||
      context.graphics_queue.api != NativeGraphicsApi::METAL ||
      context.device.kind != NativeObjectKind::DEVICE ||
      context.graphics_queue.kind != NativeObjectKind::QUEUE ||
      context.device.context_id != context.context_id ||
      context.graphics_queue.context_id != context.context_id) {
    return Invalid(ValidationCode::UNSUPPORTED_FEATURE, "context",
                   "sun-visibility V2 requires the live Ogre Metal device and graphics queue");
  }
  if (images.export_id == 0U || images.frame_id != request.frame_id ||
      images.snapshot_id != request.snapshot_id ||
      images.view_id != request.view_id ||
      !SameOwner(images.scene_snapshot, request.scene_snapshot) ||
      images.view != request.view || images.width != request.width ||
      images.height != request.height) {
    return Invalid(ValidationCode::REVISION_MISMATCH, "images.lineage",
                   "sun-visibility image set changed immutable frame lineage");
  }
  const std::array<const OgreNextSunVisibilityV2ImageBinding *, 4U> bindings{
      &images.base_hdr, &images.sun_direct_hdr, &images.visibility,
      &images.lit_hdr};
  if (!HasExpectedImageBinding(
          images.base_hdr,
          OgreNextSunVisibilityV2ImageRole::BASE_HDR_RGBA16,
          OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, context) ||
      !HasExpectedImageBinding(
          images.sun_direct_hdr,
          OgreNextSunVisibilityV2ImageRole::SUN_DIRECT_HDR_RGBA16,
          OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, context) ||
      !HasExpectedImageBinding(
          images.visibility,
          OgreNextSunVisibilityV2ImageRole::VISIBILITY_R16,
          OgreNextSunVisibilityV2ImageFormat::R16_FLOAT, context) ||
      !HasExpectedImageBinding(
          images.lit_hdr,
          OgreNextSunVisibilityV2ImageRole::LIT_HDR_RGBA16,
          OgreNextSunVisibilityV2ImageFormat::RGBA16_FLOAT, context)) {
    return Invalid(ValidationCode::VALUE_OUT_OF_RANGE, "images.bindings",
                   "sun-visibility V2 requires exact four-role GPU image bindings");
  }
  for (std::size_t lhs = 0U; lhs < bindings.size(); ++lhs) {
    for (std::size_t rhs = lhs + 1U; rhs < bindings.size(); ++rhs) {
      if (bindings[lhs]->image.value == bindings[rhs]->image.value) {
        return Invalid(
            ValidationCode::VALUE_OUT_OF_RANGE, "images.bindings",
            "sun-visibility V2 roles must name four distinct native image identities regardless of generation metadata",
            rhs);
      }
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgreNextSunVisibilityV2FrameTransaction(
    const OgreNextSunVisibilityV2ImageSetRequest &request,
    const OgreNextSunVisibilityV2ImageSetExport &images,
    const NativeContextExport &context,
    const NativeFrameSynchronization &synchronization,
    bool require_external_completion) {
  const ValidationResult images_result =
      ValidateOgreNextSunVisibilityV2ImageSetExport(request, images, context);
  if (!images_result) {
    return images_result;
  }
  const ValidationResult synchronization_result =
      ValidateNativeFrameSynchronization(synchronization, context,
                                         require_external_completion);
  if (!synchronization_result) {
    return synchronization_result;
  }
  if (synchronization.frame_id != images.frame_id ||
      synchronization.snapshot_id != images.snapshot_id ||
      synchronization.frontend_image_release_state !=
          NativeImageState::GENERAL_READ_WRITE ||
      synchronization.external_image_return_state !=
          NativeImageState::GENERAL_READ_WRITE) {
    return Invalid(
        ValidationCode::SEQUENCE_MISMATCH, "synchronization.lineage",
        "sun-visibility images and the same-queue timeline must name one exact frame transaction");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
