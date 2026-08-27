/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "JBeamVehicleImporter.h"

#include "BeamNGZipArchiveIndex.h"
#include "JBeamAdvancedStructureIR.h"
#include "JBeamPartResolver.h"
#include "JBeamStructuralIR.h"
#include "JBeamSyntax.h"
#include "JBeamToRigDef.h"
#include "JBeamWheel2Approximation.h"
#include "resources/rig_def_fileformat/RigDef_File.h"

#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace RoR {
namespace BeamNG {
namespace {

const std::string EMPTY_STRING;

std::string LowercaseHex(const unsigned char* bytes, std::size_t size)
{
    static const char HEX[] = "0123456789abcdef";
    std::string output;
    output.resize(size * 2U);
    for (std::size_t i = 0U; i < size; ++i)
    {
        output[i * 2U] = HEX[(bytes[i] >> 4U) & 0x0fU];
        output[i * 2U + 1U] = HEX[bytes[i] & 0x0fU];
    }
    return output;
}

std::string Sha256(const std::string& bytes)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_Digest(
            bytes.data(),
            bytes.size(),
            digest.data(),
            &digest_size,
            EVP_sha256(),
            nullptr) != 1 ||
        digest_size != 32U)
    {
        return std::string();
    }
    return LowercaseHex(digest.data(), digest_size);
}

bool HasJBeamSuffix(const std::string& path)
{
    constexpr char SUFFIX[] = ".jbeam";
    constexpr std::size_t SUFFIX_SIZE = sizeof(SUFFIX) - 1U;
    return path.size() > SUFFIX_SIZE &&
        path.compare(path.size() - SUFFIX_SIZE, SUFFIX_SIZE, SUFFIX) == 0;
}

bool HasConfigurationSuffix(const std::string& path)
{
    constexpr char SUFFIX[] = ".pc";
    constexpr std::size_t SUFFIX_SIZE = sizeof(SUFFIX) - 1U;
    return path.size() > SUFFIX_SIZE &&
        path.compare(path.size() - SUFFIX_SIZE, SUFFIX_SIZE, SUFFIX) == 0;
}

bool IsCanonicalConfigurationPath(const std::string& path)
{
    if (!HasConfigurationSuffix(path) || path.empty() ||
        path.front() == '/' || path.back() == '/')
    {
        return false;
    }
    std::size_t component_begin = 0U;
    for (std::size_t i = 0U; i <= path.size(); ++i)
    {
        if (i != path.size())
        {
            const unsigned char byte =
                static_cast<unsigned char>(path[i]);
            if (byte == 0U || byte < 0x20U || byte == 0x7fU ||
                path[i] == '\\')
            {
                return false;
            }
            if (path[i] != '/')
            {
                continue;
            }
        }
        const std::size_t component_size = i - component_begin;
        if (component_size == 0U ||
            (component_size == 1U && path[component_begin] == '.') ||
            (component_size == 2U && path[component_begin] == '.' &&
             path[component_begin + 1U] == '.'))
        {
            return false;
        }
        component_begin = i + 1U;
    }
    return true;
}

std::string DeriveVehicleDirectoryRoot(const std::string& package_path)
{
    constexpr char VEHICLES_PREFIX[] = "vehicles/";
    constexpr std::size_t VEHICLES_PREFIX_SIZE =
        sizeof(VEHICLES_PREFIX) - 1U;
    if (package_path.size() <= VEHICLES_PREFIX_SIZE ||
        package_path.compare(
            0U,
            VEHICLES_PREFIX_SIZE,
            VEHICLES_PREFIX) != 0)
    {
        return std::string();
    }
    const std::size_t vehicle_end =
        package_path.find('/', VEHICLES_PREFIX_SIZE);
    if (vehicle_end == std::string::npos ||
        vehicle_end == VEHICLES_PREFIX_SIZE)
    {
        return std::string();
    }
    return package_path.substr(0U, vehicle_end + 1U);
}

bool SharesVehicleDirectoryRoot(
    const std::string& configuration_path,
    const std::string& vehicle_directory_root)
{
    return !vehicle_directory_root.empty() &&
        configuration_path.size() > vehicle_directory_root.size() &&
        configuration_path.compare(
            0U,
            vehicle_directory_root.size(),
            vehicle_directory_root) == 0;
}

void AppendLengthPrefixed(
    std::ostringstream& output,
    const std::string& value)
{
    output << value.size() << ':' << value;
}

bool AppendCanonicalConfigurationValue(
    std::ostringstream& output,
    const JBeamValue& value)
{
    switch (value.type)
    {
    case JBeamValueType::BOOLEAN:
        output << (value.boolean_value ? "b1" : "b0");
        return true;
    case JBeamValueType::NUMBER:
        output << 'd';
        AppendLengthPrefixed(output, value.scalar_text);
        return true;
    case JBeamValueType::STRING:
        output << 's';
        AppendLengthPrefixed(output, value.scalar_text);
        return true;
    case JBeamValueType::NULL_VALUE:
    case JBeamValueType::ARRAY:
    case JBeamValueType::OBJECT:
        return false;
    }
    return false;
}

std::string SerializeCanonicalResolveRequest(
    const JBeamResolveRequest& request)
{
    std::ostringstream output;
    output << "ror-beamng-resolve-request-v1\nroot\t";
    AppendLengthPrefixed(output, request.root_part_name);
    output << "\nselections\t" << request.part_selections.size() << '\n';
    for (const JBeamPartSelection& selection : request.part_selections)
    {
        output << "Q\t";
        AppendLengthPrefixed(output, selection.slot_name);
        output << '\t';
        AppendLengthPrefixed(output, selection.part_name);
        output << '\n';
    }
    output << "variables\t" << request.variables.size() << '\n';
    for (const JBeamVariableAssignment& variable : request.variables)
    {
        output << "V\t";
        AppendLengthPrefixed(output, variable.name);
        output << '\t';
        if (!AppendCanonicalConfigurationValue(output, variable.value))
        {
            return std::string();
        }
        output << '\n';
    }
    return output.str();
}

