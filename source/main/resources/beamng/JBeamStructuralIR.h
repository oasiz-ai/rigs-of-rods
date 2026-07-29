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

/// @file JBeamStructuralIR.h
/// @brief Bounded, deterministic structural subset of a resolved JBeam graph.

#pragma once

#include "JBeamExpressionEvaluator.h"
#include "JBeamPartResolver.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

enum class JBeamStructuralSeverity
{
    WARNING,
    ERROR_SEVERITY
};

enum class JBeamStructuralDiagnosticCode
{
    INVALID_RESOLVED_GRAPH,
    RESOLVED_PART_LIMIT,
    ROW_LIMIT,
    NODE_LIMIT,
    BEAM_LIMIT,
    TRIANGLE_LIMIT,
    DIAGNOSTIC_LIMIT,
    RETAINED_BYTE_LIMIT,
    PRESERVED_VALUE_LIMIT,
    NORMALIZATION_ERROR,
    NORMALIZATION_WARNING,
    PART_BODY_NOT_OBJECT,
    DUPLICATE_SECTION,
    INVALID_SECTION,
    INVALID_TABLE_HEADER,
    DUPLICATE_TABLE_HEADER,
    INVALID_TABLE_ROW,
    MISSING_REQUIRED_FIELD,
    AMBIGUOUS_REQUIRED_FIELD,
    INVALID_FIELD_TYPE,
    EXPRESSION_ERROR,
    EXPRESSION_LIMIT,
    EXPRESSION_DISABLED,
    UNSUPPORTED_COMPONENT_VALUE,
    INVALID_COMPONENT_PATH,
    INVALID_VARIABLE_VALUE,
    NON_FINITE_NUMBER,
    INVALID_NODE_WEIGHT,
    INVALID_BEAM_PARAMETER,
    DUPLICATE_NODE_ID,
    MISSING_NODE_REFERENCE,
    OPTIONAL_BEAM_SKIPPED,
    OPTIONAL_SURFACE_SKIPPED,
    DUPLICATE_VERTEX,
    DEGENERATE_BEAM,
    DEGENERATE_TRIANGLE,
    SPECIAL_BEAM_TYPE_DISABLED,
    UNKNOWN_SECTION,
    UNKNOWN_FIELD,
    UNSUPPORTED_FIELD,
    MISSING_REF_NODES,
    DUPLICATE_REF_NODES,
    DEGENERATE_REF_NODES,
    MISALIGNED_REF_NODES,
    MISALIGNED_REF_CORNERS
};

struct JBeamStructuralPartIdentity
{
    std::size_t part_preorder_index;
    std::string part_name;
    std::string package_path;

    JBeamStructuralPartIdentity();
};

struct JBeamStructuralProvenance
{
    /// Shared by every entity and diagnostic originating from one part.
    std::shared_ptr<const JBeamStructuralPartIdentity> part;
    /// Interned independently because malformed hand-built graphs may attach
    /// spans from a source other than the part's defining source.
    std::shared_ptr<const std::string> source_name;
    JBeamSourcePosition begin;
    JBeamSourcePosition end;

    JBeamStructuralProvenance();
    std::size_t PartPreorderIndex() const;
    const std::string& PartName() const;
    const std::string& PackagePath() const;
    const std::string& SourceName() const;
    JBeamSourceSpan SourceSpan() const;
};

/// Unsupported data is retained as an owned parsed value. Header-only
/// diagnostics have has_preserved_value == false.
struct JBeamStructuralDiagnostic
{
    JBeamStructuralDiagnosticCode code;
    JBeamStructuralSeverity severity;
    JBeamStructuralProvenance provenance;
    std::string section;
    std::size_t row_index;
    std::string field_name;
    std::string detail;
    bool has_preserved_value;
    std::shared_ptr<const JBeamValue> preserved_value;

    JBeamStructuralDiagnostic();
};

