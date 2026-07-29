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

/// @file JBeamPressureWheelIR.h
/// @brief Bounded, inert pressure-wheel inventory from a resolved JBeam graph.

#pragma once

#include "JBeamPartResolver.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// Source-backed facts captured from the official BeamNG documentation
/// snapshot used to design this inventory pass. Recommendation and local
/// future-lowering policy fields are named as such; they are not presented as
/// BeamNG format limits. BuildJBeamPressureWheelIR never applies documented
/// defaults to missing required geometry and never claims behavioral parity.
struct JBeamPressureWheelDocumentationProfile
{
    std::string profile_id;
    std::string beamng_version;
    std::string wheel_documentation_url;
    std::string wheel_documentation_last_modified;
    std::string jbeam_syntax_documentation_url;
    std::string node_documentation_url;
    std::string beam_documentation_url;
    std::string vehicle_controller_documentation_url;
    std::string manual_shift_logic_documentation_url;
    std::string sequential_shift_logic_documentation_url;
    std::string dct_shift_logic_documentation_url;

    std::size_t recommended_minimum_num_rays;
    std::size_t recommended_maximum_num_rays;
    bool pressure_wheel_count_has_documented_maximum;
    std::size_t future_ror_lowering_maximum_wheels;

    double node_weight;
    bool node_collision;
    bool node_self_collision;
    bool node_static_collision;
    double node_friction_coefficient;
    double node_sliding_friction_coefficient;

    double normal_beam_spring;
    double normal_beam_damping;
    bool normal_beam_strength_is_flt_max;
    double normal_beam_deform;
    bool normal_beam_optional;
    double normal_beam_precompression;

    double stribeck_exponent;
    double tread_coefficient;
    double softness_coefficient;
    bool enable_tire_reinforcement_beams;
    bool enable_tire_support_beams;

    bool triangle_collision;
    bool tread_triangle_collision;
    bool side1_triangle_collision;
    bool side2_triangle_collision;
    bool hub_triangle_collision;
    bool hub_side1_triangle_collision;
    bool hub_side2_triangle_collision;
    double drag_coefficient;
    double skin_drag_coefficient;

    double brake_torque;
    double parking_torque;
    double brake_spring;
    bool enable_brake_thermals;
    double brake_diameter;
    double brake_mass;
    std::string brake_type;
    std::string rotor_material;
    std::string pad_material;
    double brake_input_split;
    double brake_split_coefficient;
    double squeal_coefficient_natural;
    double squeal_coefficient_low_speed;
    double squeal_coefficient_glazing;
    bool enable_abs;
    double abs_slip_ratio_target;
    double abs_hz;
    double brake_pressure_in_delay;
    double brake_pressure_out_delay;
    bool brake_venting_coefficient_has_documented_default;

    double low_shift_down_rpm;
    double high_shift_down_rpm;
    double low_shift_up_rpm;
    double high_shift_up_rpm;
    bool calculate_optimal_load_shift_points;
    double shift_down_rpm_offset_coefficient;
    double gearbox_decision_smoothing_down;
    double gearbox_decision_smoothing_up;
    double aggression_smoothing_up;
    double aggression_smoothing_down;
    bool use_smart_aggression_calculation;
    double aggression_hold_off_throttle_delay;
    double top_speed_limit;
    double reverse_speed_limit;
    double wheel_slip_up_threshold;
    double wheel_slip_down_threshold;
    double wheel_slip_smoothing_in;
    double wheel_slip_smoothing_out;
    std::string shift_logic_name;
    double transmission_shift_delay;
    double transmission_gear_change_delay;
    double neutral_selection_delay;

    double clutch_launch_start_rpm;
    double clutch_launch_target_rpm;
    double clutch_in_rate;
    double clutch_out_rate;
    double rev_match_throttle;

    JBeamPressureWheelDocumentationProfile();
};

/// Returns a process-lifetime immutable profile. Callers should persist
/// profile_id alongside any derived cache or comparison result.
const JBeamPressureWheelDocumentationProfile&
GetJBeamPressureWheelDocumentationProfile();

enum class JBeamPressureWheelSeverity
{
    WARNING,
    ERROR_SEVERITY
};