bool IsConfigurationDocumentSubset(const JBeamValue& configuration)
{
    if (configuration.type != JBeamValueType::OBJECT)
    {
        return false;
    }
    for (const JBeamObjectField& field : configuration.object_fields)
    {
        if (field.key != "parts" && field.key != "vars")
        {
            return false;
        }
    }
    return true;
}

bool HasAsciiCaseInsensitiveSuffix(
    const std::string& path,
    const char* suffix)
{
    const std::size_t suffix_size = std::strlen(suffix);
    if (suffix_size == 0U || path.size() <= suffix_size)
    {
        return false;
    }
    const std::size_t offset = path.size() - suffix_size;
    for (std::size_t index = 0U; index < suffix_size; ++index)
    {
        const unsigned char left = static_cast<unsigned char>(
            path[offset + index]);
        const unsigned char right = static_cast<unsigned char>(suffix[index]);
        const unsigned char normalized_left = left >= 'A' && left <= 'Z'
            ? static_cast<unsigned char>(left - 'A' + 'a')
            : left;
        const unsigned char normalized_right = right >= 'A' && right <= 'Z'
            ? static_cast<unsigned char>(right - 'A' + 'a')
            : right;
        if (normalized_left != normalized_right)
        {
            return false;
        }
    }
    return true;
}

bool IsUnsafeOgreScriptMember(const ZipArchiveEntry& entry)
{
    if (entry.kind != PackageEntryKind::REGULAR_FILE)
    {
        return false;
    }
    static const char* const SUFFIXES[] = {
        ".material",
        ".program",
        ".compositor",
        ".particle",
        ".overlay",
        ".fontdef",
        ".os"
    };
    for (const char* suffix : SUFFIXES)
    {
        if (HasAsciiCaseInsensitiveSuffix(entry.path, suffix))
        {
            return true;
        }
    }
    return false;
}

bool AddWithinLimit(
    std::size_t current,
    std::size_t addition,
    std::size_t limit,
    std::size_t& output)
{
    if (current > limit || addition > limit - current)
    {
        output = 0U;
        return false;
    }
    output = current + addition;
    return true;
}

bool DecodeMember(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const ZipArchiveEntry& entry,
    const JBeamVehicleImportLimits& limits,
    std::string& output,
    std::string& detail)
{
    output.clear();
    detail.clear();
    if (entry.kind != PackageEntryKind::REGULAR_FILE ||
        entry.expanded_size == 0U ||
        entry.expanded_size > limits.max_member_bytes ||
        entry.expanded_size >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)()) ||
        entry.compressed_size >
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::size_t>::max)()) ||
        entry.data_offset > snapshot.size() ||
        entry.compressed_size > snapshot.size() -
            static_cast<std::size_t>(entry.data_offset))
    {
        detail = "JBeam member kind, size, or payload bounds are invalid";
        return false;
    }

    const std::size_t expanded =
        static_cast<std::size_t>(entry.expanded_size);
    const std::size_t compressed =
        static_cast<std::size_t>(entry.compressed_size);
    const std::uint8_t* const source = snapshot.bytes() +
        static_cast<std::size_t>(entry.data_offset);
    output.resize(expanded);

    if (entry.compression_method == 0U)
    {
        if (compressed != expanded)
        {
            detail = "Stored JBeam member size mismatch";
            return false;
        }
        std::memcpy(&output[0], source, expanded);
    }
    else if (entry.compression_method == 8U)
    {
        if (compressed > (std::numeric_limits<uInt>::max)() ||
            expanded > (std::numeric_limits<uInt>::max)())
        {
            detail = "Deflated JBeam member exceeds zlib input bounds";
            return false;
        }
        z_stream stream;
        std::memset(&stream, 0, sizeof(stream));
        stream.next_in = const_cast<Bytef*>(
            reinterpret_cast<const Bytef*>(source));
        stream.avail_in = static_cast<uInt>(compressed);
        stream.next_out = reinterpret_cast<Bytef*>(&output[0]);
        stream.avail_out = static_cast<uInt>(expanded);
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        {
            detail = "Could not initialize raw-DEFLATE decoder";
            return false;
        }
        const int inflate_result = inflate(&stream, Z_FINISH);
        const bool exact = inflate_result == Z_STREAM_END &&
            stream.total_in == compressed &&
            stream.total_out == expanded &&
            stream.avail_in == 0U && stream.avail_out == 0U;
        const int end_result = inflateEnd(&stream);
        if (!exact || end_result != Z_OK)
        {
            detail = "Raw-DEFLATE JBeam member did not decode exactly";
            return false;
        }
    }
    else
    {
        detail = "JBeam member uses an unsupported compression method";
        return false;
    }

    const uLong observed_crc = ::crc32(
        ::crc32(0L, Z_NULL, 0),
        reinterpret_cast<const Bytef*>(output.data()),
        static_cast<uInt>(output.size()));
    if (observed_crc != entry.crc32)
    {
        detail = "Decoded JBeam member CRC32 mismatch";
        output.clear();
        return false;
    }
    return true;
}

