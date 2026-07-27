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

#include "JBeamPressureWheelIR.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace RoR {
namespace BeamNG {
namespace {

const std::size_t RECOMMENDED_MIN_NUM_RAYS = 10U;
const std::size_t RECOMMENDED_MAX_NUM_RAYS = 20U;
const std::size_t FUTURE_ROR_LOWERING_MAX_WHEELS = 64U;

const std::size_t HARD_MAX_PARTS = 4096U;
const std::size_t HARD_MAX_ENTRIES = 65536U;
const std::size_t HARD_MAX_INVENTORY_WHEELS = 4096U;
const std::size_t HARD_MAX_SOURCE_RECORDS = 4096U;
const std::size_t HARD_MAX_EFFECTIVE_FIELDS = 65536U;
const std::size_t HARD_MAX_DIAGNOSTICS = 4096U;
const std::size_t HARD_MAX_RETAINED_BYTES =
    64U * 1024U * 1024U;
const std::size_t HARD_MAX_PRESERVED_VALUE_WORK_UNITS =
    1000000U;
const std::size_t HARD_MAX_PRESERVED_VALUE_DEPTH = 64U;
const std::size_t HARD_MAX_APPROXIMATION_GENERATED_NODES =
    HARD_MAX_INVENTORY_WHEELS *
    RECOMMENDED_MAX_NUM_RAYS * 4U;
const std::size_t HARD_MAX_APPROXIMATION_GENERATED_BEAMS =
    HARD_MAX_INVENTORY_WHEELS *
    RECOMMENDED_MAX_NUM_RAYS * 25U;
const std::size_t HARD_MAX_CANONICAL_OUTPUT_BYTES =
    64U * 1024U * 1024U;
const std::size_t HARD_MAX_NUM_RAYS_PER_WHEEL =
    HARD_MAX_APPROXIMATION_GENERATED_NODES / 4U;

static_assert(
    std::numeric_limits<double>::is_iec559,
    "JBeam pressure-wheel inventory requires IEC 559 doubles");
static_assert(
    std::numeric_limits<double>::digits == 53,
    "JBeam pressure-wheel inventory requires binary64 precision");

JBeamPressureWheelLimits SanitizeLimits(
    const JBeamPressureWheelLimits& requested)
{
    JBeamPressureWheelLimits result = requested;
    result.max_parts =
        std::min(result.max_parts, HARD_MAX_PARTS);
    result.max_entries =
        std::min(result.max_entries, HARD_MAX_ENTRIES);
    result.max_wheels =
        std::min(
            result.max_wheels,
            HARD_MAX_INVENTORY_WHEELS);
    result.max_source_records =
        std::min(
            result.max_source_records,
            HARD_MAX_SOURCE_RECORDS);
    result.max_effective_fields =
        std::min(
            result.max_effective_fields,
            HARD_MAX_EFFECTIVE_FIELDS);
    result.max_diagnostics =
        std::min(
            result.max_diagnostics,
            HARD_MAX_DIAGNOSTICS);
    result.max_retained_bytes =
        std::min(
            result.max_retained_bytes,
            HARD_MAX_RETAINED_BYTES);
    result.max_preserved_value_work_units =
        std::min(
            result.max_preserved_value_work_units,
            HARD_MAX_PRESERVED_VALUE_WORK_UNITS);
    result.max_preserved_value_depth =
        std::min(
            result.max_preserved_value_depth,
            HARD_MAX_PRESERVED_VALUE_DEPTH);
    result.max_approximation_generated_nodes =
        std::min(
            result.max_approximation_generated_nodes,
            HARD_MAX_APPROXIMATION_GENERATED_NODES);
    result.max_approximation_generated_beams =
        std::min(
            result.max_approximation_generated_beams,
            HARD_MAX_APPROXIMATION_GENERATED_BEAMS);
    result.max_canonical_output_bytes =
        std::min(
            result.max_canonical_output_bytes,
            HARD_MAX_CANONICAL_OUTPUT_BYTES);
    return result;
}

bool AddSize(std::size_t value, std::size_t& total)
{
    if (value > std::numeric_limits<std::size_t>::max() - total)
    {
        return false;
    }
    total += value;
    return true;
}

bool MultiplySize(
    std::size_t left,
    std::size_t right,
    std::size_t& result)
{
    if (left != 0U &&
        right > std::numeric_limits<std::size_t>::max() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

bool IsFiniteDouble(double value)
{
    // std::isfinite may be optimized to true by -ffast-math. Inspect the
    // binary64 exponent without floating-point predicates instead.
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "JBeam pressure-wheel inventory requires binary64 doubles");
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

bool IsExpression(const JBeamValue& value)
{
    return value.type == JBeamValueType::STRING &&
        !value.scalar_text.empty() &&
        value.scalar_text[0] == '$';
}

bool IsValidNumRaysValue(
    const JBeamValue* value,
    std::size_t& rays)
{
    if (value == NULL ||
        value->type != JBeamValueType::NUMBER ||
        !IsFiniteDouble(value->number_value))
    {
        return false;
    }
    if (!(value->number_value > 0.0) ||
        value->number_value >
            static_cast<double>(
                HARD_MAX_NUM_RAYS_PER_WHEEL))
    {
        return false;
    }
    const std::size_t candidate =
        static_cast<std::size_t>(value->number_value);
    if (static_cast<double>(candidate) !=
            value->number_value ||
        candidate % 2U != 0U)
    {
        return false;
    }
    rays = candidate;
    return true;
}

bool IsLuaSection(const std::string& name)
{
    for (std::size_t i = 0U;
         i < name.size();
         ++i)
    {
        if (name.size() - i < 3U)
        {
            return false;
        }
        const unsigned char first =
            static_cast<unsigned char>(name[i]);
        const unsigned char second =
            static_cast<unsigned char>(name[i + 1U]);
        const unsigned char third =
            static_cast<unsigned char>(name[i + 2U]);
        if (std::tolower(first) == 'l' &&
            std::tolower(second) == 'u' &&
            std::tolower(third) == 'a')
        {
            return true;
        }
    }
    return false;
}

bool IsControllerOrPowertrainSection(const std::string& name)
{
    return name == "controller" ||
        name == "vehicleController" ||
        name == "powertrain" ||
        name == "electrics" ||
        name == "mainEngine";
}

bool IsResolverMetadataSection(const std::string& name)
{
    return name == "slotType" ||
        name == "slots" ||
        name == "slots2";
}

bool IsScalingProcessModifier(const std::string& name)
{
    // BeamNG's documented scaling-process syntax is "scale" followed by the
    // exact modifier name, for example scaledragCoef. Preserve every
    // non-empty target in source order; this inventory deliberately does not
    // evaluate or multiply the authored value.
    static const std::string prefix("scale");
    return name.size() > prefix.size() &&
        name.compare(0U, prefix.size(), prefix) == 0;
}

bool IsCoreField(const std::string& name)
{
    return name == "name" ||
        name == "hubGroup" ||
        name == "group" ||
        name == "node1:" ||
        name == "node1" ||
        name == "node2:" ||
        name == "node2" ||
        name == "nodeS" ||
        name == "nodeArm:" ||
        name == "nodeArm" ||
        name == "wheelDir" ||
        name == "radius" ||
        name == "hubRadius" ||
        name == "wheelOffset" ||
        name == "tireWidth" ||
        name == "hubWidth" ||
        name == "hasTire" ||
        name == "numRays";
}

bool HasBeamFamilySuffix(
    const std::string& name,
    const char* prefix_text,
    const char* const* suffixes,
    std::size_t suffix_count)
{
    const std::string prefix(prefix_text);
    if (name.compare(0U, prefix.size(), prefix) != 0)
    {
        return false;
    }
    const std::string suffix = name.substr(prefix.size());
    for (std::size_t i = 0U; i < suffix_count; ++i)
    {
        if (suffix == suffixes[i])
        {
            return true;
        }
    }
    return false;
}

bool HasDocumentedBeamFamily(
    const std::string& name)
{
    static const char* const common_prefixes[] = {
        "hubBeam",
        "hubTreadBeam",
        "hubPeripheryBeam",
        "hubSideBeam",
        "hubReinfBeam",
        "wheelSideBeam",
        "wheelSideReinfBeam",
        "wheelReinfBeam",
        "wheelTreadBeam",
        "wheelTreadReinfBeam",
        "wheelPeripheryBeam",
        "wheelPeripheryReinfBeam",
        "tireSupportBeam",
        "hubcapBeam",
        "hubcapAttachBeam",
        "hubcapSupportBeam"
    };
    static const char* const common_suffixes[] = {
        "Spring",
        "Damp",
        "Deform",
        "Strength"
    };
    static const char* const damp_cutoff_prefixes[] = {
        "wheelSideBeam",
        "wheelSideReinfBeam",
        "wheelReinfBeam",
        "wheelTreadBeam",
        "wheelTreadReinfBeam",
        "wheelPeripheryBeam",
        "wheelPeripheryReinfBeam"
    };
    static const char* const damp_cutoff_suffixes[] = {
        "DampCutoffHz"
    };
    static const char* const precompression_prefixes[] = {
        "wheelSideBeam",
        "wheelReinfBeam",
        "wheelTreadBeam",
        "wheelTreadReinfBeam",
        "wheelPeripheryBeam",
        "wheelPeripheryReinfBeam"
    };
    static const char* const precompression_suffixes[] = {
        "Precompression"
    };
    static const char* const expansion_prefixes[] = {
        "wheelSideBeam",
        "wheelSideReinfBeam"
    };
    static const char* const expansion_suffixes[] = {
        "SpringExpansion",
        "DampExpansion"
    };
    static const char* const tire_support_suffixes[] = {
        "SidewallRatio",
        "LongExtent"
    };

    for (std::size_t i = 0U;
         i < sizeof(common_prefixes) /
             sizeof(common_prefixes[0]);
         ++i)
    {
        if (HasBeamFamilySuffix(
                name,
                common_prefixes[i],
                common_suffixes,
                sizeof(common_suffixes) /
                    sizeof(common_suffixes[0])))
        {
            return true;
        }
    }
    for (std::size_t i = 0U;
         i < sizeof(damp_cutoff_prefixes) /
             sizeof(damp_cutoff_prefixes[0]);
         ++i)
    {
        if (HasBeamFamilySuffix(
                name,
                damp_cutoff_prefixes[i],
                damp_cutoff_suffixes,
                sizeof(damp_cutoff_suffixes) /
                    sizeof(damp_cutoff_suffixes[0])))
        {
            return true;
        }
    }
    for (std::size_t i = 0U;
         i < sizeof(precompression_prefixes) /
             sizeof(precompression_prefixes[0]);
         ++i)
    {
        if (HasBeamFamilySuffix(
                name,
                precompression_prefixes[i],
                precompression_suffixes,
                sizeof(precompression_suffixes) /
                    sizeof(precompression_suffixes[0])))
        {
            return true;
        }
    }
    for (std::size_t i = 0U;
         i < sizeof(expansion_prefixes) /
             sizeof(expansion_prefixes[0]);
         ++i)
    {
        if (HasBeamFamilySuffix(
                name,
                expansion_prefixes[i],
                expansion_suffixes,
                sizeof(expansion_suffixes) /
                    sizeof(expansion_suffixes[0])))
        {
            return true;
        }
    }
    return HasBeamFamilySuffix(
        name,
        "tireSupportBeam",
        tire_support_suffixes,
        sizeof(tire_support_suffixes) /
            sizeof(tire_support_suffixes[0]));
}

bool IsDocumentedWheelField(const std::string& name)
{
    static const char* const fields[] = {
        "speedo",
        "nodeCoupling",
        "torqueCoupling",
        "torqueCoupling:",
        "torqueArm",
        "torqueArm:",
        "torqueArm2",
        "torqueArm2:",
        "steerAxisUp",
        "steerAxisUp:",
        "steerAxisDown",
        "steerAxisDown:",
        "torqueJointNode1",
        "torqueJointNode1:",
        "torqueJointNode2",
        "torqueJointNode2:",
        "axleBeams",
        "disableMeshBreaking",
        "disableHubMeshBreaking",
        "propulsed",
        "selfCollision",
        "collision",
        "offsetFromNode",
        "nodeWeight",
        "hubNodeWeight",
        "hubWeight",
        "tireWeight",
        "hubWeightGainRatio",
        "hubFrictionCoef",
        "hubNodeMaterial",
        "frictionCoef",
        "slidingFrictionCoef",
        "pressurePSI",
        "maxPressurePSI",
        "stribeckExponent",
        "stribeckVelMult",
        "treadCoef",
        "noLoadCoef",
        "loadSensitivitySlope",
        "fullLoadCoef",
        "nodeMaterial",
        "softnessCoef",
        "enableTireReinfBeams",
        "enableTireLbeams",
        "enableTireSideReinfBeams",
        "enableTreadReinfBeams",
        "enableTirePeripheryReinfBeams",
        "enableTireSupportBeams",
        "wheelSideTransitionZone",
        "triangleCollision",
        "treadTriangleCollision",
        "side1TriangleCollision",
        "side2TriangleCollision",
        "hubTriangleCollision",
        "hubSide1TriangleCollision",
        "hubSide2TriangleCollision",
        "disableTriangleBreaking",
        "dragCoef",
        "skinDragCoef",
        "enableHubcaps",
        "hubcapBreakGroup",
        "hubcapGroup",
        "hubcapCollision",
        "hubcapSelfCollision",
        "enableExtraHubcapBeams",
        "hubcapOffset",
        "hubcapWidth",
        "hubcapRadius",
        "hubcapNodeWeight",
        "hubcapCenterNodeWeight",
        "hubcapNodeMaterial",
        "hubcapFrictionCoef",
        "brakeTorque",
        "parkingTorque",
        "brakeSpring",
        "enableBrakeThermals",
        "brakeDiameter",
        "brakeMass",
        "brakeType",
        "rotorMaterial",
        "padMaterial",
        "brakeInputSplit",
        "brakeSplitCoef",
        "squealCoefNatural",
        "squealCoefLowSpeed",
        "squealCoefGlazing",
        "enableABS",
        "absSlipRatioTarget",
        "absHz",
        "brakePressureInDelay",
        "brakePressureOutDelay",
        "brakeVentingCoef",
        "hubRadiusSimple"
    };
    for (std::size_t i = 0U;
         i < sizeof(fields) / sizeof(fields[0]);
         ++i)
    {
        if (name == fields[i])
        {
            return true;
        }
    }
    return HasDocumentedBeamFamily(name);
}

bool IsDocumentedJBeamModifierField(
    const std::string& name)
{
    return name == "disable";
}

bool IsDocumentedAmbiguousWheelField(
    const std::string& name)
{
    return name == "wheelDir" ||
        name == "nodeS" ||
        name == "speedo" ||
        name == "axleBeams" ||
        name == "enableABS" ||
        name == "hubcapNodeMaterial" ||
        name == "tireWeight";
}

bool IsError(const JBeamPressureWheelDiagnostic& diagnostic)
{
    return diagnostic.severity ==
        JBeamPressureWheelSeverity::ERROR;
}

bool IsDiagnosticLimit(
    const JBeamPressureWheelDiagnostic& diagnostic)
{
    return diagnostic.code ==
        JBeamPressureWheelDiagnosticCode::DIAGNOSTIC_LIMIT;
}

JBeamPressureWheelDiagnostic MakeDiagnostic(
    JBeamPressureWheelDiagnosticCode code,
    JBeamPressureWheelSeverity severity,
    const JBeamPressureWheelProvenance& provenance,
    const std::string& section,
    std::size_t row_index,
    const std::string& field_name,
    const std::string& detail)
{
    JBeamPressureWheelDiagnostic result;
    result.code = code;
    result.severity = severity;
    result.provenance = provenance;
    result.section = section;
    result.row_index = row_index;
    result.field_name = field_name;
    result.detail = detail;
    return result;
}

JBeamPressureWheelDiagnostic MakeDiagnosticLimit()
{
    return MakeDiagnostic(
        JBeamPressureWheelDiagnosticCode::DIAGNOSTIC_LIMIT,
        JBeamPressureWheelSeverity::ERROR,
        JBeamPressureWheelProvenance(),
        std::string(),
        0U,
        std::string(),
        "Additional diagnostics were deterministically suppressed");
}

bool DiagnosticLess(
    const JBeamPressureWheelDiagnostic& left,
    const JBeamPressureWheelDiagnostic& right)
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
    if (left.provenance.span.source_name !=
        right.provenance.span.source_name)
    {
        return left.provenance.span.source_name <
            right.provenance.span.source_name;
    }
    if (left.provenance.span.begin.byte_offset !=
        right.provenance.span.begin.byte_offset)
    {
        return left.provenance.span.begin.byte_offset <
            right.provenance.span.begin.byte_offset;
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

struct PartContext
{
    const JBeamResolvedPartNode* node;
    std::shared_ptr<const JBeamPressureWheelPartIdentity> identity;
    JBeamPressureWheelProvenance provenance;
};

struct SectionRef
{
    std::size_t part_index;
    const JBeamObjectField* field;
    std::size_t occurrence;
    JBeamPressureWheelSourceKind kind;
    bool duplicate_pressure_section;
};

struct AssignmentRef
{
    JBeamPressureWheelFieldOrigin origin;
    std::shared_ptr<const JBeamValue> value;
    JBeamSourceSpan span;
    std::size_t part_index;
};

struct PreflightAssignment
{
    const JBeamValue* value;
};

bool IsTableHeader(const JBeamValue& value)
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

std::size_t PositionalCellCount(
    const JBeamValue& row,
    std::size_t column_count)
{
    std::size_t count = row.array_values.size();
    if (count > column_count &&
        row.array_values[count - 1U].type ==
            JBeamValueType::OBJECT)
    {
        --count;
    }
    return count;
}

const JBeamValue* RowLocalOverrides(
    const JBeamValue& row,
    std::size_t column_count)
{
    if (row.array_values.size() > column_count &&
        row.array_values.back().type ==
            JBeamValueType::OBJECT)
    {
        return &row.array_values.back();
    }
    return NULL;
}

class PressureWheelBuilder
{
public:
    PressureWheelBuilder(
        const JBeamResolvedGraph& graph,
        const JBeamPressureWheelLimits& limits)
        : m_graph(graph)
        , m_limits(SanitizeLimits(limits))
        , m_stopped(false)
        , m_retained_bytes(0U)
        , m_work_units(0U)
        , m_preflight_wheels(0U)
        , m_preflight_fields(0U)
        , m_preflight_nodes(0U)
        , m_preflight_beams(0U)
        , m_has_inert_sources(false)
    {
        m_result.documentation_profile_id =
            GetJBeamPressureWheelDocumentationProfile().profile_id;
        m_result.runtime_policy =
            JBeamPressureWheelRuntimePolicy::
                INVENTORY_ONLY_NEVER_LOWER;
        m_result.canonical_output_byte_limit =
            m_limits.max_canonical_output_bytes;
        m_result.canonical_value_depth_limit =
            m_limits.max_preserved_value_depth;
    }

    JBeamPressureWheelIR Run()
    {
        if (!Charge(
                m_result.documentation_profile_id.size()))
        {
            Finish();
            return m_result;
        }
        if (!m_graph.IsValid() || !m_graph.root)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_RESOLVED_GRAPH,
                JBeamPressureWheelSeverity::ERROR,
                JBeamPressureWheelProvenance(),
                std::string(),
                0U,
                std::string(),
                "Pressure-wheel inventory requires a valid resolved graph");
            Finish();
            return m_result;
        }
        CollectParts();
        if (!m_stopped)
        {
            DiscoverSections();
        }
        if (!m_stopped)
        {
            Preflight();
        }
        if (!m_stopped)
        {
            m_result.source_records.reserve(m_sections.size());
            m_result.wheels.reserve(m_preflight_wheels);
            m_result.approximation_generated_node_count =
                m_preflight_nodes;
            m_result.approximation_generated_beam_count =
                m_preflight_beams;
            ProcessSections();
        }
        if (m_has_inert_sources)
        {
            for (std::size_t i = 0U;
                 i < m_result.wheels.size();
                 ++i)
            {
                m_result.wheels[i]
                    .has_inert_or_unimplemented_behavior = true;
            }
        }
        Finish();
        return m_result;
    }

private:
    enum class MeasureStatus
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
    };

    const JBeamResolvedGraph& m_graph;
    JBeamPressureWheelLimits m_limits;
    JBeamPressureWheelIR m_result;
    std::vector<PartContext> m_parts;
    std::vector<SectionRef> m_sections;
    bool m_stopped;
    std::size_t m_retained_bytes;
    std::size_t m_work_units;
    std::size_t m_preflight_wheels;
    std::size_t m_preflight_fields;
    std::size_t m_preflight_nodes;
    std::size_t m_preflight_beams;
    bool m_has_inert_sources;
    std::set<std::string>
        m_reported_documentation_ambiguities;

    void Finish()
    {
        m_result.retained_byte_count = m_retained_bytes;
        std::stable_sort(
            m_result.diagnostics.begin(),
            m_result.diagnostics.end(),
            DiagnosticLess);
    }

    void PushDirect(
        const JBeamPressureWheelDiagnostic& diagnostic)
    {
        if (!m_result.diagnostics.empty() &&
            IsDiagnosticLimit(m_result.diagnostics.back()))
        {
            m_stopped = true;
            return;
        }
        if (m_limits.max_diagnostics == 0U)
        {
            m_result.diagnostics.push_back(
                MakeDiagnosticLimit());
            m_stopped = true;
            return;
        }
        if (m_result.diagnostics.size() <
            m_limits.max_diagnostics)
        {
            m_result.diagnostics.push_back(diagnostic);
            return;
        }
        m_result.diagnostics[
            m_limits.max_diagnostics - 1U] =
                MakeDiagnosticLimit();
        m_stopped = true;
    }

    void StopRetainedLimit()
    {
        if (m_stopped)
        {
            return;
        }
        PushDirect(MakeDiagnostic(
            JBeamPressureWheelDiagnosticCode::
                RETAINED_BYTE_LIMIT,
            JBeamPressureWheelSeverity::ERROR,
            JBeamPressureWheelProvenance(),
            std::string(),
            0U,
            std::string(),
            "Pressure-wheel inventory exceeds its retained-byte budget"));
        m_stopped = true;
    }

    bool Charge(std::size_t bytes)
    {
        if (bytes >
            m_limits.max_retained_bytes -
                std::min(
                    m_retained_bytes,
                    m_limits.max_retained_bytes))
        {
            StopRetainedLimit();
            return false;
        }
        m_retained_bytes += bytes;
        return true;
    }

    void Push(
        JBeamPressureWheelDiagnosticCode code,
        JBeamPressureWheelSeverity severity,
        const JBeamPressureWheelProvenance& provenance,
        const std::string& section,
        std::size_t row_index,
        const std::string& field_name,
        const std::string& detail)
    {
        if (m_stopped)
        {
            return;
        }
        std::size_t bytes =
            sizeof(JBeamPressureWheelDiagnostic);
        if (!AddSize(section.size(), bytes) ||
            !AddSize(field_name.size(), bytes) ||
            !AddSize(detail.size(), bytes) ||
            !AddSize(
                provenance.span.source_name.size(),
                bytes) ||
            !Charge(bytes))
        {
            StopRetainedLimit();
            return;
        }
        PushDirect(MakeDiagnostic(
            code,
            severity,
            provenance,
            section,
            row_index,
            field_name,
            detail));
    }

    std::size_t RemainingWork() const
    {
        return m_limits.max_preserved_value_work_units -
            std::min(
                m_work_units,
                m_limits.max_preserved_value_work_units);
    }

    bool TryChargeWork(std::size_t units)
    {
        if (units > RemainingWork())
        {
            return false;
        }
        m_work_units += units;
        return true;
    }

    bool ChargeWork(
        std::size_t units,
        const JBeamPressureWheelProvenance& provenance)
    {
        if (!TryChargeWork(units))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    PRESERVED_VALUE_LIMIT,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                0U,
                std::string(),
                "Pressure-wheel inventory exceeds its aggregate work budget");
            m_stopped = true;
            return false;
        }
        return true;
    }

