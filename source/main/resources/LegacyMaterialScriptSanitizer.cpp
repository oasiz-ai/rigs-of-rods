/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "LegacyMaterialScriptSanitizer.h"

#include "LegacyMaterialCompatibilityPlan.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <vector>

namespace RoR
{
namespace
{

void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
    {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void AppendLittleEndian64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
    {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

bool AppendCanonicalString(
    std::vector<std::uint8_t>& bytes,
    const std::string& value)
{
    if (value.size() > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    AppendLittleEndian32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
    return true;
}

bool IsCanonicalSha256(const std::string& value)
{
    if (value.size() != 64U)
    {
        return false;
    }
    for (const char character : value)
    {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool DigestCanonicalBytes(
    const std::vector<std::uint8_t>& bytes,
    std::string& out_sha256)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0U;
    if (EVP_Digest(
            bytes.data(), bytes.size(), digest.data(), &digest_size,
            EVP_sha256(), nullptr) != 1 ||
        digest_size != 32U)
    {
        return false;
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::string candidate(64U, '0');
    for (std::size_t index = 0U; index < 32U; ++index)
    {
        candidate[index * 2U] = HEX[digest[index] >> 4U];
        candidate[index * 2U + 1U] = HEX[digest[index] & 0x0fU];
    }
    out_sha256.swap(candidate);
    return true;
}

// Exact-script inserts use CRLF because every pinned NeoQ2.0 source script is
// CRLF-authored. They retain texture units and authored blend/depth/cull state,
// while giving OGRE 14 RTShaderSystem useful physically plausible light terms.
const char NEOQ20_SURFACE_PASS_OPEN[] =
    "{\r\n"
    "          lighting on\r\n"
    "          ambient 0.18 0.18 0.18 1\r\n"
    "          diffuse 0.82 0.82 0.82 1\r\n"
    "          specular 0.06 0.06 0.06 1 16";
const char NEOQ20_ASPHALT_PASS_OPEN[] =
    "{\r\n"
    "          lighting on\r\n"
    "          ambient 0.12 0.12 0.12 1\r\n"
    "          diffuse 0.70 0.70 0.70 1\r\n"
    "          specular 0.03 0.03 0.03 1 8";
const char NEOQ20_CONCRETE_PASS_OPEN[] =
    "{\r\n"
    "          lighting on\r\n"
    "          ambient 0.20 0.20 0.20 1\r\n"
    "          diffuse 0.78 0.78 0.78 1\r\n"
    "          specular 0.05 0.05 0.05 1 12";
const char NEOQ20_METAL_PASS_OPEN[] =
    "{\r\n"
    "          lighting on\r\n"
    "          ambient 0.16 0.16 0.16 1\r\n"
    "          diffuse 0.72 0.72 0.72 1\r\n"
    "          specular 0.28 0.28 0.28 1 48";
const char NEOQ20_FACADE_PASS_OPEN[] =
    "{\r\n"
    "          lighting on\r\n"
    "          ambient 0.22 0.22 0.22 1\r\n"
    "          diffuse 0.78 0.78 0.78 1\r\n"
    "          specular 0.05 0.05 0.05 1 12";

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     30U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     37U, "pass", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     38U, "{", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     39U, "ambient  0.0 0.0 0.0", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     40U, "diffuse  0.0 0.0 0.0", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     41U, "specular 0.0 0.0 0.0 1.0 12.5", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     42U, "scene_blend add", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     43U, "}", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     81U, "scroll_z", "scroll_y"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     140U, "}", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     297U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     310U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     323U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     453U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     466U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     479U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     492U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     505U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     518U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     692U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     712U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     732U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     772U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     839U, "{", NEOQ20_SURFACE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     928U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     957U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     986U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1015U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1044U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1073U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1091U, "{", NEOQ20_METAL_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1109U, "{", NEOQ20_FACADE_PASS_OPEN},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // NQ-2-0-Base: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     17U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-B-U-C-ground: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     105U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-B-U-C-ground: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     116U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-B-U-C-ground: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     134U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-C-I-F-1: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     206U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-C-I-F-2: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     219U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-track-airport: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     232U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-marmol-floor: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     245U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-stucco-plaster: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     258U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-N-C-T: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     271U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-B-U-C-C-F: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     284U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Ladrillos-Rojos-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     336U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-adoquin-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     349U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-adoquin-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     362U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-adoquin-Entrelazado-Rosa: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     375U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-adoquin-Exagonal-Rosa: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     388U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-cesped-A: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     401U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // NQ-cesped-B: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     414U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // NQ-cesped-C: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     427U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // NQ-Prefabricados-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     440U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Senalamientos-Viales: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     531U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-Senalamientos-Viales-E: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     544U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-para-bus: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     634U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Road-Tunnel-4carriles-ext: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     647U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // NQ-Road-Tunnel-4carriles-lamparas: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     660U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // NQ-LaminaGalvanizada-A: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     674U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-Steel-Girder: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     752U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-WTM: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     809U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-MPC: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     824U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-VistaPanoramica-360grados: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     858U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-W-M-01: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     913U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-alumbrado-Publico: match_existing -> roughness 0.200
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     940U, "{",
     "{\r\n          specular 0.28 0.28 0.28 1 48"},
    // NQ-alumbrado-Publico-Gr: match_existing -> roughness 0.200
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     969U, "{",
     "{\r\n          specular 0.28 0.28 0.28 1 48"},
    // NQ-alumbrado-Publico-Rd: match_existing -> roughness 0.200
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     998U, "{",
     "{\r\n          specular 0.28 0.28 0.28 1 48"},
    // NQ-alumbrado-Publico-Bl: match_existing -> roughness 0.200
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1027U, "{",
     "{\r\n          specular 0.28 0.28 0.28 1 48"},
    // NQ-alumbrado-Publico-Bck: match_existing -> roughness 0.200
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1056U, "{",
     "{\r\n          specular 0.28 0.28 0.28 1 48"},
    // NQ-GasolineraPEMEX-Tiendaoxxo: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1116U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-Ps-Ds-Md-Wd: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1138U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_BUILDS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     109U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     132U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     224U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     362U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     385U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     569U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     675U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     676U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     701U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     702U, "}", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     807U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     831U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     927U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1157U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1170U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1183U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1196U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1209U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1222U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1235U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1248U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1261U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1274U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1287U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1300U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1313U, "{", NEOQ20_FACADE_PASS_OPEN},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // NQ2-0Md-Schl: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ2-0Md-Schl: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     13U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ2-0highschool: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     29U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     63U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     70U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     86U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     93U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-C: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     116U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-onlyCIS-D: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     139U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-onlyCIS-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     155U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     162U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-F: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     178U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-F: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     185U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-G: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     201U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-G: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     208U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-onlyCIS-H: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     231U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-EdCrsl-o-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     247U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     254U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     270U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     277U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     293U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     300U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     316U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     323U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     339U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     346U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-F: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     369U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-EdCrsl-o-G: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     392U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-EdCrsl-o-H: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     408U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-H: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     415U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-I: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     431U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdCrsl-o-I: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     438U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     454U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     461U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     477U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     484U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     500U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     507U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     523U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     530U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     546U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-O-F-D-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     553U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Shops: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     582U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-Shops: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     594U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-VentanasconColumnasExteriorastipoNeoclasico: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     611U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-VentanasconColumnasExteriorastipoNeoclasico: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     618U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-FachadadeEdificioEstiloDepartamentos: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     635U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-FachadadeEdificioEstiloDepartamentos: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     642U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-FachadadeEdificioconVentanasposModernistas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     659U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-FachadadeEdificioconVentanasposModernistas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     666U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-FachadadeCasastipoInteresSosialconVistadeValcones: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     685U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-FachadadeCasastipoInteresSosialconVistadeValcones: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     692U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-EdificiodeOficinasconVentanastipoNeoclasico: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     711U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-EdificiodeOficinasconVentanastipoNeoclasico: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     718U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-LugubreEdificioconVentanasdeVidrioExteriores: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     735U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-LugubreEdificioconVentanasdeVidrioExteriores: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     742U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-VentanasdeArquitecturadeOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     759U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-VentanasdeArquitecturadeOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     766U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // EdificioconVentanasdeVidrioExterioresTipoOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     783U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // EdificioconVentanasdeVidrioExterioresTipoOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     790U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // VentanasdeVidrioExterioresTipoOficinas: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     814U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // fachadadeedificiodeoficinasconventanasdevidrioexteriores: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     838U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // VentanasdeVidrioTipoOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     855U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // VentanasdeVidrioTipoOficinas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     862U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // EdificioconPatronRepetitivodelasVentanas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     879U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // EdificioconPatronRepetitivodelasVentanas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     886U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-EdificioconPatronRepetitivodelasVentanas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     903U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-EdificioconPatronRepetitivodelasVentanas: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     910U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // NQ-EdificiodeOficinasconVentanasdeVidrioExteriores: match_existing -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     934U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 12"},
    // NQ-DyCIS-H: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     951U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-H: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     958U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-G: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     975U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-G: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     982U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-F: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     999U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-F: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1006U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1023U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1030U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1047U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-D: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1054U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1071U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-C: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1078U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1095U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-B: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1102U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1119U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-DyCIS-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1126U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-creepy-house: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1144U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_ASPHALT_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     19U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     32U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     58U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     71U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     84U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     97U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     110U, "{", NEOQ20_ASPHALT_PASS_OPEN},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // NQ-Cebra-Peatonal-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     45U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_CONCRETE_ROAD_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     19U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     32U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     45U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     58U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     71U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     84U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     97U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     110U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     123U, "{", NEOQ20_CONCRETE_PASS_OPEN},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // NQ-C-A2-4C-C2: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     136U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Cebra-Peatonal-concreto-A: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     149U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_VEGETATION_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     8U, "ambient  0.9 0.9 0.9 0.9",
     "ambient 0.18 0.20 0.16 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     9U, "diffuse  0.1 0.1 0.1 1.0",
     "diffuse 0.82 0.88 0.76 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     10U, "lighting on",
     "lighting on\r\n"
     "          specular 0.01 0.01 0.01 1.0 4"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     31U, "ambient  0.8 0.8 0.8 0.8",
     "ambient 0.16 0.19 0.15 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     32U, "diffuse  0.1 0.1 0.1 1.0",
     "diffuse 0.78 0.86 0.74 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     33U, "lighting on",
     "lighting on\r\n"
     "          specular 0.01 0.01 0.01 1.0 4"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     54U, "ambient  0.9 0.9 0.9 0.9",
     "ambient 0.18 0.20 0.16 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     55U, "diffuse  0.1 0.1 0.1 1.0",
     "diffuse 0.82 0.88 0.76 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     56U, "lighting on",
     "lighting on\r\n"
     "          specular 0.01 0.01 0.01 1.0 4"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     77U, "ambient  0.9 0.9 0.9 0.9",
     "ambient 0.18 0.20 0.16 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     78U, "diffuse  0.1 0.1 0.1 1.0",
     "diffuse 0.82 0.88 0.76 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     79U, "lighting on",
     "lighting on\r\n"
     "          specular 0.01 0.01 0.01 1.0 4"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     100U, "ambient  0.9 0.9 0.9 0.9",
     "ambient 0.18 0.20 0.16 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     101U, "diffuse  0.1 0.1 0.1 1.0",
     "diffuse 0.82 0.88 0.76 1.0"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     102U, "lighting on",
     "lighting on\r\n"
     "          specular 0.01 0.01 0.01 1.0 4"}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_SMFS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     20U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     34U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     48U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     62U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     76U, "{", NEOQ20_FACADE_PASS_OPEN},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     90U, "{", NEOQ20_FACADE_PASS_OPEN}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQUERETARO_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     26U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     57U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     88U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     119U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     150U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     181U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     212U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     231U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     250U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     273U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     288U, "787.40157480315, 787.40157480315", ""},
    // Match the cube, zero-mipmap, PF_R8G8B8 RTT created by main.cpp.
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     512U,
     "cubic_texture EnvironmentTexture combinedUVW",
     "texture EnvironmentTexture cubic 0 PF_R8G8B8"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     513U, "env_map planar", "env_map cubic_reflection"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1087U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1099U, "texture_unit", "lighting on"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1100U, "{", "ambient 0.08 0.16 0.22 1"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1101U,
     "texture parabusimagenlateral.jpg",
     "diffuse 0.18 0.36 0.52 1"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1102U,
     "tex_address_mode wrap",
     "specular 0.04 0.04 0.04 1 8"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1103U, "filtering trilinear", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1104U, "colour_op alpha_blend", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1105U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1289U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1371U,
     "fachadasdetiendasencendidas.png",
     "fachadasdetiendasencendidas.PNG"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1460U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1627U,
     "cubic_texture EnvironmentTexture combinedUVW",
     "texture EnvironmentTexture cubic 0 PF_R8G8B8"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1628U, "env_map planar", "env_map cubic_reflection"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1727U, "color_op_ex", "colour_op_ex"},
    // Authenticated second copy of the block at lines 1698-1710.
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1772U, "material concretorojo", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1773U, "{", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1774U, "technique", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1775U, "{", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1776U, "pass", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1777U, "{", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1778U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1779U, "{", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1780U, "texture detalle-concreto-rojo.jpg", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1781U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1782U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1783U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1784U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     1864U, "{", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1876U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     2028U, "393.700787401575, 187.953200765159", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2037U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     2053U, "393.700787401575, 187.953200765159", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2062U, "color_op_ex", "colour_op_ex"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2163U, "pistaaeropuerto.jpg", "pistaaeropuerto.JPG"},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // ventanas1: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas1: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     13U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas2: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     37U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas2: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     44U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas3: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     68U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas3: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     75U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas4: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     99U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas4: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     106U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas5: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     130U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas5: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     137U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas6: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     161U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas6: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     168U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas7: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     192U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ventanas7: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     199U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // colegioporfiriocadenaventanas1: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     223U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // colegioporfiriocadenaventanas2: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     242U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // cristalesestadio: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     262U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // transitotope: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     302U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // luzsecuencia1Qr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     318U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia2Qr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     342U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia3Qr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     366U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia4Qr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     390U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia1TQr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     414U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia2TQr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     438U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // luzsecuencia3TQr: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     462U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // Color_006: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     486U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // Color_007: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     497U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // parabus: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     508U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // topeQr: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     523U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // parabusasientos: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     536U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // prado: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     549U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // pavimento: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     562U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // luminariaQrposte: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     575U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // luminariaQrbase: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     588U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // luminariaQrlampara: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     601U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // puente: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     631U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // pavimento4vias: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     644U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // prefabricados: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     657U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // curvasimple: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     671U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // crucesimple: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     684U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // cruceplano: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     697U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // crucemixto: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     710U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // colegioporfiriocadenamonolito: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     723U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // asfalto4vias: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     737U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // concreto: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     750U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretoconpasto: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     763U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // curva: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     776U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // astabandera: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     789U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // baseastabandera: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     802U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // bandera: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     815U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // adocretos: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     876U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // ventanascereso: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     889U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // puertacereso: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     902U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // piedra: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     915U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // gente: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     928U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // rojotexturizado: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     944U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // cancha: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     957U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // incrustacionesdegranitoobscuro2: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     973U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // marmolina: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     986U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // azulejodemotivodemarmol: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1002U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // granitoflameado: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1018U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // granitogrisobscuro: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1031U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // estacionamiento: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1044U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // logotiposplaza: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1057U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // imagenparabus: match_existing -> roughness 0.447
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1098U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 8"},
    // pisodemarmol: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1114U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // ccletras: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1130U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // Color_005: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1146U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // B_OGT: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1158U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // cccNQletras: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1174U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // hotelneoqueretaroletras: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1190U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // DPNQ: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1206U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // hcb: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1222U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // logodepemex1: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1254U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // logodepemex2: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1270U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // Color_G17: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1286U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // Color_A11: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1298U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // fachadadeoxxo: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1309U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // bombadegasolinera: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1333U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // tiendas: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1357U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // transportepublico: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1381U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // paradadeautobus: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1397U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // paradadeautobus: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1405U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // Wreckmastertowinglogo: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1423U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // incrustacionesdegranito: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1439U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // ventanasWreckmaster: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1452U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // frasewreckmaster: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1471U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // BRTlogo: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1487U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // 5defebreroconconstituyentes: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1503U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // constituyentescon5defebrero: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1516U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // ezequielmontescon5defebrero: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1529U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // 5defebreroconezequielmontes: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1542U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // constituyentesconcorregidora: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1555U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // corregidoraconconstituyentes: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1568U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // corregidoraconezequielmontes: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1581U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // ezequielmontesconcorregidora: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1594U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // mapa: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1607U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // semaforogris3: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1620U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // semaforogris: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1638U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // semaforogris1: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1651U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // semaforogris2: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1664U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // precaucion: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1677U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretogris: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1690U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // ventanascasadeIS: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1716U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // concretoazul: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1738U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretoverde: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1751U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretomorado: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1764U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretovioleta: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1790U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // concretocafe: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1803U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // puertademadera: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1816U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // arsilla: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1829U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // alumbradopublico: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1842U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // mallaciclonica: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1887U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // logoford: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1902U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // ficus1: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1918U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // ficus2: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1933U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // ficus3: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1948U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // ficus4: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1963U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // ficus5: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1978U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // ficus6: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1993U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // cercado: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2008U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // calledoblesentido: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2073U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // calleunsolosentido: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2086U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // calleestrecha: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2099U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // reductordevelocidadtope: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2112U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // crucepeatonalQr: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2136U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // pista: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2160U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // limitedevelocidad60: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2173U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // limitedevelocidad40: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2197U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_BUSSTOP_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     125U, "color_op", "colour_op"},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // busstopNJTopaqe_old: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     90U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

