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

#include "Actor.h"
#include "Application.h"
#include "ApplicationFatalError.h"
#include "AppContext.h"
#include "CacheSystem.h"
#include "CameraManager.h"
#include "ChatSystem.h"
#include "Collisions.h"
#include "Console.h"
#include "ContentManager.h"
#include "DiscordRpc.h"
#include "Engine.h"
#include "ErrorUtils.h"
#include "FrameTimeBudget.h"
#include "GameContext.h"
#include "GfxScene.h"
#include "GUI_DirectionArrow.h"
#include "GUI_FrictionSettings.h"
#include "GUI_GameControls.h"
#include "GUI_LoadingWindow.h"
#include "GUI_MainSelector.h"
#include "GUI_MessageBox.h"
#include "GUI_MultiplayerSelector.h"
#include "GUI_MultiplayerClientList.h"
#include "GUI_RepositorySelector.h"
#include "GUI_VehicleInfoTPanel.h"
#include "GUIManager.h"
#include "GUIUtils.h"
#include "InputEngine.h"
#include "Language.h"
#include "MumbleIntegration.h"
#include "OutGauge.h"
#include "OverlayWrapper.h"
#include "PlatformUtils.h"
#ifdef ROR_ENABLE_INTERNAL_TEST_HOOKS
#include "RigDef_TestHooks.h"
#endif
#include "RoRVersion.h"
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
#include "RendererCombinedApplicationMode.h"
#include "RendererInProcessSession.h"
#include "RendererOgreNextInProcessPresenter.h"
#include "gfx/render/NativeVisualShowcaseSceneSource.h"
#include "system/detail/OgreNextDemoInProcessFramePolicy.h"
#else
#include "RendererOgre14GameBridge.h"
#include "RendererOgre14ProductSession.h"
#endif
#include "RendererGameInputEngineTarget.h"
#include "RendererOgre14RuntimeOwnership.h"
#include "gfx/Ogre14GuiOverlayCapture.h"
#include "gfx/render/Ogre14GraphicsSceneSource.h"
#include "ScriptEngine.h"
#include "Skidmark.h"
#include "SoundScriptManager.h"
#include "Terrain.h"
#include "Utils.h"
#include <Overlay/OgreOverlaySystem.h>
#include <array>
#include <ctime>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#if defined(_WIN32) && defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <DbgHelp.h>
#include <windows.h>
#endif

#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
namespace RoR {
int RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(
    int argc,
    char** argv);
} // namespace RoR
#endif
#include <thread>
#include <vector>
#include <fstream>

#ifdef USE_CURL
#   include <curl/curl.h>
#endif //USE_CURL

