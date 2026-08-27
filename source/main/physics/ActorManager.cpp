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
/// @author Thomas Fischer (thomas{AT}thomasfischer{DOT}biz)
/// @date   24th of August 2009

#include "ActorManager.h"

#include "Actor.h"
#include "Application.h"
#include "ApproxMath.h"
#include "BeamAxialResponse.h"
#include "Buoyance.h"
#include "CacheSystem.h"
#include "ContentManager.h"
#include "ChatSystem.h"
#include "Collisions.h"
#include "DashBoardManager.h"
#include "DeterministicContactOrder.h"
#include "DeterministicInputContinuationSavegame.h"
#include "DeterministicInputTraceRuntime.h"
#include "DeterministicScenarioSchedule.h"
#include "DeterministicScenarioIdentity.h"
#include "DeterministicStateTrace.h"
#include "DeterministicVehicleInput.h"
#include "DeterministicVehicleInputActorAdapter.h"
#include "ActorStateDigestAdapter.h"
#include "DynamicCollisions.h"
#include "Engine.h"
#include "GameContext.h"
#include "GfxScene.h"
#include "GUIManager.h"
#include "Console.h"
#include "GUI_TopMenubar.h"
#include "InputEngine.h"
#include "beamng/JBeamVehicleImporter.h"
#include "Language.h"
#include "MovableText.h"
#include "Network.h"
#include "PointColDetector.h"
#include "PlatformUtils.h"
#include "Replay.h"
#include "RigDef_Validator.h"
#include "RigDef_Serializer.h"
#include "ActorSpawner.h"
#include "ScriptEngine.h"
#include "SoundScriptManager.h"
#include "Terrain.h"
#include "ThreadPool.h"
#include "TuneupFileFormat.h"
#include "Utils.h"
#include "VehicleAI.h"

#include <fmt/format.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Ogre;
using namespace RoR;

const ActorPtr ActorManager::ACTORPTR_NULL; // Dummy value to be returned as const reference.

namespace RoR {

struct InterActorContactBufferPool
{
    typedef DeterministicContactOrder::BoundedTaskBuffer<
        InterActorCollisionContact> ContactTaskBuffer;

    void Prepare(std::size_t task_count)
    {
        if (m_buffers.size() < task_count)
        {
            m_buffers.resize(task_count);
        }

        const std::size_t base_quota =
            task_count == 0
                ? 0
                : DeterministicContactOrder::
                    INTER_ACTOR_CONTACT_BUDGET / task_count;
        const std::size_t remainder =
            task_count == 0
                ? 0
                : DeterministicContactOrder::
                    INTER_ACTOR_CONTACT_BUDGET % task_count;
        for (std::size_t index = 0; index < m_buffers.size(); ++index)
        {
            const std::size_t quota =
                index < task_count
                    ? base_quota + (index < remainder ? 1U : 0U)
                    : 0U;
            m_buffers[index].Reset(quota);
        }
    }

    std::vector<ContactTaskBuffer>& GetBuffers()
    {
        return m_buffers;
    }

private:
    std::vector<ContactTaskBuffer> m_buffers;
};

struct DeterministicStateTraceRuntime
{
    std::ofstream output;
    std::unique_ptr<DeterministicStateTrace::Writer> writer;
    std::vector<DeterministicContactOrder::InterActorKey> contact_keys;
    std::vector<const Actor*> actors;
    ContactConservation::Aggregate contact_conservation;
    ContactConservation::Error contact_conservation_error =
        ContactConservation::Error::NONE;
    std::uint64_t scenario_id = 0;
    std::uint64_t step_limit =
        DeterministicStateTrace::MAX_TRACE_STEPS;
    std::string output_path;
};

static_assert(
    ContactConservation::MAX_AGGREGATE_CONTACTS ==
        static_cast<std::uint64_t>(
            DeterministicContactOrder::INTER_ACTOR_CONTACT_BUDGET) *
        DeterministicStateTrace::MAX_TRACE_STEPS,
    "contact-conservation aggregate must cover the complete trace ceiling");

struct DeterministicActorInputRuntime:
    DeterministicVehicleInput::SnapshotProvider,
    DeterministicVehicleInput::SnapshotConsumer
{
    DeterministicInputTrace::Runtime trace;
    std::unique_ptr<DeterministicVehicleInput::RecordingSource>
        recording_source;
    std::unique_ptr<DeterministicVehicleInput::ReplaySink> replay_sink;
    ActorPtr actor;
    DeterministicVehicleInputActorAdapter::PolicySnapshot policy;
    std::ofstream output;
    std::string output_path;
    std::string replay_path;
    std::string configured_mode;
    std::uint64_t scenario_id = 0;
    std::uint64_t target_id = 0;
    std::uint64_t step_limit = 0;
    DeterministicVehicleInputActorAdapter::PolicyError
        adapter_error =
            DeterministicVehicleInputActorAdapter::PolicyError::NONE;

    bool CaptureAppliedControls(
        std::uint64_t physics_step,
        DeterministicVehicleInput::Snapshot& snapshot) override;
    bool ApplyAppliedControls(
        std::uint64_t physics_step,
        const DeterministicVehicleInput::Snapshot& snapshot) override;
};

struct DeterministicActorInputPendingSavegame
{
    DeterministicInputContinuationSavegame::Payload payload;
    ActorPtr restored_player_actor;
    bool announce_scene_loaded = false;
};

} // namespace RoR

namespace {

bool ParseDecimalUint64(
    const std::string& value,
    std::uint64_t& parsed)
{
    if (value.empty())
        return false;

    std::uint64_t result = 0;
    for (std::string::const_iterator iterator = value.begin();
            iterator != value.end();
            ++iterator)
    {
        if (*iterator < '0' || *iterator > '9')
            return false;
        const std::uint64_t digit =
            static_cast<std::uint64_t>(*iterator - '0');
        if (result >
            (std::numeric_limits<std::uint64_t>::max() - digit) /
                UINT64_C(10))
        {
            return false;
        }
        result = result * UINT64_C(10) + digit;
    }
    parsed = result;
    return true;
}

static const std::uint64_t MAX_LIVE_INPUT_STEPS = UINT64_C(120000);
static const std::uint64_t MAX_LIVE_INPUT_EVENTS = UINT64_C(2000000);
static const std::uint64_t MAX_LIVE_INPUT_BYTES =
    UINT64_C(128) * UINT64_C(1024) * UINT64_C(1024);

bool IsDeterministicInputMode(
    const std::string& mode,
    RoR::DeterministicInputTrace::RuntimeMode& parsed)
{
    if (mode == "off")
    {
        parsed = RoR::DeterministicInputTrace::RuntimeMode::NONE;
        return true;
    }
    if (mode == "record")
    {
        parsed = RoR::DeterministicInputTrace::RuntimeMode::RECORD;
        return true;
    }
    if (mode == "replay")
    {
        parsed = RoR::DeterministicInputTrace::RuntimeMode::REPLAY;
        return true;
    }
    parsed = RoR::DeterministicInputTrace::RuntimeMode::NONE;
    return false;
}

const char* SnapshotErrorName(
    RoR::DeterministicStateDigest::SnapshotError error)
{
    using RoR::DeterministicStateDigest::SnapshotError;
    switch (error)
    {
    case SnapshotError::NONE:
        return "none";
    case SnapshotError::COUNT_LIMIT_EXCEEDED:
        return "count-limit-exceeded";
    case SnapshotError::SOURCE_READ_FAILED:
        return "source-read-failed";
    case SnapshotError::INVALID_ACTOR_ID:
        return "invalid-actor-id";
    case SnapshotError::DUPLICATE_ACTOR_ID:
        return "duplicate-actor-id";
    case SnapshotError::INVALID_CROSS_REFERENCE:
        return "invalid-cross-reference";
    case SnapshotError::ALLOCATION_FAILED:
        return "allocation-failed";
    case SnapshotError::DIGEST_REJECTED:
        return "digest-rejected";
    }
    return "unknown";
}

const char* DigestErrorName(
    RoR::DeterministicStateDigest::Error error)
{
    using RoR::DeterministicStateDigest::Error;
    switch (error)
    {
    case Error::NONE:
        return "none";
    case Error::INVALID_SECTION_ORDER:
        return "invalid-section-order";
    case Error::COUNT_LIMIT_EXCEEDED:
        return "count-limit-exceeded";
    case Error::COUNT_MISMATCH:
        return "count-mismatch";
    case Error::NON_CANONICAL_KEY:
        return "noncanonical-key";
    case Error::INVALID_RECORD:
        return "invalid-record";
    case Error::NON_FINITE_VALUE:
        return "non-finite-value";
    case Error::ALREADY_FINISHED:
        return "already-finished";
    }
    return "unknown";
}

std::uint32_t GetDeterministicTracePhysicsFlags()
{
#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
    return RoR::DeterministicStateTrace::PHYSICS_FLAG_FAST_MATH;
#else
    return 0;
#endif
}

enum class ExclusiveCreateResult
{
    CREATED,
    EXISTS,
    FAILED
};

#if defined(_WIN32)
bool Utf8ToWidePath(
    const std::string& path,
    std::wstring& wide_path)
{
    const int wide_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.c_str(),
        -1,
        nullptr,
        0);
    if (wide_size <= 0)
        return false;

    try
    {
        wide_path.resize(static_cast<std::size_t>(wide_size));
    }
    catch (...)
    {
        return false;
    }
    return MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.c_str(),
        -1,
        &wide_path[0],
        wide_size) == wide_size;
}
#endif

ExclusiveCreateResult CreateEmptyFileExclusive(
    const std::string& path)
{
#if defined(_WIN32)
    std::wstring wide_path;
    if (!Utf8ToWidePath(path, wide_path))
        return ExclusiveCreateResult::FAILED;

    HANDLE handle = CreateFileW(
        wide_path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS ||
                error == ERROR_ALREADY_EXISTS)
        {
            return ExclusiveCreateResult::EXISTS;
        }
        return ExclusiveCreateResult::FAILED;
    }
    if (!CloseHandle(handle))
        return ExclusiveCreateResult::FAILED;
    return ExclusiveCreateResult::CREATED;
#else
    int descriptor = -1;
    do
    {
        descriptor = ::open(
            path.c_str(),
            O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR);
    }
    while (descriptor < 0 && errno == EINTR);

    if (descriptor < 0)
    {
        if (errno == EEXIST)
            return ExclusiveCreateResult::EXISTS;
        return ExclusiveCreateResult::FAILED;
    }

    const int close_result = ::close(descriptor);
    // POSIX leaves the descriptor state unspecified after EINTR; retrying can
    // close a descriptor that another thread has since acquired.
    return close_result == 0 || errno == EINTR
        ? ExclusiveCreateResult::CREATED
        : ExclusiveCreateResult::FAILED;
#endif
}

bool OpenTraceOutputAppend(
    std::ofstream& output,
    const std::string& path)
{
#if defined(_WIN32)
    std::wstring wide_path;
    if (!Utf8ToWidePath(path, wide_path))
        return false;
    output.open(
        wide_path.c_str(),
        std::ios::out | std::ios::binary | std::ios::app);
#else
    output.open(
        path.c_str(),
        std::ios::out | std::ios::binary | std::ios::app);
#endif
    return output.is_open() && output.good();
}

bool BuildActorInputPolicy(
    const RoR::ActorPtr& actor,
    std::uint64_t target_id,
    RoR::DeterministicVehicleInputActorAdapter::PolicySnapshot& policy)
{
    using Policy =
        RoR::DeterministicVehicleInputActorAdapter::PolicySnapshot;
    if (actor == nullptr)
        return false;

    Policy candidate;
    candidate.target_id = target_id;
    candidate.local_simulated =
        actor->ar_state == RoR::ActorState::LOCAL_SIMULATED;
    candidate.truck = actor->ar_driveable == RoR::TRUCK;
    candidate.has_engine = actor->ar_engine != nullptr;
    candidate.resetting = actor->isBeingReset();
    candidate.physics_paused = actor->ar_physics_paused;
    candidate.ai_active =
        actor->ar_vehicle_ai != nullptr &&
        actor->ar_vehicle_ai->isActive();
    candidate.has_linked_actors = !actor->ar_linked_actors.empty();
    candidate.has_transfer_case =
        actor->getTransferCaseMode() != nullptr;
    candidate.anti_lock_brake_enabled = actor->alb_mode;
    candidate.traction_control_enabled = actor->tc_mode;
    candidate.cruise_control_enabled = actor->cc_mode;
    candidate.speed_limiter_enabled = actor->sl_enabled;
    candidate.forward_commands_enabled = actor->ar_forward_commands;
    candidate.import_commands_enabled = actor->ar_import_commands;
    candidate.has_simulated_event_overrides =
        !actor->ar_actor_event_simulated_values.empty();
    candidate.hydro_speed_coupling_enabled =
        actor->ar_hydro_speed_coupling_enabled;

    if (candidate.has_engine)
    {
        candidate.gearbox_mode = static_cast<std::uint32_t>(
            actor->ar_engine->getAutoMode());
        candidate.forward_gear_count =
            actor->ar_engine->getNumGears();
        candidate.gear_range_count =
            actor->ar_engine->getNumGearsRanges();
        candidate.fixed_gear = actor->ar_engine->getGear();
        candidate.fixed_gear_range =
            actor->ar_engine->getGearRange();
    }
    policy = candidate;
    return true;
}

bool CaptureActorInputState(
    const RoR::ActorPtr& actor,
    RoR::DeterministicVehicleInputActorAdapter::AppliedControlState& state)
{
    if (actor == nullptr || actor->ar_engine == nullptr)
        return false;

    RoR::DeterministicVehicleInputActorAdapter::AppliedControlState
        candidate;
    candidate.steering_command = actor->ar_hydro_dir_command;
    candidate.service_brake = actor->ar_brake;
    candidate.throttle = actor->ar_engine->getAcc();
    candidate.clutch = actor->ar_engine->getClutch();
    candidate.parking_brake = actor->ar_parking_brake;
    candidate.engine_contact = actor->ar_engine->hasContact();
    candidate.engine_starter = actor->ar_engine->isStarterActive();
    candidate.gear = actor->ar_engine->getGear();
    candidate.gear_range = actor->ar_engine->getGearRange();
    candidate.hydro_speed_coupling =
        actor->ar_hydro_speed_coupling_active;
    candidate.trailer_parking_brake =
        actor->ar_trailer_parking_brake;
    for (std::size_t index = 0;
        index < candidate.command_values.size();
        ++index)
    {
        candidate.command_values[index] =
            actor->ar_command_key[
                static_cast<int>(index + 1U)].playerInputValue;
    }
    state = candidate;
    return true;
}

void CommitActorInputPlan(
    const RoR::ActorPtr& actor,
    const RoR::DeterministicVehicleInputActorAdapter::ApplyPlan& plan)
{
    const RoR::DeterministicVehicleInputActorAdapter::AppliedControlState&
        controls = plan.controls;
    actor->ar_hydro_dir_command = controls.steering_command;
    actor->ar_brake = controls.service_brake;
    actor->ar_parking_brake = controls.parking_brake;
    actor->ar_trailer_parking_brake =
        controls.trailer_parking_brake;
    actor->ar_hydro_speed_coupling_active =
        controls.hydro_speed_coupling;
    actor->ar_engine->setAcc(controls.throttle);
    actor->ar_engine->setClutch(controls.clutch);
    actor->ar_engine->setGear(controls.gear);
    actor->ar_engine->setGearRange(controls.gear_range);
    actor->ar_engine->setDeterministicInputIgnition(
        controls.engine_contact,
        controls.engine_starter);
    for (std::size_t index = 0;
        index < controls.command_values.size();
        ++index)
    {
        actor->ar_command_key[
            static_cast<int>(index + 1U)].playerInputValue =
                controls.command_values[index];
    }
}

