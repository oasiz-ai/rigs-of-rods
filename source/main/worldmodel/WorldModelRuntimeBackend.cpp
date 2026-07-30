/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelRuntimeBackend.h"

namespace RoR {
namespace WorldModel {

RuntimeCaptureBackend::RuntimeCaptureBackend(
    FixedStepRuntime& runtime,
    RuntimeCaptureProvider& provider):
    m_runtime(runtime),
    m_provider(provider),
    m_active_transition(),
    m_advance_result(
        FixedStepCaptureBridge::BatchResult::INVALID_STEP_COUNT),
    m_runtime_owned(false),
    m_transition_active(false),
    m_advance_attempted(false)
{
}

RuntimeCaptureBackend::~RuntimeCaptureBackend()
{
    this->ReleaseRuntimeOwnership();
}

bool RuntimeCaptureBackend::AcquireRuntimeOwnership()
{
    if (m_runtime_owned ||
        m_transition_active ||
        !m_runtime.AcquireCaptureOwnership())
    {
        return false;
    }
    m_runtime_owned = true;
    return true;
}

void RuntimeCaptureBackend::ReleaseRuntimeOwnership() noexcept
{
    if (!m_runtime_owned)
        return;

    m_runtime.ReleaseCaptureOwnership();
    m_runtime_owned = false;
    m_transition_active = false;
    m_advance_attempted = false;
    m_advance_result =
        FixedStepCaptureBridge::BatchResult::INVALID_STEP_COUNT;
}

std::uint64_t
RuntimeCaptureBackend::GetCompletedPhysicsSteps() const
{
    return m_runtime.GetCompletedPhysicsSteps();
}

bool RuntimeCaptureBackend::CaptureResetBaseline(
    const ObservationId& expected_id,
    ObservationSample& observation)
{
    return m_runtime_owned &&
        !m_transition_active &&
        m_runtime.JoinPhysics() &&
        m_runtime.GetCompletedPhysicsSteps() ==
            expected_id.completed_physics_steps &&
        m_provider.CaptureResetBaseline(
            expected_id,
            observation) &&
        m_runtime.GetCompletedPhysicsSteps() ==
            expected_id.completed_physics_steps;
}

bool RuntimeCaptureBackend::BeginTransition(
    const TransitionId& transition)
{
    if (!m_runtime_owned ||
        m_transition_active ||
        m_runtime.GetCompletedPhysicsSteps() !=
            transition.source.completed_physics_steps ||
        !m_provider.BeginTransition(transition))
    {
        return false;
    }
    m_active_transition = transition;
    m_advance_result =
        FixedStepCaptureBridge::BatchResult::INVALID_STEP_COUNT;
    m_transition_active = true;
    m_advance_attempted = false;
    return true;
}

bool RuntimeCaptureBackend::AdvanceFixedSteps(
    std::uint32_t step_count)
{
    if (!m_runtime_owned ||
        !m_transition_active || m_advance_attempted ||
        m_runtime.GetCompletedPhysicsSteps() !=
            m_active_transition.source.completed_physics_steps)
    {
        return false;
    }
    const std::uint64_t expected_count =
        m_active_transition.target.completed_physics_steps -
        m_active_transition.source.completed_physics_steps;
    if (expected_count != static_cast<std::uint64_t>(step_count))
        return false;

    m_advance_attempted = true;
    m_advance_result = m_runtime.AdvanceFixedSteps(
        step_count,
        m_provider);
    return m_advance_result ==
        FixedStepCaptureBridge::BatchResult::COMPLETED;
}

bool RuntimeCaptureBackend::JoinPhysics()
{
    return m_runtime.JoinPhysics();
}

bool RuntimeCaptureBackend::CaptureCompletedTransition(
    const TransitionId& expected_transition,
    TransitionSample& transition,
    ObservationSample& observation)
{
    if (!m_runtime_owned ||
        !m_transition_active ||
        !m_advance_attempted ||
        m_advance_result !=
            FixedStepCaptureBridge::BatchResult::COMPLETED ||
        expected_transition != m_active_transition ||
        m_runtime.GetCompletedPhysicsSteps() !=
            expected_transition.target.completed_physics_steps ||
        !m_provider.CaptureCompletedTransition(
            expected_transition,
            transition,
            observation) ||
        m_runtime.GetCompletedPhysicsSteps() !=
            expected_transition.target.completed_physics_steps)
    {
        return false;
    }
    m_transition_active = false;
    m_advance_attempted = false;
    return true;
}

} // namespace WorldModel
} // namespace RoR
