#include "TerrainBundleArchiveVerifier.h"

#include <openssl/evp.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__                           \
                      << ": check failed: " #condition << "\n";                \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void Append16(std::vector<std::uint8_t>& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    Append16(output, static_cast<std::uint16_t>(value & 0xffffU));
    Append16(output, static_cast<std::uint16_t>(value >> 16U));
}

void Append64(std::vector<std::uint8_t>& output, std::uint64_t value)
{
    Append32(output, static_cast<std::uint32_t>(value & UINT32_MAX));
    Append32(output, static_cast<std::uint32_t>(value >> 32U));
}

void Patch16(
    std::vector<std::uint8_t>& output,
    std::size_t offset,
    std::uint16_t value)
{
    output[offset] = static_cast<std::uint8_t>(value & 0xffU);
    output[offset + 1U] =
        static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void Patch32(
    std::vector<std::uint8_t>& output,
    std::size_t offset,
    std::uint32_t value)
{
    Patch16(output, offset, static_cast<std::uint16_t>(value & 0xffffU));
    Patch16(output, offset + 2U, static_cast<std::uint16_t>(value >> 16U));
}

void Patch64(
    std::vector<std::uint8_t>& output,
    std::size_t offset,
    std::uint64_t value)
{
    Patch32(output, offset, static_cast<std::uint32_t>(value & UINT32_MAX));
    Patch32(output, offset + 4U, static_cast<std::uint32_t>(value >> 32U));
}

std::uint32_t Read32(
    const std::vector<std::uint8_t>& input,
    std::size_t offset)
{
    CHECK(offset <= input.size() && input.size() - offset >= 4U);
    if (offset > input.size() || input.size() - offset < 4U)
    {
        return 0U;
    }
    return static_cast<std::uint32_t>(input[offset]) |
        static_cast<std::uint32_t>(input[offset + 1U]) << 8U |
        static_cast<std::uint32_t>(input[offset + 2U]) << 16U |
        static_cast<std::uint32_t>(input[offset + 3U]) << 24U;
}

std::size_t ClassicCentralOffset(const std::vector<std::uint8_t>& input)
{
    CHECK(input.size() >= 22U);
    if (input.size() < 22U)
    {
        return 0U;
    }
    const std::size_t eocd_offset = input.size() - 22U;
    CHECK(Read32(input, eocd_offset) == UINT32_C(0x06054b50));
    return static_cast<std::size_t>(Read32(input, eocd_offset + 16U));
}

std::uint32_t Crc32(const std::string& value)
{
    std::uint32_t crc = UINT32_MAX;
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        crc ^= static_cast<std::uint8_t>(value[index]);
        for (unsigned int bit = 0U; bit < 8U; ++bit)
        {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

std::vector<std::uint8_t> MakeStoredZip(
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    struct CentralRecord
    {
        std::string name;
        std::uint32_t crc = 0U;
        std::uint32_t size = 0U;
        std::uint32_t local_offset = 0U;
    };
    std::vector<std::uint8_t> output;
    std::vector<CentralRecord> central;
    for (std::size_t index = 0U; index < entries.size(); ++index)
    {
        const std::pair<std::string, std::string>& entry = entries[index];
        CentralRecord record;
        record.name = entry.first;
        record.crc = Crc32(entry.second);
        record.size = static_cast<std::uint32_t>(entry.second.size());
        record.local_offset = static_cast<std::uint32_t>(output.size());
        Append32(output, UINT32_C(0x04034b50));
        Append16(output, 20U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, record.crc);
        Append32(output, record.size);
        Append32(output, record.size);
        Append16(output, static_cast<std::uint16_t>(record.name.size()));
        Append16(output, 0U);
        output.insert(output.end(), record.name.begin(), record.name.end());
        output.insert(output.end(), entry.second.begin(), entry.second.end());
        central.push_back(record);
    }
    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(output.size());
    for (std::size_t index = 0U; index < central.size(); ++index)
    {
        const CentralRecord& record = central[index];
        Append32(output, UINT32_C(0x02014b50));
        Append16(output, 20U);
        Append16(output, 20U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, record.crc);
        Append32(output, record.size);
        Append32(output, record.size);
        Append16(output, static_cast<std::uint16_t>(record.name.size()));
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append16(output, 0U);
        Append32(output, 0U);
        Append32(output, record.local_offset);
        output.insert(output.end(), record.name.begin(), record.name.end());
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(output.size() - central_offset);
    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, static_cast<std::uint16_t>(central.size()));
    Append16(output, static_cast<std::uint16_t>(central.size()));
    Append32(output, central_size);
    Append32(output, central_offset);
    Append16(output, 0U);
    return output;
}

std::vector<std::uint8_t> MakeStoredZipWithDescriptor(
    const std::string& name,
    const std::string& payload)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t crc = Crc32(payload);
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
    Append32(output, UINT32_C(0x04034b50));
    Append16(output, 20U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 0U);
    output.insert(output.end(), name.begin(), name.end());
    output.insert(output.end(), payload.begin(), payload.end());
    Append32(output, UINT32_C(0x08074b50));
    Append32(output, crc);
    Append32(output, size);
    Append32(output, size);

    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(output.size());
    Append32(output, UINT32_C(0x02014b50));
    Append16(output, 20U);
    Append16(output, 20U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, crc);
    Append32(output, size);
    Append32(output, size);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    output.insert(output.end(), name.begin(), name.end());
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(output.size() - central_offset);
    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 1U);
    Append16(output, 1U);
    Append32(output, central_size);
    Append32(output, central_offset);
    Append16(output, 0U);
    return output;
}

std::vector<std::uint8_t> MakeStoredZip64(
    const std::string& name,
    const std::string& payload)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t crc = Crc32(payload);
    const std::uint64_t size = payload.size();
    Append32(output, UINT32_C(0x04034b50));
    Append16(output, 45U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, crc);
    Append32(output, UINT32_MAX);
    Append32(output, UINT32_MAX);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 20U);
    output.insert(output.end(), name.begin(), name.end());
    Append16(output, UINT16_C(0x0001));
    Append16(output, 16U);
    Append64(output, size);
    Append64(output, size);
    output.insert(output.end(), payload.begin(), payload.end());

    const std::uint64_t central_offset = output.size();
    Append32(output, UINT32_C(0x02014b50));
    Append16(output, 45U);
    Append16(output, 45U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, crc);
    Append32(output, UINT32_MAX);
    Append32(output, UINT32_MAX);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 28U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, UINT32_MAX);
    output.insert(output.end(), name.begin(), name.end());
    Append16(output, UINT16_C(0x0001));
    Append16(output, 24U);
    Append64(output, size);
    Append64(output, size);
    Append64(output, 0U);
    const std::uint64_t central_size = output.size() - central_offset;

    const std::uint64_t zip64_eocd_offset = output.size();
    Append32(output, UINT32_C(0x06064b50));
    Append64(output, 44U);
    Append16(output, 45U);
    Append16(output, 45U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append64(output, 1U);
    Append64(output, 1U);
    Append64(output, central_size);
    Append64(output, central_offset);
    Append32(output, UINT32_C(0x07064b50));
    Append32(output, 0U);
    Append64(output, zip64_eocd_offset);
    Append32(output, 1U);
    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, UINT16_MAX);
    Append16(output, UINT16_MAX);
    Append32(output, UINT32_MAX);
    Append32(output, UINT32_MAX);
    Append16(output, 0U);
    return output;
}

