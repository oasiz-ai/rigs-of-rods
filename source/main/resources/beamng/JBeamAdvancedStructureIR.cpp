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

#include "JBeamAdvancedStructureIR.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace RoR {
namespace BeamNG {
namespace {

const std::size_t HARD_MAX_PARTS = 4096U;
const std::size_t HARD_MAX_GRAPH_DEPTH = 64U;
const std::size_t HARD_MAX_SOURCE_RECORDS = 32768U;
const std::size_t HARD_MAX_ENTRIES = 131072U;
const std::size_t HARD_MAX_MODIFIERS = 65536U;
const std::size_t HARD_MAX_EFFECTIVE_FIELDS = 524288U;
const std::size_t HARD_MAX_NODE_COORDINATES = 131072U;
const std::size_t HARD_MAX_DIAGNOSTICS = 4096U;
const std::size_t HARD_MAX_RETAINED_BYTES = 64U * 1024U * 1024U;
const std::size_t HARD_MAX_WORK = 4000000U;
const std::size_t HARD_MAX_VALUE_WORK = 2000000U;
const std::size_t HARD_MAX_VALUE_DEPTH = 64U;
const std::size_t HARD_MAX_CANONICAL_BYTES = 64U * 1024U * 1024U;
const std::size_t HARD_MAX_CANONICAL_WORK = 4000000U;

static_assert(
    std::numeric_limits<double>::is_iec559 &&
        std::numeric_limits<double>::digits == 53,
    "advanced JBeam inventory requires IEC 559 binary64");

bool AddSize(std::size_t value, std::size_t& total)
{
    if (value > std::numeric_limits<std::size_t>::max() - total)
    {
        return false;
    }
    total += value;
    return true;
}

JBeamAdvancedLimits SanitizeLimits(const JBeamAdvancedLimits& requested)
{
    JBeamAdvancedLimits result = requested;
    result.max_parts = std::min(result.max_parts, HARD_MAX_PARTS);
    result.max_graph_depth =
        std::min(result.max_graph_depth, HARD_MAX_GRAPH_DEPTH);
    result.max_source_records =
        std::min(result.max_source_records, HARD_MAX_SOURCE_RECORDS);
    result.max_entries = std::min(result.max_entries, HARD_MAX_ENTRIES);
    result.max_modifiers =
        std::min(result.max_modifiers, HARD_MAX_MODIFIERS);
    result.max_effective_fields =
        std::min(result.max_effective_fields, HARD_MAX_EFFECTIVE_FIELDS);
    result.max_node_coordinates =
        std::min(result.max_node_coordinates, HARD_MAX_NODE_COORDINATES);
    result.max_diagnostics =
        std::min(result.max_diagnostics, HARD_MAX_DIAGNOSTICS);
    result.max_retained_bytes =
        std::min(result.max_retained_bytes, HARD_MAX_RETAINED_BYTES);
    result.max_work_units =
        std::min(result.max_work_units, HARD_MAX_WORK);
    result.max_preserved_value_work_units =
        std::min(
            result.max_preserved_value_work_units,
            HARD_MAX_VALUE_WORK);
    result.max_preserved_value_depth =
        std::min(result.max_preserved_value_depth, HARD_MAX_VALUE_DEPTH);
    result.max_canonical_output_bytes =
        std::min(
            result.max_canonical_output_bytes,
            HARD_MAX_CANONICAL_BYTES);
    result.max_canonical_work_units =
        std::min(
            result.max_canonical_work_units,
            HARD_MAX_CANONICAL_WORK);
    return result;
}

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0U;
    volatile unsigned char stored[sizeof(double)];
    const unsigned char* source =
        reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0U; i < sizeof(double); ++i)
    {
        stored[i] = source[i];
    }
    unsigned char* destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(double); ++i)
    {
        destination[i] = stored[i];
    }
    return bits;
}

