/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelLiveCaptureConfig.h"

#include "WorldModelTelemetry.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>

namespace {

bool Fail(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

bool NonzeroSha256(const std::string& value)
{
    return RoR::WorldModel::IsCanonicalSha256(value) &&
        value.find_first_not_of('0') != std::string::npos;
}

bool SafePathText(const std::string& value)
{
    if (value.empty() || value.size() > 4096U)
        return false;
    for (const unsigned char byte : value)
    {
        if (byte < 0x20U || byte == 0x7fU)
            return false;
    }
    return true;
}

} // namespace

namespace RoR {
namespace WorldModel {

bool ParseCanonicalU64(
    const std::string& text,
    std::uint64_t& value)
{
    if (text.empty() ||
        (text.size() > 1U && text.front() == '0'))
    {
        return false;
    }
    std::uint64_t candidate = 0U;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
            return false;
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - '0');
        if (candidate >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
        {
            return false;
        }
        candidate = candidate * 10U + digit;
    }
    value = candidate;
    return true;
}

bool ValidateLiveCaptureActivationConfig(
    const LiveCaptureActivationConfig& config,
    std::string* error)
{
    if (!SafePathText(config.output_root))
    {
        return Fail(
            error,
            "capture output root has an invalid length/control byte");
    }
    const std::filesystem::path output_root(config.output_root);
    const std::filesystem::path normalized_output_root =
        output_root.lexically_normal();
    if (!output_root.is_absolute() ||
        normalized_output_root == normalized_output_root.root_path() ||
        normalized_output_root.filename().empty() ||
        normalized_output_root.filename() == "." ||
        normalized_output_root.filename() == "..")
    {
        return Fail(
            error,
            "capture output root must be an absolute non-root directory");
    }
    if (config.root_seed == 0U)
        return Fail(error, "capture root seed must be nonzero");
    if (config.transition_count == 0U ||
        config.transition_count > MAX_LIVE_CAPTURE_TRANSITIONS)
    {
        return Fail(
            error,
            "capture transition count must be within one hour at 48 Hz");
    }
    if (config.rgb_width == 0U || config.rgb_height == 0U ||
        config.rgb_width > 4096U || config.rgb_height > 2160U)
    {
        return Fail(
            error,
            "capture RGB dimensions must fit within 4096x2160");
    }
    const std::uint64_t stride =
        static_cast<std::uint64_t>(config.rgb_width) * 3U;
    const std::uint64_t rgb_bytes =
        stride * static_cast<std::uint64_t>(config.rgb_height);
    if (rgb_bytes > UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
    {
        return Fail(error, "capture RGB frame exceeds 64 MiB");
    }
    const std::uint64_t observation_count =
        config.transition_count + 1U;
    if (rgb_bytes >
        MAX_LIVE_CAPTURE_SOURCE_BYTES / observation_count)
    {
        return Fail(
            error,
            "capture raw RGB source exceeds the 16 GiB episode contract");
    }
    if (!SafePathText(config.rights_manifest_path))
    {
        return Fail(
            error,
            "rights manifest path has an invalid length/control byte");
    }
    const std::filesystem::path rights_path(
        config.rights_manifest_path);
    const std::filesystem::path normalized_rights_path =
        rights_path.lexically_normal();
    if (!rights_path.is_absolute() ||
        normalized_rights_path ==
            normalized_rights_path.root_path() ||
        normalized_rights_path.filename().empty() ||
        normalized_rights_path.filename() == "." ||
        normalized_rights_path.filename() == "..")
    {
        return Fail(
            error,
            "rights manifest path must be an absolute non-root file");
    }
    if (!NonzeroSha256(config.rights_manifest_sha256))
    {
        return Fail(
            error,
            "rights manifest must be an explicit nonzero SHA-256");
    }
    if (!IsCanonicalWorldModelIdentifier(config.data_source_id))
        return Fail(error, "data source id is not canonical");
    if (!IsCanonicalWorldModelIdentifier(
            config.participant_release_id))
    {
        return Fail(error, "participant release id is not canonical");
    }
    if (!IsCanonicalWorldModelIdentifier(config.allowed_use_id))
        return Fail(error, "allowed use id is not canonical");
    return true;
}

bool ValidateLiveCaptureRuntimeState(
    const LiveCaptureRuntimeState& state,
    std::string* error)
{
    if (state.has_section_config)
    {
        return Fail(
            error,
            "schema 1 does not admit vehicle section configurations");
    }
    if (state.has_working_tuneup)
        return Fail(error, "schema 1 does not admit vehicle tuneups");
    if (state.has_skin)
        return Fail(error, "schema 1 does not admit vehicle skins");
    if (state.addonpart_count != 0U)
        return Fail(error, "schema 1 does not admit vehicle addon parts");
    if (state.assetpack_count != 0U)
        return Fail(error, "schema 1 does not admit vehicle asset packs");
    if (!state.has_inter_point_collision_detector)
    {
        return Fail(
            error,
            "schema 1 requires the spawned inter-actor collision detector");
    }
    if (!state.has_intra_point_collision_detector)
    {
        return Fail(
            error,
            "schema 1 requires the spawned self-collision detector");
    }
    if (state.has_replay_handler)
        return Fail(error, "schema 1 does not admit a spawned replay handler");
    if (!state.terrain_collision_profile_canonical)
    {
        return Fail(
            error,
            "schema 1 requires terrain race collisions loaded under the "
            "canonical policy");
    }

    if (!state.sim_spawn_running)
        return Fail(error, "schema 1 requires sim_spawn_running=true");
    if (state.sim_replay_enabled)
        return Fail(error, "schema 1 requires sim_replay_enabled=false");
    if (state.sim_realistic_commands)
    {
        return Fail(
            error,
            "schema 1 requires sim_realistic_commands=false");
    }
    if (!state.sim_races_enabled)
        return Fail(error, "schema 1 requires sim_races_enabled=true");
    if (state.sim_no_collisions)
        return Fail(error, "schema 1 requires sim_no_collisions=false");
    if (state.sim_no_self_collisions)
    {
        return Fail(
            error,
            "schema 1 requires sim_no_self_collisions=false");
    }
    if (!state.sim_deterministic_sleeping_engine)
    {
        return Fail(
            error,
            "schema 1 requires sim_deterministic_sleeping_engine=true");
    }
    if (state.sim_deterministic_fixed_steps_per_frame != 0)
    {
        return Fail(
            error,
            "schema 1 requires "
            "sim_deterministic_fixed_steps_per_frame=0");
    }
    return true;
}

bool ValidateLiveCaptureAnalogInputState(
    float expected_smoothing,
    float expected_sensitivity,
    float current_smoothing,
    float current_sensitivity,
    std::string* error)
{
    if (!std::isfinite(expected_smoothing) ||
        !std::isfinite(expected_sensitivity) ||
        !std::isfinite(current_smoothing) ||
        !std::isfinite(current_sensitivity))
    {
        return Fail(
            error,
            "schema-1 analog input parameters must be finite");
    }
    if (current_smoothing != expected_smoothing)
    {
        return Fail(
            error,
            "io_analog_smoothing changed after capture provenance was sealed");
    }
    if (current_sensitivity != expected_sensitivity)
    {
        return Fail(
            error,
            "io_analog_sensitivity changed after capture provenance was sealed");
    }
    return true;
}

const std::vector<std::string>& LiveCaptureControlIds()
{
    static const std::vector<std::string> IDS = {
        "vehicle.brake",
        "vehicle.clutch",
        "vehicle.parking-brake",
        "vehicle.steering",
        "vehicle.throttle"};
    return IDS;
}

std::string CanonicalLiveCaptureConfig(
    const LiveCaptureActivationConfig& config)
{
    std::ostringstream stream;
    stream
        << "org.rigsofrods.worldmodel.live-capture-config@1\n"
        << "root_seed=" << config.root_seed << '\n'
        << "episode_ordinal=" << config.episode_ordinal << '\n'
        << "transition_count=" << config.transition_count << '\n'
        << "physics_rate_hz=2000\n"
        << "observation_rate_hz=48\n"
        << "rgb_width=" << config.rgb_width << '\n'
        << "rgb_height=" << config.rgb_height << '\n'
        << "matrix_order=row-major\n"
        << "coordinate_frame=ror.world.rh-y-up\n"
        << "color_space=srgb\n"
        << "pixel_format=rgb8\n"
        << "water=disabled\n"
        << "scripts=disabled\n"
        << "multiplayer=disabled\n"
        << "dynamic_world=disabled\n"
        << "vehicle_section_config=forbidden\n"
        << "vehicle_tuneup=forbidden\n"
        << "vehicle_skin=forbidden\n"
        << "vehicle_addonparts=forbidden\n"
        << "vehicle_assetpacks=forbidden\n"
        << "spawned_point_collision_detectors=required\n"
        << "spawned_replay_handler=forbidden\n"
        << "terrain_race_collisions=enabled-at-load\n"
        << "sim_spawn_running=1\n"
        << "sim_replay_enabled=0\n"
        << "sim_realistic_commands=0\n"
        << "sim_races_enabled=1\n"
        << "sim_no_collisions=0\n"
        << "sim_no_self_collisions=0\n"
        << "sim_deterministic_sleeping_engine=1\n"
        << "sim_deterministic_fixed_steps_per_frame=0\n"
        << "rights_manifest_sha256="
        << config.rights_manifest_sha256 << '\n'
        << "data_source_id=" << config.data_source_id << '\n'
        << "participant_release_id="
        << config.participant_release_id << '\n'
        << "allowed_use_id=" << config.allowed_use_id << '\n';
    return stream.str();
}

} // namespace WorldModel
} // namespace RoR
