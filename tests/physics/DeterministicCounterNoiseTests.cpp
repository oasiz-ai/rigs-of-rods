#include "DeterministicCounterNoise.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

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

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct GoldenVector
{
    std::uint64_t seed;
    std::uint64_t step;
    std::uint64_t domain;
    std::uint64_t element;
    std::uint32_t lane;
    std::uint32_t expected_hash;
    std::uint32_t expected_signed_bits;
    std::uint32_t expected_unit_bits;
};

void TestGoldenVectors()
{
    using namespace RoR::DeterministicCounterNoise;

    const GoldenVector vectors[] = {
        {
            UINT64_C(0),
            UINT64_C(0),
            DOMAIN_TURBULENT_DRAG,
            UINT64_C(0),
            UINT32_C(0),
            UINT32_C(0xa62d69b5),
            UINT32_C(0xbe94b258),
            UINT32_C(0x3eb5a6d4)
        },
        {
            UINT64_C(1),
            UINT64_C(0),
            DOMAIN_TURBULENT_DRAG,
            UINT64_C(0),
            UINT32_C(0),
            UINT32_C(0xac82342a),
            UINT32_C(0xbf772f58),
            UINT32_C(0x3c8d0a80)
        },
        {
            UINT64_C(0x5eed1234),
            UINT64_C(1),
            DOMAIN_TURBULENT_DRAG,
            UINT64_C(1),
            UINT32_C(0),
            UINT32_C(0x9c151805),
            UINT32_C(0xbf2b9fec),
            UINT32_C(0x3e28c028)
        },
        {
            UINT64_C(0x5eed1234),
            UINT64_C(1),
            DOMAIN_TURBULENT_DRAG,
            UINT64_C(1),
            UINT32_C(1),
            UINT32_C(0xa7db19fb),
            UINT32_C(0x3ed8cfd8),
            UINT32_C(0x3f3633f6)
        },
        {
            UINT64_C(0x5eed1234),
            UINT64_C(1),
            DOMAIN_TURBULENT_DRAG,
            UINT64_C(1),
            UINT32_C(2),
            UINT32_C(0xf0c713e8),
            UINT32_C(0x3de27d00),
            UINT32_C(0x3f0e27d0)
        },
        {
            UINT64_C(0x5eed1234),
            UINT64_C(0x123456789abcdef0),
            DOMAIN_ENGINE_ANTILAG,
            UINT64_C(0xffffffff),
            UINT32_C(2),
            UINT32_C(0x5dd670e8),
            UINT32_C(0x3eb38740),
            UINT32_C(0x3f2ce1d0)
        },
        {
            UINT64_MAX,
            UINT64_MAX,
            DOMAIN_TURBULENT_DRAG,
            UINT64_MAX,
            UINT32_C(2),
            UINT32_C(0xfcc55646),
            UINT32_C(0x3daac8c0),
            UINT32_C(0x3f0aac8c)
        }
    };

    for (const GoldenVector& vector : vectors)
    {
        CHECK(
            Hash(
                vector.seed,
                vector.step,
                vector.domain,
                vector.element,
                vector.lane) ==
            vector.expected_hash);
        CHECK(
            FloatBits(
                SignedSample(
                    vector.seed,
                    vector.step,
                    vector.domain,
                    vector.element,
                    vector.lane)) ==
            vector.expected_signed_bits);
        CHECK(
            FloatBits(
                UnitSample(
                    vector.seed,
                    vector.step,
                    vector.domain,
                    vector.element,
                    vector.lane)) ==
            vector.expected_unit_bits);
    }

    CHECK(MakeActorSeed(0) == UINT64_C(0xda7695f32d2c790e));
    CHECK(MakeActorSeed(1) == UINT64_C(0x0984abe9faed338d));
    CHECK(MakeActorSeed(2) == UINT64_C(0x0f87469423c5cd04));
    CHECK(
        MakeActorSeed(UINT64_C(0xffffffff)) ==
        UINT64_C(0x49b82a75014bd2b3));
}

