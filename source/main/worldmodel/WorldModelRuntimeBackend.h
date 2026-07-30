/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Exact-step runtime adapter for the native world-model recorder.

#pragma once

#include "FixedStepCaptureBridge.h"
#include "WorldModelCaptureSession.h"

#include <cstdint>

namespace RoR {
namespace WorldModel {

/// Minimal physics boundary needed by CaptureSession. The production
/// implementation delegates to ActorManager; tests use an in-memory driver.
class FixedStepRuntime
{
public:
    virtual ~FixedStepRuntime() {}

    virtual bool AcquireCaptureOwnership() = 0;
    virtual void ReleaseCaptureOwnership() noexcept = 0;
    virtual std::uint64_t GetCompletedPhysicsSteps() const = 0;
    virtual FixedStepCaptureBridge::BatchResult AdvanceFixedSteps(
        std::uint32_t fixed_step_count,
        FixedStepCaptureBridge::AppliedInputObserver& observer) = 0;
    virtual bool JoinPhysics() = 0;
};

/// Typed state/render provider owned by the game integration.
///
/// BeginTransition snapshots raw/issued/resolved input before physics.
/// ObserveAppliedInputAtFixedStepStart snapshots the controls actually consumed
/// at every 2 kHz solver boundary. CaptureCompletedTransition emits the
/// transition plus the post-step UI-free observation and RGB bytes.
class RuntimeCaptureProvider:
    public FixedStepCaptureBridge::AppliedInputObserver
{
public:
    virtual ~RuntimeCaptureProvider() {}

    virtual bool CaptureResetBaseline(
        const ObservationId& expected_id,
        ObservationSample& observation) = 0;
    virtual bool BeginTransition(
        const TransitionId& transition) = 0;
    virtual bool CaptureCompletedTransition(
        const TransitionId& expected_transition,
        TransitionSample& transition,
        ObservationSample& observation) = 0;
};

/// Enforces the CaptureBackend call order around one exact-step runtime.
class RuntimeCaptureBackend final : public CaptureBackend
{
public:
    RuntimeCaptureBackend(
        FixedStepRuntime& runtime,
        RuntimeCaptureProvider& provider);
    ~RuntimeCaptureBackend() override;

    bool AcquireRuntimeOwnership() override;
    void ReleaseRuntimeOwnership() noexcept override;
    std::uint64_t GetCompletedPhysicsSteps() const override;
    bool CaptureResetBaseline(
        const ObservationId& expected_id,
        ObservationSample& observation) override;
    bool BeginTransition(
        const TransitionId& transition) override;
    bool AdvanceFixedSteps(
        std::uint32_t step_count) override;
    bool JoinPhysics() override;
    bool CaptureCompletedTransition(
        const TransitionId& expected_transition,
        TransitionSample& transition,
        ObservationSample& observation) override;

private:
    FixedStepRuntime& m_runtime;
    RuntimeCaptureProvider& m_provider;
    TransitionId m_active_transition;
    FixedStepCaptureBridge::BatchResult m_advance_result;
    bool m_runtime_owned;
    bool m_transition_active;
    bool m_advance_attempted;
};

} // namespace WorldModel
} // namespace RoR
