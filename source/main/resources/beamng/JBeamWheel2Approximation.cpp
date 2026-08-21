/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "JBeamWheel2Approximation.h"

#include "JBeamCoordinateTransform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace RoR {
namespace BeamNG {
namespace {

const std::size_t HARD_MAX_WHEELS = 64U;
const std::size_t HARD_MAX_GENERATED_NODES = 65535U;
const std::size_t HARD_MAX_GENERATED_BEAMS = 1000000U;
const std::size_t HARD_MAX_CANONICAL_OUTPUT_BYTES = 4U * 1024U * 1024U;
const std::size_t INVALID_INDEX = (std::numeric_limits<std::size_t>::max)();

bool TryAdd(std::size_t value, std::size_t& total)
{
    if (value > (std::numeric_limits<std::size_t>::max)() - total)
    {
        return false;
    }
    total += value;
    return true;
}

bool TryMultiply(std::size_t left, std::size_t right, std::size_t& output)
{
    output = 0U;
    if (left != 0U && right >
            (std::numeric_limits<std::size_t>::max)() / left)
    {
        return false;
    }
    output = left * right;
    return true;
}

bool IsFiniteBinary32(float value)
{
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value), "binary32 required");
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

bool IsNormalPositiveBinary32(float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t exponent = bits & UINT32_C(0x7f800000);
    return (bits & UINT32_C(0x80000000)) == 0U &&
        exponent != 0U && exponent != UINT32_C(0x7f800000);
}

bool TryNarrow(double value, bool positive, float& output)
{
    output = 0.0f;
    if (!Detail::IsFiniteBinary64(value) ||
        value > static_cast<double>((std::numeric_limits<float>::max)()) ||
        value < -static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return false;
    }
    const volatile float narrowed = static_cast<float>(value);
    output = narrowed;
    if (!IsFiniteBinary32(output))
    {
        output = 0.0f;
        return false;
    }
    if (positive && !IsNormalPositiveBinary32(output))
    {
        output = 0.0f;
        return false;
    }
    if (!positive && value != 0.0 && output == 0.0f)
    {
        return false;
    }
    return true;
}

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool EqualBinary32(float left, float right)
{
    return FloatBits(left) == FloatBits(right);
}

const JBeamPressureWheelField* FindField(
    const JBeamPressureWheel& wheel,
    const std::string& name)
{
    const std::vector<JBeamPressureWheelField>::const_iterator iterator =
        std::lower_bound(
            wheel.effective_fields.begin(),
            wheel.effective_fields.end(),
            name,
            [](const JBeamPressureWheelField& field,
               const std::string& key)
            {
                return field.name < key;
            });
    return iterator != wheel.effective_fields.end() && iterator->name == name
        ? &*iterator
        : NULL;
}

bool ReadNumber(
    const JBeamPressureWheel& wheel,
    const char* name,
    double& output)
{
    const JBeamPressureWheelField* const field = FindField(wheel, name);
    if (field == NULL || !field->raw_value ||
        field->raw_value->type != JBeamValueType::NUMBER ||
        !Detail::IsFiniteBinary64(field->raw_value->number_value))
    {
        output = 0.0;
        return false;
    }
    output = field->raw_value->number_value;
    return true;
}

bool ReadBoolean(
    const JBeamPressureWheel& wheel,
    const char* name,
    bool default_value,
    bool& output)
{
    const JBeamPressureWheelField* const field = FindField(wheel, name);
    if (field == NULL)
    {
        output = default_value;
        return true;
    }
    if (!field->raw_value ||
        field->raw_value->type != JBeamValueType::BOOLEAN)
    {
        return false;
    }
    output = field->raw_value->boolean_value;
    return true;
}

bool ReadOptionalZero(
    const JBeamPressureWheel& wheel,
    const char* name)
{
    const JBeamPressureWheelField* const field = FindField(wheel, name);
    return field == NULL ||
        (field->raw_value &&
         field->raw_value->type == JBeamValueType::NUMBER &&
         Detail::IsFiniteBinary64(field->raw_value->number_value) &&
         field->raw_value->number_value == 0.0);
}

bool IsAllowedField(const std::string& name)
{
    static const char* const ALLOWED[] = {
        "name", "hubGroup", "group", "node1:", "node1",
        "node2:", "node2", "nodeS", "nodeArm:", "nodeArm",
        "wheelDir", "radius", "hubRadius", "wheelOffset",
        "tireWidth", "hubWidth", "hasTire", "numRays",
        "nodeWeight", "hubNodeWeight", "hubBeamSpring", "hubBeamDamp",
        "wheelSideBeamSpring", "wheelSideBeamDamp", "collision",
        "selfCollision", "triangleCollision", "treadTriangleCollision",
        "side1TriangleCollision", "side2TriangleCollision",
        "hubTriangleCollision", "hubSide1TriangleCollision",
        "hubSide2TriangleCollision", "propulsed", "brakeTorque",
        "parkingTorque"
    };
    for (std::size_t i = 0U; i < sizeof(ALLOWED) / sizeof(ALLOWED[0]); ++i)
    {
        if (name == ALLOWED[i])
        {
            return true;
        }
    }
    return false;
}

const JBeamStructuralNode* FindNode(
    const JBeamStructuralIR& structural,
    const std::string& id,
    std::size_t& index)
{
    index = INVALID_INDEX;
    for (std::size_t i = 0U; i < structural.nodes.size(); ++i)
    {
        if (structural.nodes[i].id == id)
        {
            if (index != INVALID_INDEX)
            {
                return NULL;
            }
            index = i;
        }
    }
    return index == INVALID_INDEX ? NULL : &structural.nodes[index];
}

struct Point3
{
    float x;
    float y;
    float z;
};

bool TransformPoint(const JBeamStructuralNode& node, Point3& output)
{
    const JBeamPoint3 source(node.x, node.y, node.z);
    JBeamPoint3 transformed;
    if (!TryTransformBeamNGPointToRoR(source, &transformed))
    {
        return false;
    }
    return TryNarrow(transformed.x, false, output.x) &&
        TryNarrow(transformed.y, false, output.y) &&
        TryNarrow(transformed.z, false, output.z);
}

bool TryDifference(const Point3& left, const Point3& right, Point3& output)
{
    const volatile float x = left.x - right.x;
    const volatile float y = left.y - right.y;
    const volatile float z = left.z - right.z;
    output.x = x;
    output.y = y;
    output.z = z;
    return IsFiniteBinary32(output.x) && IsFiniteBinary32(output.y) &&
        IsFiniteBinary32(output.z);
}

bool TryLength(const Point3& vector, float& length)
{
    const volatile float xx = vector.x * vector.x;
    const volatile float yy = vector.y * vector.y;
    const volatile float zz = vector.z * vector.z;
    const volatile float sum_xy = xx + yy;
    const volatile float sum = sum_xy + zz;
    if (!IsFiniteBinary32(xx) || !IsFiniteBinary32(yy) ||
        !IsFiniteBinary32(zz) || !IsFiniteBinary32(sum_xy) ||
        !IsFiniteBinary32(sum) || !IsNormalPositiveBinary32(sum))
    {
        length = 0.0f;
        return false;
    }
    const volatile float result = std::sqrt(sum);
    length = result;
    return IsNormalPositiveBinary32(length);
}

bool ArmIsOffAxis(
    const Point3& axis_origin,
    const Point3& axis_end,
    const Point3& arm)
{
    Point3 axis;
    Point3 lever;
    if (!TryDifference(axis_end, axis_origin, axis) ||
        !TryDifference(arm, axis_origin, lever))
    {
        return false;
    }
    Point3 cross;
    const volatile float x = axis.y * lever.z - axis.z * lever.y;
    const volatile float y = axis.z * lever.x - axis.x * lever.z;
    const volatile float z = axis.x * lever.y - axis.y * lever.x;
    cross.x = x;
    cross.y = y;
    cross.z = z;
    float length = 0.0f;
    return IsFiniteBinary32(cross.x) && IsFiniteBinary32(cross.y) &&
        IsFiniteBinary32(cross.z) && TryLength(cross, length);
}

void Reject(
    JBeamWheel2ApproximationPlanSet& result,
    JBeamWheel2ApproximationCode code,
    std::size_t wheel_index,
    const char* detail)
{
    result.code = code;
    result.rejected_wheel_index = wheel_index;
    result.detail = detail;
    result.plans.clear();
    result.generated_node_count = 0U;
    result.generated_beam_count = 0U;
}

void AppendSize(std::string& output, std::size_t value)
{
    char reversed[3U * sizeof(std::size_t) + 1U];
    std::size_t count = 0U;
    do
    {
        reversed[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    }
    while (value != 0U);
    while (count != 0U)
    {
        output.push_back(reversed[--count]);
    }
}

void AppendString(std::string& output, const std::string& value)
{
    AppendSize(output, value.size());
    output.push_back(':');
    output.append(value);
}

void AppendHex32(std::string& output, std::uint32_t value)
{
    static const char HEX[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        output.push_back(HEX[(value >> shift) & 0x0fU]);
    }
}

} // namespace

JBeamWheel2ApproximationLimits::JBeamWheel2ApproximationLimits()
    : max_wheels(HARD_MAX_WHEELS)
    , max_generated_nodes(HARD_MAX_GENERATED_NODES)
    , max_generated_beams(HARD_MAX_GENERATED_BEAMS)
    , max_canonical_output_bytes(HARD_MAX_CANONICAL_OUTPUT_BYTES)
{
}

JBeamWheel2ApproximationPlan::JBeamWheel2ApproximationPlan()
    : source_wheel_index(0U)
    , source_record_index(0U)
    , source_entry_index(0U)
    , wheel_direction(0)
    , rim_radius(0.0f)
    , tyre_radius(0.0f)
    , width(0.0f)
    , num_rays(0U)
    , mass(0.0f)
    , rim_spring(0.0f)
    , rim_damping(0.0f)
    , tyre_spring(0.0f)
    , tyre_damping(0.0f)
    , approximated_semantics(0U)
{
}

JBeamWheel2ApproximationPlanSet::JBeamWheel2ApproximationPlanSet()
    : code(JBeamWheel2ApproximationCode::INTERNAL_FAILURE)
    , rejected_wheel_index(INVALID_INDEX)
    , generated_node_count(0U)
    , generated_beam_count(0U)
    , canonical_output_byte_limit(HARD_MAX_CANONICAL_OUTPUT_BYTES)
{
}

bool JBeamWheel2ApproximationPlanSet::IsAdmitted() const
{
    return code == JBeamWheel2ApproximationCode::ADMITTED && detail.empty() &&
        rejected_wheel_index == INVALID_INDEX;
}

JBeamWheel2ApproximationPlanSet BuildJBeamWheel2ApproximationPlanSet(
    const JBeamResolvedGraph& graph,
    const JBeamWheel2ApproximationLimits& requested_limits)
{
    JBeamWheel2ApproximationPlanSet result;
    result.canonical_output_byte_limit = std::min(
        requested_limits.max_canonical_output_bytes,
        HARD_MAX_CANONICAL_OUTPUT_BYTES);
    try
    {
        if (!graph.IsValid())
        {
            Reject(result, JBeamWheel2ApproximationCode::INVALID_STRUCTURAL_IR,
                INVALID_INDEX, "Resolved graph is not valid");
            return result;
        }
        const JBeamStructuralIR structural = BuildJBeamStructuralIR(graph);
        const JBeamPressureWheelIR pressure_wheels =
            BuildJBeamPressureWheelIR(graph);
        if (!structural.IsValid())
        {
            Reject(result, JBeamWheel2ApproximationCode::INVALID_STRUCTURAL_IR,
                INVALID_INDEX, "Structural IR is not valid");
            return result;
        }
        if (!pressure_wheels.IsValid())
        {
            Reject(result,
                JBeamWheel2ApproximationCode::INVALID_PRESSURE_WHEEL_IR,
                INVALID_INDEX, "Pressure-wheel inventory is not valid");
            return result;
        }
        for (std::size_t i = 0U; i < pressure_wheels.source_records.size(); ++i)
        {
            if (pressure_wheels.source_records[i].kind !=
                    JBeamPressureWheelSourceKind::PRESSURE_WHEELS)
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::UNSUPPORTED_SOURCE_SECTION,
                    INVALID_INDEX,
                    "Controller, powertrain, Lua, and scale sections remain inert");
                return result;
            }
        }
        const std::size_t wheel_limit = std::min(
            requested_limits.max_wheels, HARD_MAX_WHEELS);
        if (pressure_wheels.wheels.size() > wheel_limit)
        {
            Reject(result, JBeamWheel2ApproximationCode::WHEEL_LIMIT,
                INVALID_INDEX, "Wheel count exceeds the J3/RoR runtime limit");
            return result;
        }
        result.plans.reserve(pressure_wheels.wheels.size());
        std::set<std::string> names;
        for (std::size_t wheel_index = 0U;
             wheel_index < pressure_wheels.wheels.size(); ++wheel_index)
        {
            const JBeamPressureWheel& source =
                pressure_wheels.wheels[wheel_index];
            if (source.admission !=
                    JBeamPressureWheelAdmission::SCHEMA_ADMISSIBLE_INVENTORY_ONLY ||
                !names.insert(source.name).second)
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::INVALID_PRESSURE_WHEEL_IR,
                    wheel_index, "Wheel row is not uniquely schema-admissible");
                return result;
            }
            for (std::size_t field_index = 0U;
                 field_index < source.effective_fields.size(); ++field_index)
            {
                if (!IsAllowedField(source.effective_fields[field_index].name))
                {
                    Reject(result,
                        JBeamWheel2ApproximationCode::INVALID_SOURCE_FIELD_SET,
                        wheel_index,
                        "Wheel uses a field outside the bounded Wheel2 profile");
                    return result;
                }
            }
            if (!source.has_tire || source.node_s != "9999" ||
                !source.node_s_disables_legacy_stabilizer ||
                source.num_rays < 10U || source.num_rays > 20U ||
                source.num_rays % 2U != 0U || source.wheel_offset != 0.0 ||
                !(source.hub_radius > 0.0) ||
                !(source.radius > source.hub_radius))
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::UNSUPPORTED_GEOMETRY,
                    wheel_index,
                    "Wheel2 requires a centred tyre, no stabilizer, 10-20 even rays, and nested radii");
                return result;
            }

            bool collision = false;
            bool self_collision = true;
            bool triangle_collision = true;
            bool tread_collision = true;
            bool side1_collision = true;
            bool side2_collision = true;
            bool hub_collision = true;
            bool hub_side1_collision = true;
            bool hub_side2_collision = true;
            if (!ReadBoolean(source, "collision", true, collision) ||
                !ReadBoolean(source, "selfCollision", false, self_collision) ||
                !ReadBoolean(source, "triangleCollision", false,
                    triangle_collision) ||
                !ReadBoolean(source, "treadTriangleCollision", false,
                    tread_collision) ||
                !ReadBoolean(source, "side1TriangleCollision", false,
                    side1_collision) ||
                !ReadBoolean(source, "side2TriangleCollision", false,
                    side2_collision) ||
                !ReadBoolean(source, "hubTriangleCollision", false,
                    hub_collision) ||
                !ReadBoolean(source, "hubSide1TriangleCollision", false,
                    hub_side1_collision) ||
                !ReadBoolean(source, "hubSide2TriangleCollision", false,
                    hub_side2_collision) ||
                !collision || self_collision || triangle_collision ||
                tread_collision || side1_collision || side2_collision ||
                hub_collision || hub_side1_collision || hub_side2_collision)
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::UNSUPPORTED_COLLISION_MODE,
                    wheel_index,
                    "Only external node collision with all generated triangle collision disabled is admitted");
                return result;
            }
            if (!ReadOptionalZero(source, "propulsed") ||
                !ReadOptionalZero(source, "brakeTorque") ||
                !ReadOptionalZero(source, "parkingTorque"))
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::UNSUPPORTED_PROPULSION_OR_BRAKING,
                    wheel_index,
                    "The first Wheel2 slice is unpropelled and unbraked");
                return result;
            }

            std::size_t node1_index = INVALID_INDEX;
            std::size_t node2_index = INVALID_INDEX;
            std::size_t arm_index = INVALID_INDEX;
            const JBeamStructuralNode* const node1 =
                FindNode(structural, source.node1, node1_index);
            const JBeamStructuralNode* const node2 =
                FindNode(structural, source.node2, node2_index);
            const JBeamStructuralNode* const arm =
                FindNode(structural, source.node_arm, arm_index);
            if (node1 == NULL || node2 == NULL || arm == NULL ||
                node1_index == node2_index || node1_index == arm_index ||
                node2_index == arm_index)
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::INVALID_NODE_REFERENCE,
                    wheel_index,
                    "Axle and reference-arm nodes must resolve uniquely and be distinct");
                return result;
            }
            Point3 point1;
            Point3 point2;
            Point3 arm_point;
            Point3 axis;
            float axis_length = 0.0f;
            if (!TransformPoint(*node1, point1) ||
                !TransformPoint(*node2, point2) ||
                !TransformPoint(*arm, arm_point) ||
                !TryDifference(point2, point1, axis) ||
                !TryLength(axis, axis_length) ||
                !ArmIsOffAxis(point1, point2, arm_point))
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::INVALID_AXIS_GEOMETRY,
                    wheel_index,
                    "Axle must be finite/nondegenerate and the reaction arm must be off-axis");
                return result;
            }

            JBeamWheel2ApproximationPlan plan;
            plan.source_wheel_index = wheel_index;
            plan.source_record_index = source.source_record_index;
            plan.source_entry_index = source.source_entry_index;
            plan.name = source.name;
            plan.node1 = source.node1;
            plan.node2 = source.node2;
            plan.node_arm = source.node_arm;
            plan.wheel_direction = source.wheel_direction;
            plan.num_rays = static_cast<unsigned int>(source.num_rays);
            plan.approximated_semantics =
                JBEAM_WHEEL2_APPROXIMATION_SEMANTICS;
            float tire_width = 0.0f;
            float hub_width = 0.0f;
            if (!TryNarrow(source.hub_radius, true, plan.rim_radius) ||
                !TryNarrow(source.radius, true, plan.tyre_radius) ||
                !TryNarrow(source.tire_width, true, tire_width) ||
                !TryNarrow(source.hub_width, true, hub_width) ||
                !EqualBinary32(tire_width, axis_length) ||
                !EqualBinary32(hub_width, axis_length))
            {
                Reject(result, JBeamWheel2ApproximationCode::FLOAT_NARROWING,
                    wheel_index,
                    "Radii/widths must narrow exactly enough for the native axis, and authored widths must equal it");
                return result;
            }
            plan.width = axis_length;

            double node_weight = 0.0;
            double hub_node_weight = 0.0;
            if (!ReadNumber(source, "nodeWeight", node_weight) ||
                !ReadNumber(source, "hubNodeWeight", hub_node_weight) ||
                !(node_weight > 0.0) || !(hub_node_weight > 0.0))
            {
                Reject(result, JBeamWheel2ApproximationCode::INVALID_MASS,
                    wheel_index,
                    "Explicit positive nodeWeight and hubNodeWeight are required");
                return result;
            }
            const double mass =
                2.0 * static_cast<double>(source.num_rays) *
                (node_weight + hub_node_weight);
            if (!TryNarrow(mass, true, plan.mass))
            {
                Reject(result, JBeamWheel2ApproximationCode::INVALID_MASS,
                    wheel_index, "Generated wheel mass does not fit binary32");
                return result;
            }

            double rim_spring = 0.0;
            double rim_damping = 0.0;
            double tyre_spring = 0.0;
            double tyre_damping = 0.0;
            if (!ReadNumber(source, "hubBeamSpring", rim_spring) ||
                !ReadNumber(source, "hubBeamDamp", rim_damping) ||
                !ReadNumber(source, "wheelSideBeamSpring", tyre_spring) ||
                !ReadNumber(source, "wheelSideBeamDamp", tyre_damping) ||
                !(rim_spring > 0.0) || rim_damping < 0.0 ||
                !(tyre_spring > 0.0) || tyre_damping < 0.0 ||
                !TryNarrow(rim_spring, true, plan.rim_spring) ||
                !TryNarrow(rim_damping, false, plan.rim_damping) ||
                !TryNarrow(tyre_spring, true, plan.tyre_spring) ||
                !TryNarrow(tyre_damping, false, plan.tyre_damping))
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::INVALID_BEAM_PARAMETERS,
                    wheel_index,
                    "Explicit finite hub and wheel-side spring/damping values are required");
                return result;
            }

            std::size_t generated_nodes = 0U;
            std::size_t generated_beams = 0U;
            if (!TryMultiply(source.num_rays, 4U, generated_nodes) ||
                !TryMultiply(source.num_rays, 24U, generated_beams) ||
                !TryAdd(generated_nodes, result.generated_node_count) ||
                !TryAdd(generated_beams, result.generated_beam_count))
            {
                Reject(result,
                    JBeamWheel2ApproximationCode::GENERATED_TOPOLOGY_LIMIT,
                    wheel_index, "Generated topology count overflowed");
                return result;
            }
            result.plans.push_back(std::move(plan));
        }

        const std::size_t node_limit = std::min(
            requested_limits.max_generated_nodes,
            HARD_MAX_GENERATED_NODES);
        const std::size_t beam_limit = std::min(
            requested_limits.max_generated_beams,
            HARD_MAX_GENERATED_BEAMS);
        if (result.generated_node_count > node_limit ||
            result.generated_beam_count > beam_limit ||
            structural.nodes.size() >
                HARD_MAX_GENERATED_NODES - result.generated_node_count ||
            structural.beams.size() >
                HARD_MAX_GENERATED_BEAMS - result.generated_beam_count)
        {
            Reject(result,
                JBeamWheel2ApproximationCode::GENERATED_TOPOLOGY_LIMIT,
                INVALID_INDEX,
                "Generated pressure-wheel topology exceeds configured or runtime limits");
            return result;
        }
        result.code = JBeamWheel2ApproximationCode::ADMITTED;
        result.rejected_wheel_index = INVALID_INDEX;
        result.detail.clear();
        return result;
    }
    catch (const std::bad_alloc&)
    {
        Reject(result, JBeamWheel2ApproximationCode::ALLOCATION_FAILURE,
            INVALID_INDEX, "Allocation failed before plan publication");
        return result;
    }
    catch (...)
    {
        Reject(result, JBeamWheel2ApproximationCode::INTERNAL_FAILURE,
            INVALID_INDEX, "Unexpected failure before plan publication");
        return result;
    }
}

