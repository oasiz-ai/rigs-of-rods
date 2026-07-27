#include "CalibratedBeamMaterialAdapter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

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

void CheckNear(
    double actual,
    double expected,
    double tolerance,
    int line)
{
    if (!RoR::CalibratedBeamMaterial::IsFinite(actual) ||
        !RoR::CalibratedBeamMaterial::IsFinite(expected) ||
        !RoR::CalibratedBeamMaterial::IsFinite(tolerance) ||
        std::abs(actual - expected) > tolerance)
    {
        std::cerr
            << "line " << line << ": expected " << expected
            << " +/- " << tolerance << ", got " << actual << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) \
    CheckNear((actual), (expected), (tolerance), __LINE__)

double DoubleFromBits(std::uint64_t bits)
{
    // Keep hostile IEEE-754 payloads observable under the game's
    // -ffast-math build. Without the volatile load, Clang is permitted to
    // constant-fold a known NaN away while compiling the test itself.
    double value = 0.0;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&value);
    for (std::size_t index = 0U; index < sizeof(value); ++index)
    {
        destination[index] = source[index];
    }
    return value;
}

std::uint64_t NextRandom(std::uint64_t& state)
{
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * UINT64_C(2685821657736338717);
}

double NextUnit(std::uint64_t& state)
{
    return static_cast<double>(NextRandom(state) >> 11U) *
        (1.0 / 9007199254740992.0);
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

RoR::CalibratedBeamMaterialAdapter::Configuration MakeConfiguration(
    double area_m2 = 0.002)
{
    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    configuration.cross_section_area_m2 = area_m2;
    configuration.material = Steel();
    return configuration;
}

RoR::CalibratedBeamMaterialAdapter::StepInput Input(
    double reference_length_m,
    double current_length_m)
{
    RoR::CalibratedBeamMaterialAdapter::StepInput input;
    input.reference_length_m = reference_length_m;
    input.current_length_m = current_length_m;
    input.direction = {{1.0, 0.0, 0.0}};
    input.is_plain_axial_beam = true;
    return input;
}

bool SameState(
    const RoR::CalibratedBeamMaterial::State& first,
    const RoR::CalibratedBeamMaterial::State& second)
{
    return
        first.plastic_strain == second.plastic_strain &&
        first.accumulated_plastic_strain ==
            second.accumulated_plastic_strain &&
        first.damage == second.damage &&
        first.damage_driver_density ==
            second.damage_driver_density &&
        first.last_total_strain == second.last_total_strain &&
        first.fractured == second.fractured;
}

void TestExplicitOptInAndAtomicConfiguration()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime runtime;
    const Runtime pristine = runtime;
    StepInput input = Input(2.0, 2.001);
    const StepResult disabled = Step(runtime, input);
    CHECK(disabled.error == Error::DISABLED);
    CHECK(!runtime.enabled);
    CHECK(!runtime.faulted);
    CHECK(SameState(runtime.state, pristine.state));

    Configuration invalid = MakeConfiguration();
    invalid.cross_section_area_m2 = 0.0;
    Error error = Error::NONE;
    RoR::CalibratedBeamMaterial::Error material_error =
        RoR::CalibratedBeamMaterial::Error::NONE;
    CHECK(!TryConfigure(
        runtime,
        invalid,
        &error,
        &material_error));
    CHECK(error == Error::INVALID_CROSS_SECTION_AREA);
    CHECK(
        material_error ==
        RoR::CalibratedBeamMaterial::Error::NONE);
    CHECK(!runtime.enabled);
    CHECK(SameState(runtime.state, pristine.state));

    const Configuration valid = MakeConfiguration();
    CHECK(TryConfigure(
        runtime,
        valid,
        &error,
        &material_error));
    CHECK(error == Error::NONE);
    CHECK(runtime.enabled);
    CHECK(!runtime.faulted);
    CHECK(
        runtime.configuration.cross_section_area_m2 ==
        valid.cross_section_area_m2);

    const Runtime configured = runtime;
    invalid = valid;
    invalid.material.elastic_modulus = 0.0;
    CHECK(!TryConfigure(
        runtime,
        invalid,
        &error,
        &material_error));
    CHECK(error == Error::MATERIAL_FAILURE);
    CHECK(
        material_error ==
        RoR::CalibratedBeamMaterial::Error::
            INVALID_ELASTIC_MODULUS);
    CHECK(runtime.enabled == configured.enabled);
    CHECK(
        runtime.configuration.cross_section_area_m2 ==
        configured.configuration.cross_section_area_m2);
    CHECK(
        runtime.configuration.material.elastic_modulus ==
        configured.configuration.material.elastic_modulus);
    CHECK(SameState(runtime.state, configured.state));

    Disable(runtime);
    CHECK(!runtime.enabled);
    CHECK(!runtime.faulted);
}

