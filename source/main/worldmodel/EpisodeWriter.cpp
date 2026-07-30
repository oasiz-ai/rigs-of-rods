/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeWriter.h"

#include "EpisodeArtifacts.h"
#include "EpisodeValidator.h"

#include <cerrno>
#include <cstdio>
#include <limits>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#   if !defined(NOMINMAX)
#       define NOMINMAX
#   endif
#   include <windows.h>
#   include <io.h>
#else
#   include <fcntl.h>
#   include <unistd.h>
#endif

namespace RoR {
namespace WorldModel {
namespace {

const char CHUNK_MAGIC[8] = {'R', 'O', 'R', 'W', 'M', 'C', '0', '1'};

void SetError(std::string* error, const std::string& text)
{
    if (error != nullptr)
        *error = text;
}

std::string IoError(const std::string& operation)
{
    return operation + ": " +
        std::error_code(errno, std::generic_category()).message();
}

#if defined(_WIN32)
std::string WindowsError(
    const std::string& operation,
    unsigned long error_code)
{
    return operation + " (Win32 error " +
        std::to_string(error_code) + ")";
}
#endif

std::FILE* OpenFile(
    const std::filesystem::path& path,
    const char* mode)
{
#if defined(_WIN32)
    std::FILE* output = nullptr;
    std::wstring wide_mode;
    for (const char* character = mode; *character != '\0'; ++character)
        wide_mode.push_back(static_cast<wchar_t>(*character));
    return ::_wfopen_s(&output, path.c_str(), wide_mode.c_str()) == 0
        ? output
        : nullptr;
#else
    return std::fopen(path.string().c_str(), mode);
#endif
}

bool WriteAll(
    std::FILE* output,
    const void* bytes,
    std::size_t size,
    std::string* error)
{
    const std::uint8_t* input =
        static_cast<const std::uint8_t*>(bytes);
    while (size != 0)
    {
        const std::size_t written = std::fwrite(input, 1, size, output);
        if (written == 0)
        {
            SetError(error, IoError("could not append episode bytes"));
            return false;
        }
        input += written;
        size -= written;
    }
    return true;
}

bool DurableClose(std::FILE*& file, std::string* error)
{
    if (file == nullptr)
        return true;
    bool success = true;
    if (std::fflush(file) != 0)
    {
        SetError(error, IoError("could not flush episode artifact"));
        success = false;
    }
    if (success)
    {
#if defined(_WIN32)
        if (_commit(_fileno(file)) != 0)
#else
        if (::fsync(::fileno(file)) != 0)
#endif
        {
            SetError(error, IoError("could not sync episode artifact"));
            success = false;
        }
    }
    if (std::fclose(file) != 0 && success)
    {
        SetError(error, IoError("could not close episode artifact"));
        success = false;
    }
    file = nullptr;
    return success;
}

bool SyncDirectory(
    const std::filesystem::path& directory,
    std::string* error)
{
#if defined(_WIN32)
    // FlushFileBuffers does not support directory handles. Windows commits
    // every file before its rename and all episode renames use
    // MOVEFILE_WRITE_THROUGH below, which is the supported metadata durability
    // boundary on this platform.
    static_cast<void>(directory);
    static_cast<void>(error);
    return true;
#else
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0)
    {
        SetError(error, IoError("could not open episode directory"));
        return false;
    }
    const bool success = ::fsync(descriptor) == 0;
    const int saved_errno = errno;
    ::close(descriptor);
    if (!success)
    {
        errno = saved_errno;
        SetError(error, IoError("could not sync episode directory"));
    }
    return success;
#endif
}

bool CommitRename(
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    std::string* error)
{
#if defined(_WIN32)
    if (::MoveFileExW(
            source.c_str(),
            target.c_str(),
            MOVEFILE_WRITE_THROUGH) == FALSE)
    {
        SetError(
            error,
            WindowsError(
                "could not durably rename " + target.string(),
                ::GetLastError()));
        return false;
    }
    return true;
#else
    std::error_code filesystem_error;
    std::filesystem::rename(source, target, filesystem_error);
    if (filesystem_error)
    {
        SetError(
            error,
            "could not rename " + target.string() + ": " +
                filesystem_error.message());
        return false;
    }
    return SyncDirectory(target.parent_path(), error);
#endif
}

bool PublishDirectory(
    const std::filesystem::path& partial,
    const std::filesystem::path& final,
    std::string* error)
{
#if defined(_WIN32)
    return CommitRename(partial, final, error);
#else
    std::error_code filesystem_error;
    std::filesystem::rename(partial, final, filesystem_error);
    if (filesystem_error)
    {
        SetError(
            error,
            "could not publish completed episode: " +
                filesystem_error.message());
        return false;
    }

    std::string sync_error;
    if (SyncDirectory(final.parent_path(), &sync_error))
        return true;

    // A failed parent-directory sync must not leave a validator-accepted
    // public episode while the producer reports failure. Roll the atomic
    // rename back into the always-rejected .partial name.
    std::error_code rollback_error;
    std::filesystem::rename(final, partial, rollback_error);
    if (!rollback_error)
    {
        std::string ignored;
        SyncDirectory(partial.parent_path(), &ignored);
        SetError(
            error,
            "episode publication durability failed and was rolled back: " +
                sync_error);
        return false;
    }

    // Catastrophic fallback: make the still-visible directory fail admission
    // by removing the required completion name. This is best-effort only for
    // filesystems that rejected both a directory fsync and the rollback.
    std::error_code quarantine_error;
    std::filesystem::rename(
        final / "COMPLETE.json",
        final / "COMPLETE.json.quarantined",
        quarantine_error);
    SetError(
        error,
        "episode publication durability failed; rollback failed (" +
            rollback_error.message() + ")" +
            (quarantine_error
                ? "; completion quarantine also failed (" +
                    quarantine_error.message() + ")"
                : "; completion marker was quarantined"));
    return false;
#endif
}

bool AtomicWrite(
    const std::filesystem::path& target,
    const std::string& contents,
    std::string* error)
{
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    if (std::filesystem::exists(temporary))
    {
        SetError(error, "temporary artifact already exists: " +
            temporary.string());
        return false;
    }
    std::FILE* output = OpenFile(temporary, "wb");
    if (output == nullptr)
    {
        SetError(error, IoError("could not create " + temporary.string()));
        return false;
    }
    if (!WriteAll(output, contents.data(), contents.size(), error))
    {
        std::fclose(output);
        return false;
    }
    if (!DurableClose(output, error))
        return false;
    return CommitRename(temporary, target, error);
}

struct StreamState
{
    explicit StreamState(EpisodeStream value):
        stream(value),
        chunks(),
        file(nullptr),
        temporary_path(),
        final_path(),
        next_chunk_index(0),
        current_record_count(0),
        current_byte_count(0),
        current_first_record_id(0),
        current_last_record_id(0),
        total_record_count(0),
        previous_record_id(0),
        has_previous_record(false)
    {
    }
    EpisodeStream stream;
    std::vector<EpisodeArtifacts::ChunkDescriptor> chunks;
    std::FILE* file;
    std::filesystem::path temporary_path;
    std::filesystem::path final_path;
    std::uint32_t next_chunk_index;
    std::uint64_t current_record_count;
    std::uint64_t current_byte_count;
    std::uint64_t current_first_record_id;
    std::uint64_t current_last_record_id;
    std::uint64_t total_record_count;
    std::uint64_t previous_record_id;
    bool has_previous_record;
};

} // namespace

EpisodeWriterOptions::EpisodeWriterOptions():
    max_records_per_chunk(1024),
    max_chunk_bytes(UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
{
}

class EpisodeWriter::Impl
{
public:
    Impl():
        root(),
        partial_directory(),
        final_directory(),
        episode_id(),
        options(),
        telemetry(EpisodeStream::TELEMETRY),
        rgb(EpisodeStream::RGB),
        open(false),
        complete(false),
        failed(false)
    {
    }

    ~Impl()
    {
        std::string ignored;
        DurableClose(telemetry.file, &ignored);
        DurableClose(rgb.file, &ignored);
    }

    bool Fail(std::string* error, const std::string& text)
    {
        failed = true;
        SetError(error, text);
        return false;
    }

    StreamState& State(EpisodeStream stream)
    {
        return stream == EpisodeStream::TELEMETRY ? telemetry : rgb;
    }

    bool BeginChunk(StreamState& state, std::string* error)
    {
        if (state.file != nullptr)
            return true;
        const std::string relative = EpisodeArtifacts::ChunkRelativePath(
            state.stream, state.next_chunk_index);
        state.final_path = partial_directory / relative;
        state.temporary_path = state.final_path;
        state.temporary_path += ".tmp";
        if (std::filesystem::exists(state.temporary_path) ||
            std::filesystem::exists(state.final_path))
            return Fail(error, "chunk path already exists: " + relative);
        state.file = OpenFile(state.temporary_path, "wb");
        if (state.file == nullptr)
            return Fail(
                error, IoError("could not create " +
                    state.temporary_path.string()));
        std::vector<std::uint8_t> header;
        header.insert(header.end(), CHUNK_MAGIC, CHUNK_MAGIC + 8);
        EpisodeArtifacts::AppendU32(header, EPISODE_FORMAT_VERSION);
        EpisodeArtifacts::AppendU32(
            header, static_cast<std::uint32_t>(state.stream));
        EpisodeArtifacts::AppendU32(header, state.next_chunk_index);
        EpisodeArtifacts::AppendU32(header, 0);
        if (header.size() != EpisodeArtifacts::CHUNK_HEADER_BYTES ||
            !WriteAll(state.file, header.data(), header.size(), error))
        {
            failed = true;
            return false;
        }
        state.current_record_count = 0;
        state.current_byte_count = header.size();
        return true;
    }

    bool Flush(StreamState& state, std::string* error)
    {
        if (state.file == nullptr)
            return true;
        if (state.current_record_count == 0)
        {
            DurableClose(state.file, error);
            std::error_code ignored;
            std::filesystem::remove(state.temporary_path, ignored);
            return true;
        }
        if (!DurableClose(state.file, error))
        {
            failed = true;
            return false;
        }
        Hash256 hash;
        if (!ComputeFileSha256(state.temporary_path, hash, error))
        {
            failed = true;
            return false;
        }
        std::error_code filesystem_error;
        const std::uint64_t bytes = std::filesystem::file_size(
            state.temporary_path, filesystem_error);
        if (filesystem_error || bytes != state.current_byte_count)
            return Fail(error, "temporary chunk size changed before commit");
        if (!CommitRename(
                state.temporary_path,
                state.final_path,
                error))
        {
            failed = true;
            return false;
        }
        EpisodeArtifacts::ChunkDescriptor chunk;
        chunk.stream = state.stream;
        chunk.index = state.next_chunk_index;
        chunk.path = EpisodeArtifacts::ChunkRelativePath(
            state.stream, state.next_chunk_index);
        chunk.first_record_id = state.current_first_record_id;
        chunk.last_record_id = state.current_last_record_id;
        chunk.record_count = state.current_record_count;
        chunk.byte_count = bytes;
        chunk.sha256 = hash;
        state.chunks.push_back(chunk);
        ++state.next_chunk_index;
        state.current_record_count = 0;
        state.current_byte_count = 0;
        return true;
    }

    bool Append(
        StreamState& state,
        std::uint64_t record_id,
        std::uint32_t record_type,
        const void* payload,
        std::size_t payload_size,
        std::string* error)
    {
        if (!open || complete || failed)
            return Fail(error, "episode writer is not writable");
        if (record_id == 0)
            return Fail(error, "record identifier must be non-zero");
        if (record_type == 0)
            return Fail(error, "record type must be non-zero");
        if (payload_size == 0)
            return Fail(error, "record payload must not be empty");
        if (payload_size != 0 && payload == nullptr)
            return Fail(error, "record payload pointer is null");
        if (payload_size > MAX_EPISODE_RECORD_BYTES)
            return Fail(error, "record payload exceeds format limit");
        if (state.has_previous_record &&
            record_id <= state.previous_record_id)
            return Fail(error,
                "record identifiers must strictly increase per stream");
        const std::uint64_t serialized_size =
            EpisodeArtifacts::RECORD_HEADER_BYTES +
            static_cast<std::uint64_t>(payload_size) +
            EpisodeArtifacts::RECORD_TRAILER_BYTES;
        if (state.file != nullptr && state.current_record_count != 0 &&
            (state.current_record_count >= options.max_records_per_chunk ||
             serialized_size > options.max_chunk_bytes ||
             state.current_byte_count >
                options.max_chunk_bytes - serialized_size))
        {
            if (!Flush(state, error))
                return false;
        }
        if (!BeginChunk(state, error))
            return false;
        std::vector<std::uint8_t> record;
        try
        {
            record.reserve(static_cast<std::size_t>(serialized_size));
            EpisodeArtifacts::AppendU32(
                record, EpisodeArtifacts::RECORD_MAGIC);
            EpisodeArtifacts::AppendU64(record, record_id);
            EpisodeArtifacts::AppendU32(record, record_type);
            EpisodeArtifacts::AppendU32(
                record, static_cast<std::uint32_t>(payload_size));
            if (payload_size != 0)
            {
                const std::uint8_t* bytes =
                    static_cast<const std::uint8_t*>(payload);
                record.insert(record.end(), bytes, bytes + payload_size);
            }
            EpisodeArtifacts::AppendU32(
                record, ComputeCrc32c(record.data(), record.size()));
        }
        catch (const std::bad_alloc&)
        {
            return Fail(error, "could not allocate record frame");
        }
        if (!WriteAll(state.file, record.data(), record.size(), error))
        {
            failed = true;
            return false;
        }
        if (state.current_record_count == 0)
            state.current_first_record_id = record_id;
        state.current_last_record_id = record_id;
        ++state.current_record_count;
        state.current_byte_count += record.size();
        ++state.total_record_count;
        state.previous_record_id = record_id;
        state.has_previous_record = true;
        if (state.current_record_count >= options.max_records_per_chunk ||
            state.current_byte_count >= options.max_chunk_bytes)
            return Flush(state, error);
        return true;
    }

    std::filesystem::path root;
    std::filesystem::path partial_directory;
    std::filesystem::path final_directory;
    std::string episode_id;
    Hash256 provenance_sha256;
    EpisodeWriterOptions options;
    StreamState telemetry;
    StreamState rgb;
    bool open;
    bool complete;
    bool failed;
};

EpisodeWriter::EpisodeWriter(): m_impl(new Impl()) {}
EpisodeWriter::~EpisodeWriter() = default;

bool EpisodeWriter::Open(
    const std::filesystem::path& output_root,
    const std::string& episode_id,
    const EpisodeProvenance& provenance,
    const EpisodeWriterOptions& options,
    std::string* error)
{
    try
    {
        if (m_impl->open || m_impl->failed)
        {
            SetError(
                error,
                m_impl->open
                    ? "episode writer is already open"
                    : "failed episode writer cannot be reused");
            return false;
        }
        if (!IsValidEpisodeId(episode_id))
        {
            SetError(error, "episode identifier is not filesystem-safe");
            return false;
        }
        std::string provenance_text;
        if (!SerializeEpisodeProvenance(
                provenance,
                provenance_text,
                error))
        {
            return false;
        }
        if (options.max_records_per_chunk == 0 ||
            options.max_chunk_bytes <
                EpisodeArtifacts::CHUNK_HEADER_BYTES +
                EpisodeArtifacts::RECORD_HEADER_BYTES +
                EpisodeArtifacts::RECORD_TRAILER_BYTES)
        {
            SetError(error, "episode chunk limits are invalid");
            return false;
        }
        std::error_code fs_error;
        std::filesystem::create_directories(output_root, fs_error);
        if (fs_error)
        {
            SetError(error, "could not create output root: " +
                fs_error.message());
            return false;
        }
        m_impl->root = std::filesystem::absolute(output_root);
        m_impl->episode_id = episode_id;
        m_impl->provenance_sha256 =
            ComputeSha256(
                provenance_text.data(),
                provenance_text.size());
        m_impl->options = options;
        m_impl->final_directory =
            m_impl->root / ("episode-" + episode_id);
        m_impl->partial_directory = m_impl->final_directory;
        m_impl->partial_directory += ".partial";
        if (std::filesystem::exists(m_impl->final_directory) ||
            std::filesystem::exists(m_impl->partial_directory))
        {
            SetError(error, "episode output already exists");
            return false;
        }
        if (!std::filesystem::create_directory(
                m_impl->partial_directory, fs_error) || fs_error)
        {
            SetError(error, "could not create partial directory: " +
                fs_error.message());
            return false;
        }
        if (!std::filesystem::create_directory(
                m_impl->partial_directory / "chunks", fs_error) || fs_error ||
            !std::filesystem::create_directory(
                m_impl->partial_directory / "rgb", fs_error) || fs_error)
        {
            SetError(error, "could not create stream directories: " +
                fs_error.message());
            return false;
        }
        if (!AtomicWrite(
                m_impl->partial_directory / "manifest.open.json",
                EpisodeArtifacts::BuildOpenManifest(episode_id), error) ||
            !AtomicWrite(
                m_impl->partial_directory / "provenance.json",
                provenance_text,
                error) ||
            !SyncDirectory(m_impl->partial_directory, error))
            return false;
        m_impl->open = true;
        return true;
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return m_impl->Fail(
            error,
            std::string("episode writer open failed: ") + exception.what());
    }
}

bool EpisodeWriter::AppendTelemetryRecord(
    std::uint64_t id, std::uint32_t type,
    const void* payload, std::size_t size, std::string* error)
{
    try
    {
        return m_impl->Append(
            m_impl->telemetry, id, type, payload, size, error);
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return m_impl->Fail(
            error,
            std::string("telemetry append failed: ") + exception.what());
    }
}

bool EpisodeWriter::AppendRgbRecord(
    std::uint64_t id, std::uint32_t type,
    const void* payload, std::size_t size, std::string* error)
{
    try
    {
        return m_impl->Append(
            m_impl->rgb, id, type, payload, size, error);
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return m_impl->Fail(
            error,
            std::string("RGB append failed: ") + exception.what());
    }
}

bool EpisodeWriter::FlushStream(
    EpisodeStream stream, std::string* error)
{
    try
    {
        if (!m_impl->open || m_impl->complete || m_impl->failed)
        {
            SetError(error, "episode writer is not writable");
            return false;
        }
        return m_impl->Flush(m_impl->State(stream), error);
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return m_impl->Fail(
            error,
            std::string("episode stream flush failed: ") + exception.what());
    }
}

bool EpisodeWriter::Complete(std::string* error)
{
    try
    {
        if (!m_impl->open || m_impl->complete || m_impl->failed)
        {
            SetError(error, "episode writer cannot be completed");
            return false;
        }
        if (!m_impl->Flush(m_impl->telemetry, error) ||
            !m_impl->Flush(m_impl->rgb, error))
            return false;
        EpisodeArtifacts::Manifest manifest;
        manifest.episode_id = m_impl->episode_id;
        manifest.provenance_sha256 =
            m_impl->provenance_sha256;
        manifest.telemetry_record_count =
            m_impl->telemetry.total_record_count;
        manifest.rgb_record_count = m_impl->rgb.total_record_count;
        manifest.telemetry_chunk_count =
            static_cast<std::uint32_t>(m_impl->telemetry.chunks.size());
        manifest.rgb_chunk_count =
            static_cast<std::uint32_t>(m_impl->rgb.chunks.size());
        manifest.chunks = m_impl->telemetry.chunks;
        manifest.chunks.insert(
            manifest.chunks.end(),
            m_impl->rgb.chunks.begin(), m_impl->rgb.chunks.end());
        if (!AtomicWrite(
                m_impl->partial_directory / "checksums.sha256",
                EpisodeArtifacts::BuildChecksums(manifest.chunks), error))
            return false;
        const std::string manifest_text =
            EpisodeArtifacts::BuildManifest(manifest);
        if (!AtomicWrite(
                m_impl->partial_directory / "manifest.json",
                manifest_text, error))
            return false;
        EpisodeArtifacts::Completion completion;
        completion.episode_id = manifest.episode_id;
        completion.manifest_sha256 =
            ComputeSha256(manifest_text.data(), manifest_text.size());
        completion.telemetry_record_count = manifest.telemetry_record_count;
        completion.rgb_record_count = manifest.rgb_record_count;
        completion.telemetry_chunk_count = manifest.telemetry_chunk_count;
        completion.rgb_chunk_count = manifest.rgb_chunk_count;
        // COMPLETE.json is intentionally the last file published.
        if (!AtomicWrite(
                m_impl->partial_directory / "COMPLETE.json",
                EpisodeArtifacts::BuildCompletion(completion), error) ||
            !SyncDirectory(m_impl->partial_directory, error))
            return false;
        const EpisodeValidationResult validation =
            EpisodeValidator::ValidateForPublication(
                m_impl->partial_directory);
        if (!validation.IsValid())
        {
            m_impl->failed = true;
            SetError(
                error,
                std::string("sealed episode failed pre-publication validation: ") +
                    EpisodeValidationErrorName(validation.error) + ": " +
                    validation.detail);
            return false;
        }
        if (!PublishDirectory(
                m_impl->partial_directory,
                m_impl->final_directory,
                error))
            return false;
        m_impl->complete = true;
        return true;
    }
    catch (const std::filesystem::filesystem_error& exception)
    {
        return m_impl->Fail(
            error,
            std::string("episode completion failed: ") + exception.what());
    }
}

bool EpisodeWriter::IsOpen() const { return m_impl->open; }
bool EpisodeWriter::IsComplete() const { return m_impl->complete; }
const std::filesystem::path& EpisodeWriter::GetPartialDirectory() const
{
    return m_impl->partial_directory;
}
const std::filesystem::path& EpisodeWriter::GetFinalDirectory() const
{
    return m_impl->final_directory;
}

} // namespace WorldModel
} // namespace RoR
