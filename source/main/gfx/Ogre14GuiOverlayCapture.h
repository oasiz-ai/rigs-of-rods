/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Overlay-only GUI RTT capture feeding the transported menu/HUD.
///
/// The combined runtime builds the complete DearIMGUI frame every tick but
/// never rendered it: the hidden OGRE 14 host presents nothing. This object
/// renders exactly the overlay render queue (DearIMGUI plus legacy
/// Ogre::Overlay HUD elements, both injected by render-queue listeners) into
/// a dedicated PF_A8R8G8B8 render target cleared to (0,0,0,0), reads the
/// premultiplied source-over composite back on the CPU, and publishes it to
/// GfxScene as the joined frame's optional HUD overlay input.
///
/// Capture is dirty-gated: an FNV-1a-64 hash over the built ImDrawData
/// (vertex/index bytes plus per-command clip/texture/count metadata) must
/// change before any GPU work or readback happens, and a configurable rate
/// cap (`gfx_hud_capture_rate_hz`, default 30) bounds the worst-case cost of
/// continuously animating HUD text. Unchanged frames publish nothing new, so
/// the producer ships no asset delta at all.

#pragma once

#include "gfx/render/GraphicsSceneSnapshotProducer.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace Ogre {
class Camera;
class RenderTarget;
class Viewport;
}

namespace RoR {

class Ogre14GuiOverlayCapture
{
public:
    // Both stay out of line: the inline-defaulted constructor would
    // instantiate the pimpl deleter in every including translation unit.
    Ogre14GuiOverlayCapture();
    ~Ogre14GuiOverlayCapture();

    Ogre14GuiOverlayCapture(const Ogre14GuiOverlayCapture&) = delete;
    Ogre14GuiOverlayCapture& operator=(const Ogre14GuiOverlayCapture&) = delete;

    /// Runs after GfxScene::UpdateScene() has built the complete DearIMGUI
    /// frame and before the joined scene is posted. Finalizes the imgui frame
    /// (ImGui::Render()), and when the draw data hash or the target extent
    /// changed and the rate cap allows, renders the overlay-only target,
    /// reads it back with the row-flip recipe, and publishes the readback to
    /// GfxScene. Failures leave the previously published readback untouched
    /// and log once per failure signature.
    /// `target_width`/`target_height` name the presented drawable extent in
    /// pixels; the presenter fails a mismatched HUD texture closed, and on a
    /// scaled backing store the hidden producer viewport's logical extent is
    /// the wrong answer. The overlay projection maps the ImGui display size
    /// onto the full render-target viewport, so capturing at the drawable
    /// extent is a clean upscale.
    void CaptureIfDirty(std::uint32_t target_width,
                        std::uint32_t target_height);

private:
    bool EnsureRenderResources(std::uint32_t width, std::uint32_t height);
    void DestroyRenderResources() noexcept;

    // Named via a process-unique sequence like the worldmodel capture RTTs so
    // re-created targets never collide inside OGRE's resource maps.
    std::string m_texture_name;
    std::string m_camera_name;
    // Ogre::TexturePtr is deliberately kept out of this header; the render/
    // producer boundary must not inherit OGRE types from gfx/ headers.
    struct NativeResources;
    std::unique_ptr<NativeResources> m_native;
    std::uint32_t m_width = 0U;
    std::uint32_t m_height = 0U;
    std::uint64_t m_last_published_hash = 0U;
    std::chrono::steady_clock::time_point m_last_capture_time{};
    bool m_has_last_capture_time = false;
    std::string m_failure_log_signature;
};

} // namespace RoR
