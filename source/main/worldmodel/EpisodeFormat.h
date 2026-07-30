/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Dependency-free integrity primitives for world-model episodes.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace RoR {
namespace WorldModel {

static const std::uint32_t EPISODE_FORMAT_VERSION = 1;
static const std::uint32_t MAX_EPISODE_RECORD_BYTES =
    UINT32_C(256) * UINT32_C(1024) * UINT32_C(1024);

enum class EpisodeStream : std::uint32_t
{
    TELEMETRY = 1,
    RGB = 2
};

struct Hash256
{
    std::array<std::uint8_t, 32> bytes;
    Hash256();
    std::string ToHex() const;
    bool operator==(const Hash256& other) const;
    bool operator!=(const Hash256& other) const;
    static bool FromHex(const std::string& hex, Hash256& hash);
};

std::uint32_t ComputeCrc32c(const void* bytes, std::size_t size);
Hash256 ComputeSha256(const void* bytes, std::size_t size);
bool ComputeFileSha256(
    const std::filesystem::path& path,
    Hash256& hash,
    std::string* error = nullptr);
bool IsValidEpisodeId(const std::string& episode_id);
const char* EpisodeStreamName(EpisodeStream stream);

} // namespace WorldModel
} // namespace RoR
