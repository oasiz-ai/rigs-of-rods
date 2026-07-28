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

/// @file BeamNGZipArchiveIndex.cpp

#include "BeamNGZipArchiveIndex.h"

#include <algorithm>
#include <limits>
#include <map>

namespace RoR {
namespace BeamNG {
namespace {

// This parser intentionally implements only the bounded classic-record subset
// of the final PKWARE APPNOTE 6.3.10 (2022-11-01), specifically 4.3.7, 4.3.9,
// 4.3.12, 4.3.16, 4.4.2-4.4.9, and 4.5.5. ZIP64, spanning, encryption, SFX
// prefixes, archive extra records, and central-directory signatures are
// recognized but fail closed.
// Source: https://pkware.cachefly.net/webdocs/APPNOTE/APPNOTE-6.3.10.TXT

const std::uint32_t LOCAL_HEADER_SIGNATURE = UINT32_C(0x04034b50);
const std::uint32_t CENTRAL_HEADER_SIGNATURE = UINT32_C(0x02014b50);
const std::uint32_t CENTRAL_DIGITAL_SIGNATURE = UINT32_C(0x05054b50);
const std::uint32_t EOCD_SIGNATURE = UINT32_C(0x06054b50);
const std::uint32_t ZIP64_EOCD_SIGNATURE = UINT32_C(0x06064b50);
const std::uint32_t ZIP64_LOCATOR_SIGNATURE = UINT32_C(0x07064b50);
const std::uint32_t ARCHIVE_EXTRA_SIGNATURE = UINT32_C(0x08064b50);
const std::uint32_t DATA_DESCRIPTOR_SIGNATURE = UINT32_C(0x08074b50);

const std::size_t LOCAL_HEADER_SIZE = 30;
const std::size_t CENTRAL_HEADER_SIZE = 46;
const std::size_t EOCD_SIZE = 22;
const std::size_t MAX_CLASSIC_COMMENT_SIZE = 65535;
const std::size_t NO_ENTRY_INDEX =
    std::numeric_limits<std::size_t>::max();
const std::uint64_t NO_OFFSET =
    std::numeric_limits<std::uint64_t>::max();

struct ByteView
{
    const std::uint8_t* bytes;
    std::size_t size;

    bool CanRead(std::size_t offset, std::size_t length) const
    {
        return offset <= size && length <= size - offset;
    }

    std::uint16_t Read16(std::size_t offset) const
    {
        return static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::uint32_t Read32(std::size_t offset) const
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }
};

ZipArchiveIndexError MakeError(
    ZipArchiveIndexErrorCode code,
    std::uint64_t offset,
    std::size_t entry_index = NO_ENTRY_INDEX)
{
    ZipArchiveIndexError error;
    error.code = code;
    error.offset = offset;
    error.entry_index = entry_index;
    return error;
}

bool CheckedAddSize(
    std::size_t left,
    std::size_t right,
    std::size_t& result)
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
    {
        return false;
    }
    result = left + right;
    return true;
}

bool U64FitsSize(std::uint64_t value)
{
    return value <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max());
}

bool BytesEqual(
    const ByteView& view,
    std::size_t offset,
    const std::string& value,
    std::size_t& mismatch_offset)
{
    if (!view.CanRead(offset, value.size()))
    {
        mismatch_offset = offset;
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (view.bytes[offset + index] !=
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(value[index])))
        {
            mismatch_offset = offset + index;
            return false;
        }
    }
    mismatch_offset = offset + value.size();
    return true;
}

enum class ExtraFieldStatus
{
    VALID,
    INVALID,
    UNSUPPORTED,
    DUPLICATE,
    ZIP64,
    STRONG_ENCRYPTION,
    AES_ENCRYPTION,
    CERTIFICATE_METADATA,
    RECORD_MANAGEMENT,
    ALTERNATE_FILENAME
};

ExtraFieldStatus InspectNtfsTimestampExtraField(
    const ByteView& view,
    std::size_t data_offset,
    std::size_t data_size,
    std::size_t& diagnostic_offset)
{
    // APPNOTE 6.3.10 section 4.5.5 currently defines exactly one NTFS
    // attribute: four reserved zero bytes followed by tag 0x0001, a 24-byte
    // payload, and three 64-bit timestamps. This timestamp-only record is the
    // sole allowlisted extra field in the J0 profile because it cannot change
    // an entry's path, type, stream, payload extent, or content identity.
    if (data_size != 32U)
    {
        diagnostic_offset = data_offset;
        return ExtraFieldStatus::INVALID;
    }
    if (view.Read32(data_offset) != 0U)
    {
        diagnostic_offset = data_offset;
        return ExtraFieldStatus::INVALID;
    }
    if (view.Read16(data_offset + 4U) != UINT16_C(0x0001))
    {
        diagnostic_offset = data_offset + 4U;
        return ExtraFieldStatus::INVALID;
    }
    if (view.Read16(data_offset + 6U) != UINT16_C(24))
    {
        diagnostic_offset = data_offset + 6U;
        return ExtraFieldStatus::INVALID;
    }
    diagnostic_offset = data_offset + data_size;
    return ExtraFieldStatus::VALID;
}