struct ParsedPackage
{
    JBeamVehicleImportCode code =
        JBeamVehicleImportCode::INVALID_ARCHIVE_AUTHORITY;
    std::string detail;
    std::string package_index_sha256;
    std::size_t retained_jbeam_bytes = 0U;
    std::vector<JBeamPackageSource> sources;
    JBeamPackageIndex index;
};

ParsedPackage ParsePackage(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const JBeamVehicleImportLimits& limits,
    ZipArchiveIndex* authenticated_index = nullptr)
{
    ParsedPackage result;
    if (!snapshot.initialized() || snapshot.bytes() == nullptr ||
        snapshot.size() == 0U || snapshot.archive_sha256().size() != 64U)
    {
        result.detail = "Importer requires one initialized immutable archive";
        return result;
    }

    const ZipArchiveIndexResult archive = BuildBeamNGZipArchiveIndex(
        snapshot.bytes(), snapshot.size());
    if (!archive.IsValid())
    {
        result.code = JBeamVehicleImportCode::ARCHIVE_INDEX_REJECTED;
        result.detail = std::string("ZIP index rejected: ") +
            ZipArchiveIndexErrorCodeToString(archive.error.code);
        return result;
    }
    if (authenticated_index != nullptr)
    {
        *authenticated_index = archive.index;
    }

    for (const ZipArchiveEntry& entry : archive.index.entries)
    {
        if (IsUnsafeOgreScriptMember(entry))
        {
            result.code =
                JBeamVehicleImportCode::UNSAFE_OGRE_SCRIPT_MEMBER;
            result.detail =
                "Package contains an unsupported auto-executable OGRE "
                "script member: " + entry.path;
            return result;
        }
    }

    for (std::size_t i = 0U; i < archive.index.entries.size(); ++i)
    {
        const ZipArchiveEntry& entry = archive.index.entries[i];
        if (!HasJBeamSuffix(entry.path))
        {
            continue;
        }
        if (result.sources.size() >= limits.max_jbeam_members)
        {
            result.code = JBeamVehicleImportCode::JBEAM_MEMBER_LIMIT;
            result.detail = "Archive exceeds the JBeam member-count limit";
            result.sources.clear();
            return result;
        }
        std::size_t total = 0U;
        if (entry.expanded_size >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)()) ||
            !AddWithinLimit(
                result.retained_jbeam_bytes,
                static_cast<std::size_t>(entry.expanded_size),
                limits.max_total_jbeam_bytes,
                total))
        {
            result.code = JBeamVehicleImportCode::JBEAM_MEMBER_LIMIT;
            result.detail = "Archive exceeds the total JBeam byte limit";
            result.sources.clear();
            return result;
        }

        std::string decoded;
        if (!DecodeMember(snapshot, entry, limits, decoded, result.detail))
        {
            result.code =
                JBeamVehicleImportCode::JBEAM_MEMBER_DECODE_REJECTED;
            result.detail = entry.path + ": " + result.detail;
            result.sources.clear();
            return result;
        }
        const JBeamParseResult parsed = ParseJBeam(decoded, entry.path);
        if (!parsed.IsValid())
        {
            result.code = JBeamVehicleImportCode::JBEAM_PARSE_REJECTED;
            result.detail = entry.path + ": relaxed-JBeam parse rejected";
            result.sources.clear();
            return result;
        }
        JBeamPackageSource source;
        source.package_path = entry.path;
        source.document = parsed.root;
        result.sources.push_back(std::move(source));
        result.retained_jbeam_bytes = total;
    }
    if (result.sources.empty())
    {
        result.code = JBeamVehicleImportCode::NO_MAIN_PART;
        result.detail = "Archive contains no canonical .jbeam members";
        return result;
    }

    result.index = BuildJBeamPackageIndex(result.sources);
    if (!result.index.IsValid())
    {
        result.code = JBeamVehicleImportCode::PACKAGE_INDEX_REJECTED;
        result.detail = "JBeam package index rejected";
        result.sources.clear();
        return result;
    }
    const std::string canonical_index =
        SerializeCanonicalJBeamPackageIndex(result.index);
    result.package_index_sha256 = Sha256(canonical_index);
    if (result.package_index_sha256.empty())
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Could not hash canonical JBeam package index";
        result.sources.clear();
        return result;
    }
    result.code = JBeamVehicleImportCode::ADMITTED;
    return result;
}

bool IsAllowedActiveField(const std::string& field)
{
    return field == "slotType" || field == "slots" ||
        field == "slots2" || field == "variables" ||
        field == "information" || field == "refNodes" ||
        field == "nodes" || field == "beams" ||
        field == "triangles" || field == "hydros" ||
        field == "pressureWheels";
}

bool ValidateActiveSections(
    const std::shared_ptr<JBeamResolvedPartNode>& node,
    std::string& detail)
{
    if (!node)
    {
        detail = "Resolved part tree contains a null node";
        return false;
    }
    for (std::size_t i = 0U;
         i < node->definition.body.object_fields.size();
         ++i)
    {
        const std::string& field =
            node->definition.body.object_fields[i].key;
        if (!IsAllowedActiveField(field))
        {
            detail = "Unsupported active section '" + field +
                "' in part '" + node->definition.name + "'";
            return false;
        }
    }
    for (std::size_t i = 0U; i < node->slots.size(); ++i)
    {
        if (node->slots[i].child &&
            !ValidateActiveSections(node->slots[i].child, detail))
        {
            return false;
        }
    }
    return true;
}

