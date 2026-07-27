/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

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

/// @file
/// @brief Dependency-free, numerically bounded axial beam response helpers.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(_MSC_VER)
#   define ROR_BEAM_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#   define ROR_BEAM_FORCE_INLINE inline __attribute__((always_inline))
#else
#   define ROR_BEAM_FORCE_INLINE inline
#endif

namespace RoR {
namespace BeamAxialResponse {

static_assert(sizeof(float) == sizeof(std::uint32_t), "32-bit IEEE-754 float required");
static_assert(sizeof(double) == sizeof(std::uint64_t), "64-bit IEEE-754 double required");
static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float required");
static_assert(std::numeric_limits<double>::is_iec559, "IEEE-754 double required");

/// Squared lengths at or below this value have no reliable axial direction.
static const float MIN_LENGTH_SQUARED = 1.0e-12f;

/// Result of applying viscous damping along a beam axis.
struct DampingResult
{
    float force = 0.0f;                 //!< Signed axial force opposing relative velocity.
    float effective_coefficient = 0.0f; //!< Damping coefficient after the energy bound.
    bool  was_limited = false;
};

/// std::isfinite() can be optimized away by the game's release `-ffast-math`
/// flags. Inspecting the IEEE-754 exponent bits keeps the simulation guard
/// effective in those builds.
ROR_BEAM_FORCE_INLINE bool IsFinite(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint32_t observed_bits = bits;
    return (observed_bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

ROR_BEAM_FORCE_INLINE bool IsFinite(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint64_t observed_bits = bits;
    return (observed_bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

ROR_BEAM_FORCE_INLINE bool HasUsableLength(float squared_length)
{
    return IsFinite(squared_length) && squared_length > MIN_LENGTH_SQUARED;
}

ROR_BEAM_FORCE_INLINE bool HasUsableLength(double squared_length)
{
    return IsFinite(squared_length) &&
        squared_length > static_cast<double>(MIN_LENGTH_SQUARED);
}

/// Returns a finite inverse mass. Extremely small positive masses saturate
/// instead of allowing their reciprocal to become infinity.
ROR_BEAM_FORCE_INLINE float InverseMass(float mass, bool movable)
{
    if (!movable || !IsFinite(mass) || mass <= 0.0f)
    {
        return 0.0f;
    }

    const float inverse_mass = 1.0f / mass;
    return IsFinite(inverse_mass)
        ? inverse_mass
        : std::numeric_limits<float>::max();
}

ROR_BEAM_FORCE_INLINE float EffectiveMass(
    float mass_1,
    float mass_2,
    bool movable_1,
    bool movable_2)
{
    if ((movable_1 && (!IsFinite(mass_1) || mass_1 <= 0.0f)) ||
        (movable_2 && (!IsFinite(mass_2) || mass_2 <= 0.0f)))
    {
        return 0.0f;
    }

    if (movable_1 && movable_2)
    {
        const float smaller_mass = std::min(mass_1, mass_2);
        const float larger_mass = std::max(mass_1, mass_2);
        return smaller_mass / (1.0f + smaller_mass / larger_mass);
    }
    if (movable_1)
    {
        return mass_1;
    }
    if (movable_2)
    {
        return mass_2;
    }
    return 0.0f;
}

/// Computes a viscous damping force that cannot reverse the two endpoints'
/// relative axial velocity in a single fixed step.
///
/// For inverse effective mass `w`, unconstrained damping is `F = -d*v`.
/// Bounding `d` to `1 / (w*dt)` guarantees:
///
///     v_next = v + w*dt*F
///
/// reaches zero at most, instead of crossing zero and adding kinetic energy.
/// An immovable endpoint contributes zero inverse mass. Mirrored half-beam
/// pairs must include both endpoints in every call so their two forces share
/// this bound. Mark an endpoint immovable only when it is genuinely kinematic.
ROR_BEAM_FORCE_INLINE DampingResult ComputeDamping(
    float relative_velocity,
    float damping_coefficient,
    float time_step,
    float mass_1,
    float mass_2,
    bool movable_1,
    bool movable_2)
{
    DampingResult result;

    if (!IsFinite(relative_velocity) ||
        !IsFinite(damping_coefficient) ||
        !IsFinite(time_step) ||
        (movable_1 && (!IsFinite(mass_1) || mass_1 <= 0.0f)) ||
        (movable_2 && (!IsFinite(mass_2) || mass_2 <= 0.0f)) ||
        damping_coefficient <= 0.0f ||
        time_step <= 0.0f)
    {
        return result;
    }

    if (!movable_1 && !movable_2)
    {
        return result;
    }

    // Most vehicle beams are comfortably below their exact damping limit.
    // The reduced mass of two movable endpoints is always at least half the
    // smaller endpoint mass, which gives this division-free conservative
    // fast path. A single movable endpoint uses its full mass.
    const float conservative_effective_mass =
        movable_1 && movable_2
            ? 0.5f * std::min(mass_1, mass_2)
            : (movable_1 ? mass_1 : mass_2);
    const float requested_damping_mass = damping_coefficient * time_step;
    if (IsFinite(requested_damping_mass) &&
        requested_damping_mass <= conservative_effective_mass)
    {
        result.effective_coefficient = damping_coefficient;
        result.force = -damping_coefficient * relative_velocity;
        if (!IsFinite(result.force))
        {
            return DampingResult();
        }
        return result;
    }

    const float effective_mass =
        EffectiveMass(mass_1, mass_2, movable_1, movable_2);
    if (effective_mass <= 0.0f)
    {
        return result;
    }

    const float maximum_coefficient = effective_mass / time_step;
    if (!IsFinite(maximum_coefficient) || maximum_coefficient <= 0.0f)
    {
        return result;
    }

    result.effective_coefficient =
        std::min(damping_coefficient, maximum_coefficient);
    result.was_limited = result.effective_coefficient < damping_coefficient;
    result.force = -result.effective_coefficient * relative_velocity;

    // Avoid propagating an overflow into every node connected to this beam.
    if (!IsFinite(result.force))
    {
        return DampingResult();
    }

    return result;
}

} // namespace BeamAxialResponse
} // namespace RoR

#undef ROR_BEAM_FORCE_INLINE
