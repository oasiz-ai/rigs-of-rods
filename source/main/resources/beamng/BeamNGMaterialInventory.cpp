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

/// @file BeamNGMaterialInventory.cpp

#include "BeamNGMaterialInventory.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace RoR {
namespace BeamNG {

namespace {

const std::size_t NO_INDEX = std::numeric_limits<std::size_t>::max();

const std::size_t HARD_MAX_MANIFEST_ENTRIES = 1000000U;
const std::size_t HARD_MAX_SOURCES = 16384U;
const std::size_t HARD_MAX_TOTAL_SOURCE_BYTES = 256U * 1024U * 1024U;
const std::size_t HARD_MAX_SOURCE_BYTES = 32U * 1024U * 1024U;
const std::size_t HARD_MAX_TOKENS = 8000000U;
const std::size_t HARD_MAX_NODES = 4000000U;
const std::size_t HARD_MAX_DEPTH = 256U;
const std::size_t HARD_MAX_STRING_BYTES = 4U * 1024U * 1024U;
const std::size_t HARD_MAX_MATERIALS = 262144U;
const std::size_t HARD_MAX_FIELDS = 8000000U;
const std::size_t HARD_MAX_STAGES = 2000000U;
const std::size_t HARD_MAX_TEXTURES = 4000000U;
const std::size_t HARD_MAX_DIAGNOSTICS = 16384U;
const std::size_t HARD_MAX_RETAINED_BYTES = 1024U * 1024U * 1024U;
const std::size_t HARD_MAX_WORK = 64000000U;
const std::size_t HARD_MAX_RESOURCE_NAME_BYTES = 4U * 1024U * 1024U;
const std::size_t HARD_MAX_CANONICAL_BYTES = 1024U * 1024U * 1024U;
const std::size_t HARD_MAX_CANONICAL_WORK = 128000000U;

namespace RetainedV1 {

// Portable logical weights. Dynamic payloads are charged separately.
const std::size_t SOURCE_RECORD = 144U;
const std::size_t VALUE_RECORD = 184U;
const std::size_t OBJECT_FIELD_RECORD = 128U;
const std::size_t MATERIAL_RECORD = 672U;
const std::size_t OBSERVATION_RECORD = 168U;
const std::size_t TEXTURE_RECORD = 248U;
const std::size_t DIAGNOSTIC_RECORD = 192U;
const std::size_t INDEX_RECORD = 8U;
const std::size_t SPAN_RECORD = 64U;

} // namespace RetainedV1

bool AddSize(
    std::size_t left,
    std::size_t right,
    std::size_t& output)
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
    {
        return false;
    }
    output = left + right;
    return true;
}

bool MultiplySize(
    std::size_t left,
    std::size_t right,
    std::size_t& output)
{
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left)
    {
        return false;
    }
    output = left * right;
    return true;
}

std::string AsciiLowercase(const std::string& value)
{
    std::string lowered(value);
    for (std::size_t i = 0U; i < lowered.size(); ++i)
    {
        const unsigned char byte =
            static_cast<unsigned char>(lowered[i]);
        if (byte >= 'A' && byte <= 'Z')
        {
            lowered[i] = static_cast<char>(byte - 'A' + 'a');
        }
    }
    return lowered;
}

bool EndsWith(
    const std::string& value,
    const std::string& suffix)
{
    return value.size() >= suffix.size() &&
        value.compare(
            value.size() - suffix.size(),
            suffix.size(),
            suffix) == 0;
}

