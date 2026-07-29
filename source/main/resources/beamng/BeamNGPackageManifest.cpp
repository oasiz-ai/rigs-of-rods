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

/// @file BeamNGPackageManifest.cpp

#include "BeamNGPackageManifest.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <sstream>

namespace RoR {
namespace BeamNG {
namespace {

const std::size_t NO_ENTRY_INDEX = std::numeric_limits<std::size_t>::max();

ManifestError MakeError(
    ManifestErrorCode code,
    std::size_t entry_index,
    const std::string& path)
{
    ManifestError error;
    error.code = code;
    error.entry_index = entry_index;
    error.path = path;
    return error;
}

bool IsAsciiLetter(char value)
{
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z');
}

char AsciiLower(char value)
{
    if (value >= 'A' && value <= 'Z')
    {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string AsciiLowercase(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), AsciiLower);
    return result;
}

PackageContentRoot ClassifyContentRoot(const std::string& path)
{
    const std::size_t separator = path.find('/');
    const std::string root = path.substr(0, separator);
    if (root == "vehicles")
        return PackageContentRoot::VEHICLES;
    if (root == "levels")
        return PackageContentRoot::LEVELS;
    if (root == "art")
        return PackageContentRoot::ART;
    if (root == "assets")
        return PackageContentRoot::ASSETS;
    if (root == "lua")
        return PackageContentRoot::LUA;
    if (root == "scripts")
        return PackageContentRoot::SCRIPTS;
    if (root == "ui")
        return PackageContentRoot::UI;
    if (root == "gameplay")
        return PackageContentRoot::GAMEPLAY;
    if (root == "settings")
        return PackageContentRoot::SETTINGS;
    if (root == "trackEditor")
        return PackageContentRoot::TRACK_EDITOR;
    if (root == "vehicleGroups")
        return PackageContentRoot::VEHICLE_GROUPS;
    return PackageContentRoot::UNKNOWN;
}

bool IsWindowsReservedName(const std::string& segment)
{
    const std::size_t extension_separator = segment.find('.');
    std::string basename = segment.substr(0, extension_separator);
    // Win32 device-name matching ignores an extension and trailing spaces or
    // dots in the basename. Reject those aliases even when the complete
    // segment itself does not end in a dot or space (for example "CON .txt").
    while (!basename.empty() &&
           (basename[basename.size() - 1] == '.' ||
            basename[basename.size() - 1] == ' '))
    {
        basename.resize(basename.size() - 1);
    }
    basename = AsciiLowercase(basename);

    if (basename == "con" ||
        basename == "conin$" ||
        basename == "conout$" ||
        basename == "prn" ||
        basename == "aux" ||
        basename == "nul" ||
        basename == "clock$")
    {
        return true;
    }

    if (basename.size() == 4 &&
        (basename.substr(0, 3) == "com" ||
         basename.substr(0, 3) == "lpt") &&
        basename[3] >= '1' &&
        basename[3] <= '9')
    {
        return true;
    }

    return false;
}

bool HasInvalidPortableCharacter(const std::string& segment)
{
    for (std::string::const_iterator it = segment.begin();
         it != segment.end();
         ++it)
    {
        const unsigned char value = static_cast<unsigned char>(*it);
        if (value < 0x20 || value == 0x7f)
        {
            return true;
        }

        switch (*it)
        {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            break;
        }
    }
    return false;
}

struct CandidateEntry
{
    std::string raw_path;
    std::size_t input_index;
    PackageManifestEntry entry;
};

bool CanonicalThenRawLess(
    const CandidateEntry& left,
    const CandidateEntry& right)
{
    if (left.entry.path != right.entry.path)
    {
        return left.entry.path < right.entry.path;
    }
    if (left.raw_path != right.raw_path)
    {
        return left.raw_path < right.raw_path;
    }
    return left.input_index < right.input_index;
}

bool CaseKeyThenCanonicalLess(
    const CandidateEntry& left,
    const CandidateEntry& right)
{
    const std::string left_key = AsciiLowercase(left.entry.path);
    const std::string right_key = AsciiLowercase(right.entry.path);
    if (left_key != right_key)
    {
        return left_key < right_key;
    }
    if (left.entry.path != right.entry.path)
    {
        return left.entry.path < right.entry.path;
    }
    return left.input_index < right.input_index;
}

bool ManifestEntryLess(
    const PackageManifestEntry& left,
    const PackageManifestEntry& right)
{
    if (left.path != right.path)
        return left.path < right.path;
    if (left.kind != right.kind)
    {
        return static_cast<int>(left.kind) <
            static_cast<int>(right.kind);
    }
    if (left.content_root != right.content_root)
    {
        return static_cast<int>(left.content_root) <
            static_cast<int>(right.content_root);
    }
    if (left.compressed_size != right.compressed_size)
        return left.compressed_size < right.compressed_size;
    return left.expanded_size < right.expanded_size;
}

void WriteLengthPrefixed(
    std::ostringstream& output,
    const std::string& value)
{
    output << value.size() << ':' << value;
}

bool SourceOriginLess(const SourceOrigin& left, const SourceOrigin& right)
{
    if (left.package_path != right.package_path)
        return left.package_path < right.package_path;
    if (left.section != right.section)
        return left.section < right.section;
    if (left.row != right.row)
        return left.row < right.row;
    if (left.line != right.line)
        return left.line < right.line;
    return left.column < right.column;
}

bool FeatureAssessmentLess(
    const FeatureAssessment& left,
    const FeatureAssessment& right)
{
    if (left.feature_id != right.feature_id)
        return left.feature_id < right.feature_id;
    if (SourceOriginLess(left.source, right.source))
        return true;
    if (SourceOriginLess(right.source, left.source))
        return false;
    if (left.status != right.status)
        return static_cast<int>(left.status) < static_cast<int>(right.status);
    return left.reason < right.reason;
}

bool CompatibilityDiagnosticLess(
    const CompatibilityDiagnostic& left,
    const CompatibilityDiagnostic& right)
{
    if (left.code != right.code)
        return left.code < right.code;
    if (SourceOriginLess(left.source, right.source))
        return true;
    if (SourceOriginLess(right.source, left.source))
        return false;
    if (left.severity != right.severity)
    {
        return static_cast<int>(left.severity) <
            static_cast<int>(right.severity);
    }
    return left.message < right.message;
}

ManifestError MakeCollisionError(
    ManifestErrorCode code,
    const CandidateEntry& first,
    const CandidateEntry& second)
{
    ManifestError error = MakeError(code, second.input_index, second.raw_path);
    error.conflicting_entry_index = first.input_index;
    error.conflicting_path = first.raw_path;
    return error;
}

bool ExceedsCompressionRatio(
    std::uint64_t expanded_size,
    std::uint64_t compressed_size,
    std::uint64_t maximum_ratio)
{
    if (expanded_size == 0)
    {
        return false;
    }
    if (compressed_size == 0)
    {
        return true;
    }

    const std::uint64_t quotient = expanded_size / compressed_size;
    const std::uint64_t remainder = expanded_size % compressed_size;
    return quotient > maximum_ratio ||
        (quotient == maximum_ratio && remainder != 0);
}

} // anonymous namespace

PackageFormatProfile::PackageFormatProfile() :
    identifier("beamng-docs"),
    version("0.38.5.0-2026-07-27")
{
}

PackageEntryInput::PackageEntryInput() :
    kind(PackageEntryKind::REGULAR_FILE),
    compressed_size(0),
    expanded_size(0),
    encrypted(false)
{
}

PackageScanLimits::PackageScanLimits() :
    max_entries(10000),
    max_path_bytes(512),
    max_path_depth(32),
    max_entry_expanded_bytes(UINT64_C(1073741824)),
    max_total_expanded_bytes(UINT64_C(4294967296)),
    // Highly uniform DDS data in the pinned FormulaCOUPE interoperability
    // fixture reaches 838.735:1 under ordinary DEFLATE. Keep the ratio guard
    // above that measured legitimate case while the independent 1 GiB entry
    // and 4 GiB package expansion ceilings remain hard bounds.
    max_compression_ratio(1024)
{
}

ManifestError::ManifestError() :
    code(ManifestErrorCode::NONE),
    entry_index(NO_ENTRY_INDEX),
    conflicting_entry_index(NO_ENTRY_INDEX)
{
}

bool ManifestError::HasError() const
{
    return code != ManifestErrorCode::NONE;
}

PackagePathResult::PackagePathResult() :
    depth(0)
{
}

bool PackagePathResult::IsValid() const
{
    return !error.HasError();
}

PackageManifest::PackageManifest() :
    total_expanded_bytes(0)
{
}

bool PackageManifestResult::IsValid() const
{
    return !error.HasError();
}

PackagePathResult NormalizePackagePath(
    const std::string& raw_path,
    bool is_directory,
    const PackageScanLimits& limits)
{
    PackagePathResult result;

    if (raw_path.empty())
    {
        result.error = MakeError(
            ManifestErrorCode::EMPTY_PATH, NO_ENTRY_INDEX, raw_path);
        return result;
    }
    if (raw_path.size() > limits.max_path_bytes)
    {
        result.error = MakeError(
            ManifestErrorCode::PATH_LENGTH_LIMIT, NO_ENTRY_INDEX, raw_path);
        return result;
    }
    if (raw_path.find('\0') != std::string::npos)
    {
        result.error = MakeError(
            ManifestErrorCode::EMBEDDED_NUL, NO_ENTRY_INDEX, raw_path);
        return result;
    }
    if (raw_path[0] == '/' || raw_path[0] == '\\' ||
        (raw_path.size() >= 2 &&
         IsAsciiLetter(raw_path[0]) &&
         raw_path[1] == ':'))
    {
        result.error = MakeError(
            ManifestErrorCode::ABSOLUTE_PATH, NO_ENTRY_INDEX, raw_path);
        return result;
    }
    if (raw_path.find('\\') != std::string::npos)
    {
        result.error = MakeError(
            ManifestErrorCode::BACKSLASH, NO_ENTRY_INDEX, raw_path);
        return result;
    }

    for (std::string::const_iterator it = raw_path.begin();
         it != raw_path.end();
         ++it)
    {
        if (static_cast<unsigned char>(*it) >= 0x80)
        {
            result.error = MakeError(
                ManifestErrorCode::NON_ASCII_PATH,
                NO_ENTRY_INDEX,
                raw_path);
            return result;
        }
    }

    const bool has_directory_marker =
        raw_path[raw_path.size() - 1] == '/';
    if (has_directory_marker && !is_directory)
    {
        result.error = MakeError(
            ManifestErrorCode::DIRECTORY_MARKER_MISMATCH,
            NO_ENTRY_INDEX,
            raw_path);
        return result;
    }

    std::vector<std::string> segments;
    std::size_t segment_start = 0;
    while (segment_start <= raw_path.size())
    {
        const std::size_t separator = raw_path.find('/', segment_start);
        const std::size_t segment_end =
            separator == std::string::npos ? raw_path.size() : separator;
        const std::string segment =
            raw_path.substr(segment_start, segment_end - segment_start);

        if (!segment.empty() && segment != ".")
        {
            if (segment == "..")
            {
                result.error = MakeError(
                    ManifestErrorCode::PARENT_TRAVERSAL,
                    NO_ENTRY_INDEX,
                    raw_path);
                return result;
            }
            if (HasInvalidPortableCharacter(segment))
            {
                result.error = MakeError(
                    ManifestErrorCode::INVALID_PATH_CHARACTER,
                    NO_ENTRY_INDEX,
                    raw_path);
                return result;
            }
            if (segment[segment.size() - 1] == '.' ||
                segment[segment.size() - 1] == ' ')
            {
                result.error = MakeError(
                    ManifestErrorCode::TRAILING_DOT_OR_SPACE,
                    NO_ENTRY_INDEX,
                    raw_path);
                return result;
            }
            if (IsWindowsReservedName(segment))
            {
                result.error = MakeError(
                    ManifestErrorCode::WINDOWS_RESERVED_NAME,
                    NO_ENTRY_INDEX,
                    raw_path);
                return result;
            }

            segments.push_back(segment);
            if (segments.size() > limits.max_path_depth)
            {
                result.error = MakeError(
                    ManifestErrorCode::PATH_DEPTH_LIMIT,
                    NO_ENTRY_INDEX,
                    raw_path);
                return result;
            }
        }

        if (separator == std::string::npos)
        {
            break;
        }
        segment_start = separator + 1;
    }

    if (segments.empty())
    {
        result.error = MakeError(
            ManifestErrorCode::EMPTY_NORMALIZED_PATH,
            NO_ENTRY_INDEX,
            raw_path);
        return result;
    }

    std::ostringstream canonical;
    for (std::size_t index = 0; index < segments.size(); ++index)
    {
        if (index != 0)
        {
            canonical << '/';
        }
        canonical << segments[index];
    }

    result.canonical_path = canonical.str();
    result.depth = segments.size();
    return result;
}

PackageManifestResult BuildPackageManifest(
    const std::vector<PackageEntryInput>& input_entries,
    const PackageScanLimits& limits,
    const PackageFormatProfile& format_profile)
{
    PackageManifestResult result;
    result.manifest.format_profile = format_profile;

    if (input_entries.empty())
    {
        result.error = MakeError(
            ManifestErrorCode::EMPTY_PACKAGE, NO_ENTRY_INDEX, std::string());
        return result;
    }
    if (input_entries.size() > limits.max_entries)
    {
        result.error = MakeError(
            ManifestErrorCode::ENTRY_COUNT_LIMIT,
            NO_ENTRY_INDEX,
            std::string());
        return result;
    }

    std::vector<CandidateEntry> candidates;
    candidates.reserve(input_entries.size());
    std::uint64_t total_expanded_bytes = 0;

    for (std::size_t index = 0; index < input_entries.size(); ++index)
    {
        const PackageEntryInput& input = input_entries[index];
        const bool is_directory = input.kind == PackageEntryKind::DIRECTORY;
        PackagePathResult path =
            NormalizePackagePath(input.path, is_directory, limits);
        if (!path.IsValid())
        {
            result.error = path.error;
            result.error.entry_index = index;
            return result;
        }

        if (input.kind == PackageEntryKind::SYMLINK)
        {
            result.error = MakeError(
                ManifestErrorCode::SYMLINK, index, input.path);
            return result;
        }
        if (input.kind != PackageEntryKind::REGULAR_FILE &&
            input.kind != PackageEntryKind::DIRECTORY &&
            input.kind != PackageEntryKind::SYMLINK)
        {
            result.error = MakeError(
                ManifestErrorCode::UNSUPPORTED_ENTRY_TYPE, index, input.path);
            return result;
        }
        if (input.encrypted)
        {
            result.error = MakeError(
                ManifestErrorCode::ENCRYPTED_ENTRY, index, input.path);
            return result;
        }
        if (is_directory &&
            (input.compressed_size != 0 || input.expanded_size != 0))
        {
            result.error = MakeError(
                ManifestErrorCode::DIRECTORY_SIZE_MISMATCH, index, input.path);
            return result;
        }
        if (input.expanded_size > limits.max_entry_expanded_bytes)
        {
            result.error = MakeError(
                ManifestErrorCode::ENTRY_SIZE_LIMIT, index, input.path);
            return result;
        }
        if (input.expanded_size >
            limits.max_total_expanded_bytes - total_expanded_bytes)
        {
            result.error = MakeError(
                ManifestErrorCode::TOTAL_SIZE_LIMIT, index, input.path);
            return result;
        }
        if (!is_directory &&
            ExceedsCompressionRatio(
                input.expanded_size,
                input.compressed_size,
                limits.max_compression_ratio))
        {
            result.error = MakeError(
                ManifestErrorCode::COMPRESSION_RATIO_LIMIT, index, input.path);
            return result;
        }

        total_expanded_bytes += input.expanded_size;

        CandidateEntry candidate;
        candidate.raw_path = input.path;
        candidate.input_index = index;
        candidate.entry.path = path.canonical_path;
        candidate.entry.kind = input.kind;
        candidate.entry.content_root =
            ClassifyContentRoot(path.canonical_path);
        candidate.entry.compressed_size = input.compressed_size;
        candidate.entry.expanded_size = input.expanded_size;
        candidates.push_back(candidate);
    }

    std::sort(
        candidates.begin(), candidates.end(), CanonicalThenRawLess);
    for (std::size_t index = 1; index < candidates.size(); ++index)
    {
        const CandidateEntry& previous = candidates[index - 1];
        const CandidateEntry& current = candidates[index];
        if (previous.entry.path == current.entry.path)
        {
            const ManifestErrorCode code =
                previous.raw_path == current.raw_path ?
                ManifestErrorCode::DUPLICATE_ENTRY :
                ManifestErrorCode::NORMALIZATION_COLLISION;
            result.error =
                MakeCollisionError(code, previous, current);
            return result;
        }
    }

    // A regular file cannot also be an ancestor directory. Archive tools and
    // filesystems disagree about whether entries such as "vehicles" followed
    // by "vehicles/car/main.jbeam" overwrite, fail, or alias one another.
    // Reject the ambiguity before any future extraction adapter sees it.
    std::map<std::string, const CandidateEntry*> entries_by_path;
    std::map<std::string, const CandidateEntry*> entries_by_casefolded_path;
    for (const CandidateEntry& candidate : candidates)
    {
        entries_by_path[candidate.entry.path] = &candidate;
        entries_by_casefolded_path[AsciiLowercase(candidate.entry.path)] =
            &candidate;
    }

    for (const CandidateEntry& current : candidates)
    {
        std::size_t separator = current.entry.path.find('/');
        while (separator != std::string::npos)
        {
            const std::string ancestor =
                current.entry.path.substr(0, separator);
            const std::map<std::string, const CandidateEntry*>::const_iterator
                found = entries_by_path.find(ancestor);
            if (found != entries_by_path.end() &&
                found->second->entry.kind == PackageEntryKind::REGULAR_FILE)
            {
                result.error = MakeCollisionError(
                    ManifestErrorCode::PATH_PREFIX_COLLISION,
                    *found->second,
                    current);
                return result;
            }

            const std::string casefolded_ancestor =
                AsciiLowercase(ancestor);
            const std::map<std::string, const CandidateEntry*>::const_iterator
                casefolded_found =
                    entries_by_casefolded_path.find(casefolded_ancestor);
            if (casefolded_found != entries_by_casefolded_path.end() &&
                casefolded_found->second->entry.kind ==
                    PackageEntryKind::REGULAR_FILE)
            {
                result.error = MakeCollisionError(
                    ManifestErrorCode::PATH_PREFIX_COLLISION,
                    *casefolded_found->second,
                    current);
                return result;
            }
            separator = current.entry.path.find('/', separator + 1);
        }
    }

    std::vector<CandidateEntry> case_candidates = candidates;
    std::sort(
        case_candidates.begin(),
        case_candidates.end(),
        CaseKeyThenCanonicalLess);
    for (std::size_t index = 1; index < case_candidates.size(); ++index)
    {
        const CandidateEntry& previous = case_candidates[index - 1];
        const CandidateEntry& current = case_candidates[index];
        if (AsciiLowercase(previous.entry.path) ==
                AsciiLowercase(current.entry.path) &&
            previous.entry.path != current.entry.path)
        {
            result.error = MakeCollisionError(
                ManifestErrorCode::CASE_COLLISION, previous, current);
            return result;
        }
    }

    result.manifest.entries.reserve(candidates.size());
    for (std::vector<CandidateEntry>::const_iterator it = candidates.begin();
         it != candidates.end();
         ++it)
    {
        result.manifest.entries.push_back(it->entry);
    }
    result.manifest.total_expanded_bytes = total_expanded_bytes;
    return result;
}

std::string SerializeCanonicalManifest(const PackageManifest& manifest)
{
    std::vector<PackageManifestEntry> entries = manifest.entries;
    std::sort(entries.begin(), entries.end(), ManifestEntryLess);

    std::ostringstream output;
    output << "ror-beamng-package-manifest-v1\n";
    output << "format-profile\t";
    WriteLengthPrefixed(output, manifest.format_profile.identifier);
    output << '\t';
    WriteLengthPrefixed(output, manifest.format_profile.version);
    output << '\n';
    for (std::vector<PackageManifestEntry>::const_iterator it =
             entries.begin();
         it != entries.end();
         ++it)
    {
        output
            << (it->kind == PackageEntryKind::DIRECTORY ? 'D' : 'F')
            << '\t'
            << it->path
            << '\t'
            << PackageContentRootToString(it->content_root)
            << '\t'
            << it->compressed_size
            << '\t'
            << it->expanded_size
            << '\n';
    }
    output << "total-expanded\t" << manifest.total_expanded_bytes << '\n';
    return output.str();
}

const char* PackageContentRootToString(PackageContentRoot root)
{
    switch (root)
    {
    case PackageContentRoot::VEHICLES:
        return "vehicles";
    case PackageContentRoot::LEVELS:
        return "levels";
    case PackageContentRoot::ART:
        return "art";
    case PackageContentRoot::ASSETS:
        return "assets";
    case PackageContentRoot::LUA:
        return "lua";
    case PackageContentRoot::SCRIPTS:
        return "scripts";
    case PackageContentRoot::UI:
        return "ui";
    case PackageContentRoot::GAMEPLAY:
        return "gameplay";
    case PackageContentRoot::SETTINGS:
        return "settings";
    case PackageContentRoot::TRACK_EDITOR:
        return "trackEditor";
    case PackageContentRoot::VEHICLE_GROUPS:
        return "vehicleGroups";
    case PackageContentRoot::UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

const char* ManifestErrorCodeToString(ManifestErrorCode code)
{
    switch (code)
    {
    case ManifestErrorCode::NONE:
        return "none";
    case ManifestErrorCode::EMPTY_PACKAGE:
        return "empty-package";
    case ManifestErrorCode::ENTRY_COUNT_LIMIT:
        return "entry-count-limit";
    case ManifestErrorCode::EMPTY_PATH:
        return "empty-path";
    case ManifestErrorCode::EMBEDDED_NUL:
        return "embedded-nul";
    case ManifestErrorCode::NON_ASCII_PATH:
        return "non-ascii-path";
    case ManifestErrorCode::ABSOLUTE_PATH:
        return "absolute-path";
    case ManifestErrorCode::BACKSLASH:
        return "backslash";
    case ManifestErrorCode::PARENT_TRAVERSAL:
        return "parent-traversal";
    case ManifestErrorCode::EMPTY_NORMALIZED_PATH:
        return "empty-normalized-path";
    case ManifestErrorCode::INVALID_PATH_CHARACTER:
        return "invalid-path-character";
    case ManifestErrorCode::WINDOWS_RESERVED_NAME:
        return "windows-reserved-name";
    case ManifestErrorCode::TRAILING_DOT_OR_SPACE:
        return "trailing-dot-or-space";
    case ManifestErrorCode::PATH_LENGTH_LIMIT:
        return "path-length-limit";
    case ManifestErrorCode::PATH_DEPTH_LIMIT:
        return "path-depth-limit";
    case ManifestErrorCode::SYMLINK:
        return "symlink";
    case ManifestErrorCode::UNSUPPORTED_ENTRY_TYPE:
        return "unsupported-entry-type";
    case ManifestErrorCode::ENCRYPTED_ENTRY:
        return "encrypted-entry";
    case ManifestErrorCode::DIRECTORY_SIZE_MISMATCH:
        return "directory-size-mismatch";
    case ManifestErrorCode::DIRECTORY_MARKER_MISMATCH:
        return "directory-marker-mismatch";
    case ManifestErrorCode::ENTRY_SIZE_LIMIT:
        return "entry-size-limit";
    case ManifestErrorCode::TOTAL_SIZE_LIMIT:
        return "total-size-limit";
    case ManifestErrorCode::COMPRESSION_RATIO_LIMIT:
        return "compression-ratio-limit";
    case ManifestErrorCode::DUPLICATE_ENTRY:
        return "duplicate-entry";
    case ManifestErrorCode::NORMALIZATION_COLLISION:
        return "normalization-collision";
    case ManifestErrorCode::CASE_COLLISION:
        return "case-collision";
    case ManifestErrorCode::PATH_PREFIX_COLLISION:
        return "path-prefix-collision";
    }
    return "unknown";
}

SourceOrigin::SourceOrigin() :
    row(0),
    line(0),
    column(0)
{
}

FeatureAssessment::FeatureAssessment() :
    status(CompatibilityStatus::UNSUPPORTED)
{
}

CompatibilityDiagnostic::CompatibilityDiagnostic() :
    severity(DiagnosticSeverity::INFO)
{
}

CompatibilityReport::CompatibilityReport() :
    schema_version(1)
{
}

std::string SerializeCanonicalCompatibilityReport(
    const CompatibilityReport& report)
{
    std::vector<FeatureAssessment> features = report.features;
    std::vector<CompatibilityDiagnostic> diagnostics = report.diagnostics;
    std::sort(features.begin(), features.end(), FeatureAssessmentLess);
    std::sort(
        diagnostics.begin(),
        diagnostics.end(),
        CompatibilityDiagnosticLess);

    std::ostringstream output;
    output << "ror-beamng-compatibility-report-v"
           << report.schema_version << '\n';
    output << "format-profile\t";
    WriteLengthPrefixed(output, report.format_profile.identifier);
    output << '\t';
    WriteLengthPrefixed(output, report.format_profile.version);
    output << '\n';
    output << "package-identity\t";
    WriteLengthPrefixed(output, report.package_identity);
    output << '\n';
    output << "importer-version\t";
    WriteLengthPrefixed(output, report.importer_version);
    output << '\n';
    output << "configuration\t";
    WriteLengthPrefixed(output, report.configuration);
    output << '\n';

    for (std::vector<FeatureAssessment>::const_iterator it =
             features.begin();
         it != features.end();
         ++it)
    {
        output << "feature\t";
        WriteLengthPrefixed(output, it->feature_id);
        output << '\t' << CompatibilityStatusToString(it->status) << '\t';
        WriteLengthPrefixed(output, it->reason);
        output << '\t';
        WriteLengthPrefixed(output, it->source.package_path);
        output << '\t';
        WriteLengthPrefixed(output, it->source.section);
        output << '\t'
               << it->source.row << '\t'
               << it->source.line << '\t'
               << it->source.column << '\n';
    }

    for (std::vector<CompatibilityDiagnostic>::const_iterator it =
             diagnostics.begin();
         it != diagnostics.end();
         ++it)
    {
        output << "diagnostic\t";
        WriteLengthPrefixed(output, it->code);
        output << '\t' << DiagnosticSeverityToString(it->severity) << '\t';
        WriteLengthPrefixed(output, it->message);
        output << '\t';
        WriteLengthPrefixed(output, it->source.package_path);
        output << '\t';
        WriteLengthPrefixed(output, it->source.section);
        output << '\t'
               << it->source.row << '\t'
               << it->source.line << '\t'
               << it->source.column << '\n';
    }
    return output.str();
}

const char* CompatibilityStatusToString(CompatibilityStatus status)
{
    switch (status)
    {
    case CompatibilityStatus::NATIVE:
        return "native";
    case CompatibilityStatus::APPROXIMATED:
        return "approximated";
    case CompatibilityStatus::PRESERVED_BUT_DISABLED:
        return "preserved-but-disabled";
    case CompatibilityStatus::UNSUPPORTED:
        return "unsupported";
    case CompatibilityStatus::REJECTED:
        return "rejected";
    }
    return "rejected";
}

const char* DiagnosticSeverityToString(DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::INFO:
        return "info";
    case DiagnosticSeverity::WARNING:
        return "warning";
    case DiagnosticSeverity::ERROR_SEVERITY:
        return "error";
    }
    return "error";
}

} // namespace BeamNG
} // namespace RoR
