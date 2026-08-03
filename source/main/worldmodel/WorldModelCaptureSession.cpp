/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "WorldModelCaptureSession.h"

#include "EpisodeFormat.h"
#include "WorldModelCaptureEncoding.h"

#include <limits>

namespace {

bool CheckedRgbSize(
    std::uint32_t row_stride,
    std::uint32_t height,
    std::size_t& size)
{
    if (row_stride == 0U || height == 0U)
        return false;
    const std::size_t stride =
        static_cast<std::size_t>(row_stride);
    const std::size_t rows =
        static_cast<std::size_t>(height);
    if (rows > std::numeric_limits<std::size_t>::max() / stride)
        return false;
    size = stride * rows;
    return true;
}

bool ObservationMatchesProvenance(
    const RoR::WorldModel::ObservationSample& sample,
    const RoR::WorldModel::EpisodeProvenance& provenance)
{
    const RoR::WorldModel::ObservationRecord& record = sample.record;
    return record.target_id == provenance.vehicle_id &&
        record.world.terrain_id == provenance.terrain_id &&
        record.world.terrain_sha256 == provenance.terrain_sha256 &&
        record.camera.camera_id == provenance.camera_profile_id &&
        record.camera.coordinate_frame == provenance.coordinate_frame &&
        record.rgb.pixel_format == provenance.pixel_format &&
        record.rgb.color_space == provenance.color_space;
}

bool ControlValueMatchesSchema(
    const RoR::WorldModel::ControlSample& sample)
{
    if (sample.control_id == "vehicle.steering")
        return sample.value >= -1.0 && sample.value <= 1.0;
    if (sample.control_id == "vehicle.parking-brake")
        return sample.value == 0.0 || sample.value == 1.0;
    return sample.value >= 0.0 && sample.value <= 1.0;
}

bool InitialStageMatchesControlProfile(
    const std::vector<RoR::WorldModel::ControlSample>& samples,
    const std::vector<std::string>& control_ids,
    std::uint64_t first_tick)
{
    if (samples.size() != control_ids.size())
        return false;
    for (std::size_t index = 0U; index < control_ids.size(); ++index)
    {
        const RoR::WorldModel::ControlSample& sample = samples[index];
        if (sample.control_id != control_ids[index] ||
            sample.effective_tick != first_tick ||
            !ControlValueMatchesSchema(sample))
        {
            return false;
        }
    }
    return true;
}

bool TransitionMatchesControlProfile(
    const RoR::WorldModel::TransitionRecord& record,
    const RoR::WorldModel::EpisodeProvenance& provenance)
{
    const std::vector<std::string>& control_ids =
        provenance.control_ids;
    const std::uint64_t first_tick =
        record.effective_steps.first_completed_step;
    const std::uint64_t last_tick =
        record.effective_steps.last_completed_step;
    if (control_ids.empty() || first_tick >= last_tick ||
        !InitialStageMatchesControlProfile(
            record.controls.raw,
            control_ids,
            first_tick) ||
        !InitialStageMatchesControlProfile(
            record.controls.issued,
            control_ids,
            first_tick) ||
        !InitialStageMatchesControlProfile(
            record.controls.resolved,
            control_ids,
            first_tick))
    {
        return false;
    }

    const std::uint64_t tick_count = last_tick - first_tick;
    if (tick_count >
        std::numeric_limits<std::size_t>::max() /
            control_ids.size())
    {
        return false;
    }
    const std::size_t expected_applied =
        static_cast<std::size_t>(tick_count) *
        control_ids.size();
    if (record.controls.applied.size() != expected_applied)
        return false;

    std::size_t index = 0U;
    for (std::uint64_t tick = first_tick;
         tick < last_tick;
         ++tick)
    {
        for (const std::string& control_id : control_ids)
        {
            const RoR::WorldModel::ControlSample& sample =
                record.controls.applied[index++];
            if (sample.control_id != control_id ||
                sample.effective_tick != tick ||
                !ControlValueMatchesSchema(sample))
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace RoR {
namespace WorldModel {

CaptureStatus::CaptureStatus():
    error(CaptureError::NONE),
    transition_index(0U),
    expected_physics_step(0U),
    actual_physics_step(0U)
{
}

CaptureConfig::CaptureConfig():
    episode(),
    target_id(),
    origin_completed_physics_steps(0U),
    maximum_transitions(0U),
    provenance()
{
}

ObservationSample::ObservationSample():
    record(),
    rgb8()
{
}

TransitionSample::TransitionSample():
    record()
{
}

bool ValidateObservationSample(
    const ObservationSample& sample,
    const ObservationId& expected_id,
    const std::string& expected_target_id)
{
    const ObservationRecord& record = sample.record;
    if (record.observation_id != expected_id ||
        record.target_id != expected_target_id ||
        !ValidateObservationRecord(record) ||
        record.rgb.width == 0U ||
        record.rgb.height == 0U ||
        record.rgb.width >
            std::numeric_limits<std::uint32_t>::max() / 3U ||
        record.rgb.row_stride_bytes != record.rgb.width * 3U)
    {
        return false;
    }

    std::size_t expected_size = 0U;
    std::uint64_t expected_rgb_record_id = 0U;
    return CanonicalRgbRecordId(
            record.observation_id.observation_index,
            expected_rgb_record_id) &&
        record.rgb.record_id == expected_rgb_record_id &&
        CheckedRgbSize(
            record.rgb.row_stride_bytes,
            record.rgb.height,
            expected_size) &&
        sample.rgb8.size() == expected_size &&
        ComputeSha256(
            sample.rgb8.data(),
            sample.rgb8.size()).ToHex() ==
            record.rgb.raw_sha256;
}

bool ValidateTransitionSample(
    const TransitionSample& sample,
    const TransitionId& expected_id,
    const std::string& expected_target_id)
{
    return sample.record.transition_id == expected_id &&
        sample.record.target_id == expected_target_id &&
        ValidateTransitionRecord(sample.record);
}

CaptureSession::CaptureSession(
    CaptureBackend& backend,
    CaptureSink& sink):
    m_backend(backend),
    m_sink(sink),
    m_lifecycle(CaptureLifecycle::IDLE),
    m_status(),
    m_config(),
    m_current_observation(),
    m_captured_transitions(0U),
    m_sink_started(false),
    m_runtime_owned(false)
{
}

CaptureSession::~CaptureSession()
{
    this->ReleaseRuntimeOwnership();
}

void CaptureSession::ReleaseRuntimeOwnership() noexcept
{
    if (!m_runtime_owned)
        return;
    m_backend.ReleaseRuntimeOwnership();
    m_runtime_owned = false;
}

bool CaptureSession::Fault(
    CaptureError error,
    std::uint64_t transition_index,
    std::uint64_t expected_step,
    std::uint64_t actual_step)
{
    if (m_lifecycle != CaptureLifecycle::FAULTED)
    {
        m_status.error = error;
        m_status.transition_index = transition_index;
        m_status.expected_physics_step = expected_step;
        m_status.actual_physics_step = actual_step;
        m_lifecycle = CaptureLifecycle::FAULTED;
        if (m_sink_started)
        {
            m_sink.AbortEpisode();
            m_sink_started = false;
        }
        this->ReleaseRuntimeOwnership();
    }
    return false;
}

bool CaptureSession::Begin(const CaptureConfig& config)
{
    if (m_lifecycle != CaptureLifecycle::IDLE)
    {
        std::uint64_t actual_step =
            config.origin_completed_physics_steps;
        if (m_backend.JoinPhysics())
            actual_step = m_backend.GetCompletedPhysicsSteps();
        return Fault(
            CaptureError::INVALID_LIFECYCLE,
            0U,
            config.origin_completed_physics_steps,
            actual_step);
    }
    if (!m_backend.AcquireRuntimeOwnership())
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            0U,
            config.origin_completed_physics_steps,
            config.origin_completed_physics_steps);
    }
    m_runtime_owned = true;

    // Enter capture only from a joined physics boundary. In normal gameplay
    // the ActorManager counter is written by the asynchronous physics worker;
    // reading it before this join would be both a race and a nondeterministic
    // choice of observation zero.
    if (!m_backend.JoinPhysics())
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            0U,
            config.origin_completed_physics_steps,
            config.origin_completed_physics_steps);
    }
    const std::uint64_t joined_step =
        m_backend.GetCompletedPhysicsSteps();
    if (!IsValidEpisodeId(config.episode) ||
        config.target_id.empty() ||
        config.origin_completed_physics_steps != 0U ||
        config.maximum_transitions == 0U ||
        !ValidateEpisodeProvenance(config.provenance) ||
        config.target_id != config.provenance.vehicle_id ||
        config.provenance.reset_seed !=
            DeriveSeed(
                config.provenance.root_seed,
                SeedDomain::RESET,
                config.episode,
                0U))
    {
        return Fault(
            CaptureError::INVALID_CONFIG,
            0U,
            config.origin_completed_physics_steps,
            joined_step);
    }

    m_lifecycle = CaptureLifecycle::ARMING;
    m_config = config;
    const std::uint64_t actual_step = joined_step;
    if (actual_step != config.origin_completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            0U,
            config.origin_completed_physics_steps,
            actual_step);
    }

    ObservationId baseline_id;
    if (!MakeObservationId(
            config.episode,
            config.origin_completed_physics_steps,
            0U,
            baseline_id))
    {
        return Fault(
            CaptureError::INVALID_CONFIG,
            0U,
            config.origin_completed_physics_steps,
            actual_step);
    }

    ObservationSample baseline;
    if (!m_backend.CaptureResetBaseline(
            baseline_id,
            baseline))
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            0U,
            baseline_id.completed_physics_steps,
            m_backend.GetCompletedPhysicsSteps());
    }
    if (m_backend.GetCompletedPhysicsSteps() !=
            baseline_id.completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            0U,
            baseline_id.completed_physics_steps,
            m_backend.GetCompletedPhysicsSteps());
    }
    if (!ValidateObservationSample(
            baseline,
            baseline_id,
            config.target_id) ||
        !ObservationMatchesProvenance(
            baseline,
            config.provenance) ||
        baseline.record.state_sha256 !=
            config.provenance.reset_state_sha256)
    {
        return Fault(
            CaptureError::INVALID_OBSERVATION,
            0U,
            baseline_id.completed_physics_steps,
            m_backend.GetCompletedPhysicsSteps());
    }

    if (!m_sink.BeginEpisode(config))
    {
        return Fault(
            CaptureError::SINK_REJECTED,
            0U,
            baseline_id.completed_physics_steps,
            m_backend.GetCompletedPhysicsSteps());
    }
    m_sink_started = true;
    if (!m_sink.AppendBaseline(baseline))
    {
        return Fault(
            CaptureError::SINK_REJECTED,
            0U,
            baseline_id.completed_physics_steps,
            m_backend.GetCompletedPhysicsSteps());
    }

    m_current_observation = baseline_id;
    m_captured_transitions = 0U;
    m_lifecycle = CaptureLifecycle::READY;
    return true;
}