namespace
{

#if defined(_WIN32) && defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
constexpr wchar_t kWindowsCrashDumpEnvironment[] =
    L"ROR_WINDOWS_CRASH_DUMP_PATH";
std::array<wchar_t, 32768U> g_windows_crash_dump_path{};

LONG WINAPI RetainWindowsCrashDump(EXCEPTION_POINTERS* exception) noexcept
{
    HANDLE file = ::CreateFileW(
        g_windows_crash_dump_path.data(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = ::GetCurrentThreadId();
        information.ExceptionPointers = exception;
        information.ClientPointers = FALSE;
        (void)::MiniDumpWriteDump(
            ::GetCurrentProcess(), ::GetCurrentProcessId(), file,
            MiniDumpNormal, exception == nullptr ? nullptr : &information,
            nullptr, nullptr);
        (void)::FlushFileBuffers(file);
        (void)::CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void ConfigureWindowsCrashDump() noexcept
{
    const DWORD capacity =
        static_cast<DWORD>(g_windows_crash_dump_path.size());
    const DWORD length = ::GetEnvironmentVariableW(
        kWindowsCrashDumpEnvironment, g_windows_crash_dump_path.data(),
        capacity);
    if (length == 0U || length >= capacity)
    {
        g_windows_crash_dump_path[0] = L'\0';
        return;
    }
    const bool drive_absolute =
        length >= 3U && g_windows_crash_dump_path[1] == L':' &&
        (g_windows_crash_dump_path[2] == L'\\' ||
         g_windows_crash_dump_path[2] == L'/');
    const bool unc_absolute =
        length >= 3U &&
        (g_windows_crash_dump_path[0] == L'\\' ||
         g_windows_crash_dump_path[0] == L'/') &&
        (g_windows_crash_dump_path[1] == L'\\' ||
         g_windows_crash_dump_path[1] == L'/');
    if (!drive_absolute && !unc_absolute)
    {
        g_windows_crash_dump_path[0] = L'\0';
        return;
    }
    (void)::SetUnhandledExceptionFilter(&RetainWindowsCrashDump);
}
#endif

void ReleaseWindowBoundRuntime(
    Ogre::OverlaySystem*& overlay_system) noexcept
{
    using namespace RoR;

    const bool had_window_bound_state =
        App::GetInputEngine() != nullptr ||
        App::GetGuiManager() != nullptr ||
        overlay_system != nullptr;
    bool clean_release = true;

    clean_release =
        App::GetAppContext()->DetachRenderWindowEvents() && clean_release;
    try
    {
        if (App::GetInputEngine() != nullptr)
        {
            App::DestroyInputEngine();
        }
    }
    catch (...)
    {
        clean_release = false;
    }
    try
    {
        if (App::GetGuiManager() != nullptr)
        {
            App::GetGuiManager()->ShutdownMyGUI();
        }
    }
    catch (...)
    {
        clean_release = false;
    }
    try
    {
        if (overlay_system != nullptr)
        {
            if (App::GetGfxScene()->GetSceneManager() != nullptr)
            {
                App::GetGfxScene()->GetSceneManager()
                    ->removeRenderQueueListener(overlay_system);
            }
            delete overlay_system;
            overlay_system = nullptr;
        }
    }
    catch (...)
    {
        clean_release = false;
    }

    if (had_window_bound_state)
    {
        LOG(clean_release
            ? "[RoR|Shutdown] Window-bound runtime integrations released"
            : "[RoR|Shutdown] ERROR releasing window-bound runtime integrations");
    }
}

bool ReleaseWorkerRuntime() noexcept
{
    using namespace RoR;

    const bool had_general_workers = App::GetThreadPool() != nullptr;
    bool clean_release =
        App::GetGameContext()->GetActorManager()->ShutdownWorkerRuntime();
    clean_release = App::DestroyThreadPool() && clean_release;

    if (had_general_workers && Ogre::LogManager::getSingletonPtr() != nullptr)
    {
        try
        {
            LOG(clean_release
                ? "[RoR|Shutdown] Physics and graphics worker pools released"
                : "[RoR|Shutdown] ERROR releasing physics and graphics worker pools");
        }
        catch (...)
        {
            // Final shutdown must not throw from a diagnostic path.
        }
    }
    return clean_release;
}

bool ReleaseWorkerRuntimeForGate(void*) noexcept
{
    return ReleaseWorkerRuntime();
}

bool ReleaseFatalSceneRuntime(void*) noexcept
{
    using namespace RoR;

    try
    {
        App::GetAppContext()->EndPostProcessScene();
    }
    catch (...)
    {
        return false;
    }
    return App::GetGameContext()->ShutdownSceneForFatalError();
}

[[noreturn]] void FailStopApplication(int exit_code) noexcept
{
    // Worker or scene ownership could not be proven quiescent. Bypass every
    // local/static destructor so neither Ogre::Root nor immutable archive
    // owners are torn down underneath a surviving callback or scene object.
    std::fflush(nullptr);
    std::_Exit(exit_code);
}

void ReleaseRendererRuntime() noexcept
{
    using namespace RoR;

    const bool had_renderer_root =
        App::GetAppContext()->GetOgreRoot() != nullptr;
    bool clean_release = App::GetGfxScene()->GetEnvMap().Shutdown();

    if (had_renderer_root && Ogre::LogManager::getSingletonPtr() != nullptr)
    {
        try
        {
            LOG(clean_release
                ? "[RoR|Shutdown] Environment map shutdown returned"
                : "[RoR|Shutdown] ERROR environment map shutdown returned");
        }
        catch (...)
        {
            clean_release = false;
        }
    }

    clean_release =
        App::GetAppContext()->ShutdownRendering() && clean_release;
    if (had_renderer_root && Ogre::LogManager::getSingletonPtr() != nullptr)
    {
        try
        {
            LOG(clean_release
                ? "[RoR|Shutdown] Renderer runtime released"
                : "[RoR|Shutdown] ERROR releasing renderer runtime");
        }
        catch (...)
        {
            // The native smoke requires this marker and remains fail-closed.
        }
    }
}

class WindowBoundRuntimeGuard
{
public:
    explicit WindowBoundRuntimeGuard(Ogre::OverlaySystem*& overlay_system):
        m_overlay_system(overlay_system)
    {
    }

    ~WindowBoundRuntimeGuard()
    {
        ReleaseWindowBoundRuntime(m_overlay_system);
    }

    WindowBoundRuntimeGuard(const WindowBoundRuntimeGuard&) = delete;
    WindowBoundRuntimeGuard& operator=(const WindowBoundRuntimeGuard&) = delete;

private:
    Ogre::OverlaySystem*& m_overlay_system;
};

class WorkerRuntimeGuard
{
public:
    WorkerRuntimeGuard():
        m_release_gate(&ReleaseWorkerRuntimeForGate)
    {
    }

    ~WorkerRuntimeGuard()
    {
        if (RoR::ResolveApplicationRuntimeShutdownGate(m_release_gate) ==
            RoR::ApplicationFatalShutdownDisposition::FAIL_STOP)
        {
            // This destructor precedes the window/renderer guards in reverse
            // declaration order. Never let either unwind while a worker may
            // still own callbacks into Ogre or ContentManager.
            FailStopApplication(EXIT_FAILURE);
        }
    }

    bool Release() noexcept { return m_release_gate.Release(); }

    WorkerRuntimeGuard(const WorkerRuntimeGuard&) = delete;
    WorkerRuntimeGuard& operator=(const WorkerRuntimeGuard&) = delete;

private:
    RoR::ApplicationFatalShutdownGate m_release_gate;
};

class RendererRuntimeGuard
{
public:
    RendererRuntimeGuard() = default;

    ~RendererRuntimeGuard()
    {
        ReleaseRendererRuntime();
    }

    RendererRuntimeGuard(const RendererRuntimeGuard&) = delete;
    RendererRuntimeGuard& operator=(const RendererRuntimeGuard&) = delete;
};

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
constexpr std::uint64_t kCombinedRendererAssetRegistryId =
    0x524f52434f4d4231ULL; // "RORCOMB1", process-local and transport-free.
constexpr std::uint64_t kCombinedRendererShutdownAttemptNanoseconds =
    500'000'000ULL;

bool ParseCombinedExactDrawableExtent(
    const char* text,
    std::uint32_t& width,
    std::uint32_t& height) noexcept
{
    if (text == nullptr)
    {
        return false;
    }
    const std::string extent(text);
    const std::size_t separator = extent.find('x');
    if (separator == std::string::npos || separator == 0U ||
        separator + 1U >= extent.size() ||
        extent.find('x', separator + 1U) != std::string::npos)
    {
        return false;
    }
    const std::string width_text = extent.substr(0U, separator);
    const std::string height_text = extent.substr(separator + 1U);
    if (width_text.find_first_not_of("0123456789") != std::string::npos ||
        height_text.find_first_not_of("0123456789") != std::string::npos)
    {
        return false;
    }
    try
    {
        const unsigned long long parsed_width = std::stoull(width_text);
        const unsigned long long parsed_height = std::stoull(height_text);
        if (parsed_width == 0U || parsed_height == 0U ||
            parsed_width > 32768U || parsed_height > 32768U)
        {
            return false;
        }
        width = static_cast<std::uint32_t>(parsed_width);
        height = static_cast<std::uint32_t>(parsed_height);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ResolveCombinedPresenterConfiguration(
    RoR::RendererOgreNextInProcessPresenterConfiguration& configuration,
    std::string& failure_detail)
{
    using namespace RoR;

    // Raw and installed combined builds share one authenticated package
    // layout beneath RoR's executable-derived ordinary resources directory.
    // No build root, cwd, environment variable, renderer selector, or legacy
    // fallback participates in media resolution.
    const std::string packaged_media_root = PathCombine(
        App::sys_resources_dir->getStr().c_str(),
        "ogrenext");
    const std::string shader_media_root =
        PathCombine(packaged_media_root, "ShaderMedia");
    const std::string presentation_media_root =
        PathCombine(packaged_media_root, "Presentation");
    const std::string expected_media = packaged_media_root;
    if (!FolderExists(shader_media_root) ||
        !FolderExists(presentation_media_root))
    {
        failure_detail = fmt::format(
            "authenticated OgreNext media was not staged at {}",
            expected_media);
        return false;
    }
    configuration.shader_media_root = shader_media_root;
    configuration.presentation_media_root = presentation_media_root;

    // The isolated performance/image harness already binds its home and exact
    // requested backing extent through this pair. Ordinary launches ignore a
    // stray extent value. Admitted harness launches hand the exact pixel pair
    // to the Ogre-Next window owner before frontend creation so Retina scaling
    // cannot silently double the measured surface.
    const char* d0_scene_home = std::getenv("ROR_D0_SCENE_HOME");
    const char* d0_exact_window_extent =
        std::getenv("ROR_D0_EXACT_WINDOW_EXTENT");
    if (d0_scene_home != nullptr && IsAbsolutePath(d0_scene_home) &&
        d0_exact_window_extent != nullptr)
    {
        if (!ParseCombinedExactDrawableExtent(
                d0_exact_window_extent,
                configuration.exact_drawable_width,
                configuration.exact_drawable_height))
        {
            failure_detail = fmt::format(
                "isolated exact OgreNext drawable extent is malformed: {}",
                d0_exact_window_extent);
            return false;
        }
    }
    return true;
}

class CombinedPresenterWindowGuard
{
public:
    explicit CombinedPresenterWindowGuard(
        RoR::RendererOgreNextInProcessPresenter& presenter) noexcept:
        m_presenter(presenter)
    {
    }

    ~CombinedPresenterWindowGuard()
    {
        // This guard is declared before RendererRuntimeGuard, so it runs only
        // after Ogre 14 has destroyed its hidden SDL resource window. Session
        // shutdown normally quiesces first; the explicit call covers every
        // early-startup return without reaching into renderer internals.
        m_presenter.ShutdownEventPump();
        (void)m_presenter.ProtectHiddenResourceWindow(nullptr);
        if (m_presenter.prepared())
        {
            (void)m_presenter.ShutdownWindow();
        }
    }

    CombinedPresenterWindowGuard(const CombinedPresenterWindowGuard&) = delete;
    CombinedPresenterWindowGuard& operator=(const CombinedPresenterWindowGuard&) = delete;

private:
    RoR::RendererOgreNextInProcessPresenter& m_presenter;
};

RoR::RendererInProcessSessionResult CloseCombinedRendererSession(
    RoR::RendererInProcessSession& session) noexcept
{
    using namespace RoR;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    RendererInProcessSessionResult result = session.Shutdown();
    while ((result.status ==
                RendererInProcessSessionStatus::PENDING_BACKPRESSURE ||
            result.status ==
                RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE ||
            (result.status ==
                 RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN &&
             result.frontend_code ==
                 Render::RenderOperationCode::TIMEOUT)) &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result = session.Shutdown();
    }
    return result;
}

bool PumpCombinedRendererLoadingWindow(void* opaque) noexcept
{
    using namespace RoR;

    auto* const session_owner =
        static_cast<std::unique_ptr<RendererInProcessSession>*>(opaque);
    if (session_owner == nullptr || *session_owner == nullptr ||
        !(*session_owner)->active())
    {
        return false;
    }
    RendererInProcessSession& session = **session_owner;
    if (session.asset_sequence() != 0U ||
        session.last_consumed_scene_snapshot_id() != 0U ||
        session.last_frontend_frame_id() != 0U)
    {
        return false;
    }
    const RendererInProcessSessionResult pumped =
        session.PresentBootstrapFrame();
    switch (pumped.status)
    {
    case RendererInProcessSessionStatus::BOOTSTRAP_PRESENTED:
    case RendererInProcessSessionStatus::WAITING_FOR_SURFACE:
    case RendererInProcessSessionStatus::PENDING_BACKPRESSURE:
    case RendererInProcessSessionStatus::PENDING_FRONTEND_SURFACE:
    case RendererInProcessSessionStatus::SHUTDOWN_REQUESTED:
        return true;
    default:
        break;
    }
    try
    {
        LOG(fmt::format(
            "[RoR|RendererCombined|Loading] Native loading pump stopped: "
            "status='{}', frontend={}, field='{}', detail='{}'",
            ToString(pumped.status),
            static_cast<unsigned int>(pumped.frontend_code),
            pumped.validation.field,
            pumped.validation.detail));
    }
    catch (...)
    {
    }
    return false;
}
#endif

/// Read one non-archived numeric budget CVar. An unparsable or trailing-junk
/// value is refused instead of silently becoming its default.
bool ReadFrameBudgetDouble(
    RoR::CVar* cvar,
    const char* name,
    double& value)
{
    const std::string text = cvar->getStr();
    try
    {
        std::size_t consumed = 0U;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size())
        {
            RoR::LogFormat(
                "[RoR|Perf] Refusing trailing characters in %s='%s'",
                name, text.c_str());
            return false;
        }
        value = parsed;
        return true;
    }
    catch (...)
    {
        RoR::LogFormat(
            "[RoR|Perf] Refusing malformed %s='%s'", name, text.c_str());
        return false;
    }
}

bool ReadFrameBudgetUnsigned(
    RoR::CVar* cvar,
    const char* name,
    std::uint64_t& value)
{
    const std::string text = cvar->getStr();
    if (text.empty() ||
            text.find_first_not_of("0123456789") != std::string::npos)
    {
        RoR::LogFormat(
            "[RoR|Perf] Refusing malformed %s='%s'", name, text.c_str());
        return false;
    }
    try
    {
        value = std::stoull(text);
        return true;
    }
    catch (...)
    {
        RoR::LogFormat(
            "[RoR|Perf] Refusing out-of-range %s='%s'", name, text.c_str());
        return false;
    }
}

/// Build the playable frame-time recorder from the explicit CVar contract.
/// Any refusal returns a null session, and a refused `gate` request also
/// reports the failure so an automated run cannot mistake a missing recorder
/// for a passing budget.
struct FrameTimeBudgetPresentationSurface final
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool fullscreen = false;
    bool vsync = false;

    bool valid() const noexcept
    {
        return width > 0U && height > 0U;
    }
};

std::unique_ptr<RoR::FrameTimeBudgetSession> CreateFrameTimeBudgetSession(
    Ogre::RenderWindow* render_window,
    const FrameTimeBudgetPresentationSurface* presentation_surface,
    bool presents_frames,
    bool& refused)
{
    using namespace RoR;

    refused = false;

    const std::string mode_name = App::gfx_frame_budget_mode->getStr();
    FrameTimeBudgetMode mode = FrameTimeBudgetMode::OFF;
    if (!ParseFrameTimeBudgetMode(mode_name, mode))
    {
        LogFormat(
            "[RoR|Perf] Refusing unknown gfx_frame_budget_mode='%s'",
            mode_name.c_str());
        refused = true;
        return nullptr;
    }
    if (mode == FrameTimeBudgetMode::OFF)
    {
        return nullptr;
    }

    FrameTimeBudgetLimits limits;
    std::uint64_t warmup = 0U;
    std::uint64_t minimum = 0U;
    std::uint64_t requested = 0U;
    std::uint64_t percentile = 0U;
    if (!ReadFrameBudgetUnsigned(
                App::gfx_frame_budget_warmup_frames,
                "gfx_frame_budget_warmup_frames", warmup) ||
            !ReadFrameBudgetUnsigned(
                App::gfx_frame_budget_minimum_frames,
                "gfx_frame_budget_minimum_frames", minimum) ||
            !ReadFrameBudgetUnsigned(
                App::gfx_frame_budget_requested_frames,
                "gfx_frame_budget_requested_frames", requested) ||
            !ReadFrameBudgetUnsigned(
                App::gfx_frame_budget_percentile,
                "gfx_frame_budget_percentile", percentile) ||
            !ReadFrameBudgetDouble(
                App::gfx_frame_budget_sustained_ms,
                "gfx_frame_budget_sustained_ms", limits.sustained_ms) ||
            !ReadFrameBudgetDouble(
                App::gfx_frame_budget_percentile_ms,
                "gfx_frame_budget_percentile_ms", limits.percentile_ms))
    {
        refused = true;
        return nullptr;
    }

    if (warmup > kFrameTimeBudgetMaximumFrames ||
            minimum > kFrameTimeBudgetMaximumFrames ||
            percentile > 100U)
    {
        LOG("[RoR|Perf] Refusing out-of-range frame budget frame counts");
        refused = true;
        return nullptr;
    }

    limits.warmup_frames = static_cast<std::uint32_t>(warmup);
    limits.minimum_frames = static_cast<std::uint32_t>(minimum);
    limits.requested_frames = requested;
    limits.percentile = static_cast<std::uint32_t>(percentile);
    if (!limits.valid())
    {
        LOG("[RoR|Perf] Refusing an invalid frame budget contract");
        refused = true;
        return nullptr;
    }

    // The receipt path is created exclusively at the end of the run. Refusing
    // an existing path here keeps a stale receipt from being read as evidence
    // for this run, and refusing a missing path keeps a gated run from
    // producing no machine-readable result at all.
    const std::string receipt_path =
        App::gfx_frame_budget_receipt_path->getStr();
    if (mode == FrameTimeBudgetMode::GATE && receipt_path.empty())
    {
        LOG("[RoR|Perf] A gated run requires gfx_frame_budget_receipt_path");
        refused = true;
        return nullptr;
    }
    if (!receipt_path.empty() && RoR::FileExists(receipt_path))
    {
        LogFormat(
            "[RoR|Perf] Refusing an existing receipt path '%s'",
            receipt_path.c_str());
        refused = true;
        return nullptr;
    }

    // The terrain and actor stay empty here: this loop's own message queue
    // loads them, so they are named by the first recorded frame instead.
    FrameTimeBudgetContext context;
    context.scenario_id = App::gfx_frame_budget_scenario_id->getStr();
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    context.renderer = "ogre-next-combined";
    context.requires_native_scene_draw_metrics = true;
#else
    context.renderer = "ogre14";
#endif
    context.fps_limit = App::gfx_fps_limit->getInt();
    context.presents_frames = presents_frames;
    if (presentation_surface != nullptr)
    {
        if (!presentation_surface->valid())
        {
            Log("[RoR|Perf] Refusing an invalid native presentation surface");
            refused = true;
            return nullptr;
        }
        context.width = presentation_surface->width;
        context.height = presentation_surface->height;
        context.fullscreen = presentation_surface->fullscreen;
        context.vsync = presentation_surface->vsync;
    }
    else if (render_window != nullptr)
    {
        context.width = static_cast<std::uint32_t>(render_window->getWidth());
        context.height = static_cast<std::uint32_t>(render_window->getHeight());
        context.fullscreen = render_window->isFullScreen();
        context.vsync = render_window->isVSyncEnabled();
    }

    // Writing a setting is not the same as it taking effect: the config
    // parser maps enum-valued graphics settings from display strings and
    // silently substitutes the first enumerator for anything it does not
    // recognize. Emit the effective values so a recorded budget can never be
    // read as describing settings the run did not actually use.
    LogFormat(
        "[RoR|Perf] Graphics:"
        " gfx_shadow_type=%d gfx_shadow_quality=%d gfx_texture_filter=%d"
        " gfx_anisotropy=%d gfx_vegetation_mode=%d gfx_water_mode=%d"
        " gfx_sky_mode=%d gfx_envmap_enabled=%d gfx_envmap_rate=%d"
        " gfx_particles_mode=%d gfx_skidmarks_mode=%d gfx_sight_range=%d"
        " gfx_postprocess_mode=%d gfx_auto_lod=%d",
        App::gfx_shadow_type->getInt(),
        App::gfx_shadow_quality->getInt(),
        App::gfx_texture_filter->getInt(),
        App::gfx_anisotropy->getInt(),
        App::gfx_vegetation_mode->getInt(),
        App::gfx_water_mode->getInt(),
        App::gfx_sky_mode->getInt(),
        App::gfx_envmap_enabled->getBool() ? 1 : 0,
        App::gfx_envmap_rate->getInt(),
        App::gfx_particles_mode->getInt(),
        App::gfx_skidmarks_mode->getInt(),
        App::gfx_sight_range->getInt(),
        App::gfx_postprocess_mode->getInt(),
        App::gfx_auto_lod->getBool() ? 1 : 0);

    LogFormat(
        "[RoR|Perf] Frame budget armed: mode=%s sustained_ms=%.4f "
        "p%u_ms=%.4f warmup=%llu minimum=%llu requested=%llu "
        "resolution=%ux%u fps_limit=%d vsync=%d",
        ToString(mode),
        limits.sustained_ms,
        static_cast<unsigned int>(limits.percentile),
        limits.percentile_ms,
        static_cast<unsigned long long>(limits.warmup_frames),
        static_cast<unsigned long long>(limits.minimum_frames),
        static_cast<unsigned long long>(limits.requested_frames),
        static_cast<unsigned int>(context.width),
        static_cast<unsigned int>(context.height),
        context.fps_limit,
        context.vsync ? 1 : 0,
        context.presents_frames ? 1 : 0);

    return std::unique_ptr<FrameTimeBudgetSession>(
        new FrameTimeBudgetSession(mode, limits, context));
}

/// Log the summary and retain the receipt. A gated run that cannot retain its
/// receipt is reported as a failure, never as a silent pass.
bool FinalizeFrameTimeBudgetSession(
    RoR::FrameTimeBudgetSession& session)
{
    using namespace RoR;

    // Re-observe the scene so a map reset or actor change inside the recorded
    // window is reported instead of being averaged into one distribution.
    if (session.RecordingStarted())
    {
        session.ObserveSceneIdentity(
            App::sim_terrain_name->getStr(),
            App::cli_preset_vehicle->getStr());
    }

    const FrameTimeBudgetReport report = session.Finalize();
    LOG(FormatFrameTimeBudgetSummary(report));

    const std::string receipt_path =
        App::gfx_frame_budget_receipt_path->getStr();
    if (receipt_path.empty())
    {
        return report.mode != FrameTimeBudgetMode::GATE;
    }

    const FrameTimeBudgetWriteResult written = WriteFrameTimeBudgetReceipt(
        receipt_path, SerializeFrameTimeBudgetReport(report));
    if (written != FrameTimeBudgetWriteResult::WRITTEN)
    {
        LogFormat(
            "[RoR|Perf] Failed to retain the frame-time receipt at '%s' (%s)",
            receipt_path.c_str(),
            written == FrameTimeBudgetWriteResult::EXISTS
                ? "already exists"
                : "write failed");
        return false;
    }
    LogFormat(
        "[RoR|Perf] Retained the frame-time receipt at '%s'",
        receipt_path.c_str());
    return report.mode != FrameTimeBudgetMode::GATE || report.passed();
}

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
/// Binds one real visible-window key press and release to the authoritative
/// truck input state and to two later completed Ogre-Next scene frames.  This
/// is deliberately passive: normal launches never synthesize input, and no
/// test hook can mint the receipt from inside the game process.
class RendererCombinedActorControlQualification final
{
public:
    void ObserveResolvedInput(
        const RoR::RendererGameInputEngineAudit& input,
        const RoR::ActorPtr& actor) noexcept
    {
        using namespace RoR;
        if (qualified_ || actor == nullptr || actor->ar_driveable != TRUCK ||
            actor->ar_engine == nullptr || !input.available ||
            input.version < 2U || !input.last_reconcile_succeeded)
        {
            return;
        }

        const float issued = actor->getEventValue(EV_TRUCK_ACCELERATE);
        const float resolved = actor->ar_engine->getAcc();
        if (!press_resolved_ &&
            input.reconciled_pressed_transition > press_transition_ &&
            input.reconciled_pressed_delivered && issued >= 0.5F &&
            resolved >= 0.25F)
        {
            actor_instance_id_ = actor->getInstanceId();
            key_ = input.reconciled_pressed_key;
            press_transition_ = input.reconciled_pressed_transition;
            press_event_id_ = input.reconciled_event_id;
            press_issued_ = issued;
            press_resolved_value_ = resolved;
            press_resolved_ = true;
        }
        if (press_resolved_ && !release_seen_ &&
            input.reconciled_released_transition > press_transition_ &&
            input.reconciled_released_key == key_ &&
            input.reconciled_released_delivered)
        {
            release_transition_ = input.reconciled_released_transition;
            release_event_id_ = input.reconciled_event_id;
            release_seen_ = true;
        }

        if (release_seen_ && !release_resolved_ &&
            actor->getInstanceId() == actor_instance_id_ &&
            issued <= 0.001F && resolved <= 0.05F)
        {
            release_issued_ = issued;
            release_resolved_value_ = resolved;
            release_resolved_ = true;
        }
    }

    void ObserveSceneSubmission(std::uint64_t frame_id) noexcept
    {
        if (qualified_ || frame_id == 0U)
        {
            return;
        }
        if (press_resolved_ && press_submitted_frame_ == 0U)
        {
            press_submitted_frame_ = frame_id;
        }
        else if (press_presented_ && release_resolved_ &&
                 release_submitted_frame_ == 0U &&
                 frame_id > press_presented_frame_)
        {
            release_submitted_frame_ = frame_id;
        }
    }

    void ObserveCompletedFrame(
        std::uint64_t frame_id,
        const RoR::RendererRetainedSceneAudit& scene) noexcept
    {
        if (qualified_ || frame_id == 0U || !scene.available ||
            scene.version < 6U ||
            scene.last_native_renderer_frame_id != frame_id ||
            !scene.last_native_pass_metrics_exact ||
            scene.last_native_scene_draws == 0U ||
            scene.retained_instances == 0U ||
            scene.last_dynamic_updates == 0U)
        {
            return;
        }
        if (!press_presented_ && frame_id == press_submitted_frame_)
        {
            press_presented_ = true;
            press_presented_frame_ = frame_id;
            press_dynamic_updates_ = scene.last_dynamic_updates;
            press_scene_draws_ = scene.last_native_scene_draws;
            return;
        }
        if (press_presented_ && release_resolved_ &&
            frame_id == release_submitted_frame_ &&
            frame_id > press_presented_frame_)
        {
            release_presented_frame_ = frame_id;
            release_dynamic_updates_ = scene.last_dynamic_updates;
            release_scene_draws_ = scene.last_native_scene_draws;
            qualified_ = true;
            LOG(fmt::format(
                "[RoR|RendererCombined|ActorControl] "
                "schema=ror.ogre_next_actor_control_receipt.v1 "
                "qualified=true input_source=visible_window_sdl "
                "presenter=ogre-next legacy_visible_fallback=false "
                "control=truck_accelerate actor_instance_id={} key={} "
                "press_transition={} press_event_id={} press_issued={} "
                "press_resolved={} press_frame_id={} "
                "press_dynamic_updates={} press_scene_draws={} "
                "release_transition={} release_event_id={} "
                "release_issued={} release_resolved={} release_frame_id={} "
                "release_dynamic_updates={} release_scene_draws={}",
                actor_instance_id_, static_cast<unsigned int>(key_),
                press_transition_, press_event_id_, press_issued_,
                press_resolved_value_, press_presented_frame_,
                press_dynamic_updates_, press_scene_draws_,
                release_transition_, release_event_id_, release_issued_,
                release_resolved_value_, release_presented_frame_,
                release_dynamic_updates_, release_scene_draws_));
        }
    }

private:
    std::uint64_t press_transition_ = 0U;
    std::uint64_t release_transition_ = 0U;
    std::uint64_t press_event_id_ = 0U;
    std::uint64_t release_event_id_ = 0U;
    std::uint64_t press_submitted_frame_ = 0U;
    std::uint64_t press_presented_frame_ = 0U;
    std::uint64_t release_submitted_frame_ = 0U;
    std::uint64_t release_presented_frame_ = 0U;
    std::uint64_t press_dynamic_updates_ = 0U;
    std::uint64_t release_dynamic_updates_ = 0U;
    std::uint64_t press_scene_draws_ = 0U;
    std::uint64_t release_scene_draws_ = 0U;
    int actor_instance_id_ = -1;
    RoR::RendererGameKey key_ = RoR::RendererGameKey::UNASSIGNED;
    float press_issued_ = 0.0F;
    float press_resolved_value_ = 0.0F;
    float release_issued_ = 0.0F;
    float release_resolved_value_ = 0.0F;
    bool press_resolved_ = false;
    bool press_presented_ = false;
    bool release_seen_ = false;
    bool release_resolved_ = false;
    bool qualified_ = false;
};
#endif

} // namespace

#ifdef __cplusplus
extern "C" {
#endif

int main(int argc, char *argv[])
{
#if defined(_WIN32) && defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    ConfigureWindowsCrashDump();
#endif
    using namespace RoR;

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    RendererCombinedApplicationArguments renderer_combined_arguments =
        ResolveRendererCombinedApplicationArguments(argc, argv);
    if (!renderer_combined_arguments.ok())
    {
        std::fprintf(
            stderr,
            "RoR combined renderer: invalid application arguments: %s\n",
            ToString(renderer_combined_arguments.status));
        std::fflush(stderr);
        return renderer_combined_arguments.status ==
                RendererCombinedApplicationArgumentsStatus::OUT_OF_MEMORY
            ? 70
            : 64;
    }
    const RendererCombinedNativeVisualScene
        renderer_combined_native_visual_scene =
            renderer_combined_arguments.native_visual_scene;
    const bool renderer_combined_native_visual_showcase =
        renderer_combined_arguments.native_visual_showcase();
    argc = renderer_combined_arguments.argc();
    argv = renderer_combined_arguments.argv();

    // The combined executable has no bridge endpoint, child process, or
    // transport handles. Ogre 14 is retained only as a hidden joined-scene and
    // resource host; the in-process OgreNext presenter owns visibility/input.
    const RendererOgre14RuntimeOwnership renderer_runtime_ownership =
        ResolveRendererOgre14RuntimeOwnership(
            RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST);
    if (!renderer_runtime_ownership.valid())
    {
        std::fputs(
            "RoR combined renderer: invalid runtime ownership plan\n",
            stderr);
        std::fflush(stderr);
        return 70;
    }

    // The menu/HUD is transported as a GUI-RTT texture asset (see
    // Ogre14GuiOverlayCapture) riding SIMULATION-state snapshots only, so a
    // bare launch must land in a simulation. A Finder launch supplies only
    // argv[0]; make exactly that case an immediately visible simulation using
    // the terrain and truck shipped in every Base_Content package. The
    // explicit forward-native showcase is consumed above and remains in
    // MAIN_MENU; every other caller argument retains pointer identity/order.
    static char combined_check_cache[] = "-checkcache";
    static char combined_map_option[] = "-map";
    static char combined_map[] = "simple2_a.terrn2";
    static char combined_truck_option[] = "-truck";
    static char combined_truck[] = "b6b0UID-semi.truck";
    static char combined_enter[] = "-enter";
    std::array<char*, 8U> renderer_combined_demo_arguments{};
    if (!renderer_combined_native_visual_showcase && argc == 1)
    {
        renderer_combined_demo_arguments = {{
            argv[0],
            combined_check_cache,
            combined_map_option,
            combined_map,
            combined_truck_option,
            combined_truck,
            combined_enter,
            nullptr,
        }};
        argc = 7;
        argv = renderer_combined_demo_arguments.data();
    }
#else
    // Decode and adopt the optional supervisor bridge before libraries or
    // worker threads can inherit its private handles. Direct RoR-Ogre14
    // launches retain the caller's original argv vector byte-for-byte.
    RendererOgre14GameBridge renderer_game_bridge;
    const RendererOgre14GameBridgeResult renderer_bridge_result =
        renderer_game_bridge.Initialize(argc, argv);
    if (!renderer_bridge_result)
    {
        std::fprintf(
            stderr,
            "RoR OGRE 14 game bridge: %s (endpoint=%s, channel=%s, "
            "native-error=%u)\n",
            ToString(renderer_bridge_result.status),
            ToString(renderer_bridge_result.endpoint_status),
            ToString(renderer_bridge_result.channel.status),
            static_cast<unsigned int>(
                renderer_bridge_result.channel.native_error_code));
        std::fflush(stderr);
        return renderer_bridge_result.status ==
                       RendererOgre14GameBridgeStatus::FAILED_INTERNAL ||
                   renderer_bridge_result.status ==
                       RendererOgre14GameBridgeStatus::
                           FAILED_CHANNEL_ADOPTION
                   ? 70
                   : 64;
    }
    if (renderer_bridge_result.active)
    {
        argc = renderer_game_bridge.forwarded_argc();
        argv = renderer_game_bridge.forwarded_argv();
    }
    const RendererOgre14RuntimeOwnership renderer_runtime_ownership =
        ResolveRendererOgre14RuntimeOwnership(
            renderer_game_bridge.active()
                ? RendererOgre14HostMode::OGRE_NEXT_BRIDGE_HOST
                : RendererOgre14HostMode::LEGACY_STANDALONE);
    if (!renderer_runtime_ownership.valid())
    {
        std::fputs(
            "RoR OGRE 14 game bridge: invalid runtime ownership plan\n",
            stderr);
        std::fflush(stderr);
        return 70;
    }
#endif

#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
    if (argc >= 2 &&
        std::strcmp(
            argv[1],
            "--internal-ogre14-authenticated-material-script-native-integration") == 0)
    {
        return RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(
            argc, argv);
    }
#endif

#ifdef USE_CURL
    curl_global_init(CURL_GLOBAL_ALL); // MUST init before any threads are started
#endif

    Ogre::OverlaySystem* overlay_system = nullptr;
    // Normal returns unwind local guards in reverse order: workers, window
    // integrations, then the renderer. Fatal returns explicitly quiesce the
    // workers and scene first; each guard caches that proven release.
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    RendererOgreNextInProcessPresenter renderer_combined_presenter;
    CombinedPresenterWindowGuard renderer_combined_window_guard(
        renderer_combined_presenter);
#endif
    RendererRuntimeGuard renderer_runtime_guard;
    WindowBoundRuntimeGuard window_bound_runtime_guard(overlay_system);
    WorkerRuntimeGuard worker_runtime_guard;
    ApplicationFatalShutdownGate fatal_scene_runtime_gate(
        &ReleaseFatalSceneRuntime);
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    Detail::OgreNextDemoInProcessFramePolicy renderer_combined_frame_policy;
    std::unique_ptr<RendererGameInputEngineTarget>
        renderer_combined_input_target;
    RendererCombinedActorControlQualification
        renderer_combined_actor_control_qualification;
    std::unique_ptr<Render::IJoinedGraphicsSceneSource>
        renderer_combined_scene_source;
    std::unique_ptr<Ogre14GuiOverlayCapture>
        renderer_combined_hud_capture;
    Render::NativeVisualShowcaseSceneSource*
        renderer_combined_native_showcase_scene_source = nullptr;
    std::unique_ptr<RendererInProcessSession>
        renderer_combined_session;
    std::string renderer_combined_scene_failure_signature;
    // Publication-degrade bookkeeping. A snapshot rejection drops a frame; it
    // must never end the session and must never stop publication for good. It
    // must also never go quiet: the historic code logged one line per distinct
    // signature and then nothing, so a rejection recurring every frame looked
    // exactly like a log that simply stopped, which is what made the last one
    // so expensive to diagnose. These count the recurrence and re-report it on
    // a bounded schedule instead.
    std::uint64_t renderer_combined_scene_failure_occurrences = 0U;
    // Re-reporting is scheduled on wall-clock, NOT on an occurrence count. A
    // rejection that fails cheaply lets the frame loop spin far faster than a
    // rendering one does: verifying this path live produced 59,759 rejections
    // in 23 seconds, where a count-based schedule firing every 600 became four
    // log lines and four notifications per second. Seconds are also what the
    // reader actually wants to know.
    std::chrono::steady_clock::time_point
        renderer_combined_scene_failure_last_log{};
    std::chrono::steady_clock::time_point
        renderer_combined_scene_failure_last_notice{};
    std::uint64_t renderer_combined_scene_publication_recoveries = 0U;
    std::uint64_t renderer_combined_scene_frames_dropped_total = 0U;
    // True while the last capture did not reach the presenter, so the window
    // is showing an older frame than the simulation.
    bool renderer_combined_scene_publication_degraded = false;
    bool renderer_combined_scene_degrade_notified = false;
    std::string renderer_combined_particle_audit_state_signature;
    std::uint64_t renderer_combined_particle_audit_logged_sequence = 0U;
    std::string renderer_combined_analytic_sky_audit_signature;
    std::string renderer_combined_aerial_haze_audit_signature;
    std::string renderer_combined_native_lighting_audit_state_signature;
    std::uint64_t renderer_combined_native_lighting_audit_logged_frame = 0U;
    std::uint64_t renderer_combined_retained_scene_logged_frame = 0U;
    // Render-boundary degrade counters. A degrade nobody can see is not a fix,
    // so this logs on every change to the total rather than on a timer.
    std::uint64_t renderer_combined_render_boundary_degrades_logged = 0U;
    std::string renderer_combined_native_sun_visibility_audit_signature;
    bool renderer_combined_turntable_audit_published = false;
    std::uint64_t renderer_combined_turntable_audit_segment = 0U;
    // Producer retained-section audit. Counters are summed over accepted
    // productions only, so a rejected frame can never inflate them, and are
    // surfaced as a periodic heartbeat rather than per frame.
    std::uint64_t renderer_combined_producer_retained_frames = 0U;
    std::uint64_t renderer_combined_producer_retained_adoptions = 0U;
    std::uint64_t renderer_combined_producer_retained_fast = 0U;
    std::uint64_t renderer_combined_producer_retained_misses = 0U;
    std::uint64_t renderer_combined_producer_retained_window = 0U;
    std::uint64_t renderer_combined_producer_retained_scoped = 0U;
    std::uint64_t renderer_combined_producer_retained_payload_full = 0U;
    std::uint64_t renderer_combined_producer_retained_compat_full = 0U;
    std::uint64_t renderer_combined_producer_retained_instances = 0U;
    // Scene-free GUI-only presentation (the main menu). Counted here rather
    // than only in the frontend audit so the heartbeat proves the GAME asked
    // for the frame, not just that the renderer could have served one.
    std::uint64_t renderer_combined_ui_overlay_presents = 0U;
    std::string renderer_combined_ui_overlay_failure_signature;
#else
    std::unique_ptr<RendererGameInputEngineTarget>
        renderer_bridge_input_target;
    std::unique_ptr<Render::Ogre14GraphicsSceneSource>
        renderer_bridge_scene_source;
    std::unique_ptr<RendererOgre14ProductSession>
        renderer_bridge_product_session;
    std::string renderer_bridge_scene_failure_signature;
#endif
    int application_exit_code = 0;

    // Fatal game failures are caught inside the scope of every local runtime
    // guard. The catch below first closes capture/presentation, proves worker
    // quiescence, and disposes the scene. Only then may returning unwind
    // RendererRuntimeGuard while ContentManager's archive owners are alive.
    try
    {

#ifndef _DEBUG
    try
    {
#endif

        // Create cvars, set default values
        App::GetConsole()->cVarSetupBuiltins();

        // Record main thread ID for checks
        App::GetAppContext()->SetUpThreads();

        // Update cvars 'sys_process_dir', 'sys_user_dir'
        if (!App::GetAppContext()->SetUpProgramPaths())
        {
            return -1; // Error already displayed
        }

        // Create OGRE default logger early
        App::GetAppContext()->SetUpLogging();

#ifdef ROR_ENABLE_INTERNAL_TEST_HOOKS
        if (argc >= 2 &&
            std::strcmp(
                argv[1],
                "--internal-rigdef-calibrated-material-roundtrip") == 0)
        {
            if (argc != 3)
            {
                std::cerr
                    << "usage: RoR "
                       "--internal-rigdef-calibrated-material-roundtrip "
                       "<fixture.truck>\n";
                return 2;
            }
            return RigDef::RunCalibratedBeamMaterialRoundTripIntegration(
                argv[2]);
        }
#endif

        // User directories
        App::sys_config_dir    ->setStr(PathCombine(App::sys_user_dir->getStr(), "config"));
        if (App::sys_cache_dir->getStr().empty())
        {
            App::sys_cache_dir->setStr(PathCombine(App::sys_user_dir->getStr(), "cache"));
        }
        if (App::sys_thumbnails_dir->getStr().empty())
        {
            App::sys_thumbnails_dir->setStr(PathCombine(App::sys_user_dir->getStr(), "thumbnails"));
        }
        App::sys_savegames_dir ->setStr(PathCombine(App::sys_user_dir->getStr(), "savegames"));
        App::sys_screenshot_dir->setStr(PathCombine(App::sys_user_dir->getStr(), "screenshots"));
        App::sys_scripts_dir   ->setStr(PathCombine(App::sys_user_dir->getStr(), "scripts"));
        App::sys_projects_dir  ->setStr(PathCombine(App::sys_user_dir->getStr(), "projects"));
        App::sys_repo_attachments_dir->setStr(PathCombine(App::sys_user_dir->getStr(), "repo_attachments"));
        LOG(fmt::format(
            "[RoR|Startup|Paths] process='{}', user='{}', config='{}', cache='{}', thumbnails='{}', logs='{}'",
            App::sys_process_dir->getStr(),
            App::sys_user_dir->getStr(),
            App::sys_config_dir->getStr(),
            App::sys_cache_dir->getStr(),
            App::sys_thumbnails_dir->getStr(),
            App::sys_logs_dir->getStr()));

        // Load RoR.cfg - updates cvars
        App::GetConsole()->loadConfig();

        // Process command line params - updates 'cli_*' cvars
        App::GetConsole()->processCommandLine(argc, argv);

        if (App::app_state->getEnum<AppState>() == AppState::PRINT_HELP_EXIT)
        {
            App::GetConsole()->showCommandLineUsage();
            return 0;
        }
        if (App::app_state->getEnum<AppState>() == AppState::PRINT_VERSION_EXIT)
        {
            App::GetConsole()->showCommandLineVersion();
            return 0;
        }

        // Find resources dir, update cvar 'sys_resources_dir'
        if (!App::GetAppContext()->SetUpResourcesDir())
        {
            return -1; // Error already displayed
        }

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        // SDL/Metal must be established before Ogre 14 asks SDL for its
        // hidden OpenGL resource host. This makes the presenter the stable
        // process-wide video/event owner from the first native window onward.
        RendererOgreNextInProcessPresenterConfiguration presenter_config;
        // Keep the general playable joined scene on the reviewed HDR/PSSM
        // graph while the bounded project-owned native showcase requires the
        // production-owned Apple Metal sun-visibility pass. Unsupported
        // platforms select the explicit raster graph before frontend state is
        // created; there is no mid-frame native fallback.
#if defined(__APPLE__)
        presenter_config.lighting_mode =
            renderer_combined_native_visual_showcase
                ? RendererOgreNextInProcessLightingMode::
                      METAL_RT_SUN_VISIBILITY_V2
                : RendererOgreNextInProcessLightingMode::RASTER_HDR_PSSM;
#else
        presenter_config.lighting_mode =
            RendererOgreNextInProcessLightingMode::RASTER_HDR_PSSM;
#endif
        std::string presenter_config_failure;
        if (!ResolveCombinedPresenterConfiguration(
                presenter_config, presenter_config_failure))
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] {}",
                presenter_config_failure));
            return 70;
        }
        if (presenter_config.exact_drawable_width != 0U)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] exact_drawable={}x{} "
                "source=isolated-scene-contract",
                presenter_config.exact_drawable_width,
                presenter_config.exact_drawable_height));
        }
        const RendererOgreNextInProcessPresenterStatus presenter_prepared =
            renderer_combined_presenter.PrepareWindow(presenter_config);
        if (presenter_prepared !=
            RendererOgreNextInProcessPresenterStatus::COMPLETED)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] Visible OgreNext window "
                "preparation failed: status='{}'",
                ToString(presenter_prepared)));
            return 70;
        }
#if defined(__APPLE__)
        constexpr const char* combined_visible_backend = "ogre-next-metal";
#elif defined(_WIN32)
        constexpr const char* combined_visible_backend = "ogre-next-d3d11";
#else
        constexpr const char* combined_visible_backend = "ogre-next-vulkan";
#endif
        LOG(fmt::format(
            "[RoR|RendererCombined|Startup] presentation_owner=ogre-next "
            "visible_window=true legacy_visible_fallback=false backend={}",
            combined_visible_backend));
#endif

        // Make sure config directory exists - to save 'ogre.cfg'
        CreateFolder(App::sys_config_dir->getStr());

        // Load and start OGRE renderer, uses config directory
        if (!App::GetAppContext()->SetUpRendering(
                renderer_runtime_ownership))
        {
            return -1; // Error already displayed
        }

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        void* const renderer_resource_window =
            App::GetAppContext()->GetCombinedRendererResourceWindow();
        const RendererOgreNextInProcessPresenterStatus resource_protected =
            renderer_combined_presenter.ProtectHiddenResourceWindow(
                renderer_resource_window);
#if defined(__APPLE__)
        // The Apple producer is an SDL window, so the presenter must retain
        // its handle and force any SHOWN/RESTORED event back to hidden. Linux
        // uses OGRE's native GLX window instead: AppContext has already
        // fail-closed on RenderWindow::isHidden(), and passing that X11 handle
        // to an SDL-only event guard would be invalid.
        const bool resource_window_missing =
            renderer_resource_window == nullptr;
#else
        const bool resource_window_missing = false;
#endif
        if (resource_window_missing || resource_protected !=
                RendererOgreNextInProcessPresenterStatus::COMPLETED)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] Hidden Ogre 14 resource "
                "window protection failed: status='{}'",
                ToString(resource_protected)));
            return 70;
        }
        LOG("[RoR|RendererCombined|Startup] resource_host=ogre14 "
            "visible_window=false protected=true");