std::string SerializeCanonicalJBeamWheel2ApproximationPlanSet(
    const JBeamWheel2ApproximationPlanSet& plans)
{
    if (!plans.IsAdmitted() || plans.canonical_output_byte_limit == 0U ||
        plans.canonical_output_byte_limit > HARD_MAX_CANONICAL_OUTPUT_BYTES)
    {
        return std::string();
    }
    std::string output;
    output.reserve(std::min<std::size_t>(
        plans.canonical_output_byte_limit, 4096U));
    output.append("ror-jbeam-wheel2-approximation-v1;");
    AppendSize(output, plans.plans.size());
    output.push_back(';');
    AppendSize(output, plans.generated_node_count);
    output.push_back(';');
    AppendSize(output, plans.generated_beam_count);
    output.push_back(';');
    for (std::size_t i = 0U; i < plans.plans.size(); ++i)
    {
        const JBeamWheel2ApproximationPlan& plan = plans.plans[i];
        AppendSize(output, plan.source_wheel_index);
        output.push_back(';');
        AppendSize(output, plan.source_record_index);
        output.push_back(';');
        AppendSize(output, plan.source_entry_index);
        output.push_back(';');
        AppendString(output, plan.name);
        AppendString(output, plan.node1);
        AppendString(output, plan.node2);
        AppendString(output, plan.node_arm);
        output.push_back(plan.wheel_direction == 1 ? '+' : '-');
        AppendHex32(output, FloatBits(plan.rim_radius));
        AppendHex32(output, FloatBits(plan.tyre_radius));
        AppendHex32(output, FloatBits(plan.width));
        AppendSize(output, plan.num_rays);
        output.push_back(';');
        AppendHex32(output, FloatBits(plan.mass));
        AppendHex32(output, FloatBits(plan.rim_spring));
        AppendHex32(output, FloatBits(plan.rim_damping));
        AppendHex32(output, FloatBits(plan.tyre_spring));
        AppendHex32(output, FloatBits(plan.tyre_damping));
        AppendHex32(output, plan.approximated_semantics);
        output.push_back(';');
        if (output.size() > plans.canonical_output_byte_limit)
        {
            return std::string();
        }
    }
    return output.size() <= plans.canonical_output_byte_limit
        ? output
        : std::string();
}

