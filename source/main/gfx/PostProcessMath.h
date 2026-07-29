/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace RoR {

/// Dependency-free CPU oracle for the version-1 LDR post-processing profile.
///
/// The renderer implementation must perform the same operations in the same
/// order. Values are normalized compositor samples, not an HDR or PBR color
/// space. V1 will introduce an explicitly linear HDR boundary.
struct PostProcessColor
{
    PostProcessColor()
        : red(0.0)
        , green(0.0)
        , blue(0.0)
    {
    }

    PostProcessColor(double red_value, double green_value, double blue_value)
        : red(red_value)
        , green(green_value)
        , blue(blue_value)
    {
    }

    double red;
    double green;
    double blue;
};

struct PostProcessConfig
{
    double exposure = 1.08;
    double contrast = 1.04;
    double saturation = 1.03;
    double bloom_threshold = 0.72;
    double bloom_soft_knee = 0.18;
    double bloom_strength = 0.08;
    double fxaa_edge_threshold = 1.0 / 8.0;
    double fxaa_edge_threshold_min = 1.0 / 24.0;
    double fxaa_blend_limit = 0.75;
};

struct FxaaNeighborhood
{
    PostProcessColor center;
    PostProcessColor north;
    PostProcessColor south;
    PostProcessColor east;
    PostProcessColor west;
};

namespace PostProcessDetail {

inline bool IsFinite(double value)
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "post-processing oracle requires IEEE-754 binary64");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

inline double Abs(double value)
{
    return value < 0.0 ? -value : value;
}

inline double Clamp(double value, double minimum, double maximum)
{
    const double bounded = std::max(minimum, std::min(maximum, value));
    return bounded == 0.0 ? 0.0 : bounded;
}

inline double Clamp01(double value)
{
    return Clamp(value, 0.0, 1.0);
}

inline bool IsFiniteColor(const PostProcessColor& color)
{
    return IsFinite(color.red) &&
        IsFinite(color.green) &&
        IsFinite(color.blue);
}

inline bool IsNormalizedColor(const PostProcessColor& color)
{
    return IsFiniteColor(color) &&
        color.red >= 0.0 && color.red <= 1.0 &&
        color.green >= 0.0 && color.green <= 1.0 &&
        color.blue >= 0.0 && color.blue <= 1.0;
}

inline double Luma(const PostProcessColor& color)
{
    // Rec.709 coefficients. V0 applies them to normalized LDR samples.
    return color.red * 0.2126 +
        color.green * 0.7152 +
        color.blue * 0.0722;
}

inline PostProcessColor Mix(
    const PostProcessColor& first,
    const PostProcessColor& second,
    double amount)
{
    PostProcessColor result;
    result.red = first.red + (second.red - first.red) * amount;
    result.green = first.green + (second.green - first.green) * amount;
    result.blue = first.blue + (second.blue - first.blue) * amount;
    return result;
}

inline bool ValidateConfig(const PostProcessConfig& config)
{
    return
        IsFinite(config.exposure) &&
        config.exposure >= 0.25 &&
        config.exposure <= 4.0 &&
        IsFinite(config.contrast) &&
        config.contrast >= 0.5 &&
        config.contrast <= 1.5 &&
        IsFinite(config.saturation) &&
        config.saturation >= 0.0 &&
        config.saturation <= 2.0 &&
        IsFinite(config.bloom_threshold) &&
        config.bloom_threshold >= 0.0 &&
        config.bloom_threshold <= 1.0 &&
        IsFinite(config.bloom_soft_knee) &&
        config.bloom_soft_knee >= 1.0 / 1024.0 &&
        config.bloom_soft_knee <= 1.0 &&
        IsFinite(config.bloom_strength) &&
        config.bloom_strength >= 0.0 &&
        config.bloom_strength <= 0.5 &&
        IsFinite(config.fxaa_edge_threshold) &&
        config.fxaa_edge_threshold >= 1.0 / 64.0 &&
        config.fxaa_edge_threshold <= 1.0 / 3.0 &&
        IsFinite(config.fxaa_edge_threshold_min) &&
        config.fxaa_edge_threshold_min >= 1.0 / 256.0 &&
        config.fxaa_edge_threshold_min <= 1.0 / 8.0 &&
        IsFinite(config.fxaa_blend_limit) &&
        config.fxaa_blend_limit >= 0.0 &&
        config.fxaa_blend_limit <= 7.0 / 8.0;
}

} // namespace PostProcessDetail

