#include "BeamAxialKinematics.h"
#include "BeamAxialResponse.h"
#include "CalibratedBeamMaterialAdapter.h"
#include "CalibratedBeamProductionStep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line
                  << ": check failed: " << expression << '\n';
        ++g_failures;
    }
}

void CheckNear(double actual, double expected, double tolerance, int line)
{
    if (!RoR::BeamAxialResponse::IsFinite(actual) ||
        !RoR::BeamAxialResponse::IsFinite(expected) ||
        !RoR::BeamAxialResponse::IsFinite(tolerance) ||
        std::abs(actual - expected) > tolerance)
    {
        std::cerr << "line " << line << ": expected " << expected
                  << " +/- " << tolerance << ", got " << actual << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) \
    CheckNear((actual), (expected), (tolerance), __LINE__)

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&value);
    for (std::size_t index = 0U; index < sizeof(value); ++index)
        destination[index] = source[index];
    return value;
}

std::uint64_t NextRandom(std::uint64_t& state)
{
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * UINT64_C(2685821657736338717);
}

double NextSigned(std::uint64_t& state, double magnitude)
{
    const double unit = static_cast<double>(NextRandom(state) >> 11U) *
        (1.0 / 9007199254740992.0);
    return (2.0 * unit - 1.0) * magnitude;
}

double NextExactSigned(std::uint64_t& state, std::int32_t modulus)
{
    const std::int32_t sample = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(NextRandom(state) >> 32U));
    return static_cast<double>(sample % modulus) / 32.0;
}

double Dot(
    const std::array<double, 3>& first,
    const std::array<double, 3>& second)
{
    return first[0] * second[0] +
        first[1] * second[1] +
        first[2] * second[2];
}

std::array<double, 3> RelativeVelocity(
    const RoR::BeamAxialKinematics::Input& input)
{
    return {{
        input.endpoint_1_velocity_mps[0] -
            input.endpoint_2_velocity_mps[0],
        input.endpoint_1_velocity_mps[1] -
            input.endpoint_2_velocity_mps[1],
        input.endpoint_1_velocity_mps[2] -
            input.endpoint_2_velocity_mps[2]
    }};
}

double KineticEnergy(
    const std::array<double, 3>& velocity_1,
    const std::array<double, 3>& velocity_2,
    double mass_1,
    double mass_2)
{
    return 0.5 * mass_1 * Dot(velocity_1, velocity_1) +
        0.5 * mass_2 * Dot(velocity_2, velocity_2);
}

bool SameMaterialState(
    const RoR::CalibratedBeamMaterial::State& first,
    const RoR::CalibratedBeamMaterial::State& second)
{
    return
        first.plastic_strain == second.plastic_strain &&
        first.accumulated_plastic_strain ==
            second.accumulated_plastic_strain &&
        first.damage == second.damage &&
        first.damage_driver_density == second.damage_driver_density &&
        first.last_total_strain == second.last_total_strain &&
        first.fractured == second.fractured;
}

void TestKnownGeometryAndProjection()
{
    using namespace RoR::BeamAxialKinematics;

    Input input;
    input.endpoint_1_position_m = {{3.0, 4.0, 12.0}};
    input.endpoint_1_velocity_mps = {{1.0, -2.0, 3.0}};
    const Result result = Compute(input);

    CHECK(result.IsValid());
    CHECK_NEAR(result.current_length_m, 13.0, 0.0);
    CHECK_NEAR(result.unit_direction[0], 3.0 / 13.0, 1.0e-16);
    CHECK_NEAR(result.unit_direction[1], 4.0 / 13.0, 1.0e-16);
    CHECK_NEAR(result.unit_direction[2], 12.0 / 13.0, 1.0e-16);
    CHECK_NEAR(Dot(result.unit_direction, result.unit_direction), 1.0, 3.0e-16);
    CHECK_NEAR(result.axial_relative_velocity_mps, 31.0 / 13.0, 5.0e-16);
    CHECK(result.current_length_runtime_m == 13.0f);
    CHECK(
        result.axial_relative_velocity_runtime_mps ==
        static_cast<float>(31.0 / 13.0));
}

