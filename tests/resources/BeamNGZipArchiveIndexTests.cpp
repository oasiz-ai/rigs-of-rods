#include "BeamNGZipArchiveIndex.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using RoR::BeamNG::ManifestErrorCode;
using RoR::BeamNG::PackageEntryKind;
using RoR::BeamNG::PackageScanLimits;
using RoR::BeamNG::ZipArchiveIndexErrorCode;
using RoR::BeamNG::ZipArchiveIndexResult;
using RoR::BeamNG::ZipArchiveScanLimits;

const std::uint32_t LOCAL_SIGNATURE = UINT32_C(0x04034b50);
const std::uint32_t CENTRAL_SIGNATURE = UINT32_C(0x02014b50);
const std::uint32_t EOCD_SIGNATURE = UINT32_C(0x06054b50);
const std::uint32_t DATA_DESCRIPTOR_SIGNATURE = UINT32_C(0x08074b50);

void Append16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & UINT16_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & UINT16_C(0xff)));
}

void Append32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & UINT32_C(0xff)));
}

void Patch16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value)
{
    CHECK(offset <= bytes.size());
    CHECK(bytes.size() - offset >= 2);
    if (offset > bytes.size() || bytes.size() - offset < 2)
        return;
    bytes[offset] = static_cast<std::uint8_t>(value & UINT16_C(0xff));
    bytes[offset + 1] =
        static_cast<std::uint8_t>((value >> 8) & UINT16_C(0xff));
}

void Patch32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value)
{
    CHECK(offset <= bytes.size());
    CHECK(bytes.size() - offset >= 4);
    if (offset > bytes.size() || bytes.size() - offset < 4)
        return;
    bytes[offset] = static_cast<std::uint8_t>(value & UINT32_C(0xff));
    bytes[offset + 1] =
        static_cast<std::uint8_t>((value >> 8) & UINT32_C(0xff));
    bytes[offset + 2] =
        static_cast<std::uint8_t>((value >> 16) & UINT32_C(0xff));
    bytes[offset + 3] =
        static_cast<std::uint8_t>((value >> 24) & UINT32_C(0xff));
}