void TestLegacyRangeAndEndpoints()
{
    using namespace RoR::DeterministicCounterNoise;

    CHECK(FloatBits(LegacyRangeTwoFromHash(0)) == UINT32_C(0x40000000));
    CHECK(
        FloatBits(LegacyRangeTwoFromHash(UINT32_C(0x007fffff))) ==
        UINT32_C(0x407fffff));
    CHECK(
        FloatBits(LegacyRangeTwoFromHash(UINT32_C(0xffffffff))) ==
        UINT32_C(0x407fffff));

    const float signed_min = LegacyRangeTwoFromHash(0) - 3.0f;
    const float signed_max =
        LegacyRangeTwoFromHash(UINT32_C(0x007fffff)) - 3.0f;
    const float unit_min = (LegacyRangeTwoFromHash(0) - 2.0f) * 0.5f;
    const float unit_max =
        (LegacyRangeTwoFromHash(UINT32_C(0x007fffff)) - 2.0f) * 0.5f;

    CHECK(FloatBits(signed_min) == UINT32_C(0xbf800000));
    CHECK(FloatBits(signed_max) == UINT32_C(0x3f7ffffc));
    CHECK(FloatBits(unit_min) == UINT32_C(0x00000000));
    CHECK(FloatBits(unit_max) == UINT32_C(0x3f7ffffe));

    for (std::uint64_t step = 0; step < 1024; ++step)
    {
        for (std::uint32_t lane = 0; lane < 3; ++lane)
        {
            const float signed_value =
                SignedSample(
                    UINT64_C(0x5eed1234),
                    step,
                    DOMAIN_TURBULENT_DRAG,
                    step * 17,
                    lane);
            const float unit_value =
                UnitSample(
                    UINT64_C(0x5eed1234),
                    step,
                    DOMAIN_ENGINE_ANTILAG,
                    step * 17,
                    lane);
            CHECK(signed_value >= -1.0f);
            CHECK(signed_value < 1.0f);
            CHECK(unit_value >= 0.0f);
            CHECK(unit_value < 1.0f);
        }
    }
}

void TestKeyDimensionsAreIndependent()
{
    using namespace RoR::DeterministicCounterNoise;

    const std::uint64_t seed = MakeActorSeed(7);
    const std::uint32_t baseline =
        Hash(seed, 11, DOMAIN_TURBULENT_DRAG, 23, 1);

    CHECK(Hash(MakeActorSeed(8), 11, DOMAIN_TURBULENT_DRAG, 23, 1) != baseline);
    CHECK(Hash(seed, 12, DOMAIN_TURBULENT_DRAG, 23, 1) != baseline);
    CHECK(Hash(seed, 11, DOMAIN_ENGINE_ANTILAG, 23, 1) != baseline);
    CHECK(Hash(seed, 11, DOMAIN_TURBULENT_DRAG, 24, 1) != baseline);
    CHECK(Hash(seed, 11, DOMAIN_TURBULENT_DRAG, 23, 2) != baseline);

    // Sampling an unrelated actor/key cannot advance or otherwise perturb A.
    const std::uint32_t actor_a_next =
        Hash(seed, 12, DOMAIN_TURBULENT_DRAG, 23, 1);
    (void)Hash(
        MakeActorSeed(99),
        4000,
        DOMAIN_ENGINE_ANTILAG,
        3,
        0);
    CHECK(
        Hash(seed, 12, DOMAIN_TURBULENT_DRAG, 23, 1) ==
        actor_a_next);

}

const std::size_t ACTOR_COUNT = 8;
// Match the pinned DAF semi's runtime node count. This is still a
// dependency-free kernel fixture, not a content/runtime simulation.
const std::size_t NODE_COUNT = 176;
const std::size_t LANE_COUNT = 3;
const std::size_t SAMPLE_COUNT = ACTOR_COUNT * NODE_COUNT * LANE_COUNT;

