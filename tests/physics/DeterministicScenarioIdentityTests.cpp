#include "DeterministicScenarioIdentity.h"

#include <cstdint>
#include <iostream>
#include <limits>

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

void TestLegacyCompatibility()
{
    using namespace RoR::DeterministicScenarioIdentity;

    const Resolution first = Resolve(0U, 0U, 1U);
    const Resolution later = Resolve(0U, 0U, 99U);
    CHECK(IsValid(first));
    CHECK(!first.explicit_identity);
    CHECK(first.scenario_seed == 0U);
    CHECK(first.actor_stream_id == 0U);
    CHECK(
        first.deterministic_seed ==
        RoR::DeterministicCounterNoise::MakeActorSeed(1U));
    CHECK(IsValid(later));
    CHECK(first.deterministic_seed != later.deterministic_seed);
}

void TestExplicitIdentityIsAllocationOrderIndependent()
{
    using namespace RoR::DeterministicScenarioIdentity;

    const std::uint64_t scenario = UINT64_C(0x123456789abcdef0);
    const std::uint64_t stream = UINT64_C(0x42);
    const Resolution allocated_first = Resolve(scenario, stream, 1U);
    const Resolution allocated_later = Resolve(scenario, stream, 987654U);

    CHECK(IsValid(allocated_first));
    CHECK(allocated_first.explicit_identity);
    CHECK(MatchesExplicitIdentity(allocated_first, allocated_later));
    CHECK(
        allocated_first.deterministic_seed ==
        UINT64_C(0x404a37f3d463e132));
    CHECK(
        allocated_first.deterministic_seed ==
        allocated_later.deterministic_seed);

    const Resolution another_stream = Resolve(scenario, stream + 1U, 1U);
    const Resolution another_scenario = Resolve(scenario + 1U, stream, 1U);
    CHECK(another_stream.deterministic_seed !=
        allocated_first.deterministic_seed);
    CHECK(another_scenario.deterministic_seed !=
        allocated_first.deterministic_seed);
    CHECK(!MatchesExplicitIdentity(allocated_first, another_stream));
    CHECK(!MatchesExplicitIdentity(allocated_first, another_scenario));
}

void TestPartialIdentityFailsClosed()
{
    using namespace RoR::DeterministicScenarioIdentity;

    const Resolution missing_scenario = Resolve(0U, 7U, 13U);
    const Resolution missing_stream = Resolve(7U, 0U, 13U);
    CHECK(!IsValid(missing_scenario));
    CHECK(!IsValid(missing_stream));
    CHECK(
        missing_scenario.error ==
        Error::PARTIAL_EXPLICIT_IDENTITY);
    CHECK(
        missing_stream.error ==
        Error::PARTIAL_EXPLICIT_IDENTITY);
    CHECK(!missing_scenario.explicit_identity);
    CHECK(!missing_stream.explicit_identity);
    CHECK(missing_scenario.deterministic_seed == 0U);
    CHECK(missing_stream.deterministic_seed == 0U);
}

void TestSavegameSeedRevalidation()
{
    using namespace RoR::DeterministicScenarioIdentity;

    const std::uint64_t scenario =
        std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t stream = UINT64_C(1);
    const Resolution identity = Resolve(scenario, stream, 77U);
    CHECK(IsValid(identity));
    CHECK(RevalidatesStoredSeed(
        scenario,
        stream,
        identity.deterministic_seed));
    CHECK(!RevalidatesStoredSeed(
        scenario,
        stream,
        identity.deterministic_seed ^ UINT64_C(1)));
    CHECK(!RevalidatesStoredSeed(0U, stream, identity.deterministic_seed));
    CHECK(!RevalidatesStoredSeed(scenario, 0U, identity.deterministic_seed));
}

} // namespace

int main()
{
    TestLegacyCompatibility();
    TestExplicitIdentityIsAllocationOrderIndependent();
    TestPartialIdentityFailsClosed();
    TestSavegameSeedRevalidation();

    if (failures != 0)
    {
        std::cerr << failures
                  << " deterministic scenario identity test(s) failed\n";
        return 1;
    }
    std::cout << "deterministic scenario identity tests passed\n";
    return 0;
}
