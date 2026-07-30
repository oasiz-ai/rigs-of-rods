#include "WorldModelCaptureContract.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

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

void TestVersionedSchemaIdentifiers()
{
    const WorldModel::SchemaIdentifier manifest =
        WorldModel::EpisodeManifestSchema();
    const WorldModel::SchemaIdentifier observation =
        WorldModel::ObservationSchema();
    const WorldModel::SchemaIdentifier transition =
        WorldModel::TransitionSchema();
    CHECK(manifest.major_version == 1U);
    CHECK(manifest.minor_version == 0U);
    CHECK(manifest != observation);
    CHECK(observation != transition);
    CHECK(WorldModel::IsSupportedSchema(manifest));
    CHECK(WorldModel::IsSupportedSchema(observation));
    CHECK(WorldModel::IsSupportedSchema(transition));
    CHECK(std::strcmp(
        WorldModel::SchemaName(manifest.kind),
        "org.rigsofrods.worldmodel.episode-manifest") == 0);
    CHECK(std::strcmp(
        WorldModel::SchemaName(observation.kind),
        "org.rigsofrods.worldmodel.observation") == 0);
    CHECK(std::strcmp(
        WorldModel::SchemaName(transition.kind),
        "org.rigsofrods.worldmodel.transition") == 0);

    WorldModel::SchemaIdentifier future = observation;
    future.major_version = 2U;
    CHECK(!WorldModel::IsSupportedSchema(future));
    future = observation;
    future.minor_version = 1U;
    CHECK(!WorldModel::IsSupportedSchema(future));
    future.kind = static_cast<WorldModel::SchemaKind>(UINT32_MAX);
    CHECK(!WorldModel::IsSupportedSchema(future));
}

void TestObservationAndTransitionIdentity()
{
    const WorldModel::EpisodeId invalid;
    const WorldModel::EpisodeId episode(1U, 2U);
    CHECK(!WorldModel::IsValidEpisodeId(invalid));
    CHECK(WorldModel::IsValidEpisodeId(episode));

    WorldModel::ObservationId unchanged;
    unchanged.observation_index = 91U;
    unchanged.completed_physics_steps = 92U;
    CHECK(!WorldModel::MakeObservationId(
        invalid, 100U, 0U, unchanged));
    CHECK(unchanged.observation_index == 91U);

    WorldModel::ObservationId observation;
    CHECK(WorldModel::MakeObservationId(
        episode, 100U, 3U, observation));
    CHECK(observation.episode == episode);
    CHECK(observation.observation_index == 3U);
    CHECK(observation.completed_physics_steps == 225U);

    WorldModel::TransitionId transition;
    CHECK(WorldModel::MakeTransitionId(
        episode, 100U, 1U, transition));
    CHECK(transition.source.observation_index == 1U);
    CHECK(transition.source.completed_physics_steps == 141U);
    CHECK(transition.target.observation_index == 2U);
    CHECK(transition.target.completed_physics_steps == 183U);
    CHECK(WorldModel::IsValidTransitionId(transition));

    WorldModel::TransitionId malformed = transition;
    malformed.target.episode.low += 1U;
    CHECK(!WorldModel::IsValidTransitionId(malformed));
    malformed = transition;
    malformed.target.observation_index += 1U;
    CHECK(!WorldModel::IsValidTransitionId(malformed));
    malformed = transition;
    ++malformed.target.completed_physics_steps;
    CHECK(!WorldModel::IsValidTransitionId(malformed));

    const WorldModel::TransitionId before = transition;
    CHECK(!WorldModel::MakeTransitionId(
        episode, 0U, UINT64_MAX, transition));
    CHECK(transition == before);
    CHECK(!WorldModel::MakeTransitionId(
        episode, UINT64_MAX, 0U, transition));
    CHECK(transition == before);
}