#endif

        Ogre::TextureManager::getSingleton().setDefaultNumMipmaps(5);

        // Deploy base config files from 'skeleton.zip'
        if (!App::GetAppContext()->SetUpConfigSkeleton())
        {
            return -1; // Error already displayed
        }

        overlay_system = new Ogre::OverlaySystem(); //Overlay init

        Ogre::ConfigOptionMap ropts = App::GetAppContext()->GetOgreRoot()->getRenderSystem()->getConfigOptions();
        int resolution = Ogre::StringConverter::parseInt(Ogre::StringUtil::split(ropts["Video Mode"].currentValue, " x ")[0], 1024);
        int fsaa = 2 * (Ogre::StringConverter::parseInt(ropts["FSAA"].currentValue, 0) / 4);
        int res = std::pow(2, std::floor(std::log2(resolution)));

        Ogre::TextureManager::getSingleton().createManual ("EnvironmentTexture",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_CUBE_MAP, res / 4, res / 4, 0,
            Ogre::PF_R8G8B8, Ogre::TU_RENDERTARGET, 0, false, fsaa);
        Ogre::TextureManager::getSingleton ().createManual ("Refraction",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D, res / 2, res / 2, 0,
            Ogre::PF_R8G8B8, Ogre::TU_RENDERTARGET, 0, false, fsaa);
        Ogre::TextureManager::getSingleton ().createManual ("Reflection",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D, res / 2, res / 2, 0,
            Ogre::PF_R8G8B8, Ogre::TU_RENDERTARGET, 0, false, fsaa);

        if (!App::diag_warning_texture->getBool())
        {
            // We overwrite the default warning texture (yellow stripes) with something unobtrusive
            // D3D11 does not scale blitFromMemory uploads, so the source box
            // must exactly match the renderer-owned warning texture.
            Ogre::HardwarePixelBufferSharedPtr warning_buffer =
                Ogre::TextureManager::getSingleton()
                    ._getWarningTexture()->getBuffer();
            const size_t warning_width = warning_buffer->getWidth();
            const size_t warning_height = warning_buffer->getHeight();
            const size_t warning_depth = warning_buffer->getDepth();
            const Ogre::PixelFormat warning_format =
                warning_buffer->getFormat();
            const size_t warning_bytes =
                Ogre::PixelUtil::getMemorySize(
                    warning_width,
                    warning_height,
                    warning_depth,
                    warning_format);
            if (warning_width == 0 ||
                warning_height == 0 ||
                warning_depth == 0 ||
                warning_bytes == 0)
            {
                LOG("[RoR|Startup|Rendering] WARNING - cannot replace "
                    "the renderer warning texture because its storage "
                    "contract is empty");
            }
            else
            {
                std::vector<Ogre::uchar> warning_data(
                    warning_bytes,
                    static_cast<Ogre::uchar>(0));
                Ogre::PixelBox warning_pixels(
                    warning_width,
                    warning_height,
                    warning_depth,
                    warning_format,
                    warning_data.data());
                warning_buffer->blitFromMemory(warning_pixels);
            }
        }

        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::FLAGS);
        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::FONTS);
        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::ICONS);
        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::OGRE_CORE);
        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::WALLPAPERS);
        App::GetContentManager()->AddResourcePack(ContentManager::ResourcePack::SCRIPTS);

#ifndef NOLANG
        App::GetLanguageEngine()->setup();
#endif // NOLANG
        App::GetConsole()->regBuiltinCommands(); // Call after localization had been set up

        App::GetContentManager()->InitContentManager();

        // Set up rendering
        App::CreateGfxScene(); // Creates OGRE SceneManager, needs content manager
        App::GetGfxScene()->GetSceneManager()->addRenderQueueListener(overlay_system);
        App::CreateCameraManager(); // Creates OGRE Camera
        App::GetGfxScene()->GetEnvMap().SetupEnvMap(); // Needs camera

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        if (renderer_combined_native_visual_showcase)
        {
            const bool selects_a0 =
                renderer_combined_native_visual_scene ==
                RendererCombinedNativeVisualScene::A0_LIGHTING_COUPON;
            const Render::NativeVisualShowcaseProfile native_profile =
                selects_a0
                    ? Render::NativeVisualShowcaseProfile::A0_LIGHTING_COUPON
                    : Render::NativeVisualShowcaseProfile::A1_NATIVE_COURSE;
            const char* const native_package_relative_path = selects_a0
                ? Render::kNativeVisualShowcaseExecutableResourceRelativePath
                : Render::
                    kNativeVisualShowcaseA1ExecutableResourceRelativePath;
            const char* const native_package_id = selects_a0
                ? Render::kNativeVisualShowcasePackageId
                : Render::kNativeVisualShowcaseA1PackageId;
            const char* const native_package_sha256 = selects_a0
                ? Render::kNativeVisualShowcasePackageSha256Hex
                : Render::kNativeVisualShowcaseA1PackageSha256Hex;
            const std::string native_showcase_package_path = PathCombine(
                App::sys_resources_dir->getStr(),
                native_package_relative_path);
            Render::NativeVisualShowcaseSceneSourceLoadResult loaded =
                Render::LoadNativeVisualShowcaseSceneSource(
                    native_showcase_package_path,
                    native_profile);
            if (!loaded)
            {
                LOG(fmt::format(
                    "[RoR|RendererCombined|NativeShowcase] Exact package "
                    "load failed: path='{}', code={}, field='{}', detail='{}'",
                    native_showcase_package_path,
                    static_cast<unsigned int>(loaded.validation.code),
                    loaded.validation.field,
                    loaded.validation.detail));
                return 70;
            }
            const std::size_t package_asset_count =
                loaded.source->package_owner()->assets.size();
            const std::size_t package_instance_count =
                loaded.source->package_owner()->static_meshes.size();
            const Render::ValidationResult turntable_enabled =
                loaded.source->SetMotionMode(
                    Render::NativeVisualShowcaseMotionMode::TURN_TABLE);
            if (!turntable_enabled)
            {
                LOG(fmt::format(
                    "[RoR|RendererCombined|NativeShowcase] Turntable "
                    "selection failed: code={}, field='{}', detail='{}'",
                    static_cast<unsigned int>(turntable_enabled.code),
                    turntable_enabled.field,
                    turntable_enabled.detail));
                return 70;
            }
            renderer_combined_native_showcase_scene_source =
                loaded.source.get();
            renderer_combined_scene_source = std::move(loaded.source);
            const bool native_rt_selected =
                presenter_config.lighting_mode ==
                RendererOgreNextInProcessLightingMode::
                    METAL_RT_SUN_VISIBILITY_V2;
            LOG(fmt::format(
                "[RoR|RendererCombined|NativeShowcase] Selected exact "
                "forward-native scene: path='{}', package='{}', "
                "sha256='{}', assets={}, instances={}, source_version={}, "
                "pipeline='{}', hdr=true, native_rt={}, profile={}, "
                "motion='{}', "
                "fixed_hz=60, revolution_ticks={}, refraction={}, "
                "motion_vectors=false",
                native_showcase_package_path,
                native_package_id,
                native_package_sha256,
                package_asset_count,
                package_instance_count,
                Render::kNativeVisualShowcaseSceneSourceVersion,
                native_rt_selected
                    ? "rt4_pbr_hdr_metal_sun_visibility_v2"
                    : "rt4_pbr_pssm_hdr_preview",
                native_rt_selected,
                selects_a0 ? "a0_lighting_coupon" : "a1_native_course",
                selects_a0 ? "turntable_opaque_gate"
                           : "turntable_thin_glass_slab",
                Render::kNativeVisualShowcaseTurntableTicksPerRevolution,
                selects_a0 ? "false"
                           : "thin_parallel_slab_screen_space"));
        }
        else
        {
            try
            {
                renderer_combined_scene_source =
                    std::make_unique<Render::Ogre14GraphicsSceneSource>(
                        *App::GetGfxScene());
                App::GetGfxScene()->EnableOgreNextDemoCapture();
                // The transported menu/HUD capture belongs to the joined
                // OGRE 14 source only; the forward-native showcase carries
                // no GUI.
                renderer_combined_hud_capture =
                    std::make_unique<Ogre14GuiOverlayCapture>();
            }
            catch (...)
            {
                LOG("[RoR|RendererCombined|Scene] Could not initialize the "
                    "OGRE 14 joined-scene adapter");
                return 70;
            }
        }
#else
        if (renderer_game_bridge.active())
        {
            try
            {
                renderer_bridge_scene_source =
                    std::make_unique<Render::Ogre14GraphicsSceneSource>(
                        *App::GetGfxScene());
                App::GetGfxScene()->EnableOgreNextDemoCapture();
            }
            catch (...)
            {
                LOG("[RoR|RendererBridge|Scene] Could not initialize the "
                    "OGRE 14 joined-scene adapter");
                return 70;
            }
        }
#endif

        App::CreateGuiManager(); // Needs scene manager

        App::GetDiscordRpc()->Init();

        if (!App::GetAppContext()->SetUpInput(renderer_runtime_ownership))
            return 70;

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        renderer_combined_input_target =
            std::make_unique<RendererGameInputEngineTarget>();
        const RendererOgreNextInProcessPresenterStatus input_attached =
            renderer_combined_presenter.AttachInputTarget(
                *renderer_combined_input_target);
        if (input_attached !=
            RendererOgreNextInProcessPresenterStatus::COMPLETED)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Input] Direct input activation "
                "failed: status='{}'",
                ToString(input_attached)));
            return 70;
        }

        Render::IRendererFrontend* const combined_frontend =
            renderer_combined_presenter.Frontend();
        if (combined_frontend == nullptr)
        {
            LOG("[RoR|RendererCombined|Startup] Presenter did not expose "
                "its in-process OgreNext frontend");
            return 70;
        }
        renderer_combined_session =
            std::make_unique<RendererInProcessSession>(
                *combined_frontend,
                renderer_combined_presenter,
                renderer_combined_frame_policy);
        // The hidden OGRE 14 producer only feeds joined captures; its own
        // shadow atlas and environment-map RTT passes render into textures
        // nothing on the presenter reads. Measured at 15.4 ms/frame on the
        // full CityWorld admission, so the combined runtime forces both off
        // regardless of the archived user configuration. The presenter's
        // native PSSM and reflection probes are unaffected.
        App::gfx_shadow_type->setVal((int)GfxShadowType::NONE);
        App::gfx_envmap_enabled->setVal(false);
        App::gfx_envmap_rate->setVal(0);
        RendererInProcessSessionConfig combined_session_config;
        combined_session_config.frontend =
            renderer_combined_presenter.InitialFrontendRequest();
        combined_session_config.producer.registry_id =
            kCombinedRendererAssetRegistryId;
        // The combined runtime admits the whole map up front (the 12 km
        // static admission ball), so one authoritative frame legitimately
        // carries the entire map's unique mesh and texture payload. The
        // conservative library default of 512 MiB stays in place for every
        // other session; this bound remains a runaway-duplication backstop.
        combined_session_config.producer.maximum_asset_payload_bytes =
            4096ULL * 1024ULL * 1024ULL;
        combined_session_config.color_format =
            presenter_config.lighting_mode ==
                    RendererOgreNextInProcessLightingMode::
                        METAL_RT_SUN_VISIBILITY_V2
                ? Render::PixelFormat::RGBA16_FLOAT
                : Render::PixelFormat::RGBA8_SRGB;
        // Keep each frontend shutdown attempt below the outer five-second
        // close budget so a typed retryable TIMEOUT can actually be retried.
        combined_session_config.shutdown_timeout_nanoseconds =
            kCombinedRendererShutdownAttemptNanoseconds;
        const RendererInProcessSessionResult session_started =
            renderer_combined_session->Start(combined_session_config);
        if (!session_started)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] In-process OgreNext "
                "session failed: status='{}', frontend={}, field='{}', "
                "detail='{}'",
                ToString(session_started.status),
                static_cast<unsigned int>(session_started.frontend_code),
                session_started.validation.field,
                session_started.validation.detail));
            const RendererInProcessSessionResult failed_start_shutdown =
                CloseCombinedRendererSession(*renderer_combined_session);
            if (failed_start_shutdown.status !=
                RendererInProcessSessionStatus::CLOSED)
            {
                FailStopApplication(70);
            }
            renderer_combined_session.reset();
            return 70;
        }
        const RendererInProcessSessionResult bootstrap_presented =
            renderer_combined_session->PresentBootstrapFrame();
        if (bootstrap_presented.status !=
            RendererInProcessSessionStatus::BOOTSTRAP_PRESENTED)
        {
            LOG(fmt::format(
                "[RoR|RendererCombined|Startup] Clear-only native "
                "presentation failed: status='{}', frontend={}, field='{}', "
                "detail='{}'",
                ToString(bootstrap_presented.status),
                static_cast<unsigned int>(
                    bootstrap_presented.frontend_code),
                bootstrap_presented.validation.field,
                bootstrap_presented.validation.detail));
            const RendererInProcessSessionResult bootstrap_shutdown =
                CloseCombinedRendererSession(*renderer_combined_session);
            if (bootstrap_shutdown.status !=
                RendererInProcessSessionStatus::CLOSED)
            {
                FailStopApplication(70);
            }
            renderer_combined_session.reset();
            return 70;
        }
        App::GetGuiManager()->LoadingWindow.SetCombinedRendererLoadingPump(
            &renderer_combined_session,
            &PumpCombinedRendererLoadingWindow);
        LOG(fmt::format(
            "[RoR|RendererCombined|Startup] Transport-free OgreNext "
            "session ready after authenticated bootstrap presentation "
            "(registry={})",
            renderer_combined_session->registry_id()));
#else
        if (renderer_game_bridge.active())
        {
            renderer_bridge_input_target =
                std::make_unique<RendererGameInputEngineTarget>();
            renderer_bridge_product_session =
                std::make_unique<RendererOgre14ProductSession>(
                    renderer_game_bridge, *renderer_bridge_input_target);
            const RendererOgre14ProductSessionResult session_started =
                renderer_bridge_product_session->Start();
            if (!session_started)
            {
                LOG(fmt::format(
                    "[RoR|RendererBridge|Product] Could not start: "
                    "status='{}', host='{}'",
                    ToString(session_started.status),
                    ToString(session_started.host_status)));
                return 70;
            }
            LOG(fmt::format(
                "[RoR|RendererBridge|Product] Ogre-Next host session "
                "started (registry={})",
                renderer_bridge_product_session->host().registry_id()));
        }
#endif

#ifdef USE_ANGELSCRIPT
        App::CreateScriptEngine();
        CreateFolder(App::sys_scripts_dir->getStr());
        CreateFolder(App::sys_projects_dir->getStr());
#endif

        App::GetGuiManager()->SetUpMenuWallpaper();

        // Add "this is obsolete" marker file to old config location
        App::GetAppContext()->SetUpObsoleteConfMarker();

        App::CreateThreadPool();

        // Load inertia config file
        App::GetGameContext()->GetActorManager()->GetInertiaConfig().LoadDefaultInertiaModels();

        // Load mod cache
        if (App::app_force_cache_purge->getBool())
        {
            App::GetGameContext()->PushMessage(Message(MSG_APP_MODCACHE_PURGE_REQUESTED));
        }
        else if (App::cli_force_cache_update->getBool() || App::app_force_cache_update->getBool())
        {
            App::GetGameContext()->PushMessage(Message(MSG_APP_MODCACHE_UPDATE_REQUESTED));
        }
        else
        {
            App::GetGameContext()->PushMessage(Message(MSG_APP_MODCACHE_LOAD_REQUESTED));
        }

        // Load startup scripts (console, then RoR.cfg)
        if (App::cli_custom_scripts->getStr() != "")
        {
            Ogre::StringVector script_names = Ogre::StringUtil::split(App::cli_custom_scripts->getStr(), ",");
            for (Ogre::String const& scriptname: script_names)
            {
                LOG(fmt::format("Loading startup script '{}' (from command line)", scriptname));
                // We cannot call `loadScript()` directly because modcache isn't up yet - gadgets cannot be resolved
                LoadScriptRequest* req = new LoadScriptRequest();
                req->lsr_category = ScriptCategory::CUSTOM;
                req->lsr_filename = scriptname;
                App::GetGameContext()->PushMessage(Message(MSG_APP_LOAD_SCRIPT_REQUESTED, req));
                // errors are logged by OGRE & AngelScript
            }
        }
        if (App::app_custom_scripts->getStr() != "")
        {
            Ogre::StringVector script_names = Ogre::StringUtil::split(App::app_custom_scripts->getStr(), ",");
            for (Ogre::String const& scriptname: script_names)
            {
                LOG(fmt::format("Loading startup script '{}' (from config file)", scriptname));
                // We cannot call `loadScript()` directly because modcache isn't up yet - gadgets cannot be resolved
                LoadScriptRequest* req = new LoadScriptRequest();
                req->lsr_category = ScriptCategory::CUSTOM;
                req->lsr_filename = scriptname;
                App::GetGameContext()->PushMessage(Message(MSG_APP_LOAD_SCRIPT_REQUESTED, req));
                // errors are logged by OGRE & AngelScript
            }
        }

        // Handle game state presets
        if (App::cli_server_host->getStr() != "" && App::cli_server_port->getInt() != 0) // Multiplayer, commandline
        {
            App::mp_server_host->setStr(App::cli_server_host->getStr());
            App::mp_server_port->setVal(App::cli_server_port->getInt());
            App::GetGameContext()->PushMessage(Message(MSG_NET_CONNECT_REQUESTED));
        }
        else if (App::mp_join_on_startup->getBool()) // Multiplayer, conf file
        {
            App::GetGameContext()->PushMessage(Message(MSG_NET_CONNECT_REQUESTED));
        }
        else // Single player
        {
            if (App::cli_preset_terrain->getStr() != "") // Terrain, commandline
            {
                App::GetGameContext()->PushMessage(Message(MSG_SIM_LOAD_TERRN_REQUESTED, App::cli_preset_terrain->getStr()));
            }
            else if (App::diag_preset_terrain->getStr() != "") // Terrain, conf file
            {
                App::GetGameContext()->PushMessage(Message(MSG_SIM_LOAD_TERRN_REQUESTED, App::diag_preset_terrain->getStr()));
            }
            else // Main menu
            {
                if (App::cli_resume_autosave->getBool())
                {
                    if (FileExists(PathCombine(App::sys_savegames_dir->getStr(), "autosave.sav")))
                    {
                        App::GetGameContext()->PushMessage(RoR::Message(MSG_SIM_LOAD_SAVEGAME_REQUESTED, "autosave.sav"));
                    }
                }
                else if (App::app_skip_main_menu->getBool())
                {
                    // MainMenu disabled (singleplayer mode) -> go directly to map selector (traditional behavior)
                    RoR::Message m(MSG_GUI_OPEN_SELECTOR_REQUESTED);
                    m.payload = reinterpret_cast<void*>(new LoaderType(LT_Terrain));
                    App::GetGameContext()->PushMessage(m);
                }
                else
                {
                    App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                }
            }
        }

        App::app_state->setVal((int)AppState::MAIN_MENU);
        App::GetGuiManager()->MenuWallpaper->show();

#ifdef USE_OPENAL
        if (App::audio_menu_music->getBool())
        {
            App::GetSoundScriptManager()->createInstance("tracks/main_menu_tune", -1);
            SOUND_START(-1, SS_TRIG_MAIN_MENU);
        }
#endif // USE_OPENAL

        // Standalone Ogre 14 needs one historical Dear ImGui bootstrap frame.
        // The combined runtime's visible frame is owned exclusively by its
        // in-process presenter; GUI_LoadingWindow defensively enforces the
        // same rule for every other loading update.
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        App::GetGuiManager()->LoadingWindow.SetProgress(
            100, "Hack", /*renderFrame=*/false);
#else
        App::GetGuiManager()->LoadingWindow.SetProgress(
            100, "Hack", /*renderFrame=*/true);
#endif
        App::GetGuiManager()->LoadingWindow.SetVisible(false);

        // --------------------------------------------------------------
        // Main rendering and event handling loop
        // --------------------------------------------------------------

        auto start_time = std::chrono::high_resolution_clock::now();

        // Arm the playable frame-time budget before the first frame so the
        // recorder observes exactly the render loop's own frame clock. A
        // refused contract fails the process instead of running unmeasured.
        bool frame_budget_refused = false;
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        const Render::FrontendSurfaceUpdate combined_budget_surface =
            renderer_combined_presenter.CurrentSurface();
        const Render::FrontendInitializationRequest combined_budget_initial =
            renderer_combined_presenter.InitialFrontendRequest();
        const FrameTimeBudgetPresentationSurface
            combined_budget_presentation = {
                combined_budget_surface.pixel_width,
                combined_budget_surface.pixel_height,
                false,
                combined_budget_initial.vertical_sync,
            };