void AppendUint32BigEndian(
    std::vector<std::uint8_t>& output,
    std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

void AppendUint64BigEndian(
    std::vector<std::uint8_t>& output,
    std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool AppendBoundedIdentityString(
    std::vector<std::uint8_t>& output,
    const std::string& value)
{
    static const std::size_t MAX_IDENTITY_COMPONENT_BYTES = 4096;
    if (value.size() > MAX_IDENTITY_COMPONENT_BYTES)
        return false;
    AppendUint32BigEndian(
        output,
        static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

bool BuildActorInputMetadata(
    const RoR::ActorPtr& actor,
    const RoR::DeterministicVehicleInputActorAdapter::PolicySnapshot& policy,
    std::uint64_t scenario_id,
    std::uint64_t first_physics_step,
    RoR::DeterministicInputTrace::Metadata& metadata)
{
    if (actor == nullptr)
        return false;
    try
    {
        std::vector<std::uint8_t> identity;
        const char* const manifest =
            RoR::DeterministicVehicleInputActorAdapter::PolicyManifest();
        const std::size_t manifest_size = std::strlen(manifest);
        identity.reserve(manifest_size + 128U +
            actor->ar_filename.size() + actor->ar_filehash.size() +
            RoR::App::sim_terrain_name->getStr().size());
        identity.insert(
            identity.end(),
            manifest,
            manifest + manifest_size);
        AppendUint64BigEndian(identity, policy.target_id);
        AppendUint32BigEndian(identity, policy.gearbox_mode);
        AppendUint32BigEndian(
            identity,
            static_cast<std::uint32_t>(policy.forward_gear_count));
        AppendUint32BigEndian(
            identity,
            static_cast<std::uint32_t>(policy.gear_range_count));
        AppendUint32BigEndian(
            identity,
            static_cast<std::uint32_t>(policy.fixed_gear));
        AppendUint32BigEndian(
            identity,
            static_cast<std::uint32_t>(policy.fixed_gear_range));
        identity.push_back(
            policy.hydro_speed_coupling_enabled ? UINT8_C(1) : UINT8_C(0));
        if (!AppendBoundedIdentityString(identity, actor->ar_filename) ||
            !AppendBoundedIdentityString(identity, actor->ar_filehash) ||
            !AppendBoundedIdentityString(
                identity,
                RoR::App::sim_terrain_name->getStr()))
        {
            return false;
        }

        RoR::DeterministicInputTrace::Metadata candidate;
        candidate.semantic_flags =
            RoR::DeterministicInputTrace::REQUIRED_SEMANTIC_FLAGS;
        candidate.scenario_id = scenario_id;
        candidate.stream_id = policy.target_id;
        candidate.first_physics_step = first_physics_step;
        candidate.physics_step_numerator = 1;
        candidate.physics_step_denominator = 2000;
        candidate.scenario_name = "ror-live-manual-truck-v1";
        candidate.source_name =
            RoR::DeterministicVehicleInput::RegistrySourceName();
        candidate.source_digest =
            RoR::DeterministicInputTrace::ComputeSha256(
                identity.data(),
                identity.size());
        metadata = candidate;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool SameActorInputMetadata(
    const RoR::DeterministicInputTrace::Metadata& first,
    const RoR::DeterministicInputTrace::Metadata& second)
{
    return first.semantic_flags == second.semantic_flags &&
        first.scenario_id == second.scenario_id &&
        first.stream_id == second.stream_id &&
        first.first_physics_step == second.first_physics_step &&
        first.physics_step_numerator == second.physics_step_numerator &&
        first.physics_step_denominator == second.physics_step_denominator &&
        first.scenario_name == second.scenario_name &&
        first.source_name == second.source_name &&
        first.source_digest == second.source_digest;
}

bool ReadContinuationMetadata(
    const RoR::DeterministicInputTrace::RuntimeContinuation& continuation,
    RoR::DeterministicInputTrace::Metadata& metadata)
{
    try
    {
        std::istringstream input(
            continuation.authenticated_trace,
            std::ios::in | std::ios::binary);
        RoR::DeterministicInputTrace::Reader reader(
            input,
            continuation.limits);
        if (!reader.IsReady())
            return false;
        metadata = reader.GetMetadata();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

RoR::DeterministicInputTrace::Limits BuildActorInputLimits(
    std::uint64_t step_limit)
{
    RoR::DeterministicInputTrace::Limits limits;
    limits.max_steps = step_limit;
    limits.max_events = MAX_LIVE_INPUT_EVENTS;
    limits.max_bytes = MAX_LIVE_INPUT_BYTES;
    limits.max_events_per_step = static_cast<std::uint32_t>(
        RoR::DeterministicVehicleInput::CONTROL_SLOT_COUNT);
    limits.max_active_controls = static_cast<std::uint32_t>(
        RoR::DeterministicVehicleInput::CONTROL_SLOT_COUNT);
    return limits;
}

bool OpenUniqueDeterministicInputTrace(
    RoR::DeterministicActorInputRuntime& runtime,
    std::uint64_t first_physics_step)
{
    const std::string logs_directory = RoR::App::sys_logs_dir->getStr();
    if (logs_directory.empty() || !RoR::FolderExists(logs_directory))
        return false;

    static const std::uint32_t MAX_FILENAME_ATTEMPTS = 10000;
    for (std::uint32_t attempt = 0;
        attempt < MAX_FILENAME_ATTEMPTS;
        ++attempt)
    {
        const std::string filename = fmt::format(
            "deterministic-input-s{}-t{}-step{}-{:04}.rorinput",
            runtime.scenario_id,
            runtime.target_id,
            first_physics_step,
            attempt);
        const std::string path = RoR::PathCombine(
            logs_directory,
            filename);
        const ExclusiveCreateResult result = CreateEmptyFileExclusive(path);
        if (result == ExclusiveCreateResult::EXISTS)
            continue;
        if (result == ExclusiveCreateResult::FAILED)
            return false;
        if (!OpenTraceOutputAppend(runtime.output, path))
            return false;
        runtime.output.seekp(0, std::ios::end);
        if (!runtime.output.good() ||
            runtime.output.tellp() != std::streampos(0))
        {
            runtime.output.close();
            continue;
        }
        runtime.output_path = path;
        return true;
    }
    return false;
}

bool ReadBoundedDeterministicInputTrace(
    const std::string& path,
    std::string& bytes)
{
    if (path.empty())
        return false;
    std::ifstream input;
#if defined(_WIN32)
    std::wstring wide_path;
    if (!Utf8ToWidePath(path, wide_path))
        return false;
    input.open(wide_path.c_str(), std::ios::in | std::ios::binary);
#else
    input.open(path.c_str(), std::ios::in | std::ios::binary);
#endif
    if (!input.is_open() || !input.good())
        return false;
    input.seekg(0, std::ios::end);
    const std::streampos end = input.tellg();
    const std::streamoff byte_count = end - std::streampos(0);
    if (byte_count <= std::streamoff(0) ||
        static_cast<std::uint64_t>(byte_count) > MAX_LIVE_INPUT_BYTES)
    {
        return false;
    }
    input.seekg(0, std::ios::beg);
    if (!input.good())
        return false;
    try
    {
        std::string candidate(
            static_cast<std::size_t>(byte_count),
            '\0');
        input.read(&candidate[0], static_cast<std::streamsize>(candidate.size()));
        if (!input.good() || input.gcount() !=
            static_cast<std::streamsize>(candidate.size()))
        {
            return false;
        }
        bytes.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool OpenUniqueDeterministicTrace(
    RoR::DeterministicStateTraceRuntime& runtime,
    std::uint64_t scenario_id,
    std::uint64_t first_physics_step)
{
    const std::string logs_directory =
        RoR::App::sys_logs_dir->getStr();
    if (logs_directory.empty() ||
            !RoR::FolderExists(logs_directory))
    {
        return false;
    }

    // Reserve a unique name atomically. APP mode then guarantees that opening
    // the stream cannot truncate even this process's zero-byte reservation.
    static const std::uint32_t MAX_FILENAME_ATTEMPTS = 10000;
    for (std::uint32_t attempt = 0;
            attempt < MAX_FILENAME_ATTEMPTS;
            ++attempt)
    {
        const std::string filename = fmt::format(
            "deterministic-state-s{}-step{}-{:04}.rortrace",
            scenario_id,
            first_physics_step,
            attempt);
        const std::string path =
            RoR::PathCombine(logs_directory, filename);
        const ExclusiveCreateResult create_result =
            CreateEmptyFileExclusive(path);
        if (create_result == ExclusiveCreateResult::EXISTS)
            continue;
        if (create_result == ExclusiveCreateResult::FAILED)
            return false;

        if (!OpenTraceOutputAppend(runtime.output, path))
        {
            runtime.output.clear();
            runtime.output.close();
            return false;
        }

        runtime.output.seekp(0, std::ios::end);
        if (!runtime.output.good() ||
                runtime.output.tellp() != std::streampos(0))
        {
            runtime.output.clear();
            runtime.output.close();
            continue;
        }
        runtime.output_path = path;
        return true;
    }
    return false;
}

bool AppendBufferedContactKeys(
    const std::vector<
        RoR::InterActorContactBufferPool::ContactTaskBuffer>& buffers,
    std::vector<
        RoR::DeterministicContactOrder::InterActorKey>& keys)
{
    try
    {
        for (const RoR::InterActorContactBufferPool::ContactTaskBuffer&
                buffer : buffers)
        {
            for (const RoR::InterActorCollisionContact& contact :
                    buffer.GetItems())
            {
                keys.push_back(contact.key);
            }
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}

} // namespace

bool DeterministicActorInputRuntime::CaptureAppliedControls(
    std::uint64_t,
    DeterministicVehicleInput::Snapshot& snapshot)
{
    DeterministicVehicleInputActorAdapter::PolicySnapshot current_policy;
    if (!BuildActorInputPolicy(actor, target_id, current_policy) ||
        !DeterministicVehicleInputActorAdapter::SamePolicy(
            policy,
            current_policy))
    {
        adapter_error =
            DeterministicVehicleInputActorAdapter::PolicyError::POLICY_CHANGED;
        return false;
    }
    DeterministicVehicleInputActorAdapter::Status policy_status;
    if (!DeterministicVehicleInputActorAdapter::ValidatePolicy(
            current_policy,
            policy_status))
    {
        adapter_error = policy_status.error;
        return false;
    }
    DeterministicVehicleInputActorAdapter::AppliedControlState controls;
    if (!CaptureActorInputState(actor, controls))
    {
        adapter_error =
            DeterministicVehicleInputActorAdapter::PolicyError::SNAPSHOT_REJECTED;
        return false;
    }
    if (!DeterministicVehicleInputActorAdapter::CaptureSnapshot(
            current_policy,
            controls,
            snapshot,
            policy_status))
    {
        adapter_error = policy_status.error;
        return false;
    }
    adapter_error =
        DeterministicVehicleInputActorAdapter::PolicyError::NONE;
    return true;
}

bool DeterministicActorInputRuntime::ApplyAppliedControls(
    std::uint64_t,
    const DeterministicVehicleInput::Snapshot& snapshot)
{
    DeterministicVehicleInputActorAdapter::PolicySnapshot current_policy;
    if (!BuildActorInputPolicy(actor, target_id, current_policy) ||
        !DeterministicVehicleInputActorAdapter::SamePolicy(
            policy,
            current_policy))
    {
        adapter_error =
            DeterministicVehicleInputActorAdapter::PolicyError::POLICY_CHANGED;
        return false;
    }
    DeterministicVehicleInputActorAdapter::ApplyPlan plan;
    DeterministicVehicleInputActorAdapter::Status policy_status;
    if (!DeterministicVehicleInputActorAdapter::BuildApplyPlan(
            current_policy,
            snapshot,
            plan,
            policy_status))
    {
        adapter_error = policy_status.error;
        return false;
    }
    CommitActorInputPlan(actor, plan);
    adapter_error =
        DeterministicVehicleInputActorAdapter::PolicyError::NONE;
    return true;
}

ActorManager::ActorManager()
    : m_dt_remainder(0.0f)
    , m_forced_awake(false)
    , m_physics_steps(2000)
    , m_simulation_speed(1.0f)
    , m_inter_contact_buffers(new InterActorContactBufferPool())
{
    // Create worker thread (used for physics calculations)
    m_sim_thread_pool = std::unique_ptr<ThreadPool>(new ThreadPool(1));
}

ActorManager::~ActorManager()
{
    this->ShutdownWorkerRuntime();
}

bool ActorManager::ShutdownWorkerRuntime() noexcept
{
    if (m_sim_thread_pool == nullptr && m_sim_task == nullptr &&
        m_deterministic_state_trace == nullptr &&
        m_deterministic_actor_input == nullptr &&
        m_deterministic_actor_input_pending_savegame == nullptr)
    {
        return true;
    }

    try
    {
        this->SyncWithSimThread();
        this->FinishDeterministicStateTrace(
            "physics worker shutdown",
            false);
        this->FinishDeterministicActorInput(
            "physics worker shutdown",
            false,
            false);
        m_deterministic_actor_input_pending_savegame.reset();
        m_sim_task.reset();
        m_sim_thread_pool.reset();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ActorManager::ShouldSuppressLiveInputForDeterministicReplay(
    const ActorPtr& actor) const
{
    if (actor == nullptr || App::sim_deterministic_input_mode == nullptr ||
        App::sim_deterministic_input_mode->getStr() != "replay" ||
        App::GetGameContext() == nullptr)
    {
        return false;
    }
    return App::GetGameContext()->GetPlayerActor() == actor;
}

void ActorManager::FinishDeterministicStateTrace(
    const char* reason,
    bool suppress_until_disabled)
{
    if (m_deterministic_state_trace != nullptr)
    {
        DeterministicStateTraceRuntime& runtime =
            *m_deterministic_state_trace;
        bool finished = true;
        std::uint64_t step_count = 0;
        if (runtime.writer != nullptr)
        {
            step_count = runtime.writer->GetStepCount();
            finished = runtime.writer->Finish();
            if (!finished)
            {
                const DeterministicStateTrace::Status& status =
                    runtime.writer->GetStatus();
                RoR::LogFormat(
                    "[RoR|Determinism] State trace '%s' could not be "
                    "finished (%s, byte=%llu, step-index=%llu; %s)",
                    runtime.output_path.c_str(),
                    DeterministicStateTrace::ToString(status.error),
                    static_cast<unsigned long long>(
                        status.byte_offset),
                    static_cast<unsigned long long>(
                        status.step_index),
                    reason != nullptr ? reason : "no reason supplied");
            }
        }

        bool stream_ok = true;
        if (runtime.output.is_open())
        {
            runtime.output.flush();
            stream_ok = runtime.output.good();
            runtime.output.close();
            stream_ok = stream_ok && runtime.output.good();
        }
        if (!stream_ok)
        {
            RoR::LogFormat(
                "[RoR|Determinism] State trace '%s' failed while "
                "flushing; treat the artifact as invalid (%s)",
                runtime.output_path.c_str(),
                reason != nullptr ? reason : "no reason supplied");
        }
        else if (finished && runtime.writer != nullptr)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Finished state trace '%s' with "
                "%llu fixed-step records (%s)",
                runtime.output_path.c_str(),
                static_cast<unsigned long long>(step_count),
                reason != nullptr ? reason : "capture complete");

            const ContactConservation::Aggregate& conservation =
                runtime.contact_conservation;
            if (runtime.contact_conservation_error !=
                    ContactConservation::Error::NONE ||
                    conservation.maximum_normalized_linear_impulse_residual >
                        1.0e-6)
            {
                RoR::LogFormat(
                    "[RoR|Determinism|ContactConservation] FAIL "
                    "schema=%u contacts=%llu fixed_steps=%llu error=%s "
                    "maximum_normalized_linear_impulse_residual=%.17g",
                    ContactConservation::SCHEMA_VERSION,
                    static_cast<unsigned long long>(
                        conservation.contact_count),
                    static_cast<unsigned long long>(step_count),
                    ContactConservation::ErrorToString(
                        runtime.contact_conservation_error),
                    conservation.
                        maximum_normalized_linear_impulse_residual);
            }
            else
            {
                RoR::LogFormat(
                    "[RoR|Determinism|ContactConservation] PASS "
                    "schema=%u contacts=%llu fixed_steps=%llu "
                    "maximum_normalized_linear_impulse_residual=%.17g "
                    "maximum_angular_impulse_delta_magnitude_nms=%.17g "
                    "summed_angular_impulse_delta_x_nms=%.17g "
                    "summed_angular_impulse_delta_y_nms=%.17g "
                    "summed_angular_impulse_delta_z_nms=%.17g "
                    "summed_isolated_contact_work_j=%.17g "
                    "summed_isolated_contact_kinetic_energy_delta_j=%.17g "
                    "summed_isolated_contact_integration_energy_delta_j=%.17g "
                    "whole_step_shared_node_energy=not_audited",
                    ContactConservation::SCHEMA_VERSION,
                    static_cast<unsigned long long>(
                        conservation.contact_count),
                    static_cast<unsigned long long>(step_count),
                    conservation.
                        maximum_normalized_linear_impulse_residual,
                    conservation.
                        maximum_angular_impulse_delta_magnitude_nms,
                    conservation.summed_angular_impulse_delta_nms.x,
                    conservation.summed_angular_impulse_delta_nms.y,
                    conservation.summed_angular_impulse_delta_nms.z,
                    conservation.summed_isolated_contact_work_j,
                    conservation.
                        summed_isolated_contact_kinetic_energy_delta_j,
                    conservation.
                        summed_isolated_contact_integration_energy_delta_j);
            }
        }
    }

    m_deterministic_state_trace.reset();
    m_deterministic_state_trace_suppressed =
        suppress_until_disabled;
}

void ActorManager::FinishDeterministicActorInput(
    const char* reason,
    bool suppress_until_disabled,
    bool stop_replay)
{
    if (m_deterministic_actor_input != nullptr)
    {
        DeterministicActorInputRuntime& runtime =
            *m_deterministic_actor_input;
        const DeterministicInputTrace::RuntimeMode mode =
            runtime.trace.GetMode();
        bool artifact_ok = true;
        std::string bytes;
        if (mode == DeterministicInputTrace::RuntimeMode::RECORD)
        {
            const DeterministicInputTrace::RuntimeLifecycle lifecycle =
                runtime.trace.GetLifecycle();
            if (lifecycle ==
                    DeterministicInputTrace::RuntimeLifecycle::RUNNING ||
                lifecycle ==
                    DeterministicInputTrace::RuntimeLifecycle::PAUSED)
            {
                artifact_ok = runtime.trace.FinalizeRecording(bytes);
            }
            else if (lifecycle ==
                DeterministicInputTrace::RuntimeLifecycle::FINISHED)
            {
                try
                {
                    bytes = runtime.trace.GetAuthenticatedTrace();
                }
                catch (...)
                {
                    artifact_ok = false;
                }
            }
            else
            {
                artifact_ok = false;
            }

            if (artifact_ok && runtime.output.is_open())
            {
                runtime.output.write(
                    bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));
                runtime.output.flush();
                artifact_ok = runtime.output.good();
            }
            else if (runtime.output.is_open())
            {
                artifact_ok = false;
            }
            if (runtime.output.is_open())
            {
                runtime.output.close();
                artifact_ok = artifact_ok && runtime.output.good();
            }

            if (artifact_ok)
            {
                RoR::LogFormat(
                    "[RoR|Determinism] Finished input recording '%s' "
                    "with %llu authenticated fixed-step records, digest=%s "
                    "(%s)",
                    runtime.output_path.c_str(),
                    static_cast<unsigned long long>(
                        runtime.trace.GetProcessedStepCount()),
                    runtime.trace.GetTraceDigest().ToHex().c_str(),
                    reason != nullptr ? reason : "capture complete");
            }
            else
            {
                const DeterministicInputTrace::RuntimeStatus& status =
                    runtime.trace.GetStatus();
                RoR::LogFormat(
                    "[RoR|Determinism] Input recording '%s' is invalid "
                    "(%s, trace=%s, step=%llu; %s)",
                    runtime.output_path.c_str(),
                    DeterministicInputTrace::ToString(status.error),
                    DeterministicInputTrace::ToString(
                        status.trace_status.error),
                    static_cast<unsigned long long>(status.physics_step),
                    reason != nullptr ? reason : "no reason supplied");
            }
        }
        else if (mode == DeterministicInputTrace::RuntimeMode::REPLAY)
        {
            const DeterministicInputTrace::RuntimeStatus& status =
                runtime.trace.GetStatus();
            RoR::LogFormat(
                "[RoR|Determinism] Finished input replay '%s' after %llu "
                "authenticated fixed-step records (lifecycle=%s, error=%s; "
                "%s)",
                runtime.replay_path.c_str(),
                static_cast<unsigned long long>(
                    runtime.trace.GetProcessedStepCount()),
                DeterministicInputTrace::ToString(
                    runtime.trace.GetLifecycle()),
                DeterministicInputTrace::ToString(status.error),
                reason != nullptr ? reason : "replay complete");
        }
    }

    m_deterministic_actor_input.reset();
    m_deterministic_actor_input_suppressed =
        suppress_until_disabled;
    m_deterministic_actor_input_stop_replay =
        suppress_until_disabled && stop_replay;
}

bool ActorManager::CaptureDeterministicActorInputSavegame(
    DeterministicInputContinuationSavegame::Payload& output,
    bool& present)
{
    present = false;
    if (m_deterministic_actor_input_pending_savegame != nullptr)
        return false;
    if (m_deterministic_actor_input == nullptr)
        return true;

    this->SyncWithSimThread();
    DeterministicActorInputRuntime& runtime =
        *m_deterministic_actor_input;
    GameContext* const context = App::GetGameContext();
    const DeterministicInputTrace::RuntimeLifecycle lifecycle =
        runtime.trace.GetLifecycle();
    if (lifecycle != DeterministicInputTrace::RuntimeLifecycle::RUNNING &&
        lifecycle != DeterministicInputTrace::RuntimeLifecycle::PAUSED)
    {
        return false;
    }

    DeterministicVehicleInputActorAdapter::PolicySnapshot current_policy;
    DeterministicVehicleInputActorAdapter::Status policy_status;
    DeterministicInputTrace::Metadata current_metadata;
    if (context == nullptr ||
        context->GetActorManager() != this ||
        runtime.actor == nullptr ||
        context->GetPlayerActor() != runtime.actor ||
        runtime.trace.GetNextPhysicsStep() != m_completed_physics_steps ||
        !BuildActorInputPolicy(
            runtime.actor,
            runtime.target_id,
            current_policy) ||
        !DeterministicVehicleInputActorAdapter::ValidatePolicy(
            current_policy,
            policy_status) ||
        !DeterministicVehicleInputActorAdapter::SamePolicy(
            current_policy,
            runtime.policy) ||
        !BuildActorInputMetadata(
            runtime.actor,
            current_policy,
            runtime.scenario_id,
            runtime.trace.GetIdentity().first_physics_step,
            current_metadata) ||
        !SameActorInputMetadata(
            current_metadata,
            runtime.trace.GetIdentity()))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Refusing input savegame because the live "
            "Actor, policy, identity, or fixed-step cursor changed");
        return false;
    }

    const bool was_running = lifecycle ==
        DeterministicInputTrace::RuntimeLifecycle::RUNNING;
    if (was_running && !runtime.trace.Pause())
    {
        this->FinishDeterministicActorInput(
            "savegame pause failed",
            true,
            runtime.trace.GetMode() ==
                DeterministicInputTrace::RuntimeMode::REPLAY);
        m_simulation_paused = true;
        return false;
    }

    DeterministicInputContinuationSavegame::Payload candidate;
    candidate.resume_after_load = was_running && !m_simulation_paused;
    candidate.scenario_id = runtime.scenario_id;
    candidate.target_id = runtime.target_id;
    candidate.step_limit = runtime.step_limit;
    candidate.completed_physics_steps = m_completed_physics_steps;
    candidate.actor_physics_step = runtime.actor->m_physics_step;
    const bool exported =
        runtime.trace.ExportContinuation(candidate.continuation);
    const bool resumed = !was_running || runtime.trace.Resume();
    if (!exported || !resumed)
    {
        const bool replay = runtime.trace.GetMode() ==
            DeterministicInputTrace::RuntimeMode::REPLAY;
        this->FinishDeterministicActorInput(
            exported ? "savegame resume failed" :
                "savegame continuation export failed",
            true,
            replay);
        m_simulation_paused = true;
        return false;
    }

    output.Swap(candidate);
    present = true;
    return true;
}

bool ActorManager::StageDeterministicActorInputSavegame(
    const DeterministicInputContinuationSavegame::Payload* payload,
    std::uint64_t completed_physics_steps,
    bool announce_scene_loaded)
{
    std::unique_ptr<DeterministicActorInputPendingSavegame> candidate;
    try
    {
        if (payload != nullptr)
        {
            if (payload->completed_physics_steps !=
                completed_physics_steps)
            {
                return false;
            }
            candidate.reset(new DeterministicActorInputPendingSavegame());
            candidate->payload = *payload;
            candidate->announce_scene_loaded = announce_scene_loaded;
        }
    }
    catch (...)
    {
        return false;
    }

    this->SyncWithSimThread();
    this->FinishDeterministicActorInput(
        "savegame load boundary",
        false,
        false);
    m_deterministic_actor_input_pending_savegame = std::move(candidate);
    m_deterministic_actor_input_suppressed = false;
    m_deterministic_actor_input_stop_replay = false;
    m_deterministic_actor_input_pause_requested.store(
        false,
        std::memory_order_release);
    m_completed_physics_steps = completed_physics_steps;
    if (payload != nullptr)
    {
        m_simulation_paused = true;
    }
    else
    {
        // Absence is authoritative: loading an ordinary save must not restart
        // a record/replay mode left over from the previously loaded scene.
        App::sim_deterministic_input_mode->setStr("off");
        App::sim_deterministic_input_path->setStr("");
    }
    return true;
}

void ActorManager::FailPendingDeterministicActorInputSavegame(
    const char* reason)
{
    RoR::LogFormat(
        "[RoR|Determinism] Rejected deterministic input savegame "
        "continuation (%s)",
        reason != nullptr ? reason : "unspecified failure");
    m_deterministic_actor_input_pending_savegame.reset();
    m_deterministic_actor_input_suppressed = true;
    m_deterministic_actor_input_stop_replay = true;
    m_simulation_paused = true;
}

void ActorManager::NotifyDeterministicInputSavegameRestoreFailed(
    const char* reason) noexcept
{
    if (m_deterministic_actor_input_pending_savegame == nullptr)
        return;
    try
    {
        this->FailPendingDeterministicActorInputSavegame(reason);
    }
    catch (...)
    {
        m_deterministic_actor_input_pending_savegame.reset();
        m_deterministic_actor_input_suppressed = true;
        m_deterministic_actor_input_stop_replay = true;
        m_simulation_paused = true;
    }
}

bool ActorManager::BindRestoredDeterministicInputSavegamePlayer(
    const ActorPtr& actor)
{
    if (m_deterministic_actor_input_pending_savegame == nullptr)
        return true;
    DeterministicActorInputPendingSavegame& pending =
        *m_deterministic_actor_input_pending_savegame;
    if (actor == nullptr ||
        pending.restored_player_actor != nullptr ||
        actor->m_physics_step != pending.payload.actor_physics_step)
    {
        this->FailPendingDeterministicActorInputSavegame(
            "restored player Actor is duplicated or stale");
        return false;
    }
    pending.restored_player_actor = actor;
    return true;
}

bool ActorManager::TryActivateDeterministicActorInputSavegame()
{
    if (m_deterministic_actor_input_pending_savegame == nullptr)
        return true;

    GameContext* const context = App::GetGameContext();
    if (context == nullptr || context->GetActorManager() != this)
        return false;

    DeterministicActorInputPendingSavegame& pending =
        *m_deterministic_actor_input_pending_savegame;
    ActorPtr actor = pending.restored_player_actor;
    if (actor == nullptr || context->GetPlayerActor() != actor)
        return false;

    const DeterministicInputContinuationSavegame::Payload& payload =
        pending.payload;
    if (actor->m_physics_step != payload.actor_physics_step)
    {
        this->FailPendingDeterministicActorInputSavegame(
            "restored Actor fixed-step cursor changed before activation");
        return false;
    }

    DeterministicInputTrace::Metadata trace_metadata;
    DeterministicVehicleInputActorAdapter::PolicySnapshot policy;
    DeterministicVehicleInputActorAdapter::Status policy_status;
    DeterministicInputTrace::Metadata expected_metadata;
    if (!ReadContinuationMetadata(
            payload.continuation,
            trace_metadata) ||
        trace_metadata.scenario_id != payload.scenario_id ||
        trace_metadata.stream_id != payload.target_id)
    {
        this->FailPendingDeterministicActorInputSavegame(
            "continuation metadata is invalid");
        return false;
    }
    if (!BuildActorInputPolicy(actor, payload.target_id, policy) ||
        !DeterministicVehicleInputActorAdapter::ValidatePolicy(
            policy,
            policy_status) ||
        !BuildActorInputMetadata(
            actor,
            policy,
            payload.scenario_id,
            trace_metadata.first_physics_step,
            expected_metadata))
    {
        this->FailPendingDeterministicActorInputSavegame(
            "restored Actor policy does not match the saved source");
        return false;
    }

    try
    {
        std::unique_ptr<DeterministicActorInputRuntime> runtime(
            new DeterministicActorInputRuntime());
        runtime->actor = actor;
        runtime->policy = policy;
        runtime->configured_mode =
            DeterministicInputTrace::ToString(payload.continuation.mode);
        runtime->scenario_id = payload.scenario_id;
        runtime->target_id = payload.target_id;
        runtime->step_limit = payload.step_limit;
        if (!runtime->trace.ImportContinuation(
                payload.continuation,
                expected_metadata) ||
            runtime->trace.GetLifecycle() !=
                DeterministicInputTrace::RuntimeLifecycle::PAUSED)
        {
            this->FailPendingDeterministicActorInputSavegame(
                "authenticated continuation import failed");
            return false;
        }

        if (payload.continuation.mode ==
            DeterministicInputTrace::RuntimeMode::RECORD)
        {
            std::unique_ptr<DeterministicVehicleInput::RecordingSource>
                recording_source(
                    new DeterministicVehicleInput::RecordingSource(
                        runtime->trace,
                        payload.target_id,
                        *runtime));
            if (!OpenUniqueDeterministicInputTrace(
                    *runtime,
                    trace_metadata.first_physics_step))
            {
                this->FailPendingDeterministicActorInputSavegame(
                    "recording artifact reservation failed");
                return false;
            }
            runtime->recording_source = std::move(recording_source);
        }
        else
        {
            runtime->replay_path = "savegame:" +
                payload.continuation.authentication_digest.ToHex();
            runtime->replay_sink.reset(
                new DeterministicVehicleInput::ReplaySink(
                    runtime->trace,
                    payload.target_id,
                    *runtime));
        }

        if (payload.resume_after_load && !runtime->trace.Resume())
        {
            this->FailPendingDeterministicActorInputSavegame(
                "continuation resume failed");
            return false;
        }

        const std::string mode = runtime->configured_mode;
        const std::string scenario = fmt::format("{}", payload.scenario_id);
        const std::string target = fmt::format("{}", payload.target_id);
        const std::string step_limit = fmt::format("{}", payload.step_limit);
        const std::string path = runtime->replay_path;

        App::sim_deterministic_input_mode->setStr(mode);
        App::sim_deterministic_input_scenario_id->setStr(scenario);
        App::sim_deterministic_input_target_id->setStr(target);
        App::sim_deterministic_input_step_limit->setStr(step_limit);
        App::sim_deterministic_input_path->setStr(path);

        const std::uint64_t processed_steps =
            runtime->trace.GetProcessedStepCount();
        const std::string digest =
            runtime->trace.GetTraceDigest().ToHex();
        const bool announce_scene_loaded = pending.announce_scene_loaded;
        m_deterministic_actor_input = std::move(runtime);
        m_completed_physics_steps = payload.completed_physics_steps;
        m_simulation_paused = !payload.resume_after_load;

        m_deterministic_actor_input_pending_savegame.reset();
        RoR::LogFormat(
            "[RoR|Determinism] Restored %s input continuation at fixed "
            "step %llu after %llu authenticated records, digest=%s",
            mode.c_str(),
            static_cast<unsigned long long>(m_completed_physics_steps),
            static_cast<unsigned long long>(processed_steps),
            digest.c_str());
        if (announce_scene_loaded)
        {
            App::GetConsole()->putMessage(
                Console::CONSOLE_MSGTYPE_INFO,
                Console::CONSOLE_SYSTEM_NOTICE,
                _L("Scene loaded with authenticated deterministic input continuation"));
        }
        return true;
    }
    catch (...)
    {
        this->FailPendingDeterministicActorInputSavegame(
            "allocation or publication exception");
        return false;
    }
}

void ActorManager::SetSimulationPaused(bool paused)
{
    this->SyncWithSimThread();
    if (!paused &&
        m_deterministic_actor_input_pending_savegame != nullptr)
    {
        if (!this->TryActivateDeterministicActorInputSavegame() ||
            m_deterministic_actor_input_pending_savegame != nullptr)
        {
            m_simulation_paused = true;
            return;
        }
    }

    if (m_deterministic_actor_input != nullptr)
    {
        DeterministicInputTrace::Runtime& trace =
            m_deterministic_actor_input->trace;
        const DeterministicInputTrace::RuntimeLifecycle lifecycle =
            trace.GetLifecycle();
        const bool transition_ok =
            paused
                ? lifecycle !=
                        DeterministicInputTrace::RuntimeLifecycle::RUNNING ||
                    trace.Pause()
                : lifecycle !=
                        DeterministicInputTrace::RuntimeLifecycle::PAUSED ||
                    trace.Resume();
        if (!transition_ok)
        {
            const bool replay = trace.GetMode() ==
                DeterministicInputTrace::RuntimeMode::REPLAY;
            this->FinishDeterministicActorInput(
                paused ? "simulation pause failed" :
                    "simulation resume failed",
                true,
                replay);
            m_simulation_paused = true;
            return;
        }
    }
    m_simulation_paused = paused;
}

bool ActorManager::ProcessDeterministicActorInputStep()
{
    // The state trace may only inherit input authority from an input frame
    // accepted for this exact fixed step. Clear the prior step before any
    // configuration, source, or sink path can return.
    m_deterministic_input_step_digest.fill(0U);
    m_deterministic_input_step_digest_physics_step =
        m_completed_physics_steps;
    m_deterministic_input_step_digest_valid = false;

    const std::string configured_mode =
        App::sim_deterministic_input_mode->getStr();
    DeterministicInputTrace::RuntimeMode requested_mode =
        DeterministicInputTrace::RuntimeMode::NONE;
    if (!IsDeterministicInputMode(configured_mode, requested_mode))
    {
        if (!m_deterministic_actor_input_suppressed)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing input runtime: "
                "sim_deterministic_input_mode must be exactly off, record, "
                "or replay");
            this->FinishDeterministicActorInput(
                "invalid mode",
                true,
                false);
        }
        return true;
    }

    if (requested_mode == DeterministicInputTrace::RuntimeMode::NONE)
    {
        if (m_deterministic_actor_input != nullptr)
        {
            this->FinishDeterministicActorInput(
                "mode disabled",
                false,
                false);
        }
        m_deterministic_actor_input_suppressed = false;
        m_deterministic_actor_input_stop_replay = false;
        return true;
    }
    if (m_deterministic_actor_input_suppressed)
        return !m_deterministic_actor_input_stop_replay;

    if (m_fixed_step_capture_owner != nullptr)
    {
        RoR::LogFormat(
            "[RoR|Determinism] Refusing input runtime while an exact-step "
            "capture owner controls ActorManager scheduling");
        this->FinishDeterministicActorInput(
            "fixed-step capture ownership conflict",
            true,
            requested_mode == DeterministicInputTrace::RuntimeMode::REPLAY);
        if (requested_mode == DeterministicInputTrace::RuntimeMode::REPLAY)
            m_deterministic_actor_input_pause_requested.store(
                true,
                std::memory_order_release);
        return requested_mode != DeterministicInputTrace::RuntimeMode::REPLAY;
    }

    std::uint64_t scenario_id = 0;
    std::uint64_t target_id = 0;
    std::uint64_t step_limit = 0;
    const std::string configured_path =
        App::sim_deterministic_input_path->getStr();
    if (!ParseDecimalUint64(
            App::sim_deterministic_input_scenario_id->getStr(),
            scenario_id) ||
        !ParseDecimalUint64(
            App::sim_deterministic_input_target_id->getStr(),
            target_id) ||
        target_id == 0 ||
        !ParseDecimalUint64(
            App::sim_deterministic_input_step_limit->getStr(),
            step_limit) ||
        step_limit == 0 ||
        step_limit > MAX_LIVE_INPUT_STEPS ||
        App::app_async_physics->getBool() ||
        (requested_mode == DeterministicInputTrace::RuntimeMode::RECORD &&
            !configured_path.empty()) ||
        (requested_mode == DeterministicInputTrace::RuntimeMode::REPLAY &&
            configured_path.empty()))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Refusing input runtime: scenario ID must be "
            "uint64, target ID must be nonzero uint64, step limit must be "
            "in [1,%llu], app_async_physics must be false, record path "
            "must be empty, and replay path must be nonempty",
            static_cast<unsigned long long>(MAX_LIVE_INPUT_STEPS));
        this->FinishDeterministicActorInput(
            "invalid input configuration",
            true,
            requested_mode == DeterministicInputTrace::RuntimeMode::REPLAY);
        if (requested_mode == DeterministicInputTrace::RuntimeMode::REPLAY)
            m_deterministic_actor_input_pause_requested.store(
                true,
                std::memory_order_release);
        return requested_mode != DeterministicInputTrace::RuntimeMode::REPLAY;
    }

    if (m_deterministic_actor_input != nullptr)
    {
        const DeterministicActorInputRuntime& runtime =
            *m_deterministic_actor_input;
        if (runtime.configured_mode != configured_mode ||
            runtime.scenario_id != scenario_id ||
            runtime.target_id != target_id ||
            runtime.step_limit != step_limit ||
            runtime.replay_path != configured_path)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing to change input identity, mode, "
                "path, or limits while active; select mode=off first");
            const bool replay = runtime.trace.GetMode() ==
                DeterministicInputTrace::RuntimeMode::REPLAY;
            this->FinishDeterministicActorInput(
                "configuration changed while active",
                true,
                replay);
            if (replay)
                m_deterministic_actor_input_pause_requested.store(
                    true,
                    std::memory_order_release);
            return !replay;
        }
    }
    else
    {
        try
        {
            ActorPtr actor = App::GetGameContext() != nullptr ?
                App::GetGameContext()->GetPlayerActor() : ActorPtr();
            DeterministicVehicleInputActorAdapter::PolicySnapshot policy;
            DeterministicVehicleInputActorAdapter::Status policy_status;
            if (!BuildActorInputPolicy(actor, target_id, policy) ||
                !DeterministicVehicleInputActorAdapter::ValidatePolicy(
                    policy,
                    policy_status))
            {
                RoR::LogFormat(
                    "[RoR|Determinism] Refusing input runtime: live Actor "
                    "is outside the authenticated manual-truck policy (%s)",
                    DeterministicVehicleInputActorAdapter::ToString(
                        policy_status.error));
                this->FinishDeterministicActorInput(
                    "actor policy rejected",
                    true,
                    requested_mode ==
                        DeterministicInputTrace::RuntimeMode::REPLAY);
                if (requested_mode ==
                    DeterministicInputTrace::RuntimeMode::REPLAY)
                {
                    m_deterministic_actor_input_pause_requested.store(
                        true,
                        std::memory_order_release);
                    return false;
                }
                return true;
            }

            DeterministicInputTrace::Metadata metadata;
            if (!BuildActorInputMetadata(
                    actor,
                    policy,
                    scenario_id,
                    m_completed_physics_steps,
                    metadata))
            {
                throw std::runtime_error(
                    "could not build bounded actor input identity");
            }

            std::unique_ptr<DeterministicActorInputRuntime> runtime(
                new DeterministicActorInputRuntime());
            runtime->actor = actor;
            runtime->policy = policy;
            runtime->configured_mode = configured_mode;
            runtime->scenario_id = scenario_id;
            runtime->target_id = target_id;
            runtime->step_limit = step_limit;
            runtime->replay_path = configured_path;
            const DeterministicInputTrace::Limits limits =
                BuildActorInputLimits(step_limit);

            if (requested_mode ==
                DeterministicInputTrace::RuntimeMode::RECORD)
            {
                if (!runtime->trace.BeginRecording(metadata, limits) ||
                    !OpenUniqueDeterministicInputTrace(
                        *runtime,
                        m_completed_physics_steps))
                {
                    throw std::runtime_error(
                        "recording initialization or unique output failed");
                }
                runtime->recording_source.reset(
                    new DeterministicVehicleInput::RecordingSource(
                        runtime->trace,
                        target_id,
                        *runtime));
                RoR::LogFormat(
                    "[RoR|Determinism] Recording authenticated input '%s' "
                    "(scenario=%llu, target=%llu, first-step=%llu, "
                    "limit=%llu, vehicle='%s', fixed-gear=%d)",
                    runtime->output_path.c_str(),
                    static_cast<unsigned long long>(scenario_id),
                    static_cast<unsigned long long>(target_id),
                    static_cast<unsigned long long>(
                        m_completed_physics_steps),
                    static_cast<unsigned long long>(step_limit),
                    actor->ar_filename.c_str(),
                    policy.fixed_gear);
            }
            else
            {
                std::string bytes;
                if (!ReadBoundedDeterministicInputTrace(
                        configured_path,
                        bytes) ||
                    !runtime->trace.BeginReplay(bytes, metadata, limits) ||
                    runtime->trace.GetLifecycle() !=
                        DeterministicInputTrace::RuntimeLifecycle::RUNNING)
                {
                    throw std::runtime_error(
                        "replay read, authentication, or identity failed");
                }
                runtime->replay_sink.reset(
                    new DeterministicVehicleInput::ReplaySink(
                        runtime->trace,
                        target_id,
                        *runtime));
                RoR::LogFormat(
                    "[RoR|Determinism] Replaying authenticated input '%s' "
                    "(scenario=%llu, target=%llu, first-step=%llu, "
                    "step-ceiling=%llu, digest=%s)",
                    configured_path.c_str(),
                    static_cast<unsigned long long>(scenario_id),
                    static_cast<unsigned long long>(target_id),
                    static_cast<unsigned long long>(
                        m_completed_physics_steps),
                    static_cast<unsigned long long>(
                        step_limit),
                    runtime->trace.GetTraceDigest().ToHex().c_str());
            }
            m_deterministic_actor_input = std::move(runtime);
        }
        catch (const std::exception& error)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Input runtime initialization failed: "
                "%s",
                error.what());
            this->FinishDeterministicActorInput(
                "initialization failed",
                true,
                requested_mode ==
                    DeterministicInputTrace::RuntimeMode::REPLAY);
            if (requested_mode ==
                DeterministicInputTrace::RuntimeMode::REPLAY)
            {
                m_deterministic_actor_input_pause_requested.store(
                    true,
                    std::memory_order_release);
                return false;
            }
            return true;
        }
        catch (...)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Input runtime initialization failed "
                "with an unknown exception");
            this->FinishDeterministicActorInput(
                "initialization exception",
                true,
                requested_mode ==
                    DeterministicInputTrace::RuntimeMode::REPLAY);
            if (requested_mode ==
                DeterministicInputTrace::RuntimeMode::REPLAY)
            {
                m_deterministic_actor_input_pause_requested.store(
                    true,
                    std::memory_order_release);
                return false;
            }
            return true;
        }
    }

    DeterministicActorInputRuntime& runtime =
        *m_deterministic_actor_input;
    if (App::GetGameContext() == nullptr ||
        App::GetGameContext()->GetPlayerActor() != runtime.actor)
    {
        const bool replay = runtime.trace.GetMode() ==
            DeterministicInputTrace::RuntimeMode::REPLAY;
        RoR::LogFormat(
            "[RoR|Determinism] Input target ownership changed at fixed "
            "step %llu",
            static_cast<unsigned long long>(m_completed_physics_steps));
        this->FinishDeterministicActorInput(
            "player Actor changed",
            true,
            replay);
        if (replay)
            m_deterministic_actor_input_pause_requested.store(
                true,
                std::memory_order_release);
        return !replay;
    }

    bool advanced = false;
    if (runtime.trace.GetMode() ==
        DeterministicInputTrace::RuntimeMode::RECORD)
    {
        advanced = runtime.trace.RecordFixedStep(
            m_completed_physics_steps,
            *runtime.recording_source);
    }
    else
    {
        advanced = runtime.trace.ReplayFixedStep(
            m_completed_physics_steps,
            *runtime.replay_sink);
    }
    if (!advanced)
    {
        const bool replay = runtime.trace.GetMode() ==
            DeterministicInputTrace::RuntimeMode::REPLAY;
        const DeterministicInputTrace::RuntimeStatus status =
            runtime.trace.GetStatus();
        RoR::LogFormat(
            "[RoR|Determinism] Input %s failed at fixed step %llu "
                    "(%s, trace=%s, adapter=%s, processed=%llu)",
            replay ? "replay" : "recording",
            static_cast<unsigned long long>(m_completed_physics_steps),
            DeterministicInputTrace::ToString(status.error),
            DeterministicInputTrace::ToString(
                status.trace_status.error),
            DeterministicVehicleInputActorAdapter::ToString(
                runtime.adapter_error),
            static_cast<unsigned long long>(status.processed_steps));
        this->FinishDeterministicActorInput(
            "fixed-step transaction failed",
            true,
            replay);
        if (replay)
            m_deterministic_actor_input_pause_requested.store(
                true,
                std::memory_order_release);
        return !replay;
    }

    // Copy the frame-chain prefix before a configured limit or replay
    // exhaustion tears down the runtime. Both record and replay publish this
    // value only after their source/sink transaction succeeds.
    m_deterministic_input_step_digest =
        runtime.trace.GetProcessedPrefixDigest().bytes;
    m_deterministic_input_step_digest_valid = true;

    if (runtime.trace.GetMode() ==
            DeterministicInputTrace::RuntimeMode::RECORD &&
        runtime.trace.GetProcessedStepCount() == runtime.step_limit)
    {
        this->FinishDeterministicActorInput(
            "configured step limit reached",
            true,
            false);
    }
    else if (runtime.trace.GetMode() ==
            DeterministicInputTrace::RuntimeMode::REPLAY &&
        runtime.trace.GetLifecycle() ==
            DeterministicInputTrace::RuntimeLifecycle::FINISHED)
    {
        this->FinishDeterministicActorInput(
            "authenticated stream exhausted",
            true,
            true);
        m_deterministic_actor_input_pause_requested.store(
            true,
            std::memory_order_release);
    }
    return true;
}

