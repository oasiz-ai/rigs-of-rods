/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelLiveCaptureController.h"

#include "Actor.h"
#include "ActorManager.h"
#include "Application.h"
#include "EpisodeCaptureSink.h"
#include "EpisodeFormat.h"
#include "EpisodeProvenance.h"
#include "GameContext.h"
#include "RoRRuntimeCaptureProvider.h"
#include "RoRVersion.h"
#include "ScriptEngine.h"
#include "Terrain.h"
#include "WorldModelCaptureContract.h"
#include "WorldModelLiveCaptureConfig.h"
#include "WorldModelTelemetry.h"

#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreRoot.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#   include <sys/utsname.h>
#else
#   include <sys/utsname.h>
#   include <unistd.h>
#endif

namespace {

using RoR::WorldModel::CaptureConfig;
using RoR::WorldModel::EpisodeCaptureSink;
using RoR::WorldModel::EpisodeId;
using RoR::WorldModel::EpisodeProvenance;
using RoR::WorldModel::Hash256;
using RoR::WorldModel::LiveCaptureActivationConfig;
using RoR::WorldModel::RoRRuntimeCaptureConfig;
using RoR::WorldModel::RoRRuntimeCaptureDescriptor;
using RoR::WorldModel::RoRRuntimeResourceIdentity;
using RoR::WorldModel::RoRWorldModelRuntime;

const char LOG_PREFIX[] = "[RoR|WorldModel|LiveCapture] ";

std::string CanonicalIdentifier(
    const std::string& prefix,
    const std::string& input)
{
    std::string output = prefix;
    bool prior_separator =
        !output.empty() &&
        !std::isalnum(static_cast<unsigned char>(output.back()));
    for (const unsigned char byte : input)
    {
        char character = static_cast<char>(std::tolower(byte));
        const bool alnum =
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
        if (alnum)
        {
            output.push_back(character);
            prior_separator = false;
        }
        else if (!output.empty() && !prior_separator)
        {
            output.push_back('-');
            prior_separator = true;
        }
        if (output.size() == 128U)
            break;
    }
    while (!output.empty() &&
           !std::isalnum(
               static_cast<unsigned char>(output.back())))
    {
        output.pop_back();
    }
    return output;
}

bool InspectPlatformId(
    std::string& platform_id,
    std::string* error)
{
#if defined(_WIN32)
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    SYSTEM_INFO system = {};
    GetNativeSystemInfo(&system);
    if (!GetVersionExW(&version))
    {
        if (error != nullptr)
            *error = "GetVersionExW failed";
        return false;
    }
    platform_id = CanonicalIdentifier(
        "platform/",
        "windows-" +
            std::to_string(version.dwMajorVersion) + "-" +
            std::to_string(version.dwMinorVersion) + "-" +
            std::to_string(version.dwBuildNumber) + "-arch-" +
            std::to_string(system.wProcessorArchitecture));
#else
    struct utsname identity = {};
    if (uname(&identity) != 0)
    {
        if (error != nullptr)
            *error = "uname failed";
        return false;
    }
    platform_id = CanonicalIdentifier(
        "platform/",
        std::string(identity.sysname) + "-" +
            identity.release + "-" + identity.machine);
#endif
    if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(platform_id))
    {
        if (error != nullptr)
            *error = "runtime platform identity is not canonical";
        return false;
    }
    return true;
}

bool CurrentExecutablePath(
    std::filesystem::path& path,
    std::string* error)
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768U);
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0U || length >= buffer.size())
    {
        if (error != nullptr)
            *error = "GetModuleFileNameW failed";
        return false;
    }
    path = std::filesystem::path(
        std::wstring(buffer.data(), length));
#elif defined(__APPLE__)
    std::uint32_t size = 0U;
    if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0U)
    {
        if (error != nullptr)
            *error = "_NSGetExecutablePath size query failed";
        return false;
    }
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        if (error != nullptr)
            *error = "_NSGetExecutablePath failed";
        return false;
    }
    std::error_code fs_error;
    path = std::filesystem::weakly_canonical(
        std::filesystem::path(buffer.data()),
        fs_error);
    if (fs_error)
    {
        if (error != nullptr)
            *error =
                "executable path canonicalization failed: " +
                fs_error.message();
        return false;
    }