// Reviewed high-resolution facade replacements. The replacement members are
// independently authored procedural textures shipped by the CityWorld Next
// local overlay under the reserved cityworld_next_replacements/ namespace
// (tools/cityworld_replacement_textures.py). Original member names are never
// intercepted: every original texture stays resolvable by its own name, and
// only these exact-script, exact-line plans reference the replacements.
const LegacyMaterialScriptEdit CITYWORLD_DNEBUILDINGS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     438U,
     "texture brickwall_darkred.dds",
     "texture cityworld_next_replacements/brickwall_darkred_1024.png"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     2385U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2566U,
     "texture lightgreybrick.dds",
     "texture cityworld_next_replacements/lightgreybrick_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2607U,
     "texture betterbrickdiffuse.dds",
     "texture cityworld_next_replacements/betterbrickdiffuse_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2637U,
     "texture concretetan.dds",
     "texture cityworld_next_replacements/concretetan_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2667U,
     "texture concretelightgrey.dds",
     "texture cityworld_next_replacements/concretelightgrey_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3205U,
     "texture brickwall_darkred.dds",
     "texture cityworld_next_replacements/brickwall_darkred_1024.png"},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // modularbuildings/TEXFACE/sidewalkedge.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     8U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/TEXFACE/Metalroof01.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     22U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/base_wall_largeblocks_darkgrey.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     36U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/base_window_narrow_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     50U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_narrow_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     75U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_narrow_archtop_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     100U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/sidewalk01.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     126U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/TEXFACE/wall_largeblocks_darkgrey.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     141U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/Epicrailhqwindow06fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     155U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/Epicrailhqwindow02fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     180U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/Epicrailhqwindow04fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     205U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/Epicrailhqwindow05fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     230U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/Epicrailhqwindow01fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     255U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/Epicrailhqwindow03fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     280U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/epicrailroof.dds: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     305U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // modularbuildings/TEXFACE/window_chicago_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     319U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_chicago_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     344U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_largearch_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     369U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindowarch_darkred.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     395U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/sign01.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     421U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwall_darkred.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     435U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/base_brickentrance_darkred.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     449U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/base_brickdoor_darkred.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     475U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/brickwindowplain_darkred.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     501U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickwindow_darkred.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     527U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice01_white.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     553U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickwindowarch_darkblue.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     567U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickwindow_white.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     592U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindowplain_yeller.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     617U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindow_yeller.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     642U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwall_darkblue.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     667U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/brickwall_white.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     681U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/brickwindownonarch_white.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     695U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindowplain_darkblue.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     720U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice01_darkblue.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     745U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/base_brickentrance_white.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     760U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/cornice01_darkred.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     785U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/base_brickentrance_darkblue.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     800U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/cornice02_darkred.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     825U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickwall_yeller.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     839U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/window_chicago_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     853U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_largearch_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     879U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_2small_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     905U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_chicago_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     931U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_standard_tan_cropped.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     957U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_arch_tan_cropped.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     983U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_standard_tan_cropped.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1009U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_narrow_tan_cropped.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1035U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_narrow_tan_cropped.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1061U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice02end_darkred.jpg: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1087U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/cornice02_darkred.jpg: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1102U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickwindow_white.jpg: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1117U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickwindow_darkblue.jpg: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1143U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindow_darkred2.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1169U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice01_concrete_fancy.jpg.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1195U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickfiredepartmentdoor.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1210U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/parkingspacewhite.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1225U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/lowercornice01_concrete.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1240U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/asphalt.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1255U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/TEXFACE/busstationsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1270U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/decowhitepanels.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1285U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/busstationsign2.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1300U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/decowindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1315U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_standard_tanbrick.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1341U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice04_tan.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1367U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/base_window_arch_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1382U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_narrow_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1408U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_narrow_lightdarkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1434U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_standard_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1460U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/lowercornice02_concrete.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1486U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/window_standard_lightdarkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1501U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_chicago_lightdarkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1527U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_columnbase_tanbrick.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1553U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_arch_tanbrick.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1579U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_column_tanbrick.png: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1605U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_columnarch_tanbrick.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1631U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/garage_base_window.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1657U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/concreteparkingspace.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1701U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/garage_cornice.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1752U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/concretebaretan.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1767U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/garage_base_door.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1818U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // queen/TEXFACE/garage_side_window.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1844U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindowarch_darkred2.jpg.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1870U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice02_darkred2.jpg.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1896U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/cornice04_lightgrey.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1911U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickwindowwide_darkred.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1926U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/brickwindowplain_darkred2.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1952U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_tallsmallarch_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     1978U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_window_tallarch_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2004U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_chicago_lightgrey2.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2030U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice05_lightgrey.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2056U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/base_window_tallarchentrance_lightgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2071U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_standard_lightgrey2.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2097U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/cornice02_tan.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2123U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/brickwindowplain_darkred3.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2138U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/policedepartmentsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2164U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickentrance_columns.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2179U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/lowercornice03_concrete.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2205U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/coppersheathing.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2220U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/gothicwindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2235U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/40wallstreetwindow01.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2261U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_smooth_narrow_lightgreytan.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2287U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_smooth_standard_lightgreytan.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2313U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/window_smooth_narrow_lightgreygreen.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2339U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/40wallstreetcornicestandard.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2365U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/window_smooth_standard_lightgreygreen.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2380U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_largewindow_loadingdock.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2407U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_smallwindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2433U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_smallwindow_entrance.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2459U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_smallwindow_top.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2485U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_largewindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2511U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/warehouse_largewindow_top.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2537U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/lightgreybrick.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2563U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/window_standard_darkgrey.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2578U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/betterbrickdiffuse.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2604U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/asphaltshingles.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2619U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/TEXFACE/concretetan.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2634U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/failsign2.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2649U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/concretelightgrey.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2664U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/concretedarkgrey.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2679U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/modernistwindow01.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2694U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/failcorplogo.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2720U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/failsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2735U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/modernistbasewindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2750U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/modernistentrance.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2776U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/antenna.tga: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2802U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/TEXFACE/brickwindow_darkred.jpg: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2817U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickdoor_darkblue.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2843U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/base_brickdoor_yeller.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2869U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/brickwindowarch_yeller.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2895U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/base_brickdoor_white.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2921U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/TEXFACE/brickwindow_white.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2947U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/facade01topwindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2973U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/facade01cornice.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     2999U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/TEXFACE/facade01basewindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3014U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/facade01window.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3040U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/TEXFACE/facade01baseentrance.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3066U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/SOLID/TEX/facade02basewindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3091U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/facade02cornice.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3115U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/SOLID/TEX/chimney01.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3128U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/SOLID/TEX/facade02window.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3141U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/facade03window.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3165U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/facade02basedoor.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3189U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/SOLID/TEX/brickwall_darkred.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3202U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/SOLID/TEX/sidewalkedge.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3215U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/SOLID/TEX/facade03door.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3228U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/SOLID/TEX/sidewalk01.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3241U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/SOLID/TEX/blueawning.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3254U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/SOLID/TEX/chimney2.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3267U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/SOLID/TEX/facade03cornice.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3280U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // modularbuildings/SOLID/TEX/facade03topwindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3293U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/asphaltshingles.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3317U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // modularbuildings/SOLID/TEX/highdetailrowhousesigns.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3330U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/facade02topwindow.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3343U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // modularbuildings/SOLID/TEX/facade03base.dds: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3367U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // modularbuildings/SOLID/TEX/facade03brick.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3391U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // modularbuildings/SOLID/TEX/roofcrap1.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3404U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // helipad/TEXFACE/Helipad.psd: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     3418U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}};