bool IsFiniteDouble(double value)
{
    return (DoubleBits(value) & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool IsExpression(const JBeamValue& value)
{
    return value.type == JBeamValueType::STRING &&
        !value.scalar_text.empty() &&
        value.scalar_text[0] == '$';
}

bool IsFltMax(const JBeamValue& value)
{
    return value.type == JBeamValueType::STRING &&
        value.scalar_text == "FLT_MAX";
}

bool IsTargetSection(
    const std::string& name,
    JBeamAdvancedSectionKind& kind)
{
    if (name == "hydros")
    {
        kind = JBeamAdvancedSectionKind::HYDROS;
        return true;
    }
    if (name == "rails")
    {
        kind = JBeamAdvancedSectionKind::RAILS;
        return true;
    }
    if (name == "rails2")
    {
        kind = JBeamAdvancedSectionKind::RAILS2;
        return true;
    }
    if (name == "slidenodes")
    {
        kind = JBeamAdvancedSectionKind::SLIDENODES;
        return true;
    }
    if (name == "thrusters")
    {
        kind = JBeamAdvancedSectionKind::THRUSTERS;
        return true;
    }
    if (name == "torsionbars")
    {
        kind = JBeamAdvancedSectionKind::TORSIONBARS;
        return true;
    }
    return false;
}

struct MeasuredValue
{
    std::size_t work;
    std::size_t bytes;
    bool valid;

    MeasuredValue() : work(0U), bytes(0U), valid(true) {}
};

bool MeasureValueRecursive(
    const JBeamValue& value,
    std::size_t depth,
    std::size_t max_depth,
    std::size_t max_work,
    std::set<const JBeamValue*>& stack,
    MeasuredValue& measured)
{
    if (depth > max_depth ||
        !AddSize(1U, measured.work) ||
        measured.work > max_work ||
        !stack.insert(&value).second)
    {
        measured.valid = false;
        return false;
    }
    if (!AddSize(sizeof(JBeamValue), measured.bytes) ||
        !AddSize(value.scalar_text.size(), measured.bytes) ||
        !AddSize(value.span.source_name.size(), measured.bytes))
    {
        measured.valid = false;
        stack.erase(&value);
        return false;
    }
    if (value.type == JBeamValueType::ARRAY)
    {
        for (std::size_t i = 0U; i < value.array_values.size(); ++i)
        {
            if (!MeasureValueRecursive(
                    value.array_values[i],
                    depth + 1U,
                    max_depth,
                    max_work,
                    stack,
                    measured))
            {
                stack.erase(&value);
                return false;
            }
        }
    }
    else if (value.type == JBeamValueType::OBJECT)
    {
        for (std::size_t i = 0U; i < value.object_fields.size(); ++i)
        {
            const JBeamObjectField& field = value.object_fields[i];
            if (!AddSize(
                    sizeof(JBeamObjectField) +
                        field.key.size() +
                        field.key_span.source_name.size(),
                    measured.bytes))
            {
                measured.valid = false;
                stack.erase(&value);
                return false;
            }
            if (field.value &&
                !MeasureValueRecursive(
                    *field.value,
                    depth + 1U,
                    max_depth,
                    max_work,
                    stack,
                    measured))
            {
                stack.erase(&value);
                return false;
            }
        }
    }
    stack.erase(&value);
    return true;
}

MeasuredValue MeasureValue(
    const JBeamValue& value,
    std::size_t max_depth,
    std::size_t max_work)
{
    MeasuredValue measured;
    std::set<const JBeamValue*> stack;
    MeasureValueRecursive(
        value,
        1U,
        max_depth,
        max_work,
        stack,
        measured);
    return measured;
}

std::shared_ptr<JBeamValue> CloneValueRecursive(
    const JBeamValue& source,
    std::map<
        const JBeamValue*,
        std::shared_ptr<JBeamValue> >& cloned)
{
    const std::map<
        const JBeamValue*,
        std::shared_ptr<JBeamValue> >::const_iterator found =
            cloned.find(&source);
    if (found != cloned.end())
    {
        return found->second;
    }
    std::shared_ptr<JBeamValue> result(new JBeamValue());
    cloned[&source] = result;
    result->type = source.type;
    result->span = source.span;
    result->boolean_value = source.boolean_value;
    result->number_value = source.number_value;
    result->scalar_text = source.scalar_text;
    result->array_values.reserve(source.array_values.size());
    for (std::size_t i = 0U; i < source.array_values.size(); ++i)
    {
        const std::shared_ptr<JBeamValue> child =
            CloneValueRecursive(source.array_values[i], cloned);
        result->array_values.push_back(*child);
    }
    result->object_fields.reserve(source.object_fields.size());
    for (std::size_t i = 0U; i < source.object_fields.size(); ++i)
    {
        const JBeamObjectField& source_field =
            source.object_fields[i];
        JBeamObjectField field;
        field.key = source_field.key;
        field.key_span = source_field.key_span;
        if (source_field.value)
        {
            field.value =
                CloneValueRecursive(*source_field.value, cloned);
        }
        result->object_fields.push_back(field);
    }
    return result;
}

std::shared_ptr<const JBeamValue> CloneValue(const JBeamValue& source)
{
    std::map<const JBeamValue*, std::shared_ptr<JBeamValue> > cloned;
    return CloneValueRecursive(source, cloned);
}

struct AssignmentView
{
    JBeamAdvancedFieldOrigin origin;
    const JBeamValue* value;
    JBeamSourceSpan span;

    AssignmentView()
        : origin(JBeamAdvancedFieldOrigin::INHERITED_DEFAULT)
        , value(NULL)
    {
    }
};

struct Vec3
{
    double x;
    double y;
    double z;
};

struct NodeInfo
{
    std::size_t count;
    bool has_coordinates;
    Vec3 coordinates;

    NodeInfo() : count(0U), has_coordinates(false)
    {
        coordinates.x = 0.0;
        coordinates.y = 0.0;
        coordinates.z = 0.0;
    }
};

bool CoordinatesEqual(const Vec3& left, const Vec3& right)
{
    return left.x == right.x &&
        left.y == right.y &&
        left.z == right.z;
}

bool NormalizedDirection(
    const Vec3& from,
    const Vec3& to,
    Vec3& direction)
{
    volatile double dx = to.x - from.x;
    volatile double dy = to.y - from.y;
    volatile double dz = to.z - from.z;
    if (!IsFiniteDouble(dx) ||
        !IsFiniteDouble(dy) ||
        !IsFiniteDouble(dz))
    {
        return false;
    }
    const double ax = std::fabs(dx);
    const double ay = std::fabs(dy);
    const double az = std::fabs(dz);
    const double maximum = std::max(ax, std::max(ay, az));
    if (!(maximum > 0.0) || !IsFiniteDouble(maximum))
    {
        return false;
    }
    int exponent = 0;
    std::frexp(maximum, &exponent);
    direction.x = std::ldexp(dx, -exponent);
    direction.y = std::ldexp(dy, -exponent);
    direction.z = std::ldexp(dz, -exponent);
    return IsFiniteDouble(direction.x) &&
        IsFiniteDouble(direction.y) &&
        IsFiniteDouble(direction.z);
}

bool DirectionsCollinear(const Vec3& left, const Vec3& right)
{
    volatile double cx =
        left.y * right.z - left.z * right.y;
    volatile double cy =
        left.z * right.x - left.x * right.z;
    volatile double cz =
        left.x * right.y - left.y * right.x;
    return cx == 0.0 && cy == 0.0 && cz == 0.0;
}

class AdvancedBuilder
{
public:
    AdvancedBuilder(
        const JBeamResolvedGraph& graph,
        const JBeamAdvancedLimits& requested_limits)
        : m_graph(graph)
        , m_limits(SanitizeLimits(requested_limits))
        , m_retained_bytes(0U)
        , m_work(0U)
        , m_value_work(0U)
        , m_effective_fields(0U)
        , m_authored_entries(0U)
        , m_node_rows(0U)
        , m_resource_limit(false)
        , m_diagnostic_limit_emitted(false)
        , m_unused_bool(false)
    {
        m_result.documentation_profile_id =
            GetJBeamAdvancedDocumentationProfile().profile_id;
        m_result.canonical_output_byte_limit =
            m_limits.max_canonical_output_bytes;
        m_result.canonical_work_unit_limit =
            m_limits.max_canonical_work_units;
        m_result.canonical_value_depth_limit =
            m_limits.max_preserved_value_depth;
    }

    JBeamAdvancedStructureIR Run()
    {
        if (!m_graph.IsValid() || !m_graph.root)
        {
            Push(
                JBeamAdvancedDiagnosticCode::INVALID_RESOLVED_GRAPH,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Advanced structure inventory requires a valid "
                "resolved graph");
            Finish();
            return m_result;
        }

        std::set<const JBeamResolvedPartNode*> stack;
        Traverse(m_graph.root, 1U, stack);
        if (m_resource_limit)
        {
            Finish();
            return m_result;
        }

        CollectNodes();
        CollectSourceRecords();
        if (m_resource_limit)
        {
            Finish();
            return m_result;
        }

        // Static rail names must be known before slidenode references are
        // classified, regardless of source-section ordering.
        for (std::size_t i = 0U;
             i < m_result.source_records.size() && !m_resource_limit;
             ++i)
        {
            if (m_result.source_records[i].kind ==
                    JBeamAdvancedSectionKind::RAILS)
            {
                ParseLegacyRails(i);
            }
            else if (m_result.source_records[i].kind ==
                    JBeamAdvancedSectionKind::RAILS2)
            {
                ParseTableSection(i);
            }
        }
        ClassifyDuplicateRailNames();

        for (std::size_t i = 0U;
             i < m_result.source_records.size() && !m_resource_limit;
             ++i)
        {
            const JBeamAdvancedSectionKind kind =
                m_result.source_records[i].kind;
            if (kind != JBeamAdvancedSectionKind::RAILS &&
                kind != JBeamAdvancedSectionKind::RAILS2)
            {
                ParseTableSection(i);
            }
        }
        Finish();
        return m_result;
    }

private:
    struct PartView
    {
        const JBeamResolvedPartNode* node;
        std::shared_ptr<const JBeamAdvancedPartIdentity> identity;
    };

    struct RecordMeta
    {
        bool duplicate_section;
        RecordMeta() : duplicate_section(false) {}
    };

    JBeamAdvancedProvenance Provenance(
        const std::shared_ptr<const JBeamAdvancedPartIdentity>& part,
        const JBeamSourceSpan& span) const
    {
        JBeamAdvancedProvenance provenance;
        provenance.part = part;
        provenance.span = span;
        return provenance;
    }

    bool Charge(std::size_t bytes)
    {
        if (!AddSize(bytes, m_retained_bytes) ||
            m_retained_bytes > m_limits.max_retained_bytes)
        {
            Push(
                JBeamAdvancedDiagnosticCode::RETAINED_BYTE_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Advanced structure retained-byte limit exceeded");
            m_resource_limit = true;
            return false;
        }
        return true;
    }

    bool Work(
        std::size_t units,
        const JBeamAdvancedProvenance& provenance =
            JBeamAdvancedProvenance(),
        JBeamAdvancedSectionKind kind =
            JBeamAdvancedSectionKind::HYDROS,
        std::size_t record_index = 0U,
        std::size_t entry_index = 0U)
    {
        if (!AddSize(units, m_work) ||
            m_work > m_limits.max_work_units)
        {
            Push(
                JBeamAdvancedDiagnosticCode::WORK_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                provenance,
                kind,
                record_index,
                entry_index,
                std::string(),
                "Advanced structure work-unit limit exceeded");
            m_resource_limit = true;
            return false;
        }
        return true;
    }

    void Push(
        JBeamAdvancedDiagnosticCode code,
        JBeamAdvancedSeverity severity,
        const JBeamAdvancedProvenance& provenance,
        JBeamAdvancedSectionKind kind,
        std::size_t record_index,
        std::size_t entry_index,
        const std::string& field,
        const std::string& detail)
    {
        if (m_diagnostic_limit_emitted)
        {
            return;
        }
        if (m_result.diagnostics.size() >= m_limits.max_diagnostics)
        {
            if (!m_result.diagnostics.empty())
            {
                m_result.diagnostics.pop_back();
            }
            JBeamAdvancedDiagnostic terminal;
            terminal.code =
                JBeamAdvancedDiagnosticCode::DIAGNOSTIC_LIMIT;
            terminal.severity = JBeamAdvancedSeverity::ERROR_SEVERITY;
            terminal.provenance = provenance;
            terminal.section_kind = kind;
            terminal.source_record_index = record_index;
            terminal.entry_index = entry_index;
            terminal.detail =
                "Advanced structure diagnostic limit exceeded";
            m_result.diagnostics.push_back(terminal);
            m_diagnostic_limit_emitted = true;
            m_resource_limit = true;
            return;
        }
        JBeamAdvancedDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.provenance = provenance;
        diagnostic.section_kind = kind;
        diagnostic.source_record_index = record_index;
        diagnostic.entry_index = entry_index;
        diagnostic.field_name = field;
        diagnostic.detail = detail;
        m_result.diagnostics.push_back(diagnostic);
    }

    void Traverse(
        const std::shared_ptr<JBeamResolvedPartNode>& node,
        std::size_t depth,
        std::set<const JBeamResolvedPartNode*>& stack)
    {
        if (!node || m_resource_limit)
        {
            return;
        }
        if (!Work(1U))
        {
            return;
        }
        if (depth > m_limits.max_graph_depth)
        {
            Push(
                JBeamAdvancedDiagnosticCode::
                    RESOLVED_GRAPH_DEPTH_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Resolved graph depth limit exceeded");
            m_resource_limit = true;
            return;
        }
        if (!stack.insert(node.get()).second)
        {
            Push(
                JBeamAdvancedDiagnosticCode::RESOLVED_GRAPH_CYCLE,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Resolved graph contains a cycle");
            m_resource_limit = true;
            return;
        }
        if (m_parts.size() >= m_limits.max_parts)
        {
            Push(
                JBeamAdvancedDiagnosticCode::RESOLVED_PART_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Resolved part limit exceeded");
            m_resource_limit = true;
            stack.erase(node.get());
            return;
        }
        std::shared_ptr<JBeamAdvancedPartIdentity> identity(
            new JBeamAdvancedPartIdentity());
        identity->part_preorder_index = m_parts.size();
        identity->part_name = node->definition.name;
        identity->package_path = node->definition.package_path;
        if (!Charge(
                sizeof(JBeamAdvancedPartIdentity) +
                identity->part_name.size() +
                identity->package_path.size()))
        {
            stack.erase(node.get());
            return;
        }
        PartView view;
        view.node = node.get();
        view.identity = identity;
        m_parts.push_back(view);
        m_result.parts.push_back(identity);
        for (std::size_t i = 0U; i < node->slots.size(); ++i)
        {
            if (node->slots[i].child)
            {
                Traverse(node->slots[i].child, depth + 1U, stack);
            }
        }
        stack.erase(node.get());
    }

    void CollectNodes()
    {
        for (std::size_t part_index = 0U;
             part_index < m_parts.size() && !m_resource_limit;
             ++part_index)
        {
            const JBeamValue& body =
                m_parts[part_index].node->definition.body;
            if (!Work(1U))
            {
                return;
            }
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
                if (!Work(1U))
                {
                    return;
                }
                if (field.key != "nodes" || !field.value)
                {
                    continue;
                }
                CollectNodeTable(*field.value);
            }
        }
    }

    void CollectNodeTable(const JBeamValue& value)
    {
        if (value.type != JBeamValueType::ARRAY ||
            value.array_values.empty() ||
            value.array_values[0].type != JBeamValueType::ARRAY)
        {
            return;
        }
        const JBeamValue& header = value.array_values[0];
        std::map<std::string, std::size_t> columns;
        for (std::size_t i = 0U; i < header.array_values.size(); ++i)
        {
            if (!Work(1U))
            {
                return;
            }
            if (header.array_values[i].type != JBeamValueType::STRING)
            {
                return;
            }
            columns[header.array_values[i].scalar_text] = i;
        }
        if (columns.count("id") == 0U ||
            columns.count("posX") == 0U ||
            columns.count("posY") == 0U ||
            columns.count("posZ") == 0U)
        {
            return;
        }
        std::map<std::string, AssignmentView> inherited;
        for (std::size_t entry_index = 1U;
             entry_index < value.array_values.size();
             ++entry_index)
        {
            const JBeamValue& entry = value.array_values[entry_index];
            if (!Work(1U))
            {
                return;
            }
            if (entry.type == JBeamValueType::OBJECT)
            {
                if (!Work(entry.object_fields.size()))
                {
                    return;
                }
                ApplyObject(
                    entry,
                    JBeamAdvancedFieldOrigin::INHERITED_DEFAULT,
                    inherited);
                continue;
            }
            if (entry.type != JBeamValueType::ARRAY)
            {
                continue;
            }
            if (!Work(
                    header.array_values.size() +
                    entry.array_values.size()))
            {
                return;
            }
            std::map<std::string, AssignmentView> effective = inherited;
            ApplyRow(header, entry, effective);
            const AssignmentView* id = Find(effective, "id");
            if (id == NULL || id->value == NULL ||
                id->value->type != JBeamValueType::STRING ||
                IsExpression(*id->value) ||
                id->value->scalar_text.empty())
            {
                continue;
            }
            if (!AddSize(1U, m_node_rows) ||
                m_node_rows > m_limits.max_node_coordinates)
            {
                Push(
                    JBeamAdvancedDiagnosticCode::NODE_COORDINATE_LIMIT,
                    JBeamAdvancedSeverity::ERROR_SEVERITY,
                    JBeamAdvancedProvenance(),
                    JBeamAdvancedSectionKind::HYDROS,
                    0U,
                    entry_index - 1U,
                    "nodes",
                    "Node coordinate inventory limit exceeded");
                m_resource_limit = true;
                return;
            }
            const std::string& node_name = id->value->scalar_text;
            std::map<std::string, NodeInfo>::iterator node_it =
                m_nodes.find(node_name);
            if (node_it == m_nodes.end())
            {
                if (!Charge(sizeof(NodeInfo) + node_name.size()))
                {
                    return;
                }
                node_it =
                    m_nodes.insert(
                        std::make_pair(node_name, NodeInfo())).first;
            }
            NodeInfo& node = node_it->second;
            ++node.count;
            const AssignmentView* x = Find(effective, "posX");
            const AssignmentView* y = Find(effective, "posY");
            const AssignmentView* z = Find(effective, "posZ");
            if (node.count == 1U &&
                LiteralFinite(x) &&
                LiteralFinite(y) &&
                LiteralFinite(z))
            {
                node.has_coordinates = true;
                node.coordinates.x = x->value->number_value;
                node.coordinates.y = y->value->number_value;
                node.coordinates.z = z->value->number_value;
            }
            else
            {
                node.has_coordinates = false;
            }
        }
    }

    static bool LiteralFinite(const AssignmentView* assignment)
    {
        return assignment != NULL &&
            assignment->value != NULL &&
            assignment->value->type == JBeamValueType::NUMBER &&
            IsFiniteDouble(assignment->value->number_value);
    }

    void CollectSourceRecords()
    {
        for (std::size_t part_index = 0U;
             part_index < m_parts.size() && !m_resource_limit;
             ++part_index)
        {
            const PartView& part = m_parts[part_index];
            const JBeamValue& body = part.node->definition.body;
            if (!Work(1U))
            {
                return;
            }
            if (body.type != JBeamValueType::OBJECT)
            {
                Push(
                    JBeamAdvancedDiagnosticCode::PART_BODY_NOT_OBJECT,
                    JBeamAdvancedSeverity::ERROR_SEVERITY,
                    Provenance(part.identity, body.span),
                    JBeamAdvancedSectionKind::HYDROS,
                    0U,
                    0U,
                    std::string(),
                    "Resolved part body is not an object");
                continue;
            }
            std::map<JBeamAdvancedSectionKind, std::size_t> counts;
            for (std::size_t field_index = 0U;
                 field_index < body.object_fields.size();
                 ++field_index)
            {
                if (!Work(1U))
                {
                    return;
                }
                JBeamAdvancedSectionKind kind =
                    JBeamAdvancedSectionKind::HYDROS;
                if (IsTargetSection(
                        body.object_fields[field_index].key,
                        kind))
                {
                    ++counts[kind];
                }
            }
            std::map<JBeamAdvancedSectionKind, std::size_t> occurrences;
            for (std::size_t field_index = 0U;
                 field_index < body.object_fields.size() &&
                    !m_resource_limit;
                 ++field_index)
            {
                const JBeamObjectField& field =
                    body.object_fields[field_index];
                if (!Work(1U))
                {
                    return;
                }
                JBeamAdvancedSectionKind kind =
                    JBeamAdvancedSectionKind::HYDROS;
                if (!IsTargetSection(field.key, kind))
                {
                    continue;
                }
                if (!field.value)
                {
                    Push(
                        JBeamAdvancedDiagnosticCode::INVALID_SECTION,
                        JBeamAdvancedSeverity::ERROR_SEVERITY,
                        Provenance(part.identity, field.key_span),
                        kind,
                        m_result.source_records.size(),
                        0U,
                        std::string(),
                        "Advanced section has no value");
                    continue;
                }
                if (m_result.source_records.size() >=
                    m_limits.max_source_records)
                {
                    Push(
                        JBeamAdvancedDiagnosticCode::SOURCE_RECORD_LIMIT,
                        JBeamAdvancedSeverity::ERROR_SEVERITY,
                        Provenance(part.identity, field.key_span),
                        kind,
                        m_result.source_records.size(),
                        0U,
                        std::string(),
                        "Advanced structure source-record limit "
                        "exceeded");
                    m_resource_limit = true;
                    return;
                }
                if (!RetainValue(*field.value))
                {
                    return;
                }
                JBeamAdvancedSourceRecord record;
                record.kind = kind;
                record.section_occurrence = occurrences[kind]++;
                record.provenance =
                    Provenance(part.identity, field.value->span);
                record.raw_value = CloneValue(*field.value);
                m_result.source_records.push_back(record);
                if (!Charge(
                        sizeof(JBeamAdvancedSourceRecord) +
                        record.provenance.span.source_name.size()))
                {
                    return;
                }
                RecordMeta meta;
                meta.duplicate_section = counts[kind] > 1U;
                m_record_meta.push_back(meta);
                if (meta.duplicate_section)
                {
                    Push(
                        JBeamAdvancedDiagnosticCode::DUPLICATE_SECTION,
                        JBeamAdvancedSeverity::ERROR_SEVERITY,
                        record.provenance,
                        kind,
                        m_result.source_records.size() - 1U,
                        0U,
                        std::string(),
                        "Duplicate advanced section in one resolved "
                        "part is preserved but rejected");
                }
            }
        }
    }

    bool RetainValue(const JBeamValue& value)
    {
        const std::size_t remaining_work =
            m_limits.max_preserved_value_work_units -
            std::min(
                m_value_work,
                m_limits.max_preserved_value_work_units);
        const MeasuredValue measured = MeasureValue(
            value,
            m_limits.max_preserved_value_depth,
            remaining_work);
        if (!measured.valid ||
            !AddSize(measured.work, m_value_work) ||
            m_value_work >
                m_limits.max_preserved_value_work_units)
        {
            Push(
                JBeamAdvancedDiagnosticCode::PRESERVED_VALUE_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                JBeamAdvancedProvenance(),
                JBeamAdvancedSectionKind::HYDROS,
                0U,
                0U,
                std::string(),
                "Preserved advanced value exceeds work/depth limit "
                "or contains a cycle");
            m_resource_limit = true;
            return false;
        }
        if (!Work(measured.work))
        {
            return false;
        }
        return Charge(measured.bytes);
    }

    static const AssignmentView* Find(
        const std::map<std::string, AssignmentView>& assignments,
        const std::string& name)
    {
        const std::map<std::string, AssignmentView>::const_iterator found =
            assignments.find(name);
        return found == assignments.end() ? NULL : &found->second;
    }

    static void ApplyObject(
        const JBeamValue& object,
        JBeamAdvancedFieldOrigin origin,
        std::map<std::string, AssignmentView>& assignments)
    {
        if (object.type != JBeamValueType::OBJECT)
        {
            return;
        }
        for (std::size_t i = 0U; i < object.object_fields.size(); ++i)
        {
            const JBeamObjectField& field = object.object_fields[i];
            AssignmentView assignment;
            assignment.origin = origin;
            assignment.value = field.value.get();
            assignment.span =
                field.value ? field.value->span : field.key_span;
            assignments[field.key] = assignment;
        }
    }

    static void ApplyRow(
        const JBeamValue& header,
        const JBeamValue& row,
        std::map<std::string, AssignmentView>& assignments)
    {
        const std::size_t positional_count =
            std::min(header.array_values.size(), row.array_values.size());
        for (std::size_t i = 0U; i < positional_count; ++i)
        {
            if (header.array_values[i].type !=
                JBeamValueType::STRING)
            {
                continue;
            }
            AssignmentView assignment;
            assignment.origin =
                JBeamAdvancedFieldOrigin::POSITIONAL_CELL;
            assignment.value = &row.array_values[i];
            assignment.span = row.array_values[i].span;
            assignments[header.array_values[i].scalar_text] = assignment;
        }
        if (row.array_values.size() > header.array_values.size() &&
            row.array_values.back().type == JBeamValueType::OBJECT)
        {
            ApplyObject(
                row.array_values.back(),
                JBeamAdvancedFieldOrigin::ROW_LOCAL_OVERRIDE,
                assignments);
        }
    }

    bool ValidateHeader(
        std::size_t record_index,
        const JBeamValue& section,
        const JBeamValue*& header)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        if (section.type != JBeamValueType::ARRAY ||
            section.array_values.empty() ||
            section.array_values[0].type != JBeamValueType::ARRAY ||
            section.array_values[0].array_values.empty())
        {
            Push(
                JBeamAdvancedDiagnosticCode::INVALID_SECTION,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                record.provenance,
                record.kind,
                record_index,
                0U,
                std::string(),
                "Advanced table section must start with a non-empty "
                "string header row");
            return false;
        }
        header = &section.array_values[0];
        std::set<std::string> names;
        bool valid = true;
        for (std::size_t i = 0U; i < header->array_values.size(); ++i)
        {
            if (!Work(
                    1U,
                    record.provenance,
                    record.kind,
                    record_index,
                    0U))
            {
                return false;
            }
            const JBeamValue& cell = header->array_values[i];
            if (cell.type != JBeamValueType::STRING ||
                cell.scalar_text.empty())
            {
                Push(
                    JBeamAdvancedDiagnosticCode::INVALID_TABLE_HEADER,
                    JBeamAdvancedSeverity::ERROR_SEVERITY,
                    Provenance(record.provenance.part, cell.span),
                    record.kind,
                    record_index,
                    0U,
                    std::string(),
                    "Advanced table header cells must be non-empty "
                    "strings");
                valid = false;
            }
            else if (!names.insert(cell.scalar_text).second)
            {
                Push(
                    JBeamAdvancedDiagnosticCode::
                        DUPLICATE_TABLE_HEADER,
                    JBeamAdvancedSeverity::ERROR_SEVERITY,
                    Provenance(record.provenance.part, cell.span),
                    record.kind,
                    record_index,
                    0U,
                    cell.scalar_text,
                    "Duplicate advanced table header is ambiguous");
                valid = false;
            }
        }
        return valid;
    }

    bool CountAuthoredEntry(
        const JBeamAdvancedSourceRecord& record,
        std::size_t record_index,
        std::size_t entry_index)
    {
        if (!AddSize(1U, m_authored_entries) ||
            m_authored_entries > m_limits.max_entries)
        {
            Push(
                JBeamAdvancedDiagnosticCode::ENTRY_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                record.provenance,
                record.kind,
                record_index,
                entry_index,
                std::string(),
                "Advanced structure authored-entry limit exceeded");
            m_resource_limit = true;
            return false;
        }
        return true;
    }

    void AddModifier(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& object)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        if (m_result.modifiers.size() >= m_limits.max_modifiers)
        {
            Push(
                JBeamAdvancedDiagnosticCode::MODIFIER_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                Provenance(record.provenance.part, object.span),
                record.kind,
                record_index,
                entry_index,
                std::string(),
                "Advanced structure modifier limit exceeded");
            m_resource_limit = true;
            return;
        }
        JBeamAdvancedModifier modifier;
        modifier.section_kind = record.kind;
        modifier.source_record_index = record_index;
        modifier.entry_index = entry_index;
        modifier.provenance =
            Provenance(record.provenance.part, object.span);
        modifier.raw_value = Alias(record.raw_value, object);
        m_result.modifiers.push_back(modifier);
        Charge(
            sizeof(JBeamAdvancedModifier) +
            modifier.provenance.span.source_name.size());
    }

    static std::shared_ptr<const JBeamValue> Alias(
        const std::shared_ptr<const JBeamValue>& owner,
        const JBeamValue& value)
    {
        return std::shared_ptr<const JBeamValue>(owner, &value);
    }

    JBeamAdvancedEntry MakeEntry(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        JBeamAdvancedEntry result;
        result.source_record_index = record_index;
        result.source_entry_index = entry_index;
        result.provenance =
            Provenance(record.provenance.part, row.span);
        result.raw_value = Alias(record.raw_value, row);
        if (!AddSize(effective.size(), m_effective_fields) ||
            m_effective_fields > m_limits.max_effective_fields)
        {
            Push(
                JBeamAdvancedDiagnosticCode::EFFECTIVE_FIELD_LIMIT,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                result.provenance,
                record.kind,
                record_index,
                entry_index,
                std::string(),
                "Advanced structure effective-field limit exceeded");
            m_resource_limit = true;
            return result;
        }
        if (!Work(
                effective.size() + 1U,
                result.provenance,
                record.kind,
                record_index,
                entry_index) ||
            !Charge(
                sizeof(JBeamAdvancedEntry) +
                result.provenance.span.source_name.size()))
        {
            return result;
        }
        for (std::map<std::string, AssignmentView>::const_iterator it =
                 effective.begin();
             it != effective.end();
             ++it)
        {
            JBeamAdvancedField field;
            field.name = it->first;
            field.origin = it->second.origin;
            field.provenance =
                Provenance(record.provenance.part, it->second.span);
            if (it->second.value)
            {
                field.raw_value =
                    Alias(record.raw_value, *it->second.value);
            }
            result.effective_fields.push_back(field);
            if (!Charge(
                    sizeof(JBeamAdvancedField) +
                    field.name.size() +
                    field.provenance.span.source_name.size()))
            {
                return result;
            }
        }
        return result;
    }

    void RejectMalformedEntry(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& value,
        const std::string& detail)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        JBeamAdvancedRejectedEntry rejected;
        rejected.section_kind = record.kind;
        rejected.entry.source_record_index = record_index;
        rejected.entry.source_entry_index = entry_index;
        rejected.entry.behavior =
            JBeamAdvancedBehavior::REJECTED_INVALID;
        rejected.entry.provenance =
            Provenance(record.provenance.part, value.span);
        rejected.entry.raw_value = Alias(record.raw_value, value);
        if (!Charge(sizeof(JBeamAdvancedRejectedEntry)))
        {
            return;
        }
        m_result.rejected_entries.push_back(rejected);
        Push(
            JBeamAdvancedDiagnosticCode::INVALID_TABLE_ENTRY,
            JBeamAdvancedSeverity::ERROR_SEVERITY,
            rejected.entry.provenance,
            record.kind,
            record_index,
            entry_index,
            std::string(),
            detail);
    }

    void ParseTableSection(std::size_t record_index)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamValue& section = *record.raw_value;
        const JBeamValue* header = NULL;
        if (!ValidateHeader(record_index, section, header))
        {
            return;
        }
        std::map<std::string, AssignmentView> inherited;
        for (std::size_t source_index = 1U;
             source_index < section.array_values.size() &&
                 !m_resource_limit;
             ++source_index)
        {
            const std::size_t entry_index = source_index - 1U;
            const JBeamValue& value = section.array_values[source_index];
            if (!Work(
                    1U,
                    record.provenance,
                    record.kind,
                    record_index,
                    entry_index))
            {
                return;
            }
            if (!CountAuthoredEntry(record, record_index, entry_index))
            {
                return;
            }
            if (value.type == JBeamValueType::OBJECT)
            {
                if (!Work(
                        value.object_fields.size(),
                        record.provenance,
                        record.kind,
                        record_index,
                        entry_index))
                {
                    return;
                }
                AddModifier(record_index, entry_index, value);
                ApplyObject(
                    value,
                    JBeamAdvancedFieldOrigin::INHERITED_DEFAULT,
                    inherited);
                continue;
            }
            if (value.type != JBeamValueType::ARRAY)
            {
                RejectMalformedEntry(
                    record_index,
                    entry_index,
                    value,
                    "Advanced table entry has invalid shape");
                continue;
            }
            std::size_t positional_count = value.array_values.size();
            if (positional_count > header->array_values.size() &&
                value.array_values.back().type ==
                    JBeamValueType::OBJECT)
            {
                --positional_count;
            }
            if (positional_count > header->array_values.size())
            {
                Push(
                    JBeamAdvancedDiagnosticCode::
                        EXTRA_POSITIONAL_VALUE_PRESERVED,
                    JBeamAdvancedSeverity::WARNING,
                    Provenance(record.provenance.part, value.span),
                    record.kind,
                    record_index,
                    entry_index,
                    std::string(),
                    "Extra advanced table cells are retained in the "
                    "raw row but have no header semantics");
            }
            std::map<std::string, AssignmentView> effective = inherited;
            if (!Work(
                    header->array_values.size() +
                    value.array_values.size(),
                    record.provenance,
                    record.kind,
                    record_index,
                    entry_index))
            {
                return;
            }
            ApplyRow(*header, value, effective);
            switch (record.kind)
            {
            case JBeamAdvancedSectionKind::HYDROS:
                ParseHydro(record_index, entry_index, value, effective);
                break;
            case JBeamAdvancedSectionKind::RAILS2:
                ParseRail2(record_index, entry_index, value, effective);
                break;
            case JBeamAdvancedSectionKind::SLIDENODES:
                ParseSlideNode(
                    record_index,
                    entry_index,
                    value,
                    effective);
                break;
            case JBeamAdvancedSectionKind::THRUSTERS:
                ParseThruster(
                    record_index,
                    entry_index,
                    value,
                    effective);
                break;
            case JBeamAdvancedSectionKind::TORSIONBARS:
                ParseTorsionBar(
                    record_index,
                    entry_index,
                    value,
                    effective);
                break;
            case JBeamAdvancedSectionKind::RAILS:
                break;
            }
        }
    }

    void ParseLegacyRails(std::size_t record_index)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamValue& section = *record.raw_value;
        if (section.type != JBeamValueType::OBJECT)
        {
            Push(
                JBeamAdvancedDiagnosticCode::INVALID_SECTION,
                JBeamAdvancedSeverity::ERROR_SEVERITY,
                record.provenance,
                record.kind,
                record_index,
                0U,
                std::string(),
                "Legacy rails section must be an object");
            return;
        }
        for (std::size_t i = 0U;
             i < section.object_fields.size() && !m_resource_limit;
             ++i)
        {
            const JBeamObjectField& field = section.object_fields[i];
            if (!Work(
                    1U,
                    record.provenance,
                    record.kind,
                    record_index,
                    i))
            {
                return;
            }
            if (!CountAuthoredEntry(record, record_index, i))
            {
                return;
            }
            if (!field.value ||
                field.value->type != JBeamValueType::OBJECT)
            {
                const JBeamValue& raw =
                    field.value ? *field.value : section;
                RejectMalformedEntry(
                    record_index,
                    i,
                    raw,
                    "Legacy rail definition must be an object");
                continue;
            }
            std::map<std::string, AssignmentView> effective;
            if (!Work(
                    field.value->object_fields.size(),
                    record.provenance,
                    record.kind,
                    record_index,
                    i))
            {
                return;
            }
            ApplyObject(
                *field.value,
                JBeamAdvancedFieldOrigin::ROW_LOCAL_OVERRIDE,
                effective);
            JBeamAdvancedRail rail;
            rail.entry = MakeEntry(
                record_index,
                i,
                *field.value,
                effective);
            rail.name = field.key;
            bool inert = !rail.name.empty() && rail.name[0] == '$';
            bool valid = !m_record_meta[record_index].duplicate_section;
            if (rail.name.empty())
            {
                Error(
                    rail.entry,
                    record.kind,
                    JBeamAdvancedDiagnosticCode::INVALID_RAIL_NAME,
                    "id",
                    "Rail name must be a non-empty literal string");
                valid = false;
            }
            const AssignmentView* links = Find(effective, "links:");
            ParseLinks(
                rail.entry,
                record.kind,
                links,
                rail.links,
                inert,
                valid);
            ParseRailOptions(
                rail.entry,
                record.kind,
                effective,
                rail,
                inert,
                valid);
            ValidateUnknownFields(
                rail.entry,
                record.kind,
                effective,
                RailFields());
            ValidateRailGeometry(rail, valid);
            rail.entry.behavior = Behavior(
                valid,
                inert,
                JBeamAdvancedBehavior::
                    NATIVE_READY_STATIC_GEOMETRY);
            if (!ChargeRail(rail))
            {
                return;
            }
            m_result.rails.push_back(rail);
            RegisterRailName(m_result.rails.size() - 1U);
        }
    }

    void ParseRail2(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        JBeamAdvancedRail rail;
        rail.entry = MakeEntry(
            record_index,
            entry_index,
            row,
            effective);
        bool inert = false;
        bool valid = !m_record_meta[record_index].duplicate_section;
        ReadString(
            rail.entry,
            record.kind,
            effective,
            "id",
            true,
            rail.name,
            inert,
            valid);
        if (rail.name.empty() && !inert)
        {
            valid = false;
        }
        ParseLinks(
            rail.entry,
            record.kind,
            Find(effective, "links:"),
            rail.links,
            inert,
            valid);
        ParseRailOptions(
            rail.entry,
            record.kind,
            effective,
            rail,
            inert,
            valid);
        ValidateUnknownFields(
            rail.entry,
            record.kind,
            effective,
            RailFields());
        ValidateRailGeometry(rail, valid);
        rail.entry.behavior = Behavior(
            valid,
            inert,
            JBeamAdvancedBehavior::NATIVE_READY_STATIC_GEOMETRY);
        if (!ChargeRail(rail))
        {
            return;
        }
        m_result.rails.push_back(rail);
        RegisterRailName(m_result.rails.size() - 1U);
    }

    void RegisterRailName(std::size_t rail_index)
    {
        const JBeamAdvancedRail& rail = m_result.rails[rail_index];
        if (!rail.name.empty() &&
            rail.entry.behavior !=
                JBeamAdvancedBehavior::
                    PRESERVED_DISABLED_INERT_EXPRESSION)
        {
            m_rail_names[rail.name].push_back(rail_index);
        }
    }

    void ClassifyDuplicateRailNames()
    {
        for (std::map<
                 std::string,
                 std::vector<std::size_t> >::const_iterator it =
                 m_rail_names.begin();
             it != m_rail_names.end();
             ++it)
        {
            if (it->second.size() < 2U)
            {
                continue;
            }
            for (std::size_t i = 0U; i < it->second.size(); ++i)
            {
                JBeamAdvancedRail& rail =
                    m_result.rails[it->second[i]];
                rail.entry.behavior =
                    JBeamAdvancedBehavior::REJECTED_INVALID;
                Error(
                    rail.entry,
                    m_result.source_records[
                        rail.entry.source_record_index].kind,
                    JBeamAdvancedDiagnosticCode::
                        DUPLICATE_RAIL_NAME,
                    "id",
                    "Rail name is not unique in the resolved graph");
            }
        }
    }

    static std::set<std::string> RailFields()
    {
        std::set<std::string> result;
        result.insert("id");
        result.insert("links:");
        result.insert("broken:");
        result.insert("looped");
        result.insert("capped");
        return result;
    }

    void ParseRailOptions(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        JBeamAdvancedRail& rail,
        bool& inert,
        bool& valid)
    {
        const JBeamAdvancedDocumentationProfile& profile =
            GetJBeamAdvancedDocumentationProfile();
        rail.looped = profile.rail_looped;
        rail.capped = profile.rail_capped;
        ReadBoolean(
            entry,
            kind,
            effective,
            "looped",
            false,
            profile.rail_looped,
            rail.looped,
            inert,
            valid);
        ReadBoolean(
            entry,
            kind,
            effective,
            "capped",
            false,
            profile.rail_capped,
            rail.capped,
            inert,
            valid);
        const AssignmentView* broken = Find(effective, "broken:");
        rail.has_legacy_broken_links = false;
        if (broken != NULL && broken->value != NULL)
        {
            if (IsExpression(*broken->value))
            {
                DisableExpression(entry, kind, "broken:", inert);
            }
            else if (broken->value->type != JBeamValueType::ARRAY)
            {
                InvalidType(
                    entry,
                    kind,
                    "broken:",
                    "Legacy broken: field must be an array",
                    valid);
            }
            else
            {
                rail.has_legacy_broken_links =
                    !broken->value->array_values.empty();
                for (std::size_t i = 0U;
                     i < broken->value->array_values.size();
                     ++i)
                {
                    if (!Work(
                            1U,
                            entry.provenance,
                            kind,
                            entry.source_record_index,
                            entry.source_entry_index))
                    {
                        valid = false;
                        return;
                    }
                    const JBeamValue& link =
                        broken->value->array_values[i];
                    if (IsExpression(link))
                    {
                        DisableExpression(
                            entry, kind, "broken:", inert);
                    }
                    else if (link.type != JBeamValueType::STRING ||
                        link.scalar_text.empty())
                    {
                        InvalidType(
                            entry,
                            kind,
                            "broken:",
                            "Legacy broken: entries must be node "
                            "name strings",
                            valid);
                    }
                }
            }
        }
    }

    void ParseLinks(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const AssignmentView* assignment,
        std::vector<std::string>& links,
        bool& inert,
        bool& valid)
    {
        if (assignment == NULL || assignment->value == NULL)
        {
            Missing(entry, kind, "links:", valid);
            return;
        }
        const JBeamValue& value = *assignment->value;
        if (IsExpression(value))
        {
            DisableExpression(entry, kind, "links:", inert);
            return;
        }
        if (value.type != JBeamValueType::ARRAY)
        {
            InvalidType(
                entry,
                kind,
                "links:",
                "Rail links: must be an array",
                valid);
            return;
        }
        if (value.array_values.size() < 2U)
        {
            Error(
                entry,
                kind,
                JBeamAdvancedDiagnosticCode::INVALID_RAIL_LINKS,
                "links:",
                "Rail requires at least two node links");
            valid = false;
            return;
        }
        for (std::size_t i = 0U; i < value.array_values.size(); ++i)
        {
            if (!Work(
                    1U,
                    entry.provenance,
                    kind,
                    entry.source_record_index,
                    entry.source_entry_index))
            {
                valid = false;
                return;
            }
            const JBeamValue& link = value.array_values[i];
            if (IsExpression(link))
            {
                DisableExpression(entry, kind, "links:", inert);
                continue;
            }
            if (link.type != JBeamValueType::STRING ||
                link.scalar_text.empty())
            {
                InvalidType(
                    entry,
                    kind,
                    "links:",
                    "Rail link must be a non-empty node name",
                    valid);
                continue;
            }
            links.push_back(link.scalar_text);
            ValidateNodeReference(
                entry,
                kind,
                link.scalar_text,
                "links:",
                valid);
            if (links.size() >= 2U &&
                links[links.size() - 2U] == links.back())
            {
                Error(
                    entry,
                    kind,
                    JBeamAdvancedDiagnosticCode::
                        DUPLICATE_NODE_REFERENCE,
                    "links:",
                    "Consecutive rail links must be distinct nodes");
                valid = false;
            }
        }
    }

    void ValidateRailGeometry(
        JBeamAdvancedRail& rail,
        bool& valid)
    {
        for (std::size_t i = 1U; i < rail.links.size(); ++i)
        {
            Vec3 first;
            Vec3 second;
            if (Coordinates(rail.links[i - 1U], first) &&
                Coordinates(rail.links[i], second) &&
                CoordinatesEqual(first, second))
            {
                Error(
                    rail.entry,
                    m_result.source_records[
                        rail.entry.source_record_index].kind,
                    JBeamAdvancedDiagnosticCode::
                        DEGENERATE_NODE_GEOMETRY,
                    "links:",
                    "Consecutive rail nodes occupy the same position");
                valid = false;
            }
        }
    }

    void ParseHydro(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamAdvancedDocumentationProfile& profile =
            GetJBeamAdvancedDocumentationProfile();
        JBeamAdvancedHydro hydro;
        hydro.entry = MakeEntry(record_index, entry_index, row, effective);
        bool inert = false;
        bool valid = !m_record_meta[record_index].duplicate_section;
        ReadString(
            hydro.entry,
            record.kind,
            effective,
            "id1:",
            true,
            hydro.node1,
            inert,
            valid);
        ReadString(
            hydro.entry,
            record.kind,
            effective,
            "id2:",
            true,
            hydro.node2,
            inert,
            valid);
        ValidateNodePair(
            hydro.entry,
            record.kind,
            hydro.node1,
            hydro.node2,
            inert,
            valid);

        hydro.input_source = profile.hydro_input_source;
        ReadString(
            hydro.entry,
            record.kind,
            effective,
            "inputSource",
            false,
            hydro.input_source,
            inert,
            valid);
        hydro.has_factor = Find(effective, "factor") != NULL;
        ReadNumber(
            hydro.entry,
            record.kind,
            effective,
            "factor",
            false,
            false,
            0.0,
            hydro.factor,
            m_unused_bool,
            inert,
            valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "outLimit", false,
            false, profile.hydro_out_limit, hydro.out_limit,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inLimit", false,
            false, profile.hydro_in_limit, hydro.in_limit,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inputFactor", false,
            false, profile.hydro_input_factor, hydro.input_factor,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inputCenter", false,
            false, profile.hydro_input_center, hydro.input_center,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inRate", false,
            false, profile.hydro_in_rate, hydro.in_rate,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "outRate", false,
            false, hydro.in_rate, hydro.out_rate,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "autoCenterRate", false,
            false, hydro.in_rate, hydro.auto_center_rate,
            m_unused_bool, inert, valid);
        hydro.has_steering_wheel_lock =
            Find(effective, "steeringWheelLock") != NULL;
        ReadNumber(
            hydro.entry, record.kind, effective, "steeringWheelLock",
            false, false, 0.0, hydro.steering_wheel_lock,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inputInLimit", false,
            false, profile.hydro_input_in_limit, hydro.input_in_limit,
            m_unused_bool, inert, valid);
        ReadNumber(
            hydro.entry, record.kind, effective, "inputOutLimit", false,
            false, profile.hydro_input_out_limit, hydro.input_out_limit,
            m_unused_bool, inert, valid);
        ValidateHydroBeamFields(
            hydro.entry,
            record.kind,
            effective,
            inert,
            valid);

        ValidateUnknownFields(
            hydro.entry,
            record.kind,
            effective,
            HydroFields());
        hydro.entry.behavior = Behavior(
            valid,
            inert,
            JBeamAdvancedBehavior::INVENTORY_ONLY);
        if (!Charge(
                sizeof(JBeamAdvancedHydro) -
                    sizeof(JBeamAdvancedEntry) +
                hydro.node1.size() +
                hydro.node2.size() +
                hydro.input_source.size()))
        {
            return;
        }
        m_result.hydros.push_back(hydro);
    }

    void ValidateHydroBeamFields(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        bool& inert,
        bool& valid)
    {
        const char* finite_fields[] = {
            "beamSpring", "beamDamp", "beamPrecompression",
            "beamLongBound", "beamShortBound", "breakGroupType"
        };
        for (std::size_t i = 0U;
             i < sizeof(finite_fields) / sizeof(finite_fields[0]);
             ++i)
        {
            if (Find(effective, finite_fields[i]) != NULL)
            {
                double ignored = 0.0;
                bool ignored_flt_max = false;
                ReadNumber(
                    entry,
                    kind,
                    effective,
                    finite_fields[i],
                    false,
                    false,
                    0.0,
                    ignored,
                    ignored_flt_max,
                    inert,
                    valid);
            }
        }
        const char* unbounded_fields[] = {
            "beamDeform", "beamStrength"
        };
        for (std::size_t i = 0U;
             i < sizeof(unbounded_fields) /
                 sizeof(unbounded_fields[0]);
             ++i)
        {
            if (Find(effective, unbounded_fields[i]) != NULL)
            {
                double ignored = 0.0;
                bool ignored_flt_max = false;
                ReadNumber(
                    entry,
                    kind,
                    effective,
                    unbounded_fields[i],
                    false,
                    true,
                    0.0,
                    ignored,
                    ignored_flt_max,
                    inert,
                    valid);
            }
        }
        if (Find(effective, "beamType") != NULL)
        {
            std::string ignored;
            ReadString(
                entry,
                kind,
                effective,
                "beamType",
                false,
                ignored,
                inert,
                valid);
        }
    }

    static std::set<std::string> HydroFields()
    {
        std::set<std::string> result;
        const char* fields[] = {
            "id1:", "id2:", "inputSource", "factor", "outLimit",
            "inLimit", "inputFactor", "inputCenter", "inRate",
            "outRate", "autoCenterRate", "steeringWheelLock",
            "inputInLimit", "inputOutLimit",
            // Hydros explicitly inherit standard-beam arguments.
            "beamSpring", "beamDamp", "beamDeform", "beamStrength",
            "beamPrecompression", "beamType", "beamLongBound",
            "beamShortBound", "breakGroup", "breakGroupType"
        };
        for (std::size_t i = 0U;
             i < sizeof(fields) / sizeof(fields[0]);
             ++i)
        {
            result.insert(fields[i]);
        }
        return result;
    }

    void ParseSlideNode(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamAdvancedDocumentationProfile& profile =
            GetJBeamAdvancedDocumentationProfile();
        JBeamAdvancedSlideNode slide;
        slide.entry =
            MakeEntry(record_index, entry_index, row, effective);
        bool inert = false;
        bool valid = !m_record_meta[record_index].duplicate_section;
        ReadString(
            slide.entry, record.kind, effective, "id:", true,
            slide.node, inert, valid);
        ReadString(
            slide.entry, record.kind, effective, "railName", true,
            slide.rail_name, inert, valid);
        if (!inert)
        {
            ValidateNodeReference(
                slide.entry,
                record.kind,
                slide.node,
                "id:",
                valid);
            const std::map<
                std::string,
                std::vector<std::size_t> >::const_iterator rail =
                    m_rail_names.find(slide.rail_name);
            if (rail == m_rail_names.end() ||
                rail->second.size() != 1U ||
                m_result.rails[rail->second[0]].entry.behavior ==
                    JBeamAdvancedBehavior::REJECTED_INVALID)
            {
                Error(
                    slide.entry,
                    record.kind,
                    JBeamAdvancedDiagnosticCode::
                        MISSING_RAIL_REFERENCE,
                    "railName",
                    "Slidenode railName does not identify one valid "
                    "literal rail");
                valid = false;
            }
        }
        ReadBoolean(
            slide.entry, record.kind, effective, "attached", false,
            profile.slidenode_attached, slide.attached,
            inert, valid);
        ReadBoolean(
            slide.entry, record.kind, effective, "fixToRail", false,
            profile.slidenode_fix_to_rail, slide.fix_to_rail,
            inert, valid);
        slide.has_tolerance = Find(effective, "tolerance") != NULL;
        ReadNumber(
            slide.entry, record.kind, effective, "tolerance", false,
            false, 0.0, slide.tolerance, m_unused_bool, inert, valid);
        slide.has_spring = Find(effective, "spring") != NULL;
        ReadNumber(
            slide.entry, record.kind, effective, "spring", false,
            false, 0.0, slide.spring, m_unused_bool, inert, valid);
        slide.has_strength = Find(effective, "strength") != NULL;
        ReadNumber(
            slide.entry, record.kind, effective, "strength", false,
            true, 0.0, slide.strength, slide.strength_is_flt_max,
            inert, valid);
        slide.has_cap_strength =
            Find(effective, "capStrength") != NULL;
        ReadNumber(
            slide.entry, record.kind, effective, "capStrength", false,
            true, 0.0, slide.cap_strength,
            slide.cap_strength_is_flt_max, inert, valid);
        ValidateUnknownFields(
            slide.entry,
            record.kind,
            effective,
            SlideFields());
        slide.entry.behavior = Behavior(
            valid,
            inert,
            JBeamAdvancedBehavior::INVENTORY_ONLY);
        if (!Charge(
                sizeof(JBeamAdvancedSlideNode) -
                    sizeof(JBeamAdvancedEntry) +
                slide.node.size() +
                slide.rail_name.size()))
        {
            return;
        }
        m_result.slidenodes.push_back(slide);
    }

    static std::set<std::string> SlideFields()
    {
        std::set<std::string> result;
        const char* fields[] = {
            "id:", "railName", "attached", "fixToRail", "tolerance",
            "spring", "strength", "capStrength"
        };
        for (std::size_t i = 0U;
             i < sizeof(fields) / sizeof(fields[0]);
             ++i)
        {
            result.insert(fields[i]);
        }
        return result;
    }

    void ParseThruster(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamAdvancedDocumentationProfile& profile =
            GetJBeamAdvancedDocumentationProfile();
        JBeamAdvancedThruster thruster;
        thruster.entry =
            MakeEntry(record_index, entry_index, row, effective);
        bool inert = false;
        bool valid = !m_record_meta[record_index].duplicate_section;
        ReadString(
            thruster.entry, record.kind, effective, "id1:", true,
            thruster.direction_node, inert, valid);
        ReadString(
            thruster.entry, record.kind, effective, "id2:", true,
            thruster.force_node, inert, valid);
        ValidateNodePair(
            thruster.entry,
            record.kind,
            thruster.direction_node,
            thruster.force_node,
            inert,
            valid);
        ReadNumber(
            thruster.entry, record.kind, effective, "factor", false,
            false, profile.thruster_factor, thruster.factor,
            m_unused_bool, inert, valid);
        ReadNumber(
            thruster.entry, record.kind, effective, "thrustLimit",
            false, true,
            static_cast<double>(std::numeric_limits<float>::max()),
            thruster.thrust_limit,
            thruster.thrust_limit_is_flt_max, inert, valid);
        if (Find(effective, "thrustLimit") == NULL)
        {
            thruster.thrust_limit_is_flt_max = true;
        }
        ReadString(
            thruster.entry, record.kind, effective, "control", true,
            thruster.control, inert, valid);
        ValidateUnknownFields(
            thruster.entry,
            record.kind,
            effective,
            ThrusterFields());
        thruster.entry.behavior = Behavior(
            valid,
            inert,
            JBeamAdvancedBehavior::INVENTORY_ONLY);
        if (!Charge(
                sizeof(JBeamAdvancedThruster) -
                    sizeof(JBeamAdvancedEntry) +
                thruster.direction_node.size() +
                thruster.force_node.size() +
                thruster.control.size()))
        {
            return;
        }
        m_result.thrusters.push_back(thruster);
    }

    static std::set<std::string> ThrusterFields()
    {
        std::set<std::string> result;
        result.insert("id1:");
        result.insert("id2:");
        result.insert("factor");
        result.insert("thrustLimit");
        result.insert("control");
        return result;
    }

    void ParseTorsionBar(
        std::size_t record_index,
        std::size_t entry_index,
        const JBeamValue& row,
        const std::map<std::string, AssignmentView>& effective)
    {
        const JBeamAdvancedSourceRecord& record =
            m_result.source_records[record_index];
        const JBeamAdvancedDocumentationProfile& profile =
            GetJBeamAdvancedDocumentationProfile();
        JBeamAdvancedTorsionBar bar;
        bar.entry =
            MakeEntry(record_index, entry_index, row, effective);
        bool inert = false;
        bool valid = !m_record_meta[record_index].duplicate_section;
        ReadString(
            bar.entry, record.kind, effective, "id1:", true,
            bar.lever1_node, inert, valid);
        ReadString(
            bar.entry, record.kind, effective, "id2:", true,
            bar.axis1_node, inert, valid);
        ReadString(
            bar.entry, record.kind, effective, "id3:", true,
            bar.axis2_node, inert, valid);
        ReadString(
            bar.entry, record.kind, effective, "id4:", true,
            bar.lever2_node, inert, valid);
        const std::string ids[] = {
            bar.lever1_node,
            bar.axis1_node,
            bar.axis2_node,
            bar.lever2_node
        };
        if (!inert)
        {
            std::set<std::string> unique;
            for (std::size_t i = 0U; i < 4U; ++i)
            {
                ValidateNodeReference(
                    bar.entry,
                    record.kind,
                    ids[i],
                    "id",
                    valid);
                unique.insert(ids[i]);
            }
            if (unique.size() != 4U)
            {
                Error(
                    bar.entry,
                    record.kind,
                    JBeamAdvancedDiagnosticCode::
                        DUPLICATE_NODE_REFERENCE,
                    "id",
                    "Torsionbar requires four distinct node roles");
                valid = false;
            }
        }
        bar.has_spring = Find(effective, "spring") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "spring", false,
            false, 0.0, bar.spring, m_unused_bool, inert, valid);
        bar.has_damp = Find(effective, "damp") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "damp", false,
            false, 0.0, bar.damp, m_unused_bool, inert, valid);
        bar.has_spring2 = Find(effective, "spring2") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "spring2", false,
            false, bar.spring, bar.spring2, m_unused_bool, inert, valid);
        bar.has_damp2 = Find(effective, "damp2") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "damp2", false,
            false, bar.damp, bar.damp2, m_unused_bool, inert, valid);
        bar.anisotropic = bar.has_spring2 || bar.has_damp2;
        bar.has_deform = Find(effective, "deform") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "deform", false,
            false, 0.0, bar.deform, m_unused_bool, inert, valid);
        bar.has_strength = Find(effective, "strength") != NULL;
        ReadNumber(
            bar.entry, record.kind, effective, "strength", false,
            false, 0.0, bar.strength, m_unused_bool, inert, valid);
        ReadNumber(
            bar.entry, record.kind, effective, "precompressionAngle",
            false, false, profile.torsion_precompression_angle,
            bar.precompression_angle, m_unused_bool, inert, valid);
        ReadNumber(
            bar.entry, record.kind, effective, "precompressionTime",
            false, false, profile.torsion_precompression_time,
            bar.precompression_time, m_unused_bool, inert, valid);
        ReadString(
            bar.entry, record.kind, effective, "name", false,
            bar.name, inert, valid);
        if (!inert)
        {
            ValidateTorsionGeometry(bar, valid);
        }
        ValidateUnknownFields(
            bar.entry,
            record.kind,
            effective,
            TorsionFields());
        bar.entry.behavior = Behavior(
            valid,
            inert,
            JBeamAdvancedBehavior::INVENTORY_ONLY);
        if (!Charge(
                sizeof(JBeamAdvancedTorsionBar) -
                    sizeof(JBeamAdvancedEntry) +
                bar.lever1_node.size() +
                bar.axis1_node.size() +
                bar.axis2_node.size() +
                bar.lever2_node.size() +
                bar.name.size()))
        {
            return;
        }
        m_result.torsionbars.push_back(bar);
    }

    static std::set<std::string> TorsionFields()
    {
        std::set<std::string> result;
        const char* fields[] = {
            "id1:", "id2:", "id3:", "id4:", "spring", "damp",
            "spring2", "damp2", "deform", "strength",
            "precompressionAngle", "precompressionTime", "name"
        };
        for (std::size_t i = 0U;
             i < sizeof(fields) / sizeof(fields[0]);
             ++i)
        {
            result.insert(fields[i]);
        }
        return result;
    }

    void ValidateTorsionGeometry(
        JBeamAdvancedTorsionBar& bar,
        bool& valid)
    {
        Vec3 lever1;
        Vec3 axis1;
        Vec3 axis2;
        Vec3 lever2;
        if (!Coordinates(bar.lever1_node, lever1) ||
            !Coordinates(bar.axis1_node, axis1) ||
            !Coordinates(bar.axis2_node, axis2) ||
            !Coordinates(bar.lever2_node, lever2))
        {
            return;
        }
        Vec3 first_arm;
        Vec3 axis;
        Vec3 second_arm;
        if (!NormalizedDirection(axis1, lever1, first_arm) ||
            !NormalizedDirection(axis1, axis2, axis) ||
            !NormalizedDirection(axis2, lever2, second_arm) ||
            DirectionsCollinear(first_arm, axis) ||
            DirectionsCollinear(second_arm, axis))
        {
            Error(
                bar.entry,
                JBeamAdvancedSectionKind::TORSIONBARS,
                JBeamAdvancedDiagnosticCode::
                    DEGENERATE_NODE_GEOMETRY,
                "id",
                "Torsionbar axis and lever arms must be "
                "non-degenerate and non-collinear");
            valid = false;
        }
    }

    void ValidateNodePair(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::string& first,
        const std::string& second,
        bool inert,
        bool& valid)
    {
        if (inert)
        {
            return;
        }
        ValidateNodeReference(entry, kind, first, "id1:", valid);
        ValidateNodeReference(entry, kind, second, "id2:", valid);
        if (first == second)
        {
            Error(
                entry,
                kind,
                JBeamAdvancedDiagnosticCode::
                    DUPLICATE_NODE_REFERENCE,
                "id2:",
                "Advanced element endpoints must be distinct nodes");
            valid = false;
            return;
        }
        Vec3 a;
        Vec3 b;
        if (Coordinates(first, a) &&
            Coordinates(second, b) &&
            CoordinatesEqual(a, b))
        {
            Error(
                entry,
                kind,
                JBeamAdvancedDiagnosticCode::
                    DEGENERATE_NODE_GEOMETRY,
                "id2:",
                "Advanced element endpoints occupy the same position");
            valid = false;
        }
    }

    void ValidateNodeReference(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::string& name,
        const std::string& field,
        bool& valid)
    {
        const std::map<std::string, NodeInfo>::const_iterator found =
            m_nodes.find(name);
        if (name.empty() ||
            found == m_nodes.end() ||
            found->second.count != 1U)
        {
            Error(
                entry,
                kind,
                JBeamAdvancedDiagnosticCode::
                    MISSING_NODE_REFERENCE,
                field,
                found != m_nodes.end() && found->second.count > 1U
                    ? "Node reference is ambiguous because the node "
                      "name is duplicated"
                    : "Node reference does not identify a literal "
                      "resolved node");
            valid = false;
        }
    }

    bool Coordinates(const std::string& name, Vec3& value) const
    {
        const std::map<std::string, NodeInfo>::const_iterator found =
            m_nodes.find(name);
        if (found == m_nodes.end() ||
            found->second.count != 1U ||
            !found->second.has_coordinates)
        {
            return false;
        }
        value = found->second.coordinates;
        return true;
    }

    void ReadString(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        const std::string& field,
        bool required,
        std::string& output,
        bool& inert,
        bool& valid)
    {
        const AssignmentView* assignment = Find(effective, field);
        if (assignment == NULL || assignment->value == NULL)
        {
            if (required)
            {
                Missing(entry, kind, field, valid);
            }
            return;
        }
        if (IsExpression(*assignment->value))
        {
            output = assignment->value->scalar_text;
            DisableExpression(entry, kind, field, inert);
            return;
        }
        if (assignment->value->type != JBeamValueType::STRING ||
            (required && assignment->value->scalar_text.empty()))
        {
            InvalidType(
                entry,
                kind,
                field,
                "Field must be a non-empty literal string",
                valid);
            return;
        }
        output = assignment->value->scalar_text;
    }

    void ReadBoolean(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        const std::string& field,
        bool required,
        bool default_value,
        bool& output,
        bool& inert,
        bool& valid)
    {
        output = default_value;
        const AssignmentView* assignment = Find(effective, field);
        if (assignment == NULL || assignment->value == NULL)
        {
            if (required)
            {
                Missing(entry, kind, field, valid);
            }
            return;
        }
        if (IsExpression(*assignment->value))
        {
            DisableExpression(entry, kind, field, inert);
            return;
        }
        if (assignment->value->type != JBeamValueType::BOOLEAN)
        {
            InvalidType(
                entry,
                kind,
                field,
                "Field must be a boolean",
                valid);
            return;
        }
        output = assignment->value->boolean_value;
    }

    void ReadNumber(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        const std::string& field,
        bool required,
        bool allow_flt_max,
        double default_value,
        double& output,
        bool& is_flt_max,
        bool& inert,
        bool& valid)
    {
        output = default_value;
        is_flt_max = false;
        const AssignmentView* assignment = Find(effective, field);
        if (assignment == NULL || assignment->value == NULL)
        {
            if (required)
            {
                Missing(entry, kind, field, valid);
            }
            return;
        }
        const JBeamValue& value = *assignment->value;
        if (IsExpression(value))
        {
            DisableExpression(entry, kind, field, inert);
            return;
        }
        if (allow_flt_max && IsFltMax(value))
        {
            is_flt_max = true;
            output =
                static_cast<double>(std::numeric_limits<float>::max());
            return;
        }
        if (value.type != JBeamValueType::NUMBER)
        {
            InvalidType(
                entry,
                kind,
                field,
                allow_flt_max
                    ? "Field must be a finite number or exact FLT_MAX"
                    : "Field must be a finite number",
                valid);
            return;
        }
        if (!IsFiniteDouble(value.number_value))
        {
            Error(
                entry,
                kind,
                JBeamAdvancedDiagnosticCode::NON_FINITE_NUMBER,
                field,
                "Non-finite numeric value is rejected");
            valid = false;
            return;
        }
        output = value.number_value;
    }

    void Missing(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::string& field,
        bool& valid)
    {
        Error(
            entry,
            kind,
            JBeamAdvancedDiagnosticCode::MISSING_REQUIRED_FIELD,
            field,
            "Required advanced structure field is missing");
        valid = false;
    }

    void InvalidType(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::string& field,
        const std::string& detail,
        bool& valid)
    {
        Error(
            entry,
            kind,
            JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE,
            field,
            detail);
        valid = false;
    }

    void DisableExpression(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::string& field,
        bool& inert)
    {
        Push(
            JBeamAdvancedDiagnosticCode::EXPRESSION_DISABLED,
            JBeamAdvancedSeverity::WARNING,
            entry.provenance,
            kind,
            entry.source_record_index,
            entry.source_entry_index,
            field,
            "Expression/variable is preserved but never evaluated");
        inert = true;
    }

    void Error(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        JBeamAdvancedDiagnosticCode code,
        const std::string& field,
        const std::string& detail)
    {
        Push(
            code,
            JBeamAdvancedSeverity::ERROR_SEVERITY,
            entry.provenance,
            kind,
            entry.source_record_index,
            entry.source_entry_index,
            field,
            detail);
    }

    void ValidateUnknownFields(
        JBeamAdvancedEntry& entry,
        JBeamAdvancedSectionKind kind,
        const std::map<std::string, AssignmentView>& effective,
        const std::set<std::string>& known)
    {
        for (std::map<std::string, AssignmentView>::const_iterator it =
                 effective.begin();
             it != effective.end();
             ++it)
        {
            if (known.count(it->first) == 0U)
            {
                Push(
                    JBeamAdvancedDiagnosticCode::
                        UNKNOWN_FIELD_PRESERVED,
                    JBeamAdvancedSeverity::WARNING,
                    entry.provenance,
                    kind,
                    entry.source_record_index,
                    entry.source_entry_index,
                    it->first,
                    "Unknown advanced field is retained inertly");
            }
        }
    }

    static JBeamAdvancedBehavior Behavior(
        bool valid,
        bool inert,
        JBeamAdvancedBehavior accepted)
    {
        if (!valid)
        {
            return JBeamAdvancedBehavior::REJECTED_INVALID;
        }
        if (inert)
        {
            return JBeamAdvancedBehavior::
                PRESERVED_DISABLED_INERT_EXPRESSION;
        }
        return accepted;
    }

    bool ChargeRail(const JBeamAdvancedRail& rail)
    {
        std::size_t bytes =
            sizeof(JBeamAdvancedRail) -
            sizeof(JBeamAdvancedEntry) +
            rail.name.size();
        for (std::size_t i = 0U; i < rail.links.size(); ++i)
        {
            if (!AddSize(
                    sizeof(std::string) + rail.links[i].size(),
                    bytes))
            {
                Push(
                    JBeamAdvancedDiagnosticCode::RETAINED_BYTE_LIMIT,
                    JBeamAdvancedSeverity::ERROR_SEVERITY,
                    rail.entry.provenance,
                    m_result.source_records[
                        rail.entry.source_record_index].kind,
                    rail.entry.source_record_index,
                    rail.entry.source_entry_index,
                    "links:",
                    "Rail retained-byte calculation overflowed");
                m_resource_limit = true;
                return false;
            }
        }
        return Charge(bytes);
    }

    void Finish()
    {
        m_result.authored_entry_count = m_authored_entries;
        m_result.node_coordinate_row_count = m_node_rows;
        m_result.effective_field_count = m_effective_fields;
        m_result.retained_byte_count = m_retained_bytes;
        m_result.work_unit_count = m_work;
        m_result.preserved_value_work_unit_count = m_value_work;
    }

    const JBeamResolvedGraph& m_graph;
    JBeamAdvancedLimits m_limits;
    JBeamAdvancedStructureIR m_result;
    std::vector<PartView> m_parts;
    std::vector<RecordMeta> m_record_meta;
    std::map<std::string, NodeInfo> m_nodes;
    std::map<std::string, std::vector<std::size_t> > m_rail_names;
    std::size_t m_retained_bytes;
    std::size_t m_work;
    std::size_t m_value_work;
    std::size_t m_effective_fields;
    std::size_t m_authored_entries;
    std::size_t m_node_rows;
    bool m_resource_limit;
    bool m_diagnostic_limit_emitted;
    bool m_unused_bool;
};

