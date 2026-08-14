/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2020 Petr Ohlidal

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file
/// @author Petr Ohlidal
/// @date   05/2020
/// @brief  System integration layer; inspired by OgreBites::ApplicationContext.

#pragma once

#include <string_view>

#include "Application.h"
#include "ForceFeedback.h"
#include "PostProcessRuntime.h"
#include "RenderDisplayMetrics.h"
#include "RendererOgre14RuntimeOwnership.h"

#include <Bites/OgreWindowEventUtilities.h>
#include <Ogre.h>
#include <OIS.h>

#include <future>

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
struct SDL_Window;
#endif

#if OGRE_VERSION_MAJOR >= 14
namespace Ogre
{
namespace RTShader
{
class ShaderGenerator;
}
}

namespace OgreBites
{
class SGTechniqueResolverListener;
}
#endif

namespace RoR {

struct RendererGameDisplayMetrics;

/// @addtogroup Application
/// @{

/// Central setup and event handler for input/windowing/rendering.
/// Inspired by OgreBites::ApplicationContext.
class AppContext: public OgreBites::WindowEventListener,
                  public OIS::MouseListener,
                  public OIS::KeyListener,
                  public OIS::JoyStickListener
{
public:
    ~AppContext();

    // Startup (in order)
    void                 SetUpThreads();
    bool                 SetUpProgramPaths();
    void                 SetUpLogging();
    bool                 SetUpResourcesDir();
    bool                 SetUpRendering(const RendererOgre14RuntimeOwnership& ownership);
    bool                 SetUpConfigSkeleton();
    bool                 SetUpInput(const RendererOgre14RuntimeOwnership& ownership);
    void                 SetUpObsoleteConfMarker();
    void                 ProcessWindowEvents();
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    /// Returns only the hidden transitional resource host owned by this
    /// context. The combined presenter uses the opaque value solely to keep
    /// that SDL window hidden while it owns the process-wide event drain.
    [[nodiscard]] void*  GetCombinedRendererResourceWindow() const noexcept;
#endif
    // Ordered injections from the renderer-owned physical input source. They
    // reuse the same GUI/camera callbacks as local OIS/SDL events; InputEngine
    // held state is reconciled atomically after each renderer poll.
    bool                 InjectRendererInputDisplayMetrics(const RendererGameDisplayMetrics& metrics) noexcept;
    void                 InjectRendererInputKey(OIS::KeyCode key, bool down) noexcept;
    bool                 InjectRendererInputMouseMotion(int x, int y, int dx, int dy) noexcept;
    bool                 InjectRendererInputMouseButton(OIS::MouseButtonID button, bool down) noexcept;
    bool                 InjectRendererInputMouseWheel(float x, float y) noexcept;
    void                 InjectRendererInputText(std::string_view utf8) noexcept;
    void                 InjectRendererInputFocus(bool focused) noexcept;
    void                 InjectRendererInputWindowClose() noexcept;

    // Rendering
    Ogre::RenderWindow*  CreateCustomRenderWindow(std::string const& name, int width, int height);
    void                 CaptureScreenshot();
    void                 ActivateFullscreen(bool val);
    bool                 DetachRenderWindowEvents() noexcept;
    /// Release every renderer/window resource while process-wide listeners
    /// and scene registries are still alive. Idempotent for static fallback.
    bool                 ShutdownRendering() noexcept;
    void                 RegisterRTShaderSceneManager(Ogre::SceneManager* scene_manager);
    void                 BeginPostProcessScene();
    void                 EndPostProcessScene();
    void                 MaintainPostProcessSceneOrder();

    // Profiling
    void                 PrepareProfiler();

    // Getters
    Ogre::Root*          GetOgreRoot() { return m_ogre_root; }
    Ogre::Viewport*      GetViewport() { return m_viewport; }
    Ogre::RenderWindow*  GetRenderWindow() { return m_render_window; }
    const RenderDisplayMetrics& GetRenderDisplayMetrics() const { return m_display_metrics; }
    float                GetDisplayPixelRatio() const { return m_display_metrics.GetFontRasterScale(); }
    RoR::ForceFeedback&  GetForceFeedback() { return m_force_feedback; }
    std::thread::id      GetMainThreadID() { return m_mainthread_id; }

private:
    // OgreBites::WindowEventListener
    virtual void         windowResized(Ogre::RenderWindow* rw) override;
    virtual void         windowFocusChange(Ogre::RenderWindow* rw) override;

    // OIS::MouseListener
    virtual bool         mouseMoved(const OIS::MouseEvent& arg) override;
    virtual bool         mousePressed(const OIS::MouseEvent& arg, OIS::MouseButtonID id) override;
    virtual bool         mouseReleased(const OIS::MouseEvent& arg, OIS::MouseButtonID id) override;

    // OIS::KeyListener
    virtual bool         keyPressed(const OIS::KeyEvent& arg) override;
    virtual bool         keyReleased(const OIS::KeyEvent& arg) override;

    // OIS::JoyStickListener
    virtual bool         buttonPressed(const OIS::JoyStickEvent& arg, int button) override;
    virtual bool         buttonReleased(const OIS::JoyStickEvent& arg, int button) override;
    virtual bool         axisMoved(const OIS::JoyStickEvent& arg, int axis) override;
    virtual bool         sliderMoved(const OIS::JoyStickEvent& arg, int) override;
    virtual bool         povMoved(const OIS::JoyStickEvent& arg, int) override;

    // Rendering and window management
    void                 SetRenderWindowIcon(Ogre::RenderWindow* rw);
    void                 ResetInputStateForFocusTransition();
    void                 RefreshRenderDisplayMetrics(bool log_change);
    void                 FinishPendingScreenshot() noexcept;
#if OGRE_VERSION_MAJOR >= 14
    bool                 SetUpRTShaderSystem();
    void                 ShutDownRTShaderSystem();
#endif

    // Variables

    Ogre::Root*          m_ogre_root     = nullptr;
    Ogre::RenderWindow*  m_render_window = nullptr;
    Ogre::Viewport*      m_viewport      = nullptr;
    bool                 m_render_window_registered = false;
    bool                 m_window_event_listener_registered = false;
    bool                 m_rendering_shutdown = false;
    bool                 m_renderer_child_owns_presentation = false;
    RenderDisplayMetrics m_display_metrics;
    PostProcessRuntime   m_postprocess_runtime;
#if OGRE_VERSION_MAJOR >= 14
    Ogre::RTShader::ShaderGenerator*          m_shader_generator = nullptr;
    OgreBites::SGTechniqueResolverListener*   m_rtshader_material_listener = nullptr;
#endif
#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    SDL_Window*          m_sdl_window = nullptr;
    bool                 m_owns_sdl_video = false;
#endif
    bool                 m_window_shutdown_requested = false;
    bool                 m_windowed_fix = false; //!< Workaround OGRE glitch when switching from fullscreen.
    bool                 m_profiler_enabled = false; //!< Last known state, to workaround OGRE v14.5.2 bug

    std::time_t          m_prev_screenshot_time;
    int                  m_prev_screenshot_index = 1;
    std::future<void>    m_screenshot_write;

    RoR::ForceFeedback   m_force_feedback;

    std::thread::id      m_mainthread_id;
};

/// @} // addtogroup Application

} // namespace RoR
