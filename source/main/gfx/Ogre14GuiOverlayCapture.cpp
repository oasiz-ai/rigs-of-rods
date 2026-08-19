/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GuiOverlayCapture.h"

#include "AppContext.h"
#include "Application.h"
#include "GUIManager.h"
#include "GfxScene.h"
#include "OgreImGui.h"

#include <fmt/format.h>
#include <imgui.h>
#include <Ogre.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>
#include <vector>

namespace {

std::atomic<std::uint64_t> g_gui_capture_resource_sequence{1U};

class GuiOverlayDrawDataHasher
{
public:
    void AddBytes(const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0U; index < size; ++index)
        {
            m_hash ^= static_cast<std::uint64_t>(bytes[index]);
            m_hash *= kPrime;
        }
    }

    void AddU32(std::uint32_t value) noexcept
    {
        AddBytes(&value, sizeof(value));
    }

    void AddU64(std::uint64_t value) noexcept
    {
        AddBytes(&value, sizeof(value));
    }

    void AddFloat(float value) noexcept
    {
        if (value == 0.0F)
        {
            value = 0.0F;
        }
        AddBytes(&value, sizeof(value));
    }

    std::uint64_t value() const noexcept { return m_hash; }

private:
    static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    static constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t m_hash = kOffsetBasis;
};

// FNV-1a-64 over the complete built ImDrawData: display metrics, every
// vertex/index byte, and per-command clip rect, texture identity, and element
// count. Equal hashes with equal target extents therefore identify equal GUI
// pixels.
std::uint64_t HashImGuiDrawData(
    const ImDrawData& draw_data,
    std::uint32_t target_width,
    std::uint32_t target_height) noexcept
{
    GuiOverlayDrawDataHasher hasher;
    hasher.AddU32(target_width);
    hasher.AddU32(target_height);
    hasher.AddFloat(draw_data.DisplaySize.x);
    hasher.AddFloat(draw_data.DisplaySize.y);
    hasher.AddFloat(draw_data.DisplayPos.x);
    hasher.AddFloat(draw_data.DisplayPos.y);
    hasher.AddFloat(draw_data.FramebufferScale.x);
    hasher.AddFloat(draw_data.FramebufferScale.y);
    hasher.AddU32(static_cast<std::uint32_t>(draw_data.CmdListsCount));
    for (int list_index = 0; list_index < draw_data.CmdListsCount;
         ++list_index)
    {
        const ImDrawList* draw_list = draw_data.CmdLists[list_index];
        hasher.AddU32(static_cast<std::uint32_t>(draw_list->VtxBuffer.Size));
        hasher.AddBytes(draw_list->VtxBuffer.Data,
                        static_cast<std::size_t>(
                            draw_list->VtxBuffer.size_in_bytes()));
        hasher.AddU32(static_cast<std::uint32_t>(draw_list->IdxBuffer.Size));
        hasher.AddBytes(draw_list->IdxBuffer.Data,
                        static_cast<std::size_t>(
                            draw_list->IdxBuffer.size_in_bytes()));
        hasher.AddU32(static_cast<std::uint32_t>(draw_list->CmdBuffer.Size));
        for (int command_index = 0;
             command_index < draw_list->CmdBuffer.Size; ++command_index)
        {
            const ImDrawCmd& command = draw_list->CmdBuffer[command_index];
            hasher.AddFloat(command.ClipRect.x);
            hasher.AddFloat(command.ClipRect.y);
            hasher.AddFloat(command.ClipRect.z);
            hasher.AddFloat(command.ClipRect.w);
            hasher.AddU64(static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(command.TextureId)));
            hasher.AddU32(command.ElemCount);
        }
    }
    return hasher.value();
}

} // namespace

