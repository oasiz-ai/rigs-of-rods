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

#include "CalibratedBeamFractureCalibration.h"
#include "CalibratedBeamMaterial.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace Calibration =
    RoR::CalibratedBeamFractureCalibration;
namespace Material = RoR::CalibratedBeamMaterial;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "FAIL line %d: %s\n",
            line,
            expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

double RelativeError(double measured, double expected)
{
    const double scale = std::max(1.0, std::abs(expected));
    return std::abs(measured - expected) / scale;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Calibration::Inputs ValidCalibrationInputs()
{
    Calibration::Inputs inputs;
    inputs.fracture_energy_per_area = 20000.0;
    inputs.characteristic_length = 0.04;
    inputs.elastic_modulus = 2.0e9;
    inputs.hardening_modulus = 4.0e7;
    return inputs;
}

bool SameCalibration(
    const Calibration::Calibration& first,
    const Calibration::Calibration& second)
{
    return std::memcmp(
        &first,
        &second,
        sizeof(first)) == 0;
}

void ExpectCalibrationError(
    const Calibration::Inputs& inputs,
    Calibration::Error expected)
{
    Calibration::Calibration output;
    output.post_onset_work_density = 11.25;
    output.hardening_ratio = 22.5;
    output.damage_driver_capacity_density = 45.0;
    const Calibration::Calibration sentinel = output;
    CHECK(Calibration::TryCalibrate(inputs, output) == expected);
    CHECK(SameCalibration(output, sentinel));
}

void TestCalibrationContract()
{
    Calibration::Inputs inputs = ValidCalibrationInputs();
    Calibration::Calibration output;
    CHECK(
        Calibration::TryCalibrate(inputs, output) ==
        Calibration::Error::NONE);
    CHECK(RelativeError(output.post_onset_work_density, 500000.0) < 1e-15);
    CHECK(RelativeError(output.hardening_ratio, 0.02) < 1e-15);
    CHECK(
        RelativeError(
            output.damage_driver_capacity_density,
            1000000.0 / 1.02) <
        1e-15);

    Calibration::Inputs refined = inputs;
    refined.characteristic_length *= 0.5;
    Calibration::Calibration refined_output;
    CHECK(
        Calibration::TryCalibrate(refined, refined_output) ==
        Calibration::Error::NONE);
    CHECK(
        RelativeError(
            refined_output.damage_driver_capacity_density,
            2.0 * output.damage_driver_capacity_density) <
        1e-15);

    Calibration::Inputs harder = inputs;
    harder.hardening_modulus *= 4.0;
    Calibration::Calibration harder_output;
    CHECK(
        Calibration::TryCalibrate(harder, harder_output) ==
        Calibration::Error::NONE);
    CHECK(
        harder_output.damage_driver_capacity_density <
        output.damage_driver_capacity_density);

    inputs = ValidCalibrationInputs();
    inputs.schema_version =
        Calibration::CALIBRATION_SCHEMA_VERSION + 1;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::UNSUPPORTED_SCHEMA);

    inputs = ValidCalibrationInputs();
    inputs.convention = static_cast<Calibration::Convention>(999);
    ExpectCalibrationError(
        inputs,
        Calibration::Error::UNSUPPORTED_CONVENTION);

    const double quiet_nan =
        DoubleFromBits(UINT64_C(0x7ff8000000000042));
    const double positive_infinity =
        DoubleFromBits(UINT64_C(0x7ff0000000000000));
    for (int field = 0; field < 4; ++field)
    {
        inputs = ValidCalibrationInputs();
        double* values[] = {
            &inputs.fracture_energy_per_area,
            &inputs.characteristic_length,
            &inputs.elastic_modulus,
            &inputs.hardening_modulus};
        *values[field] = quiet_nan;
        ExpectCalibrationError(
            inputs,
            Calibration::Error::NONFINITE_INPUT);

        inputs = ValidCalibrationInputs();
        double* infinite_values[] = {
            &inputs.fracture_energy_per_area,
            &inputs.characteristic_length,
            &inputs.elastic_modulus,
            &inputs.hardening_modulus};
        *infinite_values[field] = positive_infinity;
        ExpectCalibrationError(
            inputs,
            Calibration::Error::NONFINITE_INPUT);
    }

    inputs = ValidCalibrationInputs();
    inputs.fracture_energy_per_area = 0.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_FRACTURE_ENERGY);
    inputs.fracture_energy_per_area = -1.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_FRACTURE_ENERGY);

    inputs = ValidCalibrationInputs();
    inputs.characteristic_length = 0.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_CHARACTERISTIC_LENGTH);
    inputs.characteristic_length = -1.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_CHARACTERISTIC_LENGTH);

    inputs = ValidCalibrationInputs();
    inputs.elastic_modulus = 0.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_ELASTIC_MODULUS);
    inputs.elastic_modulus = -1.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_ELASTIC_MODULUS);

    inputs = ValidCalibrationInputs();
    inputs.hardening_modulus = -1.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::INVALID_HARDENING_MODULUS);

    inputs = ValidCalibrationInputs();
    inputs.fracture_energy_per_area = 1.0e300;
    inputs.characteristic_length = 1.0e-300;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::NUMERIC_RANGE);

    inputs = ValidCalibrationInputs();
    inputs.fracture_energy_per_area = 1.0e-300;
    inputs.characteristic_length = 1.0e300;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::NUMERIC_RANGE);

    inputs = ValidCalibrationInputs();
    inputs.elastic_modulus = 1.0e-300;
    inputs.hardening_modulus = 1.0e300;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::NUMERIC_RANGE);

    inputs = ValidCalibrationInputs();
    inputs.fracture_energy_per_area = 1.0e308;
    inputs.characteristic_length = 1.0;
    inputs.hardening_modulus = 0.0;
    ExpectCalibrationError(
        inputs,
        Calibration::Error::NUMERIC_RANGE);
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

    double Unit()
    {
        return static_cast<double>(Next() >> 11) *
            (1.0 / 9007199254740992.0);
    }

