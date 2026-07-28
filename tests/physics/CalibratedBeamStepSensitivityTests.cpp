#include "CalibratedBeamMaterialAdapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>

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

bool Finite(double value)
{
    return RoR::CalibratedBeamMaterial::IsFinite(value);
}

double DoubleFromBits(std::uint64_t bits)
{
    // Preserve hostile IEEE-754 payloads even under -ffast-math.
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

double NextUnit(std::uint64_t& state)
{
    return static_cast<double>(NextRandom(state) >> 11U) *
        (1.0 / 9007199254740992.0);
}

const std::uint64_t TOTAL_DURATION_US = UINT64_C(1200000);
const std::uint64_t RAMP_END_US = UINT64_C(183731);
const std::uint64_t CYCLIC_END_US = UINT64_C(967413);
const double PI = 3.141592653589793238462643383279502884;

// Coupon-style prescribed loading: a smooth monotonic tensile ramp, a
// biased tension/compression cyclic block, then a smooth return to zero
// imposed displacement. It exercises yield, reversal, hardening, damage, and
// unloading without relying on a vehicle integrator or contact response.
struct LoadingProfile
{
    double ramp_peak_strain = 0.0072;
    double cyclic_mean_strain = 0.0011;
    double cyclic_amplitude = 0.0059;
    double cyclic_turns = 8.373;
    double phase_warp = 0.217;
};

double ClampUnit(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double SmootherStep(double value)
{
    const double x = ClampUnit(value);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

double CyclicStrain(double normalized_time, const LoadingProfile& profile)
{
    const double s = ClampUnit(normalized_time);
    const double transition = SmootherStep(s / 0.113);
    const double envelope = SmootherStep(s / 0.097);
    const double mean =
        profile.ramp_peak_strain +
        (profile.cyclic_mean_strain -
            profile.ramp_peak_strain) *
            transition;
    const double phase =
        2.0 * PI *
        (profile.cyclic_turns * s +
            profile.phase_warp * s * (1.0 - s));
    return mean +
        profile.cyclic_amplitude * envelope * std::sin(phase);
}

double PrescribedStrain(
    std::uint64_t time_us,
    const LoadingProfile& profile)
{
    if (time_us <= RAMP_END_US)
    {
        return profile.ramp_peak_strain *
            SmootherStep(
                static_cast<double>(time_us) /
                static_cast<double>(RAMP_END_US));
    }

    if (time_us <= CYCLIC_END_US)
    {
        return CyclicStrain(
            static_cast<double>(time_us - RAMP_END_US) /
                static_cast<double>(
                    CYCLIC_END_US - RAMP_END_US),
            profile);
    }

    // End at zero imposed displacement. This is not assumed to be a
    // stress-free configuration: the remaining material plastic strain is
    // the permanent-strain calibration signal.
    const double cyclic_end = CyclicStrain(1.0, profile);
    const double unload_fraction =
        static_cast<double>(time_us - CYCLIC_END_US) /
        static_cast<double>(
            TOTAL_DURATION_US - CYCLIC_END_US);
    return cyclic_end *
        (1.0 - SmootherStep(unload_fraction));
}

RoR::CalibratedBeamMaterialAdapter::Configuration
MakeConfiguration()
{
    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    configuration.cross_section_area_m2 = 0.0018;
    configuration.material.elastic_modulus = 200.0e9;
    configuration.material.yield_stress = 250.0e6;
    configuration.material.hardening_modulus = 2.0e9;
    configuration.material.damage_onset_plastic_strain = 0.004;
    configuration.material.damage_driver_capacity_density = 100.0e6;
    return configuration;
}

struct HistoryMetrics
{
    bool valid = false;
    double peak_force_n = 0.0;
    double permanent_plastic_strain = 0.0;
    double accumulated_plastic_strain = 0.0;
    double hysteresis_dissipation_j = 0.0;
    double mechanical_work_j = 0.0;
    double final_stored_energy_j = 0.0;
    double final_damage = 0.0;
    double max_energy_balance_residual_j = 0.0;
    double max_energy_shortfall_ratio = 0.0;
    double max_normalized_momentum_residual = 0.0;
    double maximum_sampled_strain =
        -std::numeric_limits<double>::max();
    double minimum_sampled_strain =
        std::numeric_limits<double>::max();
    std::uint64_t maximum_strain_time_us = 0U;
    std::uint64_t minimum_strain_time_us = 0U;
    std::uint64_t steps = 0U;
};

HistoryMetrics RunHistory(
    std::uint64_t step_us,
    const LoadingProfile& profile)
{
    using namespace RoR::CalibratedBeamMaterial;
    using namespace RoR::CalibratedBeamMaterialAdapter;

    HistoryMetrics metrics;
    if (step_us == 0U ||
        TOTAL_DURATION_US % step_us != 0U)
    {
        return metrics;
    }

    const Configuration configuration = MakeConfiguration();
    Runtime runtime;
    if (!TryConfigure(runtime, configuration))
        return metrics;

    const double reference_length_m = 2.35;
    const double reference_volume_m3 =
        configuration.cross_section_area_m2 * reference_length_m;
    double previous_damage = 0.0;
    double previous_stress_pa = 0.0;
    double previous_strain = 0.0;

    metrics.steps = TOTAL_DURATION_US / step_us;
    for (std::uint64_t step = 1U;
         step <= metrics.steps;
         ++step)
    {
        const std::uint64_t time_us = step * step_us;
        const double requested_strain =
            PrescribedStrain(time_us, profile);
        StepInput input;
        input.reference_length_m = reference_length_m;
        input.current_length_m =
            reference_length_m * (1.0 + requested_strain);
        input.direction = {{2.0, -3.0, 6.0}};
        input.is_plain_axial_beam = true;

        const double adapter_strain =
            (input.current_length_m - input.reference_length_m) /
            input.reference_length_m;
        const StepResult result = Step(runtime, input);
        if (!result.IsValid() ||
            result.fractured ||
            !Finite(requested_strain) ||
            !Finite(adapter_strain) ||
            !Finite(result.nominal_stress_pa) ||
            !Finite(result.material_force_n) ||
            !Finite(result.stored_energy_j) ||
            !Finite(result.dissipated_energy_increment_j) ||
            !Finite(runtime.state.plastic_strain) ||
            !Finite(runtime.state.accumulated_plastic_strain) ||
            !Finite(runtime.state.damage) ||
            !Finite(runtime.state.damage_driver_density) ||
            !Finite(runtime.state.last_total_strain))
        {
            return metrics;
        }

        CHECK_NEAR(
            result.material_force_n,
            -result.nominal_stress_pa *
                configuration.cross_section_area_m2,
            2.0e-12,
            1.0e-8);

        // This is deliberately an independent endpoint-force quadrature, not
        // the kernel's exact within-step work. It lets the multi-rate gate
        // detect gross energy creation while acknowledging ordinary
        // quadrature error at yield and reversal events.
        const double quadrature_work_increment_j =
            0.5 *
            (previous_stress_pa + result.nominal_stress_pa) *
            (adapter_strain - previous_strain) *
            reference_volume_m3;
        CHECK(result.stored_energy_j >= 0.0);
        CHECK(result.dissipated_energy_increment_j >= 0.0);
        CHECK(runtime.state.damage + 1.0e-14 >= previous_damage);

        double momentum_residual_squared = 0.0;
        double momentum_scale = 0.0;
        for (std::size_t lane = 0U; lane < 3U; ++lane)
        {
            const double residual =
                result.forces.endpoint_1[lane] +
                result.forces.endpoint_2[lane];
            momentum_residual_squared += residual * residual;
            momentum_scale +=
                std::abs(result.forces.endpoint_1[lane]) +
                std::abs(result.forces.endpoint_2[lane]);
        }
        const double normalized_momentum_residual =
            std::sqrt(momentum_residual_squared) /
            std::max(momentum_scale, 1.0e-30);
        CHECK(normalized_momentum_residual == 0.0);

        metrics.peak_force_n =
            std::max(
                metrics.peak_force_n,
                std::abs(result.material_force_n));
        metrics.hysteresis_dissipation_j +=
            result.dissipated_energy_increment_j;
        metrics.mechanical_work_j +=
            quadrature_work_increment_j;
        metrics.final_stored_energy_j =
            result.stored_energy_j;
        const double energy_balance_residual_j =
            metrics.mechanical_work_j -
            metrics.final_stored_energy_j -
            metrics.hysteresis_dissipation_j;
        const double energy_balance_scale_j =
            std::max(
                1.0,
                std::abs(metrics.mechanical_work_j) +
                    metrics.final_stored_energy_j +
                    metrics.hysteresis_dissipation_j);
        metrics.max_energy_balance_residual_j =
            std::max(
                metrics.max_energy_balance_residual_j,
                std::abs(energy_balance_residual_j));
        metrics.max_energy_shortfall_ratio =
            std::max(
                metrics.max_energy_shortfall_ratio,
                std::max(0.0, -energy_balance_residual_j) /
                    energy_balance_scale_j);
        metrics.max_normalized_momentum_residual =
            std::max(
                metrics.max_normalized_momentum_residual,
                normalized_momentum_residual);
        if (adapter_strain > metrics.maximum_sampled_strain)
        {
            metrics.maximum_sampled_strain = adapter_strain;
            metrics.maximum_strain_time_us = time_us;
        }
        if (adapter_strain < metrics.minimum_sampled_strain)
        {
            metrics.minimum_sampled_strain = adapter_strain;
            metrics.minimum_strain_time_us = time_us;
        }

        previous_damage = runtime.state.damage;
        previous_stress_pa = result.nominal_stress_pa;
        previous_strain = adapter_strain;
    }

    metrics.permanent_plastic_strain =
        runtime.state.plastic_strain;
    metrics.accumulated_plastic_strain =
        runtime.state.accumulated_plastic_strain;
    metrics.final_damage = runtime.state.damage;

    const double final_balance =
        metrics.mechanical_work_j -
        metrics.final_stored_energy_j -
        metrics.hysteresis_dissipation_j;
    const double final_scale =
        std::max(
            1.0,
            std::abs(metrics.mechanical_work_j) +
            metrics.final_stored_energy_j +
                metrics.hysteresis_dissipation_j);
    if (!Finite(metrics.peak_force_n) ||
        !Finite(metrics.permanent_plastic_strain) ||
        !Finite(metrics.accumulated_plastic_strain) ||
        !Finite(metrics.hysteresis_dissipation_j) ||
        !Finite(metrics.mechanical_work_j) ||
        !Finite(metrics.final_stored_energy_j) ||
        !Finite(metrics.final_damage) ||
        !Finite(metrics.max_energy_balance_residual_j) ||
        !Finite(metrics.max_energy_shortfall_ratio) ||
        !Finite(metrics.max_normalized_momentum_residual) ||
        !Finite(final_balance) ||
        !Finite(final_scale))
    {
        return metrics;
    }
    CHECK(std::abs(final_balance) <= 0.01 * final_scale);
    CHECK(
        metrics.mechanical_work_j +
            0.01 * final_scale >=
        metrics.final_stored_energy_j +
            metrics.hysteresis_dissipation_j);
    CHECK(metrics.hysteresis_dissipation_j > 0.0);
    CHECK(metrics.accumulated_plastic_strain >
        std::abs(metrics.permanent_plastic_strain));
    CHECK(metrics.final_damage > 0.0);
    CHECK(metrics.final_damage < 1.0);
    CHECK(metrics.max_energy_shortfall_ratio <= 0.01);
    CHECK(metrics.max_normalized_momentum_residual == 0.0);

    metrics.valid = true;
    return metrics;
}

double RelativeSpread(
    double first,
    double second,
    double third)
{
    const double minimum = std::min(first, std::min(second, third));
    const double maximum = std::max(first, std::max(second, third));
    return (maximum - minimum) /
        std::max(std::abs(maximum), 1.0e-30);
}

double PairRelativeSpread(double first, double second)
{
    return std::abs(first - second) /
        std::max(
            std::max(std::abs(first), std::abs(second)),
            1.0e-30);
}

void TestQuarterHalfAndOneMillisecondGate()
{
    const LoadingProfile profile;
    const HistoryMetrics quarter = RunHistory(250U, profile);
    const HistoryMetrics half = RunHistory(500U, profile);
    const HistoryMetrics one = RunHistory(1000U, profile);
    CHECK(quarter.valid);
    CHECK(half.valid);
    CHECK(one.valid);
    if (!quarter.valid || !half.valid || !one.valid)
        return;

    // The breakpoints are deliberately not multiples of 0.25, 0.5, or
    // 1.0 ms, and the warped sinusoid has non-integral turns. The three
    // histories therefore sample different extrema. This prevents an
    // apparently perfect comparison caused by all rates sharing authored
    // control points. The gate measures sensitivity to time-discretizing a
    // smooth prescribed displacement; it does not claim to validate vehicle
    // mass integration, contacts, or a particular real-world steel coupon.
    CHECK(
        quarter.maximum_strain_time_us !=
        one.maximum_strain_time_us);
    CHECK(
        quarter.minimum_strain_time_us !=
        one.minimum_strain_time_us);
    CHECK(
        quarter.maximum_sampled_strain !=
        one.maximum_sampled_strain);
    CHECK(
        quarter.minimum_sampled_strain !=
        one.minimum_sampled_strain);

    const double peak_force_spread =
        RelativeSpread(
            quarter.peak_force_n,
            half.peak_force_n,
            one.peak_force_n);
    const double permanent_strain_spread =
        RelativeSpread(
            std::abs(quarter.permanent_plastic_strain),
            std::abs(half.permanent_plastic_strain),
            std::abs(one.permanent_plastic_strain));
    const double hysteresis_spread =
        RelativeSpread(
            quarter.hysteresis_dissipation_j,
            half.hysteresis_dissipation_j,
            one.hysteresis_dissipation_j);

    // Calibration signals: reaction-force peak constrains the area-scaled
    // strength response, residual plastic strain constrains the permanent
    // set, and irreversible dissipation constrains the hysteresis loop area.
    // Damage is monitored as a state invariant rather than treated as a
    // measured material calibration until real coupon data is supplied.
    CHECK(peak_force_spread <= 0.02);
    CHECK(permanent_strain_spread <= 0.02);
    // Dissipation is an additional calibration diagnostic. It is kept under
    // the same bound even though the roadmap's release gate names peak force
    // and permanent strain explicitly.
    CHECK(hysteresis_spread <= 0.02);
    CHECK(std::abs(quarter.permanent_plastic_strain) > 1.0e-5);

    std::cout << std::setprecision(10)
              << "dt_ms peak_force_N permanent_strain "
                 "dissipation_J damage energy_balance_J "
                 "max_energy_shortfall max_momentum_residual\n"
              << "0.25 " << quarter.peak_force_n << ' '
              << quarter.permanent_plastic_strain << ' '
              << quarter.hysteresis_dissipation_j << ' '
              << quarter.final_damage << ' '
              << quarter.mechanical_work_j -
                    quarter.final_stored_energy_j -
                    quarter.hysteresis_dissipation_j << ' '
              << quarter.max_energy_shortfall_ratio << ' '
              << quarter.max_normalized_momentum_residual << '\n'
              << "0.5 " << half.peak_force_n << ' '
              << half.permanent_plastic_strain << ' '
              << half.hysteresis_dissipation_j << ' '
              << half.final_damage << ' '
              << half.mechanical_work_j -
                    half.final_stored_energy_j -
                    half.hysteresis_dissipation_j << ' '
              << half.max_energy_shortfall_ratio << ' '
              << half.max_normalized_momentum_residual << '\n'
              << "1.0 " << one.peak_force_n << ' '
              << one.permanent_plastic_strain << ' '
              << one.hysteresis_dissipation_j << ' '
              << one.final_damage << ' '
              << one.mechanical_work_j -
                    one.final_stored_energy_j -
                    one.hysteresis_dissipation_j << ' '
              << one.max_energy_shortfall_ratio << ' '
              << one.max_normalized_momentum_residual << '\n'
              << "relative_spread " << peak_force_spread << ' '
              << permanent_strain_spread << ' '
              << hysteresis_spread << '\n';
}

void TestProfilePropertySweep()
{
    // Exercise smooth histories around the calibrated gate rather than
    // blessing a single waveform. Bounds keep every fixture in a meaningful
    // yielding, cyclic, pre-fracture regime.
    std::uint64_t random = UINT64_C(0x2ab934d1719cf05e);
    for (std::uint32_t fixture = 0U; fixture < 24U; ++fixture)
    {
        LoadingProfile profile;
        profile.ramp_peak_strain =
            0.0067 + 0.0012 * NextUnit(random);
        profile.cyclic_mean_strain =
            0.0008 + 0.0007 * NextUnit(random);
        profile.cyclic_amplitude =
            0.0052 + 0.0010 * NextUnit(random);
        profile.cyclic_turns =
            6.25 + 4.5 * NextUnit(random);
        profile.phase_warp =
            -0.35 + 0.7 * NextUnit(random);

        const HistoryMetrics fine = RunHistory(250U, profile);
        const HistoryMetrics coarse = RunHistory(1000U, profile);
        CHECK(fine.valid);
        CHECK(coarse.valid);
        if (!fine.valid || !coarse.valid)
            continue;

        CHECK(
            PairRelativeSpread(
                fine.peak_force_n,
                coarse.peak_force_n) <= 0.02);
        CHECK(
            PairRelativeSpread(
                std::abs(fine.permanent_plastic_strain),
                std::abs(coarse.permanent_plastic_strain)) <=
            0.02);
        CHECK(
            PairRelativeSpread(
                fine.hysteresis_dissipation_j,
                coarse.hysteresis_dissipation_j) <=
            0.02);
        CHECK(fine.maximum_sampled_strain >=
            coarse.maximum_sampled_strain);
        CHECK(fine.minimum_sampled_strain <=
            coarse.minimum_sampled_strain);
    }
}

void TestHostileInputsFailClosed()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    CHECK(!RunHistory(0U, LoadingProfile()).valid);
    CHECK(!RunHistory(333U, LoadingProfile()).valid);

    Runtime runtime;
    CHECK(TryConfigure(runtime, MakeConfiguration()));
    StepInput input;
    input.reference_length_m = 2.35;
    input.current_length_m =
        DoubleFromBits(UINT64_C(0x7ff8000000000042));
    input.direction = {{1.0, 0.0, 0.0}};
    input.is_plain_axial_beam = true;
    const StepResult hostile = Step(runtime, input);
    CHECK(hostile.error == Error::NONFINITE_INPUT);
    CHECK(runtime.faulted);
    CHECK(hostile.material_force_n == 0.0);
    CHECK(hostile.forces.endpoint_1[0] == 0.0);
    CHECK(hostile.forces.endpoint_2[0] == 0.0);

    input.current_length_m = 2.35;
    const StepResult latched = Step(runtime, input);
    CHECK(latched.error == Error::FAULT_LATCHED);
    CHECK(latched.axial_force_n == 0.0);
}

} // namespace

int main()
{
    TestQuarterHalfAndOneMillisecondGate();
    TestProfilePropertySweep();
    TestHostileInputsFailClosed();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
