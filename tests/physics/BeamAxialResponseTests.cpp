#include "BeamAxialResponse.h"

#include <cstddef>
#include <cmath>
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
        std::cerr << "line " << line << ": check failed: " << expression << '\n';
        ++g_failures;
    }
}

void CheckNear(float actual, float expected, float tolerance, int line)
{
    if (!RoR::BeamAxialResponse::IsFinite(actual) ||
        !RoR::BeamAxialResponse::IsFinite(expected) ||
        !RoR::BeamAxialResponse::IsFinite(tolerance) ||
        std::fabs(actual - expected) > tolerance)
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

float NextUnitFloat(std::uint32_t& state)
{
    state = state * UINT32_C(1664525) + UINT32_C(1013904223);
    return static_cast<float>(state >> 8) / static_cast<float>(UINT32_C(0x01000000));
}

float SampleLogRange(std::uint32_t& state, float exponent_min, float exponent_max)
{
    const float exponent =
        exponent_min + (exponent_max - exponent_min) * NextUnitFloat(state);
    return std::pow(10.0f, exponent);
}

float FloatFromBits(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TestFiniteAndLengthGuards()
{
    using namespace RoR::BeamAxialResponse;

    const float positive_infinity = FloatFromBits(UINT32_C(0x7f800000));
    const float negative_infinity = FloatFromBits(UINT32_C(0xff800000));
    const float quiet_nan = FloatFromBits(UINT32_C(0x7fc00001));
    const double double_infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    const double double_nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));

    CHECK(IsFinite(0.0f));
    CHECK(IsFinite(-123.5f));
    CHECK(!IsFinite(positive_infinity));
    CHECK(!IsFinite(negative_infinity));
    CHECK(!IsFinite(quiet_nan));

    CHECK(IsFinite(0.0));
    CHECK(!IsFinite(double_infinity));
    CHECK(!IsFinite(double_nan));

    CHECK(!HasUsableLength(0.0f));
    CHECK(!HasUsableLength(std::numeric_limits<float>::denorm_min()));
    CHECK(!HasUsableLength(-1.0f));
    CHECK(!HasUsableLength(MIN_LENGTH_SQUARED));
    CHECK(HasUsableLength(MIN_LENGTH_SQUARED * 1.01f));
    CHECK(!HasUsableLength(positive_infinity));
    CHECK(!HasUsableLength(negative_infinity));
    CHECK(!HasUsableLength(quiet_nan));
}

void TestLegacyResponseBelowBound()
{
    using namespace RoR::BeamAxialResponse;

    // A representative pair of 50 kg nodes with default damping at the
    // 0.5 ms physics timestep has a 50,000 Ns/m limit.
    const DampingResult result =
        ComputeDamping(5.0f, 12000.0f, 0.0005f, 50.0f, 50.0f, true, true);

    CHECK(!result.was_limited);
    CHECK_NEAR(result.effective_coefficient, 12000.0f, 0.0f);
    CHECK_NEAR(result.force, -60000.0f, 0.0f);

    const DampingResult light_nodes =
        ComputeDamping(5.0f, 12000.0f, 0.0005f, 10.0f, 10.0f, true, true);
    CHECK(light_nodes.was_limited);
    CHECK_NEAR(light_nodes.effective_coefficient, 10000.0f, 0.01f);
}

void TestEffectiveMassNumerics()
{
    using namespace RoR::BeamAxialResponse;

    const float maximum = std::numeric_limits<float>::max();
    const float minimum_normal = std::numeric_limits<float>::min();
    const float equal_maximum = EffectiveMass(maximum, maximum, true, true);
    const float disparate_ab = EffectiveMass(maximum, minimum_normal, true, true);
    const float disparate_ba = EffectiveMass(minimum_normal, maximum, true, true);

    CHECK(IsFinite(equal_maximum));
    CHECK_NEAR(equal_maximum, maximum * 0.5f, maximum * 1.0e-6f);
    CHECK(disparate_ab == disparate_ba);
    CHECK(disparate_ab == minimum_normal);
}

