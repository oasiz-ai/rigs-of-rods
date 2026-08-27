/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

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

#include "ContactConservation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace RoR {
namespace ContactConservation {
namespace {

static const double BARYCENTRIC_SUM_TOLERANCE =
    64.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
static const double TRIANGLE_AREA_TOLERANCE =
    256.0 * std::numeric_limits<double>::epsilon();

bool IsFinite(double value)
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "64-bit IEEE-754 double required");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "IEEE-754 double required");

    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint64_t observed_bits = bits;
    return (observed_bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool IsFinite(float value)
{
    static_assert(
        sizeof(float) == sizeof(std::uint32_t),
        "32-bit IEEE-754 float required");
    static_assert(
        std::numeric_limits<float>::is_iec559,
        "IEEE-754 float required");

    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint32_t observed_bits = bits;
    return (observed_bits & UINT32_C(0x7f800000)) !=
        UINT32_C(0x7f800000);
}

bool IsFinite(const Vector3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const AppliedVector3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

Vector3 Widen(const AppliedVector3& value)
{
    Vector3 widened;
    widened.x = static_cast<double>(value.x);
    widened.y = static_cast<double>(value.y);
    widened.z = static_cast<double>(value.z);
    return widened;
}

Vector3 Add(const Vector3& first, const Vector3& second)
{
    Vector3 result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    return result;
}

Vector3 Subtract(const Vector3& first, const Vector3& second)
{
    Vector3 result;
    result.x = first.x - second.x;
    result.y = first.y - second.y;
    result.z = first.z - second.z;
    return result;
}

Vector3 Scale(const Vector3& value, double scalar)
{
    Vector3 result;
    result.x = value.x * scalar;
    result.y = value.y * scalar;
    result.z = value.z * scalar;
    return result;
}

Vector3 Cross(const Vector3& first, const Vector3& second)
{
    Vector3 result;
    result.x = first.y * second.z - first.z * second.y;
    result.y = first.z * second.x - first.x * second.z;
    result.z = first.x * second.y - first.y * second.x;
    return result;
}

double Dot(const Vector3& first, const Vector3& second)
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

bool TryNorm(const Vector3& value, double& norm)
{
    const double scale = std::max(
        std::abs(value.x),
        std::max(std::abs(value.y), std::abs(value.z)));
    if (!IsFinite(scale))
        return false;
    if (scale == 0.0)
    {
        norm = 0.0;
        return true;
    }

    const double x = value.x / scale;
    const double y = value.y / scale;
    const double z = value.z / scale;
    const double scaled_squared = x * x + y * y + z * z;
    if (!IsFinite(scaled_squared) || scaled_squared <= 0.0)
        return false;
    norm = scale * std::sqrt(scaled_squared);
    return IsFinite(norm);
}

bool IsFinite(const NodeState& node)
{
    return IsFinite(node.position_m) &&
        IsFinite(node.velocity_mps) &&
        IsFinite(node.mass_kg);
}

bool HasValidMobility(const NodeState& node)
{
    return node.movable == 0 || node.movable == 1;
}

bool IsTriangleNondegenerate(
    const std::array<NodeState, 3>& surface_nodes)
{
    const Vector3 edge_ab = Subtract(
        surface_nodes[1].position_m,
        surface_nodes[0].position_m);
    const Vector3 edge_ac = Subtract(
        surface_nodes[2].position_m,
        surface_nodes[0].position_m);
    if (!IsFinite(edge_ab) || !IsFinite(edge_ac))
        return false;

    const double scale = std::max(
        std::max(
            std::abs(edge_ab.x),
            std::max(std::abs(edge_ab.y), std::abs(edge_ab.z))),
        std::max(
            std::abs(edge_ac.x),
            std::max(std::abs(edge_ac.y), std::abs(edge_ac.z))));
    if (!IsFinite(scale) || scale <= 0.0)
        return false;

    const Vector3 scaled_ab = Scale(edge_ab, 1.0 / scale);
    const Vector3 scaled_ac = Scale(edge_ac, 1.0 / scale);
    const Vector3 scaled_cross = Cross(scaled_ab, scaled_ac);
    double scaled_area = 0.0;
    return IsFinite(scaled_cross) &&
        TryNorm(scaled_cross, scaled_area) &&
        scaled_area > TRIANGLE_AREA_TOLERANCE;
}

bool TryNormalizeBarycentric(
    const std::array<double, 3>& input,
    std::array<double, 3>& output)
{
    for (std::size_t index = 0; index < input.size(); ++index)
    {
        if (!IsFinite(input[index]) ||
            input[index] < 0.0 ||
            input[index] > 1.0)
        {
            return false;
        }
    }

    const double sum = (input[0] + input[1]) + input[2];
    if (!IsFinite(sum) ||
        sum <= 0.0 ||
        std::abs(sum - 1.0) > BARYCENTRIC_SUM_TOLERANCE)
    {
        return false;
    }

    output[0] = input[0] / sum;
    output[1] = input[1] / sum;
    output[2] = 1.0 - output[0] - output[1];
    return IsFinite(output[0]) &&
        IsFinite(output[1]) &&
        IsFinite(output[2]) &&
        output[0] >= 0.0 &&
        output[1] >= 0.0 &&
        output[2] >= 0.0 &&
        output[0] <= 1.0 &&
        output[1] <= 1.0 &&
        output[2] <= 1.0;
}

bool TryAccumulateScalar(double term, double& sum)
{
    if (!IsFinite(term))
        return false;
    sum += term;
    return IsFinite(sum);
}

Error PrepareTelemetryBase(const Input& input, Telemetry& candidate)
{
    if (input.schema_version != SCHEMA_VERSION)
        return Error::UNSUPPORTED_SCHEMA;
    if (!IsFinite(input.hit_node) ||
        !IsFinite(input.force_on_hit_n) ||
        !IsFinite(input.time_step_s))
    {
        return Error::NONFINITE_INPUT;
    }
    for (std::size_t index = 0; index < input.surface_nodes.size(); ++index)
    {
        if (!IsFinite(input.surface_nodes[index]))
            return Error::NONFINITE_INPUT;
    }
    for (std::size_t index = 0; index < input.barycentric.size(); ++index)
    {
        if (!IsFinite(input.barycentric[index]))
            return Error::NONFINITE_INPUT;
    }
    if (input.time_step_s <= 0.0)
        return Error::INVALID_TIME_STEP;
    if (input.hit_node.mass_kg <= 0.0)
        return Error::INVALID_MASS;
    for (std::size_t index = 0; index < input.surface_nodes.size(); ++index)
    {
        if (input.surface_nodes[index].mass_kg <= 0.0)
            return Error::INVALID_MASS;
    }
    if (!HasValidMobility(input.hit_node))
        return Error::INVALID_NODE_MOBILITY;
    for (std::size_t index = 0; index < input.surface_nodes.size(); ++index)
    {
        if (!HasValidMobility(input.surface_nodes[index]))
            return Error::INVALID_NODE_MOBILITY;
    }
    if (!TryNormalizeBarycentric(
            input.barycentric,
            candidate.normalized_barycentric))
    {
        return Error::INVALID_BARYCENTRIC_COORDINATES;
    }
    if (!IsTriangleNondegenerate(input.surface_nodes))
        return Error::DEGENERATE_SURFACE_TRIANGLE;

    const Vector3 edge_ab = Subtract(
        input.surface_nodes[1].position_m,
        input.surface_nodes[0].position_m);
    const Vector3 edge_ac = Subtract(
        input.surface_nodes[2].position_m,
        input.surface_nodes[0].position_m);
    candidate.surface_contact_position_m = Add(
        input.surface_nodes[0].position_m,
        Add(
            Scale(edge_ab, candidate.normalized_barycentric[1]),
            Scale(edge_ac, candidate.normalized_barycentric[2])));
    if (!IsFinite(candidate.surface_contact_position_m))
        return Error::NUMERIC_RANGE;
    return Error::NONE;
}

Error CompleteTelemetry(const Input& input, Telemetry& candidate)
{
    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        if (!IsFinite(candidate.applied_forces_n[index]) ||
            !IsFinite(
                candidate.observed_force_accumulator_deltas_n[index]))
        {
            return Error::FORCE_OUT_OF_BINARY32_RANGE;
        }
        candidate.impulses_ns[index] = Scale(
            candidate.observed_force_accumulator_deltas_n[index],
            input.time_step_s);
        if (!IsFinite(candidate.impulses_ns[index]))
            return Error::NUMERIC_RANGE;
    }

    candidate.linear_impulse_residual_ns = Add(
        Add(
            candidate.impulses_ns[HIT_NODE_INDEX],
            candidate.impulses_ns[SURFACE_NODE_A_INDEX]),
        Add(
            candidate.impulses_ns[SURFACE_NODE_B_INDEX],
            candidate.impulses_ns[SURFACE_NODE_C_INDEX]));
    double residual_norm = 0.0;
    if (!IsFinite(candidate.linear_impulse_residual_ns) ||
        !TryNorm(candidate.linear_impulse_residual_ns, residual_norm))
    {
        return Error::NUMERIC_RANGE;
    }

    double impulse_scale = 0.0;
    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        double impulse_norm = 0.0;
        if (!TryNorm(candidate.impulses_ns[index], impulse_norm) ||
            !TryAccumulateScalar(impulse_norm, impulse_scale))
        {
            return Error::NUMERIC_RANGE;
        }
    }
    candidate.normalized_linear_impulse_residual =
        impulse_scale == 0.0 ? 0.0 : residual_norm / impulse_scale;
    if (!IsFinite(candidate.normalized_linear_impulse_residual))
        return Error::NUMERIC_RANGE;

    const std::array<NodeState, NODE_COUNT> nodes = {{
        input.hit_node,
        input.surface_nodes[0],
        input.surface_nodes[1],
        input.surface_nodes[2]
    }};
    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        const Vector3 lever = Subtract(
            nodes[index].position_m,
            candidate.surface_contact_position_m);
        const Vector3 angular_increment = Cross(
            lever,
            candidate.impulses_ns[index]);
        if (!IsFinite(lever) || !IsFinite(angular_increment))
            return Error::NUMERIC_RANGE;
        candidate.angular_impulse_delta_nms = Add(
            candidate.angular_impulse_delta_nms,
            angular_increment);
        if (!IsFinite(candidate.angular_impulse_delta_nms))
            return Error::NUMERIC_RANGE;

        if (nodes[index].movable != 0)
        {
            const double work_increment = Dot(
                nodes[index].velocity_mps,
                candidate.impulses_ns[index]);
            const double impulse_squared = Dot(
                candidate.impulses_ns[index],
                candidate.impulses_ns[index]);
            const double integration_increment =
                0.5 * impulse_squared / nodes[index].mass_kg;
            if (!TryAccumulateScalar(
                    work_increment,
                    candidate.isolated_contact_work_j) ||
                !TryAccumulateScalar(
                    integration_increment,
                    candidate.isolated_contact_integration_energy_delta_j))
            {
                return Error::NUMERIC_RANGE;
            }
        }
    }
    if (!TryNorm(
            candidate.angular_impulse_delta_nms,
            candidate.angular_impulse_delta_magnitude_nms))
    {
        return Error::NUMERIC_RANGE;
    }
    candidate.isolated_contact_kinetic_energy_delta_j =
        candidate.isolated_contact_work_j +
        candidate.isolated_contact_integration_energy_delta_j;
    if (!IsFinite(candidate.isolated_contact_kinetic_energy_delta_j))
        return Error::NUMERIC_RANGE;
    return Error::NONE;
}

} // namespace

