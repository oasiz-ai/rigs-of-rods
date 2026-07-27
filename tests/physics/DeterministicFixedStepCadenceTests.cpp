#include "DeterministicFixedStepCadence.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::uint64_t NextRandom(std::uint64_t& state)
{
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * UINT64_C(2685821657736338717);
}

std::vector<std::uint32_t> RunGrouped(
    const std::vector<std::uint32_t>& frame_step_counts,
    std::uint32_t period)
{
    using namespace RoR::DeterministicFixedStepCadence;
    State state;
    std::vector<std::uint32_t> ticks;
    std::uint32_t absolute_step = 0U;
    for (std::size_t frame = 0U;
         frame < frame_step_counts.size();
         ++frame)
    {
        for (std::uint32_t step = 0U;
             step < frame_step_counts[frame];
             ++step)
        {
            ++absolute_step;
            const AdvanceResult result = AdvanceOne(period, state);
            CHECK(result != AdvanceResult::INVALID);
            if (result == AdvanceResult::TICK)
                ticks.push_back(absolute_step);
        }
    }
    return ticks;
}

void TestExactCadence()
{
    using namespace RoR::DeterministicFixedStepCadence;
    State state;
    for (std::uint32_t step = 1U;
         step < SLEEPING_ENGINE_PERIOD_STEPS;
         ++step)
    {
        CHECK(
            AdvanceOne(SLEEPING_ENGINE_PERIOD_STEPS, state) ==
            AdvanceResult::WAIT);
        CHECK(state.phase == step);
    }
    CHECK(
        AdvanceOne(SLEEPING_ENGINE_PERIOD_STEPS, state) ==
        AdvanceResult::TICK);
    CHECK(state.phase == 0U);
}

void TestFrameGroupingIndependence()
{
    std::vector<std::uint32_t> one_step_frames(1024U, 1U);
    const std::vector<std::uint32_t> uneven_frames = {
        1U, 7U, 2U, 63U, 4U, 31U, 128U, 3U, 255U, 17U, 9U,
        64U, 5U, 99U, 8U, 18U, 110U, 200U
    };
    std::uint32_t uneven_total = 0U;
    for (std::size_t i = 0U; i < uneven_frames.size(); ++i)
        uneven_total += uneven_frames[i];
    CHECK(uneven_total == one_step_frames.size());

    const std::vector<std::uint32_t> baseline =
        RunGrouped(one_step_frames, 32U);
    const std::vector<std::uint32_t> regrouped =
        RunGrouped(uneven_frames, 32U);
    CHECK(baseline == regrouped);
    CHECK(baseline.size() == 32U);
    for (std::size_t i = 0U; i < baseline.size(); ++i)
        CHECK(baseline[i] == static_cast<std::uint32_t>((i + 1U) * 32U));
}

void TestPauseAndSaveContinuation()
{
    using namespace RoR::DeterministicFixedStepCadence;
    State state;
    for (std::uint32_t i = 0U; i < 17U; ++i)
        CHECK(AdvanceOne(32U, state) == AdvanceResult::WAIT);

    const std::uint32_t saved_phase = state.phase;
    // A paused simulation does not call AdvanceOne().
    for (std::uint32_t paused_frame = 0U;
         paused_frame < 10000U;
         ++paused_frame)
    {
        CHECK(state.phase == saved_phase);
    }

    State restored = Restore(saved_phase, 32U);
    for (std::uint32_t i = 0U; i < 14U; ++i)
        CHECK(AdvanceOne(32U, restored) == AdvanceResult::WAIT);
    CHECK(AdvanceOne(32U, restored) == AdvanceResult::TICK);
}

void TestInvalidStateFailsClosed()
{
    using namespace RoR::DeterministicFixedStepCadence;
    State state;
    state.phase = 9U;
    CHECK(AdvanceOne(0U, state) == AdvanceResult::INVALID);
    CHECK(state.phase == 9U);
    CHECK(AdvanceOne(9U, state) == AdvanceResult::INVALID);
    CHECK(state.phase == 9U);

    CHECK(Restore(31U, 32U).phase == 31U);
    CHECK(Restore(32U, 32U).phase == 0U);
    CHECK(Restore(UINT32_MAX, 32U).phase == 0U);
    CHECK(Restore(0U, 0U).phase == 0U);
}

void TestFullWidthCounterContract()
{
    using namespace RoR::DeterministicFixedStepCadence;
    std::uint64_t next_step = UINT64_C(0);
    std::uint64_t effect_step = UINT64_C(999);
    for (std::uint64_t expected = 0U; expected < 96U; ++expected)
    {
        const AdvanceResult result =
            AdvanceCounter(32U, next_step, effect_step);
        CHECK(result != AdvanceResult::INVALID);
        CHECK(effect_step == expected);
        CHECK(next_step == expected + 1U);
        CHECK(
            (result == AdvanceResult::TICK) ==
            ((expected + 1U) % 32U == 0U));
    }

    next_step = UINT64_MAX;
    effect_step = UINT64_C(1234);
    CHECK(
        AdvanceCounter(32U, next_step, effect_step) ==
        AdvanceResult::INVALID);
    CHECK(next_step == UINT64_MAX);
    CHECK(effect_step == UINT64_C(1234));
    CHECK(
        AdvanceCounter(0U, next_step, effect_step) ==
        AdvanceResult::INVALID);
}

void TestFixedSeedPropertyCases()
{
    using namespace RoR::DeterministicFixedStepCadence;
    std::uint64_t random = UINT64_C(0x43259f8d13a70ce1);
    for (std::uint32_t fixture = 0U; fixture < 50000U; ++fixture)
    {
        const std::uint32_t period =
            static_cast<std::uint32_t>(NextRandom(random) % 4096U) + 1U;
        const std::uint32_t initial_phase =
            static_cast<std::uint32_t>(NextRandom(random) % period);
        const std::uint32_t steps =
            static_cast<std::uint32_t>(NextRandom(random) % 8192U);

        State state = Restore(initial_phase, period);
        std::uint32_t ticks = 0U;
        for (std::uint32_t step = 0U; step < steps; ++step)
        {
            const AdvanceResult result = AdvanceOne(period, state);
            CHECK(result != AdvanceResult::INVALID);
            if (result == AdvanceResult::TICK)
                ++ticks;
        }

        const std::uint64_t total =
            static_cast<std::uint64_t>(initial_phase) + steps;
        CHECK(ticks == total / period);
        CHECK(state.phase == total % period);
    }
}

} // namespace

int main()
{
    TestExactCadence();
    TestFrameGroupingIndependence();
    TestPauseAndSaveContinuation();
    TestInvalidStateFailsClosed();
    TestFullWidthCounterContract();
    TestFixedSeedPropertyCases();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic fixed-step cadence test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "deterministic fixed-step cadence tests passed\n";
    return EXIT_SUCCESS;
}