void TestScaleSafeAndHostileInputs()
{
    using namespace RoR::BeamAxialKinematics;

    Input large;
    large.endpoint_1_position_m = {{1.0e30, -1.0e30, 1.0e30}};
    large.endpoint_1_velocity_mps = {{2.0e20, -3.0e20, 4.0e20}};
    const Result large_result = Compute(large);
    CHECK(large_result.IsValid());
    CHECK_NEAR(
        large_result.current_length_m / 1.0e30,
        std::sqrt(3.0),
        3.0e-16);
    CHECK_NEAR(
        Dot(large_result.unit_direction, large_result.unit_direction),
        1.0,
        3.0e-16);

    Input zero;
    CHECK(Compute(zero).error == Error::INVALID_LENGTH);

    Input threshold;
    threshold.endpoint_1_position_m = {{
        std::sqrt(static_cast<double>(
            RoR::BeamAxialResponse::MIN_LENGTH_SQUARED)),
        0.0,
        0.0
    }};
    CHECK(Compute(threshold).error == Error::INVALID_LENGTH);
    threshold.endpoint_1_position_m[0] *= 1.01;
    CHECK(Compute(threshold).IsValid());

    const double infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double nan =
        DoubleFromBits(UINT64_C(0x7ff8000000000001));
    Input hostile = large;
    hostile.endpoint_1_position_m[1] = infinity;
    CHECK(Compute(hostile).error == Error::NONFINITE_INPUT);
    hostile = large;
    hostile.endpoint_1_velocity_mps[2] = nan;
    CHECK(Compute(hostile).error == Error::NONFINITE_INPUT);

    Input runtime_overflow;
    runtime_overflow.endpoint_1_position_m = {{
        static_cast<double>(std::numeric_limits<float>::max()),
        static_cast<double>(std::numeric_limits<float>::max()),
        0.0
    }};
    CHECK(Compute(runtime_overflow).error == Error::NUMERIC_OVERFLOW);

    Input projection_overflow;
    projection_overflow.endpoint_1_position_m = {{1.0, 0.0, 0.0}};
    projection_overflow.endpoint_1_velocity_mps = {{
        static_cast<double>(std::numeric_limits<float>::max()),
        0.0,
        0.0
    }};
    projection_overflow.endpoint_2_velocity_mps = {{
        -static_cast<double>(std::numeric_limits<float>::max()),
        0.0,
        0.0
    }};
    CHECK(Compute(projection_overflow).error == Error::NUMERIC_OVERFLOW);

    Input finite;
    finite.endpoint_1_position_m = {{4.0, -2.0, 7.0}};
    finite.endpoint_2_position_m = {{1.0, 2.0, -5.0}};
    finite.endpoint_1_velocity_mps = {{3.0, -4.0, 9.0}};
    finite.endpoint_2_velocity_mps = {{-1.0, 5.0, 2.0}};
    for (std::size_t group = 0U; group < 4U; ++group)
    {
        for (std::size_t lane = 0U; lane < 3U; ++lane)
        {
            Input invalid_nan = finite;
            Input invalid_infinity = finite;
            std::array<double, 3>* nan_groups[] = {
                &invalid_nan.endpoint_1_position_m,
                &invalid_nan.endpoint_2_position_m,
                &invalid_nan.endpoint_1_velocity_mps,
                &invalid_nan.endpoint_2_velocity_mps
            };
            std::array<double, 3>* infinity_groups[] = {
                &invalid_infinity.endpoint_1_position_m,
                &invalid_infinity.endpoint_2_position_m,
                &invalid_infinity.endpoint_1_velocity_mps,
                &invalid_infinity.endpoint_2_velocity_mps
            };
            (*nan_groups[group])[lane] = nan;
            (*infinity_groups[group])[lane] = infinity;
            CHECK(Compute(invalid_nan).error == Error::NONFINITE_INPUT);
            CHECK(
                Compute(invalid_infinity).error ==
                Error::NONFINITE_INPUT);
        }
    }
}

