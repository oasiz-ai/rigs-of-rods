#include "TerrainBundleArchiveVerifier.h"

#include <cstdio>
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
