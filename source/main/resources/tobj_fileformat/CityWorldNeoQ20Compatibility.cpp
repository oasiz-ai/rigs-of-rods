/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "CityWorldNeoQ20Compatibility.h"

#include <array>
#include <openssl/evp.h>

namespace RoR
{
namespace
{

const char PINNED_DEPENDENCY[] =
    "CityWorld.zip:CityWorld.terrn2:"
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";
const char PINNED_TOBJ_NAME[] = "CityWorld.tobj";
const char PINNED_TOBJ_SHA256[] =
    "1cdc57dc59c4c0f403f621ad31afc301436af70c813b2e0dd01ffb0cd54f0b48";
const char TELEPOINT_NAME[] = "NeoQ2.0 Spawn";
const char* const GROUNDED_SERVICE_INSTANCE_NAMES[] = {
    "spawnZone_neoq20_truckshop_2",
    "spawnZone_neoq20_load-spawner_3",
    "spawnZone_neoq20_load-spawner_4"};

struct ExpectedPlacement
{
    std::size_t source_line;
    const char* object_definition;
    const char* type;
    const char* instance_name;
    float position_x;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
};

struct ExpectedDuplicateServicePlacement
{
    std::size_t source_line;
    const char* object_definition;
    const char* type;
    const char* instance_name;
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
};

const ExpectedPlacement EXPECTED_PLACEMENTS[] = {
    {1214U, "truckshop", "shop", "spawnZone_truckshop_2",
     6750.0f, 4160.0f, 0.0f, 89.9f, 0.0f},
    {1215U, "load-spawner", "sale", "spawnZone_load-spawner_3",
     6800.0f, 4355.0f, 0.0f, 0.0f, 0.0f},
    {1216U, "load-spawner", "sale", "spawnZone_load-spawner_4",
     6760.0f, 4355.0f, 0.0f, 0.0f, 0.0f},
    {1217U, "NeoQ2-0main-city-section-A", "",
     "auto^CityWorld.tobj(line:1217)",
     7900.0f, 5600.0f, 90.0f, 0.0f, 0.0f},
    {1218U, "NeoQ2-0main-city-section-A-1", "",
     "auto^CityWorld.tobj(line:1218)",
     8250.0f, 5450.0f, 90.0f, 0.0f, 0.0f},
    {1219U, "NeoQ2-0main-city-section-B", "",
     "auto^CityWorld.tobj(line:1219)",
     6000.0f, 6200.0f, 90.0f, 0.0f, 0.0f},
    {1220U, "NeoQ2-0main-city-section-B-1", "",
     "auto^CityWorld.tobj(line:1220)",
     6150.0f, 5750.0f, 90.0f, 0.0f, 0.0f},
    {1221U, "NeoQ2-0condos-A", "",
     "auto^CityWorld.tobj(line:1221)",
     7000.0f, 7000.0f, 90.0f, 0.0f, 0.0f},
    {1222U, "NeoQ2-0highway-distributor-B-C", "",
     "auto^CityWorld.tobj(line:1222)",
     7000.0f, 7000.0f, 90.0f, 0.0f, 0.0f},
    {1223U, "NeoQ2-0highway-section-D", "",
     "auto^CityWorld.tobj(line:1223)",
     9000.0f, 6100.0f, 90.0f, 0.0f, 0.0f},
    {1224U, "NeoQ2-0highway-section-E", "",
     "auto^CityWorld.tobj(line:1224)",
     8400.0f, 4100.0f, 90.0f, 0.0f, 0.0f},
    {1225U, "NeoQ2-0sout-center", "",
     "auto^CityWorld.tobj(line:1225)",
     5493.2f, 6601.9f, 90.0f, 0.0f, 0.0f},
    {1226U, "NeoQ2-0highway-section-A", "",
     "auto^CityWorld.tobj(line:1226)",
     5804.1f, 5410.1f, 90.0f, 0.0f, 0.0f},
    {1227U, "NeoQ2-0sout-center-bridge", "",
     "auto^CityWorld.tobj(line:1227)",
     5387.2f, 7040.2f, 90.0f, 0.0f, 0.0f},
    {1228U, "NeoQ2-0highway-section-D-E-distributor-road", "",
     "auto^CityWorld.tobj(line:1228)",
     8900.0f, 5100.0f, 90.0f, 0.0f, 0.0f},
    {1229U, "NeoQ2-0trade-center-oficinas-cuadrante-4", "",
     "auto^CityWorld.tobj(line:1229)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1230U, "NeoQ2-0industrial-zone-distributor-road", "",
     "auto^CityWorld.tobj(line:1230)",
     7000.0f, 4018.0f, 90.0f, 0.0f, 0.0f},
    {1231U, "NeoQ2-0trade-center-oficinas-cuadrante-1", "",
     "auto^CityWorld.tobj(line:1231)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1232U, "NeoQ2-0highway-section-B", "",
     "auto^CityWorld.tobj(line:1232)",
     5900.0f, 7350.0f, 90.0f, 0.0f, 0.0f},
    {1233U, "NeoQ2-0trade-center", "",
     "auto^CityWorld.tobj(line:1233)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1234U, "NeoQ2-0trade-center-distributor-road", "",
     "auto^CityWorld.tobj(line:1234)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1235U, "NeoQ2-0trade-center-oficinas-cuadrante-3", "",
     "auto^CityWorld.tobj(line:1235)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1236U, "NeoQ2-0trade-center-parking", "",
     "auto^CityWorld.tobj(line:1236)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1237U, "NeoQ2-0highway-section-C", "",
     "auto^CityWorld.tobj(line:1237)",
     9000.0f, 7500.0f, 90.0f, 0.0f, 0.0f},
    {1238U, "NeoQ2-0industrial-zone", "",
     "auto^CityWorld.tobj(line:1238)",
     7000.0f, 4445.0f, 90.0f, 0.0f, 0.0f},
    {1239U, "NeoQ2-0trade-center-oficinas-cuadrante-2", "",
     "auto^CityWorld.tobj(line:1239)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f},
    {1240U, "NeoQ2-0trade-center-bridge", "",
     "auto^CityWorld.tobj(line:1240)",
     7000.0f, 5445.0f, 90.0f, 0.0f, 0.0f},
    {1241U, "NeoQ2-0filling-A", "",
     "auto^CityWorld.tobj(line:1241)",
     5993.2f, 7101.9f, 90.0f, 0.0f, 0.0f},
    {1242U, "NeoQ2-0filling-B", "",
     "auto^CityWorld.tobj(line:1242)",
     8400.0f, 4600.0f, 90.0f, 0.0f, 0.0f},
    {1243U, "NeoQ2-0lough", "",
     "auto^CityWorld.tobj(line:1243)",
     8000.0f, 6600.0f, 90.0f, 0.0f, 0.0f},
    {1244U, "NeoQ2-0Building-under-construction", "",
     "auto^CityWorld.tobj(line:1244)",
     6666.0f, 5937.0f, 90.0f, 0.0f, 0.0f},
    {1245U, "NeoQ2-0medium-school", "",
     "auto^CityWorld.tobj(line:1245)",
     7357.0f, 7065.0f, 90.0f, 0.0f, 0.0f},
    {1246U, "NeoQ2-0high-school", "",
     "auto^CityWorld.tobj(line:1246)",
     7900.0f, 5600.0f, 90.0f, 0.0f, 0.0f},
    {1247U, "NeoQ2-0airport", "",
     "auto^CityWorld.tobj(line:1247)",
     5804.1f, 5410.1f, 90.0f, 0.0f, 0.0f},
    {1248U, "NeoQ2-0main-filling-cityworld", "",
     "auto^CityWorld.tobj(line:1248)",
     7000.0f, 6000.0f, 90.0f, 0.0f, 0.0f}};

// These earlier NeoQueretaro service objects use the same authored instance
// names as NeoQ2.0 lines 1214-1216. Collisions::getBox()/getPosition() scan
// event sources from the beginning, so keeping the duplicate names on the
// grounded objects would resolve queries to these earlier objects.
const ExpectedDuplicateServicePlacement EXPECTED_DUPLICATE_SERVICES[] = {
    {387U, "truckshop", "shop", "spawnZone_truckshop_2",
     2440.896973f, 0.85f, 1020.876587f, 0.0f, 0.0f, 0.0f},
    {388U, "load-spawner", "sale", "spawnZone_load-spawner_3",
     2402.270020f, 0.351947f, 1079.235352f, 0.0f, 0.0f, 0.0f},
    {389U, "load-spawner", "sale", "spawnZone_load-spawner_4",
     2402.270020f, 0.351947f, 1039.235352f, 0.0f, 0.0f, 0.0f}};

CityWorldNeoQ20CompatibilityResult Rejected(
    bool applicable,
    const char* reason)
{
    CityWorldNeoQ20CompatibilityResult result;
    result.applicable = applicable;
    result.rejection_reason = reason;
    return result;
}

bool Matches(
    const CityWorldNeoQ20Placement& observed,
    const ExpectedPlacement& expected)
{
    bool instance_name_matches =
        observed.instance_name == expected.instance_name;
    if (expected.source_line >= 1217U)
    {
        // TObj autogenerated names predate TObjEntry::source_line and expose
        // the parser's zero-based counter. Authenticate both the one-based
        // authored line recorded in the plan and its legacy object ID.
        const std::string authored_id =
            "auto^CityWorld.tobj(line:" +
            std::to_string(expected.source_line) + ")";
        const std::string legacy_runtime_id =
            "auto^CityWorld.tobj(line:" +
            std::to_string(expected.source_line - 1U) + ")";
        instance_name_matches =
            expected.instance_name == authored_id &&
            observed.instance_name == legacy_runtime_id;
    }

    return observed.source_line == expected.source_line &&
        observed.object_definition == expected.object_definition &&
        observed.type == expected.type &&
        instance_name_matches &&
        observed.position_x == expected.position_x &&
        observed.position_y == 50.0f &&
        observed.position_z == expected.position_z &&
        observed.rotation_x == expected.rotation_x &&
        observed.rotation_y == expected.rotation_y &&
        observed.rotation_z == expected.rotation_z;
}

bool MatchesDuplicateService(
    const CityWorldNeoQ20Placement& observed,
    const ExpectedDuplicateServicePlacement& expected)
{
    return observed.source_line == expected.source_line &&
        observed.object_definition == expected.object_definition &&
        observed.type == expected.type &&
        observed.instance_name == expected.instance_name &&
        observed.position_x == expected.position_x &&
        observed.position_y == expected.position_y &&
        observed.position_z == expected.position_z &&
        observed.rotation_x == expected.rotation_x &&
        observed.rotation_y == expected.rotation_y &&
        observed.rotation_z == expected.rotation_z;
}

} // namespace

const char* GetCityWorldNeoQ20PinnedDependency()
{
    return PINNED_DEPENDENCY;
}

bool HasCityWorldNeoQ20PinnedDependency(
    const std::vector<std::string>& authored_dependencies)
{
    std::size_t matches = 0U;
    for (const std::string& dependency : authored_dependencies)
    {
        if (dependency == PINNED_DEPENDENCY)
        {
            ++matches;
        }
    }
    return matches == 1U;
}

std::string ComputeCityWorldNeoQ20Sha256(const std::string& payload)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_Digest(
            payload.data(),
            payload.size(),
            digest.data(),
            &digest_size,
            EVP_sha256(),
            nullptr) != 1 ||
        digest_size != 32U)
    {
        return std::string();
    }

