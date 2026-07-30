/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PostProcessRuntime.h"

#include "Application.h"
#include "ContentManager.h"

#include <OgreCompositor.h>
#include <OgreCompositorChain.h>
#include <OgreCompositorInstance.h>
#include <OgreCompositorManager.h>
#include <OgreException.h>
#include <OgreGpuProgram.h>
#include <OgreGpuProgramManager.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreRenderSystem.h>
#include <OgreRenderTarget.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreTechnique.h>
#include <OgreViewport.h>

#include <array>
#include <cstdint>
#include <exception>

namespace RoR
{
namespace
{

const char POST_PROCESS_RESOURCE_GROUP[] = "PostProcessRG";
const char V0A_COMPOSITOR_NAME[] =
    "RoR/PostProcess/V0A/LdrFxaa";
const char V0A_MATERIAL_NAME[] =
    "RoR/PostProcess/V0A/LdrFxaa";
const char V0A_UNIFIED_VERTEX_PROGRAM[] =
    "RoR/PostProcess/V0A/Vertex";
const char V0A_UNIFIED_FRAGMENT_PROGRAM[] =
    "RoR/PostProcess/V0A/Fragment";

const std::array<const char*, 6U> REQUIRED_RESOURCE_FILES = {{
    "ror_postprocess_v0a.compositor",
    "ror_postprocess_v0a.material",
    "ror_postprocess_v0a.program",
    "ror_postprocess_v0a_d3d11.hlsl",
    "ror_postprocess_v0a_gl3plus.frag",
    "ror_postprocess_v0a_gl3plus.vert",
}};

std::uint32_t ViewportWidth(Ogre::Viewport* viewport) noexcept
{
    return viewport != nullptr
        ? static_cast<std::uint32_t>(viewport->getActualWidth())
        : 0U;
}

std::uint32_t ViewportHeight(Ogre::Viewport* viewport) noexcept
{
    return viewport != nullptr
        ? static_cast<std::uint32_t>(viewport->getActualHeight())
        : 0U;
}

std::string ExceptionDetail(const Ogre::Exception& exception)
{
    return exception.getDescription();
}

std::string ExceptionDetail(const std::exception& exception)
{
    return exception.what();
}

} // namespace

PostProcessBackend PostProcessRuntime::DetectBackend(
    std::string& renderer_name) const
{
    renderer_name.clear();
    if (Ogre::Root::getSingletonPtr() == nullptr ||
        Ogre::Root::getSingleton().getRenderSystem() == nullptr)
    {
        return PostProcessBackend::UNSUPPORTED;
    }

    renderer_name =
        Ogre::Root::getSingleton().getRenderSystem()->getName();
    return ClassifyPostProcessBackend(
        renderer_name,
        Ogre::GpuProgramManager::isSyntaxSupported("glsl330"),
        Ogre::GpuProgramManager::isSyntaxSupported("vs_4_0"),
        Ogre::GpuProgramManager::isSyntaxSupported("ps_4_0"));
}

bool PostProcessRuntime::EnsureResourcesAvailable(
    std::string& failure_detail)
{
    failure_detail.clear();
    try
    {
        if (!m_resource_pack_registered)
        {
            App::GetContentManager()->AddResourcePack(
                ContentManager::ResourcePack::POSTPROCESS);
            m_resource_pack_registered = true;
        }
        return this->ValidateResources(failure_detail);
    }
    catch (const Ogre::Exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (const std::exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (...)
    {
        failure_detail =
            "unknown exception while registering postprocess.zip";
    }
    return false;
}

bool PostProcessRuntime::ValidateResources(
    std::string& failure_detail) const
{
    Ogre::ResourceGroupManager* const resource_groups =
        Ogre::ResourceGroupManager::getSingletonPtr();
    Ogre::GpuProgramManager* const programs =
        Ogre::GpuProgramManager::getSingletonPtr();
    Ogre::MaterialManager* const materials =
        Ogre::MaterialManager::getSingletonPtr();
    Ogre::CompositorManager* const compositors =
        Ogre::CompositorManager::getSingletonPtr();
    if (resource_groups == nullptr ||
        programs == nullptr ||
        materials == nullptr ||
        compositors == nullptr)
    {
        failure_detail =
            "required OGRE resource manager is unavailable";
        return false;
    }

    if (!resource_groups->resourceGroupExists(
            POST_PROCESS_RESOURCE_GROUP))
    {
        failure_detail =
            "dedicated resource group was not created";
        return false;
    }
    for (const char* filename : REQUIRED_RESOURCE_FILES)
    {
        if (!resource_groups->resourceExists(
                POST_PROCESS_RESOURCE_GROUP,
                filename))
        {
            failure_detail =
                std::string("missing resource file ") + filename;
            return false;
        }
    }

    const char* selected_vertex_program = nullptr;
    const char* selected_fragment_program = nullptr;
    switch (m_backend)
    {
    case PostProcessBackend::GL3PLUS_GLSL:
        selected_vertex_program =
            "RoR/PostProcess/V0A/Vertex/GL3Plus";
        selected_fragment_program =
            "RoR/PostProcess/V0A/Fragment/GL3Plus";
        break;
    case PostProcessBackend::D3D11_HLSL:
        selected_vertex_program =
            "RoR/PostProcess/V0A/Vertex/D3D11";
        selected_fragment_program =
            "RoR/PostProcess/V0A/Fragment/D3D11";
        break;
    case PostProcessBackend::UNSUPPORTED:
        failure_detail =
            "resource validation requested for unsupported backend";
        return false;
    }

    const std::array<const char*, 4U> required_programs = {{
        selected_vertex_program,
        selected_fragment_program,
        V0A_UNIFIED_VERTEX_PROGRAM,
        V0A_UNIFIED_FRAGMENT_PROGRAM,
    }};
    for (const char* program_name : required_programs)
    {
        Ogre::GpuProgramPtr program = programs->getByName(
            program_name,
            POST_PROCESS_RESOURCE_GROUP);
        if (!program)
        {
            failure_detail =
                std::string("missing GPU program ") + program_name;
            return false;
        }
        program->load();
        if (!program->isSupported() ||
            program->hasCompileError())
        {
            failure_detail =
                std::string("unsupported or failed GPU program ") +
                program_name;
            return false;
        }
    }

    Ogre::MaterialPtr material = materials->getByName(
        V0A_MATERIAL_NAME,
        POST_PROCESS_RESOURCE_GROUP);
    if (!material)
    {
        failure_detail = "V0A material was not registered";
        return false;
    }
    material->load();
    if (material->getSupportedTechniques().empty())
    {
        failure_detail =
            "V0A material has no supported technique";
        return false;
    }
    Ogre::Technique* const technique = material->getBestTechnique();
    if (technique == nullptr || technique->getNumPasses() != 1U)
    {
        failure_detail =
            "V0A material does not expose one supported pass";
        return false;
    }
    Ogre::Pass* const pass = technique->getPass(0U);
    if (pass == nullptr ||
        !pass->hasVertexProgram() ||
        !pass->hasFragmentProgram() ||
        pass->getVertexProgramName() != V0A_UNIFIED_VERTEX_PROGRAM ||
        pass->getFragmentProgramName() !=
            V0A_UNIFIED_FRAGMENT_PROGRAM ||
        !pass->getVertexProgram()->isSupported() ||
        !pass->getFragmentProgram()->isSupported())
    {
        failure_detail =
            "V0A material did not bind the supported unified programs";
        return false;
    }

    Ogre::CompositorPtr compositor = compositors->getByName(
        V0A_COMPOSITOR_NAME,
        POST_PROCESS_RESOURCE_GROUP);
    if (!compositor)
    {
        failure_detail = "V0A compositor was not registered";
        return false;
    }
    compositor->load();
    if (compositor->getNumSupportedTechniques() != 1U)
    {
        failure_detail =
            "V0A compositor does not expose one supported technique";
        return false;
    }
    return true;
}

bool PostProcessRuntime::IsExactMainViewport(
    std::string& failure_detail) const
{
    if (m_main_viewport == nullptr ||
        m_main_render_target == nullptr)
    {
        failure_detail =
            "main viewport or render target is null";
        return false;
    }
    if (m_main_viewport->getTarget() != m_main_render_target)
    {
        failure_detail =
            "viewport is not owned by the main render target";
        return false;
    }
    if (m_main_render_target->getNumViewports() == 0U ||
        m_main_render_target->getViewport(0U) != m_main_viewport)
    {
        failure_detail =
            "viewport is not the main render target viewport zero";
        return false;
    }
    if (m_main_viewport->getCamera() == nullptr)
    {
        failure_detail = "main viewport has no camera";
        return false;
    }
    return true;
}

bool PostProcessRuntime::AttachLast(std::string& failure_detail)
{
    failure_detail.clear();
    try
    {
        if (!this->IsExactMainViewport(failure_detail) ||
            !this->ValidateResources(failure_detail))
        {
            return false;
        }

        Ogre::CompositorManager& manager =
            Ogre::CompositorManager::getSingleton();
        if (manager.hasCompositorChain(m_main_viewport))
        {
            Ogre::CompositorChain* const chain =
                manager.getCompositorChain(m_main_viewport);
            if (chain != nullptr &&
                chain->getCompositor(V0A_COMPOSITOR_NAME) != nullptr)
            {
                manager.setCompositorEnabled(
                    m_main_viewport,
                    V0A_COMPOSITOR_NAME,
                    false);
                manager.removeCompositor(
                    m_main_viewport,
                    V0A_COMPOSITOR_NAME);
            }
        }

        const bool overlays_before =
            m_main_viewport->getOverlaysEnabled();
        Ogre::CompositorInstance* const instance =
            manager.addCompositor(
                m_main_viewport,
                V0A_COMPOSITOR_NAME,
                -1);
        if (instance == nullptr)
        {
            failure_detail =
                "OGRE returned no V0A compositor instance";
            return false;
        }
        manager.setCompositorEnabled(
            m_main_viewport,
            V0A_COMPOSITOR_NAME,
            true);
        if (m_main_viewport->getOverlaysEnabled() != overlays_before)
        {
            failure_detail =
                "V0A changed the main viewport overlay flag";
            return false;
        }
        return this->IsAttachedLast(failure_detail);
    }
    catch (const Ogre::Exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (const std::exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (...)
    {
        failure_detail =
            "unknown exception while attaching V0A";
    }
    return false;
}

bool PostProcessRuntime::IsAttachedLast(
    std::string& failure_detail) const
{
    failure_detail.clear();
    try
    {
        if (!this->IsExactMainViewport(failure_detail))
        {
            return false;
        }
        Ogre::CompositorManager* const manager =
            Ogre::CompositorManager::getSingletonPtr();
        if (manager == nullptr ||
            !manager->hasCompositorChain(m_main_viewport))
        {
            failure_detail =
                "main viewport has no compositor chain";
            return false;
        }
        Ogre::CompositorChain* const chain =
            manager->getCompositorChain(m_main_viewport);
        Ogre::CompositorInstance* const instance =
            chain != nullptr
            ? chain->getCompositor(V0A_COMPOSITOR_NAME)
            : nullptr;
        if (instance == nullptr || !instance->getEnabled())
        {
            failure_detail =
                "V0A compositor is absent or disabled";
            return false;
        }
        const Ogre::CompositorChain::Instances& instances =
            chain->getCompositorInstances();
        if (instances.empty() || instances.back() != instance)
        {
            failure_detail =
                "V0A compositor is not last in the scene chain";
            return false;
        }
        return true;
    }
    catch (const Ogre::Exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (const std::exception& exception)
    {
        failure_detail = ExceptionDetail(exception);
    }
    catch (...)
    {
        failure_detail =
            "unknown exception while verifying V0A order";
    }
    return false;
}

void PostProcessRuntime::DetachNoThrow() noexcept
{
    try
    {
        Ogre::CompositorManager* const manager =
            Ogre::CompositorManager::getSingletonPtr();
        if (manager == nullptr ||
            m_main_viewport == nullptr ||
            !manager->hasCompositorChain(m_main_viewport))
        {
            return;
        }
        Ogre::CompositorChain* const chain =
            manager->getCompositorChain(m_main_viewport);
        if (chain == nullptr ||
            chain->getCompositor(V0A_COMPOSITOR_NAME) == nullptr)
        {
            return;
        }
        manager->setCompositorEnabled(
            m_main_viewport,
            V0A_COMPOSITOR_NAME,
            false);
        manager->removeCompositor(
            m_main_viewport,
            V0A_COMPOSITOR_NAME);
    }
    catch (...)
    {
        // Shutdown and failure paths are best effort and must never throw.
    }
}

bool PostProcessRuntime::ExecuteTransition(
    const PostProcessLifecycleTransition& transition,
    const char* event_name,
    std::string& failure_detail) noexcept
{
    m_lifecycle = transition.next;
    switch (transition.action)
    {
    case PostProcessLifecycleAction::NONE:
        return true;
    case PostProcessLifecycleAction::ATTACH:
        if (this->AttachLast(failure_detail))
        {
            m_effective_mode =
                PostProcessMode::V0A_LDR_FXAA;
            return true;
        }
        break;
    case PostProcessLifecycleAction::DETACH:
        this->DetachNoThrow();
        m_effective_mode = PostProcessMode::NONE;
        return true;
    case PostProcessLifecycleAction::RECREATE:
        this->DetachNoThrow();
        if (this->AttachLast(failure_detail))
        {
            m_effective_mode =
                PostProcessMode::V0A_LDR_FXAA;
            return true;
        }
        break;
    case PostProcessLifecycleAction::VERIFY_ATTACHED_LAST:
        if (this->IsAttachedLast(failure_detail))
        {
            return true;
        }
        break;
    }

    this->FailClosed(event_name, failure_detail);
    return false;
}

void PostProcessRuntime::FailClosed(
    const char* event_name,
    const std::string& failure_detail) noexcept
{
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::ADAPTER_FAILED,
                false,
                m_lifecycle.backing_width,
                m_lifecycle.backing_height});
    m_lifecycle = transition.next;
    this->DetachNoThrow();
    m_effective_mode = PostProcessMode::NONE;
    m_policy_status =
        PostProcessPolicyStatus::PROGRAM_UNAVAILABLE;
    this->LogDecision(event_name, failure_detail);
}

void PostProcessRuntime::LogDecision(
    const char* event_name,
    const std::string& detail) const noexcept
{
    try
    {
        const std::string safe_event =
            BoundPostProcessDiagnosticDetail(
                event_name != nullptr ? event_name : "unknown",
                48U);
        const std::string safe_renderer =
            BoundPostProcessDiagnosticDetail(m_renderer_name, 96U);
        const std::string safe_detail =
            BoundPostProcessDiagnosticDetail(detail, 160U);
        RoR::LogFormat(
            "[RoR|PostProcess] event=%s requested=%d effective=%d "
            "backend=%s status=%s stage=%s backing=%ux%u "
            "renderer=%s detail=%s",
            safe_event.c_str(),
            static_cast<int>(m_requested_mode),
            static_cast<int>(m_effective_mode),
            PostProcessBackendToString(m_backend),
            PostProcessPolicyStatusToString(m_policy_status),
            PostProcessLifecycleStageToString(m_lifecycle.stage),
            static_cast<unsigned int>(m_lifecycle.backing_width),
            static_cast<unsigned int>(m_lifecycle.backing_height),
            safe_renderer.empty() ? "none" : safe_renderer.c_str(),
            safe_detail.empty() ? "none" : safe_detail.c_str());
    }
    catch (...)
    {
        // Diagnostics must never turn an optional effect into a crash.
    }
}

void PostProcessRuntime::BeginScene(
    Ogre::Viewport* main_viewport,
    Ogre::RenderTarget* main_render_target,
    PostProcessMode requested_mode) noexcept
{
    try
    {
        this->BeginSceneImpl(
            main_viewport,
            main_render_target,
            requested_mode);
    }
    catch (const Ogre::Exception& exception)
    {
        this->FailClosed(
            "scene_ready",
            ExceptionDetail(exception));
    }
    catch (const std::exception& exception)
    {
        this->FailClosed(
            "scene_ready",
            ExceptionDetail(exception));
    }
    catch (...)
    {
        this->FailClosed(
            "scene_ready",
            "unknown exception while starting V0A");
    }
}

void PostProcessRuntime::BeginSceneImpl(
    Ogre::Viewport* main_viewport,
    Ogre::RenderTarget* main_render_target,
    PostProcessMode requested_mode)
{
    if (m_lifecycle.stage !=
        PostProcessLifecycleStage::INACTIVE)
    {
        this->EndScene();
    }

    m_main_viewport = main_viewport;
    m_main_render_target = main_render_target;
    m_requested_mode = requested_mode;
    m_effective_mode = PostProcessMode::NONE;
    m_backend = this->DetectBackend(m_renderer_name);

    const std::uint32_t width =
        ViewportWidth(m_main_viewport);
    const std::uint32_t height =
        ViewportHeight(m_main_viewport);
    bool program_available = false;
    std::string detail;

    if (IsKnownPostProcessMode(m_requested_mode) &&
        m_requested_mode != PostProcessMode::NONE &&
        IsSupportedPostProcessBackend(m_backend))
    {
        program_available =
            this->EnsureResourcesAvailable(detail);
    }

    const PostProcessPolicyResult policy =
        ResolvePostProcessPolicy(PostProcessPolicyInput{
            m_requested_mode,
            m_backend,
            program_available,
            width,
            height});
    m_policy_status = policy.status;

    const bool lifecycle_enabled =
        policy.status == PostProcessPolicyStatus::ENABLED ||
        policy.status == PostProcessPolicyStatus::ZERO_VIEWPORT;
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::SCENE_READY,
                lifecycle_enabled,
                width,
                height});
    if (this->ExecuteTransition(
            transition,
            "scene_ready",
            detail))
    {
        this->LogDecision("scene_ready", detail);
    }
}

void PostProcessRuntime::EndScene() noexcept
{
    std::string detail;
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::SCENE_END,
                false,
                0U,
                0U});
    const bool was_requested =
        m_requested_mode != PostProcessMode::NONE;
    if (this->ExecuteTransition(
            transition,
            "scene_end",
            detail) &&
        was_requested)
    {
        this->LogDecision("scene_end", detail);
    }
    m_main_viewport = nullptr;
    m_main_render_target = nullptr;
    m_requested_mode = PostProcessMode::NONE;
    m_effective_mode = PostProcessMode::NONE;
    m_backend = PostProcessBackend::UNSUPPORTED;
    m_policy_status =
        PostProcessPolicyStatus::REQUESTED_NONE;
    m_renderer_name.clear();
}