void TestTranslationSwapVelocityAndScaleInvariance()
{
    using namespace RoR::BeamAxialKinematics;

    Input original;
    original.endpoint_1_position_m = {{5.0, -2.0, 9.0}};
    original.endpoint_2_position_m = {{2.0, -6.0, -3.0}};
    original.endpoint_1_velocity_mps = {{7.0, -4.0, 1.0}};
    original.endpoint_2_velocity_mps = {{-2.0, 3.0, -5.0}};
    const Result reference = Compute(original);
    CHECK(reference.IsValid());

    Input translated = original;
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        translated.endpoint_1_position_m[lane] += 1024.0;
        translated.endpoint_2_position_m[lane] += 1024.0;
    }
    const Result translated_result = Compute(translated);
    CHECK(translated_result.IsValid());
    CHECK(translated_result.current_length_m == reference.current_length_m);
    CHECK(
        translated_result.axial_relative_velocity_mps ==
        reference.axial_relative_velocity_mps);
    CHECK(translated_result.unit_direction == reference.unit_direction);

    Input swapped = original;
    std::swap(
        swapped.endpoint_1_position_m,
        swapped.endpoint_2_position_m);
    std::swap(
        swapped.endpoint_1_velocity_mps,
        swapped.endpoint_2_velocity_mps);
    const Result swapped_result = Compute(swapped);
    CHECK(swapped_result.IsValid());
    CHECK(swapped_result.current_length_m == reference.current_length_m);
    CHECK(
        swapped_result.axial_relative_velocity_mps ==
        reference.axial_relative_velocity_mps);
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        CHECK(
            swapped_result.unit_direction[lane] ==
            -reference.unit_direction[lane]);
    }

    Input reversed_velocity = original;
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        reversed_velocity.endpoint_1_velocity_mps[lane] =
            -original.endpoint_1_velocity_mps[lane];
        reversed_velocity.endpoint_2_velocity_mps[lane] =
            -original.endpoint_2_velocity_mps[lane];
    }
    const Result reversed_result = Compute(reversed_velocity);
    CHECK(reversed_result.IsValid());
    CHECK(
        reversed_result.axial_relative_velocity_mps ==
        -reference.axial_relative_velocity_mps);

    Input scaled = original;
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        scaled.endpoint_1_position_m[lane] *= 8.0;
        scaled.endpoint_2_position_m[lane] *= 8.0;
    }
    const Result scaled_result = Compute(scaled);
    CHECK(scaled_result.IsValid());
    CHECK(scaled_result.current_length_m == 8.0 * reference.current_length_m);
    CHECK(scaled_result.unit_direction == reference.unit_direction);
    CHECK(
        scaled_result.axial_relative_velocity_mps ==
        reference.axial_relative_velocity_mps);
}