class CanonicalWriter
{
public:
    CanonicalWriter(std::size_t byte_limit, std::size_t work_limit)
        : m_byte_limit(std::min(byte_limit, HARD_MAX_CANONICAL_BYTES))
        , m_work_limit(std::min(work_limit, HARD_MAX_CANONICAL_WORK))
        , m_work(0U)
        , m_ok(byte_limit != 0U && work_limit != 0U)
    {
    }

    bool Ok() const { return m_ok; }
    const std::string& Data() const { return m_data; }

    void Unit()
    {
        if (!m_ok ||
            !AddSize(1U, m_work) ||
            m_work > m_work_limit)
        {
            m_ok = false;
        }
    }

    void Byte(unsigned char value)
    {
        Unit();
        Append(reinterpret_cast<const char*>(&value), 1U);
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
        Append(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }

    void Size(std::size_t value)
    {
        U64(static_cast<std::uint64_t>(value));
    }

    void Double(double value)
    {
        U64(DoubleBits(value));
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

    bool OptionalValue(
        const std::shared_ptr<const JBeamValue>& value,
        std::size_t depth_limit)
    {
        Boolean(static_cast<bool>(value));
        if (!value)
        {
            return m_ok;
        }
        std::set<const JBeamValue*> stack;
        return Value(*value, 1U, depth_limit, stack);
    }

private:
    void Append(const char* data, std::size_t size)
    {
        if (!m_ok ||
            size > m_byte_limit - std::min(m_data.size(), m_byte_limit))
        {
            m_ok = false;
            return;
        }
        m_data.append(data, size);
    }

    bool Value(
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
            Double(value.number_value);
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
                Value(
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
                    Value(
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

void WriteProvenance(
    CanonicalWriter& writer,
    const JBeamAdvancedProvenance& provenance)
{
    writer.Boolean(static_cast<bool>(provenance.part));
    if (provenance.part)
    {
        writer.Size(provenance.part->part_preorder_index);
        writer.String(provenance.part->part_name);
        writer.String(provenance.part->package_path);
    }
    writer.Span(provenance.span);
}

bool WriteEntry(
    CanonicalWriter& writer,
    const JBeamAdvancedEntry& entry,
    std::size_t depth_limit)
{
    writer.Byte(static_cast<unsigned char>(entry.behavior));
    writer.Size(entry.source_record_index);
    writer.Size(entry.source_entry_index);
    WriteProvenance(writer, entry.provenance);
    if (!writer.OptionalValue(entry.raw_value, depth_limit))
    {
        return false;
    }
    writer.Size(entry.effective_fields.size());
    for (std::size_t i = 0U;
         i < entry.effective_fields.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedField& field = entry.effective_fields[i];
        writer.String(field.name);
        writer.Byte(static_cast<unsigned char>(field.origin));
        WriteProvenance(writer, field.provenance);
        if (!writer.OptionalValue(field.raw_value, depth_limit))
        {
            return false;
        }
    }
    return writer.Ok();
}

} // namespace

JBeamAdvancedDocumentationProfile::
    JBeamAdvancedDocumentationProfile()
    : profile_id("beamng-docs-0.38.5.0-2026-07-27")
    , beamng_version("0.38.5.0")
    , hydros_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/hydros/")
    , hydros_last_modified("2025-01-24")
    , rails_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/rails/")
    , rails_last_modified("2025-12-10")
    , thrusters_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/thrusters/")
    , thrusters_last_modified("2025-01-24")
    , torsionbars_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/torsionbars/")
    , torsionbars_last_modified("2025-04-09")
    , hydro_input_source("steering_input")
    , hydro_out_limit(2.0)
    , hydro_in_limit(1.0)
    , hydro_input_factor(1.0)
    , hydro_input_center(0.0)
    , hydro_in_rate(2.0)
    , hydro_out_rate_inherits_in_rate(true)
    , hydro_auto_center_rate_inherits_in_rate(true)
    , hydro_input_in_limit(-1.0)
    , hydro_input_out_limit(1.0)
    , slidenode_attached(true)
    , slidenode_fix_to_rail(true)
    , rail_looped(false)
    , rail_capped(false)
    , thruster_factor(1.0)
    , thruster_limit_is_flt_max(true)
    , torsion_precompression_angle(0.0)
    , torsion_precompression_time(0.0)
    , torsion_spring2_inherits_spring(true)
    , torsion_damp2_inherits_damp(true)
{
}

const JBeamAdvancedDocumentationProfile&
GetJBeamAdvancedDocumentationProfile()
{
    static const JBeamAdvancedDocumentationProfile profile;
    return profile;
}

JBeamAdvancedPartIdentity::JBeamAdvancedPartIdentity()
    : part_preorder_index(0U)
{
}

JBeamAdvancedProvenance::JBeamAdvancedProvenance()
{
}

std::size_t JBeamAdvancedProvenance::PartPreorderIndex() const
{
    return part ? part->part_preorder_index : 0U;
}

const std::string& JBeamAdvancedProvenance::PartName() const
{
    static const std::string empty;
    return part ? part->part_name : empty;
}

const std::string& JBeamAdvancedProvenance::PackagePath() const
{
    static const std::string empty;
    return part ? part->package_path : empty;
}

JBeamAdvancedDiagnostic::JBeamAdvancedDiagnostic()
    : code(JBeamAdvancedDiagnosticCode::INVALID_RESOLVED_GRAPH)
    , severity(JBeamAdvancedSeverity::ERROR_SEVERITY)
    , section_kind(JBeamAdvancedSectionKind::HYDROS)
    , source_record_index(0U)
    , entry_index(0U)
{
}

JBeamAdvancedLimits::JBeamAdvancedLimits()
    : max_parts(HARD_MAX_PARTS)
    , max_graph_depth(HARD_MAX_GRAPH_DEPTH)
    , max_source_records(HARD_MAX_SOURCE_RECORDS)
    , max_entries(HARD_MAX_ENTRIES)
    , max_modifiers(HARD_MAX_MODIFIERS)
    , max_effective_fields(HARD_MAX_EFFECTIVE_FIELDS)
    , max_node_coordinates(HARD_MAX_NODE_COORDINATES)
    , max_diagnostics(HARD_MAX_DIAGNOSTICS)
    , max_retained_bytes(HARD_MAX_RETAINED_BYTES)
    , max_work_units(HARD_MAX_WORK)
    , max_preserved_value_work_units(HARD_MAX_VALUE_WORK)
    , max_preserved_value_depth(HARD_MAX_VALUE_DEPTH)
    , max_canonical_output_bytes(HARD_MAX_CANONICAL_BYTES)
    , max_canonical_work_units(HARD_MAX_CANONICAL_WORK)
{
}

JBeamAdvancedSourceRecord::JBeamAdvancedSourceRecord()
    : kind(JBeamAdvancedSectionKind::HYDROS)
    , section_occurrence(0U)
{
}

JBeamAdvancedField::JBeamAdvancedField()
    : origin(JBeamAdvancedFieldOrigin::INHERITED_DEFAULT)
{
}

JBeamAdvancedModifier::JBeamAdvancedModifier()
    : section_kind(JBeamAdvancedSectionKind::HYDROS)
    , source_record_index(0U)
    , entry_index(0U)
{
}

JBeamAdvancedEntry::JBeamAdvancedEntry()
    : behavior(JBeamAdvancedBehavior::REJECTED_INVALID)
    , source_record_index(0U)
    , source_entry_index(0U)
{
}

JBeamAdvancedHydro::JBeamAdvancedHydro()
    : has_factor(false)
    , factor(0.0)
    , out_limit(2.0)
    , in_limit(1.0)
    , input_factor(1.0)
    , input_center(0.0)
    , in_rate(2.0)
    , out_rate(2.0)
    , auto_center_rate(2.0)
    , has_steering_wheel_lock(false)
    , steering_wheel_lock(0.0)
    , input_in_limit(-1.0)
    , input_out_limit(1.0)
{
}

JBeamAdvancedRail::JBeamAdvancedRail()
    : looped(false)
    , capped(false)
    , has_legacy_broken_links(false)
{
}

JBeamAdvancedSlideNode::JBeamAdvancedSlideNode()
    : attached(true)
    , fix_to_rail(true)
    , has_tolerance(false)
    , tolerance(0.0)
    , has_spring(false)
    , spring(0.0)
    , has_strength(false)
    , strength_is_flt_max(false)
    , strength(0.0)
    , has_cap_strength(false)
    , cap_strength_is_flt_max(false)
    , cap_strength(0.0)
{
}

JBeamAdvancedThruster::JBeamAdvancedThruster()
    : factor(1.0)
    , thrust_limit_is_flt_max(true)
    , thrust_limit(
        static_cast<double>(std::numeric_limits<float>::max()))
{
}

JBeamAdvancedTorsionBar::JBeamAdvancedTorsionBar()
    : has_spring(false)
    , spring(0.0)
    , has_damp(false)
    , damp(0.0)
    , has_spring2(false)
    , spring2(0.0)
    , has_damp2(false)
    , damp2(0.0)
    , anisotropic(false)
    , has_deform(false)
    , deform(0.0)
    , has_strength(false)
    , strength(0.0)
    , precompression_angle(0.0)
    , precompression_time(0.0)
{
}

JBeamAdvancedRejectedEntry::JBeamAdvancedRejectedEntry()
    : section_kind(JBeamAdvancedSectionKind::HYDROS)
{
}

JBeamAdvancedStructureIR::JBeamAdvancedStructureIR()
    : authored_entry_count(0U)
    , node_coordinate_row_count(0U)
    , effective_field_count(0U)
    , retained_byte_count(0U)
    , work_unit_count(0U)
    , preserved_value_work_unit_count(0U)
    , canonical_output_byte_limit(0U)
    , canonical_work_unit_limit(0U)
    , canonical_value_depth_limit(0U)
{
}

bool JBeamAdvancedStructureIR::IsValid() const
{
    for (std::size_t i = 0U; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].severity == JBeamAdvancedSeverity::ERROR_SEVERITY)
        {
            return false;
        }
    }
    return true;
}

JBeamHydroActuatorAdmission::JBeamHydroActuatorAdmission()
    : code(JBeamHydroActuatorAdmissionCode::INVALID_ADVANCED_IR)
    , source_hydro_index(0U)
    , has_steering_wheel_lock(false)
    , steering_wheel_lock(0.0)
{
}

bool JBeamHydroActuatorAdmission::IsAdmitted() const
{
    return code == JBeamHydroActuatorAdmissionCode::ADMITTED;
}

JBeamHydroActuatorAdmission AdmitJBeamHydroActuator(
    const JBeamAdvancedStructureIR& ir,
    std::size_t hydro_index)
{
    JBeamHydroActuatorAdmission result;
    result.source_hydro_index = hydro_index;
    if (!ir.IsValid())
    {
        result.code =
            JBeamHydroActuatorAdmissionCode::INVALID_ADVANCED_IR;
        return result;
    }
    if (hydro_index >= ir.hydros.size())
    {
        result.code =
            JBeamHydroActuatorAdmissionCode::HYDRO_INDEX_OUT_OF_RANGE;
        return result;
    }

    const JBeamAdvancedHydro& hydro = ir.hydros[hydro_index];
    if (hydro.entry.behavior != JBeamAdvancedBehavior::INVENTORY_ONLY)
    {
        result.code = JBeamHydroActuatorAdmissionCode::
            SOURCE_NOT_LITERAL_INVENTORY;
        return result;
    }
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        const JBeamAdvancedDiagnostic& diagnostic = ir.diagnostics[i];
        if (diagnostic.section_kind == JBeamAdvancedSectionKind::HYDROS &&
            diagnostic.source_record_index ==
                hydro.entry.source_record_index &&
            diagnostic.entry_index == hydro.entry.source_entry_index)
        {
            result.code =
                JBeamHydroActuatorAdmissionCode::SOURCE_HAS_DIAGNOSTIC;
            return result;
        }
    }

    HydroActuatorConfig config;
    config.has_factor = hydro.has_factor;
    config.factor = hydro.factor;
    config.in_limit = hydro.in_limit;
    config.out_limit = hydro.out_limit;
    config.input_factor = hydro.input_factor;
    config.input_center = hydro.input_center;
    config.input_in_limit = hydro.input_in_limit;
    config.input_out_limit = hydro.input_out_limit;
    config.in_rate = hydro.in_rate;
    config.out_rate = hydro.out_rate;
    config.auto_center_rate = hydro.auto_center_rate;
    if (!HydroActuatorDetail::IsValidConfig(config))
    {
        result.code =
            JBeamHydroActuatorAdmissionCode::INVALID_ACTUATOR_CONFIG;
        return result;
    }

    result.node1 = hydro.node1;
    result.node2 = hydro.node2;
    result.input_source = hydro.input_source;
    result.has_steering_wheel_lock = hydro.has_steering_wheel_lock;
    result.steering_wheel_lock = hydro.steering_wheel_lock;
    result.config = config;
    result.code = JBeamHydroActuatorAdmissionCode::ADMITTED;
    return result;
}

const char* JBeamHydroActuatorAdmissionCodeToString(
    JBeamHydroActuatorAdmissionCode code)
{
    switch (code)
    {
    case JBeamHydroActuatorAdmissionCode::ADMITTED:
        return "admitted";
    case JBeamHydroActuatorAdmissionCode::INVALID_ADVANCED_IR:
        return "invalid-advanced-ir";
    case JBeamHydroActuatorAdmissionCode::HYDRO_INDEX_OUT_OF_RANGE:
        return "hydro-index-out-of-range";
    case JBeamHydroActuatorAdmissionCode::SOURCE_NOT_LITERAL_INVENTORY:
        return "source-not-literal-inventory";
    case JBeamHydroActuatorAdmissionCode::SOURCE_HAS_DIAGNOSTIC:
        return "source-has-diagnostic";
    case JBeamHydroActuatorAdmissionCode::INVALID_ACTUATOR_CONFIG:
        return "invalid-actuator-config";
    }
    return "unknown";
}

JBeamHydroBeamPropertyConfig::JBeamHydroBeamPropertyConfig()
    : spring(4300000.0f)
    , damping(580.0f)
    , deform(220000.0f)
    , strength(std::numeric_limits<float>::max())
    , precompression(1.0f)
    , deform_is_flt_max(false)
    , strength_is_flt_max(true)
{
}

JBeamHydroBeamPropertyAdmission::JBeamHydroBeamPropertyAdmission()
    : code(JBeamHydroBeamPropertyAdmissionCode::ACTUATOR_NOT_ADMITTED)
    , source_hydro_index(0U)
{
}

bool JBeamHydroBeamPropertyAdmission::IsAdmitted() const
{
    return code == JBeamHydroBeamPropertyAdmissionCode::ADMITTED;
}

namespace {

const JBeamAdvancedField* FindUniqueEffectiveField(
    const JBeamAdvancedEntry& entry,
    const char* name,
    bool& malformed)
{
    const JBeamAdvancedField* result = NULL;
    for (std::size_t i = 0U; i < entry.effective_fields.size(); ++i)
    {
        const JBeamAdvancedField& field = entry.effective_fields[i];
        if (field.name == name)
        {
            if (result != NULL || !field.raw_value)
            {
                malformed = true;
                return NULL;
            }
            result = &field;
        }
    }
    return result;
}

bool IsNormalBinary32(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
        "hydro beam admission requires a binary32 float");
    static_assert(std::numeric_limits<float>::is_iec559,
        "hydro beam admission requires IEC 60559 floats");
    std::uint32_t bits = 0U;
    volatile unsigned char stored[sizeof(float)];
    const unsigned char* source =
        reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0U; i < sizeof(float); ++i)
    {
        stored[i] = source[i];
    }
    unsigned char* destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(float); ++i)
    {
        destination[i] = stored[i];
    }
    const std::uint32_t exponent = bits & UINT32_C(0x7f800000);
    return exponent != 0U && exponent != UINT32_C(0x7f800000);
}