    bool ConsumePreflight(
        std::size_t units,
        const JBeamPressureWheelProvenance& provenance)
    {
        return ChargeWork(units, provenance);
    }

    JBeamPressureWheelProvenance Provenance(
        std::size_t part_index,
        const JBeamSourceSpan& span) const
    {
        JBeamPressureWheelProvenance result;
        if (part_index < m_parts.size())
        {
            result.part = m_parts[part_index].identity;
        }
        result.span = span;
        return result;
    }

    void CollectParts()
    {
        std::vector<const JBeamResolvedPartNode*> pending;
        std::set<const JBeamResolvedPartNode*> visited;
        pending.push_back(m_graph.root.get());
        while (!pending.empty() && !m_stopped)
        {
            const JBeamResolvedPartNode* node = pending.back();
            pending.pop_back();
            if (node == NULL)
            {
                continue;
            }
            if (!visited.insert(node).second)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        RESOLVED_GRAPH_CYCLE,
                    JBeamPressureWheelSeverity::ERROR,
                    JBeamPressureWheelProvenance(),
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved graph contains a cycle or aliased part node");
                m_stopped = true;
                return;
            }
            if (m_parts.size() >= m_limits.max_parts)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        RESOLVED_PART_LIMIT,
                    JBeamPressureWheelSeverity::ERROR,
                    JBeamPressureWheelProvenance(),
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved graph exceeds the configured part limit");
                m_stopped = true;
                return;
            }
            std::size_t identity_bytes =
                sizeof(JBeamPressureWheelPartIdentity);
            if (!AddSize(
                    node->definition.name.size(),
                    identity_bytes))
            {
                StopRetainedLimit();
                return;
            }
            if (!AddSize(
                    node->definition.package_path.size(),
                    identity_bytes) ||
                !AddSize(
                    node->definition.name_span.source_name.size(),
                    identity_bytes) ||
                !Charge(identity_bytes))
            {
                return;
            }
            std::shared_ptr<JBeamPressureWheelPartIdentity> identity(
                new JBeamPressureWheelPartIdentity());
            identity->part_preorder_index = m_parts.size();
            identity->part_name = node->definition.name;
            identity->package_path =
                node->definition.package_path;
            PartContext part;
            part.node = node;
            part.identity = identity;
            part.provenance.part = identity;
            part.provenance.span =
                node->definition.name_span;
            m_parts.push_back(part);
            m_result.parts.push_back(identity);

