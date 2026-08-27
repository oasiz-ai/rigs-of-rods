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
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace Contact = RoR::ContactConservation;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool SameVectorBits(
    const Contact::Vector3& first,
    const Contact::Vector3& second)
{
    return DoubleBits(first.x) == DoubleBits(second.x) &&
        DoubleBits(first.y) == DoubleBits(second.y) &&
        DoubleBits(first.z) == DoubleBits(second.z);
}

bool SameAppliedVectorBits(
    const Contact::AppliedVector3& first,
    const Contact::AppliedVector3& second)
{
    return FloatBits(first.x) == FloatBits(second.x) &&
        FloatBits(first.y) == FloatBits(second.y) &&
        FloatBits(first.z) == FloatBits(second.z);
}

bool IsFiniteFloatBits(float value)
{
    return (FloatBits(value) & UINT32_C(0x7f800000)) !=
        UINT32_C(0x7f800000);
}

bool SameAggregate(
    const Contact::Aggregate& first,
    const Contact::Aggregate& second)
{
    return first.schema_version == second.schema_version &&
        first.contact_count == second.contact_count &&
        DoubleBits(first.maximum_normalized_linear_impulse_residual) ==
            DoubleBits(second.maximum_normalized_linear_impulse_residual) &&
        DoubleBits(first.maximum_angular_impulse_delta_magnitude_nms) ==
            DoubleBits(second.maximum_angular_impulse_delta_magnitude_nms) &&
        SameVectorBits(
            first.summed_angular_impulse_delta_nms,
            second.summed_angular_impulse_delta_nms) &&
        DoubleBits(first.summed_isolated_contact_work_j) ==
            DoubleBits(second.summed_isolated_contact_work_j) &&
        DoubleBits(first.summed_isolated_contact_kinetic_energy_delta_j) ==
            DoubleBits(
                second.summed_isolated_contact_kinetic_energy_delta_j) &&
        DoubleBits(
            first.summed_isolated_contact_integration_energy_delta_j) ==
            DoubleBits(
                second.summed_isolated_contact_integration_energy_delta_j);
}

bool SameTelemetry(
    const Contact::Telemetry& first,
    const Contact::Telemetry& second)
{
    for (std::size_t index = 0;
        index < first.normalized_barycentric.size();
        ++index)
    {
        if (DoubleBits(first.normalized_barycentric[index]) !=
            DoubleBits(second.normalized_barycentric[index]))
        {
            return false;
        }
    }
    if (!SameVectorBits(
            first.surface_contact_position_m,
            second.surface_contact_position_m))
    {
        return false;
    }
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        if (!SameAppliedVectorBits(
                first.applied_forces_n[index],
                second.applied_forces_n[index]) ||
            !SameVectorBits(
                first.observed_force_accumulator_deltas_n[index],
                second.observed_force_accumulator_deltas_n[index]) ||
            !SameVectorBits(first.impulses_ns[index], second.impulses_ns[index]))
        {
            return false;
        }
    }
    return SameVectorBits(
            first.linear_impulse_residual_ns,
            second.linear_impulse_residual_ns) &&
        DoubleBits(first.normalized_linear_impulse_residual) ==
            DoubleBits(second.normalized_linear_impulse_residual) &&
        SameVectorBits(
            first.angular_impulse_delta_nms,
            second.angular_impulse_delta_nms) &&
        DoubleBits(first.angular_impulse_delta_magnitude_nms) ==
            DoubleBits(second.angular_impulse_delta_magnitude_nms) &&
        DoubleBits(first.isolated_contact_work_j) ==
            DoubleBits(second.isolated_contact_work_j) &&
        DoubleBits(first.isolated_contact_kinetic_energy_delta_j) ==
            DoubleBits(second.isolated_contact_kinetic_energy_delta_j) &&
        DoubleBits(first.isolated_contact_integration_energy_delta_j) ==
            DoubleBits(second.isolated_contact_integration_energy_delta_j);
}

Contact::Telemetry SentinelTelemetry()
{
    Contact::Telemetry telemetry;
    double value = 1.0;
    for (std::size_t index = 0;
        index < telemetry.normalized_barycentric.size();
        ++index)
    {
        telemetry.normalized_barycentric[index] = value++;
    }
    telemetry.surface_contact_position_m = {value++, value++, value++};
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        telemetry.applied_forces_n[index].x = static_cast<float>(value++);
        telemetry.applied_forces_n[index].y = static_cast<float>(value++);
        telemetry.applied_forces_n[index].z = static_cast<float>(value++);
        telemetry.observed_force_accumulator_deltas_n[index] = {
            value++, value++, value++};
        telemetry.impulses_ns[index] = {value++, value++, value++};
    }
    telemetry.linear_impulse_residual_ns = {value++, value++, value++};
    telemetry.normalized_linear_impulse_residual = value++;
    telemetry.angular_impulse_delta_nms = {value++, value++, value++};
    telemetry.angular_impulse_delta_magnitude_nms = value++;
    telemetry.isolated_contact_work_j = value++;
    telemetry.isolated_contact_kinetic_energy_delta_j = value++;
    telemetry.isolated_contact_integration_energy_delta_j = value++;
    return telemetry;
}

