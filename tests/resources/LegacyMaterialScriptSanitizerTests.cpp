#include "LegacyMaterialScriptSanitizer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

std::string ReadEnvironment(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0U;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        std::free(value);
        return std::string();
    }
    const std::string result(value);
    std::free(value);
    return result;
#else
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

void TestBalancedScriptIsByteExact()
{
    const std::string input =
        "material clean\n"
        "{\n"
        "  technique { pass { } }\n"
        "}\n";
    const RoR::LegacyMaterialScriptSanitization result =
        RoR::SanitizeLegacyMaterialScript(input);
    CHECK(result.safe);
    CHECK(result.payload == input);
    CHECK(result.removed_unmatched_close_braces.empty());
    CHECK(result.rejection_reason.empty());
}

void TestStandaloneTopLevelCloseBraceIsRemoved()
{
    const std::string input =
        "material first\n"
        "{\n"
        "}\n"
        "} // legacy exporter duplicate\n"
        "material second\n"
        "{\n"
        "}\n";
    const std::string expected =
        "material first\n"
        "{\n"
        "}\n"
        " // legacy exporter duplicate\n"
        "material second\n"
        "{\n"
        "}\n";
    const RoR::LegacyMaterialScriptSanitization result =
        RoR::SanitizeLegacyMaterialScript(input);
    CHECK(result.safe);
    CHECK(result.payload == expected);
    CHECK(result.removed_unmatched_close_braces.size() == 1U);
    CHECK(result.removed_unmatched_close_braces[0].line == 4U);
    CHECK(result.removed_unmatched_close_braces[0].column == 1U);
}

void TestCrLfAndLineNumbersArePreserved()
{
    const std::string input =
        "material first\r\n"
        "{\r\n"
        "}\r\n"
        "  }\r\n"
        "material second\r\n"
        "{\r\n"
        "}\r\n";
    const RoR::LegacyMaterialScriptSanitization result =
        RoR::SanitizeLegacyMaterialScript(input);
    CHECK(result.safe);
    CHECK(result.payload.size() + 1U == input.size());
    CHECK(result.payload.find("\r\n  \r\n") != std::string::npos);
    CHECK(result.removed_unmatched_close_braces.size() == 1U);
    CHECK(result.removed_unmatched_close_braces[0].line == 4U);
    CHECK(result.removed_unmatched_close_braces[0].column == 3U);
}

void TestBracesInCommentsAndQuotedTokensAreIgnored()
{
    const std::string input =
        "/* } */\n"
        "material \"quoted-}-name\"\n"
        "{\n"
        "  // }\n"
        "}\n";
    const RoR::LegacyMaterialScriptSanitization result =
        RoR::SanitizeLegacyMaterialScript(input);
    CHECK(result.safe);
    CHECK(result.payload == input);
    CHECK(result.removed_unmatched_close_braces.empty());
}

void TestAmbiguousAndUnbalancedScriptsAreRejectedByteExact()
{
    const std::string mixed_line = "material broken { } }\n";
    const RoR::LegacyMaterialScriptSanitization mixed_result =
        RoR::SanitizeLegacyMaterialScript(mixed_line);
    CHECK(!mixed_result.safe);
    CHECK(mixed_result.payload == mixed_line);
    CHECK(mixed_result.removed_unmatched_close_braces.empty());
    CHECK(!mixed_result.rejection_reason.empty());

    const std::string unmatched_open = "material broken\n{\n";
    const RoR::LegacyMaterialScriptSanitization open_result =
        RoR::SanitizeLegacyMaterialScript(unmatched_open);
    CHECK(!open_result.safe);
    CHECK(open_result.payload == unmatched_open);
    CHECK(open_result.removed_unmatched_close_braces.empty());
    CHECK(!open_result.rejection_reason.empty());
}

void TestMultipleStandaloneRepairsAreDeterministic()
{
    const std::string input =
        "material one\n{\n}\n}\n"
        "material two\n{\n}\n  } // duplicate\n";
    const RoR::LegacyMaterialScriptSanitization first =
        RoR::SanitizeLegacyMaterialScript(input);
    const RoR::LegacyMaterialScriptSanitization second =
        RoR::SanitizeLegacyMaterialScript(input);
    CHECK(first.safe);
    CHECK(first.payload == second.payload);
    CHECK(first.removed_unmatched_close_braces.size() == 2U);
    CHECK(first.removed_unmatched_close_braces[0].line == 4U);
    CHECK(first.removed_unmatched_close_braces[1].line == 8U);
}

