/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

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

/// @file
/// @brief Dependency-free deterministic contact-force conservation telemetry.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR {
namespace ContactConservation {

static const std::uint32_t SCHEMA_VERSION = 3;
static const std::size_t HIT_NODE_INDEX = 0;
static const std::size_t SURFACE_NODE_A_INDEX = 1;
static const std::size_t SURFACE_NODE_B_INDEX = 2;
static const std::size_t SURFACE_NODE_C_INDEX = 3;
static const std::size_t NODE_COUNT = 4;
/// Exact ceiling of 65,536 contacts per step across the maximum 16,777,216
/// supported deterministic trace steps.
static const std::uint64_t MAX_AGGREGATE_CONTACTS =
    UINT64_C(1099511627776);
static const std::uint64_t MAX_AGGREGATE_FIXED_STEPS =
    UINT64_C(16777216);
/// The deterministic parallel contact path admits at most 65,536 contacts in
/// one fixed step. Each point/triangle transaction contributes exactly four
/// node impulses to the whole-step audit.
static const std::size_t MAX_STEP_CONTACTS = 65536;
static const std::size_t MAX_STEP_NODE_CONTRIBUTIONS =
    MAX_STEP_CONTACTS * NODE_COUNT;
static const std::uint64_t MAX_AGGREGATE_NODE_CONTRIBUTIONS =
    MAX_AGGREGATE_CONTACTS * static_cast<std::uint64_t>(NODE_COUNT);
static const std::uint32_t MAX_NODE_INDEX = UINT32_C(65534);

/// Stable product identity for one solver node. Actor IDs are positive for
/// live actors; node indices retain the native uint16 range without importing
/// product headers into this dependency-free boundary.
struct NodeKey
{
    std::int32_t actor = 0;
    std::uint32_t node = 0;

    NodeKey() = default;
    NodeKey(std::int32_t actor_id, std::uint32_t node_index):
        actor(actor_id),
        node(node_index)
    {
    }

    bool operator<(const NodeKey& other) const
    {
        if (actor != other.actor)
            return actor < other.actor;
        return node < other.node;
    }

    bool operator==(const NodeKey& other) const
    {
        return actor == other.actor && node == other.node;
    }
};

struct Vector3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vector3() = default;
    Vector3(double x_value, double y_value, double z_value):
        x(x_value),
        y(y_value),
        z(z_value)
    {
    }
};

struct NodeState
{
    Vector3 position_m;
    Vector3 velocity_mps;
    double mass_kg = 0.0;

    /// Stored as a checked byte rather than bool so hostile/corrupt boundary
    /// values fail closed instead of becoming implementation-defined truth.
    std::uint8_t movable = 1;
};

/// Exact binary32 force components suitable for direct construction of the
/// Ogre::Real vectors used by the production solver. Telemetry widens these
/// exact values back to binary64; it never audits an ideal force that would be
/// changed by a later cast.
struct AppliedVector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    AppliedVector3() = default;
    AppliedVector3(float x_value, float y_value, float z_value):
        x(x_value),
        y(y_value),
        z(z_value)
    {
    }
};

/// One hit node against one triangle. `force_on_hit_n` is the complete contact
/// force chosen by the owning collision law; this kernel only distributes it,
/// closes the internal force pair, and measures its one-step consequences.
struct Input
{
    std::uint32_t schema_version = SCHEMA_VERSION;
    NodeState hit_node;
    std::array<NodeState, 3> surface_nodes;
    std::array<double, 3> barycentric = {{0.0, 0.0, 0.0}};
    Vector3 force_on_hit_n;
    double time_step_s = 0.0;
};

struct Telemetry
{
    /// Accepted weights are normalized once in stable A/B/C order.
    std::array<double, 3> normalized_barycentric = {{0.0, 0.0, 0.0}};
    Vector3 surface_contact_position_m;

    /// Order: hit node, then surface A, B, and C.
    std::array<AppliedVector3, NODE_COUNT> applied_forces_n;
    /// Exact binary64 differences between widened binary32 accumulator values
    /// observed after and before production's four `+=` operations.
    std::array<Vector3, NODE_COUNT>
        observed_force_accumulator_deltas_n;
    std::array<Vector3, NODE_COUNT> impulses_ns;

