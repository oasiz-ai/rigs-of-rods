/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file JBeamVehicleImporter.h
/// @brief Immutable ZIP-to-RigDef product admission for supported JBeam roots.

#pragma once

#include "resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RigDef {
struct Document;
typedef std::shared_ptr<Document> DocumentPtr;
}

namespace RoR {
namespace BeamNG {

constexpr std::uint32_t JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION = 2U;
constexpr std::uint32_t
    JBEAM_CONFIGURED_VEHICLE_IMPORT_AUTHORITY_VERSION = 3U;

struct JBeamVehicleImportLimits
{
    std::size_t max_jbeam_members = 512U;
    std::size_t max_member_bytes = 16U * 1024U * 1024U;
    std::size_t max_total_jbeam_bytes = 64U * 1024U * 1024U;
    std::size_t max_configuration_bytes = 1024U * 1024U;
};

enum class JBeamVehicleImportCode
{
    ADMITTED,
    INVALID_ARCHIVE_AUTHORITY,
    ARCHIVE_INDEX_REJECTED,
    UNSAFE_OGRE_SCRIPT_MEMBER,
    JBEAM_MEMBER_LIMIT,
    JBEAM_MEMBER_DECODE_REJECTED,
    JBEAM_PARSE_REJECTED,
    PACKAGE_INDEX_REJECTED,
    NO_MAIN_PART,
    ROOT_PART_NOT_FOUND,
    PART_RESOLUTION_REJECTED,
    UNSUPPORTED_ACTIVE_SECTION,
    STRUCTURAL_IR_REJECTED,
    HYDRO_PLAN_REJECTED,
    WHEEL2_PLAN_REJECTED,
    RIGDEF_CONVERSION_REJECTED,
    ALLOCATION_FAILURE,
    INTERNAL_FAILURE,
    CONFIGURATION_PATH_REJECTED,
    CONFIGURATION_MEMBER_NOT_FOUND,
    CONFIGURATION_MEMBER_DECODE_REJECTED,
    CONFIGURATION_PARSE_REJECTED,
    CONFIGURATION_REQUEST_REJECTED
};

struct JBeamVehicleCandidate
{
    std::string root_part_name;
    std::string package_path;
};

struct JBeamVehicleImportResult;

/// Opaque immutable proof that a converted document came from one exact
/// EmbeddedZip snapshot, resource group, root part, package index, and
/// resolved graph. Only the importer can mint an initialized receipt.
class JBeamVehicleImportAuthorityReceipt final
{
public:
    JBeamVehicleImportAuthorityReceipt() noexcept = default;

    bool initialized() const noexcept;
    std::uint32_t version() const noexcept;
    const std::string& resource_group() const noexcept;
    const std::string& root_part_name() const noexcept;
    const std::string& archive_sha256() const noexcept;
    const std::string& package_index_sha256() const noexcept;
    const std::string& resolved_graph_sha256() const noexcept;
    const std::string& configuration_path() const noexcept;
    const std::string& configuration_sha256() const noexcept;
    const std::string& resolve_request_sha256() const noexcept;
    const std::string& wheel2_plan_sha256() const noexcept;
    std::size_t wheel2_plan_count() const noexcept;
    std::uint32_t wheel2_approximated_semantics() const noexcept;
    std::size_t jbeam_member_count() const noexcept;
    std::size_t retained_jbeam_bytes() const noexcept;
    const TerrainBundleAuthenticatedArchiveSnapshot*
        authenticated_archive_snapshot() const noexcept;
    bool Matches(
        const std::string& expected_resource_group,
        const std::string& expected_root_part,
        const TerrainBundleAuthenticatedArchiveSnapshot& snapshot) const
        noexcept;
    bool MatchesConfigured(
        const std::string& expected_resource_group,
        const std::string& expected_root_part,
        const std::string& expected_configuration_path,
        const TerrainBundleAuthenticatedArchiveSnapshot& snapshot) const
        noexcept;

private:
    struct State;
    explicit JBeamVehicleImportAuthorityReceipt(
        std::shared_ptr<const State> state) noexcept;
    std::shared_ptr<const State> m_state;

    friend struct JBeamVehicleImportResult;
    friend JBeamVehicleImportResult ImportJBeamVehicleFromArchiveSnapshot(
        const TerrainBundleAuthenticatedArchiveSnapshot&,
        const std::string&,
        const std::string&,
        const JBeamVehicleImportLimits&);
    friend JBeamVehicleImportResult
        ImportConfiguredJBeamVehicleFromArchiveSnapshot(
            const TerrainBundleAuthenticatedArchiveSnapshot&,
            const std::string&,
            const std::string&,
            const std::string&,
            const JBeamVehicleImportLimits&);
};

struct JBeamVehiclePackageInspection
{
    JBeamVehicleImportCode code =
        JBeamVehicleImportCode::INVALID_ARCHIVE_AUTHORITY;
    std::string detail;
    std::string archive_sha256;
    std::string package_index_sha256;
    std::size_t jbeam_member_count = 0U;
    std::size_t retained_jbeam_bytes = 0U;
    std::vector<JBeamVehicleCandidate> candidates;

    bool IsValid() const noexcept
    {
        return code == JBeamVehicleImportCode::ADMITTED &&
            !candidates.empty();
    }
};

struct JBeamVehicleImportResult
{
    JBeamVehicleImportCode code =
        JBeamVehicleImportCode::INVALID_ARCHIVE_AUTHORITY;
    std::string detail;
    RigDef::DocumentPtr document;
    std::shared_ptr<const JBeamVehicleImportAuthorityReceipt> authority;

    bool IsAdmitted() const noexcept
    {
        return code == JBeamVehicleImportCode::ADMITTED &&
            document != nullptr && authority != nullptr &&
            authority->initialized();
    }
};

/// Enumerates exact slotType "main" roots after bounded decode, CRC32,
/// relaxed-JBeam parse, and deterministic package-index validation.
JBeamVehiclePackageInspection InspectJBeamVehicleArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const JBeamVehicleImportLimits& limits = JBeamVehicleImportLimits());

/// Repeats all inspection work, resolves one exact root, rejects every active
/// section outside the first native structural/hydro subset, builds the full
/// hydro plan set, and publishes one RigDef document plus opaque authority.
JBeamVehicleImportResult ImportJBeamVehicleFromArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const std::string& resource_group,
    const std::string& root_part_name,
    const JBeamVehicleImportLimits& limits = JBeamVehicleImportLimits());

/// Resolves one explicit canonical .pc archive member as inert parts/vars data.
/// The member is never auto-selected. Its exact decoded bytes and canonical
/// resolve request are bound into a version-3 authority receipt; the existing
/// root-only API continues to mint byte-compatible version-2 authority.
JBeamVehicleImportResult ImportConfiguredJBeamVehicleFromArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const std::string& resource_group,
    const std::string& root_part_name,
    const std::string& configuration_path,
    const JBeamVehicleImportLimits& limits = JBeamVehicleImportLimits());

const char* JBeamVehicleImportCodeToString(JBeamVehicleImportCode code);

} // namespace BeamNG
} // namespace RoR