bool IsNamespaceByte(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') ||
        (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') ||
        byte == '_' || byte == '-' || byte == '.';
}

bool IsNamespaceValid(const std::string& value)
{
    if (value.empty() || value.size() > 96U)
    {
        return false;
    }
    const unsigned char first =
        static_cast<unsigned char>(value[0]);
    if (!((first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9')))
    {
        return false;
    }
    for (std::size_t i = 1U; i < value.size(); ++i)
    {
        if (!IsNamespaceByte(
                static_cast<unsigned char>(value[i])))
        {
            return false;
        }
    }
    return true;
}

std::string TruncateForDiagnostic(
    const std::string& value,
    std::size_t limit)
{
    if (value.size() <= limit)
    {
        return value;
    }
    const std::string marker("[truncated]");
    if (limit <= marker.size())
    {
        return marker.substr(0U, limit);
    }
    return value.substr(0U, limit - marker.size()) + marker;
}

void RaiseDisposition(
    BeamNGMaterialDisposition requested,
    BeamNGMaterialDisposition& disposition)
{
    if (static_cast<int>(requested) > static_cast<int>(disposition))
    {
        disposition = requested;
    }
}

bool IsTextureField(BeamNGMaterialFieldKind kind)
{
    switch (kind)
    {
    case BeamNGMaterialFieldKind::BASE_COLOR_MAP:
    case BeamNGMaterialFieldKind::NORMAL_MAP:
    case BeamNGMaterialFieldKind::ROUGHNESS_MAP:
    case BeamNGMaterialFieldKind::METALLIC_MAP:
    case BeamNGMaterialFieldKind::EMISSIVE_MAP:
    case BeamNGMaterialFieldKind::AMBIENT_OCCLUSION_MAP:
    case BeamNGMaterialFieldKind::OPACITY_MAP:
    case BeamNGMaterialFieldKind::CLEAR_COAT_MAP:
    case BeamNGMaterialFieldKind::COLOR_PALETTE_MAP:
        return true;
    default:
        return false;
    }
}

bool IsBooleanField(BeamNGMaterialFieldKind kind)
{
    return kind == BeamNGMaterialFieldKind::TRANSLUCENT ||
        kind == BeamNGMaterialFieldKind::USE_ANISOTROPIC;
}

bool IsNumericField(BeamNGMaterialFieldKind kind)
{
    switch (kind)
    {
    case BeamNGMaterialFieldKind::BASE_COLOR_FACTOR:
    case BeamNGMaterialFieldKind::ROUGHNESS_FACTOR:
    case BeamNGMaterialFieldKind::METALLIC_FACTOR:
    case BeamNGMaterialFieldKind::EMISSIVE_FACTOR:
    case BeamNGMaterialFieldKind::CLEAR_COAT_FACTOR:
    case BeamNGMaterialFieldKind::CLEAR_COAT_ROUGHNESS_FACTOR:
    case BeamNGMaterialFieldKind::ALPHA_REF:
        return true;
    default:
        return false;
    }
}

bool LookupFieldKind(
    const std::string& name,
    BeamNGMaterialFieldKind& kind)
{
    if (name == "class")
    {
        kind = BeamNGMaterialFieldKind::CLASS_NAME;
    }
    else if (name == "name")
    {
        kind = BeamNGMaterialFieldKind::MATERIAL_NAME;
    }
    else if (name == "mapTo")
    {
        kind = BeamNGMaterialFieldKind::MAP_TO;
    }
    else if (name == "Stages")
    {
        kind = BeamNGMaterialFieldKind::STAGES;
    }
    else if (name == "baseColorMap")
    {
        kind = BeamNGMaterialFieldKind::BASE_COLOR_MAP;
    }
    else if (name == "normalMap")
    {
        kind = BeamNGMaterialFieldKind::NORMAL_MAP;
    }
    else if (name == "roughnessMap")
    {
        kind = BeamNGMaterialFieldKind::ROUGHNESS_MAP;
    }
    else if (name == "metallicMap")
    {
        kind = BeamNGMaterialFieldKind::METALLIC_MAP;
    }
    else if (name == "emissiveMap")
    {
        kind = BeamNGMaterialFieldKind::EMISSIVE_MAP;
    }
    else if (name == "ambientOcclusionMap")
    {
        kind = BeamNGMaterialFieldKind::AMBIENT_OCCLUSION_MAP;
    }
    else if (name == "opacityMap")
    {
        kind = BeamNGMaterialFieldKind::OPACITY_MAP;
    }
    else if (name == "clearCoatMap")
    {
        kind = BeamNGMaterialFieldKind::CLEAR_COAT_MAP;
    }
    else if (name == "colorPaletteMap")
    {
        kind = BeamNGMaterialFieldKind::COLOR_PALETTE_MAP;
    }
    else if (name == "baseColorFactor")
    {
        kind = BeamNGMaterialFieldKind::BASE_COLOR_FACTOR;
    }
    else if (name == "roughnessFactor")
    {
        kind = BeamNGMaterialFieldKind::ROUGHNESS_FACTOR;
    }
    else if (name == "metallicFactor")
    {
        kind = BeamNGMaterialFieldKind::METALLIC_FACTOR;
    }
    else if (name == "emissiveFactor")
    {
        kind = BeamNGMaterialFieldKind::EMISSIVE_FACTOR;
    }
    else if (name == "clearCoatFactor")
    {
        kind = BeamNGMaterialFieldKind::CLEAR_COAT_FACTOR;
    }
    else if (name == "clearCoatRoughnessFactor")
    {
        kind =
            BeamNGMaterialFieldKind::CLEAR_COAT_ROUGHNESS_FACTOR;
    }
    else if (name == "alphaRef")
    {
        kind = BeamNGMaterialFieldKind::ALPHA_REF;
    }
    else if (name == "translucent")
    {
        kind = BeamNGMaterialFieldKind::TRANSLUCENT;
    }
    else if (name == "useAnisotropic")
    {
        kind = BeamNGMaterialFieldKind::USE_ANISOTROPIC;
    }
    else
    {
        return false;
    }
    return true;
}

bool IsRootRelativePath(const std::string& path)
{
    static const char* const ROOTS[] = {
        "vehicles/",
        "levels/",
        "art/",
        "assets/",
        "gameplay/",
        "ui/"
    };
    for (std::size_t i = 0U;
         i < sizeof(ROOTS) / sizeof(ROOTS[0]);
         ++i)
    {
        const std::string root(ROOTS[i]);
        if (path.compare(0U, root.size(), root) == 0)
        {
            return true;
        }
    }
    return false;
}

bool CookedDdsCandidate(
    const std::string& authored_path,
    std::string& cooked_path)
{
    if (!(EndsWith(authored_path, ".color.png") ||
          EndsWith(authored_path, ".data.png") ||
          EndsWith(authored_path, ".normal.png")))
    {
        return false;
    }
    cooked_path =
        authored_path.substr(0U, authored_path.size() - 3U) +
        "dds";
    return true;
}

std::string DirectoryOf(const std::string& path)
{
    const std::size_t separator = path.rfind('/');
    if (separator == std::string::npos)
    {
        return std::string();
    }
    return path.substr(0U, separator);
}

bool BuildScopedResourceName(
    const std::string& package_namespace,
    const std::string& source_path,
    const std::string& material_key,
    std::size_t occurrence,
    std::size_t byte_limit,
    std::string& output)
{
    const std::string prefix("ror/beamng/");
    const std::string middle("/material/s");
    const std::string key_marker("/k");
    const std::string occurrence_marker("/o");
    char ordinal_buffer[
        std::numeric_limits<std::size_t>::digits10 + 2U];
    std::size_t ordinal_size = 0U;
    do
    {
        ordinal_buffer[ordinal_size++] =
            static_cast<char>('0' + occurrence % 10U);
        occurrence /= 10U;
    }
    while (occurrence != 0U);
    std::string ordinal;
    ordinal.reserve(ordinal_size);
    while (ordinal_size > 0U)
    {
        ordinal.push_back(ordinal_buffer[--ordinal_size]);
    }

    std::size_t path_hex_bytes = 0U;
    std::size_t key_hex_bytes = 0U;
    if (!MultiplySize(source_path.size(), 2U, path_hex_bytes) ||
        !MultiplySize(material_key.size(), 2U, key_hex_bytes))
    {
        return false;
    }
    std::size_t required = prefix.size();
    const std::size_t pieces[] = {
        package_namespace.size(),
        middle.size(),
        path_hex_bytes,
        key_marker.size(),
        key_hex_bytes,
        occurrence_marker.size(),
        ordinal.size()
    };
    for (std::size_t i = 0U;
         i < sizeof(pieces) / sizeof(pieces[0]);
         ++i)
    {
        if (!AddSize(required, pieces[i], required))
        {
            return false;
        }
    }
    if (required > byte_limit)
    {
        return false;
    }

    static const char HEX[] = "0123456789abcdef";
    output.clear();
    output.reserve(required);
    output += prefix;
    output += package_namespace;
    output += middle;
    for (std::size_t i = 0U; i < source_path.size(); ++i)
    {
        const unsigned char byte =
            static_cast<unsigned char>(source_path[i]);
        output.push_back(HEX[byte >> 4U]);
        output.push_back(HEX[byte & 0x0fU]);
    }
    output += key_marker;
    for (std::size_t i = 0U; i < material_key.size(); ++i)
    {
        const unsigned char byte =
            static_cast<unsigned char>(material_key[i]);
        output.push_back(HEX[byte >> 4U]);
        output.push_back(HEX[byte & 0x0fU]);
    }
    output += occurrence_marker;
    output += ordinal;
    return true;
}

struct SourceCandidate
{
    std::string package_path;
    std::string source_bytes;
};

bool SourceCandidateLess(
    const SourceCandidate& left,
    const SourceCandidate& right)
{
    return left.package_path < right.package_path;
}

class MaterialInventoryBuilder
{
public:
    MaterialInventoryBuilder(
        const std::string& package_namespace,
        const PackageManifest& manifest,
        const std::vector<BeamNGMaterialSource>& sources,
        const BeamNGMaterialLimits& limits)
        : m_package_namespace(package_namespace)
        , m_manifest(manifest)
        , m_input_sources(sources)
        , m_limits(limits)
        , m_retained(0U)
        , m_work(0U)
        , m_fields(0U)
        , m_stages(0U)
        , m_textures(0U)
        , m_fatal(false)
        , m_diagnostic_limit_emitted(false)
    {
        ClampLimits();
        m_result.documentation_profile_id =
            GetBeamNGMaterialDocumentationProfile().profile_id;
        m_result.canonical_output_byte_limit =
            m_limits.max_canonical_output_bytes;
        m_result.canonical_work_unit_limit =
            m_limits.max_canonical_work_units;
        m_result.canonical_value_depth_limit =
            m_limits.max_value_depth;
    }

    BeamNGMaterialInventory Run()
    {
        if (!IsNamespaceValid(m_package_namespace))
        {
            Fail(
                BeamNGMaterialDiagnosticCode::INVALID_PACKAGE_NAMESPACE,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Package namespace must be 1-96 portable ASCII bytes, "
                "beginning with an alphanumeric byte");
        }
        if (!m_fatal)
        {
            ValidateManifest();
        }
        if (!m_fatal)
        {
            AdmitMetadata();
        }

        std::vector<SourceCandidate> candidates;
        if (!m_fatal)
        {
            ValidateAndSortSources(candidates);
        }
        for (std::size_t i = 0U;
             i < candidates.size() && !m_fatal;
             ++i)
        {
            ParseSource(candidates[i]);
        }
        if (!m_fatal)
        {
            ResolveDuplicateGroups();
        }

        UpdateCounters();
        if (!m_fatal &&
            SerializeCanonicalBeamNGMaterialInventory(m_result).empty())
        {
            Fail(
                BeamNGMaterialDiagnosticCode::CANONICAL_OUTPUT_LIMIT,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Canonical inventory exceeds configured byte, work, or "
                "value-depth limit");
            UpdateCounters();
        }
        if (m_fatal)
        {
            ClearPayload();
        }
        return m_result;
    }

private:
    void ClampLimits()
    {
        m_limits.max_manifest_entries =
            std::min(
                m_limits.max_manifest_entries,
                HARD_MAX_MANIFEST_ENTRIES);
        m_limits.max_sources =
            std::min(m_limits.max_sources, HARD_MAX_SOURCES);
        m_limits.max_total_source_bytes =
            std::min(
                m_limits.max_total_source_bytes,
                HARD_MAX_TOTAL_SOURCE_BYTES);
        m_limits.max_source_bytes =
            std::min(
                m_limits.max_source_bytes,
                HARD_MAX_SOURCE_BYTES);
        m_limits.max_tokens_per_source =
            std::min(
                m_limits.max_tokens_per_source,
                HARD_MAX_TOKENS);
        m_limits.max_nodes_per_source =
            std::min(
                m_limits.max_nodes_per_source,
                HARD_MAX_NODES);
        m_limits.max_value_depth =
            std::min(m_limits.max_value_depth, HARD_MAX_DEPTH);
        m_limits.max_string_bytes =
            std::min(
                m_limits.max_string_bytes,
                HARD_MAX_STRING_BYTES);
        m_limits.max_materials =
            std::min(m_limits.max_materials, HARD_MAX_MATERIALS);
        m_limits.max_fields =
            std::min(m_limits.max_fields, HARD_MAX_FIELDS);
        m_limits.max_stages =
            std::min(m_limits.max_stages, HARD_MAX_STAGES);
        m_limits.max_texture_references =
            std::min(
                m_limits.max_texture_references,
                HARD_MAX_TEXTURES);
        m_limits.max_diagnostics =
            std::min(
                m_limits.max_diagnostics,
                HARD_MAX_DIAGNOSTICS);
        m_limits.max_retained_bytes =
            std::min(
                m_limits.max_retained_bytes,
                HARD_MAX_RETAINED_BYTES);
        m_limits.max_work_units =
            std::min(m_limits.max_work_units, HARD_MAX_WORK);
        m_limits.max_scoped_resource_name_bytes =
            std::min(
                m_limits.max_scoped_resource_name_bytes,
                HARD_MAX_RESOURCE_NAME_BYTES);
        m_limits.max_canonical_output_bytes =
            std::min(
                m_limits.max_canonical_output_bytes,
                HARD_MAX_CANONICAL_BYTES);
        m_limits.max_canonical_work_units =
            std::min(
                m_limits.max_canonical_work_units,
                HARD_MAX_CANONICAL_WORK);
    }

    void UpdateCounters()
    {
        m_result.authored_field_count = m_fields;
        m_result.authored_stage_count = m_stages;
        m_result.texture_reference_count = m_textures;
        m_result.retained_byte_count = m_retained;
        m_result.work_unit_count = m_work;
    }

    void ClearPayload()
    {
        m_result.sources.clear();
        m_result.materials.clear();
        m_result.authored_field_count = 0U;
        m_result.authored_stage_count = 0U;
        m_result.texture_reference_count = 0U;
        for (std::size_t i = 0U;
             i < m_result.diagnostics.size();
             ++i)
        {
            // A fatal error rejects the complete inventory transaction. Any
            // warning emitted while earlier sources were being classified can
            // no longer safely address the cleared payload vectors.
            m_result.diagnostics[i].source_index = NO_INDEX;
            m_result.diagnostics[i].material_index = NO_INDEX;
        }
    }

    void AddTerminal(
        BeamNGMaterialDiagnosticCode code,
        const JBeamSourceSpan& span,
        const std::string& detail)
    {
        BeamNGMaterialDiagnostic terminal;
        terminal.code = code;
        terminal.severity = BeamNGMaterialSeverity::ERROR_SEVERITY;
        terminal.span = span;
        terminal.detail = TruncateForDiagnostic(detail, 512U);
        if (m_result.diagnostics.empty())
        {
            m_result.diagnostics.push_back(terminal);
        }
        else
        {
            m_result.diagnostics.back() = terminal;
        }
        m_fatal = true;
    }

    bool Retain(
        std::size_t bytes,
        const JBeamSourceSpan& span)
    {
        std::size_t next = 0U;
        if (!AddSize(m_retained, bytes, next) ||
            next > m_limits.max_retained_bytes)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::RETAINED_BYTE_LIMIT,
                span,
                "Material inventory exceeds configured retained-byte "
                "limit");
            return false;
        }
        m_retained = next;
        return true;
    }

    bool ConsumeWork(
        std::size_t units,
        const JBeamSourceSpan& span)
    {
        std::size_t next = 0U;
        if (!AddSize(m_work, units, next) ||
            next > m_limits.max_work_units)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::WORK_LIMIT,
                span,
                "Material inventory exceeds configured work-unit limit");
            return false;
        }
        m_work = next;
        return true;
    }

    void AddDiagnostic(
        BeamNGMaterialDiagnosticCode code,
        BeamNGMaterialSeverity severity,
        const JBeamSourceSpan& span,
        std::size_t source_index,
        std::size_t material_index,
        const std::string& field_name,
        const std::string& detail)
    {
        if (m_fatal && severity != BeamNGMaterialSeverity::ERROR_SEVERITY)
        {
            return;
        }
        if (m_limits.max_diagnostics == 0U ||
            m_result.diagnostics.size() >=
                m_limits.max_diagnostics)
        {
            if (!m_diagnostic_limit_emitted)
            {
                m_diagnostic_limit_emitted = true;
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::DIAGNOSTIC_LIMIT,
                    span,
                    "Material inventory exceeds configured diagnostic "
                    "limit");
            }
            return;
        }

        BeamNGMaterialDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.span = span;
        diagnostic.source_index = source_index;
        diagnostic.material_index = material_index;
        diagnostic.field_name =
            TruncateForDiagnostic(field_name, 512U);
        diagnostic.detail =
            TruncateForDiagnostic(detail, 2048U);

        std::size_t retained = RetainedV1::DIAGNOSTIC_RECORD;
        const std::size_t pieces[] = {
            diagnostic.span.source_name.size(),
            diagnostic.field_name.size(),
            diagnostic.detail.size()
        };
        for (std::size_t i = 0U;
             i < sizeof(pieces) / sizeof(pieces[0]);
             ++i)
        {
            if (!AddSize(retained, pieces[i], retained))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::RETAINED_BYTE_LIMIT,
                    span,
                    "Diagnostic retained-byte accounting overflow");
                return;
            }
        }
        if (!Retain(retained, span))
        {
            return;
        }
        m_result.diagnostics.push_back(diagnostic);
        if (severity == BeamNGMaterialSeverity::ERROR_SEVERITY)
        {
            m_fatal = true;
        }
    }

    void Fail(
        BeamNGMaterialDiagnosticCode code,
        const JBeamSourceSpan& span,
        std::size_t source_index,
        std::size_t material_index,
        const std::string& field_name,
        const std::string& detail)
    {
        AddDiagnostic(
            code,
            BeamNGMaterialSeverity::ERROR_SEVERITY,
            span,
            source_index,
            material_index,
            field_name,
            detail);
    }

    PackageScanLimits PathLimits() const
    {
        PackageScanLimits limits;
        limits.max_entries = m_limits.max_manifest_entries;
        return limits;
    }

    void ValidateManifest()
    {
        if (m_manifest.format_profile.identifier.size() >
                m_limits.max_string_bytes ||
            m_manifest.format_profile.version.size() >
                m_limits.max_string_bytes)
        {
            Fail(
                BeamNGMaterialDiagnosticCode::INVALID_MANIFEST_ENTRY,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Manifest format-profile strings exceed configured "
                "string limit");
            return;
        }
        if (m_manifest.entries.empty())
        {
            Fail(
                BeamNGMaterialDiagnosticCode::INVALID_MANIFEST_ENTRY,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "A material inventory requires a non-empty validated "
                "package manifest");
            return;
        }
        if (m_manifest.entries.size() >
            m_limits.max_manifest_entries)
        {
            Fail(
                BeamNGMaterialDiagnosticCode::MANIFEST_ENTRY_LIMIT,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Package manifest exceeds configured entry limit");
            return;
        }

        const PackageScanLimits path_limits = PathLimits();
        for (std::size_t i = 0U;
             i < m_manifest.entries.size() && !m_fatal;
             ++i)
        {
            const PackageManifestEntry& entry = m_manifest.entries[i];
            if (!ConsumeWork(1U, JBeamSourceSpan()))
            {
                return;
            }
            if (entry.kind != PackageEntryKind::REGULAR_FILE &&
                entry.kind != PackageEntryKind::DIRECTORY)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::INVALID_MANIFEST_ENTRY,
                    JBeamSourceSpan(),
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Manifest contains a non-regular, non-directory "
                    "entry");
                return;
            }
            const PackagePathResult path = NormalizePackagePath(
                entry.path,
                entry.kind == PackageEntryKind::DIRECTORY,
                path_limits);
            if (!path.IsValid() ||
                path.canonical_path != entry.path)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::INVALID_MANIFEST_ENTRY,
                    JBeamSourceSpan(),
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Manifest entry is not an exact canonical portable "
                    "package path");
                return;
            }
            const std::map<std::string, const PackageManifestEntry*>::
                const_iterator exact =
                    m_manifest_by_path.find(entry.path);
            if (exact != m_manifest_by_path.end())
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        MANIFEST_DUPLICATE_PATH,
                    JBeamSourceSpan(),
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Manifest contains a duplicate canonical path");
                return;
            }
            const std::string folded = AsciiLowercase(entry.path);
            const std::map<std::string, const PackageManifestEntry*>::
                const_iterator case_match =
                    m_manifest_by_case.find(folded);
            if (case_match != m_manifest_by_case.end() &&
                case_match->second->path != entry.path)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        MANIFEST_CASE_COLLISION,
                    JBeamSourceSpan(),
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Manifest contains a case-folded path collision");
                return;
            }
            m_manifest_by_path.insert(
                std::make_pair(entry.path, &entry));
            m_manifest_by_case.insert(
                std::make_pair(folded, &entry));
            if (entry.kind == PackageEntryKind::REGULAR_FILE &&
                EndsWith(entry.path, ".materials.json"))
            {
                m_expected_source_paths.push_back(entry.path);
            }
        }
        std::sort(
            m_expected_source_paths.begin(),
            m_expected_source_paths.end());
    }

    void AdmitMetadata()
    {
        std::size_t cost = 192U;
        const std::size_t pieces[] = {
            m_result.documentation_profile_id.size(),
            m_manifest.format_profile.identifier.size(),
            m_manifest.format_profile.version.size(),
            m_package_namespace.size()
        };
        for (std::size_t i = 0U;
             i < sizeof(pieces) / sizeof(pieces[0]);
             ++i)
        {
            if (!AddSize(cost, pieces[i], cost))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::
                        RETAINED_BYTE_LIMIT,
                    JBeamSourceSpan(),
                    "Inventory metadata retained-byte accounting "
                    "overflow");
                return;
            }
        }
        if (!Retain(cost, JBeamSourceSpan()))
        {
            return;
        }
        m_result.package_format_profile =
            m_manifest.format_profile;
        m_result.package_namespace = m_package_namespace;
    }

    void ValidateAndSortSources(
        std::vector<SourceCandidate>& candidates)
    {
        if (m_input_sources.size() > m_limits.max_sources)
        {
            Fail(
                BeamNGMaterialDiagnosticCode::SOURCE_LIMIT,
                JBeamSourceSpan(),
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Material source enumeration exceeds configured limit");
            return;
        }

        std::set<std::string> seen_paths;
        std::size_t total_source_bytes = 0U;
        const PackageScanLimits path_limits = PathLimits();
        candidates.reserve(m_input_sources.size());
        for (std::size_t i = 0U;
             i < m_input_sources.size() && !m_fatal;
             ++i)
        {
            const BeamNGMaterialSource& input = m_input_sources[i];
            JBeamSourceSpan span;
            span.source_name =
                TruncateForDiagnostic(input.package_path, 512U);
            if (!ConsumeWork(1U, span))
            {
                return;
            }
            const PackagePathResult path = NormalizePackagePath(
                input.package_path, false, path_limits);
            if (!path.IsValid() ||
                path.canonical_path != input.package_path)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::INVALID_SOURCE_PATH,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source path must be an exact canonical "
                    "package-relative path");
                return;
            }
            if (!EndsWith(
                    input.package_path,
                    ".materials.json"))
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        SOURCE_NOT_MATERIAL_JSON,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source path must end with "
                    "'.materials.json' case-sensitively");
                return;
            }
            const std::map<std::string, const PackageManifestEntry*>::
                const_iterator manifest_entry =
                    m_manifest_by_path.find(input.package_path);
            if (manifest_entry == m_manifest_by_path.end())
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        SOURCE_NOT_IN_MANIFEST,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source has no exact manifest entry");
                return;
            }
            if (manifest_entry->second->kind !=
                PackageEntryKind::REGULAR_FILE)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        SOURCE_NOT_REGULAR_FILE,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source manifest entry is not a regular "
                    "file");
                return;
            }
            if (!seen_paths.insert(input.package_path).second)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        DUPLICATE_SOURCE_PATH,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source path was supplied more than once");
                return;
            }
            if (input.source_bytes.size() >
                    m_limits.max_source_bytes ||
                !AddSize(
                    total_source_bytes,
                    input.source_bytes.size(),
                    total_source_bytes) ||
                total_source_bytes >
                    m_limits.max_total_source_bytes)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::SOURCE_BYTE_LIMIT,
                    span,
                    NO_INDEX,
                    NO_INDEX,
                    std::string(),
                    "Material source bytes exceed configured per-source "
                    "or aggregate limit");
                return;
            }
            if (!Retain(input.source_bytes.size(), span))
            {
                return;
            }
            SourceCandidate candidate;
            candidate.package_path = input.package_path;
            candidate.source_bytes = input.source_bytes;
            candidates.push_back(candidate);
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            SourceCandidateLess);
        if (candidates.size() != m_expected_source_paths.size())
        {
            std::string missing_path;
            for (std::size_t i = 0U;
                 i < m_expected_source_paths.size();
                 ++i)
            {
                if (seen_paths.find(m_expected_source_paths[i]) ==
                    seen_paths.end())
                {
                    missing_path = m_expected_source_paths[i];
                    break;
                }
            }
            JBeamSourceSpan span;
            span.source_name =
                TruncateForDiagnostic(missing_path, 512U);
            Fail(
                BeamNGMaterialDiagnosticCode::
                    MATERIAL_SOURCE_NOT_SUPPLIED,
                span,
                NO_INDEX,
                NO_INDEX,
                std::string(),
                "Every manifest *.materials.json regular file must "
                "have caller-supplied bytes");
        }
    }

    void ParseSource(const SourceCandidate& candidate)
    {
        BeamNGMaterialSourceRecord source_record;
        source_record.package_path = candidate.package_path;
        JBeamSourceSpan source_span;
        source_span.source_name = candidate.package_path;
        std::size_t source_cost = RetainedV1::SOURCE_RECORD;
        if (!AddSize(
                source_cost,
                source_record.package_path.size(),
                source_cost) ||
            !Retain(source_cost, source_span))
        {
            return;
        }
        const std::size_t source_index = m_result.sources.size();
        m_result.sources.push_back(source_record);

        JBeamParseLimits parse_limits;
        parse_limits.max_source_bytes =
            m_limits.max_source_bytes;
        parse_limits.max_tokens =
            m_limits.max_tokens_per_source;
        parse_limits.max_nodes =
            m_limits.max_nodes_per_source;
        parse_limits.max_depth =
            m_limits.max_value_depth;
        parse_limits.max_string_bytes =
            m_limits.max_string_bytes;
        parse_limits.max_diagnostics =
            std::max<std::size_t>(
                1U,
                std::min(
                    m_limits.max_diagnostics,
                    HARD_MAX_DIAGNOSTICS));

        const JBeamParseResult parsed = ParseJBeam(
            candidate.source_bytes,
            candidate.package_path,
            parse_limits);
        for (std::size_t i = 0U;
             i < parsed.diagnostics.size() && !m_fatal;
             ++i)
        {
            const JBeamDiagnostic& parser_diagnostic =
                parsed.diagnostics[i];
            std::string detail(
                JBeamDiagnosticCodeToString(parser_diagnostic.code));
            detail += ": ";
            detail += parser_diagnostic.message;
            AddDiagnostic(
                BeamNGMaterialDiagnosticCode::
                    SOURCE_PARSE_DIAGNOSTIC,
                parser_diagnostic.severity ==
                        JBeamDiagnosticSeverity::ERROR_SEVERITY ?
                    BeamNGMaterialSeverity::ERROR_SEVERITY :
                    BeamNGMaterialSeverity::WARNING,
                parser_diagnostic.span,
                source_index,
                NO_INDEX,
                std::string(),
                detail);
        }
        if (m_fatal || !parsed.IsValid())
        {
            if (!m_fatal)
            {
                Fail(
                    BeamNGMaterialDiagnosticCode::
                        SOURCE_PARSE_DIAGNOSTIC,
                    source_span,
                    source_index,
                    NO_INDEX,
                    std::string(),
                    "Material parser rejected source without a retained "
                    "error diagnostic");
            }
            return;
        }
        if (parsed.root.type != JBeamValueType::OBJECT)
        {
            Fail(
                BeamNGMaterialDiagnosticCode::SOURCE_ROOT_NOT_OBJECT,
                parsed.root.span,
                source_index,
                NO_INDEX,
                std::string(),
                "A *.materials.json root must be an object");
            return;
        }
        m_result.sources[source_index].root_span = parsed.root.span;
        if (!Retain(
                parsed.root.span.source_name.size(),
                parsed.root.span))
        {
            return;
        }

        for (std::size_t i = 0U;
             i < parsed.root.object_fields.size() && !m_fatal;
             ++i)
        {
            AddMaterial(
                source_index,
                i,
                parsed.root.object_fields[i]);
        }
    }

    bool InspectRetainedValue(
        const JBeamValue& value,
        std::size_t depth)
    {
        if (depth > m_limits.max_value_depth)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::FIELD_LIMIT,
                value.span,
                "Retained material value exceeds configured depth");
            return false;
        }
        if (!ConsumeWork(1U, value.span))
        {
            return false;
        }
        std::size_t cost = RetainedV1::VALUE_RECORD;
        if (!AddSize(cost, RetainedV1::SPAN_RECORD, cost) ||
            !AddSize(cost, value.span.source_name.size(), cost) ||
            !AddSize(cost, value.scalar_text.size(), cost) ||
            !Retain(cost, value.span))
        {
            return false;
        }
        if (value.type == JBeamValueType::ARRAY)
        {
            for (std::size_t i = 0U;
                 i < value.array_values.size() && !m_fatal;
                 ++i)
            {
                if (!InspectRetainedValue(
                        value.array_values[i],
                        depth + 1U))
                {
                    return false;
                }
            }
        }
        else if (value.type == JBeamValueType::OBJECT)
        {
            for (std::size_t i = 0U;
                 i < value.object_fields.size() && !m_fatal;
                 ++i)
            {
                if (m_fields >= m_limits.max_fields)
                {
                    AddTerminal(
                        BeamNGMaterialDiagnosticCode::FIELD_LIMIT,
                        value.object_fields[i].key_span,
                        "Material definitions exceed configured field "
                        "limit");
                    return false;
                }
                ++m_fields;
                const JBeamObjectField& field =
                    value.object_fields[i];
                std::size_t field_cost =
                    RetainedV1::OBJECT_FIELD_RECORD;
                if (!AddSize(
                        field_cost,
                        RetainedV1::SPAN_RECORD,
                        field_cost) ||
                    !AddSize(
                        field_cost,
                        field.key.size(),
                        field_cost) ||
                    !AddSize(
                        field_cost,
                        field.key_span.source_name.size(),
                        field_cost) ||
                    !Retain(field_cost, field.key_span))
                {
                    return false;
                }
                if (!field.value)
                {
                    AddTerminal(
                        BeamNGMaterialDiagnosticCode::FIELD_LIMIT,
                        field.key_span,
                        "Parsed material field has no retained value");
                    return false;
                }
                if (!InspectRetainedValue(
                        *field.value,
                        depth + 1U))
                {
                    return false;
                }
            }
        }
        return !m_fatal;
    }

    void AddMaterial(
        std::size_t source_index,
        std::size_t source_material_index,
        const JBeamObjectField& top_level_field)
    {
        if (m_result.materials.size() >= m_limits.max_materials)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::MATERIAL_LIMIT,
                top_level_field.key_span,
                "Material count exceeds configured limit");
            return;
        }
        if (!top_level_field.value)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::FIELD_LIMIT,
                top_level_field.key_span,
                "Top-level material key has no retained value");
            return;
        }
        if (!InspectRetainedValue(*top_level_field.value, 1U))
        {
            return;
        }

        const std::string occurrence_key =
            m_result.sources[source_index].package_path +
            std::string(1U, '\0') +
            top_level_field.key;
        const std::size_t occurrence =
            m_resource_occurrences[occurrence_key]++;

        BeamNGMaterialRecord material;
        material.source_index = source_index;
        material.source_material_index = source_material_index;
        material.material_key = top_level_field.key;
        material.material_key_span = top_level_field.key_span;
        material.raw_definition = top_level_field.value;
        if (!BuildScopedResourceName(
                m_package_namespace,
                m_result.sources[source_index].package_path,
                material.material_key,
                occurrence,
                m_limits.max_scoped_resource_name_bytes,
                material.scoped_resource_name))
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::
                    SCOPED_RESOURCE_NAME_LIMIT,
                top_level_field.key_span,
                "Collision-free scoped material resource name exceeds "
                "configured limit");
            return;
        }
        if (!m_resource_names.insert(
                material.scoped_resource_name).second)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::
                    SCOPED_RESOURCE_NAME_COLLISION,
                top_level_field.key_span,
                "Scoped material resource identity collided");
            return;
        }

        std::size_t material_cost = RetainedV1::MATERIAL_RECORD;
        const std::size_t material_strings[] = {
            material.material_key.size(),
            material.material_key_span.source_name.size(),
            material.scoped_resource_name.size()
        };
        for (std::size_t i = 0U;
             i < sizeof(material_strings) /
                 sizeof(material_strings[0]);
             ++i)
        {
            if (!AddSize(
                    material_cost,
                    material_strings[i],
                    material_cost))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::
                        RETAINED_BYTE_LIMIT,
                    top_level_field.key_span,
                    "Material retained-byte accounting overflow");
                return;
            }
        }
        if (!Retain(material_cost, top_level_field.key_span))
        {
            return;
        }

        const std::size_t material_index =
            m_result.materials.size();
        if (material.raw_definition->type != JBeamValueType::OBJECT)
        {
            material.disposition =
                BeamNGMaterialDisposition::PRESERVED_DISABLED;
            AddDiagnostic(
                BeamNGMaterialDiagnosticCode::
                    MATERIAL_DEFINITION_NOT_OBJECT,
                BeamNGMaterialSeverity::WARNING,
                material.raw_definition->span,
                source_index,
                material_index,
                material.material_key,
                "Top-level material value is retained but is not an "
                "object");
        }
        else
        {
            ClassifyMaterial(material_index, material);
        }
        if (m_fatal)
        {
            return;
        }
        m_result.materials.push_back(material);
        m_result.sources[source_index].material_indices.push_back(
            material_index);
        if (!Retain(
                RetainedV1::INDEX_RECORD,
                top_level_field.key_span))
        {
            return;
        }
    }

    void ReadEffectiveString(
        const JBeamValue& object,
        const std::string& field_name,
        BeamNGMaterialDiagnosticCode invalid_code,
        std::size_t material_index,
        BeamNGMaterialRecord& material,
        BeamNGMaterialStringField& output)
    {
        if (m_fatal)
        {
            return;
        }
        const JBeamObjectField* effective =
            FindLastJBeamObjectField(object, field_name);
        for (std::size_t i = 0U;
             i < object.object_fields.size();
             ++i)
        {
            if (object.object_fields[i].key == field_name)
            {
                ++output.assignment_count;
            }
        }
        if (effective == NULL)
        {
            return;
        }
        output.present = true;
        output.span = effective->value ?
            effective->value->span :
            effective->key_span;
        std::size_t dynamic_bytes =
            output.span.source_name.size();
        if (effective->value &&
            effective->value->type == JBeamValueType::STRING)
        {
            output.type_valid = true;
            output.value = effective->value->scalar_text;
            if (!AddSize(
                    dynamic_bytes,
                    output.value.size(),
                    dynamic_bytes) ||
                !Retain(dynamic_bytes, output.span))
            {
                return;
            }
            return;
        }
        if (!Retain(dynamic_bytes, output.span))
        {
            return;
        }

        AddDiagnostic(
            invalid_code,
            BeamNGMaterialSeverity::WARNING,
            output.span,
            material.source_index,
            material_index,
            field_name,
            "Effective material identity field is not a string");
        if (field_name == "class")
        {
            RaiseDisposition(
                BeamNGMaterialDisposition::PRESERVED_DISABLED,
                material.disposition);
        }
        else
        {
            RaiseDisposition(
                BeamNGMaterialDisposition::PLACEHOLDER,
                material.disposition);
        }
    }

    void AddObservation(
        BeamNGMaterialFieldKind kind,
        BeamNGMaterialFieldScope scope,
        bool has_stages_assignment,
        std::size_t stages_assignment_index,
        std::size_t stage_index,
        const JBeamObjectField& field,
        bool is_effective,
        BeamNGMaterialRecord& material)
    {
        BeamNGMaterialFieldObservation observation;
        observation.kind = kind;
        observation.scope = scope;
        observation.has_stages_assignment =
            has_stages_assignment;
        observation.stages_assignment_index =
            stages_assignment_index;
        observation.stage_index = stage_index;
        observation.authored_name = field.key;
        observation.field_span = field.key_span;
        observation.raw_value = field.value;
        observation.is_effective_assignment = is_effective;

        std::size_t cost = RetainedV1::OBSERVATION_RECORD;
        if (!AddSize(cost, observation.authored_name.size(), cost) ||
            !AddSize(
                cost,
                observation.field_span.source_name.size(),
                cost) ||
            !Retain(cost, observation.field_span))
        {
            return;
        }
        material.recognized_fields.push_back(observation);
    }

    bool NumericShapeValid(
        const JBeamValue& value,
        std::size_t depth)
    {
        if (!ConsumeWork(1U, value.span) ||
            depth > m_limits.max_value_depth)
        {
            return false;
        }
        if (value.type == JBeamValueType::NULL_VALUE ||
            value.type == JBeamValueType::NUMBER)
        {
            return true;
        }
        if (value.type != JBeamValueType::ARRAY)
        {
            return false;
        }
        for (std::size_t i = 0U;
             i < value.array_values.size() && !m_fatal;
             ++i)
        {
            const JBeamValue& item = value.array_values[i];
            if (!ConsumeWork(1U, item.span) ||
                (item.type != JBeamValueType::NULL_VALUE &&
                 item.type != JBeamValueType::NUMBER))
            {
                return false;
            }
        }
        return !m_fatal;
    }

    bool BooleanShapeValid(
        const JBeamValue& value,
        std::size_t depth)
    {
        if (!ConsumeWork(1U, value.span) ||
            depth > m_limits.max_value_depth)
        {
            return false;
        }
        if (value.type == JBeamValueType::NULL_VALUE ||
            value.type == JBeamValueType::BOOLEAN)
        {
            return true;
        }
        if (value.type != JBeamValueType::ARRAY)
        {
            return false;
        }
        for (std::size_t i = 0U;
             i < value.array_values.size() && !m_fatal;
             ++i)
        {
            const JBeamValue& item = value.array_values[i];
            if (!ConsumeWork(1U, item.span) ||
                (item.type != JBeamValueType::NULL_VALUE &&
                 item.type != JBeamValueType::BOOLEAN))
            {
                return false;
            }
        }
        return !m_fatal;
    }

    void AddTextureReference(
        BeamNGMaterialFieldKind kind,
        BeamNGMaterialFieldScope scope,
        bool has_stages_assignment,
        std::size_t stages_assignment_index,
        std::size_t stage_index,
        const std::vector<std::size_t>& array_indices,
        const JBeamValue& value,
        bool affects_disposition,
        std::size_t material_index,
        BeamNGMaterialRecord& material)
    {
        if (m_textures >= m_limits.max_texture_references)
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::TEXTURE_LIMIT,
                value.span,
                "Texture reference count exceeds configured limit");
            return;
        }
        ++m_textures;
        BeamNGMaterialTextureReference texture;
        texture.field_kind = kind;
        texture.scope = scope;
        texture.has_stages_assignment = has_stages_assignment;
        texture.stages_assignment_index = stages_assignment_index;
        texture.stage_index = stage_index;
        texture.array_indices = array_indices;
        texture.raw_path = value.scalar_text;
        texture.value_span = value.span;

        if (!texture.raw_path.empty() &&
            texture.raw_path[0] == '@')
        {
            if (texture.raw_path.size() > 1U)
            {
                texture.status =
                    BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE;
            }
            else
            {
                texture.status =
                    BeamNGTextureReferenceStatus::INVALID_PATH;
            }
        }
        else if (texture.raw_path.empty())
        {
            texture.status =
                BeamNGTextureReferenceStatus::INVALID_PATH;
        }
        else
        {
            std::string candidate;
            if (texture.raw_path[0] == '/')
            {
                candidate = texture.raw_path.substr(1U);
            }
            else if (IsRootRelativePath(texture.raw_path))
            {
                candidate = texture.raw_path;
            }
            else
            {
                const std::string directory = DirectoryOf(
                    m_result.sources[material.source_index].
                        package_path);
                candidate = directory.empty() ?
                    texture.raw_path :
                    directory + "/" + texture.raw_path;
            }
            const PackagePathResult normalized =
                NormalizePackagePath(
                    candidate,
                    false,
                    PathLimits());
            if (!normalized.IsValid())
            {
                texture.status =
                    BeamNGTextureReferenceStatus::INVALID_PATH;
            }
            else
            {
                texture.candidate_path =
                    normalized.canonical_path;
                const std::map<
                    std::string,
                    const PackageManifestEntry*>::const_iterator exact =
                        m_manifest_by_path.find(
                            texture.candidate_path);
                if (exact != m_manifest_by_path.end() &&
                    exact->second->kind ==
                        PackageEntryKind::REGULAR_FILE)
                {
                    texture.status =
                        BeamNGTextureReferenceStatus::LOCAL_FOUND;
                    texture.resolved_manifest_path =
                        exact->second->path;
                }
                else
                {
                    std::string cooked_candidate;
                    const bool has_cooked_candidate =
                        CookedDdsCandidate(
                            texture.candidate_path,
                            cooked_candidate);
                    const std::map<
                        std::string,
                        const PackageManifestEntry*>::const_iterator
                            cooked_exact =
                                has_cooked_candidate ?
                                m_manifest_by_path.find(
                                    cooked_candidate) :
                                m_manifest_by_path.end();
                    if (cooked_exact != m_manifest_by_path.end() &&
                        cooked_exact->second->kind ==
                            PackageEntryKind::REGULAR_FILE)
                    {
                        texture.status =
                            BeamNGTextureReferenceStatus::
                                LOCAL_COOKED_DDS;
                        texture.resolved_manifest_path =
                            cooked_exact->second->path;
                    }
                    else
                    {
                        const std::map<
                            std::string,
                            const PackageManifestEntry*>::
                                const_iterator case_match =
                                    m_manifest_by_case.find(
                                        AsciiLowercase(
                                            texture.candidate_path));
                        const std::map<
                            std::string,
                            const PackageManifestEntry*>::
                                const_iterator cooked_case_match =
                                    has_cooked_candidate ?
                                    m_manifest_by_case.find(
                                        AsciiLowercase(
                                            cooked_candidate)) :
                                    m_manifest_by_case.end();
                        if (
                            case_match != m_manifest_by_case.end() &&
                            case_match->second->kind ==
                                PackageEntryKind::REGULAR_FILE)
                        {
                            texture.status =
                                BeamNGTextureReferenceStatus::
                                    LOCAL_CASE_MISMATCH;
                            texture.resolved_manifest_path =
                                case_match->second->path;
                        }
                        else if (
                            cooked_case_match !=
                                m_manifest_by_case.end() &&
                            cooked_case_match->second->kind ==
                                PackageEntryKind::REGULAR_FILE)
                        {
                            texture.status =
                                BeamNGTextureReferenceStatus::
                                    LOCAL_CASE_MISMATCH;
                            texture.resolved_manifest_path =
                                cooked_case_match->second->path;
                        }
                        else
                        {
                            texture.status =
                                BeamNGTextureReferenceStatus::
                                    LOCAL_MISSING;
                        }
                    }
                }
            }
        }

        std::size_t index_bytes = 0U;
        if (!MultiplySize(
                texture.array_indices.size(),
                RetainedV1::INDEX_RECORD,
                index_bytes))
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::RETAINED_BYTE_LIMIT,
                value.span,
                "Texture array-index accounting overflow");
            return;
        }
        std::size_t cost = RetainedV1::TEXTURE_RECORD;
        const std::size_t pieces[] = {
            index_bytes,
            texture.raw_path.size(),
            texture.value_span.source_name.size(),
            texture.candidate_path.size(),
            texture.resolved_manifest_path.size()
        };
        for (std::size_t i = 0U;
             i < sizeof(pieces) / sizeof(pieces[0]);
             ++i)
        {
            if (!AddSize(cost, pieces[i], cost))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::
                        RETAINED_BYTE_LIMIT,
                    value.span,
                    "Texture retained-byte accounting overflow");
                return;
            }
        }
        if (!Retain(cost, value.span))
        {
            return;
        }
        material.texture_references.push_back(texture);

        BeamNGMaterialDiagnosticCode diagnostic_code =
            BeamNGMaterialDiagnosticCode::INVALID_TEXTURE_PATH;
        bool needs_diagnostic = false;
        std::string detail;
        switch (texture.status)
        {
        case BeamNGTextureReferenceStatus::LOCAL_FOUND:
        case BeamNGTextureReferenceStatus::LOCAL_COOKED_DDS:
        case BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE:
            break;
        case BeamNGTextureReferenceStatus::LOCAL_MISSING:
            needs_diagnostic = true;
            diagnostic_code =
                BeamNGMaterialDiagnosticCode::MISSING_TEXTURE;
            detail =
                "Texture has no exact regular-file manifest entry: " +
                texture.candidate_path;
            break;
        case BeamNGTextureReferenceStatus::LOCAL_CASE_MISMATCH:
            needs_diagnostic = true;
            diagnostic_code =
                BeamNGMaterialDiagnosticCode::
                    TEXTURE_CASE_MISMATCH;
            detail =
                "Texture path case differs from manifest entry: " +
                texture.resolved_manifest_path;
            break;
        case BeamNGTextureReferenceStatus::INVALID_PATH:
            needs_diagnostic = true;
            diagnostic_code =
                BeamNGMaterialDiagnosticCode::INVALID_TEXTURE_PATH;
            detail =
                "Texture is empty, is an empty dynamic name, or is not "
                "a safe package-local path";
            break;
        }
        if (needs_diagnostic)
        {
            AddDiagnostic(
                diagnostic_code,
                BeamNGMaterialSeverity::WARNING,
                value.span,
                material.source_index,
                material_index,
                std::string(),
                detail);
            if (affects_disposition)
            {
                RaiseDisposition(
                    BeamNGMaterialDisposition::PLACEHOLDER,
                    material.disposition);
            }
        }
    }

    void ScanTextureValue(
        BeamNGMaterialFieldKind kind,
        BeamNGMaterialFieldScope scope,
        bool has_stages_assignment,
        std::size_t stages_assignment_index,
        std::size_t stage_index,
        const JBeamValue& value,
        bool affects_disposition,
        std::size_t material_index,
        BeamNGMaterialRecord& material,
        std::vector<std::size_t>& array_indices,
        std::size_t depth)
    {
        if (!ConsumeWork(1U, value.span) ||
            depth > m_limits.max_value_depth)
        {
            return;
        }
        if (value.type == JBeamValueType::NULL_VALUE)
        {
            return;
        }
        if (value.type == JBeamValueType::STRING)
        {
            AddTextureReference(
                kind,
                scope,
                has_stages_assignment,
                stages_assignment_index,
                stage_index,
                array_indices,
                value,
                affects_disposition,
                material_index,
                material);
            return;
        }
        if (value.type == JBeamValueType::ARRAY)
        {
            if (depth != 1U)
            {
                AddDiagnostic(
                    BeamNGMaterialDiagnosticCode::INVALID_PBR_INPUT,
                    BeamNGMaterialSeverity::WARNING,
                    value.span,
                    material.source_index,
                    material_index,
                    std::string(),
                    "Texture input arrays must be flat; nested arrays "
                    "are preserved but not activated");
                if (affects_disposition)
                {
                    RaiseDisposition(
                        BeamNGMaterialDisposition::PLACEHOLDER,
                        material.disposition);
                }
                return;
            }
            for (std::size_t i = 0U;
                 i < value.array_values.size() && !m_fatal;
                 ++i)
            {
                array_indices.push_back(i);
                ScanTextureValue(
                    kind,
                    scope,
                    has_stages_assignment,
                    stages_assignment_index,
                    stage_index,
                    value.array_values[i],
                    affects_disposition,
                    material_index,
                    material,
                    array_indices,
                    depth + 1U);
                array_indices.pop_back();
            }
            return;
        }

        AddDiagnostic(
            BeamNGMaterialDiagnosticCode::INVALID_PBR_INPUT,
            BeamNGMaterialSeverity::WARNING,
            value.span,
            material.source_index,
            material_index,
            std::string(),
            "Texture input must be a string, null, or an array "
            "containing only those values");
        if (affects_disposition)
        {
            RaiseDisposition(
                BeamNGMaterialDisposition::PLACEHOLDER,
                material.disposition);
        }
    }

    void ClassifyPbrField(
        BeamNGMaterialFieldKind kind,
        BeamNGMaterialFieldScope scope,
        bool has_stages_assignment,
        std::size_t stages_assignment_index,
        std::size_t stage_index,
        const JBeamObjectField& field,
        bool is_effective,
        bool affects_disposition,
        std::size_t material_index,
        BeamNGMaterialRecord& material)
    {
        AddObservation(
            kind,
            scope,
            has_stages_assignment,
            stages_assignment_index,
            stage_index,
            field,
            is_effective,
            material);
        if (m_fatal || !field.value)
        {
            return;
        }
        if (IsTextureField(kind))
        {
            std::vector<std::size_t> indices;
            ScanTextureValue(
                kind,
                scope,
                has_stages_assignment,
                stages_assignment_index,
                stage_index,
                *field.value,
                affects_disposition,
                material_index,
                material,
                indices,
                1U);
            return;
        }

        bool valid = true;
        if (IsNumericField(kind))
        {
            valid = NumericShapeValid(*field.value, 1U);
        }
        else if (IsBooleanField(kind))
        {
            valid = BooleanShapeValid(*field.value, 1U);
        }
        if (!valid && !m_fatal)
        {
            AddDiagnostic(
                BeamNGMaterialDiagnosticCode::INVALID_PBR_INPUT,
                BeamNGMaterialSeverity::WARNING,
                field.value->span,
                material.source_index,
                material_index,
                field.key,
                "Recognized PBR input has an incompatible scalar or "
                "array shape");
            if (affects_disposition)
            {
                RaiseDisposition(
                    BeamNGMaterialDisposition::PLACEHOLDER,
                    material.disposition);
            }
        }
    }

    void ScanStages(
        const JBeamObjectField& stages_field,
        std::size_t stages_assignment_index,
        bool assignment_is_effective,
        std::size_t material_index,
        BeamNGMaterialRecord& material)
    {
        if (!stages_field.value ||
            stages_field.value->type != JBeamValueType::ARRAY)
        {
            AddDiagnostic(
                BeamNGMaterialDiagnosticCode::INVALID_STAGES_FIELD,
                BeamNGMaterialSeverity::WARNING,
                stages_field.value ?
                    stages_field.value->span :
                    stages_field.key_span,
                material.source_index,
                material_index,
                stages_field.key,
                "Stages must be an array");
            if (assignment_is_effective)
            {
                RaiseDisposition(
                    BeamNGMaterialDisposition::PLACEHOLDER,
                    material.disposition);
            }
            return;
        }
        const std::size_t stage_count =
            stages_field.value->array_values.size();
        if (stage_count >
                m_limits.max_stages - std::min(
                    m_stages,
                    m_limits.max_stages))
        {
            AddTerminal(
                BeamNGMaterialDiagnosticCode::STAGE_LIMIT,
                stages_field.value->span,
                "Authored Stages entries exceed configured limit");
            return;
        }
        m_stages += stage_count;
        material.authored_stage_count += stage_count;

        for (std::size_t stage_index = 0U;
             stage_index < stage_count && !m_fatal;
             ++stage_index)
        {
            if (!ConsumeWork(
                    1U,
                    stages_field.value->
                        array_values[stage_index].span))
            {
                return;
            }
            const JBeamValue& stage =
                stages_field.value->array_values[stage_index];
            if (stage.type == JBeamValueType::NULL_VALUE)
            {
                continue;
            }
            if (stage.type != JBeamValueType::OBJECT)
            {
                AddDiagnostic(
                    BeamNGMaterialDiagnosticCode::INVALID_STAGE,
                    BeamNGMaterialSeverity::WARNING,
                    stage.span,
                    material.source_index,
                    material_index,
                    stages_field.key,
                    "Stage entry must be an object or null");
                if (assignment_is_effective)
                {
                    RaiseDisposition(
                        BeamNGMaterialDisposition::PLACEHOLDER,
                        material.disposition);
                }
                continue;
            }
            std::map<std::string, const JBeamObjectField*>
                effective_fields;
            for (std::size_t field_index = 0U;
                 field_index < stage.object_fields.size();
                 ++field_index)
            {
                effective_fields[
                    stage.object_fields[field_index].key] =
                        &stage.object_fields[field_index];
            }
            for (std::size_t field_index = 0U;
                 field_index < stage.object_fields.size() &&
                    !m_fatal;
                 ++field_index)
            {
                const JBeamObjectField& field =
                    stage.object_fields[field_index];
                BeamNGMaterialFieldKind kind;
                if (!LookupFieldKind(field.key, kind) ||
                    kind == BeamNGMaterialFieldKind::CLASS_NAME ||
                    kind == BeamNGMaterialFieldKind::MATERIAL_NAME ||
                    kind == BeamNGMaterialFieldKind::MAP_TO ||
                    kind == BeamNGMaterialFieldKind::STAGES)
                {
                    continue;
                }
                const bool field_is_effective =
                    effective_fields[field.key] == &field;
                ClassifyPbrField(
                    kind,
                    BeamNGMaterialFieldScope::STAGE,
                    true,
                    stages_assignment_index,
                    stage_index,
                    field,
                    field_is_effective,
                    assignment_is_effective &&
                        field_is_effective,
                    material_index,
                    material);
            }
        }
    }

    void ClassifyMaterial(
        std::size_t material_index,
        BeamNGMaterialRecord& material)
    {
        const JBeamValue& object = *material.raw_definition;
        ReadEffectiveString(
            object,
            "class",
            BeamNGMaterialDiagnosticCode::INVALID_CLASS_FIELD,
            material_index,
            material,
            material.class_name);
        ReadEffectiveString(
            object,
            "name",
            BeamNGMaterialDiagnosticCode::INVALID_NAME_FIELD,
            material_index,
            material,
            material.name);
        ReadEffectiveString(
            object,
            "mapTo",
            BeamNGMaterialDiagnosticCode::INVALID_MAP_TO_FIELD,
            material_index,
            material,
            material.map_to);
        if (m_fatal)
        {
            return;
        }
        if (material.class_name.present &&
            material.class_name.type_valid &&
            material.class_name.value != "Material")
        {
            RaiseDisposition(
                BeamNGMaterialDisposition::PRESERVED_DISABLED,
                material.disposition);
            AddDiagnostic(
                BeamNGMaterialDiagnosticCode::UNSUPPORTED_CLASS,
                BeamNGMaterialSeverity::WARNING,
                material.class_name.span,
                material.source_index,
                material_index,
                "class",
                "Only class 'Material' is recognized by this inert "
                "inventory; other classes are preserved disabled");
        }

        std::map<std::string, const JBeamObjectField*>
            effective_fields;
        for (std::size_t i = 0U;
             i < object.object_fields.size();
             ++i)
        {
            effective_fields[object.object_fields[i].key] =
                &object.object_fields[i];
        }
        std::size_t stages_assignment_index = 0U;
        for (std::size_t i = 0U;
             i < object.object_fields.size() && !m_fatal;
             ++i)
        {
            const JBeamObjectField& field =
                object.object_fields[i];
            BeamNGMaterialFieldKind kind;
            if (!LookupFieldKind(field.key, kind))
            {
                continue;
            }
            const bool is_effective =
                effective_fields[field.key] == &field;
            if (kind == BeamNGMaterialFieldKind::STAGES)
            {
                AddObservation(
                    kind,
                    BeamNGMaterialFieldScope::ROOT_LAYER,
                    false,
                    0U,
                    0U,
                    field,
                    is_effective,
                    material);
                if (!m_fatal)
                {
                    ScanStages(
                        field,
                        stages_assignment_index,
                        is_effective,
                        material_index,
                        material);
                }
                ++stages_assignment_index;
            }
            else if (
                kind == BeamNGMaterialFieldKind::CLASS_NAME ||
                kind == BeamNGMaterialFieldKind::MATERIAL_NAME ||
                kind == BeamNGMaterialFieldKind::MAP_TO)
            {
                AddObservation(
                    kind,
                    BeamNGMaterialFieldScope::ROOT_LAYER,
                    false,
                    0U,
                    0U,
                    field,
                    is_effective,
                    material);
            }
            else
            {
                ClassifyPbrField(
                    kind,
                    BeamNGMaterialFieldScope::ROOT_LAYER,
                    false,
                    0U,
                    0U,
                    field,
                    is_effective,
                    is_effective,
                    material_index,
                    material);
            }
        }
    }

    void ResolveDuplicateGroups()
    {
        std::map<std::string, std::vector<std::size_t> > by_key;
        std::map<std::string, std::vector<std::size_t> > by_name;
        for (std::size_t i = 0U;
             i < m_result.materials.size();
             ++i)
        {
            by_key[m_result.materials[i].material_key].push_back(i);
            if (m_result.materials[i].name.present &&
                m_result.materials[i].name.type_valid)
            {
                by_name[m_result.materials[i].name.value].
                    push_back(i);
            }
        }

        for (std::map<
                 std::string,
                 std::vector<std::size_t> >::const_iterator group =
                 by_key.begin();
             group != by_key.end() && !m_fatal;
             ++group)
        {
            std::size_t index_entries = 0U;
            std::size_t index_count = 0U;
            if (!MultiplySize(
                    group->second.size(),
                    group->second.size(),
                    index_entries))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::WORK_LIMIT,
                    m_result.materials[
                        group->second.front()].material_key_span,
                    "Duplicate-history work accounting overflow");
                return;
            }
            if (!ConsumeWork(
                    index_entries,
                    m_result.materials[
                        group->second.front()].material_key_span))
            {
                return;
            }
            if (!MultiplySize(
                    index_entries,
                    RetainedV1::INDEX_RECORD,
                    index_count))
            {
                AddTerminal(
                    BeamNGMaterialDiagnosticCode::
                        RETAINED_BYTE_LIMIT,
                    m_result.materials[
                        group->second.front()].material_key_span,
                    "Duplicate-history retained-byte accounting "
                    "overflow");
                return;
            }
            if (!Retain(
                    index_count,
                    m_result.materials[
                        group->second.front()].material_key_span))
            {
                return;
            }
            for (std::size_t i = 0U;
                 i < group->second.size();
                 ++i)
            {
                m_result.materials[group->second[i]].
                    same_key_material_indices = group->second;
                if (group->second.size() > 1U)
                {
                    RaiseDisposition(
                        BeamNGMaterialDisposition::PLACEHOLDER,
                        m_result.materials[group->second[i]].
                            disposition);
                }
                if (i > 0U)
                {
                    AddDiagnostic(
                        BeamNGMaterialDiagnosticCode::
                            DUPLICATE_MATERIAL_KEY,
                        BeamNGMaterialSeverity::WARNING,
                        m_result.materials[group->second[i]].
                            material_key_span,
                        m_result.materials[group->second[i]].
                            source_index,
                        group->second[i],
                        group->first,
                        "Exact top-level material key collides with an "
                        "earlier package material");
                }
            }
        }

        for (std::map<
                 std::string,
                 std::vector<std::size_t> >::const_iterator group =
                 by_name.begin();
             group != by_name.end() && !m_fatal;
             ++group)
        {
            if (group->second.size() <= 1U)
            {
                continue;
            }
            for (std::size_t i = 0U;
                 i < group->second.size();
                 ++i)
            {
                RaiseDisposition(
                    BeamNGMaterialDisposition::PLACEHOLDER,
                    m_result.materials[group->second[i]].
                        disposition);
                if (i > 0U)
                {
                    AddDiagnostic(
                        BeamNGMaterialDiagnosticCode::
                            DUPLICATE_MATERIAL_NAME,
                        BeamNGMaterialSeverity::WARNING,
                        m_result.materials[group->second[i]].name.span,
                        m_result.materials[group->second[i]].
                            source_index,
                        group->second[i],
                        "name",
                        "Effective material name collides with an "
                        "earlier package material");
                }
            }
        }
    }

    const std::string& m_package_namespace;
    const PackageManifest& m_manifest;
    const std::vector<BeamNGMaterialSource>& m_input_sources;
    BeamNGMaterialLimits m_limits;
    BeamNGMaterialInventory m_result;
    std::map<std::string, const PackageManifestEntry*>
        m_manifest_by_path;
    std::map<std::string, const PackageManifestEntry*>
        m_manifest_by_case;
    std::vector<std::string> m_expected_source_paths;
    std::map<std::string, std::size_t> m_resource_occurrences;
    std::set<std::string> m_resource_names;
    std::size_t m_retained;
    std::size_t m_work;
    std::size_t m_fields;
    std::size_t m_stages;
    std::size_t m_textures;
    bool m_fatal;
    bool m_diagnostic_limit_emitted;
};

