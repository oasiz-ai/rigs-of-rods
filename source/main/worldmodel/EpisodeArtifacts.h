/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Canonical artifact descriptions shared by the writer and validator.

#pragma once

#include "EpisodeFormat.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {
namespace EpisodeArtifacts {

static const std::uint32_t CHUNK_HEADER_BYTES = 24;
static const std::uint32_t RECORD_HEADER_BYTES = 20;
static const std::uint32_t RECORD_TRAILER_BYTES = 4;
static const std::uint32_t RECORD_MAGIC = UINT32_C(0x31524d57);

struct ChunkDescriptor
{
    EpisodeStream stream;
    std::uint32_t index;
    std::string path;
    std::uint64_t first_record_id;
    std::uint64_t last_record_id;
    std::uint64_t record_count;
    std::uint64_t byte_count;
    Hash256 sha256;
    ChunkDescriptor();
};

struct Manifest
{
    std::string episode_id;
    Hash256 provenance_sha256;
    std::uint64_t telemetry_record_count;
    std::uint64_t rgb_record_count;
    std::uint32_t telemetry_chunk_count;
    std::uint32_t rgb_chunk_count;
    std::vector<ChunkDescriptor> chunks;
    Manifest();
};

struct Completion
{
    std::string episode_id;
    Hash256 manifest_sha256;
    std::uint64_t telemetry_record_count;
    std::uint64_t rgb_record_count;
    std::uint32_t telemetry_chunk_count;
    std::uint32_t rgb_chunk_count;
    Completion();
};

std::string BuildOpenManifest(const std::string& episode_id);
std::string BuildManifest(const Manifest& manifest);
std::string BuildCompletion(const Completion& completion);
std::string BuildChecksums(const std::vector<ChunkDescriptor>& chunks);
bool ParseManifest(
    const std::string& text,
    Manifest& manifest,
    std::string* error = nullptr);
bool ParseCompletion(
    const std::string& text,
    Completion& completion,
    std::string* error = nullptr);
std::string ChunkRelativePath(EpisodeStream stream, std::uint32_t index);
void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value);
void AppendU64(std::vector<std::uint8_t>& output, std::uint64_t value);
std::uint32_t LoadU32(const std::uint8_t* input);
std::uint64_t LoadU64(const std::uint8_t* input);
bool ReadFile(
    const std::filesystem::path& path,
    std::string& contents,
    std::uint64_t maximum_bytes,
    std::string* error = nullptr);

} // namespace EpisodeArtifacts
} // namespace WorldModel
} // namespace RoR