Contact::Aggregate SentinelAggregate()
{
    Contact::Aggregate aggregate;
    aggregate.contact_count = 17;
    aggregate.maximum_normalized_linear_impulse_residual = 0.25;
    aggregate.maximum_angular_impulse_delta_magnitude_nms = 1.25;
    aggregate.summed_angular_impulse_delta_nms = {2.0, 3.0, 4.0};
    aggregate.summed_isolated_contact_work_j = 5.0;
    aggregate.summed_isolated_contact_integration_energy_delta_j = 7.0;
    aggregate.summed_isolated_contact_kinetic_energy_delta_j = 12.0;
    return aggregate;
}

Contact::Input ValidInput()
{
    Contact::Input input;
    input.hit_node.position_m = {0.5, 1.0, 0.0};
    input.hit_node.velocity_mps = {1.0, 2.0, 3.0};
    input.hit_node.mass_kg = 2.0;

    input.surface_nodes[0].position_m = {0.0, 0.0, 0.0};
    input.surface_nodes[0].velocity_mps = {-1.0, 0.0, 0.0};
    input.surface_nodes[0].mass_kg = 3.0;
    input.surface_nodes[1].position_m = {2.0, 0.0, 0.0};
    input.surface_nodes[1].velocity_mps = {0.0, -1.0, 0.0};
    input.surface_nodes[1].mass_kg = 4.0;
    input.surface_nodes[2].position_m = {0.0, 2.0, 0.0};
    input.surface_nodes[2].velocity_mps = {0.0, 0.0, -1.0};
    input.surface_nodes[2].mass_kg = 5.0;

    input.barycentric = {{0.25, 0.25, 0.5}};
    input.force_on_hit_n = {20.0, -10.0, 30.0};
    input.time_step_s = 0.005;
    return input;
}

double RelativeError(double measured, double expected)
{
    return std::abs(measured - expected) /
        std::max(1.0, std::abs(expected));
}

long double LongDot(
    const Contact::Vector3& first,
    const Contact::Vector3& second)
{
    return static_cast<long double>(first.x) * second.x +
        static_cast<long double>(first.y) * second.y +
        static_cast<long double>(first.z) * second.z;
}

long double AppliedNormalizedResidual(
    const Contact::Telemetry& telemetry)
{
    long double sum_x = 0.0L;
    long double sum_y = 0.0L;
    long double sum_z = 0.0L;
    long double scale = 0.0L;
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        const Contact::AppliedVector3& applied =
            telemetry.applied_forces_n[index];
        CHECK(IsFiniteFloatBits(applied.x));
        CHECK(IsFiniteFloatBits(applied.y));
        CHECK(IsFiniteFloatBits(applied.z));
        const Contact::Vector3& force =
            telemetry.observed_force_accumulator_deltas_n[index];
        sum_x += static_cast<long double>(force.x);
        sum_y += static_cast<long double>(force.y);
        sum_z += static_cast<long double>(force.z);
        scale += std::sqrt(
            static_cast<long double>(force.x) * force.x +
            static_cast<long double>(force.y) * force.y +
            static_cast<long double>(force.z) * force.z);
    }
    const long double residual = std::sqrt(
        sum_x * sum_x + sum_y * sum_y + sum_z * sum_z);
    return scale == 0.0L ? 0.0L : residual / scale;
}

