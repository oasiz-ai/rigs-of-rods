/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "HydroActuatorResponse.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void RequireNear(double actual, double expected, double tolerance,
    const char* message)
{
    Require(RoR::HydroActuatorDetail::IsFinite(actual), message);
    Require(RoR::HydroActuatorDetail::IsFinite(expected), message);
    Require(RoR::HydroActuatorDetail::IsFinite(tolerance), message);
    const double difference = actual >= expected
        ? actual - expected
        : expected - actual;
    Require(
        RoR::HydroActuatorDetail::IsFinite(difference) &&
        difference <= tolerance,
        message);
}

std::uint64_t NextRandom(std::uint64_t* state)
{
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * UINT64_C(2685821657736338717);
}

double UnitRandom(std::uint64_t* state)
{
    return static_cast<double>(NextRandom(state) >> 11) *
        (1.0 / 9007199254740992.0);
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TestDocumentedFactorExamples()
{
    RoR::HydroActuatorConfig config;
    config.has_factor = true;
    config.factor = 1.0;

    RoR::HydroActuatorStep step =
        RoR::ResolveHydroActuatorTarget(config, 1.0);
    Require(step.valid, "factor=1, input=1 must be valid");
    RequireNear(step.target_ratio, 2.0, 1e-15,
        "factor=1, input=1 must double length");

    step = RoR::ResolveHydroActuatorTarget(config, -0.5);
    Require(step.valid, "factor=1, input=-0.5 must be valid");
    RequireNear(step.target_ratio, 0.5, 1e-15,
        "factor=1, input=-0.5 must halve length");

    config.factor = 0.5;
    step = RoR::ResolveHydroActuatorTarget(config, -1.0);
    RequireNear(step.target_ratio, 0.5, 1e-15,
        "factor=.5 must reach half length at negative lock");
    step = RoR::ResolveHydroActuatorTarget(config, 1.0);
    RequireNear(step.target_ratio, 1.5, 1e-15,
        "factor=.5 must reach 1.5 length at positive lock");

    config.factor = -0.25;
    step = RoR::ResolveHydroActuatorTarget(config, 1.0);
    RequireNear(step.target_ratio, 0.75, 1e-15,
        "negative factor must reverse direction");
}

void TestLimitsCenterAndInputScaling()
{
    RoR::HydroActuatorConfig config;
    config.in_limit = 0.4;
    config.out_limit = 1.8;
    config.input_center = 0.25;
    config.input_in_limit = -0.5;
    config.input_out_limit = 1.5;

    RoR::HydroActuatorStep step =
        RoR::ResolveHydroActuatorTarget(config, -10.0);
    Require(step.valid && step.input_was_clamped,
        "input below configured limit must clamp");
    RequireNear(step.target_ratio, 0.4, 1e-15,
        "negative input lock must select inLimit");

    step = RoR::ResolveHydroActuatorTarget(config, config.input_center);
    RequireNear(step.target_ratio, 1.0, 1e-15,
        "inputCenter must map to initial length");

    step = RoR::ResolveHydroActuatorTarget(config, 10.0);
    Require(step.input_was_clamped,
        "input above configured limit must clamp");
    RequireNear(step.target_ratio, 1.8, 1e-15,
        "positive input lock must select outLimit");

    config.input_factor = 0.5;
    step = RoR::ResolveHydroActuatorTarget(config, config.input_out_limit);
    RequireNear(step.target_ratio, 1.4, 1e-15,
        "inputFactor must scale travel about the center");

    config.has_factor = true;
    config.factor = 0.2;
    config.input_factor = 100.0;
    config.in_limit = 0.99;
    config.out_limit = 1.01;
    step = RoR::ResolveHydroActuatorTarget(config, config.input_out_limit);
    RequireNear(step.target_ratio, 1.2, 1e-15,
        "factor must override limit and inputFactor behavior");

    config.in_limit = -100.0;
    config.out_limit = -1.0;
    config.input_factor = DoubleFromBits(
        UINT64_C(0x7ff8000000000001));
    step = RoR::ResolveHydroActuatorTarget(
        config, config.input_out_limit);
    Require(step.valid,
        "factor mode must ignore overwritten travel and input scaling");
    RequireNear(step.target_ratio, 1.2, 1e-15,
        "dormant non-factor settings must not change factor output");
}

void TestRateLimitsAndCentering()
{
    RoR::HydroActuatorConfig config;
    config.has_factor = true;
    config.factor = 0.5;
    config.in_rate = 0.2;
    config.out_rate = 0.4;
    config.auto_center_rate = 0.1;

    RoR::HydroActuatorState state;
    RoR::HydroActuatorStep step =
        RoR::AdvanceHydroActuator(config, state, 1.0, 0.5, false);
    Require(step.valid, "expansion step must be valid");
    RequireNear(step.target_ratio, 1.5, 1e-15,
        "expansion target must be preserved");
    RequireNear(step.state.length_ratio, 1.2, 1e-15,
        "outRate must cap expansion without overshoot");

    state = step.state;
    step = RoR::AdvanceHydroActuator(config, state, -1.0, 0.5, false);
    RequireNear(step.state.length_ratio, 1.1, 1e-15,
        "inRate must cap contraction without overshoot");

    state.length_ratio = 1.4;
    step = RoR::AdvanceHydroActuator(config, state, -1.0, 1.0, true);
    RequireNear(step.target_ratio, 1.0, 1e-15,
        "auto-center must target initial length");
    RequireNear(step.state.length_ratio, 1.3, 1e-15,
        "autoCenterRate must be independent from input direction");

    state.length_ratio = 1.01;
    step = RoR::AdvanceHydroActuator(config, state, -1.0, 1.0, true);
    RequireNear(step.state.length_ratio, 1.0, 1e-15,
        "rate limiting must never overshoot center");

    state.length_ratio = 0.7;
    step = RoR::AdvanceHydroActuator(config, state, 1.0, 0.5, true);
    RequireNear(step.state.length_ratio, 0.75, 1e-15,
        "auto-center must use its own rate when approaching from below");

    config.factor = -0.5;
    config.in_rate = 0.2;
    config.out_rate = 0.9;
    state.length_ratio = 1.0;
    step = RoR::AdvanceHydroActuator(config, state, 1.0, 0.5, false);
    RequireNear(step.target_ratio, 0.5, 1e-15,
        "negative factor must reverse requested travel");
    RequireNear(step.state.length_ratio, 0.9, 1e-15,
        "rate selection must follow actual contraction direction");
}

void TestMalformedInputsFailClosed()
{
    const double nan = DoubleFromBits(UINT64_C(0x7ff8000000000001));
    const double infinity = DoubleFromBits(UINT64_C(0x7ff0000000000000));
    RoR::HydroActuatorConfig config;

    Require(!RoR::ResolveHydroActuatorTarget(config, nan).valid,
        "NaN input must fail closed");
    Require(!RoR::ResolveHydroActuatorTarget(config, infinity).valid,
        "infinite input must fail closed");

    config.input_center = config.input_in_limit;
    Require(!RoR::ResolveHydroActuatorTarget(config, 0.0).valid,
        "collapsed negative input interval must be rejected");

    config = RoR::HydroActuatorConfig();
    config.has_factor = true;
    config.factor = 1.0;
    Require(!RoR::ResolveHydroActuatorTarget(config, -1.0).valid,
        "a requested zero length must be rejected");
    Require(RoR::ResolveHydroActuatorTarget(config, 0.0).valid,
        "factor=1 must remain usable away from its zero-length endpoint");

    config = RoR::HydroActuatorConfig();
    config.in_limit = 1.2;
    config.out_limit = 1.8;
    Require(!RoR::ResolveHydroActuatorTarget(config, 0.0).valid,
        "non-factor limits must contain the initial ratio");
    config.in_limit = 0.4;
    config.out_limit = 0.8;
    Require(!RoR::ResolveHydroActuatorTarget(config, 0.0).valid,
        "non-factor limits below the initial ratio must be rejected");

    config = RoR::HydroActuatorConfig();
    config.in_limit = 0.0;
    Require(!RoR::ResolveHydroActuatorTarget(config, -1.0).valid,
        "an official zero inLimit is valid but exact zero target is unsafe");
    RequireNear(
        RoR::ResolveHydroActuatorTarget(config, -0.5).target_ratio,
        0.5,
        1e-15,
        "zero inLimit must remain usable at positive target lengths");

    config = RoR::HydroActuatorConfig();
    RoR::HydroActuatorState state;
    state.length_ratio = nan;
    Require(!RoR::AdvanceHydroActuator(
        config, state, 0.0, 0.001, false).valid,
        "invalid prior state must fail closed");

    state.length_ratio = 1.0;
    Require(!RoR::AdvanceHydroActuator(
        config, state, 0.0, 0.0, false).valid,
        "nonpositive dt must fail closed");
    Require(!RoR::AdvanceHydroActuator(
        config, state, 0.0, infinity, false).valid,
        "non-finite dt must fail closed");

    config.out_rate = DoubleFromBits(UINT64_C(0x7fefffffffffffff));
    RoR::HydroActuatorStep saturated =
        RoR::AdvanceHydroActuator(config, state, 1.0, 2.0, false);
    Require(saturated.valid,
        "overflowing rate times dt must saturate to a finite target");
    RequireNear(saturated.state.length_ratio, 2.0, 1e-15,
        "an ample finite rate must snap to target without overflow");

    const double smallest_normal =
        DoubleFromBits(UINT64_C(0x0010000000000000));
    config.in_limit = smallest_normal;
    config.in_rate = 0.0;
    state.length_ratio = smallest_normal * 2.0;
    saturated = RoR::AdvanceHydroActuator(
        config,
        state,
        -1.0,
        DoubleFromBits(UINT64_C(0x7fefffffffffffff)),
        false);
    Require(saturated.valid,
        "zero-rate tiny-distance step must remain a valid frozen state");
    RequireNear(
        saturated.state.length_ratio,
        state.length_ratio,
        0.0,
        "division underflow must not bypass a zero contraction rate");

    double rest_length = 17.0;
    Require(!RoR::ResolveHydroRestLength(0.0, state, &rest_length),
        "zero initial length must be rejected");
    RequireNear(rest_length, 17.0, 0.0,
        "failed rest length conversion must not touch output");
    Require(!RoR::ResolveHydroRestLength(1.0, state, nullptr),
        "null output must be rejected");

    state.length_ratio =
        DoubleFromBits(UINT64_C(0x7fefffffffffffff));
    rest_length = 23.0;
    Require(!RoR::ResolveHydroRestLength(2.0, state, &rest_length),
        "rest-length multiplication overflow must fail closed");
    RequireNear(rest_length, 23.0, 0.0,
        "overflowing rest length must not touch output");

    state.length_ratio =
        DoubleFromBits(UINT64_C(0x0010000000000000));
    rest_length = 29.0;
    Require(!RoR::ResolveHydroRestLength(
        DoubleFromBits(UINT64_C(0x0010000000000000)),
        state,
        &rest_length),
        "rest-length underflow must fail closed");
    RequireNear(rest_length, 29.0, 0.0,
        "underflowing rest length must not touch output");
}

void TestExtremeFiniteInputSpan()
{
    const double maximum =
        DoubleFromBits(UINT64_C(0x7fefffffffffffff));
    double fraction = -1.0;
    Require(
        RoR::HydroActuatorDetail::UnitIntervalFraction(
            -maximum, -maximum, maximum, &fraction),
        "widest finite span start must remain normalizable");
    RequireNear(fraction, 0.0, 0.0,
        "widest finite span start must map exactly to zero");
    Require(
        RoR::HydroActuatorDetail::UnitIntervalFraction(
            0.0, -maximum, maximum, &fraction),
        "widest finite span midpoint must remain normalizable");
    RequireNear(fraction, 0.5, 0.0,
        "widest finite span midpoint must map exactly to one half");
    Require(
        RoR::HydroActuatorDetail::UnitIntervalFraction(
            maximum, -maximum, maximum, &fraction),
        "widest finite span end must remain normalizable");
    RequireNear(fraction, 1.0, 0.0,
        "widest finite span end must map exactly to one");

    RoR::HydroActuatorConfig config;
    config.input_in_limit = -maximum;
    config.input_center = maximum * 0.5;
    config.input_out_limit = maximum;

    RoR::HydroActuatorStep step =
        RoR::ResolveHydroActuatorTarget(config, -maximum);
    Require(step.valid,
        "extreme finite input span must normalize without Inf over Inf");
    RequireNear(step.target_ratio, config.in_limit, 1e-15,
        "extreme negative lock must resolve to inLimit");

    config.input_factor = maximum;
    step = RoR::ResolveHydroActuatorTarget(config, -maximum * 0.5);
    Require(step.valid,
        "finite input scaling overflow must saturate safely");
    RequireNear(step.target_ratio, config.in_limit, 1e-15,
        "overflowing input scaling must retain its command sign");
}

void TestFixedSeedSafetyProperties()
{
    std::uint64_t random_state = UINT64_C(0x9f3c7d62a10be845);
    for (int sample = 0; sample < 50000; ++sample)
    {
        RoR::HydroActuatorConfig config;
        config.has_factor = (NextRandom(&random_state) & 1U) != 0U;
        config.factor = -0.95 + 1.9 * UnitRandom(&random_state);
        config.in_limit = UnitRandom(&random_state);
        config.out_limit = 1.0 + 2.0 * UnitRandom(&random_state);
        config.input_factor = -3.0 + 6.0 * UnitRandom(&random_state);
        config.input_center = -0.5 + UnitRandom(&random_state);
        config.input_in_limit = config.input_center -
            (0.01 + 2.0 * UnitRandom(&random_state));
        config.input_out_limit = config.input_center +
            (0.01 + 2.0 * UnitRandom(&random_state));
        config.in_rate = 10.0 * UnitRandom(&random_state);
        config.out_rate = 10.0 * UnitRandom(&random_state);
        config.auto_center_rate = 10.0 * UnitRandom(&random_state);

        RoR::HydroActuatorState state;
        state.length_ratio = 0.05 + 2.95 * UnitRandom(&random_state);
        const double input = -4.0 + 8.0 * UnitRandom(&random_state);
        const double dt = 0.00001 + 0.02 * UnitRandom(&random_state);
        const bool auto_center =
            (NextRandom(&random_state) & 1U) != 0U;

        const RoR::HydroActuatorStep target =
            RoR::ResolveHydroActuatorTarget(
                config, auto_center ? config.input_center : input);
        const RoR::HydroActuatorStep step =
            RoR::AdvanceHydroActuator(
                config, state, input, dt, auto_center);
        Require(target.valid && step.valid,
            "valid randomized actuator must remain valid");
        Require(RoR::HydroActuatorDetail::IsFinite(
                    step.state.length_ratio) &&
                step.state.length_ratio > 0.0,
            "randomized actuator state must remain finite and positive");

        const double before_error =
            std::fabs(state.length_ratio - target.target_ratio);
        const double after_error =
            std::fabs(step.state.length_ratio - target.target_ratio);
        Require(after_error <= before_error + 1e-14,
            "actuator step must not move away from its target");

        const double rate = auto_center
            ? config.auto_center_rate
            : (target.target_ratio < state.length_ratio
                ? config.in_rate : config.out_rate);
        Require(std::fabs(step.state.length_ratio - state.length_ratio) <=
                rate * dt + 1e-14,
            "actuator step must honor its rate bound");

        double rest_length = 0.0;
        Require(RoR::ResolveHydroRestLength(
            0.01 + 10.0 * UnitRandom(&random_state),
            step.state, &rest_length),
            "valid randomized ratio must produce rest length");
        Require(RoR::HydroActuatorDetail::IsFinite(rest_length) &&
                rest_length > 0.0,
            "randomized rest length must be finite and positive");
    }
}

} // namespace

int main()
{
    TestDocumentedFactorExamples();
    TestLimitsCenterAndInputScaling();
    TestRateLimitsAndCentering();
    TestMalformedInputsFailClosed();
    TestExtremeFiniteInputSpan();
    TestFixedSeedSafetyProperties();
    std::cout << "Hydro actuator response tests passed\n";
    return 0;
}