void PostProcessRuntime::OnMainViewportResized() noexcept
{
    const std::uint32_t width =
        ViewportWidth(m_main_viewport);
    const std::uint32_t height =
        ViewportHeight(m_main_viewport);
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::VIEWPORT_RESIZED,
                false,
                width,
                height});
    if (transition.action == PostProcessLifecycleAction::NONE)
    {
        m_lifecycle = transition.next;
        return;
    }

    m_policy_status =
        width != 0U && height != 0U
        ? PostProcessPolicyStatus::ENABLED
        : PostProcessPolicyStatus::ZERO_VIEWPORT;
    std::string detail;
    if (this->ExecuteTransition(
            transition,
            "viewport_resize",
            detail))
    {
        this->LogDecision("viewport_resize", detail);
    }
}

void PostProcessRuntime::MaintainSceneCompositorOrder() noexcept
{
    if (m_lifecycle.stage !=
        PostProcessLifecycleStage::ATTACHED)
    {
        return;
    }
    std::string detail;
    if (this->IsAttachedLast(detail))
    {
        return;
    }

    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::
                    SCENE_COMPOSITOR_CHAIN_CHANGED,
                true,
                m_lifecycle.backing_width,
                m_lifecycle.backing_height});
    std::string changed_detail;
    changed_detail.swap(detail);
    if (this->ExecuteTransition(
            transition,
            "chain_reappend",
            detail))
    {
        this->LogDecision(
            "chain_reappend",
            changed_detail);
    }
}

void PostProcessRuntime::BeforeMainWindowReadback() noexcept
{
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::MAIN_WINDOW_READBACK,
                false,
                m_lifecycle.backing_width,
                m_lifecycle.backing_height});
    if (transition.action ==
        PostProcessLifecycleAction::NONE)
    {
        m_lifecycle = transition.next;
        return;
    }

    std::string detail;
    this->ExecuteTransition(
        transition,
        "main_window_readback",
        detail);
}

void PostProcessRuntime::Shutdown() noexcept
{
    std::string detail;
    const PostProcessLifecycleTransition transition =
        ResolvePostProcessLifecycleTransition(
            m_lifecycle,
            PostProcessLifecycleEvent{
                PostProcessLifecycleEventType::SHUTDOWN,
                false,
                0U,
                0U});
    this->ExecuteTransition(
        transition,
        "shutdown",
        detail);
    m_main_viewport = nullptr;
    m_main_render_target = nullptr;
}

} // namespace RoR
