/*
   This source file is part of Rigs of Rods
   Copyright 2005-2012 Pierre-Michel Ricordel
   Copyright 2007-2012 Thomas Fischer
   Copyright 2016 Fabian Killus

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

#pragma once

#include "DeterministicContactOrder.h"
#include "ForwardDeclarations.h"
#include "SimData.h"

#include <bitset>

namespace RoR {

/// @addtogroup Physics
/// @{

/// @addtogroup Collisions
/// @{

struct InterActorCollisionContact
{
    DeterministicContactOrder::InterActorKey key;
    NodeNum_t surface_node_o = NODENUM_INVALID;
    NodeNum_t surface_node_a = NODENUM_INVALID;
    NodeNum_t surface_node_b = NODENUM_INVALID;
    float penetration_depth = 0.f;
    float alpha = 0.f;
    float beta = 0.f;
    float gamma = 0.f;
    Ogre::Vector3 normal = Ogre::Vector3::ZERO;
    ground_model_t* ground_model = nullptr;
};

struct InterActorCollisionSchedule
{
    std::bitset<MAX_CABS> active_surface_contacts;
};

void PrepareInterActorCollisionSchedule(
        const int free_collcab,
        collcab_rate_t inter_collcabrate[],
        InterActorCollisionSchedule& schedule);

void CollectInterActorCollisionContacts(
        const ActorInstanceID_t surface_actor_id,
        PointColDetector &interPointCD,
        const InterActorCollisionSchedule& schedule,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t inter_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model,
        DeterministicContactOrder::BoundedTaskBuffer<
            InterActorCollisionContact>& out_contacts);

void ApplyInterActorCollisionContacts(
        const float dt,
        const std::vector<InterActorCollisionContact>& contacts);

/// Resolves every discovered contact in canonical order. When
/// `out_contact_keys` is non-null, it also captures the exact resolved key
/// stream. A false return only reports that optional trace capture ran out of
/// storage; collision forces are still applied in full.
bool ResolveInterActorCollisionContactsSerial(
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
                out_contact_keys = nullptr);

void ResolveIntraActorCollisions(const float dt, PointColDetector &intraPointCD,
        const int free_collcab, int collcabs[], int cabs[],
        collcab_rate_t intra_collcabrate[], node_t nodes[],
        const float collrange,
        ground_model_t &submesh_ground_model);

/// @} // addtogroup Collisions
/// @} // addtogroup Physics

} // namespace RoRs
