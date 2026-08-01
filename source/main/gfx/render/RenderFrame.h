/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Portable render-frame requests and outputs.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"
#include "ResourceHandle.h"
#include "SceneSnapshot.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderFrameContractVersion = 1U;
constexpr std::uint32_t kMaximumRenderDimension = 65535U;

enum class FrameOutputMask : std::uint32_t {
  NONE = 0U,
  COLOR = 1U << 0U,
  DEPTH = 1U << 1U,
  MOTION_VECTORS = 1U << 2U,
  OBJECT_ID = 1U << 3U,
  SURFACE_NORMAL = 1U << 4U,
};

constexpr FrameOutputMask operator|(FrameOutputMask lhs,
                                    FrameOutputMask rhs) noexcept {
  return static_cast<FrameOutputMask>(static_cast<std::uint32_t>(lhs) |
                                      static_cast<std::uint32_t>(rhs));
}

constexpr FrameOutputMask operator&(FrameOutputMask lhs,
                                    FrameOutputMask rhs) noexcept {
  return static_cast<FrameOutputMask>(static_cast<std::uint32_t>(lhs) &
                                      static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool HasFrameOutput(FrameOutputMask mask,
                                            FrameOutputMask output) noexcept {
  return output != FrameOutputMask::NONE && (mask & output) == output;
}

enum class PixelFormat : std::uint8_t {
  INVALID = 0,
  RGBA8_SRGB,
  RGBA16_FLOAT,
  R32_FLOAT,
  RG16_FLOAT,
  RG32_UINT,
  RGBA16_SNORM,
};

enum class RenderFrameStatus : std::uint8_t {
  RENDERED = 0,
  SKIPPED,
  INVALID_REQUEST,
  UNSUPPORTED,
  DEVICE_LOST,
  FAILED,
};

struct CameraViewRequest {
  std::uint64_t view_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  /// All camera values consume the referenced snapshot's render-relative
  /// coordinates. The previous matrix is rebased to the current snapshot's
  /// absolute_world_origin_meters before submission.
  Matrix4x4 clip_from_render;
  Matrix4x4 previous_clip_from_render;
  Float3 camera_render_position{};
  Float2 temporal_jitter_pixels{};
  float near_plane = 0.1F;
  float far_plane = 10000.0F;
  float exposure = 1.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
};

struct RenderFrameRequest {
  std::uint32_t version = kRenderFrameContractVersion;
  /// Strictly increasing and never reused per initialized frontend lifetime.
  std::uint64_t frame_id = 0U;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  std::vector<CameraViewRequest> views;
  FrameOutputMask requested_outputs = FrameOutputMask::COLOR;
  /// When presenting, identifies exactly one requested view and the current
  /// active FrontendSurfaceUpdate revision. The selected view's dimensions
  /// must exactly match that surface's pixel extent; implicit scaling is
  /// forbidden. Both values are zero for an offscreen frame.
  std::uint64_t presentation_view_id = 0U;
  std::uint64_t presentation_surface_revision = 0U;
  bool present = true;
  bool allow_async_compute = false;
};

/// One output for one view. Canonical payload semantics are:
///
/// - COLOR RGBA8_SRGB stores sRGB RGB with linear, straight alpha;
///   RGBA16_FLOAT stores linear scene RGB with straight alpha.
/// - DEPTH R32_FLOAT stores positive camera-forward view depth in meters;
///   background is exactly the view far_plane value.
/// - MOTION_VECTORS RG16_FLOAT stores previous-pixel minus current-pixel in
///   pixels (+X right, +Y down), with temporal jitter removed; newly visible
///   or unavailable motion is (0, 0).
/// - OBJECT_ID RG32_UINT stores the exact uint64 mesh instance_id as low 32
///   bits in R and high 32 bits in G; background is zero.
/// - SURFACE_NORMAL RGBA16_SNORM stores a unit world-space normal in XYZ and
///   validity in W (1 for geometry, 0 for background).
///
/// `gpu_resource` and `bytes` are independently optional: a presented frame
/// may expose only a GPU resource, while a capture request may include tightly
/// or row-padded CPU bytes. CPU bytes are complete when Render() returns. A GPU
/// resource becomes readable after IsFrameComplete(frame_id); successful
/// RENDERED output transfers one unique handle per attachment to the caller,
/// which must call ReleaseResource exactly once. It is never implicitly
/// recycled when the frame completes or the output object is destroyed.
struct FrameAttachment {
  std::uint64_t view_id = 0U;
  FrameOutputMask output = FrameOutputMask::NONE;
  PixelFormat format = PixelFormat::INVALID;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t row_pitch_bytes = 0U;
  ResourceHandle gpu_resource;
  std::vector<std::uint8_t> bytes;
};

struct RenderFrameOutput {
  std::uint32_t version = kRenderFrameContractVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  RenderFrameStatus status = RenderFrameStatus::FAILED;
  bool presented = false;
  /// Zero unless presented; otherwise exactly the requested presentation view.
  std::uint64_t presented_view_id = 0U;
  double cpu_submit_milliseconds = 0.0;
  double gpu_frame_milliseconds = 0.0;
  std::vector<FrameAttachment> attachments;
};

[[nodiscard]] bool IsKnownPixelFormat(PixelFormat format) noexcept;
[[nodiscard]] bool IsKnownRenderFrameStatus(RenderFrameStatus status) noexcept;
[[nodiscard]] bool IsSingleKnownFrameOutput(FrameOutputMask output) noexcept;
[[nodiscard]] ValidationResult
ValidateRenderFrameRequest(const RenderFrameRequest &request);
[[nodiscard]] ValidationResult
ValidateRenderFrameOutput(const RenderFrameOutput &output);
/// Validates both structures and proves that the output is the complete,
/// deterministic response to this exact request.
[[nodiscard]] ValidationResult
ValidateRenderFrameOutput(const RenderFrameRequest &request,
                          const RenderFrameOutput &output);

} // namespace RoR::Render
