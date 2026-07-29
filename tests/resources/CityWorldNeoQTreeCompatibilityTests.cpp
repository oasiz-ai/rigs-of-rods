#include "CityWorldNeoQTreeCompatibility.h"

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

struct Expected
{
    std::size_t source_line;
    float x;
    float y;
    float z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    const char* variant;
    float scale;
    const char* replacement;
    float replacement_yaw;
};

const Expected EXPECTED[] = {
#define CITYWORLD_NEOQ_TREE_REPLACEMENT(                              \
    source_line, position_x, position_y, position_z,                   \
    rotation_x, rotation_y, rotation_z, variant, scale,                \
    replacement_object_definition, replacement_yaw_degrees)           \
    {source_line, position_x, position_y, position_z,                   \
     rotation_x, rotation_y, rotation_z, variant, scale,               \
     replacement_object_definition, replacement_yaw_degrees},
#include "CityWorldNeoQTreePlan.inc"
#undef CITYWORLD_NEOQ_TREE_REPLACEMENT
};

std::vector<std::string> PinnedDependencies()
{
    return {RoR::GetCityWorldNeoQ20PinnedDependency()};
}

std::vector<RoR::CityWorldNeoQ20Placement> ExactPlacements(
    bool authored_instance_ids = false)
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements;
    placements.push_back({
        5U,
        "unrelated",
        "",
        "unrelated-instance",
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        5.0f,
        6.0f});
    for (const Expected& expected : EXPECTED)
    {
        const std::size_t instance_line =
            authored_instance_ids
                ? expected.source_line
                : expected.source_line - 1U;
        placements.push_back({
            expected.source_line,
            "arbol1Qr",
            "",
            "auto^CityWorld.tobj(line:" +
                std::to_string(instance_line) + ")",
            expected.x,
            expected.y,
            expected.z,
            expected.rotation_x,
            expected.rotation_y,
            expected.rotation_z});
    }
    placements.push_back({
        30U,
        "another-object",
        "sign",
        "another-instance",
        7.0f,
        8.0f,
        9.0f,
        10.0f,
        11.0f,
        12.0f});
    return placements;
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

RoR::CityWorldNeoQTreeCompatibilityResult Apply(
    std::vector<RoR::CityWorldNeoQ20Placement>& placements)
{
    return RoR::ApplyCityWorldNeoQTreeCompatibility(
        PinnedDependencies(),
        "CityWorld.tobj",
        "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48",
        placements);
}

void CheckExactApplication(bool authored_instance_ids)
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements(authored_instance_ids);
    const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
    const RoR::CityWorldNeoQTreeCompatibilityResult result =
        Apply(placements);
    CHECK(result.applicable);
    CHECK(result.applied);
    CHECK(result.replacement_count == 18U);
    CHECK(result.rejection_reason.empty());
    CHECK(placements.size() == before.size());

    CHECK(Equivalent(
        {placements.front(), placements.back()},
        {before.front(), before.back()}));
    for (std::size_t index = 0U; index < 18U; ++index)
    {
        const RoR::CityWorldNeoQ20Placement& replacement =
            placements[index + 1U];
        const RoR::CityWorldNeoQ20Placement& original =
            before[index + 1U];
        CHECK(replacement.source_line == original.source_line);
        CHECK(replacement.object_definition == EXPECTED[index].replacement);
        CHECK(replacement.type == original.type);
        CHECK(replacement.instance_name == original.instance_name);
        CHECK(replacement.position_x == original.position_x);
        CHECK(replacement.position_y == original.position_y);
        CHECK(replacement.position_z == original.position_z);
        CHECK(replacement.rotation_x == original.rotation_x);
        CHECK(replacement.rotation_y == EXPECTED[index].replacement_yaw);
        CHECK(replacement.rotation_z == original.rotation_z);
    }
}

void CheckRejectedMutation(
    const std::function<void(
        std::vector<RoR::CityWorldNeoQ20Placement>&)>& mutate)
{
    std::vector<RoR::CityWorldNeoQ20Placement> placements =
        ExactPlacements();
    mutate(placements);
    const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
    const RoR::CityWorldNeoQTreeCompatibilityResult result =
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
        const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
        const RoR::CityWorldNeoQTreeCompatibilityResult result =
            RoR::ApplyCityWorldNeoQTreeCompatibility(
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
        const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
        const RoR::CityWorldNeoQTreeCompatibilityResult result =
            RoR::ApplyCityWorldNeoQTreeCompatibility(
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
        const std::vector<RoR::CityWorldNeoQ20Placement> before = placements;
        const RoR::CityWorldNeoQTreeCompatibilityResult result =
            RoR::ApplyCityWorldNeoQTreeCompatibility(
                PinnedDependencies(),
                "CityWorld.tobj",
                std::string(64U, '0'),
                placements);
        CHECK(result.applicable);
        CHECK(!result.applied);
        CHECK(Equivalent(placements, before));
    }

    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].source_line = 10U;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].object_definition = "not-arbol1Qr";
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].type = "tree";
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].instance_name = "wrong-instance";
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].position_x += 0.001f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].position_y += 0.001f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].position_z += 0.001f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].rotation_x += 1.0f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].rotation_y += 1.0f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements[1U].rotation_z += 1.0f;
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements.push_back({
            100U,
            "arbol1Qr",
            "",
            "extra-tree",
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f});
    });
    CheckRejectedMutation([](
        std::vector<RoR::CityWorldNeoQ20Placement>& placements)
    {
        placements.push_back({
            100U,
            "rorng_city_neoq_tree_instance_00",
            "",
            "replacement-collision",
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f});
    });

    std::cout << "CityWorld NeoQ tree compatibility tests passed\n";
    return 0;
}
