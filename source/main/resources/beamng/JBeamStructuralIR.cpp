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

#include "JBeamStructuralIR.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <streambuf>
#include <utility>

namespace RoR {
namespace BeamNG {
namespace {

const std::size_t INVALID_INDEX = static_cast<std::size_t>(-1);

bool AddSize(std::size_t value, std::size_t& total)
{
    if (value > static_cast<std::size_t>(-1) - total)
    {
        return false;
    }
    total += value;
    return true;
}

bool AddProduct(
    std::size_t count,
    std::size_t element_size,
    std::size_t& total)
{
    if (count != 0U &&
        element_size >
            std::numeric_limits<std::size_t>::max() / count)
    {
        return false;
    }
    return AddSize(count * element_size, total);
}

struct PartContext
{
    const JBeamResolvedPartNode* node;
    JBeamStructuralProvenance provenance;
    /// Resolved configuration and slot-variable assignments only. Component
    /// leaves are owned once by StructuralBuilder and prepended on demand.
    std::shared_ptr<std::vector<JBeamExpressionVariable> > variables;
};

struct SectionOccurrence
{
    const JBeamValue* value;
    JBeamSourceSpan span;
};

struct PartSections
{
    PartContext part;
    std::map<std::string, std::vector<SectionOccurrence> > sections;
};

bool IsError(const JBeamStructuralDiagnostic& diagnostic)
{
    return diagnostic.severity == JBeamStructuralSeverity::ERROR_SEVERITY;
}

bool HasErrors(const std::vector<JBeamStructuralDiagnostic>& diagnostics)
{
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (IsError(diagnostics[i]))
        {
            return true;
        }
    }
    return false;
}

bool IsDiagnosticLimit(const JBeamStructuralDiagnostic& diagnostic)
{
    return diagnostic.code ==
        JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT;
}

JBeamStructuralDiagnostic MakeDiagnostic(
    JBeamStructuralDiagnosticCode code,
    JBeamStructuralSeverity severity,
    const JBeamStructuralProvenance& provenance,
    const std::string& section,
    std::size_t row_index,
    const std::string& field_name,
    const std::string& detail)
{
    JBeamStructuralDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.provenance = provenance;
    diagnostic.section = section;
    diagnostic.row_index = row_index;
    diagnostic.field_name = field_name;
    diagnostic.detail = detail;
    return diagnostic;
}

JBeamStructuralDiagnostic MakeDiagnosticLimit()
{
    return MakeDiagnostic(
        JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT,
        JBeamStructuralSeverity::ERROR_SEVERITY,
        JBeamStructuralProvenance(),
        std::string(),
        0U,
        std::string(),
        "Additional diagnostics were deterministically suppressed");
}

void PushDiagnostic(
    std::vector<JBeamStructuralDiagnostic>& diagnostics,
    std::size_t configured_limit,
    const JBeamStructuralDiagnostic& diagnostic)
{
    if (!diagnostics.empty() &&
        IsDiagnosticLimit(diagnostics.back()))
    {
        return;
    }
    if (configured_limit == 0U)
    {
        diagnostics.push_back(MakeDiagnosticLimit());
        return;
    }
    if (diagnostics.size() < configured_limit)
    {
        diagnostics.push_back(diagnostic);
        return;
    }
    diagnostics[configured_limit - 1U] = MakeDiagnosticLimit();
}

bool DiagnosticLess(
    const JBeamStructuralDiagnostic& left,
    const JBeamStructuralDiagnostic& right)
{
    if (IsDiagnosticLimit(left) != IsDiagnosticLimit(right))
    {
        return !IsDiagnosticLimit(left);
    }
    if (left.provenance.PartPreorderIndex() !=
        right.provenance.PartPreorderIndex())
    {
        return left.provenance.PartPreorderIndex() <
            right.provenance.PartPreorderIndex();
    }
    if (left.provenance.SourceName() !=
        right.provenance.SourceName())
    {
        return left.provenance.SourceName() <
            right.provenance.SourceName();
    }
    if (left.provenance.begin.byte_offset !=
        right.provenance.begin.byte_offset)
    {
        return left.provenance.begin.byte_offset <
            right.provenance.begin.byte_offset;
    }
    if (left.section != right.section)
    {
        return left.section < right.section;
    }
    if (left.row_index != right.row_index)
    {
        return left.row_index < right.row_index;
    }
    if (left.field_name != right.field_name)
    {
        return left.field_name < right.field_name;
    }
    if (left.code != right.code)
    {
        return static_cast<int>(left.code) <
            static_cast<int>(right.code);
    }
    return left.detail < right.detail;
}

void SortDiagnostics(std::vector<JBeamStructuralDiagnostic>& diagnostics)
{
    std::stable_sort(
        diagnostics.begin(), diagnostics.end(), DiagnosticLess);
}

bool IsFiniteDouble(double value)
{
    // std::isfinite may be folded to true under -ffast-math. Inspect the
    // IEEE-754 exponent instead so hostile hand-built ASTs remain rejected.
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "JBeam structural conversion requires binary64 doubles");
    volatile unsigned char stored[sizeof(double)];
    const unsigned char* const source =
        reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0U; i < sizeof(double); ++i)
    {
        stored[i] = source[i];
    }
    std::uint64_t bits = 0U;
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(double); ++i)
    {
        destination[i] = stored[i];
    }
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

double Abs(double value)
{
    return value < 0.0 ? -value : value;
}

bool IsExpressionString(const JBeamValue& value)
{
    return value.type == JBeamValueType::STRING &&
        !value.scalar_text.empty() &&
        value.scalar_text[0] == '$';
}

bool IsAsciiAlpha(unsigned char value)
{
    return (value >= static_cast<unsigned char>('a') &&
            value <= static_cast<unsigned char>('z')) ||
        (value >= static_cast<unsigned char>('A') &&
         value <= static_cast<unsigned char>('Z'));
}

bool IsAsciiDigit(unsigned char value)
{
    return value >= static_cast<unsigned char>('0') &&
        value <= static_cast<unsigned char>('9');
}

bool IsComponentPathSegment(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }
    const unsigned char first =
        static_cast<unsigned char>(value[0]);
    if (!IsAsciiAlpha(first) &&
        first != static_cast<unsigned char>('_'))
    {
        return false;
    }
    for (std::size_t i = 1U; i < value.size(); ++i)
    {
        const unsigned char current =
            static_cast<unsigned char>(value[i]);
        if (!IsAsciiAlpha(current) &&
            !IsAsciiDigit(current) &&
            current != static_cast<unsigned char>('_'))
        {
            return false;
        }
    }
    return true;
}

bool IsAsciiSpace(unsigned char value)
{
    return value == static_cast<unsigned char>(' ') ||
        value == static_cast<unsigned char>('\t') ||
        value == static_cast<unsigned char>('\n') ||
        value == static_cast<unsigned char>('\r');
}

bool ParseExactComponentReference(
    const JBeamValue& value,
    std::string& path)
{
    path.clear();
    if (value.type != JBeamValueType::STRING ||
        value.scalar_text.compare(0U, 2U, "$=") != 0)
    {
        return false;
    }

    std::size_t begin = 2U;
    while (begin < value.scalar_text.size() &&
           IsAsciiSpace(static_cast<unsigned char>(
               value.scalar_text[begin])))
    {
        ++begin;
    }
    std::size_t end = value.scalar_text.size();
    while (end > begin &&
           IsAsciiSpace(static_cast<unsigned char>(
               value.scalar_text[end - 1U])))
    {
        --end;
    }
    static const char COMPONENT_PREFIX[] = "$components.";
    static const std::size_t COMPONENT_PREFIX_SIZE =
        sizeof(COMPONENT_PREFIX) - 1U;
    if (end - begin <= COMPONENT_PREFIX_SIZE ||
        value.scalar_text.compare(
            begin, COMPONENT_PREFIX_SIZE, COMPONENT_PREFIX) != 0)
    {
        return false;
    }

    path.assign(
        value.scalar_text,
        begin + COMPONENT_PREFIX_SIZE,
        end - begin - COMPONENT_PREFIX_SIZE);
    std::size_t segment_begin = 0U;
    while (segment_begin < path.size())
    {
        const std::size_t separator = path.find('.', segment_begin);
        const std::size_t segment_end = separator == std::string::npos
            ? path.size()
            : separator;
        if (!IsComponentPathSegment(path.substr(
                segment_begin, segment_end - segment_begin)))
        {
            path.clear();
            return false;
        }
        if (separator == std::string::npos)
        {
            return true;
        }
        segment_begin = separator + 1U;
    }
    path.clear();
    return false;
}

bool IsRecognizedSection(const std::string& name)
{
    return name == "nodes" ||
        name == "beams" ||
        name == "triangles" ||
        name == "quads" ||
        name == "refNodes";
}

bool IsResolverMetadataSection(const std::string& name)
{
    return name == "slotType" ||
        name == "slots" ||
        name == "slots2" ||
        name == "components" ||
        name == "variables";
}

bool IsKnownUnsupportedField(
    const std::string& section,
    const std::string& field)
{
    if (section == "nodes")
    {
        return field == "collision" ||
            field == "selfCollision" ||
            field == "group" ||
            field == "nodeMaterial" ||
            field == "frictionCoef" ||
            field == "volumeCoef" ||
            field == "surfaceCoef" ||
            field == "flashPoint" ||
            field == "smokePoint" ||
            field == "tag";
    }
    if (section == "beams")
    {
        return field == "breakGroup" ||
            field == "deformGroup" ||
            field == "beamLimitSpring" ||
            field == "beamLimitDamp" ||
            field == "beamLongBound" ||
            field == "beamShortBound" ||
            field == "dampCutoffHz" ||
            field == "soundFile" ||
            field == "colorFactor" ||
            field == "disableMeshBreaking" ||
            field == "name";
    }
    if (section == "triangles" || section == "quads")
    {
        return field == "dragCoef" ||
            field == "skinDragCoef" ||
            field == "liftCoef" ||
            field == "stallAngle" ||
            field == "groundModel" ||
            field == "pressureGroup" ||
            field == "triangleType" ||
            field == "collision" ||
            field == "optional" ||
            field == "name";
    }
    if (section == "refNodes")
    {
        return false;
    }
    return false;
}

bool IsImplementedField(
    const std::string& section,
    const std::string& field)
{
    if (section == "nodes")
    {
        return field == "id" ||
            field == "posX" ||
            field == "posY" ||
            field == "posZ" ||
            field == "nodeWeight";
    }
    if (section == "beams")
    {
        return field == "id1:" ||
            field == "id1" ||
            field == "id2:" ||
            field == "id2" ||
            field == "beamType" ||
            field == "optional" ||
            field == "beamSpring" ||
            field == "beamDamp" ||
            field == "beamDeform" ||
            field == "beamStrength" ||
            field == "beamPrecompression";
    }
    if (section == "triangles")
    {
        return field == "id1:" ||
            field == "id1" ||
            field == "id2:" ||
            field == "id2" ||
            field == "id3:" ||
            field == "id3" ||
            field == "optional";
    }
    if (section == "quads")
    {
        return field == "id1:" ||
            field == "id1" ||
            field == "id2:" ||
            field == "id2" ||
            field == "id3:" ||
            field == "id3" ||
            field == "id4:" ||
            field == "id4" ||
            field == "optional";
    }
    if (section == "refNodes")
    {
        return field == "ref:" ||
            field == "ref" ||
            field == "back:" ||
            field == "back" ||
            field == "left:" ||
            field == "left" ||
            field == "up:" ||
            field == "up" ||
            field == "leftCorner:" ||
            field == "leftCorner" ||
            field == "rightCorner:" ||
            field == "rightCorner";
    }
    return false;
}

class StructuralBuilder
{
public:
    StructuralBuilder(
        const JBeamResolvedGraph& graph,
        const JBeamStructuralLimits& limits)
        : m_graph(graph)
        , m_limits(limits)
        , m_resource_limit(false)
        , m_ref_row_count(0U)
        , m_authored_beam_rows(0U)
        , m_retained_bytes(0U)
        , m_semantic_bytes(0U)
        , m_expression_evaluations(0U)
        , m_expression_work_units(0U)
        , m_component_nodes(0U)
        , m_component_string_bytes(0U)
    {
        m_result.canonical_output_byte_limit =
            limits.max_canonical_output_bytes;
        m_result.canonical_work_unit_limit =
            limits.max_canonical_work_units;
    }

    JBeamStructuralIR Run()
    {
        if (!m_graph.IsValid())
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_RESOLVED_GRAPH,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                JBeamStructuralProvenance(),
                std::string(),
                0U,
                std::string(),
                "Structural conversion requires a valid resolved graph");
            Finish();
            return m_result;
        }
        CollectParts();
        if (!m_resource_limit)
        {
            BuildComponentEnvironment();
        }
        if (!m_resource_limit)
        {
            BuildPartVariableEnvironments();
        }
        if (!m_resource_limit)
        {
            DiscoverSections();
        }
        if (!m_resource_limit)
        {
            PreflightRows();
        }
        if (!m_resource_limit)
        {
            ProcessNodes();
        }
        if (!m_resource_limit)
        {
            ProcessRefNodes();
        }
        if (!m_resource_limit)
        {
            ProcessBeams();
        }
        if (!m_resource_limit)
        {
            ProcessTriangles();
        }
        if (!m_resource_limit)
        {
            ProcessQuads();
        }
        if (m_ref_row_count == 0U && !m_resource_limit)
        {
            Push(
                JBeamStructuralDiagnosticCode::MISSING_REF_NODES,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                RootProvenance(),
                "refNodes",
                0U,
                std::string(),
                "Exactly one refNodes frame is required");
        }
        else if (m_ref_row_count > 1U && !m_resource_limit)
        {
            Push(
                JBeamStructuralDiagnosticCode::DUPLICATE_REF_NODES,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                RootProvenance(),
                "refNodes",
                0U,
                std::string(),
                "More than one refNodes data row is ambiguous");
            m_result.has_ref_frame = false;
        }
        Finish();
        return m_result;
    }