void TestDimensionedElasticResponse()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime tension_runtime;
    CHECK(TryConfigure(
        tension_runtime,
        MakeConfiguration(0.002)));
    StepInput tension = Input(2.0, 2.0001);
    tension.damping_force_n = -500.0;
    tension.direction = {{3.0, 4.0, 0.0}};
    const StepResult tension_result =
        Step(tension_runtime, tension);
    CHECK(tension_result.IsValid());

    const double expected_strain = 0.0001 / 2.0;
    const double expected_stress =
        Steel().elastic_modulus * expected_strain;
    const double expected_material_force =
        -expected_stress * 0.002;
    CHECK_NEAR(
        tension_result.total_strain,
        expected_strain,
        2.0e-16);
    CHECK_NEAR(
        tension_result.nominal_stress_pa,
        expected_stress,
        1.0e-3);
    CHECK_NEAR(
        tension_result.material_force_n,
        expected_material_force,
        1.0e-6);
    CHECK_NEAR(
        tension_result.axial_force_n,
        expected_material_force - 500.0,
        1.0e-6);
    CHECK_NEAR(
        tension_result.forces.endpoint_1[0],
        tension_result.axial_force_n * 0.6,
        1.0e-9);
    CHECK_NEAR(
        tension_result.forces.endpoint_1[1],
        tension_result.axial_force_n * 0.8,
        1.0e-9);
    CHECK(
        tension_result.forces.endpoint_2[0] ==
        -tension_result.forces.endpoint_1[0]);
    CHECK(
        tension_result.forces.endpoint_2[1] ==
        -tension_result.forces.endpoint_1[1]);

    // The tangent in force/displacement units is E*A/L.
    CHECK_NEAR(
        tension_result.material_force_n / 0.0001,
        -Steel().elastic_modulus * 0.002 / 2.0,
        0.01);

    const double expected_stored_energy =
        0.5 * Steel().elastic_modulus *
        expected_strain * expected_strain *
        (0.002 * 2.0);
    CHECK_NEAR(
        tension_result.stored_energy_j,
        expected_stored_energy,
        1.0e-8);

    Runtime compression_runtime;
    CHECK(TryConfigure(
        compression_runtime,
        MakeConfiguration(0.002)));
    const StepResult compression_result =
        Step(compression_runtime, Input(2.0, 1.9999));
    CHECK(compression_result.IsValid());
    CHECK_NEAR(
        compression_result.material_force_n,
        -expected_material_force,
        1.0e-6);
}

void TestPlasticHistoryAndReset()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime runtime;
    CHECK(TryConfigure(runtime, MakeConfiguration(0.001)));
    const StepResult loaded =
        Step(runtime, Input(1.0, 1.01));
    CHECK(loaded.IsValid());
    CHECK(loaded.yielded);
    CHECK(!loaded.fractured);
    CHECK(runtime.state.accumulated_plastic_strain > 0.0);
    CHECK(std::abs(runtime.state.plastic_strain) > 0.0);

    const double plastic_strain = runtime.state.plastic_strain;
    const double accumulated =
        runtime.state.accumulated_plastic_strain;
    const StepResult unloaded =
        Step(runtime, Input(1.0, 1.0 + plastic_strain));
    CHECK(unloaded.IsValid());
    CHECK_NEAR(unloaded.nominal_stress_pa, 0.0, 1.0e-3);
    CHECK_NEAR(unloaded.material_force_n, 0.0, 1.0e-6);
    CHECK(
        runtime.state.accumulated_plastic_strain ==
        accumulated);

    const Configuration retained = runtime.configuration;
    ResetHistory(runtime);
    CHECK(runtime.enabled);
    CHECK(!runtime.faulted);
    CHECK(SameState(
        runtime.state,
        RoR::CalibratedBeamMaterial::State()));
    CHECK(
        runtime.configuration.cross_section_area_m2 ==
        retained.cross_section_area_m2);
    CHECK(
        runtime.configuration.material.elastic_modulus ==
        retained.material.elastic_modulus);

    const StepResult at_rest =
        Step(runtime, Input(1.0, 1.0));
    CHECK(at_rest.IsValid());
    CHECK(at_rest.axial_force_n == 0.0);
}

