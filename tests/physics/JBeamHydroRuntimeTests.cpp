#include "JBeamHydroRuntime.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

double Binary64FromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

RoR::JBeamHydroRuntimeConfig Config()
{
    RoR::JBeamHydroRuntimeConfig config;
    config.response.has_factor = true;
    config.response.factor = 0.5;
    config.response.in_rate = 1.0;
    config.response.out_rate = 2.0;
    config.response.auto_center_rate = 0.5;
    config.has_steering_wheel_lock = true;
    config.steering_wheel_lock = 500.0;
    return config;
}

void TestInitializationAndProgress()
{
    const RoR::JBeamHydroRuntimeConfig config = Config();
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    CHECK(initialized.valid);
    CHECK(!initialized.state.fault_latched);
    CHECK(initialized.state.accepted_step_count == 0U);
    CHECK(initialized.state.response.length_ratio == 1.0);
    CHECK(initialized.rest_length == 2.0);
    CHECK(initialized.runtime_rest_length == 2.0f);

    const RoR::JBeamHydroRuntimeStep expanded =
        RoR::AdvanceJBeamHydroRuntime(
            config, initialized.state, 2.0, 1.0, 0.25, false);
    CHECK(expanded.valid);
    CHECK(expanded.state.accepted_step_count == 1U);
    CHECK(expanded.state.response.length_ratio == 1.5);
    CHECK(expanded.target_ratio == 1.5);
    CHECK(expanded.rest_length == 3.0);
    CHECK(expanded.runtime_rest_length == 3.0f);

    const RoR::JBeamHydroRuntimeStep centered =
        RoR::AdvanceJBeamHydroRuntime(
            config, expanded.state, 2.0, 1.0, 0.5, true);
    CHECK(centered.valid);
    CHECK(centered.state.accepted_step_count == 2U);
    CHECK(centered.target_ratio == 1.0);
    CHECK(centered.state.response.length_ratio == 1.25);
    CHECK(centered.runtime_rest_length == 2.5f);
}

void TestClampingAndFaultLatching()
{
    const RoR::JBeamHydroRuntimeConfig config = Config();
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 1.0);
    CHECK(initialized.valid);
    const RoR::JBeamHydroRuntimeStep clamped =
        RoR::AdvanceJBeamHydroRuntime(
            config, initialized.state, 1.0, 100.0, 1.0, false);
    CHECK(clamped.valid);
    CHECK(clamped.input_was_clamped);

    const double quiet_nan = Binary64FromBits(
        UINT64_C(0x7ff8000000000001));
    const RoR::JBeamHydroRuntimeStep rejected =
        RoR::AdvanceJBeamHydroRuntime(
            config, clamped.state, 1.0, quiet_nan, 1.0, false);
    CHECK(!rejected.valid);
    CHECK(rejected.state.fault_latched);
    CHECK(rejected.state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_INPUT);
    CHECK(rejected.state.accepted_step_count ==
        clamped.state.accepted_step_count);

    const RoR::JBeamHydroRuntimeStep repeated =
        RoR::AdvanceJBeamHydroRuntime(
            config, rejected.state, 1.0, 0.0, 1.0, false);
    CHECK(!repeated.valid);
    CHECK(repeated.state.fault_latched);
    CHECK(repeated.state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_INPUT);
}

void TestFailClosedInputs()
{
    RoR::JBeamHydroRuntimeConfig invalid_config = Config();
    invalid_config.response.input_in_limit = 1.0;
    CHECK(RoR::InitializeJBeamHydroRuntime(
        invalid_config, 1.0).state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_CONFIG);

    RoR::JBeamHydroRuntimeConfig invalid_lock = Config();
    invalid_lock.steering_wheel_lock = 0.0;
    CHECK(RoR::InitializeJBeamHydroRuntime(
        invalid_lock, 1.0).state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_CONFIG);

    const RoR::JBeamHydroRuntimeConfig config = Config();
    CHECK(RoR::InitializeJBeamHydroRuntime(
        config, 0.0).state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH);
    CHECK(RoR::InitializeJBeamHydroRuntime(
        config, static_cast<double>(std::numeric_limits<float>::max()) *
            2.0).state.fault ==
        RoR::JBeamHydroRuntimeFault::FLOAT_NARROWING);

    RoR::JBeamHydroRuntimeState invalid_state;
    invalid_state.response.length_ratio = 0.0;
    CHECK(RoR::AdvanceJBeamHydroRuntime(
        config, invalid_state, 1.0, 0.0, 1.0, false).state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE);

    RoR::JBeamHydroRuntimeState exhausted;
    exhausted.accepted_step_count =
        std::numeric_limits<std::uint64_t>::max();
    CHECK(RoR::AdvanceJBeamHydroRuntime(
        config, exhausted, 1.0, 0.0, 1.0, false).state.fault ==
        RoR::JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED);

    CHECK(std::string(RoR::JBeamHydroRuntimeFaultToString(
        RoR::JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED)) ==
        "step-counter-exhausted");
}

} // namespace

int main()
{
    TestInitializationAndProgress();
    TestClampingAndFaultLatching();
    TestFailClosedInputs();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "JBeam hydro runtime tests passed\n";
    return 0;
}