const char* ErrorToString(Error error)
{
    switch (error)
    {
    case Error::NONE:
        return "none";
    case Error::UNSUPPORTED_SCHEMA:
        return "unsupported schema";
    case Error::NONFINITE_INPUT:
        return "non-finite input";
    case Error::INVALID_TIME_STEP:
        return "invalid time step";
    case Error::INVALID_MASS:
        return "invalid mass";
    case Error::INVALID_NODE_MOBILITY:
        return "invalid node mobility";
    case Error::INVALID_BARYCENTRIC_COORDINATES:
        return "invalid barycentric coordinates";
    case Error::DEGENERATE_SURFACE_TRIANGLE:
        return "degenerate surface triangle";
    case Error::FORCE_OUT_OF_BINARY32_RANGE:
        return "force out of binary32 range";
    case Error::LINEAR_IMPULSE_RESIDUAL_EXCEEDED:
        return "linear impulse residual exceeded";
    case Error::INVALID_TELEMETRY:
        return "invalid telemetry";
    case Error::CONTACT_LIMIT_EXCEEDED:
        return "contact limit exceeded";
    case Error::NUMERIC_RANGE:
        return "numeric range";
    }
    return "unknown error";
}

bool IsSolverMovable(bool node_immovable, bool actor_networked)
{
    return !node_immovable && !actor_networked;
}