void CheckDampingSample(
    const RoR::BeamAxialKinematics::Input& input,
    float mass_1,
    float mass_2,
    float damping)
{
    using namespace RoR;

    const BeamAxialKinematics::Result kinematics =
        BeamAxialKinematics::Compute(input);
    CHECK(kinematics.IsValid());
    if (!kinematics.IsValid())
        return;

    const float time_step = 0.0005f;
    const BeamAxialResponse::DampingResult response =
        BeamAxialResponse::ComputeDamping(
            kinematics.axial_relative_velocity_runtime_mps,
            damping,
            time_step,
            mass_1,
            mass_2,
            true,
            true);
    const double axial_force = static_cast<double>(response.force);
    const std::array<double, 3> force_1 = {{
        axial_force * kinematics.unit_direction[0],
        axial_force * kinematics.unit_direction[1],
        axial_force * kinematics.unit_direction[2]
    }};
    const std::array<double, 3> force_2 = {{
        -force_1[0], -force_1[1], -force_1[2]
    }};

    const double force_scale =
        std::abs(force_1[0]) + std::abs(force_1[1]) +
        std::abs(force_1[2]) + std::abs(force_2[0]) +
        std::abs(force_2[1]) + std::abs(force_2[2]);
    const double momentum_residual =
        (std::abs(force_1[0] + force_2[0]) +
            std::abs(force_1[1] + force_2[1]) +
            std::abs(force_1[2] + force_2[2])) /
        std::max(force_scale, 1.0e-30);
    CHECK(momentum_residual <= 1.0e-6);

    const std::array<double, 3> relative_velocity =
        RelativeVelocity(input);
    const std::array<double, 3> velocity_1 = {{
        0.5 * relative_velocity[0],
        0.5 * relative_velocity[1],
        0.5 * relative_velocity[2]
    }};
    const std::array<double, 3> velocity_2 = {{
        -0.5 * relative_velocity[0],
        -0.5 * relative_velocity[1],
        -0.5 * relative_velocity[2]
    }};
    std::array<double, 3> next_velocity_1 = velocity_1;
    std::array<double, 3> next_velocity_2 = velocity_2;
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        next_velocity_1[lane] +=
            force_1[lane] / static_cast<double>(mass_1) * time_step;
        next_velocity_2[lane] +=
            force_2[lane] / static_cast<double>(mass_2) * time_step;
    }

    const double old_axial_velocity =
        kinematics.axial_relative_velocity_mps;
    const std::array<double, 3> next_relative_velocity = {{
        next_velocity_1[0] - next_velocity_2[0],
        next_velocity_1[1] - next_velocity_2[1],
        next_velocity_1[2] - next_velocity_2[2]
    }};
    const double next_axial_velocity =
        Dot(next_relative_velocity, kinematics.unit_direction);
    const double reversal_tolerance =
        1.0e-5 * (1.0 + old_axial_velocity * old_axial_velocity);
    CHECK(old_axial_velocity * next_axial_velocity >= -reversal_tolerance);
    CHECK(axial_force * old_axial_velocity <= reversal_tolerance);

    const double energy_before =
        KineticEnergy(velocity_1, velocity_2, mass_1, mass_2);
    const double energy_after =
        KineticEnergy(next_velocity_1, next_velocity_2, mass_1, mass_2);
    CHECK(energy_after <= energy_before + 1.0e-5 * (1.0 + energy_before));
}

void TestFixedSeedThreeDimensionalProperties()
{
    std::uint64_t state = UINT64_C(0xd1b54a32d192ed03);
    for (int sample = 0; sample < 20000; ++sample)
    {
        RoR::BeamAxialKinematics::Input input;
        input.endpoint_1_position_m = {{
            NextSigned(state, 50.0) + 0.25,
            NextSigned(state, 50.0),
            NextSigned(state, 50.0)
        }};
        input.endpoint_1_velocity_mps = {{
            NextSigned(state, 120.0),
            NextSigned(state, 120.0),
            NextSigned(state, 120.0)
        }};
        const float mass_1 =
            static_cast<float>(1.0 + std::abs(NextSigned(state, 249.0)));
        const float mass_2 =
            static_cast<float>(1.0 + std::abs(NextSigned(state, 249.0)));
        const float damping =
            static_cast<float>(1.0 + std::abs(NextSigned(state, 2.0e6)));
        CheckDampingSample(input, mass_1, mass_2, damping);
    }
}

