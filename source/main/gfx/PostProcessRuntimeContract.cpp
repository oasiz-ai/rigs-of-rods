/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessRuntimeContract.h"

#include <cctype>

namespace RoR
{
namespace
{

std::string NormalizeRendererName(const std::string& renderer_name)
{
    std::string normalized;
    normalized.reserve(renderer_name.size());

    bool pending_space = false;
    for (const char raw_character : renderer_name)
    {
        const unsigned char character =
            static_cast<unsigned char>(raw_character);
        if (std::isspace(character) != 0)
        {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space)
        {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
}

PostProcessLifecycleTransition FailClosed(
    const PostProcessLifecycleState& state) noexcept
{
    PostProcessLifecycleTransition transition;
    transition.next = state;
    transition.next.stage = PostProcessLifecycleStage::FAILED;
    transition.action =
        state.stage == PostProcessLifecycleStage::ATTACHED
        ? PostProcessLifecycleAction::DETACH
        : PostProcessLifecycleAction::NONE;
    return transition;
}

bool HasNonZeroExtent(
    const PostProcessLifecycleEvent& event) noexcept
{
    return event.backing_width != 0U &&
        event.backing_height != 0U;
}

bool IsKnownLifecycleStage(
    PostProcessLifecycleStage stage) noexcept
{
    switch (stage)
    {
    case PostProcessLifecycleStage::INACTIVE:
    case PostProcessLifecycleStage::BYPASSED:
    case PostProcessLifecycleStage::ATTACHED:
    case PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT:
    case PostProcessLifecycleStage::FAILED:
        return true;
    }
    return false;
}

} // namespace

PostProcessBackend ClassifyPostProcessBackend(
    const std::string& renderer_name,
    bool glsl330_supported,
    bool shader_model_4_vertex_supported,
    bool shader_model_4_fragment_supported) noexcept
{
    const std::string normalized =
        NormalizeRendererName(renderer_name);
    if (normalized == "opengl 3+ rendering subsystem" &&
        glsl330_supported)
    {
        return PostProcessBackend::GL3PLUS_GLSL;
    }
    if (normalized == "direct3d11 rendering subsystem" &&
        shader_model_4_vertex_supported &&
        shader_model_4_fragment_supported)
    {
        return PostProcessBackend::D3D11_HLSL;
    }
    return PostProcessBackend::UNSUPPORTED;
}

const char* PostProcessBackendToString(
    PostProcessBackend backend) noexcept
{
    switch (backend)
    {
    case PostProcessBackend::UNSUPPORTED:
        return "unsupported";
    case PostProcessBackend::GL3PLUS_GLSL:
        return "gl3plus_glsl330";
    case PostProcessBackend::D3D11_HLSL:
        return "d3d11_sm4";
    }
    return "unknown";
}

const char* PostProcessPolicyStatusToString(
    PostProcessPolicyStatus status) noexcept
{
    switch (status)
    {
    case PostProcessPolicyStatus::REQUESTED_NONE:
        return "requested_none";
    case PostProcessPolicyStatus::ENABLED:
        return "enabled";
    case PostProcessPolicyStatus::INVALID_MODE:
        return "invalid_mode";
    case PostProcessPolicyStatus::UNSUPPORTED_BACKEND:
        return "unsupported_backend";
    case PostProcessPolicyStatus::PROGRAM_UNAVAILABLE:
        return "program_unavailable";
    case PostProcessPolicyStatus::ZERO_VIEWPORT:
        return "zero_viewport";
    }
    return "unknown";
}

const char* PostProcessLifecycleStageToString(
    PostProcessLifecycleStage stage) noexcept
{
    switch (stage)
    {
    case PostProcessLifecycleStage::INACTIVE:
        return "inactive";
    case PostProcessLifecycleStage::BYPASSED:
        return "bypassed";
    case PostProcessLifecycleStage::ATTACHED:
        return "attached";
    case PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT:
        return "suspended_zero_extent";
    case PostProcessLifecycleStage::FAILED:
        return "failed";
    }
    return "unknown";
}

PostProcessLifecycleTransition ResolvePostProcessLifecycleTransition(
    const PostProcessLifecycleState& state,
    const PostProcessLifecycleEvent& event) noexcept
{
    if (!IsKnownLifecycleStage(state.stage))
    {
        PostProcessLifecycleTransition transition = FailClosed(state);
        transition.action = PostProcessLifecycleAction::DETACH;
        return transition;
    }

    PostProcessLifecycleTransition transition;
    transition.next = state;

    switch (event.type)
    {
    case PostProcessLifecycleEventType::SCENE_READY:
        if (state.stage != PostProcessLifecycleStage::INACTIVE)
        {
            return FailClosed(state);
        }
        transition.next.backing_width = event.backing_width;
        transition.next.backing_height = event.backing_height;
        if (!event.effective_mode_enabled)
        {
            transition.next.stage =
                PostProcessLifecycleStage::BYPASSED;
        }
        else if (!HasNonZeroExtent(event))
        {
            transition.next.stage =
                PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT;
        }
        else
        {
            transition.next.stage =
                PostProcessLifecycleStage::ATTACHED;
            transition.action =
                PostProcessLifecycleAction::ATTACH;
        }
        return transition;

    case PostProcessLifecycleEventType::VIEWPORT_RESIZED:
        transition.next.backing_width = event.backing_width;
        transition.next.backing_height = event.backing_height;
        switch (state.stage)
        {
        case PostProcessLifecycleStage::INACTIVE:
        case PostProcessLifecycleStage::BYPASSED:
        case PostProcessLifecycleStage::FAILED:
            return transition;
        case PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT:
            if (HasNonZeroExtent(event))
            {
                transition.next.stage =
                    PostProcessLifecycleStage::ATTACHED;
                transition.action =
                    PostProcessLifecycleAction::ATTACH;
            }
            return transition;
        case PostProcessLifecycleStage::ATTACHED:
            if (!HasNonZeroExtent(event))
            {
                transition.next.stage =
                    PostProcessLifecycleStage::SUSPENDED_ZERO_EXTENT;
                transition.action =
                    PostProcessLifecycleAction::DETACH;
            }
            else if (
                state.backing_width != event.backing_width ||
                state.backing_height != event.backing_height)
            {
                transition.action =
                    PostProcessLifecycleAction::RECREATE;
            }
            return transition;
        }
        return FailClosed(state);

    case PostProcessLifecycleEventType::SCENE_COMPOSITOR_CHAIN_CHANGED:
        if (state.stage == PostProcessLifecycleStage::ATTACHED)
        {
            transition.action =
                PostProcessLifecycleAction::RECREATE;
        }
        return transition;

    case PostProcessLifecycleEventType::MAIN_WINDOW_READBACK:
        if (state.stage == PostProcessLifecycleStage::ATTACHED)
        {
            transition.action =
                PostProcessLifecycleAction::VERIFY_ATTACHED_LAST;
        }
        return transition;

    case PostProcessLifecycleEventType::ADAPTER_FAILED:
        transition.next.stage =
            PostProcessLifecycleStage::FAILED;
        transition.action = PostProcessLifecycleAction::DETACH;
        return transition;

    case PostProcessLifecycleEventType::SCENE_END:
    case PostProcessLifecycleEventType::SHUTDOWN:
        transition.next.stage =
            PostProcessLifecycleStage::INACTIVE;
        transition.next.backing_width = 0U;
        transition.next.backing_height = 0U;
        if (state.stage == PostProcessLifecycleStage::ATTACHED)
        {
            transition.action =
                PostProcessLifecycleAction::DETACH;
        }
        return transition;
    }

    return FailClosed(state);
}

std::string BoundPostProcessDiagnosticDetail(
    const std::string& detail,
    std::size_t maximum_bytes)
{
    std::string normalized;
    normalized.reserve(
        detail.size() < maximum_bytes
        ? detail.size()
        : maximum_bytes);

    bool pending_space = false;
    for (const char raw_character : detail)
    {
        const unsigned char character =
            static_cast<unsigned char>(raw_character);
        if (std::isspace(character) != 0 ||
            character < 0x20U ||
            character == 0x7fU)
        {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space)
        {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(raw_character);
    }

    if (normalized.size() <= maximum_bytes)
    {
        return normalized;
    }
    if (maximum_bytes < 4U)
    {
        normalized.resize(maximum_bytes);
        return normalized;
    }
    normalized.resize(maximum_bytes - 3U);
    normalized.append("...");
    return normalized;
}

} // namespace RoR