void TestAnalyticalConservationAndEnergy()
{
    const Contact::Input input = ValidInput();
    Contact::Telemetry telemetry;
    CHECK(Contact::Evaluate(input, telemetry) == Contact::Error::NONE);

    CHECK(telemetry.normalized_barycentric == input.barycentric);
    CHECK(telemetry.surface_contact_position_m.x == 0.5);
    CHECK(telemetry.surface_contact_position_m.y == 1.0);
    CHECK(telemetry.surface_contact_position_m.z == 0.0);
    CHECK(FloatBits(
        telemetry.applied_forces_n[Contact::HIT_NODE_INDEX].x) ==
        FloatBits(static_cast<float>(input.force_on_hit_n.x)));
    CHECK(FloatBits(
        telemetry.applied_forces_n[Contact::HIT_NODE_INDEX].y) ==
        FloatBits(static_cast<float>(input.force_on_hit_n.y)));
    CHECK(FloatBits(
        telemetry.applied_forces_n[Contact::HIT_NODE_INDEX].z) ==
        FloatBits(static_cast<float>(input.force_on_hit_n.z)));

    Contact::Vector3 force_sum;
    Contact::Vector3 impulse_sum;
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        force_sum.x += telemetry.applied_forces_n[index].x;
        force_sum.y += telemetry.applied_forces_n[index].y;
        force_sum.z += telemetry.applied_forces_n[index].z;
    }
    impulse_sum.x =
        (telemetry.impulses_ns[Contact::HIT_NODE_INDEX].x +
            telemetry.impulses_ns[Contact::SURFACE_NODE_A_INDEX].x) +
        (telemetry.impulses_ns[Contact::SURFACE_NODE_B_INDEX].x +
            telemetry.impulses_ns[Contact::SURFACE_NODE_C_INDEX].x);
    impulse_sum.y =
        (telemetry.impulses_ns[Contact::HIT_NODE_INDEX].y +
            telemetry.impulses_ns[Contact::SURFACE_NODE_A_INDEX].y) +
        (telemetry.impulses_ns[Contact::SURFACE_NODE_B_INDEX].y +
            telemetry.impulses_ns[Contact::SURFACE_NODE_C_INDEX].y);
    impulse_sum.z =
        (telemetry.impulses_ns[Contact::HIT_NODE_INDEX].z +
            telemetry.impulses_ns[Contact::SURFACE_NODE_A_INDEX].z) +
        (telemetry.impulses_ns[Contact::SURFACE_NODE_B_INDEX].z +
            telemetry.impulses_ns[Contact::SURFACE_NODE_C_INDEX].z);
    CHECK(force_sum.x == 0.0);
    CHECK(force_sum.y == 0.0);
    CHECK(force_sum.z == 0.0);
    CHECK(SameVectorBits(
        impulse_sum,
        telemetry.linear_impulse_residual_ns));
    CHECK(telemetry.normalized_linear_impulse_residual <= 1.0e-6);
    CHECK(telemetry.angular_impulse_delta_magnitude_nms <= 1.0e-15);

    const std::array<Contact::NodeState, Contact::NODE_COUNT> nodes = {{
        input.hit_node,
        input.surface_nodes[0],
        input.surface_nodes[1],
        input.surface_nodes[2]
    }};
    long double expected_work = 0.0L;
    long double expected_delta = 0.0L;
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        expected_work += LongDot(
            nodes[index].velocity_mps,
            telemetry.impulses_ns[index]);
        Contact::Vector3 next_velocity = nodes[index].velocity_mps;
        next_velocity.x += telemetry.impulses_ns[index].x /
            nodes[index].mass_kg;
        next_velocity.y += telemetry.impulses_ns[index].y /
            nodes[index].mass_kg;
        next_velocity.z += telemetry.impulses_ns[index].z /
            nodes[index].mass_kg;
        expected_delta +=
            0.5L * static_cast<long double>(nodes[index].mass_kg) *
            (LongDot(next_velocity, next_velocity) -
                LongDot(nodes[index].velocity_mps,
                    nodes[index].velocity_mps));
    }
    CHECK(RelativeError(
        telemetry.isolated_contact_work_j,
        static_cast<double>(expected_work)) < 2.0e-15);
    CHECK(RelativeError(
        telemetry.isolated_contact_kinetic_energy_delta_j,
        static_cast<double>(expected_delta)) < 2.0e-15);
    CHECK(telemetry.isolated_contact_integration_energy_delta_j >= 0.0);
    CHECK(
        telemetry.isolated_contact_kinetic_energy_delta_j ==
        telemetry.isolated_contact_work_j +
            telemetry.isolated_contact_integration_energy_delta_j);
}

void TestAngularImpulseAndDissipativeStep()
{
    Contact::Input input = ValidInput();
    input.hit_node.position_m.z = 0.2;
    input.force_on_hit_n = {10.0, 0.0, 0.0};
    input.time_step_s = 0.1;
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        if (index == 0)
            input.hit_node.velocity_mps = {0.0, 0.0, 0.0};
        else
            input.surface_nodes[index - 1].velocity_mps = {0.0, 0.0, 0.0};
    }

    Contact::Telemetry telemetry;
    CHECK(Contact::Evaluate(input, telemetry) == Contact::Error::NONE);
    CHECK(std::abs(telemetry.angular_impulse_delta_nms.x) < 1.0e-15);
    CHECK(std::abs(telemetry.angular_impulse_delta_nms.y - 0.2) < 1.0e-15);
    CHECK(std::abs(telemetry.angular_impulse_delta_nms.z) < 1.0e-15);
    CHECK(std::abs(telemetry.angular_impulse_delta_magnitude_nms - 0.2) <
        1.0e-15);
    CHECK(telemetry.isolated_contact_work_j == 0.0);
    CHECK(telemetry.isolated_contact_kinetic_energy_delta_j > 0.0);
    CHECK(telemetry.isolated_contact_kinetic_energy_delta_j ==
        telemetry.isolated_contact_integration_energy_delta_j);

    input.force_on_hit_n = {0.0, 0.0, 10.0};
    CHECK(Contact::Evaluate(input, telemetry) == Contact::Error::NONE);
    CHECK(telemetry.angular_impulse_delta_magnitude_nms < 1.0e-15);

    input = ValidInput();
    input.hit_node.velocity_mps = {-1.0, 0.0, 0.0};
    for (std::size_t index = 0; index < input.surface_nodes.size(); ++index)
        input.surface_nodes[index].velocity_mps = {0.0, 0.0, 0.0};
    input.force_on_hit_n = {100.0, 0.0, 0.0};
    input.time_step_s = 0.001;
    CHECK(Contact::Evaluate(input, telemetry) == Contact::Error::NONE);
    CHECK(telemetry.isolated_contact_work_j < 0.0);
    CHECK(telemetry.isolated_contact_integration_energy_delta_j > 0.0);
    CHECK(telemetry.isolated_contact_kinetic_energy_delta_j < 0.0);
}

