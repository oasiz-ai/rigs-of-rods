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

#include "AppContext.h"

#include "AdvancedScreen.h"
#include "Actor.h"
#include "CameraManager.h"
#include "ChatSystem.h"
#include "Console.h"
#include "ContentManager.h"
#include "DashBoardManager.h"
#include "Engine.h"
#include "ErrorUtils.h"
#include "GameContext.h"
#include "GUIManager.h"
#include "GUI_LoadingWindow.h"
#include "GUI_MainSelector.h"
#include "GUI_MultiplayerClientList.h"
#include "GUI_MultiplayerSelector.h"
#include "InputEngine.h"
#include "Language.h"
#include "PlatformUtils.h"
#if defined(_WIN32)
    #include "WindowsRuntimePath.h"
#endif
#if defined(__APPLE__)
    #include "MacOSUserDirectoryLayout.h"
#endif
#include "RoRVersion.h"
#include "OverlayWrapper.h"

#if OGRE_VERSION_MAJOR >= 14
#    include <Bites/OgreSGTechniqueResolverListener.h>
#    include <RTShaderSystem/OgreRTShaderSystem.h>
#endif

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
#    define SDL_MAIN_HANDLED
#    include <SDL.h>
#    include <SDL_syswm.h>
#endif

#ifdef USE_ANGELSCRIPT
#    include "ScriptEngine.h"
#endif

#ifdef _WIN32
#   include <windows.h>
#endif

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace RoR;

namespace
{

void LogRendererShutdownMarker(const char* marker) noexcept
{
    try
    {
        if (Ogre::LogManager::getSingletonPtr() != nullptr)
        {
            Ogre::LogManager::getSingleton().logMessage(marker);
        }
    }
    catch (...)
    {
        // Shutdown diagnostics must never replace the original failure mode.
    }
}

} // namespace

AppContext::~AppContext()
{
    this->ShutdownRendering();
}

bool AppContext::ShutdownRendering() noexcept
{
    if (m_rendering_shutdown)
    {
        return true;
    }

    bool clean_shutdown = true;
    this->FinishPendingScreenshot();
    m_postprocess_runtime.Shutdown();

#if OGRE_VERSION_MAJOR >= 14
    try
    {
        this->ShutDownRTShaderSystem();
    }
    catch (...)
    {
        clean_shutdown = false;
    }

    clean_shutdown = this->DetachRenderWindowEvents() && clean_shutdown;
    if (m_render_window != nullptr)
    {
        try
        {
            m_viewport = nullptr;
            m_ogre_root->destroyRenderTarget(m_render_window);
            m_render_window = nullptr;
        }
        catch (...)
        {
            clean_shutdown = false;
        }
    }

    if (m_ogre_root != nullptr)
    {
        LogRendererShutdownMarker(
            "[RoR|Shutdown] Renderer root teardown starting");
        try
        {
            delete m_ogre_root;
            m_ogre_root = nullptr;
            m_render_window = nullptr;
            m_viewport = nullptr;
            LogRendererShutdownMarker(
                "[RoR|Shutdown] Renderer root teardown completed");
        }
        catch (...)
        {
            // A throwing C++ destructor has ended the Root object's lifetime;
            // never retry deletion from the static fallback destructor.
            m_ogre_root = nullptr;
            m_render_window = nullptr;
            m_viewport = nullptr;
            clean_shutdown = false;
        }
    }
#endif

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    // OGRE owns the OpenGL context attached to the external view. Destroy Root
    // first, then release the SDL-owned NSWindow which hosted that view.
    if (m_sdl_window != nullptr)
    {
        SDL_DestroyWindow(m_sdl_window);
        m_sdl_window = nullptr;
    }
    if (m_owns_sdl_video)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        m_owns_sdl_video = false;
    }
#endif

    m_rendering_shutdown = true;
    return clean_shutdown;
}

// --------------------------
// Input handling

bool AppContext::SetUpInput(
    const RendererOgre14RuntimeOwnership& ownership)
{
    if (!ownership.valid())
        return false;
    const bool enable_physical_input =
        ownership.legacy_physical_input_enabled;
    App::CreateInputEngine(enable_physical_input);
    App::GetInputEngine()->SetMouseListener(this);
    App::GetInputEngine()->SetKeyboardListener(this);
    App::GetInputEngine()->SetJoystickListener(this);

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    // CocoaKeyboard's constructor makes its OIS responder first responder,
    // displacing SDL's field editor. Stop/start is intentional: calling only
    // SDL_StartTextInput() does not reselect an already-attached field editor.
    // SDL remains the sole buffered source for physical keys and Unicode text.
    if (enable_physical_input)
    {
        SDL_StopTextInput();
        SDL_StartTextInput();
        ROR_ASSERT(SDL_IsTextInputActive() == SDL_TRUE);
    }
#endif

    if (enable_physical_input && App::io_ffb_enabled->getBool())
    {
        m_force_feedback.Setup();
    }
    return true;
}

void AppContext::InjectRendererBridgeKey(OIS::KeyCode key, bool down) noexcept
{
    try
    {
        const OIS::KeyEvent event(App::GetInputEngine()->GetOisKeyboard(),
                                  key, 0U);
        if (down)
            (void)this->keyPressed(event);
        else
            (void)this->keyReleased(event);
    }
    catch (...)
    {
    }
}

void AppContext::InjectRendererBridgeMouseMotion(
    int x, int y, int dx, int dy) noexcept
{
    try
    {
        OIS::MouseState state = App::GetInputEngine()->getMouseState();
        state.X.abs = x;
        state.Y.abs = y;
        state.X.rel = dx;
        state.Y.rel = dy;
        state.Z.rel = 0;
        const OIS::MouseEvent event(nullptr, state);
        (void)this->mouseMoved(event);
    }
    catch (...)
    {
    }
}

void AppContext::InjectRendererBridgeMouseButton(
    OIS::MouseButtonID button, bool down) noexcept
{
    try
    {
        OIS::MouseState state = App::GetInputEngine()->getMouseState();
        const int bit = 1 << static_cast<int>(button);
        if (down)
            state.buttons |= bit;
        else
            state.buttons &= ~bit;
        const OIS::MouseEvent event(nullptr, state);
        if (down)
            (void)this->mousePressed(event, button);
        else
            (void)this->mouseReleased(event, button);
    }
    catch (...)
    {
    }
}

void AppContext::InjectRendererBridgeMouseWheel(float x, float y) noexcept
{
    (void)x; // Legacy OIS has one wheel axis; horizontal state is reconciled.
    try
    {
        OIS::MouseState state = App::GetInputEngine()->getMouseState();
        state.X.rel = 0;
        state.Y.rel = 0;
        state.Z.rel = static_cast<int>(y * 120.0F);
        state.Z.abs += state.Z.rel;
        const OIS::MouseEvent event(nullptr, state);
        (void)this->mouseMoved(event);
    }
    catch (...)
    {
    }
}

