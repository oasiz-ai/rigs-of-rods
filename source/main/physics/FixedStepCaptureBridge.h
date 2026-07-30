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
/// @brief Dependency-free contract for exact fixed-step capture batches.

#pragma once

#include "DeterministicScenarioSchedule.h"

#include <cstdint>
#include <limits>

namespace RoR {
namespace FixedStepCaptureBridge {

/// Identity of the applied controls consumed by one 2 kHz solver step.
///
/// `completed_physics_steps` is the pre-step state boundary: that many fixed
/// steps have completed before the callback runs. Schema 1 applies controls
/// immediately at that same tick, so `effective_input_tick` must be identical.
/// A future delayed-input schema must introduce a new identity contract rather
/// than silently changing this equality.
struct StepStartIdentity
{
    std::uint64_t completed_physics_steps;
    std::uint64_t effective_input_tick;
    std::uint32_t batch_step_index;
    std::uint32_t batch_step_count;

    StepStartIdentity():
        completed_physics_steps(0U),
        effective_input_tick(0U),
        batch_step_index(0U),
        batch_step_count(0U)
    {
    }
};

/// Observes the already-resolved Actor/Engine controls immediately before the
/// solver mutates simulation state for this fixed step.
///
/// Returning false rejects the capture record, not the physics step. The exact
/// batch still completes and joins so a recorder failure cannot leave an
/// asynchronous or partially scheduled simulation behind. Implementations
/// should be observational and should not mutate gameplay state.
class AppliedInputObserver
{
public:
    virtual ~AppliedInputObserver() {}

    virtual bool ObserveAppliedInputAtFixedStepStart(
        const StepStartIdentity& identity) = 0;
};

enum class BatchResult : std::uint32_t
{
    COMPLETED = 0,
    INVALID_STEP_COUNT,
    PHYSICS_COUNTER_EXHAUSTED,
    CAPTURE_OWNERSHIP_REQUIRED,
    INPUT_RESOLUTION_REJECTED,
    OBSERVER_REJECTED,
    PHYSICS_STEP_MISMATCH
};

/// Capture batches use the same bounded grouping as the deterministic
/// diagnostic scheduler. This bound is not a timing source: every admitted
/// batch still advances exactly the authored count at PHYSICS_DT.
inline BatchResult ValidateBatch(
    std::uint64_t first_completed_physics_step,
    std::uint32_t fixed_step_count)
{
    if (fixed_step_count == 0U ||
        fixed_step_count >
            DeterministicScenarioSchedule::MAX_FIXED_STEPS_PER_FRAME)
    {
        return BatchResult::INVALID_STEP_COUNT;
    }

    const std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    if (first_completed_physics_step >
        maximum - static_cast<std::uint64_t>(fixed_step_count))
    {
        return BatchResult::PHYSICS_COUNTER_EXHAUSTED;
    }
    return BatchResult::COMPLETED;
}

/// Construct one identity without unsigned wraparound. Failure leaves
/// `identity` unchanged.
inline bool TryMakeStepStartIdentity(
    std::uint64_t first_completed_physics_step,
    std::uint32_t batch_step_index,
    std::uint32_t batch_step_count,
    StepStartIdentity& identity)
{
    if (ValidateBatch(
            first_completed_physics_step,
            batch_step_count) != BatchResult::COMPLETED ||
        batch_step_index >= batch_step_count)
    {
        return false;
    }

    StepStartIdentity candidate;
    candidate.completed_physics_steps =
        first_completed_physics_step +
        static_cast<std::uint64_t>(batch_step_index);
    candidate.effective_input_tick =
        candidate.completed_physics_steps;
    candidate.batch_step_index = batch_step_index;
    candidate.batch_step_count = batch_step_count;
    identity = candidate;
    return true;
}

/// Keep exceptions inside the capture boundary. Physics must still advance the
/// complete scheduled batch even when an observational recorder fails.
inline bool NotifyAppliedInputObserver(
    AppliedInputObserver& observer,
    const StepStartIdentity& identity) noexcept
{
    try
    {
        return observer.ObserveAppliedInputAtFixedStepStart(identity);
    }
    catch (...)
    {
        return false;
    }
}

/// Owns the first-failure latch for one exact physics batch. ActorManager runs
/// this object only on its physics worker and reads it only after joining.
/// Rejection never suppresses later callbacks: the recorder sees every fixed-
/// step boundary even if one record already made the episode unusable.
class ObservationBatch
{
public:
    ObservationBatch(
        std::uint64_t first_completed_physics_step,
        std::uint32_t fixed_step_count,
        AppliedInputObserver& observer):
        m_first_completed_physics_step(
            first_completed_physics_step),
        m_fixed_step_count(fixed_step_count),
        m_observer(observer),
        m_succeeded(
            ValidateBatch(
                first_completed_physics_step,
                fixed_step_count) == BatchResult::COMPLETED)
    {
    }

    bool ObserveFixedStepStart(
        std::uint64_t actual_completed_physics_step,
        std::uint32_t batch_step_index) noexcept
    {
        StepStartIdentity identity;
        const bool accepted =
            TryMakeStepStartIdentity(
                m_first_completed_physics_step,
                batch_step_index,
                m_fixed_step_count,
                identity) &&
            actual_completed_physics_step ==
                identity.completed_physics_steps &&
            NotifyAppliedInputObserver(m_observer, identity);
        m_succeeded = m_succeeded && accepted;
        return accepted;
    }

    bool Succeeded() const
    {
        return m_succeeded;
    }

private:
    std::uint64_t m_first_completed_physics_step;
    std::uint32_t m_fixed_step_count;
    AppliedInputObserver& m_observer;
    bool m_succeeded;
};

inline const char* ToString(BatchResult result)
{
    switch (result)
    {
    case BatchResult::COMPLETED:
        return "completed";
    case BatchResult::INVALID_STEP_COUNT:
        return "invalid step count";
    case BatchResult::PHYSICS_COUNTER_EXHAUSTED:
        return "physics counter exhausted";
    case BatchResult::CAPTURE_OWNERSHIP_REQUIRED:
        return "capture runtime ownership required";
    case BatchResult::INPUT_RESOLUTION_REJECTED:
        return "capture input resolution rejected";
    case BatchResult::OBSERVER_REJECTED:
        return "applied-input observer rejected";
    case BatchResult::PHYSICS_STEP_MISMATCH:
        return "physics step mismatch";
    }
    return "unknown";
}

} // namespace FixedStepCaptureBridge
} // namespace RoR
