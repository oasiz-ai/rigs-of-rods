#include "CalibratedBeamMaterial.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
        std::cerr << __FILE__ << ':' << line
                  << ": check failed: " << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(
    double actual,
    double expected,
    double relative_tolerance,
    double absolute_tolerance)
{
    const double scale =
        std::max(std::abs(actual), std::abs(expected));
    return std::abs(actual - expected) <=
        std::max(absolute_tolerance, relative_tolerance * scale);
}

#define CHECK_NEAR(actual, expected, relative_tolerance, absolute_tolerance) \
    CHECK(Near( \
        (actual), \
        (expected), \
        (relative_tolerance), \
        (absolute_tolerance)))

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

RoR::CalibratedBeamMaterial::Parameters Steel()
{
    RoR::CalibratedBeamMaterial::Parameters parameters;
    parameters.elastic_modulus = 200.0e9;
    parameters.yield_stress = 250.0e6;
    parameters.hardening_modulus = 2.0e9;
    parameters.damage_onset_plastic_strain = 0.04;
    parameters.damage_driver_capacity_density = 50.0e6;
    return parameters;
}

double StoredEnergy(
    const RoR::CalibratedBeamMaterial::State& state,
    const RoR::CalibratedBeamMaterial::Parameters& parameters)
{
    if (state.fractured)
        return 0.0;
    const double elastic_strain =
        state.last_total_strain - state.plastic_strain;
    const double undamaged =
        0.5 * parameters.elastic_modulus *
            elastic_strain * elastic_strain +
        0.5 * parameters.hardening_modulus *
            state.accumulated_plastic_strain *
            state.accumulated_plastic_strain;
    return (1.0 - state.damage) * undamaged;
}

void CheckEnergyIdentity(
    const RoR::CalibratedBeamMaterial::State& previous_state,
    const RoR::CalibratedBeamMaterial::Response& response,
    const RoR::CalibratedBeamMaterial::Parameters& parameters)
{
    const double expected_work =
        response.stored_energy_density -
        StoredEnergy(previous_state, parameters) +
        response.plastic_dissipation_increment +
        response.damage_dissipation_increment;
    CHECK_NEAR(
        response.mechanical_work_increment,
        expected_work,
        2.0e-12,
        1.0e-8);
}

void TestElasticSlopeYieldAndHardening()
{
    using namespace RoR::CalibratedBeamMaterial;

    const Parameters parameters = Steel();
    const State initial;
    const Response tension = Update(0.0005, parameters, initial);
    const Response compression = Update(-0.0005, parameters, initial);
    CHECK(tension.IsValid());
    CHECK(compression.IsValid());
    CHECK(!tension.yielded);
    CHECK(!compression.yielded);
    CHECK_NEAR(tension.stress, 100.0e6, 1.0e-12, 1.0e-6);
    CHECK_NEAR(compression.stress, -100.0e6, 1.0e-12, 1.0e-6);
    CHECK_NEAR(
        (tension.stress - compression.stress) / 0.001,
        parameters.elastic_modulus,
        1.0e-12,
        1.0e-3);
    CheckEnergyIdentity(initial, tension, parameters);
    CheckEnergyIdentity(initial, compression, parameters);

    const double yield_strain =
        parameters.yield_stress / parameters.elastic_modulus;
    const Response positive_yield =
        Update(yield_strain, parameters, initial);
    const Response negative_yield =
        Update(-yield_strain, parameters, initial);
    CHECK(positive_yield.IsValid());
    CHECK(negative_yield.IsValid());
    CHECK_NEAR(
        positive_yield.stress,
        parameters.yield_stress,
        1.0e-12,
        1.0e-6);
    CHECK_NEAR(
        negative_yield.stress,
        -parameters.yield_stress,
        1.0e-12,
        1.0e-6);

    const Response returned =
        Update(2.0 * yield_strain, parameters, initial);
    const double expected_multiplier =
        parameters.yield_stress /
        (parameters.elastic_modulus +
            parameters.hardening_modulus);
    CHECK(returned.IsValid());
    CHECK(returned.yielded);
    CHECK_NEAR(
        returned.plastic_multiplier,
        expected_multiplier,
        1.0e-12,
        1.0e-15);
    CHECK_NEAR(
        std::abs(returned.effective_stress),
        parameters.yield_stress +
            parameters.hardening_modulus *
            returned.state.accumulated_plastic_strain,
        1.0e-12,
        1.0e-5);
    CheckEnergyIdentity(initial, returned, parameters);
}

