#include "CityWorldNeoQ20Compatibility.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failures = 0;
const char PINNED_TOBJ_SHA256[] =
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48";
const char* const ORIGINAL_SERVICE_NAMES[] = {
    "spawnZone_truckshop_2",
    "spawnZone_load-spawner_3",
    "spawnZone_load-spawner_4"};
const char* const GROUNDED_SERVICE_NAMES[] = {
    "spawnZone_neoq20_truckshop_2",
    "spawnZone_neoq20_load-spawner_3",
    "spawnZone_neoq20_load-spawner_4"};

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__                           \
                      << ": check failed: " #condition << "\n";                \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

struct SourceRow
{
    const char* object_definition;
    const char* type;
    float position_x;
    float position_z;
    float rotation_x;
    float rotation_y;
};

const SourceRow NEOQ20_ROWS[] = {
    {"truckshop", "shop", 6750.0f, 4160.0f, 0.0f, 89.9f},
    {"load-spawner", "sale", 6800.0f, 4355.0f, 0.0f, 0.0f},
    {"load-spawner", "sale", 6760.0f, 4355.0f, 0.0f, 0.0f},
    {"NeoQ2-0main-city-section-A", "", 7900.0f, 5600.0f, 90.0f, 0.0f},
    {"NeoQ2-0main-city-section-A-1", "", 8250.0f, 5450.0f, 90.0f, 0.0f},
    {"NeoQ2-0main-city-section-B", "", 6000.0f, 6200.0f, 90.0f, 0.0f},
    {"NeoQ2-0main-city-section-B-1", "", 6150.0f, 5750.0f, 90.0f, 0.0f},
    {"NeoQ2-0condos-A", "", 7000.0f, 7000.0f, 90.0f, 0.0f},
    {"NeoQ2-0highway-distributor-B-C", "", 7000.0f, 7000.0f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-D", "", 9000.0f, 6100.0f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-E", "", 8400.0f, 4100.0f, 90.0f, 0.0f},
    {"NeoQ2-0sout-center", "", 5493.2f, 6601.9f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-A", "", 5804.1f, 5410.1f, 90.0f, 0.0f},
    {"NeoQ2-0sout-center-bridge", "", 5387.2f, 7040.2f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-D-E-distributor-road", "",
     8900.0f, 5100.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-oficinas-cuadrante-4", "",
     7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0industrial-zone-distributor-road", "",
     7000.0f, 4018.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-oficinas-cuadrante-1", "",
     7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-B", "", 5900.0f, 7350.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center", "", 7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-distributor-road", "",
     7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-oficinas-cuadrante-3", "",
     7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-parking", "", 7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0highway-section-C", "", 9000.0f, 7500.0f, 90.0f, 0.0f},
    {"NeoQ2-0industrial-zone", "", 7000.0f, 4445.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-oficinas-cuadrante-2", "",
     7000.0f, 6000.0f, 90.0f, 0.0f},
    {"NeoQ2-0trade-center-bridge", "", 7000.0f, 5445.0f, 90.0f, 0.0f},
    {"NeoQ2-0filling-A", "", 5993.2f, 7101.9f, 90.0f, 0.0f},
    {"NeoQ2-0filling-B", "", 8400.0f, 4600.0f, 90.0f, 0.0f},
    {"NeoQ2-0lough", "", 8000.0f, 6600.0f, 90.0f, 0.0f},
    {"NeoQ2-0Building-under-construction", "",
     6666.0f, 5937.0f, 90.0f, 0.0f},
    {"NeoQ2-0medium-school", "", 7357.0f, 7065.0f, 90.0f, 0.0f},
    {"NeoQ2-0high-school", "", 7900.0f, 5600.0f, 90.0f, 0.0f},
    {"NeoQ2-0airport", "", 5804.1f, 5410.1f, 90.0f, 0.0f},
    {"NeoQ2-0main-filling-cityworld", "",
     7000.0f, 6000.0f, 90.0f, 0.0f}};

std::vector<RoR::CityWorldNeoQ20Placement> ExactPlacements()
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements;
    placements.push_back({
        50U, "unrelated", "", "outside", 1.0f, 123.0f, 2.0f,
        0.0f, 0.0f, 0.0f});

    // Earlier source rows 387-389 deliberately duplicate the three authored
    // NeoQ2.0 service instance names.
    placements.push_back({
        387U, "truckshop", "shop", ORIGINAL_SERVICE_NAMES[0],
        2440.896973f, 0.85f, 1020.876587f, 0.0f, 0.0f, 0.0f});
    placements.push_back({
        388U, "load-spawner", "sale", ORIGINAL_SERVICE_NAMES[1],
        2402.270020f, 0.351947f, 1079.235352f, 0.0f, 0.0f, 0.0f});
    placements.push_back({
        389U, "load-spawner", "sale", ORIGINAL_SERVICE_NAMES[2],
        2402.270020f, 0.351947f, 1039.235352f, 0.0f, 0.0f, 0.0f});

    for (std::size_t index = 0U;
         index < sizeof(NEOQ20_ROWS) / sizeof(NEOQ20_ROWS[0]);
         ++index)
    {
        const std::string runtime_instance_name =
            index < 3U
                ? ORIGINAL_SERVICE_NAMES[index]
                : "auto^CityWorld.tobj(line:" +
                    std::to_string(1213U + index) + ")";
        placements.push_back({
            1214U + index,
            NEOQ20_ROWS[index].object_definition,
            NEOQ20_ROWS[index].type,
            runtime_instance_name,
            NEOQ20_ROWS[index].position_x,
            50.0f,
            NEOQ20_ROWS[index].position_z,
            NEOQ20_ROWS[index].rotation_x,
            NEOQ20_ROWS[index].rotation_y,
            0.0f});
    }
    return placements;
}