Error Evaluate(const Input& input, Telemetry& output)
{
    Telemetry candidate;
    const Error preparation_error = PrepareTelemetryBase(input, candidate);
    if (preparation_error != Error::NONE)
        return preparation_error;

    AppliedVector3& hit_force =
        candidate.applied_forces_n[HIT_NODE_INDEX];
    AppliedVector3& surface_a_force =
        candidate.applied_forces_n[SURFACE_NODE_A_INDEX];
    AppliedVector3& surface_b_force =
        candidate.applied_forces_n[SURFACE_NODE_B_INDEX];
    AppliedVector3& surface_c_force =
        candidate.applied_forces_n[SURFACE_NODE_C_INDEX];
    const double input_components[] = {
        input.force_on_hit_n.x,
        input.force_on_hit_n.y,
        input.force_on_hit_n.z
    };
    float* hit_components[] = {&hit_force.x, &hit_force.y, &hit_force.z};
    float* surface_a_components[] = {
        &surface_a_force.x,
        &surface_a_force.y,
        &surface_a_force.z
    };
    float* surface_b_components[] = {
        &surface_b_force.x,
        &surface_b_force.y,
        &surface_b_force.z
    };
    float* surface_c_components[] = {
        &surface_c_force.x,
        &surface_c_force.y,
        &surface_c_force.z
    };
    for (std::size_t component = 0; component < 3; ++component)
    {
        *hit_components[component] =
            static_cast<float>(input_components[component]);
        if (!IsFinite(*hit_components[component]))
            return Error::FORCE_OUT_OF_BINARY32_RANGE;
        *surface_a_components[component] = static_cast<float>(
            -static_cast<double>(*hit_components[component]) *
            candidate.normalized_barycentric[0]);
        *surface_b_components[component] = static_cast<float>(
            -static_cast<double>(*hit_components[component]) *
            candidate.normalized_barycentric[1]);
        if (!IsFinite(*surface_a_components[component]) ||
            !IsFinite(*surface_b_components[component]))
        {
            return Error::FORCE_OUT_OF_BINARY32_RANGE;
        }

        // The final binary32 component closes against the exact stable-order
        // partial sum production will apply. Its real-number residual is then
        // measured after widening all four narrowed values below.
        const float partial =
            (*hit_components[component] +
                *surface_a_components[component]) +
            *surface_b_components[component];
        *surface_c_components[component] = -partial;
        if (!IsFinite(partial) || !IsFinite(*surface_c_components[component]))
            return Error::FORCE_OUT_OF_BINARY32_RANGE;
    }

    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        candidate.observed_force_accumulator_deltas_n[index] =
            Widen(candidate.applied_forces_n[index]);
    }
    const Error telemetry_error = CompleteTelemetry(input, candidate);
    if (telemetry_error != Error::NONE)
        return telemetry_error;

    output = candidate;
    return Error::NONE;
}