std::vector<std::uint8_t> MakeStoredZip64WithDescriptor(
    const std::string& name,
    const std::string& payload)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t crc = Crc32(payload);
    const std::uint64_t size = payload.size();
    Append32(output, UINT32_C(0x04034b50));
    Append16(output, 45U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, UINT32_MAX);
    Append32(output, UINT32_MAX);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 20U);
    output.insert(output.end(), name.begin(), name.end());
    Append16(output, UINT16_C(0x0001));
    Append16(output, 16U);
    Append64(output, 0U);
    Append64(output, 0U);
    output.insert(output.end(), payload.begin(), payload.end());
    Append32(output, UINT32_C(0x08074b50));
    Append32(output, crc);
    Append64(output, size);
    Append64(output, size);

    const std::uint64_t central_offset = output.size();
    Append32(output, UINT32_C(0x02014b50));
    Append16(output, 45U);
    Append16(output, 45U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, crc);
    Append32(output, static_cast<std::uint32_t>(size));
    Append32(output, static_cast<std::uint32_t>(size));
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    output.insert(output.end(), name.begin(), name.end());
    const std::uint64_t central_size = output.size() - central_offset;

    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 1U);
    Append16(output, 1U);
    Append32(output, static_cast<std::uint32_t>(central_size));
    Append32(output, static_cast<std::uint32_t>(central_offset));
    Append16(output, 0U);
    return output;
}