const char* JBeamWheel2ApproximationCodeToString(
    JBeamWheel2ApproximationCode code)
{
    switch (code)
    {
    case JBeamWheel2ApproximationCode::ADMITTED:
        return "admitted";
    case JBeamWheel2ApproximationCode::INVALID_STRUCTURAL_IR:
        return "invalid-structural-ir";
    case JBeamWheel2ApproximationCode::INVALID_PRESSURE_WHEEL_IR:
        return "invalid-pressure-wheel-ir";
    case JBeamWheel2ApproximationCode::UNSUPPORTED_SOURCE_SECTION:
        return "unsupported-source-section";
    case JBeamWheel2ApproximationCode::WHEEL_LIMIT:
        return "wheel-limit";
    case JBeamWheel2ApproximationCode::GENERATED_TOPOLOGY_LIMIT:
        return "generated-topology-limit";
    case JBeamWheel2ApproximationCode::INVALID_SOURCE_FIELD_SET:
        return "invalid-source-field-set";
    case JBeamWheel2ApproximationCode::INVALID_NODE_REFERENCE:
        return "invalid-node-reference";
    case JBeamWheel2ApproximationCode::INVALID_AXIS_GEOMETRY:
        return "invalid-axis-geometry";
    case JBeamWheel2ApproximationCode::UNSUPPORTED_GEOMETRY:
        return "unsupported-geometry";
    case JBeamWheel2ApproximationCode::UNSUPPORTED_COLLISION_MODE:
        return "unsupported-collision-mode";
    case JBeamWheel2ApproximationCode::UNSUPPORTED_PROPULSION_OR_BRAKING:
        return "unsupported-propulsion-or-braking";
    case JBeamWheel2ApproximationCode::INVALID_MASS:
        return "invalid-mass";
    case JBeamWheel2ApproximationCode::INVALID_BEAM_PARAMETERS:
        return "invalid-beam-parameters";
    case JBeamWheel2ApproximationCode::FLOAT_NARROWING:
        return "float-narrowing";
    case JBeamWheel2ApproximationCode::ALLOCATION_FAILURE:
        return "allocation-failure";
    case JBeamWheel2ApproximationCode::INTERNAL_FAILURE:
        return "internal-failure";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