bool TryNarrowBeamProperty(double value, bool require_positive, float& output)
{
    output = 0.0f;
    if (!IsFiniteDouble(value) ||
        value < 0.0 ||
        (require_positive && !(value > 0.0)) ||
        value > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }
    const volatile float narrowed = static_cast<float>(value);
    output = narrowed;
    if (value == 0.0)
    {
        return !require_positive && output == 0.0f;
    }
    return output > 0.0f && IsNormalBinary32(output);
}

enum class BeamPropertyReadCode
{
    OK,
    MALFORMED,
    INVALID,
    NARROWING
};

BeamPropertyReadCode ReadBeamProperty(
    const JBeamAdvancedEntry& entry,
    const char* name,
    double default_value,
    bool default_is_flt_max,
    bool allow_flt_max,
    bool require_positive,
    float& output,
    bool& is_flt_max)
{
    is_flt_max = default_is_flt_max;
    bool malformed = false;
    const JBeamAdvancedField* field =
        FindUniqueEffectiveField(entry, name, malformed);
    if (malformed)
    {
        return BeamPropertyReadCode::MALFORMED;
    }
    if (field == NULL)
    {
        return TryNarrowBeamProperty(
            default_value, require_positive, output)
            ? BeamPropertyReadCode::OK
            : BeamPropertyReadCode::NARROWING;
    }
    is_flt_max = false;
    const JBeamValue& value = *field->raw_value;
    if (allow_flt_max && IsFltMax(value))
    {
        output = std::numeric_limits<float>::max();
        is_flt_max = true;
        return BeamPropertyReadCode::OK;
    }
    if (value.type != JBeamValueType::NUMBER ||
        !IsFiniteDouble(value.number_value) ||
        value.number_value < 0.0 ||
        (require_positive && !(value.number_value > 0.0)))
    {
        return BeamPropertyReadCode::INVALID;
    }
    return TryNarrowBeamProperty(
        value.number_value, require_positive, output)
        ? BeamPropertyReadCode::OK
        : BeamPropertyReadCode::NARROWING;
}