namespace RoR {

struct Ogre14GuiOverlayCapture::NativeResources
{
    Ogre::SceneManager* scene_manager = nullptr;
    Ogre::TexturePtr texture;
    Ogre::RenderTarget* target = nullptr;
    Ogre::Camera* camera = nullptr;
    Ogre::Viewport* viewport = nullptr;
};

Ogre14GuiOverlayCapture::Ogre14GuiOverlayCapture() = default;

Ogre14GuiOverlayCapture::~Ogre14GuiOverlayCapture()
{
    DestroyRenderResources();
}

bool Ogre14GuiOverlayCapture::EnsureRenderResources(
    std::uint32_t width, std::uint32_t height)
{
    if (App::GetGfxScene() == nullptr || App::GetGuiManager() == nullptr ||
        Ogre::TextureManager::getSingletonPtr() == nullptr)
    {
        return false;
    }
    Ogre::SceneManager* const scene_manager =
        App::GetGfxScene()->GetSceneManager();
    if (scene_manager == nullptr)
    {
        return false;
    }
    if (m_native != nullptr && m_width == width && m_height == height &&
        m_native->scene_manager == scene_manager)
    {
        return true;
    }
    DestroyRenderResources();

    const std::uint64_t sequence =
        g_gui_capture_resource_sequence.fetch_add(1U);
    auto candidate = std::make_unique<NativeResources>();
    candidate->scene_manager = scene_manager;
    m_texture_name = "RoRGuiOverlayCapture-" + std::to_string(sequence);
    m_camera_name = "RoRGuiOverlayCaptureCamera-" + std::to_string(sequence);
    try
    {
        // Single-mip manual target: the pinned Metal blitToMemory aliases
        // every nonzero mip request to mip 0, so allocating exactly one mip
        // avoids that quirk entirely. No hardware gamma: GUI pixels are
        // display-referred as drawn and must not be re-encoded on readback.
        candidate->texture = Ogre::TextureManager::getSingleton().createManual(
            m_texture_name,
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::TEX_TYPE_2D,
            width,
            height,
            0U,
            Ogre::PF_A8R8G8B8,
            Ogre::TU_RENDERTARGET);
        if (!candidate->texture)
        {
            return false;
        }
        candidate->target = candidate->texture->getBuffer()->getRenderTarget();
        if (candidate->target == nullptr)
        {
            return false;
        }
        candidate->target->setAutoUpdated(false);
        candidate->camera = scene_manager->createCamera(m_camera_name);
        if (candidate->camera == nullptr)
        {
            return false;
        }
        candidate->viewport =
            candidate->target->addViewport(candidate->camera);
        if (candidate->viewport == nullptr)
        {
            return false;
        }
        // Overlay-only rendering: DearIMGUI and legacy Ogre overlays are
        // injected by render-queue listeners regardless of scene culling,
        // while a zero visibility mask excludes every scene movable (their
        // default visibility flags are all-ones, so no reserved bit could).
        candidate->viewport->setClearEveryFrame(
            true, Ogre::FBT_COLOUR | Ogre::FBT_DEPTH);
        candidate->viewport->setBackgroundColour(
            Ogre::ColourValue(0.0F, 0.0F, 0.0F, 0.0F));
        candidate->viewport->setOverlaysEnabled(true);
        candidate->viewport->setSkiesEnabled(false);
        candidate->viewport->setShadowsEnabled(false);
        candidate->viewport->setVisibilityMask(0U);
    }
    catch (...)
    {
        if (candidate->camera != nullptr && candidate->scene_manager != nullptr)
        {
            candidate->scene_manager->destroyCamera(candidate->camera);
        }
        if (candidate->texture)
        {
            Ogre::TextureManager::getSingleton().remove(candidate->texture);
        }
        return false;
    }
    // Open the one producer-side gate: OgreImGui::renderQueueStarted refuses
    // every RTT except this registered HUD capture viewport, so worldmodel
    // and reflection captures stay GUI-free.
    App::GetGuiManager()->GetImGui().SetHudCaptureViewport(
        candidate->viewport);
    m_native = std::move(candidate);
    m_width = width;
    m_height = height;
    // Extent changed: the previously published readback no longer matches
    // the presented view, so force a fresh capture and publication.
    m_last_published_hash = 0U;
    return true;
}

void Ogre14GuiOverlayCapture::DestroyRenderResources() noexcept
{
    if (m_native == nullptr)
    {
        return;
    }
    try
    {
        if (App::GetGuiManager() != nullptr)
        {
            App::GetGuiManager()->GetImGui().SetHudCaptureViewport(nullptr);
        }
        if (m_native->target != nullptr)
        {
            m_native->target->removeAllViewports();
        }
        // Destroy the camera only while its owning scene manager is still the
        // live one; a torn-down scene manager already took the camera with it.
        Ogre::SceneManager* const live_scene_manager =
            App::GetGfxScene() != nullptr
                ? App::GetGfxScene()->GetSceneManager()
                : nullptr;
        if (m_native->camera != nullptr &&
            live_scene_manager != nullptr &&
            live_scene_manager == m_native->scene_manager)
        {
            live_scene_manager->destroyCamera(m_native->camera);
        }
        if (m_native->texture &&
            Ogre::TextureManager::getSingletonPtr() != nullptr)
        {
            Ogre::TextureManager::getSingleton().remove(m_native->texture);
        }
    }
    catch (...)
    {
        // Teardown is best-effort; leaked names stay unique via the sequence.
    }
    m_native.reset();
    m_width = 0U;
    m_height = 0U;
}

void Ogre14GuiOverlayCapture::CaptureIfDirty(
    const std::uint32_t target_width, const std::uint32_t target_height)
{
    const auto log_failure_once = [this](const std::string& signature) {
        if (signature != m_failure_log_signature)
        {
            m_failure_log_signature = signature;
            LOG(fmt::format("[RoR|GuiOverlayCapture] {}", signature));
        }
    };

    const int capture_rate_hz = App::gfx_hud_capture_rate_hz->getInt();
    if (capture_rate_hz <= 0)
    {
        return; // Explicitly disabled; the presenter hides the HUD overlay.
    }
    if (App::GetAppContext() == nullptr || App::GetGfxScene() == nullptr ||
        App::GetGuiManager() == nullptr)
    {
        return;
    }
    const std::uint32_t width = target_width;
    const std::uint32_t height = target_height;
    if (width == 0U || height == 0U)
    {
        return;
    }

    // Finalize the imgui frame that GfxScene::UpdateScene just built. The
    // overlay path's preRender calls ImGui::Render() again during the target
    // update below; repeated Render() within one frame is idempotent.
    ImGui::Render();
    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr)
    {
        return;
    }
    std::uint64_t content_hash = HashImGuiDrawData(*draw_data, width, height);
    if (content_hash == 0U)
    {
        content_hash = 1U; // Keep zero reserved as "never published".
    }
    if (content_hash == m_last_published_hash)
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_has_last_capture_time)
    {
        const auto minimum_interval =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(
                    1.0 / static_cast<double>(capture_rate_hz)));
        if (now - m_last_capture_time < minimum_interval)
        {
            return; // Dirty but capped; a later frame ships this change.
        }
    }

    if (!EnsureRenderResources(width, height))
    {
        log_failure_once("overlay capture target could not be (re)created");
        return;
    }

    try
    {
        m_native->target->update(false);

        const std::size_t byte_count =
            static_cast<std::size_t>(width) * height * 4U;
        auto rgba8 = std::make_shared<std::vector<std::uint8_t>>();
        rgba8->assign(byte_count, 0U);
        Ogre::PixelBox pixels(width, height, 1U, Ogre::PF_BYTE_RGBA,
                              rgba8->data());
        m_native->texture->getBuffer()->blitToMemory(pixels);
        // No row flip: the pinned GL3Plus blitToMemory already returns rows
        // top-down. Flipping on requiresTextureFlipping() (true for GL FBO
        // targets) presented the whole HUD vertically mirrored on the first
        // live run; the flag describes on-GPU addressing, not readback order.

        Render::GraphicsSceneHudOverlayInput readback;
        readback.width = width;
        readback.height = height;
        readback.content_hash = content_hash;
        readback.rgba8_bytes = std::move(rgba8);
        App::GetGfxScene()->SetOgre14HudOverlayReadback(std::move(readback));
        m_last_published_hash = content_hash;
        m_last_capture_time = now;
        m_has_last_capture_time = true;
        m_failure_log_signature.clear();
    }
    catch (const Ogre::Exception& error)
    {
        log_failure_once(fmt::format(
            "overlay capture render/readback failed: {}",
            error.getDescription()));
    }
    catch (const std::exception& error)
    {
        log_failure_once(fmt::format(
            "overlay capture render/readback failed: {}", error.what()));
    }
}

} // namespace RoR
