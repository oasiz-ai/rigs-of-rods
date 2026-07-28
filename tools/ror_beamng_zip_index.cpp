#include "BeamNGZipArchiveIndex.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// This command-line utility buffers the complete immutable input before
// indexing it. Keep that convenience boundary substantially below the
// format-level classic ZIP limit; embedders can use the library with their own
// explicitly reviewed limits.
const std::uint64_t MAX_CLI_ARCHIVE_BYTES =
    UINT64_C(512) * UINT64_C(1024) * UINT64_C(1024);

int PrintUsage()
{
    std::cerr << "usage: ror_beamng_zip_index PACKAGE.zip\n";
    return 2;
}

bool HasAsciiCaseInsensitiveSuffix(
    const std::string& value,
    const char* suffix)
{
    const std::size_t suffix_size = std::strlen(suffix);
    if (suffix_size > value.size())
        return false;

    const std::size_t value_offset = value.size() - suffix_size;
    for (std::size_t index = 0; index < suffix_size; ++index)
    {
        char character = value[value_offset + index];
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(
                character - 'A' + 'a');
        }
        if (character != suffix[index])
            return false;
    }
    return true;
}

bool ReadBoundedArchive(
    const char* path,
    std::uint64_t maximum_size,
    std::vector<std::uint8_t>& bytes)
{
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open())
        return false;

    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end < 0 ||
        static_cast<std::uint64_t>(end) > maximum_size ||
        static_cast<std::uint64_t>(end) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        static_cast<std::uint64_t>(end) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max()))
    {
        return false;
    }

    bytes.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!input.good())
        return false;
    if (!bytes.empty())
    {
        input.read(
            reinterpret_cast<char*>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
        if (!input ||
            input.gcount() !=
                static_cast<std::streamsize>(bytes.size()))
        {
            return false;
        }
    }
    // Reject a file that grew after sizing instead of indexing a stale
    // prefix. A future package-identity layer will additionally hash the same
    // immutable byte buffer used here.
    return input.peek() == std::char_traits<char>::eof();
}

} // anonymous namespace

int main(int argc, char** argv)
{
    if (argc == 2 &&
        argv[1] != NULL &&
        (std::strcmp(argv[1], "--help") == 0 ||
         std::strcmp(argv[1], "-h") == 0))
    {
        std::cout << "usage: ror_beamng_zip_index PACKAGE.zip\n";
        return 0;
    }
    if (argc != 2 || argv[1] == NULL || argv[1][0] == '\0')
        return PrintUsage();

    try
    {
        const RoR::BeamNG::ZipArchiveScanLimits zip_limits;
        std::vector<std::uint8_t> bytes;
        if (!ReadBoundedArchive(
                argv[1],
                MAX_CLI_ARCHIVE_BYTES,
                bytes))
        {
            std::cerr
                << "{\"status\":\"io-error\","
                << "\"message\":\"archive could not be read within "
                << "the configured byte limit\"}\n";
            return 2;
        }

        const RoR::BeamNG::ZipArchiveIndexResult result =
            RoR::BeamNG::BuildBeamNGZipArchiveIndex(
                bytes,
                zip_limits);
        if (!result.IsValid())
        {
            std::cout
                << "{\"status\":\"invalid\","
                << "\"error\":\""
                << RoR::BeamNG::ZipArchiveIndexErrorCodeToString(
                       result.error.code)
                << "\",\"offset\":" << result.error.offset
                << ",\"entry_index\":" << result.error.entry_index
                << ",\"manifest_error\":\""
                << RoR::BeamNG::ManifestErrorCodeToString(
                       result.error.manifest_error.code)
                << "\"}\n";
            return 1;
        }

        std::size_t pc_configuration_count = 0;
        for (std::vector<
                 RoR::BeamNG::PackageManifestEntry>::const_iterator
                 entry =
                     result.index.package_manifest.entries.begin();
             entry != result.index.package_manifest.entries.end();
             ++entry)
        {
            if (entry->kind ==
                    RoR::BeamNG::PackageEntryKind::REGULAR_FILE &&
                HasAsciiCaseInsensitiveSuffix(entry->path, ".pc"))
            {
                ++pc_configuration_count;
            }
        }

        std::cout
            << "{\"status\":\"valid\","
            << "\"zip_profile\":\""
            << result.index.format_profile.identifier << ':'
            << result.index.format_profile.version
            << "\",\"package_profile\":\""
            << result.index.package_manifest.format_profile.identifier
            << ':'
            << result.index.package_manifest.format_profile.version
            << "\",\"archive_bytes\":"
            << result.index.archive_size
            << ",\"entry_count\":"
            << result.index.entries.size()
            << ",\"total_expanded_bytes\":"
            << result.index.package_manifest.total_expanded_bytes
            << ",\"pc_configuration_count\":"
            << pc_configuration_count
            << "}\n";
        return 0;
    }
    catch (const std::bad_alloc&)
    {
        std::cerr
            << "{\"status\":\"resource-error\","
            << "\"message\":\"memory allocation failed\"}\n";
        return 2;
    }
    catch (const std::length_error&)
    {
        std::cerr
            << "{\"status\":\"resource-error\","
            << "\"message\":\"container length limit exceeded\"}\n";
        return 2;
    }
}