void AppContext::InjectRendererBridgeText(std::string_view utf8) noexcept
{
    try
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            const std::string owned(utf8);
            ImGui::GetIO().AddInputCharactersUTF8(owned.c_str());
        }
    }
    catch (...)
    {
    }
}

void AppContext::InjectRendererBridgeFocus(bool focused) noexcept
{
    if (!focused && App::GetInputEngine() != nullptr)
        App::GetInputEngine()->resetKeysAndMouseButtons();
}

void AppContext::InjectRendererBridgeWindowClose() noexcept
{
    try
    {
        if (!m_window_shutdown_requested)
        {
            m_window_shutdown_requested = true;
            App::GetGameContext()->PushMessage(
                Message(MSG_APP_SHUTDOWN_REQUESTED));
        }
    }
    catch (...)
    {
    }
}

bool AppContext::mouseMoved(const OIS::MouseEvent& arg) // overrides OIS::MouseListener
{
    App::GetGuiManager()->WakeUpGUI();
    App::GetGuiManager()->GetImGui().InjectMouseMoved(arg);
    App::GetInputEngine()->processMouseMotionEvent(arg);

    if (!ImGui::GetIO().WantCaptureMouse) // true if mouse is over any window
    {
        if (!App::GetOverlayWrapper() || !App::GetOverlayWrapper()->handleMouseMoved()) // update the old airplane / autopilot gui
        {
            if (!App::GetCameraManager()->handleMouseMoved())
            {
                App::GetGameContext()->GetSceneMouse().handleMouseMoved();
            }
        }
    }

    return true;
}

bool AppContext::mousePressed(const OIS::MouseEvent& arg, OIS::MouseButtonID _id) // overrides OIS::MouseListener
{
    App::GetGuiManager()->WakeUpGUI();
    App::GetGuiManager()->GetImGui().SetMouseButtonState(_id, /*down:*/true);
    App::GetInputEngine()->processMousePressEvent(arg, _id);

    if (!ImGui::GetIO().WantCaptureMouse) // true if mouse is over any window
    {
        if (!App::GetOverlayWrapper() || !App::GetOverlayWrapper()->handleMousePressed()) // update the old airplane / autopilot gui
        {
            if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                App::GetGameContext()->GetSceneMouse().handleMousePressed();
                App::GetCameraManager()->handleMousePressed();
            }
        }
    }

    return true;
}

bool AppContext::mouseReleased(const OIS::MouseEvent& arg, OIS::MouseButtonID _id) // overrides OIS::MouseListener
{
    App::GetGuiManager()->WakeUpGUI();
    App::GetGuiManager()->GetImGui().SetMouseButtonState(_id, /*down:*/false);
    App::GetInputEngine()->processMouseReleaseEvent(arg, _id);

    if (!ImGui::GetIO().WantCaptureMouse) // true if mouse is over any window
    {
        if (!App::GetOverlayWrapper() || !App::GetOverlayWrapper()->handleMouseReleased()) // update the old airplane / autopilot gui
        {
            if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                App::GetGameContext()->GetSceneMouse().handleMouseReleased();
            }
        }
    }

    return true;
}

bool AppContext::keyPressed(const OIS::KeyEvent& arg)
{
    App::GetGuiManager()->GetImGui().InjectKeyPressed(arg);

    if (!App::GetGuiManager()->IsGuiCaptureKeyboardRequested() &&
        !ImGui::GetIO().WantCaptureKeyboard)
    {
        App::GetInputEngine()->ProcessKeyPress(arg);
    }

    return true;
}

bool AppContext::keyReleased(const OIS::KeyEvent& arg)
{
    App::GetGuiManager()->GetImGui().InjectKeyReleased(arg);

    if (!App::GetGuiManager()->IsGuiCaptureKeyboardRequested() &&
        !ImGui::GetIO().WantCaptureKeyboard)
    {
        App::GetInputEngine()->ProcessKeyRelease(arg);
    }
    else if (App::GetInputEngine()->isKeyDownEffective(arg.key))
    {
        // If capturing is requested, still pass release events for already-pressed keys.
        App::GetInputEngine()->ProcessKeyRelease(arg);
    }

    return true;
}

bool AppContext::buttonPressed(const OIS::JoyStickEvent& arg, int)  { App::GetInputEngine()->ProcessJoystickEvent(arg); return true; }
bool AppContext::buttonReleased(const OIS::JoyStickEvent& arg, int) { App::GetInputEngine()->ProcessJoystickEvent(arg); return true; }
bool AppContext::axisMoved(const OIS::JoyStickEvent& arg, int)      { App::GetInputEngine()->ProcessJoystickEvent(arg); return true; }
bool AppContext::sliderMoved(const OIS::JoyStickEvent& arg, int)    { App::GetInputEngine()->ProcessJoystickEvent(arg); return true; }
bool AppContext::povMoved(const OIS::JoyStickEvent& arg, int)       { App::GetInputEngine()->ProcessJoystickEvent(arg); return true; }

void AppContext::windowResized(Ogre::RenderWindow* rw)
{
    this->RefreshRenderDisplayMetrics(/*log_change=*/true);
    m_postprocess_runtime.OnMainViewportResized();
    App::GetInputEngine()->windowResized(rw); // Update mouse area
    if (App::GetOverlayWrapper())
    {
        App::GetOverlayWrapper()->windowResized();
    }
    if (App::sim_state->getEnum<AppState>() == RoR::AppState::SIMULATION)
    {
        for (ActorPtr& actor: App::GetGameContext()->GetActorManager()->GetActors())
        {
            actor->ar_dashboard->windowResized();
        }
    }
}

void AppContext::windowFocusChange(Ogre::RenderWindow* rw)
{
    // If you alt+TAB out of the window while any mouse button is down, OIS will not release it until you click in the window again.
    // See https://github.com/RigsOfRods/rigs-of-rods/issues/2468
    // To work around, we reset all internal mouse button states here and pay attention not to get them polluted by OIS again.
    App::GetGuiManager()->GetImGui().ResetAllMouseButtons();
    // Same applies to keyboard keys: reset the native input and ImGui states so
    // a focus loss without matching key-up events cannot leave keys held down.
    App::GetInputEngine()->resetKeysAndMouseButtons();
#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (bool& key_down : io.KeysDown)
        {
            key_down = false;
        }
        io.KeyCtrl = false;
        io.KeyShift = false;
        io.KeyAlt = false;
        io.KeySuper = false;
    }
#endif
}

// --------------------------
// Rendering

void AppContext::SetRenderWindowIcon(Ogre::RenderWindow* rw)
{
#ifdef _WIN32
    size_t hWnd = 0;
    rw->getCustomAttribute("WINDOW", &hWnd);

    char buf[MAX_PATH];
    ::GetModuleFileNameA(0, (LPCH)&buf, MAX_PATH);

    HINSTANCE instance = ::GetModuleHandleA(buf);
    HICON hIcon = ::LoadIconA(instance, MAKEINTRESOURCEA(101));
    if (hIcon)
    {
        ::SendMessageA((HWND)hWnd, WM_SETICON, 1, (LPARAM)hIcon);
        ::SendMessageA((HWND)hWnd, WM_SETICON, 0, (LPARAM)hIcon);
    }
#endif // _WIN32
}