ExtraFieldStatus InspectExtraFields(
    const ByteView& view,
    std::size_t offset,
    std::size_t length,
    std::size_t& diagnostic_offset)
{
    if (!view.CanRead(offset, length))
    {
        diagnostic_offset = offset;
        return ExtraFieldStatus::INVALID;
    }

    std::size_t cursor = offset;
    const std::size_t end = offset + length;
    // Each allowed identifier may occur at most once in this local or central
    // area. Identical duplicates are rejected rather than resolved by order.
    bool saw_ntfs_timestamps = false;
    while (cursor < end)
    {
        if (end - cursor < 4)
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::INVALID;
        }
        const std::uint16_t identifier = view.Read16(cursor);
        const std::uint16_t data_size = view.Read16(cursor + 2);
        const std::size_t data_offset = cursor + 4;
        if (static_cast<std::size_t>(data_size) > end - data_offset)
        {
            diagnostic_offset = cursor + 2;
            return ExtraFieldStatus::INVALID;
        }

        // 0x0001 is the ZIP64 extended information extra field. This
        // profile rejects its presence even when classic fields are not
        // sentinels, because APPNOTE permits ZIP64-sized descriptors then.
        if (identifier == UINT16_C(0x0001))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::ZIP64;
        }

        if (identifier == UINT16_C(0x000a))
        {
            if (saw_ntfs_timestamps)
            {
                diagnostic_offset = cursor;
                return ExtraFieldStatus::DUPLICATE;
            }
            saw_ntfs_timestamps = true;
            const ExtraFieldStatus ntfs_status =
                InspectNtfsTimestampExtraField(
                    view,
                    data_offset,
                    data_size,
                    diagnostic_offset);
            if (ntfs_status != ExtraFieldStatus::VALID)
            {
                return ntfs_status;
            }
            cursor = data_offset + static_cast<std::size_t>(data_size);
            continue;
        }

        // Keep distinct unsupported statuses for valid PKWARE certificate,
        // encryption, and record-management extensions. They are separate
        // features in APPNOTE 4.5 and must not be conflated as malformed.
        if (identifier >= UINT16_C(0x0014) &&
            identifier <= UINT16_C(0x0016))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::CERTIFICATE_METADATA;
        }
        if (identifier == UINT16_C(0x0017) ||
            identifier == UINT16_C(0x0019))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::STRONG_ENCRYPTION;
        }
        if (identifier == UINT16_C(0x0018))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::RECORD_MANAGEMENT;
        }
        if (identifier == UINT16_C(0x9901))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::AES_ENCRYPTION;
        }
        // APPNOTE 4.6.9 recognizes the Info-ZIP Unicode Path field. Since
        // this index deliberately has one portable ASCII path identity,
        // alternate filename metadata is rejected instead of silently
        // choosing between two names.
        if (identifier == UINT16_C(0x7075) ||
            identifier == UINT16_C(0x0008) ||
            identifier == UINT16_C(0x2605) ||
            identifier == UINT16_C(0x4f4c) ||
            identifier == UINT16_C(0x554e))
        {
            diagnostic_offset = cursor;
            return ExtraFieldStatus::ALTERNATE_FILENAME;
        }

        // Every other PKWARE or third-party extension fails closed. In
        // particular, 0x000d and the Info-ZIP/ASi UNIX fields can carry link
        // targets or special-file metadata, while 0x000e and several
        // platform-specific fields can introduce alternate streams/forks.
        // Ignoring those records here and later handing the archive to a
        // feature-aware extractor would bypass the canonical manifest.
        diagnostic_offset = cursor;
        return ExtraFieldStatus::UNSUPPORTED;
    }
    diagnostic_offset = end;
    return ExtraFieldStatus::VALID;
}

ZipArchiveIndexError ExtraFieldError(
    ExtraFieldStatus status,
    bool central,
    std::size_t offset,
    std::size_t entry_index)
{
    switch (status)
    {
    case ExtraFieldStatus::UNSUPPORTED:
        return MakeError(
            ZipArchiveIndexErrorCode::EXTRA_FIELD_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::DUPLICATE:
        return MakeError(
            ZipArchiveIndexErrorCode::DUPLICATE_EXTRA_FIELD,
            offset,
            entry_index);
    case ExtraFieldStatus::ZIP64:
        return MakeError(
            ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::STRONG_ENCRYPTION:
        return MakeError(
            ZipArchiveIndexErrorCode::STRONG_ENCRYPTION_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::AES_ENCRYPTION:
        return MakeError(
            ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::CERTIFICATE_METADATA:
        return MakeError(
            ZipArchiveIndexErrorCode::
                CERTIFICATE_METADATA_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::RECORD_MANAGEMENT:
        return MakeError(
            ZipArchiveIndexErrorCode::
                RECORD_MANAGEMENT_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::ALTERNATE_FILENAME:
        return MakeError(
            ZipArchiveIndexErrorCode::
                ALTERNATE_FILENAME_UNSUPPORTED,
            offset,
            entry_index);
    case ExtraFieldStatus::INVALID:
        return MakeError(
            central
                ? ZipArchiveIndexErrorCode::CENTRAL_EXTRA_FIELD_INVALID
                : ZipArchiveIndexErrorCode::LOCAL_EXTRA_FIELD_INVALID,
            offset,
            entry_index);
    case ExtraFieldStatus::VALID:
        break;
    }
    return ZipArchiveIndexError();
}

ZipArchiveIndexError ValidateGeneralPurposeFlags(
    std::uint16_t flags,
    std::uint16_t method,
    std::size_t field_offset,
    std::size_t entry_index)
{
    const std::uint16_t reserved_mask =
        // Bits 4 and 12 are reserved for enhanced compression features
        // outside this metadata profile; bits 7-10 and 15 are unused or
        // reserved by APPNOTE 6.3.10 section 4.4.4.
        UINT16_C(0x1790) | UINT16_C(0x8000);
    if ((flags & UINT16_C(0x2000)) != 0)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::
                CENTRAL_DIRECTORY_ENCRYPTION_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    if ((flags & UINT16_C(0x0040)) != 0)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::STRONG_ENCRYPTION_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    if ((flags & UINT16_C(0x0001)) != 0 ||
        method == UINT16_C(99))
    {
        return MakeError(
            ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    if ((flags & UINT16_C(0x0020)) != 0)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::PATCHED_DATA_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    if ((flags & UINT16_C(0x4000)) != 0)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::ALTERNATE_STREAMS_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    if ((flags & reserved_mask) != 0)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::
                GENERAL_PURPOSE_FLAGS_UNSUPPORTED,
            field_offset,
            entry_index);
    }

    // Bits 1 and 2 only have defined meanings for Implode, Deflate,
    // Deflate64, and LZMA. For LZMA only bit 1 is defined. Reject undefined
    // combinations so a later decoder cannot assign implementation-specific
    // meaning to metadata accepted by this boundary.
    const std::uint16_t compression_option_flags =
        flags & UINT16_C(0x0006);
    const bool options_are_defined =
        compression_option_flags == 0 ||
        method == UINT16_C(6) ||
        method == UINT16_C(8) ||
        method == UINT16_C(9) ||
        (method == UINT16_C(14) &&
         (compression_option_flags & UINT16_C(0x0004)) == 0);
    if (!options_are_defined)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::
                GENERAL_PURPOSE_FLAGS_UNSUPPORTED,
            field_offset,
            entry_index);
    }
    return ZipArchiveIndexError();
}

bool IsValidUtf8(
    const std::string& value,
    std::size_t& invalid_offset)
{
    std::size_t cursor = 0;
    while (cursor < value.size())
    {
        const std::uint8_t first = static_cast<std::uint8_t>(
            static_cast<unsigned char>(value[cursor]));
        if (first <= UINT8_C(0x7f))
        {
            ++cursor;
            continue;
        }

        std::size_t sequence_size = 0;
        std::uint8_t second_minimum = UINT8_C(0x80);
        std::uint8_t second_maximum = UINT8_C(0xbf);
        if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf))
        {
            sequence_size = 2;
        }
        else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef))
        {
            sequence_size = 3;
            if (first == UINT8_C(0xe0))
                second_minimum = UINT8_C(0xa0);
            else if (first == UINT8_C(0xed))
                second_maximum = UINT8_C(0x9f);
        }
        else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4))
        {
            sequence_size = 4;
            if (first == UINT8_C(0xf0))
                second_minimum = UINT8_C(0x90);
            else if (first == UINT8_C(0xf4))
                second_maximum = UINT8_C(0x8f);
        }
        else
        {
            invalid_offset = cursor;
            return false;
        }

        if (sequence_size > value.size() - cursor)
        {
            invalid_offset = cursor;
            return false;
        }

        const std::uint8_t second = static_cast<std::uint8_t>(
            static_cast<unsigned char>(value[cursor + 1]));
        if (second < second_minimum || second > second_maximum)
        {
            invalid_offset = cursor + 1;
            return false;
        }
        for (std::size_t index = 2; index < sequence_size; ++index)
        {
            const std::uint8_t continuation =
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(
                        value[cursor + index]));
            if (continuation < UINT8_C(0x80) ||
                continuation > UINT8_C(0xbf))
            {
                invalid_offset = cursor + index;
                return false;
            }
        }
        cursor += sequence_size;
    }

    invalid_offset = value.size();
    return true;
}

