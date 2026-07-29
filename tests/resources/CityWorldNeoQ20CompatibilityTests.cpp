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
    const char* instance_name;
    float position_x;
    float position_z;
    float rotation_x;
    float rotation_y;
};

std::vector<RoR::CityWorldNeoQ20Placement> ExactPlacements()
{
    const SourceRow rows[] = {
        {"truckshop", "shop", "spawnZone_truckshop_2",
         6750.0f, 4160.0f, 0.0f, 89.9f},
        {"load-spawner", "sale", "spawnZone_load-spawner_3",
         6800.0f, 4355.0f, 0.0f, 0.0f},
        {"load-spawner", "sale", "spawnZone_load-spawner_4",
         6760.0f, 4355.0f, 0.0f, 0.0f},
        {"NeoQ2-0main-city-section-A", "",
         "auto^CityWorld.tobj(line:1217)",
         7900.0f, 5600.0f, 90.0f, 0.0f},
        {"NeoQ2-0main-city-section-A-1", "",
         "auto^CityWorld.tobj(line:1218)",
         8250.0f, 5450.0f, 90.0f, 0.0f},
        {"NeoQ2-0main-city-section-B", "",
         "auto^CityWorld.tobj(line:1219)",
         6000.0f, 6200.0f, 90.0f, 0.0f},
        {"NeoQ2-0main-city-section-B-1", "",
         "auto^CityWorld.tobj(line:1220)",
         6150.0f, 5750.0f, 90.0f, 0.0f},
        {"NeoQ2-0condos-A", "",
         "auto^CityWorld.tobj(line:1221)",
         7000.0f, 7000.0f, 90.0f, 0.0f},
        {"NeoQ2-0highway-distributor-B-C", "",
         "auto^CityWorld.tobj(line:1222)",
         7000.0f, 7000.0f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-D", "",
         "auto^CityWorld.tobj(line:1223)",
         9000.0f, 6100.0f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-E", "",
         "auto^CityWorld.tobj(line:1224)",
         8400.0f, 4100.0f, 90.0f, 0.0f},
        {"NeoQ2-0sout-center", "",
         "auto^CityWorld.tobj(line:1225)",
         5493.2f, 6601.9f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-A", "",
         "auto^CityWorld.tobj(line:1226)",
         5804.1f, 5410.1f, 90.0f, 0.0f},
        {"NeoQ2-0sout-center-bridge", "",
         "auto^CityWorld.tobj(line:1227)",
         5387.2f, 7040.2f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-D-E-distributor-road", "",
         "auto^CityWorld.tobj(line:1228)",
         8900.0f, 5100.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-oficinas-cuadrante-4", "",
         "auto^CityWorld.tobj(line:1229)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0industrial-zone-distributor-road", "",
         "auto^CityWorld.tobj(line:1230)",
         7000.0f, 4018.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-oficinas-cuadrante-1", "",
         "auto^CityWorld.tobj(line:1231)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-B", "",
         "auto^CityWorld.tobj(line:1232)",
         5900.0f, 7350.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center", "",
         "auto^CityWorld.tobj(line:1233)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-distributor-road", "",
         "auto^CityWorld.tobj(line:1234)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-oficinas-cuadrante-3", "",
         "auto^CityWorld.tobj(line:1235)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-parking", "",
         "auto^CityWorld.tobj(line:1236)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0highway-section-C", "",
         "auto^CityWorld.tobj(line:1237)",
         9000.0f, 7500.0f, 90.0f, 0.0f},
        {"NeoQ2-0industrial-zone", "",
         "auto^CityWorld.tobj(line:1238)",
         7000.0f, 4445.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-oficinas-cuadrante-2", "",
         "auto^CityWorld.tobj(line:1239)",
         7000.0f, 6000.0f, 90.0f, 0.0f},
        {"NeoQ2-0trade-center-bridge", "",
         "auto^CityWorld.tobj(line:1240)",
         7000.0f, 5445.0f, 90.0f, 0.0f},
        {"NeoQ2-0filling-A", "",
         "auto^CityWorld.tobj(line:1241)",
         5993.2f, 7101.9f, 90.0f, 0.0f},
        {"NeoQ2-0filling-B", "",
         "auto^CityWorld.tobj(line:1242)",
         8400.0f, 4600.0f, 90.0f, 0.0f},
        {"NeoQ2-0lough", "",
         "auto^CityWorld.tobj(line:1243)",
         8000.0f, 6600.0f, 90.0f, 0.0f},
        {"NeoQ2-0Building-under-construction", "",
         "auto^CityWorld.tobj(line:1244)",
         6666.0f, 5937.0f, 90.0f, 0.0f},
        {"NeoQ2-0medium-school", "",
         "auto^CityWorld.tobj(line:1245)",
         7357.0f, 7065.0f, 90.0f, 0.0f},
        {"NeoQ2-0high-school", "",
         "auto^CityWorld.tobj(line:1246)",
         7900.0f, 5600.0f, 90.0f, 0.0f},
        {"NeoQ2-0airport", "",
         "auto^CityWorld.tobj(line:1247)",
         5804.1f, 5410.1f, 90.0f, 0.0f},
        {"NeoQ2-0main-filling-cityworld", "",
         "auto^CityWorld.tobj(line:1248)",
         7000.0f, 6000.0f, 90.0f, 0.0f}};

    std::vector<RoR::CityWorldNeoQ20Placement> placements;
    placements.push_back({
        50U, "unrelated", "", "outside", 1.0f, 123.0f, 2.0f,
        0.0f, 0.0f, 0.0f});
    for (std::size_t index = 0U;
         index < sizeof(rows) / sizeof(rows[0]);
         ++index)
    {
        const std::string runtime_instance_name =
            index < 3U
                ? rows[index].instance_name
                : "auto^CityWorld.tobj(line:" +
                    std::to_string(1213U + index) + ")";
        placements.push_back({
            1214U + index,
            rows[index].object_definition,
            rows[index].type,
            runtime_instance_name,
            rows[index].position_x,
            50.0f,
            rows[index].position_z,
            rows[index].rotation_x,
            rows[index].rotation_y,
            0.0f});
    }
    return placements;
}