enum class JBeamPressureWheelDiagnosticCode
{
    INVALID_RESOLVED_GRAPH,
    RESOLVED_PART_LIMIT,
    RESOLVED_GRAPH_CYCLE,
    PART_BODY_NOT_OBJECT,
    SOURCE_RECORD_LIMIT,
    ENTRY_LIMIT,
    WHEEL_LIMIT,
    EFFECTIVE_FIELD_LIMIT,
    DIAGNOSTIC_LIMIT,
    RETAINED_BYTE_LIMIT,
    PRESERVED_VALUE_LIMIT,
    INVALID_SECTION,
    DUPLICATE_SECTION,
    INVALID_TABLE_HEADER,
    DUPLICATE_TABLE_HEADER,
    MISSING_REQUIRED_COLUMN,
    AMBIGUOUS_REQUIRED_COLUMN,
    INVALID_TABLE_ENTRY,
    MISSING_REQUIRED_FIELD,
    AMBIGUOUS_REQUIRED_FIELD,
    INVALID_FIELD_TYPE,
    EXPRESSION_DISABLED,
    NON_FINITE_NUMBER,
    INVALID_GEOMETRY,
    INVALID_WHEEL_DIRECTION,
    INVALID_NUM_RAYS,
    TOPOLOGY_COUNT_OVERFLOW,
    TOPOLOGY_NODE_LIMIT,
    TOPOLOGY_BEAM_LIMIT,
    DOCUMENTATION_AMBIGUITY_PRESERVED,
    DOCUMENTED_BEHAVIOR_INERT,
    UNKNOWN_FIELD_PRESERVED,
    INERT_SECTION_PRESERVED,
    INERT_SCALING_MODIFIER_PRESERVED
};

struct JBeamPressureWheelPartIdentity
{
    std::size_t part_preorder_index;
    std::string part_name;
    std::string package_path;

    JBeamPressureWheelPartIdentity();
};

struct JBeamPressureWheelProvenance
{
    std::shared_ptr<const JBeamPressureWheelPartIdentity> part;
    JBeamSourceSpan span;

    JBeamPressureWheelProvenance();
    std::size_t PartPreorderIndex() const;
    const std::string& PartName() const;
    const std::string& PackagePath() const;
};

struct JBeamPressureWheelDiagnostic
{
    JBeamPressureWheelDiagnosticCode code;
    JBeamPressureWheelSeverity severity;
    JBeamPressureWheelProvenance provenance;
    std::string section;
    std::size_t row_index;
    std::string field_name;
    std::string detail;

    JBeamPressureWheelDiagnostic();
};

struct JBeamPressureWheelLimits
{
    std::size_t max_parts;
    /// Counts pressureWheels table entries, including defaults and malformed
    /// entries. Header rows are excluded.
    std::size_t max_entries;
    /// Inventory admission capacity. This is independent of the currently
    /// proposed 64-wheel cap for a future RoR lowering implementation.
    std::size_t max_wheels;
    /// Counts pressureWheels, applicable scale* process modifiers, and
    /// explicitly inert controller/powertrain/Lua source sections retained
    /// by the result.
    std::size_t max_source_records;
    std::size_t max_effective_fields;
    /// Zero still retains one terminal diagnostic.
    std::size_t max_diagnostics;
    /// Conservative aggregate logical bytes retained by the inventory,
    /// excluding allocator bookkeeping and one fixed terminal diagnostic
    /// emitted when this limit is reached. The implementation applies an
    /// internal hard cap even when a caller supplies a larger value.
    std::size_t max_retained_bytes;
    std::size_t max_preserved_value_work_units;
    std::size_t max_preserved_value_depth;
    std::size_t max_approximation_generated_nodes;
    std::size_t max_approximation_generated_beams;
    std::size_t max_canonical_output_bytes;

    JBeamPressureWheelLimits();
};

enum class JBeamPressureWheelSourceKind
{
    PRESSURE_WHEELS,
    INERT_CONTROLLER_OR_POWERTRAIN,
    INERT_LUA,
    INERT_SCALING_MODIFIER
};