private:
    const JBeamResolvedGraph& m_graph;
    const JBeamStructuralLimits& m_limits;
    JBeamStructuralIR m_result;
    std::vector<PartContext> m_parts;
    std::vector<PartSections> m_sections;
    std::map<std::string, std::size_t> m_node_indices;
    bool m_resource_limit;
    std::size_t m_ref_row_count;
    std::size_t m_authored_beam_rows;
    std::size_t m_retained_bytes;
    std::size_t m_semantic_bytes;
    std::size_t m_expression_evaluations;
    std::size_t m_expression_work_units;
    std::size_t m_component_nodes;
    std::size_t m_component_string_bytes;
    std::vector<JBeamExpressionVariable> m_components;
    /// Exact flattened paths that still denote non-scalar values. false
    /// allows scalar descendants of an object; true also blocks descendants
    /// because an array or unsupported expression replaced the subtree.
    std::map<std::string, bool> m_non_scalar_components;
    /// Exact effective array-valued component leaves. These remain source
    /// owned and are copied only after a structural table uses an exact
    /// `$=$components.path` row reference and the complete copy has passed
    /// the retained-memory preflight.
    std::map<std::string, std::shared_ptr<const JBeamValue> >
        m_component_rows;
    std::map<
        std::string,
        std::shared_ptr<const std::string> > m_source_names;
    std::set<const JBeamValue*> m_measured_values;

    enum class ValueMeasureStatus
    {
        OK,
        BYTE_LIMIT,
        COMPLEXITY_LIMIT,
        INVALID_GRAPH
    };

    struct PendingValue
    {
        const JBeamValue* value;
        std::size_t depth;
        std::size_t header_bytes;
    };

    struct ValueMeasurement
    {
        std::size_t bytes;
        std::set<const JBeamValue*> addresses;

        ValueMeasurement()
            : bytes(0U)
        {
        }
    };

    JBeamStructuralProvenance RootProvenance() const
    {
        return m_parts.empty()
            ? JBeamStructuralProvenance()
            : m_parts[0].provenance;
    }

    void RejectRetained()
    {
        JBeamStructuralDiagnostic diagnostic = MakeDiagnostic(
            JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT,
            JBeamStructuralSeverity::ERROR_SEVERITY,
            JBeamStructuralProvenance(),
            std::string(),
            0U,
            std::string(),
            "Structural IR exceeds the retained-byte budget");
        PushDiagnostic(
            m_result.diagnostics,
            m_limits.max_diagnostics,
            diagnostic);
        m_resource_limit = true;
    }

    bool ReserveRetained(std::size_t bytes)
    {
        std::size_t admitted = m_retained_bytes;
        if (!AddSize(m_semantic_bytes, admitted))
        {
            RejectRetained();
            return false;
        }
        if (bytes >
            m_limits.max_retained_bytes -
                std::min(
                    admitted,
                    m_limits.max_retained_bytes))
        {
            RejectRetained();
            return false;
        }
        m_retained_bytes += bytes;
        m_result.retained_byte_count = m_retained_bytes;
        return true;
    }

    bool ReserveSemantic(std::size_t bytes)
    {
        std::size_t admitted = m_retained_bytes;
        if (!AddSize(m_semantic_bytes, admitted) ||
            bytes >
                m_limits.max_retained_bytes -
                    std::min(
                        admitted,
                        m_limits.max_retained_bytes))
        {
            RejectRetained();
            return false;
        }
        m_semantic_bytes += bytes;
        return true;
    }

    ValueMeasureStatus MeasureValue(
        const JBeamValue& value,
        std::size_t remaining,
        bool honor_already_retained,
        ValueMeasurement& measurement) const
    {
        static const std::size_t SHARED_ALLOCATION_OVERHEAD = 64U;
        measurement = ValueMeasurement();
        std::size_t work_units = 0U;
        std::vector<PendingValue> pending;
        PendingValue root;
        root.value = &value;
        root.depth = 0U;
        root.header_bytes =
            sizeof(JBeamValue) + SHARED_ALLOCATION_OVERHEAD;
        pending.push_back(root);

        while (!pending.empty())
        {
            const PendingValue current = pending.back();
            pending.pop_back();
            if (honor_already_retained &&
                m_measured_values.find(current.value) !=
                    m_measured_values.end())
            {
                continue;
            }
            if (!measurement.addresses.insert(
                    current.value).second)
            {
                // Parsed JBeam is a tree. Repeated addresses indicate a
                // hand-built alias or cycle, neither of which has a canonical
                // retained-size ownership model.
                return ValueMeasureStatus::INVALID_GRAPH;
            }
            if (current.depth >
                    m_limits.max_preserved_value_depth ||
                work_units >=
                    m_limits.max_preserved_value_work_units)
            {
                return ValueMeasureStatus::COMPLEXITY_LIMIT;
            }
            ++work_units;

            std::size_t bytes = current.header_bytes;
            if (!AddSize(current.value->span.source_name.size(), bytes) ||
                !AddSize(current.value->scalar_text.size(), bytes))
            {
                return ValueMeasureStatus::BYTE_LIMIT;
            }
            if (current.value->type == JBeamValueType::ARRAY)
            {
                if (!AddProduct(
                        current.value->array_values.capacity(),
                        sizeof(JBeamValue),
                        bytes))
                {
                    return ValueMeasureStatus::BYTE_LIMIT;
                }
                const std::size_t child_count =
                    current.value->array_values.size();
                if (child_count >
                    m_limits.max_preserved_value_work_units -
                        std::min(
                            work_units,
                            m_limits.max_preserved_value_work_units))
                {
                    return ValueMeasureStatus::COMPLEXITY_LIMIT;
                }
                if (bytes > remaining - std::min(
                        measurement.bytes, remaining))
                {
                    return ValueMeasureStatus::BYTE_LIMIT;
                }
                measurement.bytes += bytes;
                for (std::size_t i = child_count; i > 0U; --i)
                {
                    PendingValue child;
                    child.value =
                        &current.value->array_values[i - 1U];
                    child.depth = current.depth + 1U;
                    // The array allocation already owns each child's fixed
                    // JBeamValue storage.
                    child.header_bytes = 0U;
                    pending.push_back(child);
                }
                continue;
            }
            if (current.value->type == JBeamValueType::OBJECT)
            {
                if (!AddProduct(
                        current.value->object_fields.capacity(),
                        sizeof(JBeamObjectField),
                        bytes))
                {
                    return ValueMeasureStatus::BYTE_LIMIT;
                }
                const std::size_t field_count =
                    current.value->object_fields.size();
                if (field_count >
                    m_limits.max_preserved_value_work_units -
                        std::min(
                            work_units,
                            m_limits.max_preserved_value_work_units))
                {
                    return ValueMeasureStatus::COMPLEXITY_LIMIT;
                }
                work_units += field_count;
                for (std::size_t i = 0U; i < field_count; ++i)
                {
                    const JBeamObjectField& field =
                        current.value->object_fields[i];
                    if (!AddSize(field.key.size(), bytes) ||
                        !AddSize(
                            field.key_span.source_name.size(), bytes))
                    {
                        return ValueMeasureStatus::BYTE_LIMIT;
                    }
                }
                if (bytes > remaining - std::min(
                        measurement.bytes, remaining))
                {
                    return ValueMeasureStatus::BYTE_LIMIT;
                }
                measurement.bytes += bytes;
                for (std::size_t i = field_count; i > 0U; --i)
                {
                    const JBeamObjectField& field =
                        current.value->object_fields[i - 1U];
                    if (field.value)
                    {
                        PendingValue child;
                        child.value = field.value.get();
                        child.depth = current.depth + 1U;
                        child.header_bytes =
                            sizeof(JBeamValue) +
                            SHARED_ALLOCATION_OVERHEAD;
                        pending.push_back(child);
                    }
                }
                continue;
            }
            if (bytes > remaining - std::min(
                    measurement.bytes, remaining))
            {
                return ValueMeasureStatus::BYTE_LIMIT;
            }
            measurement.bytes += bytes;
        }
        return ValueMeasureStatus::OK;
    }

    std::shared_ptr<const std::string> InternSourceName(
        const std::string& source_name)
    {
        const std::map<
            std::string,
            std::shared_ptr<const std::string> >::const_iterator found =
                m_source_names.find(source_name);
        if (found != m_source_names.end())
        {
            return found->second;
        }
        if (!ReserveRetained(source_name.size()))
        {
            return std::shared_ptr<const std::string>();
        }
        const std::shared_ptr<const std::string> shared(
            new std::string(source_name));
        m_source_names.insert(std::make_pair(source_name, shared));
        return shared;
    }

    JBeamStructuralProvenance ProvenanceWithSpan(
        const JBeamStructuralProvenance& provenance,
        const JBeamSourceSpan& span)
    {
        JBeamStructuralProvenance result = provenance;
        result.source_name = InternSourceName(span.source_name);
        result.begin = span.begin;
        result.end = span.end;
        return result;
    }

    void Push(
        JBeamStructuralDiagnosticCode code,
        JBeamStructuralSeverity severity,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail)
    {
        std::size_t payload = 0U;
        if (!AddSize(section.size(), payload) ||
            !AddSize(field_name.size(), payload) ||
            !AddSize(detail.size(), payload))
        {
            RejectRetained();
            return;
        }
        if (code !=
                JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT &&
            !ReserveRetained(payload))
        {
            return;
        }
        PushDiagnostic(
            m_result.diagnostics,
            m_limits.max_diagnostics,
            MakeDiagnostic(
                code,
                severity,
                provenance,
                section,
                row_index,
                field_name,
                detail));
        if (!m_result.diagnostics.empty() &&
            IsDiagnosticLimit(m_result.diagnostics.back()))
        {
            m_resource_limit = true;
        }
    }

    void RejectValueMeasurement(
        ValueMeasureStatus status,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name)
    {
        if (status == ValueMeasureStatus::BYTE_LIMIT)
        {
            RejectRetained();
            return;
        }
        Push(
            JBeamStructuralDiagnosticCode::PRESERVED_VALUE_LIMIT,
            JBeamStructuralSeverity::ERROR_SEVERITY,
            provenance,
            section,
            row_index,
            field_name,
            status == ValueMeasureStatus::INVALID_GRAPH
                ? "Preserved value contains an alias or cycle"
                : "Preserved value exceeds its depth or work budget");
        m_resource_limit = true;
    }

    bool ReservePreserved(
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail,
        const JBeamValue* value,
        bool honor_already_retained,
        bool commit_addresses)
    {
        std::size_t string_bytes = 0U;
        if (!AddSize(section.size(), string_bytes) ||
            !AddSize(field_name.size(), string_bytes) ||
            !AddSize(detail.size(), string_bytes))
        {
            RejectRetained();
            return false;
        }
        const std::size_t remaining =
            m_limits.max_retained_bytes -
                std::min(
                    m_retained_bytes,
                    m_limits.max_retained_bytes);
        if (string_bytes > remaining)
        {
            RejectRetained();
            return false;
        }

        ValueMeasurement measurement;
        if (value != NULL)
        {
            const ValueMeasureStatus status = MeasureValue(
                *value,
                remaining - string_bytes,
                honor_already_retained,
                measurement);
            if (status != ValueMeasureStatus::OK)
            {
                RejectValueMeasurement(
                    status,
                    provenance,
                    section,
                    row_index,
                    field_name);
                return false;
            }
        }
        if (measurement.bytes >
            remaining - string_bytes)
        {
            RejectRetained();
            return false;
        }
        if (!ReserveRetained(
                string_bytes + measurement.bytes))
        {
            return false;
        }
        if (commit_addresses)
        {
            m_measured_values.insert(
                measurement.addresses.begin(),
                measurement.addresses.end());
        }
        return true;
    }

    void EmitPreserved(
        JBeamStructuralDiagnosticCode code,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail,
        const std::shared_ptr<const JBeamValue>& value)
    {
        JBeamStructuralDiagnostic diagnostic = MakeDiagnostic(
            code,
            JBeamStructuralSeverity::WARNING,
            provenance,
            section,
            row_index,
            field_name,
            detail);
        diagnostic.has_preserved_value = static_cast<bool>(value);
        diagnostic.preserved_value = value;
        PushDiagnostic(
            m_result.diagnostics,
            m_limits.max_diagnostics,
            diagnostic);
        if (!m_result.diagnostics.empty() &&
            IsDiagnosticLimit(m_result.diagnostics.back()))
        {
            m_resource_limit = true;
        }
    }

    void PushPreserved(
        JBeamStructuralDiagnosticCode code,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail,
        const std::shared_ptr<const JBeamValue>& value)
    {
        if (!ReservePreserved(
                provenance,
                section,
                row_index,
                field_name,
                detail,
                value.get(),
                true,
                true))
        {
            return;
        }
        EmitPreserved(
            code,
            provenance,
            section,
            row_index,
            field_name,
            detail,
            value);
    }

    void PushPreservedCopy(
        JBeamStructuralDiagnosticCode code,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail,
        const JBeamValue& value)
    {
        // Measure the complete fixed/dynamic tree before invoking its copy
        // constructor. Copies are charged independently because their embedded
        // array storage is newly allocated.
        if (!ReservePreserved(
                provenance,
                section,
                row_index,
                field_name,
                detail,
                &value,
                false,
                false))
        {
            return;
        }
        const std::shared_ptr<const JBeamValue> copy(
            new JBeamValue(value));
        EmitPreserved(
            code,
            provenance,
            section,
            row_index,
            field_name,
            detail,
            copy);
    }

    void Finish()
    {
        SortDiagnostics(m_result.diagnostics);
    }

    void CollectParts()
    {
        std::vector<const JBeamResolvedPartNode*> pending;
        std::set<const JBeamResolvedPartNode*> discovered;
        const JBeamResolvedPartNode* root = m_graph.root.get();
        pending.push_back(root);
        discovered.insert(root);
        while (!pending.empty())
        {
            const JBeamResolvedPartNode* node = pending.back();
            pending.pop_back();
            if (node == NULL)
            {
                continue;
            }
            if (m_parts.size() >= m_limits.max_parts)
            {
                Push(
                    JBeamStructuralDiagnosticCode::RESOLVED_PART_LIMIT,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    JBeamStructuralProvenance(),
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved graph exceeds the configured part limit");
                m_resource_limit = true;
                return;
            }
            PartContext part;
            part.node = node;
            part.variables.reset(
                new std::vector<JBeamExpressionVariable>());
            std::size_t identity_bytes = 0U;
            if (!AddSize(
                    node->definition.name.size(),
                    identity_bytes) ||
                !AddSize(
                    node->definition.package_path.size(),
                    identity_bytes))
            {
                RejectRetained();
                return;
            }
            if (!ReserveRetained(identity_bytes))
            {
                return;
            }
            std::shared_ptr<JBeamStructuralPartIdentity> identity(
                new JBeamStructuralPartIdentity());
            identity->part_preorder_index = m_parts.size();
            identity->part_name = node->definition.name;
            identity->package_path =
                node->definition.package_path;
            part.provenance.part = identity;
            part.provenance.source_name = InternSourceName(
                node->definition.name_span.source_name);
            if (m_resource_limit)
            {
                return;
            }
            part.provenance.begin =
                node->definition.name_span.begin;
            part.provenance.end = node->definition.name_span.end;
            m_parts.push_back(part);
            JBeamStructuralPart output_part;
            output_part.provenance = part.provenance;
            m_result.parts.push_back(output_part);

            for (std::size_t i = node->slots.size(); i > 0U; --i)
            {
                const JBeamResolvedSlot& slot = node->slots[i - 1U];
                if (slot.child)
                {
                    const JBeamResolvedPartNode* child =
                        slot.child.get();
                    if (!discovered.insert(child).second)
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                INVALID_RESOLVED_GRAPH,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            part.provenance,
                            std::string(),
                            0U,
                            std::string(),
                            "Resolved part graph contains a cycle or "
                            "aliased child");
                        m_resource_limit = true;
                        return;
                    }
                    if (discovered.size() > m_limits.max_parts)
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                RESOLVED_PART_LIMIT,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            part.provenance,
                            std::string(),
                            0U,
                            std::string(),
                            "Resolved graph exceeds the configured "
                            "part limit");
                        m_resource_limit = true;
                        return;
                    }
                    pending.push_back(child);
                }
            }
        }
    }

    bool ChargeSemanticWork(
        std::size_t units,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        const std::string& field_name,
        const std::string& detail)
    {
        if (units >
            m_limits.max_expression_work_units -
                std::min(
                    m_expression_work_units,
                    m_limits.max_expression_work_units))
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                0U,
                field_name,
                detail);
            m_resource_limit = true;
            return false;
        }
        m_expression_work_units += units;
        return true;
    }

    bool IsSimpleExpressionVariableName(
        const std::string& name) const
    {
        return name.size() >= 2U &&
            name[0] == '$' &&
            IsComponentPathSegment(name.substr(1U)) &&
            name.size() <=
                m_limits.expression_limits.max_variable_name_bytes;
    }

    bool ComposeExpressionEnvironment(
        const std::vector<JBeamExpressionVariable>& local_variables,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        const std::string& field_name,
        JBeamExpressionEnvironment& environment)
    {
        if (m_components.size() >
                m_limits.expression_limits.max_variables ||
            local_variables.size() >
                m_limits.expression_limits.max_variables -
                    m_components.size())
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                0U,
                field_name,
                "Expression environment exceeds the configured "
                "variable limit");
            m_resource_limit = true;
            return false;
        }
        const std::size_t count =
            m_components.size() + local_variables.size();
        if (!ChargeSemanticWork(
                count,
                provenance,
                section,
                field_name,
                "Expression environment construction exceeds the "
                "aggregate work limit"))
        {
            return false;
        }
        environment.variables.reserve(count);
        environment.variables.insert(
            environment.variables.end(),
            m_components.begin(),
            m_components.end());
        environment.variables.insert(
            environment.variables.end(),
            local_variables.begin(),
            local_variables.end());
        return true;
    }

    bool FindUnsupportedComponentReference(
        const std::string& expression,
        std::string& rejected_path) const
    {
        std::size_t index = 2U;
        while (index < expression.size())
        {
            if (expression[index] == '\'')
            {
                ++index;
                while (index < expression.size())
                {
                    if (expression[index] == '\\' &&
                        index + 1U < expression.size())
                    {
                        index += 2U;
                        continue;
                    }
                    if (expression[index++] == '\'')
                    {
                        break;
                    }
                }
                continue;
            }
            if (expression[index] != '$')
            {
                ++index;
                continue;
            }
            const std::size_t begin = index++;
            if (index >= expression.size() ||
                (!IsAsciiAlpha(
                    static_cast<unsigned char>(expression[index])) &&
                 expression[index] != '_'))
            {
                continue;
            }
            ++index;
            while (index < expression.size())
            {
                const unsigned char current =
                    static_cast<unsigned char>(expression[index]);
                if (IsAsciiAlpha(current) ||
                    IsAsciiDigit(current) ||
                    current == static_cast<unsigned char>('_'))
                {
                    ++index;
                    continue;
                }
                if (current == static_cast<unsigned char>('.') &&
                    index + 1U < expression.size() &&
                    expression[index + 1U] != '.' &&
                    (IsAsciiAlpha(static_cast<unsigned char>(
                         expression[index + 1U])) ||
                     expression[index + 1U] == '_'))
                {
                    ++index;
                    continue;
                }
                break;
            }
            const std::string token =
                expression.substr(begin, index - begin);
            if (token.compare(0U, 12U, "$components.") != 0)
            {
                continue;
            }
            const std::string component_path = token.substr(12U);
            std::map<std::string, bool>::const_iterator exact =
                m_non_scalar_components.find(component_path);
            if (exact != m_non_scalar_components.end())
            {
                rejected_path = token;
                return true;
            }
            std::string::size_type separator =
                component_path.find_last_of('.');
            while (separator != std::string::npos &&
                   separator != 0U)
            {
                const std::string prefix =
                    component_path.substr(0U, separator);
                const std::map<std::string, bool>::const_iterator
                    parent = m_non_scalar_components.find(prefix);
                if (parent != m_non_scalar_components.end() &&
                    parent->second)
                {
                    rejected_path = "$components." + prefix;
                    return true;
                }
                separator = prefix.find_last_of('.');
            }
        }
        return false;
    }

    bool EvaluateExpressionText(
        const std::string& expression,
        const std::vector<JBeamExpressionVariable>& local_variables,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        JBeamExpressionValue& output)
    {
        if (m_expression_evaluations >=
            m_limits.max_expression_evaluations)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Structural lowering exceeds the configured expression "
                "evaluation limit");
            m_resource_limit = true;
            return false;
        }
        if (expression.size() >
            m_limits.expression_limits.max_expression_bytes)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Expression exceeds the configured byte limit");
            return false;
        }
        if (!ChargeSemanticWork(
                expression.size(),
                provenance,
                section,
                field_name,
                "Expression scanning exceeds the aggregate semantic "
                "work limit"))
        {
            return false;
        }
        std::string unsupported_component;
        if (FindUnsupportedComponentReference(
                expression, unsupported_component))
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_ERROR,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Expression references non-scalar component '" +
                    unsupported_component +
                    "', which is preserved but not allowlisted");
            return false;
        }

        JBeamExpressionEnvironment environment;
        if (!ComposeExpressionEnvironment(
                local_variables,
                provenance,
                section,
                field_name,
                environment))
        {
            return false;
        }
        if (m_expression_work_units >=
            m_limits.max_expression_work_units)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Structural lowering exhausted the aggregate expression "
                "work limit");
            m_resource_limit = true;
            return false;
        }

        JBeamExpressionLimits limits =
            m_limits.expression_limits;
        const std::size_t remaining_work =
            m_limits.max_expression_work_units -
                std::min(
                    m_expression_work_units,
                    m_limits.max_expression_work_units);
        limits.max_work_units =
            std::min(limits.max_work_units, remaining_work);
        ++m_expression_evaluations;
        const JBeamExpressionResult evaluated =
            EvaluateJBeamExpression(expression, environment, limits);
        if (evaluated.work_units > remaining_work)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Expression evaluator exceeded the aggregate work "
                "limit");
            m_resource_limit = true;
            return false;
        }
        m_expression_work_units += evaluated.work_units;
        if (!evaluated.IsValid())
        {
            const bool has_diagnostic =
                !evaluated.diagnostics.empty();
            const JBeamExpressionDiagnostic* diagnostic =
                has_diagnostic ? &evaluated.diagnostics[0] : NULL;
            const bool is_limit =
                diagnostic != NULL &&
                (diagnostic->code ==
                        JBeamExpressionDiagnosticCode::
                            EXPRESSION_SIZE_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::TOKEN_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::DEPTH_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::WORK_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::
                            FUNCTION_ARGUMENT_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::
                            STRING_SIZE_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::
                            OUTPUT_STRING_SIZE_LIMIT ||
                 diagnostic->code ==
                        JBeamExpressionDiagnosticCode::
                            ENVIRONMENT_LIMIT);
            std::ostringstream detail;
            detail.imbue(std::locale::classic());
            detail << "JBeam expression evaluation failed";
            if (diagnostic != NULL)
            {
                detail << " at decoded byte "
                    << diagnostic->byte_offset << " ("
                    << JBeamExpressionDiagnosticCodeToString(
                        diagnostic->code)
                    << "): " << diagnostic->message;
            }
            Push(
                is_limit
                    ? JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT
                    : JBeamStructuralDiagnosticCode::EXPRESSION_ERROR,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                detail.str());
            return false;
        }
        output = evaluated.value;
        return true;
    }

    const JBeamExpressionValue* FindLocalExpressionVariable(
        const std::vector<JBeamExpressionVariable>& variables,
        const std::string& name) const
    {
        for (std::size_t i = variables.size(); i > 0U; --i)
        {
            if (variables[i - 1U].name == name)
            {
                return &variables[i - 1U].value;
            }
        }
        return NULL;
    }

    bool ExpandNamespaceString(
        const std::string& authored,
        const std::vector<JBeamExpressionVariable>& variables,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        JBeamExpressionValue& output)
    {
        std::string expanded;
        const JBeamExpressionValue* prefix =
            FindLocalExpressionVariable(variables, "$prefix");
        const JBeamExpressionValue* suffix =
            FindLocalExpressionVariable(variables, "$suffix");
        if ((prefix != NULL &&
             prefix->type != JBeamExpressionValueType::NIL_VALUE &&
             prefix->type != JBeamExpressionValueType::STRING) ||
            (suffix != NULL &&
             suffix->type != JBeamExpressionValueType::NIL_VALUE &&
             suffix->type != JBeamExpressionValueType::STRING))
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_ERROR,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Namespace variables $prefix and $suffix must be "
                "strings or nil");
            return false;
        }
        const std::size_t prefix_size =
            prefix != NULL &&
                    prefix->type == JBeamExpressionValueType::STRING
                ? prefix->string_value.size()
                : 0U;
        const std::size_t suffix_size =
            suffix != NULL &&
                    suffix->type == JBeamExpressionValueType::STRING
                ? suffix->string_value.size()
                : 0U;
        std::size_t output_size = prefix_size;
        if (!AddSize(authored.size() - 2U, output_size) ||
            !AddSize(suffix_size, output_size) ||
            output_size >
                m_limits.expression_limits.max_output_string_bytes)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Expanded namespace string exceeds the configured "
                "output limit");
            return false;
        }
        expanded.reserve(output_size);
        if (prefix_size != 0U)
        {
            expanded.append(prefix->string_value);
        }
        expanded.append(authored, 2U, std::string::npos);
        if (suffix_size != 0U)
        {
            expanded.append(suffix->string_value);
        }
        std::size_t namespace_work = output_size;
        if (!AddSize(1U, namespace_work))
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                field_name,
                "Namespace expansion work accounting overflowed");
            m_resource_limit = true;
            return false;
        }
        if (!ChargeSemanticWork(
                namespace_work,
                provenance,
                section,
                field_name,
                "Namespace expansion exceeds the aggregate semantic "
                "work limit"))
        {
            return false;
        }
        output = JBeamExpressionValue::String(expanded);
        return true;
    }

    bool ResolveScalarValue(
        const JBeamValue& value,
        const std::vector<JBeamExpressionVariable>& variables,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        JBeamExpressionValue& output)
    {
        switch (value.type)
        {
        case JBeamValueType::NULL_VALUE:
            output = JBeamExpressionValue::Nil();
            return true;
        case JBeamValueType::BOOLEAN:
            output = JBeamExpressionValue::Boolean(
                value.boolean_value);
            return true;
        case JBeamValueType::NUMBER:
            if (!IsFiniteDouble(value.number_value))
            {
                Push(
                    JBeamStructuralDiagnosticCode::NON_FINITE_NUMBER,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    provenance,
                    section,
                    row_index,
                    field_name,
                    "Non-finite structural numbers are rejected");
                return false;
            }
            output = JBeamExpressionValue::Number(
                value.number_value == 0.0 ? 0.0 : value.number_value);
            return true;
        case JBeamValueType::STRING:
            if (value.scalar_text.compare(0U, 2U, "$.") == 0)
            {
                return ExpandNamespaceString(
                    value.scalar_text,
                    variables,
                    provenance,
                    section,
                    row_index,
                    field_name,
                    output);
            }
            if (IsExpressionString(value))
            {
                const std::string expression =
                    value.scalar_text.compare(0U, 2U, "$=") == 0
                        ? value.scalar_text
                        : std::string("$=") + value.scalar_text;
                return EvaluateExpressionText(
                    expression,
                    variables,
                    provenance,
                    section,
                    row_index,
                    field_name,
                    output);
            }
            output = JBeamExpressionValue::String(
                value.scalar_text);
            return true;
        case JBeamValueType::ARRAY:
        case JBeamValueType::OBJECT:
            return false;
        }
        return false;
    }

    bool EraseComponentPath(
        std::map<std::string, JBeamExpressionValue>& values,
        const std::string& path,
        bool descendants,
        const JBeamStructuralProvenance& provenance)
    {
        const std::string prefix = path + ".";
        std::map<std::string, JBeamExpressionValue>::iterator iterator =
            values.begin();
        while (iterator != values.end())
        {
            if (!ChargeSemanticWork(
                    1U,
                    provenance,
                    "components",
                    path,
                    "Component merging exceeds the aggregate semantic "
                    "work limit"))
            {
                return false;
            }
            const bool exact = iterator->first == path;
            const bool child =
                descendants &&
                iterator->first.size() > prefix.size() &&
                iterator->first.compare(
                    0U, prefix.size(), prefix) == 0;
            if (exact || child)
            {
                values.erase(iterator++);
            }
            else
            {
                ++iterator;
            }
        }
        return true;
    }

    bool EraseNonScalarComponentPath(
        const std::string& path,
        bool descendants,
        const JBeamStructuralProvenance& provenance)
    {
        const std::string prefix = path + ".";
        std::map<std::string, bool>::iterator iterator =
            m_non_scalar_components.begin();
        while (iterator != m_non_scalar_components.end())
        {
            if (!ChargeSemanticWork(
                    1U,
                    provenance,
                    "components",
                    path,
                    "Component classification exceeds the aggregate "
                    "semantic work limit"))
            {
                return false;
            }
            const bool exact = iterator->first == path;
            const bool child =
                descendants &&
                iterator->first.size() > prefix.size() &&
                iterator->first.compare(
                    0U, prefix.size(), prefix) == 0;
            if (exact || child)
            {
                m_non_scalar_components.erase(iterator++);
            }
            else
            {
                ++iterator;
            }
        }
        return true;
    }

    bool EraseComponentRowPath(
        const std::string& path,
        bool descendants,
        const JBeamStructuralProvenance& provenance)
    {
        const std::string prefix = path + ".";
        std::map<
            std::string,
            std::shared_ptr<const JBeamValue> >::iterator iterator =
                m_component_rows.begin();
        while (iterator != m_component_rows.end())
        {
            if (!ChargeSemanticWork(
                    1U,
                    provenance,
                    "components",
                    path,
                    "Component-row classification exceeds the aggregate "
                    "semantic work limit"))
            {
                return false;
            }
            const bool exact = iterator->first == path;
            const bool child =
                descendants &&
                iterator->first.size() > prefix.size() &&
                iterator->first.compare(
                    0U, prefix.size(), prefix) == 0;
            if (exact || child)
            {
                m_component_rows.erase(iterator++);
            }
            else
            {
                ++iterator;
            }
        }
        return true;
    }

    bool SetComponentRow(
        const std::string& path,
        const std::shared_ptr<const JBeamValue>& value)
    {
        std::map<
            std::string,
            std::shared_ptr<const JBeamValue> >::iterator found =
                m_component_rows.find(path);
        if (found != m_component_rows.end())
        {
            found->second = value;
            return true;
        }
        std::size_t bytes = path.size();
        if (!AddSize(sizeof(std::shared_ptr<const JBeamValue>), bytes))
        {
            RejectRetained();
            return false;
        }
        if (!ReserveSemantic(bytes))
        {
            return false;
        }
        m_component_rows.insert(std::make_pair(path, value));
        return true;
    }

    bool SetNonScalarComponent(
        const std::string& path,
        bool blocks_descendants)
    {
        std::map<std::string, bool>::iterator found =
            m_non_scalar_components.find(path);
        if (found != m_non_scalar_components.end())
        {
            found->second = blocks_descendants;
            return true;
        }
        std::size_t bytes = path.size();
        if (!AddSize(sizeof(bool), bytes))
        {
            RejectRetained();
            return false;
        }
        if (!ReserveSemantic(bytes))
        {
            return false;
        }
        m_non_scalar_components.insert(
            std::make_pair(path, blocks_descendants));
        return true;
    }

    void PreserveUnsupportedComponent(
        JBeamStructuralDiagnosticCode code,
        const PartContext& part,
        const std::string& path,
        const std::string& detail,
        const std::shared_ptr<const JBeamValue>& value)
    {
        PushPreserved(
            code,
            value
                ? ProvenanceWithSpan(part.provenance, value->span)
                : part.provenance,
            "components",
            0U,
            path,
            detail,
            value);
    }

    void MergeComponentValue(
        const PartContext& part,
        const std::string& path,
        const std::shared_ptr<const JBeamValue>& value,
        std::size_t depth,
        std::map<std::string, JBeamExpressionValue>& values)
    {
        if (m_resource_limit)
        {
            return;
        }
        if (m_component_nodes >= m_limits.max_component_nodes ||
            depth > m_limits.max_component_depth)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                part.provenance,
                "components",
                0U,
                path,
                "Component tree exceeds the configured node or depth "
                "limit");
            m_resource_limit = true;
            return;
        }
        ++m_component_nodes;
        if (!ChargeSemanticWork(
                1U,
                part.provenance,
                "components",
                path,
                "Component discovery exceeds the aggregate semantic "
                "work limit"))
        {
            return;
        }
        if (!value)
        {
            PreserveUnsupportedComponent(
                JBeamStructuralDiagnosticCode::
                    UNSUPPORTED_COMPONENT_VALUE,
                part,
                path,
                "Null component storage is preserved but unavailable "
                "to expressions",
                value);
            return;
        }
        if (value->type == JBeamValueType::OBJECT)
        {
            if (!EraseComponentPath(
                    values, path, false, part.provenance) ||
                !EraseComponentRowPath(
                    path, false, part.provenance))
            {
                return;
            }
            if (!path.empty())
            {
                if (!SetNonScalarComponent(path, false))
                {
                    return;
                }
            }
            for (std::size_t i = 0U;
                 i < value->object_fields.size();
                 ++i)
            {
                const JBeamObjectField& field =
                    value->object_fields[i];
                if (!IsComponentPathSegment(field.key))
                {
                    PreserveUnsupportedComponent(
                        JBeamStructuralDiagnosticCode::
                            INVALID_COMPONENT_PATH,
                        part,
                        path.empty()
                            ? field.key
                            : path + "." + field.key,
                        "Component path segment cannot be represented by "
                        "the allowlisted scalar evaluator",
                        field.value);
                    continue;
                }
                const std::string child_path = path.empty()
                    ? field.key
                    : path + "." + field.key;
                const std::size_t component_prefix_bytes = 12U;
                if (m_limits.expression_limits.
                            max_variable_name_bytes <
                        component_prefix_bytes ||
                    child_path.size() >
                        m_limits.expression_limits.
                            max_variable_name_bytes -
                            component_prefix_bytes)
                {
                    PreserveUnsupportedComponent(
                        JBeamStructuralDiagnosticCode::
                            INVALID_COMPONENT_PATH,
                        part,
                        child_path,
                        "Flattened component path exceeds the "
                        "configured expression-variable name limit",
                        field.value);
                    continue;
                }
                MergeComponentValue(
                    part,
                    child_path,
                    field.value,
                    depth + 1U,
                    values);
            }
            return;
        }
        if (!EraseComponentPath(
                values, path, true, part.provenance) ||
            !EraseComponentRowPath(
                path, true, part.provenance))
        {
            return;
        }
        if (!EraseNonScalarComponentPath(
                path, true, part.provenance))
        {
            return;
        }
        if (value->type == JBeamValueType::ARRAY)
        {
            if (!SetNonScalarComponent(path, true) ||
                !SetComponentRow(path, value))
            {
                return;
            }
            return;
        }
        if (value->type == JBeamValueType::STRING &&
            IsExpressionString(*value))
        {
            if (!SetNonScalarComponent(path, true))
            {
                return;
            }
            PreserveUnsupportedComponent(
                JBeamStructuralDiagnosticCode::
                    UNSUPPORTED_COMPONENT_VALUE,
                part,
                path,
                "Expression-valued component is preserved; recursive "
                "component evaluation is not allowlisted",
                value);
            return;
        }
        if (value->type == JBeamValueType::STRING &&
            value->scalar_text.size() >
                m_limits.expression_limits.max_string_bytes)
        {
            if (!SetNonScalarComponent(path, true))
            {
                return;
            }
            PreserveUnsupportedComponent(
                JBeamStructuralDiagnosticCode::
                    UNSUPPORTED_COMPONENT_VALUE,
                part,
                path,
                "Component string exceeds the scalar evaluator limit "
                "and is preserved without entering the environment",
                value);
            return;
        }

        JBeamExpressionValue scalar;
        if (!ResolveScalarValue(
                *value,
                std::vector<JBeamExpressionVariable>(),
                ProvenanceWithSpan(part.provenance, value->span),
                "components",
                0U,
                path,
                scalar))
        {
            return;
        }
        std::size_t admission_bytes = 12U;
        if (!AddSize(path.size(), admission_bytes) ||
            (scalar.type == JBeamExpressionValueType::STRING &&
             !AddSize(
                 scalar.string_value.size(), admission_bytes)))
        {
            RejectRetained();
            return;
        }
        if (!ReserveSemantic(admission_bytes))
        {
            return;
        }
        values[path] = scalar;
        if (values.size() >
            m_limits.expression_limits.max_variables)
        {
            Push(
                JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                part.provenance,
                "components",
                0U,
                path,
                "Flattened component environment exceeds the "
                "configured variable limit");
            m_resource_limit = true;
        }
    }

    void BuildComponentEnvironment()
    {
        std::map<std::string, JBeamExpressionValue> values;
        for (std::size_t part_index = 0U;
             part_index < m_parts.size() && !m_resource_limit;
             ++part_index)
        {
            const PartContext& part = m_parts[part_index];
            const JBeamValue& body = part.node->definition.body;
            if (body.type != JBeamValueType::OBJECT)
            {
                continue;
            }
            for (std::size_t field_index = 0U;
                 field_index < body.object_fields.size();
                 ++field_index)
            {
                const JBeamObjectField& field =
                    body.object_fields[field_index];
                if (field.key != "components")
                {
                    continue;
                }
                if (!field.value ||
                    field.value->type != JBeamValueType::OBJECT)
                {
                    PreserveUnsupportedComponent(
                        JBeamStructuralDiagnosticCode::
                            UNSUPPORTED_COMPONENT_VALUE,
                        part,
                        "$components",
                        "The components section must be a dictionary; "
                        "its inert value is preserved",
                        field.value);
                    continue;
                }
                MergeComponentValue(
                    part,
                    std::string(),
                    field.value,
                    0U,
                    values);
            }
        }
        if (m_resource_limit)
        {
            return;
        }
        m_components.reserve(values.size());
        for (std::map<std::string, JBeamExpressionValue>::
                 const_iterator iterator = values.begin();
             iterator != values.end();
             ++iterator)
        {
            JBeamExpressionVariable variable;
            variable.name = "$components." + iterator->first;
            variable.value = iterator->second;
            std::size_t bytes = variable.name.size();
            if (variable.value.type ==
                JBeamExpressionValueType::STRING)
            {
                const std::size_t string_bytes =
                    variable.value.string_value.size();
                if (string_bytes >
                        m_limits.expression_limits.max_string_bytes ||
                    string_bytes >
                        m_limits.expression_limits.
                            max_environment_string_bytes -
                            std::min(
                                m_component_string_bytes,
                                m_limits.expression_limits.
                                    max_environment_string_bytes))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            EXPRESSION_LIMIT,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        JBeamStructuralProvenance(),
                        "components",
                        0U,
                        variable.name,
                        "Flattened component strings exceed the "
                        "configured expression-environment limit");
                    m_resource_limit = true;
                    return;
                }
                if (!AddSize(string_bytes, bytes))
                {
                    RejectRetained();
                    return;
                }
                m_component_string_bytes += string_bytes;
            }
            if (!ReserveSemantic(bytes))
            {
                return;
            }
            m_components.push_back(variable);
        }
    }

    void BuildPartVariableEnvironments()
    {
        for (std::size_t part_index = 0U;
             part_index < m_parts.size() && !m_resource_limit;
             ++part_index)
        {
            PartContext& part = m_parts[part_index];
            const std::vector<JBeamVariableAssignment>& authored =
                part.node->inherited_variables;
            if (authored.size() >
                m_limits.expression_limits.max_variables -
                    std::min(
                        m_components.size(),
                        m_limits.expression_limits.max_variables))
            {
                Push(
                    JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    part.provenance,
                    "variables",
                    0U,
                    std::string(),
                    "Resolved part expression environment exceeds the "
                    "configured variable limit");
                m_resource_limit = true;
                return;
            }
            part.variables->reserve(authored.size());
            std::size_t environment_string_bytes =
                m_component_string_bytes;
            for (std::size_t i = 0U;
                 i < authored.size() && !m_resource_limit;
                 ++i)
            {
                const JBeamVariableAssignment& assignment =
                    authored[i];
                const JBeamStructuralProvenance provenance =
                    ProvenanceWithSpan(
                        part.provenance, assignment.span);
                if (!IsSimpleExpressionVariableName(
                        assignment.name))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            INVALID_VARIABLE_VALUE,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        provenance,
                        "variables",
                        i,
                        assignment.name,
                        "Resolved variable name is not accepted by the "
                        "allowlisted evaluator");
                    continue;
                }
                JBeamExpressionValue value;
                if (!ResolveScalarValue(
                        assignment.value,
                        *part.variables,
                        provenance,
                        "variables",
                        i,
                        assignment.name,
                        value))
                {
                    if (assignment.value.type ==
                            JBeamValueType::ARRAY ||
                        assignment.value.type ==
                            JBeamValueType::OBJECT)
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                INVALID_VARIABLE_VALUE,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            provenance,
                            "variables",
                            i,
                            assignment.name,
                            "Resolved expression variables must be "
                            "scalar values");
                    }
                    continue;
                }
                JBeamExpressionVariable variable;
                variable.name = assignment.name;
                variable.value = value;
                std::size_t bytes = variable.name.size();
                if (variable.value.type ==
                    JBeamExpressionValueType::STRING)
                {
                    const std::size_t string_bytes =
                        variable.value.string_value.size();
                    if (string_bytes >
                            m_limits.expression_limits.max_string_bytes ||
                        string_bytes >
                            m_limits.expression_limits.
                                max_environment_string_bytes -
                                std::min(
                                    environment_string_bytes,
                                    m_limits.expression_limits.
                                        max_environment_string_bytes))
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                EXPRESSION_LIMIT,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            provenance,
                            "variables",
                            i,
                            assignment.name,
                            "Resolved variable strings exceed the "
                            "configured expression-environment limit");
                        m_resource_limit = true;
                        return;
                    }
                    if (!AddSize(string_bytes, bytes))
                    {
                        RejectRetained();
                        return;
                    }
                    environment_string_bytes += string_bytes;
                }
                if (!ReserveSemantic(bytes))
                {
                    return;
                }
                part.variables->push_back(variable);
            }
        }
    }

    void DiscoverSections()
    {
        for (std::size_t part_index = 0U;
             part_index < m_parts.size();
             ++part_index)
        {
            if (m_resource_limit)
            {
                return;
            }
            PartSections sections;
            sections.part = m_parts[part_index];
            const JBeamValue& body =
                sections.part.node->definition.body;
            if (body.type != JBeamValueType::OBJECT)
            {
                Push(
                    JBeamStructuralDiagnosticCode::PART_BODY_NOT_OBJECT,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    sections.part.provenance,
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved part body must be an object");
                m_sections.push_back(sections);
                continue;
            }
            for (std::size_t field_index = 0U;
                 field_index < body.object_fields.size();
                 ++field_index)
            {
                const JBeamObjectField& field =
                    body.object_fields[field_index];
                if (IsRecognizedSection(field.key))
                {
                    SectionOccurrence occurrence;
                    occurrence.value = field.value.get();
                    occurrence.span = field.value
                        ? field.value->span
                        : field.key_span;
                    sections.sections[field.key].push_back(
                        occurrence);
                }
                else if (!IsResolverMetadataSection(field.key))
                {
                    PushPreserved(
                        JBeamStructuralDiagnosticCode::UNKNOWN_SECTION,
                        ProvenanceWithSpan(
                            sections.part.provenance,
                            field.value
                                ? field.value->span
                                : field.key_span),
                        field.key,
                        0U,
                        field.key,
                        "Section is outside the structural J2 subset",
                        field.value);
                    if (m_resource_limit)
                    {
                        return;
                    }
                }
            }
            m_sections.push_back(sections);
        }
    }

    bool IsValidTableHeader(const JBeamValue& value) const
    {
        if (value.type != JBeamValueType::ARRAY ||
            value.array_values.empty())
        {
            return false;
        }
        const JBeamValue& header = value.array_values[0];
        if (header.type != JBeamValueType::ARRAY ||
            header.array_values.empty())
        {
            return false;
        }
        for (std::size_t i = 0U;
             i < header.array_values.size();
             ++i)
        {
            if (header.array_values[i].type !=
                    JBeamValueType::STRING ||
                header.array_values[i].scalar_text.empty())
            {
                return false;
            }
        }
        return true;
    }

    void PreflightRows()
    {
        static const char* const names[] = {
            "nodes", "beams", "triangles", "quads", "refNodes"
        };
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t name_index = 0U;
                 name_index < sizeof(names) / sizeof(names[0]);
                 ++name_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const std::string name(names[name_index]);
                const std::vector<SectionOccurrence>& occurrences =
                    m_sections[part_index].sections[name];
                if (occurrences.size() > 1U)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::DUPLICATE_SECTION,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        name,
                        0U,
                        std::string(),
                        "Duplicate structural section is ambiguous");
                }
                for (std::size_t occurrence_index = 0U;
                     occurrence_index < occurrences.size();
                     ++occurrence_index)
                {
                    if (m_resource_limit)
                    {
                        return;
                    }
                    const JBeamValue* value =
                        occurrences[occurrence_index].value;
                    if (value == NULL)
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::INVALID_SECTION,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            m_sections[part_index].part.provenance,
                            name,
                            0U,
                            std::string(),
                            "Structural section has no value");
                        continue;
                    }
                    if (!IsValidTableHeader(*value))
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                INVALID_TABLE_HEADER,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            m_sections[part_index].part.provenance,
                            name,
                            0U,
                            std::string(),
                            "Structural section requires a non-empty "
                            "string table header");
                        continue;
                    }
                    const std::size_t row_count =
                        value->array_values.size() - 1U;
                    if (row_count >
                        m_limits.max_rows -
                            std::min(
                                m_result.authored_row_count,
                                m_limits.max_rows))
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::ROW_LIMIT,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            m_sections[part_index].part.provenance,
                            name,
                            0U,
                            std::string(),
                            "Structural tables exceed the configured "
                            "row limit");
                        m_resource_limit = true;
                        return;
                    }
                    m_result.authored_row_count += row_count;
                }
            }
        }
    }

    bool ExpandComponentRows(
        const JBeamValue& source,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        JBeamValue& output,
        bool& expanded)
    {
        expanded = false;
        if (source.type != JBeamValueType::ARRAY ||
            source.array_values.size() < 2U)
        {
            return true;
        }

        typedef std::pair<
            std::size_t,
            std::shared_ptr<const JBeamValue> > Replacement;
        std::vector<Replacement> replacements;
        for (std::size_t i = 1U;
             i < source.array_values.size();
             ++i)
        {
            if (!ChargeSemanticWork(
                    1U,
                    provenance,
                    section,
                    std::string(),
                    "Component-row expansion exceeds the aggregate "
                    "semantic work limit"))
            {
                return false;
            }
            std::string path;
            if (!ParseExactComponentReference(
                    source.array_values[i], path))
            {
                if (IsExpressionString(source.array_values[i]))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        ProvenanceWithSpan(
                            provenance,
                            source.array_values[i].span),
                        section,
                        i - 1U,
                        "$components",
                        "A structural table-entry expression must be an "
                        "exact $=$components.path row reference");
                    return false;
                }
                continue;
            }
            const std::map<
                std::string,
                std::shared_ptr<const JBeamValue> >::const_iterator found =
                    m_component_rows.find(path);
            if (found == m_component_rows.end())
            {
                Push(
                    JBeamStructuralDiagnosticCode::EXPRESSION_ERROR,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    ProvenanceWithSpan(
                        provenance,
                        source.array_values[i].span),
                    section,
                    i - 1U,
                    "$components." + path,
                    "Exact component row reference does not resolve to "
                    "an effective array-valued component");
                return false;
            }
            replacements.push_back(
                std::make_pair(i, found->second));
        }
        if (replacements.empty())
        {
            return true;
        }

        std::size_t admitted = m_retained_bytes;
        if (!AddSize(m_semantic_bytes, admitted))
        {
            RejectRetained();
            return false;
        }
        const std::size_t remaining =
            m_limits.max_retained_bytes -
                std::min(admitted, m_limits.max_retained_bytes);
        ValueMeasurement source_measurement;
        ValueMeasureStatus status = MeasureValue(
            source,
            remaining,
            false,
            source_measurement);
        if (status != ValueMeasureStatus::OK)
        {
            RejectValueMeasurement(
                status,
                provenance,
                section,
                0U,
                "$components");
            return false;
        }
        std::size_t bytes = source_measurement.bytes;
        for (std::size_t i = 0U; i < replacements.size(); ++i)
        {
            if (!replacements[i].second)
            {
                continue;
            }
            ValueMeasurement replacement_measurement;
            status = MeasureValue(
                *replacements[i].second,
                remaining - std::min(bytes, remaining),
                false,
                replacement_measurement);
            if (status != ValueMeasureStatus::OK)
            {
                RejectValueMeasurement(
                    status,
                    provenance,
                    section,
                    replacements[i].first,
                    "$components");
                return false;
            }
            if (!AddSize(replacement_measurement.bytes, bytes) ||
                bytes > remaining)
            {
                RejectRetained();
                return false;
            }
        }
        if (!ReserveSemantic(bytes))
        {
            return false;
        }

        output = source;
        for (std::size_t i = 0U; i < replacements.size(); ++i)
        {
            output.array_values[replacements[i].first] =
                *replacements[i].second;
        }
        expanded = true;
        return true;
    }

    const JBeamNormalizedTable* NormalizeSection(
        const PartSections& sections,
        const std::string& name,
        JBeamNormalizeResult& normalized)
    {
        const std::map<
            std::string,
            std::vector<SectionOccurrence> >::const_iterator found =
                sections.sections.find(name);
        if (found == sections.sections.end() ||
            found->second.size() != 1U ||
            found->second[0].value == NULL ||
            !IsValidTableHeader(*found->second[0].value))
        {
            return NULL;
        }
        JBeamValue expanded_value;
        bool expanded = false;
        if (!ExpandComponentRows(
                *found->second[0].value,
                sections.part.provenance,
                name,
                expanded_value,
                expanded))
        {
            return NULL;
        }
        const JBeamValue& normalization_input = expanded
            ? expanded_value
            : *found->second[0].value;
        std::size_t admitted_bytes = m_retained_bytes;
        if (!AddSize(m_semantic_bytes, admitted_bytes))
        {
            RejectRetained();
            return NULL;
        }
        const std::size_t remaining_bytes =
            m_limits.max_retained_bytes -
                std::min(
                    admitted_bytes,
                    m_limits.max_retained_bytes);
        const std::size_t remaining_diagnostics =
            m_limits.max_diagnostics -
                std::min(
                    m_result.diagnostics.size(),
                    m_limits.max_diagnostics);
        JBeamNormalizeLimits normalize_limits;
        normalize_limits.max_retained_bytes = remaining_bytes;
        // Every normalized work item retains or examines source state which
        // must fit the same remaining structural admission budget. This keeps
        // a tiny structural budget from silently invoking the normalizer's
        // unrelated 512 MiB / 12M-unit defaults.
        normalize_limits.max_work_units = std::min(
            normalize_limits.max_work_units,
            remaining_bytes);
        normalize_limits.max_diagnostics =
            std::max<std::size_t>(1U, remaining_diagnostics);
        normalized = NormalizeJBeamTables(
            normalization_input,
            normalize_limits);

        bool normalization_error = false;
        for (std::size_t i = 0U;
             i < normalized.diagnostics.size();
             ++i)
        {
            const JBeamDiagnostic& source =
                normalized.diagnostics[i];
            const JBeamStructuralProvenance provenance =
                ProvenanceWithSpan(
                    sections.part.provenance,
                    source.span);
            if (m_resource_limit)
            {
                return NULL;
            }
            std::string detail =
                std::string("Normalizer ") +
                JBeamDiagnosticCodeToString(source.code) +
                ": " + source.message;
            if (source.code ==
                JBeamDiagnosticCode::
                    NORMALIZE_RETAINED_BYTES_LIMIT)
            {
                Push(
                    JBeamStructuralDiagnosticCode::
                        RETAINED_BYTE_LIMIT,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    provenance,
                    name,
                    0U,
                    std::string(),
                    detail);
                m_resource_limit = true;
                return NULL;
            }
            if (source.code ==
                JBeamDiagnosticCode::DIAGNOSTIC_LIMIT)
            {
                Push(
                    JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    provenance,
                    name,
                    0U,
                    std::string(),
                    detail);
                m_resource_limit = true;
                return NULL;
            }
            const bool is_error =
                source.severity == JBeamDiagnosticSeverity::ERROR_SEVERITY;
            Push(
                is_error
                    ? JBeamStructuralDiagnosticCode::
                        NORMALIZATION_ERROR
                    : JBeamStructuralDiagnosticCode::
                        NORMALIZATION_WARNING,
                is_error
                    ? JBeamStructuralSeverity::ERROR_SEVERITY
                    : JBeamStructuralSeverity::WARNING,
                provenance,
                name,
                0U,
                std::string(),
                detail);
            normalization_error =
                normalization_error || is_error;
            if (m_resource_limit)
            {
                return NULL;
            }
        }
        if (normalization_error || !normalized.IsValid())
        {
            m_resource_limit = true;
            return NULL;
        }
        const JBeamNormalizedTable* table = NULL;
        for (std::size_t i = 0U;
             i < normalized.tables.size();
             ++i)
        {
            if (normalized.tables[i].path == "$")
            {
                table = &normalized.tables[i];
                break;
            }
        }
        if (table == NULL)
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_SECTION,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                sections.part.provenance,
                name,
                0U,
                std::string(),
                "Structural table normalization failed");
            return NULL;
        }
        std::set<std::string> headers;
        for (std::size_t i = 0U; i < table->columns.size(); ++i)
        {
            if (!headers.insert(table->columns[i].name).second)
            {
                Push(
                    JBeamStructuralDiagnosticCode::
                        DUPLICATE_TABLE_HEADER,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    sections.part.provenance,
                    name,
                    0U,
                    table->columns[i].name,
                    "Duplicate table header is ambiguous for "
                    "structural conversion");
            }
        }
        return table;
    }

    void PreserveUnsupportedAssignments(
        const JBeamNormalizedTable& table,
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index)
    {
        for (std::size_t i = 0U;
             i < row.positional_assignments.size();
             ++i)
        {
            if (m_resource_limit)
            {
                return;
            }
            const JBeamFieldAssignment& assignment =
                row.positional_assignments[i];
            if (!IsImplementedField(section, assignment.name))
            {
                const bool known = IsKnownUnsupportedField(
                    section, assignment.name);
                PushPreserved(
                    known
                        ? JBeamStructuralDiagnosticCode::UNSUPPORTED_FIELD
                        : JBeamStructuralDiagnosticCode::UNKNOWN_FIELD,
                    ProvenanceWithSpan(provenance, assignment.span),
                    section,
                    row_index,
                    assignment.name,
                    known
                        ? "Field is recognized but not simulated by J2"
                        : "Unknown field is preserved but ignored",
                    assignment.value);
                if (m_resource_limit)
                {
                    return;
                }
            }
        }
        for (std::size_t i = 0U;
             i < row.row_local_assignments.size();
             ++i)
        {
            if (m_resource_limit)
            {
                return;
            }
            const JBeamFieldAssignment& assignment =
                row.row_local_assignments[i];
            if (!IsImplementedField(section, assignment.name))
            {
                const bool known = IsKnownUnsupportedField(
                    section, assignment.name);
                PushPreserved(
                    known
                        ? JBeamStructuralDiagnosticCode::UNSUPPORTED_FIELD
                        : JBeamStructuralDiagnosticCode::UNKNOWN_FIELD,
                    ProvenanceWithSpan(provenance, assignment.span),
                    section,
                    row_index,
                    assignment.name,
                    known
                        ? "Field is recognized but not simulated by J2"
                        : "Unknown field is preserved but ignored",
                    assignment.value);
                if (m_resource_limit)
                {
                    return;
                }
            }
        }
        std::size_t positional_count =
            row.raw_row.array_values.size();
        if (positional_count > table.columns.size() &&
            row.raw_row.array_values[positional_count - 1U].type ==
                JBeamValueType::OBJECT)
        {
            --positional_count;
        }
        for (std::size_t i = table.columns.size();
             i < positional_count;
             ++i)
        {
            if (m_resource_limit)
            {
                return;
            }
            std::ostringstream field_name;
            field_name.imbue(std::locale::classic());
            field_name << "<extra-cell-" << i << '>';
            const JBeamValue& value = row.raw_row.array_values[i];
            PushPreservedCopy(
                JBeamStructuralDiagnosticCode::UNKNOWN_FIELD,
                ProvenanceWithSpan(provenance, value.span),
                section,
                row_index,
                field_name.str(),
                "Extra positional cell is preserved but ignored",
                value);
            if (m_resource_limit)
            {
                return;
            }
        }
    }

    void PreserveUnsupportedDefaults(
        const JBeamNormalizedTable& table,
        const JBeamStructuralProvenance& provenance,
        const std::string& section)
    {
        for (std::size_t entry_index = 0U;
             entry_index < table.entries.size();
             ++entry_index)
        {
            if (m_resource_limit)
            {
                return;
            }
            const JBeamNormalizedTableEntry& entry =
                table.entries[entry_index];
            if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DEFAULT_MODIFIER ||
                entry.raw_value.type != JBeamValueType::OBJECT)
            {
                if (m_resource_limit)
                {
                    return;
                }
                continue;
            }
            for (std::size_t field_index = 0U;
                 field_index < entry.raw_value.object_fields.size();
                 ++field_index)
            {
                const JBeamObjectField& field =
                    entry.raw_value.object_fields[field_index];
                if (!IsImplementedField(section, field.key))
                {
                    const bool known =
                        IsKnownUnsupportedField(section, field.key);
                    PushPreserved(
                        known
                            ? JBeamStructuralDiagnosticCode::
                                UNSUPPORTED_FIELD
                            : JBeamStructuralDiagnosticCode::UNKNOWN_FIELD,
                        ProvenanceWithSpan(
                            provenance,
                            field.value
                                ? field.value->span
                                : field.key_span),
                        section,
                        entry_index,
                        field.key,
                        known
                            ? "Default is recognized but not simulated "
                                "by J2"
                            : "Unknown default is preserved but ignored",
                        field.value);
                    if (m_resource_limit)
                    {
                        return;
                    }
                }
            }
        }
    }

    const std::vector<JBeamExpressionVariable>& VariablesFor(
        const JBeamStructuralProvenance& provenance) const
    {
        static const std::vector<JBeamExpressionVariable> empty;
        if (!provenance.part ||
            provenance.PartPreorderIndex() >= m_parts.size())
        {
            return empty;
        }
        const std::shared_ptr<std::vector<JBeamExpressionVariable> >&
            variables =
                m_parts[provenance.PartPreorderIndex()].variables;
        return variables ? *variables : empty;
    }

    bool ReadRequiredString(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& primary,
        const std::string& alias,
        std::string& output)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* first =
            FindEffectiveJBeamField(row, primary);
        const JBeamFieldAssignment* second = alias.empty()
            ? NULL
            : FindEffectiveJBeamField(row, alias);
        if (first != NULL && second != NULL)
        {
            Push(
                JBeamStructuralDiagnosticCode::
                    AMBIGUOUS_REQUIRED_FIELD,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                primary,
                "Both canonical and alias fields are assigned");
            return false;
        }
        const JBeamFieldAssignment* assignment =
            first != NULL ? first : second;
        if (assignment == NULL || !assignment->value)
        {
            Push(
                JBeamStructuralDiagnosticCode::MISSING_REQUIRED_FIELD,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                primary,
                "Required string field is missing");
            return false;
        }
        const JBeamStructuralProvenance field_provenance =
            ProvenanceWithSpan(provenance, assignment->span);
        JBeamExpressionValue resolved;
        if (!ResolveScalarValue(
                *assignment->value,
                VariablesFor(provenance),
                field_provenance,
                section,
                row_index,
                primary,
                resolved))
        {
            return false;
        }
        if (resolved.type != JBeamExpressionValueType::STRING ||
            resolved.string_value.empty())
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                field_provenance,
                section,
                row_index,
                primary,
                "Required identifier expression must resolve to a "
                "non-empty string");
            return false;
        }
        output = resolved.string_value;
        return true;
    }

    bool ReadRequiredNumber(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& name,
        double& output)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* assignment =
            FindEffectiveJBeamField(row, name);
        if (assignment == NULL || !assignment->value)
        {
            Push(
                JBeamStructuralDiagnosticCode::MISSING_REQUIRED_FIELD,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                name,
                "Required numeric field is missing");
            return false;
        }
        const JBeamStructuralProvenance field_provenance =
            ProvenanceWithSpan(provenance, assignment->span);
        JBeamExpressionValue resolved;
        if (!ResolveScalarValue(
                *assignment->value,
                VariablesFor(provenance),
                field_provenance,
                section,
                row_index,
                name,
                resolved))
        {
            return false;
        }
        if (resolved.type != JBeamExpressionValueType::NUMBER)
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                field_provenance,
                section,
                row_index,
                name,
                "Field expression must resolve to a number");
            return false;
        }
        output = resolved.number_value;
        return true;
    }

    bool ReadOptionalNumber(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& name,
        bool& has_value,
        double& output)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* assignment =
            FindEffectiveJBeamField(row, name);
        has_value = assignment != NULL;
        if (assignment == NULL)
        {
            return true;
        }
        return ReadRequiredNumber(
            row, provenance, section, row_index, name, output);
    }

    bool ReadOptionalBeamLimit(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        std::size_t row_index,
        const std::string& name,
        bool& has_value,
        bool& unbounded,
        double& output)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* assignment =
            FindEffectiveJBeamField(row, name);
        has_value = assignment != NULL;
        unbounded = false;
        output = 0.0;
        if (assignment == NULL)
        {
            return true;
        }
        if (assignment->value &&
            assignment->value->type == JBeamValueType::STRING &&
            assignment->value->scalar_text == "FLT_MAX")
        {
            unbounded = true;
            return true;
        }
        return ReadRequiredNumber(
            row, provenance, "beams", row_index, name, output);
    }

    bool ReadOptionalBoolean(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& name,
        bool default_value,
        bool& output)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* assignment =
            FindEffectiveJBeamField(row, name);
        output = default_value;
        if (assignment == NULL)
        {
            return true;
        }
        if (!assignment->value)
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                provenance,
                section,
                row_index,
                name,
                "Field expression must resolve to a Boolean");
            return false;
        }
        const JBeamStructuralProvenance field_provenance =
            ProvenanceWithSpan(provenance, assignment->span);
        JBeamExpressionValue resolved;
        if (!ResolveScalarValue(
                *assignment->value,
                VariablesFor(provenance),
                field_provenance,
                section,
                row_index,
                name,
                resolved))
        {
            return false;
        }
        if (resolved.type != JBeamExpressionValueType::BOOLEAN)
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                field_provenance,
                section,
                row_index,
                name,
                "Field expression must resolve to a Boolean");
            return false;
        }
        output = resolved.boolean_value;
        return true;
    }

    bool ReadBeamType(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& provenance,
        std::size_t row_index,
        std::string& output,
        JBeamStructuralBeamStatus& status)
    {
        if (m_resource_limit)
        {
            return false;
        }
        const JBeamFieldAssignment* assignment =
            FindEffectiveJBeamField(row, "beamType");
        output = "NORMAL";
        status = JBeamStructuralBeamStatus::ENABLED;
        if (assignment == NULL)
        {
            return true;
        }
        if (!assignment->value)
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                assignment->value
                    ? ProvenanceWithSpan(
                        provenance, assignment->value->span)
                    : provenance,
                "beams",
                row_index,
                "beamType",
                "beamType must be a non-empty string");
            return false;
        }
        const JBeamStructuralProvenance field_provenance =
            ProvenanceWithSpan(provenance, assignment->span);
        JBeamExpressionValue resolved;
        if (!ResolveScalarValue(
                *assignment->value,
                VariablesFor(provenance),
                field_provenance,
                "beams",
                row_index,
                "beamType",
                resolved))
        {
            return false;
        }
        if (resolved.type != JBeamExpressionValueType::STRING ||
            resolved.string_value.empty())
        {
            Push(
                JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                field_provenance,
                "beams",
                row_index,
                "beamType",
                "beamType expression must resolve to a non-empty "
                "string");
            return false;
        }
        output = resolved.string_value;
        std::string normalized = output;
        if (!normalized.empty() && normalized[0] == '|')
        {
            normalized.erase(0U, 1U);
        }
        if (normalized == "NORMAL")
        {
            output = "NORMAL";
            return true;
        }
        status =
            JBeamStructuralBeamStatus::
                PRESERVED_DISABLED_SPECIAL_TYPE;
        PushPreserved(
            JBeamStructuralDiagnosticCode::
                SPECIAL_BEAM_TYPE_DISABLED,
            ProvenanceWithSpan(provenance, assignment->span),
            "beams",
            row_index,
            "beamType",
            "Only NORMAL beams are enabled by the J2 subset",
            assignment->value);
        return true;
    }

    void ProcessNodes()
    {
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            JBeamNormalizeResult normalized;
            const JBeamNormalizedTable* table = NormalizeSection(
                m_sections[part_index], "nodes", normalized);
            if (m_resource_limit)
            {
                return;
            }
            if (table == NULL)
            {
                continue;
            }
            PreserveUnsupportedDefaults(
                *table,
                m_sections[part_index].part.provenance,
                "nodes");
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t entry_index = 0U;
                 entry_index < table->entries.size();
                 ++entry_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const JBeamNormalizedTableEntry& entry =
                    table->entries[entry_index];
                if (entry.kind ==
                    JBeamNormalizedTableEntryKind::INVALID_ENTRY)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "nodes",
                        entry_index,
                        std::string(),
                        "Node table entry is not a data row or default");
                    continue;
                }
                if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DATA_ROW)
                {
                    continue;
                }
                PreserveUnsupportedAssignments(
                    *table,
                    entry.data_row,
                    m_sections[part_index].part.provenance,
                    "nodes",
                    entry_index);
                if (m_resource_limit)
                {
                    return;
                }
                if (m_result.nodes.size() >= m_limits.max_nodes)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::NODE_LIMIT,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "nodes",
                        entry_index,
                        std::string(),
                        "Node table exceeds the configured node limit");
                    m_resource_limit = true;
                    return;
                }
                JBeamStructuralNode node;
                node.x = 0.0;
                node.y = 0.0;
                node.z = 0.0;
                node.node_weight = 25.0;
                node.node_weight_authored = false;
                node.provenance =
                    ProvenanceWithSpan(
                        m_sections[part_index].part.provenance,
                        entry.data_row.span);
                bool valid = ReadRequiredString(
                    entry.data_row,
                    node.provenance,
                    "nodes",
                    entry_index,
                    "id",
                    std::string(),
                    node.id);
                valid = ReadRequiredNumber(
                    entry.data_row,
                    node.provenance,
                    "nodes",
                    entry_index,
                    "posX",
                    node.x) && valid;
                valid = ReadRequiredNumber(
                    entry.data_row,
                    node.provenance,
                    "nodes",
                    entry_index,
                    "posY",
                    node.y) && valid;
                valid = ReadRequiredNumber(
                    entry.data_row,
                    node.provenance,
                    "nodes",
                    entry_index,
                    "posZ",
                    node.z) && valid;
                const JBeamFieldAssignment* weight =
                    FindEffectiveJBeamField(
                        entry.data_row, "nodeWeight");
                if (weight != NULL)
                {
                    node.node_weight_authored = true;
                    valid = ReadRequiredNumber(
                        entry.data_row,
                        node.provenance,
                        "nodes",
                        entry_index,
                        "nodeWeight",
                        node.node_weight) && valid;
                    if (valid && !(node.node_weight > 0.0))
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                INVALID_NODE_WEIGHT,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            node.provenance,
                            "nodes",
                            entry_index,
                            "nodeWeight",
                            "Authored nodeWeight must be positive");
                        valid = false;
                    }
                }
                if (!node.id.empty() &&
                    m_node_indices.find(node.id) !=
                        m_node_indices.end())
                {
                    Push(
                        JBeamStructuralDiagnosticCode::DUPLICATE_NODE_ID,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        node.provenance,
                        "nodes",
                        entry_index,
                        "id",
                        "Node identifiers must be unique across the "
                        "resolved graph");
                    valid = false;
                }
                if (m_resource_limit)
                {
                    return;
                }
                if (valid)
                {
                    if (!ReserveRetained(node.id.size()))
                    {
                        return;
                    }
                    const std::size_t index = m_result.nodes.size();
                    m_node_indices.insert(
                        std::make_pair(node.id, index));
                    m_result.nodes.push_back(node);
                }
            }
        }
    }

    bool ResolveNode(
        const std::string& id,
        std::size_t& index) const
    {
        const std::map<std::string, std::size_t>::const_iterator found =
            m_node_indices.find(id);
        if (found == m_node_indices.end())
        {
            index = INVALID_INDEX;
            return false;
        }
        index = found->second;
        return true;
    }

    bool AreSamePosition(
        std::size_t first,
        std::size_t second) const
    {
        const JBeamStructuralNode& a = m_result.nodes[first];
        const JBeamStructuralNode& b = m_result.nodes[second];
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    double SafeAbsoluteDifference(
        double first,
        double second) const
    {
        if ((first < 0.0) == (second < 0.0))
        {
            // Equal-sign finite operands cannot overflow subtraction.
            return Abs(first - second);
        }
        const double first_abs = Abs(first);
        const double second_abs = Abs(second);
        const double maximum = std::numeric_limits<double>::max();
        if (first_abs > maximum - second_abs)
        {
            return maximum;
        }
        return first_abs + second_abs;
    }

    bool TryScaledDifference(
        double point,
        double origin,
        double scale,
        double& difference) const
    {
        difference = 0.0;
        if (!(scale > 0.0) ||
            !IsFiniteDouble(scale) ||
            !IsFiniteDouble(point) ||
            !IsFiniteDouble(origin))
        {
            return false;
        }
        if ((point < 0.0) == (origin < 0.0))
        {
            // Preserve small translated deltas. Equal-sign finite subtraction
            // is safe even when both coordinates are near DBL_MAX.
            difference = (point - origin) / scale;
        }
        else
        {
            // Opposite-sign subtraction may overflow. The selected scale is at
            // least their saturated absolute difference, so division first is
            // safe and preserves the direction ratio. Volatile intermediates
            // prevent -ffast-math from reassociating this back into the
            // overflowing (point - origin) / scale expression.
            volatile double scaled_point = point / scale;
            volatile double scaled_origin = origin / scale;
            difference = scaled_point - scaled_origin;
        }
        return IsFiniteDouble(difference);
    }

    bool IsDegenerateTriangle(
        std::size_t first,
        std::size_t second,
        std::size_t third) const
    {
        const JBeamStructuralNode& a = m_result.nodes[first];
        const JBeamStructuralNode& b = m_result.nodes[second];
        const JBeamStructuralNode& c = m_result.nodes[third];
        double scale = SafeAbsoluteDifference(a.x, b.x);
        scale = std::max(
            scale, SafeAbsoluteDifference(a.y, b.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(a.z, b.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(a.x, c.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(a.y, c.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(a.z, c.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(b.x, c.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(b.y, c.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(b.z, c.z));
        if (!(scale > 0.0))
        {
            return true;
        }
        double ux = 0.0;
        double uy = 0.0;
        double uz = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double vz = 0.0;
        if (!TryScaledDifference(b.x, a.x, scale, ux) ||
            !TryScaledDifference(b.y, a.y, scale, uy) ||
            !TryScaledDifference(b.z, a.z, scale, uz) ||
            !TryScaledDifference(c.x, a.x, scale, vx) ||
            !TryScaledDifference(c.y, a.y, scale, vy) ||
            !TryScaledDifference(c.z, a.z, scale, vz))
        {
            return true;
        }
        const double cx = uy * vz - uz * vy;
        const double cy = uz * vx - ux * vz;
        const double cz = ux * vy - uy * vx;
        const double area_squared =
            cx * cx + cy * cy + cz * cz;
        return !IsFiniteDouble(area_squared) ||
            !(area_squared > 1.0e-24);
    }

    bool IsDegenerateFrame(
        std::size_t reference,
        std::size_t back,
        std::size_t left,
        std::size_t up) const
    {
        const JBeamStructuralNode& r = m_result.nodes[reference];
        const JBeamStructuralNode& b = m_result.nodes[back];
        const JBeamStructuralNode& l = m_result.nodes[left];
        const JBeamStructuralNode& u = m_result.nodes[up];
        double scale = SafeAbsoluteDifference(r.x, b.x);
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, b.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, b.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.x, l.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, l.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, l.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.x, u.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, u.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, u.z));
        if (!(scale > 0.0))
        {
            return true;
        }
        double bx = 0.0;
        double by = 0.0;
        double bz = 0.0;
        double lx = 0.0;
        double ly = 0.0;
        double lz = 0.0;
        double ux = 0.0;
        double uy = 0.0;
        double uz = 0.0;
        if (!TryScaledDifference(b.x, r.x, scale, bx) ||
            !TryScaledDifference(b.y, r.y, scale, by) ||
            !TryScaledDifference(b.z, r.z, scale, bz) ||
            !TryScaledDifference(l.x, r.x, scale, lx) ||
            !TryScaledDifference(l.y, r.y, scale, ly) ||
            !TryScaledDifference(l.z, r.z, scale, lz) ||
            !TryScaledDifference(u.x, r.x, scale, ux) ||
            !TryScaledDifference(u.y, r.y, scale, uy) ||
            !TryScaledDifference(u.z, r.z, scale, uz))
        {
            return true;
        }
        const double cross_x = by * lz - bz * ly;
        const double cross_y = bz * lx - bx * lz;
        const double cross_z = bx * ly - by * lx;
        const double determinant =
            cross_x * ux + cross_y * uy + cross_z * uz;
        const double b_len2 = bx * bx + by * by + bz * bz;
        const double l_len2 = lx * lx + ly * ly + lz * lz;
        const double u_len2 = ux * ux + uy * uy + uz * uz;
        const double product = b_len2 * l_len2 * u_len2;
        return !IsFiniteDouble(determinant) ||
            !IsFiniteDouble(product) ||
            !(product > 0.0) ||
            determinant * determinant <= product * 1.0e-24;
    }

    bool IsAlignedFrame(
        std::size_t reference,
        std::size_t back,
        std::size_t left,
        std::size_t up) const
    {
        const JBeamStructuralNode& r = m_result.nodes[reference];
        const JBeamStructuralNode& b = m_result.nodes[back];
        const JBeamStructuralNode& l = m_result.nodes[left];
        const JBeamStructuralNode& u = m_result.nodes[up];
        double scale = SafeAbsoluteDifference(r.x, b.x);
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, b.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, b.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.x, l.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, l.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, l.z));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.x, u.x));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.y, u.y));
        scale = std::max(
            scale, SafeAbsoluteDifference(r.z, u.z));
        double bx = 0.0;
        double by = 0.0;
        double bz = 0.0;
        double lx = 0.0;
        double ly = 0.0;
        double lz = 0.0;
        double ux = 0.0;
        double uy = 0.0;
        double uz = 0.0;
        if (!TryScaledDifference(b.x, r.x, scale, bx) ||
            !TryScaledDifference(b.y, r.y, scale, by) ||
            !TryScaledDifference(b.z, r.z, scale, bz) ||
            !TryScaledDifference(l.x, r.x, scale, lx) ||
            !TryScaledDifference(l.y, r.y, scale, ly) ||
            !TryScaledDifference(l.z, r.z, scale, lz) ||
            !TryScaledDifference(u.x, r.x, scale, ux) ||
            !TryScaledDifference(u.y, r.y, scale, uy) ||
            !TryScaledDifference(u.z, r.z, scale, uz))
        {
            return false;
        }
        const double tolerance = 1.0e-9;
        return by > 0.0 &&
            Abs(bx) <= tolerance * by &&
            Abs(bz) <= tolerance * by &&
            lx > 0.0 &&
            Abs(ly) <= tolerance * lx &&
            Abs(lz) <= tolerance * lx &&
            uz > 0.0 &&
            Abs(ux) <= tolerance * uz &&
            Abs(uy) <= tolerance * uz;
    }

    bool AreAlignedFrontCorners(
        std::size_t reference,
        std::size_t back,
        std::size_t left,
        std::size_t left_corner,
        std::size_t right_corner) const
    {
        const JBeamStructuralNode& r = m_result.nodes[reference];
        const JBeamStructuralNode& b = m_result.nodes[back];
        const JBeamStructuralNode& l = m_result.nodes[left];
        const JBeamStructuralNode& lc =
            m_result.nodes[left_corner];
        const JBeamStructuralNode& rc =
            m_result.nodes[right_corner];
        double scale = SafeAbsoluteDifference(r.x, b.x);
        const JBeamStructuralNode* points[] = {
            &b, &l, &lc, &rc
        };
        for (std::size_t i = 0U; i < 4U; ++i)
        {
            scale = std::max(
                scale,
                SafeAbsoluteDifference(r.x, points[i]->x));
            scale = std::max(
                scale,
                SafeAbsoluteDifference(r.y, points[i]->y));
            scale = std::max(
                scale,
                SafeAbsoluteDifference(r.z, points[i]->z));
        }
        if (!(scale > 0.0))
        {
            return false;
        }
        double bx = 0.0;
        double by = 0.0;
        double bz = 0.0;
        double lx = 0.0;
        double ly = 0.0;
        double lz = 0.0;
        double lcx = 0.0;
        double lcy = 0.0;
        double lcz = 0.0;
        double rcx = 0.0;
        double rcy = 0.0;
        double rcz = 0.0;
        if (!TryScaledDifference(b.x, r.x, scale, bx) ||
            !TryScaledDifference(b.y, r.y, scale, by) ||
            !TryScaledDifference(b.z, r.z, scale, bz) ||
            !TryScaledDifference(l.x, r.x, scale, lx) ||
            !TryScaledDifference(l.y, r.y, scale, ly) ||
            !TryScaledDifference(l.z, r.z, scale, lz) ||
            !TryScaledDifference(lc.x, r.x, scale, lcx) ||
            !TryScaledDifference(lc.y, r.y, scale, lcy) ||
            !TryScaledDifference(lc.z, r.z, scale, lcz) ||
            !TryScaledDifference(rc.x, r.x, scale, rcx) ||
            !TryScaledDifference(rc.y, r.y, scale, rcy) ||
            !TryScaledDifference(rc.z, r.z, scale, rcz))
        {
            return false;
        }
        const double left_front =
            lcx * bx + lcy * by + lcz * bz;
        const double right_front =
            rcx * bx + rcy * by + rcz * bz;
        const double left_side =
            lcx * lx + lcy * ly + lcz * lz;
        const double right_side =
            rcx * lx + rcy * ly + rcz * lz;
        return IsFiniteDouble(left_front) &&
            IsFiniteDouble(right_front) &&
            IsFiniteDouble(left_side) &&
            IsFiniteDouble(right_side) &&
            left_front < 0.0 &&
            right_front < 0.0 &&
            left_side > 0.0 &&
            right_side < 0.0;
    }

    void ProcessRefNodes()
    {
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            JBeamNormalizeResult normalized;
            const JBeamNormalizedTable* table = NormalizeSection(
                m_sections[part_index], "refNodes", normalized);
            if (m_resource_limit)
            {
                return;
            }
            if (table == NULL)
            {
                continue;
            }
            PreserveUnsupportedDefaults(
                *table,
                m_sections[part_index].part.provenance,
                "refNodes");
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t entry_index = 0U;
                 entry_index < table->entries.size();
                 ++entry_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const JBeamNormalizedTableEntry& entry =
                    table->entries[entry_index];
                if (entry.kind ==
                    JBeamNormalizedTableEntryKind::INVALID_ENTRY)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "refNodes",
                        entry_index,
                        std::string(),
                        "refNodes entry must be a data row or default");
                    continue;
                }
                if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DATA_ROW)
                {
                    continue;
                }
                ++m_ref_row_count;
                PreserveUnsupportedAssignments(
                    *table,
                    entry.data_row,
                    m_sections[part_index].part.provenance,
                    "refNodes",
                    entry_index);
                if (m_resource_limit)
                {
                    return;
                }
                JBeamStructuralRefFrame frame;
                frame.reference_index = INVALID_INDEX;
                frame.back_index = INVALID_INDEX;
                frame.left_index = INVALID_INDEX;
                frame.up_index = INVALID_INDEX;
                frame.left_corner_index = INVALID_INDEX;
                frame.right_corner_index = INVALID_INDEX;
                frame.provenance =
                    ProvenanceWithSpan(
                        m_sections[part_index].part.provenance,
                        entry.data_row.span);
                bool valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "ref:",
                    "ref",
                    frame.reference);
                valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "back:",
                    "back",
                    frame.back) && valid;
                valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "left:",
                    "left",
                    frame.left) && valid;
                valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "up:",
                    "up",
                    frame.up) && valid;
                valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "leftCorner:",
                    "leftCorner",
                    frame.left_corner) && valid;
                valid = ReadRequiredString(
                    entry.data_row,
                    frame.provenance,
                    "refNodes",
                    entry_index,
                    "rightCorner:",
                    "rightCorner",
                    frame.right_corner) && valid;
                const std::string ids[] = {
                    frame.reference,
                    frame.back,
                    frame.left,
                    frame.up,
                    frame.left_corner,
                    frame.right_corner
                };
                std::size_t* indices[] = {
                    &frame.reference_index,
                    &frame.back_index,
                    &frame.left_index,
                    &frame.up_index,
                    &frame.left_corner_index,
                    &frame.right_corner_index
                };
                if (valid)
                {
                    std::set<std::string> distinct;
                    for (std::size_t i = 0U; i < 6U; ++i)
                    {
                        if (!distinct.insert(ids[i]).second)
                        {
                            Push(
                                JBeamStructuralDiagnosticCode::
                                    DUPLICATE_VERTEX,
                                JBeamStructuralSeverity::ERROR_SEVERITY,
                                frame.provenance,
                                "refNodes",
                                entry_index,
                                std::string(),
                                "refNodes identifiers must be distinct");
                            valid = false;
                            break;
                        }
                    }
                }
                if (valid)
                {
                    for (std::size_t i = 0U; i < 6U; ++i)
                    {
                        if (!ResolveNode(ids[i], *indices[i]))
                        {
                            Push(
                                JBeamStructuralDiagnosticCode::
                                    MISSING_NODE_REFERENCE,
                                JBeamStructuralSeverity::ERROR_SEVERITY,
                                frame.provenance,
                                "refNodes",
                                entry_index,
                                std::string(),
                                "refNodes references an unknown node: " +
                                    ids[i]);
                            valid = false;
                        }
                    }
                }
                if (valid &&
                    IsDegenerateFrame(
                        frame.reference_index,
                        frame.back_index,
                        frame.left_index,
                        frame.up_index))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            DEGENERATE_REF_NODES,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        frame.provenance,
                        "refNodes",
                        entry_index,
                        std::string(),
                        "refNodes axes must span a nondegenerate frame");
                    valid = false;
                }
                if (valid &&
                    !IsAlignedFrame(
                        frame.reference_index,
                        frame.back_index,
                        frame.left_index,
                        frame.up_index))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            MISALIGNED_REF_NODES,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        frame.provenance,
                        "refNodes",
                        entry_index,
                        std::string(),
                        "refNodes must align with +Y back, +X left, "
                        "and +Z up");
                    valid = false;
                }
                if (valid &&
                    !AreAlignedFrontCorners(
                        frame.reference_index,
                        frame.back_index,
                        frame.left_index,
                        frame.left_corner_index,
                        frame.right_corner_index))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            MISALIGNED_REF_CORNERS,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        frame.provenance,
                        "refNodes",
                        entry_index,
                        std::string(),
                        "leftCorner and rightCorner must be forward of "
                        "ref and on their documented sides");
                    valid = false;
                }
                if (m_resource_limit)
                {
                    return;
                }
                if (valid && m_ref_row_count == 1U)
                {
                    std::size_t retained = 0U;
                    if (!AddSize(frame.reference.size(), retained) ||
                        !AddSize(frame.back.size(), retained) ||
                        !AddSize(frame.left.size(), retained) ||
                        !AddSize(frame.up.size(), retained) ||
                        !AddSize(
                            frame.left_corner.size(), retained) ||
                        !AddSize(
                            frame.right_corner.size(), retained))
                    {
                        RejectRetained();
                        return;
                    }
                    if (!ReserveRetained(retained))
                    {
                        return;
                    }
                    m_result.ref_frame = frame;
                    m_result.has_ref_frame = true;
                }
            }
        }
    }

    void ProcessBeams()
    {
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            JBeamNormalizeResult normalized;
            const JBeamNormalizedTable* table = NormalizeSection(
                m_sections[part_index], "beams", normalized);
            if (m_resource_limit)
            {
                return;
            }
            if (table == NULL)
            {
                continue;
            }
            PreserveUnsupportedDefaults(
                *table,
                m_sections[part_index].part.provenance,
                "beams");
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t entry_index = 0U;
                 entry_index < table->entries.size();
                 ++entry_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const JBeamNormalizedTableEntry& entry =
                    table->entries[entry_index];
                if (entry.kind ==
                    JBeamNormalizedTableEntryKind::INVALID_ENTRY)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "beams",
                        entry_index,
                        std::string(),
                        "Beam table entry is not a data row or default");
                    continue;
                }
                if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DATA_ROW)
                {
                    continue;
                }
                if (m_authored_beam_rows >= m_limits.max_beams)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::BEAM_LIMIT,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "beams",
                        entry_index,
                        std::string(),
                        "Beam table exceeds the configured beam limit");
                    m_resource_limit = true;
                    return;
                }
                ++m_authored_beam_rows;
                PreserveUnsupportedAssignments(
                    *table,
                    entry.data_row,
                    m_sections[part_index].part.provenance,
                    "beams",
                    entry_index);
                if (m_resource_limit)
                {
                    return;
                }
                JBeamStructuralBeam beam;
                beam.provenance =
                    ProvenanceWithSpan(
                        m_sections[part_index].part.provenance,
                        entry.data_row.span);
                bool valid = ReadRequiredString(
                    entry.data_row,
                    beam.provenance,
                    "beams",
                    entry_index,
                    "id1:",
                    "id1",
                    beam.node_a);
                valid = ReadRequiredString(
                    entry.data_row,
                    beam.provenance,
                    "beams",
                    entry_index,
                    "id2:",
                    "id2",
                    beam.node_b) && valid;
                valid = ReadOptionalBoolean(
                    entry.data_row,
                    beam.provenance,
                    "beams",
                    entry_index,
                    "optional",
                    false,
                    beam.optional) && valid;
                valid = ReadBeamType(
                    entry.data_row,
                    beam.provenance,
                    entry_index,
                    beam.beam_type,
                    beam.status) && valid;
                valid = ReadOptionalNumber(
                    entry.data_row, beam.provenance, "beams",
                    entry_index, "beamSpring",
                    beam.has_spring, beam.spring) && valid;
                valid = ReadOptionalNumber(
                    entry.data_row, beam.provenance, "beams",
                    entry_index, "beamDamp",
                    beam.has_damping, beam.damping) && valid;
                valid = ReadOptionalBeamLimit(
                    entry.data_row, beam.provenance,
                    entry_index, "beamDeform", beam.has_deform,
                    beam.deform_unbounded, beam.deform) && valid;
                valid = ReadOptionalBeamLimit(
                    entry.data_row, beam.provenance,
                    entry_index, "beamStrength", beam.has_strength,
                    beam.strength_unbounded, beam.strength) && valid;
                valid = ReadOptionalNumber(
                    entry.data_row, beam.provenance, "beams",
                    entry_index, "beamPrecompression",
                    beam.has_precompression,
                    beam.precompression) && valid;
                const bool invalid_spring =
                    beam.has_spring && beam.spring < 0.0;
                const bool invalid_damping =
                    beam.has_damping && beam.damping < 0.0;
                const bool invalid_deform =
                    beam.has_deform && !beam.deform_unbounded &&
                    beam.deform < 0.0;
                const bool invalid_strength =
                    beam.has_strength && !beam.strength_unbounded &&
                    beam.strength < 0.0;
                const bool invalid_precompression =
                    beam.has_precompression &&
                    !(beam.precompression > 0.0);
                if (valid &&
                    (invalid_spring ||
                     invalid_damping ||
                     invalid_deform ||
                     invalid_strength ||
                     invalid_precompression))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::
                            INVALID_BEAM_PARAMETER,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        beam.provenance,
                        "beams",
                        entry_index,
                        std::string(),
                        "Beam spring, damping, deform, and strength "
                        "must be nonnegative; precompression must be "
                        "positive");
                    valid = false;
                }
                if (valid && beam.node_a == beam.node_b)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::DUPLICATE_VERTEX,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        beam.provenance,
                        "beams",
                        entry_index,
                        std::string(),
                        "Beam endpoints must be distinct");
                    valid = false;
                }
                const bool has_a =
                    valid && ResolveNode(
                        beam.node_a, beam.node_a_index);
                const bool has_b =
                    valid && ResolveNode(
                        beam.node_b, beam.node_b_index);
                if (valid && (!has_a || !has_b))
                {
                    if (beam.optional)
                    {
                        beam.status =
                            JBeamStructuralBeamStatus::
                                PRESERVED_DISABLED_OPTIONAL_REFERENCE;
                        Push(
                            JBeamStructuralDiagnosticCode::
                                OPTIONAL_BEAM_SKIPPED,
                            JBeamStructuralSeverity::WARNING,
                            beam.provenance,
                            "beams",
                            entry_index,
                            std::string(),
                            "Optional beam with a missing node reference "
                            "is preserved but disabled");
                    }
                    else
                    {
                        Push(
                            JBeamStructuralDiagnosticCode::
                                MISSING_NODE_REFERENCE,
                            JBeamStructuralSeverity::ERROR_SEVERITY,
                            beam.provenance,
                            "beams",
                            entry_index,
                            std::string(),
                            "Beam references an unknown node");
                        valid = false;
                    }
                }
                if (valid && has_a && has_b &&
                    AreSamePosition(
                        beam.node_a_index, beam.node_b_index))
                {
                    Push(
                        JBeamStructuralDiagnosticCode::DEGENERATE_BEAM,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        beam.provenance,
                        "beams",
                        entry_index,
                        std::string(),
                        "Beam endpoints occupy the same position");
                    valid = false;
                }
                if (m_resource_limit)
                {
                    return;
                }
                if (valid)
                {
                    std::size_t retained = 0U;
                    if (!AddSize(beam.node_a.size(), retained) ||
                        !AddSize(beam.node_b.size(), retained) ||
                        !AddSize(beam.beam_type.size(), retained))
                    {
                        RejectRetained();
                        return;
                    }
                    if (!ReserveRetained(retained))
                    {
                        return;
                    }
                    m_result.beams.push_back(beam);
                }
            }
        }
    }

    bool BuildTriangle(
        const JBeamNormalizedDataRow& row,
        const JBeamStructuralProvenance& part_provenance,
        const std::string& section,
        std::size_t entry_index,
        const std::string& first,
        const std::string& second,
        const std::string& third,
        bool optional,
        JBeamStructuralTriangleOrigin origin,
        JBeamStructuralTriangle& triangle)
    {
        triangle.provenance =
            ProvenanceWithSpan(part_provenance, row.span);
        triangle.node_a_index = INVALID_INDEX;
        triangle.node_b_index = INVALID_INDEX;
        triangle.node_c_index = INVALID_INDEX;
        triangle.optional = optional;
        triangle.status = JBeamStructuralTriangleStatus::ENABLED;
        triangle.origin = origin;
        triangle.authored_row_index = entry_index;
        bool valid = ReadRequiredString(
            row, triangle.provenance, section, entry_index,
            first + ":", first, triangle.node_a);
        valid = ReadRequiredString(
            row, triangle.provenance, section, entry_index,
            second + ":", second, triangle.node_b) && valid;
        valid = ReadRequiredString(
            row, triangle.provenance, section, entry_index,
            third + ":", third, triangle.node_c) && valid;
        if (!valid)
        {
            return false;
        }
        if (m_resource_limit)
        {
            return false;
        }
        if (triangle.node_a == triangle.node_b ||
            triangle.node_a == triangle.node_c ||
            triangle.node_b == triangle.node_c)
        {
            Push(
                JBeamStructuralDiagnosticCode::DUPLICATE_VERTEX,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                triangle.provenance,
                section,
                entry_index,
                std::string(),
                "Triangle vertices must be distinct");
            return false;
        }
        bool references_valid = true;
        if (!ResolveNode(
                triangle.node_a, triangle.node_a_index))
        {
            references_valid = false;
        }
        if (!ResolveNode(
                triangle.node_b, triangle.node_b_index))
        {
            references_valid = false;
        }
        if (!ResolveNode(
                triangle.node_c, triangle.node_c_index))
        {
            references_valid = false;
        }
        if (!references_valid)
        {
            if (triangle.optional)
            {
                triangle.status =
                    JBeamStructuralTriangleStatus::
                        PRESERVED_DISABLED_OPTIONAL_REFERENCE;
                Push(
                    JBeamStructuralDiagnosticCode::
                        OPTIONAL_SURFACE_SKIPPED,
                    JBeamStructuralSeverity::WARNING,
                    triangle.provenance,
                    section,
                    entry_index,
                    std::string(),
                    "Optional surface with a missing node reference "
                    "is preserved but disabled");
            }
            else
            {
                Push(
                    JBeamStructuralDiagnosticCode::
                        MISSING_NODE_REFERENCE,
                    JBeamStructuralSeverity::ERROR_SEVERITY,
                    triangle.provenance,
                    section,
                    entry_index,
                    std::string(),
                    "Triangle references an unknown node");
                return false;
            }
        }
        if (references_valid && IsDegenerateTriangle(
                triangle.node_a_index,
                triangle.node_b_index,
                triangle.node_c_index))
        {
            Push(
                JBeamStructuralDiagnosticCode::DEGENERATE_TRIANGLE,
                JBeamStructuralSeverity::ERROR_SEVERITY,
                triangle.provenance,
                section,
                entry_index,
                std::string(),
                "Triangle geometry is degenerate");
            return false;
        }
        if (m_resource_limit)
        {
            return false;
        }
        std::size_t retained = 0U;
        if (!AddSize(triangle.node_a.size(), retained) ||
            !AddSize(triangle.node_b.size(), retained) ||
            !AddSize(triangle.node_c.size(), retained))
        {
            RejectRetained();
            return false;
        }
        if (!ReserveRetained(retained))
        {
            return false;
        }
        return true;
    }

    void ProcessTriangles()
    {
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            JBeamNormalizeResult normalized;
            const JBeamNormalizedTable* table = NormalizeSection(
                m_sections[part_index], "triangles", normalized);
            if (m_resource_limit)
            {
                return;
            }
            if (table == NULL)
            {
                continue;
            }
            PreserveUnsupportedDefaults(
                *table,
                m_sections[part_index].part.provenance,
                "triangles");
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t entry_index = 0U;
                 entry_index < table->entries.size();
                 ++entry_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const JBeamNormalizedTableEntry& entry =
                    table->entries[entry_index];
                if (entry.kind ==
                    JBeamNormalizedTableEntryKind::INVALID_ENTRY)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "triangles",
                        entry_index,
                        std::string(),
                        "Triangle table entry is not a data row or default");
                    continue;
                }
                if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DATA_ROW)
                {
                    continue;
                }
                PreserveUnsupportedAssignments(
                    *table,
                    entry.data_row,
                    m_sections[part_index].part.provenance,
                    "triangles",
                    entry_index);
                if (m_resource_limit)
                {
                    return;
                }
                if (m_result.triangles.size() >=
                    m_limits.max_triangles)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::TRIANGLE_LIMIT,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "triangles",
                        entry_index,
                        std::string(),
                        "Triangle tables exceed the configured limit");
                    m_resource_limit = true;
                    return;
                }
                bool optional = false;
                if (!ReadOptionalBoolean(
                        entry.data_row,
                        m_sections[part_index].part.provenance,
                        "triangles",
                        entry_index,
                        "optional",
                        false,
                        optional))
                {
                    continue;
                }
                JBeamStructuralTriangle triangle;
                if (BuildTriangle(
                        entry.data_row,
                        m_sections[part_index].part.provenance,
                        "triangles",
                        entry_index,
                        "id1",
                        "id2",
                        "id3",
                        optional,
                        JBeamStructuralTriangleOrigin::TRIANGLE,
                        triangle))
                {
                    m_result.triangles.push_back(triangle);
                }
            }
        }
    }

    void ProcessQuads()
    {
        for (std::size_t part_index = 0U;
             part_index < m_sections.size();
             ++part_index)
        {
            JBeamNormalizeResult normalized;
            const JBeamNormalizedTable* table = NormalizeSection(
                m_sections[part_index], "quads", normalized);
            if (m_resource_limit)
            {
                return;
            }
            if (table == NULL)
            {
                continue;
            }
            PreserveUnsupportedDefaults(
                *table,
                m_sections[part_index].part.provenance,
                "quads");
            if (m_resource_limit)
            {
                return;
            }
            for (std::size_t entry_index = 0U;
                 entry_index < table->entries.size();
                 ++entry_index)
            {
                if (m_resource_limit)
                {
                    return;
                }
                const JBeamNormalizedTableEntry& entry =
                    table->entries[entry_index];
                if (entry.kind ==
                    JBeamNormalizedTableEntryKind::INVALID_ENTRY)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "quads",
                        entry_index,
                        std::string(),
                        "Quad table entry is not a data row or default");
                    continue;
                }
                if (entry.kind !=
                    JBeamNormalizedTableEntryKind::DATA_ROW)
                {
                    continue;
                }
                PreserveUnsupportedAssignments(
                    *table,
                    entry.data_row,
                    m_sections[part_index].part.provenance,
                    "quads",
                    entry_index);
                if (m_resource_limit)
                {
                    return;
                }
                if (m_limits.max_triangles -
                        std::min(
                            m_result.triangles.size(),
                            m_limits.max_triangles) < 2U)
                {
                    Push(
                        JBeamStructuralDiagnosticCode::TRIANGLE_LIMIT,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        m_sections[part_index].part.provenance,
                        "quads",
                        entry_index,
                        std::string(),
                        "Quad requires two triangle slots");
                    m_resource_limit = true;
                    return;
                }
                std::string id1;
                std::string id2;
                std::string id3;
                std::string id4;
                const JBeamStructuralProvenance provenance =
                    m_sections[part_index].part.provenance;
                bool valid = ReadRequiredString(
                    entry.data_row, provenance, "quads", entry_index,
                    "id1:", "id1", id1);
                valid = ReadRequiredString(
                    entry.data_row, provenance, "quads", entry_index,
                    "id2:", "id2", id2) && valid;
                valid = ReadRequiredString(
                    entry.data_row, provenance, "quads", entry_index,
                    "id3:", "id3", id3) && valid;
                valid = ReadRequiredString(
                    entry.data_row, provenance, "quads", entry_index,
                    "id4:", "id4", id4) && valid;
                bool optional = false;
                valid = ReadOptionalBoolean(
                    entry.data_row,
                    provenance,
                    "quads",
                    entry_index,
                    "optional",
                    false,
                    optional) && valid;
                if (!valid)
                {
                    continue;
                }
                std::set<std::string> distinct;
                distinct.insert(id1);
                distinct.insert(id2);
                distinct.insert(id3);
                distinct.insert(id4);
                if (distinct.size() != 4U)
                {
                    JBeamStructuralProvenance row_provenance =
                        ProvenanceWithSpan(
                            provenance, entry.data_row.span);
                    Push(
                        JBeamStructuralDiagnosticCode::DUPLICATE_VERTEX,
                        JBeamStructuralSeverity::ERROR_SEVERITY,
                        row_provenance,
                        "quads",
                        entry_index,
                        std::string(),
                        "Quad vertices must be distinct");
                    continue;
                }

                // Documented fixed diagonal: (id1,id2,id3) and
                // (id1,id3,id4), preserving authored winding.
                JBeamStructuralTriangle first;
                JBeamStructuralTriangle second;
                bool first_valid = BuildTriangle(
                    entry.data_row, provenance, "quads", entry_index,
                    "id1", "id2", "id3",
                    optional,
                    JBeamStructuralTriangleOrigin::QUAD_FIRST,
                    first);
                bool second_valid = BuildTriangle(
                    entry.data_row, provenance, "quads", entry_index,
                    "id1", "id3", "id4",
                    optional,
                    JBeamStructuralTriangleOrigin::QUAD_SECOND,
                    second);
                if (m_resource_limit)
                {
                    return;
                }
                if (first_valid && second_valid)
                {
                    if (first.status ==
                            JBeamStructuralTriangleStatus::
                                PRESERVED_DISABLED_OPTIONAL_REFERENCE ||
                        second.status ==
                            JBeamStructuralTriangleStatus::
                                PRESERVED_DISABLED_OPTIONAL_REFERENCE)
                    {
                        first.status =
                            JBeamStructuralTriangleStatus::
                                PRESERVED_DISABLED_OPTIONAL_REFERENCE;
                        second.status =
                            JBeamStructuralTriangleStatus::
                                PRESERVED_DISABLED_OPTIONAL_REFERENCE;
                    }
                    m_result.triangles.push_back(first);
                    m_result.triangles.push_back(second);
                }
            }
        }
    }
};