private:
    std::uint64_t m_state;
};

double LogSample(FixedRandom& random, double lower, double upper)
{
    return std::exp(
        std::log(lower) +
        random.Unit() * (std::log(upper) - std::log(lower)));
}

void TestCalibrationProperties()
{
    FixedRandom random(UINT64_C(0x62a9d9ed799705f5));
    for (int sample = 0; sample < 20000; ++sample)
    {
        Calibration::Inputs inputs;
        inputs.fracture_energy_per_area =
            LogSample(random, 1.0, 1.0e7);
        inputs.characteristic_length =
            LogSample(random, 1.0e-4, 10.0);
        inputs.elastic_modulus =
            LogSample(random, 1.0e6, 1.0e12);
        const double ratio = 4.0 * random.Unit();
        inputs.hardening_modulus =
            ratio * inputs.elastic_modulus;

        Calibration::Calibration output;
        CHECK(
            Calibration::TryCalibrate(inputs, output) ==
            Calibration::Error::NONE);
        if (g_failures != 0)
            return;

        const long double reference =
            (2.0L *
                static_cast<long double>(
                    inputs.fracture_energy_per_area) /
                static_cast<long double>(
                    inputs.characteristic_length)) /
            (1.0L +
                static_cast<long double>(
                    inputs.hardening_modulus) /
                static_cast<long double>(
                    inputs.elastic_modulus));
        const long double relative_error =
            std::abs(
                static_cast<long double>(
                    output.damage_driver_capacity_density) -
                reference) /
            reference;
        // MSVC implements long double with the same precision as double.
        // Scale the bound to that platform's representable resolution while
        // preserving the tighter gate on extended-precision implementations.
        const long double calibration_tolerance =
            std::max(
                8.0e-16L,
                8.0L *
                    std::numeric_limits<long double>::epsilon());
        if (!(relative_error < calibration_tolerance))
        {
            std::fprintf(
                stderr,
                "calibration mismatch sample=%d "
                "G=%.17g l=%.17g E=%.17g H=%.17g "
                "measured=%.17g reference=%.21Lg "
                "relative_error=%.21Lg tolerance=%.21Lg "
                "long_double_digits=%d\n",
                sample,
                inputs.fracture_energy_per_area,
                inputs.characteristic_length,
                inputs.elastic_modulus,
                inputs.hardening_modulus,
                output.damage_driver_capacity_density,
                reference,
                relative_error,
                calibration_tolerance,
                std::numeric_limits<long double>::digits);
        }
        CHECK(relative_error < calibration_tolerance);

        const double recovered_fracture_energy =
            0.5 *
            output.damage_driver_capacity_density *
            (1.0 + output.hardening_ratio) *
            inputs.characteristic_length;
        CHECK(
            RelativeError(
                recovered_fracture_energy,
                inputs.fracture_energy_per_area) <
            8.0e-15);
    }
}