            if (!ConsumePreflight(
                    node->slots.size(),
                    part.provenance))
            {
                return;
            }
            std::size_t child_count = 0U;
            for (std::size_t i = 0U;
                 i < node->slots.size();
                 ++i)
            {
                if (node->slots[i].child)
                {
                    if (child_count ==
                        std::numeric_limits<std::size_t>::max())
                    {
                        Push(
                            JBeamPressureWheelDiagnosticCode::
                                RESOLVED_PART_LIMIT,
                            JBeamPressureWheelSeverity::ERROR,
                            part.provenance,
                            std::string(),
                            0U,
                            std::string(),
                            "Resolved child count overflows");
                        m_stopped = true;
                        return;
                    }
                    ++child_count;
                }
            }
            std::size_t outstanding = m_parts.size();
            if (!AddSize(pending.size(), outstanding) ||
                child_count >
                    m_limits.max_parts -
                        std::min(
                            outstanding,
                            m_limits.max_parts))
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        RESOLVED_PART_LIMIT,
                    JBeamPressureWheelSeverity::ERROR,
                    part.provenance,
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved graph exceeds the configured part limit");
                m_stopped = true;
                return;
            }
            for (std::size_t i = node->slots.size();
                 i > 0U;
                 --i)
            {
                if (node->slots[i - 1U].child)
                {
                    pending.push_back(
                        node->slots[i - 1U].child.get());
                }
            }
        }
    }

    void DiscoverSections()
    {
        for (std::size_t part_index = 0U;
             part_index < m_parts.size() && !m_stopped;
             ++part_index)
        {
            const JBeamValue& body =
                m_parts[part_index].node->definition.body;
            if (body.type != JBeamValueType::OBJECT)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        PART_BODY_NOT_OBJECT,
                    JBeamPressureWheelSeverity::ERROR,
                    m_parts[part_index].provenance,
                    std::string(),
                    0U,
                    std::string(),
                    "Resolved part body must be an object");
                continue;
            }

            if (!ConsumePreflight(
                    body.object_fields.size(),
                    m_parts[part_index].provenance))
            {
                return;
            }
            std::map<std::string, std::size_t> occurrences;
            std::vector<std::size_t> pressure_refs;
            for (std::size_t field_index = 0U;
                 field_index < body.object_fields.size() &&
                     !m_stopped;
                 ++field_index)
            {
                const JBeamObjectField& field =
                    body.object_fields[field_index];
                JBeamPressureWheelSourceKind kind =
                    JBeamPressureWheelSourceKind::
                        PRESSURE_WHEELS;
                bool relevant = false;
                if (field.key == "pressureWheels")
                {
                    relevant = true;
                    kind = JBeamPressureWheelSourceKind::
                        PRESSURE_WHEELS;
                }
                else if (IsControllerOrPowertrainSection(
                             field.key))
                {
                    relevant = true;
                    kind = JBeamPressureWheelSourceKind::
                        INERT_CONTROLLER_OR_POWERTRAIN;
                }
                else if (IsScalingProcessModifier(field.key))
                {
                    relevant = true;
                    kind = JBeamPressureWheelSourceKind::
                        INERT_SCALING_MODIFIER;
                }
                else if (IsLuaSection(field.key))
                {
                    relevant = true;
                    kind =
                        JBeamPressureWheelSourceKind::INERT_LUA;
                }
                else if (IsResolverMetadataSection(field.key))
                {
                    continue;
                }
                if (!relevant)
                {
                    continue;
                }
                const std::size_t remaining =
                    m_limits.max_retained_bytes -
                        std::min(
                            m_retained_bytes,
                            m_limits.max_retained_bytes);
                if (field.key.size() > remaining)
                {
                    StopRetainedLimit();
                    return;
                }
                const std::size_t occurrence =
                    occurrences[field.key]++;
                if (m_sections.size() >=
                    m_limits.max_source_records)
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            SOURCE_RECORD_LIMIT,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            part_index,
                            field.value
                                ? field.value->span
                                : field.key_span),
                        field.key,
                        0U,
                        field.key,
                        "Relevant source sections exceed the "
                        "configured record limit");
                    m_stopped = true;
                    return;
                }
                SectionRef section;
                section.part_index = part_index;
                section.field = &field;
                section.occurrence = occurrence;
                section.kind = kind;
                section.duplicate_pressure_section = false;
                m_sections.push_back(section);
                if (field.key == "pressureWheels")
                {
                    pressure_refs.push_back(
                        m_sections.size() - 1U);
                }
            }
            if (pressure_refs.size() > 1U)
            {
                for (std::size_t i = 0U;
                     i < pressure_refs.size();
                     ++i)
                {
                    m_sections[pressure_refs[i]]
                        .duplicate_pressure_section = true;
                }
            }
        }
    }

    void ApplyPreflightObject(
        const JBeamValue& object,
        std::map<std::string, PreflightAssignment>& fields,
        const JBeamPressureWheelProvenance& provenance)
    {
        if (!ConsumePreflight(
                object.object_fields.size(),
                provenance))
        {
            return;
        }
        for (std::size_t i = 0U;
             i < object.object_fields.size();
             ++i)
        {
            const JBeamObjectField& field =
                object.object_fields[i];
            if (!ConsumePreflight(
                    field.key.size(),
                    provenance))
            {
                return;
            }
            if (field.value)
            {
                PreflightAssignment assignment;
                assignment.value = field.value.get();
                fields[field.key] = assignment;
            }
        }
        if (fields.size() > m_limits.max_effective_fields)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    EFFECTIVE_FIELD_LIMIT,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                0U,
                std::string(),
                "Active defaults exceed the effective-field limit");
            m_stopped = true;
        }
    }

    void Preflight()
    {
        std::map<std::string, PreflightAssignment> defaults;
        const std::size_t wheel_limit =
            m_limits.max_wheels;
        for (std::size_t section_index = 0U;
             section_index < m_sections.size() && !m_stopped;
             ++section_index)
        {
            const SectionRef& section =
                m_sections[section_index];
            const JBeamObjectField& field = *section.field;
            const JBeamPressureWheelProvenance provenance =
                Provenance(
                    section.part_index,
                    field.value
                        ? field.value->span
                        : field.key_span);
            if (!ConsumePreflight(1U, provenance))
            {
                return;
            }
            if (section.kind !=
                JBeamPressureWheelSourceKind::PRESSURE_WHEELS)
            {
                continue;
            }
            if (!field.value || !IsTableHeader(*field.value))
            {
                continue;
            }
            const JBeamValue& table = *field.value;
            const JBeamValue& header = table.array_values[0];
            const std::size_t entry_count =
                table.array_values.size() - 1U;
            if (header.array_values.size() >
                m_limits.max_effective_fields)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        EFFECTIVE_FIELD_LIMIT,
                    JBeamPressureWheelSeverity::ERROR,
                    provenance,
                    "pressureWheels",
                    0U,
                    std::string(),
                    "pressureWheels header exceeds the "
                    "effective-field limit");
                m_stopped = true;
                return;
            }
            if (entry_count >
                m_limits.max_entries -
                    std::min(
                        m_result.authored_entry_count,
                        m_limits.max_entries))
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::ENTRY_LIMIT,
                    JBeamPressureWheelSeverity::ERROR,
                    provenance,
                    "pressureWheels",
                    0U,
                    std::string(),
                    "pressureWheels tables exceed the entry limit");
                m_stopped = true;
                return;
            }
            m_result.authored_entry_count += entry_count;
            std::size_t table_work =
                header.array_values.size();
            if (!AddSize(entry_count, table_work) ||
                !ConsumePreflight(table_work, provenance))
            {
                return;
            }
            std::vector<std::string> columns;
            columns.reserve(header.array_values.size());
            for (std::size_t i = 0U;
                 i < header.array_values.size();
                 ++i)
            {
                if (!ConsumePreflight(
                        header.array_values[i]
                            .scalar_text.size(),
                        provenance))
                {
                    return;
                }
                columns.push_back(
                    header.array_values[i].scalar_text);
            }

            for (std::size_t entry_index = 1U;
                 entry_index < table.array_values.size() &&
                     !m_stopped;
                 ++entry_index)
            {
                const JBeamValue& entry =
                    table.array_values[entry_index];
                if (entry.type == JBeamValueType::OBJECT)
                {
                    ApplyPreflightObject(
                        entry, defaults, provenance);
                    continue;
                }
                if (entry.type != JBeamValueType::ARRAY)
                {
                    continue;
                }
                if (m_preflight_wheels >= wheel_limit)
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            WHEEL_LIMIT,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        std::string(),
                        wheel_limit ==
                                HARD_MAX_INVENTORY_WHEELS
                            ? "pressureWheels exceeds the hard inventory "
                                "wheel limit"
                            : "pressureWheels exceeds the configured "
                                "inventory wheel limit");
                    m_stopped = true;
                    return;
                }
                ++m_preflight_wheels;
                std::map<std::string, PreflightAssignment> effective =
                    defaults;
                const std::size_t positional =
                    PositionalCellCount(entry, columns.size());
                const std::size_t mapped =
                    std::min(positional, columns.size());
                if (!ConsumePreflight(
                        mapped, provenance))
                {
                    return;
                }
                for (std::size_t i = 0U; i < mapped; ++i)
                {
                    PreflightAssignment assignment;
                    assignment.value = &entry.array_values[i];
                    effective[columns[i]] = assignment;
                }
                const JBeamValue* overrides =
                    RowLocalOverrides(entry, columns.size());
                if (overrides != NULL)
                {
                    ApplyPreflightObject(
                        *overrides, effective, provenance);
                }
                if (effective.size() >
                    m_limits.max_effective_fields -
                        std::min(
                            m_preflight_fields,
                            m_limits.max_effective_fields))
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            EFFECTIVE_FIELD_LIMIT,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        std::string(),
                        "Wheel fields exceed the aggregate "
                        "effective-field limit");
                    m_stopped = true;
                    return;
                }
                m_preflight_fields += effective.size();
                const std::map<
                    std::string,
                    PreflightAssignment>::const_iterator rays_found =
                        effective.find("numRays");
                std::size_t rays = 0U;
                if (rays_found == effective.end() ||
                    !IsValidNumRaysValue(
                        rays_found->second.value,
                        rays))
                {
                    continue;
                }
                std::size_t nodes = 0U;
                std::size_t base_beams = 0U;
                std::size_t stabilizer_beams = rays;
                if (!MultiplySize(rays, 4U, nodes) ||
                    !MultiplySize(rays, 24U, base_beams))
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            TOPOLOGY_COUNT_OVERFLOW,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        "numRays",
                        "Expanded approximation topology overflows "
                        "the platform size type");
                    m_stopped = true;
                    return;
                }
                const std::map<
                    std::string,
                    PreflightAssignment>::const_iterator node_s =
                        effective.find("nodeS");
                if (node_s != effective.end() &&
                    node_s->second.value != NULL &&
                    node_s->second.value->type ==
                        JBeamValueType::NUMBER &&
                    IsFiniteDouble(
                        node_s->second.value->number_value) &&
                    node_s->second.value->number_value == 9999.0)
                {
                    stabilizer_beams = 0U;
                }
                std::size_t beams = base_beams;
                if (!AddSize(stabilizer_beams, beams) ||
                    !AddSize(nodes, m_preflight_nodes) ||
                    !AddSize(beams, m_preflight_beams))
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            TOPOLOGY_COUNT_OVERFLOW,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        "numRays",
                        "Aggregate approximation topology overflows "
                        "the platform size type");
                    m_stopped = true;
                    return;
                }
                if (m_preflight_nodes >
                    m_limits.max_approximation_generated_nodes)
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            TOPOLOGY_NODE_LIMIT,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        "numRays",
                        "Expanded approximation nodes exceed the "
                        "configured reservation limit");
                    m_stopped = true;
                    return;
                }
                if (m_preflight_beams >
                    m_limits.max_approximation_generated_beams)
                {
                    Push(
                        JBeamPressureWheelDiagnosticCode::
                            TOPOLOGY_BEAM_LIMIT,
                        JBeamPressureWheelSeverity::ERROR,
                        Provenance(
                            section.part_index,
                            entry.span),
                        "pressureWheels",
                        entry_index - 1U,
                        "numRays",
                        "Expanded approximation beams exceed the "
                        "configured reservation limit");
                    m_stopped = true;
                    return;
                }
            }
        }
    }

    MeasureStatus MeasureValue(
        const JBeamValue& root,
        std::size_t remaining,
        std::size_t& bytes)
    {
        bytes = 0U;
        std::vector<PendingValue> pending;
        std::set<const JBeamValue*> seen;
        PendingValue first;
        first.value = &root;
        first.depth = 0U;
        pending.push_back(first);
        while (!pending.empty())
        {
            const PendingValue current = pending.back();
            pending.pop_back();
            if (current.value == NULL ||
                !seen.insert(current.value).second)
            {
                return MeasureStatus::INVALID_GRAPH;
            }
            if (current.depth >
                    m_limits.max_preserved_value_depth ||
                !TryChargeWork(2U))
            {
                return MeasureStatus::COMPLEXITY_LIMIT;
            }
            std::size_t current_bytes =
                sizeof(JBeamValue);
            if (!AddSize(
                    current.value->span.source_name.size(),
                    current_bytes) ||
                !AddSize(
                    current.value->scalar_text.size(),
                    current_bytes))
            {
                return MeasureStatus::BYTE_LIMIT;
            }
            if (current.value->type == JBeamValueType::ARRAY)
            {
                if (current.value->array_values.size() >
                    RemainingWork() / 2U)
                {
                    return MeasureStatus::COMPLEXITY_LIMIT;
                }
                std::size_t allocation = 0U;
                if (!MultiplySize(
                        current.value->array_values.size(),
                        sizeof(JBeamValue),
                        allocation) ||
                    !AddSize(allocation, current_bytes))
                {
                    return MeasureStatus::BYTE_LIMIT;
                }
                for (std::size_t i =
                         current.value->array_values.size();
                     i > 0U;
                     --i)
                {
                    PendingValue child;
                    child.value =
                        &current.value->array_values[i - 1U];
                    child.depth = current.depth + 1U;
                    pending.push_back(child);
                }
            }
            else if (current.value->type ==
                JBeamValueType::OBJECT)
            {
                std::size_t allocation = 0U;
                std::size_t field_work = 0U;
                if (!MultiplySize(
                        current.value->object_fields.size(),
                        sizeof(JBeamObjectField),
                        allocation) ||
                    !MultiplySize(
                        current.value->object_fields.size(),
                        2U,
                        field_work) ||
                    !AddSize(allocation, current_bytes))
                {
                    return MeasureStatus::BYTE_LIMIT;
                }
                if (!TryChargeWork(field_work))
                {
                    return MeasureStatus::COMPLEXITY_LIMIT;
                }
                for (std::size_t i = 0U;
                     i < current.value->object_fields.size();
                     ++i)
                {
                    const JBeamObjectField& field =
                        current.value->object_fields[i];
                    if (!AddSize(field.key.size(), current_bytes) ||
                        !AddSize(
                            field.key_span.source_name.size(),
                            current_bytes))
                    {
                        return MeasureStatus::BYTE_LIMIT;
                    }
                }
                for (std::size_t i =
                         current.value->object_fields.size();
                     i > 0U;
                     --i)
                {
                    const JBeamObjectField& field =
                        current.value->object_fields[i - 1U];
                    if (field.value)
                    {
                        PendingValue child;
                        child.value = field.value.get();
                        child.depth = current.depth + 1U;
                        pending.push_back(child);
                    }
                }
            }
            if (current_bytes >
                remaining - std::min(bytes, remaining))
            {
                return MeasureStatus::BYTE_LIMIT;
            }
            bytes += current_bytes;
        }
        return MeasureStatus::OK;
    }

    bool CloneValue(
        const JBeamValue& source,
        std::size_t depth,
        JBeamValue& destination) const
    {
        if (depth > m_limits.max_preserved_value_depth)
        {
            return false;
        }
        destination.type = source.type;
        destination.span = source.span;
        destination.boolean_value = source.boolean_value;
        destination.number_value = source.number_value;
        destination.scalar_text = source.scalar_text;
        destination.array_values.clear();
        destination.object_fields.clear();
        destination.array_values.reserve(
            source.array_values.size());
        for (std::size_t i = 0U;
             i < source.array_values.size();
             ++i)
        {
            JBeamValue child;
            if (!CloneValue(
                    source.array_values[i],
                    depth + 1U,
                    child))
            {
                return false;
            }
            destination.array_values.push_back(child);
        }
        destination.object_fields.reserve(
            source.object_fields.size());
        for (std::size_t i = 0U;
             i < source.object_fields.size();
             ++i)
        {
            const JBeamObjectField& source_field =
                source.object_fields[i];
            JBeamObjectField field;
            field.key = source_field.key;
            field.key_span = source_field.key_span;
            if (source_field.value)
            {
                std::shared_ptr<JBeamValue> child(
                    new JBeamValue());
                if (!CloneValue(
                        *source_field.value,
                        depth + 1U,
                        *child))
                {
                    return false;
                }
                field.value = child;
            }
            destination.object_fields.push_back(field);
        }
        return true;
    }

    bool CloneSourceRecord(
        const SectionRef& source,
        JBeamPressureWheelSourceRecord& record)
    {
        const JBeamObjectField& field = *source.field;
        const JBeamSourceSpan span = field.value
            ? field.value->span
            : field.key_span;
        record.kind = source.kind;
        record.section_name = field.key;
        record.section_occurrence = source.occurrence;
        record.provenance =
            Provenance(source.part_index, span);
        if (!field.value)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_SECTION,
                JBeamPressureWheelSeverity::ERROR,
                record.provenance,
                field.key,
                0U,
                field.key,
                "Relevant section has no value");
            return false;
        }
        const std::size_t remaining =
            m_limits.max_retained_bytes -
                std::min(
                    m_retained_bytes,
                    m_limits.max_retained_bytes);
        std::size_t value_bytes = 0U;
        const MeasureStatus measured =
            MeasureValue(*field.value, remaining, value_bytes);
        if (measured != MeasureStatus::OK)
        {
            Push(
                measured == MeasureStatus::BYTE_LIMIT
                    ? JBeamPressureWheelDiagnosticCode::
                        RETAINED_BYTE_LIMIT
                    : JBeamPressureWheelDiagnosticCode::
                        PRESERVED_VALUE_LIMIT,
                JBeamPressureWheelSeverity::ERROR,
                record.provenance,
                field.key,
                0U,
                field.key,
                measured == MeasureStatus::INVALID_GRAPH
                    ? "Preserved section contains a value cycle or alias"
                    : measured == MeasureStatus::COMPLEXITY_LIMIT
                        ? "Preserved section exceeds its depth or work budget"
                        : "Preserved section exceeds its retained-byte budget");
            m_stopped = true;
            return false;
        }
        std::size_t record_bytes =
            sizeof(JBeamPressureWheelSourceRecord);
        if (!AddSize(
                record.section_name.size(),
                record_bytes))
        {
            StopRetainedLimit();
            return false;
        }
        if (!AddSize(
                record.provenance.span.source_name.size(),
                record_bytes) ||
            !AddSize(value_bytes, record_bytes) ||
            !Charge(record_bytes))
        {
            return false;
        }
        std::shared_ptr<JBeamValue> copy(new JBeamValue());
        if (!CloneValue(*field.value, 0U, *copy))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    PRESERVED_VALUE_LIMIT,
                JBeamPressureWheelSeverity::ERROR,
                record.provenance,
                field.key,
                0U,
                field.key,
                "Preserved section clone exceeds its depth budget");
            m_stopped = true;
            return false;
        }
        record.raw_value = copy;
        return true;
    }

    AssignmentRef Assignment(
        std::size_t part_index,
        JBeamPressureWheelFieldOrigin origin,
        const std::shared_ptr<const JBeamValue>& value,
        const JBeamSourceSpan& span) const
    {
        AssignmentRef result;
        result.origin = origin;
        result.value = value;
        result.span = span;
        result.part_index = part_index;
        return result;
    }

    void ApplyOwnedObject(
        std::size_t part_index,
        const JBeamValue& object,
        std::map<std::string, AssignmentRef>& fields)
    {
        for (std::size_t i = 0U;
             i < object.object_fields.size();
             ++i)
        {
            const JBeamObjectField& field =
                object.object_fields[i];
            if (!field.value)
            {
                continue;
            }
            fields[field.key] = Assignment(
                part_index,
                JBeamPressureWheelFieldOrigin::
                    INHERITED_DEFAULT,
                field.value,
                field.value->span);
        }
    }

    void ApplyRowObject(
        std::size_t part_index,
        const JBeamValue& object,
        std::map<std::string, AssignmentRef>& fields)
    {
        for (std::size_t i = 0U;
             i < object.object_fields.size();
             ++i)
        {
            const JBeamObjectField& field =
                object.object_fields[i];
            if (!field.value)
            {
                continue;
            }
            fields[field.key] = Assignment(
                part_index,
                JBeamPressureWheelFieldOrigin::
                    ROW_LOCAL_OVERRIDE,
                field.value,
                field.value->span);
        }
    }

    const AssignmentRef* FindRequired(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        const std::string& primary,
        const std::string& alias,
        bool& valid)
    {
        const std::map<
            std::string,
            AssignmentRef>::const_iterator first =
                fields.find(primary);
        const std::map<
            std::string,
            AssignmentRef>::const_iterator second =
                alias.empty()
                    ? fields.end()
                    : fields.find(alias);
        if (first != fields.end() &&
            second != fields.end())
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    AMBIGUOUS_REQUIRED_FIELD,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                row_index,
                primary,
                "Canonical and alias assignments are both effective; "
                "both remain preserved");
            valid = false;
            return NULL;
        }
        const AssignmentRef* result =
            first != fields.end()
                ? &first->second
                : second != fields.end()
                    ? &second->second
                    : NULL;
        if (result == NULL || !result->value)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    MISSING_REQUIRED_FIELD,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                row_index,
                primary,
                "Required pressure-wheel field is missing; no default "
                "is guessed");
            valid = false;
            return NULL;
        }
        return result;
    }

    bool ReadString(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        const std::string& primary,
        const std::string& alias,
        std::string& output,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            primary,
            alias,
            valid);
        if (assignment == NULL)
        {
            return false;
        }
        if (IsExpression(*assignment->value))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    EXPRESSION_DISABLED,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                primary,
                "Expressions and variable references remain inert");
            valid = false;
            return false;
        }
        if (assignment->value->type !=
                JBeamValueType::STRING ||
            assignment->value->scalar_text.empty())
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_FIELD_TYPE,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                primary,
                "Required identifier must be a non-empty string");
            valid = false;
            return false;
        }
        output = assignment->value->scalar_text;
        return true;
    }

    bool ReadNumber(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        const std::string& name,
        double& output,
        bool positive,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            name,
            std::string(),
            valid);
        if (assignment == NULL)
        {
            return false;
        }
        if (IsExpression(*assignment->value))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    EXPRESSION_DISABLED,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                name,
                "Expressions and variable references remain inert");
            valid = false;
            return false;
        }
        if (assignment->value->type !=
            JBeamValueType::NUMBER)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_FIELD_TYPE,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                name,
                "Required geometry must be explicitly numeric");
            valid = false;
            return false;
        }
        if (!IsFiniteDouble(
                assignment->value->number_value))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    NON_FINITE_NUMBER,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                name,
                "Non-finite pressure-wheel numbers are rejected");
            valid = false;
            return false;
        }
        output = assignment->value->number_value;
        if (positive && !(output > 0.0))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_GEOMETRY,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                name,
                "Radius and width geometry must be strictly positive");
            valid = false;
            return false;
        }
        return true;
    }

    void ReadNodeS(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        JBeamPressureWheel& wheel,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            "nodeS",
            std::string(),
            valid);
        if (assignment == NULL)
        {
            return;
        }
        const JBeamValue& value = *assignment->value;
        if (IsExpression(value))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    EXPRESSION_DISABLED,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                "nodeS",
                "Expressions and variable references remain inert");
            valid = false;
            return;
        }
        if (value.type == JBeamValueType::NUMBER &&
            IsFiniteDouble(value.number_value) &&
            value.number_value == 9999.0)
        {
            wheel.node_s = value.scalar_text.empty()
                ? std::string("9999")
                : value.scalar_text;
            wheel.node_s_disables_legacy_stabilizer = true;
            return;
        }
        if (value.type == JBeamValueType::STRING &&
            !value.scalar_text.empty())
        {
            wheel.node_s = value.scalar_text;
            wheel.node_s_disables_legacy_stabilizer = false;
            return;
        }
        Push(
            value.type == JBeamValueType::NUMBER &&
                    !IsFiniteDouble(value.number_value)
                ? JBeamPressureWheelDiagnosticCode::
                    NON_FINITE_NUMBER
                : JBeamPressureWheelDiagnosticCode::
                    INVALID_FIELD_TYPE,
            JBeamPressureWheelSeverity::ERROR,
            Provenance(
                assignment->part_index,
                assignment->span),
            "pressureWheels",
            row_index,
            "nodeS",
            "nodeS must be a non-empty node string or numeric 9999");
        valid = false;
    }

    void ReadWheelDirection(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        JBeamPressureWheel& wheel,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            "wheelDir",
            std::string(),
            valid);
        if (assignment == NULL)
        {
            return;
        }
        const JBeamValue& value = *assignment->value;
        if (value.type != JBeamValueType::NUMBER ||
            !IsFiniteDouble(value.number_value) ||
            (value.number_value != 1.0 &&
             value.number_value != -1.0))
        {
            Push(
                value.type == JBeamValueType::NUMBER &&
                    !IsFiniteDouble(value.number_value)
                    ? JBeamPressureWheelDiagnosticCode::
                        NON_FINITE_NUMBER
                    : JBeamPressureWheelDiagnosticCode::
                        INVALID_WHEEL_DIRECTION,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                "wheelDir",
                "wheelDir must be the exact numeric value 1 or -1");
            valid = false;
            return;
        }
        wheel.wheel_direction =
            value.number_value == 1.0 ? 1 : -1;
    }

    void ReadHasTire(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        JBeamPressureWheel& wheel,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            "hasTire",
            std::string(),
            valid);
        if (assignment == NULL)
        {
            return;
        }
        if (assignment->value->type !=
            JBeamValueType::BOOLEAN)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_FIELD_TYPE,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                "hasTire",
                "hasTire must be an explicitly authored Boolean");
            valid = false;
            return;
        }
        wheel.has_tire =
            assignment->value->boolean_value;
    }

    void ReadNumRays(
        const std::map<std::string, AssignmentRef>& fields,
        const JBeamPressureWheelProvenance& provenance,
        std::size_t row_index,
        JBeamPressureWheel& wheel,
        bool& valid)
    {
        const AssignmentRef* assignment = FindRequired(
            fields,
            provenance,
            row_index,
            "numRays",
            std::string(),
            valid);
        if (assignment == NULL)
        {
            return;
        }
        std::size_t rays = 0U;
        if (!IsValidNumRaysValue(
                assignment->value.get(),
                rays))
        {
            Push(
                assignment->value->type ==
                        JBeamValueType::NUMBER &&
                    !IsFiniteDouble(
                        assignment->value->number_value)
                    ? JBeamPressureWheelDiagnosticCode::
                        NON_FINITE_NUMBER
                    : JBeamPressureWheelDiagnosticCode::
                        INVALID_NUM_RAYS,
                JBeamPressureWheelSeverity::ERROR,
                Provenance(
                    assignment->part_index,
                    assignment->span),
                "pressureWheels",
                row_index,
                "numRays",
                "numRays must be a positive even integer within the "
                "inventory safety bound; 10 through 20 is the official "
                "recommendation, not a format maximum");
            valid = false;
            return;
        }
        wheel.num_rays = rays;
        if (!MultiplySize(
                rays,
                4U,
                wheel.approximation_generated_nodes) ||
            !MultiplySize(
                rays,
                24U,
                wheel.approximation_base_generated_beams))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    TOPOLOGY_COUNT_OVERFLOW,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                row_index,
                "numRays",
                "Expanded approximation topology overflows the "
                "platform size type");
            valid = false;
            return;
        }
        wheel.approximation_stabilizer_beams =
            wheel.node_s_disables_legacy_stabilizer
                ? 0U
                : rays;
        wheel.approximation_generated_beams =
            wheel.approximation_base_generated_beams;
        if (!AddSize(
                wheel.approximation_stabilizer_beams,
                wheel.approximation_generated_beams))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    TOPOLOGY_COUNT_OVERFLOW,
                JBeamPressureWheelSeverity::ERROR,
                provenance,
                "pressureWheels",
                row_index,
                "numRays",
                "Expanded approximation beam count overflows");
            valid = false;
        }
    }

    void PreserveEffectiveFields(
        const std::map<std::string, AssignmentRef>& fields,
        JBeamPressureWheel& wheel,
        std::size_t row_index)
    {
        wheel.effective_fields.reserve(fields.size());
        for (std::map<
                 std::string,
                 AssignmentRef>::const_iterator iterator =
                 fields.begin();
             iterator != fields.end() && !m_stopped;
             ++iterator)
        {
            const AssignmentRef& source = iterator->second;
            std::size_t bytes =
                sizeof(JBeamPressureWheelField);
            if (!AddSize(
                    iterator->first.size(),
                    bytes))
            {
                StopRetainedLimit();
                return;
            }
            if (!AddSize(
                    source.span.source_name.size(),
                    bytes) ||
                !Charge(bytes))
            {
                return;
            }
            JBeamPressureWheelField field;
            field.name = iterator->first;
            field.origin = source.origin;
            field.provenance =
                Provenance(source.part_index, source.span);
            field.raw_value = source.value;
            wheel.effective_fields.push_back(field);

            const bool ambiguous =
                IsDocumentedAmbiguousWheelField(
                    iterator->first);
            if (ambiguous &&
                m_reported_documentation_ambiguities.insert(
                    iterator->first).second)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        DOCUMENTATION_AMBIGUITY_PRESERVED,
                    JBeamPressureWheelSeverity::WARNING,
                    field.provenance,
                    "pressureWheels",
                    row_index,
                    iterator->first,
                    IsCoreField(iterator->first)
                        ? "Official pressure-wheel documentation has a "
                            "type or example ambiguity; the authored value "
                            "is preserved and the inventory uses its strict "
                            "literal core contract"
                        : "Official pressure-wheel documentation has a "
                            "type or default ambiguity; the authored value "
                            "is preserved but remains inert");
            }
            if (!IsCoreField(iterator->first))
            {
                wheel.has_inert_or_unimplemented_behavior = true;
                const bool documented_modifier =
                    IsDocumentedJBeamModifierField(
                        iterator->first);
                const bool documented =
                    documented_modifier ||
                    IsDocumentedWheelField(iterator->first);
                if (ambiguous)
                {
                    continue;
                }
                Push(
                    documented
                        ? JBeamPressureWheelDiagnosticCode::
                            DOCUMENTED_BEHAVIOR_INERT
                        : JBeamPressureWheelDiagnosticCode::
                            UNKNOWN_FIELD_PRESERVED,
                    JBeamPressureWheelSeverity::WARNING,
                    field.provenance,
                    "pressureWheels",
                    row_index,
                    iterator->first,
                    documented
                        ? documented_modifier
                            ? "Documented JBeam modifier is preserved "
                                "but not evaluated by this inventory pass"
                            : "Documented field is preserved but not "
                                "simulated by this inventory pass"
                        : "Unknown field is preserved exactly but "
                            "remains inert");
            }
        }
    }

    void ProcessPressureSection(
        const SectionRef& source,
        std::size_t source_record_index,
        std::map<std::string, AssignmentRef>& defaults)
    {
        const JBeamPressureWheelSourceRecord& record =
            m_result.source_records[source_record_index];
        const JBeamValue& table = *record.raw_value;
        if (!IsTableHeader(table))
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    INVALID_TABLE_HEADER,
                JBeamPressureWheelSeverity::ERROR,
                record.provenance,
                "pressureWheels",
                0U,
                std::string(),
                "pressureWheels requires a non-empty string table header");
            return;
        }
        if (source.duplicate_pressure_section)
        {
            Push(
                JBeamPressureWheelDiagnosticCode::
                    DUPLICATE_SECTION,
                JBeamPressureWheelSeverity::ERROR,
                record.provenance,
                "pressureWheels",
                0U,
                std::string(),
                "Duplicate pressureWheels sections in one part are "
                "preserved but ambiguous for admission");
        }

        const JBeamValue& header = table.array_values[0];
        std::vector<std::string> columns;
        std::set<std::string> column_set;
        bool header_valid = true;
        columns.reserve(header.array_values.size());
        for (std::size_t i = 0U;
             i < header.array_values.size();
             ++i)
        {
            const std::string& name =
                header.array_values[i].scalar_text;
            columns.push_back(name);
            if (!column_set.insert(name).second)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        DUPLICATE_TABLE_HEADER,
                    JBeamPressureWheelSeverity::ERROR,
                    Provenance(
                        source.part_index,
                        header.array_values[i].span),
                    "pressureWheels",
                    0U,
                    name,
                    "Duplicate pressureWheels header is ambiguous");
                header_valid = false;
            }
        }
        for (std::size_t entry_index = 1U;
             entry_index < table.array_values.size() &&
                 !m_stopped;
             ++entry_index)
        {
            const JBeamValue& entry =
                table.array_values[entry_index];
            if (entry.type == JBeamValueType::OBJECT)
            {
                ApplyOwnedObject(
                    source.part_index,
                    entry,
                    defaults);
                continue;
            }
            if (entry.type != JBeamValueType::ARRAY)
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        INVALID_TABLE_ENTRY,
                    JBeamPressureWheelSeverity::ERROR,
                    Provenance(source.part_index, entry.span),
                    "pressureWheels",
                    entry_index - 1U,
                    std::string(),
                    "Table entry is neither a defaults dictionary "
                    "nor a wheel row; it remains in the raw record");
                continue;
            }
            JBeamPressureWheel wheel;
            wheel.source_record_index = source_record_index;
            wheel.source_entry_index = entry_index - 1U;
            wheel.provenance =
                Provenance(source.part_index, entry.span);
            wheel.raw_row =
                std::shared_ptr<const JBeamValue>(
                    record.raw_value,
                    &entry);
            bool valid = header_valid &&
                !source.duplicate_pressure_section;
            std::map<std::string, AssignmentRef> effective =
                defaults;
            const std::size_t positional =
                PositionalCellCount(entry, columns.size());
            const std::size_t mapped =
                std::min(positional, columns.size());
            for (std::size_t i = 0U; i < mapped; ++i)
            {
                const JBeamValue* value =
                    &entry.array_values[i];
                effective[columns[i]] = Assignment(
                    source.part_index,
                    JBeamPressureWheelFieldOrigin::
                        POSITIONAL_CELL,
                    std::shared_ptr<const JBeamValue>(
                        record.raw_value,
                        value),
                    value->span);
            }
            const JBeamValue* overrides =
                RowLocalOverrides(entry, columns.size());
            if (overrides != NULL)
            {
                ApplyRowObject(
                    source.part_index,
                    *overrides,
                    effective);
            }
            if (positional < columns.size())
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        INVALID_TABLE_ENTRY,
                    JBeamPressureWheelSeverity::WARNING,
                    wheel.provenance,
                    "pressureWheels",
                    entry_index - 1U,
                    std::string(),
                    "Short row is preserved; missing cells must be "
                    "satisfied by explicit defaults");
            }
            else if (positional > columns.size())
            {
                Push(
                    JBeamPressureWheelDiagnosticCode::
                        INVALID_TABLE_ENTRY,
                    JBeamPressureWheelSeverity::ERROR,
                    wheel.provenance,
                    "pressureWheels",
                    entry_index - 1U,
                    std::string(),
                    "Unmapped extra positional cells make the row "
                    "ambiguous for admission");
                valid = false;
            }

            PreserveEffectiveFields(
                effective,
                wheel,
                entry_index - 1U);
            if (m_stopped)
            {
                return;
            }
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "name",
                std::string(),
                wheel.name,
                valid);
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "hubGroup",
                std::string(),
                wheel.hub_group,
                valid);
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "group",
                std::string(),
                wheel.group,
                valid);
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "node1:",
                "node1",
                wheel.node1,
                valid);
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "node2:",
                "node2",
                wheel.node2,
                valid);
            ReadNodeS(
                effective,
                wheel.provenance,
                entry_index - 1U,
                wheel,
                valid);
            ReadString(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "nodeArm:",
                "nodeArm",
                wheel.node_arm,
                valid);
            ReadWheelDirection(
                effective,
                wheel.provenance,
                entry_index - 1U,
                wheel,
                valid);
            ReadNumber(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "radius",
                wheel.radius,
                true,
                valid);
            ReadNumber(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "hubRadius",
                wheel.hub_radius,
                true,
                valid);
            ReadNumber(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "wheelOffset",
                wheel.wheel_offset,
                false,
                valid);
            ReadNumber(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "tireWidth",
                wheel.tire_width,
                true,
                valid);
            ReadNumber(
                effective,
                wheel.provenance,
                entry_index - 1U,
                "hubWidth",
                wheel.hub_width,
                true,
                valid);
            ReadHasTire(
                effective,
                wheel.provenance,
                entry_index - 1U,
                wheel,
                valid);
            ReadNumRays(
                effective,
                wheel.provenance,
                entry_index - 1U,
                wheel,
                valid);

            std::size_t string_bytes =
                sizeof(JBeamPressureWheel);
            if (!AddSize(wheel.name.size(), string_bytes) ||
                !AddSize(wheel.hub_group.size(), string_bytes) ||
                !AddSize(wheel.group.size(), string_bytes) ||
                !AddSize(wheel.node1.size(), string_bytes) ||
                !AddSize(wheel.node2.size(), string_bytes) ||
                !AddSize(wheel.node_s.size(), string_bytes) ||
                !AddSize(wheel.node_arm.size(), string_bytes) ||
                !AddSize(
                    wheel.provenance.span.source_name.size(),
                    string_bytes) ||
                !Charge(string_bytes))
            {
                return;
            }
            wheel.admission = valid
                ? JBeamPressureWheelAdmission::
                    SCHEMA_ADMISSIBLE_INVENTORY_ONLY
                : JBeamPressureWheelAdmission::
                    PRESERVED_NOT_ADMISSIBLE;
            m_result.wheels.push_back(wheel);
        }
    }

    void ProcessSections()
    {
        std::map<std::string, AssignmentRef> defaults;
        for (std::size_t section_index = 0U;
             section_index < m_sections.size() && !m_stopped;
             ++section_index)
        {
            const SectionRef& source =
                m_sections[section_index];
            JBeamPressureWheelSourceRecord record;
            if (!CloneSourceRecord(source, record))
            {
                if (!m_stopped)
                {
                    // A null source is diagnosed and skipped. Other clone
                    // failures are terminal.
                    continue;
                }
                return;
            }
            const std::size_t record_index =
                m_result.source_records.size();
            m_result.source_records.push_back(record);
            if (source.kind ==
                JBeamPressureWheelSourceKind::PRESSURE_WHEELS)
            {
                ProcessPressureSection(
                    source,
                    record_index,
                    defaults);
                continue;
            }
            m_has_inert_sources = true;
            Push(
                source.kind ==
                        JBeamPressureWheelSourceKind::
                            INERT_SCALING_MODIFIER
                    ? JBeamPressureWheelDiagnosticCode::
                        INERT_SCALING_MODIFIER_PRESERVED
                    : JBeamPressureWheelDiagnosticCode::
                        INERT_SECTION_PRESERVED,
                JBeamPressureWheelSeverity::WARNING,
                record.provenance,
                record.section_name,
                0U,
                record.section_name,
                source.kind ==
                        JBeamPressureWheelSourceKind::
                            INERT_SCALING_MODIFIER
                    ? "Scaling process modifier is preserved in resolved "
                        "source order and remains inert"
                    : source.kind ==
                        JBeamPressureWheelSourceKind::INERT_LUA
                    ? "Lua content is preserved as data and never executed"
                    : "Controller, electrics, engine, or powertrain "
                        "content is preserved as data and remains inert");
        }
    }
};