    static const char HEX_DIGITS[] = "0123456789abcdef";
    std::string result(digest_size * 2U, '0');
    for (unsigned int index = 0U; index < digest_size; ++index)
    {
        result[index * 2U] = HEX_DIGITS[digest[index] >> 4U];
        result[index * 2U + 1U] = HEX_DIGITS[digest[index] & 0x0fU];
    }
    return result;
}

CityWorldNeoQ20CompatibilityResult ApplyCityWorldNeoQ20Compatibility(
    const std::vector<std::string>& authored_dependencies,
    const std::string& tobj_name,
    const std::string& observed_tobj_sha256,
    std::vector<CityWorldNeoQ20Placement>& placements,
    std::vector<CityWorldNeoQ20Telepoint>& telepoints)
{
    if (!HasCityWorldNeoQ20PinnedDependency(authored_dependencies))
    {
        return Rejected(false, "pinned CityWorld dependency is absent");
    }
    if (tobj_name != PINNED_TOBJ_NAME)
    {
        return Rejected(false, "TOBJ name is not CityWorld.tobj");
    }
    if (observed_tobj_sha256 != PINNED_TOBJ_SHA256)
    {
        return Rejected(true, "CityWorld.tobj SHA-256 mismatch");
    }

    std::array<std::size_t,
        sizeof(EXPECTED_PLACEMENTS) / sizeof(EXPECTED_PLACEMENTS[0])>
        selected_indexes;
    selected_indexes.fill(placements.size());
    std::array<std::size_t,
        sizeof(EXPECTED_DUPLICATE_SERVICES) /
            sizeof(EXPECTED_DUPLICATE_SERVICES[0])>
        duplicate_service_indexes;
    duplicate_service_indexes.fill(placements.size());

    std::size_t source_range_count = 0U;
    for (std::size_t placement_index = 0U;
         placement_index < placements.size();
         ++placement_index)
    {
        const CityWorldNeoQ20Placement& placement =
            placements[placement_index];
        if (placement.source_line >= 1214U &&
            placement.source_line <= 1248U)
        {
            ++source_range_count;
            const std::size_t expected_index =
                placement.source_line - 1214U;
            if (expected_index >= selected_indexes.size() ||
                selected_indexes[expected_index] != placements.size())
            {
                return Rejected(
                    true,
                    "NeoQ2.0 source line is duplicated or outside the plan");
            }
            selected_indexes[expected_index] = placement_index;
        }

        for (std::size_t duplicate_index = 0U;
             duplicate_index < duplicate_service_indexes.size();
             ++duplicate_index)
        {
            if (placement.source_line !=
                EXPECTED_DUPLICATE_SERVICES[duplicate_index].source_line)
            {
                continue;
            }
            if (duplicate_service_indexes[duplicate_index] !=
                placements.size())
            {
                return Rejected(
                    true,
                    "earlier duplicate service source line is duplicated");
            }
            duplicate_service_indexes[duplicate_index] = placement_index;
        }

        for (const char* grounded_name :
             GROUNDED_SERVICE_INSTANCE_NAMES)
        {
            if (placement.instance_name == grounded_name)
            {
                return Rejected(
                    true,
                    "grounded service instance name is already in use");
            }
        }
    }
    if (source_range_count != selected_indexes.size())
    {
        return Rejected(
            true,
            "NeoQ2.0 source placement count is not exactly 35");
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
                EXPECTED_PLACEMENTS[expected_index]))
        {
            return Rejected(
                true,
                "NeoQ2.0 source placement does not match the exact plan");
        }
    }

    for (std::size_t duplicate_index = 0U;
         duplicate_index < duplicate_service_indexes.size();
         ++duplicate_index)
    {
        const std::size_t placement_index =
            duplicate_service_indexes[duplicate_index];
        if (placement_index >= placements.size() ||
            !MatchesDuplicateService(
                placements[placement_index],
                EXPECTED_DUPLICATE_SERVICES[duplicate_index]))
        {
            return Rejected(
                true,
                "earlier duplicate service does not match the exact source");
        }
    }

    std::size_t telepoint_index = telepoints.size();
    for (std::size_t index = 0U; index < telepoints.size(); ++index)
    {
        if (telepoints[index].name != TELEPOINT_NAME)
        {
            continue;
        }
        if (telepoint_index != telepoints.size())
        {
            return Rejected(true, "NeoQ2.0 telepoint is duplicated");
        }
        telepoint_index = index;
    }

    if (telepoint_index != telepoints.size())
    {
        const CityWorldNeoQ20Telepoint& telepoint =
            telepoints[telepoint_index];
        if (telepoint.position_x != 6773.92f ||
            telepoint.position_y != 50.0f ||
            telepoint.position_z != 4216.68f)
        {
            return Rejected(
                true,
                "NeoQ2.0 telepoint does not match the exact source");
        }
    }

    // Allocate and populate both commit candidates before mutating either
    // caller-owned vector. The two final swaps are non-throwing.
    std::vector<CityWorldNeoQ20Placement> committed_placements =
        placements;
    std::vector<CityWorldNeoQ20Telepoint> committed_telepoints =
        telepoints;
    for (const std::size_t placement_index : selected_indexes)
    {
        committed_placements[placement_index].position_y = 0.0f;
    }
    for (std::size_t service_index = 0U;
         service_index <
            sizeof(GROUNDED_SERVICE_INSTANCE_NAMES) /
                sizeof(GROUNDED_SERVICE_INSTANCE_NAMES[0]);
         ++service_index)
    {
        committed_placements[selected_indexes[service_index]]
            .instance_name =
            GROUNDED_SERVICE_INSTANCE_NAMES[service_index];
    }
    if (telepoint_index == telepoints.size())
    {
        committed_telepoints.push_back({
            TELEPOINT_NAME,
            6773.92f,
            0.0f,
            4216.68f});
    }
    else
    {
        committed_telepoints[telepoint_index].position_y = 0.0f;
    }
    placements.swap(committed_placements);
    telepoints.swap(committed_telepoints);

    CityWorldNeoQ20CompatibilityResult result;
    result.applicable = true;
    result.applied = true;
    result.placement_changed_count = selected_indexes.size();
    result.renamed_instance_count =
        sizeof(GROUNDED_SERVICE_INSTANCE_NAMES) /
        sizeof(GROUNDED_SERVICE_INSTANCE_NAMES[0]);
    result.telepoint_changed_count = 1U;
    return result;
}

} // namespace RoR