JBeamHydroBeamPropertyAdmissionCode MapBeamPropertyReadCode(
    BeamPropertyReadCode code)
{
    if (code == BeamPropertyReadCode::MALFORMED)
    {
        return JBeamHydroBeamPropertyAdmissionCode::
            MALFORMED_EFFECTIVE_FIELD;
    }
    if (code == BeamPropertyReadCode::NARROWING)
    {
        return JBeamHydroBeamPropertyAdmissionCode::FLOAT_NARROWING;
    }
    return JBeamHydroBeamPropertyAdmissionCode::INVALID_BEAM_PROPERTY;
}

} // namespace

JBeamHydroBeamPropertyAdmission AdmitJBeamHydroBeamProperties(
    const JBeamAdvancedStructureIR& ir,
    std::size_t hydro_index)
{
    JBeamHydroBeamPropertyAdmission result;
    result.source_hydro_index = hydro_index;
    result.actuator = AdmitJBeamHydroActuator(ir, hydro_index);
    if (!result.actuator.IsAdmitted())
    {
        result.code =
            JBeamHydroBeamPropertyAdmissionCode::ACTUATOR_NOT_ADMITTED;
        return result;
    }

    const JBeamAdvancedEntry& entry = ir.hydros[hydro_index].entry;
    bool malformed = false;
    const JBeamAdvancedField* beam_type =
        FindUniqueEffectiveField(entry, "beamType", malformed);
    if (malformed || (beam_type != NULL && !beam_type->raw_value))
    {
        result.code = JBeamHydroBeamPropertyAdmissionCode::
            MALFORMED_EFFECTIVE_FIELD;
        return result;
    }
    if (beam_type != NULL)
    {
        const JBeamValue& type_value = *beam_type->raw_value;
        if (type_value.type != JBeamValueType::STRING ||
            type_value.scalar_text.empty())
        {
            result.code = JBeamHydroBeamPropertyAdmissionCode::
                UNSUPPORTED_BEAM_TYPE;
            return result;
        }
        std::string normalized = type_value.scalar_text;
        if (!normalized.empty() && normalized[0] == '|')
        {
            normalized.erase(0U, 1U);
        }
        if (normalized != "NORMAL")
        {
            result.code = JBeamHydroBeamPropertyAdmissionCode::
                UNSUPPORTED_BEAM_TYPE;
            return result;
        }
    }

    const char* unsupported_fields[] = {
        "beamLongBound", "beamShortBound", "breakGroup",
        "breakGroupType"
    };
    for (std::size_t i = 0U;
         i < sizeof(unsupported_fields) / sizeof(unsupported_fields[0]);
         ++i)
    {
        malformed = false;
        if (FindUniqueEffectiveField(
                entry, unsupported_fields[i], malformed) != NULL ||
            malformed)
        {
            result.code = malformed
                ? JBeamHydroBeamPropertyAdmissionCode::
                    MALFORMED_EFFECTIVE_FIELD
                : JBeamHydroBeamPropertyAdmissionCode::
                    UNSUPPORTED_BEAM_BEHAVIOR;
            return result;
        }
    }

    bool unused_flt_max = false;
    BeamPropertyReadCode read = ReadBeamProperty(
        entry, "beamSpring", 4300000.0, false, false, false,
        result.beam.spring, unused_flt_max);
    if (read == BeamPropertyReadCode::OK)
    {
        read = ReadBeamProperty(
            entry, "beamDamp", 580.0, false, false, false,
            result.beam.damping, unused_flt_max);
    }
    if (read == BeamPropertyReadCode::OK)
    {
        read = ReadBeamProperty(
            entry, "beamDeform", 220000.0, false, true, false,
            result.beam.deform, result.beam.deform_is_flt_max);
    }
    if (read == BeamPropertyReadCode::OK)
    {
        read = ReadBeamProperty(
            entry, "beamStrength",
            static_cast<double>(std::numeric_limits<float>::max()),
            true, true, false, result.beam.strength,
            result.beam.strength_is_flt_max);
    }
    if (read == BeamPropertyReadCode::OK)
    {
        read = ReadBeamProperty(
            entry, "beamPrecompression", 1.0, false, false, true,
            result.beam.precompression, unused_flt_max);
    }
    if (read != BeamPropertyReadCode::OK)
    {
        result.code = MapBeamPropertyReadCode(read);
        return result;
    }

    result.code = JBeamHydroBeamPropertyAdmissionCode::ADMITTED;
    return result;
}