std::vector<std::uint8_t> Bytes(const std::string& value)
{
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

struct EntrySpec
{
    std::string path;
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> local_extra;
    std::vector<std::uint8_t> central_extra;
    std::string comment;
    std::uint16_t version_made_by;
    std::uint16_t version_needed;
    std::uint16_t flags;
    std::uint16_t method;
    std::uint32_t crc32;
    std::uint32_t expanded_size;
    std::uint16_t disk_number_start;
    std::uint16_t internal_attributes;
    std::uint32_t external_attributes;
    bool signed_descriptor;
    bool include_descriptor;

    explicit EntrySpec(const std::string& entry_path) :
        path(entry_path),
        version_made_by(UINT16_C(20)),
        version_needed(UINT16_C(20)),
        flags(0),
        method(0),
        crc32(UINT32_C(0x12345678)),
        expanded_size(0),
        disk_number_start(0),
        internal_attributes(0),
        external_attributes(0),
        signed_descriptor(true),
        include_descriptor(true)
    {
    }
};

struct ArchiveOptions
{
    std::vector<std::uint8_t> precentral_gap;
    std::vector<std::uint8_t> central_prefix;
    std::vector<std::uint8_t> central_suffix;
    std::vector<std::uint8_t> before_eocd;
    std::string archive_comment;
};

struct BuiltArchive
{
    std::vector<std::uint8_t> bytes;
    std::vector<std::size_t> local_offsets;
    std::vector<std::size_t> data_offsets;
    std::vector<std::size_t> central_offsets;
    std::size_t central_offset;
    std::size_t central_size;
    std::size_t eocd_offset;
};

void AppendRaw(
    std::vector<std::uint8_t>& destination,
    const std::vector<std::uint8_t>& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

void AppendString(
    std::vector<std::uint8_t>& destination,
    const std::string& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

BuiltArchive BuildArchive(
    const std::vector<EntrySpec>& entries,
    const ArchiveOptions& options = ArchiveOptions())
{
    BuiltArchive archive;

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const EntrySpec& entry = entries[index];
        archive.local_offsets.push_back(archive.bytes.size());
        Append32(archive.bytes, LOCAL_SIGNATURE);
        Append16(archive.bytes, entry.version_needed);
        Append16(archive.bytes, entry.flags);
        Append16(archive.bytes, entry.method);
        Append16(archive.bytes, UINT16_C(0x4321));
        Append16(archive.bytes, UINT16_C(0x5678));
        const bool descriptor = (entry.flags & UINT16_C(0x0008)) != 0;
        Append32(archive.bytes, descriptor ? 0 : entry.crc32);
        Append32(
            archive.bytes,
            descriptor
                ? 0
                : static_cast<std::uint32_t>(entry.data.size()));
        Append32(
            archive.bytes,
            descriptor ? 0 : entry.expanded_size);
        Append16(
            archive.bytes,
            static_cast<std::uint16_t>(entry.path.size()));
        Append16(
            archive.bytes,
            static_cast<std::uint16_t>(entry.local_extra.size()));
        AppendString(archive.bytes, entry.path);
        AppendRaw(archive.bytes, entry.local_extra);
        archive.data_offsets.push_back(archive.bytes.size());
        AppendRaw(archive.bytes, entry.data);

        if (descriptor && entry.include_descriptor)
        {
            if (entry.signed_descriptor)
                Append32(archive.bytes, DATA_DESCRIPTOR_SIGNATURE);
            Append32(archive.bytes, entry.crc32);
            Append32(
                archive.bytes,
                static_cast<std::uint32_t>(entry.data.size()));
            Append32(archive.bytes, entry.expanded_size);
        }
    }

    AppendRaw(archive.bytes, options.precentral_gap);
    archive.central_offset = archive.bytes.size();
    AppendRaw(archive.bytes, options.central_prefix);

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const EntrySpec& entry = entries[index];
        archive.central_offsets.push_back(archive.bytes.size());
        Append32(archive.bytes, CENTRAL_SIGNATURE);
        Append16(archive.bytes, entry.version_made_by);
        Append16(archive.bytes, entry.version_needed);
        Append16(archive.bytes, entry.flags);
        Append16(archive.bytes, entry.method);
        Append16(archive.bytes, UINT16_C(0x4321));
        Append16(archive.bytes, UINT16_C(0x5678));
        Append32(archive.bytes, entry.crc32);
        Append32(
            archive.bytes,
            static_cast<std::uint32_t>(entry.data.size()));
        Append32(archive.bytes, entry.expanded_size);
        Append16(
            archive.bytes,
            static_cast<std::uint16_t>(entry.path.size()));
        Append16(
            archive.bytes,
            static_cast<std::uint16_t>(entry.central_extra.size()));
        Append16(
            archive.bytes,
            static_cast<std::uint16_t>(entry.comment.size()));
        Append16(archive.bytes, entry.disk_number_start);
        Append16(archive.bytes, entry.internal_attributes);
        Append32(archive.bytes, entry.external_attributes);
        Append32(
            archive.bytes,
            static_cast<std::uint32_t>(archive.local_offsets[index]));
        AppendString(archive.bytes, entry.path);
        AppendRaw(archive.bytes, entry.central_extra);
        AppendString(archive.bytes, entry.comment);
    }

    AppendRaw(archive.bytes, options.central_suffix);
    archive.central_size = archive.bytes.size() - archive.central_offset;
    AppendRaw(archive.bytes, options.before_eocd);
    archive.eocd_offset = archive.bytes.size();
    Append32(archive.bytes, EOCD_SIGNATURE);
    Append16(archive.bytes, 0);
    Append16(archive.bytes, 0);
    Append16(
        archive.bytes,
        static_cast<std::uint16_t>(entries.size()));
    Append16(
        archive.bytes,
        static_cast<std::uint16_t>(entries.size()));
    Append32(
        archive.bytes,
        static_cast<std::uint32_t>(archive.central_size));
    Append32(
        archive.bytes,
        static_cast<std::uint32_t>(archive.central_offset));
    Append16(
        archive.bytes,
        static_cast<std::uint16_t>(options.archive_comment.size()));
    AppendString(archive.bytes, options.archive_comment);
    return archive;
}

EntrySpec Stored(
    const std::string& path,
    const std::string& data = std::string())
{
    EntrySpec entry(path);
    entry.data = Bytes(data);
    entry.expanded_size =
        static_cast<std::uint32_t>(entry.data.size());
    return entry;
}

ZipArchiveIndexResult Parse(const BuiltArchive& archive)
{
    return RoR::BeamNG::BuildBeamNGZipArchiveIndex(archive.bytes);
}

void CheckError(
    const BuiltArchive& archive,
    ZipArchiveIndexErrorCode expected)
{
    const ZipArchiveIndexResult result = Parse(archive);
    if (result.error.code != expected)
    {
        std::cerr
            << "expected "
            << RoR::BeamNG::ZipArchiveIndexErrorCodeToString(expected)
            << ", got "
            << RoR::BeamNG::ZipArchiveIndexErrorCodeToString(
                   result.error.code)
            << " at offset " << result.error.offset << '\n';
        ++g_failures;
    }
}

void TestValidMetadataAndManifest()
{
    std::vector<EntrySpec> entries;

    EntrySpec directory = Stored("vehicles/formulacoupe/");
    directory.version_made_by =
        static_cast<std::uint16_t>((UINT16_C(3) << 8) | UINT16_C(20));
    directory.external_attributes =
        static_cast<std::uint32_t>(
            (UINT32_C(0040755) << 16) | UINT32_C(0x10));
    entries.push_back(directory);

    EntrySpec jbeam =
        Stored("vehicles/formulacoupe/main.jbeam", "{}\n");
    jbeam.comment = "configuration";
    jbeam.flags = UINT16_C(0x0800);
    entries.push_back(jbeam);

    EntrySpec mesh("vehicles/formulacoupe/body.dae");
    mesh.data = Bytes("abc");
    mesh.expanded_size = 25;
    mesh.method = UINT16_C(93);
    mesh.version_needed = UINT16_C(63);
    entries.push_back(mesh);

    ArchiveOptions options;
    options.archive_comment = "BeamNG metadata fixture";
    const BuiltArchive archive = BuildArchive(entries, options);
    const ZipArchiveIndexResult result = Parse(archive);

    CHECK(result.IsValid());
    CHECK(result.index.format_profile.identifier == "pkware-appnote");
    CHECK(
        result.index.format_profile.version ==
        "6.3.10-classic-single-disk-index-v1");
    CHECK(
        result.index.package_manifest.format_profile.identifier ==
        "beamng-docs");
    CHECK(
        result.index.package_manifest.format_profile.version ==
        "0.38.5.0-2026-07-27");
    CHECK(result.index.entries.size() == 3);
    CHECK(result.index.package_manifest.entries.size() == 3);
    CHECK(result.index.archive_comment == options.archive_comment);
    CHECK(result.index.central_directory_offset == archive.central_offset);
    CHECK(result.index.central_directory_size == archive.central_size);
    CHECK(result.index.eocd_offset == archive.eocd_offset);
    CHECK(
        result.index.entries[0].kind ==
        PackageEntryKind::DIRECTORY);
    CHECK(
        result.index.entries[1].kind ==
        PackageEntryKind::REGULAR_FILE);
    CHECK(result.index.entries[1].utf8_names);
    CHECK(result.index.entries[1].comment == "configuration");
    CHECK(result.index.entries[2].compression_method == 93);
    CHECK(result.index.entries[2].compressed_size == 3);
    CHECK(result.index.entries[2].expanded_size == 25);
    CHECK(
        result.index.entries[2].local_record_end_offset ==
        archive.central_offset);
}

void TestDataDescriptors()
{
    EntrySpec signed_entry("vehicles/test/signed.jbeam");
    signed_entry.data = Bytes("deflated");
    signed_entry.expanded_size = 30;
    signed_entry.method = 8;
    signed_entry.flags = UINT16_C(0x0008);
    const ZipArchiveIndexResult signed_result =
        Parse(BuildArchive(std::vector<EntrySpec>(1, signed_entry)));
    CHECK(signed_result.IsValid());
    CHECK(signed_result.index.entries[0].uses_data_descriptor);
    CHECK(signed_result.index.entries[0].data_descriptor_size == 16);

    EntrySpec unsigned_entry = signed_entry;
    unsigned_entry.path = "vehicles/test/unsigned.jbeam";
    unsigned_entry.signed_descriptor = false;
    const ZipArchiveIndexResult unsigned_result =
        Parse(BuildArchive(std::vector<EntrySpec>(1, unsigned_entry)));
    CHECK(unsigned_result.IsValid());
    CHECK(unsigned_result.index.entries[0].data_descriptor_size == 12);

    EntrySpec ambiguous_entry = unsigned_entry;
    ambiguous_entry.path = "vehicles/test/crc-signature.jbeam";
    ambiguous_entry.crc32 = DATA_DESCRIPTOR_SIGNATURE;
    const ZipArchiveIndexResult ambiguous_result =
        Parse(BuildArchive(std::vector<EntrySpec>(1, ambiguous_entry)));
    CHECK(ambiguous_result.IsValid());
    CHECK(ambiguous_result.index.entries[0].data_descriptor_size == 12);

    EntrySpec valid_utf8_comment = signed_entry;
    valid_utf8_comment.flags |= UINT16_C(0x0800);
    valid_utf8_comment.comment =
        std::string("configuration ") +
        static_cast<char>(0xe2) +
        static_cast<char>(0x9c) +
        static_cast<char>(0x93);
    CHECK(
        Parse(
            BuildArchive(
                std::vector<EntrySpec>(
                    1, valid_utf8_comment))).IsValid());

    EntrySpec malformed_utf8_comment = valid_utf8_comment;
    malformed_utf8_comment.comment =
        std::string("bad ") +
        static_cast<char>(0xe2) +
        static_cast<char>(0x28) +
        static_cast<char>(0xa1);
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(
                1, malformed_utf8_comment)),
        ZipArchiveIndexErrorCode::ENTRY_UTF8_INVALID);

    const std::string malformed_utf8_comments[] = {
        std::string(1, static_cast<char>(0x80)),
        std::string("\xc0\xaf", 2),
        std::string("\xed\xa0\x80", 3),
        std::string("\xf4\x90\x80\x80", 4),
        std::string("\xf0\x9f\x92", 3)};
    for (std::size_t index = 0;
         index <
             sizeof(malformed_utf8_comments) /
                 sizeof(malformed_utf8_comments[0]);
         ++index)
    {
        malformed_utf8_comment.comment =
            malformed_utf8_comments[index];
        CheckError(
            BuildArchive(
                std::vector<EntrySpec>(
                    1, malformed_utf8_comment)),
            ZipArchiveIndexErrorCode::ENTRY_UTF8_INVALID);
    }

    BuiltArchive nonzero_local_crc =
        BuildArchive(std::vector<EntrySpec>(1, signed_entry));
    Patch32(
        nonzero_local_crc.bytes,
        nonzero_local_crc.local_offsets[0] + 14,
        1);
    CheckError(
        nonzero_local_crc,
        ZipArchiveIndexErrorCode::LOCAL_CRC_MISMATCH);

    BuiltArchive nonzero_local_compressed_size =
        BuildArchive(std::vector<EntrySpec>(1, signed_entry));
    Patch32(
        nonzero_local_compressed_size.bytes,
        nonzero_local_compressed_size.local_offsets[0] + 18,
        2);
    CheckError(
        nonzero_local_compressed_size,
        ZipArchiveIndexErrorCode::LOCAL_COMPRESSED_SIZE_MISMATCH);

    BuiltArchive nonzero_local_expanded_size =
        BuildArchive(std::vector<EntrySpec>(1, signed_entry));
    Patch32(
        nonzero_local_expanded_size.bytes,
        nonzero_local_expanded_size.local_offsets[0] + 22,
        3);
    CheckError(
        nonzero_local_expanded_size,
        ZipArchiveIndexErrorCode::LOCAL_UNCOMPRESSED_SIZE_MISMATCH);
}