const LegacyMaterialScriptEdit CITYWORLD_ASIA_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     11U,
     "texture asiaconcrete.dds",
     "texture cityworld_next_replacements/asiaconcrete_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     51U,
     "texture darkcrete.dds",
     "texture cityworld_next_replacements/darkcrete_1024.png"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     65U,
     "texture redcrete.dds",
     "texture cityworld_next_replacements/redcrete_1024.png"}};

const LegacyMaterialScriptEdit CITYWORLD_STREETFURNITURE_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     13U,
     "texture barrier.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/12d6ceb7bb9c58fb.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     38U,
     "texture table.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/75ac8ff686a68240.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     63U,
     "texture chair.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/9795566c91684ee5.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     85U,
     "texture umbrella.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/e0168964fde583c6.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     100U,
     "texture roadclosed.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/8e8aee4c22bb1900.dds"},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // streetfurniture/TEXFACE/umbrella.tga: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     82U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // streetfurniture/TEXFACE/roadclosed.tga: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     97U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"}};

const LegacyMaterialScriptEdit CITYWORLD_DNEROADS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     54U,
     "texture stopsign.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/4cdcde3752be/f1065bf44e2295b7.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     129U,
     "texture busstopsign.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/4cdcde3752be/b40762545c57e6c3.dds"},
    // Foundation F3 family-banded roughness coverage. Generated by
    // tools/generate_cityworld_roughness_repair_edits.py; see the tool
    // header for band rationale and the planar dead-zone floor.
    // dneroads/TEXFACE/asphalt.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     7U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/roadtexturehighdetailnolines.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     21U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/sidewalkedge.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     36U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/stopsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     51U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/streetlamp.tga: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     66U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // dneroads/TEXFACE/roadtexturehighdetailstopcrosswalk.psd: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     81U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/sidewalk01.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     96U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/roadtexturehighdetail.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     111U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/busstopsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     126U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/roadtexturehighdetailbrokenwhiteline.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     141U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/roadtexturehighdetailbrokenwhitelineintersection.psd: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     156U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/roadtexturehighdetailbrokenwhitelinecrosswalk.psd: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     171U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // dneroads/TEXFACE/parkingspacewhite.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     186U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/Parkingsign.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     201U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/Metalroof01.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     216U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // dneroads/TEXFACE/uglyibeamgreen.jpg: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     231U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/ramp.tga: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     246U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/streetcarrails.tga: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     261U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"},
    // dneroads/TEXFACE/concreteindent.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     276U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/blacksteelbeam.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     291U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // dneroads/TEXFACE/concretetan.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     306U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/concretebaretan.dds: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     321U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // dneroads/TEXFACE/blacksteelrivets.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     336U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // Material/SOLID: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     351U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // dneroads/TEXFACE: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     366U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // dneroads/TEXFACE/26-grass.dds: foliage -> roughness 0.894
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     381U, "{",
     "{\r\n          specular 0.02 0.02 0.02 1 0.5"},
    // dneroads/TEXFACE/highwaysignsupport.tga: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     396U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/signback01.tga: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     411U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/freewaysigns.dds: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     426U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/signback03.tga: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     441U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // dneroads/TEXFACE/signback02.tga: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     456U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"}};

