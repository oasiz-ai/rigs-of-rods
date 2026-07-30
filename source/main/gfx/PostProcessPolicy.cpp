/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessPolicy.h"

namespace RoR
{

bool IsKnownPostProcessMode(PostProcessMode mode) noexcept
{
    switch (mode)
    {
    case PostProcessMode::NONE:
    case PostProcessMode::V0A_LDR_FXAA:
        return true;
    }
    return false;
}

bool IsSupportedPostProcessBackend(PostProcessBackend backend) noexcept
{
    switch (backend)
    {
    case PostProcessBackend::GL3PLUS_GLSL:
    case PostProcessBackend::D3D11_HLSL:
        return true;
    case PostProcessBackend::UNSUPPORTED:
        return false;
    }
    return false;
}

PostProcessPolicyResult ResolvePostProcessPolicy(
    const PostProcessPolicyInput& input) noexcept
{
    PostProcessPolicyResult result;
    result.requested_mode = input.requested_mode;

    if (!IsKnownPostProcessMode(input.requested_mode))
    {
        result.status = PostProcessPolicyStatus::INVALID_MODE;
        return result;
    }

    if (input.requested_mode == PostProcessMode::NONE)
    {
        result.status = PostProcessPolicyStatus::REQUESTED_NONE;
        return result;
    }

    if (!IsSupportedPostProcessBackend(input.backend))
    {
        result.status = PostProcessPolicyStatus::UNSUPPORTED_BACKEND;
        return result;
    }

    if (!input.program_available)
    {
        result.status = PostProcessPolicyStatus::PROGRAM_UNAVAILABLE;
        return result;
    }

    if (input.viewport_width == 0 || input.viewport_height == 0)
    {
        result.status = PostProcessPolicyStatus::ZERO_VIEWPORT;
        return result;
    }

    result.effective_mode = PostProcessMode::V0A_LDR_FXAA;
    result.status = PostProcessPolicyStatus::ENABLED;
    return result;
}

} // namespace RoR
