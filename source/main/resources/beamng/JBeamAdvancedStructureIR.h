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

/// @file JBeamAdvancedStructureIR.h
/// @brief Bounded, inert inventory of advanced JBeam structure sections.

#pragma once

#include "JBeamPartResolver.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// Immutable source snapshot and documented defaults used by this pass.
/// The builder never executes expressions, electrics, Lua, or controllers.
struct JBeamAdvancedDocumentationProfile
{
    std::string profile_id;
    std::string beamng_version;
    std::string hydros_url;
    std::string hydros_last_modified;
    std::string rails_url;
    std::string rails_last_modified;
    std::string thrusters_url;
    std::string thrusters_last_modified;
    std::string torsionbars_url;
    std::string torsionbars_last_modified;

    std::string hydro_input_source;
    double hydro_out_limit;
    double hydro_in_limit;
    double hydro_input_factor;
    double hydro_input_center;
    double hydro_in_rate;
    bool hydro_out_rate_inherits_in_rate;
    bool hydro_auto_center_rate_inherits_in_rate;
    double hydro_input_in_limit;
    double hydro_input_out_limit;

    bool slidenode_attached;
    bool slidenode_fix_to_rail;
    bool rail_looped;
    bool rail_capped;

    double thruster_factor;
    bool thruster_limit_is_flt_max;

    double torsion_precompression_angle;
    double torsion_precompression_time;
    bool torsion_spring2_inherits_spring;
    bool torsion_damp2_inherits_damp;

    JBeamAdvancedDocumentationProfile();
};

const JBeamAdvancedDocumentationProfile&
GetJBeamAdvancedDocumentationProfile();

enum class JBeamAdvancedSectionKind
{
    HYDROS,
    RAILS,
    RAILS2,
    SLIDENODES,
    THRUSTERS,
    TORSIONBARS
};

/// NATIVE_READY_STATIC_GEOMETRY means only that a validated, literal static
/// topology has a direct RoR counterpart. It does not authorize runtime
/// lowering. Every actuated or force-producing behavior remains inventory
/// only until a separately reviewed adapter provides behavioral parity.
enum class JBeamAdvancedBehavior
{
    NATIVE_READY_STATIC_GEOMETRY,
    INVENTORY_ONLY,
    PRESERVED_DISABLED_INERT_EXPRESSION,
    REJECTED_INVALID
};

enum class JBeamAdvancedSeverity
{
    WARNING,
    ERROR_SEVERITY
};

enum class JBeamAdvancedDiagnosticCode
{
    INVALID_RESOLVED_GRAPH,
    RESOLVED_PART_LIMIT,
    RESOLVED_GRAPH_DEPTH_LIMIT,
    RESOLVED_GRAPH_CYCLE,
    PART_BODY_NOT_OBJECT,
    SOURCE_RECORD_LIMIT,
    ENTRY_LIMIT,
    MODIFIER_LIMIT,
    EFFECTIVE_FIELD_LIMIT,
    NODE_COORDINATE_LIMIT,
    WORK_LIMIT,
    DIAGNOSTIC_LIMIT,
    RETAINED_BYTE_LIMIT,
    PRESERVED_VALUE_LIMIT,
    DUPLICATE_SECTION,
    INVALID_SECTION,
    INVALID_TABLE_HEADER,
    DUPLICATE_TABLE_HEADER,
    INVALID_TABLE_ENTRY,
    EXTRA_POSITIONAL_VALUE_PRESERVED,
    MISSING_REQUIRED_FIELD,
    INVALID_FIELD_TYPE,
    EXPRESSION_DISABLED,
    NON_FINITE_NUMBER,
    MISSING_NODE_REFERENCE,
    DUPLICATE_NODE_REFERENCE,
    DEGENERATE_NODE_GEOMETRY,
    INVALID_RAIL_NAME,
    DUPLICATE_RAIL_NAME,
    INVALID_RAIL_LINKS,
    MISSING_RAIL_REFERENCE,
    UNKNOWN_FIELD_PRESERVED
};

struct JBeamAdvancedPartIdentity
{
    std::size_t part_preorder_index;
    std::string part_name;
    std::string package_path;

