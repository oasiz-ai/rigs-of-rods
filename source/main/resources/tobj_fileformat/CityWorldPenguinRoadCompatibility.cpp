/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "CityWorldPenguinRoadCompatibility.h"

namespace RoR
{
namespace
{

const char PINNED_TOBJ_NAME[] = "CityWorld.tobj";
const char PINNED_TOBJ_SHA256[] =
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48";
const std::size_t SOURCE_LINE = 1354U;
const char LEGACY_OBJECT_DEFINITION[] = "troadavenuesidewalk";
const char REPLACEMENT_OBJECT_DEFINITION[] = "crossroadavenuesidewalk";

CityWorldPenguinRoadCompatibilityResult Rejected(
    bool applicable,
    const char* reason)
{
    CityWorldPenguinRoadCompatibilityResult result;
    result.applicable = applicable;
    result.rejection_reason = reason;
    return result;
}

bool MatchesExactSource(const CityWorldNeoQ20Placement& placement)
{
    const std::string authored_id =
        "auto^CityWorld.tobj(line:1354)";
    const std::string legacy_runtime_id =
        "auto^CityWorld.tobj(line:1353)";
    return placement.source_line == SOURCE_LINE &&
        placement.object_definition == LEGACY_OBJECT_DEFINITION &&
        placement.type.empty() &&
        (placement.instance_name == authored_id ||
         placement.instance_name == legacy_runtime_id) &&
        placement.position_x == 485.0f &&
        placement.position_y == 0.1f &&
        placement.position_z == 370.0f &&
        placement.rotation_x == 0.0f &&
        placement.rotation_y == 90.0f &&
        placement.rotation_z == 0.0f;
}

} // namespace

CityWorldPenguinRoadCompatibilityResult
ApplyCityWorldPenguinRoadCompatibility(
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

    std::size_t selected_index = placements.size();
    std::size_t source_line_count = 0U;
    for (std::size_t index = 0U; index < placements.size(); ++index)
    {
        const CityWorldNeoQ20Placement& placement = placements[index];
        if (placement.source_line != SOURCE_LINE)
        {
            continue;
        }
        ++source_line_count;
        if (MatchesExactSource(placement))
        {
            selected_index = index;
        }
    }
    if (source_line_count != 1U)
    {
        return Rejected(
            true,
            "Penguinville source line is missing or duplicated");
    }
    if (selected_index >= placements.size())
    {
        return Rejected(
            true,
            "Penguinville source placement does not match the exact plan");
    }

    std::vector<CityWorldNeoQ20Placement> committed_placements =
        placements;
    committed_placements[selected_index].object_definition =
        REPLACEMENT_OBJECT_DEFINITION;
    placements.swap(committed_placements);

    CityWorldPenguinRoadCompatibilityResult result;
    result.applicable = true;
    result.applied = true;
    result.replacement_count = 1U;
    return result;
}

} // namespace RoR
