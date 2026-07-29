/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "TerrainBundleDependency.h"

#include <set>

namespace RoR {
namespace {

const std::size_t MAX_DEPENDENCIES = 8U;
const std::size_t MAX_DEPENDENCY_BYTES = 512U;
const std::size_t MAX_TOTAL_DEPENDENCY_BYTES = 2048U;
const std::size_t MAX_PORTABLE_NAME_BYTES = 255U;

bool IsAsciiAlphaNumeric(char value)
{
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9');
}

char AsciiLower(char value)
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string AsciiLowerString(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        result.push_back(AsciiLower(value[index]));
    }
    return result;
}

bool EndsWithAsciiCaseInsensitive(
    const std::string& value,
    const std::string& suffix)
{
    if (value.size() < suffix.size())
    {
        return false;
    }
    const std::size_t offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index)
    {
        if (AsciiLower(value[offset + index]) !=
            AsciiLower(suffix[index]))
        {
            return false;
        }
    }
    return true;
}

bool IsLowercaseSha256(const std::string& value)
{
    if (value.size() != 64U)
    {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool IsPortableRootFilename(const std::string& value)
{
    if (value.empty() ||
        value.size() > MAX_PORTABLE_NAME_BYTES ||
        !IsAsciiAlphaNumeric(value.front()) ||
        value.back() == ' ' ||
        value.back() == '.')
    {
        return false;
    }
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        const unsigned char byte =
            static_cast<unsigned char>(value[index]);
        if (byte < 32U || byte > 126U)
        {
            return false;
        }
        switch (value[index])
        {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            break;
        }
    }
    const std::size_t extension = value.find('.');
    const std::string stem = AsciiLowerString(
        value.substr(0U, extension));
    if (stem == "con" ||
        stem == "prn" ||
        stem == "aux" ||
        stem == "nul" ||
        stem == "clock$" ||
        stem == "conin$" ||
        stem == "conout$" ||
        (stem.size() == 4U &&
         (stem.substr(0U, 3U) == "com" ||
          stem.substr(0U, 3U) == "lpt") &&
         stem[3] >= '1' &&
         stem[3] <= '9'))
    {
        return false;
    }
    return true;
}

void AddDiagnostic(
    TerrainBundleDependencyPlan& plan,
    TerrainBundleDependencyDiagnosticCode code,
    std::size_t index,
    const std::string& detail)
{
    TerrainBundleDependencyDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.dependency_index = index;
    diagnostic.detail = detail;
    plan.diagnostics.push_back(diagnostic);
}

} // namespace