#else
    std::array<char, 4096U> buffer = {};
    const ssize_t length = readlink(
        "/proc/self/exe",
        buffer.data(),
        buffer.size() - 1U);
    if (length <= 0 ||
        static_cast<std::size_t>(length) >= buffer.size())
    {
        if (error != nullptr)
            *error = "readlink(/proc/self/exe) failed";
        return false;
    }
    buffer[static_cast<std::size_t>(length)] = '\0';
    path = std::filesystem::path(buffer.data());
#endif
    return !path.empty();
}

bool InspectBuildIdentity(
    std::string& build_sha256,
    std::string* error)
{
    std::filesystem::path executable;
    if (!CurrentExecutablePath(executable, error))
        return false;
    Hash256 hash;
    if (!RoR::WorldModel::ComputeFileSha256(
            executable,
            hash,
            error))
    {
        return false;
    }
    build_sha256 = hash.ToHex();
    return RoR::WorldModel::IsCanonicalSha256(build_sha256) &&
        build_sha256.find_first_not_of('0') != std::string::npos;
}

bool InspectRendererIdentity(
    std::string& gpu_id,
    std::string& driver_id,
    std::string* error)
{
    Ogre::Root* root = RoR::App::GetAppContext()->GetOgreRoot();
    Ogre::RenderSystem* render =
        root != nullptr ? root->getRenderSystem() : nullptr;
    const Ogre::RenderSystemCapabilities* capabilities =
        render != nullptr ? render->getCapabilities() : nullptr;
    if (render == nullptr || capabilities == nullptr ||
        capabilities->getDeviceName().empty())
    {
        if (error != nullptr)
            *error = "renderer device identity is unavailable";
        return false;
    }
    const Ogre::DriverVersion& version =
        render->getDriverVersion();
    if (version.major == 0 && version.minor == 0 &&
        version.release == 0 && version.build == 0)
    {
        if (error != nullptr)
            *error = "renderer driver version is unavailable";
        return false;
    }
    gpu_id = CanonicalIdentifier(
        "gpu/",
        capabilities->getDeviceName());
    driver_id = CanonicalIdentifier(
        "driver/",
        render->getName() + "-" +
            version.toString());
    if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(gpu_id) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(driver_id))
    {
        if (error != nullptr)
            *error = "renderer identity is not canonical";
        return false;
    }
    return true;
}

bool ParseActivationConfig(
    LiveCaptureActivationConfig& config,
    std::string* error)
{
    if (!RoR::WorldModel::ParseCanonicalU64(
            RoR::App::wm_capture_root_seed->getStr(),
            config.root_seed) ||
        !RoR::WorldModel::ParseCanonicalU64(
            RoR::App::wm_capture_episode_ordinal->getStr(),
            config.episode_ordinal) ||
        !RoR::WorldModel::ParseCanonicalU64(
            RoR::App::wm_capture_transition_count->getStr(),
            config.transition_count))
    {
        if (error != nullptr)
            *error = "seed/ordinal/transition count is not canonical uint64";
        return false;
    }
    const int width = RoR::App::wm_capture_rgb_width->getInt();
    const int height = RoR::App::wm_capture_rgb_height->getInt();
    if (width <= 0 || height <= 0)
    {
        if (error != nullptr)
            *error = "RGB width and height must be positive";
        return false;
    }
    config.output_root =
        RoR::App::wm_capture_output_root->getStr();
    config.rgb_width = static_cast<std::uint32_t>(width);
    config.rgb_height = static_cast<std::uint32_t>(height);
    config.rights_manifest_sha256 =
        RoR::App::wm_capture_rights_manifest_sha256->getStr();
    config.rights_manifest_path =
        RoR::App::wm_capture_rights_manifest_path->getStr();
    config.data_source_id =
        RoR::App::wm_capture_data_source_id->getStr();
    config.participant_release_id =
        RoR::App::wm_capture_participant_release_id->getStr();
    config.allowed_use_id =
        RoR::App::wm_capture_allowed_use_id->getStr();
    return RoR::WorldModel::ValidateLiveCaptureActivationConfig(
        config,
        error);
}

