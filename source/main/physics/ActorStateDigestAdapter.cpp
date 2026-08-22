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

#include "ActorStateDigestAdapter.h"

#include "Actor.h"
#include "CalibratedBeamStateDigest.h"
#include "JBeamHydroStateDigest.h"

#include <cstddef>
#include <limits>

namespace RoR {
namespace DeterministicStateDigest {

class ActorSnapshotSource final : public SnapshotSource
{
public:
    ActorSnapshotSource(
        const std::vector<const Actor*>& actors,
        const std::vector<
            DeterministicContactOrder::InterActorKey>& contacts):
        m_actors(actors),
        m_contacts(contacts)
    {
    }

    std::size_t GetActorCount() const override
    {
        return m_actors.size();
    }

    bool ReadActor(
        std::size_t source_actor_index,
        SnapshotActor& snapshot) const override
    {
        if (source_actor_index >= m_actors.size())
            return false;
        const Actor* const actor = m_actors[source_actor_index];
        if (actor == nullptr ||
            actor->ar_instance_id <= ACTORINSTANCEID_INVALID ||
            actor->ar_num_nodes < 0 ||
            actor->ar_num_nodes >
                static_cast<int>(NODENUM_INVALID) ||
            actor->ar_num_beams < 0 ||
            actor->ar_num_beams >
                static_cast<int>(MAX_BEAMS) ||
            actor->ar_num_collcabs < 0 ||
            actor->ar_num_collcabs > MAX_CABS ||
            actor->ar_hydros.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            (actor->ar_num_nodes != 0 &&
                actor->ar_nodes == nullptr) ||
            (actor->ar_num_beams != 0 &&
                actor->ar_beams == nullptr))
        {
            return false;
        }

        std::uint32_t state = 0;
        switch (actor->ar_state)
        {
        case ActorState::LOCAL_SIMULATED:
            state = ACTOR_STATE_LOCAL_SIMULATED;
            break;
        case ActorState::NETWORKED_OK:
            state = ACTOR_STATE_NETWORKED_OK;
            break;
        case ActorState::NETWORKED_HIDDEN:
            state = ACTOR_STATE_NETWORKED_HIDDEN;
            break;
        case ActorState::LOCAL_REPLAY:
            state = ACTOR_STATE_LOCAL_REPLAY;
            break;
        case ActorState::LOCAL_SLEEPING:
            state = ACTOR_STATE_LOCAL_SLEEPING;
            break;
        case ActorState::DISPOSED:
            state = ACTOR_STATE_DISPOSED;
            break;
        default:
            return false;
        }

        snapshot.actor.actor_id = actor->ar_instance_id;
        snapshot.actor.state = state;
        snapshot.actor.flags = 0;
        if (actor->ar_update_physics)
            snapshot.actor.flags |= ACTOR_FLAG_UPDATE_PHYSICS;
        if (actor->ar_physics_paused)
            snapshot.actor.flags |= ACTOR_FLAG_PHYSICS_PAUSED;
        if (actor->ar_collision_relevant)
            snapshot.actor.flags |= ACTOR_FLAG_COLLISION_RELEVANT;
        if (actor->m_ongoing_reset)
            snapshot.actor.flags |= ACTOR_FLAG_ONGOING_RESET;
        snapshot.actor.deterministic_seed =
            actor->m_deterministic_seed;
        snapshot.actor.actor_physics_step =
            actor->m_physics_step;
        snapshot.actor.engine_update_step =
            actor->m_engine_update_step;
        snapshot.actor.origin = {{
            static_cast<float>(actor->ar_origin.x),
            static_cast<float>(actor->ar_origin.y),
            static_cast<float>(actor->ar_origin.z)
        }};
        snapshot.node_count =
            static_cast<std::uint32_t>(actor->ar_num_nodes);
        snapshot.beam_count =
            static_cast<std::uint32_t>(actor->ar_num_beams);
        snapshot.hydro_count = 0U;
        for (const hydrobeam_t& hydro : actor->ar_hydros)
        {
            if (hydro.hb_has_jbeam_runtime)
                ++snapshot.hydro_count;
        }
        snapshot.surface_contact_count =
            static_cast<std::uint32_t>(actor->ar_num_collcabs);
        return true;
    }

    bool ReadNode(
        std::size_t source_actor_index,
        std::uint32_t node_index,
        NodeRecord& node) const override
    {
        if (source_actor_index >= m_actors.size())
            return false;
        const Actor* const actor = m_actors[source_actor_index];
        if (actor == nullptr || actor->ar_num_nodes < 0 ||
            node_index >=
                static_cast<std::uint32_t>(actor->ar_num_nodes) ||
            actor->ar_nodes == nullptr ||
            actor->ar_nodes[node_index].pos != node_index)
        {
            return false;
        }

        const node_t& source_node = actor->ar_nodes[node_index];
        node.actor_id = actor->ar_instance_id;
        node.node_id = node_index;
        node.position = {{
            static_cast<float>(source_node.RelPosition.x),
            static_cast<float>(source_node.RelPosition.y),
            static_cast<float>(source_node.RelPosition.z)
        }};
        node.velocity = {{
            static_cast<float>(source_node.Velocity.x),
            static_cast<float>(source_node.Velocity.y),
            static_cast<float>(source_node.Velocity.z)
        }};
        return true;
    }