class CanonicalWriter
{
public:
    explicit CanonicalWriter(std::size_t limit)
        : m_limit(limit)
        , m_ok(true)
    {
    }

    bool Ok() const
    {
        return m_ok;
    }

    const std::string& Data() const
    {
        return m_data;
    }

    void Byte(unsigned char value)
    {
        if (!Reserve(1U))
        {
            return;
        }
        m_data.push_back(static_cast<char>(value));
    }

    void U64(std::uint64_t value)
    {
        if (!Reserve(8U))
        {
            return;
        }
        for (unsigned int shift = 56U;; shift -= 8U)
        {
            m_data.push_back(static_cast<char>(
                (value >> shift) & UINT64_C(0xff)));
            if (shift == 0U)
            {
                break;
            }
        }
    }

    void Size(std::size_t value)
    {
        if (value >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max()))
        {
            m_ok = false;
            return;
        }
        U64(static_cast<std::uint64_t>(value));
    }

    void String(const std::string& value)
    {
        Size(value.size());
        if (!m_ok || !Reserve(value.size()))
        {
            return;
        }
        m_data.append(value);
    }

    void Boolean(bool value)
    {
        Byte(value ? 1U : 0U);
    }

    void Double(double value)
    {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        U64(bits);
    }

private:
    std::size_t m_limit;
    bool m_ok;
    std::string m_data;

    bool Reserve(std::size_t size)
    {
        if (!m_ok ||
            size > m_limit -
                std::min(m_data.size(), m_limit))
        {
            m_ok = false;
            return false;
        }
        return true;
    }
};

