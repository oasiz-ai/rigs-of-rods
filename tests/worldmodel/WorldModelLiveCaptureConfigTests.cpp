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