inline bool IsValidPostProcessConfig(const PostProcessConfig& config)
{
    return PostProcessDetail::ValidateConfig(config);
}

/// Apply the fixed V0 exposure, saturation, contrast, and highlight shoulder.
///
/// Failure is transactional: `output` is not modified for malformed config or
/// input. The rational shoulder maps 1.0 to 1.0 without platform libm calls.
inline bool ApplyPostProcessColorCurve(
    const PostProcessColor& input,
    const PostProcessConfig& config,
    PostProcessColor* output)
{
    if (output == nullptr ||
        !PostProcessDetail::ValidateConfig(config) ||
        !PostProcessDetail::IsNormalizedColor(input))
    {
        return false;
    }

    PostProcessColor exposed;
    exposed.red = input.red * config.exposure;
    exposed.green = input.green * config.exposure;
    exposed.blue = input.blue * config.exposure;
    if (!PostProcessDetail::IsFiniteColor(exposed))
    {
        return false;
    }

    const double luminance = PostProcessDetail::Luma(exposed);
    PostProcessColor saturated;
    saturated.red =
        luminance + (exposed.red - luminance) * config.saturation;
    saturated.green =
        luminance + (exposed.green - luminance) * config.saturation;
    saturated.blue =
        luminance + (exposed.blue - luminance) * config.saturation;

    PostProcessColor contrasted;
    contrasted.red =
        (saturated.red - 0.5) * config.contrast + 0.5;
    contrasted.green =
        (saturated.green - 0.5) * config.contrast + 0.5;
    contrasted.blue =
        (saturated.blue - 0.5) * config.contrast + 0.5;

    // Restrained rational highlight shoulder, normalized so 1 maps to 1.
    const double shoulder = 0.12;
    const double shoulder_scale = 1.0 + shoulder;
    PostProcessColor resolved;
    const double red = std::max(0.0, contrasted.red);
    const double green = std::max(0.0, contrasted.green);
    const double blue = std::max(0.0, contrasted.blue);
    resolved.red = PostProcessDetail::Clamp01(
        shoulder_scale * red / (1.0 + shoulder * red));
    resolved.green = PostProcessDetail::Clamp01(
        shoulder_scale * green / (1.0 + shoulder * green));
    resolved.blue = PostProcessDetail::Clamp01(
        shoulder_scale * blue / (1.0 + shoulder * blue));
    if (!PostProcessDetail::IsNormalizedColor(resolved))
    {
        return false;
    }

    *output = resolved;
    return true;
}

/// Extract the bright contribution before the half-resolution blur.
inline bool ExtractPostProcessBloom(
    const PostProcessColor& input,
    const PostProcessConfig& config,
    PostProcessColor* output)
{
    if (output == nullptr ||
        !PostProcessDetail::ValidateConfig(config) ||
        !PostProcessDetail::IsNormalizedColor(input))
    {
        return false;
    }

    const double brightness = std::max(
        input.red,
        std::max(input.green, input.blue));
    const double knee = config.bloom_soft_knee;
    const double soft_distance = PostProcessDetail::Clamp(
        brightness - config.bloom_threshold + knee,
        0.0,
        2.0 * knee);
    const double soft_contribution =
        (soft_distance * soft_distance) / (4.0 * knee);
    const double hard_contribution = std::max(
        brightness - config.bloom_threshold,
        0.0);
    const double contribution = std::max(
        soft_contribution,
        hard_contribution);
    const double weight = brightness > 1.0 / 65536.0
        ? PostProcessDetail::Clamp01(contribution / brightness)
        : 0.0;

    PostProcessColor resolved;
    resolved.red = input.red * weight;
    resolved.green = input.green * weight;
    resolved.blue = input.blue * weight;
    if (!PostProcessDetail::IsNormalizedColor(resolved))
    {
        return false;
    }
    *output = resolved;
    return true;
}