void WritePosition(
    CanonicalWriter& writer,
    const JBeamSourcePosition& position)
{
    writer.U64(position.byte_offset);
    writer.U64(position.line);
    writer.U64(position.column);
}

void WriteSpan(
    CanonicalWriter& writer,
    const JBeamSourceSpan& span)
{
    writer.String(span.source_name);
    WritePosition(writer, span.begin);
    WritePosition(writer, span.end);
}

void WriteProvenance(
    CanonicalWriter& writer,
    const JBeamPressureWheelProvenance& provenance)
{
    writer.Boolean(static_cast<bool>(provenance.part));
    if (provenance.part)
    {
        writer.Size(
            provenance.part->part_preorder_index);
        writer.String(provenance.part->part_name);
        writer.String(provenance.part->package_path);
    }
    WriteSpan(writer, provenance.span);
}

bool WriteValue(
    CanonicalWriter& writer,
    const JBeamValue& value,
    std::size_t depth,
    std::size_t depth_limit,
    std::set<const JBeamValue*>& path)
{
    if (!writer.Ok() ||
        depth > depth_limit ||
        !path.insert(&value).second)
    {
        return false;
    }
    writer.Byte(static_cast<unsigned char>(value.type));
    WriteSpan(writer, value.span);
    writer.Boolean(value.boolean_value);
    writer.Double(value.number_value);
    writer.String(value.scalar_text);
    writer.Size(value.array_values.size());
    for (std::size_t i = 0U;
         i < value.array_values.size() && writer.Ok();
         ++i)
    {
        if (!WriteValue(
                writer,
                value.array_values[i],
                depth + 1U,
                depth_limit,
                path))
        {
            path.erase(&value);
            return false;
        }
    }
    writer.Size(value.object_fields.size());
    for (std::size_t i = 0U;
         i < value.object_fields.size() && writer.Ok();
         ++i)
    {
        const JBeamObjectField& field =
            value.object_fields[i];
        writer.String(field.key);
        WriteSpan(writer, field.key_span);
        writer.Boolean(static_cast<bool>(field.value));
        if (field.value &&
            !WriteValue(
                writer,
                *field.value,
                depth + 1U,
                depth_limit,
                path))
        {
            path.erase(&value);
            return false;
        }
    }
    path.erase(&value);
    return writer.Ok();
}

