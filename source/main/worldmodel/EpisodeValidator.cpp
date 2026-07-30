/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeValidator.h"

#include "EpisodeArtifacts.h"
#include "EpisodeFormat.h"
#include "EpisodeProvenance.h"

#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

namespace RoR {
namespace WorldModel {
namespace {

const char CHUNK_MAGIC[8] = {'R', 'O', 'R', 'W', 'M', 'C', '0', '1'};
const std::uint64_t MAX_METADATA_BYTES =
    UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024);

EpisodeValidationResult Failure(
    EpisodeValidationError error,
    const std::filesystem::path& artifact,
    const std::string& detail)
{
    EpisodeValidationResult result;
    result.error = error;
    result.artifact = artifact;
    result.detail = detail;
    return result;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool ReadExact(std::ifstream& input, void* output, std::size_t size)
{
    input.read(static_cast<char*>(output), size);
    return input.gcount() == static_cast<std::streamsize>(size);
}

EpisodeValidationResult ValidateChunk(
    const std::filesystem::path& episode_directory,
    const EpisodeArtifacts::ChunkDescriptor& descriptor,
    bool& has_previous,
    std::uint64_t& previous_id)
{
    const std::filesystem::path path =
        episode_directory / descriptor.path;
    std::error_code fs_error;
    if (!std::filesystem::is_regular_file(path, fs_error) || fs_error)
        return Failure(
            EpisodeValidationError::MISSING_CHUNK, path,
            "referenced chunk is missing or not a regular file");
    const std::uint64_t bytes =
        std::filesystem::file_size(path, fs_error);
    if (fs_error || bytes != descriptor.byte_count)
        return Failure(
            EpisodeValidationError::CHUNK_SIZE_MISMATCH, path,
            "chunk byte count does not match manifest");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Failure(
            EpisodeValidationError::IO_ERROR, path, "could not open chunk");
    std::array<std::uint8_t, EpisodeArtifacts::CHUNK_HEADER_BYTES> header;
    if (!ReadExact(input, header.data(), header.size()))
        return Failure(
            EpisodeValidationError::INVALID_CHUNK_HEADER, path,
            "chunk header is truncated");
    if (std::memcmp(header.data(), CHUNK_MAGIC, 8) != 0 ||
        EpisodeArtifacts::LoadU32(header.data() + 8U) !=
            EPISODE_FORMAT_VERSION ||
        EpisodeArtifacts::LoadU32(header.data() + 12U) !=
            static_cast<std::uint32_t>(descriptor.stream) ||
        EpisodeArtifacts::LoadU32(header.data() + 16U) != descriptor.index ||
        EpisodeArtifacts::LoadU32(header.data() + 20U) != 0)
        return Failure(
            EpisodeValidationError::INVALID_CHUNK_HEADER, path,
            "chunk header identity does not match manifest");

    std::uint64_t first_id = 0, last_id = 0;
    for (std::uint64_t i = 0; i < descriptor.record_count; ++i)
    {
        std::array<std::uint8_t, EpisodeArtifacts::RECORD_HEADER_BYTES> frame;
        if (!ReadExact(input, frame.data(), frame.size()))
            return Failure(
                EpisodeValidationError::TRUNCATED_RECORD, path,
                "record header is truncated");
        if (EpisodeArtifacts::LoadU32(frame.data()) !=
            EpisodeArtifacts::RECORD_MAGIC)
            return Failure(
                EpisodeValidationError::TRUNCATED_RECORD, path,
                "record framing magic is invalid");
        const std::uint64_t record_id =
            EpisodeArtifacts::LoadU64(frame.data() + 4U);
        const std::uint32_t record_type =
            EpisodeArtifacts::LoadU32(frame.data() + 12U);
        const std::uint32_t payload_size =
            EpisodeArtifacts::LoadU32(frame.data() + 16U);
        if (record_id == 0 || record_type == 0 || payload_size == 0)
            return Failure(
                EpisodeValidationError::INVALID_RECORD_HEADER, path,
                "record id, type, and payload size must be non-zero");
        if (payload_size > MAX_EPISODE_RECORD_BYTES)
            return Failure(
                EpisodeValidationError::TRUNCATED_RECORD, path,
                "record payload exceeds format limit");
        std::vector<std::uint8_t> crc_input;
        try
        {
            crc_input.insert(crc_input.end(), frame.begin(), frame.end());
            crc_input.resize(frame.size() + payload_size);
        }
        catch (const std::bad_alloc&)
        {
            return Failure(
                EpisodeValidationError::IO_ERROR, path,
                "could not allocate record validation buffer");
        }
        if (!ReadExact(
                input, crc_input.data() + frame.size(), payload_size))
            return Failure(
                EpisodeValidationError::TRUNCATED_RECORD, path,
                "record payload is truncated");
        std::array<std::uint8_t, EpisodeArtifacts::RECORD_TRAILER_BYTES>
            trailer;
        if (!ReadExact(input, trailer.data(), trailer.size()))
            return Failure(
                EpisodeValidationError::TRUNCATED_RECORD, path,
                "record CRC32C is truncated");
        if (ComputeCrc32c(crc_input.data(), crc_input.size()) !=
            EpisodeArtifacts::LoadU32(trailer.data()))
            return Failure(
                EpisodeValidationError::RECORD_CRC_MISMATCH, path,
                "record CRC32C does not match payload");
        if (has_previous && record_id <= previous_id)
            return Failure(
                EpisodeValidationError::RECORD_SEQUENCE_MISMATCH, path,
                "record identifiers are not strictly increasing");
        if (i == 0)
            first_id = record_id;
        last_id = record_id;
        previous_id = record_id;
        has_previous = true;
    }
    char extra = 0;
    if (input.read(&extra, 1))
        return Failure(
            EpisodeValidationError::RECORD_COUNT_MISMATCH, path,
            "chunk has trailing bytes after declared records");
    if (!input.eof())
        return Failure(
            EpisodeValidationError::IO_ERROR, path, "chunk read failed");
    if (first_id != descriptor.first_record_id ||
        last_id != descriptor.last_record_id)
        return Failure(
            EpisodeValidationError::RECORD_SEQUENCE_MISMATCH, path,
            "chunk record range does not match manifest");
    Hash256 hash;
    std::string hash_error;
    if (!ComputeFileSha256(path, hash, &hash_error))
        return Failure(
            EpisodeValidationError::IO_ERROR, path, hash_error);
    if (hash != descriptor.sha256)
        return Failure(
            EpisodeValidationError::CHUNK_HASH_MISMATCH, path,
            "chunk SHA-256 does not match manifest");
    return EpisodeValidationResult();
}

} // namespace

EpisodeValidationResult::EpisodeValidationResult():
    error(EpisodeValidationError::NONE),
    artifact(),
    detail(),
    telemetry_record_count(0),
    rgb_record_count(0),
    telemetry_chunk_count(0),
    rgb_chunk_count(0)
{
}

bool EpisodeValidationResult::IsValid() const
{
    return error == EpisodeValidationError::NONE;
}

EpisodeValidationResult ValidateImpl(
    const std::filesystem::path& episode_directory,
    bool allow_sealed_partial)
{
    const std::string directory_name =
        episode_directory.filename().string();
    const bool is_partial = EndsWith(directory_name, ".partial");
    if (is_partial && !allow_sealed_partial)
        return Failure(
            EpisodeValidationError::PATH_IS_PARTIAL, episode_directory,
            "partial episodes are never published artifacts");
    if (allow_sealed_partial && !is_partial)
        return Failure(
            EpisodeValidationError::PATH_IS_PARTIAL, episode_directory,
            "publication validation requires a .partial directory");
    std::error_code fs_error;
    if (!std::filesystem::is_directory(episode_directory, fs_error) ||
        fs_error)
        return Failure(
            EpisodeValidationError::MISSING_ARTIFACT, episode_directory,
            "episode directory does not exist");
    for (std::filesystem::recursive_directory_iterator it(
             episode_directory, fs_error), end;
         !fs_error && it != end; it.increment(fs_error))
    {
        if (it->is_symlink())
            return Failure(
                EpisodeValidationError::UNEXPECTED_ARTIFACT, it->path(),
                "symbolic links are forbidden in episode artifacts");
        if (EndsWith(it->path().filename().string(), ".tmp"))
            return Failure(
                EpisodeValidationError::TEMPORARY_ARTIFACT_PRESENT,
                it->path(),
                "temporary artifact proves publication was interrupted");
    }
    if (fs_error)
        return Failure(
            EpisodeValidationError::IO_ERROR, episode_directory,
            "could not enumerate episode artifacts");

    const std::filesystem::path completion_path =
        episode_directory / "COMPLETE.json";
    const std::filesystem::path manifest_path =
        episode_directory / "manifest.json";
    const std::filesystem::path open_manifest_path =
        episode_directory / "manifest.open.json";
    const std::filesystem::path checksums_path =
        episode_directory / "checksums.sha256";
    const std::filesystem::path provenance_path =
        episode_directory / "provenance.json";
    if (!std::filesystem::is_regular_file(completion_path))
        return Failure(
            EpisodeValidationError::MISSING_ARTIFACT, completion_path,
            "completion marker is missing");
    if (!std::filesystem::is_regular_file(manifest_path) ||
        !std::filesystem::is_regular_file(open_manifest_path) ||
        !std::filesystem::is_regular_file(provenance_path) ||
        !std::filesystem::is_regular_file(checksums_path) ||
        !std::filesystem::is_directory(episode_directory / "chunks") ||
        !std::filesystem::is_directory(episode_directory / "rgb"))
        return Failure(
            EpisodeValidationError::MISSING_ARTIFACT, episode_directory,
            "required artifact or reserved stream directory is missing");

    std::string manifest_text, read_error;
    if (!EpisodeArtifacts::ReadFile(
            manifest_path, manifest_text, MAX_METADATA_BYTES, &read_error))
        return Failure(
            EpisodeValidationError::IO_ERROR, manifest_path, read_error);
    EpisodeArtifacts::Manifest manifest;
    if (!EpisodeArtifacts::ParseManifest(
            manifest_text, manifest, &read_error))
        return Failure(
            EpisodeValidationError::INVALID_MANIFEST,
            manifest_path, read_error);
    const std::string expected_directory =
        "episode-" + manifest.episode_id +
        (allow_sealed_partial ? ".partial" : "");
    if (directory_name != expected_directory)
        return Failure(
            EpisodeValidationError::INVALID_MANIFEST, manifest_path,
            "episode identifier does not match directory name");

    std::string provenance_text;
    EpisodeProvenance provenance;
    if (!EpisodeArtifacts::ReadFile(
            provenance_path,
            provenance_text,
            MAX_METADATA_BYTES,
            &read_error) ||
        !ParseEpisodeProvenance(
            provenance_text,
            provenance,
            &read_error) ||
        ComputeSha256(
            provenance_text.data(),
            provenance_text.size()) !=
            manifest.provenance_sha256)
    {
        return Failure(
            EpisodeValidationError::INVALID_PROVENANCE,
            provenance_path,
            read_error.empty()
                ? "provenance hash does not match manifest"
                : read_error);
    }

    std::string open_text;
    if (!EpisodeArtifacts::ReadFile(
            open_manifest_path, open_text, MAX_METADATA_BYTES, &read_error) ||
        open_text != EpisodeArtifacts::BuildOpenManifest(manifest.episode_id))
        return Failure(
            EpisodeValidationError::INVALID_OPEN_MANIFEST,
            open_manifest_path,
            read_error.empty() ? "open manifest is not canonical" : read_error);

    std::string completion_text;
    if (!EpisodeArtifacts::ReadFile(
            completion_path, completion_text,
            MAX_METADATA_BYTES, &read_error))
        return Failure(
            EpisodeValidationError::IO_ERROR, completion_path, read_error);
    EpisodeArtifacts::Completion completion;
    if (!EpisodeArtifacts::ParseCompletion(
            completion_text, completion, &read_error))
        return Failure(
            EpisodeValidationError::INVALID_COMPLETION_MARKER,
            completion_path, read_error);
    if (completion.episode_id != manifest.episode_id ||
        completion.manifest_sha256 !=
            ComputeSha256(manifest_text.data(), manifest_text.size()) ||
        completion.telemetry_record_count !=
            manifest.telemetry_record_count ||
        completion.rgb_record_count != manifest.rgb_record_count ||
        completion.telemetry_chunk_count !=
            manifest.telemetry_chunk_count ||
        completion.rgb_chunk_count != manifest.rgb_chunk_count)
        return Failure(
            EpisodeValidationError::COMPLETION_MISMATCH, completion_path,
            "completion marker does not bind the sealed manifest");

    std::string checksums;
    if (!EpisodeArtifacts::ReadFile(
            checksums_path, checksums, MAX_METADATA_BYTES, &read_error) ||
        checksums != EpisodeArtifacts::BuildChecksums(manifest.chunks))
        return Failure(
            EpisodeValidationError::INVALID_CHECKSUMS, checksums_path,
            read_error.empty()
                ? "checksums file does not match manifest" : read_error);

    std::set<std::string> allowed_files = {
        "manifest.open.json", "provenance.json", "checksums.sha256",
        "manifest.json", "COMPLETE.json"};
    for (const auto& chunk : manifest.chunks)
        allowed_files.insert(chunk.path);
    const std::set<std::string> allowed_directories = {"chunks", "rgb"};
    for (std::filesystem::recursive_directory_iterator it(
             episode_directory), end; it != end; ++it)
    {
        const std::string relative = std::filesystem::relative(
            it->path(), episode_directory).generic_string();
        if (it->is_directory())
        {
            if (allowed_directories.count(relative) == 0)
                return Failure(
                    EpisodeValidationError::UNEXPECTED_ARTIFACT, it->path(),
                    "unexpected directory in completed episode");
        }
        else if (!it->is_regular_file() ||
            allowed_files.count(relative) == 0)
        {
            const bool chunk_like =
                relative.rfind("chunks/", 0) == 0 ||
                relative.rfind("rgb/", 0) == 0;
            return Failure(
                chunk_like ? EpisodeValidationError::UNEXPECTED_CHUNK
                           : EpisodeValidationError::UNEXPECTED_ARTIFACT,
                it->path(), "artifact is not declared by sealed manifest");
        }
    }

    bool has_telemetry = false, has_rgb = false;
    std::uint64_t previous_telemetry = 0, previous_rgb = 0;
    for (const auto& chunk : manifest.chunks)
    {
        bool& has_previous = chunk.stream == EpisodeStream::TELEMETRY
            ? has_telemetry : has_rgb;
        std::uint64_t& previous = chunk.stream == EpisodeStream::TELEMETRY
            ? previous_telemetry : previous_rgb;
        EpisodeValidationResult result = ValidateChunk(
            episode_directory, chunk, has_previous, previous);
        if (!result.IsValid())
            return result;
    }
    EpisodeValidationResult success;
    success.telemetry_record_count = manifest.telemetry_record_count;
    success.rgb_record_count = manifest.rgb_record_count;
    success.telemetry_chunk_count = manifest.telemetry_chunk_count;
    success.rgb_chunk_count = manifest.rgb_chunk_count;
    return success;
}

EpisodeValidationResult EpisodeValidator::Validate(
    const std::filesystem::path& episode_directory)
{
    try
    {
        return ValidateImpl(episode_directory, false);
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return Failure(
            EpisodeValidationError::IO_ERROR,
            exception.path1().empty()
                ? episode_directory
                : exception.path1(),
            std::string("filesystem exception: ") + exception.what());
    }
}

EpisodeValidationResult EpisodeValidator::ValidateForPublication(
    const std::filesystem::path& partial_episode_directory)
{
    try
    {
        return ValidateImpl(partial_episode_directory, true);
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return Failure(
            EpisodeValidationError::IO_ERROR,
            exception.path1().empty()
                ? partial_episode_directory
                : exception.path1(),
            std::string("filesystem exception: ") + exception.what());
    }
}

const char* EpisodeValidationErrorName(EpisodeValidationError error)
{
    switch (error)
    {
    case EpisodeValidationError::NONE: return "none";
    case EpisodeValidationError::PATH_IS_PARTIAL: return "path-is-partial";
    case EpisodeValidationError::MISSING_ARTIFACT: return "missing-artifact";
    case EpisodeValidationError::UNEXPECTED_ARTIFACT:
        return "unexpected-artifact";
    case EpisodeValidationError::TEMPORARY_ARTIFACT_PRESENT:
        return "temporary-artifact-present";
    case EpisodeValidationError::INVALID_OPEN_MANIFEST:
        return "invalid-open-manifest";
    case EpisodeValidationError::INVALID_PROVENANCE:
        return "invalid-provenance";
    case EpisodeValidationError::INVALID_MANIFEST: return "invalid-manifest";
    case EpisodeValidationError::INVALID_COMPLETION_MARKER:
        return "invalid-completion-marker";
    case EpisodeValidationError::COMPLETION_MISMATCH:
        return "completion-mismatch";
    case EpisodeValidationError::INVALID_CHECKSUMS:
        return "invalid-checksums";
    case EpisodeValidationError::MISSING_CHUNK: return "missing-chunk";
    case EpisodeValidationError::UNEXPECTED_CHUNK: return "unexpected-chunk";
    case EpisodeValidationError::CHUNK_SIZE_MISMATCH:
        return "chunk-size-mismatch";
    case EpisodeValidationError::CHUNK_HASH_MISMATCH:
        return "chunk-hash-mismatch";
    case EpisodeValidationError::INVALID_CHUNK_HEADER:
        return "invalid-chunk-header";
    case EpisodeValidationError::INVALID_RECORD_HEADER:
        return "invalid-record-header";
    case EpisodeValidationError::TRUNCATED_RECORD: return "truncated-record";
    case EpisodeValidationError::RECORD_CRC_MISMATCH:
        return "record-crc-mismatch";
    case EpisodeValidationError::RECORD_SEQUENCE_MISMATCH:
        return "record-sequence-mismatch";
    case EpisodeValidationError::RECORD_COUNT_MISMATCH:
        return "record-count-mismatch";
    case EpisodeValidationError::IO_ERROR: return "io-error";
    }
    return "unknown";
}

} // namespace WorldModel
} // namespace RoR