class BoundedStringBuffer : public std::streambuf
{
public:
    explicit BoundedStringBuffer(std::size_t limit)
        : m_limit(limit)
        , m_exceeded(false)
    {
    }

    const std::string& Value() const
    {
        return m_value;
    }

    bool Exceeded() const
    {
        return m_exceeded;
    }

protected:
    virtual std::streamsize xsputn(
        const char* value,
        std::streamsize count)
    {
        if (count < 0)
        {
            m_exceeded = true;
            return 0;
        }
        const std::size_t unsigned_count =
            static_cast<std::size_t>(count);
        if (unsigned_count >
            m_limit - std::min(m_value.size(), m_limit))
        {
            m_exceeded = true;
            return 0;
        }
        m_value.append(value, unsigned_count);
        return count;
    }

    virtual int_type overflow(int_type character)
    {
        if (traits_type::eq_int_type(
                character, traits_type::eof()))
        {
            return traits_type::not_eof(character);
        }
        if (m_value.size() >= m_limit)
        {
            m_exceeded = true;
            return traits_type::eof();
        }
        m_value.push_back(
            traits_type::to_char_type(character));
        return character;
    }

private:
    std::size_t m_limit;
    bool m_exceeded;
    std::string m_value;
};

struct LengthPrefixedView
{
    const std::string* value;
};

