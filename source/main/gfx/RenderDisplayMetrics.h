/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace RoR {

/// Platform-neutral contract between a host window's logical coordinate space
/// and the renderer's backing-pixel viewport.
struct RenderDisplayMetrics
{
    std::uint32_t logical_width = 1;
    std::uint32_t logical_height = 1;
    std::uint32_t backing_width = 1;
    std::uint32_t backing_height = 1;
    float framebuffer_scale_x = 1.0f;
    float framebuffer_scale_y = 1.0f;
    bool valid = false;

    float GetFontRasterScale() const
    {
        return std::max(framebuffer_scale_x, framebuffer_scale_y);
    }
};

inline bool IsFiniteDisplayScale(float value)
{
    // Do not use std::isfinite here. RoR's Release configuration enables
    // fast-math, under which compilers may assume every floating-point value
    // is finite. Inspecting IEEE-754 representation keeps malformed config
    // values fail-safe under AppleClang, GCC, and MSVC optimization modes.
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "display scale requires IEEE-754 binary32");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline float SanitizeRequestedContentScale(float requested_scale)
{
    // OGRE currently advertises 1x and 2x. Keep a bounded range for future
    // Windows/Linux fractional or higher-density backends, while making bad
    // configuration values fall back to the existing 1x behavior.
    return IsFiniteDisplayScale(requested_scale) &&
            requested_scale >= 1.0f &&
            requested_scale <= 4.0f
        ? requested_scale
        : 1.0f;
}

inline bool ShouldRequestHighPixelDensity(float requested_scale)
{
    return SanitizeRequestedContentScale(requested_scale) > 1.0f;
}

inline std::uint32_t LogicalExtentForBacking(
    std::uint32_t backing_extent,
    float requested_scale)
{
    if (backing_extent == 0)
    {
        return 0;
    }

    const float scale = SanitizeRequestedContentScale(requested_scale);
    return static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(backing_extent) /
                  static_cast<double>(scale)));
}

inline RenderDisplayMetrics ResolveRenderDisplayMetrics(
    std::uint32_t backing_width,
    std::uint32_t backing_height,
    std::uint32_t logical_width,
    std::uint32_t logical_height)
{
    RenderDisplayMetrics metrics;
    if (backing_width == 0 || backing_height == 0 ||
        logical_width == 0 || logical_height == 0)
    {
        metrics.backing_width = std::max<std::uint32_t>(backing_width, 1);
        metrics.backing_height = std::max<std::uint32_t>(backing_height, 1);
        metrics.logical_width = metrics.backing_width;
        metrics.logical_height = metrics.backing_height;
        return metrics;
    }

    const double scale_x =
        static_cast<double>(backing_width) /
        static_cast<double>(logical_width);
    const double scale_y =
        static_cast<double>(backing_height) /
        static_cast<double>(logical_height);

    // Dear ImGui can represent non-uniform and fractional framebuffer scales.
    // Reject implausible backend data instead of propagating huge coordinates
    // or divisions by values near zero into UI projection/scissor state.
    if (!(scale_x >= 0.25 && scale_x <= 4.0) ||
        !(scale_y >= 0.25 && scale_y <= 4.0))
    {
        metrics.backing_width = backing_width;
        metrics.backing_height = backing_height;
        metrics.logical_width = backing_width;
        metrics.logical_height = backing_height;
        return metrics;
    }

    metrics.logical_width = logical_width;
    metrics.logical_height = logical_height;
    metrics.backing_width = backing_width;
    metrics.backing_height = backing_height;
    metrics.framebuffer_scale_x = static_cast<float>(scale_x);
    metrics.framebuffer_scale_y = static_cast<float>(scale_y);
    metrics.valid = true;
    return metrics;
}

} // namespace RoR
