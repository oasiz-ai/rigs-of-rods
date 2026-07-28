/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file BeamNGZipArchiveIndex.h
/// @brief Bounded, metadata-only indexing of classic BeamNG ZIP packages.

#pragma once

#include "BeamNGPackageManifest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// Security profile for the byte-level container parser. This deliberately
/// does not replace or modify PackageFormatProfile, which continues to pin the
/// independently versioned BeamNG documentation profile used by the manifest.
struct ZipArchiveFormatProfile
{
    std::string identifier;
    std::string version;

    ZipArchiveFormatProfile();
};

/// Limits which apply before any entry-controlled allocation is performed.
/// Classic ZIP fields impose smaller structural maxima in several places, but
/// explicit limits make policy visible and keep behavior deterministic.
struct ZipArchiveScanLimits
{
    std::uint64_t max_archive_bytes;
    std::uint64_t max_central_directory_bytes;
    std::size_t max_entry_metadata_bytes;
    std::size_t max_archive_comment_bytes;

    ZipArchiveScanLimits();
};

enum class ZipArchiveIndexErrorCode
{
    NONE,
    NULL_INPUT,
    ARCHIVE_TOO_SMALL,
    ARCHIVE_SIZE_LIMIT,
    EOCD_NOT_FOUND,
    EOCD_TRUNCATED,
    EOCD_COMMENT_LENGTH_MISMATCH,
    EOCD_AMBIGUOUS,
    ARCHIVE_COMMENT_LIMIT,
    ZIP64_UNSUPPORTED,
    MULTI_DISK_UNSUPPORTED,
    ENTRY_COUNT_MISMATCH,
    ENTRY_COUNT_LIMIT,
    CENTRAL_DIRECTORY_SIZE_LIMIT,
    CENTRAL_DIRECTORY_BOUNDS,
    CENTRAL_DIRECTORY_NOT_ADJACENT_TO_EOCD,
    CENTRAL_HEADER_TRUNCATED,
    CENTRAL_HEADER_SIGNATURE,
    CENTRAL_ENTRY_METADATA_LIMIT,
    CENTRAL_ENTRY_TRUNCATED,
    CENTRAL_EXTRA_FIELD_INVALID,
    EXTRA_FIELD_UNSUPPORTED,
    DUPLICATE_EXTRA_FIELD,
    ARCHIVE_EXTRA_DATA_UNSUPPORTED,
    CENTRAL_DIRECTORY_DIGITAL_SIGNATURE_UNSUPPORTED,
    CENTRAL_DIRECTORY_ENCRYPTION_UNSUPPORTED,
    STRONG_ENCRYPTION_UNSUPPORTED,
    ENCRYPTED_ENTRY_UNSUPPORTED,
    CERTIFICATE_METADATA_UNSUPPORTED,
    RECORD_MANAGEMENT_UNSUPPORTED,
    ALTERNATE_FILENAME_UNSUPPORTED,
    GENERAL_PURPOSE_FLAGS_UNSUPPORTED,
    ENTRY_UTF8_INVALID,
    PATCHED_DATA_UNSUPPORTED,
    ALTERNATE_STREAMS_UNSUPPORTED,
    ENTRY_DISK_UNSUPPORTED,
    SYMLINK_REJECTED,
    UNSUPPORTED_ENTRY_TYPE,
    ENTRY_TYPE_MISMATCH,
    DUPLICATE_LOCAL_HEADER_OFFSET,
    LOCAL_HEADER_BOUNDS,
    LOCAL_HEADER_SIGNATURE,
    LOCAL_HEADER_TRUNCATED,
    LOCAL_ENTRY_METADATA_LIMIT,
    LOCAL_EXTRA_FIELD_INVALID,
    LOCAL_VERSION_MISMATCH,
    LOCAL_FLAGS_MISMATCH,
    LOCAL_METHOD_MISMATCH,
    LOCAL_FILENAME_MISMATCH,
    LOCAL_CRC_MISMATCH,
    LOCAL_COMPRESSED_SIZE_MISMATCH,
    LOCAL_UNCOMPRESSED_SIZE_MISMATCH,
    STORED_SIZE_MISMATCH,
    DATA_CROSSES_CENTRAL_DIRECTORY,
    DATA_DESCRIPTOR_TRUNCATED,
    DATA_DESCRIPTOR_MISMATCH,
    LOCAL_RANGE_OVERLAP,
    SELF_EXTRACTING_PREFIX_UNSUPPORTED,
    INTER_RECORD_DATA_UNSUPPORTED,
    MANIFEST_REJECTED
};