ZipArchiveIndexError ClassifyEntryKind(
    const std::string& path,
    std::uint8_t creator_host,
    std::uint32_t external_attributes,
    std::size_t field_offset,
    std::size_t entry_index,
    PackageEntryKind& kind)
{
    const bool has_directory_marker =
        !path.empty() && path[path.size() - 1] == '/';
    const std::uint8_t dos_attributes =
        static_cast<std::uint8_t>(external_attributes & UINT32_C(0xff));
    const bool dos_directory = (dos_attributes & UINT8_C(0x10)) != 0;
    const bool volume_label = (dos_attributes & UINT8_C(0x08)) != 0;
    const bool dos_device = (dos_attributes & UINT8_C(0x40)) != 0;
    const bool windows_reparse_point =
        (external_attributes & UINT32_C(0x00000400)) != 0;
    const bool has_unix_attributes =
        creator_host == UINT8_C(3) || creator_host == UINT8_C(19);
    const std::uint16_t unix_mode =
        static_cast<std::uint16_t>(external_attributes >> 16);
    const std::uint16_t unix_type =
        static_cast<std::uint16_t>(unix_mode & UINT16_C(0170000));
    const bool unix_regular = unix_type == UINT16_C(0100000);
    const bool unix_directory = unix_type == UINT16_C(0040000);

    if (has_unix_attributes && unix_type == UINT16_C(0120000))
    {
        return MakeError(
            ZipArchiveIndexErrorCode::SYMLINK_REJECTED,
            field_offset,
            entry_index);
    }
    if (has_unix_attributes &&
        unix_type != 0 &&
        !unix_regular &&
        !unix_directory)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE,
            field_offset,
            entry_index);
    }
    if (volume_label || dos_device || windows_reparse_point)
    {
        return MakeError(
            ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE,
            field_offset,
            entry_index);
    }

    if (unix_regular && (has_directory_marker || dos_directory))
    {
        return MakeError(
            ZipArchiveIndexErrorCode::ENTRY_TYPE_MISMATCH,
            field_offset,
            entry_index);
    }

    const bool is_directory =
        has_directory_marker || dos_directory || unix_directory;
    kind = is_directory
        ? PackageEntryKind::DIRECTORY
        : PackageEntryKind::REGULAR_FILE;
    return ZipArchiveIndexError();
}

struct LocalRange
{
    std::uint64_t begin;
    std::uint64_t end;
    std::size_t entry_index;
};

bool LocalRangeLess(const LocalRange& left, const LocalRange& right)
{
    if (left.begin != right.begin)
        return left.begin < right.begin;
    if (left.end != right.end)
        return left.end < right.end;
    return left.entry_index < right.entry_index;
}

ZipArchiveIndexError ParseDataDescriptor(
    const ByteView& view,
    std::size_t offset,
    std::size_t central_directory_offset,
    std::uint32_t expected_crc,
    std::uint32_t expected_compressed_size,
    std::uint32_t expected_expanded_size,
    std::size_t entry_index,
    std::uint32_t& descriptor_size)
{
    descriptor_size = 0;
    if (offset > central_directory_offset ||
        central_directory_offset - offset < 12 ||
        !view.CanRead(offset, 12))
    {
        return MakeError(
            ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_TRUNCATED,
            offset,
            entry_index);
    }

    const bool unsigned_matches =
        view.Read32(offset) == expected_crc &&
        view.Read32(offset + 4) == expected_compressed_size &&
        view.Read32(offset + 8) == expected_expanded_size;

    if (view.Read32(offset) == DATA_DESCRIPTOR_SIGNATURE)
    {
        if (central_directory_offset - offset >= 16 &&
            view.CanRead(offset, 16) &&
            view.Read32(offset + 4) == expected_crc &&
            view.Read32(offset + 8) == expected_compressed_size &&
            view.Read32(offset + 12) == expected_expanded_size)
        {
            descriptor_size = 16;
            return ZipArchiveIndexError();
        }

        // A signatureless descriptor whose CRC happens to equal the
        // conventional descriptor signature remains unambiguous when its
        // following two size fields match the central directory.
        if (unsigned_matches)
        {
            descriptor_size = 12;
            return ZipArchiveIndexError();
        }

        if (central_directory_offset - offset < 16)
        {
            return MakeError(
                ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_TRUNCATED,
                offset,
                entry_index);
        }
    }
    else if (unsigned_matches)
    {
        descriptor_size = 12;
        return ZipArchiveIndexError();
    }

    return MakeError(
        ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_MISMATCH,
        offset,
        entry_index);
}

ZipArchiveIndexErrorCode SignatureSpecificCentralError(
    std::uint32_t signature)
{
    if (signature == ARCHIVE_EXTRA_SIGNATURE)
    {
        return ZipArchiveIndexErrorCode::
            ARCHIVE_EXTRA_DATA_UNSUPPORTED;
    }
    if (signature == CENTRAL_DIGITAL_SIGNATURE)
    {
        return ZipArchiveIndexErrorCode::
            CENTRAL_DIRECTORY_DIGITAL_SIGNATURE_UNSUPPORTED;
    }
    if (signature == ZIP64_EOCD_SIGNATURE ||
        signature == ZIP64_LOCATOR_SIGNATURE)
    {
        return ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED;
    }
    return ZipArchiveIndexErrorCode::CENTRAL_HEADER_SIGNATURE;
}

} // anonymous namespace

ZipArchiveFormatProfile::ZipArchiveFormatProfile() :
    identifier("pkware-appnote"),
    version("6.3.10-classic-single-disk-index-v1")
{
}

ZipArchiveScanLimits::ZipArchiveScanLimits() :
    max_archive_bytes(UINT64_C(4294967294)),
    max_central_directory_bytes(UINT64_C(67108864)),
    max_entry_metadata_bytes(262144),
    max_archive_comment_bytes(MAX_CLASSIC_COMMENT_SIZE)
{
}