    bool ReadBeam(
        std::size_t source_actor_index,
        std::uint32_t beam_index,
        BeamRecord& beam) const override
    {
        if (source_actor_index >= m_actors.size())
            return false;
        const Actor* const actor = m_actors[source_actor_index];
        if (actor == nullptr || actor->ar_num_beams < 0 ||
            beam_index >=
                static_cast<std::uint32_t>(actor->ar_num_beams) ||
            actor->ar_beams == nullptr)
        {
            return false;
        }

        const beam_t& source_beam = actor->ar_beams[beam_index];
        beam.actor_id = actor->ar_instance_id;
        beam.beam_id = beam_index;
        beam.rest_length = source_beam.L;
        beam.stress = source_beam.stress;
        if (source_beam.calibrated_material.enabled)
        {
            beam.material_schema_version =
                BEAM_MATERIAL_SCHEMA_CALIBRATED_V1;
            const CalibratedBeamMaterial::State& material_state =
                source_beam.calibrated_material.state;
            beam.plastic_strain = material_state.plastic_strain;
            beam.accumulated_plastic_strain =
                material_state.accumulated_plastic_strain;
            beam.damage = material_state.damage;
            beam.damage_driver_density =
                material_state.damage_driver_density;
            beam.last_total_strain =
                material_state.last_total_strain;
        }
        else
        {
            beam.material_schema_version =
                BEAM_MATERIAL_SCHEMA_NONE;
            beam.plastic_strain = 0.0;
            beam.accumulated_plastic_strain = 0.0;
            beam.damage = 0.0;
            beam.damage_driver_density = 0.0;
            beam.last_total_strain = 0.0;
        }
        beam.state_flags = 0;
        if (source_beam.bm_disabled)
            beam.state_flags |= BEAM_STATE_DISABLED;
        if (source_beam.bm_broken)
            beam.state_flags |= BEAM_STATE_BROKEN;
        return CalibratedBeamStateDigest::Populate(
            source_beam.calibrated_material,
            source_beam.bm_disabled,
            source_beam.bm_broken,
            beam);
    }

    bool ReadHydro(
        std::size_t source_actor_index,
        std::uint32_t hydro_index,
        HydroRecord& hydro) const override
    {
        if (source_actor_index >= m_actors.size())
            return false;
        const Actor* const actor = m_actors[source_actor_index];
        if (actor == nullptr || actor->ar_num_beams < 0 ||
            actor->ar_beams == nullptr)
        {
            return false;
        }

        std::uint32_t native_index = 0U;
        for (std::size_t source_hydro_index = 0U;
             source_hydro_index < actor->ar_hydros.size();
             ++source_hydro_index)
        {
            const hydrobeam_t& source_hydro =
                actor->ar_hydros[source_hydro_index];
            if (!source_hydro.hb_has_jbeam_runtime)
                continue;
            if (native_index++ != hydro_index)
                continue;
            if (source_hydro.hb_beam_index >=
                static_cast<std::uint32_t>(actor->ar_num_beams))
            {
                return false;
            }

            HydroRecord candidate;
            candidate.actor_id = actor->ar_instance_id;
            candidate.hydro_id =
                static_cast<std::uint32_t>(source_hydro_index);
            candidate.beam_id = source_hydro.hb_beam_index;
            if (!JBeamHydroStateDigest::Populate(
                    source_hydro.hb_jbeam_config,
                    source_hydro.hb_jbeam_control_binding,
                    source_hydro.hb_jbeam_state,
                    source_hydro.hb_ref_length,
                    actor->ar_beams[source_hydro.hb_beam_index].L,
                    candidate))
            {
                return false;
            }
            hydro = candidate;
            return true;
        }
        return false;
    }

    std::size_t GetContactCount() const override
    {
        return m_contacts.size();
    }

    bool ReadContact(
        std::size_t source_contact_index,
        ContactRecord& contact) const override
    {
        if (source_contact_index >= m_contacts.size())
            return false;
        const DeterministicContactOrder::InterActorKey& source_contact =
            m_contacts[source_contact_index];
        contact.surface_actor = source_contact.surface_actor;
        contact.surface_contact = source_contact.surface_contact;
        contact.hit_actor = source_contact.hit_actor;
        contact.hit_node = source_contact.hit_node;
        return true;
    }

private:
    const std::vector<const Actor*>& m_actors;
    const std::vector<
        DeterministicContactOrder::InterActorKey>& m_contacts;
};

bool BuildActorSnapshotDigest(
    std::uint64_t physics_step,
    std::uint64_t scenario_id,
    const std::vector<const Actor*>& actors,
    const std::vector<
        DeterministicContactOrder::InterActorKey>& contacts,
    Digest& digest,
    SnapshotStatus* status)
{
    const ActorSnapshotSource source(actors, contacts);
    return BuildSnapshotDigest(
        physics_step,
        scenario_id,
        source,
        digest,
        status);
}

} // namespace DeterministicStateDigest
} // namespace RoR
