/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Offline integrity validator for completed world-model episodes.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace RoR {
namespace WorldModel {

enum class EpisodeValidationError
{
    NONE = 0,
    PATH_IS_PARTIAL,
    MISSING_ARTIFACT,
    UNEXPECTED_ARTIFACT,
    TEMPORARY_ARTIFACT_PRESENT,
    INVALID_OPEN_MANIFEST,
    INVALID_PROVENANCE,
    INVALID_MANIFEST,
    INVALID_COMPLETION_MARKER,
    COMPLETION_MISMATCH,
    INVALID_CHECKSUMS,
    MISSING_CHUNK,
    UNEXPECTED_CHUNK,
    CHUNK_SIZE_MISMATCH,
    CHUNK_HASH_MISMATCH,
    INVALID_CHUNK_HEADER,
    INVALID_RECORD_HEADER,
    TRUNCATED_RECORD,
    RECORD_CRC_MISMATCH,
    RECORD_SEQUENCE_MISMATCH,
    RECORD_COUNT_MISMATCH,
    IO_ERROR
};

struct EpisodeValidationResult
{
    EpisodeValidationError error;
    std::filesystem::path artifact;
    std::string detail;
    std::uint64_t telemetry_record_count;
    std::uint64_t rgb_record_count;
    std::uint32_t telemetry_chunk_count;
    std::uint32_t rgb_chunk_count;
    EpisodeValidationResult();
    bool IsValid() const;
};

class EpisodeValidator
{
public:
    /// Completed-artifact integrity validation. This verifies crash-safe
    /// framing, inventory, hashes, and record sequencing only; it does not
    /// validate telemetry semantics or make an episode training-ready. A path
    /// ending in .partial is always rejected.
    static EpisodeValidationResult Validate(
        const std::filesystem::path& episode_directory);

    /// Producer-side integrity-only gate used after COMPLETE.json is durable
    /// but before the .partial directory is renamed into public visibility.
    static EpisodeValidationResult ValidateForPublication(
        const std::filesystem::path& partial_episode_directory);
};

const char* EpisodeValidationErrorName(EpisodeValidationError error);

} // namespace WorldModel
} // namespace RoR