    JBeamAdvancedPartIdentity();
};

struct JBeamAdvancedProvenance
{
    std::shared_ptr<const JBeamAdvancedPartIdentity> part;
    JBeamSourceSpan span;

    JBeamAdvancedProvenance();
    std::size_t PartPreorderIndex() const;
    const std::string& PartName() const;
    const std::string& PackagePath() const;
};

struct JBeamAdvancedDiagnostic
{
    JBeamAdvancedDiagnosticCode code;
    JBeamAdvancedSeverity severity;
    JBeamAdvancedProvenance provenance;
    JBeamAdvancedSectionKind section_kind;
    std::size_t source_record_index;
    std::size_t entry_index;
    std::string field_name;
    std::string detail;

    JBeamAdvancedDiagnostic();
};

struct JBeamAdvancedLimits
{
    std::size_t max_parts;
    std::size_t max_graph_depth;
    std::size_t max_source_records;
    /// Counts every authored rail definition, table modifier, data row, and
    /// malformed post-header table entry.
    std::size_t max_entries;
    std::size_t max_modifiers;
    std::size_t max_effective_fields;
    std::size_t max_node_coordinates;
    /// Zero still retains one terminal error diagnostic.
    std::size_t max_diagnostics;
    std::size_t max_retained_bytes;
    /// Bounds graph, section, header, modifier, field, row, link, and
    /// preserved-value admission work. Diagnostics have their own bound.
    std::size_t max_work_units;
    std::size_t max_preserved_value_work_units;
    std::size_t max_preserved_value_depth;
    std::size_t max_canonical_output_bytes;
    std::size_t max_canonical_work_units;

    JBeamAdvancedLimits();
};

struct JBeamAdvancedSourceRecord
{
    JBeamAdvancedSectionKind kind;
    std::size_t section_occurrence;
    JBeamAdvancedProvenance provenance;
    /// Exact duplicate-preserving section AST. It is never executed.
    std::shared_ptr<const JBeamValue> raw_value;

    JBeamAdvancedSourceRecord();
};

enum class JBeamAdvancedFieldOrigin
{
    INHERITED_DEFAULT,
    POSITIONAL_CELL,
    ROW_LOCAL_OVERRIDE
};

struct JBeamAdvancedField
{
    std::string name;
    JBeamAdvancedFieldOrigin origin;
    JBeamAdvancedProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_value;

    JBeamAdvancedField();
};

struct JBeamAdvancedModifier
{
    JBeamAdvancedSectionKind section_kind;
    std::size_t source_record_index;
    std::size_t entry_index;
    JBeamAdvancedProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_value;

    JBeamAdvancedModifier();
};

struct JBeamAdvancedEntry
{
    JBeamAdvancedBehavior behavior;
    std::size_t source_record_index;
    std::size_t source_entry_index;
    JBeamAdvancedProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_value;
    /// Sorted by exact field name. Duplicate/source-order assignment history
    /// remains in the containing source record and modifier records.
    std::vector<JBeamAdvancedField> effective_fields;

    JBeamAdvancedEntry();
};

struct JBeamAdvancedHydro
{
    JBeamAdvancedEntry entry;
    std::string node1;
    std::string node2;
    std::string input_source;
    /// Length ratios and input mapping are dimensionless. Rates retain the
    /// authored BeamNG values without guessing an undocumented unit.
    bool has_factor;
    double factor;
    double out_limit;
    double in_limit;
    double input_factor;
    double input_center;
    double in_rate;
    double out_rate;
    double auto_center_rate;
    bool has_steering_wheel_lock;
    double steering_wheel_lock;
    double input_in_limit;
    double input_out_limit;

    JBeamAdvancedHydro();
};

struct JBeamAdvancedRail
{
    JBeamAdvancedEntry entry;
    std::string name;
    std::vector<std::string> links;
    bool looped;
    bool capped;
    /// Exact legacy field is retained in effective_fields/raw source. This
    /// flag merely records whether a non-empty legacy `broken:` array exists.
    bool has_legacy_broken_links;

