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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderFrameContractVersion = 2U;
constexpr std::uint32_t kMaximumRenderDimension = 65535U;

enum class FrameOutputMask : std::uint32_t {
  NONE = 0U,
  COLOR = 1U << 0U,
  DEPTH = 1U << 1U,
  MOTION_VECTORS = 1U << 2U,
  OBJECT_ID = 1U << 3U,
  SURFACE_NORMAL = 1U << 4U,
  MATERIAL_ID = 1U << 5U,
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
  RGBA32_UINT,
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
  /// coordinates. View and projection remain separate so a backend never has
  /// to guess a matrix decomposition for camera-space PBR state. Previous view
  /// coordinates are rebased to the current snapshot's absolute origin.
  Matrix4x4 view_from_render;
  Matrix4x4 clip_from_view;
  Matrix4x4 previous_view_from_render;
  Matrix4x4 previous_clip_from_view;
  /// Applied by the adapter after the unjittered projection. +X is right and
  /// +Y is down in pixels; each component is limited to half a pixel. The
  /// previous projection is also unjittered so motion can remove jitter.
  Float2 temporal_jitter_pixels{};
  float near_plane = 0.1F;
  float far_plane = 10000.0F;
  float exposure = 1.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
};

/// Exact semantic equality for one validated raster view. Native image
/// interop uses this to ensure an external contribution targets the same
/// immutable camera state that produced the borrowed raster image.
inline bool operator==(const CameraViewRequest &lhs,
                       const CameraViewRequest &rhs) noexcept {
  return lhs.view_id == rhs.view_id && lhs.width == rhs.width &&
         lhs.height == rhs.height &&
         lhs.view_from_render == rhs.view_from_render &&
         lhs.clip_from_view == rhs.clip_from_view &&
         lhs.previous_view_from_render == rhs.previous_view_from_render &&
         lhs.previous_clip_from_view == rhs.previous_clip_from_view &&
         lhs.temporal_jitter_pixels == rhs.temporal_jitter_pixels &&
         lhs.near_plane == rhs.near_plane &&
         lhs.far_plane == rhs.far_plane && lhs.exposure == rhs.exposure &&
         lhs.visibility_mask == rhs.visibility_mask;
}

inline bool operator!=(const CameraViewRequest &lhs,
                       const CameraViewRequest &rhs) noexcept {
  return !(lhs == rhs);
}

struct RenderFrameRequest {
  std::uint32_t version = kRenderFrameContractVersion;
  /// Strictly increasing and never reused per initialized frontend lifetime.
  std::uint64_t frame_id = 0U;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  std::vector<CameraViewRequest> views;
  FrameOutputMask requested_outputs = FrameOutputMask::COLOR;
  /// Exact color attachment encoding. RGBA16_FLOAT is linear scene-referred
  /// HDR; RGBA8_SRGB is display-referred SDR. It applies to every requested
  /// view and must name one of these formats even when COLOR is not requested.
  PixelFormat color_format = PixelFormat::RGBA8_SRGB;
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
/// - MATERIAL_ID RGBA32_UINT stores the exact stable 128-bit RenderAssetId as
///   high-to-low uint32 words in RGBA; background is all zero.
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
ValidateCameraViewRequest(const CameraViewRequest &view,
                          std::size_t index = 0U);
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