// Foundation F3 family-banded roughness plans for scripts that previously
// carried no repair plan. Generated by
// tools/generate_cityworld_roughness_repair_edits.py.
const LegacyMaterialScriptEdit CITYWORLD_LETREROSENALAMIENTOQR_ROUGHNESS_EDITS[] = {
    // autodromoestadioprision: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}
};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ2_0_INTONLE_ROUGHNESS_EDITS[] = {
    // NQ-Carretera-Autovia-3carriles-Concreto-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     6U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Concreto-Vial-A-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     20U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Concreto-Vial-Estacionamiento-A-E: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     34U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // NQ-Concreto-wall-tiles-E: ceramic_wet -> roughness 0.378
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     48U, "{",
     "{\r\n          specular 0.10 0.10 0.10 1 12"},
    // NQ-GuardaVialdeAcero-3-E: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     62U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-Steel-Girder-E: polished -> roughness 0.302 (planar floor clamp)
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     83U, "{",
     "{\r\n          specular 0.25 0.25 0.25 1 20"},
    // NQ-Road-Tunnel-4carriles: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     104U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"}
};

const LegacyMaterialScriptEdit CITYWORLD_BUSSTOPNJT_ROUGHNESS_EDITS[] = {
    // busstopNJTopaqe: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     7U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"}
};

