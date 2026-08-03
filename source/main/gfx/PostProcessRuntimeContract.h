/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free V0A renderer classification and lifecycle contract.

#pragma once

#include "PostProcessPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace RoR
{

/// Classify only the exact renderer/program pairs implemented by V0A.
///
/// Renderer-name matching is case-insensitive and whitespace-normalized, but
/// deliberately rejects partial names and future renderers. Shader syntax
/// support is a separate mandatory gate so a renderer name cannot enable an
/// unavailable program pair.
PostProcessBackend ClassifyPostProcessBackend(
    const std::string& renderer_name,
    bool glsl330_supported,
    bool shader_model_4_vertex_supported,
    bool shader_model_4_fragment_supported) noexcept;

const char* PostProcessBackendToString(
    PostProcessBackend backend) noexcept;
const char* PostProcessPolicyStatusToString(
    PostProcessPolicyStatus status) noexcept;

enum class PostProcessLifecycleStage : std::uint8_t
{
    INACTIVE = 0,
    BYPASSED = 1,
    ATTACHED = 2,
    SUSPENDED_ZERO_EXTENT = 3,
    FAILED = 4,
};

enum class PostProcessLifecycleEventType : std::uint8_t
{
    SCENE_READY = 0,
    VIEWPORT_RESIZED = 1,
    SCENE_COMPOSITOR_CHAIN_CHANGED = 2,
    MAIN_WINDOW_READBACK = 3,
    ADAPTER_FAILED = 4,
    SCENE_END = 5,
    SHUTDOWN = 6,
};

enum class PostProcessLifecycleAction : std::uint8_t
{
    NONE = 0,
    ATTACH = 1,
    DETACH = 2,
    RECREATE = 3,
    VERIFY_ATTACHED_LAST = 4,
};

const char* PostProcessLifecycleStageToString(
    PostProcessLifecycleStage stage) noexcept;

struct PostProcessLifecycleState
{
    PostProcessLifecycleStage stage =
        PostProcessLifecycleStage::INACTIVE;
    std::uint32_t backing_width = 0;
    std::uint32_t backing_height = 0;
};

struct PostProcessLifecycleEvent
{
    PostProcessLifecycleEventType type =
        PostProcessLifecycleEventType::SCENE_READY;
    bool effective_mode_enabled = false;
    std::uint32_t backing_width = 0;
    std::uint32_t backing_height = 0;
};

struct PostProcessLifecycleTransition
{
    PostProcessLifecycleState next;
    PostProcessLifecycleAction action =
        PostProcessLifecycleAction::NONE;
};

/// Resolve one synchronous runtime transition without renderer dependencies.
///
/// ATTACH and RECREATE optimistically enter ATTACHED because the adapter
/// executes the action synchronously. The adapter must immediately submit an
/// ADAPTER_FAILED event if the action fails; that transition always requests a
/// best-effort detach and suppresses retries until the next scene.
PostProcessLifecycleTransition ResolvePostProcessLifecycleTransition(
    const PostProcessLifecycleState& state,
    const PostProcessLifecycleEvent& event) noexcept;

/// Make arbitrary renderer exception text safe for one bounded log line.
std::string BoundPostProcessDiagnosticDetail(
    const std::string& detail,
    std::size_t maximum_bytes = 160U);

} // namespace RoR