void TestEocdDiscoveryAndLimits()
{
    const BuiltArchive valid =
        BuildArchive(std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));

    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            static_cast<const std::uint8_t*>(NULL),
            1).error.code ==
        ZipArchiveIndexErrorCode::NULL_INPUT);
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            std::vector<std::uint8_t>()).error.code ==
        ZipArchiveIndexErrorCode::ARCHIVE_TOO_SMALL);

    std::vector<std::uint8_t> no_eocd(30, 0);
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(no_eocd).error.code ==
        ZipArchiveIndexErrorCode::EOCD_NOT_FOUND);

    std::vector<std::uint8_t> truncated(24, 0);
    Patch32(truncated, 20, EOCD_SIGNATURE);
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(truncated).error.code ==
        ZipArchiveIndexErrorCode::EOCD_TRUNCATED);

    BuiltArchive trailing = valid;
    trailing.bytes.push_back(UINT8_C(0x42));
    CheckError(
        trailing,
        ZipArchiveIndexErrorCode::EOCD_COMMENT_LENGTH_MISMATCH);

    ArchiveOptions ambiguous_options;
    ambiguous_options.archive_comment.assign(22, '\0');
    BuiltArchive ambiguous = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")),
        ambiguous_options);
    Patch32(ambiguous.bytes, ambiguous.eocd_offset + 22, EOCD_SIGNATURE);
    Patch16(ambiguous.bytes, ambiguous.eocd_offset + 42, 0);
    CheckError(ambiguous, ZipArchiveIndexErrorCode::EOCD_AMBIGUOUS);

    ZipArchiveScanLimits archive_limit;
    archive_limit.max_archive_bytes = valid.bytes.size() - 1;
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            valid.bytes, archive_limit).error.code ==
        ZipArchiveIndexErrorCode::ARCHIVE_SIZE_LIMIT);

    ArchiveOptions comment_options;
    comment_options.archive_comment = "1234";
    const BuiltArchive commented = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")),
        comment_options);
    ZipArchiveScanLimits comment_limit;
    comment_limit.max_archive_comment_bytes = 3;
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            commented.bytes, comment_limit).error.code ==
        ZipArchiveIndexErrorCode::ARCHIVE_COMMENT_LIMIT);
}

