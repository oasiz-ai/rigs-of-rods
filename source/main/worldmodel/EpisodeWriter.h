/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Crash-safe append-only writer for world-model episode artifacts.

#pragma once

#include "EpisodeFormat.h"
#include "EpisodeProvenance.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace RoR {
namespace WorldModel {

struct EpisodeWriterOptions
{
    std::uint32_t max_records_per_chunk;
    std::uint64_t max_chunk_bytes;
    EpisodeWriterOptions();
};

/// The episode id is the capture contract's non-zero 128-bit identifier
/// encoded as exactly 32 lowercase hexadecimal characters. The writer uses
/// episode-<id>.partial and publishes episode-<id> only after all
/// chunks, checksums, the sealed manifest, and COMPLETE.json are durable.
class EpisodeWriter
{
public:
    EpisodeWriter();
    ~EpisodeWriter();
    EpisodeWriter(const EpisodeWriter&) = delete;
    EpisodeWriter& operator=(const EpisodeWriter&) = delete;

    bool Open(
        const std::filesystem::path& output_root,
        const std::string& episode_id,
        const EpisodeProvenance& provenance,
        const EpisodeWriterOptions& options = EpisodeWriterOptions(),
        std::string* error = nullptr);
    bool AppendTelemetryRecord(
        std::uint64_t record_id,
        std::uint32_t record_type,
        const void* payload,
        std::size_t payload_size,
        std::string* error = nullptr);
    bool AppendRgbRecord(
        std::uint64_t record_id,
        std::uint32_t record_type,
        const void* payload,
        std::size_t payload_size,
        std::string* error = nullptr);
    bool FlushStream(EpisodeStream stream, std::string* error = nullptr);
    bool Complete(std::string* error = nullptr);
    bool IsOpen() const;
    bool IsComplete() const;
    const std::filesystem::path& GetPartialDirectory() const;
    const std::filesystem::path& GetFinalDirectory() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace WorldModel
} // namespace RoR