void TestExtremeDampingStopsWithoutReversing()
{
    using namespace RoR::BeamAxialResponse;

    const float velocity = -20.0f;
    const float inverse_effective_mass = 1.0f / 50.0f + 1.0f / 50.0f;
    const float time_step = 0.0005f;
    const DampingResult result =
        ComputeDamping(velocity, 1.0e9f, time_step, 50.0f, 50.0f, true, true);
    const float next_velocity =
        velocity + inverse_effective_mass * time_step * result.force;

    CHECK(result.was_limited);
    CHECK_NEAR(result.effective_coefficient, 50000.0f, 0.01f);
    CHECK_NEAR(next_velocity, 0.0f, 1.0e-5f);
    CHECK(result.force * velocity <= 0.0f);
}

void TestEndpointMassCases()
{
    using namespace RoR::BeamAxialResponse;

    const DampingResult unequal =
        ComputeDamping(2.0f, 1.0e9f, 0.0005f, 10.0f, 90.0f, true, true);
    CHECK_NEAR(unequal.effective_coefficient, 18000.0f, 0.01f);

    const DampingResult fixed_target =
        ComputeDamping(2.0f, 1.0e9f, 0.0005f, 10.0f, 90.0f, true, false);
    CHECK_NEAR(fixed_target.effective_coefficient, 20000.0f, 0.01f);

    const DampingResult both_fixed =
        ComputeDamping(2.0f, 1000.0f, 0.0005f, 10.0f, 90.0f, false, false);
    CHECK_NEAR(both_fixed.force, 0.0f, 0.0f);

    const DampingResult invalid_mass =
        ComputeDamping(2.0f, 1000.0f, 0.0005f, -1.0f, 0.0f, true, true);
    CHECK_NEAR(invalid_mass.force, 0.0f, 0.0f);

    const DampingResult one_invalid_mass =
        ComputeDamping(2.0f, 1000.0f, 0.0005f, -1.0f, 90.0f, true, true);
    CHECK_NEAR(one_invalid_mass.force, 0.0f, 0.0f);
}

void TestMirroredHalfBeamPair()
{
    using namespace RoR::BeamAxialResponse;

    const float mass_1 = 10.0f;
    const float mass_2 = 90.0f;
    const float time_step = 0.0005f;
    const float velocity_1 = 1.0f;
    const float velocity_2 = -1.0f;
    const float relative_velocity = velocity_1 - velocity_2;
    const DampingResult result =
        ComputeDamping(
            relative_velocity,
            1.0e9f,
            time_step,
            mass_1,
            mass_2,
            true,
            true);

    // The two script half-beams apply equal and opposite forces. Both calls
    // use both masses, so the pair reaches the center-of-mass velocity without
    // reversing relative motion.
    const float next_velocity_1 =
        velocity_1 + result.force / mass_1 * time_step;
    const float next_velocity_2 =
        velocity_2 - result.force / mass_2 * time_step;
    const float center_of_mass_velocity =
        (mass_1 * velocity_1 + mass_2 * velocity_2) / (mass_1 + mass_2);

    CHECK_NEAR(next_velocity_1 - next_velocity_2, 0.0f, 1.0e-6f);
    CHECK_NEAR(next_velocity_1, center_of_mass_velocity, 1.0e-6f);
    CHECK_NEAR(next_velocity_2, center_of_mass_velocity, 1.0e-6f);
}