void TestContainerFeatureRejections()
{
    const std::vector<EntrySpec> entries(
        1, Stored("vehicles/test/main.jbeam", "{}"));

    BuiltArchive zip64_sentinel = BuildArchive(entries);
    Patch32(
        zip64_sentinel.bytes,
        zip64_sentinel.eocd_offset + 16,
        UINT32_MAX);
    CheckError(
        zip64_sentinel,
        ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED);

    ArchiveOptions locator_options;
    Append32(locator_options.before_eocd, UINT32_C(0x07064b50));
    locator_options.before_eocd.resize(20, 0);
    CheckError(
        BuildArchive(entries, locator_options),
        ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED);

    BuiltArchive multidisk = BuildArchive(entries);
    Patch16(multidisk.bytes, multidisk.eocd_offset + 4, 1);
    CheckError(
        multidisk,
        ZipArchiveIndexErrorCode::MULTI_DISK_UNSUPPORTED);

    BuiltArchive count_mismatch = BuildArchive(entries);
    Patch16(count_mismatch.bytes, count_mismatch.eocd_offset + 8, 0);
    CheckError(
        count_mismatch,
        ZipArchiveIndexErrorCode::ENTRY_COUNT_MISMATCH);

    PackageScanLimits count_limit;
    count_limit.max_entries = 0;
    const BuiltArchive count_limited = BuildArchive(entries);
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            count_limited.bytes,
            ZipArchiveScanLimits(),
            count_limit).error.code ==
        ZipArchiveIndexErrorCode::ENTRY_COUNT_LIMIT);

    ZipArchiveScanLimits central_limit;
    central_limit.max_central_directory_bytes = 1;
    const BuiltArchive central_limited = BuildArchive(entries);
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            central_limited.bytes,
            central_limit).error.code ==
        ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_SIZE_LIMIT);

    BuiltArchive central_bounds = BuildArchive(entries);
    Patch32(
        central_bounds.bytes,
        central_bounds.eocd_offset + 16,
        static_cast<std::uint32_t>(central_bounds.central_offset + 1));
    CheckError(
        central_bounds,
        ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_BOUNDS);

    ArchiveOptions trailing_structure;
    trailing_structure.before_eocd.push_back(UINT8_C(0xaa));
    CheckError(
        BuildArchive(entries, trailing_structure),
        ZipArchiveIndexErrorCode::
            CENTRAL_DIRECTORY_NOT_ADJACENT_TO_EOCD);

    ArchiveOptions archive_extra;
    Append32(archive_extra.central_prefix, UINT32_C(0x08064b50));
    Append32(archive_extra.central_prefix, 0);
    CheckError(
        BuildArchive(entries, archive_extra),
        ZipArchiveIndexErrorCode::ARCHIVE_EXTRA_DATA_UNSUPPORTED);

    ArchiveOptions digital_signature;
    Append32(digital_signature.central_suffix, UINT32_C(0x05054b50));
    Append16(digital_signature.central_suffix, 0);
    CheckError(
        BuildArchive(entries, digital_signature),
        ZipArchiveIndexErrorCode::
            CENTRAL_DIRECTORY_DIGITAL_SIGNATURE_UNSUPPORTED);

    BuiltArchive extra_central_record = BuildArchive(entries);
    Patch16(
        extra_central_record.bytes,
        extra_central_record.eocd_offset + 8,
        0);
    Patch16(
        extra_central_record.bytes,
        extra_central_record.eocd_offset + 10,
        0);
    CheckError(
        extra_central_record,
        ZipArchiveIndexErrorCode::ENTRY_COUNT_MISMATCH);
}