std::uint64_t DoubleBits(double value)
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "Canonical material identity requires IEEE-width doubles");
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

class CanonicalWriter
{
public:
    CanonicalWriter(
        std::size_t byte_limit,
        std::size_t work_limit)
        : m_byte_limit(
              std::min(byte_limit, HARD_MAX_CANONICAL_BYTES))
        , m_work_limit(
              std::min(work_limit, HARD_MAX_CANONICAL_WORK))
        , m_work(0U)
        , m_ok(byte_limit != 0U && work_limit != 0U)
    {
    }

    bool Ok() const { return m_ok; }
    const std::string& Data() const { return m_data; }

    void Unit()
    {
        std::size_t next = 0U;
        if (!m_ok ||
            !AddSize(m_work, 1U, next) ||
            next > m_work_limit)
        {
            m_ok = false;
            return;
        }
        m_work = next;
    }

    void Byte(unsigned char value)
    {
        Unit();
        Append(
            reinterpret_cast<const char*>(&value),
            1U);
    }

    void Boolean(bool value)
    {
        Byte(value ? 1U : 0U);
    }

    void U64(std::uint64_t value)
    {
        Unit();
        unsigned char bytes[8];
        for (std::size_t i = 0U; i < 8U; ++i)
        {
            bytes[i] = static_cast<unsigned char>(
                (value >> (i * 8U)) & UINT64_C(0xff));
        }
        Append(
            reinterpret_cast<const char*>(bytes),
            sizeof(bytes));
    }

