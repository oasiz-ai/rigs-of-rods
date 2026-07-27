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

/// @file HydroActuatorResponse.h
/// @brief Dependency-free, bounded response kernel for variable-length beams.

#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace RoR {

/// Versioned interpretation used by the native BeamNG hydro adapter.
///
/// Ratios are relative to the authored initial beam length. Input limits map
/// to the fully contracted/extended positions and input_center maps to the
/// initial-length position. A supplied factor overrides in/out length limits
/// and input_factor, matching the documented precedence.
struct HydroActuatorConfig
{
    bool has_factor = false;
    double factor = 0.0;

    double in_limit = 1.0;
    double out_limit = 2.0;
    double input_factor = 1.0;
    double input_center = 0.0;
    double input_in_limit = -1.0;
    double input_out_limit = 1.0;

    /// Maximum ratio change per second.
    double in_rate = 2.0;
    double out_rate = 2.0;
    double auto_center_rate = 2.0;
};

struct HydroActuatorState
{
    double length_ratio = 1.0;
};

struct HydroActuatorStep
{
    HydroActuatorState state;
    double target_ratio = 1.0;
    bool valid = false;
    bool input_was_clamped = false;
};

namespace HydroActuatorDetail {

inline bool IsFinite(double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t),
        "hydro response requires a binary64 double");
    static_assert(std::numeric_limits<double>::is_iec559,
        "hydro response requires IEC 60559 doubles");

    std::uint64_t bits = 0U;
    const volatile unsigned char* const source_bytes =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* const destination_bytes =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t byte_index = 0U;
         byte_index < sizeof(bits);
         ++byte_index)
    {
        destination_bytes[byte_index] = source_bytes[byte_index];
    }
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

inline double Clamp(double value, double minimum, double maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

inline bool IsValidConfig(const HydroActuatorConfig& config)
{
    if (!IsFinite(config.input_center) ||
        !IsFinite(config.input_in_limit) ||
        !IsFinite(config.input_out_limit) ||
        !IsFinite(config.in_rate) ||
        !IsFinite(config.out_rate) ||
        !IsFinite(config.auto_center_rate))
    {
        return false;
    }

    if (!(config.input_in_limit < config.input_center) ||
        !(config.input_center < config.input_out_limit) ||
        !(config.in_rate >= 0.0) ||
        !(config.out_rate >= 0.0) ||
        !(config.auto_center_rate >= 0.0))
    {
        return false;
    }

    if (config.has_factor)
    {
        if (!IsFinite(config.factor))
        {
            return false;
        }
        const double endpoint_a = 1.0 - config.factor;
        const double endpoint_b = 1.0 + config.factor;
        return IsFinite(endpoint_a) && IsFinite(endpoint_b) &&
            endpoint_a >= 0.0 && endpoint_b >= 0.0 &&
            (endpoint_a > 0.0 || endpoint_b > 0.0);
    }

    return IsFinite(config.in_limit) &&
        IsFinite(config.out_limit) &&
        IsFinite(config.input_factor) &&
        config.in_limit >= 0.0 &&
        config.in_limit <= 1.0 &&
        config.out_limit >= 1.0;
}

/// Return value's position in the inclusive finite interval [start, end].
/// Scaling opposite-sign endpoints by one half avoids overflowing end-start;
/// same-sign subtraction cannot overflow.
inline bool UnitIntervalFraction(
    double value,
    double start,
    double end,
    double* fraction)
{
    if (fraction == nullptr ||
        !IsFinite(value) ||
        !IsFinite(start) ||
        !IsFinite(end) ||
        !(start < end) ||
        value < start ||
        value > end)
    {
        return false;
    }

    double numerator = 0.0;
    double denominator = 0.0;
    if (start < 0.0 && end > 0.0)
    {
        // Volatile stores make the two bounded products observable before the
        // subtraction. GCC fast-math must not reassociate these expressions
        // back into (value - start) * 0.5 or (end - start) * 0.5, because the
        // intermediate subtraction can overflow for finite binary64 endpoints.
        const volatile double scaled_value = value * 0.5;
        const volatile double scaled_start = start * 0.5;
        const volatile double scaled_end = end * 0.5;
        numerator = scaled_value - scaled_start;
        denominator = scaled_end - scaled_start;
    }
    else
    {
        numerator = value - start;
        denominator = end - start;
    }
    if (!IsFinite(numerator) ||
        !IsFinite(denominator) ||
        !(denominator > 0.0))
    {
        return false;
    }

    const double resolved = numerator / denominator;
    if (!IsFinite(resolved) || resolved < 0.0 || resolved > 1.0)
    {
        return false;
    }
    *fraction = resolved;
    return true;
}

} // namespace HydroActuatorDetail