void TestCanonicalEpisodeText()
{
    const WorldModel::EpisodeId expected(
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210));
    std::string text = "unchanged";
    CHECK(WorldModel::FormatEpisodeId(expected, text));
    CHECK(text == "0123456789abcdeffedcba9876543210");

    WorldModel::EpisodeId parsed(9U, 10U);
    CHECK(WorldModel::ParseEpisodeId(text, parsed));
    CHECK(parsed == expected);

    std::string unchanged = "unchanged";
    CHECK(!WorldModel::FormatEpisodeId(
        WorldModel::EpisodeId(), unchanged));
    CHECK(unchanged == "unchanged");

    const WorldModel::EpisodeId sentinel(9U, 10U);
    parsed = sentinel;
    CHECK(!WorldModel::ParseEpisodeId("", parsed));
    CHECK(parsed == sentinel);
    CHECK(!WorldModel::ParseEpisodeId(
        "0123456789abcdeffedcba987654321", parsed));
    CHECK(parsed == sentinel);
    CHECK(!WorldModel::ParseEpisodeId(
        "0123456789abcdeffedcba98765432100", parsed));
    CHECK(parsed == sentinel);
    CHECK(!WorldModel::ParseEpisodeId(
        "0123456789ABCDEFFEDCBA9876543210", parsed));
    CHECK(parsed == sentinel);
    CHECK(!WorldModel::ParseEpisodeId(
        "0123456789abcdef-fedcba987654321", parsed));
    CHECK(parsed == sentinel);
    CHECK(!WorldModel::ParseEpisodeId(
        "00000000000000000000000000000000", parsed));
    CHECK(parsed == sentinel);
}

void TestDeterministicEpisodeAndSeedDomains()
{
    const std::uint64_t root = UINT64_C(0x0123456789abcdef);
    const WorldModel::EpisodeId first =
        WorldModel::DeriveEpisodeId(root, 0U);
    const WorldModel::EpisodeId repeated =
        WorldModel::DeriveEpisodeId(root, 0U);
    const WorldModel::EpisodeId second =
        WorldModel::DeriveEpisodeId(root, 1U);
    CHECK(WorldModel::IsValidEpisodeId(first));
    CHECK(first == repeated);
    CHECK(first != second);

    const std::uint64_t simulation = WorldModel::DeriveSeed(
        root,
        WorldModel::SeedDomain::SIMULATION,
        first,
        7U,
        9U);
    CHECK(simulation == WorldModel::DeriveSeed(
        root,
        WorldModel::SeedDomain::SIMULATION,
        first,
        7U,
        9U));
    CHECK(simulation != WorldModel::DeriveSeed(
        root,
        WorldModel::SeedDomain::RESET,
        first,
        7U,
        9U));
    CHECK(simulation != WorldModel::DeriveSeed(
        root,
        WorldModel::SeedDomain::SIMULATION,
        second,
        7U,
        9U));

    WorldModel::ObservationId observation;
    CHECK(WorldModel::MakeObservationId(
        first, 123U, 19U, observation));
    const std::uint64_t sensor_seed =
        WorldModel::DeriveObservationSeed(
            root, observation, 3U);
    WorldModel::ObservationId moved = observation;
    ++moved.completed_physics_steps;
    CHECK(sensor_seed != WorldModel::DeriveObservationSeed(
        root, moved, 3U));

    WorldModel::TransitionId transition;
    CHECK(WorldModel::MakeTransitionId(
        first, 123U, 19U, transition));
    const std::uint64_t transition_seed =
        WorldModel::DeriveTransitionSeed(
            root, transition, 3U);
    CHECK(sensor_seed != transition_seed);
    WorldModel::TransitionId moved_transition = transition;
    ++moved_transition.target.completed_physics_steps;
    CHECK(transition_seed != WorldModel::DeriveTransitionSeed(
        root, moved_transition, 3U));

    // Fixed vectors are filled after compiling the implementation.
    CHECK(first.high == UINT64_C(0xcec8df0a80efbcf9));
    CHECK(first.low == UINT64_C(0xd04e40ad40c6fad4));
    CHECK(simulation == UINT64_C(0x4e67503b6206fc15));
    CHECK(sensor_seed == UINT64_C(0x99e408d9c51b5f6a));
    CHECK(transition_seed == UINT64_C(0x141b8d37405a306b));
}

} // namespace

int main()
{
    TestVersionedSchemaIdentifiers();
    TestObservationAndTransitionIdentity();
    TestCanonicalEpisodeText();
    TestDeterministicEpisodeAndSeedDomains();
    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "%d world-model contract test(s) failed\n",
            g_failures);
        return EXIT_FAILURE;
    }
    std::puts("world-model capture contract tests passed");
    return EXIT_SUCCESS;
}