void TestPerfectPlasticityAndElasticUnloading()
{
    using namespace RoR::CalibratedBeamMaterial;

    Parameters parameters = Steel();
    parameters.hardening_modulus = 0.0;
    parameters.damage_onset_plastic_strain = 0.01;

    const State initial;
    const Response loaded = Update(0.003, parameters, initial);
    CHECK(loaded.IsValid());
    CHECK(loaded.yielded);
    CHECK_NEAR(
        loaded.stress,
        parameters.yield_stress,
        1.0e-12,
        1.0e-5);
    CHECK_NEAR(
        loaded.nominal_tangent_modulus,
        0.0,
        0.0,
        1.0e-8);
    CheckEnergyIdentity(initial, loaded, parameters);

    const double unloading_strain =
        loaded.state.last_total_strain - 0.0005;
    const Response unloaded =
        Update(unloading_strain, parameters, loaded.state);
    CHECK(unloaded.IsValid());
    CHECK(!unloaded.yielded);
    CHECK_NEAR(
        unloaded.nominal_tangent_modulus,
        parameters.elastic_modulus,
        1.0e-12,
        1.0e-3);
    CHECK(unloaded.mechanical_work_increment < 0.0);
    CHECK(unloaded.plastic_dissipation_increment == 0.0);
    CHECK(unloaded.damage_dissipation_increment == 0.0);
    CheckEnergyIdentity(loaded.state, unloaded, parameters);

    parameters.damage_onset_plastic_strain = 0.001;
    parameters.damage_driver_capacity_density = 2.0e6;
    const double damaged_alpha = 0.002;
    const double damaged_strain =
        damaged_alpha +
        parameters.yield_stress /
            parameters.elastic_modulus;
    const Response damaged =
        Update(damaged_strain, parameters, State());
    CHECK(damaged.IsValid());
    CHECK(damaged.state.damage > 0.0);
    CHECK(!damaged.state.fractured);
    CHECK_NEAR(
        damaged.nominal_tangent_modulus,
        -parameters.yield_stress *
            parameters.yield_stress /
            parameters.damage_driver_capacity_density,
        2.0e-12,
        1.0e-3);
    CheckEnergyIdentity(State(), damaged, parameters);
}

struct HistoryResult
{
    RoR::CalibratedBeamMaterial::State state;
    double mechanical_work = 0.0;
    double plastic_dissipation = 0.0;
    double damage_dissipation = 0.0;
    double peak_stress = 0.0;
    double quadrature_work = 0.0;
    double fracture_event_strain = 0.0;
    bool observed_fracture_event = false;
};