LengthPrefixedView LengthPrefixed(const std::string& value)
{
    LengthPrefixedView view;
    view.value = &value;
    return view;
}

std::ostream& operator<<(
    std::ostream& output,
    const LengthPrefixedView& view)
{
    // Stream directly into the caller's bounded buffer. Returning a complete
    // std::string here would allocate an unbounded second copy before the
    // BoundedStringBuffer could reject it.
    output << view.value->size() << ':';
    output << *view.value;
    return output;
}

void AppendProvenance(
    const JBeamStructuralProvenance& provenance,
    std::ostream& output)
{
    output
        << provenance.PartPreorderIndex() << '\t'
        << LengthPrefixed(provenance.PartName()) << '\t'
        << LengthPrefixed(provenance.PackagePath()) << '\t'
        << LengthPrefixed(provenance.SourceName()) << '\t'
        << provenance.begin.byte_offset << '\t'
        << provenance.begin.line << '\t'
        << provenance.begin.column << '\t'
        << provenance.end.byte_offset << '\t'
        << provenance.end.line << '\t'
        << provenance.end.column;
}

bool AppendCanonicalValue(
    const JBeamValue& value,
    std::ostream& output,
    std::set<const JBeamValue*>& active,
    std::size_t depth,
    std::size_t& work_units,
    std::size_t max_work_units)
{
    static const std::size_t MAX_CANONICAL_VALUE_DEPTH = 256U;
    if (depth > MAX_CANONICAL_VALUE_DEPTH ||
        work_units >= max_work_units ||
        !active.insert(&value).second)
    {
        return false;
    }
    ++work_units;
    bool valid = true;
    switch (value.type)
    {
    case JBeamValueType::NULL_VALUE:
        output << 'n';
        break;
    case JBeamValueType::BOOLEAN:
        output << (value.boolean_value ? "b1" : "b0");
        break;
    case JBeamValueType::NUMBER:
        output << 'd' << LengthPrefixed(value.scalar_text);
        break;
    case JBeamValueType::STRING:
        output << 's' << LengthPrefixed(value.scalar_text);
        break;
    case JBeamValueType::ARRAY:
        output << 'a' << value.array_values.size() << '[';
        for (std::size_t i = 0U;
             valid && i < value.array_values.size();
             ++i)
        {
            valid = AppendCanonicalValue(
                value.array_values[i],
                output,
                active,
                depth + 1U,
                work_units,
                max_work_units);
            valid = valid && static_cast<bool>(output);
        }
        output << ']';
        break;
    case JBeamValueType::OBJECT:
        output << 'o' << value.object_fields.size() << '{';
        for (std::size_t i = 0U;
             valid && i < value.object_fields.size();
             ++i)
        {
            output << 'k'
                << LengthPrefixed(value.object_fields[i].key);
            if (value.object_fields[i].value)
            {
                valid = AppendCanonicalValue(
                    *value.object_fields[i].value,
                    output,
                    active,
                    depth + 1U,
                    work_units,
                    max_work_units);
            }
            else
            {
                output << 'x';
            }
            valid = valid && static_cast<bool>(output);
        }
        output << '}';
        break;
    }
    active.erase(&value);
    return valid && static_cast<bool>(output);
}

