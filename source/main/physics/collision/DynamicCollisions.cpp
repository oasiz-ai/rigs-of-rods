/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2016      Fabian Killus

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

#include "DynamicCollisions.h"

#include "Application.h"
#include "Actor.h"
#include "ContactConservation.h"
#include "SimData.h"
#include "CartesianToTriangleTransform.h"
#include "Collisions.h"
#include "GameContext.h"
#include "PointColDetector.h"
#include "Triangle.h"

using namespace Ogre;
using namespace RoR;

/// Determine on which side of a triangle an occuring collision takes place.
/**
 * The frontface of the triangle is the side towards which the surface normal is pointing.
 * The backface is the opposite side.
 *
 * \note This implementation is a heuristic, not an accurate calculation.
 *
 * @param distance      Signed shortest distance from collision point to triangle plane.
 * @param normal        Surface normal of the triangle.
 * @param surface_point Arbitrary point within the triangle plane.
 * @param neighbour_node_ids Indices of neighbouring nodes connected to the colliding node.
 * @param nodes         
 */
static bool BackfaceCollisionTest(const float distance,
        const Vector3 &normal,
        const node_t &surface_point,
        const std::vector<int> &neighbour_node_ids,
        const node_t nodes[])
{
    auto sign = [](float x){ return (x >= 0) ? 1 : -1; };

    // Summarize over the collision node and its connected neighbour nodes to infer on which side
    // of the collision plane most of these nodes are located.
    // Nodes in front contribute positively, nodes on the backface contribute negatively.

    // the contribution of the collision node itself has triple weight in this heuristic
    const int weight = 3;
    int face_indicator = weight * sign(distance);

    // calculate the contribution of neighbouring nodes (if it can still change the final outcome)
    if (neighbour_node_ids.size() > weight) {
        for (auto id : neighbour_node_ids) {
            const auto neighbour_distance = normal.dotProduct(nodes[id].AbsPosition - surface_point.AbsPosition);
            face_indicator += sign(neighbour_distance);
        }
    }

    // a negative sum indicates that the collision is occuring on the backface
    return (face_indicator < 0);
}


/// Test if a point given in triangle local coordinates lies within the triangle itself.
/**
 * A point (within in the triangle plane) is located inside the triangle if its barycentric coordinates
 * \f$\alpha, \beta, \gamma\f$ are all positive. The margin additionally defines the maximum distance
 * from the triangle plane within which a three-dimensional point is still considered to be close
 * enough to be inside the triangle.
 *
 * @param local Point in triangle local coordinates.
 * @param margin Range within which a point is considered to be close enough to the triangle plane.
 */
static bool InsideTriangleTest(const CartesianToTriangleTransform::TriangleCoord &local, const float margin)
{
    const auto coord    = local.barycentric;
    const auto distance = local.distance;
    return (coord.alpha >= 0) && (coord.beta >= 0) && (coord.gamma >= 0) && (std::abs(distance) <= margin);
}


namespace {

struct CollisionForceResult
{
    ContactConservation::Error error = ContactConservation::Error::NONE;
    bool forces_applied = false;
};

ContactConservation::Vector3 ToConservationVector(
        const Vector3& value)
{
    return ContactConservation::Vector3(
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z));
}

ContactConservation::NodeState ToConservationNode(
        const node_t& node,
        const bool movable)
{
    ContactConservation::NodeState state;
    state.position_m = ToConservationVector(node.AbsPosition);
    state.velocity_mps = ToConservationVector(node.Velocity);
    state.mass_kg = static_cast<double>(node.mass);
    state.movable = movable ? 1U : 0U;
    return state;
}

Vector3 ToOgreVector(
        const ContactConservation::AppliedVector3& value)
{
    return Vector3(value.x, value.y, value.z);
}

ContactConservation::AppliedVector3 ToAppliedVector(
        const Vector3& value)
{
    return ContactConservation::AppliedVector3(
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z));
}

void ApplyLegacyCollisionForces(
        const Vector3& forcevec,
        node_t& hitnode,
        node_t& na,
        node_t& nb,
        node_t& no,
        const float alpha,
        const float beta,
        const float gamma)
{
    hitnode.Forces += forcevec;
    na.Forces -= forcevec * alpha;
    nb.Forces -= forcevec * beta;
    no.Forces -= forcevec * gamma;
}

