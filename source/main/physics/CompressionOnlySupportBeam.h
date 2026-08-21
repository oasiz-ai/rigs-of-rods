/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Deterministic compression-only support-beam response policy.

#pragma once

#include "BeamAxialResponse.h"

namespace RoR {
namespace CompressionOnlySupportBeam {

/// Compression-only beams may use a precompressed spring activation length,
/// while their extension-break ratio is measured from the geometric spawned
/// length. Keeping both lengths explicit prevents precompression from silently
/// changing the authored break point.
struct Response
{
    bool valid = false;
    bool compression_active = false;
    bool break_now = false;
    float spring = 0.0f;
    float damping = 0.0f;
};

inline Response Evaluate(
    float current_length,
    float activation_length,
    float spawned_length,
    float extension_break_ratio,
    float authored_spring,
    float authored_damping)
{
    Response result;
    if (!BeamAxialResponse::IsFinite(current_length) ||
        !BeamAxialResponse::IsFinite(activation_length) ||
        !BeamAxialResponse::IsFinite(spawned_length) ||
        !BeamAxialResponse::IsFinite(extension_break_ratio) ||
        !BeamAxialResponse::IsFinite(authored_spring) ||
        !BeamAxialResponse::IsFinite(authored_damping) ||
        !(current_length > 0.0f) ||
        !(activation_length > 0.0f) ||
        !(spawned_length > 0.0f) ||
        extension_break_ratio < 0.0f ||
        authored_spring < 0.0f ||
        authored_damping < 0.0f)
    {
        return result;
    }

    result.valid = true;
    if (current_length < activation_length)
    {
        result.compression_active = true;
        result.spring = authored_spring;
        result.damping = authored_damping;
        return result;
    }

    // Binary64 has enough exponent headroom for the ratio of any two finite
    // positive binary32 lengths. This avoids overflowing
    // spawned_length * (1 + extension_break_ratio) in the physics loop.
    const double relative_extension =
        static_cast<double>(current_length) /
            static_cast<double>(spawned_length) -
        1.0;
    result.break_now =
        relative_extension >
            static_cast<double>(extension_break_ratio);
    return result;
}

} // namespace CompressionOnlySupportBeam
} // namespace RoR