Error AuditAppliedForces(
    const Input& input,
    const std::array<AppliedVector3, NODE_COUNT>& applied_forces_n,
    const std::array<AppliedVector3, NODE_COUNT>& accumulator_before_n,
    const std::array<AppliedVector3, NODE_COUNT>& accumulator_after_n,
    Telemetry& output)
{
    Telemetry candidate;
    const Error preparation_error = PrepareTelemetryBase(input, candidate);
    if (preparation_error != Error::NONE)
        return preparation_error;
    candidate.applied_forces_n = applied_forces_n;
    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        if (!IsFinite(accumulator_before_n[index]) ||
            !IsFinite(accumulator_after_n[index]))
        {
            return Error::FORCE_OUT_OF_BINARY32_RANGE;
        }
        candidate.observed_force_accumulator_deltas_n[index].x =
            static_cast<double>(accumulator_after_n[index].x) -
            static_cast<double>(accumulator_before_n[index].x);
        candidate.observed_force_accumulator_deltas_n[index].y =
            static_cast<double>(accumulator_after_n[index].y) -
            static_cast<double>(accumulator_before_n[index].y);
        candidate.observed_force_accumulator_deltas_n[index].z =
            static_cast<double>(accumulator_after_n[index].z) -
            static_cast<double>(accumulator_before_n[index].z);
    }
    const Error telemetry_error = CompleteTelemetry(input, candidate);
    if (telemetry_error != Error::NONE)
        return telemetry_error;
    output = candidate;
    return Error::NONE;
}