bool CaptureSession::CaptureNext()
{
    // A render frame may have queued asynchronous physics since the preceding
    // API call in a non-production backend. Always establish and cache one
    // joined source boundary before reading the counter or invoking a provider.
    if (!m_backend.JoinPhysics())
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            m_current_observation.completed_physics_steps);
    }
    const std::uint64_t joined_source_step =
        m_backend.GetCompletedPhysicsSteps();

    if (m_lifecycle != CaptureLifecycle::READY &&
        m_lifecycle != CaptureLifecycle::CAPTURING)
    {
        return Fault(
            CaptureError::INVALID_LIFECYCLE,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_source_step);
    }
    if (m_captured_transitions >=
        m_config.maximum_transitions)
    {
        return Fault(
            CaptureError::INVALID_TRANSITION,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_source_step);
    }

    m_lifecycle = CaptureLifecycle::CAPTURING;
    TransitionId expected;
    if (!MakeTransitionId(
            m_config.episode,
            m_config.origin_completed_physics_steps,
            m_captured_transitions,
            expected) ||
        expected.source != m_current_observation)
    {
        return Fault(
            CaptureError::INVALID_TRANSITION,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_source_step);
    }

    if (joined_source_step !=
        expected.source.completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            m_captured_transitions,
            expected.source.completed_physics_steps,
            joined_source_step);
    }
    if (!m_backend.BeginTransition(expected))
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            expected.source.completed_physics_steps,
            joined_source_step);
    }

    const std::uint64_t step_count =
        expected.target.completed_physics_steps -
        expected.source.completed_physics_steps;
    if (step_count >
        std::numeric_limits<std::uint32_t>::max())
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            joined_source_step);
    }
    const bool advanced = m_backend.AdvanceFixedSteps(
        static_cast<std::uint32_t>(step_count));
    // Once advancement is attempted, always drain physics workers before any
    // error path can quarantine or inspect the partial episode.
    const bool joined = m_backend.JoinPhysics();
    const std::uint64_t joined_target_step = joined
        ? m_backend.GetCompletedPhysicsSteps()
        : joined_source_step;
    if (!advanced || !joined)
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            joined_target_step);
    }

    if (joined_target_step !=
        expected.target.completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            joined_target_step);
    }

    TransitionSample transition;
    ObservationSample observation;
    const bool captured =
        m_backend.CaptureCompletedTransition(
            expected,
            transition,
            observation);
    // Provider reads occur only after the joined target boundary above. Join
    // once more before inspecting the counter so provider-side mutation or
    // accidental scheduling is detected from another cached boundary.
    const bool capture_joined = m_backend.JoinPhysics();
    const std::uint64_t captured_boundary = capture_joined
        ? m_backend.GetCompletedPhysicsSteps()
        : joined_target_step;
    if (!captured || !capture_joined)
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            captured_boundary);
    }
    if (captured_boundary !=
            expected.target.completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            captured_boundary);
    }
    if (!ValidateTransitionSample(
            transition,
            expected,
            m_config.target_id) ||
        !TransitionMatchesControlProfile(
            transition.record,
            m_config.provenance))
    {
        return Fault(
            CaptureError::INVALID_TRANSITION_RECORD,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            captured_boundary);
    }
    if (!ValidateObservationSample(
            observation,
            expected.target,
            m_config.target_id) ||
        !ObservationMatchesProvenance(
            observation,
            m_config.provenance))
    {
        return Fault(
            CaptureError::INVALID_OBSERVATION,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            captured_boundary);
    }
    if (!m_sink.AppendTransitionAndObservation(
            transition,
            observation))
    {
        return Fault(
            CaptureError::SINK_REJECTED,
            m_captured_transitions,
            expected.target.completed_physics_steps,
            captured_boundary);
    }

    m_current_observation = expected.target;
    ++m_captured_transitions;
    m_lifecycle = CaptureLifecycle::READY;
    return true;
}