std::size_t SampleIndex(
    std::size_t actor,
    std::size_t node,
    std::size_t lane)
{
    return (actor * NODE_COUNT + node) * LANE_COUNT + lane;
}

void GenerateActor(
    std::vector<std::uint32_t>& output,
    std::size_t actor,
    bool reverse_nodes)
{
    using namespace RoR::DeterministicCounterNoise;

    const std::uint64_t seed =
        MakeActorSeed(static_cast<std::uint64_t>(actor + 1));
    for (std::size_t ordinal = 0; ordinal < NODE_COUNT; ++ordinal)
    {
        const std::size_t node =
            reverse_nodes ? NODE_COUNT - ordinal - 1 : ordinal;
        for (std::size_t lane = 0; lane < LANE_COUNT; ++lane)
        {
            output[SampleIndex(actor, node, lane)] =
                Hash(
                    seed,
                    UINT64_C(123456),
                    DOMAIN_TURBULENT_DRAG,
                    static_cast<std::uint64_t>(node),
                    static_cast<std::uint32_t>(lane));
        }
    }
}

std::vector<std::uint32_t> GenerateSerial(bool reverse_actors, bool reverse_nodes)
{
    std::vector<std::uint32_t> output(SAMPLE_COUNT);
    for (std::size_t ordinal = 0; ordinal < ACTOR_COUNT; ++ordinal)
    {
        const std::size_t actor =
            reverse_actors ? ACTOR_COUNT - ordinal - 1 : ordinal;
        GenerateActor(output, actor, reverse_nodes);
    }
    return output;
}

std::vector<std::uint32_t> GenerateParallel(std::size_t worker_count)
{
    std::vector<std::uint32_t> output(SAMPLE_COUNT);
    std::vector<std::thread> workers;
    for (std::size_t worker = 0; worker < worker_count; ++worker)
    {
        workers.push_back(
            std::thread(
                [&output, worker, worker_count]()
                {
                    for (
                        std::size_t actor = worker;
                        actor < ACTOR_COUNT;
                        actor += worker_count)
                    {
                        GenerateActor(output, actor, (actor & 1U) != 0);
                    }
                }));
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    return output;
}

void TestTraversalAndWorkerIndependence()
{
    const std::vector<std::uint32_t> expected =
        GenerateSerial(false, false);

    CHECK(GenerateSerial(true, false) == expected);
    CHECK(GenerateSerial(false, true) == expected);
    CHECK(GenerateSerial(true, true) == expected);

    for (int repetition = 0; repetition < 32; ++repetition)
    {
        CHECK(GenerateParallel(1) == expected);
        CHECK(GenerateParallel(2) == expected);
        CHECK(GenerateParallel(8) == expected);
    }

    // Omitting another actor entirely leaves this actor's canonical samples
    // unchanged.
    std::vector<std::uint32_t> actor_a_before(SAMPLE_COUNT);
    std::vector<std::uint32_t> actor_a_after(SAMPLE_COUNT);
    GenerateActor(actor_a_before, 0, false);
    std::vector<std::uint32_t> unrelated(SAMPLE_COUNT);
    GenerateActor(unrelated, 1, true);
    GenerateActor(actor_a_after, 0, true);
    for (std::size_t node = 0; node < NODE_COUNT; ++node)
    {
        for (std::size_t lane = 0; lane < LANE_COUNT; ++lane)
        {
            const std::size_t index = SampleIndex(0, node, lane);
            CHECK(actor_a_before[index] == actor_a_after[index]);
        }
    }
}

} // namespace

int main()
{
    TestGoldenVectors();
    TestLegacyRangeAndEndpoints();
    TestKeyDimensionsAreIndependent();
    TestTraversalAndWorkerIndependence();

    if (g_failures != 0)
    {
        std::cerr
            << g_failures
            << " deterministic counter noise test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "deterministic counter noise tests passed\n";
    return EXIT_SUCCESS;
}