void AppendDouble(double value, std::ostream& output)
{
    if (value == 0.0)
    {
        value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::ios_base::fmtflags previous = output.flags();
    const char previous_fill = output.fill();
    output << std::hex << std::setw(16) << std::setfill('0') << bits;
    output.flags(previous);
    output.fill(previous_fill);
}

} // namespace

JBeamStructuralPartIdentity::JBeamStructuralPartIdentity()
    : part_preorder_index(0U)
{
}

JBeamStructuralProvenance::JBeamStructuralProvenance()
{
}

std::size_t JBeamStructuralProvenance::PartPreorderIndex() const
{
    return part ? part->part_preorder_index : 0U;
}

const std::string& JBeamStructuralProvenance::PartName() const
{
    static const std::string empty;
    return part ? part->part_name : empty;
}

const std::string& JBeamStructuralProvenance::PackagePath() const
{
    static const std::string empty;
    return part ? part->package_path : empty;
}

const std::string& JBeamStructuralProvenance::SourceName() const
{
    static const std::string empty;
    return source_name ? *source_name : empty;
}

JBeamSourceSpan JBeamStructuralProvenance::SourceSpan() const
{
    JBeamSourceSpan span;
    span.source_name = SourceName();
    span.begin = begin;
    span.end = end;
    return span;
}