void TestFixedNodeEnergySemantics()
{
    Contact::Input movable_input = ValidInput();
    Contact::Telemetry movable;
    CHECK(Contact::Evaluate(movable_input, movable) == Contact::Error::NONE);

    Contact::Input fixed_input = movable_input;
    fixed_input.surface_nodes[0].movable = 0;
    fixed_input.surface_nodes[0].velocity_mps = {-3.0, 4.0, -5.0};
    movable_input.surface_nodes[0].velocity_mps =
        fixed_input.surface_nodes[0].velocity_mps;
    CHECK(Contact::Evaluate(movable_input, movable) == Contact::Error::NONE);
    Contact::Telemetry fixed;
    CHECK(Contact::Evaluate(fixed_input, fixed) == Contact::Error::NONE);

    CHECK(SameAppliedVectorBits(
        movable.applied_forces_n[Contact::SURFACE_NODE_A_INDEX],
        fixed.applied_forces_n[Contact::SURFACE_NODE_A_INDEX]));
    CHECK(SameVectorBits(
        movable.angular_impulse_delta_nms,
        fixed.angular_impulse_delta_nms));
    const Contact::Vector3& fixed_impulse =
        fixed.impulses_ns[Contact::SURFACE_NODE_A_INDEX];
    const double omitted_work =
        fixed_input.surface_nodes[0].velocity_mps.x * fixed_impulse.x +
        fixed_input.surface_nodes[0].velocity_mps.y * fixed_impulse.y +
        fixed_input.surface_nodes[0].velocity_mps.z * fixed_impulse.z;
    const double omitted_quadratic =
        0.5 * (fixed_impulse.x * fixed_impulse.x +
            fixed_impulse.y * fixed_impulse.y +
            fixed_impulse.z * fixed_impulse.z) /
        fixed_input.surface_nodes[0].mass_kg;
    CHECK(RelativeError(
        movable.isolated_contact_work_j -
            fixed.isolated_contact_work_j,
        omitted_work) < 2.0e-15);
    CHECK(RelativeError(
        movable.isolated_contact_integration_energy_delta_j -
            fixed.isolated_contact_integration_energy_delta_j,
        omitted_quadratic) < 2.0e-15);
    CHECK(RelativeError(
        movable.isolated_contact_kinetic_energy_delta_j -
            fixed.isolated_contact_kinetic_energy_delta_j,
        omitted_work + omitted_quadratic) < 2.0e-15);
    CHECK(fixed.isolated_contact_kinetic_energy_delta_j ==
        fixed.isolated_contact_work_j +
            fixed.isolated_contact_integration_energy_delta_j);
}

void TestAccumulatorDeltaAuditAndMobility()
{
    CHECK(Contact::IsSolverMovable(false, false));
    CHECK(!Contact::IsSolverMovable(true, false));
    CHECK(!Contact::IsSolverMovable(false, true));
    CHECK(!Contact::IsSolverMovable(true, true));

    Contact::Input remote_surface = ValidInput();
    for (std::size_t index = 0;
        index < remote_surface.surface_nodes.size();
        ++index)
    {
        remote_surface.surface_nodes[index].movable =
            Contact::IsSolverMovable(false, true) ? 1U : 0U;
    }
    Contact::Telemetry remote_surface_telemetry;
    CHECK(Contact::Evaluate(remote_surface, remote_surface_telemetry) ==
        Contact::Error::NONE);
    const Contact::Vector3& hit_impulse =
        remote_surface_telemetry.impulses_ns[Contact::HIT_NODE_INDEX];
    const double expected_hit_work =
        remote_surface.hit_node.velocity_mps.x * hit_impulse.x +
        remote_surface.hit_node.velocity_mps.y * hit_impulse.y +
        remote_surface.hit_node.velocity_mps.z * hit_impulse.z;
    const double expected_hit_integration =
        0.5 * (hit_impulse.x * hit_impulse.x +
            hit_impulse.y * hit_impulse.y +
            hit_impulse.z * hit_impulse.z) /
        remote_surface.hit_node.mass_kg;
    CHECK(RelativeError(
        remote_surface_telemetry.isolated_contact_work_j,
        expected_hit_work) < 2.0e-15);
    CHECK(RelativeError(
        remote_surface_telemetry.
            isolated_contact_integration_energy_delta_j,
        expected_hit_integration) < 2.0e-15);

    Contact::Input input = ValidInput();
    input.force_on_hit_n = {1.0, 0.0, 0.0};
    Contact::Telemetry planned;
    CHECK(Contact::Evaluate(input, planned) == Contact::Error::NONE);

    std::array<Contact::AppliedVector3, Contact::NODE_COUNT> before;
    before[Contact::HIT_NODE_INDEX].x = 16777216.0f;
    before[Contact::SURFACE_NODE_A_INDEX].x = -1024.0f;
    before[Contact::SURFACE_NODE_B_INDEX].x = 0.0f;
    before[Contact::SURFACE_NODE_C_INDEX].x = 4096.0f;
    std::array<Contact::AppliedVector3, Contact::NODE_COUNT> after = before;
    for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
    {
        after[index].x += planned.applied_forces_n[index].x;
        after[index].y += planned.applied_forces_n[index].y;
        after[index].z += planned.applied_forces_n[index].z;
    }

    Contact::Telemetry audited;
    CHECK(Contact::AuditAppliedForces(
        input,
        planned.applied_forces_n,
        before,
        after,
        audited) == Contact::Error::NONE);
    CHECK(audited.observed_force_accumulator_deltas_n[
        Contact::HIT_NODE_INDEX].x == 0.0);
    CHECK(audited.observed_force_accumulator_deltas_n[
        Contact::SURFACE_NODE_A_INDEX].x == -0.25);
    CHECK(audited.observed_force_accumulator_deltas_n[
        Contact::SURFACE_NODE_B_INDEX].x == -0.25);
    CHECK(audited.observed_force_accumulator_deltas_n[
        Contact::SURFACE_NODE_C_INDEX].x == -0.5);
    CHECK(audited.normalized_linear_impulse_residual > 0.99);

    const double infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    Contact::Telemetry unchanged = SentinelTelemetry();
    const Contact::Telemetry sentinel = unchanged;
    before[0].x = static_cast<float>(infinity);
    CHECK(Contact::AuditAppliedForces(
        input,
        planned.applied_forces_n,
        before,
        after,
        unchanged) == Contact::Error::FORCE_OUT_OF_BINARY32_RANGE);
    CHECK(SameTelemetry(unchanged, sentinel));
}