bool AppContext::SetUpRendering(
    const RendererOgre14RuntimeOwnership& ownership)
{
    if (!ownership.valid())
        return false;
    m_renderer_child_owns_presentation =
        ownership.child_window_visible;
    // Create 'OGRE root' facade
    // * leave 'plugins' param empty, we load manually below
    // * note file 'ogre.cfg' isn't read immediatelly but only after calling 'restoreConfig()' below.
    std::string log_filepath = PathCombine(App::sys_logs_dir->getStr(), "RoR.log");
    std::string cfg_filepath = PathCombine(App::sys_config_dir->getStr(), "ogre.cfg");
    LOG(fmt::format("[RoR|Startup|Rendering] Creating OGRE renderer Root object, config='{}'", cfg_filepath));
    m_ogre_root = new Ogre::Root("", cfg_filepath, log_filepath);

    // load OGRE plugins manually
#ifdef _DEBUG
    std::string plugins_path = PathCombine(RoR::App::sys_process_dir->getStr(), "plugins_d.cfg");
#else
	std::string plugins_path = PathCombine(RoR::App::sys_process_dir->getStr(), "plugins.cfg");
#endif
    LOG(fmt::format("[RoR|Startup|Rendering] Loading OGRE renderer plugins config '{}'.", plugins_path));
    try
    {
        Ogre::ConfigFile cfg;
        cfg.load(plugins_path);
        std::string plugin_dir = cfg.getSetting("PluginFolder", /*section=*/"", /*default=*/App::sys_process_dir->getStr());
        if (!IsAbsolutePath(plugin_dir))
        {
            plugin_dir = PathCombine(GetParentDirectory(plugins_path.c_str()), plugin_dir);
        }
        Ogre::StringVector plugins = cfg.getMultiSetting("Plugin");
        for (Ogre::String plugin_filename: plugins)
        {
            try
            {
                m_ogre_root->loadPlugin(PathCombine(plugin_dir, plugin_filename));
            }
            catch (Ogre::Exception&) {} // Logged by OGRE
        }
    }
    catch (Ogre::Exception& e)
    {
        ErrorUtils::ShowError (
            _L("Startup error"), 
            fmt::format(_L("Could not load file '{}' - make sure the game is installed correctly.\n\nDetailed info: {}"), plugins_path, e.getDescription()));
        return false;
    }

    // Load renderer configuration
    bool autodetect_resolution = false;
    try
    {
        if (!m_ogre_root->restoreConfig())
        {
            autodetect_resolution = true;
            LOG(fmt::format("[RoR|Startup|Rendering] WARNING - invalid 'ogre.cfg', selecting render plugin manually..."));

            const auto render_systems = App::GetAppContext()->GetOgreRoot()->getAvailableRenderers();
            if (!render_systems.empty())
            {
                LOG(fmt::format("[RoR|Startup|Rendering] Auto-selected renderer plugin '{}'", render_systems.front()->getName()));
                    m_ogre_root->setRenderSystem(render_systems.front());
            }
            else
            {
                ErrorUtils::ShowError (_L("Startup error"), _L("No render system plugin available. Check your plugins.cfg"));
                return false;
            }
        }
    }
    catch (Ogre::Exception& e)
    {
        ErrorUtils::ShowError (_L("Error restoring settings from 'ogre.cfg'"), e.getDescription());
        return false;
    }

    const auto rs = m_ogre_root->getRenderSystemByName(App::app_rendersys_override->getStr());
    if (rs != nullptr && rs != m_ogre_root->getRenderSystem())
    {
        LOG(fmt::format("[RoR|Startup|Rendering] Setting renderer '{}' on behalf of 'app_rendersys_override' (user selection via Settings UI)", rs->getName()));
        // The user has selected a different render system during the previous session.
        m_ogre_root->setRenderSystem(rs);
        m_ogre_root->saveConfig();
    }
    App::app_rendersys_override->setStr("");

    // Start the renderer
    LOG(fmt::format("[RoR|Startup|Rendering] Starting renderer '{}' (without auto-creating render window)", m_ogre_root->getRenderSystem()->getName()));
    m_ogre_root->initialise(/*createWindow=*/false);

    // Review configuration options
    Ogre::ConfigOptionMap ropts = m_ogre_root->getRenderSystem()->getConfigOptions();
    std::stringstream ropts_log;
    for (auto& pair: ropts)
    {
        ropts_log << "  " << pair.first << " = " << pair.second.currentValue << " (";
        for (auto& val: pair.second.possibleValues)
        {
            ropts_log << val << ", ";
        }
        ropts_log << ")\n";
    }
    LOG(fmt::format("[RoR|Startup|Rendering] Renderer options as reported by OGRE:\n{}", ropts_log.str()));

    // Configure the render window
    Ogre::NameValuePairList miscParams;
    miscParams["FSAA"] = ropts["FSAA"].currentValue;
    miscParams["vsync"] = ropts["VSync"].currentValue;
    miscParams["gamma"] = ropts["sRGB Gamma Conversion"].currentValue;
    if (!App::diag_allow_window_resize->getBool())
    {
        miscParams["border"] = "fixed";
    }
    if (m_renderer_child_owns_presentation)
    {
        // Every supported OGRE 14 desktop backend consumes this at native
        // window creation time. It is not a post-create hide that could flash
        // a second presentation surface.
        miscParams["hidden"] = "true";
        miscParams["border"] = "none";
    }
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
    const auto rd = ropts["Rendering Device"];
    const auto it = std::find(rd.possibleValues.begin(), rd.possibleValues.end(), rd.currentValue);
    const int idx = std::distance(rd.possibleValues.begin(), it);
    miscParams["monitorIndex"] = Ogre::StringConverter::toString(idx);
    miscParams["windowProc"] = Ogre::StringConverter::toString((size_t)OgreBites::WindowEventUtilities::_WndProc);
#endif

    // Validate rendering resolution
    Ogre::uint32 width, height;
    std::istringstream mode (ropts["Video Mode"].currentValue);
    Ogre::String token;
    mode >> width;
    mode >> token; // 'x' as seperator between width and height
    mode >> height;
    
    if(width < 800) width = 800;
    if(height < 600) height = 600;

    if (autodetect_resolution)
    {
        for (auto& p_mode_str: ropts["Video Mode"].possibleValues)
        {
            Ogre::uint32 p_width, p_height;
            std::istringstream p_mode (p_mode_str);
            p_mode >> p_width;
            p_mode >> token; // 'x' as seperator between width and height
            p_mode >> p_height;
            if (p_width >= width && p_height >= height)
            {
                width = p_width;
                height = p_height;
                m_ogre_root->getRenderSystem()->setConfigOption("Video Mode", p_mode_str);
            }
        }

        LOG(fmt::format("[RoR|Startup|Rendering] WARNING - invalid 'ogre.cfg', auto-detected resolution {}x{}", width, height));
        m_ogre_root->saveConfig();
    }

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
    // OGRE's D3D11 window backend expands a requested client extent by the
    // title-bar/border size and then clamps that outer window to rcWork. The
    // hosted Windows desktop is smaller than the D0 gate's 1280x720 target,
    // so a decorated window silently becomes a smaller render target. Only
    // the isolated scene harness may request a borderless outer-dimension
    // window, for which outer and client extents are identical. Ordinary
    // launches and malformed diagnostic environments keep the fixed border.
    const char* d0_scene_home = std::getenv("ROR_D0_SCENE_HOME");
    const char* d0_exact_window_extent =
        std::getenv("ROR_D0_EXACT_WINDOW_EXTENT");
    const std::string selected_extent =
        fmt::format("{}x{}", width, height);
    const bool use_d0_exact_window =
        d0_scene_home != nullptr && IsAbsolutePath(d0_scene_home) &&
        d0_exact_window_extent != nullptr &&
        selected_extent == d0_exact_window_extent &&
        ropts["Full Screen"].currentValue == "No" &&
        !App::diag_allow_window_resize->getBool();
    if (use_d0_exact_window)
    {
        miscParams["border"] = "none";
        miscParams["outerDimensions"] = "true";
        LOG(fmt::format(
            "[RoR|Startup|Rendering] D0 exact window extent enabled: {}",
            selected_extent));
    }
#endif

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    // OGRE's macOS video modes are backing-pixel dimensions, while SDL sizes
    // Cocoa windows in logical points. Preserve the selected native backing
    // resolution without applying Retina scaling twice.
    const auto content_scale_option = ropts.find("Content Scaling Factor");
    if (content_scale_option != ropts.end())
    {
        miscParams["contentScalingFactor"] =
            content_scale_option->second.currentValue;
    }
#endif

    // Review render window settings
    std::stringstream miscParams_log;
    for (auto& pair: miscParams)
    {
        miscParams_log << "  " << pair.first << " = " << pair.second << "\n";
    }
    LOG(fmt::format("[RoR|Startup|Rendering] Creating render window with settings:\n{}", miscParams_log.str()));

    // Create render window
#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    // OGRE 14 deliberately rejects its broken built-in Cocoa window path.
    // Follow OgreBites::ApplicationContextSDL: let SDL own the NSWindow and
    // give OGRE that external Cocoa handle so OGRE only owns the GL context.
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            ErrorUtils::ShowError(
                _L("Startup error"),
                fmt::format(_L("Could not initialize SDL video: {}"),
                            SDL_GetError()));
            return false;
        }
        m_owns_sdl_video = true;
    }

    const bool full_screen =
        !m_renderer_child_owns_presentation &&
        ropts["Full Screen"].currentValue == "Yes";
    Ogre::Real content_scale = SanitizeRequestedContentScale(
        Ogre::StringConverter::parseReal(
            miscParams["contentScalingFactor"], 1.0f));
    miscParams["contentScalingFactor"] =
        Ogre::StringConverter::toString(content_scale);

    Uint32 sdl_window_flags = m_renderer_child_owns_presentation
                                  ? SDL_WINDOW_HIDDEN
                                  : SDL_WINDOW_SHOWN;
    if (ShouldRequestHighPixelDensity(content_scale))
    {
        sdl_window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    }
    if (full_screen)
    {
        sdl_window_flags |= SDL_WINDOW_FULLSCREEN;
    }
    else if (!m_renderer_child_owns_presentation &&
             App::diag_allow_window_resize->getBool())
    {
        sdl_window_flags |= SDL_WINDOW_RESIZABLE;
    }

    const Ogre::String window_title =
        "Rigs of Rods version " + Ogre::String(ROR_VERSION_STRING);
    const int logical_width = static_cast<int>(
        LogicalExtentForBacking(width, content_scale));
    const int logical_height = static_cast<int>(
        LogicalExtentForBacking(height, content_scale));
    LOG(fmt::format(
        "[RoR|Startup|Rendering] Creating SDL host at {}x{} logical points "
        "for {}x{} backing pixels (scale {:.1f})",
        logical_width,
        logical_height,
        width,
        height,
        content_scale));
    m_sdl_window = SDL_CreateWindow(
        window_title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        logical_width,
        logical_height,
        sdl_window_flags);
    if (m_sdl_window == nullptr)
    {
        ErrorUtils::ShowError(
            _L("Startup error"),
            fmt::format(_L("Could not create the macOS window: {}"),
                        SDL_GetError()));
        return false;
    }
    if (m_renderer_child_owns_presentation)
    {
        const Uint32 actual_flags = SDL_GetWindowFlags(m_sdl_window);
        if ((actual_flags & SDL_WINDOW_HIDDEN) == 0U ||
            (actual_flags & SDL_WINDOW_SHOWN) != 0U)
        {
            ErrorUtils::ShowError(
                _L("Startup error"),
                _L("The OGRE 14 resource host could not create a hidden macOS window; refusing to expose a second presentation owner."));
            return false;
        }
    }
    SDL_SetWindowMinimumSize(
        m_sdl_window,
        static_cast<int>(std::ceil(800.0f / content_scale)),
        static_cast<int>(std::ceil(600.0f / content_scale)));

    SDL_SysWMinfo window_info;
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(m_sdl_window, &window_info) != SDL_TRUE ||
        window_info.subsystem != SDL_SYSWM_COCOA ||
        window_info.info.cocoa.window == nullptr)
    {
        ErrorUtils::ShowError(
            _L("Startup error"),
            fmt::format(
                _L("SDL did not provide a Cocoa NSWindow handle: {}"),
                SDL_GetError()));
        return false;
    }

    miscParams["externalWindowHandle"] =
        Ogre::StringConverter::toString(
            reinterpret_cast<size_t>(window_info.info.cocoa.window));
    m_render_window = Ogre::Root::getSingleton().createRenderWindow(
        window_title,
        static_cast<Ogre::uint32>(logical_width),
        static_cast<Ogre::uint32>(logical_height),
        full_screen,
        &miscParams);
