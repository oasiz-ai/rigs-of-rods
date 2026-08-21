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

/// @file JBeamToRigDef.h
/// @brief Fail-closed JBeam structural IR to spawn-ready RigDef adapter.

#pragma once

#include "JBeamStructuralIR.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RigDef {
struct Document;
typedef std::shared_ptr<Document> DocumentPtr;
}

namespace RoR {
namespace BeamNG {

struct JBeamHydroRuntimePlanSet;

/// BeamNG's defaults for ordinary structural beams. These are deliberately
/// adapter-owned rather than inherited from RoR's truck parser.
extern const float JBEAM_RIGDEF_DEFAULT_BEAM_SPRING;
extern const float JBEAM_RIGDEF_DEFAULT_BEAM_DAMPING;
extern const float JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM;

/// ActorSpawner stores valid node indices in a uint16_t and reserves 65535 as
/// the invalid sentinel. Indices 0 through 65534 therefore allow 65535 nodes.
extern const std::size_t JBEAM_RIGDEF_RUNTIME_NODE_LIMIT;
/// Actor stores the live beam count and beam indices in signed integers.
/// The adapter also keeps a substantially lower safety ceiling so a caller
/// cannot disable bounded admission by passing SIZE_MAX.
extern const std::size_t JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT;
/// Actor::ar_cabs has room for exactly MAX_CABS topology triangles.
extern const std::size_t JBEAM_RIGDEF_RUNTIME_CAB_LIMIT;
/// Hard adapter envelopes for hand-built IR objects. These are independent
/// from the enabled runtime limits above: disabled records and upstream
/// diagnostics still consume admission work and cannot bypass these ceilings.
extern const std::size_t JBEAM_RIGDEF_INPUT_RECORD_LIMIT;
extern const std::size_t JBEAM_RIGDEF_WORK_UNIT_LIMIT;
extern const std::size_t JBEAM_RIGDEF_DIAGNOSTIC_LIMIT;
extern const std::size_t JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT;

enum class JBeamToRigDefDiagnosticCode
{
    INVALID_DOCUMENT_NAME,
    INVALID_STRUCTURAL_IR,
    MISSING_REF_FRAME,
    INPUT_RECORD_LIMIT,
    WORK_LIMIT,
    DIAGNOSTIC_LIMIT,
    DIAGNOSTIC_DETAIL_LIMIT,
    NODE_LIMIT,
    BEAM_LIMIT,
    MISSING_STRUCTURAL_BEAM,
    CAB_LIMIT,
    INVALID_NODE_ID,
    DUPLICATE_NODE_ID,
    INVALID_ENTITY_STATE,
    INVALID_NODE_REFERENCE,
    DUPLICATE_VERTEX,
    SOURCE_LINE_LIMIT,
    NON_FINITE_VALUE,
    FLOAT_NARROWING,
    INVALID_NODE_MASS,
    UNSUPPORTED_NODE_COLLISION_MODE,
    TOTAL_MASS_OVERFLOW,
    INVALID_CENTER_OF_MASS,
    INVALID_BOUNDS,
    DEGENERATE_BEAM,
    INVALID_BEAM_PARAMETER,
    BEAM_LENGTH_OVERFLOW,
    DEGENERATE_TRIANGLE,
    INVALID_REF_FRAME,
    MISALIGNED_REF_FRAME,
    MISALIGNED_REF_CORNERS,
    INVALID_HYDRO_RUNTIME_PLAN,
    HYDRO_RUNTIME_LIMIT,
    ALLOCATION_FAILURE,
    RIGDEF_CONSTRUCTION_FAILURE
};

enum class JBeamToRigDefEntityKind
{
    DOCUMENT,
    STRUCTURAL_IR,
    NODE,
    BEAM,
    HYDRO,
    TRIANGLE,
    REF_FRAME
};

struct JBeamToRigDefDiagnostic
{
    JBeamToRigDefDiagnosticCode code;
    JBeamToRigDefEntityKind entity_kind;
    /// Source-order index within the corresponding IR vector. SIZE_MAX means
    /// that the diagnostic applies to the whole document or ref frame.
    std::size_t source_index;
    JBeamStructuralProvenance provenance;
    std::string detail;