void ExpectError(const Contact::Input& input, Contact::Error expected)
{
    Contact::Telemetry output = SentinelTelemetry();
    const Contact::Telemetry sentinel = output;
    CHECK(Contact::Evaluate(input, output) == expected);
    CHECK(SameTelemetry(output, sentinel));
}

void TestFailClosedInputs()
{
    Contact::Input input = ValidInput();
    input.schema_version = Contact::SCHEMA_VERSION + 1;
    ExpectError(input, Contact::Error::UNSUPPORTED_SCHEMA);

    const double quiet_nan = DoubleFromBits(UINT64_C(0x7ff8000000000042));
    const double infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    for (int hostile = 0; hostile < 2; ++hostile)
    {
        const double value = hostile == 0 ? quiet_nan : infinity;
        input = ValidInput();
        input.hit_node.position_m.x = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.hit_node.velocity_mps.y = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.hit_node.mass_kg = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.surface_nodes[2].position_m.z = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.surface_nodes[1].velocity_mps.x = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.surface_nodes[0].mass_kg = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.barycentric[1] = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.force_on_hit_n.z = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
        input = ValidInput();
        input.time_step_s = value;
        ExpectError(input, Contact::Error::NONFINITE_INPUT);
    }

    input = ValidInput();
    input.time_step_s = 0.0;
    ExpectError(input, Contact::Error::INVALID_TIME_STEP);
    input.time_step_s = -0.001;
    ExpectError(input, Contact::Error::INVALID_TIME_STEP);

    input = ValidInput();
    input.hit_node.mass_kg = 0.0;
    ExpectError(input, Contact::Error::INVALID_MASS);
    input = ValidInput();
    input.surface_nodes[1].mass_kg = -1.0;
    ExpectError(input, Contact::Error::INVALID_MASS);

    input = ValidInput();
    input.hit_node.movable = 2;
    ExpectError(input, Contact::Error::INVALID_NODE_MOBILITY);
    input = ValidInput();
    input.surface_nodes[2].movable = 255;
    ExpectError(input, Contact::Error::INVALID_NODE_MOBILITY);

    input = ValidInput();
    input.barycentric = {{-0.1, 0.6, 0.5}};
    ExpectError(input, Contact::Error::INVALID_BARYCENTRIC_COORDINATES);
    input.barycentric = {{0.1, 0.2, 0.6}};
    ExpectError(input, Contact::Error::INVALID_BARYCENTRIC_COORDINATES);
    input.barycentric = {{0.0, 0.0, 0.0}};
    ExpectError(input, Contact::Error::INVALID_BARYCENTRIC_COORDINATES);
    input.barycentric = {{1.1, 0.0, 0.0}};
    ExpectError(input, Contact::Error::INVALID_BARYCENTRIC_COORDINATES);

    input = ValidInput();
    input.surface_nodes[2].position_m =
        input.surface_nodes[1].position_m;
    ExpectError(input, Contact::Error::DEGENERATE_SURFACE_TRIANGLE);
    input = ValidInput();
    input.surface_nodes[2].position_m = {4.0, 0.0, 0.0};
    ExpectError(input, Contact::Error::DEGENERATE_SURFACE_TRIANGLE);

    input = ValidInput();
    input.force_on_hit_n.x = std::numeric_limits<double>::max();
    input.time_step_s = 2.0;
    ExpectError(input, Contact::Error::FORCE_OUT_OF_BINARY32_RANGE);

    CHECK(std::strcmp(Contact::ErrorToString(Contact::Error::NONE), "none") == 0);
    CHECK(std::strcmp(
        Contact::ErrorToString(Contact::Error::NUMERIC_RANGE),
        "numeric range") == 0);
    CHECK(std::strcmp(
        Contact::ErrorToString(Contact::Error::INVALID_NODE_MOBILITY),
        "invalid node mobility") == 0);
    CHECK(std::strcmp(
        Contact::ErrorToString(static_cast<Contact::Error>(999)),
        "unknown error") == 0);
}