bool ValidateExplicitSelections(
    const std::shared_ptr<JBeamResolvedPartNode>& node,
    std::string& detail)
{
    if (!node)
    {
        detail = "Configured resolved part tree contains a null node";
        return false;
    }
    for (const JBeamResolvedSlot& slot : node->slots)
    {
        if (slot.explicitly_selected &&
            slot.status != JBeamResolvedSlotStatus::RESOLVED &&
            !(slot.selected_part.empty() &&
              slot.status == JBeamResolvedSlotStatus::EMPTY))
        {
            detail = "Explicit configuration selection for slot '" +
                slot.definition.name + "' did not resolve exactly";
            return false;
        }
        if (slot.child &&
            !ValidateExplicitSelections(slot.child, detail))
        {
            return false;
        }
    }
    return true;
}

bool ValidateConfiguredGraph(
    const JBeamResolvedGraph& graph,
    std::string& detail)
{
    for (const JBeamResolveDiagnostic& diagnostic : graph.diagnostics)
    {
        if (diagnostic.code ==
            JBeamResolveDiagnosticCode::UNUSED_PART_SELECTION)
        {
            detail = "Explicit configuration selection did not match a "
                "resolved slot";
            return false;
        }
    }
    return ValidateExplicitSelections(graph.root, detail);
}

std::vector<JBeamVehicleCandidate> MainCandidates(
    const JBeamPackageIndex& index)
{
    std::vector<JBeamVehicleCandidate> candidates;
    for (std::size_t i = 0U; i < index.parts.size(); ++i)
    {
        const JBeamPartDefinition& part = index.parts[i];
        if (std::find(
                part.slot_types.begin(),
                part.slot_types.end(),
                "main") == part.slot_types.end())
        {
            continue;
        }
        JBeamVehicleCandidate candidate;
        candidate.root_part_name = part.name;
        candidate.package_path = part.package_path;
        candidates.push_back(std::move(candidate));
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const JBeamVehicleCandidate& left,
           const JBeamVehicleCandidate& right)
        {
            if (left.root_part_name != right.root_part_name)
            {
                return left.root_part_name < right.root_part_name;
            }
            return left.package_path < right.package_path;
        });
    return candidates;
}

struct ParsedConfiguration
{
    JBeamVehicleImportCode code =
        JBeamVehicleImportCode::CONFIGURATION_PATH_REJECTED;
    std::string detail;
    std::string configuration_sha256;
    std::string resolve_request_sha256;
    JBeamResolveRequest request;
};

ParsedConfiguration ParseExplicitConfiguration(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const ZipArchiveIndex& archive,
    const std::string& configuration_path,
    const std::string& vehicle_directory_root,
    const std::string& root_part_name,
    const JBeamVehicleImportLimits& limits)
{
    ParsedConfiguration result;
    if (!IsCanonicalConfigurationPath(configuration_path))
    {
        result.detail =
            "Configuration path must be one exact canonical lowercase .pc "
            "archive member";
        return result;
    }
    if (!SharesVehicleDirectoryRoot(
            configuration_path, vehicle_directory_root))
    {
        result.detail =
            "Configuration path must remain under the selected main "
            "part's exact vehicles/<vehicle>/ directory root";
        return result;
    }

    const ZipArchiveEntry* entry = nullptr;
    for (const ZipArchiveEntry& candidate : archive.entries)
    {
        if (candidate.path != configuration_path)
        {
            continue;
        }
        if (entry != nullptr)
        {
            result.code =
                JBeamVehicleImportCode::CONFIGURATION_PATH_REJECTED;
            result.detail = "Configuration path is not unique";
            return result;
        }
        entry = &candidate;
    }
    if (entry == nullptr)
    {
        result.code =
            JBeamVehicleImportCode::CONFIGURATION_MEMBER_NOT_FOUND;
        result.detail =
            "Requested configuration is not an exact archive member";
        return result;
    }

    if (limits.max_configuration_bytes == 0U ||
        entry->expanded_size > limits.max_configuration_bytes)
    {
        result.code =
            JBeamVehicleImportCode::CONFIGURATION_MEMBER_DECODE_REJECTED;
        result.detail = "Configuration member exceeds its byte limit";
        return result;
    }
    JBeamVehicleImportLimits configuration_limits = limits;
    configuration_limits.max_member_bytes = std::min(
        limits.max_member_bytes, limits.max_configuration_bytes);
    std::string decoded;
    if (!DecodeMember(
            snapshot,
            *entry,
            configuration_limits,
            decoded,
            result.detail))
    {
        result.code =
            JBeamVehicleImportCode::CONFIGURATION_MEMBER_DECODE_REJECTED;
        result.detail = configuration_path + ": " + result.detail;
        return result;
    }
    result.configuration_sha256 = Sha256(decoded);
    if (result.configuration_sha256.empty())
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Could not hash exact configuration bytes";
        return result;
    }

    const JBeamParseResult parsed = ParseJBeam(decoded, configuration_path);
    if (!parsed.IsValid())
    {
        result.code = JBeamVehicleImportCode::CONFIGURATION_PARSE_REJECTED;
        result.detail = "Explicit .pc relaxed-JBeam parse rejected";
        return result;
    }
    if (!IsConfigurationDocumentSubset(parsed.root))
    {
        result.code =
            JBeamVehicleImportCode::CONFIGURATION_REQUEST_REJECTED;
        result.detail =
            "Explicit .pc contains content outside inert parts/vars data";
        return result;
    }
    const JBeamConfigurationResult configuration =
        ParseJBeamConfiguration(parsed.root);
    if (!configuration.IsValid())
    {
        result.code =
            JBeamVehicleImportCode::CONFIGURATION_REQUEST_REJECTED;
        result.detail = "Explicit .pc parts/vars request rejected";
        return result;
    }
    result.request = configuration.request;
    result.request.root_part_name = root_part_name;
    const std::string canonical_request =
        SerializeCanonicalResolveRequest(result.request);
    result.resolve_request_sha256 = Sha256(canonical_request);
    if (canonical_request.empty() || result.resolve_request_sha256.empty())
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Could not hash canonical resolve request";
        return result;
    }
    result.code = JBeamVehicleImportCode::ADMITTED;
    return result;
}

