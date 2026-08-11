/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "TerrainBundleArchiveVerifier.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>
#endif

namespace RoR {
namespace {

const std::size_t READ_BUFFER_BYTES = 64U * 1024U;
const std::uint32_t ZIP_LOCAL_HEADER_SIGNATURE = UINT32_C(0x04034b50);
const std::uint32_t ZIP_CENTRAL_HEADER_SIGNATURE = UINT32_C(0x02014b50);
const std::uint32_t ZIP_DATA_DESCRIPTOR_SIGNATURE = UINT32_C(0x08074b50);
const std::uint32_t ZIP_EOCD_SIGNATURE = UINT32_C(0x06054b50);
const std::uint32_t ZIP64_EOCD_SIGNATURE = UINT32_C(0x06064b50);
const std::uint32_t ZIP64_LOCATOR_SIGNATURE = UINT32_C(0x07064b50);
const std::size_t ZIP_CENTRAL_HEADER_BYTES = 46U;
const std::size_t ZIP_LOCAL_HEADER_BYTES = 30U;
const std::size_t ZIP_EOCD_BYTES = 22U;
const std::size_t ZIP64_LOCATOR_BYTES = 20U;
const std::size_t ZIP64_EOCD_MINIMUM_BYTES = 56U;
const std::size_t ZIP_MAXIMUM_COMMENT_BYTES = 65535U;
const std::uint16_t ZIP64_EXTRA_FIELD_IDENTIFIER = UINT16_C(0x0001);
const std::uint16_t ZIP_METHOD_STORED = 0U;
const std::uint16_t ZIP_METHOD_DEFLATED = 8U;
const std::uint16_t ZIP_FLAG_DATA_DESCRIPTOR = UINT16_C(0x0008);
const std::uint16_t ZIP_UNSUPPORTED_FLAGS =
    UINT16_C(0x0001) | UINT16_C(0x0020) | UINT16_C(0x0040) |
    UINT16_C(0x2000);
const std::uint32_t ZIP_DOS_DIRECTORY_ATTRIBUTE = UINT32_C(0x10);
const std::uint64_t MEMBER_SHA256_IDENTITY_BYTES = 64U;

struct ZipLocalSpan final
{
    ZipLocalSpan(std::uint64_t span_begin, std::uint64_t span_end) noexcept:
        begin(span_begin),
        end(span_end)
    {
    }

    std::uint64_t begin;
    std::uint64_t end;
};

struct ZipByteView
{
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0U;

    ZipByteView(const std::uint8_t* value, std::size_t count) noexcept:
        bytes(value),
        size(count)
    {
    }

    bool CanRead(std::size_t offset, std::size_t count) const noexcept
    {
        return offset <= size && count <= size - offset;
    }

    std::uint16_t Read16(std::size_t offset) const noexcept
    {
        return static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U;
    }

    std::uint32_t Read32(std::size_t offset) const noexcept
    {
        return static_cast<std::uint32_t>(bytes[offset]) |
            static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
            static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
            static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U;
    }