/// Resolve the requested length ratio without changing actuator state.
inline HydroActuatorStep ResolveHydroActuatorTarget(
    const HydroActuatorConfig& config,
    double input)
{
    HydroActuatorStep result;
    if (!HydroActuatorDetail::IsValidConfig(config) ||
        !HydroActuatorDetail::IsFinite(input))
    {
        return result;
    }

    const double clamped_input = HydroActuatorDetail::Clamp(
        input, config.input_in_limit, config.input_out_limit);
    result.input_was_clamped = clamped_input != input;

    double normalized = 0.0;
    if (clamped_input < config.input_center)
    {
        double fraction = 0.0;
        if (!HydroActuatorDetail::UnitIntervalFraction(
                clamped_input,
                config.input_in_limit,
                config.input_center,
                &fraction))
        {
            return result;
        }
        normalized = fraction - 1.0;
    }
    else if (clamped_input > config.input_center)
    {
        if (!HydroActuatorDetail::UnitIntervalFraction(
                clamped_input,
                config.input_center,
                config.input_out_limit,
                &normalized))
        {
            return result;
        }
    }
    if (!HydroActuatorDetail::IsFinite(normalized) ||
        normalized < -1.0 ||
        normalized > 1.0)
    {
        return result;
    }

    double target = 1.0;
    if (config.has_factor)
    {
        target += config.factor * normalized;
    }
    else
    {
        const double scaled = normalized * config.input_factor;
        double driven = scaled;
        if (!HydroActuatorDetail::IsFinite(scaled))
        {
            // Both operands are finite. Their only non-finite product is an
            // overflow whose sign still determines the saturated command.
            driven =
                (normalized < 0.0) != (config.input_factor < 0.0)
                ? -1.0
                : 1.0;
        }
        else
        {
            driven = HydroActuatorDetail::Clamp(scaled, -1.0, 1.0);
        }
        if (!HydroActuatorDetail::IsFinite(driven))
        {
            return result;
        }
        if (driven < 0.0)
        {
            target += (1.0 - config.in_limit) * driven;
        }
        else
        {
            target += (config.out_limit - 1.0) * driven;
        }
        target = HydroActuatorDetail::Clamp(
            target, config.in_limit, config.out_limit);
    }

    if (!HydroActuatorDetail::IsFinite(target) || !(target > 0.0))
    {
        return HydroActuatorStep();
    }

    result.target_ratio = target;
    result.state.length_ratio = target;
    result.valid = true;
    return result;
}

/// Advance a ratio-bounded actuator. When auto_center is true, the input is
/// deliberately ignored and the target is the initial-length ratio.
inline HydroActuatorStep AdvanceHydroActuator(
    const HydroActuatorConfig& config,
    const HydroActuatorState& previous,
    double input,
    double dt,
    bool auto_center)
{
    HydroActuatorStep result;
    if (!HydroActuatorDetail::IsFinite(previous.length_ratio) ||
        !(previous.length_ratio > 0.0) ||
        !HydroActuatorDetail::IsFinite(dt) ||
        !(dt > 0.0))
    {
        return result;
    }

    HydroActuatorStep target = ResolveHydroActuatorTarget(
        config, auto_center ? config.input_center : input);
    if (!target.valid)
    {
        return result;
    }

    const double rate = auto_center
        ? config.auto_center_rate
        : (target.target_ratio < previous.length_ratio
            ? config.in_rate : config.out_rate);
    const double signed_remaining =
        target.target_ratio - previous.length_ratio;
    const double remaining = signed_remaining < 0.0
        ? -signed_remaining
        : signed_remaining;
    if (!HydroActuatorDetail::IsFinite(remaining))
    {
        return result;
    }

    double next = target.target_ratio;
    if (remaining > 0.0 && !(rate > 0.0))
    {
        next = previous.length_ratio;
    }
    else if (remaining > 0.0)
    {
        const double required_time = remaining / rate;
        const bool can_reach =
            HydroActuatorDetail::IsFinite(required_time) &&
            dt >= required_time;
        if (!can_reach)
        {
            const double maximum_delta = rate * dt;
            if (!HydroActuatorDetail::IsFinite(maximum_delta) ||
                maximum_delta < 0.0 ||
                maximum_delta > remaining)
            {
                return result;
            }
            next = signed_remaining < 0.0
                ? previous.length_ratio - maximum_delta
                : previous.length_ratio + maximum_delta;
        }
    }
    if (!HydroActuatorDetail::IsFinite(next) || !(next > 0.0))
    {
        return result;
    }

    result.state.length_ratio = next;
    result.target_ratio = target.target_ratio;
    result.valid = true;
    result.input_was_clamped = target.input_was_clamped;
    return result;
}

/// Convert a valid ratio to a rest length without allowing non-finite state to
/// enter the beam solver.
inline bool ResolveHydroRestLength(
    double initial_length,
    const HydroActuatorState& state,
    double* rest_length)
{
    if (rest_length == nullptr ||
        !HydroActuatorDetail::IsFinite(initial_length) ||
        !(initial_length > std::numeric_limits<double>::min()) ||
        !HydroActuatorDetail::IsFinite(state.length_ratio) ||
        !(state.length_ratio > 0.0))
    {
        return false;
    }

    const double resolved = initial_length * state.length_ratio;
    if (!HydroActuatorDetail::IsFinite(resolved) ||
        !(resolved > std::numeric_limits<double>::min()))
    {
        return false;
    }

    *rest_length = resolved;
    return true;
}

} // namespace RoR
