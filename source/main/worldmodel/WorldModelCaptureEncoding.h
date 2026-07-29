/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Binds typed telemetry, raw RGB bytes, and capture-session samples.

#pragma once

#include "WorldModelCaptureSession.h"
#include "WorldModelTelemetry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

/// Schema 1 assigns RGB record N+1 to observation N. Zero is deliberately
/// unused, which makes an uninitialized RGB reference fail validation.
bool CanonicalRgbRecordId(
    std::uint64_t observation_index,
    std::uint64_t& record_id);

/// Binds typed telemetry to raw RGB bytes only when dimensions, stride,
/// identity and SHA-256 agree. On failure, output is unchanged.
bool EncodeObservationSample(
    const ObservationRecord& record,
    const std::vector<std::uint8_t>& rgb8,
    ObservationSample& output,
    std::string* error = nullptr);

/// Binds a fully validated typed transition to the capture session.
bool EncodeTransitionSample(
    const TransitionRecord& record,
    TransitionSample& output,
    std::string* error = nullptr);

} // namespace WorldModel
} // namespace RoR