/// Combine the scene and already blurred bloom texture, then apply the V0
/// display curve. The bloom input remains bounded to avoid emissive clipping.
inline bool ComposePostProcessBloom(
    const PostProcessColor& scene,
    const PostProcessColor& blurred_bloom,
    const PostProcessConfig& config,
    PostProcessColor* output)
{
    if (output == nullptr ||
        !PostProcessDetail::ValidateConfig(config) ||
        !PostProcessDetail::IsNormalizedColor(scene) ||
        !PostProcessDetail::IsNormalizedColor(blurred_bloom))
    {
        return false;
    }

    PostProcessColor combined;
    combined.red = PostProcessDetail::Clamp01(
        scene.red + blurred_bloom.red * config.bloom_strength);
    combined.green = PostProcessDetail::Clamp01(
        scene.green + blurred_bloom.green * config.bloom_strength);
    combined.blue = PostProcessDetail::Clamp01(
        scene.blue + blurred_bloom.blue * config.bloom_strength);
    return ApplyPostProcessColorCurve(combined, config, output);
}

/// Resolve the bounded scene-only edge blend used by the V0 shader profile.
///
/// This is the CPU oracle for the FXAA edge decision and blend, not a UI pass:
/// the runtime compositor must execute before native-resolution overlays.
inline bool ResolvePostProcessFxaa(
    const FxaaNeighborhood& samples,
    const PostProcessConfig& config,
    PostProcessColor* output)
{
    if (output == nullptr ||
        !PostProcessDetail::ValidateConfig(config) ||
        !PostProcessDetail::IsNormalizedColor(samples.center) ||
        !PostProcessDetail::IsNormalizedColor(samples.north) ||
        !PostProcessDetail::IsNormalizedColor(samples.south) ||
        !PostProcessDetail::IsNormalizedColor(samples.east) ||
        !PostProcessDetail::IsNormalizedColor(samples.west))
    {
        return false;
    }

    const double center_luma = PostProcessDetail::Luma(samples.center);
    const double north_luma = PostProcessDetail::Luma(samples.north);
    const double south_luma = PostProcessDetail::Luma(samples.south);
    const double east_luma = PostProcessDetail::Luma(samples.east);
    const double west_luma = PostProcessDetail::Luma(samples.west);
    const double minimum_luma = std::min(
        center_luma,
        std::min(
            std::min(north_luma, south_luma),
            std::min(east_luma, west_luma)));
    const double maximum_luma = std::max(
        center_luma,
        std::max(
            std::max(north_luma, south_luma),
            std::max(east_luma, west_luma)));
    const double luma_range = maximum_luma - minimum_luma;
    const double edge_threshold = std::max(
        config.fxaa_edge_threshold_min,
        maximum_luma * config.fxaa_edge_threshold);
    if (luma_range <= edge_threshold)
    {
        *output = samples.center;
        return true;
    }

    const double north_south_gradient = PostProcessDetail::Abs(
        north_luma + south_luma - 2.0 * center_luma);
    const double east_west_gradient = PostProcessDetail::Abs(
        east_luma + west_luma - 2.0 * center_luma);
    PostProcessColor neighbor_average;
    if (north_south_gradient >= east_west_gradient)
    {
        neighbor_average.red =
            (samples.north.red + samples.south.red) * 0.5;
        neighbor_average.green =
            (samples.north.green + samples.south.green) * 0.5;
        neighbor_average.blue =
            (samples.north.blue + samples.south.blue) * 0.5;
    }
    else
    {
        neighbor_average.red =
            (samples.east.red + samples.west.red) * 0.5;
        neighbor_average.green =
            (samples.east.green + samples.west.green) * 0.5;
        neighbor_average.blue =
            (samples.east.blue + samples.west.blue) * 0.5;
    }

    const double blend = PostProcessDetail::Clamp(
        (luma_range - edge_threshold) / luma_range,
        0.0,
        config.fxaa_blend_limit);
    PostProcessColor resolved = PostProcessDetail::Mix(
        samples.center,
        neighbor_average,
        blend);
    resolved.red = PostProcessDetail::Clamp01(resolved.red);
    resolved.green = PostProcessDetail::Clamp01(resolved.green);
    resolved.blue = PostProcessDetail::Clamp01(resolved.blue);
    if (!PostProcessDetail::IsNormalizedColor(resolved))
    {
        return false;
    }
    *output = resolved;
    return true;
}

} // namespace RoR
