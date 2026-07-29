/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Exact compatibility repair for the elevated NeoQ2.0 CityWorld sector.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR
{

struct CityWorldNeoQ20Placement
{
    CityWorldNeoQ20Placement() = default;
    CityWorldNeoQ20Placement(
        std::size_t source_line_value,
        const std::string& object_definition_value,
        const std::string& type_value,
        const std::string& instance_name_value,
        float position_x_value,
        float position_y_value,
        float position_z_value,
        float rotation_x_value,
        float rotation_y_value,
        float rotation_z_value)
        : source_line(source_line_value),
          object_definition(object_definition_value),
          type(type_value),
          instance_name(instance_name_value),
          position_x(position_x_value),
          position_y(position_y_value),
          position_z(position_z_value),
          rotation_x(rotation_x_value),
          rotation_y(rotation_y_value),
          rotation_z(rotation_z_value)
    {
    }

    std::size_t source_line = 0U;
    std::string object_definition;
    std::string type;
    std::string instance_name;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    float rotation_z = 0.0f;
};

struct CityWorldNeoQ20Telepoint
{
    CityWorldNeoQ20Telepoint() = default;
    CityWorldNeoQ20Telepoint(
        const std::string& name_value,
        float position_x_value,
        float position_y_value,
        float position_z_value)
        : name(name_value),
          position_x(position_x_value),
          position_y(position_y_value),
          position_z(position_z_value)
    {
    }

    std::string name;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
};

struct CityWorldNeoQ20CompatibilityResult
{
    bool applicable = false;
    bool applied = false;
    std::size_t placement_changed_count = 0U;
    std::size_t renamed_instance_count = 0U;
    std::size_t telepoint_changed_count = 0U;
    std::string rejection_reason;
};

/// Return the exact authenticated dependency which selects this compatibility
/// policy. CacheSystem verifies the archive bytes before it mounts this entry.
const char* GetCityWorldNeoQ20PinnedDependency();

/// Return true only when the exact pinned CityWorld archive is an authored
/// dependency. Duplicate entries are rejected.
bool HasCityWorldNeoQ20PinnedDependency(
    const std::vector<std::string>& authored_dependencies);

/// Compute a lowercase SHA-256 digest for exact TOBJ authentication.
std::string ComputeCityWorldNeoQ20Sha256(const std::string& payload);

/// Validate and commit the complete NeoQ2.0 compatibility transaction:
/// lower all 35 authenticated placements to y=0, give the three duplicated
/// service placements unique runtime instance names, and ground or add the
/// telepoint. Any placement, duplicate-service, or telepoint mismatch leaves
/// both input vectors byte-for-byte equivalent.
CityWorldNeoQ20CompatibilityResult ApplyCityWorldNeoQ20Compatibility(
    const std::vector<std::string>& authored_dependencies,
    const std::string& tobj_name,
    const std::string& observed_tobj_sha256,
    std::vector<CityWorldNeoQ20Placement>& placements,
    std::vector<CityWorldNeoQ20Telepoint>& telepoints);

} // namespace RoR