std::string CaptureErrorDetail(
    const RoR::WorldModel::CaptureSession& session,
    const EpisodeCaptureSink& sink)
{
    const RoR::WorldModel::CaptureStatus& status =
        session.GetStatus();
    std::ostringstream stream;
    stream
        << "capture_error=" << static_cast<std::uint32_t>(status.error)
        << ", transition=" << status.transition_index
        << ", expected_step=" << status.expected_physics_step
        << ", actual_step=" << status.actual_physics_step;
    if (!sink.GetLastError().empty())
        stream << ", sink='" << sink.GetLastError() << '\'';
    return stream.str();
}

} // namespace

namespace RoR {
namespace WorldModel {

class LiveCaptureController::Impl
{
public:
    enum class State
    {
        IDLE,
        WAITING_FOR_FRESH_SCENE,
        ACTIVE
    };

    bool IsActive() const
    {
        return state == State::ACTIVE &&
            runtime != nullptr &&
            sink != nullptr &&
            session != nullptr;
    }

    void DisableAfterFailure(const std::string& reason)
    {
        LOG(std::string(LOG_PREFIX) + "refused: " + reason);
        App::wm_capture_enabled->setVal(false);
        state = State::IDLE;
        last_error = reason;
    }

    bool EnvironmentReady(bool& wait, std::string* error)
    {
        wait = false;
        if (App::app_state->getEnum<AppState>() !=
                AppState::SIMULATION ||
            App::sim_state->getEnum<SimState>() != SimState::RUNNING ||
            App::GetGameContext() == nullptr ||
            App::GetGameContext()->GetTerrain() == nullptr ||
            App::GetGameContext()->GetActorManager() == nullptr)
        {
            wait = true;
            return false;
        }

        ActorManager* manager =
            App::GetGameContext()->GetActorManager();
        manager->SyncWithSimThread();
        if (manager->GetCompletedPhysicsSteps() != 0U)
        {
            if (error != nullptr)
                *error =
                    "capture requires a fresh scene at global physics step 0";
            return false;
        }
        const ActorPtrVec& actors = manager->GetActors();
        ActorPtr player = App::GetGameContext()->GetPlayerActor();
        if (actors.empty() || player == nullptr)
        {
            wait = true;
            return false;
        }
        if (actors.size() != 1U || actors.front() != player ||
            player->ar_state != ActorState::LOCAL_SIMULATED ||
            player->ar_driveable != TRUCK ||
            player->ar_engine == nullptr)
        {
            if (error != nullptr)
                *error =
                    "capture requires exactly one powered local player truck";
            return false;
        }
        if (!player->ar_linked_actors.empty() ||
            !manager->inter_actor_links.empty() ||
            !manager->GetFreeForces().empty())
        {
            if (error != nullptr)
                *error =
                    "linked actors and dynamic free forces are unsupported";
            return false;
        }
        if (App::GetGameContext()->GetTerrain()->getWater() != nullptr ||
            App::gfx_water_waves->getBool())
        {
            if (error != nullptr)
                *error = "water and wavefield simulation are unsupported";
            return false;
        }
        if (App::gfx_sky_time_cycle->getBool())
        {
            if (error != nullptr)
                *error = "dynamic sky time is unsupported";
            return false;
        }
        if (App::io_arcade_controls->getBool())
        {
            if (error != nullptr)
                *error = "arcade control remapping is unsupported";
            return false;
        }
        if (App::sim_gearbox_mode->getEnum<SimGearboxMode>() !=
            SimGearboxMode::AUTO)
        {
            if (error != nullptr)
                *error =
                    "schema 1 requires automatic gearbox mode";
            return false;
        }
        if (App::mp_state->getEnum<MpState>() != MpState::DISABLED)
        {
            if (error != nullptr)
                *error = "multiplayer capture is unsupported";
            return false;
        }
#ifdef USE_ANGELSCRIPT
        if (App::GetScriptEngine() != nullptr &&
            !App::GetScriptEngine()->getScriptUnits().empty())
        {
            if (error != nullptr)
                *error = "loaded scripts are unsupported";
            return false;
        }
#endif
        return true;
    }

