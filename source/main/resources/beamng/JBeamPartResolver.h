/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file JBeamPartResolver.h
/// @brief Deterministic BeamNG part index and bounded slot-graph resolver.

#pragma once

#include "JBeamSyntax.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

enum class JBeamResolveSeverity
{
    WARNING,
    ERROR_SEVERITY
};

enum class JBeamResolveDiagnosticCode
{
    PACKAGE_DOCUMENT_NOT_OBJECT,
    PART_NOT_OBJECT,
    EMPTY_PART_NAME,
    MISSING_SLOT_TYPE,
    INVALID_SLOT_TYPE,
    DUPLICATE_SLOT_TYPE,
    DUPLICATE_PART,
    MULTIPLE_MAIN_PARTS,
    DUPLICATE_SLOT_SECTION,
    INVALID_SLOT_TABLE,
    INVALID_SLOT_ROW,
    MISSING_SLOT_FIELD,
    INVALID_SLOT_FIELD,
    DUPLICATE_SLOT,
    INVALID_SLOT_VARIABLE,
    DUPLICATE_TUNING_VARIABLE_SECTION,
    INVALID_TUNING_VARIABLE_TABLE,
    INVALID_TUNING_VARIABLE_ROW,
    MISSING_TUNING_VARIABLE_FIELD,
    INVALID_TUNING_VARIABLE_FIELD,
    INVALID_TUNING_VARIABLE_OVERRIDE,
    DUPLICATE_TUNING_VARIABLE,
    TUNING_VARIABLE_LIMIT,
    INDEX_PART_LIMIT,
    CONFIGURATION_NOT_OBJECT,
    INVALID_CONFIGURATION_SECTION,
    DUPLICATE_CONFIGURATION_SECTION,
    INVALID_PART_SELECTION,
    DUPLICATE_PART_SELECTION,
    INVALID_CONFIGURATION_VARIABLE,
    DUPLICATE_CONFIGURATION_VARIABLE,
    INDEX_INVALID,
    ROOT_PART_NOT_FOUND,
    ROOT_PART_NOT_MAIN,
    AMBIGUOUS_PART,
    MISSING_REQUIRED_PART,
    MISSING_OPTIONAL_PART,
    PART_NOT_ALLOWED_IN_SLOT,
    SLOT_CYCLE,
    RESOLVE_DEPTH_LIMIT,
    RESOLVED_PART_LIMIT,
    RESOLVED_VARIABLE_LIMIT,
    UNUSED_PART_SELECTION,
    INDEX_INPUT_ENTRY_LIMIT,
    PART_SELECTION_LIMIT,
    REQUEST_VARIABLE_LIMIT,
    DIAGNOSTIC_LIMIT
};

struct JBeamResolveDiagnostic
{
    JBeamResolveDiagnosticCode code;
    JBeamResolveSeverity severity;
    JBeamSourceSpan span;
    std::string part_name;
    std::string slot_name;
    std::string detail;

    JBeamResolveDiagnostic();
};

struct JBeamResolverLimits
{
    /// Counts every top-level object entry examined, including malformed
    /// entries which cannot become indexed parts.
    std::size_t max_input_entries;
    std::size_t max_indexed_parts;
    std::size_t max_resolved_parts;
    std::size_t max_depth;
    std::size_t max_slots_per_part;
    /// Applies both while parsing a .pc file and while resolving a manually
    /// constructed request.
    std::size_t max_request_selections;
    std::size_t max_request_variables;
    std::size_t max_variables_per_node;
    /// Per-table normalization gates used by slots, slots2, and authored
    /// variables. They prevent temporary normalized-row copies from escaping
    /// the resolver's deterministic resource contract.
    std::size_t max_table_normalize_work_units;
    std::size_t max_table_normalize_retained_bytes;
    /// Maximum retained diagnostics. Zero still retains one terminal
    /// diagnostic when any diagnostic is emitted so failure cannot appear
    /// successful.
    std::size_t max_diagnostics;

    JBeamResolverLimits();
};

struct JBeamPackageSource
{
    std::string package_path;
    JBeamValue document;
};

enum class JBeamSlotKind
{
    SLOTS,
    SLOTS2
};

enum class JBeamVariableOrigin
{
    AUTHORED_DEFAULT,
    CONFIGURATION,
    SLOT
};

/// Values are intentionally retained as parsed scalar JBeam values. In
/// particular, "$=" expressions and "$variable" strings are never evaluated
/// by this structural resolver.
struct JBeamVariableAssignment
{
    std::string name;
    JBeamValue value;
    JBeamSourceSpan span;
    JBeamVariableOrigin origin;
};

/// One selected part's documented tuning-variable row. Required range fields
/// are admitted as literal finite values. The duplicate-preserving raw row
/// remains in the owning part body so UI metadata outside this first semantic
/// subset is never lost or redundantly deep-copied.
struct JBeamTuningVariableDefinition
{
    std::string part_name;
    std::string package_path;
    std::string name;
    std::string type;
    std::string unit;
    std::string category;
    JBeamValue default_value;
    JBeamValue min_value;
    JBeamValue max_value;
    std::string title;
    std::string description;
    std::string sub_category;
    bool has_step_dis;
    JBeamValue step_dis;
    bool has_min_dis;
    JBeamValue min_dis;
    bool has_max_dis;
    JBeamValue max_dis;
    bool hide_in_ui;
    JBeamSourceSpan span;