bool ActorManager::PrepareDeterministicStateTraceStep()
{
    if (!App::sim_deterministic_state_trace->getBool())
    {
        if (m_deterministic_state_trace != nullptr)
        {
            this->FinishDeterministicStateTrace(
                "capture disabled",
                false);
        }
        m_deterministic_state_trace_suppressed = false;
        return false;
    }

    if (m_deterministic_state_trace_suppressed)
        return false;

    std::uint64_t scenario_id = 0;
    if (!ParseDecimalUint64(
            App::sim_deterministic_state_trace_scenario_id->getStr(),
            scenario_id))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Refusing state trace: "
            "sim_deterministic_state_trace_scenario_id must be an "
            "unsigned decimal 64-bit integer; disable capture before "
            "correcting it");
        this->FinishDeterministicStateTrace(
            "invalid scenario ID",
            true);
        return false;
    }

    std::uint64_t step_limit = 0;
    if (!DeterministicScenarioSchedule::TryParseTraceStepLimit(
            App::sim_deterministic_state_trace_step_limit->getStr(),
            DeterministicStateTrace::MAX_TRACE_STEPS,
            step_limit))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Refusing state trace: "
            "sim_deterministic_state_trace_step_limit must be an "
            "unsigned decimal integer in [0, %llu], where zero uses "
            "the format maximum; disable capture before correcting it",
            static_cast<unsigned long long>(
                DeterministicStateTrace::MAX_TRACE_STEPS));
        this->FinishDeterministicStateTrace(
            "invalid trace step limit",
            true);
        return false;
    }

    if (m_deterministic_state_trace != nullptr)
    {
        if (m_deterministic_state_trace->scenario_id != scenario_id)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing to change scenario ID "
                "inside an active state trace; disable capture before "
                "changing sim_deterministic_state_trace_scenario_id");
            this->FinishDeterministicStateTrace(
                "scenario ID changed while active",
                true);
            return false;
        }
        if (m_deterministic_state_trace->step_limit != step_limit)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing to change the step limit "
                "inside an active state trace; disable capture before "
                "changing sim_deterministic_state_trace_step_limit");
            this->FinishDeterministicStateTrace(
                "trace step limit changed while active",
                true);
            return false;
        }
        m_deterministic_state_trace->contact_keys.clear();
        return true;
    }

    try
    {
        std::unique_ptr<DeterministicStateTraceRuntime> runtime(
            new DeterministicStateTraceRuntime());
        runtime->scenario_id = scenario_id;
        runtime->step_limit = step_limit;

        ThreadPool* const worker_pool = App::GetThreadPool();
        const std::size_t worker_count =
            worker_pool != nullptr
                ? worker_pool->m_threads.size()
                : 0;
        if (worker_count == 0 ||
                worker_count >
                    DeterministicStateTrace::MAX_WORKERS)
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing state trace: actual "
                "physics worker count %llu is outside [1, %u]",
                static_cast<unsigned long long>(worker_count),
                DeterministicStateTrace::MAX_WORKERS);
            m_deterministic_state_trace = std::move(runtime);
            this->FinishDeterministicStateTrace(
                "invalid worker count",
                true);
            return false;
        }

        if (!OpenUniqueDeterministicTrace(
                *runtime,
                scenario_id,
                m_completed_physics_steps))
        {
            RoR::LogFormat(
                "[RoR|Determinism] Refusing state trace: could not "
                "reserve a new trace file under log directory '%s'; "
                "check directory permissions and free space",
                App::sys_logs_dir->getStr().c_str());
            m_deterministic_state_trace = std::move(runtime);
            this->FinishDeterministicStateTrace(
                "trace output could not be opened",
                true);
            return false;
        }

        DeterministicStateTrace::Metadata metadata;
        metadata.worker_count =
            static_cast<std::uint32_t>(worker_count);
        metadata.scenario_id = scenario_id;
        metadata.first_physics_step =
            m_completed_physics_steps;
        metadata.physics_step_numerator = 1;
        metadata.physics_step_denominator = 2000;
        metadata.physics_flags =
            GetDeterministicTracePhysicsFlags();

        runtime->writer.reset(
            new DeterministicStateTrace::Writer(
                runtime->output,
                metadata));
        if (!runtime->writer->IsReady())
        {
            const DeterministicStateTrace::Status& status =
                runtime->writer->GetStatus();
            RoR::LogFormat(
                "[RoR|Determinism] Refusing state trace '%s': writer "
                "initialization failed (%s, byte=%llu)",
                runtime->output_path.c_str(),
                DeterministicStateTrace::ToString(status.error),
                static_cast<unsigned long long>(
                    status.byte_offset));
            m_deterministic_state_trace = std::move(runtime);
            this->FinishDeterministicStateTrace(
                "writer initialization failed",
                true);
            return false;
        }

        m_deterministic_state_trace = std::move(runtime);
        m_deterministic_state_trace->contact_keys.reserve(
            DeterministicContactOrder::
                INTER_ACTOR_CONTACT_BUDGET);
        m_deterministic_state_trace->actors.reserve(
            m_actors.size());
        RoR::LogFormat(
            "[RoR|Determinism] Recording state trace '%s' "
            "(scenario=%llu, workers=%llu, step=1/2000 s, "
            "limit=%llu, fast-math=%s)",
            m_deterministic_state_trace->output_path.c_str(),
            static_cast<unsigned long long>(scenario_id),
            static_cast<unsigned long long>(worker_count),
            static_cast<unsigned long long>(step_limit),
            (GetDeterministicTracePhysicsFlags() &
                DeterministicStateTrace::
                    PHYSICS_FLAG_FAST_MATH) != 0
                ? "true"
                : "false");
        return true;
    }
    catch (const std::exception& error)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace initialization failed: %s; "
            "capture is suppressed until it is disabled",
            error.what());
    }
    catch (...)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace initialization failed with "
            "an unknown error; capture is suppressed until it is "
            "disabled");
    }

    this->FinishDeterministicStateTrace(
        "trace initialization exception",
        true);
    return false;
}