#else
    m_render_window = Ogre::Root::getSingleton().createRenderWindow (
        "Rigs of Rods version " + Ogre::String (ROR_VERSION_STRING),
        width, height,
        !m_renderer_child_owns_presentation &&
            ropts["Full Screen"].currentValue == "Yes",
        &miscParams);
#endif
    if (m_renderer_child_owns_presentation &&
        !m_render_window->isHidden())
    {
        ErrorUtils::ShowError(
            _L("Startup error"),
            _L("The OGRE 14 backend does not support the required hidden resource host; refusing to run with two presentation windows."));
        return false;
    }
    OgreBites::WindowEventUtilities::_addRenderWindow(m_render_window);
    m_render_window_registered = true;
    OgreBites::WindowEventUtilities::addWindowEventListener(m_render_window, this);
    m_window_event_listener_registered = true;

    if (!m_renderer_child_owns_presentation)
        this->SetRenderWindowIcon(m_render_window);
    m_render_window->setActive(!m_renderer_child_owns_presentation);

    // Create viewport (without camera)
    m_viewport = m_render_window->addViewport(/*camera=*/nullptr);
    m_viewport->setBackgroundColour(Ogre::ColourValue::Black);
    this->RefreshRenderDisplayMetrics(/*log_change=*/true);

#if OGRE_VERSION_MAJOR >= 14
    if (!this->SetUpRTShaderSystem())
    {
        return false;
    }
