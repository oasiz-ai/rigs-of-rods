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

/// @file
/// @brief Frame-grouping-independent cadence for lower-rate simulation work.

#pragma once

#include <cstdint>
#include <limits>

namespace RoR {
namespace DeterministicFixedStepCadence {

/// Sleeping engines retain a roughly 62.5 Hz update rate while their update
/// boundaries are derived only from completed 2 kHz physics steps.
static const std::uint32_t SLEEPING_ENGINE_PERIOD_STEPS = 32U;

struct State
{
    /// Number of completed fixed steps since the last cadence tick.
    std::uint32_t phase;

    State() : phase(0U) {}
};

enum class AdvanceResult
{
    WAIT,
    TICK,
    INVALID
};

inline bool IsValid(const State& state, std::uint32_t period_steps)
{
    return period_steps != 0U && state.phase < period_steps;
}

/// Advances exactly one fixed physics step. Invalid state fails closed without
/// mutation. A tick resets phase to zero.
inline AdvanceResult AdvanceOne(
    std::uint32_t period_steps,
    State& state)
{
    if (!IsValid(state, period_steps))
        return AdvanceResult::INVALID;

    ++state.phase;
    if (state.phase == period_steps)
    {
        state.phase = 0U;
        return AdvanceResult::TICK;
    }
    return AdvanceResult::WAIT;
}

/// Savegame input is untrusted. An out-of-range phase restarts the cadence
/// rather than allowing an overflow or an update burst.
inline State Restore(
    std::uint32_t serialized_phase,
    std::uint32_t period_steps)
{
    State restored;
    if (period_steps != 0U && serialized_phase < period_steps)
        restored.phase = serialized_phase;
    return restored;
}

/// Advances a full-width fixed-step counter and reports the step key to pass
/// into a counter-based effect. Overflow fails closed without mutating either
/// output. The cadence phase is encoded by `next_step % period_steps`, so the
/// next update boundary survives save/load without a second state variable.
inline AdvanceResult AdvanceCounter(
    std::uint32_t period_steps,
    std::uint64_t& next_step,
    std::uint64_t& effect_step)
{
    if (period_steps == 0U ||
        next_step == std::numeric_limits<std::uint64_t>::max())
    {
        return AdvanceResult::INVALID;
    }

    State cadence = Restore(
        static_cast<std::uint32_t>(next_step % period_steps),
        period_steps);
    const AdvanceResult result = AdvanceOne(period_steps, cadence);
    if (result == AdvanceResult::INVALID)
        return result;

    effect_step = next_step;
    ++next_step;
    return result;
}

} // namespace DeterministicFixedStepCadence
} // namespace RoR
