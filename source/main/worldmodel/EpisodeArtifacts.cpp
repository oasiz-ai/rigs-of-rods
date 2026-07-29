/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeArtifacts.h"

#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>

namespace RoR {
namespace WorldModel {
namespace EpisodeArtifacts {
namespace {

void SetError(std::string* error, const std::string& text)
{
    if (error != nullptr)
        *error = text;
}

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> result;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
        result.push_back(line);
    if (text.empty() || text.back() != '\n')
        result.push_back("<missing-final-newline>");
    return result;
}

bool ParseString(
    const std::string& line,
    const std::string& prefix,
    const std::string& suffix,
    std::string& value)
{
    if (line.size() < prefix.size() + suffix.size() ||
        line.compare(0, prefix.size(), prefix) != 0 ||
        line.compare(line.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;
    value = line.substr(
        prefix.size(), line.size() - prefix.size() - suffix.size());
    return true;
}

template <typename Integer>
bool ParseUnsigned(
    const std::string& line,
    const std::string& prefix,
    const std::string& suffix,
    Integer& value)
{
    std::string digits;
    if (!ParseString(line, prefix, suffix, digits) || digits.empty() ||
        (digits.size() > 1 && digits.front() == '0'))
        return false;
    std::uint64_t parsed = 0;
    for (char digit : digits)
    {
        if (digit < '0' || digit > '9')
            return false;
        const unsigned int unit = static_cast<unsigned int>(digit - '0');
        if (parsed >
            (std::numeric_limits<std::uint64_t>::max() - unit) / 10U)
            return false;
        parsed = parsed * 10U + unit;
    }
    if (parsed > std::numeric_limits<Integer>::max())
        return false;
    value = static_cast<Integer>(parsed);
    return true;
}

bool ParseChunk(
    const std::string& line,
    bool trailing_comma,
    ChunkDescriptor& chunk)
{
    static const std::regex PATTERN(
        "^    \\{\\\"stream\\\":\\\"(telemetry|rgb)\\\","
        "\\\"index\\\":([0-9]+),"
        "\\\"path\\\":\\\"((chunks|rgb)/chunk-[0-9]{6}\\.bin)\\\","
        "\\\"first_record_id\\\":([0-9]+),"
        "\\\"last_record_id\\\":([0-9]+),"
        "\\\"record_count\\\":([0-9]+),"
        "\\\"byte_count\\\":([0-9]+),"
        "\\\"sha256\\\":\\\"([0-9a-f]{64})\\\"\\}(,?)$");
    std::smatch match;
    if (!std::regex_match(line, match, PATTERN) ||
        (match[10].str() == ",") != trailing_comma)
        return false;
    chunk.stream = match[1].str() == "telemetry"
        ? EpisodeStream::TELEMETRY : EpisodeStream::RGB;
    if (!ParseUnsigned(match[2].str(), "", "", chunk.index) ||
        !ParseUnsigned(match[5].str(), "", "", chunk.first_record_id) ||
        !ParseUnsigned(match[6].str(), "", "", chunk.last_record_id) ||
        !ParseUnsigned(match[7].str(), "", "", chunk.record_count) ||
        !ParseUnsigned(match[8].str(), "", "", chunk.byte_count) ||
        !Hash256::FromHex(match[9].str(), chunk.sha256))
        return false;
    chunk.path = match[3].str();
    return chunk.path == ChunkRelativePath(chunk.stream, chunk.index) &&
        chunk.record_count != 0 &&
        chunk.first_record_id <= chunk.last_record_id;
}

} // namespace

ChunkDescriptor::ChunkDescriptor():
    stream(EpisodeStream::TELEMETRY),
    index(0),
    path(),
    first_record_id(0),
    last_record_id(0),
    record_count(0),
    byte_count(0),
    sha256()
{
}

Manifest::Manifest():
    episode_id(),
    provenance_sha256(),
    telemetry_record_count(0),
    rgb_record_count(0),
    telemetry_chunk_count(0),
    rgb_chunk_count(0),
    chunks()
{
}

Completion::Completion():
    episode_id(),
    manifest_sha256(),
    telemetry_record_count(0),
    rgb_record_count(0),
    telemetry_chunk_count(0),
    rgb_chunk_count(0)
{
}

std::string BuildOpenManifest(const std::string& episode_id)
{
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"ror-world-model-episode\",\n"
        << "  \"format_version\": 1,\n"
        << "  \"episode_id\": \"" << episode_id << "\",\n"
        << "  \"state\": \"open\"\n"
        << "}\n";
    return out.str();
}

std::string BuildManifest(const Manifest& manifest)
{
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"ror-world-model-episode\",\n"
        << "  \"format_version\": 1,\n"
        << "  \"episode_id\": \"" << manifest.episode_id << "\",\n"
        << "  \"record_framing\": "
           "\"ror-world-model-record-v1-crc32c\",\n"
        << "  \"state\": \"complete\",\n"
        << "  \"provenance_path\": \"provenance.json\",\n"
        << "  \"provenance_sha256\": \""
        << manifest.provenance_sha256.ToHex() << "\",\n"
        << "  \"telemetry_record_count\": "
        << manifest.telemetry_record_count << ",\n"
        << "  \"rgb_record_count\": " << manifest.rgb_record_count << ",\n"
        << "  \"telemetry_chunk_count\": "
        << manifest.telemetry_chunk_count << ",\n"
        << "  \"rgb_chunk_count\": " << manifest.rgb_chunk_count << ",\n"
        << "  \"chunks\": [\n";
    for (std::size_t i = 0; i < manifest.chunks.size(); ++i)
    {
        const ChunkDescriptor& chunk = manifest.chunks[i];
        out << "    {\"stream\":\"" << EpisodeStreamName(chunk.stream)
            << "\",\"index\":" << chunk.index
            << ",\"path\":\"" << chunk.path
            << "\",\"first_record_id\":" << chunk.first_record_id
            << ",\"last_record_id\":" << chunk.last_record_id
            << ",\"record_count\":" << chunk.record_count
            << ",\"byte_count\":" << chunk.byte_count
            << ",\"sha256\":\"" << chunk.sha256.ToHex() << "\"}"
            << (i + 1U == manifest.chunks.size() ? "" : ",") << '\n';
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string BuildCompletion(const Completion& completion)
{
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"ror-world-model-episode-completion\",\n"
        << "  \"format_version\": 1,\n"
        << "  \"episode_id\": \"" << completion.episode_id << "\",\n"
        << "  \"state\": \"complete\",\n"
        << "  \"manifest_sha256\": \""
        << completion.manifest_sha256.ToHex() << "\",\n"
        << "  \"telemetry_record_count\": "
        << completion.telemetry_record_count << ",\n"
        << "  \"rgb_record_count\": "
        << completion.rgb_record_count << ",\n"
        << "  \"telemetry_chunk_count\": "
        << completion.telemetry_chunk_count << ",\n"
        << "  \"rgb_chunk_count\": "
        << completion.rgb_chunk_count << "\n"
        << "}\n";
    return out.str();
}

std::string BuildChecksums(const std::vector<ChunkDescriptor>& chunks)
{
    std::ostringstream out;
    for (const ChunkDescriptor& chunk : chunks)
        out << chunk.sha256.ToHex() << "  " << chunk.path << '\n';
    return out.str();
}

bool ParseManifest(
    const std::string& text,
    Manifest& manifest,
    std::string* error)
{
    const std::vector<std::string> lines = Lines(text);
    if (lines.size() < 15 || lines[0] != "{" ||
        lines[1] != "  \"format\": \"ror-world-model-episode\"," ||
        lines[2] != "  \"format_version\": 1," ||
        lines[4] !=
            "  \"record_framing\": "
            "\"ror-world-model-record-v1-crc32c\"," ||
        lines[5] != "  \"state\": \"complete\"," ||
        lines[6] != "  \"provenance_path\": \"provenance.json\"," ||
        lines[12] != "  \"chunks\": [" ||
        lines[lines.size() - 2U] != "  ]" || lines.back() != "}")
    {
        SetError(error, "manifest structure is not canonical version 1");
        return false;
    }
    Manifest parsed;
    std::string provenance_sha256;
    if (!ParseString(
            lines[3], "  \"episode_id\": \"", "\",", parsed.episode_id) ||
        !IsValidEpisodeId(parsed.episode_id) ||
        !ParseString(
            lines[7],
            "  \"provenance_sha256\": \"",
            "\",",
            provenance_sha256) ||
        !Hash256::FromHex(
            provenance_sha256,
            parsed.provenance_sha256) ||
        !ParseUnsigned(
            lines[8], "  \"telemetry_record_count\": ", ",",
            parsed.telemetry_record_count) ||
        !ParseUnsigned(
            lines[9], "  \"rgb_record_count\": ", ",",
            parsed.rgb_record_count) ||
        !ParseUnsigned(
            lines[10], "  \"telemetry_chunk_count\": ", ",",
            parsed.telemetry_chunk_count) ||
        !ParseUnsigned(
            lines[11], "  \"rgb_chunk_count\": ", ",",
            parsed.rgb_chunk_count))
    {
        SetError(error, "manifest scalar field is invalid");
        return false;
    }
    const std::size_t chunk_count = lines.size() - 15U;
    if (chunk_count != parsed.telemetry_chunk_count +
            static_cast<std::size_t>(parsed.rgb_chunk_count))
    {
        SetError(error, "manifest chunk count does not match entries");
        return false;
    }
    std::uint64_t telemetry_records = 0, rgb_records = 0;
    for (std::size_t i = 0; i < chunk_count; ++i)
    {
        ChunkDescriptor chunk;
        if (!ParseChunk(lines[13U + i], i + 1U != chunk_count, chunk))
        {
            SetError(error, "manifest chunk entry is invalid");
            return false;
        }
        const bool telemetry = i < parsed.telemetry_chunk_count;
        const EpisodeStream expected = telemetry
            ? EpisodeStream::TELEMETRY : EpisodeStream::RGB;
        const std::uint32_t expected_index = telemetry
            ? static_cast<std::uint32_t>(i)
            : static_cast<std::uint32_t>(i - parsed.telemetry_chunk_count);
        if (chunk.stream != expected || chunk.index != expected_index)
        {
            SetError(error, "manifest chunks are not in canonical order");
            return false;
        }
        if (telemetry)
            telemetry_records += chunk.record_count;
        else
            rgb_records += chunk.record_count;
        parsed.chunks.push_back(chunk);
    }
    if (telemetry_records != parsed.telemetry_record_count ||
        rgb_records != parsed.rgb_record_count ||
        BuildManifest(parsed) != text)
    {
        SetError(error, "manifest counts or canonical encoding is invalid");
        return false;
    }
    manifest = parsed;
    return true;
}

bool ParseCompletion(
    const std::string& text,
    Completion& completion,
    std::string* error)
{
    const std::vector<std::string> lines = Lines(text);
    if (lines.size() != 11 || lines[0] != "{" ||
        lines[1] !=
            "  \"format\": \"ror-world-model-episode-completion\"," ||
        lines[2] != "  \"format_version\": 1," ||
        lines[4] != "  \"state\": \"complete\"," || lines.back() != "}")
    {
        SetError(error, "completion marker structure is not canonical");
        return false;
    }
    Completion parsed;
    std::string digest;
    if (!ParseString(
            lines[3], "  \"episode_id\": \"", "\",", parsed.episode_id) ||
        !IsValidEpisodeId(parsed.episode_id) ||
        !ParseString(
            lines[5], "  \"manifest_sha256\": \"", "\",", digest) ||
        !Hash256::FromHex(digest, parsed.manifest_sha256) ||
        !ParseUnsigned(
            lines[6], "  \"telemetry_record_count\": ", ",",
            parsed.telemetry_record_count) ||
        !ParseUnsigned(
            lines[7], "  \"rgb_record_count\": ", ",",
            parsed.rgb_record_count) ||
        !ParseUnsigned(
            lines[8], "  \"telemetry_chunk_count\": ", ",",
            parsed.telemetry_chunk_count) ||
        !ParseUnsigned(
            lines[9], "  \"rgb_chunk_count\": ", "",
            parsed.rgb_chunk_count) ||
        BuildCompletion(parsed) != text)
    {
        SetError(error, "completion marker field is invalid");
        return false;
    }
    completion = parsed;
    return true;
}

std::string ChunkRelativePath(EpisodeStream stream, std::uint32_t index)
{
    std::ostringstream path;
    path << (stream == EpisodeStream::TELEMETRY ? "chunks/" : "rgb/")
         << "chunk-" << std::setw(6) << std::setfill('0') << index
         << ".bin";
    return path.str();
}

void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8U)));
}

void AppendU64(std::vector<std::uint8_t>& output, std::uint64_t value)
{
    for (unsigned int i = 0; i < 8; ++i)
        output.push_back(static_cast<std::uint8_t>(value >> (i * 8U)));
}

std::uint32_t LoadU32(const std::uint8_t* input)
{
    std::uint32_t result = 0;
    for (unsigned int i = 0; i < 4; ++i)
        result |= static_cast<std::uint32_t>(input[i]) << (i * 8U);
    return result;
}

std::uint64_t LoadU64(const std::uint8_t* input)
{
    std::uint64_t result = 0;
    for (unsigned int i = 0; i < 8; ++i)
        result |= static_cast<std::uint64_t>(input[i]) << (i * 8U);
    return result;
}

bool ReadFile(
    const std::filesystem::path& path,
    std::string& contents,
    std::uint64_t maximum_bytes,
    std::string* error)
{
    std::error_code filesystem_error;
    const std::uint64_t size =
        std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error)
    {
        SetError(error, "could not inspect file: " + path.string());
        return false;
    }
    if (size > maximum_bytes ||
        size > std::numeric_limits<std::size_t>::max())
    {
        SetError(error, "file exceeds validation size limit: " + path.string());
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        SetError(error, "could not open file: " + path.string());
        return false;
    }
    contents.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad() || contents.size() != size)
    {
        SetError(error, "failed while reading file: " + path.string());
        return false;
    }
    return true;
}

} // namespace EpisodeArtifacts
} // namespace WorldModel
} // namespace RoR