void TestPinnedPlanMetadataIsExact()
{
    const RoR::LegacyMaterialScriptEditPlan* plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3",
            "NeoQ2-0.material");
    CHECK(plan != nullptr);
    CHECK(
        std::string(plan->script_sha256) ==
        "03e17f9fab655321e7b266ce848e55d3ecd581d417e4f336f3a7928cd9d6e919");
    CHECK(plan->edit_count == 32U);
    CHECK(plan->edits[0].line == 30U);
    CHECK(
        std::string(plan->edits[0].replacement).find(
            "ambient 0.12 0.12 0.12 1") != std::string::npos);
    const RoR::LegacyMaterialScriptEditPlan* builds_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "NeoQ2-0-builds.material");
    CHECK(builds_plan != nullptr);
    CHECK(builds_plan->edit_count == 26U);
    const struct
    {
        const char* script_name;
        const char* script_sha256;
        std::size_t edit_count;
    } neoq20_grade_plans[] = {
        {"NeoQ2-0-asphalt.material",
         "6ce129e2f04aaca9fe8dd29b62b09781f3dca3c19b18d58450976e330b165ae6",
         8U},
        {"NeoQ2-0-concrete-road.material",
         "fe3c212dd0a1df62fa5c904575d8b0e61d440c42972c00f2792a1fcbab9354a4",
         10U},
        {"NeoQ2-0-vegetation.material",
         "63fd8844d1efe2393c3499678f06d9c7c09f757c11ae660f41141311ddb94484",
         15U},
        {"NeoQ2-0-SmfS.material",
         "0491e5ca22aec7150a5df80bf5eaf73136bd7c03e0ae5ae984f807bd4b7882d9",
         7U}};
    for (const auto& expected : neoq20_grade_plans)
    {
        const RoR::LegacyMaterialScriptEditPlan* grade_plan =
            RoR::FindLegacyMaterialScriptEditPlan(
                plan->archive_sha256,
                expected.script_name);
        CHECK(grade_plan != nullptr);
        if (grade_plan != nullptr)
        {
            CHECK(
                std::string(grade_plan->script_sha256) ==
                expected.script_sha256);
            CHECK(grade_plan->edit_count == expected.edit_count);
        }
    }
    const RoR::LegacyMaterialScriptEditPlan* city_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "NeoQueretaro.material");
    CHECK(city_plan != nullptr);
    CHECK(city_plan->edit_count == 47U);
    const std::size_t environment_edit_indexes[] = {
        11U, 12U, 24U, 25U};
    const std::size_t environment_lines[] = {
        512U, 513U, 1627U, 1628U};
    const char* environment_anchors[] = {
        "cubic_texture EnvironmentTexture combinedUVW",
        "env_map planar",
        "cubic_texture EnvironmentTexture combinedUVW",
        "env_map planar"};
    const char* environment_replacements[] = {
        "texture EnvironmentTexture cubic 0 PF_R8G8B8",
        "env_map cubic_reflection",
        "texture EnvironmentTexture cubic 0 PF_R8G8B8",
        "env_map cubic_reflection"};
    for (std::size_t index = 0U;
         index < sizeof(environment_lines) / sizeof(environment_lines[0]);
         ++index)
    {
        const RoR::LegacyMaterialScriptEdit& edit =
            city_plan->edits[environment_edit_indexes[index]];
        CHECK(
            edit.kind ==
            RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE);
        CHECK(edit.line == environment_lines[index]);
        CHECK(std::string(edit.expected) == environment_anchors[index]);
        CHECK(
            std::string(edit.replacement) ==
            environment_replacements[index]);
    }
    const std::size_t duplicate_lines[] = {
        1772U,
        1773U,
        1774U,
        1775U,
        1776U,
        1777U,
        1778U,
        1779U,
        1780U,
        1781U,
        1782U,
        1783U,
        1784U};
    const char* duplicate_anchors[] = {
        "material concretorojo",
        "{",
        "technique",
        "{",
        "pass",
        "{",
        "texture_unit",
        "{",
        "texture detalle-concreto-rojo.jpg",
        "}",
        "}",
        "}",
        "}"};
    const std::size_t duplicate_edit_begin = 27U;
    for (std::size_t index = 0U;
         index < sizeof(duplicate_lines) / sizeof(duplicate_lines[0]);
         ++index)
    {
        const RoR::LegacyMaterialScriptEdit& edit =
            city_plan->edits[duplicate_edit_begin + index];
        CHECK(
            edit.kind ==
            RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE);
        CHECK(edit.line == duplicate_lines[index]);
        CHECK(std::string(edit.expected) == duplicate_anchors[index]);
        CHECK(std::string(edit.replacement).empty());
    }
    for (std::size_t index = 0U; index < city_plan->edit_count; ++index)
    {
        if (index > 0U)
        {
            CHECK(
                city_plan->edits[index - 1U].line <
                city_plan->edits[index].line);
        }
        CHECK(
            city_plan->edits[index].line < 1698U ||
            city_plan->edits[index].line > 1710U);
    }
    const RoR::LegacyMaterialScriptEditPlan* furniture_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "streetfurniture.material");
    CHECK(furniture_plan != nullptr);
    CHECK(furniture_plan->edit_count == 5U);
    CHECK(
        std::string(furniture_plan->edits[0].expected) ==
        "texture barrier.dds");
    CHECK(
        std::string(furniture_plan->edits[0].replacement) ==
        "texture RoR/LegacyTextureFallback/ebeac2f0204f/"
        "0a9bd28b7f23/12d6ceb7bb9c58fb.dds");
    const RoR::LegacyMaterialScriptEditPlan* roads_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "dneroads.material");
    CHECK(roads_plan != nullptr);
    CHECK(roads_plan->edit_count == 2U);
    CHECK(
        std::string(roads_plan->edits[0].expected) ==
        "texture stopsign.dds");
    CHECK(
        std::string(roads_plan->edits[0].replacement) ==
        "texture RoR/LegacyTextureFallback/ebeac2f0204f/"
        "4cdcde3752be/f1065bf44e2295b7.dds");
    CHECK(
        RoR::FindLegacyMaterialScriptEditPlan(
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "NeoQ2-0.material") == nullptr);
    CHECK(
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "unplanned.material") == nullptr);
}

