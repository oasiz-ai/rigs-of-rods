/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "WorldModelCaptureEncoding.h"

#include "EpisodeFormat.h"

#include <limits>
#include <utility>

namespace {

bool Fail(std::string* error, const std::string& message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

bool CheckedRgbSize(
    std::uint32_t row_stride,
    std::uint32_t height,
    std::size_t& byte_count)
{
    if (row_stride == 0U || height == 0U)
        return false;
    const std::size_t stride = static_cast<std::size_t>(row_stride);
    const std::size_t rows = static_cast<std::size_t>(height);
    if (rows > std::numeric_limits<std::size_t>::max() / stride)
        return false;
    byte_count = stride * rows;
    return true;
}

} // namespace

namespace RoR {
namespace WorldModel {

bool CanonicalRgbRecordId(
    std::uint64_t observation_index,
    std::uint64_t& record_id)
{
    if (observation_index == std::numeric_limits<std::uint64_t>::max())
        return false;
    record_id = observation_index + 1U;
    return true;
}

bool EncodeObservationSample(
    const ObservationRecord& record,
    const std::vector<std::uint8_t>& rgb8,
    ObservationSample& output,
    std::string* error)
{
    if (!ValidateObservationRecord(record, error))
        return false;

    std::uint64_t expected_rgb_record_id = 0U;
    if (!CanonicalRgbRecordId(
            record.observation_id.observation_index,
            expected_rgb_record_id) ||
        record.rgb.record_id != expected_rgb_record_id)
    {
        return Fail(
            error,
            "rgb.record_id must equal observation_index + 1");
    }

    std::size_t expected_size = 0U;
    if (!CheckedRgbSize(
            record.rgb.row_stride_bytes,
            record.rgb.height,
            expected_size) ||
        rgb8.size() != expected_size)
    {
        return Fail(
            error,
            "raw RGB byte count does not match the declared dimensions");
    }

    const std::string actual_rgb_sha256 =
        ComputeSha256(rgb8.data(), rgb8.size()).ToHex();
    if (actual_rgb_sha256 != record.rgb.raw_sha256)
    {
        return Fail(
            error,
            "raw RGB SHA-256 does not match the observation");
    }

    ObservationSample candidate;
    candidate.record = record;
    candidate.rgb8 = rgb8;
    if (!ValidateObservationSample(
            candidate,
            record.observation_id,
            record.target_id))
    {
        return Fail(
            error,
            "encoded observation does not satisfy the capture contract");
    }

    output = std::move(candidate);
    return true;
}

bool EncodeTransitionSample(
    const TransitionRecord& record,
    TransitionSample& output,
    std::string* error)
{
    if (!ValidateTransitionRecord(record, error))
        return false;

    TransitionSample candidate;
    candidate.record = record;
    if (!ValidateTransitionSample(
            candidate,
            record.transition_id,
            record.target_id))
    {
        return Fail(
            error,
            "encoded transition does not satisfy the capture contract");
    }

    output = std::move(candidate);
    return true;
}

} // namespace WorldModel
} // namespace RoR