void TestBoundedAggregateFailClosed()
{
    Contact::Telemetry telemetry;
    CHECK(Contact::Evaluate(ValidInput(), telemetry) == Contact::Error::NONE);

    Contact::Aggregate aggregate;
    CHECK(Contact::Accumulate(telemetry, aggregate) == Contact::Error::NONE);
    CHECK(aggregate.contact_count == 1);
    CHECK(DoubleBits(
        aggregate.maximum_normalized_linear_impulse_residual) ==
        DoubleBits(telemetry.normalized_linear_impulse_residual));
    CHECK(DoubleBits(aggregate.summed_isolated_contact_work_j) ==
        DoubleBits(telemetry.isolated_contact_work_j));

    Contact::Aggregate boundary;
    boundary.contact_count = Contact::MAX_AGGREGATE_CONTACTS - 1;
    CHECK(Contact::Accumulate(telemetry, boundary) == Contact::Error::NONE);
    CHECK(boundary.contact_count == Contact::MAX_AGGREGATE_CONTACTS);
    const Contact::Aggregate full_boundary = boundary;
    CHECK(Contact::Accumulate(telemetry, boundary) ==
        Contact::Error::CONTACT_LIMIT_EXCEEDED);
    CHECK(SameAggregate(boundary, full_boundary));

    const double quiet_nan = DoubleFromBits(UINT64_C(0x7ff8000000000042));
    Contact::Aggregate unchanged = SentinelAggregate();
    Contact::Aggregate sentinel = unchanged;
    Contact::Telemetry invalid = telemetry;
    invalid.isolated_contact_work_j = quiet_nan;
    CHECK(Contact::Accumulate(invalid, unchanged) ==
        Contact::Error::INVALID_TELEMETRY);
    CHECK(SameAggregate(unchanged, sentinel));

    unchanged = SentinelAggregate();
    unchanged.maximum_normalized_linear_impulse_residual = quiet_nan;
    sentinel = unchanged;
    CHECK(Contact::Accumulate(telemetry, unchanged) ==
        Contact::Error::INVALID_TELEMETRY);
    CHECK(SameAggregate(unchanged, sentinel));

    unchanged = SentinelAggregate();
    unchanged.schema_version = Contact::SCHEMA_VERSION + 1;
    sentinel = unchanged;
    CHECK(Contact::Accumulate(telemetry, unchanged) ==
        Contact::Error::UNSUPPORTED_SCHEMA);
    CHECK(SameAggregate(unchanged, sentinel));

    unchanged = SentinelAggregate();
    unchanged.contact_count = Contact::MAX_AGGREGATE_CONTACTS;
    sentinel = unchanged;
    CHECK(Contact::Accumulate(telemetry, unchanged) ==
        Contact::Error::CONTACT_LIMIT_EXCEEDED);
    CHECK(SameAggregate(unchanged, sentinel));

    unchanged = SentinelAggregate();
    unchanged.summed_isolated_contact_work_j =
        std::numeric_limits<double>::max();
    unchanged.summed_isolated_contact_kinetic_energy_delta_j =
        unchanged.summed_isolated_contact_work_j +
        unchanged.summed_isolated_contact_integration_energy_delta_j;
    invalid = telemetry;
    invalid.isolated_contact_work_j = std::numeric_limits<double>::max();
    invalid.isolated_contact_kinetic_energy_delta_j =
        invalid.isolated_contact_work_j +
        invalid.isolated_contact_integration_energy_delta_j;
    sentinel = unchanged;
    CHECK(Contact::Accumulate(invalid, unchanged) ==
        Contact::Error::NUMERIC_RANGE);
    CHECK(SameAggregate(unchanged, sentinel));

    unchanged = SentinelAggregate();
    invalid = telemetry;
    invalid.isolated_contact_kinetic_energy_delta_j += 1.0;
    sentinel = unchanged;
    CHECK(Contact::Accumulate(invalid, unchanged) ==
        Contact::Error::INVALID_TELEMETRY);
    CHECK(SameAggregate(unchanged, sentinel));
}

class FixedRandom
{
public:
    explicit FixedRandom(std::uint64_t seed):
        m_state(seed)
    {
    }

