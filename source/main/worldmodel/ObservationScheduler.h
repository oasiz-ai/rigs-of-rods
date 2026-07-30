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
/// @brief Exact 48 Hz observation cadence over the 2 kHz physics clock.

#pragma once

#include <cstdint>

namespace RoR {
namespace WorldModel {

static const std::uint32_t PHYSICS_STEPS_PER_SECOND = 2000U;
static const std::uint32_t OBSERVATIONS_PER_SECOND = 48U;
static const std::uint32_t CADENCE_NUMERATOR = 125U;
static const std::uint32_t CADENCE_DENOMINATOR = 3U;

/// A boundary counts completed fixed steps. Observation n is captured at
/// `origin + floor(n * 125 / 3)`, yielding 41/42/42-step transitions.
struct ObservationBoundary
{
    std::uint64_t observation_index;
    std::uint64_t completed_physics_steps;
    ObservationBoundary();
};

bool operator==(
    const ObservationBoundary& first,
    const ObservationBoundary& second);
bool operator!=(
    const ObservationBoundary& first,
    const ObservationBoundary& second);

/// False means the exact result cannot fit in uint64_t. Output is unchanged.
bool TryObservationStepOffset(
    std::uint64_t observation_index,
    std::uint64_t& step_offset);

/// Adds the cadence offset to the episode origin without overflowing.
/// Arithmetic failure leaves `boundary` unchanged.
bool TryObservationBoundary(
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t observation_index,
    ObservationBoundary& boundary);

std::uint32_t TransitionPhysicsStepCount(
    std::uint64_t transition_index);

enum class ObservationPollResult
{
    WAIT,
    EMIT,
    MISSED_BOUNDARY,
    EXHAUSTED
};

/// Called at each completed fixed-step boundary. It reports a skipped boundary
/// instead of silently duplicating or dropping training observations.
class ObservationScheduler
{
public:
    explicit ObservationScheduler(
        std::uint64_t origin_completed_physics_steps = 0U);
    bool Reset(
        std::uint64_t origin_completed_physics_steps,
        std::uint64_t next_observation_index = 0U);
    ObservationPollResult Poll(
        std::uint64_t completed_physics_steps,
        ObservationBoundary& boundary);

    std::uint64_t GetOriginCompletedPhysicsSteps() const;
    bool HasNextBoundary() const;
    const ObservationBoundary& GetNextBoundary() const;

private:
    std::uint64_t m_origin_completed_physics_steps;
    ObservationBoundary m_next_boundary;
    bool m_has_next_boundary;
};

} // namespace WorldModel
} // namespace RoR
