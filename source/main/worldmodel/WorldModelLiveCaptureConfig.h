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

/// Exact schema-1 control surface, sorted and unique.
const std::vector<std::string>& LiveCaptureControlIds();

/// Canonical activation encoding used as the configuration SHA-256 input.
/// Runtime controller/camera profiles are inspected from the provider itself.
std::string CanonicalLiveCaptureConfig(
    const LiveCaptureActivationConfig& config);

} // namespace WorldModel
} // namespace RoR