JBeamStructuralDiagnostic::JBeamStructuralDiagnostic()
    : code(JBeamStructuralDiagnosticCode::INVALID_RESOLVED_GRAPH)
    , severity(JBeamStructuralSeverity::ERROR_SEVERITY)
    , row_index(0U)
    , has_preserved_value(false)
{
}

JBeamStructuralLimits::JBeamStructuralLimits()
    : max_parts(1024U)
    , max_rows(1000000U)
    , max_nodes(250000U)
    , max_beams(1000000U)
    , max_triangles(1000000U)
    , max_diagnostics(4096U)
    , max_retained_bytes(64U * 1024U * 1024U)
    , max_preserved_value_work_units(1000000U)
    , max_preserved_value_depth(256U)
    , max_expression_evaluations(1000000U)
    , max_expression_work_units(268435456U)
    , max_component_nodes(16384U)
    , max_component_depth(64U)
    , max_canonical_output_bytes(256U * 1024U * 1024U)
    , max_canonical_work_units(4000000U)
{
}

JBeamStructuralBeam::JBeamStructuralBeam()
    : node_a_index(INVALID_INDEX)
    , node_b_index(INVALID_INDEX)
    , optional(false)
    , status(JBeamStructuralBeamStatus::ENABLED)
    , has_spring(false)
    , spring(0.0)
    , has_damping(false)
    , damping(0.0)
    , has_deform(false)
    , deform_unbounded(false)
    , deform(0.0)
    , has_strength(false)
    , strength_unbounded(false)
    , strength(0.0)
    , has_precompression(false)
    , precompression(0.0)
{
}

