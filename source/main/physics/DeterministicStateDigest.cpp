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

#include "DeterministicStateDigest.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <vector>

namespace {

std::uint32_t RotateRight(std::uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

std::uint32_t ExactBinary32Bits(const float& value)
{
    // A volatile character read forces inspection of the object
    // representation. Some fast-math optimizers otherwise replace a
    // memcpy-based exponent check with an isfinite-style floating comparison
    // and assume that NaN/infinity cannot exist.
    unsigned char representation[sizeof(value)];
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
        representation[index] = source[index];

    std::uint32_t bits = 0;
    std::memcpy(&bits, representation, sizeof(bits));
    return bits;
}

bool IsFiniteBinary32(const float& value)
{
    const std::uint32_t bits = ExactBinary32Bits(value);
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

std::uint64_t ExactBinary64Bits(const double& value)
{
    unsigned char representation[sizeof(value)];
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
        representation[index] = source[index];

    std::uint64_t bits = 0;
    std::memcpy(&bits, representation, sizeof(bits));
    return bits;
}

bool IsFiniteBinary64(const double& value)
{
    const std::uint64_t bits = ExactBinary64Bits(value);
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool ContactLess(
    const RoR::DeterministicStateDigest::ContactRecord& left,
    const RoR::DeterministicStateDigest::ContactRecord& right)
{
    if (left.surface_actor != right.surface_actor)
        return left.surface_actor < right.surface_actor;
    if (left.surface_contact != right.surface_contact)
        return left.surface_contact < right.surface_contact;
    if (left.hit_actor != right.hit_actor)
        return left.hit_actor < right.hit_actor;
    return left.hit_node < right.hit_node;
}

struct OrderedSnapshotActor
{
    std::size_t source_index;
    RoR::DeterministicStateDigest::SnapshotActor snapshot;
};

bool OrderedSnapshotActorLess(
    const OrderedSnapshotActor& left,
    const OrderedSnapshotActor& right)
{
    return left.snapshot.actor.actor_id <
        right.snapshot.actor.actor_id;
}

struct OrderedSnapshotContact
{
    std::size_t source_index;
    RoR::DeterministicStateDigest::ContactRecord contact;
};

bool OrderedSnapshotContactLess(
    const OrderedSnapshotContact& left,
    const OrderedSnapshotContact& right)
{
    return ContactLess(left.contact, right.contact);
}

const std::uint32_t SHA256_CONSTANTS[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491),
    UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
    UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
    UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
    UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
    UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
    UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
    UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624),
    UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
    UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
    UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
    UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static_assert(sizeof(float) == sizeof(std::uint32_t),
    "deterministic state digests require binary32 float storage");
static_assert(std::numeric_limits<float>::is_iec559,
    "deterministic state digests require IEEE-754 floats");
static_assert(std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24,
    "deterministic state digests require IEEE-754 binary32 floats");
static_assert(sizeof(double) == sizeof(std::uint64_t),
    "deterministic state digests require binary64 double storage");
static_assert(std::numeric_limits<double>::is_iec559,
    "deterministic state digests require IEEE-754 doubles");
static_assert(std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53,
    "deterministic state digests require IEEE-754 binary64 doubles");

} // namespace

namespace RoR {
namespace DeterministicStateDigest {

Digest::Digest(): bytes() {}

std::string Digest::ToHex() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::uint8_t byte : bytes)
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    return stream.str();
}

bool Digest::operator==(const Digest& other) const
{
    return bytes == other.bytes;
}

bool Digest::operator!=(const Digest& other) const
{
    return !(*this == other);
}

ActorRecord::ActorRecord():
    actor_id(0),
    state(0),
    flags(0),
    deterministic_seed(0),
    actor_physics_step(0),
    engine_update_step(0),
    origin()
{
}

NodeRecord::NodeRecord():
    actor_id(0),
    node_id(0),
    position(),
    velocity()
{
}

BeamRecord::BeamRecord():
    actor_id(0),
    beam_id(0),
    rest_length(0.f),
    stress(0.f),
    material_schema_version(0),
    plastic_strain(0.0),
    accumulated_plastic_strain(0.0),
    damage(0.0),
    damage_driver_density(0.0),
    last_total_strain(0.0),
    material_runtime_error(BEAM_MATERIAL_RUNTIME_ERROR_NONE),
    material_error(BEAM_MATERIAL_ERROR_NONE),
    state_flags(0)
{
}

ContactRecord::ContactRecord():
    surface_actor(0),
    surface_contact(0),
    hit_actor(0),
    hit_node(0)
{
}

SnapshotActor::SnapshotActor():
    actor(),
    node_count(0),
    beam_count(0),
    surface_contact_count(0)
{
}

SnapshotSource::~SnapshotSource()
{
}

SnapshotStatus::SnapshotStatus():
    error(SnapshotError::NONE),
    digest_error(Error::NONE),
    source_index(std::numeric_limits<std::size_t>::max()),
    record_index(0)
{
}

Builder::Builder(std::uint64_t physics_step, std::uint64_t scenario_id):
    m_section(Section::INITIAL),
    m_error(Error::NONE),
    m_error_record_index(0),
    m_expected_count(0),
    m_observed_count(0),
    m_previous_actor_id(0),
    m_previous_node_actor_id(0),
    m_previous_node_id(0),
    m_previous_beam_actor_id(0),
    m_previous_beam_id(0),
    m_previous_contact(),
    m_has_previous_key(false),
    m_sha_state(),
    m_sha_buffer(),
    m_sha_byte_count(0),
    m_sha_buffer_size(0)
{
    m_sha_state[0] = UINT32_C(0x6a09e667);
    m_sha_state[1] = UINT32_C(0xbb67ae85);
    m_sha_state[2] = UINT32_C(0x3c6ef372);
    m_sha_state[3] = UINT32_C(0xa54ff53a);
    m_sha_state[4] = UINT32_C(0x510e527f);
    m_sha_state[5] = UINT32_C(0x9b05688c);
    m_sha_state[6] = UINT32_C(0x1f83d9ab);
    m_sha_state[7] = UINT32_C(0x5be0cd19);

    static const std::uint8_t magic[] = {
        'R', 'o', 'R', '-', 'D', '0', '-', 'S', 't', 'a', 't', 'e'
    };
    HashBytes(magic, sizeof(magic));
    HashU32(SCHEMA_VERSION);
    HashU64(physics_step);
    HashU64(scenario_id);
}

bool Builder::RequireWritable()
{
    if (m_error != Error::NONE)
        return false;
    if (m_section == Section::FINISHED)
        return Fail(Error::ALREADY_FINISHED, m_observed_count);
    return true;
}

bool Builder::Fail(Error error, std::uint32_t record_index)
{
    if (m_error == Error::NONE)
    {
        m_error = error;
        m_error_record_index = record_index;
    }
    return false;
}

bool Builder::CompleteCurrentSection() const
{
    return m_observed_count == m_expected_count;
}

bool Builder::BeginSection(
    Section required_previous,
    Section next,
    std::uint32_t count,
    std::uint32_t limit,
    std::uint8_t tag)
{
    if (!RequireWritable())
        return false;
    if (m_section != required_previous)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (required_previous != Section::INITIAL && !CompleteCurrentSection())
        return Fail(Error::COUNT_MISMATCH, m_observed_count);
    if (count > limit)
        return Fail(Error::COUNT_LIMIT_EXCEEDED, count);

    m_section = next;
    m_expected_count = count;
    m_observed_count = 0;
    m_has_previous_key = false;
    HashByte(tag);
    HashU32(count);
    return true;
}

bool Builder::BeginActors(std::uint32_t count)
{
    return BeginSection(
        Section::INITIAL,
        Section::ACTORS,
        count,
        MAX_ACTORS,
        UINT8_C(0xa1));
}

bool Builder::RequireFinite(
    const float& value,
    std::uint32_t record_index)
{
    if (!IsFiniteBinary32(value))
        return Fail(Error::NON_FINITE_VALUE, record_index);
    return true;
}

bool Builder::RequireFinite(
    const double& value,
    std::uint32_t record_index)
{
    if (!IsFiniteBinary64(value))
        return Fail(Error::NON_FINITE_VALUE, record_index);
    return true;
}

bool Builder::AddActor(const ActorRecord& record)
{
    if (!RequireWritable())
        return false;
    if (m_section != Section::ACTORS)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (m_observed_count >= m_expected_count)
        return Fail(Error::COUNT_MISMATCH, m_observed_count);
    if (record.actor_id < 0 ||
        record.state > ACTOR_STATE_DISPOSED ||
        (record.flags & ~ACTOR_FLAG_MASK) != 0)
        return Fail(Error::INVALID_RECORD, m_observed_count);
    if (m_has_previous_key && record.actor_id <= m_previous_actor_id)
        return Fail(Error::NON_CANONICAL_KEY, m_observed_count);
    for (const float& value : record.origin)
    {
        if (!RequireFinite(value, m_observed_count))
            return false;
    }

    HashI32(record.actor_id);
    HashU32(record.state);
    HashU32(record.flags);
    HashU64(record.deterministic_seed);
    HashU64(record.actor_physics_step);
    HashU64(record.engine_update_step);
    for (const float& value : record.origin)
        HashFloat(value);

    m_previous_actor_id = record.actor_id;
    m_has_previous_key = true;
    ++m_observed_count;
    return true;
}

bool Builder::BeginNodes(std::uint32_t count)
{
    return BeginSection(
        Section::ACTORS,
        Section::NODES,
        count,
        MAX_NODES,
        UINT8_C(0xa2));
}

bool Builder::AddNode(const NodeRecord& record)
{
    if (!RequireWritable())
        return false;
    if (m_section != Section::NODES)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (m_observed_count >= m_expected_count)
        return Fail(Error::COUNT_MISMATCH, m_observed_count);
    if (record.actor_id < 0)
        return Fail(Error::INVALID_RECORD, m_observed_count);
    if (m_has_previous_key &&
            (record.actor_id < m_previous_node_actor_id ||
             (record.actor_id == m_previous_node_actor_id &&
              record.node_id <= m_previous_node_id)))
    {
        return Fail(Error::NON_CANONICAL_KEY, m_observed_count);
    }
    for (const float& value : record.position)
    {
        if (!RequireFinite(value, m_observed_count))
            return false;
    }
    for (const float& value : record.velocity)
    {
        if (!RequireFinite(value, m_observed_count))
            return false;
    }

    HashI32(record.actor_id);
    HashU32(record.node_id);
    for (const float& value : record.position)
        HashFloat(value);
    for (const float& value : record.velocity)
        HashFloat(value);

    m_previous_node_actor_id = record.actor_id;
    m_previous_node_id = record.node_id;
    m_has_previous_key = true;
    ++m_observed_count;
    return true;
}

bool Builder::BeginBeams(std::uint32_t count)
{
    return BeginSection(
        Section::NODES,
        Section::BEAMS,
        count,
        MAX_BEAMS,
        UINT8_C(0xa3));
}

bool Builder::AddBeam(const BeamRecord& record)
{
    if (!RequireWritable())
        return false;
    if (m_section != Section::BEAMS)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (m_observed_count >= m_expected_count)
        return Fail(Error::COUNT_MISMATCH, m_observed_count);
    if (record.actor_id < 0 || record.rest_length <= 0.f ||
            record.accumulated_plastic_strain < 0.0 ||
            record.damage < 0.0 || record.damage > 1.0 ||
            record.damage_driver_density < 0.0)
    {
        return Fail(Error::INVALID_RECORD, m_observed_count);
    }
    if (m_has_previous_key &&
            (record.actor_id < m_previous_beam_actor_id ||
             (record.actor_id == m_previous_beam_actor_id &&
              record.beam_id <= m_previous_beam_id)))
    {
        return Fail(Error::NON_CANONICAL_KEY, m_observed_count);
    }
    if (!RequireFinite(record.rest_length, m_observed_count) ||
            !RequireFinite(record.stress, m_observed_count) ||
            !RequireFinite(record.plastic_strain, m_observed_count) ||
            !RequireFinite(
                record.accumulated_plastic_strain,
                m_observed_count) ||
            !RequireFinite(record.damage, m_observed_count) ||
            !RequireFinite(
                record.damage_driver_density,
                m_observed_count) ||
            !RequireFinite(record.last_total_strain, m_observed_count))
        return false;
    if (record.material_schema_version >
            BEAM_MATERIAL_SCHEMA_CALIBRATED_V1 ||
        record.material_runtime_error >
            BEAM_MATERIAL_RUNTIME_ERROR_FORCE_OUT_OF_RUNTIME_RANGE ||
        record.material_error >
            BEAM_MATERIAL_ERROR_NUMERIC_OVERFLOW ||
        (record.state_flags & ~BEAM_STATE_MASK) != 0)
    {
        return Fail(Error::INVALID_RECORD, m_observed_count);
    }
    if (record.material_schema_version == BEAM_MATERIAL_SCHEMA_NONE &&
        ((record.state_flags &
              (BEAM_STATE_MATERIAL_FRACTURED |
               BEAM_STATE_MATERIAL_FAULTED)) != 0 ||
         ExactBinary64Bits(record.plastic_strain) != 0 ||
         ExactBinary64Bits(record.accumulated_plastic_strain) != 0 ||
         ExactBinary64Bits(record.damage) != 0 ||
         ExactBinary64Bits(record.damage_driver_density) != 0 ||
         ExactBinary64Bits(record.last_total_strain) != 0 ||
         record.material_runtime_error !=
             BEAM_MATERIAL_RUNTIME_ERROR_NONE ||
         record.material_error != BEAM_MATERIAL_ERROR_NONE))
    {
        return Fail(Error::INVALID_RECORD, m_observed_count);
    }
    const bool material_fractured =
        (record.state_flags & BEAM_STATE_MATERIAL_FRACTURED) != 0;
    const bool material_faulted =
        (record.state_flags & BEAM_STATE_MATERIAL_FAULTED) != 0;
    if (material_fractured &&
        (((record.state_flags & BEAM_STATE_DISABLED) == 0) ||
         ((record.state_flags & BEAM_STATE_BROKEN) == 0) ||
         material_faulted))
    {
        return Fail(Error::INVALID_RECORD, m_observed_count);
    }
    if (material_faulted)
    {
        const bool is_material_failure =
            record.material_runtime_error ==
            BEAM_MATERIAL_RUNTIME_ERROR_MATERIAL_FAILURE;
        const bool has_material_error =
            record.material_error != BEAM_MATERIAL_ERROR_NONE;
        if (record.material_schema_version !=
                BEAM_MATERIAL_SCHEMA_CALIBRATED_V1 ||
            (record.state_flags & BEAM_STATE_DISABLED) == 0 ||
            record.material_runtime_error ==
                BEAM_MATERIAL_RUNTIME_ERROR_NONE ||
            is_material_failure != has_material_error)
        {
            return Fail(Error::INVALID_RECORD, m_observed_count);
        }
    }
    else if (record.material_runtime_error !=
                 BEAM_MATERIAL_RUNTIME_ERROR_NONE ||
             record.material_error != BEAM_MATERIAL_ERROR_NONE)
    {
        return Fail(Error::INVALID_RECORD, m_observed_count);
    }

    HashI32(record.actor_id);
    HashU32(record.beam_id);
    HashFloat(record.rest_length);
    HashFloat(record.stress);
    HashU32(record.material_schema_version);
    HashDouble(record.plastic_strain);
    HashDouble(record.accumulated_plastic_strain);
    HashDouble(record.damage);
    HashDouble(record.damage_driver_density);
    HashDouble(record.last_total_strain);
    HashU32(record.material_runtime_error);
    HashU32(record.material_error);
    HashU32(record.state_flags);

    m_previous_beam_actor_id = record.actor_id;
    m_previous_beam_id = record.beam_id;
    m_has_previous_key = true;
    ++m_observed_count;
    return true;
}

bool Builder::BeginContacts(std::uint32_t count)
{
    return BeginSection(
        Section::BEAMS,
        Section::CONTACTS,
        count,
        MAX_CONTACTS,
        UINT8_C(0xa4));
}

bool Builder::AddContact(const ContactRecord& record)
{
    if (!RequireWritable())
        return false;
    if (m_section != Section::CONTACTS)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (m_observed_count >= m_expected_count)
        return Fail(Error::COUNT_MISMATCH, m_observed_count);
    if (record.surface_actor < 0 || record.hit_actor < 0)
        return Fail(Error::INVALID_RECORD, m_observed_count);
    if (m_has_previous_key && !ContactLess(m_previous_contact, record))
        return Fail(Error::NON_CANONICAL_KEY, m_observed_count);

    HashI32(record.surface_actor);
    HashU32(record.surface_contact);
    HashI32(record.hit_actor);
    HashU32(record.hit_node);

    m_previous_contact = record;
    m_has_previous_key = true;
    ++m_observed_count;
    return true;
}

bool Builder::Finish(Digest& digest)
{
    if (!RequireWritable())
        return false;
    if (m_section != Section::CONTACTS)
        return Fail(Error::INVALID_SECTION_ORDER, m_observed_count);
    if (!CompleteCurrentSection())
        return Fail(Error::COUNT_MISMATCH, m_observed_count);

    FinalizeSha256(digest);
    m_section = Section::FINISHED;
    return true;
}

Error Builder::GetError() const
{
    return m_error;
}

std::uint32_t Builder::GetErrorRecordIndex() const
{
    return m_error_record_index;
}

bool BuildSnapshotDigest(
    std::uint64_t physics_step,
    std::uint64_t scenario_id,
    const SnapshotSource& source,
    Digest& digest,
    SnapshotStatus* status)
{
    SnapshotStatus local_status;
    const auto fail =
        [&local_status, status](
            SnapshotError error,
            std::size_t source_index,
            std::uint32_t record_index,
            Error digest_error)
        {
            local_status.error = error;
            local_status.digest_error = digest_error;
            local_status.source_index = source_index;
            local_status.record_index = record_index;
            if (status != nullptr)
                *status = local_status;
            return false;
        };

    const std::size_t actor_count = source.GetActorCount();
    const std::size_t contact_count = source.GetContactCount();
    if (actor_count > MAX_ACTORS || contact_count > MAX_CONTACTS)
    {
        return fail(
            SnapshotError::COUNT_LIMIT_EXCEEDED,
            std::numeric_limits<std::size_t>::max(),
            0,
            Error::NONE);
    }

    std::vector<OrderedSnapshotActor> actors;
    std::vector<OrderedSnapshotContact> contacts;
    try
    {
        actors.reserve(actor_count);
        contacts.reserve(contact_count);
    }
    catch (const std::bad_alloc&)
    {
        return fail(
            SnapshotError::ALLOCATION_FAILED,
            std::numeric_limits<std::size_t>::max(),
            0,
            Error::NONE);
    }

    std::uint32_t total_nodes = 0;
    std::uint32_t total_beams = 0;
    for (std::size_t source_index = 0;
            source_index < actor_count;
            ++source_index)
    {
        OrderedSnapshotActor ordered;
        ordered.source_index = source_index;
        if (!source.ReadActor(source_index, ordered.snapshot))
        {
            return fail(
                SnapshotError::SOURCE_READ_FAILED,
                source_index,
                0,
                Error::NONE);
        }
        if (ordered.snapshot.actor.actor_id <= 0)
        {
            return fail(
                SnapshotError::INVALID_ACTOR_ID,
                source_index,
                0,
                Error::NONE);
        }
        if (ordered.snapshot.node_count > MAX_NODES - total_nodes ||
            ordered.snapshot.beam_count > MAX_BEAMS - total_beams)
        {
            return fail(
                SnapshotError::COUNT_LIMIT_EXCEEDED,
                source_index,
                0,
                Error::NONE);
        }
        total_nodes += ordered.snapshot.node_count;
        total_beams += ordered.snapshot.beam_count;
        try
        {
            actors.push_back(ordered);
        }
        catch (const std::bad_alloc&)
        {
            return fail(
                SnapshotError::ALLOCATION_FAILED,
                source_index,
                0,
                Error::NONE);
        }
    }

    std::sort(
        actors.begin(),
        actors.end(),
        OrderedSnapshotActorLess);
    for (std::size_t index = 1; index < actors.size(); ++index)
    {
        if (actors[index - 1].snapshot.actor.actor_id ==
            actors[index].snapshot.actor.actor_id)
        {
            return fail(
                SnapshotError::DUPLICATE_ACTOR_ID,
                actors[index].source_index,
                0,
                Error::NONE);
        }
    }

    Builder builder(physics_step, scenario_id);
    if (!builder.BeginActors(static_cast<std::uint32_t>(actor_count)))
    {
        return fail(
            SnapshotError::DIGEST_REJECTED,
            std::numeric_limits<std::size_t>::max(),
            builder.GetErrorRecordIndex(),
            builder.GetError());
    }
    for (const OrderedSnapshotActor& ordered : actors)
    {
        if (!builder.AddActor(ordered.snapshot.actor))
        {
            return fail(
                SnapshotError::DIGEST_REJECTED,
                ordered.source_index,
                builder.GetErrorRecordIndex(),
                builder.GetError());
        }
    }

    if (!builder.BeginNodes(total_nodes))
    {
        return fail(
            SnapshotError::DIGEST_REJECTED,
            std::numeric_limits<std::size_t>::max(),
            builder.GetErrorRecordIndex(),
            builder.GetError());
    }
    for (const OrderedSnapshotActor& ordered : actors)
    {
        for (std::uint32_t node_index = 0;
                node_index < ordered.snapshot.node_count;
                ++node_index)
        {
            NodeRecord node;
            if (!source.ReadNode(
                    ordered.source_index,
                    node_index,
                    node))
            {
                return fail(
                    SnapshotError::SOURCE_READ_FAILED,
                    ordered.source_index,
                    node_index,
                    Error::NONE);
            }
            if (node.actor_id !=
                    ordered.snapshot.actor.actor_id ||
                node.node_id != node_index)
            {
                return fail(
                    SnapshotError::INVALID_CROSS_REFERENCE,
                    ordered.source_index,
                    node_index,
                    Error::NONE);
            }
            if (!builder.AddNode(node))
            {
                return fail(
                    SnapshotError::DIGEST_REJECTED,
                    ordered.source_index,
                    builder.GetErrorRecordIndex(),
                    builder.GetError());
            }
        }
    }

    if (!builder.BeginBeams(total_beams))
    {
        return fail(
            SnapshotError::DIGEST_REJECTED,
            std::numeric_limits<std::size_t>::max(),
            builder.GetErrorRecordIndex(),
            builder.GetError());
    }
    for (const OrderedSnapshotActor& ordered : actors)
    {
        for (std::uint32_t beam_index = 0;
                beam_index < ordered.snapshot.beam_count;
                ++beam_index)
        {
            BeamRecord beam;
            if (!source.ReadBeam(
                    ordered.source_index,
                    beam_index,
                    beam))
            {
                return fail(
                    SnapshotError::SOURCE_READ_FAILED,
                    ordered.source_index,
                    beam_index,
                    Error::NONE);
            }
            if (beam.actor_id !=
                    ordered.snapshot.actor.actor_id ||
                beam.beam_id != beam_index)
            {
                return fail(
                    SnapshotError::INVALID_CROSS_REFERENCE,
                    ordered.source_index,
                    beam_index,
                    Error::NONE);
            }
            if (!builder.AddBeam(beam))
            {
                return fail(
                    SnapshotError::DIGEST_REJECTED,
                    ordered.source_index,
                    builder.GetErrorRecordIndex(),
                    builder.GetError());
            }
        }
    }

    for (std::size_t source_index = 0;
            source_index < contact_count;
            ++source_index)
    {
        OrderedSnapshotContact ordered;
        ordered.source_index = source_index;
        if (!source.ReadContact(source_index, ordered.contact))
        {
            return fail(
                SnapshotError::SOURCE_READ_FAILED,
                source_index,
                0,
                Error::NONE);
        }
        try
        {
            contacts.push_back(ordered);
        }
        catch (const std::bad_alloc&)
        {
            return fail(
                SnapshotError::ALLOCATION_FAILED,
                source_index,
                0,
                Error::NONE);
        }
    }
    std::sort(
        contacts.begin(),
        contacts.end(),
        OrderedSnapshotContactLess);

    if (!builder.BeginContacts(
            static_cast<std::uint32_t>(contact_count)))
    {
        return fail(
            SnapshotError::DIGEST_REJECTED,
            std::numeric_limits<std::size_t>::max(),
            builder.GetErrorRecordIndex(),
            builder.GetError());
    }
    for (const OrderedSnapshotContact& ordered : contacts)
    {
        const auto actor_for_id =
            [&actors](std::int32_t actor_id)
                -> const OrderedSnapshotActor*
            {
                const auto found = std::lower_bound(
                    actors.begin(),
                    actors.end(),
                    actor_id,
                    [](const OrderedSnapshotActor& actor,
                       std::int32_t id)
                    {
                        return actor.snapshot.actor.actor_id < id;
                    });
                if (found == actors.end() ||
                    found->snapshot.actor.actor_id != actor_id)
                {
                    return nullptr;
                }
                return &*found;
            };

        const OrderedSnapshotActor* const surface_actor =
            actor_for_id(ordered.contact.surface_actor);
        const OrderedSnapshotActor* const hit_actor =
            actor_for_id(ordered.contact.hit_actor);
        if (surface_actor == nullptr || hit_actor == nullptr ||
            ordered.contact.surface_actor ==
                ordered.contact.hit_actor ||
            ordered.contact.surface_contact >=
                surface_actor->snapshot.surface_contact_count ||
            ordered.contact.hit_node >=
                hit_actor->snapshot.node_count)
        {
            return fail(
                SnapshotError::INVALID_CROSS_REFERENCE,
                ordered.source_index,
                0,
                Error::NONE);
        }
        if (!builder.AddContact(ordered.contact))
        {
            return fail(
                SnapshotError::DIGEST_REJECTED,
                ordered.source_index,
                builder.GetErrorRecordIndex(),
                builder.GetError());
        }
    }

    Digest completed;
    if (!builder.Finish(completed))
    {
        return fail(
            SnapshotError::DIGEST_REJECTED,
            std::numeric_limits<std::size_t>::max(),
            builder.GetErrorRecordIndex(),
            builder.GetError());
    }
    digest = completed;
    if (status != nullptr)
        *status = local_status;
    return true;
}

void Builder::HashByte(std::uint8_t value)
{
    HashBytes(&value, 1);
}

void Builder::HashU32(std::uint32_t value)
{
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)
    };
    HashBytes(bytes, sizeof(bytes));
}