void TestHardeningConventionAgainstMaterialLaw()
{
    Calibration::Inputs inputs;
    inputs.fracture_energy_per_area = 18000.0;
    inputs.characteristic_length = 0.025;
    inputs.elastic_modulus = 2.0e9;
    inputs.hardening_modulus = 4.0e7;
    Calibration::Calibration calibration;
    CHECK(
        Calibration::TryCalibrate(inputs, calibration) ==
        Calibration::Error::NONE);

    Material::Parameters parameters;
    parameters.elastic_modulus = inputs.elastic_modulus;
    parameters.yield_stress = 2.0e6;
    parameters.hardening_modulus = inputs.hardening_modulus;
    parameters.damage_onset_plastic_strain = 0.01;
    parameters.damage_driver_capacity_density =
        calibration.damage_driver_capacity_density;
    CHECK(
        Material::ValidateParameters(parameters) ==
        Material::Error::NONE);

    double onset_flow_stress = 0.0;
    CHECK(
        Material::TryFlowStress(
            parameters.damage_onset_plastic_strain,
            parameters,
            onset_flow_stress));
    Material::State state;
    const Material::Response onset_response = Material::Update(
        parameters.damage_onset_plastic_strain +
            onset_flow_stress / parameters.elastic_modulus,
        parameters,
        state);
    CHECK(onset_response.IsValid());
    CHECK(!onset_response.state.fractured);
    CHECK(
        RelativeError(
            onset_response.state.accumulated_plastic_strain,
            parameters.damage_onset_plastic_strain) <
        2.0e-14);
    CHECK(onset_response.state.damage == 0.0);
    state = onset_response.state;

    double fracture_alpha = 0.0;
    CHECK(
        Material::TryFracturePlasticStrain(
            parameters,
            fracture_alpha));
    double measured_post_onset_work_density = 0.0;
    int fracture_events = 0;
    const int steps = 4096;
    for (int step = 1; step <= steps; ++step)
    {
        double requested_alpha =
            parameters.damage_onset_plastic_strain +
            (fracture_alpha -
                parameters.damage_onset_plastic_strain) *
                static_cast<double>(step) /
                static_cast<double>(steps);
        if (step == steps)
        {
            requested_alpha *=
                1.0 +
                128.0 * std::numeric_limits<double>::epsilon();
        }
        double flow_stress = 0.0;
        CHECK(
            Material::TryFlowStress(
                requested_alpha,
                parameters,
                flow_stress));
        const Material::Response response = Material::Update(
            requested_alpha +
                flow_stress / parameters.elastic_modulus,
            parameters,
            state);
        CHECK(response.IsValid());
        if (!response.IsValid())
            return;
        state = response.state;
        measured_post_onset_work_density +=
            response.mechanical_work_increment;
        if (response.fractured_this_step)
            ++fracture_events;
    }

    CHECK(fracture_events == 1);
    CHECK(state.fractured);
    CHECK(
        RelativeError(
            measured_post_onset_work_density,
            calibration.post_onset_work_density) <
        2.0e-11);
}

struct CouponElement
{
    Material::Parameters parameters;
    Material::State state;
    double length = 0.0;
    bool is_notch = false;
};

