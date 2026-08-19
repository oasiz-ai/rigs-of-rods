#include "LegacyMaterialCompatibilityPlan.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <set>
#include <string>

namespace
{

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__     \
                      << ": " #condition << '\n';                               \
            std::exit(EXIT_FAILURE);                                            \
        }                                                                       \
    } while (false)

const std::string CITYWORLD_SHA =
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";

void TestArchiveIdentityAuthorityIsPublicAndExact()
{
    CHECK(
        CITYWORLD_SHA ==
        RoR::kCityWorldLegacyMaterialCompatibilityArchiveSha256);
    CHECK(
        std::string(64U, '0') !=
        RoR::kCityWorldLegacyMaterialCompatibilityArchiveSha256);
    CHECK(
        RoR::kCityWorldLegacyMaterialCompatibilityArchiveBytes ==
        158845395ULL);
    CHECK(RoR::ShouldProbeLegacyMaterialPrimaryArchive(true, true, true));
    CHECK(!RoR::ShouldProbeLegacyMaterialPrimaryArchive(false, true, true));
    CHECK(!RoR::ShouldProbeLegacyMaterialPrimaryArchive(true, false, true));
    CHECK(!RoR::ShouldProbeLegacyMaterialPrimaryArchive(true, true, false));
}

void TestPrimaryArchiveMountDispatchIsExclusive()
{
    int authenticated_mounts = 0;
    int ordinary_mounts = 0;
    int ordinary_registrations = 0;
    RoR::DispatchLegacyMaterialPrimaryArchiveMount(
        true,
        [&]() { ++authenticated_mounts; },
        [&]() { ++ordinary_mounts; },
        [&]() { ++ordinary_registrations; });
    CHECK(authenticated_mounts == 1);
    CHECK(ordinary_mounts == 0);
    CHECK(ordinary_registrations == 0);

    authenticated_mounts = 0;
    RoR::DispatchLegacyMaterialPrimaryArchiveMount(
        false,
        [&]() { ++authenticated_mounts; },
        [&]() { ++ordinary_mounts; },
        [&]() { ++ordinary_registrations; });
    CHECK(authenticated_mounts == 0);
    CHECK(ordinary_mounts == 1);
    CHECK(ordinary_registrations == 1);

    bool rejected = false;
    try
    {
        RoR::DispatchLegacyMaterialPrimaryArchiveMount(
            true,
            [&]() { throw std::runtime_error("mount rejected"); },
            [&]() { ++ordinary_mounts; },
            [&]() { ++ordinary_registrations; });
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(ordinary_mounts == 1);
    CHECK(ordinary_registrations == 1);
}

void TestExactAliasDoesNotUseFuzzySelection()
{
    const RoR::LegacyMaterialReferenceResolution resolution =
        RoR::ResolveLegacyMaterialReference(
            CITYWORLD_SHA,
            "Material.005/TEXFACE/sidewalk01.dds");
    CHECK(
        resolution.disposition ==
        RoR::LegacyMaterialReferenceDisposition::ALIAS);
    CHECK(
        resolution.target_material ==
        "modularbuildings/TEXFACE/sidewalk01.dds");

    CHECK(
        RoR::ResolveLegacyMaterialReference(
            CITYWORLD_SHA,
            "material.005/texface/sidewalk01.dds").disposition ==
        RoR::LegacyMaterialReferenceDisposition::NONE);
    CHECK(
        RoR::ResolveLegacyMaterialReference(
            std::string(64U, 'f'),
            "Material.005/TEXFACE/sidewalk01.dds").disposition ==
        RoR::LegacyMaterialReferenceDisposition::NONE);
}

void TestAmbiguousAndUndeclaredNamesUseReviewedFallbacks()
{
    const RoR::LegacyMaterialReferenceResolution ambiguous =
        RoR::ResolveLegacyMaterialReference(
            CITYWORLD_SHA, "Material.019/TEXFACE");
    CHECK(
        ambiguous.disposition ==
        RoR::LegacyMaterialReferenceDisposition::GENERATED_FALLBACK);
    CHECK(ambiguous.target_material.empty());
    CHECK(!ambiguous.color.high_specular);

    const RoR::LegacyMaterialReferenceResolution chrome =
        RoR::ResolveLegacyMaterialReference(CITYWORLD_SHA, "cromo");
    CHECK(
        chrome.disposition ==
        RoR::LegacyMaterialReferenceDisposition::GENERATED_FALLBACK);
    CHECK(chrome.color.high_specular);

    CHECK(
        RoR::ResolveLegacyMaterialReference(
            CITYWORLD_SHA, "not-reviewed").disposition ==
        RoR::LegacyMaterialReferenceDisposition::NONE);
}

void TestGeneratedResourceNamesArePortableStableAndDistinct()
{
    const std::string first =
        RoR::BuildLegacyMaterialFallbackResourceName(
            CITYWORLD_SHA, "Material.004");
    CHECK(
        first ==
        RoR::BuildLegacyMaterialFallbackResourceName(
            CITYWORLD_SHA, "Material.004"));
    CHECK(first.find("ebeac2f0204f/") != std::string::npos);
    CHECK(first.find(' ') == std::string::npos);
    CHECK(
        first !=
        RoR::BuildLegacyMaterialFallbackResourceName(
            CITYWORLD_SHA, "Material.001"));
}

void TestMissingTexturesAreExactAndArchiveScoped()
{
    struct TextureFixture
    {
        const char* script_sha256;
        const char* original;
    };
    const TextureFixture fixtures[] = {
        {"0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
         "barrier.dds"},
        {"4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
         "busstopsign.dds"},
        {"0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
         "chair.dds"},
        {"0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
         "roadclosed.dds"},
        {"4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
         "stopsign.dds"},
        {"0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
         "table.dds"},
        {"0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
         "umbrella.dds"}};

    std::set<std::string> generated_names;
    for (const TextureFixture& fixture : fixtures)
    {
        const std::string generated_name =
            RoR::BuildLegacyTextureFallbackResourceName(
                CITYWORLD_SHA,
                fixture.script_sha256,
                fixture.original);
        CHECK(
            generated_name.find(
                "RoR/LegacyTextureFallback/ebeac2f0204f/") == 0U);
        CHECK(
            generated_name.size() > 4U &&
            generated_name.compare(
                generated_name.size() - 4U, 4U, ".dds") == 0);
        CHECK(generated_names.insert(generated_name).second);
        RoR::LegacyMaterialColor generated_color = {0U, 0U, 0U, false};
        CHECK(
            RoR::ResolveLegacyMissingTexture(
                CITYWORLD_SHA, generated_name, generated_color));
        CHECK(
            !RoR::ResolveLegacyMissingTexture(
                CITYWORLD_SHA, fixture.original, generated_color));
    }
    CHECK(generated_names.size() == 7U);

    RoR::LegacyMaterialColor color = {0U, 0U, 0U, false};
    const std::string generated =
        RoR::BuildLegacyTextureFallbackResourceName(
            CITYWORLD_SHA,
            "4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
            "stopsign.dds");
    CHECK(
        generated ==
        "RoR/LegacyTextureFallback/ebeac2f0204f/"
        "4cdcde3752be/f1065bf44e2295b7.dds");
    CHECK(
        RoR::ResolveLegacyMissingTexture(
            CITYWORLD_SHA, generated, color));
    CHECK(color.red > color.green);
    CHECK(
        !RoR::ResolveLegacyMissingTexture(
            CITYWORLD_SHA, "stopsign.dds", color));
    CHECK(
        !RoR::ResolveLegacyMissingTexture(
            CITYWORLD_SHA, "parabusimagenlateral.jpg", color));
    CHECK(
        !RoR::ResolveLegacyMissingTexture(
            std::string(64U, '0'), generated, color));
    const std::string wrong_script_name =
        RoR::BuildLegacyTextureFallbackResourceName(
            CITYWORLD_SHA,
            std::string(64U, '1'),
            "stopsign.dds");
    CHECK(
        !RoR::ResolveLegacyMissingTexture(
            CITYWORLD_SHA, wrong_script_name, color));
}

void TestReplacementNamespaceIsDisjointFromReviewedRules()
{
    // The CityWorld Next overlay ships reviewed replacement textures under
    // this reserved namespace; they are reached only through script-repair
    // plans. No alias, material-fallback, or missing-texture rule may ever
    // resolve a replacement name, and no generated fallback name may enter
    // the reserved namespace.
    const std::string namespace_prefix = "cityworld_next_replacements/";
    const char* replacement_members[] = {
        "cityworld_next_replacements/asiaconcrete_1024.png",
        "cityworld_next_replacements/darkcrete_1024.png",
        "cityworld_next_replacements/redcrete_1024.png",
        "cityworld_next_replacements/betterbrickdiffuse_1024.png",
        "cityworld_next_replacements/lightgreybrick_1024.png",
        "cityworld_next_replacements/brickwall_darkred_1024.png",
        "cityworld_next_replacements/concretetan_1024.png",
        "cityworld_next_replacements/concretelightgrey_1024.png"};
    for (const char* member : replacement_members)
    {
        CHECK(
            RoR::ResolveLegacyMaterialReference(
                CITYWORLD_SHA, member).disposition ==
            RoR::LegacyMaterialReferenceDisposition::NONE);
        RoR::LegacyMaterialColor color = {0U, 0U, 0U, false};
        CHECK(
            !RoR::ResolveLegacyMissingTexture(
                CITYWORLD_SHA, member, color));
        const std::string fallback_name =
            RoR::BuildLegacyMaterialFallbackResourceName(
                CITYWORLD_SHA, member);
        CHECK(fallback_name.compare(
            0U, namespace_prefix.size(), namespace_prefix) != 0);
        const std::string texture_fallback_name =
            RoR::BuildLegacyTextureFallbackResourceName(
                CITYWORLD_SHA, CITYWORLD_SHA, member);
        CHECK(texture_fallback_name.compare(
            0U, namespace_prefix.size(), namespace_prefix) != 0);
    }

    // Alias targets stay outside the reserved namespace: an original name
    // is never rewritten to itself or into a replacement member.
    const char* alias_requests[] = {
        "modularbuildings/SOLID/TEX/betterbrickdiffuse.dds",
        "Material.005/TEXFACE/brickwall_darkred.dds",
        "Material.005/TEXFACE/betterbrickdiffuse.dds"};
    for (const char* requested : alias_requests)
    {
        const RoR::LegacyMaterialReferenceResolution resolution =
            RoR::ResolveLegacyMaterialReference(CITYWORLD_SHA, requested);
        CHECK(
            resolution.disposition ==
            RoR::LegacyMaterialReferenceDisposition::ALIAS);
        CHECK(resolution.target_material.compare(
            0U, namespace_prefix.size(), namespace_prefix) != 0);
        CHECK(resolution.target_material != requested);
    }
}

} // namespace

int main()
{
    TestArchiveIdentityAuthorityIsPublicAndExact();
    TestPrimaryArchiveMountDispatchIsExclusive();
    TestExactAliasDoesNotUseFuzzySelection();
    TestAmbiguousAndUndeclaredNamesUseReviewedFallbacks();
    TestGeneratedResourceNamesArePortableStableAndDistinct();
    TestMissingTexturesAreExactAndArchiveScoped();
    TestReplacementNamespaceIsDisjointFromReviewedRules();
    return EXIT_SUCCESS;
}