    void Size(std::size_t value)
    {
        U64(static_cast<std::uint64_t>(value));
    }

    void OptionalSize(std::size_t value)
    {
        const bool present = value != NO_INDEX;
        Boolean(present);
        if (present)
        {
            Size(value);
        }
    }

    void String(const std::string& value)
    {
        Size(value.size());
        Append(value.data(), value.size());
    }

    void Span(const JBeamSourceSpan& span)
    {
        String(span.source_name);
        U64(span.begin.byte_offset);
        U64(span.begin.line);
        U64(span.begin.column);
        U64(span.end.byte_offset);
        U64(span.end.line);
        U64(span.end.column);
    }

    bool Value(
        const std::shared_ptr<const JBeamValue>& value,
        std::size_t depth_limit)
    {
        Boolean(static_cast<bool>(value));
        if (!value)
        {
            return m_ok;
        }
        std::set<const JBeamValue*> stack;
        return ValueImpl(*value, 1U, depth_limit, stack);
    }

private:
    void Append(const char* data, std::size_t size)
    {
        if (!m_ok ||
            m_data.size() > m_byte_limit ||
            size > m_byte_limit - m_data.size())
        {
            m_ok = false;
            return;
        }
        m_data.append(data, size);
    }

    bool ValueImpl(
        const JBeamValue& value,
        std::size_t depth,
        std::size_t depth_limit,
        std::set<const JBeamValue*>& stack)
    {
        Unit();
        if (!m_ok ||
            depth > depth_limit ||
            !stack.insert(&value).second)
        {
            m_ok = false;
            return false;
        }
        Byte(static_cast<unsigned char>(value.type));
        Span(value.span);
        switch (value.type)
        {
        case JBeamValueType::NULL_VALUE:
            break;
        case JBeamValueType::BOOLEAN:
            Boolean(value.boolean_value);
            break;
        case JBeamValueType::NUMBER:
            String(value.scalar_text);
            U64(DoubleBits(value.number_value));
            break;
        case JBeamValueType::STRING:
            String(value.scalar_text);
            break;
        case JBeamValueType::ARRAY:
            Size(value.array_values.size());
            for (std::size_t i = 0U;
                 i < value.array_values.size() && m_ok;
                 ++i)
            {
                ValueImpl(
                    value.array_values[i],
                    depth + 1U,
                    depth_limit,
                    stack);
            }
            break;
        case JBeamValueType::OBJECT:
            Size(value.object_fields.size());
            for (std::size_t i = 0U;
                 i < value.object_fields.size() && m_ok;
                 ++i)
            {
                const JBeamObjectField& field =
                    value.object_fields[i];
                String(field.key);
                Span(field.key_span);
                Boolean(static_cast<bool>(field.value));
                if (field.value)
                {
                    ValueImpl(
                        *field.value,
                        depth + 1U,
                        depth_limit,
                        stack);
                }
            }
            break;
        }
        stack.erase(&value);
        return m_ok;
    }