void ActorManager::CaptureDeterministicStateTraceStep(
    bool contact_capture_succeeded,
    ContactConservation::Error contact_conservation_error)
{
    if (m_deterministic_state_trace == nullptr ||
            m_deterministic_state_trace->writer == nullptr)
    {
        return;
    }

    DeterministicStateTraceRuntime& runtime =
        *m_deterministic_state_trace;
    if (contact_conservation_error !=
            ContactConservation::Error::NONE)
    {
        runtime.contact_conservation_error =
            contact_conservation_error;
        RoR::LogFormat(
            "[RoR|Determinism] State trace contact-conservation audit "
            "failed at fixed step %llu (%s); physics completed, but "
            "trace capture is being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps),
            ContactConservation::ErrorToString(
                contact_conservation_error));
        this->FinishDeterministicStateTrace(
            "contact-conservation audit failed",
            true);
        return;
    }

    if (!contact_capture_succeeded)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace contact-key capture "
            "exceeded its bounded storage or allocation failed at "
            "fixed step %llu; physics completed, but trace capture is "
            "being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps));
        this->FinishDeterministicStateTrace(
            "contact-key capture failed",
            true);
        return;
    }

    if (m_actors.size() >
            DeterministicStateDigest::MAX_ACTORS)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace actor limit exceeded at "
            "fixed step %llu (actors=%llu); capture is being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps),
            static_cast<unsigned long long>(m_actors.size()));
        this->FinishDeterministicStateTrace(
            "actor limit exceeded",
            true);
        return;
    }

    try
    {
        runtime.actors.clear();
        if (runtime.actors.capacity() < m_actors.size())
            runtime.actors.reserve(m_actors.size());
        for (const ActorPtr& actor : m_actors)
            runtime.actors.push_back(actor.GetRef());
    }
    catch (...)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace actor-list allocation "
            "failed at fixed step %llu; capture is being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps));
        this->FinishDeterministicStateTrace(
            "actor-list capture failed",
            true);
        return;
    }

    if (runtime.actors.size() >
            DeterministicStateDigest::MAX_ACTORS ||
        runtime.contact_keys.size() >
            DeterministicStateDigest::MAX_CONTACTS)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace snapshot limits exceeded "
            "at fixed step %llu (actors=%llu, contacts=%llu); capture "
            "is being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps),
            static_cast<unsigned long long>(runtime.actors.size()),
            static_cast<unsigned long long>(
                runtime.contact_keys.size()));
        this->FinishDeterministicStateTrace(
            "snapshot limits exceeded",
            true);
        return;
    }

    DeterministicStateDigest::Digest digest;
    DeterministicStateDigest::SnapshotStatus snapshot_status;
    if (!DeterministicStateDigest::BuildActorSnapshotDigest(
            m_completed_physics_steps,
            runtime.scenario_id,
            runtime.actors,
            runtime.contact_keys,
            digest,
            &snapshot_status))
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace snapshot failed at fixed "
            "step %llu (%s, digest=%s, source-index=%llu, "
            "record-index=%u); capture is being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps),
            SnapshotErrorName(snapshot_status.error),
            DigestErrorName(snapshot_status.digest_error),
            static_cast<unsigned long long>(
                snapshot_status.source_index),
            snapshot_status.record_index);
        this->FinishDeterministicStateTrace(
            "snapshot digest failed",
            true);
        return;
    }

    DeterministicStateTrace::StepRecord record;
    record.physics_step = m_completed_physics_steps;
    record.actor_count =
        static_cast<std::uint32_t>(runtime.actors.size());
    record.contact_count =
        static_cast<std::uint32_t>(runtime.contact_keys.size());
    record.digest = digest;
    if (m_deterministic_input_step_digest_valid &&
        m_deterministic_input_step_digest_physics_step ==
            m_completed_physics_steps)
    {
        record.input_flags =
            DeterministicStateTrace::STEP_INPUT_AUTHENTICATED_PREFIX;
        record.input_digest = m_deterministic_input_step_digest;
    }
    if (!runtime.writer->Append(record))
    {
        const DeterministicStateTrace::Status& status =
            runtime.writer->GetStatus();
        RoR::LogFormat(
            "[RoR|Determinism] State trace append failed at fixed "
            "step %llu (%s, byte=%llu, step-index=%llu); capture is "
            "being stopped",
            static_cast<unsigned long long>(
                m_completed_physics_steps),
            DeterministicStateTrace::ToString(status.error),
            static_cast<unsigned long long>(status.byte_offset),
            static_cast<unsigned long long>(status.step_index));
        this->FinishDeterministicStateTrace(
            "step append failed",
            true);
        return;
    }

    if (runtime.writer->GetStepCount() ==
            runtime.step_limit)
    {
        RoR::LogFormat(
            "[RoR|Determinism] State trace '%s' reached its configured "
            "%llu-step limit and is being finalized",
            runtime.output_path.c_str(),
            static_cast<unsigned long long>(
                runtime.step_limit));
        this->FinishDeterministicStateTrace(
            "trace step limit reached",
            true);
    }
}

ActorPtr ActorManager::CreateNewActor(ActorSpawnRequest rq, RigDef::DocumentPtr def)
{
    if (def == nullptr)
    {
        RoR::Log("[RoR|JBeam] Rejected actor spawn without a RigDef document");
        return nullptr;
    }

    const bool cache_claims_jbeam =
        rq.asr_cache_entry != nullptr &&
        rq.asr_cache_entry->fext == "jbeam";
    const std::shared_ptr<const
        BeamNG::JBeamVehicleImportAuthorityReceipt> jbeam_authority =
            def->_jbeam_import_authority;
    if (cache_claims_jbeam || jbeam_authority != nullptr)
    {
        const TerrainBundleAuthenticatedArchiveSnapshot* snapshot =
            jbeam_authority != nullptr
                ? jbeam_authority->authenticated_archive_snapshot()
                : nullptr;
        const CacheEntryPtr current_cache_entry =
            rq.asr_cache_entry != nullptr
                ? App::GetCacheSystem()->GetEntryByNumber(
                      rq.asr_cache_entry->number)
                : CacheEntryPtr();
        const bool exact_authority =
            cache_claims_jbeam && jbeam_authority != nullptr &&
            jbeam_authority->initialized() &&
            jbeam_authority->version() ==
                BeamNG::JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION &&
            rq.asr_cache_entry != nullptr &&
            current_cache_entry == rq.asr_cache_entry &&
            !rq.asr_cache_entry->deleted &&
            rq.asr_cache_entry->resource_bundle_type == "Zip" &&
            !rq.asr_cache_entry->resource_group.empty() &&
            rq.asr_cache_entry->beamng_archive_size != 0U &&
            rq.asr_cache_entry->beamng_archive_size <=
                TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES &&
            def->root_module != nullptr &&
            rq.asr_cache_entry->beamng_root_part == def->name &&
            rq.asr_cache_entry->beamng_root_part ==
                jbeam_authority->root_part_name() &&
            rq.asr_cache_entry->beamng_archive_sha256 ==
                jbeam_authority->archive_sha256() &&
            snapshot != nullptr && snapshot->initialized() &&
            snapshot->source_archive_identity() ==
                rq.asr_cache_entry->resource_bundle_path &&
            static_cast<std::uint64_t>(snapshot->size()) ==
                rq.asr_cache_entry->beamng_archive_size &&
            jbeam_authority->Matches(
                rq.asr_cache_entry->resource_group,
                rq.asr_cache_entry->beamng_root_part,
                *snapshot) &&
            App::GetContentManager()->
                IsExactAuthenticatedPackageSnapshotMounted(
                    rq.asr_cache_entry->resource_group,
                    *snapshot);
        if (!exact_authority)
        {
            RoR::LogFormat(
                "[RoR|JBeam] Rejected actor spawn before publication: "
                "the cache entry, root, immutable archive, or mounted "
                "generation is not current (actor='%s')",
                def->name.c_str());
            return nullptr;
        }
    }

    const std::uint64_t prospective_actor_id =
        rq.asr_instance_id == ACTORINSTANCEID_INVALID
            ? static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(m_actor_next_instance_id))
            : static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(rq.asr_instance_id));
    const DeterministicScenarioIdentity::Resolution identity =
        DeterministicScenarioIdentity::Resolve(
            rq.asr_deterministic_scenario_seed,
            rq.asr_deterministic_actor_stream_id,
            prospective_actor_id);
    if (!DeterministicScenarioIdentity::IsValid(identity))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Rejected actor spawn with partial "
            "scenario identity (scenario=%llu, stream=%llu)",
            static_cast<unsigned long long>(
                rq.asr_deterministic_scenario_seed),
            static_cast<unsigned long long>(
                rq.asr_deterministic_actor_stream_id));
        return nullptr;
    }
    if (identity.explicit_identity)
    {
        for (const ActorPtr& existing_actor: m_actors)
        {
            if (existing_actor != nullptr &&
                existing_actor->ar_state != ActorState::DISPOSED &&
                existing_actor->m_deterministic_scenario_seed ==
                    identity.scenario_seed &&
                existing_actor->m_deterministic_actor_stream_id ==
                    identity.actor_stream_id)
            {
                RoR::LogFormat(
                    "[RoR|Determinism] Rejected duplicate live actor "
                    "stream (scenario=%llu, stream=%llu)",
                    static_cast<unsigned long long>(
                        identity.scenario_seed),
                    static_cast<unsigned long long>(
                        identity.actor_stream_id));
                return nullptr;
            }
        }
    }

    // Reserve the ownership slot before constructing/registering anything.
    // Once graphics registration commits, the final ActorPtr append cannot
    // allocate and therefore cannot strand a capture-side raw owner.
    m_actors.reserve(m_actors.size() + 1U);
    if (rq.asr_instance_id == ACTORINSTANCEID_INVALID)
    {
        rq.asr_instance_id = this->GetActorNextInstanceId();
    }
    ActorPtr actor = new Actor(rq.asr_instance_id, static_cast<int>(m_actors.size()), def, rq);

    if (App::mp_state->getEnum<MpState>() == MpState::CONNECTED && rq.asr_origin != ActorSpawnRequest::Origin::NETWORK)
    {
        actor->sendStreamSetup();
    }

    LOG(" == Spawning vehicle: " + def->name);

    ActorSpawner spawner;
    spawner.ConfigureSections(actor->m_section_config, def);
    spawner.ConfigureAddonParts(actor);
    spawner.ConfigureAssetPacks(actor);
    spawner.ProcessNewActor(actor, rq, def);

    if (App::diag_actor_dump->getBool())
    {
        actor->WriteDiagnosticDump(actor->ar_filename + "_dump_raw.txt"); // Saves file to 'logs'
    }

    /* POST-PROCESSING */

    actor->ar_initial_node_positions.resize(actor->ar_num_nodes);
    actor->ar_initial_beam_defaults.resize(actor->ar_num_beams);
    actor->ar_initial_node_masses.resize(actor->ar_num_nodes);

    actor->UpdateBoundingBoxes(); // (records the unrotated dimensions for 'veh_aab_size')

    // Apply spawn position & spawn rotation
    for (int i = 0; i < actor->ar_num_nodes; i++)
    {
        actor->ar_nodes[i].AbsPosition = rq.asr_position + rq.asr_rotation * (actor->ar_nodes[i].AbsPosition - rq.asr_position);
        actor->ar_nodes[i].RelPosition = actor->ar_nodes[i].AbsPosition - actor->ar_origin;
    };

    /* Place correctly */
    if (spawner.GetMemoryRequirements().num_fixes == 0)
    {
        Ogre::Vector3 vehicle_position = rq.asr_position;

        // check if over-sized
        actor->UpdateBoundingBoxes();
        vehicle_position.x += vehicle_position.x - actor->ar_bounding_box.getCenter().x;
        vehicle_position.z += vehicle_position.z - actor->ar_bounding_box.getCenter().z;

        float miny = 0.0f;

        if (!actor->m_preloaded_with_terrain)
        {
            miny = vehicle_position.y;
        }

        if (rq.asr_spawnbox != nullptr)
        {
            miny = rq.asr_spawnbox->relo.y + rq.asr_spawnbox->center.y;
        }

        if (rq.asr_free_position)
            actor->resetPosition(vehicle_position, true);
        else
            actor->resetPosition(vehicle_position.x, vehicle_position.z, true, miny);

        if (rq.asr_spawnbox != nullptr)
        {
            bool inside = true;

            for (int i = 0; i < actor->ar_num_nodes; i++)
                inside = (inside && App::GetGameContext()->GetTerrain()->GetCollisions()->isInside(actor->ar_nodes[i].AbsPosition, rq.asr_spawnbox, 0.2f));

            if (!inside)
            {
                Vector3 gpos = Vector3(vehicle_position.x, 0.0f, vehicle_position.z);

                gpos -= rq.asr_rotation * Vector3((rq.asr_spawnbox->hi.x - rq.asr_spawnbox->lo.x + actor->ar_bounding_box.getMaximum().x - actor->ar_bounding_box.getMinimum().x) * 0.6f, 0.0f, 0.0f);

                actor->resetPosition(gpos.x, gpos.z, true, miny);
            }
        }
    }
    else
    {
        actor->resetPosition(rq.asr_position, true);
    }
    actor->UpdateBoundingBoxes();

    //compute final mass
    actor->recalculateNodeMasses();
    actor->ar_initial_total_mass = actor->ar_total_mass;
    actor->ar_original_dry_mass = actor->ar_dry_mass;
    actor->ar_original_load_mass = actor->ar_load_mass;
    actor->ar_orig_minimass = actor->ar_minimass;
    for (int i = 0; i < actor->ar_num_nodes; i++)
    {
        actor->ar_initial_node_masses[i] = actor->ar_nodes[i].mass;
    }

    //setup default sounds
    if (!actor->m_disable_default_sounds)
    {
        ActorSpawner::SetupDefaultSoundSources(actor);
    }

    //compute node connectivity graph
    actor->calcNodeConnectivityGraph();

    actor->UpdateBoundingBoxes();
    actor->calculateAveragePosition();

    // calculate minimum camera radius
    actor->calculateAveragePosition();
    for (int i = 0; i < actor->ar_num_nodes; i++)
    {
        Real dist = actor->ar_nodes[i].AbsPosition.squaredDistance(actor->m_avg_node_position);
        if (dist > actor->m_min_camera_radius)
        {
            actor->m_min_camera_radius = dist;
        }
    }
    actor->m_min_camera_radius = std::sqrt(actor->m_min_camera_radius) * 1.2f; // twenty percent buffer

    // fix up submesh collision model
    std::string subMeshGroundModelName = spawner.GetSubmeshGroundmodelName();
    if (!subMeshGroundModelName.empty())
    {
        actor->ar_submesh_ground_model = App::GetGameContext()->GetTerrain()->GetCollisions()->getGroundModelByString(subMeshGroundModelName);
        if (!actor->ar_submesh_ground_model)
        {
            actor->ar_submesh_ground_model = App::GetGameContext()->GetTerrain()->GetCollisions()->defaultgm;
        }
    }

    // Set beam defaults
    for (int i = 0; i < actor->ar_num_beams; i++)
    {
        actor->ar_beams[i].initial_beam_strength       = actor->ar_beams[i].strength;
        actor->ar_beams[i].default_beam_deform         = actor->ar_beams[i].minmaxposnegstress;
        actor->ar_initial_beam_defaults[i]             = std::make_pair(actor->ar_beams[i].k, actor->ar_beams[i].d);
    }

    actor->m_spawn_rotation = actor->getRotation();

    TRIGGER_EVENT_ASYNC(SE_GENERIC_NEW_TRUCK, actor->ar_instance_id);

    actor->NotifyActorCameraChanged(); // setup sounds properly

    // calculate the number of wheel nodes
    actor->m_wheel_node_count = 0;
    for (int i = 0; i < actor->ar_num_nodes; i++)
    {
        if (actor->ar_nodes[i].nd_tyre_node)
            actor->m_wheel_node_count++;
    }

    // search m_net_first_wheel_node
    actor->m_net_first_wheel_node = actor->ar_num_nodes;
    for (int i = 0; i < actor->ar_num_nodes; i++)
    {
        if (actor->ar_nodes[i].nd_tyre_node || actor->ar_nodes[i].nd_rim_node)
        {
            actor->m_net_first_wheel_node = i;
            break;
        }
    }

    // Initialize visuals
    actor->updateVisual();
    actor->GetGfxActor()->SetDebugView((DebugViewType)rq.asr_debugview);

    // perform full visual update only if the vehicle won't be immediately driven by player.
    if (actor->isPreloadedWithTerrain() ||                         // .tobj file - Spawned sleeping somewhere on terrain
        rq.asr_origin == ActorSpawnRequest::Origin::CONFIG_FILE || // RoR.cfg or commandline - not entered by default
        actor->ar_num_cinecams == 0)                               // Not intended for player-controlling
    {
        actor->GetGfxActor()->UpdateSimDataBuffer(); // Initial fill of sim data buffers

        actor->GetGfxActor()->UpdateFlexbodies(); // Push tasks to threadpool
        actor->GetGfxActor()->UpdateWheelVisuals(); // Push tasks to threadpool
        actor->GetGfxActor()->UpdateCabMesh();
        actor->GetGfxActor()->UpdateWingMeshes();
        actor->GetGfxActor()->UpdateProps(0.f, false);
        actor->GetGfxActor()->UpdateRods(); // beam visuals
        actor->GetGfxActor()->FinishWheelUpdates(); // Sync tasks from threadpool
        actor->GetGfxActor()->FinishFlexbodyTasks(); // Sync tasks from threadpool
    }

    if (actor->ar_engine)
    {
        if (!actor->m_preloaded_with_terrain && App::sim_spawn_running->getBool())
            actor->ar_engine->startEngine();
        else
            actor->ar_engine->offStart();
    }
    // pressurize tires
    if (actor->getTyrePressure().IsEnabled())
    {
        actor->getTyrePressure().ModifyTyrePressure(0.f); // Initialize springiness of pressure-beams.
    }

    actor->ar_state = ActorState::LOCAL_SLEEPING;

    if (App::mp_state->getEnum<MpState>() == RoR::MpState::CONNECTED)
    {
        // network buffer layout (without RoRnet::VehicleState):
        // -----------------------------------------------------

        //  - 3 floats (x,y,z) for the reference node 0
        //  - ar_num_nodes - 1 times 3 short ints (compressed position info)
        actor->m_net_node_buf_size = sizeof(float) * 3 + (actor->m_net_first_wheel_node - 1) * sizeof(short int) * 3;
        actor->m_net_total_buffer_size += actor->m_net_node_buf_size;
        //  - ar_num_wheels times a float for the wheel rotation
        actor->m_net_wheel_buf_size = actor->ar_num_wheels * sizeof(float);
        actor->m_net_total_buffer_size += actor->m_net_wheel_buf_size;
        //  - bit array (made of ints) for the prop animation key states
        actor->m_net_propanimkey_buf_size = 
            (actor->m_prop_anim_key_states.size() / 8) + // whole chars
            (size_t)(actor->m_prop_anim_key_states.size() % 8 != 0); // remainder: 0 or 1 chars
        actor->m_net_total_buffer_size += actor->m_net_propanimkey_buf_size;

        if (rq.asr_origin == ActorSpawnRequest::Origin::NETWORK)
        {
            actor->ar_state = ActorState::NETWORKED_OK;
            if (actor->ar_engine)
            {
                actor->ar_engine->startEngine();
            }
        }

        actor->m_net_username = rq.asr_net_username;
        actor->m_net_color_num = rq.asr_net_color;
    }
    else if (App::sim_replay_enabled->getBool())
    {
        actor->m_replay_handler = new Replay(actor, App::sim_replay_length->getInt());
    }

    //cache buoyancy nodes (must be done when position is final)
    if (actor->m_buoyance)
    {
        for (int i = 0; i < actor->ar_num_buoycabs; i++)
        {
            int tmpv = actor->ar_buoycabs[i] * 3;
            actor->ar_cabs_buoy_cache_ids[tmpv] = actor->m_buoyance->cacheBuoycabNode(&actor->ar_nodes[actor->ar_cabs[tmpv]]);
            actor->ar_cabs_buoy_cache_ids[tmpv+1] = actor->m_buoyance->cacheBuoycabNode(&actor->ar_nodes[actor->ar_cabs[tmpv + 1]]);
            actor->ar_cabs_buoy_cache_ids[tmpv+2] = actor->m_buoyance->cacheBuoycabNode(&actor->ar_nodes[actor->ar_cabs[tmpv + 2]]);
        }
        actor->m_buoyance->buoy_projected_nodes = actor->m_buoyance->buoy_cached_nodes;
    }

    LOG(" ===== DONE LOADING VEHICLE");

    if (App::diag_actor_dump->getBool())
    {
        actor->WriteDiagnosticDump(actor->ar_filename + "_dump_recalc.txt"); // Saves file to 'logs'
    }

    try
    {
        if (!App::GetGfxScene()->RegisterGfxActor(actor->GetGfxActor()))
        {
            throw std::runtime_error(
                "failed to register durable GfxActor capture identity");
        }
    }
    catch (...)
    {
        // Registration itself is strongly transactional. Dispose the still
        // unowned Actor so its destructor contract remains satisfied.
        actor->dispose();
        throw;
    }
    m_actors.push_back(ActorPtr(actor));

    return actor;
}