    bool Start(const LiveCaptureActivationConfig& activation)
    {
        Hash256 measured_rights_hash;
        std::string error;
        if (!ComputeFileSha256(
                activation.rights_manifest_path,
                measured_rights_hash,
                &error))
        {
            DisableAfterFailure(
                "rights manifest file/hash verification failed: " +
                error);
            return false;
        }
        if (measured_rights_hash.ToHex() !=
            activation.rights_manifest_sha256)
        {
            DisableAfterFailure(
                "rights manifest file/hash verification failed: "
                "expected=" +
                activation.rights_manifest_sha256 +
                ", measured=" + measured_rights_hash.ToHex());
            return false;
        }

        ActorManager* manager =
            App::GetGameContext()->GetActorManager();
        ActorPtr player = App::GetGameContext()->GetPlayerActor();
        const EpisodeId episode = DeriveEpisodeId(
            activation.root_seed,
            activation.episode_ordinal);
        const std::uint64_t reset_seed = DeriveSeed(
            activation.root_seed,
            SeedDomain::RESET,
            episode,
            0U);
        if (reset_seed == 0U ||
            !player->PrepareWorldModelCaptureReset(reset_seed) ||
            player->GetWorldModelDeterministicSeed() != reset_seed)
        {
            DisableAfterFailure(
                "player reset/seed installation failed");
            return false;
        }
        player->ar_arcade_controls = false;
        player->ar_hydro_speed_coupling_enabled =
            App::io_hydro_coupling->getBool();
        player->ar_engine->setAutoMode(SimGearboxMode::AUTO);
        manager->SyncWithSimThread();
        if (manager->GetCompletedPhysicsSteps() != 0U)
        {
            DisableAfterFailure(
                "physics advanced while sealing reset");
            return false;
        }

        RoRRuntimeResourceIdentity resources;
        if (!InspectCurrentRoRRuntimeResourceIdentity(
                resources,
                &error))
        {
            DisableAfterFailure(
                "loaded resource identity failed: " + error);
            return false;
        }

        RoRRuntimeCaptureConfig runtime_config;
        runtime_config.target_id = resources.target_id;
        runtime_config.vehicle_sha256 = resources.vehicle_sha256;
        runtime_config.world_id =
            "ror.world/" + resources.terrain_sha256;
        runtime_config.terrain_id = resources.terrain_id;
        runtime_config.terrain_sha256 = resources.terrain_sha256;
        runtime_config.weather_id = "ror.weather/static";
        runtime_config.camera_id = "driver/main";
        runtime_config.coordinate_frame = "ror.world.rh-y-up";
        runtime_config.control_ids = LiveCaptureControlIds();
        runtime_config.state_digest_scenario_id = reset_seed;
        runtime_config.rgb_width = activation.rgb_width;
        runtime_config.rgb_height = activation.rgb_height;

        runtime = CreateCurrentRoRWorldModelRuntime(
            runtime_config,
            &error);
        if (runtime == nullptr)
        {
            DisableAfterFailure(
                "runtime creation failed: " + error);
            return false;
        }

        RoRRuntimeCaptureDescriptor descriptor;
        if (!runtime->GetProvider().InspectCaptureDescriptor(
                descriptor,
                &error))
        {
            DisableAfterFailure(
                "provider descriptor inspection failed: " +
                error);
            runtime.reset();
            return false;
        }
        if (descriptor.control_ids != runtime_config.control_ids)
        {
            DisableAfterFailure(
                "provider descriptor control policy differs from "
                "the requested schema-1 policy");
            runtime.reset();
            return false;
        }

        std::string reset_state_sha256;
        if (!runtime->GetProvider().CaptureCurrentStateSha256(
                0U,
                reset_state_sha256))
        {
            DisableAfterFailure(
                "joined reset-state digest failed");
            runtime.reset();
            return false;
        }

        EpisodeProvenance provenance;
        provenance.root_seed = activation.root_seed;
        provenance.reset_seed = reset_seed;
        provenance.engine_commit = ROR_GIT_COMMIT;
        provenance.engine_branch = CanonicalIdentifier(
            "branch/",
            ROR_GIT_BRANCH);
        provenance.build_id = CanonicalIdentifier(
            "ror/",
            ROR_VERSION_STRING);
        if (!InspectBuildIdentity(provenance.build_sha256, &error))
        {
            DisableAfterFailure(
                "build identity failed: " + error);
            runtime.reset();
            return false;
        }
        if (!InspectPlatformId(provenance.os_id, &error))
        {
            DisableAfterFailure(
                "platform identity failed: " + error);
            runtime.reset();
            return false;
        }
        if (!InspectRendererIdentity(
                provenance.gpu_id,
                provenance.driver_id,
                &error))
        {
            DisableAfterFailure(
                "renderer identity failed: " + error);
            runtime.reset();
            return false;
        }
        std::string canonical_config =
            CanonicalLiveCaptureConfig(activation);
        canonical_config +=
            "arcade_controls=" +
            std::to_string(App::io_arcade_controls->getBool() ? 1 : 0) +
            "\n";
        canonical_config +=
            "hydro_speed_coupling=" +
            std::to_string(App::io_hydro_coupling->getBool() ? 1 : 0) +
            "\n";
        canonical_config +=
            "gearbox_mode=" +
            std::to_string(App::sim_gearbox_mode->getInt()) +
            "\n";
        canonical_config +=
            "analog_smoothing=" +
            App::io_analog_smoothing->getStr() +
            "\n";
        canonical_config +=
            "analog_sensitivity=" +
            App::io_analog_sensitivity->getStr() +
            "\n";
        provenance.config_sha256 = ComputeSha256(
            canonical_config.data(),
            canonical_config.size()).ToHex();
        provenance.vehicle_id = resources.target_id;
        provenance.vehicle_sha256 = resources.vehicle_sha256;
        provenance.terrain_id = resources.terrain_id;
        provenance.terrain_sha256 = resources.terrain_sha256;
        provenance.controller_profile_id =
            descriptor.controller_profile_id;
        provenance.controller_profile_sha256 =
            descriptor.controller_profile_sha256;
        provenance.control_ids = descriptor.control_ids;
        provenance.camera_profile_id =
            descriptor.camera_profile_id;
        provenance.camera_profile_sha256 =
            descriptor.camera_profile_sha256;
        provenance.reset_state_sha256 = reset_state_sha256;
        provenance.rights_manifest_sha256 =
            activation.rights_manifest_sha256;
        provenance.data_source_id = activation.data_source_id;
        provenance.participant_release_id =
            activation.participant_release_id;
        provenance.allowed_use_id = activation.allowed_use_id;
        provenance.matrix_order = "row-major";
        provenance.coordinate_frame = "ror.world.rh-y-up";
        provenance.color_space = "srgb";
        provenance.pixel_format = "rgb8";
        if (!ValidateEpisodeProvenance(provenance, &error))
        {
            DisableAfterFailure(
                "provenance validation failed: " + error);
            runtime.reset();
            return false;
        }

        CaptureConfig capture_config;
        capture_config.episode = episode;
        capture_config.target_id = resources.target_id;
        capture_config.origin_completed_physics_steps = 0U;
        capture_config.maximum_transitions =
            activation.transition_count;
        capture_config.provenance = provenance;

        sink.reset(new EpisodeCaptureSink(activation.output_root));
        session.reset(new CaptureSession(runtime->GetBackend(), *sink));
        if (!session->Begin(capture_config))
        {
            const std::string detail =
                CaptureErrorDetail(*session, *sink);
            sink->AbortEpisode();
            session.reset();
            sink.reset();
            runtime.reset();
            DisableAfterFailure("episode begin failed: " + detail);
            return false;
        }

        state = State::ACTIVE;
        last_error.clear();
        LOG(std::string(LOG_PREFIX) +
            "started episode partial='" +
            sink->GetPartialDirectory().string() +
            "', transitions=" +
            std::to_string(activation.transition_count));
        return true;
    }