struct ZipArchiveIndexError
{
    ZipArchiveIndexErrorCode code;
    std::uint64_t offset;
    std::size_t entry_index;
    std::size_t conflicting_entry_index;
    ManifestError manifest_error;

    ZipArchiveIndexError();
    bool HasError() const;
};

/// Metadata retained from both a central directory record and its validated
/// local record. Payload bytes and extra-field bodies are never copied.
struct ZipArchiveEntry
{
    std::string path;
    std::string comment;
    PackageEntryKind kind;
    std::uint16_t version_made_by;
    std::uint8_t creator_host_system;
    std::uint16_t version_needed;
    std::uint16_t general_purpose_flags;
    std::uint16_t compression_method;
    std::uint16_t modification_time;
    std::uint16_t modification_date;
    std::uint32_t crc32;
    std::uint64_t compressed_size;
    std::uint64_t expanded_size;
    std::uint16_t disk_number_start;
    std::uint16_t internal_file_attributes;
    std::uint32_t external_file_attributes;
    std::uint64_t central_header_offset;
    std::uint64_t central_record_end_offset;
    std::uint64_t local_header_offset;
    std::uint64_t data_offset;
    std::uint64_t data_end_offset;
    std::uint64_t local_record_end_offset;
    std::uint32_t data_descriptor_size;
    bool uses_data_descriptor;
    bool utf8_names;

    ZipArchiveEntry();
};

struct ZipArchiveIndex
{
    ZipArchiveFormatProfile format_profile;
    std::uint64_t archive_size;
    std::uint64_t central_directory_offset;
    std::uint64_t central_directory_size;
    std::uint64_t eocd_offset;
    std::string archive_comment;
    std::vector<ZipArchiveEntry> entries;
    PackageManifest package_manifest;

    ZipArchiveIndex();
};

struct ZipArchiveIndexResult
{
    ZipArchiveIndex index;
    ZipArchiveIndexError error;

    bool IsValid() const;
};

/// Parses an already-loaded, explicitly supplied local byte stream. It performs
/// no file access, decompression, extraction, execution, writes, or networking.
/// A valid result proves bounded structural/metadata consistency only: CRC32 is
/// retained but payload bytes are not decoded or checksummed by this layer.
///
/// Supported container profile:
///   PKWARE APPNOTE 6.3.10, sections 4.3.7, 4.3.9, 4.3.12, 4.3.16,
///   4.4.2-4.4.9, and 4.5.5, classic single-disk records only.
///   The only extra-field extension admitted is one exact APPNOTE 4.5.5 NTFS
///   timestamp record (0x000a) in each local or central extra-field area.
///   Duplicate 0x000a records and every non-allowlisted identifier fail closed.
///
/// Unsupported-but-valid ZIP features return dedicated *_UNSUPPORTED errors
/// instead of being described as malformed. The immutable source profile is:
/// https://pkware.cachefly.net/webdocs/APPNOTE/APPNOTE-6.3.10.TXT
ZipArchiveIndexResult BuildBeamNGZipArchiveIndex(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    const ZipArchiveScanLimits& zip_limits = ZipArchiveScanLimits(),
    const PackageScanLimits& package_limits = PackageScanLimits());

ZipArchiveIndexResult BuildBeamNGZipArchiveIndex(
    const std::vector<std::uint8_t>& bytes,
    const ZipArchiveScanLimits& zip_limits = ZipArchiveScanLimits(),
    const PackageScanLimits& package_limits = PackageScanLimits());

const char* ZipArchiveIndexErrorCodeToString(
    ZipArchiveIndexErrorCode code);

} // namespace BeamNG
} // namespace RoR