#endif

    return true;
}

bool AppContext::DetachRenderWindowEvents() noexcept
{
    if (m_render_window == nullptr)
    {
        m_window_event_listener_registered = false;
        m_render_window_registered = false;
        return true;
    }

    bool clean_detach = true;
    if (m_window_event_listener_registered)
    {
        try
        {
            OgreBites::WindowEventUtilities::removeWindowEventListener(
                m_render_window, this);
            m_window_event_listener_registered = false;
        }
        catch (...)
        {
            clean_detach = false;
        }
    }
    if (m_render_window_registered)
    {
        try
        {
            OgreBites::WindowEventUtilities::_removeRenderWindow(
                m_render_window);
            m_render_window_registered = false;
        }
        catch (...)
        {
            clean_detach = false;
        }
    }
    return clean_detach;
}

void AppContext::RefreshRenderDisplayMetrics(bool log_change)
{
    if (m_render_window == nullptr)
    {
        return;
    }

    const Ogre::uint32 backing_width = m_viewport != nullptr
        ? m_viewport->getActualWidth()
        : m_render_window->getWidth();
    const Ogre::uint32 backing_height = m_viewport != nullptr
        ? m_viewport->getActualHeight()
        : m_render_window->getHeight();
    Ogre::uint32 logical_width = backing_width;
    Ogre::uint32 logical_height = backing_height;

#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    if (m_sdl_window != nullptr)
    {
        int host_width = 0;
        int host_height = 0;
        SDL_GetWindowSize(m_sdl_window, &host_width, &host_height);
        if (host_width > 0 && host_height > 0)
        {
            logical_width = static_cast<Ogre::uint32>(host_width);
            logical_height = static_cast<Ogre::uint32>(host_height);
        }
    }
#endif

    const RenderDisplayMetrics next = ResolveRenderDisplayMetrics(
        backing_width,
        backing_height,
        logical_width,
        logical_height);
    const bool changed =
        next.logical_width != m_display_metrics.logical_width ||
        next.logical_height != m_display_metrics.logical_height ||
        next.backing_width != m_display_metrics.backing_width ||
        next.backing_height != m_display_metrics.backing_height ||
        next.valid != m_display_metrics.valid;
    m_display_metrics = next;

    if (log_change && changed)
    {
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
        const char* backend = "windows";
#elif OGRE_PLATFORM == OGRE_PLATFORM_APPLE
        const char* backend = "macos";
#elif OGRE_PLATFORM == OGRE_PLATFORM_LINUX
        const char* backend = "linux";
#else
        const char* backend = "other";
#endif
        LOG(fmt::format(
            "[RoR|DisplayMetrics] backend={} logical={}x{} backing={}x{} "
            "scale={:.3f}x{:.3f} viewport={}x{} valid={}",
            backend,
            m_display_metrics.logical_width,
            m_display_metrics.logical_height,
            m_display_metrics.backing_width,
            m_display_metrics.backing_height,
            m_display_metrics.framebuffer_scale_x,
            m_display_metrics.framebuffer_scale_y,
            backing_width,
            backing_height,
            m_display_metrics.valid ? 1 : 0));
    }
}

void AppContext::ProcessWindowEvents()
{
#if OGRE_VERSION_MAJOR >= 14 && OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    if (m_sdl_window == nullptr || m_render_window == nullptr)
    {
        return;
    }

    const Uint32 main_window_id = SDL_GetWindowID(m_sdl_window);
    InputEngine* const input_engine = App::GetInputEngine();
    input_engine->BeginSdlControllerEventFrame();
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (m_renderer_child_owns_presentation)
        {
            // The Ogre-Next child is the only physical input and visible
            // presentation owner. Drain the hidden SDL host queue without
            // translating any event into gameplay. If platform code attempts
            // to reveal the resource host, force it back to hidden state.
            if (event.type == SDL_WINDOWEVENT &&
                event.window.windowID == main_window_id &&
                (event.window.event == SDL_WINDOWEVENT_SHOWN ||
                 event.window.event == SDL_WINDOWEVENT_RESTORED))
            {
                SDL_HideWindow(m_sdl_window);
                m_render_window->setHidden(true);
                m_render_window->setActive(false);
            }
            continue;
        }
        if (input_engine->ProcessSdlControllerEvent(event))
        {
            continue;
        }

        if (event.type == SDL_QUIT)
        {
            if (!m_window_shutdown_requested)
            {
                m_window_shutdown_requested = true;
                App::GetGameContext()->PushMessage(
                    Message(MSG_APP_SHUTDOWN_REQUESTED));
            }
            continue;
        }

        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
            event.key.windowID == main_window_id)
        {
            const OIS::KeyCode key =
                MacOSInputBridge::TranslateScancode(
                    event.key.keysym.scancode);
            const bool down = event.type == SDL_KEYDOWN;

            // State transitions suppress SDL key-repeat events for gameplay;
            // repeated text remains available through SDL_TEXTINPUT.
            if (key != OIS::KC_UNASSIGNED &&
                App::GetInputEngine()->SetSdlKeyState(key, down))
            {
                const OIS::KeyEvent key_event(
                    App::GetInputEngine()->GetOisKeyboard(),
                    key,
                    0u);
                if (down)
                {
                    this->keyPressed(key_event);
                }
                else
                {
                    this->keyReleased(key_event);
                }
            }
            continue;
        }

        if (event.type == SDL_TEXTINPUT &&
            event.text.windowID == main_window_id)
        {
            // This is the sole text path on macOS; the unbuffered OIS facade
            // has text translation disabled, so composed UTF-8 is not
            // duplicated.
            if (ImGui::GetCurrentContext() != nullptr)
            {
                ImGui::GetIO().AddInputCharactersUTF8(event.text.text);
            }
            continue;
        }

        if (event.type != SDL_WINDOWEVENT ||
            event.window.windowID != main_window_id)
        {
            continue;
        }

        switch (event.window.event)
        {
        case SDL_WINDOWEVENT_CLOSE:
            if (!m_window_shutdown_requested)
            {
                m_window_shutdown_requested = true;
                App::GetGameContext()->PushMessage(
                    Message(MSG_APP_SHUTDOWN_REQUESTED));
            }
            break;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            m_render_window->resize(
                static_cast<unsigned int>(event.window.data1),
                static_cast<unsigned int>(event.window.data2));
            this->windowResized(m_render_window);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            m_render_window->setActive(true);
            this->windowFocusChange(m_render_window);
            input_engine->RefreshSdlControllerStates();
            // SDL's Cocoa text responder may have been displaced by another
            // native input client. Recreate it so both SDL_KEY* and composed
            // SDL_TEXTINPUT events continue after every focus cycle.
            SDL_StopTextInput();
            SDL_StartTextInput();
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            m_render_window->setActive(false);
            this->windowFocusChange(m_render_window);
            input_engine->ResetSdlControllerStates();
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            m_render_window->setActive(false);
            m_render_window->setVisible(false);
            input_engine->ResetSdlControllerStates();
            break;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            m_render_window->setVisible(true);
            m_render_window->setActive(true);
            input_engine->RefreshSdlControllerStates();
            break;
        default:
            break;
        }
    }
