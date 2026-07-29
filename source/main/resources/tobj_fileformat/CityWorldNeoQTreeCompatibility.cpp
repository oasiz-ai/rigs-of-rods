/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "CityWorldNeoQTreeCompatibility.h"

#include <array>

namespace RoR
{
namespace
{

const char PINNED_TOBJ_NAME[] = "CityWorld.tobj";
const char PINNED_TOBJ_SHA256[] =
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48";
const char LEGACY_OBJECT_DEFINITION[] = "arbol1Qr";

struct ExpectedTreeReplacement
{
    std::size_t source_line;
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    const char* variant;
    float scale;
    const char* replacement_object_definition;
    float replacement_yaw_degrees;
};

// This table is the native counterpart of
// rorng_city_neoq_tree_family.v1.json selector.assignments. Source positions
// and rotations are authenticated against exact CityWorld.tobj lines 9-26.
const ExpectedTreeReplacement EXPECTED_REPLACEMENTS[] = {
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

CityWorldNeoQTreeCompatibilityResult Rejected(
    bool applicable,
    const char* reason)
{
    CityWorldNeoQTreeCompatibilityResult result;
    result.applicable = applicable;
    result.rejection_reason = reason;
    return result;
}

bool Matches(
    const CityWorldNeoQ20Placement& observed,
    const ExpectedTreeReplacement& expected)
{
    const std::string authored_id =
        "auto^CityWorld.tobj(line:" +
        std::to_string(expected.source_line) + ")";
    const std::string legacy_runtime_id =
        "auto^CityWorld.tobj(line:" +
        std::to_string(expected.source_line - 1U) + ")";
    return observed.source_line == expected.source_line &&
        observed.object_definition == LEGACY_OBJECT_DEFINITION &&
        observed.type.empty() &&
        (observed.instance_name == authored_id ||
         observed.instance_name == legacy_runtime_id) &&
        observed.position_x == expected.position_x &&
        observed.position_y == expected.position_y &&
        observed.position_z == expected.position_z &&
        observed.rotation_x == expected.rotation_x &&
        observed.rotation_y == expected.rotation_y &&
        observed.rotation_z == expected.rotation_z;
}

} // namespace

CityWorldNeoQTreeCompatibilityResult
ApplyCityWorldNeoQTreeCompatibility(
    const std::vector<std::string>& authored_dependencies,
    const std::string& tobj_name,
    const std::string& observed_tobj_sha256,
    std::vector<CityWorldNeoQ20Placement>& placements)
{
    if (!HasCityWorldNeoQ20PinnedDependency(authored_dependencies))
    {
        return Rejected(false, "pinned CityWorld archive dependency is absent");
    }
    if (tobj_name != PINNED_TOBJ_NAME)
    {
        return Rejected(false, "TOBJ name is not CityWorld.tobj");
    }
    if (observed_tobj_sha256 != PINNED_TOBJ_SHA256)
    {
        return Rejected(true, "CityWorld.tobj SHA-256 is not pinned");
    }

    constexpr std::size_t REPLACEMENT_COUNT =
        sizeof(EXPECTED_REPLACEMENTS) / sizeof(EXPECTED_REPLACEMENTS[0]);
    std::array<std::size_t, REPLACEMENT_COUNT> selected_indexes;
    selected_indexes.fill(placements.size());

    std::size_t legacy_object_count = 0U;
    for (std::size_t placement_index = 0U;
         placement_index < placements.size();
         ++placement_index)
    {
        const CityWorldNeoQ20Placement& placement =
            placements[placement_index];
        if (placement.object_definition == LEGACY_OBJECT_DEFINITION)
        {
            ++legacy_object_count;
        }
        if (placement.source_line >= 9U && placement.source_line <= 26U)
        {
            const std::size_t expected_index =
                placement.source_line - 9U;
            if (selected_indexes[expected_index] != placements.size())
            {
                return Rejected(
                    true,
                    "NeoQ tree source line is duplicated");
            }
            selected_indexes[expected_index] = placement_index;
        }
        for (const ExpectedTreeReplacement& replacement :
             EXPECTED_REPLACEMENTS)
        {
            if (placement.object_definition ==
                replacement.replacement_object_definition)
            {
                return Rejected(
                    true,
                    "replacement ODEF is already used by CityWorld.tobj");
            }
        }
    }
    if (legacy_object_count != REPLACEMENT_COUNT)
    {
        return Rejected(
            true,
            "legacy arbol1Qr placement count is not exactly 18");
    }

    for (std::size_t expected_index = 0U;
         expected_index < selected_indexes.size();
         ++expected_index)
    {
        const std::size_t placement_index =
            selected_indexes[expected_index];
        if (placement_index >= placements.size() ||
            !Matches(
                placements[placement_index],
                EXPECTED_REPLACEMENTS[expected_index]))
        {
            return Rejected(
                true,
                "NeoQ tree source placement does not match the exact plan");
        }
    }

    // Populate a complete commit candidate before the non-throwing swap.
    std::vector<CityWorldNeoQ20Placement> committed_placements =
        placements;
    for (std::size_t expected_index = 0U;
         expected_index < selected_indexes.size();
         ++expected_index)
    {
        CityWorldNeoQ20Placement& replacement =
            committed_placements[selected_indexes[expected_index]];
        replacement.object_definition =
            EXPECTED_REPLACEMENTS[expected_index]
                .replacement_object_definition;
        replacement.rotation_y =
            EXPECTED_REPLACEMENTS[expected_index]
                .replacement_yaw_degrees;
    }
    placements.swap(committed_placements);

    CityWorldNeoQTreeCompatibilityResult result;
    result.applicable = true;
    result.applied = true;
    result.replacement_count = REPLACEMENT_COUNT;
    return result;
}

} // namespace RoR