const char* JBeamHydroBeamPropertyAdmissionCodeToString(
    JBeamHydroBeamPropertyAdmissionCode code)
{
    switch (code)
    {
    case JBeamHydroBeamPropertyAdmissionCode::ADMITTED:
        return "admitted";
    case JBeamHydroBeamPropertyAdmissionCode::ACTUATOR_NOT_ADMITTED:
        return "actuator-not-admitted";
    case JBeamHydroBeamPropertyAdmissionCode::MALFORMED_EFFECTIVE_FIELD:
        return "malformed-effective-field";
    case JBeamHydroBeamPropertyAdmissionCode::UNSUPPORTED_BEAM_TYPE:
        return "unsupported-beam-type";
    case JBeamHydroBeamPropertyAdmissionCode::UNSUPPORTED_BEAM_BEHAVIOR:
        return "unsupported-beam-behavior";
    case JBeamHydroBeamPropertyAdmissionCode::INVALID_BEAM_PROPERTY:
        return "invalid-beam-property";
    case JBeamHydroBeamPropertyAdmissionCode::FLOAT_NARROWING:
        return "float-narrowing";
    }
    return "unknown";
}

JBeamHydroRuntimePlan::JBeamHydroRuntimePlan()
    : code(JBeamHydroRuntimePlanCode::ADVANCED_ADMISSION_REJECTED)
    , source_hydro_index(0U)
    , node1_source_index(0U)
    , node2_source_index(0U)
    , geometric_length(0.0)
    , initial_rest_length(0.0)
{
}