HistoryResult RunPiecewiseHistory(
    const RoR::CalibratedBeamMaterial::Parameters& parameters,
    const std::vector<double>& control_points,
    int subdivisions)
{
    using namespace RoR::CalibratedBeamMaterial;

    HistoryResult result;
    result.state.last_total_strain = control_points.front();
    CHECK(IsValidState(result.state, parameters));
    double previous_stress = 0.0;

    for (std::size_t segment = 1;
         segment < control_points.size();
         ++segment)
    {
        const double begin = control_points[segment - 1];
        const double end = control_points[segment];
        for (int step = 1; step <= subdivisions; ++step)
        {
            const double fraction =
                static_cast<double>(step) /
                static_cast<double>(subdivisions);
            const double strain = begin + (end - begin) * fraction;
            const State previous_state = result.state;
            const Response response =
                Update(strain, parameters, previous_state);
            CHECK(response.IsValid());
            if (!response.IsValid())
                return result;
            CHECK(
                response.state.accumulated_plastic_strain +
                    1.0e-14 >=
                previous_state.accumulated_plastic_strain);
            CHECK(
                response.state.damage + 1.0e-14 >=
                previous_state.damage);
            CHECK(
                response.state.damage_driver_density + 1.0e-7 >=
                previous_state.damage_driver_density);
            CHECK(response.plastic_dissipation_increment >= 0.0);
            CHECK(response.damage_dissipation_increment >= 0.0);
            CheckEnergyIdentity(previous_state, response, parameters);

            result.mechanical_work +=
                response.mechanical_work_increment;
            result.quadrature_work +=
                0.5 * (previous_stress + response.stress) *
                (strain - previous_state.last_total_strain);
            result.plastic_dissipation +=
                response.plastic_dissipation_increment;
            result.damage_dissipation +=
                response.damage_dissipation_increment;
            result.peak_stress =
                std::max(
                    result.peak_stress,
                    response.peak_abs_stress);
            if (response.fractured_this_step)
            {
                result.observed_fracture_event = true;
                result.fracture_event_strain =
                    response.fracture_event_strain;
            }
            result.state = response.state;
            previous_stress = response.stress;
        }
    }
    return result;
}

void TestCyclicHysteresisGoldenAndReversal()
{
    const RoR::CalibratedBeamMaterial::Parameters parameters = Steel();
    const std::vector<double> history = {
        0.0,
        0.004,
        -0.003,
        0.0025,
        0.0
    };

    const HistoryResult one =
        RunPiecewiseHistory(parameters, history, 1);
    const HistoryResult fine =
        RunPiecewiseHistory(parameters, history, 400);

    // Version-1 material-point golden values. Hysteresis is the irreversible
    // dissipation, not external work that still contains final stored energy.
    const double approved_hysteresis = 2.488380823e6;
    const double approved_residual_strain = 1.150464767e-3;
    CHECK_NEAR(
        fine.plastic_dissipation + fine.damage_dissipation,
        approved_hysteresis,
        1.0e-8,
        1.0e-8);
    CHECK_NEAR(
        fine.state.plastic_strain,
        approved_residual_strain,
        1.0e-8,
        1.0e-12);
    CHECK(fine.state.accumulated_plastic_strain >
        std::abs(fine.state.plastic_strain));
    CHECK(fine.state.damage == 0.0);

    CHECK_NEAR(
        one.plastic_dissipation,
        fine.plastic_dissipation,
        2.0e-11,
        1.0e-7);
    CHECK_NEAR(
        one.state.plastic_strain,
        fine.state.plastic_strain,
        2.0e-11,
        1.0e-14);
    CHECK_NEAR(
        one.peak_stress,
        fine.peak_stress,
        2.0e-11,
        1.0e-5);
    CHECK_NEAR(
        fine.mechanical_work,
        StoredEnergy(fine.state, parameters) +
            fine.plastic_dissipation +
            fine.damage_dissipation,
        2.0e-11,
        1.0e-6);
}