#endif
}

void AppContext::RegisterRTShaderSceneManager(Ogre::SceneManager* scene_manager)
{
#if OGRE_VERSION_MAJOR >= 14
    if (m_shader_generator != nullptr && scene_manager != nullptr)
    {
        // ShaderGenerator::addSceneManager() is explicitly idempotent.
        m_shader_generator->addSceneManager(scene_manager);
    }
#else
    (void)scene_manager;
#endif
}

#if OGRE_VERSION_MAJOR >= 14
bool AppContext::SetUpRTShaderSystem()
{
    if (m_shader_generator != nullptr)
    {
        return true;
    }

    const std::string ogre14_media_path =
        PathCombine(App::sys_resources_dir->getStr(), "ogre14");
    const std::string main_shader_lib_path =
        PathCombine(ogre14_media_path, "Main");
    const std::string rtshader_lib_path =
        PathCombine(ogre14_media_path, "RTShaderLib");
    const std::string terrain_shader_lib_path =
        PathCombine(ogre14_media_path, "Terrain");
    if (!FolderExists(main_shader_lib_path) ||
        !FolderExists(rtshader_lib_path) ||
        !FolderExists(terrain_shader_lib_path))
    {
        ErrorUtils::ShowError(
            _L("Startup error"),
            fmt::format(
                _L("OGRE 14 shader resources are missing.\nExpected '{}', '{}', and '{}'."),
                main_shader_lib_path,
                rtshader_lib_path,
                terrain_shader_lib_path));
        return false;
    }

    try
    {
        Ogre::ResourceGroupManager& resource_groups =
            Ogre::ResourceGroupManager::getSingleton();
        resource_groups.addResourceLocation(
            main_shader_lib_path,
            "FileSystem",
            Ogre::RGN_INTERNAL);
        resource_groups.addResourceLocation(
            rtshader_lib_path,
            "FileSystem",
            Ogre::RGN_INTERNAL);
        resource_groups.addResourceLocation(
            terrain_shader_lib_path,
            "FileSystem",
            Ogre::RGN_INTERNAL);

        if (!Ogre::RTShader::ShaderGenerator::initialize())
        {
            ErrorUtils::ShowError(
                _L("Startup error"),
                _L("OGRE RTShader System initialization failed."));
            return false;
        }

        m_shader_generator =
            Ogre::RTShader::ShaderGenerator::getSingletonPtr();
        m_shader_generator->setTargetLinearColours(
            m_render_window->isHardwareGammaEnabled());

        const std::string shader_cache_path =
            PathCombine(App::sys_cache_dir->getStr(), "rtshader");
        CreateFolder(App::sys_cache_dir->getStr());
        CreateFolder(shader_cache_path);
        m_shader_generator->setShaderCachePath(shader_cache_path);

        m_rtshader_material_listener =
            new OgreBites::SGTechniqueResolverListener(m_shader_generator);
        Ogre::MaterialManager::getSingleton().addListener(
            m_rtshader_material_listener);

        LOG(fmt::format(
            "[RoR|Startup|Rendering] OGRE RTShader System initialized; cache='{}'",
            shader_cache_path));
    }
    catch (const Ogre::Exception& e)
    {
        this->ShutDownRTShaderSystem();
        ErrorUtils::ShowError(
            _L("OGRE RTShader System initialization failed"),
            e.getDescription());
        return false;
    }

    return true;
}

void AppContext::ShutDownRTShaderSystem()
{
    if (m_shader_generator == nullptr)
    {
        return;
    }

    if (Ogre::MaterialManager::getSingletonPtr() != nullptr)
    {
        Ogre::MaterialManager::getSingleton().setActiveScheme(
            Ogre::MaterialManager::DEFAULT_SCHEME_NAME);
        if (m_rtshader_material_listener != nullptr)
        {
            Ogre::MaterialManager::getSingleton().removeListener(
                m_rtshader_material_listener);
        }
    }

    delete m_rtshader_material_listener;
    m_rtshader_material_listener = nullptr;

    // ShaderGenerator::destroy() unregisters every SceneManager listener while
    // Root still owns those SceneManagers. Root is deleted by our destructor
    // only after this method returns.
    Ogre::RTShader::ShaderGenerator::destroy();
    m_shader_generator = nullptr;
}
#endif

Ogre::RenderWindow* AppContext::CreateCustomRenderWindow(std::string const& window_name, int width, int height)
{
    Ogre::NameValuePairList misc;
    Ogre::ConfigOptionMap ropts = m_ogre_root->getRenderSystem()->getConfigOptions();
    misc["FSAA"] = Ogre::StringConverter::parseInt(ropts["FSAA"].currentValue, 0);
    if (m_renderer_child_owns_presentation)
        misc["hidden"] = "true";

    Ogre::RenderWindow* rw = Ogre::Root::getSingleton().createRenderWindow(window_name, width, height, false, &misc);
    if (m_renderer_child_owns_presentation)
    {
        if (!rw->isHidden())
            throw std::runtime_error(
                "OGRE 14 custom render window could not remain hidden in Ogre-Next bridge mode");
        rw->setActive(false);
    }
    return rw;
}

void AppContext::BeginPostProcessScene()
{
    m_postprocess_runtime.BeginScene(
        m_viewport,
        m_render_window,
        static_cast<PostProcessMode>(
            App::gfx_postprocess_mode->getInt()));
}

void AppContext::EndPostProcessScene()
{
    m_postprocess_runtime.EndScene();
}

void AppContext::MaintainPostProcessSceneOrder()
{
    m_postprocess_runtime.MaintainSceneCompositorOrder();
}