std::vector<std::uint8_t> MakeStoredZip64OffsetWithDescriptor(
    const std::string& name,
    const std::string& payload)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t crc = Crc32(payload);
    const std::uint64_t size = payload.size();
    Append32(output, UINT32_C(0x04034b50));
    Append16(output, 45U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 0U);
    output.insert(output.end(), name.begin(), name.end());
    output.insert(output.end(), payload.begin(), payload.end());
    Append32(output, UINT32_C(0x08074b50));
    Append32(output, crc);
    Append64(output, size);
    Append64(output, size);

    const std::uint64_t central_offset = output.size();
    Append32(output, UINT32_C(0x02014b50));
    Append16(output, 45U);
    Append16(output, 45U);
    Append16(output, UINT16_C(0x0008));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, crc);
    Append32(output, static_cast<std::uint32_t>(size));
    Append32(output, static_cast<std::uint32_t>(size));
    Append16(output, static_cast<std::uint16_t>(name.size()));
    Append16(output, 12U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 0U);
    Append32(output, 0U);
    Append32(output, UINT32_MAX);
    output.insert(output.end(), name.begin(), name.end());
    Append16(output, UINT16_C(0x0001));
    Append16(output, 8U);
    Append64(output, 0U);
    const std::uint64_t central_size = output.size() - central_offset;

    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, 1U);
    Append16(output, 1U);
    Append32(output, static_cast<std::uint32_t>(central_size));
    Append32(output, static_cast<std::uint32_t>(central_offset));
    Append16(output, 0U);
    return output;
}

std::vector<std::uint8_t> MakeZip64CountEnvelope(
    std::uint64_t entry_count,
    std::uint64_t zip64_payload_size = 44U)
{
    std::vector<std::uint8_t> output;
    Append32(output, UINT32_C(0x06064b50));
    Append64(output, zip64_payload_size);
    Append16(output, 45U);
    Append16(output, 45U);
    Append32(output, 0U);
    Append32(output, 0U);
    Append64(output, entry_count);
    Append64(output, entry_count);
    Append64(output, 0U);
    Append64(output, 0U);
    Append32(output, UINT32_C(0x07064b50));
    Append32(output, 0U);
    Append64(output, 0U);
    Append32(output, 1U);
    Append32(output, UINT32_C(0x06054b50));
    Append16(output, 0U);
    Append16(output, 0U);
    Append16(output, UINT16_MAX);
    Append16(output, UINT16_MAX);
    Append32(output, 0U);
    Append32(output, 0U);
    Append16(output, 0U);
    return output;
}

std::string Sha256(const std::vector<std::uint8_t>& bytes)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    CHECK(EVP_Digest(
        bytes.data(), bytes.size(), digest.data(), &digest_size,
        EVP_sha256(), nullptr) == 1);
    CHECK(digest_size == 32U);
    static const char HEX[] = "0123456789abcdef";
    std::string output(64U, '0');
    for (std::size_t index = 0U; index < 32U; ++index)
    {
        output[index * 2U] = HEX[digest[index] >> 4U];
        output[index * 2U + 1U] = HEX[digest[index] & 0x0fU];
    }
    return output;
}

struct TemporaryFile
{
    explicit TemporaryFile(const std::string& value)
    {
        std::ostringstream name;
        name << "ror-terrain-bundle-verifier-" << this << ".tmp";
        path = name.str();
        std::ofstream stream(path.c_str(), std::ios::out | std::ios::binary);
        stream.write(
            value.data(),
            static_cast<std::streamsize>(value.size()));
        stream.close();
        valid = stream.good();
    }

    ~TemporaryFile()
    {
        std::remove(path.c_str());
    }

    std::string path;
    bool valid;
};

RoR::TerrainBundleAuthenticatedArchiveSnapshot MakeSnapshot(
    const std::vector<std::uint8_t>& bytes)
{
    const std::string payload(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    TemporaryFile archive(payload);
    CHECK(archive.valid);
    RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::string observed;
    std::string error;
    CHECK(RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        archive.path,
        Sha256(bytes),
        bytes.size(),
        snapshot,
        observed,
        error));
    CHECK(error.empty());
    return snapshot;
}