    void Abort(const std::string& reason)
    {
        if (!IsActive())
            return;
        const std::filesystem::path partial =
            sink->GetPartialDirectory();
        sink->AbortEpisode();
        session.reset();
        sink.reset();
        runtime.reset();
        state = State::IDLE;
        App::wm_capture_enabled->setVal(false);
        last_error = reason;
        LOG(std::string(LOG_PREFIX) +
            "aborted: " + reason +
            "; incomplete evidence remains partial='" +
            partial.string() + "'");
    }

    State state = State::IDLE;
    std::string last_error;
    std::unique_ptr<RoRWorldModelRuntime> runtime;
    std::unique_ptr<EpisodeCaptureSink> sink;
    std::unique_ptr<CaptureSession> session;
};

LiveCaptureController::LiveCaptureController():
    m_impl(new Impl())
{
}

LiveCaptureController::~LiveCaptureController()
{
    if (m_impl != nullptr && m_impl->IsActive())
        m_impl->Abort("controller destruction");
}

void LiveCaptureController::UpdateRequestedState()
{
    if (m_impl == nullptr)
        return;
    if (!App::wm_capture_enabled->getBool())
    {
        if (m_impl->IsActive())
            m_impl->Abort("wm_capture_enabled was turned off");
        else
            m_impl->state = Impl::State::IDLE;
        return;
    }
    if (m_impl->IsActive())
        return;

    LiveCaptureActivationConfig config;
    std::string error;
    if (!ParseActivationConfig(config, &error))
    {
        m_impl->DisableAfterFailure(error);
        return;
    }
    bool wait = false;
    if (!m_impl->EnvironmentReady(wait, &error))
    {
        if (wait)
        {
            m_impl->state = Impl::State::WAITING_FOR_FRESH_SCENE;
            return;
        }
        m_impl->DisableAfterFailure(error);
        return;
    }
    m_impl->Start(config);
}

bool LiveCaptureController::IsActive() const
{
    return m_impl != nullptr && m_impl->IsActive();
}

bool LiveCaptureController::OwnsSimulationLoop() const
{
    return IsActive();
}

bool LiveCaptureController::CaptureControlledFrame()
{
    if (!IsActive())
        return false;
    if (!App::wm_capture_enabled->getBool())
    {
        m_impl->Abort("wm_capture_enabled was turned off");
        return false;
    }
    if (!m_impl->session->CaptureNext())
    {
        const std::string detail =
            CaptureErrorDetail(*m_impl->session, *m_impl->sink);
        m_impl->Abort("transition capture failed: " + detail);
        return false;
    }
    if (m_impl->session->GetCapturedTransitionCount() <
        m_impl->session->GetConfig().maximum_transitions)
    {
        return true;
    }
    if (!m_impl->session->Complete())
    {
        const std::string detail =
            CaptureErrorDetail(*m_impl->session, *m_impl->sink);
        m_impl->Abort("episode completion failed: " + detail);
        return false;
    }
    const std::filesystem::path final_path =
        m_impl->sink->GetFinalDirectory();
    if (!m_impl->sink->IsComplete())
    {
        m_impl->Abort(
            "sink did not report COMPLETE after finalization");
        return false;
    }
    m_impl->session.reset();
    m_impl->sink.reset();
    m_impl->runtime.reset();
    m_impl->state = Impl::State::IDLE;
    App::wm_capture_enabled->setVal(false);
    LOG(std::string(LOG_PREFIX) +
        "complete episode='" + final_path.string() + "'");
    return true;
}

void LiveCaptureController::Abort(const std::string& reason)
{
    if (m_impl != nullptr)
        m_impl->Abort(reason);
}

void LiveCaptureController::Shutdown()
{
    Abort("application shutdown");
}

} // namespace WorldModel
} // namespace RoR