struct JBeamStructuralLimits
{
    std::size_t max_parts;
    /// Counts every post-header entry, including defaults and malformed rows.
    std::size_t max_rows;
    std::size_t max_nodes;
    /// Counts every authored beam row, including disabled optional/special rows.
    std::size_t max_beams;
    /// Counts emitted triangle primitives; each quad reserves two.
    std::size_t max_triangles;
    /// Zero still retains one terminal error diagnostic.
    std::size_t max_diagnostics;
    /// Bounds dynamic string/value payload retained by the result. Shared
    /// provenance strings are charged only once.
    std::size_t max_retained_bytes;
    /// Bounds graph work while measuring a preserved unsupported value. This
    /// also protects hand-built cyclic or unusually deep value graphs.
    std::size_t max_preserved_value_work_units;
    std::size_t max_preserved_value_depth;
    /// Per-expression bounds inherited by the allowlisted scalar evaluator.
    JBeamExpressionLimits expression_limits;
    /// Aggregate bounds across component discovery, inherited-variable
    /// materialization, and all structural-field evaluations. These prevent a
    /// large but individually valid table from multiplying the evaluator's
    /// per-expression allowance.
    std::size_t max_expression_evaluations;
    std::size_t max_expression_work_units;
    std::size_t max_component_nodes;
    std::size_t max_component_depth;
    /// Bounds canonical serialization. Serialization returns an empty string
    /// rather than constructing a partial or oversized identity.
    std::size_t max_canonical_output_bytes;
    /// Bounds top-level entities, diagnostics, and preserved-value nodes
    /// visited by canonical serialization, including hand-built IR objects.
    std::size_t max_canonical_work_units;

    JBeamStructuralLimits();
};

struct JBeamStructuralPart
{
    JBeamStructuralProvenance provenance;
};

struct JBeamStructuralNode
{
    std::string id;
    double x;
    double y;
    double z;
    double node_weight;
    bool node_weight_authored;
    JBeamStructuralProvenance provenance;
};

enum class JBeamStructuralBeamStatus
{
    ENABLED,
    PRESERVED_DISABLED_SPECIAL_TYPE,
    PRESERVED_DISABLED_OPTIONAL_REFERENCE
};

struct JBeamStructuralBeam
{
    std::string node_a;
    std::string node_b;
    std::size_t node_a_index;
    std::size_t node_b_index;
    std::string beam_type;
    bool optional;
    JBeamStructuralBeamStatus status;
    bool has_spring;
    double spring;
    bool has_damping;
    double damping;
    bool has_deform;
    bool deform_unbounded;
    double deform;
    bool has_strength;
    bool strength_unbounded;
    double strength;
    bool has_precompression;
    double precompression;
    JBeamStructuralProvenance provenance;

    JBeamStructuralBeam();
};

enum class JBeamStructuralTriangleOrigin
{
    TRIANGLE,
    QUAD_FIRST,
    QUAD_SECOND
};

enum class JBeamStructuralTriangleStatus
{
    ENABLED,
    PRESERVED_DISABLED_OPTIONAL_REFERENCE
};

struct JBeamStructuralTriangle
{
    std::string node_a;
    std::string node_b;
    std::string node_c;
    std::size_t node_a_index;
    std::size_t node_b_index;
    std::size_t node_c_index;
    bool optional;
    JBeamStructuralTriangleStatus status;
    JBeamStructuralTriangleOrigin origin;
    std::size_t authored_row_index;
    JBeamStructuralProvenance provenance;
};

struct JBeamStructuralRefFrame
{
    std::string reference;
    std::string back;
    std::string left;
    std::string up;
    std::string left_corner;
    std::string right_corner;
    std::size_t reference_index;
    std::size_t back_index;
    std::size_t left_index;
    std::size_t up_index;
    std::size_t left_corner_index;
    std::size_t right_corner_index;
    JBeamStructuralProvenance provenance;
};

struct JBeamStructuralIR
{
    std::vector<JBeamStructuralPart> parts;
    std::vector<JBeamStructuralNode> nodes;
    std::vector<JBeamStructuralBeam> beams;
    std::vector<JBeamStructuralTriangle> triangles;
    JBeamStructuralRefFrame ref_frame;
    bool has_ref_frame;
    std::size_t authored_row_count;
    /// Dynamic payload bytes charged while constructing this IR.
    std::size_t retained_byte_count;
    std::size_t canonical_output_byte_limit;
    std::size_t canonical_work_unit_limit;
    std::vector<JBeamStructuralDiagnostic> diagnostics;

    JBeamStructuralIR();
    bool IsValid() const;
};

/// Builds the dependency-free J2 structural subset in BeamNG's authored SI
/// coordinate system. Explicitly supported scalar fields resolve standalone
/// variables, namespace strings, and the allowlisted "$=" expression subset
/// against the resolved part's variables and globally merged scalar component
/// leaves. Unsupported fields and table-valued components remain inert.
JBeamStructuralIR BuildJBeamStructuralIR(
    const JBeamResolvedGraph& graph,
    const JBeamStructuralLimits& limits = JBeamStructuralLimits());

/// Stable identity material for the supported structural semantics, provenance,
/// disabled records, and deterministic diagnostic report.
std::string SerializeCanonicalJBeamStructuralIR(
    const JBeamStructuralIR& ir);

const char* JBeamStructuralDiagnosticCodeToString(
    JBeamStructuralDiagnosticCode code);

} // namespace BeamNG
} // namespace RoR