ZipArchiveIndexError::ZipArchiveIndexError() :
    code(ZipArchiveIndexErrorCode::NONE),
    offset(NO_OFFSET),
    entry_index(NO_ENTRY_INDEX),
    conflicting_entry_index(NO_ENTRY_INDEX)
{
}

bool ZipArchiveIndexError::HasError() const
{
    return code != ZipArchiveIndexErrorCode::NONE;
}

ZipArchiveEntry::ZipArchiveEntry() :
    kind(PackageEntryKind::REGULAR_FILE),
    version_made_by(0),
    creator_host_system(0),
    version_needed(0),
    general_purpose_flags(0),
    compression_method(0),
    modification_time(0),
    modification_date(0),
    crc32(0),
    compressed_size(0),
    expanded_size(0),
    disk_number_start(0),
    internal_file_attributes(0),
    external_file_attributes(0),
    central_header_offset(0),
    central_record_end_offset(0),
    local_header_offset(0),
    data_offset(0),
    data_end_offset(0),
    local_record_end_offset(0),
    data_descriptor_size(0),
    uses_data_descriptor(false),
    utf8_names(false)
{
}

ZipArchiveIndex::ZipArchiveIndex() :
    archive_size(0),
    central_directory_offset(0),
    central_directory_size(0),
    eocd_offset(0)
{
}

bool ZipArchiveIndexResult::IsValid() const
{
    return !error.HasError();
}