struct PreparedVehicleImport
{
    JBeamVehicleImportCode code =
        JBeamVehicleImportCode::INVALID_ARCHIVE_AUTHORITY;
    std::string detail;
    RigDef::DocumentPtr document;
    std::string package_index_sha256;
    std::string resolved_graph_sha256;
    std::string wheel2_plan_sha256;
    std::size_t wheel2_plan_count = 0U;
    std::uint32_t wheel2_approximated_semantics = 0U;
    std::size_t jbeam_member_count = 0U;
    std::size_t retained_jbeam_bytes = 0U;
};

PreparedVehicleImport PrepareVehicleImport(
    ParsedPackage parsed,
    const std::string& root_part_name,
    const JBeamResolveRequest& request)
{
    PreparedVehicleImport result;
    result.code = parsed.code;
    result.detail = parsed.detail;
    if (parsed.code != JBeamVehicleImportCode::ADMITTED)
    {
        return result;
    }

    const std::vector<JBeamVehicleCandidate> candidates =
        MainCandidates(parsed.index);
    if (std::find_if(
            candidates.begin(),
            candidates.end(),
            [&](const JBeamVehicleCandidate& candidate)
            {
                return candidate.root_part_name == root_part_name;
            }) == candidates.end())
    {
        result.code = JBeamVehicleImportCode::ROOT_PART_NOT_FOUND;
        result.detail = "Requested root is not an exact slotType main part";
        return result;
    }

    const JBeamResolvedGraph graph =
        ResolveJBeamPartGraph(parsed.index, request);
    if (!graph.IsValid())
    {
        result.code = JBeamVehicleImportCode::PART_RESOLUTION_REJECTED;
        result.detail = "Selected JBeam part graph did not resolve";
        return result;
    }
    if (!request.part_selections.empty() &&
        !ValidateConfiguredGraph(graph, result.detail))
    {
        result.code = JBeamVehicleImportCode::PART_RESOLUTION_REJECTED;
        return result;
    }
    if (!ValidateActiveSections(graph.root, result.detail))
    {
        result.code = JBeamVehicleImportCode::UNSUPPORTED_ACTIVE_SECTION;
        return result;
    }

    const JBeamStructuralIR structural = BuildJBeamStructuralIR(graph);
    if (!structural.IsValid())
    {
        result.code = JBeamVehicleImportCode::STRUCTURAL_IR_REJECTED;
        result.detail =
            "Resolved graph did not produce a valid structural IR";
        return result;
    }
    const JBeamHydroRuntimePlanSet hydro_plans =
        BuildJBeamHydroRuntimePlanSet(graph);
    if (!hydro_plans.IsAdmitted())
    {
        result.code = JBeamVehicleImportCode::HYDRO_PLAN_REJECTED;
        result.detail = std::string("Hydro plan set rejected: ") +
            JBeamHydroRuntimePlanSetCodeToString(hydro_plans.code);
        return result;
    }
    const JBeamWheel2ApproximationPlanSet wheel_plans =
        BuildJBeamWheel2ApproximationPlanSet(graph);
    if (!wheel_plans.IsAdmitted())
    {
        result.code = JBeamVehicleImportCode::WHEEL2_PLAN_REJECTED;
        result.detail = std::string("Wheel2 plan set rejected: ") +
            JBeamWheel2ApproximationCodeToString(wheel_plans.code);
        return result;
    }
    const std::string canonical_wheel_plans =
        SerializeCanonicalJBeamWheel2ApproximationPlanSet(wheel_plans);
    result.wheel2_plan_sha256 = Sha256(canonical_wheel_plans);
    if (canonical_wheel_plans.empty() ||
        result.wheel2_plan_sha256.empty())
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Could not hash canonical Wheel2 plans";
        return result;
    }

    std::vector<JBeamToRigDefDiagnostic> diagnostics;
    result.document = ConvertJBeamToRigDefWithRuntimePlans(
        structural,
        hydro_plans,
        wheel_plans,
        root_part_name,
        diagnostics);
    if (!result.document || !diagnostics.empty())
    {
        result.code = JBeamVehicleImportCode::RIGDEF_CONVERSION_REJECTED;
        result.detail = diagnostics.empty()
            ? "RigDef conversion returned no document"
            : std::string("RigDef conversion rejected: ") +
                ToString(diagnostics[0].code);
        result.document.reset();
        return result;
    }

    const std::string canonical_graph =
        SerializeCanonicalJBeamResolvedGraph(graph);
    result.resolved_graph_sha256 = Sha256(canonical_graph);
    if (result.resolved_graph_sha256.empty())
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Could not hash canonical resolved graph";
        result.document.reset();
        return result;
    }

    result.package_index_sha256 = parsed.package_index_sha256;
    result.wheel2_plan_count = wheel_plans.plans.size();
    result.wheel2_approximated_semantics = wheel_plans.plans.empty()
        ? 0U
        : JBEAM_WHEEL2_APPROXIMATION_SEMANTICS;
    result.jbeam_member_count = parsed.sources.size();
    result.retained_jbeam_bytes = parsed.retained_jbeam_bytes;
    result.code = JBeamVehicleImportCode::ADMITTED;
    return result;
}

} // namespace

