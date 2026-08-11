/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral native presentation policy shared by direct and
/// transport-backed frontend sessions.

#pragma once

#include "RendererFrontend.h"

#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kRendererFrontendPresentationPolicyVersion = 3U;

/// Explicit policy supplied by the native window/presentation owner for each
/// scene. A presented scene selects its sole camera and the exact active
/// surface revision. An offscreen scene uses a zero revision. The policy is
/// independent of whether the scene arrived by direct in-process submission or
/// through the compatibility transport.
struct RendererFrontendPresentationPolicy final {
  std::uint32_t version = kRendererFrontendPresentationPolicyVersion;
  FrameOutputMask requested_outputs = FrameOutputMask::COLOR;
  PixelFormat color_format = PixelFormat::RGBA8_SRGB;
  std::uint64_t presentation_surface_revision = 0U;
  /// Exact drawable extent of the presentation surface named above. When the
  /// guard below is enabled, a camera captured against any other extent is
  /// consumed as retired and can never reach the frontend.
  std::uint32_t presentation_drawable_width = 0U;
  std::uint32_t presentation_drawable_height = 0U;
  bool present = false;
  bool allow_async_compute = false;
  /// Consume the scene without querying capabilities or invoking the
  /// frontend. Used only for a scene already in flight when the native surface
  /// changed.
  bool retire_scene_without_render = false;
  /// Re-check the camera against the current drawable extent. This is required
  /// for presentable live scenes because a surface change may overtake scene
  /// capture.
  bool retire_scene_on_presentation_extent_mismatch = false;
};

[[nodiscard]] ValidationResult ValidateRendererFrontendPresentationPolicy(
    const RendererFrontendPresentationPolicy &policy);

} // namespace RoR::Render