struct CouponResult
{
    int element_count = 0;
    int fracture_events = 0;
    bool valid = true;
    bool non_notch_yielded = false;
    bool monotonic_global_displacement = true;
    double element_length = 0.0;
    double localized_post_onset_work = 0.0;
    double total_internal_work = 0.0;
    double total_external_work = 0.0;
    double total_irreversible_dissipation = 0.0;
    double peak_force = 0.0;
    double permanent_set = 0.0;
    double fracture_displacement = 0.0;
    double max_equilibrium_residual = 0.0;
};

Material::Parameters CouponParameters(
    double yield_stress,
    double element_length)
{
    const double elastic_modulus = 2.0e9;
    const double hardening_modulus = 0.0;
    const double fracture_energy = 20000.0;

    Calibration::Inputs inputs;
    inputs.fracture_energy_per_area = fracture_energy;
    inputs.characteristic_length = element_length;
    inputs.elastic_modulus = elastic_modulus;
    inputs.hardening_modulus = hardening_modulus;
    Calibration::Calibration calibration;
    CHECK(
        Calibration::TryCalibrate(inputs, calibration) ==
        Calibration::Error::NONE);

    Material::Parameters parameters;
    parameters.elastic_modulus = elastic_modulus;
    parameters.yield_stress = yield_stress;
    parameters.hardening_modulus = hardening_modulus;
    parameters.damage_onset_plastic_strain = 0.0;
    parameters.damage_driver_capacity_density =
        calibration.damage_driver_capacity_density;
    CHECK(
        Material::ValidateParameters(parameters) ==
        Material::Error::NONE);
    return parameters;
}

void AccumulateResponse(
    const Material::Response& response,
    double reference_volume,
    CouponResult& result)
{
    if (!response.IsValid())
    {
        result.valid = false;
        return;
    }
    result.total_internal_work +=
        response.mechanical_work_increment * reference_volume;
    result.total_irreversible_dissipation +=
        (response.plastic_dissipation_increment +
            response.damage_dissipation_increment) *
        reference_volume;
    result.peak_force = std::max(
        result.peak_force,
        response.peak_abs_stress * 0.001);
}