void TestExtraFieldsAndEntryFeatures()
{
    std::vector<std::uint8_t> zip64_extra;
    Append16(zip64_extra, UINT16_C(0x0001));
    Append16(zip64_extra, 0);
    EntrySpec zip64 = Stored("vehicles/test/main.jbeam", "{}");
    zip64.central_extra = zip64_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, zip64)),
        ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED);

    EntrySpec local_zip64 = Stored("vehicles/test/main.jbeam", "{}");
    local_zip64.local_extra = zip64_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, local_zip64)),
        ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED);

    std::vector<std::uint8_t> malformed_extra;
    Append16(malformed_extra, UINT16_C(0x1234));
    Append16(malformed_extra, 4);
    malformed_extra.push_back(1);
    EntrySpec malformed = Stored("vehicles/test/main.jbeam", "{}");
    malformed.central_extra = malformed_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, malformed)),
        ZipArchiveIndexErrorCode::CENTRAL_EXTRA_FIELD_INVALID);

    std::vector<std::uint8_t> ntfs_timestamps;
    Append16(ntfs_timestamps, UINT16_C(0x000a));
    Append16(ntfs_timestamps, UINT16_C(32));
    Append32(ntfs_timestamps, 0);
    Append16(ntfs_timestamps, UINT16_C(0x0001));
    Append16(ntfs_timestamps, UINT16_C(24));
    for (std::size_t word = 0; word < 6; ++word)
    {
        Append32(
            ntfs_timestamps,
            static_cast<std::uint32_t>(word + 1));
    }
    EntrySpec ntfs = Stored("vehicles/test/main.jbeam", "{}");
    ntfs.central_extra = ntfs_timestamps;
    ntfs.local_extra = ntfs_timestamps;
    CHECK(Parse(BuildArchive(std::vector<EntrySpec>(1, ntfs))).IsValid());

    EntrySpec duplicate_ntfs = ntfs;
    AppendRaw(duplicate_ntfs.central_extra, ntfs_timestamps);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, duplicate_ntfs)),
        ZipArchiveIndexErrorCode::DUPLICATE_EXTRA_FIELD);

    duplicate_ntfs = ntfs;
    AppendRaw(duplicate_ntfs.local_extra, ntfs_timestamps);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, duplicate_ntfs)),
        ZipArchiveIndexErrorCode::DUPLICATE_EXTRA_FIELD);

    EntrySpec malformed_ntfs = ntfs;
    Patch32(malformed_ntfs.central_extra, 4, 1);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, malformed_ntfs)),
        ZipArchiveIndexErrorCode::CENTRAL_EXTRA_FIELD_INVALID);

    std::vector<std::uint8_t> opaque_extra;
    Append16(opaque_extra, UINT16_C(0xcafe));
    Append16(opaque_extra, 2);
    opaque_extra.push_back(1);
    opaque_extra.push_back(2);
    EntrySpec opaque = Stored("vehicles/test/main.jbeam", "{}");
    opaque.central_extra = opaque_extra;
    opaque.local_extra = opaque_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, opaque)),
        ZipArchiveIndexErrorCode::EXTRA_FIELD_UNSUPPORTED);

    std::vector<std::uint8_t> unix_link_extra;
    Append16(unix_link_extra, UINT16_C(0x000d));
    const std::string link_target = "../../escape";
    Append16(
        unix_link_extra,
        static_cast<std::uint16_t>(12 + link_target.size()));
    Append32(unix_link_extra, 0);
    Append32(unix_link_extra, 0);
    Append16(unix_link_extra, 0);
    Append16(unix_link_extra, 0);
    AppendString(unix_link_extra, link_target);
    EntrySpec unix_link = Stored("vehicles/test/main.jbeam", "{}");
    unix_link.central_extra = unix_link_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, unix_link)),
        ZipArchiveIndexErrorCode::EXTRA_FIELD_UNSUPPORTED);

    unix_link.central_extra.clear();
    unix_link.local_extra = unix_link_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, unix_link)),
        ZipArchiveIndexErrorCode::EXTRA_FIELD_UNSUPPORTED);

    std::vector<std::uint8_t> strong_extra;
    Append16(strong_extra, UINT16_C(0x0017));
    Append16(strong_extra, 0);
    EntrySpec strong = Stored("vehicles/test/main.jbeam", "{}");
    strong.central_extra = strong_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, strong)),
        ZipArchiveIndexErrorCode::STRONG_ENCRYPTION_UNSUPPORTED);

    std::vector<std::uint8_t> certificate_extra;
    Append16(certificate_extra, UINT16_C(0x0015));
    Append16(certificate_extra, 0);
    EntrySpec certificate = Stored("vehicles/test/main.jbeam", "{}");
    certificate.central_extra = certificate_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, certificate)),
        ZipArchiveIndexErrorCode::
            CERTIFICATE_METADATA_UNSUPPORTED);

    std::vector<std::uint8_t> management_extra;
    Append16(management_extra, UINT16_C(0x0018));
    Append16(management_extra, 0);
    EntrySpec management = Stored("vehicles/test/main.jbeam", "{}");
    management.central_extra = management_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, management)),
        ZipArchiveIndexErrorCode::RECORD_MANAGEMENT_UNSUPPORTED);

    std::vector<std::uint8_t> alternate_path_extra;
    Append16(alternate_path_extra, UINT16_C(0x7075));
    Append16(alternate_path_extra, 0);
    EntrySpec alternate_path = Stored("vehicles/test/main.jbeam", "{}");
    alternate_path.central_extra = alternate_path_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, alternate_path)),
        ZipArchiveIndexErrorCode::ALTERNATE_FILENAME_UNSUPPORTED);

    std::vector<std::uint8_t> aes_extra;
    Append16(aes_extra, UINT16_C(0x9901));
    Append16(aes_extra, 0);
    EntrySpec aes = Stored("vehicles/test/main.jbeam", "{}");
    aes.central_extra = aes_extra;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, aes)),
        ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED);

    EntrySpec encrypted = Stored("vehicles/test/main.jbeam", "{}");
    encrypted.flags = UINT16_C(0x0001);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, encrypted)),
        ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED);

    EntrySpec method_99 = Stored("vehicles/test/main.jbeam", "{}");
    method_99.method = UINT16_C(99);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, method_99)),
        ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED);

    EntrySpec strong_flag = Stored("vehicles/test/main.jbeam", "{}");
    strong_flag.flags = UINT16_C(0x0041);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, strong_flag)),
        ZipArchiveIndexErrorCode::STRONG_ENCRYPTION_UNSUPPORTED);

    EntrySpec central_encryption =
        Stored("vehicles/test/main.jbeam", "{}");
    central_encryption.flags = UINT16_C(0x2000);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, central_encryption)),
        ZipArchiveIndexErrorCode::
            CENTRAL_DIRECTORY_ENCRYPTION_UNSUPPORTED);

    EntrySpec patched = Stored("vehicles/test/main.jbeam", "{}");
    patched.flags = UINT16_C(0x0020);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, patched)),
        ZipArchiveIndexErrorCode::PATCHED_DATA_UNSUPPORTED);

    EntrySpec streams = Stored("vehicles/test/main.jbeam", "{}");
    streams.flags = UINT16_C(0x4000);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, streams)),
        ZipArchiveIndexErrorCode::ALTERNATE_STREAMS_UNSUPPORTED);

    EntrySpec reserved = Stored("vehicles/test/main.jbeam", "{}");
    reserved.flags = UINT16_C(0x0080);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, reserved)),
        ZipArchiveIndexErrorCode::
            GENERAL_PURPOSE_FLAGS_UNSUPPORTED);

    EntrySpec reserved_enhanced_compression =
        Stored("vehicles/test/main.jbeam", "{}");
    reserved_enhanced_compression.flags = UINT16_C(0x1000);
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(
                1, reserved_enhanced_compression)),
        ZipArchiveIndexErrorCode::
            GENERAL_PURPOSE_FLAGS_UNSUPPORTED);

    EntrySpec legacy_enhanced_deflate =
        Stored("vehicles/test/main.jbeam", "{}");
    legacy_enhanced_deflate.method = UINT16_C(8);
    legacy_enhanced_deflate.flags = UINT16_C(0x0010);
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(1, legacy_enhanced_deflate)),
        ZipArchiveIndexErrorCode::
            GENERAL_PURPOSE_FLAGS_UNSUPPORTED);

    EntrySpec undefined_compression_option =
        Stored("vehicles/test/main.jbeam", "{}");
    undefined_compression_option.flags = UINT16_C(0x0002);
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(
                1, undefined_compression_option)),
        ZipArchiveIndexErrorCode::
            GENERAL_PURPOSE_FLAGS_UNSUPPORTED);

    EntrySpec defined_deflate_option =
        Stored("vehicles/test/main.jbeam", "{}");
    defined_deflate_option.method = UINT16_C(8);
    defined_deflate_option.flags = UINT16_C(0x0006);
    CHECK(
        Parse(
            BuildArchive(
                std::vector<EntrySpec>(
                    1, defined_deflate_option))).IsValid());

    EntrySpec defined_lzma_option(
        "vehicles/test/main.jbeam");
    defined_lzma_option.method = UINT16_C(14);
    defined_lzma_option.flags = UINT16_C(0x0002);
    CHECK(
        Parse(
            BuildArchive(
                std::vector<EntrySpec>(
                    1, defined_lzma_option))).IsValid());

    EntrySpec lzma_undefined_compression_option(
        "vehicles/test/main.jbeam");
    lzma_undefined_compression_option.method = UINT16_C(14);
    lzma_undefined_compression_option.flags = UINT16_C(0x0004);
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(
                1, lzma_undefined_compression_option)),
        ZipArchiveIndexErrorCode::
            GENERAL_PURPOSE_FLAGS_UNSUPPORTED);

    EntrySpec other_disk = Stored("vehicles/test/main.jbeam", "{}");
    other_disk.disk_number_start = 1;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, other_disk)),
        ZipArchiveIndexErrorCode::ENTRY_DISK_UNSUPPORTED);

    const BuiltArchive metadata_archive = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    ZipArchiveScanLimits central_metadata_limit;
    central_metadata_limit.max_entry_metadata_bytes = 1;
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            metadata_archive.bytes,
            central_metadata_limit).error.code ==
        ZipArchiveIndexErrorCode::CENTRAL_ENTRY_METADATA_LIMIT);

    EntrySpec local_metadata = Stored("v/a", "{}");
    Append16(local_metadata.local_extra, UINT16_C(0xcafe));
    Append16(local_metadata.local_extra, 0);
    const BuiltArchive local_metadata_archive =
        BuildArchive(std::vector<EntrySpec>(1, local_metadata));
    ZipArchiveScanLimits local_metadata_limit;
    local_metadata_limit.max_entry_metadata_bytes =
        local_metadata.path.size() + 3;
    CHECK(
        RoR::BeamNG::BuildBeamNGZipArchiveIndex(
            local_metadata_archive.bytes,
            local_metadata_limit).error.code ==
        ZipArchiveIndexErrorCode::LOCAL_ENTRY_METADATA_LIMIT);
}

