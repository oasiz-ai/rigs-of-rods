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
/// @brief Dependency-free policy for deterministic runtime scene scheduling.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace RoR {
namespace DeterministicScenarioSchedule {

/// Matches the normal main-loop ceiling of 1/20 s at the 2 kHz physics rate.
static const std::uint32_t MAX_FIXED_STEPS_PER_FRAME = 100U;

/// Resolve the opt-in fixed number of physics steps launched by each rendered
/// frame. Zero preserves normal wall-clock scheduling. Failure leaves
/// `resolved_steps` unchanged.
inline bool TryResolveFixedStepsPerFrame(
    int configured_steps,
    std::uint32_t& resolved_steps)
{
    if (configured_steps < 0 ||
        configured_steps >
            static_cast<int>(MAX_FIXED_STEPS_PER_FRAME))
    {
        return false;
    }

    resolved_steps = static_cast<std::uint32_t>(configured_steps);
    return true;
}

/// Parse the state-trace step limit without locale, signs, whitespace, or
/// overflow. Authored zero means the supplied immutable maximum. Failure
/// leaves `resolved_limit` unchanged.
inline bool TryParseTraceStepLimit(
    const std::string& text,
    std::uint64_t maximum,
    std::uint64_t& resolved_limit)
{
    if (text.empty() || maximum == 0U)
        return false;

    std::uint64_t parsed = 0U;
    const std::uint64_t integer_maximum =
        std::numeric_limits<std::uint64_t>::max();
    for (std::string::const_iterator iterator = text.begin();
            iterator != text.end();
            ++iterator)
    {
        if (*iterator < '0' || *iterator > '9')
            return false;

        const std::uint64_t digit =
            static_cast<std::uint64_t>(*iterator - '0');
        if (parsed > (integer_maximum - digit) / UINT64_C(10))
            return false;
        parsed = parsed * UINT64_C(10) + digit;
    }

    if (parsed == 0U)
        parsed = maximum;
    if (parsed > maximum)
        return false;

    resolved_limit = parsed;
    return true;
}

} // namespace DeterministicScenarioSchedule
} // namespace RoR