    std::size_t m_byte_limit;
    std::size_t m_work_limit;
    std::size_t m_work;
    bool m_ok;
    std::string m_data;
};

void WriteStringField(
    CanonicalWriter& writer,
    const BeamNGMaterialStringField& field)
{
    writer.Boolean(field.present);
    writer.Boolean(field.type_valid);
    writer.String(field.value);
    writer.Span(field.span);
    writer.Size(field.assignment_count);
}

} // namespace

BeamNGMaterialDocumentationProfile::
    BeamNGMaterialDocumentationProfile()
    : profile_id("beamng-materials-docs-0.38.5.0-2026-07-27")
    , beamng_version("0.38.5.0")
    , source_url(
        "https://documentation.beamng.com/modding/file_formats/"
        "materials/")
    , last_modified("2026-07-08")
    , texture_cooker_url(
        "https://documentation.beamng.com/modding/materials/"
        "texture_cooker/")
    , texture_cooker_last_modified("2026-04-22")
{
}

const BeamNGMaterialDocumentationProfile&
GetBeamNGMaterialDocumentationProfile()
{
    static const BeamNGMaterialDocumentationProfile profile;
    return profile;
}

BeamNGMaterialSource::BeamNGMaterialSource()
{
}

BeamNGMaterialDiagnostic::BeamNGMaterialDiagnostic()
    : code(
          BeamNGMaterialDiagnosticCode::SOURCE_PARSE_DIAGNOSTIC)
    , severity(BeamNGMaterialSeverity::ERROR_SEVERITY)
    , source_index(NO_INDEX)
    , material_index(NO_INDEX)
{
}