    Vector3 linear_impulse_residual_ns;
    double normalized_linear_impulse_residual = 0.0;

    /// Net angular impulse about `surface_contact_position_m`.
    Vector3 angular_impulse_delta_nms;
    double angular_impulse_delta_magnitude_nms = 0.0;

    /// Work evaluated from pre-step velocities of movable nodes, followed by
    /// their exact kinetic-energy change. Fixed-node reaction work is excluded
    /// because the solver does not apply J/m to those nodes. Their difference
    /// is the positive quadratic integration term for movable nodes only.
    double isolated_contact_work_j = 0.0;
    double isolated_contact_kinetic_energy_delta_j = 0.0;
    double isolated_contact_integration_energy_delta_j = 0.0;
};

/// One exact node impulse observed after production's binary32 force
/// accumulator mutation. The contact ordinal and slot retain canonical input
/// order after records are grouped by node identity during finalization.
struct StepNodeContribution
{
    NodeKey key;
    NodeState node;
    Vector3 impulse_ns;
    std::uint64_t contact_ordinal = 0;
    std::uint8_t node_slot = 0;
};

/// Reused bounded scratch state for one fixed step. Storage may grow only while
/// opt-in deterministic trace evidence is active. Append failure is reported
/// transactionally and must never truncate the product collision response.
struct StepAccumulator
{
    std::uint32_t schema_version = SCHEMA_VERSION;
    std::uint64_t contact_count = 0;
    double time_step_s = 0.0;
    double summed_isolated_contact_work_j = 0.0;
    double summed_isolated_contact_kinetic_energy_delta_j = 0.0;
    double summed_isolated_contact_integration_energy_delta_j = 0.0;
    std::vector<StepNodeContribution> node_contributions;
};

/// Exact contact-only energy attribution after all impulses sharing a node in
/// one fixed step have been reduced together. It deliberately excludes forces
/// from beams, gravity, tyres, FreeForces, and other solver systems.
struct StepTelemetry
{
    std::uint32_t schema_version = SCHEMA_VERSION;
    std::uint64_t contact_count = 0;
    std::uint64_t unique_node_count = 0;
    std::uint64_t shared_node_count = 0;
    std::uint64_t maximum_node_contact_multiplicity = 0;
    double summed_isolated_contact_work_j = 0.0;
    double summed_isolated_contact_kinetic_energy_delta_j = 0.0;
    double summed_isolated_contact_integration_energy_delta_j = 0.0;
    double whole_step_contact_work_j = 0.0;
    double whole_step_contact_kinetic_energy_delta_j = 0.0;
    double whole_step_contact_integration_energy_delta_j = 0.0;
    /// Difference between grouped whole-step and isolated-contact quadratic
    /// integration energy. Linear work is intentionally excluded so floating
    /// summation order cannot masquerade as a shared-node interaction.
    double shared_node_cross_term_j = 0.0;
};

/// Bounded stable-order aggregate for a fixed physics step or scenario. The
/// caller must feed contacts in canonical contact-key order. No allocation or
/// hidden synchronization occurs here.
struct Aggregate
{
    std::uint32_t schema_version = SCHEMA_VERSION;
    std::uint64_t contact_count = 0;
    double maximum_normalized_linear_impulse_residual = 0.0;
    double maximum_angular_impulse_delta_magnitude_nms = 0.0;
    Vector3 summed_angular_impulse_delta_nms;
    /// These remain stable sums of isolated-contact attributions for backward
    /// comparison with schema 2.
    double summed_isolated_contact_work_j = 0.0;
    double summed_isolated_contact_kinetic_energy_delta_j = 0.0;
    double summed_isolated_contact_integration_energy_delta_j = 0.0;