    JBeamToRigDefDiagnostic();
};

/// Optional stricter admission limits. Runtime limits are always enforced even
/// when a caller supplies a larger value.
struct JBeamToRigDefLimits
{
    /// Counts every node, beam, and triangle input record, including disabled
    /// records which do not produce runtime objects.
    std::size_t max_input_records;
    /// Conservative visit budget for all bounded preflight record scans.
    std::size_t max_work_units;
    /// Bounds both upstream diagnostics inspected and adapter diagnostics
    /// retained. Zero still retains one terminal limit diagnostic.
    std::size_t max_diagnostics;
    /// Bounds the combined detail strings retained by adapter diagnostics.
    std::size_t max_diagnostic_detail_bytes;
    /// Enabled runtime-object limits remain separate from the input envelope.
    std::size_t max_nodes;
    std::size_t max_beams;
    std::size_t max_cab_triangles;

    JBeamToRigDefLimits();
};

struct JBeamRigDefPoint3
{
    float x;
    float y;
    float z;

    JBeamRigDefPoint3();
    JBeamRigDefPoint3(float x_value, float y_value, float z_value);
};

struct JBeamRigDefBeamPlan
{
    std::size_t source_index;
    float spring;
    float damping;
    float deform;
    float strength;
    float rest_length_scale;
    bool support;
    float extension_break_limit;
    float geometric_length;
    float scaled_rest_length;

    JBeamRigDefBeamPlan();
};

struct JBeamToRigDefMetrics
{
    std::size_t enabled_beam_count;
    std::size_t enabled_cab_triangle_count;
    /// Sum in binary64 of the exact binary32 masses emitted to RigDef.
    double total_mass_kg;
    /// Actor's binary32 accumulation in final spawn order.
    float runtime_total_mass_kg;
    float runtime_total_beam_length;
    JBeamRigDefPoint3 center_of_mass;
    JBeamRigDefPoint3 bounds_min;
    JBeamRigDefPoint3 bounds_max;

    JBeamToRigDefMetrics();
};

/// Dependency-light, allocation-bounded plan used by the production adapter.
/// Positions and physical values have already been narrowed to the exact
/// binary32 values that will be handed to ActorSpawner.
struct JBeamToRigDefPreflightResult
{
    /// Source node indices in final RigDef order: ref node first, followed by
    /// every remaining node in stable source order.
    std::vector<std::size_t> node_source_order;
    /// Indexed by source node index.
    std::vector<JBeamRigDefPoint3> transformed_nodes;
    /// Indexed by source node index.
    std::vector<float> node_masses;
    /// Enabled NORMAL beams only, in stable source order.
    std::vector<JBeamRigDefBeamPlan> beams;
    /// Enabled triangles only, in stable source order.
    std::vector<std::size_t> triangle_source_indices;
    JBeamToRigDefMetrics metrics;
    std::vector<JBeamToRigDefDiagnostic> diagnostics;

    bool IsValid() const;
};

/// Validates every value ActorSpawner will consume before a RigDef object is
/// allocated. Invalid results contain diagnostics and no usable plan vectors.
JBeamToRigDefPreflightResult PreflightJBeamToRigDef(
    const JBeamStructuralIR& ir,
    const std::string& document_name,
    const JBeamToRigDefLimits& limits = JBeamToRigDefLimits());

#if !defined(ROR_JBEAM_TO_RIGDEF_PREFLIGHT_ONLY)
/// Returns a fresh, spawn-ready document on success. On any error it returns a
/// null DocumentPtr, replaces `diagnostics`, and never exposes a partial
/// document. This adapter does not invoke SequentialImporter or ActorSpawner.
RigDef::DocumentPtr ConvertJBeamToRigDef(
    const JBeamStructuralIR& ir,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits = JBeamToRigDefLimits());

/// Converts the structural IR and an all-or-none hydro runtime plan set into
/// one fresh document. Every hydro plan is revalidated against the exact
/// structural node identity and binary32 spawn geometry before any document
/// is published. This remains an internal conversion boundary: the plan set
/// does not by itself prove package or resolver authority.
RigDef::DocumentPtr ConvertJBeamToRigDefWithHydroRuntimePlans(
    const JBeamStructuralIR& ir,
    const JBeamHydroRuntimePlanSet& hydro_plans,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits = JBeamToRigDefLimits());
#endif

const char* ToString(JBeamToRigDefDiagnosticCode code);
const char* ToString(JBeamToRigDefEntityKind kind);

} // namespace BeamNG
} // namespace RoR