void TestVerifiedDigest()
{
    TemporaryFile archive("abc");
    CHECK(archive.valid);
    std::string observed;
    std::string error;
    CHECK(RoR::VerifyTerrainBundleArchiveSha256(
        archive.path,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        observed,
        error));
    CHECK(observed ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad");
    CHECK(error.empty());
}

void TestMismatchReportsObservedDigest()
{
    TemporaryFile archive("abc");
    CHECK(archive.valid);
    std::string observed;
    std::string error;
    CHECK(!RoR::VerifyTerrainBundleArchiveSha256(
        archive.path,
        std::string(64U, '0'),
        observed,
        error));
    CHECK(observed ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad");
    CHECK(error == "archive SHA-256 mismatch");
}

void TestImmutableAuthenticatedSnapshotAndRollback()
{
    TemporaryFile archive("abc");
    CHECK(archive.valid);
    RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::string observed;
    std::string error;
    CHECK(RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        archive.path,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        3U,
        snapshot,
        observed,
        error));
    CHECK(snapshot.initialized());
    CHECK(snapshot.version() ==
        RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION);
    CHECK(snapshot.source_archive_identity() == archive.path);
    CHECK(snapshot.archive_sha256() == observed);
    CHECK(snapshot.size() == 3U);
    CHECK(snapshot.bytes() != nullptr);
    CHECK(snapshot.bytes()[0U] == static_cast<std::uint8_t>('a'));
    CHECK(snapshot.bytes()[1U] == static_cast<std::uint8_t>('b'));
    CHECK(snapshot.bytes()[2U] == static_cast<std::uint8_t>('c'));
    CHECK(error.empty());

    const RoR::TerrainBundleAuthenticatedArchiveSnapshot owner = snapshot;
    CHECK(!RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        archive.path,
        std::string(64U, '0'),
        3U,
        snapshot,
        observed,
        error));
    CHECK(snapshot.SharesImmutableStateWith(owner));
    CHECK(error == "archive SHA-256 mismatch");
}

void TestAuthenticatedSnapshotCaps()
{
    TemporaryFile archive("abc");
    CHECK(archive.valid);
    RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::string observed;
    std::string error;
    CHECK(!RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        archive.path,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        2U,
        snapshot,
        observed,
        error));
    CHECK(!snapshot.initialized());
    CHECK(error == "archive exceeds authenticated snapshot byte cap");
    CHECK(!RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        archive.path,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES + 1U,
        snapshot,
        observed,
        error));
    CHECK(error == "archive byte limit is zero or exceeds its hard cap");
}

void TestStreamsAcrossReadBufferBoundaries()
{
    TemporaryFile archive(std::string(131089U, 'x'));
    CHECK(archive.valid);
    std::string observed;
    std::string error;
    CHECK(RoR::VerifyTerrainBundleArchiveSha256(
        archive.path,
        "e3c33f1a7c00a23610a13fa6b862df28"
        "82931be5b0262aeada7a47e282f1c679",
        observed,
        error));
    CHECK(observed ==
        "e3c33f1a7c00a23610a13fa6b862df28"
        "82931be5b0262aeada7a47e282f1c679");
    CHECK(error.empty());
}

void TestInvalidExpectedDigestFailsBeforeFileAccess()
{
    std::string observed = "stale";
    std::string error;
    CHECK(!RoR::VerifyTerrainBundleArchiveSha256(
        "path-does-not-exist",
        std::string(64U, 'A'),
        observed,
        error));
    CHECK(observed.empty());
    CHECK(error ==
        "expected SHA-256 must be 64 lowercase hexadecimal characters");
}

void TestMissingArchiveFailsClosed()
{
    std::string observed = "stale";
    std::string error;
    CHECK(!RoR::VerifyTerrainBundleArchiveSha256(
        "ror-terrain-bundle-verifier-does-not-exist.zip",
        std::string(64U, '0'),
        observed,
        error));
    CHECK(observed.empty());
    CHECK(error == "could not open archive for SHA-256 verification");
}

