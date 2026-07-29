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

#include "ObservationScheduler.h"

#include <limits>

namespace RoR {
namespace WorldModel {

ObservationBoundary::ObservationBoundary():
    observation_index(0U),
    completed_physics_steps(0U)
{
}

bool operator==(
    const ObservationBoundary& first,
    const ObservationBoundary& second)
{
    return first.observation_index == second.observation_index &&
        first.completed_physics_steps == second.completed_physics_steps;
}

bool operator!=(
    const ObservationBoundary& first,
    const ObservationBoundary& second)
{
    return !(first == second);
}

bool TryObservationStepOffset(
    std::uint64_t observation_index,
    std::uint64_t& step_offset)
{
    // floor(n*125/3) = floor(n/3)*125 + floor((n%3)*125/3).
    const std::uint64_t quotient =
        observation_index / CADENCE_DENOMINATOR;
    const std::uint64_t remainder =
        observation_index % CADENCE_DENOMINATOR;
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    if (quotient > maximum / CADENCE_NUMERATOR)
        return false;

    const std::uint64_t whole = quotient * CADENCE_NUMERATOR;
    const std::uint64_t fractional =
        (remainder * CADENCE_NUMERATOR) / CADENCE_DENOMINATOR;
    if (whole > maximum - fractional)
        return false;

    step_offset = whole + fractional;
    return true;
}

bool TryObservationBoundary(
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t observation_index,
    ObservationBoundary& boundary)
{
    std::uint64_t offset = 0U;
    if (!TryObservationStepOffset(observation_index, offset))
        return false;
    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    if (origin_completed_physics_steps > maximum - offset)
        return false;

    ObservationBoundary candidate;
    candidate.observation_index = observation_index;
    candidate.completed_physics_steps =
        origin_completed_physics_steps + offset;
    boundary = candidate;
    return true;
}

std::uint32_t TransitionPhysicsStepCount(
    std::uint64_t transition_index)
{
    return transition_index % CADENCE_DENOMINATOR == 0U ? 41U : 42U;
}

ObservationScheduler::ObservationScheduler(
    std::uint64_t origin_completed_physics_steps):
    m_origin_completed_physics_steps(origin_completed_physics_steps),
    m_next_boundary(),
    m_has_next_boundary(true)
{
    m_next_boundary.observation_index = 0U;
    m_next_boundary.completed_physics_steps =
        origin_completed_physics_steps;
}

bool ObservationScheduler::Reset(
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t next_observation_index)
{
    ObservationBoundary candidate;
    if (!TryObservationBoundary(
            origin_completed_physics_steps,
            next_observation_index,
            candidate))
    {
        return false;
    }
    m_origin_completed_physics_steps =
        origin_completed_physics_steps;
    m_next_boundary = candidate;
    m_has_next_boundary = true;
    return true;
}

ObservationPollResult ObservationScheduler::Poll(
    std::uint64_t completed_physics_steps,
    ObservationBoundary& boundary)
{
    if (!m_has_next_boundary)
        return ObservationPollResult::EXHAUSTED;
    if (completed_physics_steps <
        m_next_boundary.completed_physics_steps)
    {
        return ObservationPollResult::WAIT;
    }
    if (completed_physics_steps >
        m_next_boundary.completed_physics_steps)
    {
        return ObservationPollResult::MISSED_BOUNDARY;
    }

    boundary = m_next_boundary;
    if (m_next_boundary.observation_index ==
        std::numeric_limits<std::uint64_t>::max())
    {
        m_has_next_boundary = false;
        return ObservationPollResult::EMIT;
    }

    ObservationBoundary next;
    if (!TryObservationBoundary(
            m_origin_completed_physics_steps,
            m_next_boundary.observation_index + 1U,
            next))
    {
        m_has_next_boundary = false;
    }
    else
    {
        m_next_boundary = next;
    }
    return ObservationPollResult::EMIT;
}

std::uint64_t
ObservationScheduler::GetOriginCompletedPhysicsSteps() const
{
    return m_origin_completed_physics_steps;
}

bool ObservationScheduler::HasNextBoundary() const
{
    return m_has_next_boundary;
}

const ObservationBoundary&
ObservationScheduler::GetNextBoundary() const
{
    return m_next_boundary;
}

} // namespace WorldModel
} // namespace RoR
