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

#include "WorldModelCaptureContract.h"

#include "ObservationScheduler.h"

#include <limits>

namespace {

const char EPISODE_SCHEMA_NAME[] =
    "org.rigsofrods.worldmodel.episode-manifest";
const char OBSERVATION_SCHEMA_NAME[] =
    "org.rigsofrods.worldmodel.observation";
const char TRANSITION_SCHEMA_NAME[] =
    "org.rigsofrods.worldmodel.transition";
const char UNKNOWN_SCHEMA_NAME[] =
    "org.rigsofrods.worldmodel.unknown";

const std::uint64_t CAPTURE_SEED_DOMAIN =
    UINT64_C(0x524f522d574d2d31); // "ROR-WM-1"
const std::uint64_t EPISODE_HIGH_DOMAIN =
    UINT64_C(0x657069736f646548);
const std::uint64_t EPISODE_LOW_DOMAIN =
    UINT64_C(0x657069736f64654c);

std::uint64_t Mix64(std::uint64_t value)
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return value;
}

std::uint64_t Absorb(
    std::uint64_t state,
    std::uint64_t value)
{
    return Mix64(
        state ^
        Mix64(value + UINT64_C(0x9e3779b97f4a7c15)));
}

std::uint64_t DeriveWords(
    std::uint64_t root_seed,
    std::uint64_t domain,
    std::uint64_t first,
    std::uint64_t second,
    std::uint64_t third,
    std::uint64_t fourth)
{
    std::uint64_t state = Mix64(root_seed ^ CAPTURE_SEED_DOMAIN);
    state = Absorb(state, domain);
    state = Absorb(state, first);
    state = Absorb(state, second);
    state = Absorb(state, third);
    state = Absorb(state, fourth);
    return state;
}

} // namespace