void TestBoundedZipPreflight()
{
    const std::vector<std::uint8_t> bytes = MakeStoredZip({
        std::make_pair("materials/root.material", "material Root {}\n"),
        std::make_pair("textures/root.dds", "dds")});
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot =
        MakeSnapshot(bytes);
    RoR::TerrainBundleAuthenticatedArchivePreflight preflight;
    std::string error;
    CHECK(RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        snapshot, preflight, error));
    CHECK(error.empty());
    CHECK(preflight.version ==
        RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_PREFLIGHT_VERSION);
    CHECK(preflight.members.size() == 2U);
    CHECK(preflight.members[0U].exact_member_name ==
        "materials/root.material");
    CHECK(preflight.members[0U].uncompressed_size == 17U);
    CHECK(preflight.members[1U].exact_member_name == "textures/root.dds");
    CHECK(preflight.total_uncompressed_bytes == 20U);
    CHECK(preflight.retained_member_identity_bytes ==
        std::string("materials/root.material").size() + 64U +
        std::string("textures/root.dds").size() + 64U);
}

void ExpectZipPreflightFailure(const std::vector<std::uint8_t>& bytes)
{
    RoR::TerrainBundleAuthenticatedArchivePreflight sentinel;
    sentinel.version = 91U;
    sentinel.retained_member_identity_bytes = 17U;
    std::string error;
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        MakeSnapshot(bytes), sentinel, error));
    CHECK(!error.empty());
    CHECK(sentinel.version == 91U);
    CHECK(sentinel.retained_member_identity_bytes == 17U);
    CHECK(sentinel.members.empty());
}

void TestZip64MemberAndDataDescriptorPreflight()
{
    const std::string zip64_name = "materials/zip64.material";
    const std::string zip64_payload = "material Zip64 {}\n";
    RoR::TerrainBundleAuthenticatedArchivePreflight preflight;
    std::string error;
    CHECK(RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        MakeSnapshot(MakeStoredZip64(zip64_name, zip64_payload)),
        preflight,
        error));
    CHECK(error.empty());
    CHECK(preflight.members.size() == 1U);
    CHECK(preflight.members[0U].exact_member_name == zip64_name);
    CHECK(preflight.members[0U].compressed_size == zip64_payload.size());
    CHECK(preflight.members[0U].uncompressed_size == zip64_payload.size());

    const std::string descriptor_name = "descriptor.material";
    const std::string descriptor_payload = "material Descriptor {}\n";
    CHECK(RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        MakeSnapshot(MakeStoredZipWithDescriptor(
            descriptor_name, descriptor_payload)),
        preflight,
        error));
    CHECK(error.empty());
    CHECK(preflight.members.size() == 1U);
    CHECK(preflight.members[0U].exact_member_name == descriptor_name);

    const std::string zip64_descriptor_name =
        "materials/zip64-descriptor.material";
    const std::string zip64_descriptor_payload =
        "material Zip64Descriptor {}\n";
    CHECK(RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        MakeSnapshot(MakeStoredZip64WithDescriptor(
            zip64_descriptor_name, zip64_descriptor_payload)),
        preflight,
        error));
    CHECK(error.empty());
    CHECK(preflight.members.size() == 1U);
    CHECK(preflight.members[0U].exact_member_name == zip64_descriptor_name);
    CHECK(preflight.members[0U].compressed_size ==
        zip64_descriptor_payload.size());
    CHECK(preflight.members[0U].uncompressed_size ==
        zip64_descriptor_payload.size());

    const std::string offset_descriptor_name =
        "materials/zip64-offset-descriptor.material";
    const std::string offset_descriptor_payload =
        "material Zip64OffsetDescriptor {}\n";
    CHECK(RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        MakeSnapshot(MakeStoredZip64OffsetWithDescriptor(
            offset_descriptor_name, offset_descriptor_payload)),
        preflight,
        error));
    CHECK(error.empty());
    CHECK(preflight.members.size() == 1U);
    CHECK(preflight.members[0U].exact_member_name ==
        offset_descriptor_name);
    CHECK(preflight.members[0U].compressed_size ==
        offset_descriptor_payload.size());
    CHECK(preflight.members[0U].uncompressed_size ==
        offset_descriptor_payload.size());

    std::vector<std::uint8_t> malformed_descriptor =
        MakeStoredZipWithDescriptor(descriptor_name, descriptor_payload);
    const std::size_t descriptor_crc_offset =
        30U + descriptor_name.size() + descriptor_payload.size() + 4U;
    Patch32(malformed_descriptor, descriptor_crc_offset, 0U);
    ExpectZipPreflightFailure(malformed_descriptor);

    std::vector<std::uint8_t> malformed_zip64_descriptor =
        MakeStoredZip64WithDescriptor(
            zip64_descriptor_name, zip64_descriptor_payload);
    const std::size_t zip64_descriptor_crc_offset =
        30U + zip64_descriptor_name.size() + 20U +
        zip64_descriptor_payload.size() + 4U;
    Patch32(malformed_zip64_descriptor, zip64_descriptor_crc_offset, 0U);
    ExpectZipPreflightFailure(malformed_zip64_descriptor);

    std::vector<std::uint8_t> malformed_offset_descriptor =
        MakeStoredZip64OffsetWithDescriptor(
            offset_descriptor_name, offset_descriptor_payload);
    const std::size_t offset_descriptor_crc_offset =
        30U + offset_descriptor_name.size() +
        offset_descriptor_payload.size() + 4U;
    Patch32(malformed_offset_descriptor, offset_descriptor_crc_offset, 0U);
    ExpectZipPreflightFailure(malformed_offset_descriptor);
}