    /// Exact per-contact sums since the last finalized fixed-step commit.
    /// These independently bind StepTelemetry to the Aggregate without
    /// reconstructing floating-point deltas from scenario totals.
    double pending_step_isolated_contact_work_j = 0.0;
    double pending_step_isolated_contact_kinetic_energy_delta_j = 0.0;
    double pending_step_isolated_contact_integration_energy_delta_j = 0.0;

    /// Schema 3 groups every movable node's exact observed contact impulses
    /// before evaluating the quadratic integration term. Counts are summed
    /// per fixed step, so the same physical node may contribute once to each
    /// audited step.
    std::uint64_t audited_fixed_step_count = 0;
    std::uint64_t whole_step_contact_count = 0;
    std::uint64_t summed_unique_node_count = 0;
    std::uint64_t summed_shared_node_count = 0;
    std::uint64_t maximum_node_contact_multiplicity = 0;
    double summed_whole_step_contact_work_j = 0.0;
    double summed_whole_step_contact_kinetic_energy_delta_j = 0.0;
    double summed_whole_step_contact_integration_energy_delta_j = 0.0;
    double summed_shared_node_cross_term_j = 0.0;
};

enum class Error
{
    NONE,
    UNSUPPORTED_SCHEMA,
    NONFINITE_INPUT,
    INVALID_TIME_STEP,
    INVALID_MASS,
    INVALID_NODE_MOBILITY,
    INVALID_NODE_KEY,
    INVALID_BARYCENTRIC_COORDINATES,
    DEGENERATE_SURFACE_TRIANGLE,
    FORCE_OUT_OF_BINARY32_RANGE,
    LINEAR_IMPULSE_RESIDUAL_EXCEEDED,
    INVALID_TELEMETRY,
    CONTACT_LIMIT_EXCEEDED,
    INCONSISTENT_NODE_STATE,
    STORAGE_FAILURE,
    NUMERIC_RANGE
};

const char* ErrorToString(Error error);

/// Validates and evaluates one internal point/triangle contact transaction.
/// `output` remains byte-for-byte unchanged on every failure.
Error Evaluate(const Input& input, Telemetry& output);

/// Audits the exact differences between four binary32 accumulator observations
/// captured before and after production's `+=` operations. No redistribution
/// or additional narrowing is performed.
/// `output` remains byte-for-byte unchanged on every failure.
Error AuditAppliedForces(
    const Input& input,
    const std::array<AppliedVector3, NODE_COUNT>& applied_forces_n,
    const std::array<AppliedVector3, NODE_COUNT>& accumulator_before_n,
    const std::array<AppliedVector3, NODE_COUNT>& accumulator_after_n,
    Telemetry& output);

/// Clears one fixed-step accumulator without releasing reusable storage.
void ResetStepAccumulator(StepAccumulator& accumulator);

/// Transactionally appends the four exact node impulses from one accepted
/// product contact. The keys must correspond to hit, surface A, surface B,
/// and surface C in that order. `accumulator` is unchanged on every failure.
Error AccumulateStepContact(
    const std::array<NodeKey, NODE_COUNT>& node_keys,
    const Input& input,
    const Telemetry& telemetry,
    StepAccumulator& accumulator);

/// Canonically groups all contributions by node identity and evaluates the
/// complete contact-only energy attribution for this fixed step. Sorting
/// mutates scratch order, but `output` remains unchanged on every failure.
Error FinalizeStep(
    StepAccumulator& accumulator,
    StepTelemetry& output);

/// Maps product actor/node state to the mobility semantics used by energy
/// attribution. Network-replicated actors and fixed nodes are not integrated.
bool IsSolverMovable(bool node_immovable, bool actor_networked);

/// Transactionally appends one accepted telemetry record. `aggregate` remains
/// byte-for-byte unchanged on invalid telemetry, overflow, or quota failure.
Error Accumulate(const Telemetry& telemetry, Aggregate& aggregate);

/// Transactionally appends one finalized fixed-step result to the scenario
/// aggregate and binds its contact count to the per-contact aggregate.
Error AccumulateStep(
    const StepTelemetry& telemetry,
    Aggregate& aggregate);

} // namespace ContactConservation
} // namespace RoR
