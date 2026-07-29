/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace RoR {

enum class TerrainBundleDependencyDiagnosticCode
{
    EMPTY_DEPENDENCY,
    DEPENDENCY_COUNT_LIMIT,
    DEPENDENCY_BYTES_LIMIT,
    MALFORMED_QUALIFIER,
    MISSING_ARCHIVE_SHA256,
    INVALID_ARCHIVE_SHA256,
    UNSAFE_PORTABLE_NAME,
    UNSUPPORTED_BUNDLE_TYPE,
    UNSUPPORTED_TERRAIN_TYPE,
    DUPLICATE_BUNDLE
};

struct TerrainBundleDependencyDiagnostic
{
    TerrainBundleDependencyDiagnosticCode code;
    std::size_t dependency_index;
    std::string detail;
};

/// A terrain bundle selected by an exact, portable
/// "bundle.zip:terrain.terrn2:<sha256>" identity.
struct TerrainBundleDependency
{
    std::string authored_name;
    std::string bundle_name;
    std::string terrain_filename;
    std::string expected_archive_sha256;
};

struct TerrainBundleDependencyPlan
{
    std::vector<TerrainBundleDependency> dependencies;
    std::vector<TerrainBundleDependencyDiagnostic> diagnostics;

    bool IsValid() const { return diagnostics.empty(); }
};

/// Validates a bounded dependency list without filesystem or OGRE access.
///
/// Dependencies are intentionally limited to exact root-level terrain entries
/// in ZIP bundles. This prevents ambiguous partial lookup, paths, platform
/// filename differences, and dependency-driven host access.
TerrainBundleDependencyPlan BuildTerrainBundleDependencyPlan(
    const std::vector<std::string>& authored_dependencies);

const char* TerrainBundleDependencyDiagnosticCodeToString(
    TerrainBundleDependencyDiagnosticCode code);

} // namespace RoR