void TestCalibratedAdapterHandoff()
{
    using namespace RoR;

    BeamAxialKinematics::Input geometry;
    geometry.endpoint_1_position_m = {{1.2, -1.6, 0.9}};
    geometry.endpoint_1_velocity_mps = {{3.0, -4.0, 0.5}};
    const BeamAxialKinematics::Result kinematics =
        BeamAxialKinematics::Compute(geometry);
    CHECK(kinematics.IsValid());

    CalibratedBeamMaterialAdapter::Configuration configuration;
    configuration.cross_section_area_m2 = 0.002;
    configuration.material.elastic_modulus = 1.0e6;
    configuration.material.yield_stress = 2.0e6;
    configuration.material.hardening_modulus = 1.0e5;
    configuration.material.damage_onset_plastic_strain = 0.04;
    configuration.material.damage_driver_capacity_density = 50.0e6;
    CalibratedBeamMaterialAdapter::Runtime runtime;
    CHECK(CalibratedBeamMaterialAdapter::TryConfigure(
        runtime,
        configuration));

    CalibratedBeamProductionStep::Input input;
    input.endpoints = geometry;
    // Binary scaling by one half makes the strict-side total strain exactly
    // one. A caller that narrows the current length to float before the
    // material handoff will no longer reproduce that value.
    input.reference_length_m = kinematics.current_length_m * 0.5;
    input.damping_coefficient = 12000.0f;
    input.time_step = 0.0005f;
    input.mass_1 = 50.0f;
    input.mass_2 = 50.0f;
    input.movable_1 = true;
    input.movable_2 = true;
    input.is_plain_axial_beam = true;
    const CalibratedBeamProductionStep::Result response =
        CalibratedBeamProductionStep::Step(runtime, input);
    CHECK(response.IsValid());
    CHECK(response.material.total_strain == 1.0);
    CHECK(response.material.axial_force_n != 0.0);
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        CHECK(
            response.material.forces.endpoint_1[lane] ==
            -response.material.forces.endpoint_2[lane]);
        CHECK(
            response.material.forces.endpoint_1[lane] ==
            response.material.axial_force_n *
                response.kinematics.unit_direction[lane]);
    }
    CHECK_NEAR(
        Dot(
            response.kinematics.unit_direction,
            response.kinematics.unit_direction),
        1.0,
        3.0e-16);

    const CalibratedBeamMaterial::State committed_state = runtime.state;
    CalibratedBeamMaterialAdapter::Runtime degenerate_runtime = runtime;
    CalibratedBeamProductionStep::Input degenerate = input;
    degenerate.endpoints.endpoint_1_position_m =
        degenerate.endpoints.endpoint_2_position_m;
    const CalibratedBeamProductionStep::Result degenerate_result =
        CalibratedBeamProductionStep::Step(
            degenerate_runtime,
            degenerate);
    CHECK(!degenerate_result.IsValid());
    CHECK(
        degenerate_result.material.error ==
        CalibratedBeamMaterialAdapter::Error::INVALID_CURRENT_LENGTH);
    CHECK(degenerate_runtime.faulted);
    CHECK(SameMaterialState(degenerate_runtime.state, committed_state));

    const CalibratedBeamMaterialAdapter::Error first_latched_error =
        degenerate_runtime.last_error;
    CalibratedBeamProductionStep::Input second_failure = input;
    second_failure.endpoints.endpoint_1_velocity_mps[0] =
        DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const CalibratedBeamProductionStep::Result repeated_result =
        CalibratedBeamProductionStep::Step(
            degenerate_runtime,
            second_failure);
    CHECK(
        repeated_result.material.error ==
        CalibratedBeamMaterialAdapter::Error::FAULT_LATCHED);
    CHECK(degenerate_runtime.last_error == first_latched_error);
    CHECK(SameMaterialState(degenerate_runtime.state, committed_state));

    CalibratedBeamMaterialAdapter::Runtime nonfinite_runtime = runtime;
    CalibratedBeamProductionStep::Input nonfinite = input;
    nonfinite.endpoints.endpoint_1_velocity_mps[1] =
        DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const CalibratedBeamProductionStep::Result nonfinite_result =
        CalibratedBeamProductionStep::Step(
            nonfinite_runtime,
            nonfinite);
    CHECK(!nonfinite_result.IsValid());
    CHECK(
        nonfinite_result.material.error ==
        CalibratedBeamMaterialAdapter::Error::NONFINITE_INPUT);
    CHECK(nonfinite_runtime.faulted);
    CHECK(SameMaterialState(nonfinite_runtime.state, committed_state));

    CalibratedBeamMaterialAdapter::Runtime disabled_runtime;
    const CalibratedBeamProductionStep::Result disabled_result =
        CalibratedBeamProductionStep::Step(
            disabled_runtime,
            nonfinite);
    CHECK(
        disabled_result.material.error ==
        CalibratedBeamMaterialAdapter::Error::DISABLED);
    CHECK(!disabled_runtime.faulted);
}