/// Calculate collision forces and apply them to the collision node and the
/// three vertex nodes of the collision triangle. The conservation kernel
/// narrows and closes the exact binary32 forces before any node force
/// accumulator is mutated.
CollisionForceResult ResolveCollisionForces(const float penetration_depth,
        node_t &hitnode, node_t &na, node_t &nb, node_t &no,
        const float alpha, const float beta, const float gamma,
        const Vector3 &normal,
        const float dt,
        const bool hit_actor_networked,
        const bool surface_actor_networked,
        ground_model_t &submesh_ground_model,
        ContactConservation::Aggregate* out_conservation)
{
    const auto velocity = hitnode.Velocity - (na.Velocity * alpha + nb.Velocity * beta + no.Velocity * gamma);
    const float tr_mass = na.mass * alpha + nb.mass * beta + no.mass * gamma;
    const float mass = hit_actor_networked
        ? hitnode.mass
        : (hitnode.mass * tr_mass) / (hitnode.mass + tr_mass);

    const Vector3 forcevec = primitiveCollision(
        &hitnode,
        velocity,
        mass,
        normal,
        dt,
        &submesh_ground_model,
        penetration_depth);

    ContactConservation::Input input;
    input.hit_node = ToConservationNode(
        hitnode,
        ContactConservation::IsSolverMovable(
            hitnode.nd_immovable,
            hit_actor_networked));
    input.surface_nodes[0] = ToConservationNode(
        na,
        ContactConservation::IsSolverMovable(
            na.nd_immovable,
            surface_actor_networked));
    input.surface_nodes[1] = ToConservationNode(
        nb,
        ContactConservation::IsSolverMovable(
            nb.nd_immovable,
            surface_actor_networked));
    input.surface_nodes[2] = ToConservationNode(
        no,
        ContactConservation::IsSolverMovable(
            no.nd_immovable,
            surface_actor_networked));
    input.barycentric[0] = static_cast<double>(alpha);
    input.barycentric[1] = static_cast<double>(beta);
    input.barycentric[2] = static_cast<double>(gamma);
    input.force_on_hit_n = ToConservationVector(forcevec);
    input.time_step_s = static_cast<double>(dt);

    ContactConservation::Telemetry telemetry;
    CollisionForceResult result;
    result.error = ContactConservation::Evaluate(input, telemetry);
    if (result.error != ContactConservation::Error::NONE)
    {
        ApplyLegacyCollisionForces(
            forcevec,
            hitnode,
            na,
            nb,
            no,
            alpha,
            beta,
            gamma);
        result.forces_applied = true;
        return result;
    }

    const Vector3 forces_before[ContactConservation::NODE_COUNT] = {
        hitnode.Forces,
        na.Forces,
        nb.Forces,
        no.Forces
    };

    hitnode.Forces += ToOgreVector(
        telemetry.applied_forces_n[
            ContactConservation::HIT_NODE_INDEX]);
    na.Forces += ToOgreVector(
        telemetry.applied_forces_n[
            ContactConservation::SURFACE_NODE_A_INDEX]);
    nb.Forces += ToOgreVector(
        telemetry.applied_forces_n[
            ContactConservation::SURFACE_NODE_B_INDEX]);
    no.Forces += ToOgreVector(
        telemetry.applied_forces_n[
            ContactConservation::SURFACE_NODE_C_INDEX]);
    result.forces_applied = true;

    const std::array<
        ContactConservation::AppliedVector3,
        ContactConservation::NODE_COUNT> accumulator_before = {{
            ToAppliedVector(forces_before[0]),
            ToAppliedVector(forces_before[1]),
            ToAppliedVector(forces_before[2]),
            ToAppliedVector(forces_before[3])
        }};
    const std::array<
        ContactConservation::AppliedVector3,
        ContactConservation::NODE_COUNT> accumulator_after = {{
            ToAppliedVector(hitnode.Forces),
            ToAppliedVector(na.Forces),
            ToAppliedVector(nb.Forces),
            ToAppliedVector(no.Forces)
        }};
    ContactConservation::Telemetry applied_telemetry;
    result.error = ContactConservation::AuditAppliedForces(
        input,
        telemetry.applied_forces_n,
        accumulator_before,
        accumulator_after,
        applied_telemetry);
    if (result.error == ContactConservation::Error::NONE &&
            applied_telemetry.normalized_linear_impulse_residual > 1.0e-6)
    {
        result.error = ContactConservation::Error::
            LINEAR_IMPULSE_RESIDUAL_EXCEEDED;
    }
    if (out_conservation != nullptr)
    {
        if (result.error == ContactConservation::Error::NONE)
        {
            result.error = ContactConservation::Accumulate(
                applied_telemetry,
                *out_conservation);
        }
    }
    if (result.error != ContactConservation::Error::NONE)
    {
        // The audit is diagnostic and must never become a new physics law.
        // Restore the exact pre-contact accumulators, then preserve the legacy
        // force response while reporting the independent trace failure.
        hitnode.Forces = forces_before[0];
        na.Forces = forces_before[1];
        nb.Forces = forces_before[2];
        no.Forces = forces_before[3];
        ApplyLegacyCollisionForces(
            forcevec,
            hitnode,
            na,
            nb,
            no,
            alpha,
            beta,
            gamma);
    }
    return result;
}

} // namespace


