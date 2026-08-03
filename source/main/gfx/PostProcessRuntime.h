/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed OGRE adapter for the opt-in V0A scene compositor.

#pragma once

#include "PostProcessPolicy.h"
#include "PostProcessRuntimeContract.h"

#include <string>

namespace Ogre
{
class RenderTarget;
class Viewport;
}

namespace RoR
{

/// Owns only the main scene viewport's V0A compositor instance.
///
/// Every public operation is synchronous, main-thread-only, and non-throwing.
/// Resource and renderer errors detach V0A and suppress retries for the
/// remainder of the current scene.
class PostProcessRuntime
{
public:
    void BeginScene(
        Ogre::Viewport* main_viewport,
        Ogre::RenderTarget* main_render_target,
        PostProcessMode requested_mode) noexcept;
    void EndScene() noexcept;
    void OnMainViewportResized() noexcept;
    void MaintainSceneCompositorOrder() noexcept;
    void BeforeMainWindowReadback() noexcept;
    void Shutdown() noexcept;

private:
    void BeginSceneImpl(
        Ogre::Viewport* main_viewport,
        Ogre::RenderTarget* main_render_target,
        PostProcessMode requested_mode);
    PostProcessBackend DetectBackend(
        std::string& renderer_name) const;
    bool EnsureResourcesAvailable(std::string& failure_detail);
    bool ValidateResources(std::string& failure_detail) const;
    bool AttachLast(std::string& failure_detail);
    bool IsAttachedLast(std::string& failure_detail) const;
    bool IsExactMainViewport(std::string& failure_detail) const;
    void DetachNoThrow() noexcept;
    bool ExecuteTransition(
        const PostProcessLifecycleTransition& transition,
        const char* event_name,
        std::string& failure_detail) noexcept;
    void FailClosed(
        const char* event_name,
        const std::string& failure_detail) noexcept;
    void LogDecision(
        const char* event_name,
        const std::string& detail) const noexcept;

    PostProcessLifecycleState m_lifecycle;
    PostProcessMode m_requested_mode = PostProcessMode::NONE;
    PostProcessMode m_effective_mode = PostProcessMode::NONE;
    PostProcessBackend m_backend = PostProcessBackend::UNSUPPORTED;
    PostProcessPolicyStatus m_policy_status =
        PostProcessPolicyStatus::REQUESTED_NONE;
    Ogre::Viewport* m_main_viewport = nullptr;
    Ogre::RenderTarget* m_main_render_target = nullptr;
    bool m_resource_pack_registered = false;
    std::string m_renderer_name;
};

} // namespace RoR
