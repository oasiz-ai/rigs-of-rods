/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeFormat.h"
#include "WorldModelLiveCaptureConfig.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

RoR::WorldModel::LiveCaptureActivationConfig GoodConfig()
{
    RoR::WorldModel::LiveCaptureActivationConfig config;
    config.output_root = "/capture";
    config.root_seed = 42U;
    config.episode_ordinal = 7U;
    config.transition_count = 48U;
    config.rgb_width = 1920U;
    config.rgb_height = 1080U;
    config.rights_manifest_path = "/capture/rights.json";
    config.rights_manifest_sha256 =
        RoR::WorldModel::ComputeSha256("rights", 6U).ToHex();
    config.data_source_id = "beam-cloud/canary";
    config.participant_release_id = "canary/open";
    config.allowed_use_id = "world-model-training";
    return config;
}

RoR::WorldModel::LiveCaptureRuntimeState GoodRuntimeState()
{
    RoR::WorldModel::LiveCaptureRuntimeState state;
    state.has_inter_point_collision_detector = true;
    state.has_intra_point_collision_detector = true;
    state.terrain_collision_profile_canonical = true;
    state.sim_spawn_running = true;
    state.sim_races_enabled = true;
    state.sim_deterministic_sleeping_engine = true;
    return state;
}

} // namespace

int main()
{
    using namespace RoR::WorldModel;

    std::uint64_t parsed = 99U;
    Check(ParseCanonicalU64("0", parsed) && parsed == 0U,
        "canonical zero must parse");
    Check(ParseCanonicalU64(
            "18446744073709551615", parsed) &&
            parsed == std::numeric_limits<std::uint64_t>::max(),
        "UINT64_MAX must parse");
    for (const std::string& invalid : {
            "", "00", "01", "-1", "+1", " 1", "1 ",
            "0x1", "18446744073709551616"})
    {
        Check(!ParseCanonicalU64(invalid, parsed),
            "noncanonical/overflow integer accepted");
    }

    LiveCaptureActivationConfig config = GoodConfig();
    std::string error;
    Check(ValidateLiveCaptureActivationConfig(config, &error),
        error.c_str());
    Check(LiveCaptureControlIds().size() == 5U,
        "schema-1 control profile changed");

    const std::string canonical = CanonicalLiveCaptureConfig(config);
    Check(canonical.find("water=disabled\n") != std::string::npos,
        "config does not seal water policy");
    Check(canonical.find("allowed_use_id=world-model-training\n") !=
            std::string::npos,
        "config does not bind allowed use");
    Check(canonical.find("vehicle_tuneup=forbidden\n") !=
            std::string::npos,
        "config does not seal optional vehicle-content policy");
    Check(canonical.find("sim_no_self_collisions=0\n") !=
            std::string::npos,
        "config does not seal self-collision policy");

    LiveCaptureRuntimeState runtime_state = GoodRuntimeState();
    Check(ValidateLiveCaptureRuntimeState(runtime_state, &error),
        error.c_str());

    runtime_state = GoodRuntimeState();
    runtime_state.has_section_config = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "vehicle section configuration accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.has_working_tuneup = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "vehicle tuneup accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.has_skin = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "vehicle skin accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.addonpart_count = 1U;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "vehicle addon part accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.assetpack_count = 1U;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "vehicle asset pack accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.has_inter_point_collision_detector = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "missing spawned inter-actor collision detector accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.has_intra_point_collision_detector = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "missing spawned self-collision detector accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.has_replay_handler = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "spawned replay handler accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.terrain_collision_profile_canonical = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "terrain collision world loaded under a noncanonical policy accepted");

    runtime_state = GoodRuntimeState();
    runtime_state.sim_spawn_running = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "noncanonical engine spawn state accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_replay_enabled = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "replay-enabled runtime accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_realistic_commands = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "realistic-command runtime accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_races_enabled = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "terrain race-collision policy drift accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_no_collisions = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "disabled inter-actor collisions accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_no_self_collisions = true;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "disabled self collisions accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_deterministic_sleeping_engine = false;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "nondeterministic sleeping-engine cadence accepted");
    runtime_state = GoodRuntimeState();
    runtime_state.sim_deterministic_fixed_steps_per_frame = 1;
    Check(!ValidateLiveCaptureRuntimeState(runtime_state, &error),
        "competing fixed-step scheduler accepted");

    Check(ValidateLiveCaptureAnalogInputState(
            1.0f, 1.0f, 1.0f, 1.0f, &error),
        error.c_str());
    Check(!ValidateLiveCaptureAnalogInputState(
            1.0f, 1.0f, 1.25f, 1.0f, &error),
        "analog smoothing drift accepted");
    Check(!ValidateLiveCaptureAnalogInputState(
            1.0f, 1.0f, 1.0f, 0.75f, &error),
        "analog sensitivity drift accepted");
    Check(!ValidateLiveCaptureAnalogInputState(
            std::numeric_limits<float>::quiet_NaN(),
            1.0f,
            std::numeric_limits<float>::quiet_NaN(),
            1.0f,
            &error),
        "non-finite analog input parameter accepted");

    config.rights_manifest_sha256 = std::string(64U, '0');
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "zero rights hash accepted");
    config = GoodConfig();
    config.allowed_use_id.clear();
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "implicit allowed-use accepted");
    config = GoodConfig();
    config.rgb_width = 8192U;
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "unbounded RGB allocation accepted");
    config = GoodConfig();
    config.output_root = "/";
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "filesystem root accepted as capture output");
    config = GoodConfig();
    config.output_root = "/capture/..";
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "lexically disguised filesystem root accepted");
    config = GoodConfig();
    config.output_root = "capture";
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "relative capture output accepted");
    config = GoodConfig();
    config.output_root = "/capture\nforged-log-entry";
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "control byte accepted in capture output");
    config = GoodConfig();
    config.rights_manifest_path = "rights.json";
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "relative rights manifest accepted");
    config = GoodConfig();
    config.transition_count = MAX_LIVE_CAPTURE_TRANSITIONS + 1U;
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "unbounded capture transition count accepted");
    config = GoodConfig();
    config.rgb_width = 4096U;
    config.rgb_height = 2160U;
    config.transition_count = 646U;
    Check(ValidateLiveCaptureActivationConfig(config, &error),
        "16 GiB raw RGB boundary rejected");
    config.transition_count = 647U;
    Check(!ValidateLiveCaptureActivationConfig(config, &error),
        "raw RGB episode above the 16 GiB source contract accepted");

    std::cout << "World-model live capture config tests passed\n";
    return 0;
}
