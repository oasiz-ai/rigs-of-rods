/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Reproducibility and rights boundary sealed into every episode.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

struct EpisodeProvenance
{
    std::uint64_t root_seed = 0U;
    std::uint64_t reset_seed = 0U;
    std::string engine_commit;
    std::string engine_branch;
    std::string build_id;
    std::string build_sha256;
    std::string os_id;
    std::string gpu_id;
    std::string driver_id;
    std::string config_sha256;
    std::string vehicle_id;
    std::string vehicle_sha256;
    std::string terrain_id;
    std::string terrain_sha256;
    std::string controller_profile_id;
    std::string controller_profile_sha256;
    std::vector<std::string> control_ids;
    std::string camera_profile_id;
    std::string camera_profile_sha256;
    std::string reset_state_sha256;
    std::string rights_manifest_sha256;
    std::string data_source_id;
    std::string participant_release_id;
    std::string allowed_use_id;
    std::string matrix_order;
    std::string coordinate_frame;
    std::string color_space;
    std::string pixel_format;
};

bool ValidateEpisodeProvenance(
    const EpisodeProvenance& provenance,
    std::string* error = nullptr);

/// Canonical LF-terminated JSON. On failure, output is unchanged.
bool SerializeEpisodeProvenance(
    const EpisodeProvenance& provenance,
    std::string& output,
    std::string* error = nullptr);

/// Accepts only the exact schema-1 producer encoding.
bool ParseEpisodeProvenance(
    const std::string& text,
    EpisodeProvenance& provenance,
    std::string* error = nullptr);

} // namespace WorldModel
} // namespace RoR