TerrainBundleDependencyPlan BuildTerrainBundleDependencyPlan(
    const std::vector<std::string>& authored_dependencies)
{
    TerrainBundleDependencyPlan plan;
    if (authored_dependencies.size() > MAX_DEPENDENCIES)
    {
        AddDiagnostic(
            plan,
            TerrainBundleDependencyDiagnosticCode::
                DEPENDENCY_COUNT_LIMIT,
            MAX_DEPENDENCIES,
            "terrain resource bundle dependency count exceeds eight");
        return plan;
    }

    std::size_t total_bytes = 0U;
    for (std::size_t index = 0U;
         index < authored_dependencies.size();
         ++index)
    {
        const std::string& authored = authored_dependencies[index];
        if (authored.size() > MAX_DEPENDENCY_BYTES ||
            total_bytes >
                MAX_TOTAL_DEPENDENCY_BYTES - authored.size())
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    DEPENDENCY_BYTES_LIMIT,
                index,
                "terrain resource bundle dependencies exceed byte limits");
            return plan;
        }
        total_bytes += authored.size();
    }

    std::set<std::string> seen_bundles;
    for (std::size_t index = 0U;
         index < authored_dependencies.size();
         ++index)
    {
        const std::string& authored = authored_dependencies[index];
        if (authored.empty())
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::EMPTY_DEPENDENCY,
                index,
                "terrain resource bundle dependency is empty");
            continue;
        }

        const std::size_t bundle_separator = authored.find(':');
        if (bundle_separator == std::string::npos ||
            bundle_separator == 0U)
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    MALFORMED_QUALIFIER,
                index,
                "dependency must use exact "
                "bundle.zip:terrain.terrn2:<sha256> syntax");
            continue;
        }

        const std::size_t hash_separator = authored.find(
            ':', bundle_separator + 1U);
        if (hash_separator == std::string::npos)
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    MISSING_ARCHIVE_SHA256,
                index,
                "terrain resource dependency requires an archive SHA-256");
            continue;
        }
        if (bundle_separator + 1U == hash_separator ||
            authored.find(':', hash_separator + 1U) != std::string::npos)
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    MALFORMED_QUALIFIER,
                index,
                "dependency must use exact "
                "bundle.zip:terrain.terrn2:<sha256> syntax");
            continue;
        }

        const std::string bundle = authored.substr(0U, bundle_separator);
        const std::string terrain = authored.substr(
            bundle_separator + 1U,
            hash_separator - bundle_separator - 1U);
        const std::string expected_sha256 = authored.substr(
            hash_separator + 1U);
        if (expected_sha256.empty())
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    MISSING_ARCHIVE_SHA256,
                index,
                "terrain resource dependency requires an archive SHA-256");
            continue;
        }
        if (!IsLowercaseSha256(expected_sha256))
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    INVALID_ARCHIVE_SHA256,
                index,
                "archive SHA-256 must be 64 lowercase hexadecimal "
                "characters");
            continue;
        }
        if (!IsPortableRootFilename(bundle) ||
            !IsPortableRootFilename(terrain))
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    UNSAFE_PORTABLE_NAME,
                index,
                "dependency names must be portable root filenames");
            continue;
        }
        if (!EndsWithAsciiCaseInsensitive(bundle, ".zip"))
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    UNSUPPORTED_BUNDLE_TYPE,
                index,
                "terrain resource dependency bundle must be a ZIP");
            continue;
        }
        if (!EndsWithAsciiCaseInsensitive(terrain, ".terrn2"))
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::
                    UNSUPPORTED_TERRAIN_TYPE,
                index,
                "terrain resource dependency member must be a terrn2 file");
            continue;
        }

        const std::string folded_bundle = AsciiLowerString(bundle);
        if (!seen_bundles.insert(folded_bundle).second)
        {
            AddDiagnostic(
                plan,
                TerrainBundleDependencyDiagnosticCode::DUPLICATE_BUNDLE,
                index,
                "terrain resource dependency repeats a bundle");
            continue;
        }

        TerrainBundleDependency dependency;
        dependency.authored_name = authored;
        dependency.bundle_name = bundle;
        dependency.terrain_filename = terrain;
        dependency.expected_archive_sha256 = expected_sha256;
        plan.dependencies.push_back(dependency);
    }
    return plan;
}

const char* TerrainBundleDependencyDiagnosticCodeToString(
    TerrainBundleDependencyDiagnosticCode code)
{
    switch (code)
    {
    case TerrainBundleDependencyDiagnosticCode::EMPTY_DEPENDENCY:
        return "empty-dependency";
    case TerrainBundleDependencyDiagnosticCode::DEPENDENCY_COUNT_LIMIT:
        return "dependency-count-limit";
    case TerrainBundleDependencyDiagnosticCode::DEPENDENCY_BYTES_LIMIT:
        return "dependency-bytes-limit";
    case TerrainBundleDependencyDiagnosticCode::MALFORMED_QUALIFIER:
        return "malformed-qualifier";
    case TerrainBundleDependencyDiagnosticCode::MISSING_ARCHIVE_SHA256:
        return "missing-archive-sha256";
    case TerrainBundleDependencyDiagnosticCode::INVALID_ARCHIVE_SHA256:
        return "invalid-archive-sha256";
    case TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME:
        return "unsafe-portable-name";
    case TerrainBundleDependencyDiagnosticCode::UNSUPPORTED_BUNDLE_TYPE:
        return "unsupported-bundle-type";
    case TerrainBundleDependencyDiagnosticCode::UNSUPPORTED_TERRAIN_TYPE:
        return "unsupported-terrain-type";
    case TerrainBundleDependencyDiagnosticCode::DUPLICATE_BUNDLE:
        return "duplicate-bundle";
    }
    return "unknown";
}

} // namespace RoR
