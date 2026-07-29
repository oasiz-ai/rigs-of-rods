/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief ActorManager implementation of the recorder's fixed-step runtime.

#pragma once

#include "ActorManager.h"
#include "WorldModelRuntimeBackend.h"

namespace RoR {
namespace WorldModel {

class ActorManagerFixedStepRuntime final : public FixedStepRuntime
{
public:
    ActorManagerFixedStepRuntime(
        ActorManager& actor_manager,
        ActorPtr player_actor);
    ~ActorManagerFixedStepRuntime() override;

    void SetPlayerActor(ActorPtr player_actor);

    bool AcquireCaptureOwnership() override;
    void ReleaseCaptureOwnership() noexcept override;
    std::uint64_t GetCompletedPhysicsSteps() const override;
    FixedStepCaptureBridge::BatchResult AdvanceFixedSteps(
        std::uint32_t fixed_step_count,
        FixedStepCaptureBridge::AppliedInputObserver& observer) override;
    bool JoinPhysics() override;

private:
    ActorManager& m_actor_manager;
    ActorPtr m_player_actor;
};

} // namespace WorldModel
} // namespace RoR