void TestDamageAndFractureDisconnectDamping()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Configuration configuration = MakeConfiguration(0.001);
    configuration.material.damage_onset_plastic_strain =
        0.001;
    configuration.material.damage_driver_capacity_density =
        2.0e6;

    Runtime runtime;
    CHECK(TryConfigure(runtime, configuration));
    StepInput input = Input(1.0, 1.05);
    input.damping_force_n = 1.0e6;
    input.direction = {{-2.0, 1.0, 3.0}};
    const StepResult fractured = Step(runtime, input);
    CHECK(fractured.IsValid());
    CHECK(fractured.yielded);
    CHECK(fractured.fractured);
    CHECK(fractured.fractured_this_step);
    CHECK(runtime.state.fractured);
    CHECK(runtime.state.damage == 1.0);
    CHECK(fractured.axial_force_n == 0.0);
    CHECK(fractured.damping_force_n == 0.0);
    CHECK(fractured.forces.endpoint_1[0] == 0.0);
    CHECK(fractured.forces.endpoint_1[1] == 0.0);
    CHECK(fractured.forces.endpoint_1[2] == 0.0);
    CHECK(fractured.forces.endpoint_2[0] == 0.0);
    CHECK(fractured.dissipated_energy_increment_j > 0.0);

    const StepResult after_fracture =
        Step(runtime, Input(1.0, 0.9));
    CHECK(after_fracture.IsValid());
    CHECK(after_fracture.fractured);
    CHECK(!after_fracture.fractured_this_step);
    CHECK(after_fracture.axial_force_n == 0.0);
}

void TestInvalidStateAndInputsFailClosed()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime runtime;
    CHECK(TryConfigure(runtime, MakeConfiguration()));
    runtime.state.plastic_strain = 0.2;
    runtime.state.accumulated_plastic_strain = 0.01;
    const RoR::CalibratedBeamMaterial::State corrupted =
        runtime.state;
    const StepResult invalid_state =
        Step(runtime, Input(1.0, 1.001));
    CHECK(invalid_state.error == Error::MATERIAL_FAILURE);
    CHECK(
        invalid_state.material_error ==
        RoR::CalibratedBeamMaterial::Error::INVALID_STATE);
    CHECK(runtime.faulted);
    CHECK(SameState(runtime.state, corrupted));
    CHECK(invalid_state.axial_force_n == 0.0);
    CHECK(invalid_state.forces.endpoint_1[0] == 0.0);

    const StepResult latched =
        Step(runtime, Input(1.0, 1.001));
    CHECK(latched.error == Error::FAULT_LATCHED);
    CHECK(SameState(runtime.state, corrupted));

    ResetHistory(runtime);
    CHECK(!runtime.faulted);
    const StepResult recovered =
        Step(runtime, Input(1.0, 1.001));
    CHECK(recovered.IsValid());

    Runtime nonfinite_runtime;
    CHECK(TryConfigure(
        nonfinite_runtime,
        MakeConfiguration()));
    StepInput nonfinite_input = Input(1.0, 1.001);
    nonfinite_input.current_length_m =
        DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const StepResult nonfinite =
        Step(nonfinite_runtime, nonfinite_input);
    CHECK(nonfinite.error == Error::NONFINITE_INPUT);
    CHECK(nonfinite_runtime.faulted);
    CHECK(SameState(
        nonfinite_runtime.state,
        RoR::CalibratedBeamMaterial::State()));

    Runtime nonfinite_history_runtime;
    CHECK(TryConfigure(
        nonfinite_history_runtime,
        MakeConfiguration()));
    nonfinite_history_runtime.state.damage =
        DoubleFromBits(UINT64_C(0x7ff8000000000011));
    const StepResult nonfinite_history =
        Step(
            nonfinite_history_runtime,
            Input(1.0, 1.001));
    CHECK(
        nonfinite_history.error ==
        Error::MATERIAL_FAILURE);
    CHECK(
        nonfinite_history.material_error ==
        RoR::CalibratedBeamMaterial::Error::INVALID_STATE);
    CHECK(nonfinite_history_runtime.faulted);
    CHECK(!RoR::CalibratedBeamMaterial::IsFinite(
        nonfinite_history_runtime.state.damage));
    CHECK(nonfinite_history.axial_force_n == 0.0);

    Runtime role_runtime;
    CHECK(TryConfigure(role_runtime, MakeConfiguration()));
    StepInput role_input = Input(1.0, 1.001);
    role_input.is_plain_axial_beam = false;
    const StepResult unsupported =
        Step(role_runtime, role_input);
    CHECK(unsupported.error == Error::UNSUPPORTED_BEAM_ROLE);
    CHECK(role_runtime.faulted);
    CHECK(unsupported.axial_force_n == 0.0);

    Runtime direction_runtime;
    CHECK(TryConfigure(
        direction_runtime,
        MakeConfiguration()));
    StepInput direction_input = Input(1.0, 1.001);
    direction_input.direction = {{0.0, 0.0, 0.0}};
    const StepResult invalid_direction =
        Step(direction_runtime, direction_input);
    CHECK(invalid_direction.error == Error::INVALID_DIRECTION);
    CHECK(direction_runtime.faulted);

    Runtime range_runtime;
    CHECK(TryConfigure(
        range_runtime,
        MakeConfiguration(1.0e40)));
    const StepResult out_of_range =
        Step(range_runtime, Input(1.0, 1.0001));
    CHECK(
        out_of_range.error ==
        Error::FORCE_OUT_OF_RUNTIME_RANGE);
    CHECK(range_runtime.faulted);
    CHECK(out_of_range.axial_force_n == 0.0);
    CHECK(SameState(
        range_runtime.state,
        RoR::CalibratedBeamMaterial::State()));
}

