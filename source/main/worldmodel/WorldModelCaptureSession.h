/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed lifecycle for one deterministic world-model episode.

#pragma once

#include "EpisodeProvenance.h"
#include "WorldModelTelemetry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

enum class CaptureLifecycle : std::uint32_t
{
    IDLE = 0U,
    ARMING,
    READY,
    CAPTURING,
    FINALIZING,
    COMPLETE,
    FAULTED
};

enum class CaptureError : std::uint32_t
{
    NONE = 0U,
    INVALID_TRANSITION,
    INVALID_CONFIG,
    INVALID_LIFECYCLE,
    PHYSICS_BOUNDARY_MISMATCH,
    BACKEND_REJECTED,
    INVALID_OBSERVATION,
    INVALID_TRANSITION_RECORD,
    SINK_REJECTED,
    FINALIZATION_REJECTED
};

struct CaptureStatus
{
    CaptureError error;
    std::uint64_t transition_index;
    std::uint64_t expected_physics_step;
    std::uint64_t actual_physics_step;

    CaptureStatus();
};

struct CaptureConfig
{
    EpisodeId episode;
    std::string target_id;
    std::uint64_t origin_completed_physics_steps;
    std::uint64_t maximum_transitions;
    EpisodeProvenance provenance;

    CaptureConfig();
};

/// The backend emits typed telemetry plus tightly packed RGB8. Validation and
/// serialization happen after the physics join; unparsed JSON never crosses
/// this trust boundary.
struct ObservationSample
{
    ObservationRecord record;
    std::vector<std::uint8_t> rgb8;

    ObservationSample();
};

struct TransitionSample
{
    TransitionRecord record;

    TransitionSample();
};

bool ValidateObservationSample(
    const ObservationSample& sample,
    const ObservationId& expected_id,
    const std::string& expected_target_id);

bool ValidateTransitionSample(
    const TransitionSample& sample,
    const TransitionId& expected_id,
    const std::string& expected_target_id);

/// Game/physics/render adapter. CaptureSession calls these in the declared
/// order and checks the fixed-step counter before and after every transition.
class CaptureBackend
{
public:
    virtual ~CaptureBackend() {}

    /// Capture owns the runtime scheduler from Begin through completion or
    /// fault. Production ownership suspends normal ActorManager frame batches.
    virtual bool AcquireRuntimeOwnership() = 0;
    virtual void ReleaseRuntimeOwnership() noexcept = 0;
    virtual std::uint64_t GetCompletedPhysicsSteps() const = 0;

    /// Capture observation zero from a fully reset, joined simulation.
    virtual bool CaptureResetBaseline(
        const ObservationId& expected_id,
        ObservationSample& observation) = 0;

    /// Sample raw/issued/resolved input before any transition step executes.
    virtual bool BeginTransition(const TransitionId& transition) = 0;

    /// Apply and record controls at every fixed-step start while advancing the
    /// exact requested count. No wall-clock scheduling is permitted here.
    virtual bool AdvanceFixedSteps(std::uint32_t step_count) = 0;

    virtual bool JoinPhysics() = 0;

    /// Capture applied-input lineage, events, immutable state and UI-free RGB
    /// after JoinPhysics(), without advancing simulation or polling input.
    virtual bool CaptureCompletedTransition(
        const TransitionId& expected_transition,
        TransitionSample& transition,
        ObservationSample& observation) = 0;
};

/// Durable artifact adapter. AppendTransitionAndObservation is a logical
/// transaction: if it fails, the session faults and Complete is never called.
class CaptureSink
{
public:
    virtual ~CaptureSink() {}

    virtual bool BeginEpisode(const CaptureConfig& config) = 0;
    virtual bool AppendBaseline(
        const ObservationSample& observation) = 0;
    virtual bool AppendTransitionAndObservation(
        const TransitionSample& transition,
        const ObservationSample& observation) = 0;
    virtual bool CompleteEpisode() = 0;
    virtual void AbortEpisode() = 0;
};

class CaptureSession
{
public:
    CaptureSession(CaptureBackend& backend, CaptureSink& sink);
    ~CaptureSession();

    bool Begin(const CaptureConfig& config);
    bool CaptureNext();
    bool Complete();

    CaptureLifecycle GetLifecycle() const;
    const CaptureStatus& GetStatus() const;
    const CaptureConfig& GetConfig() const;
    std::uint64_t GetCapturedTransitionCount() const;
    const ObservationId& GetCurrentObservation() const;

private:
    bool Fault(
        CaptureError error,
        std::uint64_t transition_index,
        std::uint64_t expected_step,
        std::uint64_t actual_step);
    void ReleaseRuntimeOwnership() noexcept;

    CaptureBackend& m_backend;
    CaptureSink& m_sink;
    CaptureLifecycle m_lifecycle;
    CaptureStatus m_status;
    CaptureConfig m_config;
    ObservationId m_current_observation;
    std::uint64_t m_captured_transitions;
    bool m_sink_started;
    bool m_runtime_owned;
};

} // namespace WorldModel
} // namespace RoR