void TestInvalidInputsStayFinite()
{
    using namespace RoR::BeamAxialResponse;

    const float positive_infinity = FloatFromBits(UINT32_C(0x7f800000));
    const float negative_infinity = FloatFromBits(UINT32_C(0xff800000));
    const float quiet_nan = FloatFromBits(UINT32_C(0x7fc00001));
    const float tiny = std::numeric_limits<float>::denorm_min();

    CHECK_NEAR(ComputeDamping(4.0f, 0.0f, 0.0005f, 50.0f, 50.0f, true, true).force, 0.0f, 0.0f);
    CHECK_NEAR(ComputeDamping(4.0f, -1.0f, 0.0005f, 50.0f, 50.0f, true, true).force, 0.0f, 0.0f);
    CHECK_NEAR(ComputeDamping(4.0f, 1.0f, 0.0f, 50.0f, 50.0f, true, true).force, 0.0f, 0.0f);

    const float invalid_values[] =
    {
        positive_infinity,
        negative_infinity,
        quiet_nan
    };
    for (std::size_t i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]); ++i)
    {
        const float invalid = invalid_values[i];
        const DampingResult invalid_velocity =
            ComputeDamping(invalid, 1.0f, 0.0005f, 50.0f, 50.0f, true, true);
        const DampingResult invalid_damping =
            ComputeDamping(4.0f, invalid, 0.0005f, 50.0f, 50.0f, true, true);
        const DampingResult invalid_time_step =
            ComputeDamping(4.0f, 1.0f, invalid, 50.0f, 50.0f, true, true);
        const DampingResult invalid_mass_1 =
            ComputeDamping(4.0f, 1.0f, 0.0005f, invalid, 50.0f, true, true);
        const DampingResult invalid_mass_2 =
            ComputeDamping(4.0f, 1.0f, 0.0005f, 50.0f, invalid, true, true);

        CHECK_NEAR(invalid_velocity.force, 0.0f, 0.0f);
        CHECK_NEAR(invalid_damping.force, 0.0f, 0.0f);
        CHECK_NEAR(invalid_time_step.force, 0.0f, 0.0f);
        CHECK_NEAR(invalid_mass_1.force, 0.0f, 0.0f);
        CHECK_NEAR(invalid_mass_2.force, 0.0f, 0.0f);
        CHECK(IsFinite(invalid_velocity.effective_coefficient));
        CHECK(IsFinite(invalid_damping.effective_coefficient));
        CHECK(IsFinite(invalid_time_step.effective_coefficient));
        CHECK(IsFinite(invalid_mass_1.effective_coefficient));
        CHECK(IsFinite(invalid_mass_2.effective_coefficient));
    }

    const DampingResult tiny_mass =
        ComputeDamping(4.0f, 1.0f, 0.0005f, tiny, tiny, true, true);
    const DampingResult tiny_time_step =
        ComputeDamping(4.0f, 1.0f, tiny, 50.0f, 50.0f, true, true);
    CHECK(IsFinite(tiny_mass.force));
    CHECK(IsFinite(tiny_mass.effective_coefficient));
    CHECK(IsFinite(tiny_time_step.force));
    CHECK(IsFinite(tiny_time_step.effective_coefficient));
    CHECK(IsFinite(InverseMass(tiny, true)));
    CHECK_NEAR(InverseMass(tiny, false), 0.0f, 0.0f);

    const DampingResult overflow =
        ComputeDamping(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            0.0005f,
            50.0f,
            50.0f,
            true,
            true);
    CHECK(IsFinite(overflow.force));
}

void TestLongRunningDampingSoak()
{
    using namespace RoR::BeamAxialResponse;

    const float mass_1 = 10.0f;
    const float mass_2 = 90.0f;
    const float time_step = 0.0005f;
    const float damping = 12000.0f;
    float velocity_1 = 30.0f;
    float velocity_2 = -5.0f;

    for (int step = 0; step < 120000; ++step)
    {
        // Re-excite the pair at a fixed cadence so the soak exercises the
        // damping path instead of spending nearly all 60 simulated seconds at
        // equilibrium. Equal and opposite impulses preserve total momentum.
        if (step % 997 == 0)
        {
            velocity_1 += 2.0f;
            velocity_2 -= 2.0f * mass_1 / mass_2;
        }

        const float relative_velocity = velocity_1 - velocity_2;
        const double energy_before =
            0.5 * static_cast<double>(mass_1) * velocity_1 * velocity_1 +
            0.5 * static_cast<double>(mass_2) * velocity_2 * velocity_2;
        const double momentum_before =
            static_cast<double>(mass_1) * velocity_1 +
            static_cast<double>(mass_2) * velocity_2;
        const DampingResult result =
            ComputeDamping(
                relative_velocity,
                damping,
                time_step,
                mass_1,
                mass_2,
                true,
                true);

        velocity_1 += result.force / mass_1 * time_step;
        velocity_2 -= result.force / mass_2 * time_step;

        const double energy_after =
            0.5 * static_cast<double>(mass_1) * velocity_1 * velocity_1 +
            0.5 * static_cast<double>(mass_2) * velocity_2 * velocity_2;
        const double momentum_after =
            static_cast<double>(mass_1) * velocity_1 +
            static_cast<double>(mass_2) * velocity_2;
        const double energy_tolerance = 1.0e-5 * (1.0 + energy_before);
        const double momentum_tolerance =
            1.0e-5 * (1.0 + std::fabs(momentum_before));

        CHECK(IsFinite(result.force));
        CHECK(IsFinite(result.effective_coefficient));
        CHECK(IsFinite(velocity_1));
        CHECK(IsFinite(velocity_2));
        CHECK(energy_after <= energy_before + energy_tolerance);
        CHECK(std::fabs(momentum_after - momentum_before) <= momentum_tolerance);
    }
}

