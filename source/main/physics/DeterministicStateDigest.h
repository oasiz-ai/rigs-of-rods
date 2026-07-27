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
/// @brief Canonical, dependency-free physics-state digest contract.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RoR {
namespace DeterministicStateDigest {

static const std::uint32_t SCHEMA_VERSION = 1;

// These ceilings are part of the digest schema and cannot be loosened by
// content or callers. The streaming builder itself retains no records.
static const std::uint32_t MAX_ACTORS = 4096;
static const std::uint32_t MAX_NODES = 1048576;
static const std::uint32_t MAX_BEAMS = 4194304;
static const std::uint32_t MAX_CONTACTS = 65536;

struct Digest
{
    std::array<std::uint8_t, 32> bytes;

    Digest();
    std::string ToHex() const;
    bool operator==(const Digest& other) const;
    bool operator!=(const Digest& other) const;
};

enum class Error
{
    NONE = 0,
    INVALID_SECTION_ORDER,
    COUNT_LIMIT_EXCEEDED,
    COUNT_MISMATCH,
    NON_CANONICAL_KEY,
    INVALID_RECORD,
    NON_FINITE_VALUE,
    ALREADY_FINISHED
};

struct ActorRecord
{
    std::int32_t actor_id;
    std::uint32_t state;
    std::uint32_t flags;
    std::uint64_t deterministic_seed;
    std::uint64_t actor_physics_step;
    std::uint64_t engine_update_step;
    std::array<float, 3> origin;

    ActorRecord();
};

struct NodeRecord
{
    std::int32_t actor_id;
    std::uint32_t node_id;
    std::array<float, 3> position;
    std::array<float, 3> velocity;

    NodeRecord();
};

enum BeamStateFlags : std::uint32_t
{
    BEAM_STATE_DISABLED = UINT32_C(1) << 0,
    BEAM_STATE_BROKEN = UINT32_C(1) << 1,
    BEAM_STATE_MATERIAL_FRACTURED = UINT32_C(1) << 2
};

struct BeamRecord
{
    std::int32_t actor_id;
    std::uint32_t beam_id;
    float rest_length;
    float stress;
    std::uint32_t material_schema_version;
    double plastic_strain;
    double accumulated_plastic_strain;
    double damage;
    double damage_driver_density;
    double last_total_strain;
    std::uint32_t state_flags;

    BeamRecord();
};

struct ContactRecord
{
    std::int32_t surface_actor;
    std::uint32_t surface_contact;
    std::int32_t hit_actor;
    std::uint32_t hit_node;

    ContactRecord();
};

/// Streams one complete snapshot in the fixed section order
/// actors -> nodes -> beams -> contacts. Records must have strictly increasing
/// canonical keys within each section. Counts are written before records, and
/// Finish() succeeds only after every declared record was observed.
///
/// Floating-point values are hashed as their exact IEEE-754 binary32 or
/// binary64 payloads in little-endian schema order. Signed zero is
/// intentionally distinct. NaN and infinity are rejected through bit
/// inspection so release fast-math cannot make an invalid snapshot appear
/// finite.
class Builder
{
public:
    Builder(std::uint64_t physics_step, std::uint64_t scenario_id);

    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    bool BeginActors(std::uint32_t count);
    bool AddActor(const ActorRecord& record);
    bool BeginNodes(std::uint32_t count);
    bool AddNode(const NodeRecord& record);
    bool BeginBeams(std::uint32_t count);
    bool AddBeam(const BeamRecord& record);
    bool BeginContacts(std::uint32_t count);
    bool AddContact(const ContactRecord& record);
    bool Finish(Digest& digest);

    Error GetError() const;
    std::uint32_t GetErrorRecordIndex() const;

private:
    enum class Section
    {
        INITIAL,
        ACTORS,
        NODES,
        BEAMS,
        CONTACTS,
        FINISHED
    };

    bool BeginSection(
        Section required_previous,
        Section next,
        std::uint32_t count,
        std::uint32_t limit,
        std::uint8_t tag);
    bool RequireWritable();
    bool RequireFinite(const float& value, std::uint32_t record_index);
    bool RequireFinite(const double& value, std::uint32_t record_index);
    bool Fail(Error error, std::uint32_t record_index);
    bool CompleteCurrentSection() const;

    void HashByte(std::uint8_t value);
    void HashU32(std::uint32_t value);
    void HashI32(std::int32_t value);
    void HashU64(std::uint64_t value);
    void HashFloat(const float& value);
    void HashDouble(const double& value);
    void HashBytes(const std::uint8_t* data, std::size_t size);
    void TransformSha256(const std::uint8_t block[64]);
    void FinalizeSha256(Digest& digest);

    Section m_section;
    Error m_error;
    std::uint32_t m_error_record_index;
    std::uint32_t m_expected_count;
    std::uint32_t m_observed_count;

    std::int32_t m_previous_actor_id;
    std::int32_t m_previous_node_actor_id;
    std::uint32_t m_previous_node_id;
    std::int32_t m_previous_beam_actor_id;
    std::uint32_t m_previous_beam_id;
    ContactRecord m_previous_contact;
    bool m_has_previous_key;

    std::array<std::uint32_t, 8> m_sha_state;
    std::array<std::uint8_t, 64> m_sha_buffer;
    std::uint64_t m_sha_byte_count;
    std::size_t m_sha_buffer_size;
};

} // namespace DeterministicStateDigest
} // namespace RoR
