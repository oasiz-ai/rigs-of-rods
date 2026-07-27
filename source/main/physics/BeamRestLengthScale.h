/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file BeamRestLengthScale.h
/// @brief Fail-closed rest-length scaling for imported precompressed beams.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace RoR {
namespace Physics {
namespace Detail {

/// std::isfinite may be folded under finite-math assumptions. Inspecting the
/// IEEE-754 exponent keeps this untrusted import boundary effective in the
/// game's release floating-point mode.
inline bool IsFiniteBinary32(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
        "Beam rest-length scaling requires binary32 floats");
    static_assert(std::numeric_limits<float>::is_iec559,
        "Beam rest-length scaling requires IEC 60559 floats");

    std::uint32_t bits = 0U;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(bits); ++i)
    {
        destination[i] = source[i];
    }
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

} // namespace Detail

/// Computes `geometric_length * scale` for an imported beam.
///
/// Both inputs and the result must be finite, positive normal binary32 values.
/// Subnormal lengths are rejected because they cannot provide a stable
/// normalization direction in the solver. Failure resets `output` to zero.
inline bool TryScaleBeamRestLength(
    float geometric_length,
    float scale,
    float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;
    if (!Detail::IsFiniteBinary32(geometric_length) ||
        !Detail::IsFiniteBinary32(scale) ||
        geometric_length < std::numeric_limits<float>::min() ||
        scale < std::numeric_limits<float>::min())
    {
        return false;
    }
    const float scaled = geometric_length * scale;
    if (!Detail::IsFiniteBinary32(scaled) ||
        scaled < std::numeric_limits<float>::min())
    {
        return false;
    }
    *output = scaled;
    return true;
}

} // namespace Physics
} // namespace RoR
