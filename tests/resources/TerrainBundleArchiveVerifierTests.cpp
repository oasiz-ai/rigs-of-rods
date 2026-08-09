#include "TerrainBundleArchiveVerifier.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
    if (failures != 0)
    {
        std::cerr << failures << " terrain archive verifier checks failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
