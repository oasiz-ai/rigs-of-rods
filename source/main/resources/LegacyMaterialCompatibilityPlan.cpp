/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "LegacyMaterialCompatibilityPlan.h"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace RoR
{

namespace
{

struct AliasRule
{
    const char* requested;
    const char* target;
};

const AliasRule CITYWORLD_ALIAS_RULES[] = {
    {"/TEXFACE/Metalroof01.dds",
     "modularbuildings/TEXFACE/Metalroof01.dds"},
    {"/TEXFACE/cornice01_darkred.dds",
     "modularbuildings/TEXFACE/cornice01_darkred.dds"},
    {"/TEXFACE/wall_largeblocks_darkgrey.dds",
     "modularbuildings/TEXFACE/wall_largeblocks_darkgrey.dds"},
    {"Material.005/TEXFACE/Metalroof01.dds",
     "modularbuildings/TEXFACE/Metalroof01.dds"},
    {"Material.005/TEXFACE/asphaltshingles.dds",
     "modularbuildings/TEXFACE/asphaltshingles.dds"},
    {"Material.005/TEXFACE/betterbrickdiffuse.dds",
     "modularbuildings/TEXFACE/betterbrickdiffuse.dds"},
    {"Material.005/TEXFACE/brickwall_darkred.dds",
     "modularbuildings/TEXFACE/brickwall_darkred.dds"},
    {"Material.005/TEXFACE/cornice02_tan.dds",
     "modularbuildings/TEXFACE/cornice02_tan.dds"},
    {"Material.005/TEXFACE/sidewalk01.dds",
     "modularbuildings/TEXFACE/sidewalk01.dds"},
    {"Material.005/TEXFACE/sidewalkedge.dds",
     "modularbuildings/TEXFACE/sidewalkedge.dds"},
    {"Material.005/TEXFACE/wall_largeblocks_darkgrey.dds",
     "modularbuildings/TEXFACE/wall_largeblocks_darkgrey.dds"},
    {"Material.025/TEXFACE/Metalroof01.dds",
     "modularbuildings/TEXFACE/Metalroof01.dds"},
    {"Material.025/TEXFACE/sidewalk01.dds",
     "modularbuildings/TEXFACE/sidewalk01.dds"},
    {"Material.025/TEXFACE/sidewalkedge.dds",
     "modularbuildings/TEXFACE/sidewalkedge.dds"},
    {"Material.044/TEXFACE/asphalt.dds",
     "modularbuildings/TEXFACE/asphalt.dds"},
    {"Material.044/TEXFACE/concretebaretan.dds",
     "modularbuildings/TEXFACE/concretebaretan.dds"},
    {"Material.044/TEXFACE/sidewalk01.dds",
     "modularbuildings/TEXFACE/sidewalk01.dds"},
    {"Material.044/TEXFACE/sidewalkedge.dds",
     "modularbuildings/TEXFACE/sidewalkedge.dds"},
    {"modularbuildings/SOLID/TEX/betterbrickdiffuse.dds",
     "modularbuildings/TEXFACE/betterbrickdiffuse.dds"},
    {"penguinvilleblock02/TEXFACE/ramp.tga",
     "dneroads/TEXFACE/ramp.tga"},
    {"textface/TEXFACE/Metalroof01.dds",
     "modularbuildings/TEXFACE/Metalroof01.dds"},
    {"textface/TEXFACE/sidewalk01.dds",
     "modularbuildings/TEXFACE/sidewalk01.dds"},
    {"textface/TEXFACE/sidewalkedge.dds",
     "modularbuildings/TEXFACE/sidewalkedge.dds"}};

struct FallbackRule
{
    const char* requested;
    const char* script_sha256;
    LegacyMaterialColor color;
};

const FallbackRule CITYWORLD_FALLBACK_RULES[] = {
    {"/TEXFACE", nullptr, {145U, 145U, 140U, false}},
    {"Material.005/TEXFACE", nullptr, {150U, 147U, 140U, false}},
    {"Material.019/TEXFACE", nullptr, {142U, 145U, 148U, false}},
    {"Material.044/TEXFACE", nullptr, {138U, 140U, 142U, false}},
    {"modularbuildings/TEXFACE", nullptr, {148U, 145U, 138U, false}},
    {"09_-_Default", nullptr, {145U, 145U, 145U, false}},
    {"Material.001", nullptr, {152U, 148U, 142U, false}},
    {"Material.004", nullptr, {142U, 146U, 150U, false}},
    {"SketchupDefault", nullptr, {158U, 154U, 145U, false}},
    {"cromo", nullptr, {178U, 188U, 198U, true}},
    {"jean_blue", nullptr, {56U, 82U, 128U, false}}};

const FallbackRule CITYWORLD_TEXTURE_RULES[] = {
    {"barrier.dds",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     {224U, 136U, 40U, false}},
    {"busstopsign.dds",
     "4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
     {38U, 105U, 168U, false}},
    {"chair.dds",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     {116U, 82U, 52U, false}},
    {"roadclosed.dds",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     {206U, 72U, 48U, false}},
    {"stopsign.dds",
     "4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
     {190U, 32U, 38U, false}},
    {"table.dds",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     {110U, 78U, 48U, false}},
    {"umbrella.dds",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     {218U, 190U, 72U, false}}};

std::uint64_t Fnv1a64(const std::string& value)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        hash ^= static_cast<unsigned char>(value[index]);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

LegacyMaterialReferenceResolution None()
{
    return {
        LegacyMaterialReferenceDisposition::NONE,
        std::string(),
        {0U, 0U, 0U, false}};
}

} // namespace

LegacyMaterialReferenceResolution ResolveLegacyMaterialReference(
    const std::string& archive_sha256,
    const std::string& requested_material)
{
    if (archive_sha256 !=
        kCityWorldLegacyMaterialCompatibilityArchiveSha256)
    {
        return None();
    }
    for (const AliasRule& rule : CITYWORLD_ALIAS_RULES)
    {
        if (requested_material == rule.requested)
        {
            return {
                LegacyMaterialReferenceDisposition::ALIAS,
                rule.target,
                {0U, 0U, 0U, false}};
        }
    }
    for (const FallbackRule& rule : CITYWORLD_FALLBACK_RULES)
    {
        if (requested_material == rule.requested)
        {
            return {
                LegacyMaterialReferenceDisposition::GENERATED_FALLBACK,
                std::string(),
                rule.color};
        }
    }
    return None();
}

std::string BuildLegacyMaterialFallbackResourceName(
    const std::string& archive_sha256,
    const std::string& requested_material)
{
    std::ostringstream name;
    name << "RoR/LegacyMaterialFallback/";
    if (archive_sha256.size() >= 12U)
    {
        name << archive_sha256.substr(0U, 12U);
    }
    else
    {
        name << "unverified";
    }
    name << '/'
         << std::hex
         << std::setfill('0')
         << std::setw(16)
         << Fnv1a64(requested_material);
    return name.str();
}

std::string BuildLegacyTextureFallbackResourceName(
    const std::string& archive_sha256,
    const std::string& script_sha256,
    const std::string& original_texture)
{
    std::ostringstream name;
    name << "RoR/LegacyTextureFallback/";
    if (archive_sha256.size() >= 12U)
    {
        name << archive_sha256.substr(0U, 12U);
    }
    else
    {
        name << "unverified";
    }
    name << '/';
    if (script_sha256.size() >= 12U)
    {
        name << script_sha256.substr(0U, 12U);
    }
    else
    {
        name << "unverified";
    }
    name << '/'
         << std::hex
         << std::setfill('0')
         << std::setw(16)
         << Fnv1a64(original_texture)
         << ".dds";
    return name.str();
}

bool ResolveLegacyMissingTexture(
    const std::string& archive_sha256,
    const std::string& requested_texture,
    LegacyMaterialColor& out_color)
{
    if (archive_sha256 !=
        kCityWorldLegacyMaterialCompatibilityArchiveSha256)
    {
        return false;
    }
    for (const FallbackRule& rule : CITYWORLD_TEXTURE_RULES)
    {
        if (requested_texture ==
            BuildLegacyTextureFallbackResourceName(
                archive_sha256,
                rule.script_sha256,
                rule.requested))
        {
            out_color = rule.color;
            return true;
        }
    }
    return false;
}

} // namespace RoR