void Builder::HashI32(std::int32_t value)
{
    HashU32(static_cast<std::uint32_t>(value));
}

void Builder::HashU64(std::uint64_t value)
{
    const std::uint8_t bytes[8] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
        static_cast<std::uint8_t>(value >> 32U),
        static_cast<std::uint8_t>(value >> 40U),
        static_cast<std::uint8_t>(value >> 48U),
        static_cast<std::uint8_t>(value >> 56U)
    };
    HashBytes(bytes, sizeof(bytes));
}

void Builder::HashFloat(const float& value)
{
    HashU32(ExactBinary32Bits(value));
}

void Builder::HashDouble(const double& value)
{
    HashU64(ExactBinary64Bits(value));
}

void Builder::HashBytes(const std::uint8_t* data, std::size_t size)
{
    m_sha_byte_count += static_cast<std::uint64_t>(size);
    while (size != 0)
    {
        const std::size_t available = m_sha_buffer.size() - m_sha_buffer_size;
        const std::size_t copied = size < available ? size : available;
        std::memcpy(
            m_sha_buffer.data() + m_sha_buffer_size,
            data,
            copied);
        m_sha_buffer_size += copied;
        data += copied;
        size -= copied;

        if (m_sha_buffer_size == m_sha_buffer.size())
        {
            TransformSha256(m_sha_buffer.data());
            m_sha_buffer_size = 0;
        }
    }
}

