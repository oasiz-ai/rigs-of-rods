/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Exact compatibility metadata for authenticated legacy packages.

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace RoR
{

/// Exact archive identity which owns the reviewed CityWorld material-script,
/// missing-material, and missing-texture compatibility policy.  A filename or
/// cache entry never confers this authority; callers must verify the complete
/// archive bytes against this digest before selecting the policy.
constexpr char kCityWorldLegacyMaterialCompatibilityArchiveSha256[] =
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";
constexpr std::uint64_t
    kCityWorldLegacyMaterialCompatibilityArchiveBytes = 158845395ULL;

/// The reviewed CityWorld Next local overlay (v14). Its members carry the
/// authenticated texture authority the combined runtime's road-material
/// capture requires (cityworld_road2_basecolor.dds). The digest is the
/// deterministic output of tools/build_cityworld_local_overlay.py over the
/// pinned inputs, with its terrain members restaged by
/// tools/restage_cityworld_overlay_terrain.py; rebuilding the overlay with
/// changed content must update this pin in the same commit, or the runtime
/// falls back to the ordinary unauthenticated mount and road captures fail
/// closed.
constexpr char kCityWorldNextLocalOverlayArchiveSha256[] =
    "ba20e68bba11731a9ca6620d66d9ba7332f114a250ee72bafd579a046637facc";
constexpr std::uint64_t
    kCityWorldNextLocalOverlayArchiveBytes = 37265508ULL;

/// Hash probing is private to the active OgreNext migration session and only
/// applies to a selected primary terrain ZIP.  These inexpensive facts limit
/// work; only the verified full-archive digest grants compatibility authority.
constexpr bool ShouldProbeLegacyMaterialPrimaryArchive(
    bool ogre_next_demo_capture_enabled,
    bool primary_terrain,
    bool zip_archive) noexcept
{
    return ogre_next_demo_capture_enabled && primary_terrain && zip_archive;
}

/// Execute exactly one primary-location route.  The ordinary path preserves
/// the historical pathname mount followed by package registration.  The
/// authenticated path publishes only the immutable snapshot and deliberately
/// skips both ordinary callbacks.  Exceptions propagate to the caller's
/// resource-group transaction.
template <
    typename AuthenticatedMount,
    typename OrdinaryMount,
    typename OrdinaryRegister>
void DispatchLegacyMaterialPrimaryArchiveMount(
    bool authenticated_snapshot_verified,
    AuthenticatedMount&& authenticated_mount,
    OrdinaryMount&& ordinary_mount,
    OrdinaryRegister&& ordinary_register)
{
    if (authenticated_snapshot_verified)
    {
        std::forward<AuthenticatedMount>(authenticated_mount)();
        return;
    }
    std::forward<OrdinaryMount>(ordinary_mount)();
    std::forward<OrdinaryRegister>(ordinary_register)();
}

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