void ActorManager::RemoveStreamSource(int sourceid)
{
    m_stream_mismatches.erase(sourceid);
    RoR::EraseIf(m_stream_mismatched_regs,
        [sourceid](const RoRnet::ActorStreamRegister& reg)
        {
            return reg.origin_sourceid == sourceid;
        });

    for (ActorPtr& actor : m_actors)
    {
        if (actor->ar_state != ActorState::NETWORKED_OK)
            continue;

        if (actor->ar_net_source_id == sourceid)
        {
            App::GetGameContext()->PushMessage(Message(MSG_SIM_DELETE_ACTOR_REQUESTED, static_cast<void*>(new ActorPtr(actor))));
        }
    }
}

void ActorManager::RemoveStream(int sourceid, int streamid)
{
    // Delete associated actor
    ActorPtr b = this->GetActorByNetworkLinks(sourceid, streamid);
    if (b)
    {
        if (b->ar_state == ActorState::NETWORKED_OK || b->ar_state == ActorState::NETWORKED_HIDDEN)
        {
            App::GetGameContext()->PushMessage(Message(MSG_SIM_DELETE_ACTOR_REQUESTED, static_cast<void*>(new ActorPtr(b))));
        }
    }

    // Erase stream mismatch records
    m_stream_mismatches[sourceid].erase(streamid);
    RoR::EraseIf(m_stream_mismatched_regs,
        [sourceid, streamid](const RoRnet::ActorStreamRegister& reg)
        {
            return reg.origin_sourceid == sourceid &&
                    reg.origin_streamid == streamid;
        });
}

void ActorManager::RetryFailedStreamRegistrations(ScriptEventArgs* args)
{
    ROR_ASSERT(args->type == SE_GENERIC_MODCACHE_ACTIVITY);
    ROR_ASSERT(args->arg1 == modCacheActivityType::MODCACHEACTIVITY_ENTRY_ADDED);

    std::string filename = args->arg5ex;
    CacheEntryPtr entry = App::GetCacheSystem()->GetEntryByNumber(args->arg2ex);
    ROR_ASSERT(entry);

    for (auto it = m_stream_mismatched_regs.begin(); it != m_stream_mismatched_regs.end(); )
    {
        RoRnet::ActorStreamRegister reg = *it;
        std::string reg_filename_maybe_bundlequalified = SanitizeUtf8CString(reg.name);
        std::string reg_filename;
        std::string reg_bundlename;
        SplitBundleQualifiedFilename(reg_filename_maybe_bundlequalified, /*out:*/ reg_bundlename, /*out:*/ reg_filename);
        if (reg_filename == filename)
        {
            RoR::LogFormat("[RoR] Retrying STREAM_REGISTER for user id %d, stream id %d, filename '%s'",
                reg.origin_sourceid, reg.origin_streamid, reg_filename_maybe_bundlequalified.c_str());
            m_stream_mismatches[reg.origin_sourceid].erase(reg.origin_streamid);
            it = m_stream_mismatched_regs.erase(it);

            // Gather info needed to spawn
            RoRnet::UserInfo info;
            BitMask_t peeropts = BitMask_t(0);
            if (!App::GetNetwork()->GetUserInfo(reg.origin_sourceid, info)
                || !App::GetNetwork()->GetUserPeerOpts(reg.origin_sourceid, peeropts))
            {
                RoR::LogFormat("[RoR] Error retrying STREAM_REGISTER, user id %d does not exist", reg.origin_sourceid);
            }
            else
            {
                this->RequestSpawnRemoteActor(&reg, entry, info, peeropts);
            }
        }
        else
        {
            ++it;
        }
    }
}

#ifdef USE_SOCKETW
void ActorManager::HandleActorStreamData(std::vector<RoR::NetRecvPacket> packet_buffer)
{
    // Sort by stream source
    std::stable_sort(packet_buffer.begin(), packet_buffer.end(),
            [](const RoR::NetRecvPacket& a, const RoR::NetRecvPacket& b)
            { return a.header.source > b.header.source; });
    // Compress data stream by eliminating all but the last update from every consecutive group of stream data updates
    auto it = std::unique(packet_buffer.rbegin(), packet_buffer.rend(),
            [](const RoR::NetRecvPacket& a, const RoR::NetRecvPacket& b)
            { return !memcmp(&a.header, &b.header, sizeof(RoRnet::Header)) &&
            a.header.command == RoRnet::MSG2_STREAM_DATA; });
    packet_buffer.erase(packet_buffer.begin(), it.base());
    for (auto& packet : packet_buffer)
    {
        if (packet.header.command == RoRnet::MSG2_STREAM_REGISTER)
        {
            RoRnet::StreamRegister* reg = (RoRnet::StreamRegister *)packet.buffer;
            if (reg->type == 0)
            {
                reg->name[127] = 0;
                // NOTE: The filename is by default in "Bundle-qualified" format, i.e. "mybundle.zip:myactor.truck"
                std::string filename_maybe_bundlequalified = SanitizeUtf8CString(reg->name);
                std::string filename;
                std::string bundlename;
                SplitBundleQualifiedFilename(filename_maybe_bundlequalified, /*out:*/ bundlename, /*out:*/ filename);

                RoRnet::UserInfo info;
                BitMask_t peeropts = BitMask_t(0);
                if (!App::GetNetwork()->GetUserInfo(reg->origin_sourceid, info)
                    || !App::GetNetwork()->GetUserPeerOpts(reg->origin_sourceid, peeropts))
                {
                    RoR::LogFormat("[RoR] Invalid STREAM_REGISTER, user id %d does not exist", reg->origin_sourceid);
                    reg->status = -1;
                }
                else if (filename.empty())
                {
                    RoR::LogFormat("[RoR] Invalid STREAM_REGISTER (user '%s', ID %d), filename is empty string", info.username, reg->origin_sourceid);
                    reg->status = -1;
                }
                else
                {
                    auto actor_reg = reinterpret_cast<RoRnet::ActorStreamRegister*>(reg);
                    Str<200> text;
                    text << _L("spawned a new vehicle: ") << filename;
                    App::GetConsole()->putNetMessage(
                        reg->origin_sourceid, Console::CONSOLE_SYSTEM_NOTICE, text.ToCStr());

                    LOG("[RoR] Creating remote actor for " + TOSTRING(reg->origin_sourceid) + ":" + TOSTRING(reg->origin_streamid));

                    // Based on negative user feedback we don't check the bundle in multiplayer.
                    CacheEntryPtr actor_entry = App::GetCacheSystem()->FindEntryByFilename(LT_AllBeam, /*partial:*/false, filename);

                    if (!actor_entry)
                    {
                        App::GetConsole()->putMessage(
                            Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
                            _L("Mod not installed: ") + filename);
                        RoR::LogFormat("[RoR] Cannot create remote actor (not installed), filename: '%s'", filename_maybe_bundlequalified.c_str());
                        this->AddStreamMismatch(actor_reg);
                        reg->status = -1;
                    }
                    else
                    {
                        RoR::LogFormat("[RoR] Creating remote actor (user id %d, stream id %d) with filename '%s'",
                            reg->origin_sourceid, reg->origin_streamid, filename_maybe_bundlequalified.c_str());
                        this->RequestSpawnRemoteActor(actor_reg, actor_entry, info, peeropts);
                        reg->status = 1; // success
                    }
                }

                App::GetNetwork()->AddPacket(reg->origin_streamid, RoRnet::MSG2_STREAM_REGISTER_RESULT, sizeof(RoRnet::StreamRegister), (char *)reg);
            }
        }
        else if (packet.header.command == RoRnet::MSG2_STREAM_REGISTER_RESULT)
        {
            RoRnet::StreamRegister* reg = (RoRnet::StreamRegister *)packet.buffer;
            for (ActorPtr& actor: m_actors)
            {
                if (actor->ar_net_source_id == reg->origin_sourceid && actor->ar_net_stream_id == reg->origin_streamid)
                {
                    int sourceid = packet.header.source;
                    actor->ar_net_stream_results[sourceid] = reg->status;

                    String message = "";
                    switch (reg->status)
                    {
                        case  1: message = "successfully loaded stream"; break;
                        case -2: message = "detected mismatch stream"; break;
                        default: message = "could not load stream"; break;
                    }
                    LOG("Client " + TOSTRING(sourceid) + " " + message + " " + TOSTRING(reg->origin_streamid) +
                            " with name '" + reg->name + "', result code: " + TOSTRING(reg->status));
                    break;
                }
            }
        }
        else if (packet.header.command == RoRnet::MSG2_STREAM_UNREGISTER)
        {
            this->RemoveStream(packet.header.source, packet.header.streamid);
        }
        else if (packet.header.command == RoRnet::MSG2_USER_LEAVE)
        {
            this->RemoveStreamSource(packet.header.source);
        }
        else if (packet.header.command == RoRnet::MSG2_STREAM_DATA)
        {
            for (ActorPtr& actor: m_actors)
            {
                if (actor->ar_state != ActorState::NETWORKED_OK)
                    continue;
                if (packet.header.source == actor->ar_net_source_id && packet.header.streamid == actor->ar_net_stream_id)
                {
                    actor->pushNetwork(packet.buffer, packet.header.size);
                    break;
                }
            }
        }
    }
}
#endif // USE_SOCKETW

void ActorManager::RequestSpawnRemoteActor(RoRnet::ActorStreamRegister* actor_reg, const CacheEntryPtr& actor_entry, const RoRnet::UserInfo& userinfo, BitMask_t peeropts)
{
    if (m_stream_time_offsets.find(actor_reg->origin_sourceid) == m_stream_time_offsets.end())
    {
        int offset = actor_reg->time - m_net_timer.getMilliseconds();
        m_stream_time_offsets[actor_reg->origin_sourceid] = offset - 100;
    }
    ActorSpawnRequest* rq = new ActorSpawnRequest;
    rq->asr_origin = ActorSpawnRequest::Origin::NETWORK;
    rq->asr_cache_entry = actor_entry;
    if (strnlen(actor_reg->skin, 60) < 60 && actor_reg->skin[0] != '\0')
    {
        rq->asr_skin_entry = App::GetCacheSystem()->FetchSkinByName(actor_reg->skin); // FIXME: fetch skin by name+guid! ~ 03/2019
    }
    if (strnlen(actor_reg->sectionconfig, 60) < 60)
    {
        rq->asr_config = actor_reg->sectionconfig;
    }
    rq->asr_net_username = tryConvertUTF(userinfo.username);
    rq->asr_net_color    = userinfo.colournum;
    rq->asr_net_peeropts = peeropts;
    rq->net_source_id    = actor_reg->origin_sourceid;
    rq->net_stream_id    = actor_reg->origin_streamid;

    App::GetGameContext()->PushMessage(Message(
        MSG_SIM_SPAWN_ACTOR_REQUESTED, (void*)rq));
}

void ActorManager::AddStreamMismatch(RoRnet::ActorStreamRegister* reg)
{
    m_stream_mismatches[reg->origin_sourceid].insert(reg->origin_streamid);
    m_stream_mismatched_regs.push_back(*reg);
}

int ActorManager::GetNetTimeOffset(int sourceid)
{
    auto search = m_stream_time_offsets.find(sourceid);
    if (search != m_stream_time_offsets.end())
    {
        return search->second;
    }
    return 0;
}

void ActorManager::UpdateNetTimeOffset(int sourceid, int offset)
{
    if (m_stream_time_offsets.find(sourceid) != m_stream_time_offsets.end())
    {
        m_stream_time_offsets[sourceid] += offset;
    }
}

RoRnet::UiStreamsHealth ActorManager::CheckNetworkStreamsOk(int sourceid)
{
    if (!m_stream_mismatches[sourceid].empty())
        return RoRnet::UiStreamsHealth::MISMATCHES;

    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_state != ActorState::NETWORKED_OK)
            continue;

        if (actor->ar_net_source_id == sourceid)
        {
            return RoRnet::UiStreamsHealth::ALL_OK;
        }
    }

    return RoRnet::UiStreamsHealth::IDLE;
}

RoRnet::UiStreamsHealth ActorManager::CheckNetRemoteStreamsOk(int sourceid)
{
    RoRnet::UiStreamsHealth result = RoRnet::UiStreamsHealth::IDLE;

    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_state == ActorState::NETWORKED_OK)
            continue;

        int stream_result = actor->ar_net_stream_results[sourceid];
        if (stream_result == -1 || stream_result == -2)
            return RoRnet::UiStreamsHealth::MISMATCHES;
        if (stream_result == 1)
            result = RoRnet::UiStreamsHealth::ALL_OK;
    }

    return result;
}

const ActorPtr& ActorManager::GetActorByNetworkLinks(int source_id, int stream_id)
{
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_net_source_id == source_id && actor->ar_net_stream_id == stream_id)
        {
            return actor;
        }
    }

    return ACTORPTR_NULL;
}

bool ActorManager::CheckActorCollAabbIntersect(int a, int b)
{
    if (m_actors[a]->ar_collision_bounding_boxes.empty() && m_actors[b]->ar_collision_bounding_boxes.empty())
    {
        return m_actors[a]->ar_bounding_box.intersects(m_actors[b]->ar_bounding_box);
    }
    else if (m_actors[a]->ar_collision_bounding_boxes.empty())
    {
        for (const auto& bbox_b : m_actors[b]->ar_collision_bounding_boxes)
            if (bbox_b.intersects(m_actors[a]->ar_bounding_box))
                return true;
    }
    else if (m_actors[b]->ar_collision_bounding_boxes.empty())
    {
        for (const auto& bbox_a : m_actors[a]->ar_collision_bounding_boxes)
            if (bbox_a.intersects(m_actors[b]->ar_bounding_box))
                return true;
    }
    else
    {
        for (const auto& bbox_a : m_actors[a]->ar_collision_bounding_boxes)
            for (const auto& bbox_b : m_actors[b]->ar_collision_bounding_boxes)
                if (bbox_a.intersects(bbox_b))
                    return true;
    }

    return false;
}

bool ActorManager::PredictActorCollAabbIntersect(int a, int b)
{
    if (m_actors[a]->ar_predicted_coll_bounding_boxes.empty() && m_actors[b]->ar_predicted_coll_bounding_boxes.empty())
    {
        return m_actors[a]->ar_predicted_bounding_box.intersects(m_actors[b]->ar_predicted_bounding_box);
    }
    else if (m_actors[a]->ar_predicted_coll_bounding_boxes.empty())
    {
        for (const auto& bbox_b : m_actors[b]->ar_predicted_coll_bounding_boxes)
            if (bbox_b.intersects(m_actors[a]->ar_predicted_bounding_box))
                return true;
    }
    else if (m_actors[b]->ar_predicted_coll_bounding_boxes.empty())
    {
        for (const auto& bbox_a : m_actors[a]->ar_predicted_coll_bounding_boxes)
            if (bbox_a.intersects(m_actors[b]->ar_predicted_bounding_box))
                return true;
    }
    else
    {
        for (const auto& bbox_a : m_actors[a]->ar_predicted_coll_bounding_boxes)
            for (const auto& bbox_b : m_actors[b]->ar_predicted_coll_bounding_boxes)
                if (bbox_a.intersects(bbox_b))
                    return true;
    }

    return false;
}

void ActorManager::RecursiveActivation(int j, std::vector<bool>& visited)
{
    if (visited[j] || m_actors[j]->ar_state != ActorState::LOCAL_SIMULATED)
        return;

    visited[j] = true;

    for (unsigned int t = 0; t < m_actors.size(); t++)
    {
        if (t == j || visited[t])
            continue;
        if (m_actors[t]->ar_state == ActorState::LOCAL_SIMULATED && CheckActorCollAabbIntersect(t, j))
        {
            m_actors[t]->ar_sleep_counter = 0.0f;
            this->RecursiveActivation(t, visited);
        }
        if (m_actors[t]->ar_state == ActorState::LOCAL_SLEEPING && PredictActorCollAabbIntersect(t, j))
        {
            m_actors[t]->ar_sleep_counter = 0.0f;
            m_actors[t]->ar_state = ActorState::LOCAL_SIMULATED;
            this->RecursiveActivation(t, visited);
        }
    }
}