void RoR::PrepareInterActorCollisionSchedule(
        const int free_collcab,
        collcab_rate_t inter_collcabrate[],
        InterActorCollisionSchedule& schedule)
{
    schedule.active_surface_contacts.reset();
    for (int i=0; i<free_collcab; i++)
    {
        if (inter_collcabrate[i].rate > 0)
        {
            inter_collcabrate[i].distance++;
            inter_collcabrate[i].rate--;
            continue;
        }
        inter_collcabrate[i].rate = std::min(inter_collcabrate[i].distance, 12);
        inter_collcabrate[i].distance = 0;
        schedule.active_surface_contacts.set(static_cast<std::size_t>(i));
    }
}

namespace {

class InterActorCollisionResolver
{
public:
    explicit InterActorCollisionResolver(
            float dt,
            ContactConservation::Aggregate* out_conservation):
        m_dt(dt),
        m_conservation(out_conservation)
    {
    }

    ContactConservation::Error Apply(
            const InterActorCollisionContact& contact)
    {
        if (contact.key.surface_actor != m_cached_surface_actor_id)
        {
            m_cached_surface_actor_id = contact.key.surface_actor;
            m_surface_actor = App::GetGameContext()
                ->GetActorManager()->GetActorById(
                    m_cached_surface_actor_id).GetRef();
        }
        if (contact.key.hit_actor != m_cached_hit_actor_id)
        {
            m_cached_hit_actor_id = contact.key.hit_actor;
            m_hit_actor = App::GetGameContext()
                ->GetActorManager()->GetActorById(
                    m_cached_hit_actor_id).GetRef();
        }
        if (m_surface_actor == nullptr ||
                m_hit_actor == nullptr ||
                contact.ground_model == nullptr)
        {
            return ContactConservation::Error::NONE;
        }

        node_t& hitnode = m_hit_actor->ar_nodes[contact.key.hit_node];
        node_t& no = m_surface_actor->ar_nodes[contact.surface_node_o];
        node_t& na = m_surface_actor->ar_nodes[contact.surface_node_a];
        node_t& nb = m_surface_actor->ar_nodes[contact.surface_node_b];
        const bool hit_actor_networked =
            (m_hit_actor->ar_state == ActorState::NETWORKED_OK);
        const bool surface_actor_networked =
            (m_surface_actor->ar_state == ActorState::NETWORKED_OK);

        CollisionForceResult force_result = ResolveCollisionForces(
            contact.penetration_depth,
            hitnode,
            na,
            nb,
            no,
            contact.alpha,
            contact.beta,
            contact.gamma,
            contact.normal,
            m_dt,
            hit_actor_networked,
            surface_actor_networked,
            *contact.ground_model,
            m_conservation);

        if (!force_result.forces_applied)
            return force_result.error;

        hitnode.nd_last_collision_gm = contact.ground_model;
        hitnode.nd_has_mesh_contact = true;
        na.nd_has_mesh_contact = true;
        nb.nd_has_mesh_contact = true;
        no.nd_has_mesh_contact = true;
        return force_result.error;
    }

private:
    float m_dt = 0.f;
    ActorInstanceID_t m_cached_surface_actor_id = ACTORINSTANCEID_INVALID;
    ActorInstanceID_t m_cached_hit_actor_id = ACTORINSTANCEID_INVALID;
    Actor* m_surface_actor = nullptr;
    Actor* m_hit_actor = nullptr;
    ContactConservation::Aggregate* m_conservation = nullptr;
};

template <typename ContactSink>
void DiscoverInterActorCollisionContacts(
        const ActorInstanceID_t surface_actor_id,
        PointColDetector &interPointCD,
        const InterActorCollisionSchedule& schedule,
        const bool update_rate_state,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t inter_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model,
        const ContactSink& contact_sink)
{
    DeterministicContactOrder::CanonicalOrderValidator<
        DeterministicContactOrder::InterActorKey> emitted_order;
    for (int i=0; i<free_collcab; i++)
    {
        if (!schedule.active_surface_contacts.test(
                static_cast<std::size_t>(i)))
        {
            continue;
        }

        int tmpv = collcabs[i]*3;
        const auto no = &nodes[cabs[tmpv]];
        const auto na = &nodes[cabs[tmpv+1]];
        const auto nb = &nodes[cabs[tmpv+2]];

        interPointCD.query(no->AbsPosition
                , na->AbsPosition
                , nb->AbsPosition, collrange);

        if (!interPointCD.hit_list.empty())
        {
            // setup transformation of points to triangle local coordinates
            const Triangle triangle(na->AbsPosition, nb->AbsPosition, no->AbsPosition);
            const CartesianToTriangleTransform transform(triangle);

            ActorInstanceID_t cached_actor_id = ACTORINSTANCEID_INVALID;
            Actor* hit_actor = nullptr;
            for (PointidID_t h : interPointCD.hit_list)
            {
                const PointColDetector::pointid_t& point_id =
                    interPointCD.hit_pointid_list[h];
                if (point_id.actorid != cached_actor_id)
                {
                    cached_actor_id = point_id.actorid;
                    hit_actor = App::GetGameContext()
                        ->GetActorManager()->GetActorById(
                            cached_actor_id).GetRef();
                }
                if (hit_actor == nullptr)
                    continue;

                const NodeNum_t hitnode_num = point_id.nodenum;
                const node_t& hitnode = hit_actor->ar_nodes[hitnode_num];

                // Transform point to triangle local coordinates.
                const auto local_point = transform(hitnode.AbsPosition);

                if (!InsideTriangleTest(local_point, collrange))
                    continue;

                if (update_rate_state)
                    inter_collcabrate[i].rate = 0;

                const auto coord = local_point.barycentric;
                auto distance = local_point.distance;
                auto normal = triangle.normal();

                // Adapt in case the collision occurs on the backface.
                const auto& neighbour_node_ids =
                    hit_actor->ar_node_to_node_connections[hitnode_num];
                const bool is_backface = BackfaceCollisionTest(
                    distance,
                    normal,
                    *no,
                    neighbour_node_ids,
                    hit_actor->ar_nodes);
                if (is_backface)
                {
                    normal = -normal;
                    distance = -distance;
                }

                InterActorCollisionContact contact;
                contact.key.surface_actor = surface_actor_id;
                contact.key.surface_contact = static_cast<std::uint32_t>(i);
                contact.key.hit_actor = point_id.actorid;
                contact.key.hit_node = hitnode_num;
                contact.surface_node_o = static_cast<NodeNum_t>(cabs[tmpv]);
                contact.surface_node_a = static_cast<NodeNum_t>(cabs[tmpv+1]);
                contact.surface_node_b = static_cast<NodeNum_t>(cabs[tmpv+2]);
                contact.penetration_depth = collrange - distance;
                contact.alpha = coord.alpha;
                contact.beta = coord.beta;
                contact.gamma = coord.gamma;
                contact.normal = normal;
                contact.ground_model = &submesh_ground_model;
                ROR_ASSERT(emitted_order.Observe(contact.key));
                contact_sink(contact);
            }
        }
        else if (update_rate_state)
        {
            inter_collcabrate[i].rate++;
        }
    }
}

} // namespace

