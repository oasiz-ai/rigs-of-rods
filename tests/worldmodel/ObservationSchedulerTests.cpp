#include "ObservationScheduler.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace WorldModel = RoR::WorldModel;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(
            stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestExactRationalCadence()
{
    CHECK(WorldModel::PHYSICS_STEPS_PER_SECOND == 2000U);
    CHECK(WorldModel::OBSERVATIONS_PER_SECOND == 48U);
    const std::uint64_t expected[] = {
        0U, 41U, 83U, 125U, 166U,
        208U, 250U, 291U, 333U, 375U
    };
    for (std::uint64_t index = 0U;
         index < sizeof(expected) / sizeof(expected[0]);
         ++index)
    {
        std::uint64_t offset = UINT64_MAX;
        CHECK(WorldModel::TryObservationStepOffset(index, offset));
        CHECK(offset == expected[index]);
    }

    for (std::uint64_t transition = 0U;
         transition < 300U;
         ++transition)
    {
        const std::uint32_t expected_steps =
            transition % 3U == 0U ? 41U : 42U;
        CHECK(
            WorldModel::TransitionPhysicsStepCount(transition) ==
            expected_steps);
        std::uint64_t source = 0U;
        std::uint64_t target = 0U;
        CHECK(WorldModel::TryObservationStepOffset(
            transition, source));
        CHECK(WorldModel::TryObservationStepOffset(
            transition + 1U, target));
        CHECK(target - source == expected_steps);
    }

    std::uint64_t offset = 0U;
    CHECK(WorldModel::TryObservationStepOffset(144U, offset));
    CHECK(offset == 6000U);
    CHECK(WorldModel::TryObservationStepOffset(
        UINT64_C(172800), offset));
    CHECK(offset == UINT64_C(7200000));
}

void TestStrictPolling()
{
    WorldModel::ObservationScheduler scheduler(1000U);
    WorldModel::ObservationBoundary emitted;
    CHECK(
        scheduler.Poll(999U, emitted) ==
        WorldModel::ObservationPollResult::WAIT);
    CHECK(
        scheduler.Poll(1000U, emitted) ==
        WorldModel::ObservationPollResult::EMIT);
    CHECK(emitted.observation_index == 0U);
    for (std::uint64_t completed = 1001U;
         completed < 1041U;
         ++completed)
    {
        CHECK(
            scheduler.Poll(completed, emitted) ==
            WorldModel::ObservationPollResult::WAIT);
    }
    CHECK(
        scheduler.Poll(1041U, emitted) ==
        WorldModel::ObservationPollResult::EMIT);
    CHECK(emitted.observation_index == 1U);
    CHECK(
        scheduler.Poll(1084U, emitted) ==
        WorldModel::ObservationPollResult::MISSED_BOUNDARY);
    CHECK(scheduler.GetNextBoundary().observation_index == 2U);
    CHECK(
        scheduler.GetNextBoundary().completed_physics_steps ==
        1083U);
}

void TestLongRunHasNoDropsOrDuplicates()
{
    WorldModel::ObservationScheduler scheduler(17U);
    WorldModel::ObservationBoundary emitted;
    std::vector<WorldModel::ObservationBoundary> observations;
    for (std::uint64_t completed = 17U;
         completed <= UINT64_C(12517);
         ++completed)
    {
        const WorldModel::ObservationPollResult result =
            scheduler.Poll(completed, emitted);
        CHECK(
            result == WorldModel::ObservationPollResult::WAIT ||
            result == WorldModel::ObservationPollResult::EMIT);
        if (result == WorldModel::ObservationPollResult::EMIT)
            observations.push_back(emitted);
    }

    CHECK(observations.size() == 301U);
    for (std::size_t index = 0U;
         index < observations.size();
         ++index)
    {
        std::uint64_t expected_offset = 0U;
        CHECK(WorldModel::TryObservationStepOffset(
            static_cast<std::uint64_t>(index),
            expected_offset));
        CHECK(
            observations[index].observation_index ==
            static_cast<std::uint64_t>(index));
        CHECK(
            observations[index].completed_physics_steps ==
            17U + expected_offset);
    }
}

void TestRestoreAndOverflowSafety()
{
    WorldModel::ObservationScheduler scheduler(100U);
    CHECK(scheduler.Reset(500U, 3U));
    CHECK(
        scheduler.GetNextBoundary().completed_physics_steps ==
        625U);
    const WorldModel::ObservationBoundary before =
        scheduler.GetNextBoundary();
    CHECK(!scheduler.Reset(UINT64_MAX, 1U));
    CHECK(scheduler.GetNextBoundary() == before);

    WorldModel::ObservationBoundary unchanged;
    unchanged.observation_index = 77U;
    unchanged.completed_physics_steps = 88U;
    CHECK(!WorldModel::TryObservationBoundary(
        UINT64_MAX, 1U, unchanged));
    CHECK(unchanged.observation_index == 77U);
    CHECK(unchanged.completed_physics_steps == 88U);

    std::uint64_t low = 0U;
    std::uint64_t high = UINT64_MAX;
    while (low < high)
    {
        const std::uint64_t middle =
            low + (high - low) / 2U + 1U;
        std::uint64_t ignored = 0U;
        if (WorldModel::TryObservationStepOffset(
                middle, ignored))
            low = middle;
        else
            high = middle - 1U;
    }
    std::uint64_t maximum_offset = 0U;
    CHECK(WorldModel::TryObservationStepOffset(
        low, maximum_offset));
    CHECK(low < UINT64_MAX);
    std::uint64_t sentinel = UINT64_C(0x123456789abcdef0);
    CHECK(!WorldModel::TryObservationStepOffset(
        low + 1U, sentinel));
    CHECK(sentinel == UINT64_C(0x123456789abcdef0));

    WorldModel::ObservationScheduler last_origin(UINT64_MAX);
    WorldModel::ObservationBoundary emitted;
    CHECK(
        last_origin.Poll(UINT64_MAX, emitted) ==
        WorldModel::ObservationPollResult::EMIT);
    CHECK(!last_origin.HasNextBoundary());
    CHECK(
        last_origin.Poll(UINT64_MAX, emitted) ==
        WorldModel::ObservationPollResult::EXHAUSTED);
}

} // namespace

int main()
{
    TestExactRationalCadence();
    TestStrictPolling();
    TestLongRunHasNoDropsOrDuplicates();
    TestRestoreAndOverflowSafety();
    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "%d observation scheduler test(s) failed\n",
            g_failures);
        return EXIT_FAILURE;
    }
    std::puts("observation scheduler tests passed");
    return EXIT_SUCCESS;
}
