#include "CityWorldPenguinRoadCompatibility.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace
{

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__  \
                      << ": " #condition "\n";                              \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

std::vector<std::string> PinnedDependencies()
{
    return {RoR::GetCityWorldNeoQ20PinnedDependency()};
}

RoR::CityWorldNeoQ20Placement ExactSource(bool authored_instance_id = false)
{
    return {
        1354U,
        "troadavenuesidewalk",
        "",
        authored_instance_id
            ? "auto^CityWorld.tobj(line:1354)"
            : "auto^CityWorld.tobj(line:1353)",
        485.0f,
        0.1f,
        370.0f,
        0.0f,
        90.0f,
        0.0f};
}

std::vector<RoR::CityWorldNeoQ20Placement> ExactPlacements(
    bool authored_instance_id = false)
{
    return {
        {
            1353U,
            "unrelated",
            "",
            "unrelated-before",
            1.0f,
            2.0f,
            3.0f,
            4.0f,
            5.0f,
            6.0f,
        },
        ExactSource(authored_instance_id),
        {
            1355U,
            "another-object",
            "sign",
            "unrelated-after",
            7.0f,
            8.0f,
            9.0f,
            10.0f,
            11.0f,
            12.0f,
        },
    };
}

bool Equivalent(
    const std::vector<RoR::CityWorldNeoQ20Placement>& left,
    const std::vector<RoR::CityWorldNeoQ20Placement>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        const RoR::CityWorldNeoQ20Placement& a = left[index];
        const RoR::CityWorldNeoQ20Placement& b = right[index];
        if (a.source_line != b.source_line ||
            a.object_definition != b.object_definition ||
            a.type != b.type ||
            a.instance_name != b.instance_name ||
            a.position_x != b.position_x ||
            a.position_y != b.position_y ||
            a.position_z != b.position_z ||
            a.rotation_x != b.rotation_x ||
            a.rotation_y != b.rotation_y ||
            a.rotation_z != b.rotation_z)
        {
            return false;
        }
    }
    return true;
}

RoR::CityWorldPenguinRoadCompatibilityResult Apply(
    std::vector<RoR::CityWorldNeoQ20Placement>& placements)
{
    return RoR::ApplyCityWorldPenguinRoadCompatibility(
        PinnedDependencies(),
        "CityWorld.tobj",
        "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48",
        placements);
}

void CheckExactApplication(bool authored_instance_id)
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements(authored_instance_id);
    const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
    const RoR::CityWorldPenguinRoadCompatibilityResult result =
        Apply(placements);
    CHECK(result.applicable);
    CHECK(result.applied);
    CHECK(result.replacement_count == 1U);
    CHECK(result.rejection_reason.empty());
    CHECK(placements.size() == before.size());
    CHECK(placements[1U].object_definition ==
        "crossroadavenuesidewalk");
    CHECK(placements[1U].source_line == before[1U].source_line);
    CHECK(placements[1U].type == before[1U].type);
    CHECK(placements[1U].instance_name == before[1U].instance_name);
    CHECK(placements[1U].position_x == before[1U].position_x);
    CHECK(placements[1U].position_y == before[1U].position_y);
    CHECK(placements[1U].position_z == before[1U].position_z);
    CHECK(placements[1U].rotation_x == before[1U].rotation_x);
    CHECK(placements[1U].rotation_y == before[1U].rotation_y);
    CHECK(placements[1U].rotation_z == before[1U].rotation_z);
    CHECK(Equivalent(
        {placements.front(), placements.back()},
        {before.front(), before.back()}));
}

void CheckRejectedMutation(
    const std::function<void(
        std::vector<RoR::CityWorldNeoQ20Placement>&)>& mutate)
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements();
    mutate(placements);
    const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
    const RoR::CityWorldPenguinRoadCompatibilityResult result =
        Apply(placements);
    CHECK(result.applicable);
    CHECK(!result.applied);
    CHECK(result.replacement_count == 0U);
    CHECK(!result.rejection_reason.empty());
    CHECK(Equivalent(placements, before));
}

} // namespace

int main()
{
    CheckExactApplication(false);
    CheckExactApplication(true);

    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        const std::vector<RoR::CityWorldNeoQ20Placement> before =
            placements;
        const auto result = RoR::ApplyCityWorldPenguinRoadCompatibility(
            {},
            "CityWorld.tobj",
            "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48",
            placements);
        CHECK(!result.applicable);
        CHECK(!result.applied);
        CHECK(Equivalent(placements, before));
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        const std::vector<RoR::CityWorldNeoQ20Placement> before =
            placements;
        const auto result = RoR::ApplyCityWorldPenguinRoadCompatibility(
            PinnedDependencies(),
            "other.tobj",
            "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48",
            placements);
        CHECK(!result.applicable);
        CHECK(!result.applied);
        CHECK(Equivalent(placements, before));
    }
    {
        std::vector<RoR::CityWorldNeoQ20Placement> placements =
            ExactPlacements();
        const std::vector<RoR::CityWorldNeoQ20Placement> before =
            placements;
        const auto result = RoR::ApplyCityWorldPenguinRoadCompatibility(
            PinnedDependencies(),
            "CityWorld.tobj",
            std::string(64U, '0'),
            placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CHECK(Equivalent(placements, before));
    }

    using Placements = std::vector<RoR::CityWorldNeoQ20Placement>;
    CheckRejectedMutation([](Placements& value) {
        value.erase(value.begin() + 1U);
    });
    CheckRejectedMutation([](Placements& value) {
        value.push_back(ExactSource());
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].source_line = 1356U;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].object_definition = "crossroadavenuesidewalk";
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].type = "road";
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].instance_name = "wrong-instance";
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].position_x += 0.001f;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].position_y += 0.001f;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].position_z += 0.001f;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].rotation_x += 1.0f;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].rotation_y += 1.0f;
    });
    CheckRejectedMutation([](Placements& value) {
        value[1U].rotation_z += 1.0f;
    });

    std::cout << "CityWorld Penguin road compatibility tests passed\n";
    return 0;
}