void ActorManager::ForwardCommands(ActorPtr source_actor)
{
    if (source_actor->ar_forward_commands)
    {
        auto linked_actors = source_actor->ar_linked_actors;

        for (ActorPtr& actor : this->GetActors())
        {
            if (actor != source_actor && actor->ar_import_commands &&
                    (actor->getPosition().distance(source_actor->getPosition()) < 
                     actor->m_min_camera_radius + source_actor->m_min_camera_radius))
            {
                // activate the truck
                if (actor->ar_state == ActorState::LOCAL_SLEEPING)
                {
                    actor->ar_sleep_counter = 0.0f;
                    actor->ar_state = ActorState::LOCAL_SIMULATED;
                }

                if (App::sim_realistic_commands->getBool())
                {
                    if (std::find(linked_actors.begin(), linked_actors.end(), actor) == linked_actors.end())
                        continue;
                }

                // forward commands
                for (int j = 1; j <= MAX_COMMANDS; j++) // BEWARE: commandkeys are indexed 1-MAX_COMMANDS!
                {
                    actor->ar_command_key[j].playerInputValue = std::max(source_actor->ar_command_key[j].playerInputValue,
                                                                         source_actor->ar_command_key[j].commandValue);
                }
                if (source_actor->ar_toggle_ties)
                {
                    //actor->tieToggle();
                    ActorLinkingRequest* rq = new ActorLinkingRequest();
                    rq->alr_type = ActorLinkingRequestType::TIE_TOGGLE;
                    rq->alr_actor_instance_id = actor->ar_instance_id;
                    rq->alr_tie_group = -1;
                    App::GetGameContext()->PushMessage(Message(MSG_SIM_ACTOR_LINKING_REQUESTED, rq));

                }
                if (source_actor->ar_toggle_ropes)
                {
                    //actor->ropeToggle(-1);
                    ActorLinkingRequest* rq = new ActorLinkingRequest();
                    rq->alr_type = ActorLinkingRequestType::ROPE_TOGGLE;
                    rq->alr_actor_instance_id = actor->ar_instance_id;
                    rq->alr_rope_group = -1;
                    App::GetGameContext()->PushMessage(Message(MSG_SIM_ACTOR_LINKING_REQUESTED, rq));
                }
            }
        }
        // just send brake and lights to the connected trucks, and no one else :)
        for (auto hook : source_actor->ar_hooks)
        {
            if (!hook.hk_locked_actor || hook.hk_locked_actor == source_actor)
                continue;

            // forward brakes
            hook.hk_locked_actor->ar_brake = source_actor->ar_brake;
            if (hook.hk_locked_actor->ar_parking_brake != source_actor->ar_trailer_parking_brake)
            {
                hook.hk_locked_actor->parkingbrakeToggle();
            }

            // forward lights
            hook.hk_locked_actor->importLightStateMask(source_actor->getLightStateMask());
        }
    }
}

bool ActorManager::AreActorsDirectlyLinked(const ActorPtr& a1, const ActorPtr& a2)
{
    for (auto& entry: inter_actor_links)
    {
        auto& actor_pair = entry.second;
        if ((actor_pair.first == a1 && actor_pair.second == a2) ||
            (actor_pair.first == a2 && actor_pair.second == a1))
        {
            return true;
        }
    }
    return false;
}

void ActorManager::UpdateSleepingState(ActorPtr player_actor, float dt)
{
    if (!m_forced_awake)
    {
        for (ActorPtr& actor: m_actors)
        {
            if (actor->ar_state != ActorState::LOCAL_SIMULATED)
                continue;
            if (actor->ar_driveable == AI)
                continue;
            if (actor->getVelocity().squaredLength() > 0.01f)
            {
                actor->ar_sleep_counter = 0.0f;
                continue;
            }

            actor->ar_sleep_counter += dt;

            if (actor->ar_sleep_counter >= 10.0f)
            {
                actor->ar_state = ActorState::LOCAL_SLEEPING;
            }
        }
    }

    if (player_actor && player_actor->ar_state == ActorState::LOCAL_SLEEPING)
    {
        player_actor->ar_state = ActorState::LOCAL_SIMULATED;
    }

    std::vector<bool> visited(m_actors.size());
    // Recursivly activate all actors which can be reached from current actor
    if (player_actor && player_actor->ar_state == ActorState::LOCAL_SIMULATED)
    {
        player_actor->ar_sleep_counter = 0.0f;
        this->RecursiveActivation(player_actor->ar_vector_index, visited);
    }
    // Snowball effect (activate all actors which might soon get hit by a moving actor)
    for (unsigned int t = 0; t < m_actors.size(); t++)
    {
        if (m_actors[t]->ar_state == ActorState::LOCAL_SIMULATED && m_actors[t]->ar_sleep_counter == 0.0f)
            this->RecursiveActivation(t, visited);
    }
}

void ActorManager::WakeUpAllActors()
{
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_state == ActorState::LOCAL_SLEEPING)
        {
            actor->ar_state = ActorState::LOCAL_SIMULATED;
            actor->ar_sleep_counter = 0.0f;
        }
    }
}

void ActorManager::SendAllActorsSleeping()
{
    m_forced_awake = false;
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_state == ActorState::LOCAL_SIMULATED)
        {
            actor->ar_state = ActorState::LOCAL_SLEEPING;
        }
    }
}

ActorPtr ActorManager::FindActorInsideBox(Collisions* collisions, const Ogre::String& inst, const Ogre::String& box)
{
    // try to find the desired actor (the one in the box)
    ActorPtr ret = nullptr;
    for (ActorPtr& actor: m_actors)
    {
        if (collisions->isInside(actor->ar_nodes[0].AbsPosition, inst, box))
        {
            if (ret == nullptr)
            // first actor found
                ret = actor;
            else
            // second actor found -> unclear which one was meant
                return nullptr;
        }
    }
    return ret;
}

void ActorManager::RepairActor(Collisions* collisions, const Ogre::String& inst, const Ogre::String& box, bool keepPosition)
{
    ActorPtr actor = this->FindActorInsideBox(collisions, inst, box);
    if (actor != nullptr)
    {
        SOUND_PLAY_ONCE(actor, SS_TRIG_REPAIR);

        ActorModifyRequest* rq = new ActorModifyRequest;
        rq->amr_actor = actor->ar_instance_id;
        rq->amr_type = ActorModifyRequest::Type::RESET_ON_SPOT;
        App::GetGameContext()->PushMessage(Message(MSG_SIM_MODIFY_ACTOR_REQUESTED, (void*)rq));
    }
}

std::pair<ActorPtr, float> ActorManager::GetNearestActor(Vector3 position)
{
    ActorPtr nearest_actor = nullptr;
    float min_squared_distance = std::numeric_limits<float>::max();
    for (ActorPtr& actor : m_actors)
    {
        float squared_distance = position.squaredDistance(actor->ar_nodes[0].AbsPosition);
        if (squared_distance < min_squared_distance)
        {
            min_squared_distance = squared_distance;
            nearest_actor = actor;
        }
    }
    return std::make_pair(nearest_actor, std::sqrt(min_squared_distance));
}

void ActorManager::CleanUpSimulation() // Called after simulation finishes
{
    this->SyncWithSimThread();
    this->FinishDeterministicActorInput(
        "simulation cleanup",
        false,
        false);
    m_deterministic_actor_input_pending_savegame.reset();
    this->FinishDeterministicStateTrace(
        "simulation cleanup",
        false);

    while (m_actors.size() > 0)
    {
        this->DeleteActorInternal(m_actors.back()); // OK to invoke here - CleanUpSimulation() - processing `MSG_SIM_UNLOAD_TERRAIN_REQUESTED`
    }

    m_total_sim_time = 0.f;
    m_last_simulation_speed = 0.1f;
    m_simulation_paused = false;
    m_simulation_speed = 1.f;
    m_completed_physics_steps = 0;
    m_deterministic_state_trace_suppressed = false;
    m_deterministic_actor_input_suppressed = false;
    m_deterministic_actor_input_stop_replay = false;
    m_deterministic_actor_input_pause_requested.store(
        false,
        std::memory_order_release);
}

void ActorManager::DeleteActorInternal(ActorPtr actor)
{
    if (actor == nullptr)
        return;

    // Tombstone the renderer identity before any synchronization, script
    // callback, network operation, or disposal can throw. Actor::dispose()
    // releases GfxActor ownership, so no raw capture pointer may survive it.
    if (App::GetGfxScene() != nullptr && actor->GetGfxActor() != nullptr)
    {
        App::GetGfxScene()->DestroyGfxActor(actor->GetGfxActor());
    }

    // A prior disposal attempt may have thrown after setting DISPOSED but
    // before the ownership vector was compacted. Retrying cleanup must still
    // release that retained ActorPtr instead of spinning forever on the same
    // already-tombstoned entry.
    if (actor->ar_state == ActorState::DISPOSED)
    {
        EraseIf(m_actors,
            [actor](ActorPtr& cur_actor) { return actor == cur_actor; });
        for (unsigned int index = 0U; index < m_actors.size(); ++index)
            m_actors[index]->ar_vector_index = index;
        return;
    }

    this->SyncWithSimThread();

#ifdef USE_SOCKETW
    if (App::mp_state->getEnum<MpState>() == RoR::MpState::CONNECTED)
    {
        if (actor->ar_state != ActorState::NETWORKED_OK)
        {
            App::GetNetwork()->AddPacket(actor->ar_net_stream_id, RoRnet::MSG2_STREAM_UNREGISTER, 0, 0);
        }
        else if (std::count_if(m_actors.begin(), m_actors.end(), [actor](ActorPtr& b)
                    { return b->ar_net_source_id == actor->ar_net_source_id; }) == 1)
        {
            // We're deleting the last actor from this stream source, reset the stream time offset
            m_stream_time_offsets.erase(actor->ar_net_source_id);
        }
    }
#endif // USE_SOCKETW

    // Unload actor's scripts
    std::vector<ScriptUnitID_t> unload_list;
    for (auto& pair : App::GetScriptEngine()->getScriptUnits())
    {
        if (pair.second.associatedActor == actor)
            unload_list.push_back(pair.first);
    }
    for (ScriptUnitID_t id : unload_list)
    {
        App::GetScriptEngine()->unloadScript(id);
    }

    // Remove FreeForces referencing this actor
    m_free_forces.erase(
        std::remove_if(
            m_free_forces.begin(),
            m_free_forces.end(),
            [actor](FreeForce& item) { return item.ffc_base_actor == actor || item.ffc_target_actor == actor; }),
        m_free_forces.end());

    // Only dispose(), do not `delete`; a script may still hold pointer to the object.
    actor->dispose();

    EraseIf(m_actors, [actor](ActorPtr& curActor) { return actor == curActor; });

    // Upate actor indices
    for (unsigned int i = 0; i < m_actors.size(); i++)
        m_actors[i]->ar_vector_index = i;
}

// ACTORLIST for cycling with hotkeys
// ----------------------------------

int FindPivotActorId(ActorPtr player, ActorPtr prev_player)
{
    if (player != nullptr)
        return player->ar_vector_index;
    else if (prev_player != nullptr)
        return prev_player->ar_vector_index + 1;
    return -1;
}

bool ShouldIncludeActorInList(const ActorPtr& actor)
{
    bool retval = !actor->isPreloadedWithTerrain();

    // Exclude remote actors, if desired
    if (!App::mp_cyclethru_net_actors->getBool())
    {
        if (actor->ar_state == ActorState::NETWORKED_OK || actor->ar_state == ActorState::NETWORKED_HIDDEN)
        {
            retval = false;
        }
    }

    return retval;
}

const ActorPtr& ActorManager::FetchNextVehicleOnList(ActorPtr player, ActorPtr prev_player)
{
    int pivot_index = FindPivotActorId(player, prev_player);

    for (int i = pivot_index + 1; i < m_actors.size(); i++)
    {
        if (ShouldIncludeActorInList(m_actors[i]))
            return m_actors[i];
    }

    for (int i = 0; i < pivot_index; i++)
    {
        if (ShouldIncludeActorInList(m_actors[i]))
            return m_actors[i];
    }

    if (pivot_index >= 0)
    {
        if (ShouldIncludeActorInList(m_actors[pivot_index]))
            return m_actors[pivot_index];
    }

    return ACTORPTR_NULL;
}

const ActorPtr& ActorManager::FetchPreviousVehicleOnList(ActorPtr player, ActorPtr prev_player)
{
    int pivot_index = FindPivotActorId(player, prev_player);

    for (int i = pivot_index - 1; i >= 0; i--)
    {
        if (ShouldIncludeActorInList(m_actors[i]))
            return m_actors[i];
    }

    for (int i = static_cast<int>(m_actors.size()) - 1; i > pivot_index; i--)
    {
        if (ShouldIncludeActorInList(m_actors[i]))
            return m_actors[i];
    }

    if (pivot_index >= 0)
    {
        if (ShouldIncludeActorInList(m_actors[pivot_index]))
            return m_actors[pivot_index];
    }

    return ACTORPTR_NULL;
}

// END actorlist

const ActorPtr& ActorManager::FetchRescueVehicle()
{
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_rescuer_flag)
        {
            return actor;
        }
    }
    return ACTORPTR_NULL;
}

void ActorManager::UpdateActors(ActorPtr player_actor)
{
    // Replay faults and authenticated exhaustion originate on the private
    // physics worker. Transfer only the pause request through an atomic; the
    // main thread remains the sole owner of the legacy pause flag.
    if (m_deterministic_actor_input_pause_requested.exchange(
            false,
            std::memory_order_acq_rel))
    {
        m_simulation_paused = true;
        RoR::LogFormat(
            "[RoR|Determinism] Physics paused after deterministic input "
            "replay stopped");
    }

    // A deterministic savegame always loads behind a zero-step barrier. Actor
    // spawn/seat messages may need several main-loop turns; do not schedule a
    // single physics step until the exact restored owner and source policy have
    // authenticated the continuation.
    if (m_deterministic_actor_input_pending_savegame != nullptr)
    {
        this->SyncWithSimThread();
        if (!this->TryActivateDeterministicActorInputSavegame())
            return;

        // Activation publishes the authenticated continuation and restores
        // the exact completed-step cursor. Keep this main-loop turn as a
        // zero-step handoff so scripts and receipt collectors can observe and
        // arm against that cursor before any resumed physics is scheduled.
        return;
    }

    // An exact-step capture runtime is the sole scheduler while it owns this
    // manager. Input/render work may continue on the main thread, but normal
    // wall-clock physics must not race or interleave with authored batches.
    if (m_fixed_step_capture_owner != nullptr)
        return;

    float dt = m_simulation_time;

    std::uint32_t fixed_steps_per_frame = 0U;
    const int configured_fixed_steps =
        App::sim_deterministic_fixed_steps_per_frame->getInt();
    if (!DeterministicScenarioSchedule::
            TryResolveFixedStepsPerFrame(
                configured_fixed_steps,
                fixed_steps_per_frame))
    {
        RoR::LogFormat(
            "[RoR|Determinism] Invalid "
            "sim_deterministic_fixed_steps_per_frame=%d; expected "
            "[0, %u]. Restoring wall-clock scheduling.",
            configured_fixed_steps,
            DeterministicScenarioSchedule::
                MAX_FIXED_STEPS_PER_FRAME);
        App::sim_deterministic_fixed_steps_per_frame->setVal(0);
        fixed_steps_per_frame = 0U;
    }

    if (fixed_steps_per_frame > 0U)
    {
        // Diagnostic scene mode deliberately ignores wall-clock dt and
        // simulation-speed grouping. Physics time still advances by exactly
        // PHYSICS_DT for every scheduled substep.
        m_physics_steps =
            static_cast<int>(fixed_steps_per_frame);
        m_dt_remainder = 0.0f;
        dt = PHYSICS_DT * m_physics_steps;
    }
    else
    {
        // do not allow dt > 1/20
        dt = std::min(dt, 1.0f / 20.0f);

        dt *= m_simulation_speed;

        dt += m_dt_remainder;
        m_physics_steps = dt / PHYSICS_DT;
    }
    if (m_physics_steps == 0)
    {
        if ((m_deterministic_actor_input != nullptr ||
                m_deterministic_actor_input_suppressed) &&
            App::sim_deterministic_input_mode->getStr() == "off")
        {
            this->SyncWithSimThread();
            this->FinishDeterministicActorInput(
                "mode disabled while no fixed step was due",
                false,
                false);
        }
        if ((m_deterministic_state_trace != nullptr ||
                m_deterministic_state_trace_suppressed) &&
            !App::sim_deterministic_state_trace->getBool())
        {
            this->SyncWithSimThread();
            this->FinishDeterministicStateTrace(
                "capture disabled while no fixed step was due",
                false);
        }
        return;
    }

    if (fixed_steps_per_frame == 0U)
    {
        m_dt_remainder = dt - (m_physics_steps * PHYSICS_DT);
        dt = PHYSICS_DT * m_physics_steps;
    }

    this->RunPhysicsStepBatch(
        player_actor,
        dt,
        nullptr,
        false);
}

bool ActorManager::AcquireFixedStepCaptureOwnership(
    const void* owner_token,
    ActorPtr player_actor)
{
    if (owner_token == nullptr || player_actor == nullptr)
        return false;

    // Establish ownership only from a fully joined boundary.
    this->SyncWithSimThread();
    GameContext* const context = App::GetGameContext();
    if (m_fixed_step_capture_owner != nullptr ||
        m_deterministic_actor_input != nullptr ||
        (App::sim_deterministic_input_mode != nullptr &&
            App::sim_deterministic_input_mode->getStr() != "off") ||
        context == nullptr ||
        context->GetActorManager() != this ||
        context->GetPlayerActor() != player_actor)
    {
        return false;
    }

    m_fixed_step_capture_player = player_actor;
    m_fixed_step_capture_owner = owner_token;
    return true;
}

void ActorManager::ReleaseFixedStepCaptureOwnership(
    const void* owner_token) noexcept
{
    if (owner_token == nullptr ||
        m_fixed_step_capture_owner != owner_token)
    {
        return;
    }

    // The adapter cannot release the scheduling gate while one of its own
    // physics batches is still in flight.
    this->SyncWithSimThread();
    m_fixed_step_capture_player = nullptr;
    m_fixed_step_capture_owner = nullptr;
}

FixedStepCaptureBridge::BatchResult
ActorManager::AdvanceFixedStepsForCapture(
    const void* owner_token,
    ActorPtr player_actor,
    std::uint32_t fixed_step_count,
    FixedStepCaptureBridge::AppliedInputObserver& observer)
{
    // The current asynchronous frame, if any, owns m_physics_steps and actor
    // state until it joins. Validate against the canonical post-join boundary.
    this->SyncWithSimThread();

    GameContext* const context = App::GetGameContext();
    if (!this->HasFixedStepCaptureOwnership(owner_token) ||
        player_actor == nullptr ||
        player_actor != m_fixed_step_capture_player ||
        context == nullptr ||
        context->GetActorManager() != this ||
        context->GetPlayerActor() != player_actor)
    {
        return FixedStepCaptureBridge::
            BatchResult::CAPTURE_OWNERSHIP_REQUIRED;
    }

    const std::uint64_t first_completed_physics_step =
        m_completed_physics_steps;
    const FixedStepCaptureBridge::BatchResult validation =
        FixedStepCaptureBridge::ValidateBatch(
            first_completed_physics_step,
            fixed_step_count);
    if (validation !=
        FixedStepCaptureBridge::BatchResult::COMPLETED)
    {
        return validation;
    }

    const float dt =
        PHYSICS_DT * static_cast<float>(fixed_step_count);
    // Preserve the normal GameContext ordering while excluding unrelated
    // frame/UI work. Raw devices were sampled once by the owning main-frame
    // path. Advance edge timers exactly once for this authored batch, then
    // resolve steering/Engine controls and the parking-brake edge.
    if (App::GetInputEngine() == nullptr)
    {
        return FixedStepCaptureBridge::
            BatchResult::INPUT_RESOLUTION_REJECTED;
    }
    App::GetInputEngine()->updateKeyBounces(dt);
    if (!context->ResolveTruckDrivingInputs(dt, player_actor) ||
        !context->ApplyTruckParkingBrakeInput(player_actor))
    {
        return FixedStepCaptureBridge::
            BatchResult::INPUT_RESOLUTION_REJECTED;
    }

    m_physics_steps = static_cast<int>(fixed_step_count);
    FixedStepCaptureBridge::ObservationBatch observation_batch(
        first_completed_physics_step,
        fixed_step_count,
        observer);
    this->RunPhysicsStepBatch(
        player_actor,
        dt,
        &observation_batch,
        true);

    const std::uint64_t expected_completed_physics_step =
        first_completed_physics_step +
        static_cast<std::uint64_t>(fixed_step_count);
    if (m_completed_physics_steps !=
        expected_completed_physics_step)
    {
        return FixedStepCaptureBridge::
            BatchResult::PHYSICS_STEP_MISMATCH;
    }
    if (!observation_batch.Succeeded())
    {
        return FixedStepCaptureBridge::
            BatchResult::OBSERVER_REJECTED;
    }
    return FixedStepCaptureBridge::BatchResult::COMPLETED;
}

