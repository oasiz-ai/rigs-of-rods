/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "EpisodeCaptureSink.h"

#include "EpisodeValidator.h"
#include "WorldModelCaptureEncoding.h"

#include <limits>
#include <utility>

namespace RoR {
namespace WorldModel {

bool CanonicalObservationTelemetryRecordId(
    std::uint64_t observation_index,
    std::uint64_t& record_id)
{
    const std::uint64_t maximum =
        (std::numeric_limits<std::uint64_t>::max() - 1U) / 2U;
    if (observation_index > maximum)
        return false;
    record_id = observation_index * 2U + 1U;
    return true;
}

bool CanonicalTransitionTelemetryRecordId(
    std::uint64_t transition_index,
    std::uint64_t& record_id)
{
    const std::uint64_t maximum =
        (std::numeric_limits<std::uint64_t>::max() - 2U) / 2U;
    if (transition_index > maximum)
        return false;
    record_id = transition_index * 2U + 2U;
    return true;
}

EpisodeCaptureSink::EpisodeCaptureSink(
    std::filesystem::path output_root,
    EpisodeWriterOptions options):
    m_output_root(std::move(output_root)),
    m_options(options),
    m_writer(),
    m_config(),
    m_last_error(),
    m_transition_count(0U),
    m_started(false),
    m_aborted(false),
    m_complete(false)
{
}

bool EpisodeCaptureSink::Fail(const std::string& message)
{
    m_last_error = message;
    return false;
}

bool EpisodeCaptureSink::BeginEpisode(const CaptureConfig& config)
{
    if (m_started || m_aborted || m_complete)
        return Fail("episode sink cannot be reused");
    if (!IsValidEpisodeId(config.episode) ||
        !IsCanonicalWorldModelIdentifier(config.target_id) ||
        config.maximum_transitions == 0U)
    {
        return Fail("episode sink received an invalid capture configuration");
    }
    std::uint64_t last_transition_record = 0U;
    std::uint64_t last_observation_record = 0U;
    std::uint64_t last_rgb_record = 0U;
    if (!CanonicalTransitionTelemetryRecordId(
            config.maximum_transitions - 1U,
            last_transition_record) ||
        !CanonicalObservationTelemetryRecordId(
            config.maximum_transitions,
            last_observation_record) ||
        !CanonicalRgbRecordId(
            config.maximum_transitions,
            last_rgb_record))
    {
        return Fail("episode record identifiers would overflow");
    }

    std::string episode_id;
    if (!FormatEpisodeId(config.episode, episode_id))
        return Fail("episode identifier could not be formatted");
    std::string writer_error;
    if (!m_writer.Open(
            m_output_root,
            episode_id,
            config.provenance,
            m_options,
            &writer_error))
    {
        return Fail("episode writer open failed: " + writer_error);
    }

    m_config = config;
    m_transition_count = 0U;
    m_started = true;
    return true;
}

bool EpisodeCaptureSink::AppendObservation(
    const ObservationSample& observation)
{
    std::string canonical_json;
    std::string telemetry_error;
    if (!SerializeObservationRecord(
            observation.record,
            canonical_json,
            &telemetry_error))
    {
        return Fail(
            "observation serialization failed: " + telemetry_error);
    }

    std::uint64_t rgb_record_id = 0U;
    std::uint64_t telemetry_record_id = 0U;
    if (!CanonicalRgbRecordId(
            observation.record.observation_id.observation_index,
            rgb_record_id) ||
        !CanonicalObservationTelemetryRecordId(
            observation.record.observation_id.observation_index,
            telemetry_record_id))
    {
        return Fail("observation record identifier overflow");
    }

    std::string writer_error;
    if (!m_writer.AppendRgbRecord(
            rgb_record_id,
            EPISODE_RGB8,
            observation.rgb8.data(),
            observation.rgb8.size(),
            &writer_error))
    {
        return Fail("RGB append failed: " + writer_error);
    }
    if (!m_writer.AppendTelemetryRecord(
            telemetry_record_id,
            EPISODE_TELEMETRY_OBSERVATION,
            canonical_json.data(),
            canonical_json.size(),
            &writer_error))
    {
        return Fail("observation append failed: " + writer_error);
    }
    return true;
}

bool EpisodeCaptureSink::AppendBaseline(
    const ObservationSample& observation)
{
    if (!m_started || m_aborted || m_complete)
        return Fail("baseline append requires an open episode");
    ObservationId expected;
    if (!MakeObservationId(
            m_config.episode,
            m_config.origin_completed_physics_steps,
            0U,
            expected) ||
        !ValidateObservationSample(
            observation,
            expected,
            m_config.target_id))
    {
        return Fail("baseline observation identity is invalid");
    }
    return AppendObservation(observation);
}

bool EpisodeCaptureSink::AppendTransitionAndObservation(
    const TransitionSample& transition,
    const ObservationSample& observation)
{
    if (!m_started || m_aborted || m_complete)
        return Fail("transition append requires an open episode");
    if (m_transition_count >= m_config.maximum_transitions)
        return Fail("transition count exceeds the declared episode length");

    TransitionId expected;
    if (!MakeTransitionId(
            m_config.episode,
            m_config.origin_completed_physics_steps,
            m_transition_count,
            expected) ||
        !ValidateTransitionSample(
            transition,
            expected,
            m_config.target_id) ||
        !ValidateObservationSample(
            observation,
            expected.target,
            m_config.target_id) ||
        transition.record.transition_index !=
            m_transition_count)
    {
        return Fail("transition/observation identity join is invalid");
    }

    std::uint64_t transition_record_id = 0U;
    if (!CanonicalTransitionTelemetryRecordId(
            m_transition_count,
            transition_record_id))
    {
        return Fail("transition record identifier overflow");
    }

    std::string writer_error;
    std::string canonical_json;
    std::string telemetry_error;
    if (!SerializeTransitionRecord(
            transition.record,
            canonical_json,
            &telemetry_error))
    {
        return Fail(
            "transition serialization failed: " + telemetry_error);
    }
    if (!m_writer.AppendTelemetryRecord(
            transition_record_id,
            EPISODE_TELEMETRY_TRANSITION,
            canonical_json.data(),
            canonical_json.size(),
            &writer_error))
    {
        return Fail("transition append failed: " + writer_error);
    }
    if (!AppendObservation(observation))
        return false;

    ++m_transition_count;
    return true;
}

bool EpisodeCaptureSink::CompleteEpisode()
{
    if (!m_started || m_aborted || m_complete)
        return Fail("completion requires an open episode");
    if (m_transition_count != m_config.maximum_transitions)
        return Fail("episode transition count does not match the declaration");

    std::string writer_error;
    if (!m_writer.Complete(&writer_error))
        return Fail("episode writer completion failed: " + writer_error);

    const EpisodeValidationResult result =
        EpisodeValidator::Validate(m_writer.GetFinalDirectory());
    if (!result.IsValid())
    {
        return Fail(
            std::string("completed episode failed validation: ") +
            EpisodeValidationErrorName(result.error) + ": " +
            result.detail);
    }
    std::uint64_t expected_telemetry = 0U;
    std::uint64_t expected_rgb = 0U;
    if (!CanonicalObservationTelemetryRecordId(
            m_config.maximum_transitions,
            expected_telemetry) ||
        !CanonicalRgbRecordId(
            m_config.maximum_transitions,
            expected_rgb))
    {
        return Fail("completed episode record count overflow");
    }
    if (result.telemetry_record_count != expected_telemetry ||
        result.rgb_record_count != expected_rgb)
    {
        return Fail("completed episode record counts are inconsistent");
    }

    m_complete = true;
    return true;
}

void EpisodeCaptureSink::AbortEpisode()
{
    if (!m_complete)
        m_aborted = true;
}

const std::string& EpisodeCaptureSink::GetLastError() const
{
    return m_last_error;
}

const std::filesystem::path&
EpisodeCaptureSink::GetPartialDirectory() const
{
    return m_writer.GetPartialDirectory();
}

const std::filesystem::path&
EpisodeCaptureSink::GetFinalDirectory() const
{
    return m_writer.GetFinalDirectory();
}

bool EpisodeCaptureSink::IsComplete() const
{
    return m_complete;
}

} // namespace WorldModel
} // namespace RoR