void TestFilesystemTypeRejections()
{
    EntrySpec symlink = Stored("vehicles/test/link", "target");
    symlink.version_made_by =
        static_cast<std::uint16_t>((UINT16_C(3) << 8) | UINT16_C(20));
    symlink.external_attributes =
        static_cast<std::uint32_t>(UINT32_C(0120777) << 16);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, symlink)),
        ZipArchiveIndexErrorCode::SYMLINK_REJECTED);

    EntrySpec fifo = Stored("vehicles/test/pipe");
    fifo.version_made_by =
        static_cast<std::uint16_t>((UINT16_C(3) << 8) | UINT16_C(20));
    fifo.external_attributes =
        static_cast<std::uint32_t>(UINT32_C(0010644) << 16);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, fifo)),
        ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE);

    EntrySpec volume = Stored("VOLUME");
    volume.external_attributes = UINT32_C(0x08);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, volume)),
        ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE);

    EntrySpec dos_device = Stored("vehicles/test/device");
    dos_device.external_attributes = UINT32_C(0x40);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, dos_device)),
        ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE);

    EntrySpec reparse_point = Stored("vehicles/test/reparse", "target");
    reparse_point.version_made_by =
        static_cast<std::uint16_t>(
            (UINT16_C(10) << 8) | UINT16_C(20));
    reparse_point.external_attributes = UINT32_C(0x00000400);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, reparse_point)),
        ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE);

    EntrySpec mismatch = Stored("vehicles/test/fake/");
    mismatch.version_made_by =
        static_cast<std::uint16_t>((UINT16_C(3) << 8) | UINT16_C(20));
    mismatch.external_attributes =
        static_cast<std::uint32_t>(UINT32_C(0100644) << 16);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, mismatch)),
        ZipArchiveIndexErrorCode::ENTRY_TYPE_MISMATCH);
}

void TestCentralAndLocalConsistency()
{
    const std::vector<EntrySpec> two_entries = {
        Stored("vehicles/test/a.jbeam", "a"),
        Stored("vehicles/test/b.jbeam", "b")};

    BuiltArchive duplicate_offset = BuildArchive(two_entries);
    Patch32(
        duplicate_offset.bytes,
        duplicate_offset.central_offsets[1] + 42,
        static_cast<std::uint32_t>(duplicate_offset.local_offsets[0]));
    CheckError(
        duplicate_offset,
        ZipArchiveIndexErrorCode::DUPLICATE_LOCAL_HEADER_OFFSET);

    BuiltArchive local_bounds = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(
        local_bounds.bytes,
        local_bounds.central_offsets[0] + 42,
        static_cast<std::uint32_t>(local_bounds.central_offset));
    CheckError(
        local_bounds,
        ZipArchiveIndexErrorCode::LOCAL_HEADER_BOUNDS);

    BuiltArchive signature = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(signature.bytes, signature.local_offsets[0], 0);
    CheckError(
        signature,
        ZipArchiveIndexErrorCode::LOCAL_HEADER_SIGNATURE);

    ArchiveOptions truncated_options;
    truncated_options.precentral_gap.resize(4, 0);
    BuiltArchive truncated = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")),
        truncated_options);
    const std::size_t fake_local = truncated.central_offset - 4;
    Patch32(truncated.bytes, fake_local, LOCAL_SIGNATURE);
    Patch32(
        truncated.bytes,
        truncated.central_offsets[0] + 42,
        static_cast<std::uint32_t>(fake_local));
    CheckError(
        truncated,
        ZipArchiveIndexErrorCode::LOCAL_HEADER_TRUNCATED);

    BuiltArchive version = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch16(version.bytes, version.local_offsets[0] + 4, 10);
    CheckError(
        version,
        ZipArchiveIndexErrorCode::LOCAL_VERSION_MISMATCH);

    BuiltArchive flags = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch16(flags.bytes, flags.local_offsets[0] + 6, UINT16_C(0x0800));
    CheckError(
        flags,
        ZipArchiveIndexErrorCode::LOCAL_FLAGS_MISMATCH);

    BuiltArchive method = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch16(method.bytes, method.local_offsets[0] + 8, 8);
    CheckError(
        method,
        ZipArchiveIndexErrorCode::LOCAL_METHOD_MISMATCH);

    BuiltArchive filename = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    filename.bytes[filename.local_offsets[0] + 30] ^= UINT8_C(0x01);
    const ZipArchiveIndexResult filename_result = Parse(filename);
    CHECK(
        filename_result.error.code ==
        ZipArchiveIndexErrorCode::LOCAL_FILENAME_MISMATCH);
    CHECK(filename_result.error.offset == filename.local_offsets[0] + 30);

    BuiltArchive crc = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(crc.bytes, crc.local_offsets[0] + 14, 0);
    CheckError(crc, ZipArchiveIndexErrorCode::LOCAL_CRC_MISMATCH);

    BuiltArchive compressed = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(compressed.bytes, compressed.local_offsets[0] + 18, 1);
    CheckError(
        compressed,
        ZipArchiveIndexErrorCode::LOCAL_COMPRESSED_SIZE_MISMATCH);

    BuiltArchive expanded = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(expanded.bytes, expanded.local_offsets[0] + 22, 1);
    CheckError(
        expanded,
        ZipArchiveIndexErrorCode::LOCAL_UNCOMPRESSED_SIZE_MISMATCH);

    EntrySpec extra_entry = Stored("vehicles/test/main.jbeam", "{}");
    Append16(extra_entry.local_extra, UINT16_C(0x1234));
    Append16(extra_entry.local_extra, 4);
    extra_entry.local_extra.push_back(1);
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, extra_entry)),
        ZipArchiveIndexErrorCode::LOCAL_EXTRA_FIELD_INVALID);
}