void TestDamageOnsetAndConsistentTangent()
{
    using namespace RoR::CalibratedBeamMaterial;

    Parameters parameters = Steel();
    parameters.damage_onset_plastic_strain = 0.001;
    parameters.damage_driver_capacity_density = 2.0e6;

    const double target_alpha = 0.0015;
    const double target_flow_stress =
        parameters.yield_stress +
        parameters.hardening_modulus * target_alpha;
    const double target_strain =
        target_alpha +
        target_flow_stress / parameters.elastic_modulus;
    const State initial;
    const Response onset_crossing =
        Update(target_strain, parameters, initial);
    CHECK(onset_crossing.IsValid());
    CHECK(onset_crossing.yielded);
    CHECK(!onset_crossing.state.fractured);

    const double offset =
        target_alpha -
        parameters.damage_onset_plastic_strain;
    const double onset_flow_stress =
        parameters.yield_stress +
        parameters.hardening_modulus *
            parameters.damage_onset_plastic_strain;
    const double expected_driver =
        onset_flow_stress * offset +
        0.5 * parameters.hardening_modulus *
            offset * offset;
    CHECK_NEAR(
        onset_crossing.state.accumulated_plastic_strain,
        target_alpha,
        2.0e-12,
        1.0e-14);
    CHECK_NEAR(
        onset_crossing.state.damage_driver_density,
        expected_driver,
        2.0e-12,
        1.0e-7);
    CHECK_NEAR(
        onset_crossing.state.damage,
        expected_driver /
            parameters.damage_driver_capacity_density,
        2.0e-12,
        1.0e-14);
    CHECK(onset_crossing.damage_dissipation_increment > 0.0);
    CheckEnergyIdentity(initial, onset_crossing, parameters);

    const double next_strain = target_strain + 0.0004;
    const double perturbation = 1.0e-9;
    const Response center =
        Update(next_strain, parameters, onset_crossing.state);
    const Response lower =
        Update(
            next_strain - perturbation,
            parameters,
            onset_crossing.state);
    const Response upper =
        Update(
            next_strain + perturbation,
            parameters,
            onset_crossing.state);
    CHECK(center.IsValid());
    CHECK(lower.IsValid());
    CHECK(upper.IsValid());
    const double finite_difference =
        (upper.stress - lower.stress) /
        (2.0 * perturbation);
    CHECK(center.nominal_tangent_modulus < 0.0);
    CHECK_NEAR(
        center.nominal_tangent_modulus,
        finite_difference,
        2.0e-6,
        1.0e3);

    const Response elastic_unload =
        Update(
            onset_crossing.state.last_total_strain - 0.0001,
            parameters,
            onset_crossing.state);
    CHECK(elastic_unload.IsValid());
    CHECK(!elastic_unload.yielded);
    CHECK_NEAR(
        elastic_unload.nominal_tangent_modulus,
        (1.0 - onset_crossing.state.damage) *
            parameters.elastic_modulus,
        2.0e-12,
        1.0e-3);
}

void TestFractureEventIsBoundedAndSubdivisionStable()
{
    using namespace RoR::CalibratedBeamMaterial;

    Parameters parameters = Steel();
    parameters.damage_onset_plastic_strain = 0.001;
    parameters.damage_driver_capacity_density = 2.0e6;
    const std::vector<double> ramp = {0.0, 0.02};

    const HistoryResult one =
        RunPiecewiseHistory(parameters, ramp, 1);
    const HistoryResult two =
        RunPiecewiseHistory(parameters, ramp, 2);
    const HistoryResult ten =
        RunPiecewiseHistory(parameters, ramp, 10);
    const HistoryResult thousand =
        RunPiecewiseHistory(parameters, ramp, 1000);
    const HistoryResult quadrature_reference =
        RunPiecewiseHistory(parameters, ramp, 10000);

    CHECK(one.state.fractured);
    CHECK(two.state.fractured);
    CHECK(ten.state.fractured);
    CHECK(thousand.state.fractured);
    CHECK(one.observed_fracture_event);
    CHECK(thousand.observed_fracture_event);
    CHECK(one.state.damage == 1.0);
    CHECK(
        one.state.damage_driver_density ==
        parameters.damage_driver_capacity_density);

    CHECK_NEAR(
        one.state.accumulated_plastic_strain,
        thousand.state.accumulated_plastic_strain,
        2.0e-12,
        1.0e-14);
    CHECK_NEAR(
        two.state.plastic_strain,
        thousand.state.plastic_strain,
        2.0e-12,
        1.0e-14);
    CHECK_NEAR(
        ten.mechanical_work,
        thousand.mechanical_work,
        2.0e-11,
        1.0e-6);
    CHECK_NEAR(
        one.plastic_dissipation,
        thousand.plastic_dissipation,
        2.0e-11,
        1.0e-6);
    CHECK_NEAR(
        one.damage_dissipation,
        thousand.damage_dissipation,
        2.0e-11,
        1.0e-6);
    CHECK_NEAR(
        one.peak_stress,
        thousand.peak_stress,
        2.0e-11,
        1.0e-5);
    CHECK_NEAR(
        one.fracture_event_strain,
        thousand.fracture_event_strain,
        2.0e-12,
        1.0e-14);
    CHECK_NEAR(
        one.mechanical_work,
        one.plastic_dissipation + one.damage_dissipation,
        2.0e-11,
        1.0e-6);
    // Independent endpoint-force quadrature converges to the exact
    // constitutive work reported by Update.
    CHECK_NEAR(
        quadrature_reference.quadrature_work,
        one.mechanical_work,
        2.0e-4,
        1.0);
    CHECK(one.peak_stress > 200.0e6);

    const Response huge_step =
        Update(1.0e6, parameters, State());
    CHECK(huge_step.IsValid());
    CHECK(huge_step.state.fractured);
    CHECK_NEAR(
        huge_step.state.accumulated_plastic_strain,
        one.state.accumulated_plastic_strain,
        2.0e-12,
        1.0e-14);
    CHECK_NEAR(
        huge_step.mechanical_work_increment,
        one.mechanical_work,
        2.0e-11,
        1.0e-6);
    CHECK_NEAR(
        huge_step.peak_abs_stress,
        one.peak_stress,
        2.0e-11,
        1.0e-5);
}