void ActorManager::RunPhysicsStepBatch(
    ActorPtr player_actor,
    float dt,
    FixedStepCaptureBridge::ObservationBatch* observation_batch,
    bool force_join)
{
    this->SyncWithSimThread();

    this->UpdateSleepingState(player_actor, dt);

    for (ActorPtr& actor: m_actors)
    {
        actor->HandleInputEvents(dt);
        actor->HandleAngelScriptEvents(dt);

#ifdef USE_ANGELSCRIPT
        if (actor->ar_vehicle_ai && actor->ar_vehicle_ai->isActive())
            actor->ar_vehicle_ai->update(dt, 0);
#endif // USE_ANGELSCRIPT

        if (actor->ar_engine)
        {
            if (actor->ar_driveable == TRUCK)
            {
                this->UpdateTruckFeatures(actor, dt);
            }
            if (actor->ar_state == ActorState::LOCAL_SLEEPING &&
                !App::sim_deterministic_sleeping_engine->getBool())
            {
                const std::uint64_t engine_update_step =
                    actor->m_engine_update_step++;
                actor->ar_engine->UpdateEngine(
                    dt,
                    1,
                    actor->m_deterministic_seed,
                    engine_update_step);
            }
            actor->ar_engine->UpdateEngineAudio();
        }

        // Always update indicator states - used by 'u' type flares.
        actor->updateDashBoards(dt);

        // Blinkers (turn signals) must always be updated
        actor->updateFlareStates(dt);

        if (actor->ar_state != ActorState::LOCAL_SLEEPING)
        {
            actor->updateVisual(dt);
            if (actor->ar_update_physics && App::gfx_skidmarks_mode->getInt() > 0)
            {
                actor->updateSkidmarks();
            }
        }
        if (App::mp_state->getEnum<MpState>() == RoR::MpState::CONNECTED)
        {
            // FIXME: Hidden actors must also be updated to workaround a glitch, see https://github.com/RigsOfRods/rigs-of-rods/issues/2911
            if (actor->ar_state == ActorState::NETWORKED_OK || actor->ar_state == ActorState::NETWORKED_HIDDEN)
                actor->calcNetwork();
            else
                actor->sendStreamData();
        }
    }

    if (player_actor != nullptr)
    {
        this->ForwardCommands(player_actor);
        if (player_actor->ar_toggle_ties)
        {
            //player_actor->tieToggle();
            ActorLinkingRequest* rq = new ActorLinkingRequest();
            rq->alr_type = ActorLinkingRequestType::TIE_TOGGLE;
            rq->alr_actor_instance_id = player_actor->ar_instance_id;
            rq->alr_tie_group = -1;
            App::GetGameContext()->PushMessage(Message(MSG_SIM_ACTOR_LINKING_REQUESTED, rq));

            player_actor->ar_toggle_ties = false;
        }
        if (player_actor->ar_toggle_ropes)
        {
            //player_actor->ropeToggle(-1);
            ActorLinkingRequest* rq = new ActorLinkingRequest();
            rq->alr_type = ActorLinkingRequestType::ROPE_TOGGLE;
            rq->alr_actor_instance_id = player_actor->ar_instance_id;
            rq->alr_rope_group = -1;
            App::GetGameContext()->PushMessage(Message(MSG_SIM_ACTOR_LINKING_REQUESTED, rq));

            player_actor->ar_toggle_ropes = false;
        }

        player_actor->ForceFeedbackStep(m_physics_steps);

        if (player_actor->ar_state == ActorState::LOCAL_REPLAY)
        {
            player_actor->getReplay()->replayStepActor();
        }
    }

    auto func = std::function<void()>(
        [this,
         observation_batch]()
        {
            this->UpdatePhysicsSimulation(
                observation_batch);
        });
    m_sim_task = m_sim_thread_pool->RunTask(func);

    m_total_sim_time += dt;

    if (force_join || !App::app_async_physics->getBool())
        m_sim_task->join();
}

const ActorPtr& ActorManager::GetActorById(ActorInstanceID_t actor_id)
{
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_instance_id == actor_id)
        {
            return actor;
        }
    }
    return ACTORPTR_NULL;
}

void ActorManager::UpdatePhysicsSimulation()
{
    this->UpdatePhysicsSimulation(
        nullptr);
}

void ActorManager::UpdatePhysicsSimulation(
    FixedStepCaptureBridge::ObservationBatch* observation_batch)
{
    for (ActorPtr& actor: m_actors)
    {
        actor->UpdatePhysicsOrigin();
    }
    for (int i = 0; i < m_physics_steps; i++)
    {
        // Authenticated replay is applied before every other fixed-step-start
        // observer and before worker force dispatch. A replay fault/exhaustion
        // stops this batch without advancing the canonical step counter.
        if (!this->ProcessDeterministicActorInputStep())
            return;

        if (observation_batch != nullptr)
        {
            observation_batch->ObserveFixedStepStart(
                m_completed_physics_steps,
                static_cast<std::uint32_t>(i));
        }

        const bool capture_deterministic_state =
            this->PrepareDeterministicStateTraceStep();
        std::vector<
            DeterministicContactOrder::InterActorKey>*
                trace_contact_keys =
                    capture_deterministic_state
                        ? &m_deterministic_state_trace->contact_keys
                        : nullptr;
        bool trace_contact_capture_succeeded = true;
        ContactConservation::Aggregate* trace_contact_conservation =
            capture_deterministic_state
                ? &m_deterministic_state_trace->contact_conservation
                : nullptr;
        ContactConservation::Error trace_contact_conservation_error =
            ContactConservation::Error::NONE;

        if (App::sim_deterministic_sleeping_engine->getBool())
        {
            // Sleeping engines run at a fixed 32-substep cadence instead of
            // once per render frame. The same counter advances on every
            // active or sleeping physics step, making both update boundaries
            // and anti-lag samples independent of frame grouping.
            for (ActorPtr& actor: m_actors)
            {
                if (actor->ar_state == ActorState::LOCAL_SLEEPING)
                    actor->UpdateSleepingEngineFixedStep();
            }
        }
        {
            std::vector<std::function<void()>> tasks;
            for (ActorPtr& actor: m_actors)
            {
                if (actor->ar_update_physics = actor->CalcForcesEulerPrepare(i == 0))
                {
                    Actor* const actor_raw = actor.GetRef();
                    auto func = std::function<void()>([this, i, actor_raw]()
                        {
                            actor_raw->CalcForcesEulerCompute(
                                i == 0,
                                m_physics_steps);
                        });
                    tasks.push_back(func);
                }
            }
            App::GetThreadPool()->Parallelize(tasks);
            for (ActorPtr& actor: m_actors)
            {
                if (actor->ar_update_physics)
                {
                    actor->CalcBeamsInterActor();
                }
            }
        }
        {
            std::vector<Actor*> collision_actors;
            for (ActorPtr& actor: m_actors)
            {
                if (actor->m_inter_point_col_detector != nullptr &&
                        (actor->ar_update_physics ||
                        (App::mp_pseudo_collisions->getBool() &&
                        actor->ar_state == ActorState::NETWORKED_OK)))
                {
                    collision_actors.push_back(actor.GetRef());
                }
            }
            std::sort(
                collision_actors.begin(),
                collision_actors.end(),
                [](const Actor* left, const Actor* right)
                {
                    return left->ar_instance_id < right->ar_instance_id;
                });

            // Updating detectors can reset collision-rate state on both actors,
            // so establish that state in one canonical order before launching
            // read-only contact discovery tasks.
            for (Actor* actor: collision_actors)
                actor->m_inter_point_col_detector->UpdateInterPoint();

            std::vector<Actor*> contact_actors;
            std::vector<InterActorCollisionSchedule> contact_schedules;
            for (Actor* actor: collision_actors)
            {
                if (actor->ar_collision_relevant)
                {
                    InterActorCollisionSchedule schedule;
                    PrepareInterActorCollisionSchedule(
                        actor->ar_num_collcabs,
                        actor->ar_inter_collcabrate,
                        schedule);
                    if (schedule.active_surface_contacts.any())
                    {
                        contact_actors.push_back(actor);
                        contact_schedules.push_back(schedule);
                    }
                }
            }

            auto resolve_contacts_serially =
                [&contact_actors,
                 &contact_schedules,
                 trace_contact_keys,
                 &trace_contact_capture_succeeded,
                 trace_contact_conservation,
                 &trace_contact_conservation_error](
                    bool update_rate_state)
                {
                    if (trace_contact_keys != nullptr)
                        trace_contact_keys->clear();
                    for (std::size_t actor_index = 0;
                            actor_index < contact_actors.size();
                            ++actor_index)
                    {
                        Actor* const actor = contact_actors[actor_index];
                        ROR_ASSERT(
                            actor_index == 0 ||
                            contact_actors[actor_index - 1]->ar_instance_id <
                                actor->ar_instance_id);
                        ContactConservation::Error actor_conservation_error =
                            ContactConservation::Error::NONE;
                        const bool actor_contacts_captured =
                            ResolveInterActorCollisionContactsSerial(
                            actor->ar_instance_id,
                            PHYSICS_DT,
                           *actor->m_inter_point_col_detector,
                            contact_schedules[actor_index],
                            update_rate_state,
                            actor->ar_num_collcabs,
                            actor->ar_collcabs,
                            actor->ar_cabs,
                            actor->ar_inter_collcabrate,
                            actor->ar_nodes,
                            actor->ar_collision_range,
                           *actor->ar_submesh_ground_model,
                            trace_contact_capture_succeeded
                                ? trace_contact_keys
                                : nullptr,
                            trace_contact_conservation,
                            &actor_conservation_error);
                        trace_contact_capture_succeeded =
                            trace_contact_capture_succeeded &&
                            actor_contacts_captured;
                        if (trace_contact_conservation_error ==
                                ContactConservation::Error::NONE &&
                                actor_conservation_error !=
                                    ContactConservation::Error::NONE)
                        {
                            trace_contact_conservation_error =
                                actor_conservation_error;
                        }
                    }
                };
            auto report_contact_fallback =
                [this](const char* reason)
                {
                    const std::uint64_t count =
                        ++m_inter_contact_fallback_count;
                    if ((count & (count - 1)) == 0)
                    {
                        RoR::LogFormat(
                            "[RoR|Physics] Inter-actor contact buffer "
                            "fallback #%llu (%s)",
                            static_cast<unsigned long long>(count),
                            reason);
                    }
                };

            typedef DeterministicContactOrder::BoundedTaskBuffer<
                InterActorCollisionContact> ContactTaskBuffer;
            std::vector<ContactTaskBuffer>* contact_buffers = nullptr;
            bool buffers_ready = true;
            try
            {
                m_inter_contact_buffers->Prepare(contact_actors.size());
                contact_buffers =
                    &m_inter_contact_buffers->GetBuffers();
            }
            catch (const std::bad_alloc&)
            {
                buffers_ready = false;
                report_contact_fallback("task-buffer allocation");
            }

            if (buffers_ready)
            {
                std::vector<std::function<void()>> tasks;
                for (std::size_t actor_index = 0;
                        actor_index < contact_actors.size();
                        ++actor_index)
                {
                    Actor* const actor = contact_actors[actor_index];
                    auto func = std::function<void()>(
                        [actor, actor_index, &contact_buffers,
                         &contact_schedules]()
                        {
                            CollectInterActorCollisionContacts(
                                actor->ar_instance_id,
                               *actor->m_inter_point_col_detector,
                                contact_schedules[actor_index],
                                actor->ar_num_collcabs,
                                actor->ar_collcabs,
                                actor->ar_cabs,
                                actor->ar_inter_collcabrate,
                                actor->ar_nodes,
                                actor->ar_collision_range,
                               *actor->ar_submesh_ground_model,
                                (*contact_buffers)[actor_index]);
                        });
                    tasks.push_back(func);
                }
                App::GetThreadPool()->Parallelize(tasks);

                const bool quota_overflow =
                    DeterministicContactOrder::AnyTaskBufferOverflowed(
                        *contact_buffers);
                const bool growth_allocation_failed =
                    DeterministicContactOrder::
                        AnyTaskBufferAllocationFailed(*contact_buffers);
                DeterministicContactOrder::ProcessTaskBuffersOrFallback(
                    *contact_buffers,
                    [](const InterActorCollisionContact& contact)
                    {
                        return contact.key;
                    },
                    [trace_contact_keys,
                     &trace_contact_capture_succeeded,
                     trace_contact_conservation,
                     &trace_contact_conservation_error](
                        const std::vector<ContactTaskBuffer>& buffers)
                    {
                        for (const ContactTaskBuffer& buffer : buffers)
                        {
                            const ContactConservation::Error error =
                                ApplyInterActorCollisionContacts(
                                PHYSICS_DT,
                                buffer.GetItems(),
                                trace_contact_conservation);
                            if (trace_contact_conservation_error ==
                                    ContactConservation::Error::NONE &&
                                    error !=
                                        ContactConservation::Error::NONE)
                            {
                                trace_contact_conservation_error = error;
                            }
                        }
                        if (trace_contact_keys != nullptr)
                        {
                            trace_contact_capture_succeeded =
                                AppendBufferedContactKeys(
                                    buffers,
                                    *trace_contact_keys);
                        }
                    },
                    [&resolve_contacts_serially,
                     &report_contact_fallback,
                     growth_allocation_failed,
                     quota_overflow]()
                    {
                        report_contact_fallback(
                            growth_allocation_failed
                                ? "task-buffer growth allocation"
                                : quota_overflow
                                ? "per-actor contact quota overflow"
                                : "noncanonical task ranges");
                        // Discovery already finalized collision-rate state.
                        resolve_contacts_serially(false);
                    });
            }
            else
            {
                // Schedule transitions were prepared, but discovery has not
                // run yet, so the serial path must finalize rate state.
                resolve_contacts_serially(true);
            }
        }

        // Apply FreeForces - intentionally as a separate pass over all actors
        this->CalcFreeForces();

        if (capture_deterministic_state)
        {
            this->CaptureDeterministicStateTraceStep(
                trace_contact_capture_succeeded,
                trace_contact_conservation_error);
        }
        if (m_completed_physics_steps ==
                std::numeric_limits<std::uint64_t>::max())
        {
            if (m_deterministic_state_trace != nullptr)
            {
                RoR::LogFormat(
                    "[RoR|Determinism] Fixed-step counter exhausted; "
                    "state trace capture is being stopped");
                this->FinishDeterministicStateTrace(
                    "fixed-step counter exhausted",
                    true);
            }
        }
        else
        {
            ++m_completed_physics_steps;
        }
    }
    for (ActorPtr& actor: m_actors)
    {
        actor->m_ongoing_reset = false;
        if (actor->ar_update_physics && m_physics_steps > 0)
        {
            Vector3  camera_gforces = actor->m_camera_gforces_accu / m_physics_steps;
            actor->m_camera_gforces_accu = Vector3::ZERO;
            actor->m_camera_gforces = actor->m_camera_gforces * 0.5f + camera_gforces * 0.5f;
            actor->calculateLocalGForces();
            actor->calculateAveragePosition();
            actor->m_avg_node_velocity  = actor->m_avg_node_position - actor->m_avg_node_position_prev;
            actor->m_avg_node_velocity /= (m_physics_steps * PHYSICS_DT);
            actor->m_avg_node_position_prev = actor->m_avg_node_position;
            actor->ar_top_speed = std::max(actor->ar_top_speed, actor->ar_nodes[0].Velocity.length());
        }
    }
}

void ActorManager::SyncWithSimThread()
{
    if (m_sim_task)
        m_sim_task->join();
}

void HandleErrorLoadingFile(std::string type, std::string filename, std::string exception_msg)
{
    RoR::Str<200> msg;
    msg << "Failed to load '" << filename << "' (type: '" << type << "'), message: " << exception_msg;
    App::GetConsole()->putMessage(
        Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, msg.ToCStr(), "error.png");
}

void HandleErrorLoadingTruckfile(std::string filename, std::string exception_msg)
{
    HandleErrorLoadingFile("actor", filename, exception_msg);
}

RigDef::DocumentPtr ActorManager::FetchActorDef(RoR::ActorSpawnRequest& rq)
{
    // Check the actor exists in mod cache
    if (rq.asr_cache_entry == nullptr)
    {
        HandleErrorLoadingTruckfile(rq.asr_filename, "Truckfile not found in ModCache (probably not installed)");
        return nullptr;
    }

    // If already parsed, re-use
    if (rq.asr_cache_entry->actor_def != nullptr)
    {
        return rq.asr_cache_entry->actor_def;
    }

    // Load the 'truckfile'
    try
    {
        App::GetCacheSystem()->LoadResource(rq.asr_cache_entry);
        if (rq.asr_cache_entry->actor_def != nullptr)
        {
            return rq.asr_cache_entry->actor_def;
        }
        if (rq.asr_cache_entry->fext == "jbeam")
        {
            HandleErrorLoadingTruckfile(
                rq.asr_cache_entry->fname,
                "Authenticated JBeam import did not publish a current "
                "RigDef document");
            return nullptr;
        }
        Ogre::DataStreamPtr stream = Ogre::ResourceGroupManager::getSingleton().openResource(rq.asr_cache_entry->fname, rq.asr_cache_entry->resource_group);

        if (!stream || !stream->isReadable())
        {
            HandleErrorLoadingTruckfile(rq.asr_cache_entry->fname, "Unable to open/read truckfile");
            return nullptr;
        }

        RoR::LogFormat("[RoR] Parsing truckfile '%s'", rq.asr_cache_entry->fname.c_str());
        RigDef::Parser parser;
        parser.Prepare();
        parser.ProcessOgreStream(stream.get(), rq.asr_cache_entry->resource_group);
        parser.Finalize();

        auto def = parser.GetFile();

        // VALIDATING
        LOG(" == Validating vehicle: " + def->name);

        RigDef::Validator validator;
        validator.Setup(def);

        if (rq.asr_origin == ActorSpawnRequest::Origin::TERRN_DEF)
        {
            // Workaround: Some terrains pre-load truckfiles with special purpose:
            //     "soundloads" = play sound effect at certain spot
            //     "fixes"      = structures of N/B fixed to the ground
            // These files can have no beams. Possible extensions: .load or .fixed
            std::string file_extension = rq.asr_cache_entry->fname.substr(rq.asr_cache_entry->fname.find_last_of('.'));
            Ogre::StringUtil::toLowerCase(file_extension);
            if ((file_extension == ".load") || (file_extension == ".fixed"))
            {
                validator.SetCheckBeams(false);
            }
        }

        validator.Validate(); // Sends messages to console

        def->hash = Sha1Hash(stream->getAsString());

        rq.asr_cache_entry->actor_def = def;
        return def;
    }
    catch (Ogre::Exception& oex)
    {
        HandleErrorLoadingTruckfile(rq.asr_cache_entry->fname, oex.getDescription().c_str());
        return nullptr;
    }
    catch (std::exception& stex)
    {
        HandleErrorLoadingTruckfile(rq.asr_cache_entry->fname, stex.what());
        return nullptr;
    }
    catch (...)
    {
        HandleErrorLoadingTruckfile(rq.asr_cache_entry->fname, "<Unknown exception occurred>");
        return nullptr;
    }
}

void ActorManager::ExportActorDef(RigDef::DocumentPtr def, std::string filename, std::string rg_name)
{
    try
    {
        Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();

        // Open OGRE stream for writing
        Ogre::DataStreamPtr stream = rgm.createResource(filename, rg_name, /*overwrite=*/true);
        if (stream.isNull() || !stream->isWriteable())
        {
            OGRE_EXCEPT(Ogre::Exception::ERR_CANNOT_WRITE_TO_FILE,
                "Stream NULL or not writeable, filename: '" + filename
                + "', resource group: '" + rg_name + "'");
        }

        // Serialize actor to string
        RigDef::Serializer serializer(def);
        serializer.Serialize();

        // Flush the string to file
        stream->write(serializer.GetOutput().c_str(), serializer.GetOutput().size());
        stream->close();
    }
    catch (Ogre::Exception& oex)
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_ACTOR, Console::CONSOLE_SYSTEM_ERROR,
                                      fmt::format(_LC("Truck", "Failed to export truck '{}' to resource group '{}', message: {}"),
                                                  filename, rg_name, oex.getFullDescription()));
    }
}

std::vector<ActorPtr> ActorManager::GetLocalActors()
{
    std::vector<ActorPtr> actors;
    for (ActorPtr& actor: m_actors)
    {
        if (actor->ar_state != ActorState::NETWORKED_OK)
            actors.push_back(actor);
    }
    return actors;
}