void Builder::TransformSha256(const std::uint8_t block[64])
{
    std::uint32_t schedule[64];
    for (std::size_t index = 0; index < 16; ++index)
    {
        const std::size_t offset = index * 4;
        schedule[index] =
            (static_cast<std::uint32_t>(block[offset]) << 24U) |
            (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
            (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
            static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < 64; ++index)
    {
        const std::uint32_t first = schedule[index - 15];
        const std::uint32_t second = schedule[index - 2];
        const std::uint32_t sigma0 =
            RotateRight(first, 7) ^
            RotateRight(first, 18) ^
            (first >> 3U);
        const std::uint32_t sigma1 =
            RotateRight(second, 17) ^
            RotateRight(second, 19) ^
            (second >> 10U);
        schedule[index] =
            schedule[index - 16] + sigma0 +
            schedule[index - 7] + sigma1;
    }

    std::uint32_t a = m_sha_state[0];
    std::uint32_t b = m_sha_state[1];
    std::uint32_t c = m_sha_state[2];
    std::uint32_t d = m_sha_state[3];
    std::uint32_t e = m_sha_state[4];
    std::uint32_t f = m_sha_state[5];
    std::uint32_t g = m_sha_state[6];
    std::uint32_t h = m_sha_state[7];

    for (std::size_t index = 0; index < 64; ++index)
    {
        const std::uint32_t sum1 =
            RotateRight(e, 6) ^
            RotateRight(e, 11) ^
            RotateRight(e, 25);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 =
            h + sum1 + choose + SHA256_CONSTANTS[index] + schedule[index];
        const std::uint32_t sum0 =
            RotateRight(a, 2) ^
            RotateRight(a, 13) ^
            RotateRight(a, 22);
        const std::uint32_t majority =
            (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    m_sha_state[0] += a;
    m_sha_state[1] += b;
    m_sha_state[2] += c;
    m_sha_state[3] += d;
    m_sha_state[4] += e;
    m_sha_state[5] += f;
    m_sha_state[6] += g;
    m_sha_state[7] += h;
}

void Builder::FinalizeSha256(Digest& digest)
{
    const std::uint64_t message_bit_count = m_sha_byte_count * UINT64_C(8);

    const std::uint8_t one = UINT8_C(0x80);
    HashBytes(&one, 1);
    const std::uint8_t zero = 0;
    while (m_sha_buffer_size != 56)
        HashBytes(&zero, 1);

    const std::uint8_t length[8] = {
        static_cast<std::uint8_t>(message_bit_count >> 56U),
        static_cast<std::uint8_t>(message_bit_count >> 48U),
        static_cast<std::uint8_t>(message_bit_count >> 40U),
        static_cast<std::uint8_t>(message_bit_count >> 32U),
        static_cast<std::uint8_t>(message_bit_count >> 24U),
        static_cast<std::uint8_t>(message_bit_count >> 16U),
        static_cast<std::uint8_t>(message_bit_count >> 8U),
        static_cast<std::uint8_t>(message_bit_count)
    };
    HashBytes(length, sizeof(length));

    for (std::size_t word = 0; word < m_sha_state.size(); ++word)
    {
        digest.bytes[word * 4] =
            static_cast<std::uint8_t>(m_sha_state[word] >> 24U);
        digest.bytes[word * 4 + 1] =
            static_cast<std::uint8_t>(m_sha_state[word] >> 16U);
        digest.bytes[word * 4 + 2] =
            static_cast<std::uint8_t>(m_sha_state[word] >> 8U);
        digest.bytes[word * 4 + 3] =
            static_cast<std::uint8_t>(m_sha_state[word]);
    }
}

} // namespace DeterministicStateDigest
} // namespace RoR
