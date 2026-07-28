#include "DeterministicScenarioSchedule.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

#define CHECK(condition)                                                      \
    do                                                                        \
    {                                                                         \
        if (!(condition))                                                     \
        {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                          \
                      << ": check failed: " #condition << '\n';               \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

void TestFixedStepBatchContract()
{
    using namespace RoR::DeterministicScenarioSchedule;

    std::uint32_t resolved = 999U;
    CHECK(TryResolveFixedStepsPerFrame(0, resolved));
    CHECK(resolved == 0U);
    CHECK(TryResolveFixedStepsPerFrame(1, resolved));
    CHECK(resolved == 1U);
    CHECK(TryResolveFixedStepsPerFrame(10, resolved));
    CHECK(resolved == 10U);
    CHECK(TryResolveFixedStepsPerFrame(
        static_cast<int>(MAX_FIXED_STEPS_PER_FRAME),
        resolved));
    CHECK(resolved == MAX_FIXED_STEPS_PER_FRAME);

    resolved = 77U;
    CHECK(!TryResolveFixedStepsPerFrame(-1, resolved));
    CHECK(resolved == 77U);
    CHECK(!TryResolveFixedStepsPerFrame(
        static_cast<int>(MAX_FIXED_STEPS_PER_FRAME) + 1,
        resolved));
    CHECK(resolved == 77U);
}

void TestTraceStepLimitContract()
{
    using namespace RoR::DeterministicScenarioSchedule;

    const std::uint64_t maximum = UINT64_C(1000000);
    std::uint64_t resolved = 999U;
    CHECK(TryParseTraceStepLimit("0", maximum, resolved));
    CHECK(resolved == maximum);
    CHECK(TryParseTraceStepLimit("1", maximum, resolved));
    CHECK(resolved == 1U);
    CHECK(TryParseTraceStepLimit("1000", maximum, resolved));
    CHECK(resolved == 1000U);
    CHECK(TryParseTraceStepLimit("00001000", maximum, resolved));
    CHECK(resolved == 1000U);
    CHECK(TryParseTraceStepLimit("1000000", maximum, resolved));
    CHECK(resolved == maximum);

    const std::string invalid[] = {
        "",
        "-1",
        "+1",
        " 1",
        "1 ",
        "1.0",
        "1000001",
        "18446744073709551616"
    };
    for (std::size_t index = 0;
            index < sizeof(invalid) / sizeof(invalid[0]);
            ++index)
    {
        resolved = 123U;
        CHECK(!TryParseTraceStepLimit(
            invalid[index],
            maximum,
            resolved));
        CHECK(resolved == 123U);
    }

    resolved = 456U;
    CHECK(!TryParseTraceStepLimit("0", 0U, resolved));
    CHECK(resolved == 456U);

    const std::string full_width =
        "18446744073709551615";
    CHECK(TryParseTraceStepLimit(
        full_width,
        std::numeric_limits<std::uint64_t>::max(),
        resolved));
    CHECK(resolved == std::numeric_limits<std::uint64_t>::max());
}

} // namespace

int main()
{
    TestFixedStepBatchContract();
    TestTraceStepLimitContract();

    if (failures != 0)
    {
        std::cerr << failures
                  << " deterministic scenario schedule test(s) failed\n";
        return 1;
    }
    std::cout << "deterministic scenario schedule tests passed\n";
    return 0;
}