void TestUnsupportedAndHostileLocalMetadataFailsBeforeMount()
{
    const std::string name = "root.material";
    const std::string payload = "material Root {}\n";

    std::vector<std::uint8_t> unsupported_method =
        MakeStoredZip({std::make_pair(name, payload)});
    std::size_t central = ClassicCentralOffset(unsupported_method);
    Patch16(unsupported_method, 8U, 99U);
    Patch16(unsupported_method, central + 10U, 99U);
    ExpectZipPreflightFailure(unsupported_method);

    std::vector<std::uint8_t> zero_compressed_deflate =
        MakeStoredZip({std::make_pair(name, payload)});
    central = ClassicCentralOffset(zero_compressed_deflate);
    Patch16(zero_compressed_deflate, 8U, 8U);
    Patch16(zero_compressed_deflate, central + 10U, 8U);
    Patch32(zero_compressed_deflate, 18U, 0U);
    Patch32(zero_compressed_deflate, central + 20U, 0U);
    ExpectZipPreflightFailure(zero_compressed_deflate);

    std::vector<std::uint8_t> encrypted =
        MakeStoredZip({std::make_pair(name, payload)});
    central = ClassicCentralOffset(encrypted);
    Patch16(encrypted, 6U, UINT16_C(0x0001));
    Patch16(encrypted, central + 8U, UINT16_C(0x0001));
    ExpectZipPreflightFailure(encrypted);

    std::vector<std::uint8_t> patched =
        MakeStoredZip({std::make_pair(name, payload)});
    central = ClassicCentralOffset(patched);
    Patch16(patched, 6U, UINT16_C(0x0020));
    Patch16(patched, central + 8U, UINT16_C(0x0020));
    ExpectZipPreflightFailure(patched);

    std::vector<std::uint8_t> attribute_directory =
        MakeStoredZip({std::make_pair("attribute-directory", "")});
    central = ClassicCentralOffset(attribute_directory);
    Patch32(attribute_directory, central + 38U, UINT32_C(0x10));
    ExpectZipPreflightFailure(attribute_directory);

    std::vector<std::uint8_t> local_name_mismatch =
        MakeStoredZip({std::make_pair(name, payload)});
    local_name_mismatch[30U] = 'R';
    ExpectZipPreflightFailure(local_name_mismatch);

    std::vector<std::uint8_t> local_extra_overflow =
        MakeStoredZip({std::make_pair(name, payload)});
    Patch16(local_extra_overflow, 28U, UINT16_MAX);
    ExpectZipPreflightFailure(local_extra_overflow);

    const std::string second_name = "next.material";
    std::vector<std::uint8_t> overlap = MakeStoredZip({
        std::make_pair(name, "x"),
        std::make_pair(second_name, "y")});
    central = ClassicCentralOffset(overlap);
    const std::uint32_t overlapping_size = 11U;
    Patch32(overlap, 18U, overlapping_size);
    Patch32(overlap, 22U, overlapping_size);
    Patch32(overlap, central + 20U, overlapping_size);
    Patch32(overlap, central + 24U, overlapping_size);
    ExpectZipPreflightFailure(overlap);

    std::vector<std::uint8_t> zip64_compressed_overflow =
        MakeStoredZip64(name, payload);
    const std::size_t zip64_central =
        30U + name.size() + 20U + payload.size();
    const std::size_t zip64_central_extra =
        zip64_central + 46U + name.size();
    Patch64(
        zip64_compressed_overflow,
        zip64_central_extra + 12U,
        UINT64_MAX);
    ExpectZipPreflightFailure(zip64_compressed_overflow);
}