    std::uint64_t Read64(std::size_t offset) const noexcept
    {
        return static_cast<std::uint64_t>(Read32(offset)) |
            static_cast<std::uint64_t>(Read32(offset + 4U)) << 32U;
    }
};

bool CheckedAddU64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept
{
    if (left > (std::numeric_limits<std::uint64_t>::max)() - right)
    {
        return false;
    }
    result = left + right;
    return true;
}

bool U64FitsSize(std::uint64_t value) noexcept
{
    return value <= static_cast<std::uint64_t>(
        (std::numeric_limits<std::size_t>::max)());
}

bool IsValidUtf8(const std::uint8_t* value, std::size_t size) noexcept
{
    std::size_t offset = 0U;
    while (offset < size)
    {
        const std::uint8_t first = value[offset];
        if (first <= 0x7fU)
        {
            if (first == 0U)
            {
                return false;
            }
            ++offset;
            continue;
        }
        std::size_t continuation_count = 0U;
        std::uint32_t codepoint = 0U;
        std::uint32_t minimum = 0U;
        if (first >= 0xc2U && first <= 0xdfU)
        {
            continuation_count = 1U;
            codepoint = first & 0x1fU;
            minimum = 0x80U;
        }
        else if (first >= 0xe0U && first <= 0xefU)
        {
            continuation_count = 2U;
            codepoint = first & 0x0fU;
            minimum = 0x800U;
        }
        else if (first >= 0xf0U && first <= 0xf4U)
        {
            continuation_count = 3U;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            return false;
        }
        if (continuation_count > size - offset - 1U)
        {
            return false;
        }
        for (std::size_t index = 0U;
             index < continuation_count;
             ++index)
        {
            const std::uint8_t continuation = value[offset + index + 1U];
            if ((continuation & 0xc0U) != 0x80U)
            {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
        {
            return false;
        }
        offset += continuation_count + 1U;
    }
    return true;
}

bool BuildZipLookupKey(
    const std::uint8_t* name_bytes,
    std::size_t name_size,
    std::string& exact_name,
    std::string& lookup_key,
    bool& directory)
{
    if (name_size == 0U ||
        name_size >
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_NAME_BYTES ||
        !IsValidUtf8(name_bytes, name_size) || name_bytes[0U] == '/' ||
        name_bytes[0U] == '\\')
    {
        return false;
    }
    exact_name.assign(
        reinterpret_cast<const char*>(name_bytes), name_size);
    lookup_key.resize(name_size);
    std::size_t segment_begin = 0U;
    for (std::size_t index = 0U; index < name_size; ++index)
    {
        const unsigned char character =
            static_cast<unsigned char>(exact_name[index]);
        if (character == '\\' || character == 0U)
        {
            return false;
        }
        if (character == '/')
        {
            const std::size_t segment_size = index - segment_begin;
            if (segment_size == 0U ||
                (segment_size == 1U &&
                 exact_name[segment_begin] == '.') ||
                (segment_size == 2U &&
                 exact_name[segment_begin] == '.' &&
                 exact_name[segment_begin + 1U] == '.'))
            {
                return false;
            }
            segment_begin = index + 1U;
        }
        lookup_key[index] = character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : static_cast<char>(character);
    }
    directory = exact_name[name_size - 1U] == '/';
    if (!directory)
    {
        const std::size_t segment_size = name_size - segment_begin;
        if (segment_size == 0U ||
            (segment_size == 1U && exact_name[segment_begin] == '.') ||
            (segment_size == 2U &&
             exact_name[segment_begin] == '.' &&
             exact_name[segment_begin + 1U] == '.'))
        {
            return false;
        }
    }
    return true;
}

bool ReadRequiredZip64Value(
    const ZipByteView& view,
    std::size_t field_end,
    std::size_t& cursor,
    std::uint64_t& value) noexcept
{
    if (cursor > field_end || field_end - cursor < 8U ||
        !view.CanRead(cursor, 8U))
    {
        return false;
    }
    value = view.Read64(cursor);
    cursor += 8U;
    return true;
}

bool ResolveZip64CentralValues(
    const ZipByteView& view,
    std::size_t extra_offset,
    std::size_t extra_size,
    bool needs_uncompressed,
    bool needs_compressed,
    bool needs_local_offset,
    bool needs_disk,
    std::uint64_t& uncompressed,
    std::uint64_t& compressed,
    std::uint64_t& local_offset,
    std::uint64_t& disk)
{
    const bool required = needs_uncompressed || needs_compressed ||
        needs_local_offset || needs_disk;
    bool found = false;
    std::size_t cursor = extra_offset;
    const std::size_t extra_end = extra_offset + extra_size;
    while (cursor < extra_end)
    {
        if (extra_end - cursor < 4U || !view.CanRead(cursor, 4U))
        {
            return false;
        }
        const std::uint16_t identifier = view.Read16(cursor);
        const std::size_t field_size = view.Read16(cursor + 2U);
        cursor += 4U;
        if (field_size > extra_end - cursor ||
            !view.CanRead(cursor, field_size))
        {
            return false;
        }
        const std::size_t field_end = cursor + field_size;
        if (identifier == ZIP64_EXTRA_FIELD_IDENTIFIER)
        {
            if (found)
            {
                return false;
            }
            found = true;
            std::size_t value_cursor = cursor;
            if ((needs_uncompressed &&
                 !ReadRequiredZip64Value(
                     view, field_end, value_cursor, uncompressed)) ||
                (needs_compressed &&
                 !ReadRequiredZip64Value(
                     view, field_end, value_cursor, compressed)) ||
                (needs_local_offset &&
                 !ReadRequiredZip64Value(
                     view, field_end, value_cursor, local_offset)))
            {
                return false;
            }
            if (needs_disk)
            {
                if (value_cursor > field_end ||
                    field_end - value_cursor < 4U ||
                    !view.CanRead(value_cursor, 4U))
                {
                    return false;
                }
                disk = view.Read32(value_cursor);
                value_cursor += 4U;
            }
            if (value_cursor != field_end)
            {
                return false;
            }
        }
        cursor = field_end;
    }
    return cursor == extra_end && (!required || found);
}

bool MatchZipDataDescriptor(
    const ZipByteView& view,
    std::uint64_t descriptor_offset_u64,
    std::uint64_t central_offset,
    bool has_signature,
    bool zip64_sizes,
    std::uint32_t expected_crc32,
    std::uint64_t expected_compressed,
    std::uint64_t expected_uncompressed,
    std::uint64_t& descriptor_end) noexcept
{
    const std::uint64_t descriptor_bytes =
        (has_signature ? 4U : 0U) + 4U +
        (zip64_sizes ? 16U : 8U);
    std::uint64_t end = 0U;
    if (!CheckedAddU64(descriptor_offset_u64, descriptor_bytes, end) ||
        end > central_offset || !U64FitsSize(descriptor_offset_u64) ||
        !U64FitsSize(end))
    {
        return false;
    }
    const std::size_t descriptor_offset =
        static_cast<std::size_t>(descriptor_offset_u64);
    if (!view.CanRead(
            descriptor_offset, static_cast<std::size_t>(descriptor_bytes)))
    {
        return false;
    }
    std::size_t cursor = descriptor_offset;
    if (has_signature)
    {
        if (view.Read32(cursor) != ZIP_DATA_DESCRIPTOR_SIGNATURE)
        {
            return false;
        }
        cursor += 4U;
    }
    if (view.Read32(cursor) != expected_crc32)
    {
        return false;
    }
    cursor += 4U;
    const std::uint64_t compressed = zip64_sizes
        ? view.Read64(cursor)
        : static_cast<std::uint64_t>(view.Read32(cursor));
    cursor += zip64_sizes ? 8U : 4U;
    const std::uint64_t uncompressed = zip64_sizes
        ? view.Read64(cursor)
        : static_cast<std::uint64_t>(view.Read32(cursor));
    if (compressed != expected_compressed ||
        uncompressed != expected_uncompressed)
    {
        return false;
    }
    descriptor_end = end;
    return true;
}

bool ResolveZipDataDescriptor(
    const ZipByteView& view,
    std::uint64_t descriptor_offset,
    std::uint64_t central_offset,
    bool zip64_sizes,
    std::uint32_t expected_crc32,
    std::uint64_t expected_compressed,
    std::uint64_t expected_uncompressed,
    std::uint64_t& descriptor_end) noexcept
{
    return MatchZipDataDescriptor(
               view,
               descriptor_offset,
               central_offset,
               true,
               zip64_sizes,
               expected_crc32,
               expected_compressed,
               expected_uncompressed,
               descriptor_end) ||
        MatchZipDataDescriptor(
               view,
               descriptor_offset,
               central_offset,
               false,
               zip64_sizes,
               expected_crc32,
               expected_compressed,
               expected_uncompressed,
               descriptor_end);
}

bool IsLowercaseSha256(const std::string& value)
{
    if (value.size() != 64U)
    {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

#if defined(_WIN32)
FILE* OpenArchiveReadOnly(const std::string& path)
{
    if (path.empty())
    {
        return nullptr;
    }
    const int wide_length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.c_str(),
        -1,
        nullptr,
        0);
    if (wide_length <= 0)
    {
        return nullptr;
    }
    std::vector<wchar_t> wide_path(
        static_cast<std::size_t>(wide_length));
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path.c_str(),
            -1,
            &wide_path[0],
            wide_length) != wide_length)
    {
        return nullptr;
    }
    FILE* archive = nullptr;
    if (_wfopen_s(&archive, &wide_path[0], L"rb") != 0)
    {
        return nullptr;
    }
    return archive;
}
#else
FILE* OpenArchiveReadOnly(const std::string& path)
{
    return std::fopen(path.c_str(), "rb");
}
#endif

struct FileCloser
{
    void operator()(FILE* file) const
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

struct DigestContextDeleter
{
    void operator()(EVP_MD_CTX* context) const
    {
        EVP_MD_CTX_free(context);
    }
};

std::string LowercaseHex(
    const unsigned char* digest,
    std::size_t digest_size)
{
    static const char HEX_DIGITS[] = "0123456789abcdef";
    std::string result(digest_size * 2U, '0');
    for (std::size_t index = 0U; index < digest_size; ++index)
    {
        result[index * 2U] = HEX_DIGITS[digest[index] >> 4U];
        result[index * 2U + 1U] =
            HEX_DIGITS[digest[index] & 0x0fU];
    }
    return result;
}

} // namespace

struct TerrainBundleAuthenticatedArchiveSnapshot::State
{
    std::uint32_t version =
        TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION;
    std::string source_archive_identity;
    std::string archive_sha256;
    std::vector<std::uint8_t> bytes;
};

TerrainBundleAuthenticatedArchiveSnapshot::
    TerrainBundleAuthenticatedArchiveSnapshot(
        std::shared_ptr<const State> state) noexcept:
    m_state(std::move(state))
{
}

bool TerrainBundleAuthenticatedArchiveSnapshot::initialized() const noexcept
{
    return m_state != nullptr;
}

std::uint32_t TerrainBundleAuthenticatedArchiveSnapshot::version() const noexcept
{
    return m_state != nullptr ? m_state->version : 0U;
}

const std::string&
TerrainBundleAuthenticatedArchiveSnapshot::source_archive_identity() const noexcept
{
    static const std::string EMPTY;
    return m_state != nullptr ? m_state->source_archive_identity : EMPTY;
}

const std::string&
TerrainBundleAuthenticatedArchiveSnapshot::archive_sha256() const noexcept
{
    static const std::string EMPTY;
    return m_state != nullptr ? m_state->archive_sha256 : EMPTY;
}

const std::uint8_t*
TerrainBundleAuthenticatedArchiveSnapshot::bytes() const noexcept
{
    return m_state != nullptr && !m_state->bytes.empty()
        ? m_state->bytes.data()
        : nullptr;
}

std::size_t TerrainBundleAuthenticatedArchiveSnapshot::size() const noexcept
{
    return m_state != nullptr ? m_state->bytes.size() : 0U;
}

bool TerrainBundleAuthenticatedArchiveSnapshot::SharesImmutableStateWith(
    const TerrainBundleAuthenticatedArchiveSnapshot& other) const noexcept
{
    return m_state == other.m_state;
}

bool VerifyTerrainBundleArchiveSha256(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::string& out_observed_sha256,
    std::string& out_error)
{
    out_observed_sha256.clear();
    out_error.clear();
    if (!IsLowercaseSha256(expected_sha256))
    {
        out_error =
            "expected SHA-256 must be 64 lowercase hexadecimal characters";
        return false;
    }

    std::unique_ptr<FILE, FileCloser> archive(
        OpenArchiveReadOnly(archive_path));
    if (!archive)
    {
        out_error = "could not open archive for SHA-256 verification";
        return false;
    }

    std::unique_ptr<EVP_MD_CTX, DigestContextDeleter> context(
        EVP_MD_CTX_new());
    if (!context ||
        EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    {
        out_error = "could not initialize SHA-256 verification";
        return false;
    }

    std::array<unsigned char, READ_BUFFER_BYTES> buffer;
    for (;;)
    {
        const std::size_t bytes_read = std::fread(
            buffer.data(),
            1U,
            buffer.size(),
            archive.get());
        if (bytes_read != 0U &&
            EVP_DigestUpdate(
                context.get(),
                buffer.data(),
                bytes_read) != 1)
        {
            out_error = "could not update SHA-256 verification";
            return false;
        }
        if (bytes_read != buffer.size())
        {
            if (std::ferror(archive.get()) != 0)
            {
                out_error = "could not read archive for SHA-256 verification";
                return false;
            }
            break;
        }
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_DigestFinal_ex(
            context.get(),
            digest.data(),
            &digest_size) != 1 ||
        digest_size != 32U)
    {
        out_error = "could not finalize SHA-256 verification";
        return false;
    }

    out_observed_sha256 = LowercaseHex(digest.data(), digest_size);
    if (out_observed_sha256 != expected_sha256)
    {
        out_error = "archive SHA-256 mismatch";
        return false;
    }
    return true;
}

bool LoadAndVerifyTerrainBundleArchiveSnapshot(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::uint64_t maximum_archive_bytes,
    TerrainBundleAuthenticatedArchiveSnapshot& out_snapshot,
    std::string& out_observed_sha256,
    std::string& out_error)
{
    out_observed_sha256.clear();
    out_error.clear();
    if (!IsLowercaseSha256(expected_sha256))
    {
        out_error =
            "expected SHA-256 must be 64 lowercase hexadecimal characters";
        return false;
    }
    if (archive_path.empty() || archive_path.size() > 16384U)
    {
        out_error = "archive identity is empty or exceeds its hard cap";
        return false;
    }
    if (maximum_archive_bytes == 0U ||
        maximum_archive_bytes >
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES ||
        maximum_archive_bytes >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)()))
    {
        out_error = "archive byte limit is zero or exceeds its hard cap";
        return false;
    }

    try
    {
        std::unique_ptr<FILE, FileCloser> archive(
            OpenArchiveReadOnly(archive_path));
        if (!archive)
        {
            out_error = "could not open archive for authenticated snapshot";
            return false;
        }

        std::unique_ptr<EVP_MD_CTX, DigestContextDeleter> context(
            EVP_MD_CTX_new());
        if (!context ||
            EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        {
            out_error = "could not initialize authenticated snapshot SHA-256";
            return false;
        }

        std::shared_ptr<TerrainBundleAuthenticatedArchiveSnapshot::State>
            candidate = std::make_shared<
                TerrainBundleAuthenticatedArchiveSnapshot::State>();
        candidate->source_archive_identity = archive_path;
        candidate->archive_sha256 = expected_sha256;
        std::array<unsigned char, READ_BUFFER_BYTES> buffer;
        for (;;)
        {
            const std::size_t bytes_read = std::fread(
                buffer.data(),
                1U,
                buffer.size(),
                archive.get());
            if (bytes_read != 0U)
            {
                if (bytes_read >
                    static_cast<std::size_t>(maximum_archive_bytes) -
                        candidate->bytes.size())
                {
                    out_error =
                        "archive exceeds authenticated snapshot byte cap";
                    return false;
                }
                if (EVP_DigestUpdate(
                        context.get(),
                        buffer.data(),
                        bytes_read) != 1)
                {
                    out_error =
                        "could not update authenticated snapshot SHA-256";
                    return false;
                }
                candidate->bytes.insert(
                    candidate->bytes.end(),
                    buffer.begin(),
                    buffer.begin() +
                        static_cast<std::ptrdiff_t>(bytes_read));
            }
            if (bytes_read != buffer.size())
            {
                if (std::ferror(archive.get()) != 0)
                {
                    out_error =
                        "could not read archive for authenticated snapshot";
                    return false;
                }
                break;
            }
        }
        if (candidate->bytes.empty())
        {
            out_error = "authenticated archive snapshot is empty";
            return false;
        }

        std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
        unsigned int digest_size = 0U;
        if (EVP_DigestFinal_ex(
                context.get(),
                digest.data(),
                &digest_size) != 1 ||
            digest_size != 32U)
        {
            out_error =
                "could not finalize authenticated snapshot SHA-256";
            return false;
        }
        out_observed_sha256 = LowercaseHex(digest.data(), digest_size);
        if (out_observed_sha256 != expected_sha256)
        {
            out_error = "archive SHA-256 mismatch";
            return false;
        }

        out_snapshot = TerrainBundleAuthenticatedArchiveSnapshot(
            std::shared_ptr<const
                TerrainBundleAuthenticatedArchiveSnapshot::State>(
                    std::move(candidate)));
        return true;
    }
    catch (const std::bad_alloc&)
    {
        out_error = "allocation failed before authenticated snapshot commit";
        return false;
    }
    catch (const std::length_error&)
    {
        out_error = "authenticated snapshot allocation exceeded size limits";
        return false;
    }
    catch (...)
    {
        out_error = "unexpected failure before authenticated snapshot commit";
        return false;
    }
}

bool BuildTerrainBundleAuthenticatedArchivePreflight(
    const TerrainBundleAuthenticatedArchiveSnapshot& archive_snapshot,
    TerrainBundleAuthenticatedArchivePreflight& out_preflight,
    std::string& out_error)
{
    out_error.clear();
    if (!archive_snapshot.initialized() ||
        archive_snapshot.version() !=
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION ||
        archive_snapshot.bytes() == nullptr ||
        archive_snapshot.size() < ZIP_EOCD_BYTES ||
        static_cast<std::uint64_t>(archive_snapshot.size()) >
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES)
    {
        out_error = "authenticated ZIP preflight requires one valid snapshot";
        return false;
    }

    try
    {
        const ZipByteView view = {
            archive_snapshot.bytes(), archive_snapshot.size()};
        const std::size_t search_span =
            ZIP_EOCD_BYTES + ZIP_MAXIMUM_COMMENT_BYTES;
        const std::size_t search_begin =
            view.size > search_span ? view.size - search_span : 0U;
        std::size_t eocd_offset = 0U;
        std::size_t eocd_count = 0U;
        for (std::size_t offset = search_begin;
             offset <= view.size - ZIP_EOCD_BYTES;
             ++offset)
        {
            if (view.Read32(offset) != ZIP_EOCD_SIGNATURE)
            {
                continue;
            }
            const std::size_t comment_size = view.Read16(offset + 20U);
            if (comment_size <= view.size - offset - ZIP_EOCD_BYTES &&
                offset + ZIP_EOCD_BYTES + comment_size == view.size)
            {
                eocd_offset = offset;
                ++eocd_count;
            }
        }
        if (eocd_count != 1U)
        {
            out_error = "ZIP end-of-central-directory record is missing or ambiguous";
            return false;
        }

        const std::uint16_t classic_disk = view.Read16(eocd_offset + 4U);
        const std::uint16_t classic_central_disk =
            view.Read16(eocd_offset + 6U);
        const std::uint16_t classic_entries_on_disk =
            view.Read16(eocd_offset + 8U);
        const std::uint16_t classic_entry_count =
            view.Read16(eocd_offset + 10U);
        const std::uint32_t classic_central_size =
            view.Read32(eocd_offset + 12U);
        const std::uint32_t classic_central_offset =
            view.Read32(eocd_offset + 16U);
        const bool has_classic_sentinel =
            classic_disk == UINT16_MAX ||
            classic_central_disk == UINT16_MAX ||
            classic_entries_on_disk == UINT16_MAX ||
            classic_entry_count == UINT16_MAX ||
            classic_central_size == UINT32_MAX ||
            classic_central_offset == UINT32_MAX;
        const bool has_zip64_locator =
            eocd_offset >= ZIP64_LOCATOR_BYTES &&
            view.Read32(eocd_offset - ZIP64_LOCATOR_BYTES) ==
                ZIP64_LOCATOR_SIGNATURE;
        if (has_classic_sentinel != has_zip64_locator)
        {
            out_error = "ZIP64 sentinel and locator are inconsistent";
            return false;
        }

        std::uint64_t entry_count = classic_entry_count;
        std::uint64_t entries_on_disk = classic_entries_on_disk;
        std::uint64_t central_size = classic_central_size;
        std::uint64_t central_offset = classic_central_offset;
        std::uint64_t central_end_limit = eocd_offset;
        if (has_zip64_locator)
        {
            const std::size_t locator_offset =
                eocd_offset - ZIP64_LOCATOR_BYTES;
            if (view.Read32(locator_offset + 4U) != 0U ||
                view.Read32(locator_offset + 16U) != 1U)
            {
                out_error = "multi-disk ZIP64 archives are unsupported";
                return false;
            }
            const std::uint64_t zip64_offset_u64 =
                view.Read64(locator_offset + 8U);
            if (!U64FitsSize(zip64_offset_u64))
            {
                out_error = "ZIP64 end record offset exceeds platform bounds";
                return false;
            }
            const std::size_t zip64_offset =
                static_cast<std::size_t>(zip64_offset_u64);
            if (!view.CanRead(zip64_offset, ZIP64_EOCD_MINIMUM_BYTES) ||
                view.Read32(zip64_offset) != ZIP64_EOCD_SIGNATURE)
            {
                out_error = "ZIP64 end record is missing or truncated";
                return false;
            }
            const std::uint64_t zip64_payload_size =
                view.Read64(zip64_offset + 4U);
            std::uint64_t zip64_record_size = 0U;
            std::uint64_t zip64_record_end = 0U;
            if (zip64_payload_size < 44U ||
                !CheckedAddU64(12U, zip64_payload_size, zip64_record_size) ||
                zip64_offset >= locator_offset ||
                !CheckedAddU64(
                    zip64_offset_u64,
                    zip64_record_size,
                    zip64_record_end) ||
                zip64_record_end != locator_offset)
            {
                out_error = "ZIP64 end record size is invalid";
                return false;
            }
            const std::uint32_t zip64_disk = view.Read32(zip64_offset + 16U);
            const std::uint32_t zip64_central_disk =
                view.Read32(zip64_offset + 20U);
            entries_on_disk = view.Read64(zip64_offset + 24U);
            entry_count = view.Read64(zip64_offset + 32U);
            central_size = view.Read64(zip64_offset + 40U);
            central_offset = view.Read64(zip64_offset + 48U);
            if (zip64_disk != 0U || zip64_central_disk != 0U ||
                (classic_disk != UINT16_MAX && classic_disk != zip64_disk) ||
                (classic_central_disk != UINT16_MAX &&
                 classic_central_disk != zip64_central_disk) ||
                (classic_entries_on_disk != UINT16_MAX &&
                 classic_entries_on_disk != entries_on_disk) ||
                (classic_entry_count != UINT16_MAX &&
                 classic_entry_count != entry_count) ||
                (classic_central_size != UINT32_MAX &&
                 classic_central_size != central_size) ||
                (classic_central_offset != UINT32_MAX &&
                 classic_central_offset != central_offset))
            {
                out_error = "ZIP64 end record disagrees with its classic envelope";
                return false;
            }
            central_end_limit = zip64_offset_u64;
        }
        else if (classic_disk != 0U || classic_central_disk != 0U)
        {
            out_error = "multi-disk ZIP archives are unsupported";
            return false;
        }

        if (entry_count == 0U || entries_on_disk != entry_count ||
            entry_count >
                TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_ENTRIES)
        {
            out_error = "ZIP member count is zero, inconsistent, or exceeds its hard cap";
            return false;
        }
        std::uint64_t central_end_u64 = 0U;
        if (!CheckedAddU64(
                central_offset, central_size, central_end_u64) ||
            !U64FitsSize(central_offset) || !U64FitsSize(central_end_u64) ||
            central_end_u64 != central_end_limit)
        {
            out_error = "ZIP central-directory bounds are invalid";
            return false;
        }
        const std::size_t central_begin =
            static_cast<std::size_t>(central_offset);
        const std::size_t central_end =
            static_cast<std::size_t>(central_end_u64);
        if (!view.CanRead(central_begin, central_end - central_begin))
        {
            out_error = "ZIP central directory exceeds the immutable snapshot";
            return false;
        }

        TerrainBundleAuthenticatedArchivePreflight candidate;
        candidate.members.reserve(static_cast<std::size_t>(entry_count));
        std::vector<ZipLocalSpan> local_spans;
        local_spans.reserve(static_cast<std::size_t>(entry_count));
        std::set<std::string> lookup_keys;
        std::size_t cursor = central_begin;
        std::size_t file_count = 0U;
        for (std::uint64_t entry_index = 0U;
             entry_index < entry_count;
             ++entry_index)
        {
            if (!view.CanRead(cursor, ZIP_CENTRAL_HEADER_BYTES) ||
                cursor > central_end ||
                ZIP_CENTRAL_HEADER_BYTES > central_end - cursor ||
                view.Read32(cursor) != ZIP_CENTRAL_HEADER_SIGNATURE)
            {
                out_error = "ZIP central member header is missing or truncated";
                return false;
            }
            const std::uint16_t flags = view.Read16(cursor + 8U);
            const std::uint16_t method = view.Read16(cursor + 10U);
            const std::uint32_t crc32 = view.Read32(cursor + 16U);
            const std::uint32_t compressed32 = view.Read32(cursor + 20U);
            const std::uint32_t uncompressed32 = view.Read32(cursor + 24U);
            const std::size_t name_size = view.Read16(cursor + 28U);
            const std::size_t extra_size = view.Read16(cursor + 30U);
            const std::size_t comment_size = view.Read16(cursor + 32U);
            const std::uint16_t disk16 = view.Read16(cursor + 34U);
            const std::uint32_t external_attributes =
                view.Read32(cursor + 38U);
            const std::uint32_t local_offset32 = view.Read32(cursor + 42U);
            std::uint64_t record_size = ZIP_CENTRAL_HEADER_BYTES;
            if (!CheckedAddU64(record_size, name_size, record_size) ||
                !CheckedAddU64(record_size, extra_size, record_size) ||
                !CheckedAddU64(record_size, comment_size, record_size) ||
                record_size > central_end - cursor)
            {
                out_error = "ZIP central member metadata exceeds its directory";
                return false;
            }
            const std::size_t name_offset = cursor + ZIP_CENTRAL_HEADER_BYTES;
            const std::size_t extra_offset = name_offset + name_size;
            std::uint64_t compressed = compressed32;
            std::uint64_t uncompressed = uncompressed32;
            std::uint64_t local_offset = local_offset32;
            std::uint64_t disk = disk16;
            if (!ResolveZip64CentralValues(
                    view,
                    extra_offset,
                    extra_size,
                    uncompressed32 == UINT32_MAX,
                    compressed32 == UINT32_MAX,
                    local_offset32 == UINT32_MAX,
                    disk16 == UINT16_MAX,
                    uncompressed,
                    compressed,
                    local_offset,
                    disk) ||
                disk != 0U || local_offset >= central_offset)
            {
                out_error = "ZIP64 member metadata is malformed or unsupported";
                return false;
            }
            if ((method != ZIP_METHOD_STORED &&
                 method != ZIP_METHOD_DEFLATED) ||
                (flags & ZIP_UNSUPPORTED_FLAGS) != 0U ||
                (method == ZIP_METHOD_STORED &&
                 compressed != uncompressed) ||
                (compressed == 0U &&
                 (method != ZIP_METHOD_STORED || uncompressed != 0U ||
                  crc32 != 0U)))
            {
                out_error = "ZIP member compression method or flags are unsupported";
                return false;
            }

            std::uint64_t prospective_identity = 0U;
            std::uint64_t name_and_digest = 0U;
            std::uint64_t metadata_identity = 0U;
            std::uint64_t prospective_uncompressed = 0U;
            if (!CheckedAddU64(
                    static_cast<std::uint64_t>(name_size),
                    MEMBER_SHA256_IDENTITY_BYTES,
                    name_and_digest) ||
                !CheckedAddU64(
                    name_and_digest,
                    static_cast<std::uint64_t>(extra_size),
                    metadata_identity) ||
                !CheckedAddU64(
                    metadata_identity,
                    static_cast<std::uint64_t>(comment_size),
                    metadata_identity) ||
                !CheckedAddU64(
                    candidate.retained_member_identity_bytes,
                    metadata_identity,
                    prospective_identity) ||
                prospective_identity >
                    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_IDENTITY_BYTES ||
                uncompressed >
                    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_BYTES ||
                !CheckedAddU64(
                    candidate.total_uncompressed_bytes,
                    uncompressed,
                    prospective_uncompressed) ||
                prospective_uncompressed >
                    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_TOTAL_MEMBER_BYTES)
            {
                out_error = "ZIP member identity or decoded-size cap is exceeded";
                return false;
            }

            TerrainBundleAuthenticatedArchiveMemberPreflight member;
            std::string lookup_key;
            if (!BuildZipLookupKey(
                    view.bytes + name_offset,
                    name_size,
                    member.exact_member_name,
                    lookup_key,
                    member.directory) ||
                !lookup_keys.insert(std::move(lookup_key)).second)
            {
                out_error = "ZIP member identity is malformed or lookup-ambiguous";
                return false;
            }
            const bool dos_directory =
                (external_attributes & ZIP_DOS_DIRECTORY_ATTRIBUTE) != 0U;
            if ((dos_directory && !member.directory) ||
                (member.directory &&
                 (method != ZIP_METHOD_STORED || compressed != 0U ||
                  uncompressed != 0U || crc32 != 0U ||
                  (flags & ZIP_FLAG_DATA_DESCRIPTOR) != 0U)))
            {
                out_error = "ZIP directory metadata disagrees with its exact name";
                return false;
            }

            if (!U64FitsSize(local_offset) ||
                !view.CanRead(
                    static_cast<std::size_t>(local_offset),
                    ZIP_LOCAL_HEADER_BYTES) ||
                view.Read32(static_cast<std::size_t>(local_offset)) !=
                    ZIP_LOCAL_HEADER_SIGNATURE)
            {
                out_error = "ZIP member local header is missing or truncated";
                return false;
            }
            const std::size_t local_header_offset =
                static_cast<std::size_t>(local_offset);
            const std::uint16_t local_flags =
                view.Read16(local_header_offset + 6U);
            const std::uint16_t local_method =
                view.Read16(local_header_offset + 8U);
            const std::uint32_t local_crc32 =
                view.Read32(local_header_offset + 14U);
            const std::uint32_t local_compressed32 =
                view.Read32(local_header_offset + 18U);
            const std::uint32_t local_uncompressed32 =
                view.Read32(local_header_offset + 22U);
            const std::size_t local_name_size =
                view.Read16(local_header_offset + 26U);
            const std::size_t local_extra_size =
                view.Read16(local_header_offset + 28U);
            std::uint64_t local_name_offset_u64 = 0U;
            std::uint64_t local_extra_offset_u64 = 0U;
            std::uint64_t local_data_offset = 0U;
            if (local_flags != flags || local_method != method ||
                local_name_size != name_size ||
                !CheckedAddU64(
                    local_offset,
                    ZIP_LOCAL_HEADER_BYTES,
                    local_name_offset_u64) ||
                !CheckedAddU64(
                    local_name_offset_u64,
                    local_name_size,
                    local_extra_offset_u64) ||
                !CheckedAddU64(
                    local_extra_offset_u64,
                    local_extra_size,
                    local_data_offset) ||
                local_data_offset > central_offset ||
                !U64FitsSize(local_name_offset_u64) ||
                !U64FitsSize(local_extra_offset_u64) ||
                !U64FitsSize(local_data_offset) ||
                !view.CanRead(
                    static_cast<std::size_t>(local_name_offset_u64),
                    local_name_size) ||
                std::memcmp(
                    view.bytes +
                        static_cast<std::size_t>(local_name_offset_u64),
                    view.bytes + name_offset,
                    name_size) != 0)
            {
                out_error = "ZIP local header disagrees with its central member";
                return false;
            }

            std::uint64_t local_compressed = local_compressed32;
            std::uint64_t local_uncompressed = local_uncompressed32;
            std::uint64_t unused_local_offset = 0U;
            std::uint64_t unused_disk = 0U;
            if (!ResolveZip64CentralValues(
                    view,
                    static_cast<std::size_t>(local_extra_offset_u64),
                    local_extra_size,
                    local_uncompressed32 == UINT32_MAX,
                    local_compressed32 == UINT32_MAX,
                    false,
                    false,
                    local_uncompressed,
                    local_compressed,
                    unused_local_offset,
                    unused_disk))
            {
                out_error = "ZIP local ZIP64 metadata is malformed or unsupported";
                return false;
            }

            const bool has_data_descriptor =
                (flags & ZIP_FLAG_DATA_DESCRIPTOR) != 0U;
            const bool local_placeholders =
                local_crc32 == 0U && local_compressed == 0U &&
                local_uncompressed == 0U;
            const bool local_values_match =
                local_crc32 == crc32 && local_compressed == compressed &&
                local_uncompressed == uncompressed;
            if ((!has_data_descriptor && !local_values_match) ||
                (has_data_descriptor &&
                 !local_placeholders && !local_values_match))
            {
                out_error = "ZIP local checksum or sizes disagree with the central member";
                return false;
            }

            std::uint64_t local_data_end = 0U;
            if (!CheckedAddU64(
                    local_data_offset, compressed, local_data_end) ||
                local_data_end > central_offset)
            {
                out_error = "ZIP member compressed-data span exceeds its local region";
                return false;
            }
            std::uint64_t local_span_end = local_data_end;
            if (has_data_descriptor &&
                !ResolveZipDataDescriptor(
                    view,
                    local_data_end,
                    central_offset,
                    compressed32 == UINT32_MAX ||
                        uncompressed32 == UINT32_MAX ||
                        local_offset32 == UINT32_MAX ||
                        disk16 == UINT16_MAX ||
                        local_compressed32 == UINT32_MAX ||
                        local_uncompressed32 == UINT32_MAX,
                    crc32,
                    compressed,
                    uncompressed,
                    local_span_end))
            {
                out_error = "ZIP data descriptor is missing or disagrees with its member";
                return false;
            }
            local_spans.push_back({local_offset, local_span_end});
            member.compressed_size = compressed;
            member.uncompressed_size = uncompressed;
            member.crc32 = crc32;
            if (!member.directory)
            {
                ++file_count;
            }
            candidate.members.push_back(std::move(member));
            candidate.retained_member_identity_bytes = prospective_identity;
            candidate.total_uncompressed_bytes = prospective_uncompressed;
            cursor += static_cast<std::size_t>(record_size);
        }
        std::sort(
            local_spans.begin(),
            local_spans.end(),
            [](const ZipLocalSpan& left, const ZipLocalSpan& right) {
                return left.begin < right.begin ||
                    (left.begin == right.begin && left.end < right.end);
            });
        for (std::size_t index = 1U; index < local_spans.size(); ++index)
        {
            if (local_spans[index - 1U].end > local_spans[index].begin)
            {
                out_error = "ZIP local member spans overlap";
                return false;
            }
        }
        if (cursor != central_end || file_count == 0U)
        {
            out_error = "ZIP central directory has trailing data or no file members";
            return false;
        }
        out_preflight = std::move(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        out_error = "allocation failed during authenticated ZIP preflight";
        return false;
    }
    catch (const std::length_error&)
    {
        out_error = "authenticated ZIP preflight exceeded container limits";
        return false;
    }
    catch (...)
    {
        out_error = "unexpected failure during authenticated ZIP preflight";
        return false;
    }
}

} // namespace RoR