void TestRangesAndDataBoundaries()
{
    BuiltArchive stored_mismatch = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(
        stored_mismatch.bytes,
        stored_mismatch.local_offsets[0] + 22,
        3);
    Patch32(
        stored_mismatch.bytes,
        stored_mismatch.central_offsets[0] + 24,
        3);
    CheckError(
        stored_mismatch,
        ZipArchiveIndexErrorCode::STORED_SIZE_MISMATCH);

    BuiltArchive crossing = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    Patch32(crossing.bytes, crossing.local_offsets[0] + 18, 1000);
    Patch32(crossing.bytes, crossing.local_offsets[0] + 22, 1000);
    Patch32(crossing.bytes, crossing.central_offsets[0] + 20, 1000);
    Patch32(crossing.bytes, crossing.central_offsets[0] + 24, 1000);
    CheckError(
        crossing,
        ZipArchiveIndexErrorCode::DATA_CROSSES_CENTRAL_DIRECTORY);

    EntrySpec descriptor("vehicles/test/main.jbeam");
    descriptor.data = Bytes("abc");
    descriptor.expanded_size = 9;
    descriptor.method = 8;
    descriptor.flags = UINT16_C(0x0008);
    BuiltArchive descriptor_mismatch =
        BuildArchive(std::vector<EntrySpec>(1, descriptor));
    Patch32(
        descriptor_mismatch.bytes,
        descriptor_mismatch.data_offsets[0] +
            descriptor.data.size() + 4,
        0);
    CheckError(
        descriptor_mismatch,
        ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_MISMATCH);

    EntrySpec missing_descriptor = descriptor;
    missing_descriptor.include_descriptor = false;
    CheckError(
        BuildArchive(std::vector<EntrySpec>(1, missing_descriptor)),
        ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_TRUNCATED);

    ArchiveOptions gap_options;
    gap_options.precentral_gap.push_back(UINT8_C(0xaa));
    CheckError(
        BuildArchive(
            std::vector<EntrySpec>(
                1, Stored("vehicles/test/main.jbeam", "{}")),
            gap_options),
        ZipArchiveIndexErrorCode::INTER_RECORD_DATA_UNSUPPORTED);

    BuiltArchive prefixed = BuildArchive(
        std::vector<EntrySpec>(
            1, Stored("vehicles/test/main.jbeam", "{}")));
    const std::size_t prefix_size = 4;
    prefixed.bytes.insert(prefixed.bytes.begin(), prefix_size, UINT8_C(0x7f));
    prefixed.central_offset += prefix_size;
    prefixed.eocd_offset += prefix_size;
    prefixed.local_offsets[0] += prefix_size;
    prefixed.central_offsets[0] += prefix_size;
    Patch32(
        prefixed.bytes,
        prefixed.eocd_offset + 16,
        static_cast<std::uint32_t>(prefixed.central_offset));
    Patch32(
        prefixed.bytes,
        prefixed.central_offsets[0] + 42,
        static_cast<std::uint32_t>(prefixed.local_offsets[0]));
    CheckError(
        prefixed,
        ZipArchiveIndexErrorCode::
            SELF_EXTRACTING_PREFIX_UNSUPPORTED);

    // The second central record points at a complete nested local record
    // inside the first entry's payload. Both headers validate in isolation,
    // so the range check must reject the overlap.
    EntrySpec nested_spec = Stored("vehicles/test/nested.jbeam", "x");
    const BuiltArchive nested_only =
        BuildArchive(std::vector<EntrySpec>(1, nested_spec));
    const std::size_t nested_record_size =
        nested_only.central_offset - nested_only.local_offsets[0];
    EntrySpec outer = Stored("vehicles/test/outer.bin");
    outer.data.assign(
        nested_only.bytes.begin(),
        nested_only.bytes.begin() +
            static_cast<std::vector<std::uint8_t>::difference_type>(
                nested_record_size));
    outer.expanded_size =
        static_cast<std::uint32_t>(outer.data.size());
    std::vector<EntrySpec> overlap_entries;
    overlap_entries.push_back(outer);
    overlap_entries.push_back(nested_spec);
    BuiltArchive overlap = BuildArchive(overlap_entries);
    Patch32(
        overlap.bytes,
        overlap.central_offsets[1] + 42,
        static_cast<std::uint32_t>(overlap.data_offsets[0]));
    CheckError(
        overlap,
        ZipArchiveIndexErrorCode::LOCAL_RANGE_OVERLAP);
}

