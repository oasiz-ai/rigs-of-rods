/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Narrow, renderer-independent repair for legacy material scripts.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace RoR
{

struct LegacyMaterialScriptRepair
{
    std::size_t line;
    std::size_t column;
};

struct LegacyMaterialScriptSanitization
{
    bool safe;
    std::string payload;
    std::vector<LegacyMaterialScriptRepair> removed_unmatched_close_braces;
    std::string rejection_reason;
};

enum class LegacyMaterialScriptEditKind
{
    REMOVE_TRIMMED_LINE,
    REPLACE_TOKEN_ON_LINE
};

struct LegacyMaterialScriptEdit
{
    LegacyMaterialScriptEditKind kind;
    std::size_t line;
    const char* expected;
    const char* replacement;
};

struct LegacyMaterialScriptEditPlan
{
    const char* archive_sha256;
    const char* script_name;
    const char* script_sha256;
    const LegacyMaterialScriptEdit* edits;
    std::size_t edit_count;
};

struct LegacyMaterialScriptPlanApplication
{
    bool applicable;
    bool safe;
    std::string payload;
    std::size_t applied_edit_count;
    std::string rejection_reason;
};

/// Remove only standalone close braces which occur at top level.
///
/// This intentionally does not try to rewrite arbitrary OGRE syntax. A repair
/// is returned only when the remaining script is lexically balanced and every
/// removed token occupied an otherwise-empty line (apart from whitespace or a
/// trailing // comment). Ambiguous input is returned byte-for-byte with
/// `safe == false`.
LegacyMaterialScriptSanitization SanitizeLegacyMaterialScript(
    const std::string& payload);

/// Find the exact, rights-safe compatibility plan for a verified archive and
/// material script. The returned metadata contains hashes and edit anchors,
/// never source payload.
const LegacyMaterialScriptEditPlan* FindLegacyMaterialScriptEditPlan(
    const std::string& archive_sha256,
    const std::string& script_name);

/// Apply a plan only when both the full archive identity selected the plan and
/// the observed script hash matches. Every edit must match its exact line
/// anchor and the resulting script must remain balanced.
LegacyMaterialScriptPlanApplication ApplyLegacyMaterialScriptEditPlan(
    const LegacyMaterialScriptEditPlan& plan,
    const std::string& observed_script_sha256,
    const std::string& payload);

} // namespace RoR