bool WriteOptionalValue(
    CanonicalWriter& writer,
    const std::shared_ptr<const JBeamValue>& value,
    std::size_t depth_limit)
{
    writer.Boolean(static_cast<bool>(value));
    if (!value)
    {
        return writer.Ok();
    }
    std::set<const JBeamValue*> path;
    return WriteValue(
        writer,
        *value,
        0U,
        depth_limit,
        path);
}

} // namespace

JBeamPressureWheelDocumentationProfile::
    JBeamPressureWheelDocumentationProfile()
    : profile_id("beamng-docs-0.38.5.0-2026-07-27")
    , beamng_version("0.38.5.0")
    , wheel_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/wheels/")
    , wheel_documentation_last_modified("2026-06-09")
    , jbeam_syntax_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "intro_jbeam/jbeamsyntax/")
    , node_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/nodes/")
    , beam_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/beams/")
    , vehicle_controller_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "vehicle_system/controller/main/vehiclecontroller/")
    , manual_shift_logic_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "vehicle_system/controller/main/vehiclecontroller/"
        "shiftlogic-manualgearbox/")
    , sequential_shift_logic_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "vehicle_system/controller/main/vehiclecontroller/"
        "shiftlogic-sequentialgearbox/")
    , dct_shift_logic_documentation_url(
        "https://documentation.beamng.com/modding/vehicle/"
        "vehicle_system/controller/main/vehiclecontroller/"
        "shiftlogic-dctgearbox/")
    , recommended_minimum_num_rays(
        RECOMMENDED_MIN_NUM_RAYS)
    , recommended_maximum_num_rays(
        RECOMMENDED_MAX_NUM_RAYS)
    , pressure_wheel_count_has_documented_maximum(false)
    , future_ror_lowering_maximum_wheels(
        FUTURE_ROR_LOWERING_MAX_WHEELS)
    , node_weight(25.0)
    , node_collision(true)
    , node_self_collision(false)
    , node_static_collision(true)
    , node_friction_coefficient(1.0)
    , node_sliding_friction_coefficient(1.0)
    , normal_beam_spring(4300000.0)
    , normal_beam_damping(580.0)
    , normal_beam_strength_is_flt_max(true)
    , normal_beam_deform(220000.0)
    , normal_beam_optional(false)
    , normal_beam_precompression(1.0)
    , stribeck_exponent(1.75)
    , tread_coefficient(1.0)
    , softness_coefficient(0.6)
    , enable_tire_reinforcement_beams(false)
    , enable_tire_support_beams(false)
    , triangle_collision(false)
    , tread_triangle_collision(false)
    , side1_triangle_collision(false)
    , side2_triangle_collision(false)
    , hub_triangle_collision(false)
    , hub_side1_triangle_collision(false)
    , hub_side2_triangle_collision(false)
    , drag_coefficient(100.0)
    , skin_drag_coefficient(0.0)
    , brake_torque(0.0)
    , parking_torque(0.0)
    , brake_spring(10.0)
    , enable_brake_thermals(false)
    , brake_diameter(0.35)
    , brake_mass(10.0)
    , brake_type("vented-disc")
    , rotor_material("steel")
    , pad_material("basic")
    , brake_input_split(1.0)
    , brake_split_coefficient(1.0)
    , squeal_coefficient_natural(0.0)
    , squeal_coefficient_low_speed(0.0)
    , squeal_coefficient_glazing(1.0)
    , enable_abs(false)
    , abs_slip_ratio_target(0.18)
    , abs_hz(100.0)
    , brake_pressure_in_delay(0.04)
    , brake_pressure_out_delay(0.04)
    , brake_venting_coefficient_has_documented_default(false)
    , low_shift_down_rpm(2000.0)
    , high_shift_down_rpm(3500.0)
    , low_shift_up_rpm(2500.0)
    , high_shift_up_rpm(5000.0)
    , calculate_optimal_load_shift_points(false)
    , shift_down_rpm_offset_coefficient(1.3)
    , gearbox_decision_smoothing_down(2.0)
    , gearbox_decision_smoothing_up(5.0)
    , aggression_smoothing_up(1.5)
    , aggression_smoothing_down(0.15)
    , use_smart_aggression_calculation(true)
    , aggression_hold_off_throttle_delay(2.25)
    , top_speed_limit(-1.0)
    , reverse_speed_limit(-1.0)
    , wheel_slip_up_threshold(20000.0)
    , wheel_slip_down_threshold(30000.0)
    , wheel_slip_smoothing_in(10.0)
    , wheel_slip_smoothing_out(20.0)
    , shift_logic_name("gearboxType")
    , transmission_shift_delay(0.2)
    , transmission_gear_change_delay(0.5)
    , neutral_selection_delay(0.5)
    , clutch_launch_start_rpm(2000.0)
    , clutch_launch_target_rpm(3000.0)
    , clutch_in_rate(15.0)
    , clutch_out_rate(10.0)
    , rev_match_throttle(0.5)
{
}