void TestMalformedStatesAndInputsFailClosed()
{
    using namespace RoR::CalibratedBeamMaterial;

    const Parameters parameters = Steel();
    State valid;
    const Response loaded = Update(0.003, parameters, valid);
    CHECK(loaded.IsValid());
    valid = loaded.state;

    State malformed = valid;
    malformed.damage = 0.5;
    Response response = Update(0.0031, parameters, malformed);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_STATE);
    CHECK(response.state.plastic_strain == malformed.plastic_strain);

    malformed = valid;
    malformed.damage_driver_density = 1.0;
    response = Update(0.0031, parameters, malformed);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_STATE);

    malformed = valid;
    malformed.fractured = true;
    response = Update(0.0031, parameters, malformed);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_STATE);

    malformed = valid;
    malformed.plastic_strain =
        malformed.accumulated_plastic_strain + 0.1;
    response = Update(0.0031, parameters, malformed);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_STATE);

    malformed = valid;
    malformed.last_total_strain = 1.0;
    response = Update(0.0031, parameters, malformed);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_STATE);

    response = Update(
        DoubleFromBits(UINT64_C(0x7ff0000000000000)),
        parameters,
        valid);
    CHECK(!response.IsValid());
    CHECK(response.error == Error::NONFINITE_INPUT);
    CHECK(response.stress == 0.0);
    CHECK(response.state.plastic_strain == valid.plastic_strain);

    Parameters invalid = parameters;
    invalid.schema_version += 1;
    response = Update(0.0, invalid, State());
    CHECK(!response.IsValid());
    CHECK(response.error == Error::UNSUPPORTED_SCHEMA);

    invalid = parameters;
    invalid.elastic_modulus = -1.0;
    response = Update(0.0, invalid, State());
    CHECK(!response.IsValid());
    CHECK(response.error == Error::INVALID_ELASTIC_MODULUS);

    invalid = parameters;
    invalid.hardening_modulus =
        std::numeric_limits<double>::max();
    invalid.damage_driver_capacity_density =
        std::numeric_limits<double>::max();
    response = Update(0.0, invalid, State());
    CHECK(!response.IsValid());
    CHECK(response.error == Error::NUMERIC_OVERFLOW);
}