namespace RoR {
namespace App {

namespace {
WorldModel::LiveCaptureController& WorldModelCaptureController()
{
    static WorldModel::LiveCaptureController controller;
    return controller;
}
} // namespace

void UpdateWorldModelCaptureRequest()
{
    WorldModelCaptureController().UpdateRequestedState();
}

bool IsWorldModelCaptureActive()
{
    return WorldModelCaptureController().IsActive();
}

bool WorldModelCaptureOwnsSimulationLoop()
{
    return WorldModelCaptureController().OwnsSimulationLoop();
}

bool CaptureWorldModelControlledFrame()
{
    return WorldModelCaptureController().CaptureControlledFrame();
}

void AbortWorldModelCapture(const std::string& reason)
{
    WorldModelCaptureController().Abort(reason);
}

void ShutdownWorldModelCapture()
{
    WorldModelCaptureController().Shutdown();
}

bool WorldModelCaptureMessageRequiresAbort(MsgType type)
{
    switch (type)
    {
    case MSG_APP_SCREENSHOT_REQUESTED:
    case MSG_APP_SCRIPT_THREAD_STATUS:
    case MSG_GUI_MP_CLIENTS_REFRESH:
    case MSG_GUI_SHOW_MESSAGE_BOX_REQUESTED:
    case MSG_GUI_HIDE_MESSAGE_BOX_REQUESTED:
    case MSG_GUI_SHOW_CHATBOX_REQUESTED:
        return false;
    default:
        return true;
    }
}

} // namespace App
} // namespace RoR