    std::uint64_t Next()
    {
        std::uint64_t value = m_state;
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        m_state = value;
        return value * UINT64_C(2685821657736338717);
    }

    std::uint64_t Bounded(std::uint64_t exclusive_limit)
    {
        return Next() % exclusive_limit;
    }

private:
    std::uint64_t m_state;
};

double GridValue(
    FixedRandom& random,
    std::uint64_t maximum_steps,
    double quantum)
{
    const std::int64_t step = static_cast<std::int64_t>(
        random.Bounded(2 * maximum_steps + 1)) -
        static_cast<std::int64_t>(maximum_steps);
    return static_cast<double>(step) * quantum;
}

Contact::Vector3 GridVector(
    FixedRandom& random,
    std::uint64_t maximum_steps,
    double quantum)
{
    return Contact::Vector3(
        GridValue(random, maximum_steps, quantum),
        GridValue(random, maximum_steps, quantum),
        GridValue(random, maximum_steps, quantum));
}

Contact::Input RandomInput(FixedRandom& random)
{
    Contact::Input input;
    const Contact::Vector3 origin = GridVector(random, 16000, 1.0 / 16.0);
    const double first_scale = static_cast<double>(
        2 + random.Bounded(319)) / 16.0;
    const double second_scale = static_cast<double>(
        2 + random.Bounded(319)) / 16.0;
    input.surface_nodes[0].position_m = origin;
    input.surface_nodes[1].position_m = {
        origin.x + first_scale, origin.y, origin.z};
    input.surface_nodes[2].position_m = {
        origin.x, origin.y + second_scale, origin.z};

    const std::uint64_t first_weight_units =
        1 + random.Bounded(UINT64_C(65533));
    const std::uint64_t remaining_units =
        UINT64_C(65536) - first_weight_units;
    const std::uint64_t second_weight_units =
        1 + random.Bounded(remaining_units - 1);
    const std::uint64_t third_weight_units =
        remaining_units - second_weight_units;
    input.barycentric[0] =
        static_cast<double>(first_weight_units) / 65536.0;
    input.barycentric[1] =
        static_cast<double>(second_weight_units) / 65536.0;
    input.barycentric[2] =
        static_cast<double>(third_weight_units) / 65536.0;

    input.hit_node.position_m = {
        origin.x + input.barycentric[1] * first_scale,
        origin.y + input.barycentric[2] * second_scale,
        origin.z + GridValue(random, 3, 1.0 / 64.0)};

    input.hit_node.velocity_mps = GridVector(random, 1600, 1.0 / 32.0);
    input.hit_node.mass_kg = static_cast<double>(
        2 + random.Bounded(1599)) / 8.0;
    for (std::size_t index = 0; index < input.surface_nodes.size(); ++index)
    {
        input.surface_nodes[index].velocity_mps =
            GridVector(random, 1600, 1.0 / 32.0);
        input.surface_nodes[index].mass_kg =
            static_cast<double>(2 + random.Bounded(1599)) / 8.0;
    }
    input.force_on_hit_n = GridVector(random, 1000000, 1.0);
    input.time_step_s = static_cast<double>(
        1 + random.Bounded(2097)) / 1048576.0;
    return input;
}

void EvaluateRange(
    const std::vector<Contact::Input>& inputs,
    std::vector<Contact::Telemetry>& outputs,
    std::vector<Contact::Error>& errors,
    std::size_t begin,
    std::size_t end)
{
    for (std::size_t index = begin; index < end; ++index)
        errors[index] = Contact::Evaluate(inputs[index], outputs[index]);
}

std::uint64_t Mix(std::uint64_t hash, std::uint64_t value)
{
    hash ^= value;
    hash *= UINT64_C(1099511628211);
    return hash;
}

std::uint64_t TelemetryDigest(
    const std::vector<Contact::Telemetry>& telemetry)
{
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (std::size_t record = 0; record < telemetry.size(); ++record)
    {
        const Contact::Telemetry& value = telemetry[record];
        for (std::size_t index = 0; index < Contact::NODE_COUNT; ++index)
        {
            hash = Mix(hash, FloatBits(value.applied_forces_n[index].x));
            hash = Mix(hash, FloatBits(value.applied_forces_n[index].y));
            hash = Mix(hash, FloatBits(value.applied_forces_n[index].z));
            hash = Mix(
                hash,
                DoubleBits(
                    value.observed_force_accumulator_deltas_n[index].x));
            hash = Mix(
                hash,
                DoubleBits(
                    value.observed_force_accumulator_deltas_n[index].y));
            hash = Mix(
                hash,
                DoubleBits(
                    value.observed_force_accumulator_deltas_n[index].z));
        }
        hash = Mix(hash, DoubleBits(value.normalized_linear_impulse_residual));
        hash = Mix(hash, DoubleBits(value.angular_impulse_delta_nms.x));
        hash = Mix(hash, DoubleBits(value.angular_impulse_delta_nms.y));
        hash = Mix(hash, DoubleBits(value.angular_impulse_delta_nms.z));
        hash = Mix(hash, DoubleBits(value.isolated_contact_work_j));
        hash = Mix(
            hash,
            DoubleBits(value.isolated_contact_kinetic_energy_delta_j));
    }
    return hash;
}