const JBeamPressureWheelDocumentationProfile&
GetJBeamPressureWheelDocumentationProfile()
{
    static const JBeamPressureWheelDocumentationProfile profile;
    return profile;
}

JBeamPressureWheelPartIdentity::
    JBeamPressureWheelPartIdentity()
    : part_preorder_index(0U)
{
}

JBeamPressureWheelProvenance::
    JBeamPressureWheelProvenance()
{
}

std::size_t
JBeamPressureWheelProvenance::PartPreorderIndex() const
{
    return part
        ? part->part_preorder_index
        : std::numeric_limits<std::size_t>::max();
}

const std::string&
JBeamPressureWheelProvenance::PartName() const
{
    static const std::string empty;
    return part ? part->part_name : empty;
}

const std::string&
JBeamPressureWheelProvenance::PackagePath() const
{
    static const std::string empty;
    return part ? part->package_path : empty;
}

JBeamPressureWheelDiagnostic::
    JBeamPressureWheelDiagnostic()
    : code(
        JBeamPressureWheelDiagnosticCode::
            INVALID_RESOLVED_GRAPH)
    , severity(JBeamPressureWheelSeverity::ERROR)
    , row_index(0U)
{
}

JBeamPressureWheelLimits::JBeamPressureWheelLimits()
    : max_parts(256U)
    , max_entries(16384U)
    , max_wheels(HARD_MAX_INVENTORY_WHEELS)
    , max_source_records(HARD_MAX_SOURCE_RECORDS)
    , max_effective_fields(HARD_MAX_EFFECTIVE_FIELDS)
    , max_diagnostics(HARD_MAX_DIAGNOSTICS)
    , max_retained_bytes(HARD_MAX_RETAINED_BYTES)
    , max_preserved_value_work_units(
        HARD_MAX_PRESERVED_VALUE_WORK_UNITS)
    , max_preserved_value_depth(
        HARD_MAX_PRESERVED_VALUE_DEPTH)
    , max_approximation_generated_nodes(
        HARD_MAX_APPROXIMATION_GENERATED_NODES)
    , max_approximation_generated_beams(
        HARD_MAX_APPROXIMATION_GENERATED_BEAMS)
    , max_canonical_output_bytes(
        HARD_MAX_CANONICAL_OUTPUT_BYTES)
{
}

JBeamPressureWheelSourceRecord::
    JBeamPressureWheelSourceRecord()
    : kind(
        JBeamPressureWheelSourceKind::
            PRESSURE_WHEELS)
    , section_occurrence(0U)
{
}