BeamNGMaterialLimits::BeamNGMaterialLimits()
    : max_manifest_entries(200000U)
    , max_sources(4096U)
    , max_total_source_bytes(64U * 1024U * 1024U)
    , max_source_bytes(16U * 1024U * 1024U)
    , max_tokens_per_source(2000000U)
    , max_nodes_per_source(1000000U)
    , max_value_depth(128U)
    , max_string_bytes(1024U * 1024U)
    , max_materials(65536U)
    , max_fields(2000000U)
    , max_stages(524288U)
    , max_texture_references(524288U)
    , max_diagnostics(4096U)
    , max_retained_bytes(256U * 1024U * 1024U)
    , max_work_units(12000000U)
    , max_scoped_resource_name_bytes(256U * 1024U)
    , max_canonical_output_bytes(512U * 1024U * 1024U)
    , max_canonical_work_units(24000000U)
{
}

BeamNGMaterialStringField::BeamNGMaterialStringField()
    : present(false)
    , type_valid(false)
    , assignment_count(0U)
{
}

BeamNGMaterialFieldObservation::
    BeamNGMaterialFieldObservation()
    : kind(BeamNGMaterialFieldKind::CLASS_NAME)
    , scope(BeamNGMaterialFieldScope::ROOT_LAYER)
    , has_stages_assignment(false)
    , stages_assignment_index(0U)
    , stage_index(0U)
    , is_effective_assignment(false)
{
}