void TestConditionedYieldResidual()
{
    using namespace RoR::CalibratedBeamMaterial;

    // Regression for a return map whose elastic strain is obtained by
    // subtracting two much larger strains. The exact endpoint is admissible;
    // only floating-point cancellation leaves a tiny positive yield residual.
    Parameters parameters;
    parameters.elastic_modulus = 1.1937975476e10;
    parameters.yield_stress = 50862.89355;
    parameters.hardening_modulus = 5.11479599e6;
    parameters.damage_onset_plastic_strain = 2.623e-7;
    parameters.damage_driver_capacity_density = 737.11437;

    const Response mapped =
        Update(-0.0078495202857, parameters, State());
    CHECK(mapped.IsValid());
    CHECK(IsValidState(mapped.state, parameters));

    State materially_overstressed = mapped.state;
    materially_overstressed.last_total_strain -= 1.0e-5;
    CHECK(!IsValidState(materially_overstressed, parameters));
}

void TestMonotonicPostOnsetEnergyCalibration()
{
    using namespace RoR::CalibratedBeamMaterial;

    Parameters parameters = Steel();
    const double target_energy_density = 12.5e6;
    parameters.damage_driver_capacity_density =
        2.0 * target_energy_density /
        (1.0 +
            parameters.hardening_modulus /
                parameters.elastic_modulus);

    double fracture_alpha = 0.0;
    CHECK(TryFracturePlasticStrain(parameters, fracture_alpha));

    double post_onset_path_work = 0.0;
    CHECK(TryPlasticPathWork(
        parameters.damage_onset_plastic_strain,
        fracture_alpha,
        parameters,
        post_onset_path_work));
    CHECK_NEAR(
        post_onset_path_work,
        target_energy_density,
        2.0e-12,
        1.0e-6);

    const double begin_alpha =
        parameters.damage_onset_plastic_strain;
    const double yield = parameters.yield_stress;
    const double hardening = parameters.hardening_modulus;
    const double elastic = parameters.elastic_modulus;
    const double capacity =
        parameters.damage_driver_capacity_density;
    const auto damage_antiderivative =
        [yield, hardening, elastic](double alpha)
        {
            const double alpha2 = alpha * alpha;
            const double alpha3 = alpha2 * alpha;
            const double alpha4 = alpha2 * alpha2;
            const double yield2 = yield * yield;
            const double hardening2 = hardening * hardening;
            const double hardening3 = hardening2 * hardening;
            return
                yield * yield2 / elastic * alpha +
                1.5 * yield2 * hardening / elastic * alpha2 +
                yield * hardening2 / elastic * alpha3 +
                0.25 * hardening3 / elastic * alpha4 +
                hardening * yield / 3.0 * alpha3 +
                0.25 * hardening2 * alpha4;
        };
    const double expected_damage_dissipation =
        0.5 / capacity *
        (damage_antiderivative(fracture_alpha) -
            damage_antiderivative(begin_alpha));
    const Response fractured =
        Update(1.0, parameters, State());
    CHECK(fractured.IsValid());
    CHECK(fractured.fractured_this_step);
    CHECK_NEAR(
        fractured.damage_dissipation_increment,
        expected_damage_dissipation,
        2.0e-11,
        1.0e-6);

    parameters.hardening_modulus = 0.0;
    parameters.damage_driver_capacity_density =
        2.0 * target_energy_density;
    CHECK(TryFracturePlasticStrain(parameters, fracture_alpha));
    CHECK(TryPlasticPathWork(
        parameters.damage_onset_plastic_strain,
        fracture_alpha,
        parameters,
        post_onset_path_work));
    CHECK_NEAR(
        post_onset_path_work,
        target_energy_density,
        2.0e-12,
        1.0e-6);
    const Response perfect_plastic_fracture =
        Update(1.0, parameters, State());
    const double perfect_plastic_damage_dissipation =
        0.5 *
        parameters.yield_stress *
        parameters.yield_stress /
        parameters.elastic_modulus;
    CHECK(perfect_plastic_fracture.IsValid());
    CHECK(perfect_plastic_fracture.fractured_this_step);
    CHECK_NEAR(
        perfect_plastic_fracture.damage_dissipation_increment,
        perfect_plastic_damage_dissipation,
        2.0e-11,
        1.0e-6);
}

