#include "PointColDetector.h"

#include "DeterministicContactOrder.h"

#include <OgreVector.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

class FixedRandom
{
public:
    explicit FixedRandom(std::uint32_t state): m_state(state) {}

    std::uint32_t Next()
    {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return m_state;
    }

    int Uniform(int limit)
    {
        return static_cast<int>(
            Next() % static_cast<std::uint32_t>(limit));
    }

private:
    std::uint32_t m_state;
};

void SetBinary32Bits(float& target, std::uint32_t bits)
{
    unsigned char bytes[sizeof(bits)] = {};
    std::memcpy(bytes, &bits, sizeof(bytes));
    volatile unsigned char* destination =
        reinterpret_cast<volatile unsigned char*>(&target);
    for (std::size_t index = 0; index < sizeof(bytes); ++index)
        destination[index] = bytes[index];
}

RoR::DeterministicContactOrder::PointKey MakeKey(
    const RoR::PointColDetector::oracle_point_t& point,
    int candidate)
{
    return RoR::DeterministicContactOrder::PointKey(
        point.actorid,
        point.nodenum,
        candidate);
}

std::vector<RoR::DeterministicContactOrder::PointKey> BruteForce(
    const std::vector<RoR::PointColDetector::oracle_point_t>& points,
    const std::array<float, 3>& box_min,
    const std::array<float, 3>& box_max)
{
    std::vector<RoR::DeterministicContactOrder::PointKey> result;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const std::array<float, 3>& position = points[index].point;
        if (position[0] >= box_min[0] && position[0] <= box_max[0] &&
                position[1] >= box_min[1] && position[1] <= box_max[1] &&
                position[2] >= box_min[2] && position[2] <= box_max[2])
        {
            result.push_back(MakeKey(points[index], static_cast<int>(index)));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void CheckQuery(
    RoR::PointColDetector& detector,
    const std::vector<RoR::PointColDetector::oracle_point_t>& points,
    const std::array<float, 3>& raw_min,
    const std::array<float, 3>& raw_max,
    float enlargement)
{
    const Ogre::Vector3 minimum(raw_min[0], raw_min[1], raw_min[2]);
    const Ogre::Vector3 maximum(raw_max[0], raw_max[1], raw_max[2]);
    const Ogre::Vector3 third(raw_min[0], raw_max[1], raw_min[2]);
    detector.query(minimum, maximum, third, enlargement);

    std::array<float, 3> expanded_min = raw_min;
    std::array<float, 3> expanded_max = raw_max;
    for (int axis = 0; axis < 3; ++axis)
    {
        expanded_min[axis] -= enlargement;
        expanded_max[axis] += enlargement;
    }
    const std::vector<RoR::DeterministicContactOrder::PointKey> expected =
        BruteForce(points, expanded_min, expanded_max);

    std::vector<RoR::DeterministicContactOrder::PointKey> actual;
    actual.reserve(detector.hit_list.size());
    for (RoR::PointidID_t candidate : detector.hit_list)
    {
        CHECK(candidate >= 0);
        CHECK(static_cast<std::size_t>(candidate) < points.size());
        if (candidate < 0 ||
                static_cast<std::size_t>(candidate) >= points.size())
        {
            continue;
        }
        const RoR::PointColDetector::pointid_t& point_id =
            detector.hit_pointid_list[static_cast<std::size_t>(candidate)];
        actual.push_back(RoR::DeterministicContactOrder::PointKey(
            point_id.actorid,
            point_id.nodenum,
            candidate));
    }

    CHECK(actual.size() == expected.size());
    if (actual.size() == expected.size())
        CHECK(std::equal(actual.begin(), actual.end(), expected.begin()));
    CHECK(std::is_sorted(actual.begin(), actual.end()));
}

void TestRandomizedProductionKdTreeAgainstBruteForce()
{
    FixedRandom random(UINT32_C(0xa511e9b3));
    RoR::PointColDetector detector;

    for (int fixture_index = 0; fixture_index < 10000; ++fixture_index)
    {
        const int point_count = 1 + random.Uniform(64);
        std::vector<RoR::PointColDetector::oracle_point_t> points;
        points.reserve(static_cast<std::size_t>(point_count));
        for (int point_index = 0; point_index < point_count; ++point_index)
        {
            RoR::PointColDetector::oracle_point_t point;
            point.actorid = 1 + random.Uniform(8);
            point.nodenum = static_cast<RoR::NodeNum_t>(random.Uniform(512));
            for (int axis = 0; axis < 3; ++axis)
            {
                point.point[axis] = static_cast<float>(
                    random.Uniform(2001) - 1000);
            }
            points.push_back(point);
        }

        for (std::size_t remaining = points.size(); remaining > 1; --remaining)
        {
            const std::size_t selected = static_cast<std::size_t>(
                random.Next() % static_cast<std::uint32_t>(remaining));
            std::swap(points[remaining - 1], points[selected]);
        }
        CHECK(detector.LoadProductionOracleFixture(points));

        for (int query_index = 0; query_index < 2; ++query_index)
        {
            std::array<float, 3> box_min;
            std::array<float, 3> box_max;
            for (int axis = 0; axis < 3; ++axis)
            {
                const int center = random.Uniform(1601) - 800;
                const int radius = random.Uniform(401);
                box_min[axis] = static_cast<float>(center - radius);
                box_max[axis] = static_cast<float>(center + radius);
            }
            const float enlargement =
                static_cast<float>(random.Uniform(17));
            CheckQuery(detector, points, box_min, box_max, enlargement);
        }
    }
}

void TestInclusiveBoundsAndStableDuplicateTieBreak()
{
    std::vector<RoR::PointColDetector::oracle_point_t> points(5);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        points[index].actorid = 3;
        points[index].nodenum = 7;
    }
    points[0].point = {{-2.f, -2.f, -2.f}};
    points[1].point = {{-1.f, -1.f, -1.f}};
    points[2].point = {{0.f, 0.f, 0.f}};
    points[3].point = {{1.f, 1.f, 1.f}};
    points[4].point = {{2.f, 2.f, 2.f}};

    RoR::PointColDetector detector;
    CHECK(detector.LoadProductionOracleFixture(points));
    CheckQuery(
        detector,
        points,
        {{-1.f, -1.f, -1.f}},
        {{1.f, 1.f, 1.f}},
        0.f);
    CHECK(detector.hit_list.size() == 3);
    if (detector.hit_list.size() == 3)
    {
        CHECK(detector.hit_list[0] == 1);
        CHECK(detector.hit_list[1] == 2);
        CHECK(detector.hit_list[2] == 3);
    }

    CheckQuery(
        detector,
        points,
        {{0.f, 0.f, 0.f}},
        {{0.f, 0.f, 0.f}},
        2.f);
    CHECK(detector.hit_list.size() == points.size());
}

void TestInvalidFixtureReplacementIsTransactional()
{
    RoR::PointColDetector::oracle_point_t valid_point;
    valid_point.actorid = 9;
    valid_point.nodenum = 4;
    valid_point.point = {{1.f, 2.f, 3.f}};
    const std::vector<RoR::PointColDetector::oracle_point_t> valid = {
        valid_point};

    RoR::PointColDetector detector;
    CHECK(detector.LoadProductionOracleFixture(valid));
    CHECK(!detector.LoadProductionOracleFixture(
        std::vector<RoR::PointColDetector::oracle_point_t>()));

    RoR::PointColDetector::oracle_point_t invalid = valid_point;
    invalid.actorid = RoR::ACTORINSTANCEID_INVALID;
    CHECK(!detector.LoadProductionOracleFixture({invalid}));
    invalid = valid_point;
    invalid.nodenum = RoR::NODENUM_INVALID;
    CHECK(!detector.LoadProductionOracleFixture({invalid}));
    invalid = valid_point;
    SetBinary32Bits(invalid.point[1], UINT32_C(0x7fc00000));
    CHECK(!detector.LoadProductionOracleFixture({invalid}));

    CheckQuery(
        detector,
        valid,
        {{1.f, 2.f, 3.f}},
        {{1.f, 2.f, 3.f}},
        0.f);
    CHECK(detector.hit_list.size() == 1);
}

} // namespace

int main()
{
    TestRandomizedProductionKdTreeAgainstBruteForce();
    TestInclusiveBoundsAndStableDuplicateTieBreak();
    TestInvalidFixtureReplacementIsTransactional();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " PointColDetector production-oracle test(s) failed\n";
        return 1;
    }

    std::cout << "PointColDetector production broad-phase oracle passed\n";
    return 0;
}