BeamNGMaterialTextureReference::
    BeamNGMaterialTextureReference()
    : field_kind(BeamNGMaterialFieldKind::BASE_COLOR_MAP)
    , scope(BeamNGMaterialFieldScope::ROOT_LAYER)
    , has_stages_assignment(false)
    , stages_assignment_index(0U)
    , stage_index(0U)
    , status(BeamNGTextureReferenceStatus::INVALID_PATH)
{
}

BeamNGMaterialSourceRecord::BeamNGMaterialSourceRecord()
{
}

BeamNGMaterialRecord::BeamNGMaterialRecord()
    : source_index(0U)
    , source_material_index(0U)
    , disposition(BeamNGMaterialDisposition::INVENTORY_ONLY)
    , authored_stage_count(0U)
{
}

BeamNGMaterialInventory::BeamNGMaterialInventory()
    : authored_field_count(0U)
    , authored_stage_count(0U)
    , texture_reference_count(0U)
    , retained_byte_count(0U)
    , work_unit_count(0U)
    , canonical_output_byte_limit(0U)
    , canonical_work_unit_limit(0U)
    , canonical_value_depth_limit(0U)
{
}

bool BeamNGMaterialInventory::IsValid() const
{
    for (std::size_t i = 0U; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].severity ==
            BeamNGMaterialSeverity::ERROR_SEVERITY)
        {
            return false;
        }
    }
    return true;
}

BeamNGMaterialInventory BuildBeamNGMaterialInventory(
    const std::string& package_namespace,
    const PackageManifest& manifest,
    const std::vector<BeamNGMaterialSource>& sources,
    const BeamNGMaterialLimits& limits)
{
    return MaterialInventoryBuilder(
        package_namespace,
        manifest,
        sources,
        limits).Run();
}