void TestPinnedNeoQ20PayloadsWhenAvailable()
{
    const std::string fixture_directory =
        ReadEnvironment("ROR_CITYWORLD_NEOQ20_MATERIAL_DIR");
    if (fixture_directory.empty())
    {
        return;
    }

    const char* scripts[] = {
        "NeoQ2-0.material",
        "NeoQ2-0-builds.material",
        "NeoQ2-0-asphalt.material",
        "NeoQ2-0-concrete-road.material",
        "NeoQ2-0-vegetation.material",
        "NeoQ2-0-SmfS.material"};
    const std::string archive_sha256 =
        "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";
    for (const char* script : scripts)
    {
        const std::string path = fixture_directory + "/" + script;
        std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
        CHECK(input.good());
        if (!input.good())
        {
            continue;
        }
        std::ostringstream payload_stream;
        payload_stream << input.rdbuf();
        const std::string payload = payload_stream.str();
        const RoR::LegacyMaterialScriptEditPlan* plan =
            RoR::FindLegacyMaterialScriptEditPlan(
                archive_sha256,
                script);
        CHECK(plan != nullptr);
        if (plan == nullptr)
        {
            continue;
        }
        const RoR::LegacyMaterialScriptPlanApplication applied =
            RoR::ApplyLegacyMaterialScriptEditPlan(
                *plan,
                plan->script_sha256,
                payload);
        CHECK(applied.applicable);
        CHECK(applied.safe);
        CHECK(applied.applied_edit_count == plan->edit_count);
        CHECK(applied.payload != payload);
        CHECK(
            applied.payload.find("lighting on") !=
            std::string::npos);
        CHECK(
            applied.payload.find("specular ") !=
            std::string::npos);
    }
}

