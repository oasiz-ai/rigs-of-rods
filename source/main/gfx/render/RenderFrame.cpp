/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderFrame.h"

#include <algorithm>
#include <array>
#include <limits>

namespace RoR::Render {
namespace {

constexpr std::uint32_t kKnownOutputBits =
    static_cast<std::uint32_t>(FrameOutputMask::COLOR) |
    static_cast<std::uint32_t>(FrameOutputMask::DEPTH) |
    static_cast<std::uint32_t>(FrameOutputMask::MOTION_VECTORS) |
    static_cast<std::uint32_t>(FrameOutputMask::OBJECT_ID) |
    static_cast<std::uint32_t>(FrameOutputMask::SURFACE_NORMAL) |
    static_cast<std::uint32_t>(FrameOutputMask::MATERIAL_ID);

constexpr std::array<FrameOutputMask, 6U> kOrderedFrameOutputs{{
    FrameOutputMask::COLOR,
    FrameOutputMask::DEPTH,
    FrameOutputMask::MOTION_VECTORS,
    FrameOutputMask::OBJECT_ID,
    FrameOutputMask::SURFACE_NORMAL,
    FrameOutputMask::MATERIAL_ID,
}};

std::uint32_t BytesPerPixel(PixelFormat format) noexcept {
  switch (format) {
  case PixelFormat::RGBA8_SRGB:
  case PixelFormat::R32_FLOAT:
  case PixelFormat::RG16_FLOAT:
    return 4U;
  case PixelFormat::RGBA16_FLOAT:
  case PixelFormat::RG32_UINT:
  case PixelFormat::RGBA16_SNORM:
    return 8U;
  case PixelFormat::RGBA32_UINT:
    return 16U;
  case PixelFormat::INVALID:
    return 0U;
  }
  return 0U;
}

} // namespace

bool IsKnownPixelFormat(PixelFormat format) noexcept {
  return BytesPerPixel(format) != 0U;
}

bool IsKnownRenderFrameStatus(RenderFrameStatus status) noexcept {
  switch (status) {
  case RenderFrameStatus::RENDERED:
  case RenderFrameStatus::SKIPPED:
  case RenderFrameStatus::INVALID_REQUEST:
  case RenderFrameStatus::UNSUPPORTED:
  case RenderFrameStatus::DEVICE_LOST:
  case RenderFrameStatus::FAILED:
    return true;
  }
  return false;
}

bool IsSingleKnownFrameOutput(FrameOutputMask output) noexcept {
  const std::uint32_t value = static_cast<std::uint32_t>(output);
  return value != 0U && (value & (value - 1U)) == 0U &&
         (value & ~kKnownOutputBits) == 0U;
}

ValidationResult ValidateCameraViewRequest(const CameraViewRequest &view,
                                           std::size_t index) {
  if (view.view_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "views.view_id",
        "view identifier must be nonzero", index);
  }
  if (view.width == 0U || view.height == 0U ||
      view.width > kMaximumRenderDimension ||
      view.height > kMaximumRenderDimension) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "views.extent",
        "view dimensions must be in [1, 65535]", index);
  }
  if (!IsFinite(view.view_from_render) || !IsFinite(view.clip_from_view) ||
      !IsFinite(view.previous_view_from_render) ||
      !IsFinite(view.previous_clip_from_view) ||
      !IsFinite(view.temporal_jitter_pixels) || !IsFinite(view.near_plane) ||
      !IsFinite(view.far_plane) || !IsFinite(view.exposure)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "views.camera",
        "view matrices and camera values must be finite", index);
  }
  if (view.near_plane <= 0.0F || view.far_plane <= view.near_plane ||
      view.exposure <= 0.0F || view.visibility_mask == 0U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "views.camera",
        "view clipping, exposure, and visibility must be positive", index);
  }
  if (std::fabs(view.temporal_jitter_pixels.x) > 0.5F ||
      std::fabs(view.temporal_jitter_pixels.y) > 0.5F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "views.temporal_jitter_pixels",
        "temporal jitter must remain within half a pixel per axis", index);
  }
  if (!HasRigidRightHandedAffineTransform(view.view_from_render) ||
      !HasRigidRightHandedAffineTransform(view.previous_view_from_render)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "views.view_from_render",
        "current and previous view matrices must be rigid right-handed affine",
        index);
  }
  if (!IsCanonicalProjection(view.clip_from_view, view.near_plane,
                             view.far_plane) ||
      !IsCanonicalProjection(view.previous_clip_from_view, view.near_plane,
                             view.far_plane)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "views.clip_from_view",
        "current and previous projections must match the canonical camera convention",
        index);
  }
  return ValidationResult::Success();
}