/// Owns an exact, duplicate-preserving copy of a relevant authored section.
/// Nothing in raw_value is executed.
struct JBeamPressureWheelSourceRecord
{
    JBeamPressureWheelSourceKind kind;
    std::string section_name;
    std::size_t section_occurrence;
    JBeamPressureWheelProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_value;

    JBeamPressureWheelSourceRecord();
};

enum class JBeamPressureWheelFieldOrigin
{
    INHERITED_DEFAULT,
    POSITIONAL_CELL,
    ROW_LOCAL_OVERRIDE
};

/// An effective assignment, plus an aliasing owner into a source record.
/// Earlier assignments and duplicate history remain present in the record's
/// raw_value.
struct JBeamPressureWheelField
{
    std::string name;
    JBeamPressureWheelFieldOrigin origin;
    JBeamPressureWheelProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_value;
};

enum class JBeamPressureWheelAdmission
{
    PRESERVED_NOT_ADMISSIBLE,
    SCHEMA_ADMISSIBLE_INVENTORY_ONLY
};

enum class JBeamPressureWheelRuntimePolicy
{
    INVENTORY_ONLY_NEVER_LOWER
};

struct JBeamPressureWheel
{
    JBeamPressureWheelAdmission admission;
    JBeamPressureWheelRuntimePolicy runtime_policy;
    bool has_inert_or_unimplemented_behavior;

    std::string name;
    std::string hub_group;
    std::string group;
    std::string node1;
    std::string node2;
    std::string node_s;
    bool node_s_disables_legacy_stabilizer;
    std::string node_arm;
    int wheel_direction;

    double radius;
    double hub_radius;
    double wheel_offset;
    double tire_width;
    double hub_width;
    bool has_tire;
    std::size_t num_rays;

    /// Conservative reservation for a future RoR Wheel2-style approximation:
    /// four generated nodes per ray; 24 base beams per ray; and one optional
    /// stabilizer beam per ray unless nodeS is the numeric 9999 sentinel.
    /// No topology is allocated and no behavioral parity is implied.
    std::size_t approximation_generated_nodes;
    std::size_t approximation_base_generated_beams;
    std::size_t approximation_stabilizer_beams;
    std::size_t approximation_generated_beams;

    std::size_t source_record_index;
    std::size_t source_entry_index;
    JBeamPressureWheelProvenance provenance;
    std::shared_ptr<const JBeamValue> raw_row;
    /// Sorted by exact field name for canonical lookup/serialization.
    std::vector<JBeamPressureWheelField> effective_fields;

    JBeamPressureWheel();
};

struct JBeamPressureWheelIR
{
    std::string documentation_profile_id;
    JBeamPressureWheelRuntimePolicy runtime_policy;
    std::vector<
        std::shared_ptr<const JBeamPressureWheelPartIdentity> > parts;
    std::vector<JBeamPressureWheelSourceRecord> source_records;
    std::vector<JBeamPressureWheel> wheels;
    std::size_t authored_entry_count;
    std::size_t approximation_generated_node_count;
    std::size_t approximation_generated_beam_count;
    std::size_t retained_byte_count;
    std::size_t canonical_output_byte_limit;
    std::size_t canonical_value_depth_limit;
    std::vector<JBeamPressureWheelDiagnostic> diagnostics;

    JBeamPressureWheelIR();
    bool IsValid() const;
    bool AllWheelsSchemaAdmissible() const;
};

/// Inventories pressureWheels in deterministic resolved-part preorder. The
/// pass validates only a literal, bounded core contract. Required geometry is
/// never guessed; expressions, controller/powertrain state, and Lua remain
/// inert; and no RigDef or runtime vehicle is produced.
JBeamPressureWheelIR BuildJBeamPressureWheelIR(
    const JBeamResolvedGraph& graph,
    const JBeamPressureWheelLimits& limits =
        JBeamPressureWheelLimits());

/// Stable identity material for the exact profile, source records, effective
/// fields, admission decisions, topology reservations, and diagnostics.
/// Returns an empty string for cyclic/manual IR values or output overflow.
std::string SerializeCanonicalJBeamPressureWheelIR(
    const JBeamPressureWheelIR& ir);

const char* JBeamPressureWheelDiagnosticCodeToString(
    JBeamPressureWheelDiagnosticCode code);

} // namespace BeamNG
} // namespace RoR
