/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR {

constexpr std::uint32_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION = 1U;
constexpr std::uint64_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES =
        512ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_PREFLIGHT_VERSION = 1U;
constexpr std::uint64_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_ENTRIES = 65536U;
constexpr std::uint64_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_IDENTITY_BYTES =
        16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_BYTES =
        512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_TOTAL_MEMBER_BYTES =
        1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_NAME_BYTES = 16384U;

struct TerrainBundleAuthenticatedArchiveMemberPreflight final
{
    std::string exact_member_name;
    std::uint64_t compressed_size = 0U;
    std::uint64_t uncompressed_size = 0U;
    std::uint32_t crc32 = 0U;
    bool directory = false;
};

/// Bounded metadata parsed directly from the immutable ZIP central and local
/// records before OGRE or EmbeddedZip observes the archive. `members` includes
/// directory entries because they also consume native index capacity.
struct TerrainBundleAuthenticatedArchivePreflight final
{
    std::uint32_t version =
        TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_PREFLIGHT_VERSION;
    std::uint64_t retained_member_identity_bytes = 0U;
    std::uint64_t total_uncompressed_bytes = 0U;
    std::vector<TerrainBundleAuthenticatedArchiveMemberPreflight> members;
};

/// Immutable owner for the exact archive bytes finalized by SHA-256. This is
/// the only input accepted by the authenticated EmbeddedZip mount path, so
/// OGRE never reopens the mutable source pathname after verification.
class TerrainBundleAuthenticatedArchiveSnapshot final
{
public:
    TerrainBundleAuthenticatedArchiveSnapshot() = default;
    ~TerrainBundleAuthenticatedArchiveSnapshot() = default;
    TerrainBundleAuthenticatedArchiveSnapshot(
        const TerrainBundleAuthenticatedArchiveSnapshot&) noexcept = default;
    TerrainBundleAuthenticatedArchiveSnapshot& operator=(
        const TerrainBundleAuthenticatedArchiveSnapshot&) noexcept = default;
    TerrainBundleAuthenticatedArchiveSnapshot(
        TerrainBundleAuthenticatedArchiveSnapshot&&) noexcept = default;
    TerrainBundleAuthenticatedArchiveSnapshot& operator=(
        TerrainBundleAuthenticatedArchiveSnapshot&&) noexcept = default;

    bool initialized() const noexcept;
    std::uint32_t version() const noexcept;
    const std::string& source_archive_identity() const noexcept;
    const std::string& archive_sha256() const noexcept;
    const std::uint8_t* bytes() const noexcept;
    std::size_t size() const noexcept;
    bool SharesImmutableStateWith(
        const TerrainBundleAuthenticatedArchiveSnapshot& other) const noexcept;

private:
    struct State;
    explicit TerrainBundleAuthenticatedArchiveSnapshot(
        std::shared_ptr<const State> state) noexcept;
    std::shared_ptr<const State> m_state;

    friend bool LoadAndVerifyTerrainBundleArchiveSnapshot(
        const std::string&,
        const std::string&,
        std::uint64_t,
        TerrainBundleAuthenticatedArchiveSnapshot&,
        std::string&,
        std::string&);
};

/// Streams an archive through SHA-256 and compares it with a mandatory,
/// lowercase hexadecimal digest.
///
/// The archive is opened read-only and is never buffered in full. On success,
/// `out_observed_sha256` contains the verified digest and `out_error` is empty.
/// On failure, the observed digest is supplied when hashing completed.
bool VerifyTerrainBundleArchiveSha256(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::string& out_observed_sha256,
    std::string& out_error);

/// Computes the lowercase SHA-256 of one archive without accepting or
/// mounting it. Callers that need an immutable snapshot must subsequently use
/// LoadAndVerifyTerrainBundleArchiveSnapshot with this digest; that second
/// full read closes mutation between discovery and publication.
bool ComputeTerrainBundleArchiveSha256(
    const std::string& archive_path,
    std::string& out_observed_sha256,
    std::string& out_error);

/// Reads the archive exactly once, enforces a caller cap no larger than the
/// hard 512 MiB ceiling, hashes those same bytes, and atomically publishes an
/// immutable snapshot only when the expected lowercase SHA-256 matches.
bool LoadAndVerifyTerrainBundleArchiveSnapshot(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::uint64_t maximum_archive_bytes,
    TerrainBundleAuthenticatedArchiveSnapshot& out_snapshot,
    std::string& out_observed_sha256,
    std::string& out_error);

/// Parses the immutable snapshot's classic or ZIP64 central-directory
/// envelope without mounting or decompressing it. The parser rejects count,
/// name, identity, decoded-size, compression/flag, local-span, descriptor,
/// arithmetic, directory-classification, overlap, and lookup-alias hazards
/// before any entry-controlled OGRE allocation. Output is transactional on
/// failure.
bool BuildTerrainBundleAuthenticatedArchivePreflight(
    const TerrainBundleAuthenticatedArchiveSnapshot& archive_snapshot,
    TerrainBundleAuthenticatedArchivePreflight& out_preflight,
    std::string& out_error);

} // namespace RoR