void AppContext::CaptureScreenshot()
{
    // Bound screenshot concurrency to one encoder and surface any previous
    // write failure on the main thread before reusing codec/runtime state.
    this->FinishPendingScreenshot();
    m_postprocess_runtime.BeforeMainWindowReadback();

    const std::time_t time = std::time(nullptr);
    const int index = (time == m_prev_screenshot_time) ? m_prev_screenshot_index+1 : 1;

    std::stringstream stamp;
    stamp << std::put_time(std::localtime(&time), "%Y-%m-%d_%H-%M-%S") << "_" << index
          << "." << App::app_screenshot_format->getStr();
    std::string path = PathCombine(App::sys_screenshot_dir->getStr(), "screenshot_") + stamp.str();

    if (App::app_screenshot_format->getStr() == "png")
    {
        AdvancedScreen png(m_render_window, path);

        png.addData("User_NickName", App::mp_player_name->getStr());
        png.addData("User_Language", App::app_language->getStr());

        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION && 
            App::GetGameContext()->GetPlayerActor())
        {
            png.addData("Truck_file", App::GetGameContext()->GetPlayerActor()->ar_filename);
            png.addData("Truck_name", App::GetGameContext()->GetPlayerActor()->getTruckName());
        }
        if (App::GetGameContext()->GetTerrain())
        {
            png.addData("Terrn_file", App::sim_terrain_name->getStr());
            png.addData("Terrn_name", App::sim_terrain_gui_name->getStr());
        }
        if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
        {
            png.addData("MP_ServerName", App::mp_server_host->getStr());
        }

        m_screenshot_write = png.write();
    }
    else
    {
        m_render_window->writeContentsToFile(path);
    }

    App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                  _L("Screenshot: ") + stamp.str());

    m_prev_screenshot_time = time;
    m_prev_screenshot_index = index;
}

void AppContext::FinishPendingScreenshot() noexcept
{
    if (!m_screenshot_write.valid())
    {
        return;
    }

    try
    {
        m_screenshot_write.get();
    }
    catch (const Ogre::Exception& e)
    {
        LOG(fmt::format(
            "[RoR|Screenshot] Asynchronous PNG write failed: {}",
            e.getDescription()));
    }
    catch (const std::exception& e)
    {
        LOG(fmt::format(
            "[RoR|Screenshot] Asynchronous PNG write failed: {}",
            e.what()));
    }
    catch (...)
    {
        LOG("[RoR|Screenshot] Asynchronous PNG write failed with an unknown exception");
    }
}

void AppContext::ActivateFullscreen(bool val)
{
    if (m_renderer_child_owns_presentation)
        return;
    if (!val && !m_windowed_fix)
    {
        // Workaround OGRE glitch - badly aligned viewport after first full->window switch
        // Observed on Win10/OpenGL (GeForce GTX 660)
        m_render_window->setFullscreen(false, m_render_window->getWidth()-1, m_render_window->getHeight()-1);    
        m_render_window->setFullscreen(false, m_render_window->getWidth()+1, m_render_window->getHeight()+1);    
    }
    else
    {
        m_render_window->setFullscreen(val, m_render_window->getWidth(), m_render_window->getHeight());
    }
}

// --------------------------
// Profiling

void AppContext::PrepareProfiler()
{
#if OGRE_PROFILING == 1 // Singleton is null otherwise

    // WORKAROUND for OGRE v14.5.2 bug - repeated `setEnabled()` true causes shutdown-reinit cycle.
    // See https://github.com/OGRECave/ogre/commit/05dd52dc7c9abfc3a15734dab59deede7288404a
    if (App::diag_profiler_enabled->getBool() != m_profiler_enabled)
    {
        m_profiler_enabled = App::diag_profiler_enabled->getBool();
        Ogre::Profiler::getSingleton().setEnabled(m_profiler_enabled);
    }

    if (App::diag_profiler_rate->getInt() < 1) // Check valid uint & Avoid division by zero in OGRE
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format(_L("Invalid profiler update rate ({}), resetting to default"), App::diag_profiler_rate->getInt()));
        App::diag_profiler_rate->setVal(10); // Default in OGRE
    }
    Ogre::Profiler::getSingleton().setUpdateDisplayFrequency((Ogre::uint)App::diag_profiler_rate->getInt());
#endif
}

// --------------------------
// Program paths and logging

bool AppContext::SetUpProgramPaths()
{
    // Process directory
    std::string exe_path = RoR::GetExecutablePath();
#if defined(_WIN32)
    // The public renderer launcher resolves its child through the extended
    // Win32 namespace. OGRE's filesystem archive appends resource names with
    // '/', which cannot be normalized under that namespace. Restore the
    // conventional drive/UNC spelling before deriving any runtime roots.
    exe_path =
        RoR::PlatformUtilsDetail::NormalizeWindowsExtendedPathForRuntime(
            exe_path);
#endif
    if (exe_path.empty())
    {
        ErrorUtils::ShowError(_L("Startup error"), _L("Error while retrieving program directory path"));
        return false;
    }
    App::sys_process_dir->setStr(RoR::GetParentDirectory(exe_path.c_str()).c_str());

    // RoR's home directory
    std::string local_userdir = PathCombine(App::sys_process_dir->getStr(), "config"); // TODO: Think of a better name, this is ambiguious with ~/.rigsofrods/config which stores configfiles! ~ only_a_ptr, 02/2018
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE
    std::string user_home = RoR::GetUserHomeDirectory();
    if (user_home.empty())
    {
        ErrorUtils::ShowError(_L("Startup error"), _L("Error while retrieving user directory path"));
        return false;
    }

    const std::string application_support_parent =
        PathCombine(user_home, "Library/Application Support");
    const std::string application_support_dir =
        PathCombine(application_support_parent, "Rigs of Rods");
    const std::string legacy_user_dir =
        PathCombine(user_home, "RigsOfRods");

    PlatformUtilsDetail::MacOSUserDirectoryState directory_state;
    directory_state.process_config_exists = FolderExists(local_userdir);
    directory_state.application_support_exists =
        FolderExists(application_support_dir);
    directory_state.legacy_user_data_exists =
        FolderExists(PathCombine(legacy_user_dir, "config")) ||
        FolderExists(PathCombine(legacy_user_dir, "mods")) ||
        FolderExists(PathCombine(legacy_user_dir, "packs")) ||
        FolderExists(PathCombine(legacy_user_dir, "terrains")) ||
        FolderExists(PathCombine(legacy_user_dir, "vehicles")) ||
        FolderExists(PathCombine(legacy_user_dir, "projects")) ||
        FolderExists(PathCombine(legacy_user_dir, "savegames"));

    const PlatformUtilsDetail::MacOSUserDirectoryLayout directory_layout =
        PlatformUtilsDetail::ResolveMacOSUserDirectoryLayout(
            App::sys_process_dir->getStr(),
            user_home,
            directory_state);

    if (PlatformUtilsDetail::IsMacOSAppBundleProcessDirectory(
            App::sys_process_dir->getStr()))
    {
        // mkdir() only creates one component, so ensure each conventional
        // Library parent exists before creating the product directories.
        const std::string library_dir = PathCombine(user_home, "Library");
        const std::string caches_parent = PathCombine(library_dir, "Caches");
        const std::string logs_parent = PathCombine(library_dir, "Logs");
        CreateFolder(library_dir);
        CreateFolder(application_support_parent);
        CreateFolder(caches_parent);
        CreateFolder(logs_parent);
    }

    CreateFolder(directory_layout.user_dir);
    CreateFolder(directory_layout.cache_dir);
    CreateFolder(directory_layout.thumbnails_dir);
    CreateFolder(directory_layout.logs_dir);
    App::sys_user_dir->setStr(directory_layout.user_dir);
    App::sys_cache_dir->setStr(directory_layout.cache_dir);
    App::sys_thumbnails_dir->setStr(directory_layout.thumbnails_dir);
    App::sys_logs_dir->setStr(directory_layout.logs_dir);
#else
    if (FolderExists(local_userdir))
    {
        // It's a portable installation
        App::sys_user_dir->setStr(local_userdir.c_str());
    }
    else
    {
        // Default location - user's home directory
        std::string user_home = RoR::GetUserHomeDirectory();
        if (user_home.empty())
        {
            ErrorUtils::ShowError(_L("Startup error"), _L("Error while retrieving user directory path"));
            return false;
        }
        RoR::Str<500> ror_homedir;
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
        ror_homedir << user_home << PATH_SLASH << "My Games";
        CreateFolder(ror_homedir.ToCStr());
        ror_homedir << PATH_SLASH << "Rigs of Rods";
#elif OGRE_PLATFORM == OGRE_PLATFORM_LINUX
        char* env_SNAP = getenv("SNAP_USER_COMMON");
        if(env_SNAP)
            ror_homedir = env_SNAP;
        else
            ror_homedir << user_home << PATH_SLASH << ".rigsofrods";
#endif
        CreateFolder(ror_homedir.ToCStr ());
        App::sys_user_dir->setStr(ror_homedir.ToCStr ());
    }
#endif

    return true;
}