Error Accumulate(const Telemetry& telemetry, Aggregate& aggregate)
{
    if (aggregate.schema_version != SCHEMA_VERSION)
        return Error::UNSUPPORTED_SCHEMA;
    if (aggregate.contact_count >= MAX_AGGREGATE_CONTACTS)
        return Error::CONTACT_LIMIT_EXCEEDED;
    if (!IsFinite(aggregate.maximum_normalized_linear_impulse_residual) ||
        !IsFinite(aggregate.maximum_angular_impulse_delta_magnitude_nms) ||
        !IsFinite(aggregate.summed_angular_impulse_delta_nms) ||
        !IsFinite(aggregate.summed_isolated_contact_work_j) ||
        !IsFinite(
            aggregate.summed_isolated_contact_kinetic_energy_delta_j) ||
        !IsFinite(
            aggregate.summed_isolated_contact_integration_energy_delta_j) ||
        aggregate.maximum_normalized_linear_impulse_residual < 0.0 ||
        aggregate.maximum_angular_impulse_delta_magnitude_nms < 0.0 ||
        aggregate.summed_isolated_contact_integration_energy_delta_j < 0.0 ||
        aggregate.summed_isolated_contact_kinetic_energy_delta_j !=
            aggregate.summed_isolated_contact_work_j +
            aggregate.summed_isolated_contact_integration_energy_delta_j)
    {
        return Error::INVALID_TELEMETRY;
    }
    if (!IsFinite(telemetry.surface_contact_position_m) ||
        !IsFinite(telemetry.linear_impulse_residual_ns) ||
        !IsFinite(telemetry.normalized_linear_impulse_residual) ||
        !IsFinite(telemetry.angular_impulse_delta_nms) ||
        !IsFinite(telemetry.angular_impulse_delta_magnitude_nms) ||
        !IsFinite(telemetry.isolated_contact_work_j) ||
        !IsFinite(telemetry.isolated_contact_kinetic_energy_delta_j) ||
        !IsFinite(telemetry.isolated_contact_integration_energy_delta_j) ||
        telemetry.normalized_linear_impulse_residual < 0.0 ||
        telemetry.angular_impulse_delta_magnitude_nms < 0.0 ||
        telemetry.isolated_contact_integration_energy_delta_j < 0.0 ||
        telemetry.isolated_contact_kinetic_energy_delta_j !=
            telemetry.isolated_contact_work_j +
            telemetry.isolated_contact_integration_energy_delta_j)
    {
        return Error::INVALID_TELEMETRY;
    }
    for (std::size_t index = 0;
        index < telemetry.normalized_barycentric.size();
        ++index)
    {
        if (!IsFinite(telemetry.normalized_barycentric[index]) ||
            telemetry.normalized_barycentric[index] < 0.0 ||
            telemetry.normalized_barycentric[index] > 1.0)
        {
            return Error::INVALID_TELEMETRY;
        }
    }
    for (std::size_t index = 0; index < NODE_COUNT; ++index)
    {
        if (!IsFinite(telemetry.applied_forces_n[index]) ||
            !IsFinite(
                telemetry.observed_force_accumulator_deltas_n[index]) ||
            !IsFinite(telemetry.impulses_ns[index]))
        {
            return Error::INVALID_TELEMETRY;
        }
    }

    Aggregate candidate = aggregate;
    ++candidate.contact_count;
    candidate.maximum_normalized_linear_impulse_residual = std::max(
        candidate.maximum_normalized_linear_impulse_residual,
        telemetry.normalized_linear_impulse_residual);
    candidate.maximum_angular_impulse_delta_magnitude_nms = std::max(
        candidate.maximum_angular_impulse_delta_magnitude_nms,
        telemetry.angular_impulse_delta_magnitude_nms);
    candidate.summed_angular_impulse_delta_nms = Add(
        candidate.summed_angular_impulse_delta_nms,
        telemetry.angular_impulse_delta_nms);
    if (!TryAccumulateScalar(
            telemetry.isolated_contact_work_j,
            candidate.summed_isolated_contact_work_j) ||
        !TryAccumulateScalar(
            telemetry.isolated_contact_integration_energy_delta_j,
            candidate.
                summed_isolated_contact_integration_energy_delta_j) ||
        !IsFinite(candidate.summed_angular_impulse_delta_nms) ||
        !IsFinite(candidate.maximum_normalized_linear_impulse_residual) ||
        !IsFinite(candidate.maximum_angular_impulse_delta_magnitude_nms))
    {
        return Error::NUMERIC_RANGE;
    }
    candidate.summed_isolated_contact_kinetic_energy_delta_j =
        candidate.summed_isolated_contact_work_j +
        candidate.summed_isolated_contact_integration_energy_delta_j;
    if (!IsFinite(
            candidate.summed_isolated_contact_kinetic_energy_delta_j))
    {
        return Error::NUMERIC_RANGE;
    }
    aggregate = candidate;
    return Error::NONE;
}

} // namespace ContactConservation
} // namespace RoR