std::vector<RoR::CityWorldNeoQ20Telepoint> ExactTelepoints()
{
    return {
        {"Penguinville Spawn", 436.5f, 0.1f, 446.0f},
        {"NeoQ2.0 Spawn", 6773.92f, 50.0f, 4216.68f}};
}

std::vector<std::string> PinnedDependencies()
{
    return {
        RoR::GetCityWorldNeoQ20PinnedDependency(),
        "Other.zip:Other.terrn2:"
        "0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef"};
}

const RoR::CityWorldNeoQ20Placement* FindBySourceLine(
    const std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    std::size_t source_line)
{
    for (const RoR::CityWorldNeoQ20Placement& placement : placements)
    {
        if (placement.source_line == source_line)
        {
            return &placement;
        }
    }
    return nullptr;
}

RoR::CityWorldNeoQ20Placement* FindMutableBySourceLine(
    std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    std::size_t source_line)
{
    for (RoR::CityWorldNeoQ20Placement& placement : placements)
    {
        if (placement.source_line == source_line)
        {
            return &placement;
        }
    }
    return nullptr;
}

// This intentionally mirrors Collisions::getBox()/getPosition(): the first
// event source with the requested instance name wins.
const RoR::CityWorldNeoQ20Placement* ResolveFirstInstance(
    const std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    const std::string& instance_name)
{
    for (const RoR::CityWorldNeoQ20Placement& placement : placements)
    {
        if (placement.instance_name == instance_name)
        {
            return &placement;
        }
    }
    return nullptr;
}

void CheckNeoQ20State(
    const std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    float expected_y,
    bool expect_grounded_names,
    std::size_t expected_count = 35U)
{
    std::size_t count = 0U;
    for (const RoR::CityWorldNeoQ20Placement& placement : placements)
    {
        if (placement.source_line < 1214U ||
            placement.source_line > 1248U)
        {
            continue;
        }
        ++count;
        CHECK(placement.position_y == expected_y);
        const std::size_t service_index =
            placement.source_line - 1214U;
        if (service_index < 3U)
        {
            CHECK(
                placement.instance_name ==
                (expect_grounded_names
                    ? GROUNDED_SERVICE_NAMES[service_index]
                    : ORIGINAL_SERVICE_NAMES[service_index]));
        }
    }
    CHECK(count == expected_count);

    for (std::size_t duplicate_index = 0U;
         duplicate_index < 3U;
         ++duplicate_index)
    {
        const RoR::CityWorldNeoQ20Placement* earlier =
            FindBySourceLine(placements, 387U + duplicate_index);
        CHECK(earlier != nullptr);
        if (earlier != nullptr)
        {
            CHECK(
                earlier->instance_name ==
                ORIGINAL_SERVICE_NAMES[duplicate_index]);
        }
    }
}

void CheckTelepointStillAtSource(
    const std::vector<RoR::CityWorldNeoQ20Telepoint>& telepoints)
{
    CHECK(telepoints.size() == 2U);
    CHECK(telepoints[1].name == "NeoQ2.0 Spawn");
    CHECK(telepoints[1].position_x == 6773.92f);
    CHECK(telepoints[1].position_y == 50.0f);
    CHECK(telepoints[1].position_z == 4216.68f);
}

RoR::CityWorldNeoQ20CompatibilityResult ApplyExact(
    std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    std::vector<RoR::CityWorldNeoQ20Telepoint>& telepoints)
{
    return RoR::ApplyCityWorldNeoQ20Compatibility(
        PinnedDependencies(),
        "CityWorld.tobj",
        PINNED_TOBJ_SHA256,
        placements,
        telepoints);
}