CouponResult RunLocalizedSeriesCoupon(int element_count)
{
    const double total_length = 0.08;
    const double area = 0.001;
    const double elastic_modulus = 2.0e9;
    const double base_yield_stress = 2.0e6;
    const double notch_yield_stress =
        0.995 * base_yield_stress;
    const int path_steps = 32768;
    const double element_length =
        total_length / static_cast<double>(element_count);
    const int notch_index = element_count / 2;

    CouponResult result;
    result.element_count = element_count;
    result.element_length = element_length;

    std::vector<CouponElement> elements(
        static_cast<std::size_t>(element_count));
    for (int index = 0; index < element_count; ++index)
    {
        CouponElement& element =
            elements[static_cast<std::size_t>(index)];
        element.length = element_length;
        element.is_notch = index == notch_index;
        element.parameters = CouponParameters(
            element.is_notch ?
                notch_yield_stress :
                base_yield_stress,
            element_length);
    }

    // Load the series chain elastically to the 0.5%-weaker notch yield point.
    // Every element sees the same axial force. The other elements remain
    // elastic for the complete softening branch and unload as the notch loses
    // force; they are not independent parallel copies of the failing beam.
    const double initial_common_stress = notch_yield_stress;
    double previous_displacement = 0.0;
    for (int index = 0; index < element_count; ++index)
    {
        CouponElement& element =
            elements[static_cast<std::size_t>(index)];
        const Material::Response response = Material::Update(
            initial_common_stress / elastic_modulus,
            element.parameters,
            element.state);
        AccumulateResponse(
            response,
            area * element.length,
            result);
        if (!response.IsValid())
            return result;
        element.state = response.state;
        previous_displacement +=
            response.state.last_total_strain *
            element.length;
        if (!element.is_notch && response.yielded)
            result.non_notch_yielded = true;
    }

    double previous_force = initial_common_stress * area;
    result.total_external_work =
        0.5 * previous_force * previous_displacement;

    CouponElement& notch =
        elements[static_cast<std::size_t>(notch_index)];
    double fracture_plastic_strain = 0.0;
    CHECK(
        Material::TryFracturePlasticStrain(
            notch.parameters,
            fracture_plastic_strain));

    for (int step = 1; step <= path_steps; ++step)
    {
        double requested_alpha =
            fracture_plastic_strain *
            static_cast<double>(step) /
            static_cast<double>(path_steps);
        if (step == path_steps)
        {
            // Cross the event by a few ulps so both strict and fast-math builds
            // take the exact event-limited branch. Geometry below uses the
            // reported event strain, never this harmless zero-force overshoot.
            requested_alpha *=
                1.0 +
                128.0 * std::numeric_limits<double>::epsilon();
        }
        const double flow_stress =
            notch.parameters.yield_stress +
            notch.parameters.hardening_modulus *
                requested_alpha;
        const double notch_requested_strain =
            requested_alpha +
            flow_stress / notch.parameters.elastic_modulus;
        const Material::Response notch_response =
            Material::Update(
                notch_requested_strain,
                notch.parameters,
                notch.state);
        AccumulateResponse(
            notch_response,
            area * notch.length,
            result);
        if (!notch_response.IsValid())
            return result;
        notch.state = notch_response.state;
        result.localized_post_onset_work +=
            notch_response.mechanical_work_increment *
            area * notch.length;
        if (notch_response.fractured_this_step)
            ++result.fracture_events;

        const double common_stress = notch_response.stress;
        const double notch_geometry_strain =
            notch_response.fractured_this_step ?
                notch_response.fracture_event_strain :
                notch_response.state.last_total_strain;
        double displacement =
            notch_geometry_strain * notch.length;

        for (int index = 0; index < element_count; ++index)
        {
            if (index == notch_index)
                continue;
            CouponElement& element =
                elements[static_cast<std::size_t>(index)];
            const Material::Response response = Material::Update(
                common_stress / element.parameters.elastic_modulus,
                element.parameters,
                element.state);
            AccumulateResponse(
                response,
                area * element.length,
                result);
            if (!response.IsValid())
                return result;
            element.state = response.state;
            if (response.yielded)
                result.non_notch_yielded = true;

            const double force_scale = std::max(
                1.0,
                std::max(
                    std::abs(common_stress),
                    std::abs(response.stress)));
            result.max_equilibrium_residual = std::max(
                result.max_equilibrium_residual,
                std::abs(response.stress - common_stress) /
                    force_scale);
            displacement +=
                response.state.last_total_strain *
                element.length;
        }

        const double force = common_stress * area;
        if (displacement + 1.0e-14 < previous_displacement)
            result.monotonic_global_displacement = false;
        result.total_external_work +=
            0.5 * (previous_force + force) *
            (displacement - previous_displacement);
        previous_force = force;
        previous_displacement = displacement;
    }

    for (std::size_t index = 0; index < elements.size(); ++index)
    {
        result.permanent_set +=
            elements[index].state.plastic_strain *
            elements[index].length;
    }
    result.fracture_displacement = previous_displacement;
    return result;
}

double RelativeSpread(
    const std::vector<CouponResult>& results,
    double CouponResult::*field,
    double reference)
{
    double minimum = results.front().*field;
    double maximum = minimum;
    for (std::size_t index = 1; index < results.size(); ++index)
    {
        minimum = std::min(minimum, results[index].*field);
        maximum = std::max(maximum, results[index].*field);
    }
    return (maximum - minimum) / std::max(1.0, std::abs(reference));
}

