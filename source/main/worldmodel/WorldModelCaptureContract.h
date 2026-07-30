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
/// @brief Stable record identities and seed domains for world-model capture.

#pragma once

#include <cstdint>
#include <string>

namespace RoR {
namespace WorldModel {

enum class SchemaKind : std::uint32_t
{
    EPISODE_MANIFEST = 1U,
    OBSERVATION = 2U,
    TRANSITION = 3U
};

struct SchemaIdentifier
{
    SchemaKind kind;
    std::uint16_t major_version;
    std::uint16_t minor_version;

    SchemaIdentifier();
    SchemaIdentifier(
        SchemaKind schema_kind,
        std::uint16_t major,
        std::uint16_t minor);
};

bool operator==(
    const SchemaIdentifier& first,
    const SchemaIdentifier& second);
bool operator!=(
    const SchemaIdentifier& first,
    const SchemaIdentifier& second);

SchemaIdentifier EpisodeManifestSchema();
SchemaIdentifier ObservationSchema();
SchemaIdentifier TransitionSchema();
const char* SchemaName(SchemaKind kind);
bool IsSupportedSchema(const SchemaIdentifier& schema);

/// Stable 128-bit episode identity, independent of platform UUID formatting.
struct EpisodeId
{
    std::uint64_t high;
    std::uint64_t low;

    EpisodeId();
    EpisodeId(std::uint64_t high_word, std::uint64_t low_word);
};

bool operator==(const EpisodeId& first, const EpisodeId& second);
bool operator!=(const EpisodeId& first, const EpisodeId& second);
bool IsValidEpisodeId(const EpisodeId& episode);

/// Canonical episode text is exactly 32 lowercase hexadecimal characters:
/// the high word first, then the low word, with no separators or host-order
/// dependency. Invalid input fails without changing the output.
bool FormatEpisodeId(
    const EpisodeId& episode,
    std::string& text);
bool ParseEpisodeId(
    const std::string& text,
    EpisodeId& episode);

struct ObservationId
{
    EpisodeId episode;
    std::uint64_t observation_index;
    std::uint64_t completed_physics_steps;

    ObservationId();
};

bool operator==(
    const ObservationId& first,
    const ObservationId& second);
bool operator!=(
    const ObservationId& first,
    const ObservationId& second);

/// Source n and target n+1 cover the half-open physics interval
/// [source.completed_physics_steps, target.completed_physics_steps).
struct TransitionId
{
    ObservationId source;
    ObservationId target;

    TransitionId();
};

bool operator==(
    const TransitionId& first,
    const TransitionId& second);
bool operator!=(
    const TransitionId& first,
    const TransitionId& second);

bool MakeObservationId(
    const EpisodeId& episode,
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t observation_index,
    ObservationId& observation);

bool MakeTransitionId(
    const EpisodeId& episode,
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t transition_index,
    TransitionId& transition);

bool IsValidTransitionId(const TransitionId& transition);

/// A new random consumer gets a new domain constant; unrelated semantics
/// cannot share a domain without revising the capture schema.
enum class SeedDomain : std::uint64_t
{
    SIMULATION = UINT64_C(0x73696d756c617469),
    RESET = UINT64_C(0x7265736574000001),
    OBSERVATION = UINT64_C(0x6f62736572766174),
    TRANSITION = UINT64_C(0x7472616e73697469),
    SENSOR = UINT64_C(0x73656e736f720001),
    AUGMENTATION = UINT64_C(0x6175676d656e7401)
};

/// No wall clock, process state, std::hash, or host byte order is consulted.
std::uint64_t DeriveSeed(
    std::uint64_t root_seed,
    SeedDomain domain,
    const EpisodeId& episode,
    std::uint64_t sequence_index,
    std::uint64_t stream_id = 0U);

EpisodeId DeriveEpisodeId(
    std::uint64_t root_seed,
    std::uint64_t episode_ordinal);

std::uint64_t DeriveObservationSeed(
    std::uint64_t root_seed,
    const ObservationId& observation,
    std::uint64_t sensor_stream_id);

std::uint64_t DeriveTransitionSeed(
    std::uint64_t root_seed,
    const TransitionId& transition,
    std::uint64_t stream_id);

} // namespace WorldModel
} // namespace RoR