#endif
        std::unique_ptr<FrameTimeBudgetSession> frame_budget_session =
            CreateFrameTimeBudgetSession(
                App::GetAppContext()->GetRenderWindow(),
                // The combined process measures the sole visible Ogre-Next
                // drawable. Its hidden OGRE 14 resource window is neither a
                // resolution nor presentation authority.
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                &combined_budget_presentation,
#else
                nullptr,
#endif
                // "This process presents" is not the same fact as "OGRE 14
                // presents". The combined runtime also hides OGRE 14 behind a
                // bridge-active ownership plan, but its in-process OgreNext
                // presenter owns the window, so its loop interval really is a
                // frame interval. Only the two-process bridge, where a
                // separate child presents, produces without presenting.
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                renderer_combined_session != nullptr,
#else
                renderer_runtime_ownership.legacy_frame_presentation_enabled,
#endif
                frame_budget_refused);
        if (frame_budget_refused)
        {
            LOG("[RoR|Perf] Shutting down: the frame budget was refused");
            App::GetGameContext()->PushMessage(
                Message(MSG_APP_SHUTDOWN_REQUESTED));
        }
        bool frame_budget_shutdown_requested = false;

        while (App::app_state->getEnum<AppState>() != AppState::SHUTDOWN)
        {
            App::GetAppContext()->PrepareProfiler();
#if !defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
            OgreBites::WindowEventUtilities::messagePump();
            App::GetAppContext()->ProcessWindowEvents();
#endif

            // Halt physics (wait for async tasks to finish)
            if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                App::GetGameContext()->GetActorManager()->SyncWithSimThread();
            }

            // Game events
            OgreProfileBegin("RoR message queue");
            while (App::GetGameContext()->HasMessages())
            {
                Message m = App::GetGameContext()->PopMessage();
                if (App::IsWorldModelCaptureActive() &&
                    App::WorldModelCaptureMessageRequiresAbort(m.type))
                {
                    App::AbortWorldModelCapture(
                        std::string("game message: ") +
                        MsgTypeToString(m.type));
                }
                bool failed_m = false;
                switch (m.type)
                {

                // -- Application events --

                case MSG_APP_SHUTDOWN_REQUESTED:
                {
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
#if OGRE_VERSION_MAJOR >= 14
                            // The OGRE 14 terrain material generator owns RTShader
                            // sub-render states. Run the normal terrain cleanup
                            // before entering SHUTDOWN so those states are released
                            // before AppContext destroys the shader generator.
                            App::GetGameContext()->PushMessage(
                                Message(MSG_SIM_UNLOAD_TERRN_REQUESTED));
                            App::GetGameContext()->ChainMessage(
                                Message(MSG_APP_SHUTDOWN_REQUESTED));
                            break;
#else
                            App::GetGameContext()->SaveScene("autosave.sav");
#endif
                        }
                        App::GetConsole()->saveConfig(); // RoR.cfg
                        App::GetDiscordRpc()->Shutdown();
    #ifdef USE_SOCKETW
                        if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                        {
                            App::GetNetwork()->Disconnect();
                        }
    #endif // USE_SOCKETW
                        App::app_state->setVal((int)AppState::SHUTDOWN);
                        App::GetScriptEngine()->setEventsEnabled(false); // Hack to enable fast shutdown without cleanup.
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_SCREENSHOT_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::HIDDEN);
                        App::GetAppContext()->CaptureScreenshot();
                        App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::VISIBLE);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_DISPLAY_FULLSCREEN_REQUESTED:
                {
                    try
                    {
                        App::GetAppContext()->ActivateFullscreen(true);
                        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                      _L("Display mode changed to fullscreen"));
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_DISPLAY_WINDOWED_REQUESTED:
                {
                    try
                    {
                        App::GetAppContext()->ActivateFullscreen(false);
                        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                      _L("Display mode changed to windowed"));
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_MODCACHE_LOAD_REQUESTED:
                {
                    try
                    {
                        if (!App::GetCacheSystem()->IsModCacheLoaded()) // If not already loaded...
                        {
                            App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::HIDDEN);
                            App::GetContentManager()->InitModCache(CacheValidity::UNKNOWN);
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_MODCACHE_UPDATE_REQUESTED:
                {
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::MAIN_MENU) // No actors must be spawned; they keep pointers to CacheEntries
                        {
                            RoR::Log("[RoR|ModCache] Cache update requested");
                            App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::HIDDEN);
                            App::GetContentManager()->InitModCache(CacheValidity::NEEDS_UPDATE);
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_MODCACHE_PURGE_REQUESTED:
                {
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::MAIN_MENU) // No actors must be spawned; they keep pointers to CacheEntries
                        {
                            RoR::Log("[RoR|ModCache] Cache rebuild requested");
                            App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::HIDDEN);
                            App::GetContentManager()->InitModCache(CacheValidity::NEEDS_REBUILD);
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_LOAD_SCRIPT_REQUESTED:
                {
                    LoadScriptRequest* request = static_cast<LoadScriptRequest*>(m.payload);
                    try
                    {
                        ActorPtr actor = App::GetGameContext()->GetActorManager()->GetActorById(request->lsr_associated_actor);
                        // Notifications for script manipulations are sent by loadScript().
                        App::GetScriptEngine()->loadScript(request->lsr_filename, request->lsr_category, actor, request->lsr_buffer);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_APP_UNLOAD_SCRIPT_REQUESTED:
                {
                    ScriptUnitID_t* id = static_cast<ScriptUnitID_t*>(m.payload);
                    try
                    {
                        // Notifications for script manipulations are sent by unloadScript().
                        App::GetScriptEngine()->unloadScript(*id);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete id;
                    break;
                }

                case MSG_APP_SCRIPT_THREAD_STATUS:
                {
                    ScriptEventArgs* args = static_cast<ScriptEventArgs*>(m.payload);
                    try
                    {
                        App::GetScriptEngine()->triggerEvent(SE_ANGELSCRIPT_THREAD_STATUS,
                            args->arg1, args->arg2ex, args->arg3ex, args->arg4ex, args->arg5ex, args->arg6ex, args->arg7ex);
                        delete args;
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_APP_REINIT_INPUT_REQUESTED:
                {
                    try
                    {
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                        // The presenter's controller handles and direct target
                        // are process-lifetime state. Replacing InputEngine
                        // underneath them would create a dangling physical
                        // input owner, so defer live reinitialization until a
                        // deliberate presenter restart contract exists.
                        LOG("[RoR|RendererCombined|Input] Live input "
                            "reinitialization is disabled in combined mode");
                        App::GetGuiManager()->LoadingWindow.SetVisible(false);
#else
                        LOG(fmt::format("[RoR] !! Reinitializing input engine !!"));
                        App::DestroyInputEngine();
                        if (!App::GetAppContext()->SetUpInput(
                                renderer_runtime_ownership))
                        {
                            throw std::runtime_error(
                                "could not restore renderer input ownership after reinitialization");
                        }
                        if (renderer_bridge_product_session != nullptr &&
                            renderer_bridge_product_session->active() &&
                            (renderer_bridge_input_target == nullptr ||
                             !renderer_bridge_input_target->ActivateInput()))
                        {
                            throw std::runtime_error(
                                "could not restore renderer transport input after reinitialization");
                        }
                        LOG(fmt::format("[RoR] DONE Reinitializing input engine."));
                        App::GetGuiManager()->LoadingWindow.SetVisible(false); // Shown by `GUI::GameSettings` when changing 'grab mode'
#endif
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                // -- Network events --

                case MSG_NET_CONNECT_REQUESTED:
                {
#if USE_SOCKETW
                    try
                    {
                        App::GetNetwork()->StartConnecting();
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
#endif
                    break;
                }

                case MSG_NET_DISCONNECT_REQUESTED:
                {
#if USE_SOCKETW
                    try
                    {
                        if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                        {
                            App::GetNetwork()->Disconnect();
                            if (App::app_state->getEnum<AppState>() == AppState::MAIN_MENU)
                            {
                                App::GetGuiManager()->MainSelector.Close(); // We may get disconnected while still in map selection
                                App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                            }
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
#endif // USE_SOCKETW
                    break;
                }

                case MSG_NET_SERVER_KICK:
                {
                    try
                    {
                        App::GetGameContext()->PushMessage(Message(MSG_NET_DISCONNECT_REQUESTED));
                        App::GetGameContext()->PushMessage(Message(MSG_SIM_UNLOAD_TERRN_REQUESTED));
                        App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                        App::GetGuiManager()->ShowMessageBox(_LC("Network", "Network disconnected"), m.description.c_str());
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_RECV_ERROR:
                {
                    try
                    {
                        App::GetGameContext()->PushMessage(Message(MSG_NET_DISCONNECT_REQUESTED));
                        App::GetGameContext()->PushMessage(Message(MSG_SIM_UNLOAD_TERRN_REQUESTED));
                        App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                        App::GetGuiManager()->ShowMessageBox(_L("Network fatal error: "), m.description.c_str());
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_CONNECT_STARTED:
                {
                    try
                    {
                        App::GetGuiManager()->LoadingWindow.SetProgressNetConnect(m.description);
                        App::GetGuiManager()->MultiplayerSelector.SetVisible(false);
                        App::GetGameContext()->PushMessage(Message(MSG_GUI_CLOSE_MENU_REQUESTED));
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_CONNECT_PROGRESS:
                {
                    try
                    {
                        App::GetGuiManager()->LoadingWindow.SetProgressNetConnect(m.description);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_CONNECT_SUCCESS:
                {
#if USE_SOCKETW
                    try
                    {
                        App::GetGuiManager()->LoadingWindow.SetVisible(false);
                        App::GetNetwork()->StopConnecting();
                        App::mp_state->setVal((int)RoR::MpState::CONNECTED);
                        RoR::ChatSystem::SendStreamSetup();
                        if (!App::GetMumble())
                        {
                            App::CreateMumble();
                        }
                        if (App::GetNetwork()->GetTerrainName() != "any")
                        {
                            App::GetGameContext()->PushMessage(Message(MSG_SIM_LOAD_TERRN_REQUESTED, App::GetNetwork()->GetTerrainName()));
                        }
                        else
                        {
                            // Connected -> go directly to map selector
                            if (App::diag_preset_terrain->getStr().empty())
                            {
                                RoR::Message m(MSG_GUI_OPEN_SELECTOR_REQUESTED);
                                m.payload = reinterpret_cast<void*>(new LoaderType(LT_Terrain));
                                App::GetGameContext()->PushMessage(m);
                            }
                            else
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_SIM_LOAD_TERRN_REQUESTED, App::diag_preset_terrain->getStr()));
                            }
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
#endif // USE_SOCKETW
                    break;
                }

                case MSG_NET_CONNECT_FAILURE:
                {
#if USE_SOCKETW
                    try
                    {
                        App::GetGuiManager()->LoadingWindow.SetVisible(false);
                        App::GetNetwork()->StopConnecting();
                        App::GetGameContext()->PushMessage(Message(MSG_NET_DISCONNECT_REQUESTED));
                        App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                        App::GetGuiManager()->ShowMessageBox(
                            _LC("Network", "Multiplayer: connection failed"), m.description.c_str());
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
#endif // USE_SOCKETW
                    break;
                }

                case MSG_NET_REFRESH_SERVERLIST_SUCCESS:
                {
                    GUI::MpServerInfoVec* data = static_cast<GUI::MpServerInfoVec*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->MultiplayerSelector.UpdateServerlist(data);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete data;
                    break;
                }

                case MSG_NET_REFRESH_SERVERLIST_FAILURE:
                {
                    CurlFailInfo* failinfo = static_cast<CurlFailInfo*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->MultiplayerSelector.DisplayRefreshFailed(failinfo);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete failinfo;
                    break;
                }

                case MSG_NET_REFRESH_REPOLIST_SUCCESS:
                {
                    GUI::ResourcesCollection* data = static_cast<GUI::ResourcesCollection*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->RepositorySelector.UpdateResources(data);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete data;
                    break;
                }

                case MSG_NET_OPEN_RESOURCE_SUCCESS:
                {
                    GUI::ResourcesCollection* data = static_cast<GUI::ResourcesCollection*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->RepositorySelector.UpdateResourceFilesAndDescription(data);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete data;
                    break;
                }

                case MSG_NET_REFRESH_REPOLIST_FAILURE:
                {
                    CurlFailInfo* failinfo = static_cast<CurlFailInfo*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->RepositorySelector.ShowError(failinfo);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete failinfo;
                    break;
                }

                case MSG_NET_FETCH_AI_PRESETS_SUCCESS:
                {
                    try
                    {
                        App::GetGuiManager()->TopMenubar.ai_presets_extern_fetching = false;
                        App::GetGuiManager()->TopMenubar.ai_presets_extern.Parse(m.description.c_str());
                        App::GetGuiManager()->TopMenubar.RefreshAiPresets();
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_FETCH_AI_PRESETS_FAILURE:
                {
                    try
                    {
                        App::GetGuiManager()->TopMenubar.ai_presets_extern_fetching = false;
                        App::GetGuiManager()->TopMenubar.ai_presets_extern_error = m.description;
                        App::GetGuiManager()->TopMenubar.RefreshAiPresets();
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_NET_ADD_PEEROPTIONS_REQUESTED:
                {
                    PeerOptionsRequest* request = static_cast<PeerOptionsRequest*>(m.payload);
                    try
                    {
                        // Record the options for future incoming traffic.
                        App::GetNetwork()->AddPeerOptions(request);

                        // On MUTE_CHAT also purge old messages
                        if (BITMASK_IS_1(request->por_peeropts, RoRnet::PEEROPT_MUTE_CHAT))
                        {
                            App::GetConsole()->purgeNetChatMessagesByUser(request->por_uid);
                        }

                        // MUTE existing actors if needed
                        if (BITMASK_IS_1(request->por_peeropts, RoRnet::PEEROPT_MUTE_ACTORS))
                        {
                            for (ActorPtr& actor : App::GetGameContext()->GetActorManager()->GetActors())
                            {
                                if (actor->ar_net_source_id == request->por_uid)
                                {
                                    App::GetGameContext()->PushMessage(Message(MSG_SIM_MUTE_NET_ACTOR_REQUESTED, new ActorPtr(actor)));
                                }
                            }
                        }

                        // HIDE existing actors if needed
                        if (BITMASK_IS_1(request->por_peeropts, RoRnet::PEEROPT_HIDE_ACTORS))
                        {
                            for (ActorPtr& actor : App::GetGameContext()->GetActorManager()->GetActors())
                            {
                                if (actor->ar_net_source_id == request->por_uid)
                                {
                                    App::GetGameContext()->PushMessage(Message(MSG_SIM_HIDE_NET_ACTOR_REQUESTED, new ActorPtr(actor)));
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_NET_REMOVE_PEEROPTIONS_REQUESTED:
                {
                    PeerOptionsRequest* request = static_cast<PeerOptionsRequest*>(m.payload);
                    try
                    {
                        // Record the options for future incoming traffic.
                        App::GetNetwork()->RemovePeerOptions(request);

                        // un-MUTE existing actors if needed
                        if (BITMASK_IS_1(request->por_peeropts, RoRnet::PEEROPT_MUTE_ACTORS))
                        {
                            for (ActorPtr& actor : App::GetGameContext()->GetActorManager()->GetActors())
                            {
                                if (actor->ar_net_source_id == request->por_uid)
                                {
                                    App::GetGameContext()->PushMessage(Message(MSG_SIM_UNMUTE_NET_ACTOR_REQUESTED, new ActorPtr(actor)));
                                }
                            }
                        }

                        // un-HIDE existing actors if needed
                        if (BITMASK_IS_1(request->por_peeropts, RoRnet::PEEROPT_HIDE_ACTORS))
                        {
                            for (ActorPtr& actor : App::GetGameContext()->GetActorManager()->GetActors())
                            {
                                if (actor->ar_net_source_id == request->por_uid)
                                {
                                    App::GetGameContext()->PushMessage(Message(MSG_SIM_UNHIDE_NET_ACTOR_REQUESTED, new ActorPtr(actor)));
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_NET_DOWNLOAD_REPOIMAGE_SUCCESS:
                case MSG_NET_DOWNLOAD_REPOIMAGE_FAILURE: // If failed there is no file on disk so placeholder will be set instead.
                {
                    RepoImageDownloadRequest* rq = static_cast<RepoImageDownloadRequest*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->RepositorySelector.LoadDownloadedImage(rq);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                case MSG_NET_DOWNLOAD_REPOFILE_REQUESTED:
                {
                    RepoFileInstallRequest* request = static_cast<RepoFileInstallRequest*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->RepositorySelector.QueueInstallRepoFile(request);

                        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                       fmt::format(_LC("RepositorySelector", "Repo file installation requested: {}"), request->rfir_filename));
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                // -- Gameplay events --

                case MSG_SIM_PAUSE_REQUESTED:
                {
                    try
                    {
                        for (ActorPtr& actor: App::GetGameContext()->GetActorManager()->GetActors())
                        {
                            actor->muteAllSounds();
                        }
                        App::sim_state->setVal((int)SimState::PAUSED);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_SIM_UNPAUSE_REQUESTED:
                {
                    try
                    {
                        for (ActorPtr& actor: App::GetGameContext()->GetActorManager()->GetActors())
                        {
                            if (!actor->ar_muted_by_peeropt)
                            {
                                actor->unmuteAllSounds();
                            }
                        }
                        App::sim_state->setVal((int)SimState::RUNNING);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_SIM_LOAD_TERRN_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::HIDDEN);
                        App::GetGuiManager()->LoadingWindow.SetProgress(5, _L("Loading resources"));
                        App::GetContentManager()->LoadGameplayResources();

                        if (App::GetGameContext()->LoadTerrain(m.description))
                        {
                            App::GetAppContext()->BeginPostProcessScene();
                            App::GetGameContext()->CreatePlayerCharacter();
                            // Spawn preselected vehicle; commandline has precedence
                            if (App::cli_preset_vehicle->getStr() != "")
                                App::GetGameContext()->SpawnPreselectedActor(App::cli_preset_vehicle->getStr(), App::cli_preset_veh_config->getStr()); // Needs character for position
                            else if (App::diag_preset_vehicle->getStr() != "")
                                App::GetGameContext()->SpawnPreselectedActor(App::diag_preset_vehicle->getStr(), App::diag_preset_veh_config->getStr()); // Needs character for position
                            App::GetGameContext()->GetSceneMouse().InitializeVisuals();
                            App::CreateOverlayWrapper();
                            App::GetGuiManager()->DirectionArrow.LoadOverlay();
                            if (App::audio_menu_music->getBool())
                            {
                                SOUND_KILL(-1, SS_TRIG_MAIN_MENU);
                            }
                            if (GetEffectiveGfxSkyMode() == GfxSkyMode::SANDSTORM)
                            {
                                App::GetGfxScene()->GetSceneManager()->setAmbientLight(Ogre::ColourValue(0.7f, 0.7f, 0.7f));
                            }
                            else
                            {
                                App::GetGfxScene()->GetSceneManager()->setAmbientLight(Ogre::ColourValue(0.3f, 0.3f, 0.3f));
                            }
                            App::GetDiscordRpc()->UpdatePresence();
                            App::sim_state->setVal((int)SimState::RUNNING);
                            App::app_state->setVal((int)AppState::SIMULATION);
                            App::GetGuiManager()->GameMainMenu .SetVisible(false);
                            App::GetGuiManager()->MenuWallpaper->hide();
                            App::GetGuiManager()->LoadingWindow.SetVisible(false);
                            App::GetGuiManager()->SetMouseCursorVisibility(GUIManager::MouseCursorVisibility::VISIBLE);
                            App::gfx_fov_external->setVal(App::gfx_fov_external_default->getInt());
                            App::gfx_fov_internal->setVal(App::gfx_fov_internal_default->getInt());
    #ifdef USE_SOCKETW
                            if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                            {
                                App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                                      fmt::format(_LC("ChatBox", "Press {} to start chatting"),
                                                   App::GetInputEngine()->getEventCommandTrimmed(EV_COMMON_ENTER_CHATMODE)), "lightbulb.png");
                            }
    #endif // USE_SOCKETW
                            if (App::io_outgauge_mode->getInt() > 0)
                            {
                                App::GetOutGauge()->Connect();
                            }
                        }
                        else
                        {
                            if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_NET_DISCONNECT_REQUESTED));
                            }
                            else
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                            }
                            App::GetGuiManager()->LoadingWindow.SetVisible(false);
                            failed_m = true;
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_SIM_UNLOAD_TERRN_REQUESTED:
                {
                    try
                    {
                        bool renderer_scene_reset_failed = false;
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                        if (renderer_combined_session != nullptr &&
                            renderer_combined_session->active())
                        {
                            // Finalize the currently open map generation before
                            // any source Ogre object is destroyed. Direct
                            // dispatch has no transport to drain, but a surface
                            // transition may retain the immutable finalization;
                            // retry that exact production within a fixed bound.
                            const auto reset_deadline =
                                std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(2000);
                            RendererInProcessSessionResult reset_result =
                                renderer_combined_session->
                                    ResetSceneGeneration();
                            while ((reset_result.status ==
                                        RendererInProcessSessionStatus::
                                            PENDING_BACKPRESSURE ||
                                    reset_result.status ==
                                        RendererInProcessSessionStatus::
                                            PENDING_FRONTEND_SURFACE) &&
                                   std::chrono::steady_clock::now() <
                                       reset_deadline)
                            {
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(1));
                                reset_result = renderer_combined_session->
                                                   ResetSceneGeneration();
                            }
                            if (!reset_result)
                            {
                                const RendererInProcessSessionResult
                                    renderer_shutdown =
                                        CloseCombinedRendererSession(
                                            *renderer_combined_session);
                                if (renderer_shutdown.status !=
                                    RendererInProcessSessionStatus::CLOSED)
                                {
                                    FailStopApplication(EXIT_FAILURE);
                                }
                                renderer_scene_reset_failed = true;
                                failed_m = true;
                                try
                                {
                                    LOG(fmt::format(
                                        "[RoR|RendererCombined|Scene] Terrain "
                                        "unload held at generation boundary: "
                                        "status='{}', frontend={}, field='{}', "
                                        "detail='{}', backend='{}'",
                                        ToString(reset_result.status),
                                        static_cast<unsigned int>(
                                            reset_result.frontend_code),
                                        reset_result.validation.field,
                                        reset_result.validation.detail,
                                        reset_result.frontend_detail.c_str()));
                                    LOG(fmt::format(
                                        "[RoR|RendererCombined|Scene] Session "
                                        "closed after generation-reset failure: "
                                        "status='{}'",
                                        ToString(renderer_shutdown.status)));
                                }
                                catch (...)
                                {
                                }
                            }
                            else
                            {
                                renderer_combined_scene_failure_signature.clear();
                            }
                        }
#else
                        if (renderer_bridge_product_session != nullptr &&
                            renderer_bridge_product_session->active())
                        {
                            // The child/input transport is process-scoped, but
                            // producer identity and simulation time are map-
                            // scoped. Admit the authoritative empty old scene
                            // before any actor, character, terrain, or OGRE
                            // resource is destroyed. Bounded transport pressure
                            // is drained here so ClearScene cannot overtake a
                            // partially submitted asset/scene pair.
                            const auto reset_deadline =
                                std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(2000);
                            RendererOgre14ProductSessionResult reset_result =
                                renderer_bridge_product_session->
                                    ResetSceneGeneration();
                            while (reset_result.status ==
                                       RendererOgre14ProductSessionStatus::
                                           PENDING_BACKPRESSURE &&
                                   std::chrono::steady_clock::now() <
                                       reset_deadline)
                            {
                                const RendererOgre14ProductSessionResult
                                    reverse =
                                        renderer_bridge_product_session->
                                            PumpReverse();
                                if (reverse.terminal)
                                {
                                    reset_result = reverse;
                                    break;
                                }
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(1));
                                reset_result =
                                    renderer_bridge_product_session->
                                        ResetSceneGeneration();
                            }
                            if (!reset_result)
                            {
                                // Never consume an unload while leaving the
                                // old live map behind. If the empty production
                                // cannot cross the bounded transport, close the
                                // product session terminally before local OGRE
                                // teardown, suppress any chained map load, and
                                // request process shutdown after ClearScene.
                                const RendererOgre14ProductSessionResult
                                    renderer_shutdown =
                                        renderer_bridge_product_session->
                                            Shutdown();
                                renderer_scene_reset_failed = true;
                                failed_m = true;
                                // Diagnostics must not be able to interrupt
                                // the already-selected local teardown path,
                                // particularly for an allocation failure.
                                try
                                {
                                    LOG(fmt::format(
                                        "[RoR|RendererBridge|Scene] Terrain "
                                        "unload held at generation boundary: "
                                        "status='{}', host='{}', field='{}', "
                                        "detail='{}'",
                                        ToString(reset_result.status),
                                        ToString(reset_result.host_status),
                                        reset_result.validation.field,
                                        reset_result.validation.detail));
                                    LOG(fmt::format(
                                        "[RoR|RendererBridge|Scene] Product "
                                        "closed after generation-reset failure: "
                                        "status='{}', host='{}'",
                                        ToString(renderer_shutdown.status),
                                        ToString(renderer_shutdown.host_status)));
                                }
                                catch (...)
                                {
                                }
                            }
                            else
                            {
                                renderer_bridge_scene_failure_signature.clear();
                            }
                        }
#endif
                        // The product has now either accepted the authoritative
                        // empty old scene or closed terminally. Release every
                        // capture-side authenticated receipt/cache owner before
                        // actor, terrain, texture, or resource-group teardown.
                        App::GetGfxScene()->ResetOgre14GraphicsSceneGeneration();
                        App::GetAppContext()->EndPostProcessScene();
                        if (App::sim_state->getEnum<SimState>() == SimState::EDITOR_MODE)
                        {
                            App::GetGameContext()->GetTerrain()->GetTerrainEditor()->WriteSeparateOutputFile();
                        }
                        App::GetGameContext()->SaveScene("autosave.sav");
                        App::GetGameContext()->ChangePlayerActor(nullptr);
                        App::GetGameContext()->GetActorManager()->CleanUpSimulation();
                        App::GetGameContext()->GetCharacterFactory()->DeleteAllCharacters();
                        App::GetGameContext()->GetSceneMouse().DiscardVisuals();
                        App::DestroyOverlayWrapper();
                        App::GetCameraManager()->ResetAllBehaviors();
                        App::GetGuiManager()->CollisionsDebug.CleanUp();
                        App::GetGuiManager()->MainSelector.Close();
                        App::GetGuiManager()->LoadingWindow.SetVisible(false);
                        App::GetGuiManager()->MenuWallpaper->show();
                        App::GetGuiManager()->TopMenubar.ai_waypoints.clear();
                        App::sim_state->setVal((int)SimState::OFF);
                        App::app_state->setVal((int)AppState::MAIN_MENU);
                        App::GetGameContext()->UnloadTerrain();
                        App::GetGfxScene()->ClearScene();
                        App::sim_terrain_name->setStr("");
                        App::sim_terrain_gui_name->setStr("");
                        App::GetOutGauge()->Close();
#ifdef USE_OPENAL
                        App::GetSoundScriptManager()->SetListener(/*position:*/Ogre::Vector3::ZERO, /*direction:*/Ogre::Vector3::ZERO, /*up:*/Ogre::Vector3::UNIT_Y, /*velocity:*/Ogre::Vector3::ZERO);
                        App::GetSoundScriptManager()->getSoundManager()->CleanUp();
#endif // USE_OPENAL
                        App::GetGameContext()->GetRaceSystem().ResetRaceUI();
                        if (renderer_scene_reset_failed)
                        {
                            App::GetGameContext()->PushMessage(
                                Message(MSG_APP_SHUTDOWN_REQUESTED));
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_SIM_LOAD_SAVEGAME_REQUESTED:
                {
                    try
                    {
                        std::string terrn_filename = App::GetGameContext()->ExtractSceneTerrain(m.description);
                        if (terrn_filename == "")
                        {
                            Str<400> msg; msg << _L("Could not read savegame file") << "'" << m.description << "'";
                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, msg.ToCStr());
                            if (App::app_state->getEnum<AppState>() == AppState::MAIN_MENU)
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_GUI_OPEN_MENU_REQUESTED));
                            }
                        }
                        else if (terrn_filename == App::sim_terrain_name->getStr())
                        {
                            App::GetGameContext()->LoadScene(m.description);
                        }
                        else if (terrn_filename != App::sim_terrain_name->getStr() && App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                        {
                            Str<400> msg; msg << _L("Error while loading scene: Terrain mismatch");
                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, msg.ToCStr());
                        }
                        else
                        {
                            if (App::sim_terrain_name->getStr() != "")
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_SIM_UNLOAD_TERRN_REQUESTED));
                            }

                            RoR::LogFormat("[RoR|Savegame] Loading terrain '%s' ...", terrn_filename.c_str());
                            App::GetGameContext()->PushMessage(Message(MSG_SIM_LOAD_TERRN_REQUESTED, terrn_filename));
                            // Loading terrain may produce actor-spawn requests; the savegame-request must be posted after them.
                            App::GetGameContext()->ChainMessage(Message(MSG_SIM_LOAD_SAVEGAME_REQUESTED, m.description));
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_SIM_SPAWN_ACTOR_REQUESTED:
                {
                    ActorSpawnRequest* rq = static_cast<ActorSpawnRequest*>(m.payload);
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
                            App::GetGameContext()->SpawnActor(*rq);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                case MSG_SIM_MODIFY_ACTOR_REQUESTED:
                {
                    ActorModifyRequest* rq = static_cast<ActorModifyRequest*>(m.payload);
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
                            App::GetGameContext()->ModifyActor(*rq);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                case MSG_SIM_DELETE_ACTOR_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr);
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
                            App::GetGameContext()->DeleteActor(*actor_ptr);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_SEAT_PLAYER_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr); // Even if leaving vehicle, the pointer must be valid.
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
                            App::GetGameContext()->ChangePlayerActor(*actor_ptr);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_TELEPORT_PLAYER_REQUESTED:
                {
                    Ogre::Vector3* pos = static_cast<Ogre::Vector3*>(m.payload);
                    try
                    {
                        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                        {
                            App::GetGameContext()->TeleportPlayer(pos->x, pos->z);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete pos;
                    break;
                }

                case MSG_SIM_HIDE_NET_ACTOR_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr);
                        if ((App::mp_state->getEnum<MpState>() == MpState::CONNECTED) &&
                            ((*actor_ptr)->ar_state == ActorState::NETWORKED_OK))
                        {
                            ActorPtr actor = *actor_ptr;
                            if (App::GetGfxScene()->HideGfxActor(
                                    actor->GetGfxActor()))
                            {
                                actor->ar_state = ActorState::NETWORKED_HIDDEN; // Stop net. updates
                                actor->GetGfxActor()->GetSimDataBuffer().simbuf_actor_state = ActorState::NETWORKED_HIDDEN; // Hack - manually propagate the new state to SimBuffer so Character can reflect it.
                                actor->GetGfxActor()->SetAllMeshesVisible(false);
                                actor->GetGfxActor()->SetCastShadows(false);
                                actor->muteAllSounds(); // Stop sounds
                                actor->forceAllFlaresOff();
                                actor->setSmokeEnabled(false);
                            }
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_UNHIDE_NET_ACTOR_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr);
                        if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED &&
                            ((*actor_ptr)->ar_state == ActorState::NETWORKED_HIDDEN))
                        {
                            ActorPtr actor = *actor_ptr;
                            if (App::GetGfxScene()->UnhideGfxActor(
                                    actor->GetGfxActor()))
                            {
                                actor->ar_state = ActorState::NETWORKED_OK; // Resume net. updates
                                actor->GetGfxActor()->SetAllMeshesVisible(true);
                                actor->GetGfxActor()->SetCastShadows(true);
                                actor->unmuteAllSounds(); // Unmute sounds
                                actor->setSmokeEnabled(true);
                            }
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_MUTE_NET_ACTOR_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr);
                        if ((App::mp_state->getEnum<MpState>() == MpState::CONNECTED) &&
                            ((*actor_ptr)->ar_state == ActorState::NETWORKED_OK))
                        {
                            ActorPtr actor = *actor_ptr;
                            actor->ar_muted_by_peeropt = true;
                            actor->muteAllSounds();
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_UNMUTE_NET_ACTOR_REQUESTED:
                {
                    ActorPtr* actor_ptr = static_cast<ActorPtr*>(m.payload);
                    try
                    {
                        ROR_ASSERT(actor_ptr);
                        if ((App::mp_state->getEnum<MpState>() == MpState::CONNECTED) &&
                            ((*actor_ptr)->ar_state == ActorState::NETWORKED_OK))
                        {
                            ActorPtr actor = *actor_ptr;
                            actor->ar_muted_by_peeropt = false;
                            actor->unmuteAllSounds();
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete actor_ptr;
                    break;
                }

                case MSG_SIM_SCRIPT_EVENT_TRIGGERED:
                {
                    ScriptEventArgs* args = static_cast<ScriptEventArgs*>(m.payload);
                    try
                    {
                        if (args->type == SE_GENERIC_FREEFORCES_ACTIVITY && args->arg1 == freeForcesActivityType::FREEFORCESACTIVITY_BROKEN)
                        {
                            App::GetGfxScene()->OnFreeForceBroken(args->arg2ex);
                        }
                        else if (args->type == SE_GENERIC_MODCACHE_ACTIVITY && args->arg1 == modCacheActivityType::MODCACHEACTIVITY_ENTRY_ADDED && App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                        {
                            // Catch up to other players who may already be driving this mod
                            App::GetGameContext()->GetActorManager()->RetryFailedStreamRegistrations(args);
                        }
                        App::GetScriptEngine()->triggerEvent(args->type, args->arg1, args->arg2ex, args->arg3ex, args->arg4ex, args->arg5ex, args->arg6ex, args->arg7ex, args->arg8ex);

                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete args;
                    break;
                }

                case MSG_SIM_SCRIPT_CALLBACK_QUEUED:
                {
                    ScriptCallbackArgs* args = static_cast<ScriptCallbackArgs*>(m.payload);
                    try
                    {
                        App::GetScriptEngine()->envokeCallback(args->eventsource->es_script_handler, args->eventsource, args->node);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete args;
                    break;
                }

                case MSG_SIM_ACTOR_LINKING_REQUESTED:
                {
                    // Estabilishing a physics linkage between 2 actors modifies a global linkage table
                    // and triggers immediate update of every actor's linkage tables,
                    // so it has to be done sequentially on main thread.
                    // ---------------------------------------------------------------------------------
                    ActorLinkingRequest* request = static_cast<ActorLinkingRequest*>(m.payload);
                    try
                    {
                        ActorPtr actor = App::GetGameContext()->GetActorManager()->GetActorById(request->alr_actor_instance_id);
                        if (actor)
                        {
                            switch (request->alr_type)
                            {
                            case ActorLinkingRequestType::HOOK_LOCK:
                            case ActorLinkingRequestType::HOOK_UNLOCK:
                            case ActorLinkingRequestType::HOOK_TOGGLE:
                                actor->hookToggle(request->alr_hook_group, request->alr_type);
                                break;

                            case ActorLinkingRequestType::HOOK_MOUSE_TOGGLE:
                                actor->hookToggle(request->alr_hook_group, request->alr_type, request->alr_hook_mousenode);
                                    TRIGGER_EVENT_ASYNC(SE_TRUCK_MOUSE_GRAB, request->alr_actor_instance_id);
                                break;

                            case ActorLinkingRequestType::TIE_TOGGLE:
                                actor->tieToggle(request->alr_tie_group);
                                break;

                            case ActorLinkingRequestType::ROPE_TOGGLE:
                                actor->ropeToggle(request->alr_rope_group);
                                break;

                            case ActorLinkingRequestType::SLIDENODE_TOGGLE:
                                actor->toggleSlideNodeLock();
                                break;
                            }
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_SIM_ADD_FREEFORCE_REQUESTED:
                {
                    FreeForceRequest* rq = static_cast<FreeForceRequest*>(m.payload);
                    try
                    {
                        App::GetGameContext()->GetActorManager()->AddFreeForce(rq);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                case MSG_SIM_MODIFY_FREEFORCE_REQUESTED:
                {
                    FreeForceRequest* rq = static_cast<FreeForceRequest*>(m.payload);
                    try
                    {
                        App::GetGameContext()->GetActorManager()->ModifyFreeForce(rq);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                case MSG_SIM_REMOVE_FREEFORCE_REQUESTED:
                {
                    FreeForceID_t* rq = static_cast<FreeForceID_t*>(m.payload);
                    try
                    {
                        App::GetGameContext()->GetActorManager()->RemoveFreeForce(*rq);
                        App::GetGfxScene()->OnFreeForceRemoved(*rq);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete rq;
                    break;
                }

                // -- GUI events ---

                case MSG_GUI_OPEN_MENU_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->GameMainMenu.SetVisible(true);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_CLOSE_MENU_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->GameMainMenu.SetVisible(false);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_OPEN_SELECTOR_REQUESTED:
                {
                    LoaderType* type = static_cast<LoaderType*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->MainSelector.Show(*type, m.description);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete type;
                    break;
                }

                case MSG_GUI_CLOSE_SELECTOR_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->MainSelector.Close();
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_MP_CLIENTS_REFRESH:
                {
                    try
                    {
                        App::GetGuiManager()->MpClientList.UpdateClients();
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_SHOW_MESSAGE_BOX_REQUESTED:
                {
                    GUI::MessageBoxConfig* conf = static_cast<GUI::MessageBoxConfig*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->ShowMessageBox(*conf);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete conf;
                    break;
                }

                case MSG_NET_DOWNLOAD_REPOFILE_PROGRESS:
                {
                    int* percentage = static_cast<int*>(m.payload);
                    try
                    {
                        if (percentage)
                        {
                            App::GetGuiManager()->LoadingWindow.SetProgress(*percentage, m.description, false);
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete percentage;
                    break;
                }

                case MSG_NET_DOWNLOAD_REPOFILE_SUCCESS:
                case MSG_NET_DOWNLOAD_REPOFILE_FAILURE:
                {
                    RepoFileInstallRequest* request = static_cast<RepoFileInstallRequest*>(m.payload);
                    try
                    {
                        App::GetGuiManager()->LoadingWindow.SetVisible(false);
                        App::GetGuiManager()->RepositorySelector.SetVisible(true);
                        App::GetGuiManager()->RepositorySelector.InstallDownloadedRepoFile(m.type, request);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_GUI_REFRESH_TUNING_MENU_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->TopMenubar.RefreshTuningMenu();
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_SHOW_CHATBOX_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->ChatBox.SetVisible(true);
                        if (m.description != "")
                        {
                            App::GetGuiManager()->ChatBox.AssignBuffer(m.description);
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_GUI_OPEN_MP_SETTINGS_REQUESTED:
                {
                    try
                    {
                        App::GetGuiManager()->GameMainMenu.SetVisible(false);
                        App::GetGuiManager()->MultiplayerSelector.SetVisible(true);
                        App::GetGuiManager()->MultiplayerSelector.SetSettingsTabSelected();
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                // -- Editing events --

                case MSG_EDI_MODIFY_GROUNDMODEL_REQUESTED:
                {
                    try
                    {
                        ground_model_t* modified_gm = static_cast<ground_model_t*>(m.payload);
                        ground_model_t* live_gm = App::GetGameContext()->GetTerrain()->GetCollisions()->getGroundModelByString(modified_gm->name);
                        *live_gm = *modified_gm; // Copy over
                        //DO NOT `delete` the payload - it's a weak pointer, the data are owned by `RoR::Collisions`; See `enum MsgType` in file 'Application.h'.
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_ENTER_TERRN_EDITOR_REQUESTED:
                {
                    try
                    {
                        if (App::sim_state->getEnum<SimState>() != SimState::EDITOR_MODE)
                        {
                            App::sim_state->setVal((int)SimState::EDITOR_MODE);
                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                          _L("Entered terrain editing mode"));

                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                fmt::format(_L("Press {} or middle mouse click to select an object"),
                                    App::GetInputEngine()->getEventCommandTrimmed(EV_COMMON_ENTER_OR_EXIT_TRUCK)), "lightbulb.png");
                            
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_LEAVE_TERRN_EDITOR_REQUESTED:
                {
                    try
                    {
                        if (App::sim_state->getEnum<SimState>() == SimState::EDITOR_MODE)
                        {
                            App::GetGameContext()->GetTerrain()->GetTerrainEditor()->WriteSeparateOutputFile(); // Always write 'editor_out.log'
                            App::GetGameContext()->GetTerrain()->GetTerrainEditor()->ClearSelectedObject();
                            App::sim_state->setVal((int)SimState::RUNNING);
                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                                          _L("Left terrain editing mode"));
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_SAVE_TERRN_CHANGES_REQUESTED:
                {
                    try
                    {
                        if (App::sim_state->getEnum<SimState>() == SimState::EDITOR_MODE
                            && App::GetGameContext()->GetTerrain()->getCacheEntry()->resource_bundle_type == "FileSystem")
                        {
                            // This is a project (unzipped mod) - update TOBJ files in place
                            App::GetGameContext()->GetTerrain()->GetTerrainEditor()->WriteEditsToTobjFiles();
                            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE,
                                _L("Terrain files have been updated"));
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_LOAD_BUNDLE_REQUESTED:
                {
                    CacheEntryPtr* entry_ptr = static_cast<CacheEntryPtr*>(m.payload);
                    try
                    {
                        App::GetCacheSystem()->LoadResource(*entry_ptr);
                        TRIGGER_EVENT_ASYNC(SE_GENERIC_MODCACHE_ACTIVITY,  
                            /*ints*/ MODCACHEACTIVITY_BUNDLE_LOADED, (*entry_ptr)->number, 0, 0,
                            /*strings*/ (*entry_ptr)->resource_group);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete entry_ptr;
                    break;
                }

                case MSG_EDI_RELOAD_BUNDLE_REQUESTED:
                {
                    // To reload the bundle, it's resource group must be destroyed and re-created. All actors using it must be deleted.
                    CacheEntryPtr* entry_ptr = static_cast<CacheEntryPtr*>(m.payload);
                    try
                    {
                        bool all_clear = true;
                        for (ActorPtr& actor: App::GetGameContext()->GetActorManager()->GetActors())
                        {
                            if (actor->GetGfxActor()->GetResourceGroup() == (*entry_ptr)->resource_group)
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_SIM_DELETE_ACTOR_REQUESTED, static_cast<void*>(new ActorPtr(actor))));
                                all_clear = false;
                            }
                        }

                        if (all_clear)
                        {
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                            // A bundle reload destroys and re-creates a live
                            // resource group's materials and textures, which
                            // is a scene-generation boundary for the combined
                            // renderer: retained frozen material decisions
                            // and cached authenticated observations would
                            // otherwise fail every later capture closed on
                            // cache revalidation, presenting as a permanent
                            // freeze. Finalize the open generation exactly
                            // like the terrain-unload boundary above.
                            if (renderer_combined_session != nullptr &&
                                renderer_combined_session->active())
                            {
                                const auto reload_reset_deadline =
                                    std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(2000);
                                RendererInProcessSessionResult
                                    reload_reset_result =
                                        renderer_combined_session->
                                            ResetSceneGeneration();
                                while ((reload_reset_result.status ==
                                            RendererInProcessSessionStatus::
                                                PENDING_BACKPRESSURE ||
                                        reload_reset_result.status ==
                                            RendererInProcessSessionStatus::
                                                PENDING_FRONTEND_SURFACE) &&
                                       std::chrono::steady_clock::now() <
                                           reload_reset_deadline)
                                {
                                    std::this_thread::sleep_for(
                                        std::chrono::milliseconds(1));
                                    reload_reset_result =
                                        renderer_combined_session->
                                            ResetSceneGeneration();
                                }
                                if (!reload_reset_result)
                                {
                                    LOG(fmt::format(
                                        "[RoR|RendererCombined|Scene] Bundle "
                                        "reload held at generation boundary: "
                                        "status='{}', frontend={}, field='{}',"
                                        " detail='{}', backend='{}'",
                                        ToString(reload_reset_result.status),
                                        static_cast<unsigned int>(
                                            reload_reset_result.frontend_code),
                                        reload_reset_result.validation.field,
                                        reload_reset_result.validation
                                            .detail,
                                        reload_reset_result.frontend_detail
                                            .c_str()));
                                    const RendererInProcessSessionResult
                                        reload_shutdown =
                                            CloseCombinedRendererSession(
                                                *renderer_combined_session);
                                    if (reload_shutdown.status !=
                                        RendererInProcessSessionStatus::CLOSED)
                                    {
                                        FailStopApplication(EXIT_FAILURE);
                                    }
                                    App::GetGameContext()->PushMessage(
                                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                                    delete entry_ptr;
                                    break;
                                }
                            }
#endif
                            // Release every capture-side authenticated
                            // receipt/cache owner before this resource group
                            // is destroyed; the next capture re-observes and
                            // re-mints the fresh generation.
                            App::GetGfxScene()->
                                ResetOgre14GraphicsSceneGeneration();
                            // Nobody uses the RG anymore -> destroy and re-create it.
                            App::GetCacheSystem()->ReLoadResource(*entry_ptr);

                            TRIGGER_EVENT_ASYNC(SE_GENERIC_MODCACHE_ACTIVITY,  
                                /*ints*/ MODCACHEACTIVITY_BUNDLE_RELOADED, (*entry_ptr)->number, 0, 0,
                                /*strings*/ (*entry_ptr)->resource_group);

                            delete entry_ptr;
                        }
                        else
                        {
                            // Re-post the same message again so that it's message chain is executed later.
                            App::GetGameContext()->PushMessage(m);
                            failed_m = true;
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_UNLOAD_BUNDLE_REQUESTED:
                {
                    // Unloading bundle means the resource group will be destroyed. All actors using it must be deleted.
                    CacheEntryPtr* entry_ptr = static_cast<CacheEntryPtr*>(m.payload);
                    try
                    {
                        bool all_clear = true;
                        for (ActorPtr& actor: App::GetGameContext()->GetActorManager()->GetActors())
                        {
                            ROR_ASSERT(actor);
                            ROR_ASSERT(actor->getUsedActorEntry());
                            const bool uses_actor_rg = (actor->getUsedActorEntry()->resource_group == (*entry_ptr)->resource_group);
                            // Skin entry is optional.
                            const bool uses_skin_rg = (actor->getUsedSkinEntry() && actor->getUsedSkinEntry()->resource_group == (*entry_ptr)->resource_group);
                            // Look for addonparts, too.
                            const bool uses_addonpart_rg = std::find_if(
                                actor->getUsedAddonpartEntries().begin(),
                                actor->getUsedAddonpartEntries().end(),
                                [entry_ptr](const CacheEntryPtr& ap_entry)
                                {
                                    return ap_entry->resource_group == (*entry_ptr)->resource_group;
                                }) != actor->getUsedAddonpartEntries().end();
                            // Finally look for assetpacks
                            const bool uses_assetpack_rg = std::find_if(
                                actor->getUsedAssetpackEntries().begin(),
                                actor->getUsedAssetpackEntries().end(),
                                [entry_ptr](const CacheEntryPtr& ap_entry)
                                {
                                    return ap_entry->resource_group == (*entry_ptr)->resource_group;
                                }) != actor->getUsedAssetpackEntries().end();
                            if (uses_actor_rg || uses_skin_rg || uses_addonpart_rg || uses_assetpack_rg)
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_SIM_DELETE_ACTOR_REQUESTED, static_cast<void*>(new ActorPtr(actor))));
                                all_clear = false;
                            }
                        }
                        // Check terrain, too! Could have been uninstalled via RepoUI.
                        if (App::GetGameContext()->GetTerrain()
                            && App::GetGameContext()->GetTerrain()->getCacheEntry()->resource_group == (*entry_ptr)->resource_group)
                        {
                            if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_NET_DISCONNECT_REQUESTED));
                            }
                            else
                            {
                                App::GetGameContext()->PushMessage(Message(MSG_SIM_UNLOAD_TERRN_REQUESTED));
                            }
                            all_clear = false;
                        }

                        if (all_clear)
                        {
                            // Nobody uses the RG anymore -> destroy it.
                            if (App::GetCacheSystem()->UnLoadResource(
                                    *entry_ptr))
                            {
                                TRIGGER_EVENT_ASYNC(
                                    SE_GENERIC_MODCACHE_ACTIVITY,
                                    /*ints*/
                                    MODCACHEACTIVITY_BUNDLE_UNLOADED,
                                    (*entry_ptr)->number,
                                    0,
                                    0);

                                delete entry_ptr;
                            }
                            else
                            {
                                // Keep the owner discoverable and retry after
                                // OGRE releases the resource group.
                                App::GetGameContext()->PushMessage(m);
                                failed_m = true;
                            }
                        }
                        else
                        {
                            // Re-post the same message again so that it's message chain is executed later.
                            App::GetGameContext()->PushMessage(m);
                            failed_m = true;
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
  
                    break;
                }

                case MSG_EDI_DELETE_BUNDLE_REQUESTED:
                {
                    try
                    {
                        std::string bundle_filepath;
                        if (!App::GetCacheSystem()->IsRepoFileInstalled(m.description, /*[out]*/bundle_filepath))
                        {
                            break; // nothing to do
                        }

                        // A bundle can be mounted as a dependency inside a
                        // different bundle's resource group. Unload every
                        // live owner group before removing the archive.
                        const std::vector<CacheEntryPtr> loaded_owners =
                            App::GetCacheSystem()->
                                FindLoadedResourceGroupOwnersUsingBundlePath(
                                    bundle_filepath);
                        const bool all_clear = loaded_owners.empty();
                        for (const CacheEntryPtr& owner: loaded_owners)
                        {
                            App::GetGameContext()->PushMessage(
                                Message(
                                    MSG_EDI_UNLOAD_BUNDLE_REQUESTED,
                                    static_cast<void*>(
                                        new CacheEntryPtr(owner))));
                        }

                        if (all_clear)
                        {
                            std::string bundle_basename, bundle_dirpath;
                            Ogre::StringUtil::splitFilename(bundle_filepath, bundle_basename, bundle_dirpath);
                            if (App::GetCacheSystem()->
                                    DeleteResourceBundleByFilename(
                                        bundle_basename))
                            {
                                App::GetGuiManager()->RepositorySelector.
                                    NotifyRepoFileUninstalled(bundle_basename);
                            }
                        }
                        else
                        {
                            // Re-post the same message again so that it's message chain is executed later.
                            App::GetGameContext()->PushMessage(m);
                            failed_m = true;
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    break;
                }

                case MSG_EDI_CREATE_PROJECT_REQUESTED:
                {
                    CreateProjectRequest* request = static_cast<CreateProjectRequest*>(m.payload);
                    try 
                    {
                        if (!App::GetCacheSystem()->CreateProject(request))
                        {
                            failed_m = true;
                        }
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_EDI_MODIFY_PROJECT_REQUESTED:
                {
                    ModifyProjectRequest* request = static_cast<ModifyProjectRequest*>(m.payload);
                    try
                    {
                        if (App::mp_state->getEnum<MpState>() != MpState::CONNECTED) // Do not allow tuning in multiplayer
                        {
                            App::GetCacheSystem()->ModifyProject(request);
                        }
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_EDI_DELETE_PROJECT_REQUESTED:
                {
                    CacheEntryPtr* entry_ptr = static_cast<CacheEntryPtr*>(m.payload);
                    try
                    {
                        App::GetCacheSystem()->DeleteProject(*entry_ptr);
                    }
                    catch (...) 
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete entry_ptr;
                    break;
                }

                case MSG_EDI_ADD_FREEBEAMGFX_REQUESTED:
                {
                    FreeBeamGfxRequest* request = static_cast<FreeBeamGfxRequest*>(m.payload);
                    try
                    {
                        App::GetGfxScene()->AddFreeBeamGfx(request);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_EDI_MODIFY_FREEBEAMGFX_REQUESTED:
                {
                    FreeBeamGfxRequest* request = static_cast<FreeBeamGfxRequest*>(m.payload);
                    try
                    {
                        App::GetGfxScene()->ModifyFreeBeamGfx(request);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                case MSG_EDI_DELETE_FREEBEAMGFX_REQUESTED:
                {
                    FreeBeamGfxID_t* request = static_cast<FreeBeamGfxID_t*>(m.payload);
                    try
                    {
                        App::GetGfxScene()->RemoveFreeBeamGfx(*request);
                    }
                    catch (...)
                    {
                        HandleMsgQueueException(m.type);
                    }
                    delete request;
                    break;
                }

                default:;
                }

                // Once shutdown is committed, leave any remaining raw-payload
                // messages undispatched. Process exit owns those allocations;
                // invoking their GUI/input/render callbacks after SHUTDOWN is
                // unsafe and was the source of the Win32 fast-fail path.
                if (App::app_state->getEnum<AppState>() == AppState::SHUTDOWN)
                {
                    break;
                }

                // Process chained messages
                if (!failed_m)
                {
                    for (Message& chained_msg: m.chain)
                    {
                        App::GetGameContext()->PushMessage(chained_msg);
                    }
                }

            } // Game events block
            OgreProfileEnd("RoR message queue");

            // A shutdown request is handled inside the queue above. Do not run
            // input, script, GUI, or renderer callbacks for a state that has
            // already transitioned to SHUTDOWN. In particular, the Win32
            // D3D11/OIS stack must not receive one more frame after the quit
            // message has been committed.
            if (App::app_state->getEnum<AppState>() == AppState::SHUTDOWN)
            {
                LOG("[RoR|Shutdown] Leaving the main loop after the shutdown message");
                break;
            }

            // Arm only after the message queue has created and seated the
            // startup truck. Admission requires this still to be a fresh
            // joined scene at global physics step zero.
            App::UpdateWorldModelCaptureRequest();
            const bool world_model_capture_frame =
                App::WorldModelCaptureOwnsSimulationLoop();

            // Check FPS limit
            if (App::gfx_fps_limit->getInt() > 0)
            {
                OgreProfile("RoR FPS limiter");
                const float min_frame_time = 1.0f / Ogre::Math::Clamp(App::gfx_fps_limit->getInt(), 5, 240);
                float dt = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();
                while (dt < min_frame_time)
                {
                    dt = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start_time).count();
                }
            } // Check FPS limit block

            OgreProfileBegin("RoR Main Loop");
            const auto record_frame_budget = [&](float frame_dt)
            {
                if (frame_budget_session == nullptr)
                    return;
                // Loading screens and GUI-only grants are not world frames.
                // The explicit forward-native showcase is the one MAIN_MENU
                // exception: its authenticated package is already the whole
                // presented scene and the hidden producer does no rendering.
                bool frame_budget_scene_ready = false;
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                frame_budget_scene_ready =
                    renderer_combined_native_visual_showcase;
#endif
                if (!frame_budget_scene_ready)
                {
                    // Start warm-up only after the requested terrain is
                    // authoritative and the requested startup vehicle is
                    // seated. This keeps the receipt tied to the exact scene
                    // it names.
                    const bool requested_actor_ready =
                        App::cli_preset_vehicle->getStr().empty() ||
                        App::GetGameContext()->GetPlayerActor() != nullptr;
                    frame_budget_scene_ready =
                        App::app_state->getEnum<AppState>() ==
                            AppState::SIMULATION &&
                        !App::sim_terrain_name->getStr().empty() &&
                        requested_actor_ready;
                }
                if (!frame_budget_scene_ready)
                    return;
                frame_budget_session->RecordFrame(
                    static_cast<double>(frame_dt));
                // The terrain and actor are loaded by this loop's own message
                // queue, so they are unknown when the recorder is armed. Name
                // the scene on the first recorded frame only; the identity is
                // observed again at finalize to catch a mid-run map reset,
                // and neither observation costs the measured frames anything.
                if (frame_budget_session->AcceptedFrames() == 1U)
                {
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                    if (renderer_combined_native_visual_showcase)
                    {
                        frame_budget_session->ObserveSceneIdentity(
                            renderer_combined_native_visual_scene ==
                                    RendererCombinedNativeVisualScene::
                                        A0_LIGHTING_COUPON
                                ? Render::kNativeVisualShowcasePackageId
                                : Render::kNativeVisualShowcaseA1PackageId,
                            "");
                    }
                    else
#endif
                    {
                    frame_budget_session->ObserveSceneIdentity(
                        App::sim_terrain_name->getStr(),
                        App::cli_preset_vehicle->getStr());
                    }
                }
                if (!frame_budget_shutdown_requested &&
                        frame_budget_session->ShutdownRequested())
                {
                    frame_budget_shutdown_requested = true;
                    LogFormat(
                        "[RoR|Perf] Requested frame count reached (%llu); "
                        "shutting down",
                        static_cast<unsigned long long>(
                            frame_budget_session->AcceptedFrames()));
                    App::GetGameContext()->PushMessage(
                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                }
            };
            // In combined mode a poll that is waiting for the preceding native
            // frame is not a simulation or presented frame. Its elapsed time is
            // accumulated until the session grants the next simulation step
            // below. The ordinary renderer has no such split grant boundary.
            float dt = 0.0F;
#if !defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
            const auto now = std::chrono::high_resolution_clock::now();
            dt = std::chrono::duration<float>(now - start_time).count();
            start_time = now;
            record_frame_budget(dt);
#endif

#ifdef USE_SOCKETW
            // Process incoming network traffic
            if (!world_model_capture_frame &&
                App::mp_state->getEnum<MpState>() == MpState::CONNECTED)
            {
                std::vector<RoR::NetRecvPacket> packets = App::GetNetwork()->GetIncomingStreamData();
                if (!packets.empty())
                {
                    RoR::ChatSystem::HandleStreamData(packets);
                    if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                    {
                        App::GetGameContext()->GetActorManager()->HandleActorStreamData(packets);
                        App::GetGameContext()->GetCharacterFactory()->handleStreamData(packets); // Update characters last (or else beam coupling might fail)
                    }
                }
            }
#endif // USE_SOCKETW

            // Set arcade controls and hydro coupling settings for player actors
            // Default to false for other actors.
            if (!world_model_capture_frame &&
                App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                ActorPtr player_actor = App::GetGameContext()->GetPlayerActor();
                for (ActorPtr actor : App::GetGameContext()->GetActorManager()->GetActors())
                {
                    bool is_player_actor = player_actor != nullptr && actor->getInstanceId() == player_actor->getInstanceId();
                    actor->ar_arcade_controls = is_player_actor && App::io_arcade_controls->getBool();
                    actor->ar_hydro_speed_coupling_enabled = is_player_actor && App::io_hydro_coupling->getBool();
                }
            }

            // Process input events
            OgreProfileBegin("Input processing");
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
            bool renderer_combined_simulation_granted = false;
            RendererInProcessSessionResult renderer_combined_events;
            bool renderer_combined_events_available = false;
            bool frame_budget_native_draw_recorded = false;
            const auto record_combined_native_budget =
                [&](std::uint64_t frontend_frame_id)
            {
                if (frame_budget_session == nullptr ||
                    frame_budget_native_draw_recorded)
                {
                    return;
                }
                const RendererRetainedSceneAudit retained_scene_audit =
                    renderer_combined_presenter.RetainedSceneAudit();
                const bool exact_native_scene =
                    retained_scene_audit.available &&
                    retained_scene_audit.version >= 6U &&
                    retained_scene_audit.last_native_renderer_frame_id ==
                        frontend_frame_id &&
                    retained_scene_audit.last_native_pass_metrics_exact;
                frame_budget_session->RecordNativeSceneDrawSubmissions(
                    exact_native_scene
                        ? retained_scene_audit.last_native_scene_draws
                        : 0U,
                    exact_native_scene);
                if (exact_native_scene)
                {
                    const auto record_native_phase =
                        [&](FrameTimeBudgetPhase phase,
                            std::uint64_t microseconds)
                    {
                        frame_budget_session->RecordPhaseMicroseconds(
                            phase, microseconds);
                    };
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_VALIDATION,
                        retained_scene_audit.
                            last_validation_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_FRAME_PREPARE,
                        retained_scene_audit.
                            last_frame_prepare_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_LIGHTS,
                        retained_scene_audit.last_light_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_INSTANCES,
                        retained_scene_audit.last_instance_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_PREPARE,
                        retained_scene_audit.
                            last_native_prepare_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_RENDER,
                        retained_scene_audit.
                            last_native_render_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_POST_RENDER,
                        retained_scene_audit.
                            last_post_render_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_CLEANUP,
                        retained_scene_audit.last_cleanup_phase_microseconds);
                    record_native_phase(
                        FrameTimeBudgetPhase::NATIVE_PUBLICATION,
                        retained_scene_audit.
                            last_publication_phase_microseconds);
                }
                frame_budget_native_draw_recorded = true;
            };
            // Capture first: it clears prior relative deltas. The presenter's
            // sole SDL drain then installs this frame's ordered transitions,
            // before any gameplay/GUI consumer observes InputEngine state.
            App::GetInputEngine()->Capture();
            bool renderer_input_captured = true;
            if (renderer_combined_session != nullptr &&
                renderer_combined_session->active())
            {
                renderer_combined_events = renderer_combined_session->
                    PumpEventsBeforeSimulation();
                renderer_combined_events_available = true;
                const RendererInProcessSessionResult& events =
                    renderer_combined_events;
                renderer_combined_simulation_granted =
                    events.simulation_may_advance;
                if (events.shutdown_requested ||
                    events.status ==
                        RendererInProcessSessionStatus::SHUTDOWN_REQUESTED)
                {
                    App::GetGameContext()->PushMessage(
                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                }
                if (events.terminal)
                {
                    LOG(fmt::format(
                        "[RoR|RendererCombined|Input] Direct event pump "
                        "failed: status='{}', frontend={}, field='{}', "
                        "detail='{}'",
                        ToString(events.status),
                        static_cast<unsigned int>(events.frontend_code),
                        events.validation.field,
                        events.validation.detail));
                    App::GetGameContext()->PushMessage(
                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                    const RendererInProcessSessionResult renderer_shutdown =
                        CloseCombinedRendererSession(
                            *renderer_combined_session);
                    if (renderer_shutdown.status !=
                        RendererInProcessSessionStatus::CLOSED)
                    {
                        FailStopApplication(EXIT_FAILURE);
                    }
                }
                else if (!events &&
                         events.status !=
                             RendererInProcessSessionStatus::
                                 PENDING_BACKPRESSURE &&
                         events.status !=
                             RendererInProcessSessionStatus::
                                 PENDING_FRONTEND_SURFACE)
                {
                    LOG(fmt::format(
                        "[RoR|RendererCombined|Input] Frame grant rejected: "
                        "status='{}', frontend={}, field='{}', detail='{}'",
                        ToString(events.status),
                        static_cast<unsigned int>(events.frontend_code),
                        events.validation.field,
                        events.validation.detail));
                    App::GetGameContext()->PushMessage(
                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                }
            }
            else
            {
                App::GetGameContext()->PushMessage(
                    Message(MSG_APP_SHUTDOWN_REQUESTED));
            }

            if (!renderer_combined_simulation_granted)
            {
                // The direct session may be retiring an immutable frame or
                // synchronizing a surface. It explicitly withholds the grant;
                // do not let physics, GUI, or script state overtake it.
                OgreProfileEnd("Input processing");
                OgreProfileEnd("RoR Main Loop");
                continue;
            }
            // One combined game frame begins only when the preceding native
            // presentation boundary has granted simulation. Include every
            // backpressure poll in this delta; otherwise the performance
            // recorder measures the polling cadence while physics advances by
            // only the final poll interval.
            const auto combined_frame_now =
                std::chrono::high_resolution_clock::now();
            dt = std::chrono::duration<float>(
                combined_frame_now - start_time).count();
            start_time = combined_frame_now;
            record_frame_budget(dt);
            if (renderer_combined_events_available &&
                renderer_combined_events.status ==
                    RendererInProcessSessionStatus::FRAME_COMPLETED)
            {
                renderer_combined_actor_control_qualification.
                    ObserveCompletedFrame(
                        renderer_combined_events.frontend_frame_id,
                        renderer_combined_presenter.RetainedSceneAudit());
            }
            if (frame_budget_session != nullptr &&
                renderer_combined_events_available &&
                renderer_combined_events.status ==
                    RendererInProcessSessionStatus::FRAME_COMPLETED)
            {
                record_combined_native_budget(
                    renderer_combined_events.frontend_frame_id);
            }
#else
            bool renderer_input_captured = false;
            if (renderer_bridge_product_session != nullptr &&
                renderer_bridge_product_session->active())
            {
                // The child owns physical devices. Sample/clear prior deltas
                // exactly once, then apply every published reverse batch
                // before any gameplay consumer reads InputEngine state.
                App::GetInputEngine()->Capture();
                renderer_input_captured = true;
                const RendererOgre14ProductSessionResult reverse =
                    renderer_bridge_product_session->PumpReverse();
                if (reverse.terminal)
                {
                    LOG(fmt::format(
                        "[RoR|RendererBridge|Product] Reverse stream failed: "
                        "status='{}', host='{}', input='{}'",
                        ToString(reverse.status),
                        ToString(reverse.host_status),
                        ToString(reverse.input_status)));
                    App::GetGameContext()->PushMessage(
                        Message(MSG_APP_SHUTDOWN_REQUESTED));
                    (void)renderer_bridge_product_session->Shutdown();
                }
            }
#endif
            if (world_model_capture_frame)
            {
                // Capture owns the simulation scheduler and samples devices
                // exactly once before its single 48 Hz transition. The owned
                // batch advances input-bounce time once; no GUI, sky, actor,
                // or script input mutator runs on this path.
                if (!renderer_input_captured)
                    App::GetInputEngine()->Capture();
            }
            else if (dt != 0.f)
            {
                if (!renderer_input_captured)
                    App::GetInputEngine()->Capture();
                App::GetInputEngine()->updateKeyBounces(dt);

                if (!App::GetGuiManager()->GameControls.IsInteractiveKeyBindingActive())
                {
                    if (!App::GetGuiManager()->MainSelector.IsVisible() && !App::GetGuiManager()->MultiplayerSelector.IsVisible() &&
                        !App::GetGuiManager()->GameSettings.IsVisible() && !App::GetGuiManager()->GameControls.IsVisible() &&
                        !App::GetGuiManager()->GameAbout.IsVisible() && !App::GetGuiManager()->RepositorySelector.IsVisible())
                    {
                        App::GetGameContext()->HandleSavegameHotkeys();
                    }
                    App::GetGameContext()->UpdateGlobalInputEvents();
                    App::GetGuiManager()->UpdateInputEvents(dt);

                    if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                    {
                        if (App::sim_state->getEnum<SimState>() == SimState::EDITOR_MODE)
                        {
                            App::GetGameContext()->UpdateSkyInputEvents(dt);
                            App::GetGameContext()->GetTerrain()->GetTerrainEditor()->UpdateInputEvents(dt);
                        }
                        else
                        {
                            App::GetGameContext()->GetCharacterFactory()->Update(dt); // Character MUST be updated before CameraManager, otherwise camera position is always 1 frame behind the character position, causing stuttering.
                        }
                        App::GetCameraManager()->UpdateInputEvents(dt);
                        App::GetOverlayWrapper()->update(dt);
                        App::GetGameContext()->GetRepairMode().UpdateInputEvents(dt);
                        App::GetGameContext()->GetActorManager()->UpdateInputEvents(dt);
                        if (App::sim_state->getEnum<SimState>() == SimState::RUNNING)
                        {
                            if (App::GetCameraManager()->GetCurrentBehavior() != CameraManager::CAMERA_BEHAVIOR_FREE)
                            {
                                App::GetGameContext()->UpdateSimInputEvents(dt);
                            }

                            App::GetGameContext()->UpdateSkyInputEvents(dt);
                            for (ActorPtr actor : App::GetGameContext()->GetActorManager()->GetActors())
                            {
                                if (actor->ar_state != ActorState::NETWORKED_OK)
                                {
                                    const bool deterministic_replay_owns_input =
                                        App::GetGameContext()->GetActorManager()->
                                            ShouldSuppressLiveInputForDeterministicReplay(actor);
                                    if (!deterministic_replay_owns_input)
                                    {
                                        App::GetGameContext()->UpdateCommonInputEvents(dt, actor);
                                        if (actor->ar_state != ActorState::LOCAL_REPLAY)
                                        {
                                            if (actor->ar_driveable == TRUCK)
                                            {
                                                App::GetGameContext()->UpdateTruckInputEvents(dt, actor);
                                            }
                                            if (actor->ar_driveable == AIRPLANE)
                                            {
                                                App::GetGameContext()->UpdateAirplaneInputEvents(dt, actor);
                                            }
                                            if (actor->ar_driveable == BOAT)
                                            {
                                                App::GetGameContext()->UpdateBoatInputEvents(dt, actor);
                                            }
                                        }

                                        actor->UpdatePropAnimInputEvents();
                                        for (ActorPtr linked_actor : actor->ar_linked_actors)
                                        {
                                            linked_actor->UpdatePropAnimInputEvents();
                                        }
                                    }
                                }
                            }
                        }
                    } // app state SIMULATION
                } // interactive key binding mode
            } // dt != 0
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
            if (renderer_combined_input_target != nullptr &&
                App::app_state->getEnum<AppState>() == AppState::SIMULATION &&
                App::sim_state->getEnum<SimState>() == SimState::RUNNING)
            {
                renderer_combined_actor_control_qualification.
                    ObserveResolvedInput(
                        renderer_combined_input_target->Audit(),
                        App::GetGameContext()->GetPlayerActor());
            }
#endif
            OgreProfileEnd("Input processing");

            if (world_model_capture_frame)
            {
                App::CaptureWorldModelControlledFrame();
            }

            // Update OutGauge device
            if (!world_model_capture_frame &&
                App::io_outgauge_mode->getInt() > 0)
            {
                OgreProfile("OutGauge");
                App::GetOutGauge()->Update(dt, App::GetGameContext()->GetPlayerActor());
            }

            // Early GUI updates which require halted physics
            App::GetGuiManager()->NewImGuiFrame(dt);
            if (!world_model_capture_frame &&
                App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                OgreProfile("Scene and GUI");
                App::GetGuiManager()->DrawSimulationGui(dt);
                for (ActorPtr& actor : App::GetGameContext()->GetActorManager()->GetActors())
                {
                    actor->GetGfxActor()->UpdateDebugView();
                }
                if (App::GetGameContext()->GetPlayerActor())
                {
                    App::GetGuiManager()->VehicleInfoTPanel.UpdateStats(dt, App::GetGameContext()->GetPlayerActor());
                    if (App::GetGuiManager()->FrictionSettings.IsVisible())
                    {
                        App::GetGuiManager()->FrictionSettings.setActiveCol(App::GetGameContext()->GetPlayerActor()->ar_last_fuzzy_ground_model);
                    }
                }
            }

#ifdef USE_MUMBLE
            if (App::GetMumble())
            {
                OgreProfile("Mumble");
                App::GetMumble()->Update(); // 3d voice over network
            }
#endif // USE_MUMBLE

#ifdef USE_OPENAL
            OgreProfileBegin("3D audio");
            App::GetSoundScriptManager()->update(dt); // update 3d audio listener position
            OgreProfileEnd("3D audio");
#endif // USE_OPENAL

#ifdef USE_ANGELSCRIPT
            OgreProfileBegin("Scripting");
            if (!world_model_capture_frame)
                App::GetScriptEngine()->framestep(dt);
            OgreProfileEnd("Scripting");
#endif // USE_ANGELSCRIPT

            if (!world_model_capture_frame &&
                App::io_ffb_enabled->getBool() &&
                App::sim_state->getEnum<SimState>() == SimState::RUNNING)
            {
                OgreProfile("Force Feedback");
                App::GetAppContext()->GetForceFeedback().Update();
            }

            OgreProfileBegin("Simulation");
            if (!world_model_capture_frame &&
                App::sim_state->getEnum<SimState>() == SimState::RUNNING)
            {
                App::GetGameContext()->GetSceneMouse().UpdateSimulation();
            }

            // Create snapshot of simulation state for Gfx/GUI updates
            if (App::sim_state->getEnum<SimState>() == SimState::RUNNING ||   // Obviously
                App::sim_state->getEnum<SimState>() == SimState::PAUSED ||    // Avoid dangling (DISPOSED) pointers in simbuffer
                App::sim_state->getEnum<SimState>() == SimState::EDITOR_MODE) // Needed for character movement
            {
                App::GetGfxScene()->BufferSimulationData();
            }

            // Calculate elapsed simulation time (taking simulation speed and pause into account)
            float dt_sim = 0.f;
            if (!world_model_capture_frame &&
                App::sim_state->getEnum<SimState>() == SimState::RUNNING &&
                !App::GetGameContext()->GetActorManager()->IsSimulationPaused())
            {
                dt_sim = dt * App::GetGameContext()->GetActorManager()->GetSimulationSpeed();
            }

            // Advance simulation
            if (!world_model_capture_frame &&
                App::sim_state->getEnum<SimState>() == SimState::RUNNING)
            {
                if (App::GetGameContext()->GetTerrain()->getWater())
                {
                    App::GetGameContext()->GetTerrain()->getWater()->FrameStepWaveField(dt_sim);
                }
                App::GetGameContext()->UpdateActors(); // *** Start new physics tasks. No reading from Actor N/B beyond this point.
            }
            OgreProfileEnd("Simulation");

            // Scene and GUI updates
            OgreProfileBegin("Scene and GUI"); // Adds up to existing profile
            if (App::app_state->getEnum<AppState>() == AppState::MAIN_MENU
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                && !renderer_combined_native_visual_showcase
#endif
                )
            {
                App::GetGuiManager()->DrawMainMenuGui();
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                // MAIN_MENU captures no joined scene, so this GUI would never
                // reach the presenter through the scene snapshot's HUD
                // overlay. Capture it here, at the same point in the tick the
                // SIMULATION branch does, and present it below through the
                // scene-free GUI-only path. The capture is hash-gated and
                // rate-capped, so a static menu costs nothing after the first
                // frame.
                if (renderer_combined_hud_capture != nullptr &&
                    renderer_combined_session != nullptr &&
                    renderer_combined_session->active())
                {
                    const Render::FrontendSurfaceUpdate menu_surface =
                        renderer_combined_presenter.CurrentSurface();
                    if (!menu_surface.suspended)
                    {
                        renderer_combined_hud_capture->CaptureIfDirty(
                            menu_surface.pixel_width,
                            menu_surface.pixel_height);
                    }
                }
#endif
            }
            else if (
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                renderer_combined_native_visual_showcase ||
#endif
                App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                // The forward-native source already owns an immutable complete
                // camera/light/asset scene. Keep the hidden Ogre 14 host in its
                // current application state and submit the native source on
                // every session grant, including while RoR is in MAIN_MENU.
                if (!renderer_combined_native_visual_showcase)
                {
#endif
                // Attribute the hidden OGRE 14 producer separately from the
                // Ogre-Next dispatch below. A frame-time total alone cannot
                // say which of the two co-resident renderers costs what.
                const auto producer_started =
                    std::chrono::high_resolution_clock::now();
                App::GetGfxScene()->UpdateScene(dt_sim); // Draws GUI as well
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                // Capture the just-built menu/HUD overlay between UpdateScene
                // (which drew the GUI) and the joined-scene post below, so
                // the readback rides this exact joined boundary. The cost is
                // attributed to the PRODUCER phase recorded underneath.
                if (renderer_combined_hud_capture != nullptr &&
                    renderer_combined_session != nullptr &&
                    renderer_combined_session->active())
                {
                    // The presenter fails a HUD texture that does not match
                    // the presented drawable extent closed, so the capture
                    // targets that exact pixel extent (not the hidden
                    // producer viewport's logical size).
                    const Render::FrontendSurfaceUpdate hud_surface =
                        renderer_combined_presenter.CurrentSurface();
                    if (!hud_surface.suspended)
                    {
                        renderer_combined_hud_capture->CaptureIfDirty(
                            hud_surface.pixel_width,
                            hud_surface.pixel_height);
                    }
                }
#endif
                if (frame_budget_session != nullptr)
                {
                    frame_budget_session->RecordPhase(
                        FrameTimeBudgetPhase::PRODUCER,
                        std::chrono::duration<double>(
                            std::chrono::high_resolution_clock::now() -
                            producer_started).count());
                }
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                }
#endif
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                // The ordinary path captures the exact completed Ogre 14
                // UpdateScene above. The explicit showcase instead captures
                // its authenticated forward-native owner. Both dispatch into
                // the co-resident N1 frontend without transport or a child.
                if (renderer_combined_simulation_granted &&
                    renderer_combined_scene_source != nullptr &&
                    renderer_combined_session != nullptr &&
                    renderer_combined_session->active())
                {
                    const auto renderer_started =
                        std::chrono::high_resolution_clock::now();
                    const RendererInProcessSessionResult scene_result =
                        renderer_combined_session->PostUpdatedScene(
                            *renderer_combined_scene_source);
                    if (scene_result.scene_snapshot_id > 0U &&
                        scene_result.frontend_frame_id > 0U &&
                        !scene_result.terminal)
                    {
                        renderer_combined_actor_control_qualification.
                            ObserveSceneSubmission(
                                scene_result.frontend_frame_id);
                    }
                    if (scene_result.status ==
                        RendererInProcessSessionStatus::FRAME_COMPLETED)
                    {
                        renderer_combined_actor_control_qualification.
                            ObserveCompletedFrame(
                                scene_result.frontend_frame_id,
                                renderer_combined_presenter.
                                    RetainedSceneAudit());
                    }
                    if (frame_budget_session != nullptr)
                    {
                        // The session reports its own split between CPU scene
                        // conversion and dispatch/GPU completion; recording
                        // its numbers keeps the two attributable separately.
                        frame_budget_session->RecordPhase(
                            FrameTimeBudgetPhase::SCENE_SOURCE_READ,
                            static_cast<double>(
                                scene_result.scene_source_read_ns) / 1.0e9);
                        frame_budget_session->RecordPhase(
                            FrameTimeBudgetPhase::SCENE_SOURCE_VALIDATE,
                            static_cast<double>(
                                scene_result.scene_source_validate_ns) / 1.0e9);
                        frame_budget_session->RecordPhase(
                            FrameTimeBudgetPhase::SCENE_NORMALIZE,
                            static_cast<double>(
                                scene_result.scene_normalize_ns) / 1.0e9);
                        frame_budget_session->RecordPhase(
                            FrameTimeBudgetPhase::SCENE_PRODUCE,
                            static_cast<double>(
                                scene_result.scene_produce_ns) / 1.0e9);
                        frame_budget_session->RecordPhase(
                            FrameTimeBudgetPhase::SCENE_DISPATCH,
                            static_cast<double>(
                                scene_result.scene_dispatch_ns) / 1.0e9);
                        // A deferred native frame is accounted when the next
                        // event pump completes it. Only a synchronously
                        // completed frame is available here. Pending is not a
                        // rejected workload sample, and hidden OGRE 14 draw
                        // counters are never consulted.
                        if (!frame_budget_native_draw_recorded &&
                            scene_result.status ==
                                RendererInProcessSessionStatus::FRAME_COMPLETED)
                        {
                            record_combined_native_budget(
                                scene_result.frontend_frame_id);
                        }
                        (void)renderer_started;
                    }
                    renderer_combined_simulation_granted = false;
                    if (!scene_result &&
                        scene_result.status !=
                            RendererInProcessSessionStatus::
                                PENDING_BACKPRESSURE &&
                        scene_result.status !=
                            RendererInProcessSessionStatus::
                                PENDING_FRONTEND_SURFACE)
                    {
                        // This capture is not reaching the presenter. The
                        // governing invariant of this boundary is that a
                        // per-frame validation may reject a frame or an
                        // object, but may not end a session and may not
                        // permanently stop publication. So the default answer
                        // here is to drop the frame and capture again next
                        // tick: the presenter keeps showing the last scene it
                        // accepted, which is a coherent picture one or more
                        // frames stale rather than a black window or garbage,
                        // and the simulation and event pump keep running so
                        // the window stays responsive and can recover.
                        ++renderer_combined_scene_failure_occurrences;
                        ++renderer_combined_scene_frames_dropped_total;
                        const std::string failure_signature =
                            ToString(scene_result.status) +
                            std::string("\n") +
                            std::to_string(static_cast<unsigned int>(
                                scene_result.frontend_code)) + "\n" +
                            scene_result.validation.field + "\n" +
                            scene_result.validation.detail + "\n" +
                            scene_result.frontend_detail.c_str();
                        if (failure_signature !=
                            renderer_combined_scene_failure_signature)
                        {
                            renderer_combined_scene_failure_signature =
                                failure_signature;
                            // A new signature always reports immediately: the
                            // occurrence counter resets to 1, which the gate
                            // below treats as the start of an episode.
                            renderer_combined_scene_failure_occurrences = 1U;
                        }
                        // Report the first occurrence of an episode, then at
                        // most one line every two seconds, always carrying the
                        // recurrence count. Logging every occurrence floods;
                        // logging once ever -- the historic behaviour -- goes
                        // silent precisely when the failure is persistent
                        // enough to matter, and makes a recurring rejection
                        // indistinguishable from a log that simply stopped.
                        const auto failure_observed_at =
                            std::chrono::steady_clock::now();
                        const bool report_this_occurrence =
                            renderer_combined_scene_failure_occurrences == 1U ||
                            (failure_observed_at -
                             renderer_combined_scene_failure_last_log) >=
                                std::chrono::seconds(2);
                        if (report_this_occurrence)
                        {
                            renderer_combined_scene_failure_last_log =
                                failure_observed_at;
                            LOG(fmt::format(
                                "[RoR|RendererCombined|Scene] Snapshot not "
                                "presented (x{}, {} frames dropped this "
                                "session): status='{}', terminal={}, "
                                "frontend={}, field='{}', detail='{}', "
                                "backend='{}'",
                                renderer_combined_scene_failure_occurrences,
                                renderer_combined_scene_frames_dropped_total,
                                ToString(scene_result.status),
                                scene_result.terminal ? 1 : 0,
                                static_cast<unsigned int>(
                                    scene_result.frontend_code),
                                scene_result.validation.field,
                                scene_result.validation.detail,
                                scene_result.frontend_detail.c_str()));
                        }
                        // A terminal rejection used to end the process
                        // outright, whatever caused it -- one rejected
                        // snapshot killed a live session. Ask the session to
                        // resume publication instead. It grants that only for
                        // causes that committed nothing AND only while its
                        // dispatcher is not itself latched, so a device-class
                        // failure still falls through to the fatal path
                        // below; see
                        // IsRecoverableRendererInProcessSessionTerminalCause.
                        bool renderer_publication_lost = false;
                        if (scene_result.terminal)
                        {
                            const RendererInProcessSessionResult resumed =
                                renderer_combined_session->
                                    RecoverPublication();
                            renderer_publication_lost = !resumed;
                            // Share the bounded schedule above: a terminal
                            // cause that recurs every frame must not flood the
                            // log either. An unrecoverable one is logged
                            // regardless, because it happens exactly once.
                            if (report_this_occurrence ||
                                renderer_publication_lost)
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|Scene] Terminal "
                                    "snapshot failure: cause='{}', "
                                    "publication={} (x{})",
                                    ToString(scene_result.terminal_cause),
                                    renderer_publication_lost
                                        ? "unrecoverable, ending session"
                                        : "resumed, frame dropped",
                                    renderer_combined_scene_failure_occurrences));
                            }
                        }
                        if (renderer_publication_lost)
                        {
                            try
                            {
                                App::GetConsole()->putMessage(
                                    Console::CONSOLE_MSGTYPE_INFO,
                                    Console::CONSOLE_SYSTEM_ERROR,
                                    fmt::format(
                                        _L("Renderer stopped: {} ({}). The "
                                           "session cannot continue."),
                                        ToString(
                                            scene_result.terminal_cause),
                                        scene_result.validation.field));
                            }
                            catch (...)
                            {
                            }
                            App::GetGameContext()->PushMessage(
                                Message(MSG_APP_SHUTDOWN_REQUESTED));
                            const RendererInProcessSessionResult
                                renderer_shutdown =
                                    CloseCombinedRendererSession(
                                        *renderer_combined_session);
                            if (renderer_shutdown.status !=
                                RendererInProcessSessionStatus::CLOSED)
                            {
                                FailStopApplication(EXIT_FAILURE);
                            }
                        }
                        else
                        {
                            // A degraded renderer the user cannot see is its
                            // own trap: the picture silently stops advancing
                            // while the simulation runs on. Announce entering
                            // the degrade, then re-announce on the same
                            // bounded schedule as the log so a persistent one
                            // stays visible instead of scrolling away.
                            // Re-announce no faster than the on-screen message
                            // lifetime, so a persistent degrade stays visible
                            // without the notification area becoming a wall of
                            // identical lines.
                            const bool announce_degrade =
                                !renderer_combined_scene_publication_degraded ||
                                (failure_observed_at -
                                 renderer_combined_scene_failure_last_notice) >=
                                    std::chrono::seconds(10);
                            renderer_combined_scene_publication_degraded = true;
                            if (announce_degrade)
                            {
                                renderer_combined_scene_failure_last_notice =
                                    failure_observed_at;
                                renderer_combined_scene_degrade_notified = true;
                                try
                                {
                                    App::GetConsole()->putMessage(
                                        Console::CONSOLE_MSGTYPE_INFO,
                                        Console::CONSOLE_SYSTEM_WARNING,
                                        fmt::format(
                                            _L("Renderer degraded: showing "
                                               "the last good frame ({} "
                                               "rejected, '{}'). Retrying."),
                                            renderer_combined_scene_failure_occurrences,
                                            scene_result.validation.field));
                                }
                                catch (...)
                                {
                                }
                            }
                        }
                    }
                    else
                    {
                        renderer_combined_scene_failure_signature.clear();
                        // Publication is live again. Closing the degrade out
                        // loud matters as much as opening it: otherwise the
                        // log shows a renderer that failed and never shows it
                        // coming back, and the user is left assuming the
                        // stale picture they were warned about is permanent.
                        if (renderer_combined_scene_publication_degraded)
                        {
                            renderer_combined_scene_publication_degraded =
                                false;
                            ++renderer_combined_scene_publication_recoveries;
                            LOG(fmt::format(
                                "[RoR|RendererCombined|Scene] Publication "
                                "resumed after {} rejected frame(s): "
                                "status='{}' (recovery #{}, {} frames dropped "
                                "this session)",
                                renderer_combined_scene_failure_occurrences,
                                ToString(scene_result.status),
                                renderer_combined_scene_publication_recoveries,
                                renderer_combined_scene_frames_dropped_total));
                            if (renderer_combined_scene_degrade_notified)
                            {
                                try
                                {
                                    // NOTICE, not REPLY: GameChatBox maps
                                    // CONSOLE_SYSTEM_REPLY onto its "commands"
                                    // filter and disables that filter in its
                                    // constructor, so a REPLY reaches the log
                                    // and the console window but is never
                                    // drawn in the in-game notification area
                                    // -- exactly where the user needs to see
                                    // that the renderer came back.
                                    App::GetConsole()->putMessage(
                                        Console::CONSOLE_MSGTYPE_INFO,
                                        Console::CONSOLE_SYSTEM_NOTICE,
                                        fmt::format(
                                            _L("Renderer recovered after {} "
                                               "dropped frame(s)."),
                                            renderer_combined_scene_failure_occurrences));
                                }
                                catch (...)
                                {
                                }
                            }
                            renderer_combined_scene_degrade_notified = false;
                        }
                        renderer_combined_scene_failure_occurrences = 0U;
                        // Retained-section reuse is invisible in the frame
                        // timings alone: identical costs can come from a fast
                        // path or from silent re-adoption churn. These
                        // counters separate the two. A zero produce span
                        // means this poll never reached the producer, so it
                        // must not advance the heartbeat's frame count.
                        if (scene_result.scene_produce_ns != 0U)
                        {
                            const Render::
                                GraphicsSceneSnapshotProduction::Diagnostics&
                                    producer_diagnostics =
                                        scene_result.producer_diagnostics;
                            ++renderer_combined_producer_retained_frames;
                            renderer_combined_producer_retained_adoptions +=
                                producer_diagnostics
                                    .retained_static_adoptions;
                            renderer_combined_producer_retained_fast +=
                                producer_diagnostics
                                    .retained_static_block_reuses;
                            renderer_combined_producer_retained_misses +=
                                producer_diagnostics
                                    .retained_static_precondition_misses;
                            renderer_combined_producer_retained_window +=
                                producer_diagnostics
                                    .retained_static_window_verifications;
                            renderer_combined_producer_retained_scoped +=
                                producer_diagnostics
                                    .scene_asset_compatibility_scoped_validations;
                            renderer_combined_producer_retained_payload_full +=
                                producer_diagnostics
                                    .asset_payload_full_validations;
                            renderer_combined_producer_retained_compat_full +=
                                producer_diagnostics
                                    .scene_asset_compatibility_full_validations;
                            renderer_combined_producer_retained_instances +=
                                producer_diagnostics
                                    .retained_static_instances_reused;
                            if ((renderer_combined_producer_retained_frames %
                                 300U) == 0U)
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|ProducerRetained] "
                                    "schema_version=1 adoptions={} fast={} "
                                    "misses={} window_verified={} "
                                    "scoped_dynamic={} "
                                    "payload_full_validations={} "
                                    "compat_full={} reused_instances={}",
                                    renderer_combined_producer_retained_adoptions,
                                    renderer_combined_producer_retained_fast,
                                    renderer_combined_producer_retained_misses,
                                    renderer_combined_producer_retained_window,
                                    renderer_combined_producer_retained_scoped,
                                    renderer_combined_producer_retained_payload_full,
                                    renderer_combined_producer_retained_compat_full,
                                    renderer_combined_producer_retained_instances));
                            }
                        }
                        if (scene_result.status ==
                            RendererInProcessSessionStatus::FRAME_COMPLETED)
                        {
                            if (renderer_combined_native_showcase_scene_source !=
                                    nullptr &&
                                renderer_combined_native_showcase_scene_source
                                    ->has_committed_capture() &&
                                renderer_combined_native_showcase_scene_source
                                    ->committed_motion_mode() ==
                                    Render::NativeVisualShowcaseMotionMode::
                                        TURN_TABLE)
                            {
                                const std::uint64_t committed_tick =
                                    renderer_combined_native_showcase_scene_source
                                        ->committed_simulation_tick();
                                const std::uint64_t audit_segment =
                                    committed_tick / 90U;
                                if (!renderer_combined_turntable_audit_published ||
                                    audit_segment !=
                                        renderer_combined_turntable_audit_segment)
                                {
                                    LOG(fmt::format(
                                        "[RoR|RendererCombined|"
                                        "NativeShowcase|Turntable] "
                                        "mode='{}' "
                                        "frame={} snapshot={} tick={} "
                                        "angle_degrees={} "
                                        "transform_revision={} "
                                        "selected_object_id={} fixed_hz=60 "
                                        "revolution_ticks={} "
                                        "opaque_motion_only={} "
                                        "refraction={} motion_vectors=false",
                                        renderer_combined_native_showcase_scene_source
                                                    ->profile() ==
                                                Render::NativeVisualShowcaseProfile::
                                                    A1_NATIVE_COURSE
                                            ? "turntable_thin_glass_slab"
                                            : "turntable_opaque_gate",
                                        scene_result.frontend_frame_id,
                                        scene_result.scene_snapshot_id,
                                        committed_tick,
                                        renderer_combined_native_showcase_scene_source
                                            ->committed_turntable_angle_degrees(),
                                        renderer_combined_native_showcase_scene_source
                                            ->committed_gate_transform_revision(),
                                        renderer_combined_native_showcase_scene_source
                                            ->motion_source_object_id(),
                                        Render::
                                            kNativeVisualShowcaseTurntableTicksPerRevolution,
                                        renderer_combined_native_showcase_scene_source
                                                    ->profile() ==
                                                Render::NativeVisualShowcaseProfile::
                                                    A0_LIGHTING_COUPON
                                            ? "true"
                                            : "false",
                                        renderer_combined_native_showcase_scene_source
                                                    ->profile() ==
                                                Render::NativeVisualShowcaseProfile::
                                                    A1_NATIVE_COURSE
                                            ? "thin_parallel_slab_screen_space"
                                            : "false"));
                                    renderer_combined_turntable_audit_published =
                                        true;
                                    renderer_combined_turntable_audit_segment =
                                        audit_segment;
                                }
                            }
                            const RendererContinuousParticleAudit audit =
                                renderer_combined_presenter
                                    .ContinuousParticleAudit();
                            const std::string audit_state_signature =
                                fmt::format(
                                "available={} live_systems={} "
                                "lifetime_max_live_systems={} "
                                "lifetime_max_live_particles={} "
                                "distinct_source_textures={} "
                                "source_alpha_textures={} "
                                "lifetime_max_source_backed_textures={} "
                                "lifetime_max_source_alpha_textures={} "
                                "gpu_readbacks={}",
                                audit.available,
                                audit.live_systems,
                                audit.lifetime_max_live_systems,
                                audit.lifetime_max_live_particles,
                                audit.source_backed_textures,
                                audit.source_alpha_textures,
                                audit.lifetime_max_source_backed_textures,
                                audit.lifetime_max_source_alpha_textures,
                                audit.gpu_readbacks);
                            const bool particle_audit_heartbeat =
                                audit.committed_source_sequence >=
                                    renderer_combined_particle_audit_logged_sequence &&
                                audit.committed_source_sequence -
                                        renderer_combined_particle_audit_logged_sequence >=
                                    300U;
                            if (audit_state_signature !=
                                    renderer_combined_particle_audit_state_signature ||
                                renderer_combined_particle_audit_logged_sequence ==
                                    0U ||
                                particle_audit_heartbeat)
                            {
                                const std::string audit_snapshot = fmt::format(
                                "available={} committed_source_sequence={} "
                                "create_commands={} update_commands={} "
                                "stop_commands={} destroy_commands={} "
                                "live_systems={} live_particles={} "
                                "lifetime_max_live_systems={} "
                                "lifetime_max_live_particles={} "
                                "distinct_source_textures={} "
                                "source_alpha_textures={} "
                                "lifetime_max_source_backed_textures={} "
                                "lifetime_max_source_alpha_textures={} "
                                "gpu_readbacks={} "
                                "native_batch_creates={} "
                                "native_batch_destroys={} "
                                "native_particles_submitted={} "
                                "native_state_readbacks={} "
                                "native_state_verifications={}",
                                audit.available,
                                audit.committed_source_sequence,
                                audit.create_commands,
                                audit.update_commands,
                                audit.stop_commands,
                                audit.destroy_commands,
                                audit.live_systems,
                                audit.live_particles,
                                audit.lifetime_max_live_systems,
                                audit.lifetime_max_live_particles,
                                audit.source_backed_textures,
                                audit.source_alpha_textures,
                                audit.lifetime_max_source_backed_textures,
                                audit.lifetime_max_source_alpha_textures,
                                audit.gpu_readbacks,
                                audit.native_batch_creates,
                                audit.native_batch_destroys,
                                audit.native_particles_submitted,
                                audit.native_state_readbacks,
                                audit.native_state_verifications);
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|"
                                    "ContinuousParticles] {}",
                                    audit_snapshot));
                                renderer_combined_particle_audit_state_signature =
                                    audit_state_signature;
                                renderer_combined_particle_audit_logged_sequence =
                                    audit.committed_source_sequence;
                            }
                            const RendererAnalyticSkyAudit sky_audit =
                                renderer_combined_presenter
                                    .AnalyticSkyAudit();
                            const bool state_verifications_understood =
                                sky_audit.completed_frames > 0U &&
                                sky_audit.native_state_verifications ==
                                    sky_audit.completed_frames;
                            const std::string sky_audit_change_key =
                                fmt::format(
                                    "available={} sun_light_id={} "
                                    "native_ownership_balanced={} "
                                    "expected_per_frame_ownership={} "
                                    "cpu_geometry_digest_verified={} "
                                    "native_geometry_metadata_verified={} "
                                    "production_gpu_readbacks_zero={} "
                                    "exact_native_geometry_readback={} "
                                    "separate_sun_alpha_replace={} "
                                    "state_verifications_understood={}",
                                    sky_audit.available,
                                    sky_audit.sun_light_id,
                                    sky_audit.native_ownership_balanced,
                                    sky_audit.expected_per_frame_ownership,
                                    sky_audit.cpu_geometry_digest_verified,
                                    sky_audit
                                        .native_geometry_metadata_verified,
                                    sky_audit.production_gpu_readbacks_zero,
                                    sky_audit
                                        .exact_native_geometry_readback,
                                    sky_audit.separate_sun_alpha_replace,
                                    state_verifications_understood);
                            if (sky_audit_change_key !=
                                renderer_combined_analytic_sky_audit_signature)
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|AnalyticSky|"
                                    "Native] {} completed_frames={} "
                                    "cpu_geometry_fnv1a64={} "
                                    "native_gpu_content_readbacks={} "
                                    "native_state_verifications={} "
                                    "gpu_readback_scope="
                                    "production_disabled_test_artifact_only",
                                    sky_audit_change_key,
                                    sky_audit.completed_frames,
                                    sky_audit.cpu_geometry_fnv1a64,
                                    sky_audit
                                        .native_gpu_content_readbacks,
                                    sky_audit.native_state_verifications));
                                renderer_combined_analytic_sky_audit_signature =
                                    sky_audit_change_key;
                            }
                            const RendererNativeLightingAudit lighting_audit =
                                renderer_combined_presenter
                                    .NativeLightingAudit();
                            const std::string lighting_audit_state_signature =
                                fmt::format(
                                    "v={} available={} descriptor={} "
                                    "directional={} pbs={} transmission={} "
                                    "normal={} emissive={} casters={} "
                                    "receivers={} lod={}/{}/{}/{}/{}/{} "
                                    "reflection={}/{}/{}/{}/{}/{}/{} "
                                    "lighting={} hdr={} pssm={} gpu={} "
                                    "no_ogre14={}",
                                    lighting_audit.version,
                                    lighting_audit.available,
                                    lighting_audit.material_descriptor_version,
                                    lighting_audit.directional_lights,
                                    lighting_audit.pbs_items,
                                    lighting_audit.transmission_items,
                                    lighting_audit.normal_mapped_items,
                                    lighting_audit.emissive_items,
                                    lighting_audit.shadow_casters,
                                    lighting_audit.shadow_receivers,
                                    lighting_audit.distance_lod_items,
                                    lighting_audit.distance_lod_reduced_items,
                                    lighting_audit.distance_lod_max_selected_level,
                                    lighting_audit.distance_lod_selected_level_sum,
                                    lighting_audit.base_triangles,
                                    lighting_audit.selected_triangles,
                                    lighting_audit.reflection_live_probe_count,
                                    lighting_audit.reflection_successful_capture_count,
                                    lighting_audit.reflection_failed_capture_count,
                                    lighting_audit.reflection_completed_face_count,
                                    lighting_audit.reflection_completed_mip_count,
                                    lighting_audit.reflection_initialized,
                                    lighting_audit.reflection_pbs_bound,
                                    lighting_audit.native_scene_lighting_pass,
                                    lighting_audit.linear_rgba16_hdr_target,
                                    lighting_audit.pssm_shadow_response,
                                    lighting_audit.production_gpu_only,
                                    lighting_audit.no_ogre14_lighting);
                            const bool lighting_audit_heartbeat =
                                lighting_audit.completed_frames >=
                                    renderer_combined_native_lighting_audit_logged_frame &&
                                lighting_audit.completed_frames -
                                        renderer_combined_native_lighting_audit_logged_frame >=
                                    300U;
                            if (lighting_audit_state_signature !=
                                    renderer_combined_native_lighting_audit_state_signature ||
                                renderer_combined_native_lighting_audit_logged_frame ==
                                    0U ||
                                lighting_audit_heartbeat)
                            {
                                const std::string lighting_audit_snapshot =
                                    fmt::format(
                                    "schema_version={} available={} "
                                    "frame={} snapshot={} descriptor_v={} "
                                    "directional={} point={} spot={} "
                                    "forward_clustered={} "
                                    "pbs={} transmission={} normal={} "
                                    "emissive={} casters={} receivers={} "
                                    "lod_items={} lod_reduced={} lod_max={} "
                                    "lod_level_sum={} triangles_base={} "
                                    "triangles_selected={} lod_exact={} "
                                    "hdr_topology={} pssm_populated_finalize={} "
                                    "reflection_audit_v={} reflection_probes={} "
                                    "reflection_captures={} reflection_failures={} "
                                    "reflection_capture_frame={} "
                                    "reflection_capture_tick={} "
                                    "reflection_faces={} reflection_mips={} "
                                    "reflection_probe_resolution={} "
                                    "reflection_blend_resolution={} "
                                    "reflection_native_evidence={} "
                                    "reflection_initialized={} "
                                    "reflection_resources={} reflection_pcc={} "
                                    "reflection_pbs_bound={} "
                                    "reflection_blend_ready={} "
                                    "reflection_ui_free={} "
                                    "reflection_reserved_queue_excluded={} "
                                    "reflection_scene_reset_teardowns={} "
                                    "reflection_scene_reset_retired_probe_count={} "
                                    "native_scene_lighting={} rgba16_hdr={} "
                                    "base_hdr={} sun_full_unoccluded={} "
                                    "sun_direct_hdr={} gpu_sun_derivation={} "
                                    "transactional_sun_toggle={} "
                                    "raster_lit_hdr={} scene_evaluations={} "
                                    "single_history_step={} "
                                    "calibrated_directional={} ambient={} "
                                    "ambient_sh={} ambient_sh_gain={:.4g} "
                                    "ambient_sh_band0_lum={:.4g} "
                                    "probe_sky={} "
                                    "analytic_sky={} emissive_response={} "
                                    "pssm={} thin_slab_refraction={} "
                                    "physical_snell={} beer_lambert={} "
                                    "screen_space_lookup={} "
                                    "refraction_scene_evaluations={} "
                                    "auto_exposure={} "
                                    "gpu_history={} bloom={} filmic={} "
                                    "srgb={} gpu_only={} "
                                    "production_content_readbacks={} "
                                    "production_framebuffer_readbacks={} "
                                    "ogre14_lighting_passes={} "
                                    "no_ogre14_lighting={} "
                                    "native_state_verifications={}",
                                    lighting_audit.version,
                                    lighting_audit.available,
                                    lighting_audit.last_frame_id,
                                    lighting_audit.last_snapshot_id,
                                    lighting_audit.material_descriptor_version,
                                    lighting_audit.directional_lights,
                                    lighting_audit.point_lights,
                                    lighting_audit.spot_lights,
                                    lighting_audit.forward_clustered,
                                    lighting_audit.pbs_items,
                                    lighting_audit.transmission_items,
                                    lighting_audit.normal_mapped_items,
                                    lighting_audit.emissive_items,
                                    lighting_audit.shadow_casters,
                                    lighting_audit.shadow_receivers,
                                    lighting_audit.distance_lod_items,
                                    lighting_audit
                                        .distance_lod_reduced_items,
                                    lighting_audit
                                        .distance_lod_max_selected_level,
                                    lighting_audit
                                        .distance_lod_selected_level_sum,
                                    lighting_audit.base_triangles,
                                    lighting_audit.selected_triangles,
                                    lighting_audit
                                        .exact_native_distance_lod_state,
                                    lighting_audit.hdr_scene_topology,
                                    lighting_audit
                                        .pssm_finalized_with_populated_scene,
                                    lighting_audit
                                        .reflection_probe_audit_version,
                                    lighting_audit.reflection_live_probe_count,
                                    lighting_audit
                                        .reflection_successful_capture_count,
                                    lighting_audit
                                        .reflection_failed_capture_count,
                                    lighting_audit
                                        .reflection_last_capture_frame_id,
                                    lighting_audit
                                        .reflection_last_capture_simulation_tick,
                                    lighting_audit
                                        .reflection_completed_face_count,
                                    lighting_audit
                                        .reflection_completed_mip_count,
                                    lighting_audit.reflection_probe_resolution,
                                    lighting_audit.reflection_blend_resolution,
                                    lighting_audit
                                        .reflection_native_execution_evidence,
                                    lighting_audit.reflection_initialized,
                                    lighting_audit
                                        .reflection_exact_resources_loaded,
                                    lighting_audit.reflection_pcc_enabled,
                                    lighting_audit.reflection_pbs_bound,
                                    lighting_audit
                                        .reflection_blend_texture_ready,
                                    lighting_audit.reflection_ui_free_capture,
                                    lighting_audit
                                        .reflection_reserved_render_queue_excluded,
                                    lighting_audit
                                        .reflection_scene_reset_teardowns,
                                    lighting_audit
                                        .reflection_scene_reset_retired_probe_count,
                                    lighting_audit.native_scene_lighting_pass,
                                    lighting_audit.linear_rgba16_hdr_target,
                                    lighting_audit.separate_base_hdr_target,
                                    lighting_audit
                                        .separate_unoccluded_sun_full_hdr_target,
                                    lighting_audit
                                        .separate_sun_direct_hdr_target,
                                    lighting_audit.gpu_sun_direct_derivation,
                                    lighting_audit
                                        .transactional_directional_sun_toggle,
                                    lighting_audit.raster_lit_hdr_target,
                                    lighting_audit.raster_scene_evaluations,
                                    lighting_audit.single_step_hdr_history,
                                    lighting_audit
                                        .calibrated_directional_lighting,
                                    lighting_audit
                                        .ambient_environment_lighting,
                                    lighting_audit.ambient_sh_bound,
                                    lighting_audit.ambient_sh_gain,
                                    lighting_audit.ambient_sh_band0_luminance,
                                    lighting_audit.probe_sky_admission,
                                    lighting_audit.analytic_sky_contribution,
                                    lighting_audit.emissive_material_response,
                                    lighting_audit.pssm_shadow_response,
                                    lighting_audit
                                        .thin_parallel_slab_refraction,
                                    lighting_audit.physical_snell_refraction,
                                    lighting_audit.beer_lambert_attenuation,
                                    lighting_audit
                                        .screen_space_radiance_lookup,
                                    lighting_audit
                                        .refraction_scene_evaluations,
                                    lighting_audit.hdr_auto_exposure,
                                    lighting_audit.gpu_hdr_history_sequenced,
                                    lighting_audit.hdr_bloom,
                                    lighting_audit.filmic_tone_map,
                                    lighting_audit.srgb_presentation,
                                    lighting_audit.production_gpu_only,
                                    lighting_audit
                                        .production_content_readbacks,
                                    lighting_audit
                                        .production_framebuffer_readbacks,
                                    lighting_audit.ogre14_lighting_passes,
                                    lighting_audit.no_ogre14_lighting,
                                    lighting_audit
                                        .native_state_verifications);
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|NativeLighting] "
                                    "{} completed_frames={}",
                                    lighting_audit_snapshot,
                                    lighting_audit.completed_frames));
                                renderer_combined_native_lighting_audit_state_signature =
                                    lighting_audit_state_signature;
                                renderer_combined_native_lighting_audit_logged_frame =
                                    lighting_audit.completed_frames;
                            }
                            // Aerial perspective evidence. constants_bound_
                            // verified must be 1 on every presented frame: it
                            // is the per-frame _readRawConstants readback, not
                            // a "we tried" flag. enabled=0 means the canonical
                            // identity binding is in force (no sky, or the
                            // validated zero-extinction payload), which the
                            // shader answers with a bit-exact pass-through.
                            const std::string aerial_haze_signature =
                                fmt::format(
                                    "enabled={} "
                                    "node=RoRAerialHazeNodeV1 "
                                    "depth=RoROpaqueDepth "
                                    "depth_export_verified={} "
                                    "node_verified={} "
                                    "constants_bound_verified={} "
                                    "extinction_per_meter={:.9g} "
                                    "inscatter=[{:.9g},{:.9g},{:.9g}]",
                                    lighting_audit.aerial_haze_applied ? 1 : 0,
                                    lighting_audit
                                            .aerial_haze_depth_export_verified
                                        ? 1
                                        : 0,
                                    lighting_audit
                                            .aerial_haze_workspace_verified
                                        ? 1
                                        : 0,
                                    lighting_audit.aerial_haze_constants_bound
                                        ? 1
                                        : 0,
                                    lighting_audit
                                        .aerial_haze_extinction_per_meter,
                                    lighting_audit.aerial_haze_inscatter_r,
                                    lighting_audit.aerial_haze_inscatter_g,
                                    lighting_audit.aerial_haze_inscatter_b);
                            if (aerial_haze_signature !=
                                renderer_combined_aerial_haze_audit_signature)
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|AerialHaze|"
                                    "Runtime] {} completed_frames={}",
                                    aerial_haze_signature,
                                    lighting_audit.completed_frames));
                                renderer_combined_aerial_haze_audit_signature =
                                    aerial_haze_signature;
                            }
                            const RendererRetainedSceneAudit
                                retained_scene_audit =
                                    renderer_combined_presenter
                                        .RetainedSceneAudit();
                            // Throttled: the first diffed frame, every frame
                            // whose diff created or destroyed native state,
                            // and a heartbeat every 300 presented frames.
                            if (retained_scene_audit.available &&
                                retained_scene_audit.frames_diffed != 0U &&
                                retained_scene_audit.frames_diffed !=
                                    renderer_combined_retained_scene_logged_frame &&
                                (renderer_combined_retained_scene_logged_frame ==
                                     0U ||
                                 retained_scene_audit.last_created +
                                         retained_scene_audit.last_destroyed !=
                                     0U ||
                                 retained_scene_audit.last_dynamic_updates !=
                                     0U ||
                                 retained_scene_audit.frames_diffed >=
                                     renderer_combined_retained_scene_logged_frame +
                                         300U))
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|RetainedScene] "
                                    "created={} updated={} destroyed={} "
                                    "dynamic_updates={} "
                                    "dynamic_buffer_updates={} "
                                    "dynamic_mesh_rebuilds={} "
                                    "dynamic_vertex_upload_bytes={} "
                                    "retained={} verified={} retained_proof={} "
                                    "validation_phase_us={} "
                                    "frame_prepare_phase_us={} "
                                    "light_phase_us={} instance_phase_us={} "
                                    "native_prepare_phase_us={} "
                                    "native_render_phase_us={} "
                                    "post_render_phase_us={} "
                                    "cleanup_phase_us={} "
                                    "publication_phase_us={} "
                                    "recovery_teardowns={} "
                                    "retired_light_teardowns={}",
                                    retained_scene_audit.last_created,
                                    retained_scene_audit.last_updated,
                                    retained_scene_audit.last_destroyed,
                                    retained_scene_audit
                                        .last_dynamic_updates,
                                    retained_scene_audit
                                        .last_dynamic_buffer_updates,
                                    retained_scene_audit
                                        .last_dynamic_mesh_rebuilds,
                                    retained_scene_audit
                                        .last_dynamic_vertex_upload_bytes,
                                    retained_scene_audit.retained_instances,
                                    retained_scene_audit.last_verified,
                                    retained_scene_audit
                                        .last_diff_used_retained_block_proof,
                                    retained_scene_audit
                                        .last_validation_phase_microseconds,
                                    retained_scene_audit
                                        .last_frame_prepare_phase_microseconds,
                                    retained_scene_audit
                                        .last_light_phase_microseconds,
                                    retained_scene_audit
                                        .last_instance_phase_microseconds,
                                    retained_scene_audit
                                        .last_native_prepare_phase_microseconds,
                                    retained_scene_audit
                                        .last_native_render_phase_microseconds,
                                    retained_scene_audit
                                        .last_post_render_phase_microseconds,
                                    retained_scene_audit
                                        .last_cleanup_phase_microseconds,
                                    retained_scene_audit
                                        .last_publication_phase_microseconds,
                                    retained_scene_audit.recovery_teardowns,
                                    retained_scene_audit
                                        .retired_light_teardowns));
                                renderer_combined_retained_scene_logged_frame =
                                    retained_scene_audit.frames_diffed;
                            }
                            const RendererRenderBoundaryDegradeAudit
                                render_boundary_degrade_audit =
                                    renderer_combined_presenter
                                        .RenderBoundaryDegradeAudit();
                            const std::uint64_t render_boundary_degrade_total =
                                render_boundary_degrade_audit.total() +
                                scene_result.dispatch_rejected_frames +
                                scene_result
                                    .dispatch_recoverable_frame_failures;
                            if (render_boundary_degrade_audit.available &&
                                render_boundary_degrade_total !=
                                    renderer_combined_render_boundary_degrades_logged)
                            {
                                LOG(fmt::format(
                                    "[RoR|RendererCombined|Degrade] "
                                    "rejected_frames={} "
                                    "recoverable_frame_failures={} "
                                    "post_submit_recoverable_failures={} "
                                    "hud_extent_mismatch_frames={} "
                                    "particle_basis_rejections={} "
                                    "pssm_pose_renormalizations={} "
                                    "non_uniform_scale_instance_rejections={}",
                                    scene_result.dispatch_rejected_frames,
                                    scene_result
                                        .dispatch_recoverable_frame_failures,
                                    render_boundary_degrade_audit
                                        .post_submit_recoverable_failures,
                                    render_boundary_degrade_audit
                                        .hud_extent_mismatch_frames,
                                    render_boundary_degrade_audit
                                        .particle_basis_rejections,
                                    render_boundary_degrade_audit
                                        .pssm_pose_renormalizations,
                                    render_boundary_degrade_audit
                                        .non_uniform_scale_instance_rejections));
                                renderer_combined_render_boundary_degrades_logged =
                                    render_boundary_degrade_total;
                            }
                            const RendererNativeSunVisibilityV2Audit
                                sun_visibility_audit =
                                    renderer_combined_presenter
                                        .NativeSunVisibilityV2Audit();
                            if (sun_visibility_audit.available)
                            {
                                const std::string sun_visibility_signature =
                                    fmt::format(
                                        "schema_version={} frame={} snapshot={} "
                                        "view={} plan={} selected={} admitted={} "
                                        "excluded={} receivers={} casters={} "
                                        "unique_meshes={} blas_build={} "
                                        "blas_hit={} blas_refit={} tlas_build={} "
                                        "tlas_hit={} tlas_refit={} primary_rays={} "
                                        "sun_rays={} visible_texels={} "
                                        "occluded_texels={} gpu_ns={} "
                                        "supports_rt={} apple_family9={} "
                                        "same_ogre_device={} same_ogre_queue={} "
                                        "same_ogre_timeline={} shader_lock={} "
                                        "sun_direct_only={} completed={} "
                                        "cpu_content_readbacks={} "
                                        "gpu_content_readbacks={}",
                                        sun_visibility_audit.version,
                                        sun_visibility_audit.frame_id,
                                        sun_visibility_audit.snapshot_id,
                                        sun_visibility_audit.view_id,
                                        sun_visibility_audit.scene_plan_digest,
                                        sun_visibility_audit.selected_instances,
                                        sun_visibility_audit.admitted_instances,
                                        sun_visibility_audit.excluded_instances,
                                        sun_visibility_audit.receivers,
                                        sun_visibility_audit.casters,
                                        sun_visibility_audit.unique_meshes,
                                        sun_visibility_audit.blas_builds,
                                        sun_visibility_audit.blas_cache_hits,
                                        sun_visibility_audit.blas_refits,
                                        sun_visibility_audit.tlas_builds,
                                        sun_visibility_audit.tlas_cache_hits,
                                        sun_visibility_audit.tlas_refits,
                                        sun_visibility_audit.primary_rays,
                                        sun_visibility_audit
                                            .sun_visibility_rays,
                                        sun_visibility_audit.visible_texels,
                                        sun_visibility_audit.occluded_texels,
                                        sun_visibility_audit
                                            .gpu_execution_nanoseconds,
                                        sun_visibility_audit
                                            .supports_raytracing,
                                        sun_visibility_audit.apple_family_9,
                                        sun_visibility_audit.same_ogre_device,
                                        sun_visibility_audit.same_ogre_queue,
                                        sun_visibility_audit.same_ogre_timeline,
                                        sun_visibility_audit
                                            .shader_lock_verified,
                                        sun_visibility_audit
                                            .sun_direct_only_visibility_modulation,
                                        sun_visibility_audit
                                            .submission_completed,
                                        sun_visibility_audit
                                            .production_cpu_content_readbacks,
                                        sun_visibility_audit
                                            .production_gpu_content_readbacks);
                                if (sun_visibility_signature !=
                                    renderer_combined_native_sun_visibility_audit_signature)
                                {
                                    LOG(fmt::format(
                                        "[RoR|RendererCombined|MetalRT|"
                                        "SunVisibilityV2] {} "
                                        "completed_frames={}",
                                        sun_visibility_signature,
                                        sun_visibility_audit.completed_frames));
                                    renderer_combined_native_sun_visibility_audit_signature =
                                        sun_visibility_signature;
                                }
                            }
                        }
                    }
                }
#else
                // Capture only after UpdateScene has consumed the copied
                // simulation buffers and joined flex/wheel tasks. The source
                // adapter reads the completed OGRE scene, never live solver
                // state. ProductSession retains that one immutable production
                // losslessly while bounded transport backpressure clears.
                if (renderer_bridge_scene_source != nullptr &&
                    renderer_bridge_product_session != nullptr &&
                    renderer_bridge_product_session->active())
                {
                    const RendererOgre14ProductSessionResult scene_result =
                        renderer_bridge_product_session->PostUpdatedScene(
                            *renderer_bridge_scene_source);
                    if (!scene_result &&
                        scene_result.status !=
                            RendererOgre14ProductSessionStatus::
                                PENDING_BACKPRESSURE)
                    {
                        const std::string failure_signature =
                            ToString(scene_result.status) +
                            std::string("\n") +
                            ToString(scene_result.host_status) + "\n" +
                            scene_result.validation.field + "\n" +
                            scene_result.validation.detail;
                        if (failure_signature !=
                            renderer_bridge_scene_failure_signature)
                        {
                            renderer_bridge_scene_failure_signature =
                                failure_signature;
                            LOG(fmt::format(
                                "[RoR|RendererBridge|Scene] Snapshot not "
                                "published: status='{}', host='{}', "
                                "field='{}', detail='{}'",
                                ToString(scene_result.status),
                                ToString(scene_result.host_status),
                                scene_result.validation.field,
                                scene_result.validation.detail));
                        }
                        if (scene_result.terminal)
                        {
                            App::GetGameContext()->PushMessage(
                                Message(MSG_APP_SHUTDOWN_REQUESTED));
                            (void)renderer_bridge_product_session->Shutdown();
                        }
                    }
                    else
                    {
                        renderer_bridge_scene_failure_signature.clear();
                    }
                }
#endif
            }
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
            if (renderer_combined_simulation_granted)
            {
                // Ordinary MAIN_MENU has no valid joined terrain light/camera
                // capture, and an empty scene cannot be rendered: the PSSM
                // admission gate requires exactly one shadow-casting
                // directional light. So this grant is never spent on a scene.
                // It is spent instead on a scene-free GUI-only present, which
                // shows the menu the frame already built. Only when no GUI
                // image exists yet (or its extent no longer matches the
                // presented drawable) does the grant fall back to the historic
                // skip, which manufactures nothing.
                const Render::FrontendSurfaceUpdate ui_surface =
                    renderer_combined_presenter.CurrentSurface();
                const Render::GraphicsSceneHudOverlayInput* const ui_overlay =
                    renderer_combined_hud_capture != nullptr
                        ? renderer_combined_hud_capture->LastPublishedOverlay()
                        : nullptr;
                const bool ui_overlay_presentable =
                    ui_overlay != nullptr && !ui_surface.suspended &&
                    ui_overlay->rgba8_bytes != nullptr &&
                    ui_overlay->content_hash != 0U &&
                    ui_overlay->width == ui_surface.pixel_width &&
                    ui_overlay->height == ui_surface.pixel_height;
                RendererInProcessSessionResult ui_result;
                if (ui_overlay_presentable)
                {
                    Render::UiOverlayFrameRequest ui_request;
                    ui_request.width = ui_overlay->width;
                    ui_request.height = ui_overlay->height;
                    ui_request.content_hash = ui_overlay->content_hash;
                    ui_request.rgba8_bytes = ui_overlay->rgba8_bytes->data();
                    ui_request.rgba8_byte_count =
                        ui_overlay->rgba8_bytes->size();
                    ui_result =
                        renderer_combined_session->PresentUiOverlayFrame(
                            ui_request);
                }
                else
                {
                    // Unchanged historic path, including its fatal handling: a
                    // grant that can be neither spent nor consumed means the
                    // one-shot contract itself broke.
                    ui_result = renderer_combined_session->SkipUpdatedScene();
                    if (!ui_result)
                    {
                        LOG(fmt::format(
                            "[RoR|RendererCombined|Scene] Could not consume "
                            "non-simulation grant: status='{}', field='{}', "
                            "detail='{}'",
                            ToString(ui_result.status),
                            ui_result.validation.field,
                            ui_result.validation.detail));
                        App::GetGameContext()->PushMessage(
                            Message(MSG_APP_SHUTDOWN_REQUESTED));
                    }
                }
                renderer_combined_simulation_granted = false;
                if (ui_result.status ==
                    RendererInProcessSessionStatus::UI_OVERLAY_PRESENTED)
                {
                    ++renderer_combined_ui_overlay_presents;
                    renderer_combined_ui_overlay_failure_signature.clear();
                    if ((renderer_combined_ui_overlay_presents % 300U) == 0U)
                    {
                        const RendererUiOverlayPresentationAudit ui_audit =
                            renderer_combined_presenter
                                .UiOverlayPresentationAudit();
                        LOG(fmt::format(
                            "[RoR|RendererCombined|UiOverlay] schema_version=1 "
                            "presents={} audit_version={} presented_frames={} "
                            "render_one_frame_calls={} image_uploads={} "
                            "image_creates={} image_destroys={} "
                            "workspace_creates={} workspace_destroys={} "
                            "scene_presented_frames={} extent={}x{}",
                            renderer_combined_ui_overlay_presents,
                            ui_audit.version,
                            ui_audit.presented_frames,
                            ui_audit.render_one_frame_calls,
                            ui_audit.image_uploads,
                            ui_audit.image_creates,
                            ui_audit.image_destroys,
                            ui_audit.workspace_creates,
                            ui_audit.workspace_destroys,
                            ui_audit.scene_presented_frames,
                            ui_audit.last_width,
                            ui_audit.last_height));
                    }
                }
                else if (ui_overlay_presentable && !ui_result &&
                         ui_result.status !=
                             RendererInProcessSessionStatus::
                                 PENDING_FRONTEND_SURFACE)
                {
                    const std::string ui_failure_signature =
                        ToString(ui_result.status) + std::string("\n") +
                        std::to_string(static_cast<unsigned int>(
                            ui_result.frontend_code)) + "\n" +
                        ui_result.validation.field + "\n" +
                        ui_result.validation.detail + "\n" +
                        ui_result.frontend_detail.c_str();
                    if (ui_failure_signature !=
                        renderer_combined_ui_overlay_failure_signature)
                    {
                        renderer_combined_ui_overlay_failure_signature =
                            ui_failure_signature;
                        LOG(fmt::format(
                            "[RoR|RendererCombined|UiOverlay] GUI-only frame "
                            "not presented: status='{}', frontend={}, "
                            "field='{}', detail='{}', backend='{}'",
                            ToString(ui_result.status),
                            static_cast<unsigned int>(
                                ui_result.frontend_code),
                            ui_result.validation.field,
                            ui_result.validation.detail,
                            ui_result.frontend_detail.c_str()));
                    }
                    if (ui_result.terminal)
                    {
                        App::GetGameContext()->PushMessage(
                            Message(MSG_APP_SHUTDOWN_REQUESTED));
                        const RendererInProcessSessionResult ui_shutdown =
                            CloseCombinedRendererSession(
                                *renderer_combined_session);
                        if (ui_shutdown.status !=
                            RendererInProcessSessionStatus::CLOSED)
                        {
                            FailStopApplication(EXIT_FAILURE);
                        }
                    }
                }
            }
#endif
            OgreProfileEnd("Scene and GUI");

            OgreProfileEnd("RoR Main Loop");

            // Render!
            Ogre::RenderWindow* render_window = RoR::App::GetAppContext()->GetRenderWindow();
            if (render_window->isClosed())
            {
                App::GetGameContext()->PushMessage(Message(MSG_APP_SHUTDOWN_REQUESTED));
            }
            else if (renderer_runtime_ownership.
                         legacy_frame_presentation_enabled)
            {
                App::GetAppContext()->MaintainPostProcessSceneOrder();
                App::GetAppContext()->GetOgreRoot()->renderOneFrame();
                if (!render_window->isActive() && render_window->isVisible())
                {
                    render_window->update(); // update even when in background !
                }
            } // Render block

            if (!world_model_capture_frame)
                App::GetGuiManager()->ApplyGuiCaptureKeyboard();

            App::GetGuiManager()->UpdateMouseCursorVisibility();

        } // End of main rendering/input loop

        // The budget is finalized before renderer teardown so the receipt
        // describes the measured session and not the shutdown path.
        if (frame_budget_session != nullptr)
        {
            if (!FinalizeFrameTimeBudgetSession(*frame_budget_session))
            {
                application_exit_code = kFrameTimeBudgetFailureExitCode;
            }
            frame_budget_session.reset();
        }
        else if (frame_budget_refused)
        {
            application_exit_code = kFrameTimeBudgetFailureExitCode;
        }

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
        if (renderer_combined_session != nullptr)
        {
            App::GetGuiManager()->LoadingWindow
                .SetCombinedRendererLoadingPump(nullptr, nullptr);
            const RendererInProcessSessionResult renderer_shutdown =
                CloseCombinedRendererSession(*renderer_combined_session);
            LOG(fmt::format(
                "[RoR|RendererCombined|Shutdown] status='{}', pending={}",
                ToString(renderer_shutdown.status),
                renderer_shutdown.pending_frame ? 1 : 0));
            if (renderer_shutdown.status !=
                RendererInProcessSessionStatus::CLOSED)
            {
                // Shutdown timeout/failure may leave OgreNext borrowing the
                // native window. Never unwind hidden Ogre 14 or process SDL
                // ownership underneath that unresolved borrow.
                FailStopApplication(EXIT_FAILURE);
            }
        }
        renderer_combined_session.reset();
        renderer_combined_scene_source.reset();
        renderer_combined_input_target.reset();
#else
        if (renderer_bridge_product_session != nullptr)
        {
            const RendererOgre14ProductSessionResult renderer_shutdown =
                renderer_bridge_product_session->Shutdown();
            LOG(fmt::format(
                "[RoR|RendererBridge|Product] Shutdown: status='{}', "
                "host='{}', pending={}",
                ToString(renderer_shutdown.status),
                ToString(renderer_shutdown.host_status),
                renderer_shutdown.pending_frame ? 1 : 0));
        }
#endif

        App::ShutdownWorldModelCapture();

#ifndef _DEBUG
    }
    catch (Ogre::Exception& e)
    {
        App::ShutdownWorldModelCapture();
        LOG(e.getFullDescription());
        ErrorUtils::ShowError(_L("An exception has occured!"), e.getFullDescription());
    }
    catch (std::runtime_error& e)
    {
        App::ShutdownWorldModelCapture();
        LOG(e.what());
        ErrorUtils::ShowError(_L("An exception (std::runtime_error) has occured!"), e.what());
    }
#endif

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    // The release-only exception handlers above bypass the normal loop tail.
    // Prove frontend closure here as well before local guards can unwind.
    if (renderer_combined_session != nullptr)
    {
        App::GetGuiManager()->LoadingWindow
            .SetCombinedRendererLoadingPump(nullptr, nullptr);
        const RendererInProcessSessionResult renderer_shutdown =
            CloseCombinedRendererSession(*renderer_combined_session);
        if (renderer_shutdown.status !=
            RendererInProcessSessionStatus::CLOSED)
        {
            FailStopApplication(EXIT_FAILURE);
        }
        renderer_combined_session.reset();
        renderer_combined_scene_source.reset();
        renderer_combined_input_target.reset();
    }
#endif

    }
    catch (const ApplicationFatalError& fatal)
    {
        // Preserve the selected fatal result before any cleanup diagnostics.
        try
        {
            LOG(fmt::format(
                "[RoR|Fatal] Controlled shutdown requested: code={}, "
                "reason='{}'",
                fatal.exit_code(),
                fatal.what()));
        }
        catch (...)
        {
        }

        const ApplicationFatalShutdownDisposition shutdown_disposition =
            RunApplicationFatalShutdownSequence(
                []() {
                    App::ShutdownWorldModelCapture();
                    return true;
                },
                [&]() {
#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
                    if (renderer_combined_session != nullptr)
                    {
                        App::GetGuiManager()->LoadingWindow
                            .SetCombinedRendererLoadingPump(nullptr, nullptr);
                        const RendererInProcessSessionResult
                            renderer_shutdown =
                                CloseCombinedRendererSession(
                                    *renderer_combined_session);
                        if (renderer_shutdown.status !=
                            RendererInProcessSessionStatus::CLOSED)
                        {
                            // Leave every owner intact; the fatal coordinator
                            // will select fail-stop before renderer unwinding.
                            return false;
                        }
                    }
                    renderer_combined_session.reset();
                    renderer_combined_scene_source.reset();
                    renderer_combined_input_target.reset();
#else
                    if (renderer_bridge_product_session != nullptr)
                    {
                        (void)renderer_bridge_product_session->Shutdown();
                    }
                    renderer_bridge_product_session.reset();
                    renderer_bridge_scene_source.reset();
                    renderer_bridge_input_target.reset();
#endif
                    return true;
                },
                [&]() {
                    return worker_runtime_guard.Release();
                },
                [&]() {
                    return fatal_scene_runtime_gate.Release();
                });
        if (shutdown_disposition ==
            ApplicationFatalShutdownDisposition::FAIL_STOP)
        {
            try
            {
                LOG("[RoR|Fatal] Presentation/worker/scene release was not "
                    "proven; preserving renderer and archive owners until "
                    "process fail-stop");
            }
            catch (...)
            {
            }
            FailStopApplication(fatal.exit_code());
        }
        application_exit_code = fatal.exit_code();
    }

    return application_exit_code;
}

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
INT WINAPI WinMain( HINSTANCE hInst, HINSTANCE, LPSTR strCmdLine, INT )
{
    return main(__argc, __argv);
}
#endif

#ifdef __cplusplus
}
#endif