void TestMomentumConservationProperties()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime runtime;
    CHECK(TryConfigure(runtime, MakeConfiguration(0.001)));
    std::uint64_t random = UINT64_C(0x783e46d294bc10af);
    for (std::uint32_t fixture = 0U;
         fixture < 50000U;
         ++fixture)
    {
        StepInput input;
        input.reference_length_m =
            0.25 + 4.75 * NextUnit(random);
        const double strain =
            (2.0 * NextUnit(random) - 1.0) * 5.0e-4;
        input.current_length_m =
            input.reference_length_m * (1.0 + strain);
        input.damping_force_n =
            (2.0 * NextUnit(random) - 1.0) * 1.0e5;
        input.direction = {{
            2.0 * NextUnit(random) - 1.0,
            2.0 * NextUnit(random) - 1.0,
            2.0 * NextUnit(random) - 1.0
        }};
        input.is_plain_axial_beam = true;
        if (input.direction[0] == 0.0 &&
            input.direction[1] == 0.0 &&
            input.direction[2] == 0.0)
        {
            input.direction[0] = 1.0;
        }

        const StepResult result = Step(runtime, input);
        CHECK(result.IsValid());
        if (!result.IsValid())
            break;

        double residual_squared = 0.0;
        double absolute_sum = 0.0;
        for (std::size_t lane = 0;
             lane < result.forces.endpoint_1.size();
             ++lane)
        {
            const double residual =
                result.forces.endpoint_1[lane] +
                result.forces.endpoint_2[lane];
            residual_squared += residual * residual;
            absolute_sum +=
                std::abs(result.forces.endpoint_1[lane]) +
                std::abs(result.forces.endpoint_2[lane]);
        }
        const double normalized_residual =
            std::sqrt(residual_squared) /
            std::max(absolute_sum, 1.0e-30);
        CHECK(normalized_residual <= 1.0e-6);
        CHECK(normalized_residual == 0.0);
    }
}

} // namespace

int main()
{
    TestExplicitOptInAndAtomicConfiguration();
    TestDimensionedElasticResponse();
    TestPlasticHistoryAndReset();
    TestDamageAndFractureDisconnectDamping();
    TestInvalidStateAndInputsFailClosed();
    TestMomentumConservationProperties();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