JBeamStructuralIR::JBeamStructuralIR()
    : has_ref_frame(false)
    , authored_row_count(0U)
    , retained_byte_count(0U)
    , canonical_output_byte_limit(256U * 1024U * 1024U)
    , canonical_work_unit_limit(4000000U)
{
    ref_frame.reference_index = INVALID_INDEX;
    ref_frame.back_index = INVALID_INDEX;
    ref_frame.left_index = INVALID_INDEX;
    ref_frame.up_index = INVALID_INDEX;
    ref_frame.left_corner_index = INVALID_INDEX;
    ref_frame.right_corner_index = INVALID_INDEX;
}

bool JBeamStructuralIR::IsValid() const
{
    return has_ref_frame && !HasErrors(diagnostics);
}

JBeamStructuralIR BuildJBeamStructuralIR(
    const JBeamResolvedGraph& graph,
    const JBeamStructuralLimits& limits)
{
    return StructuralBuilder(graph, limits).Run();
}

std::string SerializeCanonicalJBeamStructuralIR(
    const JBeamStructuralIR& ir)
{
    if (ir.canonical_output_byte_limit == 0U ||
        ir.canonical_work_unit_limit == 0U)
    {
        return std::string();
    }
    std::size_t canonical_work = 1U;
    if (!AddSize(ir.parts.size(), canonical_work) ||
        !AddSize(ir.nodes.size(), canonical_work) ||
        !AddSize(ir.beams.size(), canonical_work) ||
        !AddSize(ir.triangles.size(), canonical_work) ||
        !AddSize(ir.diagnostics.size(), canonical_work) ||
        canonical_work > ir.canonical_work_unit_limit)
    {
        return std::string();
    }
    BoundedStringBuffer buffer(ir.canonical_output_byte_limit);
    std::ostream output(&buffer);
    output.imbue(std::locale::classic());
    output << "ror-beamng-structural-ir-v1\n";
    output << "parts\t" << ir.parts.size() << '\n';
    for (std::size_t i = 0U;
         output && i < ir.parts.size();
         ++i)
    {
        output << "P\t";
        AppendProvenance(ir.parts[i].provenance, output);
        output << '\n';
    }
    if (!output)
    {
        return std::string();
    }
    output << "rows\t" << ir.authored_row_count << '\n';
    output << "nodes\t" << ir.nodes.size() << '\n';
    for (std::size_t i = 0U;
         output && i < ir.nodes.size();
         ++i)
    {
        const JBeamStructuralNode& node = ir.nodes[i];
        output << "N\t" << LengthPrefixed(node.id) << '\t';
        AppendDouble(node.x, output);
        output << '\t';
        AppendDouble(node.y, output);
        output << '\t';
        AppendDouble(node.z, output);
        output << '\t';
        AppendDouble(node.node_weight, output);
        output << '\t' << (node.node_weight_authored ? 1 : 0) << '\t';
        AppendProvenance(node.provenance, output);
        output << '\n';
    }
    if (!output)
    {
        return std::string();
    }
    output << "beams\t" << ir.beams.size() << '\n';
    for (std::size_t i = 0U;
         output && i < ir.beams.size();
         ++i)
    {
        const JBeamStructuralBeam& beam = ir.beams[i];
        output
            << "B\t" << LengthPrefixed(beam.node_a) << '\t'
            << LengthPrefixed(beam.node_b) << '\t'
            << beam.node_a_index << '\t'
            << beam.node_b_index << '\t'
            << LengthPrefixed(beam.beam_type) << '\t'
            << (beam.optional ? 1 : 0) << '\t'
            << static_cast<int>(beam.status) << '\t';
        const bool flags[] = {
            beam.has_spring,
            beam.has_damping,
            beam.has_deform,
            beam.has_strength,
            beam.has_precompression
        };
        const double values[] = {
            beam.spring,
            beam.damping,
            beam.deform,
            beam.strength,
            beam.precompression
        };
        for (std::size_t field = 0U; field < 5U; ++field)
        {
            output << (flags[field] ? 1 : 0) << ':';
            AppendDouble(values[field], output);
            output << '\t';
        }
        output
            << (beam.deform_unbounded ? 1 : 0) << '\t'
            << (beam.strength_unbounded ? 1 : 0) << '\t';
        AppendProvenance(beam.provenance, output);
        output << '\n';
    }
    if (!output)
    {
        return std::string();
    }
    output << "triangles\t" << ir.triangles.size() << '\n';
    for (std::size_t i = 0U;
         output && i < ir.triangles.size();
         ++i)
    {
        const JBeamStructuralTriangle& triangle = ir.triangles[i];
        output
            << "T\t" << LengthPrefixed(triangle.node_a) << '\t'
            << LengthPrefixed(triangle.node_b) << '\t'
            << LengthPrefixed(triangle.node_c) << '\t'
            << triangle.node_a_index << '\t'
            << triangle.node_b_index << '\t'
            << triangle.node_c_index << '\t'
            << (triangle.optional ? 1 : 0) << '\t'
            << static_cast<int>(triangle.status) << '\t'
            << static_cast<int>(triangle.origin) << '\t'
            << triangle.authored_row_index << '\t';
        AppendProvenance(triangle.provenance, output);
        output << '\n';
    }
    if (!output)
    {
        return std::string();
    }
    output << "ref-frame\t" << (ir.has_ref_frame ? 1 : 0);
    if (ir.has_ref_frame)
    {
        output
            << '\t' << LengthPrefixed(ir.ref_frame.reference)
            << '\t' << LengthPrefixed(ir.ref_frame.back)
            << '\t' << LengthPrefixed(ir.ref_frame.left)
            << '\t' << LengthPrefixed(ir.ref_frame.up)
            << '\t' << LengthPrefixed(ir.ref_frame.left_corner)
            << '\t' << LengthPrefixed(ir.ref_frame.right_corner)
            << '\t' << ir.ref_frame.reference_index
            << '\t' << ir.ref_frame.back_index
            << '\t' << ir.ref_frame.left_index
            << '\t' << ir.ref_frame.up_index
            << '\t' << ir.ref_frame.left_corner_index
            << '\t' << ir.ref_frame.right_corner_index << '\t';
        AppendProvenance(ir.ref_frame.provenance, output);
    }
    output << '\n';
    if (!output)
    {
        return std::string();
    }
    std::vector<std::size_t> diagnostic_order;
    diagnostic_order.reserve(ir.diagnostics.size());
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        diagnostic_order.push_back(i);
    }
    std::sort(
        diagnostic_order.begin(),
        diagnostic_order.end(),
        [&ir](std::size_t left_index, std::size_t right_index)
        {
            const JBeamStructuralDiagnostic& left =
                ir.diagnostics[left_index];
            const JBeamStructuralDiagnostic& right =
                ir.diagnostics[right_index];
            if (DiagnosticLess(left, right))
            {
                return true;
            }
            if (DiagnosticLess(right, left))
            {
                return false;
            }
            return left_index < right_index;
        });
    output << "diagnostics\t" << diagnostic_order.size() << '\n';
    for (std::size_t i = 0U;
         output && i < diagnostic_order.size();
         ++i)
    {
        const JBeamStructuralDiagnostic& diagnostic =
            ir.diagnostics[diagnostic_order[i]];
        output
            << "D\t" << static_cast<int>(diagnostic.code) << '\t'
            << static_cast<int>(diagnostic.severity) << '\t';
        AppendProvenance(diagnostic.provenance, output);
        output
            << '\t' << LengthPrefixed(diagnostic.section)
            << '\t' << diagnostic.row_index
            << '\t' << LengthPrefixed(diagnostic.field_name)
            << '\t' << LengthPrefixed(diagnostic.detail)
            << '\t' << (diagnostic.has_preserved_value ? 1 : 0);
        if (diagnostic.has_preserved_value &&
            diagnostic.preserved_value)
        {
            output << '\t';
            std::set<const JBeamValue*> active_values;
            if (!AppendCanonicalValue(
                    *diagnostic.preserved_value,
                    output,
                    active_values,
                    0U,
                    canonical_work,
                    ir.canonical_work_unit_limit))
            {
                return std::string();
            }
        }
        output << '\n';
    }
    if (buffer.Exceeded() || !output)
    {
        return std::string();
    }
    return buffer.Value();
}

