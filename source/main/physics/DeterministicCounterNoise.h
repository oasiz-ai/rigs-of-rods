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
/// @brief Dependency-free counter noise for deterministic simulation effects.

#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

#if defined(_MSC_VER)
#   define ROR_COUNTER_NOISE_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#   define ROR_COUNTER_NOISE_FORCE_INLINE inline __attribute__((always_inline))
#else
#   define ROR_COUNTER_NOISE_FORCE_INLINE inline
#endif

namespace RoR {
namespace DeterministicCounterNoise {

static_assert(sizeof(float) == sizeof(std::uint32_t), "32-bit IEEE-754 float required");
static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float required");

/// Default seed for deterministic local simulation. The resolved per-actor seed
/// is persisted in savegames so actor IDs do not need to survive a reload.
static const std::uint64_t DEFAULT_WORLD_SEED = UINT64_C(0x00000000d0125eed);

/// Domain salts keep independent simulation effects from sharing samples.
static const std::uint64_t DOMAIN_TURBULENT_DRAG = UINT64_C(0x54555242554c454e);
static const std::uint64_t DOMAIN_ENGINE_ANTILAG = UINT64_C(0x414e54494c414721);

/// SplitMix64 finalizer. All overflow is deliberately fixed-width unsigned
/// arithmetic, so the result is defined and identical on every C++11 target.
ROR_COUNTER_NOISE_FORCE_INLINE std::uint64_t Mix64(std::uint64_t value)
{
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

/// Resolves a stream seed from an explicit actor stream ID. Callers that need
/// repeatability across fresh scenes must preserve the same spawn-to-ID
/// assignment. Savegames store the resolved value because runtime actor IDs may
/// differ when a scene is restored.
ROR_COUNTER_NOISE_FORCE_INLINE std::uint64_t MakeActorSeed(
    std::uint64_t actor_stream_id,
    std::uint64_t world_seed = DEFAULT_WORLD_SEED)
{
    return Mix64(
        world_seed ^
        actor_stream_id * UINT64_C(0xd1342543de82ef95));
}

/// Returns the integer sample for one immutable simulation key.
///
/// `step` is an effect-specific fixed counter, `element` is a stable node or
/// turbo index, and `lane` selects a component such as X/Y/Z. Sampling has no
/// mutable state: actor traversal, task completion, and argument evaluation
/// order cannot affect the result.
ROR_COUNTER_NOISE_FORCE_INLINE std::uint32_t Hash(
    std::uint64_t actor_seed,
    std::uint64_t step,
    std::uint64_t domain,
    std::uint64_t element,
    std::uint32_t lane)
{
    std::uint64_t key = actor_seed ^ domain;
    key += step * UINT64_C(0xd1342543de82ef95);
    key ^= element * UINT64_C(0x9e3779b185ebca87);
    key += static_cast<std::uint64_t>(lane) *
        UINT64_C(0xc2b2ae3d27d4eb4f);
    return static_cast<std::uint32_t>(Mix64(key) >> 32);
}

/// Reconstructs the legacy random lattice [2, 4) without pointer aliasing.
ROR_COUNTER_NOISE_FORCE_INLINE float LegacyRangeTwoFromHash(std::uint32_t hash)
{
    const std::uint32_t bits =
        UINT32_C(0x40000000) | (hash & UINT32_C(0x007fffff));
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/// Returns an exact IEEE-754 lattice sample in [-1, 1).
ROR_COUNTER_NOISE_FORCE_INLINE float SignedSample(
    std::uint64_t actor_seed,
    std::uint64_t step,
    std::uint64_t domain,
    std::uint64_t element,
    std::uint32_t lane)
{
    return LegacyRangeTwoFromHash(
        Hash(actor_seed, step, domain, element, lane)) - 3.0f;
}

/// Returns an exact IEEE-754 lattice sample in [0, 1).
ROR_COUNTER_NOISE_FORCE_INLINE float UnitSample(
    std::uint64_t actor_seed,
    std::uint64_t step,
    std::uint64_t domain,
    std::uint64_t element,
    std::uint32_t lane)
{
    return (
        LegacyRangeTwoFromHash(
            Hash(actor_seed, step, domain, element, lane)) -
        2.0f) * 0.5f;
}

} // namespace DeterministicCounterNoise
} // namespace RoR

#undef ROR_COUNTER_NOISE_FORCE_INLINE