struct JBeamVehicleImportAuthorityReceipt::State
{
    TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::uint32_t authority_version = 0U;
    std::string resource_group;
    std::string root_part_name;
    std::string package_index_sha256;
    std::string resolved_graph_sha256;
    std::string configuration_path;
    std::string configuration_sha256;
    std::string resolve_request_sha256;
    std::string wheel2_plan_sha256;
    std::size_t wheel2_plan_count = 0U;
    std::uint32_t wheel2_approximated_semantics = 0U;
    std::size_t jbeam_member_count = 0U;
    std::size_t retained_jbeam_bytes = 0U;
};

JBeamVehicleImportAuthorityReceipt::JBeamVehicleImportAuthorityReceipt(
    std::shared_ptr<const State> state) noexcept:
    m_state(std::move(state))
{
}

bool JBeamVehicleImportAuthorityReceipt::initialized() const noexcept
{
    if (m_state == nullptr || !m_state->snapshot.initialized())
    {
        return false;
    }
    const bool base = !m_state->resource_group.empty() &&
        !m_state->root_part_name.empty() &&
        m_state->package_index_sha256.size() == 64U &&
        m_state->resolved_graph_sha256.size() == 64U &&
        m_state->wheel2_plan_sha256.size() == 64U &&
        m_state->jbeam_member_count != 0U &&
        m_state->retained_jbeam_bytes != 0U;
    if (!base)
    {
        return false;
    }
    if (m_state->authority_version ==
        JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION)
    {
        return m_state->configuration_path.empty() &&
            m_state->configuration_sha256.empty() &&
            m_state->resolve_request_sha256.empty();
    }
    return m_state->authority_version ==
            JBEAM_CONFIGURED_VEHICLE_IMPORT_AUTHORITY_VERSION &&
        !m_state->configuration_path.empty() &&
        m_state->configuration_sha256.size() == 64U &&
        m_state->resolve_request_sha256.size() == 64U;
}