void RoR::CollectInterActorCollisionContacts(
        const ActorInstanceID_t surface_actor_id,
        PointColDetector &interPointCD,
        const InterActorCollisionSchedule& schedule,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t inter_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model,
        DeterministicContactOrder::BoundedTaskBuffer<
            InterActorCollisionContact>& out_contacts)
{
    DiscoverInterActorCollisionContacts(
        surface_actor_id,
        interPointCD,
        schedule,
        true,
        free_collcab,
        collcabs,
        cabs,
        inter_collcabrate,
        nodes,
        collrange,
        submesh_ground_model,
        [&out_contacts](const InterActorCollisionContact& contact)
        {
            out_contacts.TryPush(contact);
        });
}

ContactConservation::Error RoR::ApplyInterActorCollisionContacts(
        const float dt,
        const std::vector<InterActorCollisionContact>& contacts,
        ContactConservation::Aggregate* out_conservation)
{
    InterActorCollisionResolver resolver(dt, out_conservation);
    ContactConservation::Error first_error =
        ContactConservation::Error::NONE;
    for (const InterActorCollisionContact& contact : contacts)
    {
        const ContactConservation::Error error = resolver.Apply(contact);
        if (first_error == ContactConservation::Error::NONE &&
                error != ContactConservation::Error::NONE)
        {
            first_error = error;
        }
    }
    return first_error;
}