void TestExactPlanAppliesTransactionally()
{
    const std::string input =
        "material clean\n"
        "{\n"
        "  old_token\n"
        "}\n"
        "}\n";
    const RoR::LegacyMaterialScriptEdit edits[] = {
        {RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
         3U,
         "old_token",
         "new_token"},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         5U,
         "}",
         ""}};
    const RoR::LegacyMaterialScriptEditPlan plan = {
        "archive",
        "fixture.material",
        "fixture-script",
        edits,
        sizeof(edits) / sizeof(edits[0])};

    const RoR::LegacyMaterialScriptPlanApplication applied =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            plan, "fixture-script", input);
    CHECK(applied.applicable);
    CHECK(applied.safe);
    CHECK(applied.applied_edit_count == 2U);
    CHECK(
        applied.payload ==
        "material clean\n"
        "{\n"
        "  new_token\n"
        "}\n"
        "\n");

    const RoR::LegacyMaterialScriptPlanApplication hash_mismatch =
        RoR::ApplyLegacyMaterialScriptEditPlan(plan, "wrong", input);
    CHECK(hash_mismatch.applicable);
    CHECK(!hash_mismatch.safe);
    CHECK(hash_mismatch.payload == input);
    CHECK(hash_mismatch.applied_edit_count == 0U);

    RoR::LegacyMaterialScriptEdit bad_edit = edits[0];
    bad_edit.line = 2U;
    RoR::LegacyMaterialScriptEditPlan bad_plan = plan;
    bad_plan.edits = &bad_edit;
    bad_plan.edit_count = 1U;
    const RoR::LegacyMaterialScriptPlanApplication anchor_mismatch =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            bad_plan, "fixture-script", input);
    CHECK(anchor_mismatch.applicable);
    CHECK(!anchor_mismatch.safe);
    CHECK(anchor_mismatch.payload == input);
}

void TestDuplicateMaterialBlockRemovalIsTransactional()
{
    const std::string block =
        "material repeated\n"
        "{\n"
        "  technique\n"
        "  {\n"
        "    pass\n"
        "    {\n"
        "      texture_unit\n"
        "      {\n"
        "        texture repeated.png\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    const std::string input =
        block +
        block +
        "material next\n"
        "{\n"
        "}\n";
    const RoR::LegacyMaterialScriptEdit edits[] = {
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         14U, "material repeated", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         15U, "{", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         16U, "technique", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         17U, "{", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         18U, "pass", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         19U, "{", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         20U, "texture_unit", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         21U, "{", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         22U, "texture repeated.png", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         23U, "}", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         24U, "}", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         25U, "}", ""},
        {RoR::LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
         26U, "}", ""}};
    const RoR::LegacyMaterialScriptEditPlan plan = {
        "archive",
        "fixture.material",
        "fixture-script",
        edits,
        sizeof(edits) / sizeof(edits[0])};

    const RoR::LegacyMaterialScriptPlanApplication applied =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            plan, "fixture-script", input);
    CHECK(applied.applicable);
    CHECK(applied.safe);
    CHECK(applied.applied_edit_count == 13U);
    CHECK(applied.payload.substr(0U, block.size()) == block);
    CHECK(
        applied.payload.find(
            "material repeated", block.size()) == std::string::npos);
    CHECK(applied.payload.find("material next") != std::string::npos);

    std::string changed = input;
    const std::size_t second_texture =
        changed.find("texture repeated.png", block.size());
    CHECK(second_texture != std::string::npos);
    changed.replace(
        second_texture,
        std::string("texture repeated.png").size(),
        "texture changed.png");
    const RoR::LegacyMaterialScriptPlanApplication rejected =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            plan, "fixture-script", changed);
    CHECK(rejected.applicable);
    CHECK(!rejected.safe);
    CHECK(rejected.payload == changed);
    CHECK(rejected.applied_edit_count == 0U);
    CHECK(!rejected.rejection_reason.empty());
}