std::uint64_t NextRandom(std::uint64_t& state)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

double UnitRandom(std::uint64_t& state)
{
    return static_cast<double>(NextRandom(state) >> 11) *
        (1.0 / 9007199254740992.0);
}

void TestRandomizedThermodynamicAndStateProperties()
{
    using namespace RoR::CalibratedBeamMaterial;

    std::uint64_t random_state =
        UINT64_C(0x6d5a56e9f13c2b47);
    for (int history = 0; history < 200; ++history)
    {
        Parameters parameters;
        parameters.elastic_modulus =
            (50.0 + 200.0 * UnitRandom(random_state)) * 1.0e9;
        parameters.yield_stress =
            (100.0 + 500.0 * UnitRandom(random_state)) * 1.0e6;
        parameters.hardening_modulus =
            history % 11 == 0 ?
            0.0 :
            (0.1 + 10.0 * UnitRandom(random_state)) * 1.0e9;
        parameters.damage_onset_plastic_strain =
            0.0002 + 0.01 * UnitRandom(random_state);
        parameters.damage_driver_capacity_density =
            (0.5 + 20.0 * UnitRandom(random_state)) * 1.0e6;

        State state;
        double stored_energy = 0.0;
        for (int step = 0; step < 100; ++step)
        {
            const double signed_increment =
                (2.0 * UnitRandom(random_state) - 1.0) *
                0.003;
            const double next_strain =
                state.last_total_strain + signed_increment;
            const State previous_state = state;
            const Response response =
                Update(next_strain, parameters, previous_state);
            CHECK(response.IsValid());
            if (!response.IsValid())
                break;
            CHECK(IsValidState(response.state, parameters));
            CHECK(
                response.state.accumulated_plastic_strain +
                    1.0e-13 >=
                previous_state.accumulated_plastic_strain);
            CHECK(
                response.state.damage + 1.0e-13 >=
                previous_state.damage);
            CHECK(response.plastic_dissipation_increment >= 0.0);
            CHECK(response.damage_dissipation_increment >= 0.0);
            CHECK(response.peak_abs_stress + 1.0e-5 >=
                std::abs(response.stress));
            CHECK_NEAR(
                response.mechanical_work_increment,
                response.stored_energy_density -
                    stored_energy +
                    response.plastic_dissipation_increment +
                    response.damage_dissipation_increment,
                5.0e-11,
                1.0e-6);

            if (!response.state.fractured)
            {
                const double effective_stress =
                    parameters.elastic_modulus *
                    (next_strain -
                        response.state.plastic_strain);
                const double flow_stress =
                    parameters.yield_stress +
                    parameters.hardening_modulus *
                    response.state.accumulated_plastic_strain;
                CHECK(
                    std::abs(effective_stress) <=
                    flow_stress +
                        1.0e-5 *
                        std::max(1.0, flow_stress));
            }
            else
            {
                CHECK(response.stress == 0.0);
                CHECK(response.stored_energy_density == 0.0);
                CHECK(response.nominal_tangent_modulus == 0.0);
            }

            stored_energy = response.stored_energy_density;
            state = response.state;
        }
    }
}

} // anonymous namespace

int main()
{
    TestElasticSlopeYieldAndHardening();
    TestPerfectPlasticityAndElasticUnloading();
    TestCyclicHysteresisGoldenAndReversal();
    TestDamageOnsetAndConsistentTangent();
    TestFractureEventIsBoundedAndSubdivisionStable();
    TestMalformedStatesAndInputsFailClosed();
    TestConditionedYieldResidual();
    TestMonotonicPostOnsetEnergyCalibration();
    TestRandomizedThermodynamicAndStateProperties();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " calibrated material test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "calibrated beam material tests passed\n";
    return EXIT_SUCCESS;
}