ValidationResult ValidateRenderFrameRequest(const RenderFrameRequest &request) {
  if (request.version != kRenderFrameContractVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported frame request version");
  }
  if (request.frame_id == 0U) {
    return ValidationResult::Failure(ValidationCode::INVALID_IDENTIFIER,
                                     "frame_id",
                                     "frame identifier must be nonzero");
  }
  if (request.scene_snapshot == nullptr) {
    return ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                     "scene_snapshot",
                                     "frame requires an immutable snapshot");
  }
  const std::uint32_t outputs =
      static_cast<std::uint32_t>(request.requested_outputs);
  if (outputs == 0U || (outputs & ~kKnownOutputBits) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
        "output mask must contain only known, nonzero outputs");
  }
  if (request.color_format != PixelFormat::RGBA8_SRGB &&
      request.color_format != PixelFormat::RGBA16_FLOAT) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "color_format",
        "color output format must be RGBA8_SRGB or RGBA16_FLOAT");
  }
  if (request.present &&
      !HasFrameOutput(request.requested_outputs, FrameOutputMask::COLOR)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
        "presented frames must request a color output");
  }
  if (request.views.empty()) {
    return ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD, "views",
                                     "frame requires at least one view");
  }
  if (request.present && (request.presentation_view_id == 0U ||
                          request.presentation_surface_revision == 0U)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "presentation",
        "presented frame requires view and active surface revision identities");
  }
  if (!request.present && (request.presentation_view_id != 0U ||
                           request.presentation_surface_revision != 0U)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "presentation",
        "offscreen frame cannot identify a presentation view or surface");
  }

  std::uint64_t previous_view_id = 0U;
  bool found_presentation_view = false;
  for (std::size_t index = 0U; index < request.views.size(); ++index) {
    const CameraViewRequest &view = request.views[index];
    ValidationResult view_validation =
        ValidateCameraViewRequest(view, index);
    if (!view_validation) {
      return view_validation;
    }
    if (index != 0U && view.view_id == previous_view_id) {
      return ValidationResult::Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                                       "views.view_id",
                                       "view identifier is duplicated", index);
    }
    if (index != 0U && view.view_id < previous_view_id) {
      return ValidationResult::Failure(
          ValidationCode::NON_DETERMINISTIC_ORDER, "views.view_id",
          "view identifiers must be strictly increasing", index);
    }
    previous_view_id = view.view_id;
    found_presentation_view =
        found_presentation_view || view.view_id == request.presentation_view_id;
  }
  if (request.present && !found_presentation_view) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "presentation_view_id",
        "presentation view must identify one requested camera view");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateRenderFrameOutput(const RenderFrameOutput &output) {
  if (output.version != kRenderFrameContractVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported frame output version");
  }
  if (output.frame_id == 0U || output.snapshot_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "frame_id",
        "frame and snapshot identifiers must be nonzero");
  }
  if (!IsKnownRenderFrameStatus(output.status)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "status",
                                     "unknown frame output status");
  }
  if (output.presented != (output.presented_view_id != 0U)) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "presented_view_id",
        "presented state and view identity must be supplied together");
  }
  if (!IsFinite(output.cpu_submit_milliseconds) ||
      output.cpu_submit_milliseconds < 0.0 ||
      !IsFinite(output.gpu_frame_milliseconds) ||
      output.gpu_frame_milliseconds < 0.0) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "timings",
        "frame timings must be finite and nonnegative");
  }

  std::uint64_t previous_view = 0U;
  std::uint32_t previous_output = 0U;
  bool has_previous = false;
  std::vector<ResourceHandle> owned_gpu_resources;
  for (std::size_t index = 0U; index < output.attachments.size(); ++index) {
    const FrameAttachment &attachment = output.attachments[index];
    const std::uint32_t output_value =
        static_cast<std::uint32_t>(attachment.output);
    if (attachment.view_id == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "attachments.view_id",
          "attachment view identifier must be nonzero", index);
    }
    if (!IsSingleKnownFrameOutput(attachment.output)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_OUTPUT_MASK, "attachments.output",
          "attachment output must contain exactly one known bit", index);
    }
    if (has_previous && (attachment.view_id < previous_view ||
                         (attachment.view_id == previous_view &&
                          output_value <= previous_output))) {
      return ValidationResult::Failure(
          attachment.view_id == previous_view && output_value == previous_output
              ? ValidationCode::DUPLICATE_IDENTIFIER
              : ValidationCode::NON_DETERMINISTIC_ORDER,
          "attachments.order",
          "attachments must be unique and ordered by view then output", index);
    }
    previous_view = attachment.view_id;
    previous_output = output_value;
    has_previous = true;
    if (!IsKnownPixelFormat(attachment.format)) {
      return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                       "attachments.format",
                                       "unknown pixel format", index);
    }
    const bool compatible_format =
        (attachment.output == FrameOutputMask::COLOR &&
         (attachment.format == PixelFormat::RGBA8_SRGB ||
          attachment.format == PixelFormat::RGBA16_FLOAT)) ||
        (attachment.output == FrameOutputMask::DEPTH &&
         attachment.format == PixelFormat::R32_FLOAT) ||
        (attachment.output == FrameOutputMask::MOTION_VECTORS &&
         attachment.format == PixelFormat::RG16_FLOAT) ||
        (attachment.output == FrameOutputMask::OBJECT_ID &&
         attachment.format == PixelFormat::RG32_UINT) ||
        (attachment.output == FrameOutputMask::SURFACE_NORMAL &&
         attachment.format == PixelFormat::RGBA16_SNORM) ||
        (attachment.output == FrameOutputMask::MATERIAL_ID &&
         attachment.format == PixelFormat::RGBA32_UINT);
    if (!compatible_format) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_ENUM, "attachments.format",
          "pixel format is incompatible with the requested output", index);
    }
    if (attachment.width == 0U || attachment.height == 0U ||
        attachment.width > kMaximumRenderDimension ||
        attachment.height > kMaximumRenderDimension) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS, "attachments.extent",
          "attachment dimensions must be in [1, 65535]", index);
    }
    if (attachment.gpu_resource.valid() &&
        attachment.gpu_resource.kind() != ResourceKind::RENDER_TARGET &&
        attachment.gpu_resource.kind() != ResourceKind::TEXTURE) {
      return ValidationResult::Failure(
          ValidationCode::WRONG_RESOURCE_KIND, "attachments.gpu_resource",
          "attachment GPU resource must be a texture or render target", index);
    }
    if (attachment.gpu_resource.valid()) {
      if (std::find(owned_gpu_resources.begin(), owned_gpu_resources.end(),
                    attachment.gpu_resource) != owned_gpu_resources.end()) {
        return ValidationResult::Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "attachments.gpu_resource",
            "each transferred GPU output handle must be unique", index);
      }
      owned_gpu_resources.push_back(attachment.gpu_resource);
    }
    if (!attachment.gpu_resource.valid() && attachment.bytes.empty()) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "attachments",
          "attachment requires a GPU resource or CPU readback", index);
    }
    if (!attachment.bytes.empty()) {
      const std::uint64_t minimum_pitch =
          static_cast<std::uint64_t>(attachment.width) *
          BytesPerPixel(attachment.format);
      if (attachment.row_pitch_bytes < minimum_pitch ||
          attachment.row_pitch_bytes >
              (std::numeric_limits<std::uint64_t>::max)() / attachment.height) {
        return ValidationResult::Failure(
            ValidationCode::SIZE_MISMATCH, "attachments.row_pitch_bytes",
            "readback row pitch is too small or overflows", index);
      }
      const std::uint64_t expected_size =
          attachment.row_pitch_bytes * attachment.height;
      if (expected_size != attachment.bytes.size()) {
        return ValidationResult::Failure(
            ValidationCode::SIZE_MISMATCH, "attachments.bytes",
            "readback byte count must equal row pitch times height", index);
      }
    } else if (attachment.row_pitch_bytes != 0U) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "attachments.row_pitch_bytes",
          "row pitch must be zero when no CPU readback is present", index);
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateRenderFrameOutput(const RenderFrameRequest &request,
                                           const RenderFrameOutput &output) {
  ValidationResult validation = ValidateRenderFrameRequest(request);
  if (!validation) {
    return validation;
  }
  validation = ValidateRenderFrameOutput(output);
  if (!validation) {
    return validation;
  }

  if (output.frame_id != request.frame_id ||
      output.snapshot_id != request.scene_snapshot->snapshot_id()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "output.identity",
        "output frame and snapshot identifiers must match the request");
  }

  if (output.status != RenderFrameStatus::RENDERED) {
    if (output.presented || !output.attachments.empty()) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "output.failure_payload",
          "non-rendered output cannot be presented or contain attachments");
    }
    return ValidationResult::Success();
  }

  if (output.presented != request.present) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "output.presented",
        "rendered output presentation state must match the request");
  }
  if (output.presented_view_id != request.presentation_view_id) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "output.presented_view_id",
        "output presentation view must match the exact request");
  }

  std::size_t expected_count = 0U;
  for (const FrameOutputMask candidate : kOrderedFrameOutputs) {
    if (HasFrameOutput(request.requested_outputs, candidate)) {
      ++expected_count;
    }
  }
  expected_count *= request.views.size();
  if (output.attachments.size() != expected_count) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "output.attachments",
        "rendered output must contain every requested view/output pair");
  }

  std::size_t attachment_index = 0U;
  for (const CameraViewRequest &view : request.views) {
    for (const FrameOutputMask candidate : kOrderedFrameOutputs) {
      if (!HasFrameOutput(request.requested_outputs, candidate)) {
        continue;
      }
      const FrameAttachment &attachment = output.attachments[attachment_index];
      if (attachment.view_id != view.view_id ||
          attachment.output != candidate) {
        return ValidationResult::Failure(
            ValidationCode::MISSING_REFERENCE, "output.attachments",
            "attachment does not match the requested view/output pair",
            attachment_index);
      }
      if (attachment.width != view.width || attachment.height != view.height) {
        return ValidationResult::Failure(
            ValidationCode::INVALID_DIMENSIONS, "output.attachments.extent",
            "attachment extent must match its requested view",
            attachment_index);
      }
      if (candidate == FrameOutputMask::COLOR &&
          attachment.format != request.color_format) {
        return ValidationResult::Failure(
            ValidationCode::INVALID_ENUM, "output.attachments.format",
            "color attachment format must match the exact request",
            attachment_index);
      }
      ++attachment_index;
    }
  }

  return ValidationResult::Success();
}

} // namespace RoR::Render
