/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Dependency-light validation and hashing for live capture activation.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

static const std::uint64_t MAX_LIVE_CAPTURE_TRANSITIONS =
    UINT64_C(48) * UINT64_C(60) * UINT64_C(60);
static const std::uint64_t MAX_LIVE_CAPTURE_SOURCE_BYTES =
    UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024) *
    UINT64_C(1024);

struct LiveCaptureActivationConfig
{
    std::string output_root;
    std::uint64_t root_seed = 0U;
    std::uint64_t episode_ordinal = 0U;
    std::uint64_t transition_count = 0U;
    std::uint32_t rgb_width = 0U;
    std::uint32_t rgb_height = 0U;
    std::string rights_manifest_path;
    std::string rights_manifest_sha256;
    std::string data_source_id;
    std::string participant_release_id;
    std::string allowed_use_id;
};

/// Snapshot of runtime choices which can change the spawned vehicle, terrain
/// collision world, or fixed-step physics without changing the base resource
/// archives. Schema 1 intentionally admits only one canonical profile until
/// these choices have their own authenticated, reproducible encodings.
struct LiveCaptureRuntimeState
{
    bool has_section_config = false;
    bool has_working_tuneup = false;
    bool has_skin = false;
    std::uint64_t addonpart_count = 0U;
    std::uint64_t assetpack_count = 0U;
    bool has_inter_point_collision_detector = false;
    bool has_intra_point_collision_detector = false;
    bool has_replay_handler = false;
    bool terrain_collision_profile_canonical = false;

    bool sim_spawn_running = false;
    bool sim_replay_enabled = false;
    bool sim_realistic_commands = false;
    bool sim_races_enabled = false;
    bool sim_no_collisions = false;
    bool sim_no_self_collisions = false;
    bool sim_deterministic_sleeping_engine = false;
    int sim_deterministic_fixed_steps_per_frame = 0;
};

/// Parses the full unsigned 64-bit decimal grammar. Signs, whitespace,
/// prefixes, leading zeroes (except "0"), and overflow are rejected.
bool ParseCanonicalU64(
    const std::string& text,
    std::uint64_t& value);

/// Validates the explicit operator-owned activation inputs. Rights and
/// allowed-use values have no defaults: capture cannot invent consent.
bool ValidateLiveCaptureActivationConfig(
    const LiveCaptureActivationConfig& config,
    std::string* error = nullptr);

/// Enforces the schema-1 runtime profile. Optional vehicle configuration is
/// refused rather than silently represented by the base vehicle hash, and
/// simulation CVars are fixed to the canonical values encoded by
/// CanonicalLiveCaptureConfig().
bool ValidateLiveCaptureRuntimeState(
    const LiveCaptureRuntimeState& state,
    std::string* error = nullptr);

/// Validates that the live analog steering parameters still equal the values
/// sealed into episode provenance before the provider advances another batch.
bool ValidateLiveCaptureAnalogInputState(
    float expected_smoothing,
    float expected_sensitivity,
    float current_smoothing,
    float current_sensitivity,
    std::string* error = nullptr);

/// Exact schema-1 control surface, sorted and unique.
const std::vector<std::string>& LiveCaptureControlIds();

/// Canonical activation encoding used as the configuration SHA-256 input.
/// Runtime controller/camera profiles are inspected from the provider itself.
std::string CanonicalLiveCaptureConfig(
    const LiveCaptureActivationConfig& config);

} // namespace WorldModel
} // namespace RoR