void TestLookupAliasesFailBeforeMount()
{
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot =
        MakeSnapshot(MakeStoredZip({
            std::make_pair("materials/A.material", "first"),
            std::make_pair("materials/a.material", "other")}));
    RoR::TerrainBundleAuthenticatedArchivePreflight sentinel;
    sentinel.version = 99U;
    sentinel.retained_member_identity_bytes = 7U;
    std::string error;
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        snapshot, sentinel, error));
    CHECK(error ==
        "ZIP member identity is malformed or lookup-ambiguous");
    CHECK(sentinel.version == 99U);
    CHECK(sentinel.retained_member_identity_bytes == 7U);
    CHECK(sentinel.members.empty());

    const RoR::TerrainBundleAuthenticatedArchiveSnapshot slash_snapshot =
        MakeSnapshot(MakeStoredZip({
            std::make_pair("materials\\root.material", "bad")}));
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        slash_snapshot, sentinel, error));
    CHECK(error ==
        "ZIP member identity is malformed or lookup-ambiguous");
}

void TestZip64CountAndOverflowFailBeforeIndexing()
{
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot count_snapshot =
        MakeSnapshot(MakeZip64CountEnvelope(
            RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_ENTRIES + 1U));
    RoR::TerrainBundleAuthenticatedArchivePreflight sentinel;
    sentinel.version = 77U;
    std::string error;
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        count_snapshot, sentinel, error));
    CHECK(error ==
        "ZIP member count is zero, inconsistent, or exceeds its hard cap");
    CHECK(sentinel.version == 77U);

    const RoR::TerrainBundleAuthenticatedArchiveSnapshot overflow_snapshot =
        MakeSnapshot(MakeZip64CountEnvelope(1U, UINT64_MAX));
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        overflow_snapshot, sentinel, error));
    CHECK(error == "ZIP64 end record size is invalid");
    CHECK(sentinel.version == 77U);

    std::vector<std::uint8_t> after_locator =
        MakeZip64CountEnvelope(1U);
    const std::size_t locator_offset = 56U;
    const std::size_t eocd_offset = locator_offset + 20U;
    const std::size_t fake_record_offset = eocd_offset + 24U;
    Patch16(after_locator, eocd_offset + 20U, 100U);
    after_locator.resize(eocd_offset + 22U + 100U, 0U);
    Patch64(after_locator, locator_offset + 8U, fake_record_offset);
    Patch32(after_locator, fake_record_offset, UINT32_C(0x06064b50));
    Patch64(after_locator, fake_record_offset + 4U, 44U);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot after_snapshot =
        MakeSnapshot(after_locator);
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        after_snapshot, sentinel, error));
    CHECK(error == "ZIP64 end record size is invalid");
    CHECK(sentinel.version == 77U);
}

void TestMalformedCentralDirectoryFailsClosed()
{
    std::vector<std::uint8_t> bytes = MakeStoredZip({
        std::make_pair("root.material", "material Root {}\n")});
    const std::size_t eocd_offset = bytes.size() - 22U;
    const std::size_t central_offset =
        eocd_offset - 46U - std::string("root.material").size();
    bytes[central_offset] = 0U;
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot =
        MakeSnapshot(bytes);
    RoR::TerrainBundleAuthenticatedArchivePreflight preflight;
    std::string error;
    CHECK(!RoR::BuildTerrainBundleAuthenticatedArchivePreflight(
        snapshot, preflight, error));
    CHECK(!error.empty());
    CHECK(preflight.members.empty());
}

} // namespace

int main()
{
    TestVerifiedDigest();
    TestMismatchReportsObservedDigest();
    TestImmutableAuthenticatedSnapshotAndRollback();
    TestAuthenticatedSnapshotCaps();
    TestStreamsAcrossReadBufferBoundaries();
    TestInvalidExpectedDigestFailsBeforeFileAccess();
    TestMissingArchiveFailsClosed();
    TestBoundedZipPreflight();
    TestZip64MemberAndDataDescriptorPreflight();
    TestUnsupportedAndHostileLocalMetadataFailsBeforeMount();
    TestLookupAliasesFailBeforeMount();
    TestZip64CountAndOverflowFailBeforeIndexing();
    TestMalformedCentralDirectoryFailsClosed();
    if (failures != 0)
    {
        std::cerr << failures << " terrain archive verifier checks failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