ZipArchiveIndexResult BuildBeamNGZipArchiveIndex(
    const std::uint8_t* bytes,
    std::size_t byte_count,
    const ZipArchiveScanLimits& zip_limits,
    const PackageScanLimits& package_limits)
{
    ZipArchiveIndexResult result;
    result.index.archive_size = static_cast<std::uint64_t>(byte_count);

    if (bytes == NULL && byte_count != 0)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::NULL_INPUT, 0);
        return result;
    }
    if (static_cast<std::uint64_t>(byte_count) >
        zip_limits.max_archive_bytes)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ARCHIVE_SIZE_LIMIT,
            zip_limits.max_archive_bytes);
        return result;
    }
    if (byte_count < EOCD_SIZE)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ARCHIVE_TOO_SMALL,
            byte_count);
        return result;
    }

    const ByteView view = {bytes, byte_count};

    // APPNOTE caps a classic archive comment at 65535 bytes. A candidate
    // EOCD is accepted only when its declared comment ends exactly at EOF.
    const std::size_t maximum_search_span =
        EOCD_SIZE + MAX_CLASSIC_COMMENT_SIZE;
    const std::size_t search_begin =
        byte_count > maximum_search_span
            ? byte_count - maximum_search_span
            : 0;
    std::size_t exact_eocd = 0;
    std::size_t exact_count = 0;
    std::size_t last_signature = 0;
    bool saw_signature = false;

    for (std::size_t offset = search_begin;
         offset <= byte_count - 4;
         ++offset)
    {
        if (view.Read32(offset) != EOCD_SIGNATURE)
        {
            continue;
        }
        saw_signature = true;
        last_signature = offset;
        if (!view.CanRead(offset, EOCD_SIZE))
        {
            continue;
        }
        const std::size_t comment_size = view.Read16(offset + 20);
        const std::size_t record_size = EOCD_SIZE + comment_size;
        if (view.CanRead(offset, record_size) &&
            offset + record_size == byte_count)
        {
            exact_eocd = offset;
            ++exact_count;
        }
    }

    if (exact_count == 0)
    {
        if (!saw_signature)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::EOCD_NOT_FOUND,
                search_begin);
        }
        else if (!view.CanRead(last_signature, EOCD_SIZE))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::EOCD_TRUNCATED,
                last_signature);
        }
        else
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    EOCD_COMMENT_LENGTH_MISMATCH,
                last_signature + 20);
        }
        return result;
    }
    if (exact_count != 1)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::EOCD_AMBIGUOUS,
            exact_eocd);
        return result;
    }

    const std::size_t eocd_offset = exact_eocd;
    const std::uint16_t archive_comment_size =
        view.Read16(eocd_offset + 20);
    if (archive_comment_size > zip_limits.max_archive_comment_bytes)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ARCHIVE_COMMENT_LIMIT,
            eocd_offset + 20);
        return result;
    }

    const std::uint16_t disk_number = view.Read16(eocd_offset + 4);
    const std::uint16_t central_directory_disk =
        view.Read16(eocd_offset + 6);
    const std::uint16_t entries_on_disk =
        view.Read16(eocd_offset + 8);
    const std::uint16_t entry_count =
        view.Read16(eocd_offset + 10);
    const std::uint32_t central_directory_size =
        view.Read32(eocd_offset + 12);
    const std::uint32_t central_directory_offset =
        view.Read32(eocd_offset + 16);

    std::size_t zip64_sentinel_offset = eocd_offset + 4;
    bool has_zip64_sentinel = false;
    if (disk_number == UINT16_MAX)
    {
        has_zip64_sentinel = true;
    }
    else if (central_directory_disk == UINT16_MAX)
    {
        has_zip64_sentinel = true;
        zip64_sentinel_offset = eocd_offset + 6;
    }
    else if (entries_on_disk == UINT16_MAX)
    {
        has_zip64_sentinel = true;
        zip64_sentinel_offset = eocd_offset + 8;
    }
    else if (entry_count == UINT16_MAX)
    {
        has_zip64_sentinel = true;
        zip64_sentinel_offset = eocd_offset + 10;
    }
    else if (central_directory_size == UINT32_MAX)
    {
        has_zip64_sentinel = true;
        zip64_sentinel_offset = eocd_offset + 12;
    }
    else if (central_directory_offset == UINT32_MAX)
    {
        has_zip64_sentinel = true;
        zip64_sentinel_offset = eocd_offset + 16;
    }
    if (has_zip64_sentinel)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
            zip64_sentinel_offset);
        return result;
    }
    if (eocd_offset >= 20 &&
        view.Read32(eocd_offset - 20) == ZIP64_LOCATOR_SIGNATURE)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
            eocd_offset - 20);
        return result;
    }
    if (disk_number != 0 || central_directory_disk != 0)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::MULTI_DISK_UNSUPPORTED,
            eocd_offset + 4);
        return result;
    }
    if (entries_on_disk != entry_count)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ENTRY_COUNT_MISMATCH,
            eocd_offset + 8);
        return result;
    }
    if (static_cast<std::size_t>(entry_count) >
        package_limits.max_entries)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::ENTRY_COUNT_LIMIT,
            eocd_offset + 10);
        return result;
    }
    if (static_cast<std::uint64_t>(central_directory_size) >
        zip_limits.max_central_directory_bytes)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_SIZE_LIMIT,
            eocd_offset + 12);
        return result;
    }

    const std::uint64_t central_end_u64 =
        static_cast<std::uint64_t>(central_directory_offset) +
        static_cast<std::uint64_t>(central_directory_size);
    if (!U64FitsSize(central_directory_offset) ||
        !U64FitsSize(central_end_u64) ||
        central_end_u64 >
            static_cast<std::uint64_t>(eocd_offset))
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_BOUNDS,
            eocd_offset + 16);
        return result;
    }
    const std::size_t central_offset =
        static_cast<std::size_t>(central_directory_offset);
    const std::size_t central_end =
        static_cast<std::size_t>(central_end_u64);
    if (central_end != eocd_offset)
    {
        if (central_end <= byte_count - 4 &&
            (view.Read32(central_end) == ZIP64_EOCD_SIGNATURE ||
             view.Read32(central_end) == ZIP64_LOCATOR_SIGNATURE))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
                central_end);
        }
        else
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    CENTRAL_DIRECTORY_NOT_ADJACENT_TO_EOCD,
                central_end);
        }
        return result;
    }

    result.index.eocd_offset = eocd_offset;
    result.index.central_directory_offset = central_directory_offset;
    result.index.central_directory_size = central_directory_size;
    result.index.archive_comment.assign(
        reinterpret_cast<const char*>(bytes + eocd_offset + EOCD_SIZE),
        archive_comment_size);
    result.index.entries.reserve(entry_count);

    std::map<std::uint64_t, std::size_t> local_offsets;
    std::size_t cursor = central_offset;
    for (std::size_t entry_index = 0;
         entry_index < static_cast<std::size_t>(entry_count);
         ++entry_index)
    {
        if (!view.CanRead(cursor, CENTRAL_HEADER_SIZE) ||
            cursor + CENTRAL_HEADER_SIZE > central_end)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::CENTRAL_HEADER_TRUNCATED,
                cursor,
                entry_index);
            return result;
        }
        const std::uint32_t signature = view.Read32(cursor);
        if (signature != CENTRAL_HEADER_SIGNATURE)
        {
            result.error = MakeError(
                SignatureSpecificCentralError(signature),
                cursor,
                entry_index);
            return result;
        }

        const std::uint16_t name_size = view.Read16(cursor + 28);
        const std::uint16_t extra_size = view.Read16(cursor + 30);
        const std::uint16_t comment_size = view.Read16(cursor + 32);
        std::size_t metadata_size =
            static_cast<std::size_t>(name_size);
        if (!CheckedAddSize(
                metadata_size,
                static_cast<std::size_t>(extra_size),
                metadata_size) ||
            !CheckedAddSize(
                metadata_size,
                static_cast<std::size_t>(comment_size),
                metadata_size) ||
            metadata_size > zip_limits.max_entry_metadata_bytes)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    CENTRAL_ENTRY_METADATA_LIMIT,
                cursor + 28,
                entry_index);
            return result;
        }
        std::size_t record_end = 0;
        if (!CheckedAddSize(
                cursor,
                CENTRAL_HEADER_SIZE,
                record_end) ||
            !CheckedAddSize(record_end, metadata_size, record_end) ||
            record_end > central_end)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::CENTRAL_ENTRY_TRUNCATED,
                cursor + 28,
                entry_index);
            return result;
        }

        ZipArchiveEntry entry;
        entry.central_header_offset = cursor;
        entry.central_record_end_offset = record_end;
        entry.version_made_by = view.Read16(cursor + 4);
        entry.creator_host_system =
            static_cast<std::uint8_t>(entry.version_made_by >> 8);
        entry.version_needed = view.Read16(cursor + 6);
        entry.general_purpose_flags = view.Read16(cursor + 8);
        entry.compression_method = view.Read16(cursor + 10);
        entry.modification_time = view.Read16(cursor + 12);
        entry.modification_date = view.Read16(cursor + 14);
        entry.crc32 = view.Read32(cursor + 16);
        entry.compressed_size = view.Read32(cursor + 20);
        entry.expanded_size = view.Read32(cursor + 24);
        entry.disk_number_start = view.Read16(cursor + 34);
        entry.internal_file_attributes = view.Read16(cursor + 36);
        entry.external_file_attributes = view.Read32(cursor + 38);
        entry.local_header_offset = view.Read32(cursor + 42);
        entry.uses_data_descriptor =
            (entry.general_purpose_flags & UINT16_C(0x0008)) != 0;
        entry.utf8_names =
            (entry.general_purpose_flags & UINT16_C(0x0800)) != 0;

        std::size_t entry_zip64_offset = cursor + 20;
        bool entry_uses_zip64_sentinel = false;
        if (entry.compressed_size == UINT32_MAX)
        {
            entry_uses_zip64_sentinel = true;
        }
        else if (entry.expanded_size == UINT32_MAX)
        {
            entry_uses_zip64_sentinel = true;
            entry_zip64_offset = cursor + 24;
        }
        else if (entry.disk_number_start == UINT16_MAX)
        {
            entry_uses_zip64_sentinel = true;
            entry_zip64_offset = cursor + 34;
        }
        else if (entry.local_header_offset == UINT32_MAX)
        {
            entry_uses_zip64_sentinel = true;
            entry_zip64_offset = cursor + 42;
        }
        if (entry_uses_zip64_sentinel)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
                entry_zip64_offset,
                entry_index);
            return result;
        }
        if (entry.disk_number_start != 0)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::ENTRY_DISK_UNSUPPORTED,
                cursor + 34,
                entry_index);
            return result;
        }

        result.error = ValidateGeneralPurposeFlags(
            entry.general_purpose_flags,
            entry.compression_method,
            cursor + 8,
            entry_index);
        if (result.error.HasError())
        {
            return result;
        }

        const std::size_t name_offset = cursor + CENTRAL_HEADER_SIZE;
        const std::size_t extra_offset =
            name_offset + static_cast<std::size_t>(name_size);
        const std::size_t comment_offset =
            extra_offset + static_cast<std::size_t>(extra_size);
        std::size_t extra_diagnostic_offset = extra_offset;
        const ExtraFieldStatus extra_status = InspectExtraFields(
            view,
            extra_offset,
            extra_size,
            extra_diagnostic_offset);
        if (extra_status != ExtraFieldStatus::VALID)
        {
            result.error = ExtraFieldError(
                extra_status,
                true,
                extra_diagnostic_offset,
                entry_index);
            return result;
        }

        entry.path.assign(
            reinterpret_cast<const char*>(bytes + name_offset),
            name_size);
        entry.comment.assign(
            reinterpret_cast<const char*>(bytes + comment_offset),
            comment_size);

        if (entry.utf8_names)
        {
            std::size_t invalid_utf8_offset = 0;
            if (!IsValidUtf8(entry.path, invalid_utf8_offset))
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::ENTRY_UTF8_INVALID,
                    name_offset + invalid_utf8_offset,
                    entry_index);
                return result;
            }
            if (!IsValidUtf8(entry.comment, invalid_utf8_offset))
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::ENTRY_UTF8_INVALID,
                    comment_offset + invalid_utf8_offset,
                    entry_index);
                return result;
            }
        }

        result.error = ClassifyEntryKind(
            entry.path,
            entry.creator_host_system,
            entry.external_file_attributes,
            cursor + 38,
            entry_index,
            entry.kind);
        if (result.error.HasError())
        {
            return result;
        }

        const std::pair<
            std::map<std::uint64_t, std::size_t>::iterator,
            bool> inserted =
                local_offsets.insert(
                    std::make_pair(
                        entry.local_header_offset,
                        entry_index));
        if (!inserted.second)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    DUPLICATE_LOCAL_HEADER_OFFSET,
                cursor + 42,
                entry_index);
            result.error.conflicting_entry_index =
                inserted.first->second;
            return result;
        }

        result.index.entries.push_back(entry);
        cursor = record_end;
    }

    if (cursor != central_end)
    {
        const std::uint32_t signature =
            view.CanRead(cursor, 4) ? view.Read32(cursor) : 0;
        ZipArchiveIndexErrorCode code =
            SignatureSpecificCentralError(signature);
        if (signature == CENTRAL_HEADER_SIGNATURE)
        {
            code = ZipArchiveIndexErrorCode::ENTRY_COUNT_MISMATCH;
        }
        result.error = MakeError(code, cursor);
        return result;
    }

    std::vector<LocalRange> ranges;
    ranges.reserve(result.index.entries.size());
    for (std::size_t entry_index = 0;
         entry_index < result.index.entries.size();
         ++entry_index)
    {
        ZipArchiveEntry& entry = result.index.entries[entry_index];
        if (!U64FitsSize(entry.local_header_offset))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_HEADER_BOUNDS,
                entry.central_header_offset + 42,
                entry_index);
            return result;
        }
        const std::size_t local_offset =
            static_cast<std::size_t>(entry.local_header_offset);
        if (local_offset >= central_offset ||
            !view.CanRead(local_offset, 4))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_HEADER_BOUNDS,
                entry.central_header_offset + 42,
                entry_index);
            return result;
        }
        if (view.Read32(local_offset) != LOCAL_HEADER_SIGNATURE)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_HEADER_SIGNATURE,
                local_offset,
                entry_index);
            return result;
        }
        if (!view.CanRead(local_offset, LOCAL_HEADER_SIZE) ||
            local_offset + LOCAL_HEADER_SIZE > central_offset)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_HEADER_TRUNCATED,
                local_offset,
                entry_index);
            return result;
        }

        const std::uint16_t local_version =
            view.Read16(local_offset + 4);
        const std::uint16_t local_flags =
            view.Read16(local_offset + 6);
        const std::uint16_t local_method =
            view.Read16(local_offset + 8);
        const std::uint32_t local_crc =
            view.Read32(local_offset + 14);
        const std::uint32_t local_compressed_size =
            view.Read32(local_offset + 18);
        const std::uint32_t local_expanded_size =
            view.Read32(local_offset + 22);
        const std::uint16_t local_name_size =
            view.Read16(local_offset + 26);
        const std::uint16_t local_extra_size =
            view.Read16(local_offset + 28);

        std::size_t local_metadata_size =
            static_cast<std::size_t>(local_name_size);
        if (!CheckedAddSize(
                local_metadata_size,
                static_cast<std::size_t>(local_extra_size),
                local_metadata_size) ||
            local_metadata_size > zip_limits.max_entry_metadata_bytes)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_ENTRY_METADATA_LIMIT,
                local_offset + 26,
                entry_index);
            return result;
        }
        std::size_t data_offset = 0;
        if (!CheckedAddSize(
                local_offset,
                LOCAL_HEADER_SIZE,
                data_offset) ||
            !CheckedAddSize(
                data_offset,
                local_metadata_size,
                data_offset) ||
            data_offset > central_offset)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_HEADER_TRUNCATED,
                local_offset + 26,
                entry_index);
            return result;
        }

        if (local_compressed_size == UINT32_MAX ||
            local_expanded_size == UINT32_MAX)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED,
                local_offset +
                    (local_compressed_size == UINT32_MAX ? 18 : 22),
                entry_index);
            return result;
        }
        if (local_version != entry.version_needed)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_VERSION_MISMATCH,
                local_offset + 4,
                entry_index);
            return result;
        }
        if (local_flags != entry.general_purpose_flags)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_FLAGS_MISMATCH,
                local_offset + 6,
                entry_index);
            return result;
        }
        if (local_method != entry.compression_method)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_METHOD_MISMATCH,
                local_offset + 8,
                entry_index);
            return result;
        }
        if (local_name_size != entry.path.size())
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_FILENAME_MISMATCH,
                local_offset + 26,
                entry_index);
            return result;
        }

        const std::size_t local_name_offset =
            local_offset + LOCAL_HEADER_SIZE;
        std::size_t filename_mismatch_offset = local_name_offset;
        if (!BytesEqual(
                view,
                local_name_offset,
                entry.path,
                filename_mismatch_offset))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::LOCAL_FILENAME_MISMATCH,
                filename_mismatch_offset,
                entry_index);
            return result;
        }

        const std::size_t local_extra_offset =
            local_name_offset + local_name_size;
        std::size_t local_extra_diagnostic_offset =
            local_extra_offset;
        const ExtraFieldStatus local_extra_status =
            InspectExtraFields(
                view,
                local_extra_offset,
                local_extra_size,
                local_extra_diagnostic_offset);
        if (local_extra_status != ExtraFieldStatus::VALID)
        {
            result.error = ExtraFieldError(
                local_extra_status,
                false,
                local_extra_diagnostic_offset,
                entry_index);
            return result;
        }

        if (!entry.uses_data_descriptor)
        {
            if (local_crc != entry.crc32)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::LOCAL_CRC_MISMATCH,
                    local_offset + 14,
                    entry_index);
                return result;
            }
            if (local_compressed_size != entry.compressed_size)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::
                        LOCAL_COMPRESSED_SIZE_MISMATCH,
                    local_offset + 18,
                    entry_index);
                return result;
            }
            if (local_expanded_size != entry.expanded_size)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::
                        LOCAL_UNCOMPRESSED_SIZE_MISMATCH,
                    local_offset + 22,
                    entry_index);
                return result;
            }
        }
        else
        {
            // APPNOTE 6.3.10 sections 4.4.4 and 4.4.7-4.4.9 require
            // streamed classic entries to zero all three local placeholders.
            // Accepting arbitrary values here creates conflicting metadata
            // interpretations between readers that prefer local versus
            // central records.
            if (local_crc != 0U)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::LOCAL_CRC_MISMATCH,
                    local_offset + 14,
                    entry_index);
                return result;
            }
            if (local_compressed_size != 0U)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::
                        LOCAL_COMPRESSED_SIZE_MISMATCH,
                    local_offset + 18,
                    entry_index);
                return result;
            }
            if (local_expanded_size != 0U)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::
                        LOCAL_UNCOMPRESSED_SIZE_MISMATCH,
                    local_offset + 22,
                    entry_index);
                return result;
            }
        }

        if (entry.compression_method == 0 &&
            entry.compressed_size != entry.expanded_size)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::STORED_SIZE_MISMATCH,
                entry.central_header_offset + 20,
                entry_index);
            return result;
        }

        const std::uint64_t data_end_u64 =
            static_cast<std::uint64_t>(data_offset) +
            entry.compressed_size;
        if (!U64FitsSize(data_end_u64) ||
            data_end_u64 >
                static_cast<std::uint64_t>(central_offset))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    DATA_CROSSES_CENTRAL_DIRECTORY,
                entry.central_header_offset + 20,
                entry_index);
            return result;
        }
        const std::size_t data_end =
            static_cast<std::size_t>(data_end_u64);
        std::uint32_t descriptor_size = 0;
        if (entry.uses_data_descriptor)
        {
            result.error = ParseDataDescriptor(
                view,
                data_end,
                central_offset,
                entry.crc32,
                static_cast<std::uint32_t>(entry.compressed_size),
                static_cast<std::uint32_t>(entry.expanded_size),
                entry_index,
                descriptor_size);
            if (result.error.HasError())
            {
                return result;
            }
        }

        const std::uint64_t record_end_u64 =
            data_end_u64 + descriptor_size;
        if (!U64FitsSize(record_end_u64) ||
            record_end_u64 >
                static_cast<std::uint64_t>(central_offset))
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    DATA_CROSSES_CENTRAL_DIRECTORY,
                data_end,
                entry_index);
            return result;
        }

        entry.data_offset = data_offset;
        entry.data_end_offset = data_end;
        entry.data_descriptor_size = descriptor_size;
        entry.local_record_end_offset = record_end_u64;

        LocalRange range;
        range.begin = entry.local_header_offset;
        range.end = entry.local_record_end_offset;
        range.entry_index = entry_index;
        ranges.push_back(range);
    }

    std::sort(ranges.begin(), ranges.end(), LocalRangeLess);
    if (!ranges.empty())
    {
        if (ranges[0].begin != 0)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    SELF_EXTRACTING_PREFIX_UNSUPPORTED,
                0,
                ranges[0].entry_index);
            return result;
        }

        for (std::size_t index = 1; index < ranges.size(); ++index)
        {
            if (ranges[index].begin < ranges[index - 1].end)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::LOCAL_RANGE_OVERLAP,
                    ranges[index].begin,
                    ranges[index].entry_index);
                result.error.conflicting_entry_index =
                    ranges[index - 1].entry_index;
                return result;
            }
            if (ranges[index].begin != ranges[index - 1].end)
            {
                result.error = MakeError(
                    ZipArchiveIndexErrorCode::
                        INTER_RECORD_DATA_UNSUPPORTED,
                    ranges[index - 1].end,
                    ranges[index].entry_index);
                result.error.conflicting_entry_index =
                    ranges[index - 1].entry_index;
                return result;
            }
        }

        if (ranges[ranges.size() - 1].end !=
            central_directory_offset)
        {
            result.error = MakeError(
                ZipArchiveIndexErrorCode::
                    INTER_RECORD_DATA_UNSUPPORTED,
                ranges[ranges.size() - 1].end,
                ranges[ranges.size() - 1].entry_index);
            return result;
        }
    }
    else if (central_directory_offset != 0)
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::
                SELF_EXTRACTING_PREFIX_UNSUPPORTED,
            0);
        return result;
    }

    std::vector<PackageEntryInput> manifest_entries;
    manifest_entries.reserve(result.index.entries.size());
    for (std::vector<ZipArchiveEntry>::const_iterator it =
             result.index.entries.begin();
         it != result.index.entries.end();
         ++it)
    {
        PackageEntryInput manifest_entry;
        manifest_entry.path = it->path;
        manifest_entry.kind = it->kind;
        manifest_entry.compressed_size = it->compressed_size;
        manifest_entry.expanded_size = it->expanded_size;
        manifest_entry.encrypted = false;
        manifest_entries.push_back(manifest_entry);
    }

    const PackageManifestResult manifest_result =
        BuildPackageManifest(manifest_entries, package_limits);
    if (!manifest_result.IsValid())
    {
        result.error = MakeError(
            ZipArchiveIndexErrorCode::MANIFEST_REJECTED,
            eocd_offset);
        result.error.manifest_error = manifest_result.error;
        result.error.entry_index = manifest_result.error.entry_index;
        result.error.conflicting_entry_index =
            manifest_result.error.conflicting_entry_index;
        if (manifest_result.error.entry_index <
            result.index.entries.size())
        {
            result.error.offset =
                result.index.entries[
                    manifest_result.error.entry_index].
                    central_header_offset;
        }
        return result;
    }

    result.index.package_manifest = manifest_result.manifest;
    return result;
}