void TestLocalizedSeriesMeshRefinement()
{
    const double area = 0.001;
    const double elastic_modulus = 2.0e9;
    const double notch_yield_stress = 0.995 * 2.0e6;
    const double fracture_energy_per_area = 20000.0;
    const double target_crack_work =
        fracture_energy_per_area * area;
    const double target_peak_force =
        notch_yield_stress * area;
    const double target_permanent_set =
        2.0 * fracture_energy_per_area /
        notch_yield_stress;
    const int refinements[] = {1, 2, 4, 8, 16};

    std::vector<CouponResult> results;
    for (std::size_t index = 0;
        index < sizeof(refinements) / sizeof(refinements[0]);
        ++index)
    {
        const CouponResult result =
            RunLocalizedSeriesCoupon(refinements[index]);
        results.push_back(result);

        const double expected_total_work =
            target_crack_work +
            0.5 *
                notch_yield_stress * notch_yield_stress /
                elastic_modulus *
                area * result.element_length;
        const double expected_fracture_displacement =
            target_permanent_set +
            result.element_length *
                notch_yield_stress / elastic_modulus;

        CHECK(result.valid);
        CHECK(result.fracture_events == 1);
        CHECK(!result.non_notch_yielded);
        CHECK(result.monotonic_global_displacement);
        CHECK(result.max_equilibrium_residual < 2.0e-13);
        CHECK(
            RelativeError(
                result.localized_post_onset_work,
                target_crack_work) <
            2.0e-11);
        CHECK(
            RelativeError(
                result.total_internal_work,
                expected_total_work) <
            2.0e-11);
        CHECK(
            RelativeError(
                result.total_external_work,
                result.total_internal_work) <
            2.0e-10);
        CHECK(
            RelativeError(
                result.total_irreversible_dissipation,
                result.total_internal_work) <
            2.0e-10);
        CHECK(
            RelativeError(
                result.peak_force,
                target_peak_force) <
            2.0e-13);
        CHECK(
            RelativeError(
                result.permanent_set,
                target_permanent_set) <
            2.0e-11);
        CHECK(
            RelativeError(
                result.fracture_displacement,
                expected_fracture_displacement) <
            2.0e-11);

        std::printf(
            "series N=%2d l=% .6e crack_work=% .12e "
            "total_work=% .12e diss=% .12e peak=% .12e "
            "perm=% .12e event_u=% .12e eq=% .3e\n",
            result.element_count,
            result.element_length,
            result.localized_post_onset_work,
            result.total_internal_work,
            result.total_irreversible_dissipation,
            result.peak_force,
            result.permanent_set,
            result.fracture_displacement,
            result.max_equilibrium_residual);
    }

    const double fracture_energy_spread = RelativeSpread(
        results,
        &CouponResult::localized_post_onset_work,
        target_crack_work);
    const double total_work_spread = RelativeSpread(
        results,
        &CouponResult::total_internal_work,
        target_crack_work);
    const double dissipation_spread = RelativeSpread(
        results,
        &CouponResult::total_irreversible_dissipation,
        target_crack_work);
    const double peak_force_spread = RelativeSpread(
        results,
        &CouponResult::peak_force,
        target_peak_force);
    const double permanent_set_spread = RelativeSpread(
        results,
        &CouponResult::permanent_set,
        target_permanent_set);

    std::printf(
        "mesh spreads: crack_work=%.9f%% total_work=%.9f%% "
        "dissipation=%.9f%% peak=%.9f%% permanent_set=%.9f%%\n",
        100.0 * fracture_energy_spread,
        100.0 * total_work_spread,
        100.0 * dissipation_spread,
        100.0 * peak_force_spread,
        100.0 * permanent_set_spread);

    // P1's 2% threshold is applied to an actual localized series system:
    // common force, summed element elongations, one deterministic weak element,
    // and unloading of the intact chain. No result is divided by a measured
    // mesh-dependent correction after the simulation.
    CHECK(fracture_energy_spread <= 0.02);
    CHECK(total_work_spread <= 0.02);
    CHECK(dissipation_spread <= 0.02);
    CHECK(peak_force_spread <= 0.02);
}

} // namespace

int main()
{
    TestCalibrationContract();
    TestCalibrationProperties();
    TestHardeningConventionAgainstMaterialLaw();
    TestLocalizedSeriesMeshRefinement();

    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "%d calibrated beam mesh-refinement checks failed\n",
            g_failures);
        return 1;
    }
    std::printf(
        "calibrated beam fracture calibration and localized "
        "mesh-refinement checks passed\n");
    return 0;
}