    JBeamTuningVariableDefinition();
};

struct JBeamSlotDefinition
{
    JBeamSlotKind kind;
    std::string name;
    std::vector<std::string> allow_types;
    std::vector<std::string> deny_types;
    std::string default_part;
    std::string description;
    bool core_slot;
    JBeamSourceSpan span;
    std::vector<JBeamVariableAssignment> variables;

    JBeamSlotDefinition();
};

struct JBeamPartDefinition
{
    std::string name;
    std::string package_path;
    JBeamSourceSpan name_span;
    JBeamValue body;
    /// Source order and duplicates are retained. Matching treats this as a set.
    std::vector<std::string> slot_types;
    std::vector<JBeamSlotDefinition> slots;
    /// Ordered definitions from the effective authored `variables` table.
    std::vector<JBeamTuningVariableDefinition> tuning_variables;
};

struct JBeamPackageIndex
{
    std::vector<JBeamPartDefinition> parts;
    std::vector<JBeamResolveDiagnostic> diagnostics;

    bool IsValid() const;
};

/// Builds a canonical package index independent of input document enumeration.
/// Object-field order inside each document is retained because duplicate-key
/// last-assignment semantics are source-observable.
JBeamPackageIndex BuildJBeamPackageIndex(
    const std::vector<JBeamPackageSource>& sources,
    const JBeamResolverLimits& limits = JBeamResolverLimits());

/// Stable index identity material. Length-prefixed values preserve embedded
/// control bytes and every duplicate object field without executing content.
std::string SerializeCanonicalJBeamPackageIndex(
    const JBeamPackageIndex& index);

struct JBeamPartSelection
{
    std::string slot_name;
    std::string part_name;
    JBeamSourceSpan span;
};

struct JBeamResolveRequest
{
    /// Empty selects the package's unique slotType "main" part.
    std::string root_part_name;
    std::vector<JBeamPartSelection> part_selections;
    std::vector<JBeamVariableAssignment> variables;
};

struct JBeamConfigurationResult
{
    JBeamResolveRequest request;
    std::vector<JBeamResolveDiagnostic> diagnostics;

    bool IsValid() const;
};

/// Reads the documented .pc {"parts": {...}, "vars": {...}} subset.
/// Duplicate keys remain ordered and use last-assignment semantics.
JBeamConfigurationResult ParseJBeamConfiguration(
    const JBeamValue& configuration);

JBeamConfigurationResult ParseJBeamConfiguration(
    const JBeamValue& configuration,
    const JBeamResolverLimits& limits);

enum class JBeamResolvedSlotStatus
{
    RESOLVED,
    EMPTY,
    MISSING,
    NOT_ALLOWED,
    CYCLE,
    LIMIT_REJECTED
};

struct JBeamResolvedPartNode;

struct JBeamResolvedSlot
{
    JBeamSlotDefinition definition;
    bool explicitly_selected;
    std::string selected_part;
    JBeamResolvedSlotStatus status;
    std::shared_ptr<JBeamResolvedPartNode> child;

    JBeamResolvedSlot();
};

struct JBeamResolvedPartNode
{
    JBeamPartDefinition definition;
    std::vector<JBeamVariableAssignment> inherited_variables;
    std::vector<JBeamResolvedSlot> slots;
};

struct JBeamResolvedGraph
{
    /// Full ordered request history is retained so duplicate .pc assignments
    /// cannot collapse to the same canonical graph identity.
    JBeamResolveRequest request;
    std::shared_ptr<JBeamResolvedPartNode> root;
    /// Active authored definitions in deterministic resolved-part preorder.
    /// Duplicate names are retained; later rows are effective unless a .pc or
    /// descendant slot assignment overrides them.
    std::vector<JBeamTuningVariableDefinition> tuning_variables;
    std::vector<JBeamResolveDiagnostic> diagnostics;
    std::size_t resolved_part_count;

    JBeamResolvedGraph();
    bool IsValid() const;
};

JBeamResolvedGraph ResolveJBeamPartGraph(
    const JBeamPackageIndex& index,
    const JBeamResolveRequest& request = JBeamResolveRequest(),
    const JBeamResolverLimits& limits = JBeamResolverLimits());

/// Canonical pre-order graph serialization includes the selected definition's
/// full duplicate-preserving AST, all inherited variable assignments, empty or
/// rejected slot edges, and deterministic diagnostics.
std::string SerializeCanonicalJBeamResolvedGraph(
    const JBeamResolvedGraph& graph);

const JBeamVariableAssignment* FindEffectiveJBeamVariable(
    const JBeamResolvedPartNode& node,
    const std::string& name);

const JBeamTuningVariableDefinition* FindEffectiveJBeamTuningVariable(
    const JBeamResolvedGraph& graph,
    const std::string& name);

const char* JBeamResolveDiagnosticCodeToString(
    JBeamResolveDiagnosticCode code);

} // namespace BeamNG
} // namespace RoR