ZipArchiveIndexResult BuildBeamNGZipArchiveIndex(
    const std::vector<std::uint8_t>& bytes,
    const ZipArchiveScanLimits& zip_limits,
    const PackageScanLimits& package_limits)
{
    return BuildBeamNGZipArchiveIndex(
        bytes.empty() ? NULL : &bytes[0],
        bytes.size(),
        zip_limits,
        package_limits);
}

const char* ZipArchiveIndexErrorCodeToString(
    ZipArchiveIndexErrorCode code)
{
    switch (code)
    {
    case ZipArchiveIndexErrorCode::NONE:
        return "none";
    case ZipArchiveIndexErrorCode::NULL_INPUT:
        return "null-input";
    case ZipArchiveIndexErrorCode::ARCHIVE_TOO_SMALL:
        return "archive-too-small";
    case ZipArchiveIndexErrorCode::ARCHIVE_SIZE_LIMIT:
        return "archive-size-limit";
    case ZipArchiveIndexErrorCode::EOCD_NOT_FOUND:
        return "eocd-not-found";
    case ZipArchiveIndexErrorCode::EOCD_TRUNCATED:
        return "eocd-truncated";
    case ZipArchiveIndexErrorCode::EOCD_COMMENT_LENGTH_MISMATCH:
        return "eocd-comment-length-mismatch";
    case ZipArchiveIndexErrorCode::EOCD_AMBIGUOUS:
        return "eocd-ambiguous";
    case ZipArchiveIndexErrorCode::ARCHIVE_COMMENT_LIMIT:
        return "archive-comment-limit";
    case ZipArchiveIndexErrorCode::ZIP64_UNSUPPORTED:
        return "zip64-unsupported";
    case ZipArchiveIndexErrorCode::MULTI_DISK_UNSUPPORTED:
        return "multi-disk-unsupported";
    case ZipArchiveIndexErrorCode::ENTRY_COUNT_MISMATCH:
        return "entry-count-mismatch";
    case ZipArchiveIndexErrorCode::ENTRY_COUNT_LIMIT:
        return "entry-count-limit";
    case ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_SIZE_LIMIT:
        return "central-directory-size-limit";
    case ZipArchiveIndexErrorCode::CENTRAL_DIRECTORY_BOUNDS:
        return "central-directory-bounds";
    case ZipArchiveIndexErrorCode::
        CENTRAL_DIRECTORY_NOT_ADJACENT_TO_EOCD:
        return "central-directory-not-adjacent-to-eocd";
    case ZipArchiveIndexErrorCode::CENTRAL_HEADER_TRUNCATED:
        return "central-header-truncated";
    case ZipArchiveIndexErrorCode::CENTRAL_HEADER_SIGNATURE:
        return "central-header-signature";
    case ZipArchiveIndexErrorCode::CENTRAL_ENTRY_METADATA_LIMIT:
        return "central-entry-metadata-limit";
    case ZipArchiveIndexErrorCode::CENTRAL_ENTRY_TRUNCATED:
        return "central-entry-truncated";
    case ZipArchiveIndexErrorCode::CENTRAL_EXTRA_FIELD_INVALID:
        return "central-extra-field-invalid";
    case ZipArchiveIndexErrorCode::EXTRA_FIELD_UNSUPPORTED:
        return "extra-field-unsupported";
    case ZipArchiveIndexErrorCode::DUPLICATE_EXTRA_FIELD:
        return "duplicate-extra-field";
    case ZipArchiveIndexErrorCode::ARCHIVE_EXTRA_DATA_UNSUPPORTED:
        return "archive-extra-data-unsupported";
    case ZipArchiveIndexErrorCode::
        CENTRAL_DIRECTORY_DIGITAL_SIGNATURE_UNSUPPORTED:
        return "central-directory-digital-signature-unsupported";
    case ZipArchiveIndexErrorCode::
        CENTRAL_DIRECTORY_ENCRYPTION_UNSUPPORTED:
        return "central-directory-encryption-unsupported";
    case ZipArchiveIndexErrorCode::STRONG_ENCRYPTION_UNSUPPORTED:
        return "strong-encryption-unsupported";
    case ZipArchiveIndexErrorCode::ENCRYPTED_ENTRY_UNSUPPORTED:
        return "encrypted-entry-unsupported";
    case ZipArchiveIndexErrorCode::CERTIFICATE_METADATA_UNSUPPORTED:
        return "certificate-metadata-unsupported";
    case ZipArchiveIndexErrorCode::RECORD_MANAGEMENT_UNSUPPORTED:
        return "record-management-unsupported";
    case ZipArchiveIndexErrorCode::ALTERNATE_FILENAME_UNSUPPORTED:
        return "alternate-filename-unsupported";
    case ZipArchiveIndexErrorCode::
        GENERAL_PURPOSE_FLAGS_UNSUPPORTED:
        return "general-purpose-flags-unsupported";
    case ZipArchiveIndexErrorCode::ENTRY_UTF8_INVALID:
        return "entry-utf8-invalid";
    case ZipArchiveIndexErrorCode::PATCHED_DATA_UNSUPPORTED:
        return "patched-data-unsupported";
    case ZipArchiveIndexErrorCode::ALTERNATE_STREAMS_UNSUPPORTED:
        return "alternate-streams-unsupported";
    case ZipArchiveIndexErrorCode::ENTRY_DISK_UNSUPPORTED:
        return "entry-disk-unsupported";
    case ZipArchiveIndexErrorCode::SYMLINK_REJECTED:
        return "symlink-rejected";
    case ZipArchiveIndexErrorCode::UNSUPPORTED_ENTRY_TYPE:
        return "unsupported-entry-type";
    case ZipArchiveIndexErrorCode::ENTRY_TYPE_MISMATCH:
        return "entry-type-mismatch";
    case ZipArchiveIndexErrorCode::DUPLICATE_LOCAL_HEADER_OFFSET:
        return "duplicate-local-header-offset";
    case ZipArchiveIndexErrorCode::LOCAL_HEADER_BOUNDS:
        return "local-header-bounds";
    case ZipArchiveIndexErrorCode::LOCAL_HEADER_SIGNATURE:
        return "local-header-signature";
    case ZipArchiveIndexErrorCode::LOCAL_HEADER_TRUNCATED:
        return "local-header-truncated";
    case ZipArchiveIndexErrorCode::LOCAL_ENTRY_METADATA_LIMIT:
        return "local-entry-metadata-limit";
    case ZipArchiveIndexErrorCode::LOCAL_EXTRA_FIELD_INVALID:
        return "local-extra-field-invalid";
    case ZipArchiveIndexErrorCode::LOCAL_VERSION_MISMATCH:
        return "local-version-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_FLAGS_MISMATCH:
        return "local-flags-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_METHOD_MISMATCH:
        return "local-method-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_FILENAME_MISMATCH:
        return "local-filename-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_CRC_MISMATCH:
        return "local-crc-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_COMPRESSED_SIZE_MISMATCH:
        return "local-compressed-size-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_UNCOMPRESSED_SIZE_MISMATCH:
        return "local-uncompressed-size-mismatch";
    case ZipArchiveIndexErrorCode::STORED_SIZE_MISMATCH:
        return "stored-size-mismatch";
    case ZipArchiveIndexErrorCode::DATA_CROSSES_CENTRAL_DIRECTORY:
        return "data-crosses-central-directory";
    case ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_TRUNCATED:
        return "data-descriptor-truncated";
    case ZipArchiveIndexErrorCode::DATA_DESCRIPTOR_MISMATCH:
        return "data-descriptor-mismatch";
    case ZipArchiveIndexErrorCode::LOCAL_RANGE_OVERLAP:
        return "local-range-overlap";
    case ZipArchiveIndexErrorCode::
        SELF_EXTRACTING_PREFIX_UNSUPPORTED:
        return "self-extracting-prefix-unsupported";
    case ZipArchiveIndexErrorCode::INTER_RECORD_DATA_UNSUPPORTED:
        return "inter-record-data-unsupported";
    case ZipArchiveIndexErrorCode::MANIFEST_REJECTED:
        return "manifest-rejected";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