void TestLegacyEnvironmentMapConversionIsTransactional()
{
    const std::string input =
        "material environment\n"
        "{\n"
        "  technique\n"
        "  {\n"
        "    pass\n"
        "    {\n"
        "      texture_unit\n"
        "      {\n"
        "        cubic_texture EnvironmentTexture combinedUVW\n"
        "        env_map planar\n"
        "      }\n"
        "      texture_unit\n"
        "      {\n"
        "        cubic_texture EnvironmentTexture combinedUVW\n"
        "        env_map planar\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n";
    const RoR::LegacyMaterialScriptEdit edits[] = {
        {RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
         9U,
         "cubic_texture EnvironmentTexture combinedUVW",
         "texture EnvironmentTexture cubic 0 PF_R8G8B8"},
        {RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
         10U,
         "env_map planar",
         "env_map cubic_reflection"},
        {RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
         14U,
         "cubic_texture EnvironmentTexture combinedUVW",
         "texture EnvironmentTexture cubic 0 PF_R8G8B8"},
        {RoR::LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
         15U,
         "env_map planar",
         "env_map cubic_reflection"}};
    const RoR::LegacyMaterialScriptEditPlan plan = {
        "archive",
        "fixture.material",
        "fixture-script",
        edits,
        sizeof(edits) / sizeof(edits[0])};

    const RoR::LegacyMaterialScriptPlanApplication applied =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            plan, "fixture-script", input);
    CHECK(applied.applicable);
    CHECK(applied.safe);
    CHECK(applied.applied_edit_count == 4U);
    CHECK(
        applied.payload ==
        "material environment\n"
        "{\n"
        "  technique\n"
        "  {\n"
        "    pass\n"
        "    {\n"
        "      texture_unit\n"
        "      {\n"
        "        texture EnvironmentTexture cubic 0 PF_R8G8B8\n"
        "        env_map cubic_reflection\n"
        "      }\n"
        "      texture_unit\n"
        "      {\n"
        "        texture EnvironmentTexture cubic 0 PF_R8G8B8\n"
        "        env_map cubic_reflection\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n");

    std::string changed = input;
    const std::size_t first_environment =
        changed.find(
            "cubic_texture EnvironmentTexture combinedUVW");
    CHECK(first_environment != std::string::npos);
    const std::size_t second_environment =
        changed.find(
            "cubic_texture EnvironmentTexture combinedUVW",
            first_environment + 1U);
    CHECK(second_environment != std::string::npos);
    changed.replace(
        second_environment,
        std::string(
            "cubic_texture EnvironmentTexture combinedUVW").size(),
        "cubic_texture ChangedEnvironment combinedUVW");
    const RoR::LegacyMaterialScriptPlanApplication rejected =
        RoR::ApplyLegacyMaterialScriptEditPlan(
            plan, "fixture-script", changed);
    CHECK(rejected.applicable);
    CHECK(!rejected.safe);
    CHECK(rejected.payload == changed);
    CHECK(rejected.applied_edit_count == 0U);
    CHECK(!rejected.rejection_reason.empty());
}

void TestCanonicalRepairPlanDigestsAreDomainSeparated()
{
    const std::string archive_sha256 =
        "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";
    const RoR::LegacyMaterialScriptEditPlan* plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            archive_sha256, "NeoQ2-0.material");
    CHECK(plan != nullptr);
    std::string applied;
    std::string repeated;
    std::string none;
    CHECK(RoR::ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
        *plan, "NeoQ2-0.material", plan->script_sha256, applied));
    CHECK(RoR::ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
        *plan, "NeoQ2-0.material", plan->script_sha256, repeated));
    CHECK(RoR::ComputeLegacyMaterialScriptNoRepairPlanSha256(
        archive_sha256, "NeoQ2-0.material", plan->script_sha256, none));
    CHECK(applied.size() == 64U);
    CHECK(applied == repeated);
    CHECK(applied != none);
    CHECK(none ==
        "94950a0d8dd46673d5003fa7995dd436354be4596222d551d38e3099cf352c35");
    CHECK(!RoR::ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
        *plan, "nested/NeoQ2-0.material", plan->script_sha256, repeated));
    CHECK(!RoR::ComputeLegacyMaterialScriptNoRepairPlanSha256(
        "not-a-digest", "NeoQ2-0.material", plan->script_sha256, repeated));
    RoR::LegacyMaterialScriptEditPlan oversized = *plan;
    oversized.edit_count =
        RoR::kLegacyMaterialScriptMaximumRepairPlanEdits + 1U;
    repeated = "sentinel";
    CHECK(!RoR::ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
        oversized, "NeoQ2-0.material", plan->script_sha256, repeated));
    CHECK(repeated == "sentinel");
}

} // namespace

int main()
{
    TestBalancedScriptIsByteExact();
    TestStandaloneTopLevelCloseBraceIsRemoved();
    TestCrLfAndLineNumbersArePreserved();
    TestBracesInCommentsAndQuotedTokensAreIgnored();
    TestAmbiguousAndUnbalancedScriptsAreRejectedByteExact();
    TestMultipleStandaloneRepairsAreDeterministic();
    TestPinnedPlanMetadataIsExact();
    TestPinnedNeoQ20PayloadsWhenAvailable();
    TestExactPlanAppliesTransactionally();
    TestDuplicateMaterialBlockRemovalIsTransactional();
    TestLegacyEnvironmentMapConversionIsTransactional();
    TestCanonicalRepairPlanDigestsAreDomainSeparated();
    return EXIT_SUCCESS;
}