void ActorManager::UpdateInputEvents(float dt)
{
    // Simulation pace adjustment (slowmotion)
    if (!App::GetGameContext()->GetRaceSystem().IsRaceInProgress())
    {
        // EV_COMMON_ACCELERATE_SIMULATION
        if (App::GetInputEngine()->getEventBoolValue(EV_COMMON_ACCELERATE_SIMULATION))
        {
            float simulation_speed = this->GetSimulationSpeed() * pow(2.0f, dt / 2.0f);
            this->SetSimulationSpeed(simulation_speed);
            String ssmsg = _L("New simulation speed: ") + TOSTRING(Round(simulation_speed * 100.0f, 1)) + "%";
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
        }

        // EV_COMMON_DECELERATE_SIMULATION
        if (App::GetInputEngine()->getEventBoolValue(EV_COMMON_DECELERATE_SIMULATION))
        {
            float simulation_speed = this->GetSimulationSpeed() * pow(0.5f, dt / 2.0f);
            this->SetSimulationSpeed(simulation_speed);
            String ssmsg = _L("New simulation speed: ") + TOSTRING(Round(simulation_speed * 100.0f, 1)) + "%";
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
        }

        // EV_COMMON_RESET_SIMULATION_PACE
        if (App::GetInputEngine()->getEventBoolValueBounce(EV_COMMON_RESET_SIMULATION_PACE))
        {
            float simulation_speed = this->GetSimulationSpeed();
            if (simulation_speed != 1.0f)
            {
                m_last_simulation_speed = simulation_speed;
                this->SetSimulationSpeed(1.0f);
                std::string ssmsg = _L("Simulation speed reset.");
                App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
            }
            else if (m_last_simulation_speed != 1.0f)
            {
                this->SetSimulationSpeed(m_last_simulation_speed);
                String ssmsg = _L("New simulation speed: ") + TOSTRING(Round(m_last_simulation_speed * 100.0f, 1)) + "%";
                App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
            }
        }

        // Special adjustment while racing
        if (App::GetGameContext()->GetRaceSystem().IsRaceInProgress() && this->GetSimulationSpeed() != 1.0f)
        {
            m_last_simulation_speed = this->GetSimulationSpeed();
            this->SetSimulationSpeed(1.f);
        }
    }

    // EV_COMMON_TOGGLE_PHYSICS - Freeze/unfreeze physics
    if (App::GetInputEngine()->getEventBoolValueBounce(EV_COMMON_TOGGLE_PHYSICS))
    {
        this->SetSimulationPaused(!m_simulation_paused);

        if (m_simulation_paused)
        {
            String ssmsg = _L("Physics paused");
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
        }
        else
        {
            String ssmsg = _L("Physics unpaused");
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_NOTICE, ssmsg);
        }
    }

    // Calculate simulation time
    if (m_simulation_paused)
    {
        m_simulation_time = 0.f;

        // Frozen physics stepping
        if (this->GetSimulationSpeed() > 0.0f)
        {
            // EV_COMMON_REPLAY_FAST_FORWARD - Advance simulation while pressed
            // EV_COMMON_REPLAY_FORWARD - Advanced simulation one step
            if (App::GetInputEngine()->getEventBoolValue(EV_COMMON_REPLAY_FAST_FORWARD) ||
                App::GetInputEngine()->getEventBoolValueBounce(EV_COMMON_REPLAY_FORWARD))
            {
                m_simulation_time = PHYSICS_DT / this->GetSimulationSpeed();
            }
        }
    }
    else
    {
        m_simulation_time = dt;
    }
}

void ActorManager::UpdateTruckFeatures(ActorPtr vehicle, float dt)
{
    if (vehicle->isBeingReset() || vehicle->ar_physics_paused)
        return;
#ifdef USE_ANGELSCRIPT
    if (vehicle->ar_vehicle_ai && vehicle->ar_vehicle_ai->isActive())
        return;
#endif // USE_ANGELSCRIPT

    EnginePtr engine = vehicle->ar_engine;

    /* Anti roll-back is disabled when the brake input
     event is simulated for the vehicle.
     That way the brake simulated input can be freely
     modified without conflicts.*/
    if (engine && engine->hasContact() &&
        engine->getAutoMode() == SimGearboxMode::AUTO &&
        engine->getAutoShift() != Engine::NEUTRAL &&
        !vehicle->hasEventSimulatedValue(EV_TRUCK_BRAKE))
    {
        Ogre::Vector3 dirDiff = vehicle->getDirection();
        Ogre::Degree pitchAngle = Ogre::Radian(asin(dirDiff.dotProduct(Ogre::Vector3::UNIT_Y)));

        if (std::abs(pitchAngle.valueDegrees()) > 2.0f)
        {
            if (engine->getAutoShift() > Engine::NEUTRAL && vehicle->ar_avg_wheel_speed < +0.02f && pitchAngle.valueDegrees() > 0.0f ||
                engine->getAutoShift() < Engine::NEUTRAL && vehicle->ar_avg_wheel_speed > -0.02f && pitchAngle.valueDegrees() < 0.0f)
            {
                // anti roll back in SimGearboxMode::AUTO (DRIVE, TWO, ONE) mode
                // anti roll forth in SimGearboxMode::AUTO (REAR) mode
                float g = std::abs(App::GetGameContext()->GetTerrain()->getGravity());
                float downhill_force = std::abs(sin(pitchAngle.valueRadians()) * vehicle->getTotalMass()) * g;
                float engine_force = std::abs(engine->getTorque()) / vehicle->getAvgPropedWheelRadius();
                float ratio = std::max(0.0f, 1.0f - (engine_force / downhill_force));
                if (vehicle->ar_avg_wheel_speed * pitchAngle.valueDegrees() > 0.0f)
                {
                    ratio *= sqrt((0.02f - vehicle->ar_avg_wheel_speed) / 0.02f);
                }
                vehicle->ar_brake = sqrt(ratio);
            }
        }
        else if (vehicle->ar_brake == 0.0f && !vehicle->ar_parking_brake && engine->getTorque() == 0.0f)
        {
            float ratio = std::max(0.0f, 0.2f - std::abs(vehicle->ar_avg_wheel_speed)) / 0.2f;
            vehicle->ar_brake = ratio;
        }
    }

    if (vehicle->cc_mode)
    {
        vehicle->UpdateCruiseControl(dt);
    }
    if (vehicle->sl_enabled)
    {
        // check speed limit
        if (engine && engine->getGear() != 0)
        {
            float accl = (vehicle->sl_speed_limit - std::abs(vehicle->ar_wheel_speed / 1.02f)) * 2.0f;
            engine->setAcc(Ogre::Math::Clamp(accl, 0.0f, engine->getAcc()));
        }
    }

    BITMASK_SET(vehicle->m_lightmask, RoRnet::LIGHTMASK_BRAKES, (vehicle->ar_brake > 0.01f && !vehicle->ar_parking_brake));
    BITMASK_SET(vehicle->m_lightmask, RoRnet::LIGHTMASK_REVERSE, (vehicle->ar_engine && vehicle->ar_engine->getGear() < 0));
}

void ActorManager::CalcFreeForces()
{
    for (FreeForce& freeforce: m_free_forces)
    {
        // Sanity checks
        ROR_ASSERT(freeforce.ffc_base_actor != nullptr);
        ROR_ASSERT(freeforce.ffc_base_actor->ar_state != ActorState::DISPOSED);
        ROR_ASSERT(freeforce.ffc_base_node != NODENUM_INVALID);
        ROR_ASSERT(freeforce.ffc_base_node <= freeforce.ffc_base_actor->ar_num_nodes);

        
        switch (freeforce.ffc_type)
        {
            case FreeForceType::CONSTANT:
                freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].Forces += freeforce.ffc_force_magnitude * freeforce.ffc_force_const_direction;
                break;
            
            case FreeForceType::TOWARDS_COORDS:
                {
                    const Vector3 force_direction = (freeforce.ffc_target_coords - freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].AbsPosition).normalisedCopy();
                    freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].Forces += freeforce.ffc_force_magnitude * force_direction;
                }
                break;

            case FreeForceType::TOWARDS_NODE:
                {
                    // Sanity checks
                    ROR_ASSERT(freeforce.ffc_target_actor != nullptr);
                    ROR_ASSERT(freeforce.ffc_target_actor->ar_state != ActorState::DISPOSED);
                    ROR_ASSERT(freeforce.ffc_target_node != NODENUM_INVALID);
                    ROR_ASSERT(freeforce.ffc_target_node <= freeforce.ffc_target_actor->ar_num_nodes);

                    const Vector3 force_direction = (freeforce.ffc_target_actor->ar_nodes[freeforce.ffc_target_node].AbsPosition - freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].AbsPosition).normalisedCopy();
                    freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].Forces += freeforce.ffc_force_magnitude * force_direction;
                }
                break;

            case FreeForceType::HALFBEAM_GENERIC:
            case FreeForceType::HALFBEAM_ROPE:
            {
                // Sanity checks
                ROR_ASSERT(freeforce.ffc_target_actor != nullptr);
                ROR_ASSERT(freeforce.ffc_target_actor->ar_state != ActorState::DISPOSED);
                ROR_ASSERT(freeforce.ffc_target_node != NODENUM_INVALID);
                ROR_ASSERT(freeforce.ffc_target_node <= freeforce.ffc_target_actor->ar_num_nodes);

                // ---- BEGIN COPYPASTE of `Actor::CalcBeamsInterActor()` ----

                // FreeForce half-beams do not carry calibrated-material
                // configuration or history. They intentionally remain on the
                // bit-compatible legacy law; the strict-FP production step is
                // only for explicit per-beam calibrated opt-ins.

                // Calculate beam length
                node_t* p1 = &freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node];
                node_t* p2 = &freeforce.ffc_target_actor->ar_nodes[freeforce.ffc_target_node];
                const Vector3 dis = p1->AbsPosition - p2->AbsPosition;

                Real dislen = dis.squaredLength();
                if (!BeamAxialResponse::HasUsableLength(dislen))
                {
                    freeforce.ffc_halfb_stress = 0.0f;
                    break;
                }
                const Real inverted_dislen = fast_invSqrt(dislen);

                dislen *= inverted_dislen;

                // Calculate beam's deviation from normal
                Real difftoBeamL = dislen - freeforce.ffc_halfb_L;

                Real k = freeforce.ffc_halfb_spring;
                Real d = freeforce.ffc_halfb_damp;

                if (freeforce.ffc_type == FreeForceType::HALFBEAM_ROPE && difftoBeamL < 0.0f)
                {
                    k = 0.0f;
                    d *= 0.1f;
                }

                // Calculate beam's rate of change
                Vector3 v = p1->Velocity - p2->Velocity;
                const float relative_velocity = v.dotProduct(dis) * inverted_dislen;
                // Free beams are represented by two mirrored half-beams. Both
                // endpoint masses must share the same damping bound even
                // though this half applies force only to its base node.
                const BeamAxialResponse::DampingResult damping_response =
                    BeamAxialResponse::ComputeDamping(
                        relative_velocity,
                        d,
                        PHYSICS_DT,
                        p1->mass,
                        p2->mass,
                        !p1->nd_immovable,
                        !p2->nd_immovable);

                float slen = -k * difftoBeamL + damping_response.force;
                freeforce.ffc_halfb_stress = slen;

                // Fast test for deformation
                float len = std::abs(slen);
                if (len > freeforce.ffc_halfb_minmaxposnegstress)
                {
                    if (k != 0.0f)
                    {
                        // Actual deformation tests
                        if (slen > freeforce.ffc_halfb_maxposstress && difftoBeamL < 0.0f) // compression
                        {
                            Real yield_length = freeforce.ffc_halfb_maxposstress / k;
                            Real deform = difftoBeamL + yield_length * (1.0f - freeforce.ffc_halfb_plastic_coef);
                            Real Lold = freeforce.ffc_halfb_L;
                            freeforce.ffc_halfb_L += deform;
                            freeforce.ffc_halfb_L = std::max(MIN_BEAM_LENGTH, freeforce.ffc_halfb_L);
                            slen = slen - (slen - freeforce.ffc_halfb_maxposstress) * 0.5f;
                            len = slen;
                            if (freeforce.ffc_halfb_L > 0.0f && Lold > freeforce.ffc_halfb_L)
                            {
                                freeforce.ffc_halfb_maxposstress *= Lold / freeforce.ffc_halfb_L;
                                freeforce.ffc_halfb_minmaxposnegstress = std::min(freeforce.ffc_halfb_maxposstress, -freeforce.ffc_halfb_maxnegstress);
                                freeforce.ffc_halfb_minmaxposnegstress = std::min(freeforce.ffc_halfb_minmaxposnegstress, freeforce.ffc_halfb_strength);
                            }
                            // For the compression case we do not remove any of the beam's
                            // strength for structure stability reasons
                            //freeforce.ffc_halfb_strength += deform * k * 0.5f;

                            TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_DEFORMED, freeforce.ffc_id, 0, 0,
                                fmt::format("{}", slen), fmt::format("{}", freeforce.ffc_halfb_maxposstress));
                        }
                        else if (slen < freeforce.ffc_halfb_maxnegstress && difftoBeamL > 0.0f) // expansion
                        {
                            Real yield_length = freeforce.ffc_halfb_maxnegstress / k;
                            Real deform = difftoBeamL + yield_length * (1.0f - freeforce.ffc_halfb_plastic_coef);
                            Real Lold = freeforce.ffc_halfb_L;
                            freeforce.ffc_halfb_L += deform;
                            slen = slen - (slen - freeforce.ffc_halfb_maxnegstress) * 0.5f;
                            len = -slen;
                            if (Lold > 0.0f && freeforce.ffc_halfb_L > Lold)
                            {
                                freeforce.ffc_halfb_maxnegstress *= freeforce.ffc_halfb_L / Lold;
                                freeforce.ffc_halfb_minmaxposnegstress = std::min(freeforce.ffc_halfb_maxposstress, -freeforce.ffc_halfb_maxnegstress);
                                freeforce.ffc_halfb_minmaxposnegstress = std::min(freeforce.ffc_halfb_minmaxposnegstress, freeforce.ffc_halfb_strength);
                            }
                            freeforce.ffc_halfb_strength -= deform * k;

                            TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_DEFORMED, freeforce.ffc_id, 0, 0,
                                fmt::format("{}", slen), fmt::format("{}", freeforce.ffc_halfb_maxnegstress));
                        }
                    }

                    // Test if the beam should break
                    if (len > freeforce.ffc_halfb_strength)
                    {
                        // Sound effect.
                        // Sound volume depends on springs stored energy
                        SOUND_MODULATE(freeforce.ffc_base_actor->ar_instance_id, SS_MOD_BREAK, 0.5 * k * difftoBeamL * difftoBeamL);
                        SOUND_PLAY_ONCE(freeforce.ffc_base_actor->ar_instance_id, SS_TRIG_BREAK);

                        freeforce.ffc_type = FreeForceType::DUMMY;

                        TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_BROKEN, freeforce.ffc_id, 0, 0,
                            fmt::format("{}", len), fmt::format("{}", freeforce.ffc_halfb_strength));
                    }
                }

                // At last update the beam forces
                Vector3 f = dis;
                f *= (slen * inverted_dislen);
                p1->Forces += f;
                // ---- END COPYPASTE of `Actor::CalcBeamsInterActor()` ----
            }
            break;

            default:
                break;
        }
    }
}

static bool ProcessFreeForce(FreeForceRequest* rq, FreeForce& freeforce)
{
    // internal helper for processing add/modify requests, with checks
    // ---------------------------------------------------------------

    // Unchecked stuff
    freeforce.ffc_id = (FreeForceID_t)rq->ffr_id;
    freeforce.ffc_type = (FreeForceType)rq->ffr_type;
    freeforce.ffc_force_magnitude = (float)rq->ffr_force_magnitude;
    freeforce.ffc_force_const_direction = rq->ffr_force_const_direction;
    freeforce.ffc_target_coords = rq->ffr_target_coords;

    // Base actor
    freeforce.ffc_base_actor = App::GetGameContext()->GetActorManager()->GetActorById(rq->ffr_base_actor);
    ROR_ASSERT(freeforce.ffc_base_actor != nullptr && freeforce.ffc_base_actor->ar_state != ActorState::DISPOSED);
    if (!freeforce.ffc_base_actor || freeforce.ffc_base_actor->ar_state == ActorState::DISPOSED)
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
            fmt::format("Cannot add free force with ID {} to actor {}: Base actor not found or disposed", freeforce.ffc_id, rq->ffr_base_actor));
        return false;
    }

    // Base node
    ROR_ASSERT(rq->ffr_base_node >= 0);
    ROR_ASSERT(rq->ffr_base_node <= NODENUM_MAX);
    ROR_ASSERT(rq->ffr_base_node <= freeforce.ffc_base_actor->ar_num_nodes);
    if (rq->ffr_base_node < 0 || rq->ffr_base_node >= NODENUM_MAX || rq->ffr_base_node >= freeforce.ffc_base_actor->ar_num_nodes)
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
            fmt::format("Cannot add free force with ID {} to actor {}: Invalid base node number {}", freeforce.ffc_id, rq->ffr_base_actor, rq->ffr_base_node));
        return false;
    }
    freeforce.ffc_base_node = (NodeNum_t)rq->ffr_base_node;

    if (freeforce.ffc_type == FreeForceType::TOWARDS_NODE ||
        freeforce.ffc_type == FreeForceType::HALFBEAM_GENERIC ||
        freeforce.ffc_type == FreeForceType::HALFBEAM_ROPE)
    {
        // Target actor
        freeforce.ffc_target_actor = App::GetGameContext()->GetActorManager()->GetActorById(rq->ffr_target_actor);
        ROR_ASSERT(freeforce.ffc_target_actor != nullptr && freeforce.ffc_target_actor->ar_state != ActorState::DISPOSED);
        if (!freeforce.ffc_target_actor || freeforce.ffc_target_actor->ar_state == ActorState::DISPOSED)
        {
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
                fmt::format("Cannot add free force of type 'TOWARDS_NODE' with ID {} to actor {}: Target actor not found or disposed", freeforce.ffc_id, rq->ffr_target_actor));
            return false;
        }

        // Target node
        ROR_ASSERT(rq->ffr_target_node >= 0);
        ROR_ASSERT(rq->ffr_target_node <= NODENUM_MAX);
        ROR_ASSERT(rq->ffr_target_node <= freeforce.ffc_target_actor->ar_num_nodes);
        if (rq->ffr_target_node < 0 || rq->ffr_target_node >= NODENUM_MAX || rq->ffr_target_node >= freeforce.ffc_target_actor->ar_num_nodes)
        {
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
                fmt::format("Cannot add free force of type 'TOWARDS_NODE' with ID {} to actor {}: Invalid target node number {}", freeforce.ffc_id, rq->ffr_target_actor, rq->ffr_target_node));
            return false;
        }
        freeforce.ffc_target_node = (NodeNum_t)rq->ffr_target_node;

        if (freeforce.ffc_type == FreeForceType::HALFBEAM_GENERIC ||
            freeforce.ffc_type == FreeForceType::HALFBEAM_ROPE)
        {
            freeforce.ffc_halfb_spring = (float)rq->ffr_halfb_spring;
            freeforce.ffc_halfb_damp = (float)rq->ffr_halfb_damp;
            freeforce.ffc_halfb_strength = (float)rq->ffr_halfb_strength;
            freeforce.ffc_halfb_deform = (float)rq->ffr_halfb_deform;
            freeforce.ffc_halfb_diameter = (float)rq->ffr_halfb_diameter;
            freeforce.ffc_halfb_plastic_coef = (float)rq->ffr_halfb_plastic_coef;

            freeforce.ffc_halfb_minmaxposnegstress = (float)rq->ffr_halfb_deform;
            freeforce.ffc_halfb_maxposstress = (float)rq->ffr_halfb_deform;
            freeforce.ffc_halfb_maxnegstress = -(float)rq->ffr_halfb_deform;

            // Calc length
            const Ogre::Vector3 base_pos = freeforce.ffc_base_actor->ar_nodes[freeforce.ffc_base_node].AbsPosition;
            const Ogre::Vector3 target_pos = freeforce.ffc_target_actor->ar_nodes[freeforce.ffc_target_node].AbsPosition;
            freeforce.ffc_halfb_L = target_pos.distance(base_pos);
        }
    }

    return true;
}

bool ActorManager::FindFreeForce(FreeForceID_t id, ActorManager::FreeForceVec_t::iterator& out_itor)
{
    out_itor = std::find_if(m_free_forces.begin(), m_free_forces.end(), [id](FreeForce& item) { return id == item.ffc_id; });
    return out_itor != m_free_forces.end();
}

void ActorManager::AddFreeForce(FreeForceRequest* rq)
{
    // Make sure ID is unique
    ActorManager::FreeForceVec_t::iterator it;
    if (this->FindFreeForce(rq->ffr_id, it))
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
            fmt::format("Cannot add free force with ID {}: ID already in use", rq->ffr_id));
        return;
    }

    FreeForce freeforce;
    if (ProcessFreeForce(rq, freeforce))
    {
        m_free_forces.push_back(freeforce);
        TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_ADDED, rq->ffr_id);
    }
}

void ActorManager::ModifyFreeForce(FreeForceRequest* rq)
{
    ActorManager::FreeForceVec_t::iterator it;
    if (!this->FindFreeForce(rq->ffr_id, it))
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
            fmt::format("Cannot modify free force with ID {}: ID not found", rq->ffr_id));
        return;
    }

    FreeForce& freeforce = *it;
    if (ProcessFreeForce(rq, freeforce))
    {
        *it = freeforce;
        TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_MODIFIED, rq->ffr_id);
    }
}

void ActorManager::RemoveFreeForce(FreeForceID_t id)
{
    ActorManager::FreeForceVec_t::iterator it;
    if (!this->FindFreeForce(id, it))
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_ERROR, 
            fmt::format("Cannot remove free force with ID {}: ID not found", id));
        return;
    }

    m_free_forces.erase(it);
    TRIGGER_EVENT_ASYNC(SE_GENERIC_FREEFORCES_ACTIVITY, FREEFORCESACTIVITY_REMOVED, id);
}
