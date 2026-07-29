#include "LegacyMaterialScriptSanitizer.h"

#include <cstdlib>
#include <iostream>
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
    CHECK(plan->edit_count == 2U);
    const RoR::LegacyMaterialScriptEditPlan* builds_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "NeoQ2-0-builds.material");
    CHECK(builds_plan != nullptr);
    CHECK(builds_plan->edit_count == 4U);
    const RoR::LegacyMaterialScriptEditPlan* city_plan =
        RoR::FindLegacyMaterialScriptEditPlan(
            plan->archive_sha256,
            "NeoQueretaro.material");
    CHECK(city_plan != nullptr);
    CHECK(city_plan->edit_count == 43U);
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
    const std::size_t duplicate_edit_begin = 23U;
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
    TestExactPlanAppliesTransactionally();
    TestDuplicateMaterialBlockRemovalIsTransactional();
    return EXIT_SUCCESS;
}