bool JBeamHydroRuntimePlan::IsAdmitted() const
{
    return code == JBeamHydroRuntimePlanCode::ADMITTED;
}

namespace {

bool FindUniqueStructuralNode(
    const JBeamStructuralIR& structural,
    const std::string& id,
    std::size_t& output)
{
    bool found = false;
    output = 0U;
    for (std::size_t i = 0U; i < structural.nodes.size(); ++i)
    {
        if (structural.nodes[i].id == id)
        {
            if (found)
            {
                return false;
            }
            found = true;
            output = i;
        }
    }
    return found;
}

double AbsoluteRuntimeCoordinate(double value)
{
    return value < 0.0 ? -value : value;
}

bool ResolveStructuralNodeDistance(
    const JBeamStructuralNode& first,
    const JBeamStructuralNode& second,
    double& output)
{
    output = 0.0;
    if (!IsFiniteDouble(first.x) ||
        !IsFiniteDouble(first.y) ||
        !IsFiniteDouble(first.z) ||
        !IsFiniteDouble(second.x) ||
        !IsFiniteDouble(second.y) ||
        !IsFiniteDouble(second.z))
    {
        return false;
    }
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    const double dz = first.z - second.z;
    if (!IsFiniteDouble(dx) ||
        !IsFiniteDouble(dy) ||
        !IsFiniteDouble(dz))
    {
        return false;
    }
    const double maximum = std::max(
        AbsoluteRuntimeCoordinate(dx),
        std::max(
            AbsoluteRuntimeCoordinate(dy),
            AbsoluteRuntimeCoordinate(dz)));
    if (!IsFiniteDouble(maximum) || !(maximum > 0.0))
    {
        return false;
    }
    const double sx = dx / maximum;
    const double sy = dy / maximum;
    const double sz = dz / maximum;
    const double squared = sx * sx + sy * sy + sz * sz;
    if (!IsFiniteDouble(squared) || !(squared > 0.0))
    {
        return false;
    }
    const double unit_length = std::sqrt(squared);
    const double length = maximum * unit_length;
    if (!IsFiniteDouble(length) || !(length > 0.0))
    {
        return false;
    }
    output = length;
    return true;
}

} // namespace