void AppContext::SetUpLogging()
{
    std::string logs_dir = App::sys_logs_dir->getStr();
    if (logs_dir.empty())
    {
        logs_dir = PathCombine(App::sys_user_dir->getStr(), "logs");
    }
    CreateFolder(logs_dir);
    App::sys_logs_dir->setStr(logs_dir.c_str());

    auto ogre_log_manager = OGRE_NEW Ogre::LogManager();
    std::string rorlog_path = PathCombine(logs_dir, "RoR.log");
    Ogre::Log* rorlog = ogre_log_manager->createLog(rorlog_path, true, true);
    rorlog->stream() << "[RoR] Rigs of Rods (www.rigsofrods.org) version " << ROR_VERSION_STRING;
    std::time_t t = std::time(nullptr);
    rorlog->stream() << "[RoR] Current date: " << std::put_time(std::localtime(&t), "%Y-%m-%d");

    rorlog->addListener(App::GetConsole());  // Allow console to intercept log messages
}

bool AppContext::SetUpResourcesDir()
{
    std::string process_dir = PathCombine(App::sys_process_dir->getStr(), "resources");
#if OGRE_PLATFORM == OGRE_PLATFORM_LINUX
    if (!FolderExists(process_dir))
    {
        process_dir = "/usr/share/rigsofrods/resources/";
    }
#endif
    if (!FolderExists(process_dir))
    {
        ErrorUtils::ShowError(_L("Startup error"), _L("Resources folder not found. Check if correctly installed."));
        return false;
    }
    App::sys_resources_dir->setStr(process_dir);
    return true;
}

bool AppContext::SetUpConfigSkeleton()
{
    Ogre::String src_path = PathCombine(App::sys_resources_dir->getStr(), "skeleton.zip");
    Ogre::ResourceGroupManager::getSingleton().addResourceLocation(src_path, "Zip", "SrcRG");
    Ogre::FileInfoListPtr fl = Ogre::ResourceGroupManager::getSingleton().findResourceFileInfo("SrcRG", "*", true);
    if (fl->empty())
    {
        ErrorUtils::ShowError(_L("Startup error"), _L("Faulty resource folder. Check if correctly installed."));
        return false;
    }
    Ogre::String dst_path = PathCombine(App::sys_user_dir->getStr(), "");
    for (auto file : *fl)
    {
        CreateFolder(dst_path + file.basename);
    }
    fl = Ogre::ResourceGroupManager::getSingleton().findResourceFileInfo("SrcRG", "*");
    if (fl->empty())
    {
        ErrorUtils::ShowError(_L("Startup error"), _L("Faulty resource folder. Check if correctly installed."));
        return false;
    }
    Ogre::ResourceGroupManager::getSingleton().addResourceLocation(dst_path, "FileSystem", "DstRG", false, false);
    for (auto file : *fl)
    {
        if (file.uncompressedSize == 0)
            continue;
        Ogre::String path = file.path + file.basename;
        if (!Ogre::ResourceGroupManager::getSingleton().findResourceFileInfo("DstRG", path)->empty())
            continue;
        Ogre::DataStreamPtr src_ds = Ogre::ResourceGroupManager::getSingleton().openResource(path, "SrcRG");
        Ogre::DataStreamPtr dst_ds = Ogre::ResourceGroupManager::getSingleton().createResource(path, "DstRG");
        std::vector<char> buf(src_ds->size());
        size_t read = src_ds->read(buf.data(), src_ds->size());
        if (read > 0)
        {
            dst_ds->write(buf.data(), read);
        }
    }
    Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("SrcRG");
    Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("DstRG");

    return true;
}

void AppContext::SetUpObsoleteConfMarker()
{
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
    Ogre::String old_ror_homedir = Ogre::StringUtil::format("%s\\Rigs of Rods 0.4", RoR::GetUserHomeDirectory().c_str());
    if(FolderExists(old_ror_homedir))
    {
        if (!FileExists(Ogre::StringUtil::format("%s\\OBSOLETE_FOLDER.txt", old_ror_homedir.c_str())))
        {
            Ogre::String obsoleteMessage = Ogre::StringUtil::format("This folder is obsolete, please move your mods to  %s", App::sys_user_dir->getStr());
            try
            {
                Ogre::ResourceGroupManager::getSingleton().addResourceLocation(old_ror_homedir, "FileSystem", "homedir", false, false);
                Ogre::DataStreamPtr stream = Ogre::ResourceGroupManager::getSingleton().createResource("OBSOLETE_FOLDER.txt", "homedir");
                stream->write(obsoleteMessage.c_str(), obsoleteMessage.length());
                Ogre::ResourceGroupManager::getSingleton().destroyResourceGroup("homedir");
            }
            catch (std::exception & e)
            {
                RoR::LogFormat("Error writing to %s, message: '%s'", old_ror_homedir.c_str(), e.what());
            }
            Ogre::String message = Ogre::StringUtil::format(
                "Welcome to Rigs of Rods %s\nPlease note that the mods folder has moved to:\n\"%s\"\nPlease move your mods to the new folder to continue using them",
                ROR_VERSION_STRING_SHORT,
                App::sys_user_dir->getStr()
            );

            RoR::App::GetGuiManager()->ShowMessageBox("Obsolete folder detected", message.c_str());
        }
    }
#endif // OGRE_PLATFORM_WIN32
}

void AppContext::SetUpThreads()
{
    m_mainthread_id = std::this_thread::get_id();

    // This cannot be done earlier as it uses the above thread ID to assert() against invalid access.
    App::GetGameContext()->m_dummy_cache_selection = new CacheEntry();
}