    JBeamAdvancedRail();
};

struct JBeamAdvancedSlideNode
{
    JBeamAdvancedEntry entry;
    std::string node;
    std::string rail_name;
    bool attached;
    bool fix_to_rail;
    bool has_tolerance;
    /// Meters.
    double tolerance;
    bool has_spring;
    /// N/m.
    double spring;
    bool has_strength;
    bool strength_is_flt_max;
    /// N, or the documented FLT_MAX sentinel.
    double strength;
    bool has_cap_strength;
    bool cap_strength_is_flt_max;
    /// N, or the documented FLT_MAX sentinel.
    double cap_strength;

    JBeamAdvancedSlideNode();
};

struct JBeamAdvancedThruster
{
    JBeamAdvancedEntry entry;
    /// BeamNG applies force to force_node in the direction from force_node
    /// toward direction_node.
    std::string direction_node;
    std::string force_node;
    double factor;
    bool thrust_limit_is_flt_max;
    /// N, or the documented FLT_MAX default/sentinel.
    double thrust_limit;
    std::string control;

    JBeamAdvancedThruster();
};

struct JBeamAdvancedTorsionBar
{
    JBeamAdvancedEntry entry;
    std::string lever1_node;
    std::string axis1_node;
    std::string axis2_node;
    std::string lever2_node;
    bool has_spring;
    /// Nm/rad.
    double spring;
    bool has_damp;
    /// Nm.s/rad.
    double damp;
    bool has_spring2;
    double spring2;
    bool has_damp2;
    double damp2;
    bool anisotropic;
    bool has_deform;
    /// Nm.
    double deform;
    bool has_strength;
    /// Nm.
    double strength;
    /// Radians and seconds respectively.
    double precompression_angle;
    double precompression_time;
    std::string name;

    JBeamAdvancedTorsionBar();
};

struct JBeamAdvancedRejectedEntry
{
    JBeamAdvancedEntry entry;
    JBeamAdvancedSectionKind section_kind;

    JBeamAdvancedRejectedEntry();
};

struct JBeamAdvancedStructureIR
{
    std::string documentation_profile_id;
    std::vector<std::shared_ptr<const JBeamAdvancedPartIdentity> > parts;
    std::vector<JBeamAdvancedSourceRecord> source_records;
    std::vector<JBeamAdvancedModifier> modifiers;
    std::vector<JBeamAdvancedHydro> hydros;
    std::vector<JBeamAdvancedRail> rails;
    std::vector<JBeamAdvancedSlideNode> slidenodes;
    std::vector<JBeamAdvancedThruster> thrusters;
    std::vector<JBeamAdvancedTorsionBar> torsionbars;
    std::vector<JBeamAdvancedRejectedEntry> rejected_entries;
    std::size_t authored_entry_count;
    std::size_t node_coordinate_row_count;
    std::size_t effective_field_count;
    std::size_t retained_byte_count;
    std::size_t work_unit_count;
    std::size_t preserved_value_work_unit_count;
    std::size_t canonical_output_byte_limit;
    std::size_t canonical_work_unit_limit;
    std::size_t canonical_value_depth_limit;
    std::vector<JBeamAdvancedDiagnostic> diagnostics;

    JBeamAdvancedStructureIR();
    bool IsValid() const;
};

/// Inventories the official hydros, legacy rails, rails2, slidenodes,
/// thrusters, and torsionbars contracts without lowering or executing them.
JBeamAdvancedStructureIR BuildJBeamAdvancedStructureIR(
    const JBeamResolvedGraph& graph,
    const JBeamAdvancedLimits& limits = JBeamAdvancedLimits());

/// Stable identity material for exact source ASTs, modifiers, resolved
/// defaults, classifications, and diagnostics. Returns empty on overflow or a
/// cyclic/manually-corrupted preserved value.
std::string SerializeCanonicalJBeamAdvancedStructureIR(
    const JBeamAdvancedStructureIR& ir);

const char* JBeamAdvancedDiagnosticCodeToString(
    JBeamAdvancedDiagnosticCode code);

} // namespace BeamNG
} // namespace RoR