void TestIdentityAndHash()
{
    CHECK(
        std::string(RoR::GetCityWorldNeoQ20PinnedDependency()) ==
        "CityWorld.zip:CityWorld.terrn2:"
        "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3");
    CHECK(
        RoR::ComputeCityWorldNeoQ20Sha256("abc") ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad");
    CHECK(RoR::HasCityWorldNeoQ20PinnedDependency(PinnedDependencies()));

    std::vector<std::string> duplicate = PinnedDependencies();
    duplicate.push_back(RoR::GetCityWorldNeoQ20PinnedDependency());
    CHECK(!RoR::HasCityWorldNeoQ20PinnedDependency(duplicate));
}

void TestAggregateCommitAndFirstMatchCollisionResolution()
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements();
    std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
        ExactTelepoints();

    for (std::size_t service_index = 0U;
         service_index < 3U;
         ++service_index)
    {
        const RoR::CityWorldNeoQ20Placement* resolved =
            ResolveFirstInstance(
                placements,
                ORIGINAL_SERVICE_NAMES[service_index]);
        CHECK(resolved != nullptr);
        if (resolved != nullptr)
        {
            CHECK(resolved->source_line == 387U + service_index);
        }
    }

    const RoR::CityWorldNeoQ20CompatibilityResult result =
        ApplyExact(placements, telepoints);
    CHECK(result.applicable);
    CHECK(result.applied);
    CHECK(result.placement_changed_count == 35U);
    CHECK(result.renamed_instance_count == 3U);
    CHECK(result.telepoint_changed_count == 1U);
    CHECK(result.rejection_reason.empty());
    CheckNeoQ20State(placements, 0.0f, true);
    CHECK(telepoints.size() == 2U);
    CHECK(telepoints[1].position_y == 0.0f);

    for (std::size_t service_index = 0U;
         service_index < 3U;
         ++service_index)
    {
        const RoR::CityWorldNeoQ20Placement* original_resolved =
            ResolveFirstInstance(
                placements,
                ORIGINAL_SERVICE_NAMES[service_index]);
        CHECK(original_resolved != nullptr);
        if (original_resolved != nullptr)
        {
            CHECK(
                original_resolved->source_line ==
                387U + service_index);
        }

        const RoR::CityWorldNeoQ20Placement* grounded_resolved =
            ResolveFirstInstance(
                placements,
                GROUNDED_SERVICE_NAMES[service_index]);
        CHECK(grounded_resolved != nullptr);
        if (grounded_resolved != nullptr)
        {
            CHECK(
                grounded_resolved->source_line ==
                1214U + service_index);
            CHECK(grounded_resolved->position_y == 0.0f);
        }
    }
}

void TestMissingTelepointIsAddedInAggregateCommit()
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements();
    std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints = {
        {"Penguinville Spawn", 436.5f, 0.1f, 446.0f}};
    const RoR::CityWorldNeoQ20CompatibilityResult result =
        ApplyExact(placements, telepoints);
    CHECK(result.applied);
    CHECK(result.placement_changed_count == 35U);
    CHECK(result.renamed_instance_count == 3U);
    CHECK(result.telepoint_changed_count == 1U);
    CheckNeoQ20State(placements, 0.0f, true);
    CHECK(telepoints.size() == 2U);
    CHECK(telepoints[1].name == "NeoQ2.0 Spawn");
    CHECK(telepoints[1].position_y == 0.0f);
}

void TestTelepointFailurePreservesAllPlacements()
{
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        telepoints.push_back(
            {"NeoQ2.0 Spawn", 6773.92f, 50.0f, 4216.68f});
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CHECK(telepoints.size() == 3U);
        CHECK(telepoints[1].position_y == 50.0f);
        CHECK(telepoints[2].position_y == 50.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        telepoints[1].position_y = 49.0f;
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CHECK(telepoints[1].position_y == 49.0f);
    }
}

void TestPlacementFailurePreservesTelepointAndAllOtherPlacements()
{
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        placements.erase(placements.begin() + 20);
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false, 34U);
        CheckTelepointStillAtSource(telepoints);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        FindMutableBySourceLine(placements, 1230U)->position_x += 1.0f;
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CheckTelepointStillAtSource(telepoints);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        FindMutableBySourceLine(placements, 387U)->position_y = 1.0f;
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CheckTelepointStillAtSource(telepoints);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        placements.front().instance_name = GROUNDED_SERVICE_NAMES[0];
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            ApplyExact(placements, telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CheckTelepointStillAtSource(telepoints);
    }
}

void TestAuthenticationMismatchPreservesBothInputs()
{
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Compatibility(
                PinnedDependencies(),
                "CityWorld.tobj",
                std::string(64U, '0'),
                placements,
                telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CheckTelepointStillAtSource(telepoints);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints =
            ExactTelepoints();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Compatibility(
                {},
                "CityWorld.tobj",
                PINNED_TOBJ_SHA256,
                placements,
                telepoints);
        CHECK(!result.applicable);
        CHECK(!result.applied);
        CheckNeoQ20State(placements, 50.0f, false);
        CheckTelepointStillAtSource(telepoints);
    }
}

} // namespace

int main()
{
    TestIdentityAndHash();
    TestAggregateCommitAndFirstMatchCollisionResolution();
    TestMissingTelepointIsAddedInAggregateCommit();
    TestTelepointFailurePreservesAllPlacements();
    TestPlacementFailurePreservesTelepointAndAllOtherPlacements();
    TestAuthenticationMismatchPreservesBothInputs();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "CityWorld NeoQ2.0 compatibility tests passed\n";
    return EXIT_SUCCESS;
}