namespace RoR {
namespace WorldModel {

SchemaIdentifier::SchemaIdentifier():
    kind(SchemaKind::EPISODE_MANIFEST),
    major_version(0U),
    minor_version(0U)
{
}

SchemaIdentifier::SchemaIdentifier(
    SchemaKind schema_kind,
    std::uint16_t major,
    std::uint16_t minor):
    kind(schema_kind),
    major_version(major),
    minor_version(minor)
{
}

bool operator==(
    const SchemaIdentifier& first,
    const SchemaIdentifier& second)
{
    return first.kind == second.kind &&
        first.major_version == second.major_version &&
        first.minor_version == second.minor_version;
}

bool operator!=(
    const SchemaIdentifier& first,
    const SchemaIdentifier& second)
{
    return !(first == second);
}

SchemaIdentifier EpisodeManifestSchema()
{
    return SchemaIdentifier(
        SchemaKind::EPISODE_MANIFEST, 1U, 0U);
}

SchemaIdentifier ObservationSchema()
{
    return SchemaIdentifier(SchemaKind::OBSERVATION, 1U, 0U);
}

SchemaIdentifier TransitionSchema()
{
    return SchemaIdentifier(SchemaKind::TRANSITION, 1U, 0U);
}

const char* SchemaName(SchemaKind kind)
{
    switch (kind)
    {
    case SchemaKind::EPISODE_MANIFEST:
        return EPISODE_SCHEMA_NAME;
    case SchemaKind::OBSERVATION:
        return OBSERVATION_SCHEMA_NAME;
    case SchemaKind::TRANSITION:
        return TRANSITION_SCHEMA_NAME;
    }
    return UNKNOWN_SCHEMA_NAME;
}

bool IsSupportedSchema(const SchemaIdentifier& schema)
{
    switch (schema.kind)
    {
    case SchemaKind::EPISODE_MANIFEST:
        return schema == EpisodeManifestSchema();
    case SchemaKind::OBSERVATION:
        return schema == ObservationSchema();
    case SchemaKind::TRANSITION:
        return schema == TransitionSchema();
    }
    return false;
}

EpisodeId::EpisodeId(): high(0U), low(0U)
{
}

EpisodeId::EpisodeId(
    std::uint64_t high_word,
    std::uint64_t low_word):
    high(high_word),
    low(low_word)
{
}

bool operator==(const EpisodeId& first, const EpisodeId& second)
{
    return first.high == second.high && first.low == second.low;
}

bool operator!=(const EpisodeId& first, const EpisodeId& second)
{
    return !(first == second);
}

bool IsValidEpisodeId(const EpisodeId& episode)
{
    return episode.high != 0U || episode.low != 0U;
}

bool FormatEpisodeId(
    const EpisodeId& episode,
    std::string& text)
{
    if (!IsValidEpisodeId(episode))
        return false;

    const char digits[] = "0123456789abcdef";
    std::string candidate(32U, '0');
    const std::uint64_t words[] = {episode.high, episode.low};
    for (std::size_t word = 0U; word < 2U; ++word)
    {
        for (std::size_t digit = 0U; digit < 16U; ++digit)
        {
            const unsigned int shift =
                static_cast<unsigned int>((15U - digit) * 4U);
            candidate[word * 16U + digit] =
                digits[(words[word] >> shift) & 0xfU];
        }
    }
    text = candidate;
    return true;
}

bool ParseEpisodeId(
    const std::string& text,
    EpisodeId& episode)
{
    if (text.size() != 32U)
        return false;

    std::uint64_t words[] = {0U, 0U};
    for (std::size_t index = 0U; index < text.size(); ++index)
    {
        const char character = text[index];
        std::uint64_t nibble = 0U;
        if (character >= '0' && character <= '9')
            nibble = static_cast<std::uint64_t>(character - '0');
        else if (character >= 'a' && character <= 'f')
            nibble = static_cast<std::uint64_t>(
                character - 'a' + 10);
        else
            return false;

        const std::size_t word = index / 16U;
        words[word] = (words[word] << 4U) | nibble;
    }

    const EpisodeId candidate(words[0], words[1]);
    if (!IsValidEpisodeId(candidate))
        return false;
    episode = candidate;
    return true;
}

ObservationId::ObservationId():
    episode(),
    observation_index(0U),
    completed_physics_steps(0U)
{
}

bool operator==(
    const ObservationId& first,
    const ObservationId& second)
{
    return first.episode == second.episode &&
        first.observation_index == second.observation_index &&
        first.completed_physics_steps ==
            second.completed_physics_steps;
}

bool operator!=(
    const ObservationId& first,
    const ObservationId& second)
{
    return !(first == second);
}

TransitionId::TransitionId(): source(), target()
{
}

bool operator==(
    const TransitionId& first,
    const TransitionId& second)
{
    return first.source == second.source &&
        first.target == second.target;
}

bool operator!=(
    const TransitionId& first,
    const TransitionId& second)
{
    return !(first == second);
}

bool MakeObservationId(
    const EpisodeId& episode,
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t observation_index,
    ObservationId& observation)
{
    if (!IsValidEpisodeId(episode))
        return false;

    ObservationBoundary boundary;
    if (!TryObservationBoundary(
            origin_completed_physics_steps,
            observation_index,
            boundary))
    {
        return false;
    }
    ObservationId candidate;
    candidate.episode = episode;
    candidate.observation_index = boundary.observation_index;
    candidate.completed_physics_steps =
        boundary.completed_physics_steps;
    observation = candidate;
    return true;
}

bool MakeTransitionId(
    const EpisodeId& episode,
    std::uint64_t origin_completed_physics_steps,
    std::uint64_t transition_index,
    TransitionId& transition)
{
    if (transition_index ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    TransitionId candidate;
    if (!MakeObservationId(
            episode,
            origin_completed_physics_steps,
            transition_index,
            candidate.source) ||
        !MakeObservationId(
            episode,
            origin_completed_physics_steps,
            transition_index + 1U,
            candidate.target))
    {
        return false;
    }
    transition = candidate;
    return true;
}

bool IsValidTransitionId(const TransitionId& transition)
{
    if (!IsValidEpisodeId(transition.source.episode) ||
        transition.source.episode != transition.target.episode ||
        transition.source.observation_index ==
            std::numeric_limits<std::uint64_t>::max() ||
        transition.target.observation_index !=
            transition.source.observation_index + 1U ||
        transition.target.completed_physics_steps <
            transition.source.completed_physics_steps)
    {
        return false;
    }
    const std::uint64_t step_count =
        transition.target.completed_physics_steps -
        transition.source.completed_physics_steps;
    return step_count == TransitionPhysicsStepCount(
        transition.source.observation_index);
}

std::uint64_t DeriveSeed(
    std::uint64_t root_seed,
    SeedDomain domain,
    const EpisodeId& episode,
    std::uint64_t sequence_index,
    std::uint64_t stream_id)
{
    return DeriveWords(
        root_seed,
        static_cast<std::uint64_t>(domain),
        episode.high,
        episode.low,
        sequence_index,
        stream_id);
}

EpisodeId DeriveEpisodeId(
    std::uint64_t root_seed,
    std::uint64_t episode_ordinal)
{
    EpisodeId episode(
        DeriveWords(
            root_seed, EPISODE_HIGH_DOMAIN,
            episode_ordinal, 0U, 0U, 0U),
        DeriveWords(
            root_seed, EPISODE_LOW_DOMAIN,
            episode_ordinal, 0U, 0U, 0U));
    if (!IsValidEpisodeId(episode))
        episode.low = 1U;
    return episode;
}

std::uint64_t DeriveObservationSeed(
    std::uint64_t root_seed,
    const ObservationId& observation,
    std::uint64_t sensor_stream_id)
{
    const std::uint64_t seed = DeriveSeed(
        root_seed,
        SeedDomain::OBSERVATION,
        observation.episode,
        observation.observation_index,
        sensor_stream_id);
    return Absorb(seed, observation.completed_physics_steps);
}

std::uint64_t DeriveTransitionSeed(
    std::uint64_t root_seed,
    const TransitionId& transition,
    std::uint64_t stream_id)
{
    std::uint64_t seed = DeriveSeed(
        root_seed,
        SeedDomain::TRANSITION,
        transition.source.episode,
        transition.source.observation_index,
        stream_id);
    seed = Absorb(
        seed,
        transition.source.completed_physics_steps);
    return Absorb(
        seed,
        transition.target.completed_physics_steps);
}

} // namespace WorldModel
} // namespace RoR
