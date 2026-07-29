/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "RoRWorldModelRuntimeAdapter.h"

#include "Actor.h"

namespace RoR {
namespace WorldModel {

ActorManagerFixedStepRuntime::ActorManagerFixedStepRuntime(
    ActorManager& actor_manager,
    ActorPtr player_actor):
    m_actor_manager(actor_manager),
    m_player_actor(player_actor)
{
}

ActorManagerFixedStepRuntime::~ActorManagerFixedStepRuntime() = default;

void ActorManagerFixedStepRuntime::SetPlayerActor(
    ActorPtr player_actor)
{
    m_player_actor = player_actor;
}

bool ActorManagerFixedStepRuntime::AcquireCaptureOwnership()
{
    return m_actor_manager.AcquireFixedStepCaptureOwnership(
        this,
        m_player_actor);
}

void ActorManagerFixedStepRuntime::ReleaseCaptureOwnership() noexcept
{
    m_actor_manager.ReleaseFixedStepCaptureOwnership(this);
}

std::uint64_t
ActorManagerFixedStepRuntime::GetCompletedPhysicsSteps() const
{
    return m_actor_manager.GetCompletedPhysicsSteps();
}

FixedStepCaptureBridge::BatchResult
ActorManagerFixedStepRuntime::AdvanceFixedSteps(
    std::uint32_t fixed_step_count,
    FixedStepCaptureBridge::AppliedInputObserver& observer)
{
    return m_actor_manager.AdvanceFixedStepsForCapture(
        this,
        m_player_actor,
        fixed_step_count,
        observer);
}

bool ActorManagerFixedStepRuntime::JoinPhysics()
{
    m_actor_manager.SyncWithSimThread();
    return true;
}

} // namespace WorldModel
} // namespace RoR