std::uint32_t JBeamVehicleImportAuthorityReceipt::version() const noexcept
{
    return initialized() ? m_state->authority_version : 0U;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::resource_group() const noexcept
{
    return m_state ? m_state->resource_group : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::root_part_name() const noexcept
{
    return m_state ? m_state->root_part_name : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::archive_sha256() const noexcept
{
    return m_state ? m_state->snapshot.archive_sha256() : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::package_index_sha256() const noexcept
{
    return m_state ? m_state->package_index_sha256 : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::resolved_graph_sha256() const noexcept
{
    return m_state ? m_state->resolved_graph_sha256 : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::configuration_path() const noexcept
{
    return m_state ? m_state->configuration_path : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::configuration_sha256() const noexcept
{
    return m_state ? m_state->configuration_sha256 : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::resolve_request_sha256() const noexcept
{
    return m_state ? m_state->resolve_request_sha256 : EMPTY_STRING;
}

const std::string&
JBeamVehicleImportAuthorityReceipt::wheel2_plan_sha256() const noexcept
{
    return m_state ? m_state->wheel2_plan_sha256 : EMPTY_STRING;
}

std::size_t
JBeamVehicleImportAuthorityReceipt::wheel2_plan_count() const noexcept
{
    return m_state ? m_state->wheel2_plan_count : 0U;
}

std::uint32_t
JBeamVehicleImportAuthorityReceipt::wheel2_approximated_semantics() const
    noexcept
{
    return m_state ? m_state->wheel2_approximated_semantics : 0U;
}

std::size_t
JBeamVehicleImportAuthorityReceipt::jbeam_member_count() const noexcept
{
    return m_state ? m_state->jbeam_member_count : 0U;
}

std::size_t
JBeamVehicleImportAuthorityReceipt::retained_jbeam_bytes() const noexcept
{
    return m_state ? m_state->retained_jbeam_bytes : 0U;
}

const TerrainBundleAuthenticatedArchiveSnapshot*
JBeamVehicleImportAuthorityReceipt::authenticated_archive_snapshot() const
    noexcept
{
    return m_state ? &m_state->snapshot : nullptr;
}

bool JBeamVehicleImportAuthorityReceipt::Matches(
    const std::string& expected_resource_group,
    const std::string& expected_root_part,
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot) const noexcept
{
    return initialized() &&
        version() == JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION &&
        expected_resource_group == resource_group() &&
        expected_root_part == root_part_name() && snapshot.initialized() &&
        m_state->snapshot.SharesImmutableStateWith(snapshot) &&
        snapshot.archive_sha256() == archive_sha256() &&
        snapshot.size() == m_state->snapshot.size();
}

bool JBeamVehicleImportAuthorityReceipt::MatchesConfigured(
    const std::string& expected_resource_group,
    const std::string& expected_root_part,
    const std::string& expected_configuration_path,
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot) const noexcept
{
    return initialized() &&
        version() == JBEAM_CONFIGURED_VEHICLE_IMPORT_AUTHORITY_VERSION &&
        expected_resource_group == resource_group() &&
        expected_root_part == root_part_name() &&
        expected_configuration_path == configuration_path() &&
        snapshot.initialized() &&
        m_state->snapshot.SharesImmutableStateWith(snapshot) &&
        snapshot.archive_sha256() == archive_sha256() &&
        snapshot.size() == m_state->snapshot.size();
}

JBeamVehiclePackageInspection InspectJBeamVehicleArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const JBeamVehicleImportLimits& limits)
{
    JBeamVehiclePackageInspection inspection;
    try
    {
        ParsedPackage parsed = ParsePackage(snapshot, limits);
        inspection.code = parsed.code;
        inspection.detail = parsed.detail;
        inspection.archive_sha256 = snapshot.archive_sha256();
        inspection.package_index_sha256 = parsed.package_index_sha256;
        inspection.jbeam_member_count = parsed.sources.size();
        inspection.retained_jbeam_bytes = parsed.retained_jbeam_bytes;
        if (parsed.code != JBeamVehicleImportCode::ADMITTED)
        {
            return inspection;
        }
        inspection.candidates = MainCandidates(parsed.index);
        if (inspection.candidates.empty())
        {
            inspection.code = JBeamVehicleImportCode::NO_MAIN_PART;
            inspection.detail = "Package contains no slotType main part";
            return inspection;
        }
        inspection.code = JBeamVehicleImportCode::ADMITTED;
        return inspection;
    }
    catch (const std::bad_alloc&)
    {
        inspection.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        inspection.detail = "Allocation failed before inspection publication";
        return inspection;
    }
    catch (const std::length_error&)
    {
        inspection.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        inspection.detail = "Inspection allocation exceeded a size limit";
        return inspection;
    }
    catch (...)
    {
        inspection.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        inspection.detail = "Unexpected failure before inspection publication";
        return inspection;
    }
}

JBeamVehicleImportResult ImportJBeamVehicleFromArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const std::string& resource_group,
    const std::string& root_part_name,
    const JBeamVehicleImportLimits& limits)
{
    JBeamVehicleImportResult result;
    try
    {
        if (resource_group.empty() || root_part_name.empty())
        {
            result.detail = "Resource group and root part must be non-empty";
            return result;
        }
        JBeamResolveRequest request;
        request.root_part_name = root_part_name;
        PreparedVehicleImport prepared = PrepareVehicleImport(
            ParsePackage(snapshot, limits), root_part_name, request);
        if (prepared.code != JBeamVehicleImportCode::ADMITTED)
        {
            result.code = prepared.code;
            result.detail = prepared.detail;
            return result;
        }

        std::shared_ptr<JBeamVehicleImportAuthorityReceipt::State> state =
            std::make_shared<
                JBeamVehicleImportAuthorityReceipt::State>();
        state->snapshot = snapshot;
        state->authority_version =
            JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION;
        state->resource_group = resource_group;
        state->root_part_name = root_part_name;
        state->package_index_sha256 = prepared.package_index_sha256;
        state->resolved_graph_sha256 = prepared.resolved_graph_sha256;
        state->wheel2_plan_sha256 = prepared.wheel2_plan_sha256;
        state->wheel2_plan_count = prepared.wheel2_plan_count;
        state->wheel2_approximated_semantics =
            prepared.wheel2_approximated_semantics;
        state->jbeam_member_count = prepared.jbeam_member_count;
        state->retained_jbeam_bytes = prepared.retained_jbeam_bytes;
        const std::shared_ptr<const JBeamVehicleImportAuthorityReceipt>
            authority(new JBeamVehicleImportAuthorityReceipt(
                std::shared_ptr<const
                    JBeamVehicleImportAuthorityReceipt::State>(
                        std::move(state))));
        if (!authority->initialized())
        {
            result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
            result.detail = "Importer could not finalize source authority";
            return result;
        }

        prepared.document->_jbeam_import_authority = authority;
        result.document = std::move(prepared.document);
        result.authority = authority;
        result.code = JBeamVehicleImportCode::ADMITTED;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        result.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        result.detail = "Allocation failed before import publication";
        return result;
    }
    catch (const std::length_error&)
    {
        result.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        result.detail = "Import allocation exceeded a size limit";
        return result;
    }
    catch (...)
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail = "Unexpected failure before import publication";
        return result;
    }
}

JBeamVehicleImportResult ImportConfiguredJBeamVehicleFromArchiveSnapshot(
    const TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const std::string& resource_group,
    const std::string& root_part_name,
    const std::string& configuration_path,
    const JBeamVehicleImportLimits& limits)
{
    JBeamVehicleImportResult result;
    try
    {
        if (resource_group.empty() || root_part_name.empty())
        {
            result.detail = "Resource group and root part must be non-empty";
            return result;
        }
        ZipArchiveIndex archive;
        ParsedPackage parsed = ParsePackage(snapshot, limits, &archive);
        if (parsed.code != JBeamVehicleImportCode::ADMITTED)
        {
            result.code = parsed.code;
            result.detail = parsed.detail;
            return result;
        }
        const std::vector<JBeamVehicleCandidate> candidates =
            MainCandidates(parsed.index);
        const std::vector<JBeamVehicleCandidate>::const_iterator candidate =
            std::find_if(
                candidates.begin(),
                candidates.end(),
                [&](const JBeamVehicleCandidate& value)
                {
                    return value.root_part_name == root_part_name;
                });
        if (candidate == candidates.end())
        {
            result.code = JBeamVehicleImportCode::ROOT_PART_NOT_FOUND;
            result.detail =
                "Requested root is not an exact slotType main part";
            return result;
        }
        const std::string vehicle_directory_root =
            DeriveVehicleDirectoryRoot(candidate->package_path);
        if (vehicle_directory_root.empty())
        {
            result.code =
                JBeamVehicleImportCode::CONFIGURATION_PATH_REJECTED;
            result.detail =
                "Selected main part must be under one exact canonical "
                "vehicles/<vehicle>/ directory root";
            return result;
        }
        const ParsedConfiguration configuration =
            ParseExplicitConfiguration(
                snapshot,
                archive,
                configuration_path,
                vehicle_directory_root,
                root_part_name,
                limits);
        if (configuration.code != JBeamVehicleImportCode::ADMITTED)
        {
            result.code = configuration.code;
            result.detail = configuration.detail;
            return result;
        }

        PreparedVehicleImport prepared = PrepareVehicleImport(
            std::move(parsed), root_part_name, configuration.request);
        if (prepared.code != JBeamVehicleImportCode::ADMITTED)
        {
            result.code = prepared.code;
            result.detail = prepared.detail;
            return result;
        }

        std::shared_ptr<JBeamVehicleImportAuthorityReceipt::State> state =
            std::make_shared<
                JBeamVehicleImportAuthorityReceipt::State>();
        state->snapshot = snapshot;
        state->authority_version =
            JBEAM_CONFIGURED_VEHICLE_IMPORT_AUTHORITY_VERSION;
        state->resource_group = resource_group;
        state->root_part_name = root_part_name;
        state->package_index_sha256 = prepared.package_index_sha256;
        state->resolved_graph_sha256 = prepared.resolved_graph_sha256;
        state->configuration_path = configuration_path;
        state->configuration_sha256 =
            configuration.configuration_sha256;
        state->resolve_request_sha256 =
            configuration.resolve_request_sha256;
        state->wheel2_plan_sha256 = prepared.wheel2_plan_sha256;
        state->wheel2_plan_count = prepared.wheel2_plan_count;
        state->wheel2_approximated_semantics =
            prepared.wheel2_approximated_semantics;
        state->jbeam_member_count = prepared.jbeam_member_count;
        state->retained_jbeam_bytes = prepared.retained_jbeam_bytes;
        const std::shared_ptr<const JBeamVehicleImportAuthorityReceipt>
            authority(new JBeamVehicleImportAuthorityReceipt(
                std::shared_ptr<const
                    JBeamVehicleImportAuthorityReceipt::State>(
                        std::move(state))));
        if (!authority->initialized())
        {
            result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
            result.detail =
                "Configured importer could not finalize source authority";
            return result;
        }

        prepared.document->_jbeam_import_authority = authority;
        result.document = std::move(prepared.document);
        result.authority = authority;
        result.code = JBeamVehicleImportCode::ADMITTED;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        result.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        result.detail =
            "Allocation failed before configured import publication";
        return result;
    }
    catch (const std::length_error&)
    {
        result.code = JBeamVehicleImportCode::ALLOCATION_FAILURE;
        result.detail =
            "Configured import allocation exceeded a size limit";
        return result;
    }
    catch (...)
    {
        result.code = JBeamVehicleImportCode::INTERNAL_FAILURE;
        result.detail =
            "Unexpected failure before configured import publication";
        return result;
    }
}

const char* JBeamVehicleImportCodeToString(JBeamVehicleImportCode code)
{
    switch (code)
    {
    case JBeamVehicleImportCode::ADMITTED:
        return "admitted";
    case JBeamVehicleImportCode::INVALID_ARCHIVE_AUTHORITY:
        return "invalid-archive-authority";
    case JBeamVehicleImportCode::ARCHIVE_INDEX_REJECTED:
        return "archive-index-rejected";
    case JBeamVehicleImportCode::UNSAFE_OGRE_SCRIPT_MEMBER:
        return "unsafe-ogre-script-member";
    case JBeamVehicleImportCode::JBEAM_MEMBER_LIMIT:
        return "jbeam-member-limit";
    case JBeamVehicleImportCode::JBEAM_MEMBER_DECODE_REJECTED:
        return "jbeam-member-decode-rejected";
    case JBeamVehicleImportCode::JBEAM_PARSE_REJECTED:
        return "jbeam-parse-rejected";
    case JBeamVehicleImportCode::PACKAGE_INDEX_REJECTED:
        return "package-index-rejected";
    case JBeamVehicleImportCode::NO_MAIN_PART:
        return "no-main-part";
    case JBeamVehicleImportCode::ROOT_PART_NOT_FOUND:
        return "root-part-not-found";
    case JBeamVehicleImportCode::CONFIGURATION_PATH_REJECTED:
        return "configuration-path-rejected";
    case JBeamVehicleImportCode::CONFIGURATION_MEMBER_NOT_FOUND:
        return "configuration-member-not-found";
    case JBeamVehicleImportCode::CONFIGURATION_MEMBER_DECODE_REJECTED:
        return "configuration-member-decode-rejected";
    case JBeamVehicleImportCode::CONFIGURATION_PARSE_REJECTED:
        return "configuration-parse-rejected";
    case JBeamVehicleImportCode::CONFIGURATION_REQUEST_REJECTED:
        return "configuration-request-rejected";
    case JBeamVehicleImportCode::PART_RESOLUTION_REJECTED:
        return "part-resolution-rejected";
    case JBeamVehicleImportCode::UNSUPPORTED_ACTIVE_SECTION:
        return "unsupported-active-section";
    case JBeamVehicleImportCode::STRUCTURAL_IR_REJECTED:
        return "structural-ir-rejected";
    case JBeamVehicleImportCode::HYDRO_PLAN_REJECTED:
        return "hydro-plan-rejected";
    case JBeamVehicleImportCode::WHEEL2_PLAN_REJECTED:
        return "wheel2-plan-rejected";
    case JBeamVehicleImportCode::RIGDEF_CONVERSION_REJECTED:
        return "rigdef-conversion-rejected";
    case JBeamVehicleImportCode::ALLOCATION_FAILURE:
        return "allocation-failure";
    case JBeamVehicleImportCode::INTERNAL_FAILURE:
        return "internal-failure";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