void TestSixtySecondThreeDimensionalSoak()
{
    using namespace RoR;

    const double time_step = 0.0005;
    const double mass_1 = 37.0;
    const double mass_2 = 83.0;
    std::array<double, 3> position_1 = {{2.0, -1.0, 3.5}};
    std::array<double, 3> position_2 = {{-1.0, 2.5, -0.5}};
    std::array<double, 3> velocity_1 = {{0.0, 0.0, 0.0}};
    std::array<double, 3> velocity_2 = {{0.0, 0.0, 0.0}};

    for (int step = 0; step < 120000; ++step)
    {
        BeamAxialKinematics::Input input;
        for (std::size_t lane = 0U; lane < 3U; ++lane)
        {
            input.endpoint_1_position_m[lane] = position_1[lane];
            input.endpoint_2_position_m[lane] = position_2[lane];
            input.endpoint_1_velocity_mps[lane] = velocity_1[lane];
            input.endpoint_2_velocity_mps[lane] = velocity_2[lane];
        }
        BeamAxialKinematics::Result kinematics =
            BeamAxialKinematics::Compute(input);
        CHECK(kinematics.IsValid());
        if (!kinematics.IsValid())
            return;

        if (step % 997 == 0)
        {
            const double impulse =
                2.0 + static_cast<double>((step / 997) % 7) * 0.125;
            for (std::size_t lane = 0U; lane < 3U; ++lane)
            {
                velocity_1[lane] +=
                    impulse * kinematics.unit_direction[lane];
                velocity_2[lane] -=
                    impulse * mass_1 / mass_2 *
                    kinematics.unit_direction[lane];
                input.endpoint_1_velocity_mps[lane] = velocity_1[lane];
                input.endpoint_2_velocity_mps[lane] = velocity_2[lane];
            }
            kinematics = BeamAxialKinematics::Compute(input);
            CHECK(kinematics.IsValid());
        }

        const double old_axial =
            kinematics.axial_relative_velocity_mps;
        const double energy_before =
            KineticEnergy(velocity_1, velocity_2, mass_1, mass_2);
        const BeamAxialResponse::DampingResult damping =
            BeamAxialResponse::ComputeDamping(
                kinematics.axial_relative_velocity_runtime_mps,
                1.0e9f,
                static_cast<float>(time_step),
                static_cast<float>(mass_1),
                static_cast<float>(mass_2),
                true,
                true);
        const double axial_force = damping.force;
        for (std::size_t lane = 0U; lane < 3U; ++lane)
        {
            const double force =
                axial_force * kinematics.unit_direction[lane];
            velocity_1[lane] += force / mass_1 * time_step;
            velocity_2[lane] -= force / mass_2 * time_step;
        }
        const std::array<double, 3> next_relative = {{
            velocity_1[0] - velocity_2[0],
            velocity_1[1] - velocity_2[1],
            velocity_1[2] - velocity_2[2]
        }};
        const double next_axial =
            Dot(next_relative, kinematics.unit_direction);
        const double tolerance =
            1.0e-5 * (1.0 + old_axial * old_axial);
        CHECK(old_axial * next_axial >= -tolerance);
        CHECK(axial_force * old_axial <= tolerance);
        const double energy_after =
            KineticEnergy(velocity_1, velocity_2, mass_1, mass_2);
        CHECK(
            energy_after <=
            energy_before + 1.0e-5 * (1.0 + energy_before));

        for (std::size_t lane = 0U; lane < 3U; ++lane)
        {
            position_1[lane] += velocity_1[lane] * time_step;
            position_2[lane] += velocity_2[lane] * time_step;
            CHECK(BeamAxialResponse::IsFinite(position_1[lane]));
            CHECK(BeamAxialResponse::IsFinite(position_2[lane]));
            CHECK(BeamAxialResponse::IsFinite(velocity_1[lane]));
            CHECK(BeamAxialResponse::IsFinite(velocity_2[lane]));
        }
    }
}