const char* JBeamStructuralDiagnosticCodeToString(
    JBeamStructuralDiagnosticCode code)
{
    switch (code)
    {
    case JBeamStructuralDiagnosticCode::INVALID_RESOLVED_GRAPH:
        return "invalid-resolved-graph";
    case JBeamStructuralDiagnosticCode::RESOLVED_PART_LIMIT:
        return "resolved-part-limit";
    case JBeamStructuralDiagnosticCode::ROW_LIMIT:
        return "row-limit";
    case JBeamStructuralDiagnosticCode::NODE_LIMIT:
        return "node-limit";
    case JBeamStructuralDiagnosticCode::BEAM_LIMIT:
        return "beam-limit";
    case JBeamStructuralDiagnosticCode::TRIANGLE_LIMIT:
        return "triangle-limit";
    case JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT:
        return "retained-byte-limit";
    case JBeamStructuralDiagnosticCode::PRESERVED_VALUE_LIMIT:
        return "preserved-value-limit";
    case JBeamStructuralDiagnosticCode::NORMALIZATION_ERROR:
        return "normalization-error";
    case JBeamStructuralDiagnosticCode::NORMALIZATION_WARNING:
        return "normalization-warning";
    case JBeamStructuralDiagnosticCode::PART_BODY_NOT_OBJECT:
        return "part-body-not-object";
    case JBeamStructuralDiagnosticCode::DUPLICATE_SECTION:
        return "duplicate-section";
    case JBeamStructuralDiagnosticCode::INVALID_SECTION:
        return "invalid-section";
    case JBeamStructuralDiagnosticCode::INVALID_TABLE_HEADER:
        return "invalid-table-header";
    case JBeamStructuralDiagnosticCode::DUPLICATE_TABLE_HEADER:
        return "duplicate-table-header";
    case JBeamStructuralDiagnosticCode::INVALID_TABLE_ROW:
        return "invalid-table-row";
    case JBeamStructuralDiagnosticCode::MISSING_REQUIRED_FIELD:
        return "missing-required-field";
    case JBeamStructuralDiagnosticCode::AMBIGUOUS_REQUIRED_FIELD:
        return "ambiguous-required-field";
    case JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE:
        return "invalid-field-type";
    case JBeamStructuralDiagnosticCode::EXPRESSION_ERROR:
        return "expression-error";
    case JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT:
        return "expression-limit";
    case JBeamStructuralDiagnosticCode::EXPRESSION_DISABLED:
        return "expression-disabled";
    case JBeamStructuralDiagnosticCode::UNSUPPORTED_COMPONENT_VALUE:
        return "unsupported-component-value";
    case JBeamStructuralDiagnosticCode::INVALID_COMPONENT_PATH:
        return "invalid-component-path";
    case JBeamStructuralDiagnosticCode::INVALID_VARIABLE_VALUE:
        return "invalid-variable-value";
    case JBeamStructuralDiagnosticCode::NON_FINITE_NUMBER:
        return "non-finite-number";
    case JBeamStructuralDiagnosticCode::INVALID_NODE_WEIGHT:
        return "invalid-node-weight";
    case JBeamStructuralDiagnosticCode::INVALID_BEAM_PARAMETER:
        return "invalid-beam-parameter";
    case JBeamStructuralDiagnosticCode::DUPLICATE_NODE_ID:
        return "duplicate-node-id";
    case JBeamStructuralDiagnosticCode::MISSING_NODE_REFERENCE:
        return "missing-node-reference";
    case JBeamStructuralDiagnosticCode::OPTIONAL_BEAM_SKIPPED:
        return "optional-beam-skipped";
    case JBeamStructuralDiagnosticCode::OPTIONAL_SURFACE_SKIPPED:
        return "optional-surface-skipped";
    case JBeamStructuralDiagnosticCode::DUPLICATE_VERTEX:
        return "duplicate-vertex";
    case JBeamStructuralDiagnosticCode::DEGENERATE_BEAM:
        return "degenerate-beam";
    case JBeamStructuralDiagnosticCode::DEGENERATE_TRIANGLE:
        return "degenerate-triangle";
    case JBeamStructuralDiagnosticCode::SPECIAL_BEAM_TYPE_DISABLED:
        return "special-beam-type-disabled";
    case JBeamStructuralDiagnosticCode::UNKNOWN_SECTION:
        return "unknown-section";
    case JBeamStructuralDiagnosticCode::UNKNOWN_FIELD:
        return "unknown-field";
    case JBeamStructuralDiagnosticCode::UNSUPPORTED_FIELD:
        return "unsupported-field";
    case JBeamStructuralDiagnosticCode::MISSING_REF_NODES:
        return "missing-ref-nodes";
    case JBeamStructuralDiagnosticCode::DUPLICATE_REF_NODES:
        return "duplicate-ref-nodes";
    case JBeamStructuralDiagnosticCode::DEGENERATE_REF_NODES:
        return "degenerate-ref-nodes";
    case JBeamStructuralDiagnosticCode::MISALIGNED_REF_NODES:
        return "misaligned-ref-nodes";
    case JBeamStructuralDiagnosticCode::MISALIGNED_REF_CORNERS:
        return "misaligned-ref-corners";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
