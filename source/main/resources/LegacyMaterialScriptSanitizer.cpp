/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "LegacyMaterialScriptSanitizer.h"

#include <algorithm>
#include <cctype>

namespace RoR
{
namespace
{

const char CITYWORLD_ARCHIVE_SHA256[] =
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3";

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     81U, "scroll_z", "scroll_y"},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     140U, "}", ""}};

const LegacyMaterialScriptEdit CITYWORLD_NEOQ20_BUILDS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     675U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     676U, "}", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     701U, "texture_unit", ""},
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     702U, "}", ""}};

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
     2163U, "pistaaeropuerto.jpg", "pistaaeropuerto.JPG"}};

const LegacyMaterialScriptEdit CITYWORLD_BUSSTOP_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     125U, "color_op", "colour_op"}};

const LegacyMaterialScriptEdit CITYWORLD_DNEBUILDINGS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REMOVE_TRIMMED_LINE,
     2385U, "texture_unit", ""}};

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
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/0a9bd28b7f23/8e8aee4c22bb1900.dds"}};

const LegacyMaterialScriptEdit CITYWORLD_DNEROADS_EDITS[] = {
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     54U,
     "texture stopsign.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/4cdcde3752be/f1065bf44e2295b7.dds"},
    {LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,
     129U,
     "texture busstopsign.dds",
     "texture RoR/LegacyTextureFallback/ebeac2f0204f/4cdcde3752be/b40762545c57e6c3.dds"}};

const LegacyMaterialScriptEditPlan CITYWORLD_PLANS[] = {
    {CITYWORLD_ARCHIVE_SHA256,
     "NeoQ2-0.material",
     "03e17f9fab655321e7b266ce848e55d3ecd581d417e4f336f3a7928cd9d6e919",
     CITYWORLD_NEOQ20_EDITS,
     sizeof(CITYWORLD_NEOQ20_EDITS) /
         sizeof(CITYWORLD_NEOQ20_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "NeoQ2-0-builds.material",
     "95ce5cd0b9ca2bb4776baed80f89a0a2619a47fa54d943c88995787f0f7184ca",
     CITYWORLD_NEOQ20_BUILDS_EDITS,
     sizeof(CITYWORLD_NEOQ20_BUILDS_EDITS) /
         sizeof(CITYWORLD_NEOQ20_BUILDS_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "NeoQueretaro.material",
     "9dac0249de8f55b47d5672ab2f8750026abada4e460b2b3a60c4b11ccceec6a3",
     CITYWORLD_NEOQUERETARO_EDITS,
     sizeof(CITYWORLD_NEOQUERETARO_EDITS) /
         sizeof(CITYWORLD_NEOQUERETARO_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "busstopNJTnormalmapped.material",
     "5eba9fb3b4873e7f4ef81c65490ce9eb429d9245700dfac8cdd871b5ed857b49",
     CITYWORLD_BUSSTOP_EDITS,
     sizeof(CITYWORLD_BUSSTOP_EDITS) /
         sizeof(CITYWORLD_BUSSTOP_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "dnebuildings.material",
     "11bb735dfadd54f594bfa02e967014edcd67cb5b7fcda8b3c8c3668cea2dc420",
     CITYWORLD_DNEBUILDINGS_EDITS,
     sizeof(CITYWORLD_DNEBUILDINGS_EDITS) /
         sizeof(CITYWORLD_DNEBUILDINGS_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "streetfurniture.material",
     "0a9bd28b7f23cd028181e923cdcda2ffff19674e8f833b254f523845259b1be0",
     CITYWORLD_STREETFURNITURE_EDITS,
     sizeof(CITYWORLD_STREETFURNITURE_EDITS) /
         sizeof(CITYWORLD_STREETFURNITURE_EDITS[0])},
    {CITYWORLD_ARCHIVE_SHA256,
     "dneroads.material",
     "4cdcde3752bec2c6e8b0d73c464112ea35e013b91f5872c462ccb5dbfdcf1d21",
     CITYWORLD_DNEROADS_EDITS,
     sizeof(CITYWORLD_DNEROADS_EDITS) /
         sizeof(CITYWORLD_DNEROADS_EDITS[0])}};

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

} // namespace RoR