void TestFixedSeedPartitionDeterminism()
{
    static const std::size_t FIXTURE_COUNT = 20000;
    FixedRandom random(UINT64_C(0x7d935af12ec4480b));
    std::vector<Contact::Input> inputs;
    inputs.reserve(FIXTURE_COUNT);
    for (std::size_t index = 0; index < FIXTURE_COUNT; ++index)
        inputs.push_back(RandomInput(random));

    std::vector<Contact::Telemetry> baseline(FIXTURE_COUNT);
    std::vector<Contact::Error> baseline_errors(
        FIXTURE_COUNT,
        Contact::Error::NUMERIC_RANGE);
    EvaluateRange(
        inputs,
        baseline,
        baseline_errors,
        0,
        FIXTURE_COUNT);

    double maximum_linear_residual = 0.0;
    double maximum_energy_identity_error = 0.0;
    Contact::Aggregate baseline_aggregate;
    for (std::size_t index = 0; index < FIXTURE_COUNT; ++index)
    {
        CHECK(baseline_errors[index] == Contact::Error::NONE);
        if (baseline_errors[index] != Contact::Error::NONE)
            continue;
        maximum_linear_residual = std::max(
            maximum_linear_residual,
            baseline[index].normalized_linear_impulse_residual);
        const double reconstructed =
            baseline[index].isolated_contact_work_j +
            baseline[index].isolated_contact_integration_energy_delta_j;
        maximum_energy_identity_error = std::max(
            maximum_energy_identity_error,
            std::abs(
                reconstructed -
                baseline[index].isolated_contact_kinetic_energy_delta_j));
        CHECK(baseline[index].normalized_linear_impulse_residual <= 1.0e-6);
        CHECK(AppliedNormalizedResidual(baseline[index]) <= 1.0e-6L);
        CHECK(
            baseline[index].isolated_contact_integration_energy_delta_j >=
            0.0);
        CHECK(reconstructed ==
            baseline[index].isolated_contact_kinetic_energy_delta_j);
        CHECK(Contact::Accumulate(
            baseline[index],
            baseline_aggregate) == Contact::Error::NONE);
    }
    CHECK(baseline_aggregate.contact_count == FIXTURE_COUNT);
    CHECK(DoubleBits(
        baseline_aggregate.maximum_normalized_linear_impulse_residual) ==
        DoubleBits(maximum_linear_residual));

    const std::uint64_t expected_digest = TelemetryDigest(baseline);
    const std::size_t worker_counts[] = {2, 8};
    for (std::size_t worker_case = 0;
        worker_case < sizeof(worker_counts) / sizeof(worker_counts[0]);
        ++worker_case)
    {
        const std::size_t worker_count = worker_counts[worker_case];
        std::vector<Contact::Telemetry> outputs(FIXTURE_COUNT);
        std::vector<Contact::Error> errors(
            FIXTURE_COUNT,
            Contact::Error::NUMERIC_RANGE);
        std::vector<std::thread> workers;
        for (std::size_t worker = 0; worker < worker_count; ++worker)
        {
            const std::size_t begin =
                FIXTURE_COUNT * worker / worker_count;
            const std::size_t end =
                FIXTURE_COUNT * (worker + 1) / worker_count;
            workers.push_back(std::thread(
                EvaluateRange,
                std::cref(inputs),
                std::ref(outputs),
                std::ref(errors),
                begin,
                end));
        }
        for (std::size_t worker = 0; worker < workers.size(); ++worker)
            workers[worker].join();

        CHECK(TelemetryDigest(outputs) == expected_digest);
        Contact::Aggregate partition_aggregate;
        for (std::size_t index = 0; index < FIXTURE_COUNT; ++index)
        {
            CHECK(errors[index] == Contact::Error::NONE);
            CHECK(SameTelemetry(outputs[index], baseline[index]));
            CHECK(Contact::Accumulate(
                outputs[index],
                partition_aggregate) == Contact::Error::NONE);
        }
        CHECK(SameAggregate(partition_aggregate, baseline_aggregate));
    }

    std::printf(
        "contact conservation fixed-seed digest=%016llx fixtures=%zu "
        "max_linear_residual=%.17g max_energy_identity_error=%.17g\n",
        static_cast<unsigned long long>(expected_digest),
        FIXTURE_COUNT,
        maximum_linear_residual,
        maximum_energy_identity_error);
}

} // namespace

int main()
{
    TestAnalyticalConservationAndEnergy();
    TestAngularImpulseAndDissipativeStep();
    TestFixedNodeEnergySemantics();
    TestAccumulatorDeltaAuditAndMobility();
    TestFailClosedInputs();
    TestBoundedAggregateFailClosed();
    TestFixedSeedPartitionDeterminism();

    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "%d contact conservation checks failed\n",
            g_failures);
        return 1;
    }
    std::printf("contact conservation checks passed\n");
    return 0;
}
