/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Durable CaptureSink backed by the native episode writer.

#pragma once

#include "EpisodeWriter.h"
#include "WorldModelCaptureSession.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace RoR {
namespace WorldModel {

static const std::uint32_t EPISODE_TELEMETRY_OBSERVATION = 1U;
static const std::uint32_t EPISODE_TELEMETRY_TRANSITION = 2U;
static const std::uint32_t EPISODE_RGB8 = 1U;

/// Record IDs are deterministic and independent for each stream:
/// telemetry observation N = 2*N+1, transition N = 2*N+2, RGB N = N+1.
bool CanonicalObservationTelemetryRecordId(
    std::uint64_t observation_index,
    std::uint64_t& record_id);
bool CanonicalTransitionTelemetryRecordId(
    std::uint64_t transition_index,
    std::uint64_t& record_id);

class EpisodeCaptureSink final : public CaptureSink
{
public:
    explicit EpisodeCaptureSink(
        std::filesystem::path output_root,
        EpisodeWriterOptions options = EpisodeWriterOptions());

    bool BeginEpisode(const CaptureConfig& config) override;
    bool AppendBaseline(const ObservationSample& observation) override;
    bool AppendTransitionAndObservation(
        const TransitionSample& transition,
        const ObservationSample& observation) override;
    bool CompleteEpisode() override;
    void AbortEpisode() override;

    const std::string& GetLastError() const;
    const std::filesystem::path& GetPartialDirectory() const;
    const std::filesystem::path& GetFinalDirectory() const;
    bool IsComplete() const;

private:
    bool Fail(const std::string& message);
    bool AppendObservation(const ObservationSample& observation);

    std::filesystem::path m_output_root;
    EpisodeWriterOptions m_options;
    EpisodeWriter m_writer;
    CaptureConfig m_config;
    std::string m_last_error;
    std::uint64_t m_transition_count;
    bool m_started;
    bool m_aborted;
    bool m_complete;
};

} // namespace WorldModel
} // namespace RoR