std::uint64_t HashBytes(
    std::uint64_t hash,
    const void* data,
    std::size_t size)
{
    const unsigned char* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

RoR::BeamAxialKinematics::Input PartitionInput(std::size_t index)
{
    std::uint64_t state =
        UINT64_C(0x9e3779b97f4a7c15) ^
        (static_cast<std::uint64_t>(index) *
            UINT64_C(0xd1b54a32d192ed03));
    RoR::BeamAxialKinematics::Input input;
    input.endpoint_1_position_m = {{
        NextExactSigned(state, 2560) + 0.5,
        NextExactSigned(state, 2560),
        NextExactSigned(state, 2560)
    }};
    input.endpoint_1_velocity_mps = {{
        NextExactSigned(state, 6400),
        NextExactSigned(state, 6400),
        NextExactSigned(state, 6400)
    }};
    return input;
}

std::uint64_t PartitionDigest(std::size_t partition_count)
{
    const std::size_t count = 675U;
    const std::size_t step_count = 64U;
    std::vector<RoR::BeamAxialKinematics::Input> states;
    states.reserve(count);
    for (std::size_t index = 0U; index < count; ++index)
        states.push_back(PartitionInput(index));

    for (std::size_t step = 0U; step < step_count; ++step)
    {
        for (std::size_t traversal = 0U;
             traversal < partition_count;
             ++traversal)
        {
            // A multi-partition run deliberately visits partitions in the
            // opposite global order. Final hashing remains canonical by beam
            // index, so this detects hidden shared/order state instead of
            // merely testing different loop boundaries over the same order.
            const std::size_t partition =
                partition_count == 1U
                    ? traversal
                    : partition_count - 1U - traversal;
            const std::size_t begin = count * partition / partition_count;
            const std::size_t end =
                count * (partition + 1U) / partition_count;
            for (std::size_t index = begin; index < end; ++index)
            {
                const RoR::BeamAxialKinematics::Result result =
                    RoR::BeamAxialKinematics::Compute(states[index]);
                CHECK(result.IsValid());
                const double axial_impulse =
                    -result.axial_relative_velocity_mps / 64.0;
                for (std::size_t lane = 0U; lane < 3U; ++lane)
                {
                    volatile double velocity_delta =
                        axial_impulse * result.unit_direction[lane];
                    states[index].endpoint_1_velocity_mps[lane] =
                        states[index].endpoint_1_velocity_mps[lane] +
                        velocity_delta;
                    states[index].endpoint_2_velocity_mps[lane] =
                        states[index].endpoint_2_velocity_mps[lane] -
                        velocity_delta;
                    volatile double position_delta_1 =
                        states[index].endpoint_1_velocity_mps[lane] /
                        2048.0;
                    volatile double position_delta_2 =
                        states[index].endpoint_2_velocity_mps[lane] /
                        2048.0;
                    states[index].endpoint_1_position_m[lane] =
                        states[index].endpoint_1_position_m[lane] +
                        position_delta_1;
                    states[index].endpoint_2_position_m[lane] =
                        states[index].endpoint_2_position_m[lane] +
                        position_delta_2;
                }
            }
        }
    }

    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (std::size_t index = 0U; index < states.size(); ++index)
    {
        const RoR::BeamAxialKinematics::Result result =
            RoR::BeamAxialKinematics::Compute(states[index]);
        CHECK(result.IsValid());
        hash = HashBytes(
            hash,
            &result.current_length_m,
            sizeof(result.current_length_m));
        hash = HashBytes(
            hash,
            &result.axial_relative_velocity_mps,
            sizeof(result.axial_relative_velocity_mps));
        hash = HashBytes(
            hash,
            result.unit_direction.data(),
            sizeof(double) * result.unit_direction.size());
    }
    return hash;
}

void TestPartitionDeterminism()
{
    const std::uint64_t one = PartitionDigest(1U);
    const std::uint64_t two = PartitionDigest(2U);
    const std::uint64_t eight = PartitionDigest(8U);
    CHECK(one == two);
    CHECK(one == eight);
    CHECK(one == UINT64_C(0xe17857fb5c6d66d0));
}

} // namespace

int main()
{
    TestKnownGeometryAndProjection();
    TestScaleSafeAndHostileInputs();
    TestTranslationSwapVelocityAndScaleInvariance();
    TestFixedSeedThreeDimensionalProperties();
    TestCalibratedAdapterHandoff();
    TestSixtySecondThreeDimensionalSoak();
    TestPartitionDeterminism();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " beam axial kinematics checks failed\n";
        return 1;
    }
    std::cout
        << "beam axial kinematics checks passed (20000 properties, "
        << "120000-step 3-D soak, partitions 1/2/8)\n";
    return 0;
}
