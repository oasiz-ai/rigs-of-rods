/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-independent post-processing availability policy.

#pragma once

#include <cstdint>

namespace RoR
{

/// User-visible post-processing modes.
///
/// Keep the explicit values stable because runtime configuration will persist
/// them as integers. Unknown values are rejected by ResolvePostProcessPolicy().
enum class PostProcessMode : std::int32_t
{
    NONE = 0,
    V0A_LDR_FXAA = 1,
};

/// Normalized renderer/program pairs supported by the V0A implementation.
///
/// Runtime renderer discovery must translate its native API and shader
/// language into one of these values before consulting the policy. Combining
/// the renderer and language prevents mismatched pairs such as GL3Plus/HLSL
/// from being treated as supported.
enum class PostProcessBackend : std::uint8_t
{
    UNSUPPORTED = 0,
    GL3PLUS_GLSL = 1,
    D3D11_HLSL = 2,
};

/// Stable reason for the requested-to-effective mode decision.
///
/// Failure precedence is invalid mode, unsupported backend, unavailable
/// program, then zero viewport. This makes diagnostics deterministic when
/// several runtime prerequisites are missing at once.
enum class PostProcessPolicyStatus : std::uint8_t
{
    REQUESTED_NONE = 0,
    ENABLED = 1,
    INVALID_MODE = 2,
    UNSUPPORTED_BACKEND = 3,
    PROGRAM_UNAVAILABLE = 4,
    ZERO_VIEWPORT = 5,
};

struct PostProcessPolicyInput
{
    PostProcessMode requested_mode = PostProcessMode::NONE;
    PostProcessBackend backend = PostProcessBackend::UNSUPPORTED;
    bool program_available = false;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
};

struct PostProcessPolicyResult
{
    PostProcessMode requested_mode = PostProcessMode::NONE;
    PostProcessMode effective_mode = PostProcessMode::NONE;
    PostProcessPolicyStatus status =
        PostProcessPolicyStatus::REQUESTED_NONE;
};

bool IsKnownPostProcessMode(PostProcessMode mode) noexcept;
bool IsSupportedPostProcessBackend(PostProcessBackend backend) noexcept;

/// Resolve the effective mode without touching renderer state.
///
/// NONE is unconditional and does not require a valid backend, program, or
/// viewport. V0A_LDR_FXAA fails closed to NONE unless every prerequisite is
/// present. The requested value is preserved in the result, including an
/// unknown enum value, so callers can produce an accurate diagnostic.
PostProcessPolicyResult ResolvePostProcessPolicy(
    const PostProcessPolicyInput& input) noexcept;

} // namespace RoR
