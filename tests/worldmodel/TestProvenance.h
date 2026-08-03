#pragma once

#include "EpisodeFormat.h"
#include "EpisodeProvenance.h"
#include "WorldModelCaptureContract.h"

#include <string>

namespace RoRWorldModelTest {

inline std::string HashText(const std::string& value)
{
    return RoR::WorldModel::ComputeSha256(
        value.data(),
        value.size()).ToHex();
}

inline RoR::WorldModel::EpisodeProvenance MakeProvenance(
    const RoR::WorldModel::EpisodeId& episode,
    const std::string& vehicle_id = "truck/main",
    const std::string& terrain_id = "test/default",
    const std::string& camera_id = "driver/main")
{
    using namespace RoR::WorldModel;
    EpisodeProvenance value;
    value.root_seed = UINT64_C(0x4f4153495a524f52);
    value.reset_seed = DeriveSeed(
        value.root_seed,
        SeedDomain::RESET,
        episode,
        0U);
    value.engine_commit =
        "27ad2540075e45020498840859a0a386a2e5f814";
    value.engine_branch = "agent/world-model-recorder";
    value.build_id = "test/native-recorder";
    value.build_sha256 = HashText("test-build");
    value.os_id = "test/linux";
    value.gpu_id = "test/software-renderer";
    value.driver_id = "test/driver";
    value.config_sha256 = HashText("test-config");
    value.vehicle_id = vehicle_id;
    value.vehicle_sha256 = HashText(vehicle_id);
    value.terrain_id = terrain_id;
    value.terrain_sha256 = HashText(terrain_id);
    value.controller_profile_id = "test/throttle-only";
    value.controller_profile_sha256 =
        HashText("vehicle.throttle");
    value.control_ids = {"vehicle.throttle"};
    value.camera_profile_id = camera_id;
    value.camera_profile_sha256 = HashText(camera_id);
    value.reset_state_sha256 = HashText("fresh-reset-state");
    value.rights_manifest_sha256 = HashText("test-rights");
    value.data_source_id = "scripted/test";
    value.participant_release_id = "not-applicable";
    value.allowed_use_id = "research/test-only";
    value.matrix_order = "row-major";
    value.coordinate_frame = "ror.world.rh-y-up";
    value.color_space = "srgb";
    value.pixel_format = "rgb8";
    return value;
}

inline RoR::WorldModel::EpisodeProvenance MakeProvenance(
    const std::string& episode_id,
    const std::string& vehicle_id = "truck/main",
    const std::string& terrain_id = "test/default",
    const std::string& camera_id = "driver/main")
{
    RoR::WorldModel::EpisodeId episode;
    if (!RoR::WorldModel::ParseEpisodeId(episode_id, episode))
        return RoR::WorldModel::EpisodeProvenance();
    return MakeProvenance(
        episode,
        vehicle_id,
        terrain_id,
        camera_id);
}

} // namespace RoRWorldModelTest