JBeamPressureWheel::JBeamPressureWheel()
    : admission(
        JBeamPressureWheelAdmission::
            PRESERVED_NOT_ADMISSIBLE)
    , runtime_policy(
        JBeamPressureWheelRuntimePolicy::
            INVENTORY_ONLY_NEVER_LOWER)
    , has_inert_or_unimplemented_behavior(true)
    , node_s_disables_legacy_stabilizer(false)
    , wheel_direction(0)
    , radius(0.0)
    , hub_radius(0.0)
    , wheel_offset(0.0)
    , tire_width(0.0)
    , hub_width(0.0)
    , has_tire(false)
    , num_rays(0U)
    , approximation_generated_nodes(0U)
    , approximation_base_generated_beams(0U)
    , approximation_stabilizer_beams(0U)
    , approximation_generated_beams(0U)
    , source_record_index(0U)
    , source_entry_index(0U)
{
}

JBeamPressureWheelIR::JBeamPressureWheelIR()
    : runtime_policy(
        JBeamPressureWheelRuntimePolicy::
            INVENTORY_ONLY_NEVER_LOWER)
    , authored_entry_count(0U)
    , approximation_generated_node_count(0U)
    , approximation_generated_beam_count(0U)
    , retained_byte_count(0U)
    , canonical_output_byte_limit(0U)
    , canonical_value_depth_limit(0U)
{
}

bool JBeamPressureWheelIR::IsValid() const
{
    for (std::size_t i = 0U;
         i < diagnostics.size();
         ++i)
    {
        if (IsError(diagnostics[i]))
        {
            return false;
        }
    }
    return true;
}

bool JBeamPressureWheelIR::
    AllWheelsSchemaAdmissible() const
{
    if (!IsValid() || wheels.empty())
    {
        return false;
    }
    for (std::size_t i = 0U;
         i < wheels.size();
         ++i)
    {
        if (wheels[i].admission !=
            JBeamPressureWheelAdmission::
                SCHEMA_ADMISSIBLE_INVENTORY_ONLY)
        {
            return false;
        }
    }
    return true;
}

JBeamPressureWheelIR BuildJBeamPressureWheelIR(
    const JBeamResolvedGraph& graph,
    const JBeamPressureWheelLimits& limits)
{
    return PressureWheelBuilder(graph, limits).Run();
}

std::string SerializeCanonicalJBeamPressureWheelIR(
    const JBeamPressureWheelIR& ir)
{
    const std::size_t output_limit =
        std::min(
            ir.canonical_output_byte_limit,
            HARD_MAX_CANONICAL_OUTPUT_BYTES);
    const std::size_t depth_limit =
        std::min(
            ir.canonical_value_depth_limit,
            HARD_MAX_PRESERVED_VALUE_DEPTH);
    CanonicalWriter writer(output_limit);
    writer.String("ror-jbeam-pressure-wheel-ir-v1");
    writer.String(ir.documentation_profile_id);
    writer.Byte(static_cast<unsigned char>(
        ir.runtime_policy));
    writer.Size(ir.authored_entry_count);
    writer.Size(
        ir.approximation_generated_node_count);
    writer.Size(
        ir.approximation_generated_beam_count);
    writer.Size(ir.retained_byte_count);
    writer.Size(ir.parts.size());
    for (std::size_t i = 0U;
         i < ir.parts.size() && writer.Ok();
         ++i)
    {
        writer.Boolean(static_cast<bool>(ir.parts[i]));
        if (ir.parts[i])
        {
            writer.Size(
                ir.parts[i]->part_preorder_index);
            writer.String(ir.parts[i]->part_name);
            writer.String(ir.parts[i]->package_path);
        }
    }
    writer.Size(ir.source_records.size());
    for (std::size_t i = 0U;
         i < ir.source_records.size() && writer.Ok();
         ++i)
    {
        const JBeamPressureWheelSourceRecord& record =
            ir.source_records[i];
        writer.Byte(static_cast<unsigned char>(record.kind));
        writer.String(record.section_name);
        writer.Size(record.section_occurrence);
        WriteProvenance(writer, record.provenance);
        if (!WriteOptionalValue(
                writer,
                record.raw_value,
                depth_limit))
        {
            return std::string();
        }
    }
    writer.Size(ir.wheels.size());
    for (std::size_t i = 0U;
         i < ir.wheels.size() && writer.Ok();
         ++i)
    {
        const JBeamPressureWheel& wheel = ir.wheels[i];
        writer.Byte(static_cast<unsigned char>(
            wheel.admission));
        writer.Byte(static_cast<unsigned char>(
            wheel.runtime_policy));
        writer.Boolean(
            wheel.has_inert_or_unimplemented_behavior);
        writer.String(wheel.name);
        writer.String(wheel.hub_group);
        writer.String(wheel.group);
        writer.String(wheel.node1);
        writer.String(wheel.node2);
        writer.String(wheel.node_s);
        writer.Boolean(
            wheel.node_s_disables_legacy_stabilizer);
        writer.String(wheel.node_arm);
        writer.U64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                wheel.wheel_direction)));
        writer.Double(wheel.radius);
        writer.Double(wheel.hub_radius);
        writer.Double(wheel.wheel_offset);
        writer.Double(wheel.tire_width);
        writer.Double(wheel.hub_width);
        writer.Boolean(wheel.has_tire);
        writer.Size(wheel.num_rays);
        writer.Size(
            wheel.approximation_generated_nodes);
        writer.Size(
            wheel.approximation_base_generated_beams);
        writer.Size(
            wheel.approximation_stabilizer_beams);
        writer.Size(
            wheel.approximation_generated_beams);
        writer.Size(wheel.source_record_index);
        writer.Size(wheel.source_entry_index);
        WriteProvenance(writer, wheel.provenance);
        if (!WriteOptionalValue(
                writer,
                wheel.raw_row,
                depth_limit))
        {
            return std::string();
        }
        writer.Size(wheel.effective_fields.size());
        for (std::size_t field_index = 0U;
             field_index <
                 wheel.effective_fields.size() &&
                 writer.Ok();
             ++field_index)
        {
            const JBeamPressureWheelField& field =
                wheel.effective_fields[field_index];
            writer.String(field.name);
            writer.Byte(static_cast<unsigned char>(
                field.origin));
            WriteProvenance(writer, field.provenance);
            if (!WriteOptionalValue(
                    writer,
                    field.raw_value,
                    depth_limit))
            {
                return std::string();
            }
        }
    }
    writer.Size(ir.diagnostics.size());
    for (std::size_t i = 0U;
         i < ir.diagnostics.size() && writer.Ok();
         ++i)
    {
        const JBeamPressureWheelDiagnostic& diagnostic =
            ir.diagnostics[i];
        writer.Byte(static_cast<unsigned char>(
            diagnostic.code));
        writer.Byte(static_cast<unsigned char>(
            diagnostic.severity));
        WriteProvenance(writer, diagnostic.provenance);
        writer.String(diagnostic.section);
        writer.Size(diagnostic.row_index);
        writer.String(diagnostic.field_name);
        writer.String(diagnostic.detail);
    }
    return writer.Ok() ? writer.Data() : std::string();
}

const char* JBeamPressureWheelDiagnosticCodeToString(
    JBeamPressureWheelDiagnosticCode code)
{
    switch (code)
    {
    case JBeamPressureWheelDiagnosticCode::
        INVALID_RESOLVED_GRAPH:
        return "invalid-resolved-graph";
    case JBeamPressureWheelDiagnosticCode::
        RESOLVED_PART_LIMIT:
        return "resolved-part-limit";
    case JBeamPressureWheelDiagnosticCode::
        RESOLVED_GRAPH_CYCLE:
        return "resolved-graph-cycle";
    case JBeamPressureWheelDiagnosticCode::
        PART_BODY_NOT_OBJECT:
        return "part-body-not-object";
    case JBeamPressureWheelDiagnosticCode::
        SOURCE_RECORD_LIMIT:
        return "source-record-limit";
    case JBeamPressureWheelDiagnosticCode::ENTRY_LIMIT:
        return "entry-limit";
    case JBeamPressureWheelDiagnosticCode::WHEEL_LIMIT:
        return "wheel-limit";
    case JBeamPressureWheelDiagnosticCode::
        EFFECTIVE_FIELD_LIMIT:
        return "effective-field-limit";
    case JBeamPressureWheelDiagnosticCode::
        DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamPressureWheelDiagnosticCode::
        RETAINED_BYTE_LIMIT:
        return "retained-byte-limit";
    case JBeamPressureWheelDiagnosticCode::
        PRESERVED_VALUE_LIMIT:
        return "preserved-value-limit";
    case JBeamPressureWheelDiagnosticCode::INVALID_SECTION:
        return "invalid-section";
    case JBeamPressureWheelDiagnosticCode::DUPLICATE_SECTION:
        return "duplicate-section";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_TABLE_HEADER:
        return "invalid-table-header";
    case JBeamPressureWheelDiagnosticCode::
        DUPLICATE_TABLE_HEADER:
        return "duplicate-table-header";
    case JBeamPressureWheelDiagnosticCode::
        MISSING_REQUIRED_COLUMN:
        return "missing-required-column";
    case JBeamPressureWheelDiagnosticCode::
        AMBIGUOUS_REQUIRED_COLUMN:
        return "ambiguous-required-column";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_TABLE_ENTRY:
        return "invalid-table-entry";
    case JBeamPressureWheelDiagnosticCode::
        MISSING_REQUIRED_FIELD:
        return "missing-required-field";
    case JBeamPressureWheelDiagnosticCode::
        AMBIGUOUS_REQUIRED_FIELD:
        return "ambiguous-required-field";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_FIELD_TYPE:
        return "invalid-field-type";
    case JBeamPressureWheelDiagnosticCode::
        EXPRESSION_DISABLED:
        return "expression-disabled";
    case JBeamPressureWheelDiagnosticCode::
        NON_FINITE_NUMBER:
        return "non-finite-number";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_GEOMETRY:
        return "invalid-geometry";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_WHEEL_DIRECTION:
        return "invalid-wheel-direction";
    case JBeamPressureWheelDiagnosticCode::
        INVALID_NUM_RAYS:
        return "invalid-num-rays";
    case JBeamPressureWheelDiagnosticCode::
        TOPOLOGY_COUNT_OVERFLOW:
        return "topology-count-overflow";
    case JBeamPressureWheelDiagnosticCode::
        TOPOLOGY_NODE_LIMIT:
        return "topology-node-limit";
    case JBeamPressureWheelDiagnosticCode::
        TOPOLOGY_BEAM_LIMIT:
        return "topology-beam-limit";
    case JBeamPressureWheelDiagnosticCode::
        DOCUMENTATION_AMBIGUITY_PRESERVED:
        return "documentation-ambiguity-preserved";
    case JBeamPressureWheelDiagnosticCode::
        DOCUMENTED_BEHAVIOR_INERT:
        return "documented-behavior-inert";
    case JBeamPressureWheelDiagnosticCode::
        UNKNOWN_FIELD_PRESERVED:
        return "unknown-field-preserved";
    case JBeamPressureWheelDiagnosticCode::
        INERT_SECTION_PRESERVED:
        return "inert-section-preserved";
    case JBeamPressureWheelDiagnosticCode::
        INERT_SCALING_MODIFIER_PRESERVED:
        return "inert-scaling-modifier-preserved";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