const LegacyMaterialScriptEdit CITYWORLD_FANCYTRAFFICLIGHT_ROUGHNESS_EDITS[] = {
    // dnetrafficlight01: gloss_paint -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     7U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"}
};

const LegacyMaterialScriptEdit CITYWORLD_SKYSCRAPER01_ROUGHNESS_EDITS[] = {
    // /TEXFACE/Epicrailhqwindow06fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     7U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/Epicrailhqwindow02fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     21U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/Epicrailhqwindow04fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     35U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/Epicrailhqwindow05fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     49U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/sidewalk01.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     63U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // /TEXFACE/Epicrailhqwindow01fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     77U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/Epicrailhqwindow03fixed.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     91U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // /TEXFACE/sidewalkedge.dds: asphalt_stucco -> roughness 0.845
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     105U, "{",
     "{\r\n          specular 0.03 0.03 0.03 1 0.8"},
    // /TEXFACE/epicrailroof.dds: semigloss -> roughness 0.500
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     119U, "{",
     "{\r\n          specular 0.06 0.06 0.06 1 6"}
};

const LegacyMaterialScriptEdit CITYWORLD_TEST_ROUGHNESS_EDITS[] = {
    // test/TEXFACE/window_smooth_standard_lightgreytan.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     7U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // test/TEXFACE/Metalroof01.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     22U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // test/TEXFACE/40wallstreetcornicestandard.dds: satin -> roughness 0.577
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     37U, "{",
     "{\r\n          specular 0.05 0.05 0.05 1 4"},
    // test/TEXFACE/window_smooth_standard_lightgreygreen.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     52U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // greentest: masonry -> roughness 0.791
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     67U, "{",
     "{\r\n          specular 0.04 0.04 0.04 1 1.2"},
    // test/TEXFACE/window_smooth_narrow_lightgreytan.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     78U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // test/TEXFACE/window_smooth_narrow_lightgreygreen.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     93U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"},
    // Material.053/TEXFACE/40wallstreetwindow01.dds: glazed_facade -> roughness 0.408
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     108U, "{",
     "{\r\n          specular 0.08 0.08 0.08 1 10"}
};

