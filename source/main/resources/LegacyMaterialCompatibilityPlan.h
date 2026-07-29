/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Exact compatibility metadata for authenticated legacy packages.

#pragma once

#include <string>

namespace RoR
{

struct LegacyMaterialColor
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    bool high_specular;
};

enum class LegacyMaterialReferenceDisposition
{
    NONE,
    ALIAS,
    GENERATED_FALLBACK
};

struct LegacyMaterialReferenceResolution
{
    LegacyMaterialReferenceDisposition disposition;
    std::string target_material;
    LegacyMaterialColor color;
};

/// Resolve only explicitly reviewed names for an exact authenticated archive.
/// Unknown archives and names return NONE; no fuzzy or iteration-order match
/// is ever performed at runtime.
LegacyMaterialReferenceResolution ResolveLegacyMaterialReference(
    const std::string& archive_sha256,
    const std::string& requested_material);

/// Build a portable generated-resource name. This is used only after
/// ResolveLegacyMaterialReference returned GENERATED_FALLBACK.
std::string BuildLegacyMaterialFallbackResourceName(
    const std::string& archive_sha256,
    const std::string& requested_material);

/// Build the unique DDS resource name inserted only by an authenticated,
/// exact-script repair plan. Original legacy filenames are never intercepted.
std::string BuildLegacyTextureFallbackResourceName(
    const std::string& archive_sha256,
    const std::string& script_sha256,
    const std::string& original_texture);

/// Return true only for an explicitly reviewed texture which is absent from an
/// exact authenticated archive and whose unique repaired resource name may
/// receive a procedural local fallback.
bool ResolveLegacyMissingTexture(
    const std::string& archive_sha256,
    const std::string& requested_texture,
    LegacyMaterialColor& out_color);

} // namespace RoR