void TestManifestPolicyIsApplied()
{
    EntrySpec traversal = Stored("../escape.jbeam", "{}");
    const ZipArchiveIndexResult traversal_result =
        Parse(BuildArchive(std::vector<EntrySpec>(1, traversal)));
    CHECK(
        traversal_result.error.code ==
        ZipArchiveIndexErrorCode::MANIFEST_REJECTED);
    CHECK(
        traversal_result.error.manifest_error.code ==
        ManifestErrorCode::PARENT_TRAVERSAL);
    CHECK(traversal_result.error.entry_index == 0);

    std::vector<EntrySpec> duplicate;
    duplicate.push_back(Stored("vehicles/test/Main.jbeam", "{}"));
    duplicate.push_back(Stored("vehicles/test/main.jbeam", "{}"));
    const ZipArchiveIndexResult duplicate_result =
        Parse(BuildArchive(duplicate));
    CHECK(
        duplicate_result.error.code ==
        ZipArchiveIndexErrorCode::MANIFEST_REJECTED);
    CHECK(
        duplicate_result.error.manifest_error.code ==
        ManifestErrorCode::CASE_COLLISION);

    EntrySpec ratio("vehicles/test/main.jbeam");
    ratio.data.push_back(1);
    ratio.expanded_size = 1025;
    ratio.method = 8;
    const ZipArchiveIndexResult ratio_result =
        Parse(BuildArchive(std::vector<EntrySpec>(1, ratio)));
    CHECK(
        ratio_result.error.code ==
        ZipArchiveIndexErrorCode::MANIFEST_REJECTED);
    CHECK(
        ratio_result.error.manifest_error.code ==
        ManifestErrorCode::COMPRESSION_RATIO_LIMIT);

    const BuiltArchive empty =
        BuildArchive(std::vector<EntrySpec>());
    const ZipArchiveIndexResult empty_result = Parse(empty);
    CHECK(
        empty_result.error.code ==
        ZipArchiveIndexErrorCode::MANIFEST_REJECTED);
    CHECK(
        empty_result.error.manifest_error.code ==
        ManifestErrorCode::EMPTY_PACKAGE);
}

void CheckValidInvariants(const ZipArchiveIndexResult& result)
{
    CHECK(result.IsValid());
    if (!result.IsValid())
        return;
    CHECK(
        result.index.central_directory_offset +
            result.index.central_directory_size ==
        result.index.eocd_offset);
    CHECK(
        result.index.entries.size() ==
        result.index.package_manifest.entries.size());
    CHECK(
        result.index.eocd_offset <= result.index.archive_size);
    for (std::size_t index = 0;
         index < result.index.entries.size();
         ++index)
    {
        const RoR::BeamNG::ZipArchiveEntry& entry =
            result.index.entries[index];
        CHECK(entry.local_header_offset <= entry.data_offset);
        CHECK(entry.data_offset <= entry.data_end_offset);
        CHECK(entry.data_end_offset <= entry.local_record_end_offset);
        CHECK(
            entry.local_record_end_offset <=
            result.index.central_directory_offset);
        CHECK(
            entry.central_header_offset >=
            result.index.central_directory_offset);
        CHECK(
            entry.central_record_end_offset <=
            result.index.eocd_offset);
    }
}

std::uint32_t NextRandom(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void TestAllTruncationsAndFixedSeedMutations()
{
    std::vector<EntrySpec> entries;
    entries.push_back(Stored("vehicles/test/main.jbeam", "{\"a\":1}"));
    EntrySpec streamed("vehicles/test/body.dae");
    streamed.data = Bytes("compressed-payload");
    streamed.expanded_size = 80;
    streamed.method = 8;
    streamed.flags = UINT16_C(0x0808);
    streamed.signed_descriptor = true;
    entries.push_back(streamed);
    ArchiveOptions options;
    options.archive_comment = "mutation corpus";
    const BuiltArchive corpus = BuildArchive(entries, options);
    CHECK(Parse(corpus).IsValid());

    for (std::size_t length = 0; length < corpus.bytes.size(); ++length)
    {
        const std::vector<std::uint8_t> truncated(
            corpus.bytes.begin(),
            corpus.bytes.begin() +
                static_cast<std::vector<std::uint8_t>::difference_type>(
                    length));
        const ZipArchiveIndexResult result =
            RoR::BeamNG::BuildBeamNGZipArchiveIndex(truncated);
        CHECK(!result.IsValid());
        CHECK(result.error.code != ZipArchiveIndexErrorCode::NONE);
        CHECK(
            std::string(
                RoR::BeamNG::ZipArchiveIndexErrorCodeToString(
                    result.error.code)) != "unknown");
    }

    std::uint32_t random_state = UINT32_C(0x9e3779b9);
    for (std::size_t iteration = 0; iteration < 5000; ++iteration)
    {
        std::vector<std::uint8_t> mutated = corpus.bytes;
        const std::size_t mutation_count =
            1 + (NextRandom(random_state) % 4);
        for (std::size_t mutation = 0;
             mutation < mutation_count;
             ++mutation)
        {
            const std::size_t offset =
                NextRandom(random_state) % mutated.size();
            const std::uint8_t bit = static_cast<std::uint8_t>(
                UINT8_C(1) << (NextRandom(random_state) % 8));
            mutated[offset] ^= bit;
        }

        const ZipArchiveIndexResult result =
            RoR::BeamNG::BuildBeamNGZipArchiveIndex(mutated);
        if (result.IsValid())
        {
            CheckValidInvariants(result);
        }
        else
        {
            CHECK(
                std::string(
                    RoR::BeamNG::ZipArchiveIndexErrorCodeToString(
                        result.error.code)) != "unknown");
            CHECK(
                result.error.offset ==
                    std::numeric_limits<std::uint64_t>::max() ||
                result.error.offset <= mutated.size());
        }
    }
}

} // anonymous namespace

int main()
{
    TestValidMetadataAndManifest();
    TestDataDescriptors();
    TestEocdDiscoveryAndLimits();
    TestContainerFeatureRejections();
    TestExtraFieldsAndEntryFeatures();
    TestFilesystemTypeRejections();
    TestCentralAndLocalConsistency();
    TestRangesAndDataBoundaries();
    TestManifestPolicyIsApplied();
    TestAllTruncationsAndFixedSeedMutations();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " BeamNG ZIP archive index checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout
        << "BeamNG ZIP archive index checks passed"
        << " (PKWARE APPNOTE 6.3.10 classic profile,"
        << " 5000 fixed-seed mutations)\n";
    return EXIT_SUCCESS;
}