const LegacyMaterialScriptEditPlan CITYWORLD_PLANS[] = {
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0.material",
     "03e17f9fab655321e7b266ce848e55d3ecd581d417e4f336f3a7928cd9d6e919",
     CITYWORLD_NEOQ20_EDITS,
     sizeof(CITYWORLD_NEOQ20_EDITS) /
         sizeof(CITYWORLD_NEOQ20_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-builds.material",
     "95ce5cd0b9ca2bb4776baed80f89a0a2619a47fa54d943c88995787f0f7184ca",
     CITYWORLD_NEOQ20_BUILDS_EDITS,
     sizeof(CITYWORLD_NEOQ20_BUILDS_EDITS) /
         sizeof(CITYWORLD_NEOQ20_BUILDS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-asphalt.material",
     "6ce129e2f04aaca9fe8dd29b62b09781f3dca3c19b18d58450976e330b165ae6",
     CITYWORLD_NEOQ20_ASPHALT_EDITS,
     sizeof(CITYWORLD_NEOQ20_ASPHALT_EDITS) /
         sizeof(CITYWORLD_NEOQ20_ASPHALT_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-concrete-road.material",
     "fe3c212dd0a1df62fa5c904575d8b0e61d440c42972c00f2792a1fcbab9354a4",
     CITYWORLD_NEOQ20_CONCRETE_ROAD_EDITS,
     sizeof(CITYWORLD_NEOQ20_CONCRETE_ROAD_EDITS) /
         sizeof(CITYWORLD_NEOQ20_CONCRETE_ROAD_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-vegetation.material",
     "63fd8844d1efe2393c3499678f06d9c7c09f757c11ae660f41141311ddb94484",
     CITYWORLD_NEOQ20_VEGETATION_EDITS,
     sizeof(CITYWORLD_NEOQ20_VEGETATION_EDITS) /
         sizeof(CITYWORLD_NEOQ20_VEGETATION_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-SmfS.material",
     "0491e5ca22aec7150a5df80bf5eaf73136bd7c03e0ae5ae984f807bd4b7882d9",
     CITYWORLD_NEOQ20_SMFS_EDITS,
     sizeof(CITYWORLD_NEOQ20_SMFS_EDITS) /
         sizeof(CITYWORLD_NEOQ20_SMFS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQueretaro.material",
     "9dac0249de8f55b47d5672ab2f8750026abada4e460b2b3a60c4b11ccceec6a3",
     CITYWORLD_NEOQUERETARO_EDITS,
     sizeof(CITYWORLD_NEOQUERETARO_EDITS) /
         sizeof(CITYWORLD_NEOQUERETARO_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "busstopNJTnormalmapped.material",
     "5eba9fb3b4873e7f4ef81c65490ce9eb429d9245700dfac8cdd871b5ed857b49",
     CITYWORLD_BUSSTOP_EDITS,
     sizeof(CITYWORLD_BUSSTOP_EDITS) /
         sizeof(CITYWORLD_BUSSTOP_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "dnebuildings.material",
     "11bb735dfadd54f594bfa02e967014edcd67cb5b7fcda8b3c8c3668cea2dc420",
     CITYWORLD_DNEBUILDINGS_EDITS,
     sizeof(CITYWORLD_DNEBUILDINGS_EDITS) /
         sizeof(CITYWORLD_DNEBUILDINGS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "asia.material",
     "ec34c578c12989e9a1559dfb56c539da49454d5fe7bbda2763fd7e279af6bc66",
     CITYWORLD_ASIA_EDITS,
     sizeof(CITYWORLD_ASIA_EDITS) /
         sizeof(CITYWORLD_ASIA_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "streetfurniture.material",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     CITYWORLD_STREETFURNITURE_EDITS,
     sizeof(CITYWORLD_STREETFURNITURE_EDITS) /
         sizeof(CITYWORLD_STREETFURNITURE_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "dneroads.material",
     "4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
     CITYWORLD_DNEROADS_EDITS,
     sizeof(CITYWORLD_DNEROADS_EDITS) /
         sizeof(CITYWORLD_DNEROADS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "LetrerosenalamientoQr.material",
     "62031bb860a261f17dc81b94554ef594363b1e810b5c52d959adfd6e9f40b60f",
     CITYWORLD_LETREROSENALAMIENTOQR_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_LETREROSENALAMIENTOQR_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_LETREROSENALAMIENTOQR_ROUGHNESS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "NeoQ2-0-IntOnLE.material",
     "5bfb00612d90309ed8cb4520934311d95be0690304fa878480548a270b0a6f20",
     CITYWORLD_NEOQ2_0_INTONLE_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_NEOQ2_0_INTONLE_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_NEOQ2_0_INTONLE_ROUGHNESS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "busstopNJT.material",
     "40fe8163ea1485904e09f235cd8a54baf95fbdaa9efa27252ad59ad646b0ea58",
     CITYWORLD_BUSSTOPNJT_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_BUSSTOPNJT_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_BUSSTOPNJT_ROUGHNESS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "fancytrafficlight.material",
     "e6081f8d8d1833bbafaa5047427131446ff85ea042db4e782aae2e43d3ebe01a",
     CITYWORLD_FANCYTRAFFICLIGHT_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_FANCYTRAFFICLIGHT_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_FANCYTRAFFICLIGHT_ROUGHNESS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "skyscraper01.material",
     "745d34ee0ff22ecc589df08854eb0955e47c6239ba13a7842a727d53db1a9223",
     CITYWORLD_SKYSCRAPER01_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_SKYSCRAPER01_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_SKYSCRAPER01_ROUGHNESS_EDITS[0])},
    {kCityWorldLegacyMaterialCompatibilityArchiveSha256,
     "test.material",
     "30d962541a0d0f34793b0de8689064087bfb66b5613a231b6a07f03199fde887",
     CITYWORLD_TEST_ROUGHNESS_EDITS,
     sizeof(CITYWORLD_TEST_ROUGHNESS_EDITS) /
         sizeof(CITYWORLD_TEST_ROUGHNESS_EDITS[0])}};

bool IsHorizontalWhitespace(char value)
{
    return value == ' ' || value == '\t' || value == '\r';
}

bool IsStandaloneCloseBraceLine(
    const std::string& payload,
    std::size_t line_start,
    std::size_t close_brace,
    std::size_t line_end)
{
    for (std::size_t index = line_start; index < close_brace; ++index)
    {
        if (!IsHorizontalWhitespace(payload[index]))
        {
            return false;
        }
    }

    std::size_t index = close_brace + 1U;
    while (index < line_end && IsHorizontalWhitespace(payload[index]))
    {
        ++index;
    }
    return index == line_end ||
        (index + 1U < line_end &&
         payload[index] == '/' &&
         payload[index + 1U] == '/');
}

LegacyMaterialScriptSanitization Rejected(
    const std::string& payload,
    const std::string& reason)
{
    LegacyMaterialScriptSanitization result;
    result.safe = false;
    result.payload = payload;
    result.rejection_reason = reason;
    return result;
}

std::string Trimmed(const std::string& value)
{
    std::size_t begin = 0U;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0)
    {
        --end;
    }
    return value.substr(begin, end - begin);
}

struct EditableLine
{
    std::string content;
    std::string ending;
};

std::vector<EditableLine> SplitLines(const std::string& payload)
{
    std::vector<EditableLine> lines;
    std::size_t begin = 0U;
    while (begin < payload.size())
    {
        const std::size_t newline = payload.find('\n', begin);
        if (newline == std::string::npos)
        {
            lines.push_back({payload.substr(begin), std::string()});
            return lines;
        }
        std::size_t content_end = newline;
        std::string ending("\n");
        if (content_end > begin && payload[content_end - 1U] == '\r')
        {
            --content_end;
            ending = "\r\n";
        }
        lines.push_back({
            payload.substr(begin, content_end - begin),
            ending});
        begin = newline + 1U;
    }
    if (payload.empty())
    {
        return lines;
    }
    return lines;
}

LegacyMaterialScriptPlanApplication RejectedPlan(
    const std::string& payload,
    const std::string& reason)
{
    LegacyMaterialScriptPlanApplication result;
    result.applicable = true;
    result.safe = false;
    result.payload = payload;
    result.applied_edit_count = 0U;
    result.rejection_reason = reason;
    return result;
}

} // namespace

LegacyMaterialScriptSanitization SanitizeLegacyMaterialScript(
    const std::string& payload)
{
    std::vector<std::size_t> removals;
    std::vector<LegacyMaterialScriptRepair> repairs;
    std::size_t brace_depth = 0U;
    std::size_t line = 1U;
    std::size_t line_start = 0U;
    bool in_block_comment = false;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t index = 0U; index < payload.size(); ++index)
    {
        const char value = payload[index];
        const char next =
            index + 1U < payload.size() ? payload[index + 1U] : '\0';

        if (value == '\n')
        {
            ++line;
            line_start = index + 1U;
            if (quote != '\0')
            {
                return Rejected(payload, "unterminated quoted token");
            }
            escaped = false;
            continue;
        }

        if (in_block_comment)
        {
            if (value == '*' && next == '/')
            {
                in_block_comment = false;
                ++index;
            }
            continue;
        }

        if (quote != '\0')
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (value == '\\')
            {
                escaped = true;
            }
            else if (value == quote)
            {
                quote = '\0';
            }
            continue;
        }

        if (value == '/' && next == '/')
        {
            const std::size_t newline = payload.find('\n', index + 2U);
            if (newline == std::string::npos)
            {
                break;
            }
            index = newline - 1U;
            continue;
        }
        if (value == '/' && next == '*')
        {
            in_block_comment = true;
            ++index;
            continue;
        }
        if (value == '"' || value == '\'')
        {
            quote = value;
            continue;
        }
        if (value == '{')
        {
            ++brace_depth;
            continue;
        }
        if (value != '}')
        {
            continue;
        }

        if (brace_depth != 0U)
        {
            --brace_depth;
            continue;
        }

        const std::size_t newline = payload.find('\n', index);
        const std::size_t line_end =
            newline == std::string::npos ? payload.size() : newline;
        if (!IsStandaloneCloseBraceLine(
                payload, line_start, index, line_end))
        {
            return Rejected(
                payload,
                "unmatched close brace shares a line with another token");
        }

        removals.push_back(index);
        repairs.push_back({line, index - line_start + 1U});
    }

    if (in_block_comment)
    {
        return Rejected(payload, "unterminated block comment");
    }
    if (quote != '\0')
    {
        return Rejected(payload, "unterminated quoted token");
    }
    if (brace_depth != 0U)
    {
        return Rejected(payload, "unmatched open brace");
    }

    LegacyMaterialScriptSanitization result;
    result.safe = true;
    result.removed_unmatched_close_braces = repairs;
    if (removals.empty())
    {
        result.payload = payload;
        return result;
    }

    result.payload.reserve(payload.size() - removals.size());
    std::size_t removal_index = 0U;
    for (std::size_t index = 0U; index < payload.size(); ++index)
    {
        if (removal_index < removals.size() &&
            removals[removal_index] == index)
        {
            ++removal_index;
            continue;
        }
        result.payload.push_back(payload[index]);
    }
    return result;
}

const LegacyMaterialScriptEditPlan* FindLegacyMaterialScriptEditPlan(
    const std::string& archive_sha256,
    const std::string& script_name)
{
    for (std::size_t index = 0U;
         index < sizeof(CITYWORLD_PLANS) / sizeof(CITYWORLD_PLANS[0]);
         ++index)
    {
        if (archive_sha256 == CITYWORLD_PLANS[index].archive_sha256 &&
            script_name == CITYWORLD_PLANS[index].script_name)
        {
            return &CITYWORLD_PLANS[index];
        }
    }
    return nullptr;
}

LegacyMaterialScriptPlanApplication ApplyLegacyMaterialScriptEditPlan(
    const LegacyMaterialScriptEditPlan& plan,
    const std::string& observed_script_sha256,
    const std::string& payload)
{
    if (observed_script_sha256 != plan.script_sha256)
    {
        return RejectedPlan(payload, "material script SHA-256 mismatch");
    }
    if (plan.edits == nullptr || plan.edit_count == 0U)
    {
        return RejectedPlan(payload, "material script edit plan is empty");
    }

    std::vector<EditableLine> lines = SplitLines(payload);
    for (std::size_t edit_index = 0U;
         edit_index < plan.edit_count;
         ++edit_index)
    {
        const LegacyMaterialScriptEdit& edit = plan.edits[edit_index];
        if (edit.line == 0U || edit.line > lines.size())
        {
            return RejectedPlan(payload, "material script edit line is absent");
        }
        EditableLine& line = lines[edit.line - 1U];
        if (edit.kind ==
            LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE)
        {
            if (Trimmed(line.content) != edit.expected)
            {
                return RejectedPlan(
                    payload,
                    "material script removal anchor mismatch");
            }
            line.content.clear();
            continue;
        }

        const std::string expected(edit.expected);
        const std::size_t match = line.content.find(expected);
        if (expected.empty() ||
            match == std::string::npos ||
            line.content.find(expected, match + expected.size()) !=
                std::string::npos)
        {
            return RejectedPlan(
                payload,
                "material script replacement anchor mismatch");
        }
        line.content.replace(match, expected.size(), edit.replacement);
    }

    std::string patched;
    patched.reserve(payload.size());
    for (const EditableLine& line : lines)
    {
        patched += line.content;
        patched += line.ending;
    }

    const LegacyMaterialScriptSanitization validation =
        SanitizeLegacyMaterialScript(patched);
    if (!validation.safe ||
        !validation.removed_unmatched_close_braces.empty())
    {
        return RejectedPlan(
            payload,
            "material script edits did not produce a balanced script");
    }

    LegacyMaterialScriptPlanApplication result;
    result.applicable = true;
    result.safe = true;
    result.payload = patched;
    result.applied_edit_count = plan.edit_count;
    return result;
}

bool ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
    const LegacyMaterialScriptEditPlan& plan,
    const std::string& exact_member_name,
    const std::string& observed_script_sha256,
    std::string& out_sha256)
{
    try
    {
        if (plan.archive_sha256 == nullptr || plan.script_name == nullptr ||
            plan.script_sha256 == nullptr || plan.edits == nullptr ||
            plan.edit_count == 0U ||
            exact_member_name != plan.script_name ||
            observed_script_sha256 != plan.script_sha256 ||
            !IsCanonicalSha256(plan.archive_sha256) ||
            !IsCanonicalSha256(observed_script_sha256) ||
            plan.edit_count > kLegacyMaterialScriptMaximumRepairPlanEdits ||
            plan.edit_count > (std::numeric_limits<std::uint32_t>::max)())
        {
            return false;
        }

        std::vector<std::uint8_t> canonical;
        canonical.reserve(256U + plan.edit_count * 32U);
        if (!AppendCanonicalString(
                canonical, "ror.ogre14.material-script-repair.applied") ||
            !AppendCanonicalString(canonical, plan.archive_sha256) ||
            !AppendCanonicalString(canonical, exact_member_name) ||
            !AppendCanonicalString(canonical, observed_script_sha256))
        {
            return false;
        }
        AppendLittleEndian32(
            canonical, kLegacyMaterialScriptRepairPlanVersion);
        AppendLittleEndian32(
            canonical, static_cast<std::uint32_t>(plan.edit_count));
        for (std::size_t index = 0U; index < plan.edit_count; ++index)
        {
            const LegacyMaterialScriptEdit& edit = plan.edits[index];
            if (edit.expected == nullptr || edit.replacement == nullptr ||
                edit.line == 0U)
            {
                return false;
            }
            switch (edit.kind)
            {
            case LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE:
                AppendLittleEndian32(canonical, 0U);
                break;
            case LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE:
                AppendLittleEndian32(canonical, 1U);
                break;
            default:
                return false;
            }
            AppendLittleEndian64(
                canonical, static_cast<std::uint64_t>(edit.line));
            if (!AppendCanonicalString(canonical, edit.expected) ||
                !AppendCanonicalString(canonical, edit.replacement))
            {
                return false;
            }
        }
        return DigestCanonicalBytes(canonical, out_sha256);
    }
    catch (...)
    {
        return false;
    }
}

bool ComputeLegacyMaterialScriptNoRepairPlanSha256(
    const std::string& archive_sha256,
    const std::string& exact_member_name,
    const std::string& observed_script_sha256,
    std::string& out_sha256)
{
    try
    {
        if (!IsCanonicalSha256(archive_sha256) ||
            exact_member_name.empty() ||
            !IsCanonicalSha256(observed_script_sha256))
        {
            return false;
        }
        std::vector<std::uint8_t> canonical;
        canonical.reserve(256U);
        if (!AppendCanonicalString(
                canonical, "ror.ogre14.material-script-repair.none") ||
            !AppendCanonicalString(canonical, archive_sha256) ||
            !AppendCanonicalString(canonical, exact_member_name) ||
            !AppendCanonicalString(canonical, observed_script_sha256))
        {
            return false;
        }
        AppendLittleEndian32(
            canonical, kLegacyMaterialScriptRepairPlanVersion);
        AppendLittleEndian32(canonical, 0U);
        return DigestCanonicalBytes(canonical, out_sha256);
    }
    catch (...)
    {
        return false;
    }
}

} // namespace RoR