std::vector<std::string> PinnedDependencies()
{
    return {
        RoR::GetCityWorldNeoQ20PinnedDependency(),
        "Other.zip:Other.terrn2:"
        "0123456789abcdef0123456789abcdef"
        "0123456789abcdef0123456789abcdef"};
}

void CheckAllSourceY(
    const std::vector<RoR::CityWorldNeoQ20Placement>& placements,
    float expected_y)
{
    CHECK(placements.front().position_y == 123.0f);
    for (std::size_t index = 1U; index < placements.size(); ++index)
    {
        CHECK(placements[index].position_y == expected_y);
    }
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

void TestExactGroundingIsAtomicAndComplete()
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements();
    const RoR::CityWorldNeoQ20CompatibilityResult result =
        RoR::ApplyCityWorldNeoQ20Grounding(
            PinnedDependencies(),
            "CityWorld.tobj",
            PINNED_TOBJ_SHA256,
            placements);
    CHECK(result.applicable);
    CHECK(result.applied);
    CHECK(result.changed_count == 35U);
    CHECK(result.rejection_reason.empty());
    CheckAllSourceY(placements, 0.0f);
}

void TestAnySourceMismatchRejectsWithoutPartialMutation()
{
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        placements.erase(placements.begin() + 17);
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Grounding(
                PinnedDependencies(),
                "CityWorld.tobj",
                PINNED_TOBJ_SHA256,
                placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckAllSourceY(placements, 50.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        placements[20].position_x += 1.0f;
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Grounding(
                PinnedDependencies(),
                "CityWorld.tobj",
                PINNED_TOBJ_SHA256,
                placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckAllSourceY(placements, 50.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        placements.push_back(placements[10]);
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Grounding(
                PinnedDependencies(),
                "CityWorld.tobj",
                PINNED_TOBJ_SHA256,
                placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckAllSourceY(placements, 50.0f);
    }
}

void TestAuthenticationMismatchRejectsWithoutMutation()
{
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Grounding(
                PinnedDependencies(),
                "CityWorld.tobj",
                std::string(64U, '0'),
                placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CheckAllSourceY(placements, 50.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20Grounding(
                {},
                "CityWorld.tobj",
                PINNED_TOBJ_SHA256,
                placements);
        CHECK(!result.applicable);
        CHECK(!result.applied);
        CheckAllSourceY(placements, 50.0f);
    }
}

void TestTelepointGrounding()
{
    {
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints = {
            {"Penguinville Spawn", 436.5f, 0.1f, 446.0f},
            {"NeoQ2.0 Spawn", 6773.92f, 50.0f, 4216.68f}};
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20TelepointGrounding(
                true,
                telepoints);
        CHECK(result.applied);
        CHECK(result.changed_count == 1U);
        CHECK(telepoints.size() == 2U);
        CHECK(telepoints[1].position_y == 0.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints = {
            {"Penguinville Spawn", 436.5f, 0.1f, 446.0f}};
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20TelepointGrounding(
                true,
                telepoints);
        CHECK(result.applied);
        CHECK(telepoints.size() == 2U);
        CHECK(telepoints[1].name == "NeoQ2.0 Spawn");
        CHECK(telepoints[1].position_y == 0.0f);
    }
    {
        std::vector<RoR::CityWorldNeoQ20Telepoint> telepoints = {
            {"NeoQ2.0 Spawn", 6773.92f, 49.0f, 4216.68f}};
        const RoR::CityWorldNeoQ20CompatibilityResult result =
            RoR::ApplyCityWorldNeoQ20TelepointGrounding(
                true,
                telepoints);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CHECK(telepoints[0].position_y == 49.0f);
    }
}

} // namespace

int main()
{
    TestIdentityAndHash();
    TestExactGroundingIsAtomicAndComplete();
    TestAnySourceMismatchRejectsWithoutPartialMutation();
    TestAuthenticationMismatchRejectsWithoutMutation();
    TestTelepointGrounding();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "CityWorld NeoQ2.0 compatibility tests passed\n";
    return EXIT_SUCCESS;
}