std::string SerializeCanonicalBeamNGMaterialInventory(
    const BeamNGMaterialInventory& inventory)
{
    CanonicalWriter writer(
        inventory.canonical_output_byte_limit,
        inventory.canonical_work_unit_limit);
    writer.String("ror-beamng-material-inventory-v1");
    writer.String(inventory.documentation_profile_id);
    writer.String(inventory.package_format_profile.identifier);
    writer.String(inventory.package_format_profile.version);
    writer.String(inventory.package_namespace);
    writer.Size(inventory.authored_field_count);
    writer.Size(inventory.authored_stage_count);
    writer.Size(inventory.texture_reference_count);
    writer.Size(inventory.retained_byte_count);
    writer.Size(inventory.work_unit_count);

    writer.Size(inventory.sources.size());
    for (std::size_t i = 0U;
         i < inventory.sources.size() && writer.Ok();
         ++i)
    {
        const BeamNGMaterialSourceRecord& source =
            inventory.sources[i];
        writer.String(source.package_path);
        writer.Span(source.root_span);
        writer.Size(source.material_indices.size());
        for (std::size_t j = 0U;
             j < source.material_indices.size() && writer.Ok();
             ++j)
        {
            writer.Size(source.material_indices[j]);
        }
    }

    writer.Size(inventory.materials.size());
    for (std::size_t i = 0U;
         i < inventory.materials.size() && writer.Ok();
         ++i)
    {
        const BeamNGMaterialRecord& material =
            inventory.materials[i];
        writer.Size(material.source_index);
        writer.Size(material.source_material_index);
        writer.String(material.material_key);
        writer.Span(material.material_key_span);
        writer.Size(material.same_key_material_indices.size());
        for (std::size_t j = 0U;
             j < material.same_key_material_indices.size() &&
                writer.Ok();
             ++j)
        {
            writer.Size(material.same_key_material_indices[j]);
        }
        writer.String(material.scoped_resource_name);
        writer.Byte(
            static_cast<unsigned char>(material.disposition));
        if (!writer.Value(
                material.raw_definition,
                inventory.canonical_value_depth_limit))
        {
            return std::string();
        }
        WriteStringField(writer, material.class_name);
        WriteStringField(writer, material.name);
        WriteStringField(writer, material.map_to);
        writer.Size(material.authored_stage_count);

        writer.Size(material.recognized_fields.size());
        for (std::size_t j = 0U;
             j < material.recognized_fields.size() &&
                writer.Ok();
             ++j)
        {
            const BeamNGMaterialFieldObservation& field =
                material.recognized_fields[j];
            writer.Byte(static_cast<unsigned char>(field.kind));
            writer.Byte(static_cast<unsigned char>(field.scope));
            writer.Boolean(field.has_stages_assignment);
            if (field.has_stages_assignment)
            {
                writer.Size(field.stages_assignment_index);
                writer.Size(field.stage_index);
            }
            writer.String(field.authored_name);
            writer.Span(field.field_span);
            writer.Boolean(field.is_effective_assignment);
            if (!writer.Value(
                    field.raw_value,
                    inventory.canonical_value_depth_limit))
            {
                return std::string();
            }
        }

        writer.Size(material.texture_references.size());
        for (std::size_t j = 0U;
             j < material.texture_references.size() &&
                writer.Ok();
             ++j)
        {
            const BeamNGMaterialTextureReference& texture =
                material.texture_references[j];
            writer.Byte(
                static_cast<unsigned char>(texture.field_kind));
            writer.Byte(
                static_cast<unsigned char>(texture.scope));
            writer.Boolean(texture.has_stages_assignment);
            if (texture.has_stages_assignment)
            {
                writer.Size(texture.stages_assignment_index);
                writer.Size(texture.stage_index);
            }
            writer.Size(texture.array_indices.size());
            for (std::size_t k = 0U;
                 k < texture.array_indices.size() && writer.Ok();
                 ++k)
            {
                writer.Size(texture.array_indices[k]);
            }
            writer.String(texture.raw_path);
            writer.Span(texture.value_span);
            writer.Byte(static_cast<unsigned char>(texture.status));
            writer.String(texture.candidate_path);
            writer.String(texture.resolved_manifest_path);
        }
    }

    writer.Size(inventory.diagnostics.size());
    for (std::size_t i = 0U;
         i < inventory.diagnostics.size() && writer.Ok();
         ++i)
    {
        const BeamNGMaterialDiagnostic& diagnostic =
            inventory.diagnostics[i];
        writer.Byte(static_cast<unsigned char>(diagnostic.code));
        writer.Byte(static_cast<unsigned char>(diagnostic.severity));
        writer.Span(diagnostic.span);
        writer.OptionalSize(diagnostic.source_index);
        writer.OptionalSize(diagnostic.material_index);
        writer.String(diagnostic.field_name);
        writer.String(diagnostic.detail);
    }
    return writer.Ok() ? writer.Data() : std::string();
}

const char* BeamNGMaterialDiagnosticCodeToString(
    BeamNGMaterialDiagnosticCode code)
{
    switch (code)
    {
    case BeamNGMaterialDiagnosticCode::INVALID_PACKAGE_NAMESPACE:
        return "invalid-package-namespace";
    case BeamNGMaterialDiagnosticCode::MANIFEST_ENTRY_LIMIT:
        return "manifest-entry-limit";
    case BeamNGMaterialDiagnosticCode::INVALID_MANIFEST_ENTRY:
        return "invalid-manifest-entry";
    case BeamNGMaterialDiagnosticCode::MANIFEST_DUPLICATE_PATH:
        return "manifest-duplicate-path";
    case BeamNGMaterialDiagnosticCode::MANIFEST_CASE_COLLISION:
        return "manifest-case-collision";
    case BeamNGMaterialDiagnosticCode::SOURCE_LIMIT:
        return "source-limit";
    case BeamNGMaterialDiagnosticCode::SOURCE_BYTE_LIMIT:
        return "source-byte-limit";
    case BeamNGMaterialDiagnosticCode::INVALID_SOURCE_PATH:
        return "invalid-source-path";
    case BeamNGMaterialDiagnosticCode::SOURCE_NOT_IN_MANIFEST:
        return "source-not-in-manifest";
    case BeamNGMaterialDiagnosticCode::SOURCE_NOT_REGULAR_FILE:
        return "source-not-regular-file";
    case BeamNGMaterialDiagnosticCode::SOURCE_NOT_MATERIAL_JSON:
        return "source-not-material-json";
    case BeamNGMaterialDiagnosticCode::MATERIAL_SOURCE_NOT_SUPPLIED:
        return "material-source-not-supplied";
    case BeamNGMaterialDiagnosticCode::DUPLICATE_SOURCE_PATH:
        return "duplicate-source-path";
    case BeamNGMaterialDiagnosticCode::SOURCE_PARSE_DIAGNOSTIC:
        return "source-parse-diagnostic";
    case BeamNGMaterialDiagnosticCode::SOURCE_ROOT_NOT_OBJECT:
        return "source-root-not-object";
    case BeamNGMaterialDiagnosticCode::MATERIAL_LIMIT:
        return "material-limit";
    case BeamNGMaterialDiagnosticCode::FIELD_LIMIT:
        return "field-limit";
    case BeamNGMaterialDiagnosticCode::STAGE_LIMIT:
        return "stage-limit";
    case BeamNGMaterialDiagnosticCode::TEXTURE_LIMIT:
        return "texture-limit";
    case BeamNGMaterialDiagnosticCode::RETAINED_BYTE_LIMIT:
        return "retained-byte-limit";
    case BeamNGMaterialDiagnosticCode::WORK_LIMIT:
        return "work-limit";
    case BeamNGMaterialDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case BeamNGMaterialDiagnosticCode::SCOPED_RESOURCE_NAME_LIMIT:
        return "scoped-resource-name-limit";
    case BeamNGMaterialDiagnosticCode::
        SCOPED_RESOURCE_NAME_COLLISION:
        return "scoped-resource-name-collision";
    case BeamNGMaterialDiagnosticCode::CANONICAL_OUTPUT_LIMIT:
        return "canonical-output-limit";
    case BeamNGMaterialDiagnosticCode::
        MATERIAL_DEFINITION_NOT_OBJECT:
        return "material-definition-not-object";
    case BeamNGMaterialDiagnosticCode::INVALID_CLASS_FIELD:
        return "invalid-class-field";
    case BeamNGMaterialDiagnosticCode::UNSUPPORTED_CLASS:
        return "unsupported-class";
    case BeamNGMaterialDiagnosticCode::INVALID_NAME_FIELD:
        return "invalid-name-field";
    case BeamNGMaterialDiagnosticCode::INVALID_MAP_TO_FIELD:
        return "invalid-map-to-field";
    case BeamNGMaterialDiagnosticCode::INVALID_STAGES_FIELD:
        return "invalid-stages-field";
    case BeamNGMaterialDiagnosticCode::INVALID_STAGE:
        return "invalid-stage";
    case BeamNGMaterialDiagnosticCode::INVALID_PBR_INPUT:
        return "invalid-pbr-input";
    case BeamNGMaterialDiagnosticCode::DUPLICATE_MATERIAL_KEY:
        return "duplicate-material-key";
    case BeamNGMaterialDiagnosticCode::DUPLICATE_MATERIAL_NAME:
        return "duplicate-material-name";
    case BeamNGMaterialDiagnosticCode::INVALID_TEXTURE_PATH:
        return "invalid-texture-path";
    case BeamNGMaterialDiagnosticCode::MISSING_TEXTURE:
        return "missing-texture";
    case BeamNGMaterialDiagnosticCode::TEXTURE_CASE_MISMATCH:
        return "texture-case-mismatch";
    }
    return "unknown";
}

const char* BeamNGMaterialDispositionToString(
    BeamNGMaterialDisposition disposition)
{
    switch (disposition)
    {
    case BeamNGMaterialDisposition::INVENTORY_ONLY:
        return "inventory-only";
    case BeamNGMaterialDisposition::PLACEHOLDER:
        return "placeholder";
    case BeamNGMaterialDisposition::PRESERVED_DISABLED:
        return "preserved-disabled";
    }
    return "preserved-disabled";
}

const char* BeamNGTextureReferenceStatusToString(
    BeamNGTextureReferenceStatus status)
{
    switch (status)
    {
    case BeamNGTextureReferenceStatus::LOCAL_FOUND:
        return "local-found";
    case BeamNGTextureReferenceStatus::LOCAL_COOKED_DDS:
        return "local-cooked-dds";
    case BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE:
        return "dynamic-texture";
    case BeamNGTextureReferenceStatus::LOCAL_MISSING:
        return "local-missing";
    case BeamNGTextureReferenceStatus::LOCAL_CASE_MISMATCH:
        return "local-case-mismatch";
    case BeamNGTextureReferenceStatus::INVALID_PATH:
        return "invalid-path";
    }
    return "invalid-path";
}

} // namespace BeamNG
} // namespace RoR
