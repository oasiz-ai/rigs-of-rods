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
/// @brief Read-only production Actor bridge for deterministic state digests.

#pragma once

#include "DeterministicContactOrder.h"
#include "DeterministicStateDigest.h"

#include <cstdint>
#include <vector>

namespace RoR {

class Actor;

namespace DeterministicStateDigest {

/// Extracts a complete canonical snapshot from live Actors and the transient
/// inter-actor contact keys retained by the caller for the current step.
/// Actors and contacts may arrive in any order. This function never mutates
/// simulation state and leaves `digest` unchanged on failure.
///
/// Caller synchronization is mandatory: all physics/contact worker tasks must
/// be joined, and no Actor array, state, seed, or counter may change until this
/// function returns. The adapter deliberately performs no locking.
bool BuildActorSnapshotDigest(
    std::uint64_t physics_step,
    std::uint64_t scenario_id,
    const std::vector<const Actor*>& actors,
    const std::vector<
        DeterministicContactOrder::InterActorKey>& contacts,
    Digest& digest,
    SnapshotStatus* status = nullptr);

} // namespace DeterministicStateDigest
} // namespace RoR