bool RoR::ResolveInterActorCollisionContactsSerial(
        const ActorInstanceID_t surface_actor_id,
        const float dt,
        PointColDetector &interPointCD,
        const InterActorCollisionSchedule& schedule,
        const bool update_rate_state,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t inter_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model,
        std::vector<
            DeterministicContactOrder::InterActorKey>*
                out_contact_keys,
        ContactConservation::Aggregate* out_conservation,
        ContactConservation::Error* out_conservation_error)
{
    InterActorCollisionResolver resolver(dt, out_conservation);
    bool contact_capture_succeeded = true;
    ContactConservation::Error first_conservation_error =
        ContactConservation::Error::NONE;
    DiscoverInterActorCollisionContacts(
        surface_actor_id,
        interPointCD,
        schedule,
        update_rate_state,
        free_collcab,
        collcabs,
        cabs,
        inter_collcabrate,
        nodes,
        collrange,
        submesh_ground_model,
        [&resolver,
         out_contact_keys,
         &contact_capture_succeeded,
         &first_conservation_error](
            const InterActorCollisionContact& contact)
        {
            if (out_contact_keys != nullptr &&
                    contact_capture_succeeded)
            {
                if (out_contact_keys->size() >=
                        DeterministicContactOrder::
                            INTER_ACTOR_CONTACT_BUDGET)
                {
                    contact_capture_succeeded = false;
                }
                else
                {
                    try
                    {
                        out_contact_keys->push_back(contact.key);
                    }
                    catch (...)
                    {
                        // Trace capture is diagnostic-only. Preserve the
                        // complete collision response and let the caller fail
                        // the trace.
                        contact_capture_succeeded = false;
                    }
                }
            }
            const ContactConservation::Error error = resolver.Apply(contact);
            if (first_conservation_error ==
                    ContactConservation::Error::NONE &&
                    error != ContactConservation::Error::NONE)
            {
                first_conservation_error = error;
            }
        });
    if (out_conservation_error != nullptr)
        *out_conservation_error = first_conservation_error;
    return contact_capture_succeeded;
}


void RoR::ResolveIntraActorCollisions(const float dt, PointColDetector &intraPointCD,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t intra_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model)
{
    for (int i=0; i<free_collcab; i++)
    {
        if (intra_collcabrate[i].rate > 0)
        {
            intra_collcabrate[i].distance++;
            intra_collcabrate[i].rate--;
            continue;
        }
        if (intra_collcabrate[i].distance > 0)
        {
            intra_collcabrate[i].rate = std::min(intra_collcabrate[i].distance, 12);
            intra_collcabrate[i].distance = 0;
        }

        int tmpv = collcabs[i]*3;
        const auto no = &nodes[cabs[tmpv]];
        const auto na = &nodes[cabs[tmpv+1]];
        const auto nb = &nodes[cabs[tmpv+2]];

        intraPointCD.query(no->AbsPosition
                , na->AbsPosition
                , nb->AbsPosition, collrange);

        bool collision = false;

        if (!intraPointCD.hit_list.empty())
        {
            // setup transformation of points to triangle local coordinates
            const Triangle triangle(na->AbsPosition, nb->AbsPosition, no->AbsPosition);
            const CartesianToTriangleTransform transform(triangle);

            for (PointidID_t h : intraPointCD.hit_list)
            {
                NodeNum_t hitnode_num = intraPointCD.hit_pointid_list[h].nodenum;
                node_t& hitnode = nodes[hitnode_num];

                //ignore wheel/chassis self contact
                if (hitnode.nd_tyre_node) continue;
                if (no == &hitnode || na == &hitnode || nb == &hitnode) continue;

                // transform point to triangle local coordinates
                const auto local_point = transform(hitnode.AbsPosition);

                // collision test
                const bool is_colliding = InsideTriangleTest(local_point, collrange);
                if (is_colliding)
                {
                    const auto coord = local_point.barycentric;
                    auto distance = local_point.distance;
                    auto normal   = triangle.normal();

                    // adapt in case the collision is occuring on the backface of the triangle
                    if (distance < 0) 
                    {
                        // flip surface normal and distance to triangle plane
                        normal   = -normal;
                        distance = -distance;
                    }

                    const auto penetration_depth = collrange - distance;

                    const CollisionForceResult force_result =
                        ResolveCollisionForces(penetration_depth, hitnode, *na, *nb, *no, coord.alpha,
                            coord.beta, coord.gamma, normal, dt, false, false,
                            submesh_ground_model, nullptr);
                    collision = collision || force_result.forces_applied;
                }
            }
        }

        if (collision)
        {
            intra_collcabrate[i].rate = -20000;
        }
        else
        {
            intra_collcabrate[i].rate++;
        }
    }
}
