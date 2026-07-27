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

/// @file BeamNGPackageManifest.h
/// @brief Dependency-free, non-extracting package manifest validation.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

struct PackageFormatProfile
{
    std::string identifier;
    std::string version;

    PackageFormatProfile();
};

enum class PackageEntryKind
{
    REGULAR_FILE,
    DIRECTORY,
    SYMLINK,
    OTHER
};

struct PackageEntryInput
{
    std::string path;
    PackageEntryKind kind;
    std::uint64_t compressed_size;
    std::uint64_t expanded_size;
    bool encrypted;

    PackageEntryInput();
};

enum class PackageContentRoot
{
    VEHICLES,
    LEVELS,
    ART,
    ASSETS,
    LUA,
    SCRIPTS,
    UI,
    GAMEPLAY,
    SETTINGS,
    TRACK_EDITOR,
    VEHICLE_GROUPS,
    UNKNOWN
};

struct PackageScanLimits
{
    std::size_t max_entries;
    std::size_t max_path_bytes;
    std::size_t max_path_depth;
    std::uint64_t max_entry_expanded_bytes;
    std::uint64_t max_total_expanded_bytes;
    std::uint64_t max_compression_ratio;

    PackageScanLimits();
};

enum class ManifestErrorCode
{
    NONE,
    EMPTY_PACKAGE,
    ENTRY_COUNT_LIMIT,
    EMPTY_PATH,
    EMBEDDED_NUL,
    NON_ASCII_PATH,
    ABSOLUTE_PATH,
    BACKSLASH,
    PARENT_TRAVERSAL,
    EMPTY_NORMALIZED_PATH,
    INVALID_PATH_CHARACTER,
    WINDOWS_RESERVED_NAME,
    TRAILING_DOT_OR_SPACE,
    PATH_LENGTH_LIMIT,
    PATH_DEPTH_LIMIT,
    SYMLINK,
    UNSUPPORTED_ENTRY_TYPE,
    ENCRYPTED_ENTRY,
    DIRECTORY_SIZE_MISMATCH,
    DIRECTORY_MARKER_MISMATCH,
    ENTRY_SIZE_LIMIT,
    TOTAL_SIZE_LIMIT,
    COMPRESSION_RATIO_LIMIT,
    DUPLICATE_ENTRY,
    NORMALIZATION_COLLISION,
    CASE_COLLISION,
    PATH_PREFIX_COLLISION
};

struct ManifestError
{
    ManifestErrorCode code;
    std::size_t entry_index;
    std::size_t conflicting_entry_index;
    std::string path;
    std::string conflicting_path;

    ManifestError();
    bool HasError() const;
};

struct PackagePathResult
{
    std::string canonical_path;
    std::size_t depth;
    ManifestError error;

    PackagePathResult();
    bool IsValid() const;
};

struct PackageManifestEntry
{
    std::string path;
    PackageEntryKind kind;
    PackageContentRoot content_root;
    std::uint64_t compressed_size;
    std::uint64_t expanded_size;
};

struct PackageManifest
{
    PackageFormatProfile format_profile;
    std::vector<PackageManifestEntry> entries;
    std::uint64_t total_expanded_bytes;

    PackageManifest();
};

struct PackageManifestResult
{
    PackageManifest manifest;
    ManifestError error;

    bool IsValid() const;
};

/// Validates and canonicalizes a single archive-relative path without touching
/// the filesystem. Non-ASCII paths currently fail closed so case and filename
/// semantics remain identical on every supported platform.
PackagePathResult NormalizePackagePath(
    const std::string& raw_path,
    bool is_directory,
    const PackageScanLimits& limits);

/// Builds a deterministic manifest from archive metadata. The caller is
/// responsible for accurately identifying symlinks and encrypted entries from
/// the archive format; this function performs no extraction or code execution.
PackageManifestResult BuildPackageManifest(
    const std::vector<PackageEntryInput>& input_entries,
    const PackageScanLimits& limits = PackageScanLimits(),
    const PackageFormatProfile& format_profile = PackageFormatProfile());

/// Returns a stable, versioned representation suitable for feeding into a
/// cryptographic package-identity hash.
std::string SerializeCanonicalManifest(const PackageManifest& manifest);

const char* ManifestErrorCodeToString(ManifestErrorCode code);
const char* PackageContentRootToString(PackageContentRoot root);

enum class CompatibilityStatus
{
    NATIVE,
    APPROXIMATED,
    PRESERVED_BUT_DISABLED,
    UNSUPPORTED,
    REJECTED
};

enum class DiagnosticSeverity
{
    INFO,
    WARNING,
    ERROR
};

/// A 1-based source origin. Zero denotes an unknown row, line, or column.
struct SourceOrigin
{
    std::string package_path;
    std::string section;
    std::uint64_t row;
    std::uint64_t line;
    std::uint64_t column;

    SourceOrigin();
};

struct FeatureAssessment
{
    std::string feature_id;
    CompatibilityStatus status;
    std::string reason;
    SourceOrigin source;

    FeatureAssessment();
};

struct CompatibilityDiagnostic
{
    std::string code;
    DiagnosticSeverity severity;
    std::string message;
    SourceOrigin source;

    CompatibilityDiagnostic();
};

/// Versioned data-only report. It contains no callbacks or executable source;
/// imported Lua can therefore be reported as preserved-but-disabled without
/// being loaded into a scripting runtime.
struct CompatibilityReport
{
    std::uint32_t schema_version;
    PackageFormatProfile format_profile;
    std::string package_identity;
    std::string importer_version;
    std::string configuration;
    std::vector<FeatureAssessment> features;
    std::vector<CompatibilityDiagnostic> diagnostics;

    CompatibilityReport();
};

/// Canonical byte serialization sorts assessments and diagnostics without
/// changing their in-memory discovery order.
std::string SerializeCanonicalCompatibilityReport(
    const CompatibilityReport& report);

const char* CompatibilityStatusToString(CompatibilityStatus status);
const char* DiagnosticSeverityToString(DiagnosticSeverity severity);

} // namespace BeamNG
} // namespace RoR