bool CaptureSession::Complete()
{
    if (!m_backend.JoinPhysics())
    {
        return Fault(
            CaptureError::BACKEND_REJECTED,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            m_current_observation.completed_physics_steps);
    }
    const std::uint64_t joined_step =
        m_backend.GetCompletedPhysicsSteps();
    if (m_lifecycle != CaptureLifecycle::READY ||
        !m_sink_started)
    {
        return Fault(
            CaptureError::INVALID_LIFECYCLE,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_step);
    }
    if (m_captured_transitions !=
        m_config.maximum_transitions)
    {
        return Fault(
            CaptureError::INVALID_TRANSITION,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_step);
    }
    if (joined_step !=
        m_current_observation.completed_physics_steps)
    {
        return Fault(
            CaptureError::PHYSICS_BOUNDARY_MISMATCH,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_step);
    }

    m_lifecycle = CaptureLifecycle::FINALIZING;
    if (!m_sink.CompleteEpisode())
    {
        return Fault(
            CaptureError::FINALIZATION_REJECTED,
            m_captured_transitions,
            m_current_observation.completed_physics_steps,
            joined_step);
    }
    m_sink_started = false;
    m_lifecycle = CaptureLifecycle::COMPLETE;
    this->ReleaseRuntimeOwnership();
    return true;
}

CaptureLifecycle CaptureSession::GetLifecycle() const
{
    return m_lifecycle;
}

const CaptureStatus& CaptureSession::GetStatus() const
{
    return m_status;
}

const CaptureConfig& CaptureSession::GetConfig() const
{
    return m_config;
}

std::uint64_t
CaptureSession::GetCapturedTransitionCount() const
{
    return m_captured_transitions;
}

const ObservationId&
CaptureSession::GetCurrentObservation() const
{
    return m_current_observation;
}

} // namespace WorldModel
} // namespace RoR