JBeamHydroRuntimePlan BuildJBeamHydroRuntimePlan(
    const JBeamResolvedGraph& graph,
    std::size_t hydro_index,
    const JBeamAdvancedLimits& advanced_limits,
    const JBeamStructuralLimits& structural_limits)
{
    JBeamHydroRuntimePlan result;
    result.source_hydro_index = hydro_index;
    const JBeamAdvancedStructureIR advanced =
        BuildJBeamAdvancedStructureIR(graph, advanced_limits);
    result.properties =
        AdmitJBeamHydroBeamProperties(advanced, hydro_index);
    if (!result.properties.IsAdmitted())
    {
        result.code =
            JBeamHydroRuntimePlanCode::ADVANCED_ADMISSION_REJECTED;
        return result;
    }

    const JBeamStructuralIR structural =
        BuildJBeamStructuralIR(graph, structural_limits);
    if (!structural.IsValid())
    {
        result.code = JBeamHydroRuntimePlanCode::INVALID_STRUCTURAL_IR;
        return result;
    }
    if (structural.nodes.size() > 65535U)
    {
        result.code = JBeamHydroRuntimePlanCode::STRUCTURAL_NODE_LIMIT;
        return result;
    }

    const JBeamHydroActuatorAdmission& actuator =
        result.properties.actuator;
    if (actuator.input_source != "steering_input")
    {
        result.code = JBeamHydroRuntimePlanCode::UNSUPPORTED_INPUT_SOURCE;
        return result;
    }
    if (!FindUniqueStructuralNode(
            structural, actuator.node1, result.node1_source_index) ||
        !FindUniqueStructuralNode(
            structural, actuator.node2, result.node2_source_index))
    {
        result.code =
            JBeamHydroRuntimePlanCode::STRUCTURAL_NODE_NOT_UNIQUE;
        return result;
    }
    if (result.node1_source_index > 65534U ||
        result.node2_source_index > 65534U)
    {
        result.code = JBeamHydroRuntimePlanCode::STRUCTURAL_NODE_LIMIT;
        return result;
    }
    if (!ResolveStructuralNodeDistance(
            structural.nodes[result.node1_source_index],
            structural.nodes[result.node2_source_index],
            result.geometric_length))
    {
        result.code = JBeamHydroRuntimePlanCode::DEGENERATE_GEOMETRY;
        return result;
    }

    result.initial_rest_length = result.geometric_length *
        static_cast<double>(result.properties.beam.precompression);
    if (!IsFiniteDouble(result.initial_rest_length) ||
        !(result.initial_rest_length > 0.0))
    {
        result.code = JBeamHydroRuntimePlanCode::DEGENERATE_GEOMETRY;
        return result;
    }

    result.runtime_config.response = actuator.config;
    result.runtime_config.input_route =
        JBeamHydroInputRoute::STEERING_INPUT;
    result.runtime_config.has_steering_wheel_lock =
        actuator.has_steering_wheel_lock;
    result.runtime_config.steering_wheel_lock =
        actuator.steering_wheel_lock;
    result.initialized_runtime = InitializeJBeamHydroRuntime(
        result.runtime_config, result.initial_rest_length);
    if (!result.initialized_runtime.valid)
    {
        result.code =
            JBeamHydroRuntimePlanCode::RUNTIME_INITIALIZATION_REJECTED;
        return result;
    }

    result.code = JBeamHydroRuntimePlanCode::ADMITTED;
    return result;
}

const char* JBeamHydroRuntimePlanCodeToString(
    JBeamHydroRuntimePlanCode code)
{
    switch (code)
    {
    case JBeamHydroRuntimePlanCode::ADMITTED:
        return "admitted";
    case JBeamHydroRuntimePlanCode::ADVANCED_ADMISSION_REJECTED:
        return "advanced-admission-rejected";
    case JBeamHydroRuntimePlanCode::INVALID_STRUCTURAL_IR:
        return "invalid-structural-ir";
    case JBeamHydroRuntimePlanCode::UNSUPPORTED_INPUT_SOURCE:
        return "unsupported-input-source";
    case JBeamHydroRuntimePlanCode::STRUCTURAL_NODE_NOT_UNIQUE:
        return "structural-node-not-unique";
    case JBeamHydroRuntimePlanCode::STRUCTURAL_NODE_LIMIT:
        return "structural-node-limit";
    case JBeamHydroRuntimePlanCode::DEGENERATE_GEOMETRY:
        return "degenerate-geometry";
    case JBeamHydroRuntimePlanCode::RUNTIME_INITIALIZATION_REJECTED:
        return "runtime-initialization-rejected";
    }
    return "unknown";
}

JBeamAdvancedStructureIR BuildJBeamAdvancedStructureIR(
    const JBeamResolvedGraph& graph,
    const JBeamAdvancedLimits& limits)
{
    return AdvancedBuilder(graph, limits).Run();
}

std::string SerializeCanonicalJBeamAdvancedStructureIR(
    const JBeamAdvancedStructureIR& ir)
{
    CanonicalWriter writer(
        ir.canonical_output_byte_limit,
        ir.canonical_work_unit_limit);
    const std::size_t depth_limit =
        std::min(
            ir.canonical_value_depth_limit,
            HARD_MAX_VALUE_DEPTH);
    writer.String("ror-jbeam-advanced-structure-ir-v1");
    writer.String(ir.documentation_profile_id);
    writer.Size(ir.authored_entry_count);
    writer.Size(ir.node_coordinate_row_count);
    writer.Size(ir.effective_field_count);
    writer.Size(ir.retained_byte_count);
    writer.Size(ir.work_unit_count);
    writer.Size(ir.preserved_value_work_unit_count);

    writer.Size(ir.parts.size());
    for (std::size_t i = 0U;
         i < ir.parts.size() && writer.Ok();
         ++i)
    {
        writer.Boolean(static_cast<bool>(ir.parts[i]));
        if (ir.parts[i])
        {
            writer.Size(ir.parts[i]->part_preorder_index);
            writer.String(ir.parts[i]->part_name);
            writer.String(ir.parts[i]->package_path);
        }
    }

    writer.Size(ir.source_records.size());
    for (std::size_t i = 0U;
         i < ir.source_records.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedSourceRecord& record =
            ir.source_records[i];
        writer.Byte(static_cast<unsigned char>(record.kind));
        writer.Size(record.section_occurrence);
        WriteProvenance(writer, record.provenance);
        if (!writer.OptionalValue(record.raw_value, depth_limit))
        {
            return std::string();
        }
    }

    writer.Size(ir.modifiers.size());
    for (std::size_t i = 0U;
         i < ir.modifiers.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedModifier& modifier = ir.modifiers[i];
        writer.Byte(static_cast<unsigned char>(modifier.section_kind));
        writer.Size(modifier.source_record_index);
        writer.Size(modifier.entry_index);
        WriteProvenance(writer, modifier.provenance);
        if (!writer.OptionalValue(modifier.raw_value, depth_limit))
        {
            return std::string();
        }
    }

    writer.Size(ir.hydros.size());
    for (std::size_t i = 0U; i < ir.hydros.size() && writer.Ok(); ++i)
    {
        const JBeamAdvancedHydro& value = ir.hydros[i];
        if (!WriteEntry(writer, value.entry, depth_limit))
        {
            return std::string();
        }
        writer.String(value.node1);
        writer.String(value.node2);
        writer.String(value.input_source);
        writer.Boolean(value.has_factor);
        writer.Double(value.factor);
        writer.Double(value.out_limit);
        writer.Double(value.in_limit);
        writer.Double(value.input_factor);
        writer.Double(value.input_center);
        writer.Double(value.in_rate);
        writer.Double(value.out_rate);
        writer.Double(value.auto_center_rate);
        writer.Boolean(value.has_steering_wheel_lock);
        writer.Double(value.steering_wheel_lock);
        writer.Double(value.input_in_limit);
        writer.Double(value.input_out_limit);
    }

    writer.Size(ir.rails.size());
    for (std::size_t i = 0U; i < ir.rails.size() && writer.Ok(); ++i)
    {
        const JBeamAdvancedRail& value = ir.rails[i];
        if (!WriteEntry(writer, value.entry, depth_limit))
        {
            return std::string();
        }
        writer.String(value.name);
        writer.Size(value.links.size());
        for (std::size_t j = 0U; j < value.links.size(); ++j)
        {
            writer.String(value.links[j]);
        }
        writer.Boolean(value.looped);
        writer.Boolean(value.capped);
        writer.Boolean(value.has_legacy_broken_links);
    }

    writer.Size(ir.slidenodes.size());
    for (std::size_t i = 0U;
         i < ir.slidenodes.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedSlideNode& value = ir.slidenodes[i];
        if (!WriteEntry(writer, value.entry, depth_limit))
        {
            return std::string();
        }
        writer.String(value.node);
        writer.String(value.rail_name);
        writer.Boolean(value.attached);
        writer.Boolean(value.fix_to_rail);
        writer.Boolean(value.has_tolerance);
        writer.Double(value.tolerance);
        writer.Boolean(value.has_spring);
        writer.Double(value.spring);
        writer.Boolean(value.has_strength);
        writer.Boolean(value.strength_is_flt_max);
        writer.Double(value.strength);
        writer.Boolean(value.has_cap_strength);
        writer.Boolean(value.cap_strength_is_flt_max);
        writer.Double(value.cap_strength);
    }

    writer.Size(ir.thrusters.size());
    for (std::size_t i = 0U;
         i < ir.thrusters.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedThruster& value = ir.thrusters[i];
        if (!WriteEntry(writer, value.entry, depth_limit))
        {
            return std::string();
        }
        writer.String(value.direction_node);
        writer.String(value.force_node);
        writer.Double(value.factor);
        writer.Boolean(value.thrust_limit_is_flt_max);
        writer.Double(value.thrust_limit);
        writer.String(value.control);
    }

    writer.Size(ir.torsionbars.size());
    for (std::size_t i = 0U;
         i < ir.torsionbars.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedTorsionBar& value = ir.torsionbars[i];
        if (!WriteEntry(writer, value.entry, depth_limit))
        {
            return std::string();
        }
        writer.String(value.lever1_node);
        writer.String(value.axis1_node);
        writer.String(value.axis2_node);
        writer.String(value.lever2_node);
        writer.Boolean(value.has_spring);
        writer.Double(value.spring);
        writer.Boolean(value.has_damp);
        writer.Double(value.damp);
        writer.Boolean(value.has_spring2);
        writer.Double(value.spring2);
        writer.Boolean(value.has_damp2);
        writer.Double(value.damp2);
        writer.Boolean(value.anisotropic);
        writer.Boolean(value.has_deform);
        writer.Double(value.deform);
        writer.Boolean(value.has_strength);
        writer.Double(value.strength);
        writer.Double(value.precompression_angle);
        writer.Double(value.precompression_time);
        writer.String(value.name);
    }

    writer.Size(ir.rejected_entries.size());
    for (std::size_t i = 0U;
         i < ir.rejected_entries.size() && writer.Ok();
         ++i)
    {
        writer.Byte(static_cast<unsigned char>(
            ir.rejected_entries[i].section_kind));
        if (!WriteEntry(
                writer,
                ir.rejected_entries[i].entry,
                depth_limit))
        {
            return std::string();
        }
    }

    writer.Size(ir.diagnostics.size());
    for (std::size_t i = 0U;
         i < ir.diagnostics.size() && writer.Ok();
         ++i)
    {
        const JBeamAdvancedDiagnostic& diagnostic = ir.diagnostics[i];
        writer.Byte(static_cast<unsigned char>(diagnostic.code));
        writer.Byte(static_cast<unsigned char>(diagnostic.severity));
        WriteProvenance(writer, diagnostic.provenance);
        writer.Byte(static_cast<unsigned char>(diagnostic.section_kind));
        writer.Size(diagnostic.source_record_index);
        writer.Size(diagnostic.entry_index);
        writer.String(diagnostic.field_name);
        writer.String(diagnostic.detail);
    }
    return writer.Ok() ? writer.Data() : std::string();
}

const char* JBeamAdvancedDiagnosticCodeToString(
    JBeamAdvancedDiagnosticCode code)
{
    switch (code)
    {
    case JBeamAdvancedDiagnosticCode::INVALID_RESOLVED_GRAPH:
        return "invalid-resolved-graph";
    case JBeamAdvancedDiagnosticCode::RESOLVED_PART_LIMIT:
        return "resolved-part-limit";
    case JBeamAdvancedDiagnosticCode::RESOLVED_GRAPH_DEPTH_LIMIT:
        return "resolved-graph-depth-limit";
    case JBeamAdvancedDiagnosticCode::RESOLVED_GRAPH_CYCLE:
        return "resolved-graph-cycle";
    case JBeamAdvancedDiagnosticCode::PART_BODY_NOT_OBJECT:
        return "part-body-not-object";
    case JBeamAdvancedDiagnosticCode::SOURCE_RECORD_LIMIT:
        return "source-record-limit";
    case JBeamAdvancedDiagnosticCode::ENTRY_LIMIT:
        return "entry-limit";
    case JBeamAdvancedDiagnosticCode::MODIFIER_LIMIT:
        return "modifier-limit";
    case JBeamAdvancedDiagnosticCode::EFFECTIVE_FIELD_LIMIT:
        return "effective-field-limit";
    case JBeamAdvancedDiagnosticCode::NODE_COORDINATE_LIMIT:
        return "node-coordinate-limit";
    case JBeamAdvancedDiagnosticCode::WORK_LIMIT:
        return "work-limit";
    case JBeamAdvancedDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamAdvancedDiagnosticCode::RETAINED_BYTE_LIMIT:
        return "retained-byte-limit";
    case JBeamAdvancedDiagnosticCode::PRESERVED_VALUE_LIMIT:
        return "preserved-value-limit";
    case JBeamAdvancedDiagnosticCode::DUPLICATE_SECTION:
        return "duplicate-section";
    case JBeamAdvancedDiagnosticCode::INVALID_SECTION:
        return "invalid-section";
    case JBeamAdvancedDiagnosticCode::INVALID_TABLE_HEADER:
        return "invalid-table-header";
    case JBeamAdvancedDiagnosticCode::DUPLICATE_TABLE_HEADER:
        return "duplicate-table-header";
    case JBeamAdvancedDiagnosticCode::INVALID_TABLE_ENTRY:
        return "invalid-table-entry";
    case JBeamAdvancedDiagnosticCode::
        EXTRA_POSITIONAL_VALUE_PRESERVED:
        return "extra-positional-value-preserved";
    case JBeamAdvancedDiagnosticCode::MISSING_REQUIRED_FIELD:
        return "missing-required-field";
    case JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE:
        return "invalid-field-type";
    case JBeamAdvancedDiagnosticCode::EXPRESSION_DISABLED:
        return "expression-disabled";
    case JBeamAdvancedDiagnosticCode::NON_FINITE_NUMBER:
        return "non-finite-number";
    case JBeamAdvancedDiagnosticCode::MISSING_NODE_REFERENCE:
        return "missing-node-reference";
    case JBeamAdvancedDiagnosticCode::DUPLICATE_NODE_REFERENCE:
        return "duplicate-node-reference";
    case JBeamAdvancedDiagnosticCode::DEGENERATE_NODE_GEOMETRY:
        return "degenerate-node-geometry";
    case JBeamAdvancedDiagnosticCode::INVALID_RAIL_NAME:
        return "invalid-rail-name";
    case JBeamAdvancedDiagnosticCode::DUPLICATE_RAIL_NAME:
        return "duplicate-rail-name";
    case JBeamAdvancedDiagnosticCode::INVALID_RAIL_LINKS:
        return "invalid-rail-links";
    case JBeamAdvancedDiagnosticCode::MISSING_RAIL_REFERENCE:
        return "missing-rail-reference";
    case JBeamAdvancedDiagnosticCode::UNKNOWN_FIELD_PRESERVED:
        return "unknown-field-preserved";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