void TestRandomizedEnergyBound()
{
    using namespace RoR::BeamAxialResponse;

    std::uint32_t random_state = UINT32_C(0x5eed1234);
    for (int sample = 0; sample < 20000; ++sample)
    {
        const float mass_1 = SampleLogRange(random_state, -1.0f, 4.0f);
        const float mass_2 = SampleLogRange(random_state, -1.0f, 4.0f);
        const float time_step = SampleLogRange(random_state, -6.0f, -2.0f);
        const float damping = SampleLogRange(random_state, -2.0f, 9.0f);
        const float relative_velocity =
            (NextUnitFloat(random_state) * 2.0f - 1.0f) * 1000.0f;
        bool movable_1 = NextUnitFloat(random_state) >= 0.25f;
        bool movable_2 = NextUnitFloat(random_state) >= 0.25f;
        if (!movable_1 && !movable_2)
        {
            movable_1 = true;
        }

        const float inverse_effective_mass =
            InverseMass(mass_1, movable_1) + InverseMass(mass_2, movable_2);
        const DampingResult result =
            ComputeDamping(
                relative_velocity,
                damping,
                time_step,
                mass_1,
                mass_2,
                movable_1,
                movable_2);
        const double next_velocity =
            static_cast<double>(relative_velocity) +
            static_cast<double>(inverse_effective_mass) *
                static_cast<double>(time_step) *
                static_cast<double>(result.force);
        const double effective_mass = 1.0 / inverse_effective_mass;
        const double energy_before =
            0.5 * effective_mass * relative_velocity * relative_velocity;
        const double energy_after =
            0.5 * effective_mass * next_velocity * next_velocity;
        const double velocity_tolerance =
            1.0e-4 * (1.0 + std::fabs(relative_velocity));
        const double energy_tolerance =
            1.0e-5 * (1.0 + energy_before);

        CHECK(IsFinite(result.force));
        CHECK(result.force * relative_velocity <= 0.0f);
        CHECK(energy_after <= energy_before + energy_tolerance);

        if (relative_velocity > 0.0f)
        {
            CHECK(next_velocity >= -velocity_tolerance);
            CHECK(next_velocity <= relative_velocity + velocity_tolerance);
        }
        else if (relative_velocity < 0.0f)
        {
            CHECK(next_velocity <= velocity_tolerance);
            CHECK(next_velocity >= relative_velocity - velocity_tolerance);
        }
    }
}

} // namespace

int main()
{
    TestFiniteAndLengthGuards();
    TestLegacyResponseBelowBound();
    TestEffectiveMassNumerics();
    TestExtremeDampingStopsWithoutReversing();
    TestEndpointMassCases();
    TestMirroredHalfBeamPair();
    TestInvalidInputsStayFinite();
    TestLongRunningDampingSoak();
    TestRandomizedEnergyBound();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " beam axial response test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "beam axial response tests passed\n";
    return EXIT_SUCCESS;
}
