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

#include "DeterministicInputTrace.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <istream>
#include <limits>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace {

using RoR::DeterministicInputTrace::Digest;

const std::uint8_t INPUT_MAGIC[16] = {
    'R', 'o', 'R', '-', 'D', '0', '-', 'I',
    'n', 'p', 'u', 't', '\r', '\n', UINT8_C(0x1a), '\n'
};

const std::uint32_t FRAME_TAG = UINT32_C(0x4d415246); // "FRAM"
const std::uint32_t END_TAG = UINT32_C(0x21444e45);   // "END!"

const std::uint8_t FRAME_HASH_DOMAIN[] = {
    'R', 'o', 'R', '-', 'D', '0', '-', 'I', 'n', 'p', 'u', 't',
    '-', 'F', 'r', 'a', 'm', 'e', '-', 'v', '1'
};

const std::uint8_t TRAILER_HASH_DOMAIN[] = {
    'R', 'o', 'R', '-', 'D', '0', '-', 'I', 'n', 'p', 'u', 't',
    '-', 'T', 'r', 'a', 'i', 'l', 'e', 'r', '-', 'v', '1'
};

void StoreU32(std::uint8_t* destination, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index)
    {
        destination[index] =
            static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

void StoreU64(std::uint8_t* destination, std::uint64_t value)
{
    for (unsigned int index = 0; index < 8; ++index)
    {
        destination[index] =
            static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint32_t LoadU32(const std::uint8_t* source)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(source[index])
            << (index * 8U);
    }
    return value;
}

std::uint64_t LoadU64(const std::uint8_t* source)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(source[index])
            << (index * 8U);
    }
    return value;
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

double Binary64FromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool IsFiniteBinary64Bits(std::uint64_t bits)
{
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool IsNegativeZeroBits(std::uint64_t bits)
{
    return bits == UINT64_C(0x8000000000000000);
}

bool IsPositiveZeroBits(std::uint64_t bits)
{
    return bits == 0;
}

bool AddWouldOverflow(std::uint64_t left, std::uint64_t right)
{
    return left > std::numeric_limits<std::uint64_t>::max() - right;
}

bool MultiplyWouldOverflow(std::uint64_t left, std::uint64_t right)
{
    return left != 0 &&
        right > std::numeric_limits<std::uint64_t>::max() / left;
}

std::uint64_t GreatestCommonDivisor(
    std::uint64_t left,
    std::uint64_t right)
{
    while (right != 0)
    {
        const std::uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

std::uint32_t RotateRight(std::uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
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

class Sha256
{
public:
    Sha256():
        m_state(),
        m_buffer(),
        m_byte_count(0),
        m_buffer_size(0)
    {
        m_state[0] = UINT32_C(0x6a09e667);
        m_state[1] = UINT32_C(0xbb67ae85);
        m_state[2] = UINT32_C(0x3c6ef372);
        m_state[3] = UINT32_C(0xa54ff53a);
        m_state[4] = UINT32_C(0x510e527f);
        m_state[5] = UINT32_C(0x9b05688c);
        m_state[6] = UINT32_C(0x1f83d9ab);
        m_state[7] = UINT32_C(0x5be0cd19);
    }

    void Update(const std::uint8_t* data, std::size_t size)
    {
        if (size == 0)
            return;
        m_byte_count += static_cast<std::uint64_t>(size);
        for (std::size_t index = 0; index < size; ++index)
        {
            m_buffer[m_buffer_size++] = data[index];
            if (m_buffer_size == m_buffer.size())
            {
                Transform(m_buffer.data());
                m_buffer_size = 0;
            }
        }
    }

    Digest Finish()
    {
        const std::uint64_t bit_count = m_byte_count * UINT64_C(8);
        m_buffer[m_buffer_size++] = UINT8_C(0x80);
        if (m_buffer_size > 56)
        {
            while (m_buffer_size < m_buffer.size())
                m_buffer[m_buffer_size++] = 0;
            Transform(m_buffer.data());
            m_buffer_size = 0;
        }
        while (m_buffer_size < 56)
            m_buffer[m_buffer_size++] = 0;
        for (unsigned int index = 0; index < 8; ++index)
        {
            m_buffer[63U - index] =
                static_cast<std::uint8_t>(bit_count >> (index * 8U));
        }
        Transform(m_buffer.data());

        Digest digest;
        for (std::size_t word = 0; word < m_state.size(); ++word)
        {
            for (unsigned int byte = 0; byte < 4; ++byte)
            {
                digest.bytes[word * 4U + byte] =
                    static_cast<std::uint8_t>(
                        m_state[word] >> ((3U - byte) * 8U));
            }
        }
        return digest;
    }

private:
    void Transform(const std::uint8_t block[64])
    {
        std::uint32_t words[64] = {};
        for (std::size_t index = 0; index < 16; ++index)
        {
            words[index] =
                (static_cast<std::uint32_t>(block[index * 4U]) << 24U) |
                (static_cast<std::uint32_t>(block[index * 4U + 1U]) << 16U) |
                (static_cast<std::uint32_t>(block[index * 4U + 2U]) << 8U) |
                static_cast<std::uint32_t>(block[index * 4U + 3U]);
        }
        for (std::size_t index = 16; index < 64; ++index)
        {
            const std::uint32_t left = words[index - 15U];
            const std::uint32_t right = words[index - 2U];
            const std::uint32_t sigma0 =
                RotateRight(left, 7U) ^
                RotateRight(left, 18U) ^
                (left >> 3U);
            const std::uint32_t sigma1 =
                RotateRight(right, 17U) ^
                RotateRight(right, 19U) ^
                (right >> 10U);
            words[index] =
                words[index - 16U] + sigma0 +
                words[index - 7U] + sigma1;
        }

        std::uint32_t a = m_state[0];
        std::uint32_t b = m_state[1];
        std::uint32_t c = m_state[2];
        std::uint32_t d = m_state[3];
        std::uint32_t e = m_state[4];
        std::uint32_t f = m_state[5];
        std::uint32_t g = m_state[6];
        std::uint32_t h = m_state[7];
        for (std::size_t index = 0; index < 64; ++index)
        {
            const std::uint32_t sum1 =
                RotateRight(e, 6U) ^
                RotateRight(e, 11U) ^
                RotateRight(e, 25U);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 =
                h + sum1 + choose + SHA256_CONSTANTS[index] + words[index];
            const std::uint32_t sum0 =
                RotateRight(a, 2U) ^
                RotateRight(a, 13U) ^
                RotateRight(a, 22U);
            const std::uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<std::uint32_t, 8> m_state;
    std::array<std::uint8_t, 64> m_buffer;
    std::uint64_t m_byte_count;
    std::size_t m_buffer_size;
};

Digest HashBytes(const std::uint8_t* data, std::size_t size)
{
    Sha256 hash;
    hash.Update(data, size);
    return hash.Finish();
}

Digest HashFrame(
    const Digest& previous,
    const std::uint8_t* prefix,
    const std::uint8_t* events,
    std::size_t event_bytes)
{
    Sha256 hash;
    hash.Update(FRAME_HASH_DOMAIN, sizeof(FRAME_HASH_DOMAIN));
    hash.Update(previous.bytes.data(), previous.bytes.size());
    hash.Update(
        prefix,
        RoR::DeterministicInputTrace::FRAME_PREFIX_SIZE);
    hash.Update(events, event_bytes);
    return hash.Finish();
}

Digest HashTrailer(const std::uint8_t* trailer_prefix)
{
    Sha256 hash;
    hash.Update(TRAILER_HASH_DOMAIN, sizeof(TRAILER_HASH_DOMAIN));
    hash.Update(trailer_prefix, 72);
    return hash.Finish();
}

bool DigestEquals(const Digest& left, const Digest& right)
{
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < left.bytes.size(); ++index)
        difference |= left.bytes[index] ^ right.bytes[index];
    return difference == 0;
}

bool IsAllZero(const Digest& digest)
{
    std::uint8_t combined = 0;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
        combined |= digest.bytes[index];
    return combined == 0;
}

bool IsUnicodeNoncharacter(std::uint32_t codepoint)
{
    return (codepoint >= UINT32_C(0xfdd0) &&
            codepoint <= UINT32_C(0xfdef)) ||
        ((codepoint & UINT32_C(0xffff)) == UINT32_C(0xfffe)) ||
        ((codepoint & UINT32_C(0xffff)) == UINT32_C(0xffff));
}

bool IsCanonicalIdentityString(const std::string& value)
{
    if (value.empty())
        return false;

    std::size_t index = 0;
    while (index < value.size())
    {
        const std::uint8_t first =
            static_cast<std::uint8_t>(value[index]);
        std::uint32_t codepoint = 0;
        std::size_t continuation_count = 0;
        std::uint32_t minimum = 0;
        if (first <= UINT8_C(0x7f))
        {
            codepoint = first;
        }
        else if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf))
        {
            codepoint = first & UINT8_C(0x1f);
            continuation_count = 1;
            minimum = UINT32_C(0x80);
        }
        else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef))
        {
            codepoint = first & UINT8_C(0x0f);
            continuation_count = 2;
            minimum = UINT32_C(0x800);
        }
        else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4))
        {
            codepoint = first & UINT8_C(0x07);
            continuation_count = 3;
            minimum = UINT32_C(0x10000);
        }
        else
        {
            return false;
        }

        if (continuation_count > value.size() - index - 1U)
            return false;
        for (std::size_t continuation = 0;
             continuation < continuation_count;
             ++continuation)
        {
            const std::uint8_t byte =
                static_cast<std::uint8_t>(
                    value[index + continuation + 1U]);
            if ((byte & UINT8_C(0xc0)) != UINT8_C(0x80))
                return false;
            codepoint =
                (codepoint << 6U) | (byte & UINT8_C(0x3f));
        }
        if (continuation_count != 0 && codepoint < minimum)
            return false;
        if (codepoint > UINT32_C(0x10ffff) ||
            (codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff)) ||
            codepoint <= UINT32_C(0x1f) ||
            (codepoint >= UINT32_C(0x7f) &&
             codepoint <= UINT32_C(0x9f)) ||
            IsUnicodeNoncharacter(codepoint))
        {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

RoR::DeterministicInputTrace::Limits ClampLimits(
    const RoR::DeterministicInputTrace::Limits& requested)
{
    using namespace RoR::DeterministicInputTrace;
    Limits limits = requested;
    limits.max_steps = std::min(limits.max_steps, MAX_TRACE_STEPS);
    limits.max_events = std::min(limits.max_events, MAX_TRACE_EVENTS);
    limits.max_bytes = std::min(limits.max_bytes, MAX_TRACE_BYTES);
    limits.max_events_per_step =
        std::min(limits.max_events_per_step, MAX_EVENTS_PER_STEP);
    limits.max_active_controls =
        std::min(limits.max_active_controls, MAX_ACTIVE_CONTROLS);
    limits.max_identity_string_bytes =
        std::min(
            limits.max_identity_string_bytes,
            MAX_IDENTITY_STRING_BYTES);
    return limits;
}

bool ValidateMetadata(
    const RoR::DeterministicInputTrace::Metadata& metadata,
    const RoR::DeterministicInputTrace::Limits& limits,
    RoR::DeterministicInputTrace::Error& error)
{
    using namespace RoR::DeterministicInputTrace;
    if (metadata.semantic_flags != REQUIRED_SEMANTIC_FLAGS)
    {
        error = Error::INVALID_SEMANTICS;
        return false;
    }
    if (metadata.stream_id == 0 ||
        metadata.physics_step_numerator == 0 ||
        metadata.physics_step_denominator == 0 ||
        metadata.physics_step_numerator > metadata.physics_step_denominator ||
        metadata.physics_step_numerator > MAX_PHYSICS_RATE_COMPONENT ||
        metadata.physics_step_denominator > MAX_PHYSICS_RATE_COMPONENT ||
        GreatestCommonDivisor(
            metadata.physics_step_numerator,
            metadata.physics_step_denominator) != 1 ||
        IsAllZero(metadata.source_digest))
    {
        error = Error::INVALID_METADATA;
        return false;
    }
    if (metadata.scenario_name.size() > limits.max_identity_string_bytes ||
        metadata.source_name.size() > limits.max_identity_string_bytes)
    {
        error = Error::STRING_LIMIT_EXCEEDED;
        return false;
    }
    if (!IsCanonicalIdentityString(metadata.scenario_name) ||
        !IsCanonicalIdentityString(metadata.source_name))
    {
        error = Error::INVALID_IDENTITY_STRING;
        return false;
    }
    return true;
}

bool EventKeyLess(
    std::uint64_t left_target,
    std::uint32_t left_control,
    std::uint64_t right_target,
    std::uint32_t right_control)
{
    if (left_target != right_target)
        return left_target < right_target;
    return left_control < right_control;
}

bool PersistentControlLessThanKey(
    const RoR::DeterministicInputTrace::PersistentControl& left,
    const std::pair<std::uint64_t, std::uint32_t>& right)
{
    return EventKeyLess(
        left.target_id,
        left.control_id,
        right.first,
        right.second);
}

bool PersistentControlKeyEquals(
    const RoR::DeterministicInputTrace::PersistentControl& control,
    std::uint64_t target_id,
    std::uint32_t control_id)
{
    return control.target_id == target_id &&
        control.control_id == control_id;
}

std::vector<RoR::DeterministicInputTrace::PersistentControl>::iterator
FindPersistentControl(
    std::vector<RoR::DeterministicInputTrace::PersistentControl>& controls,
    std::uint64_t target_id,
    std::uint32_t control_id)
{
    return std::lower_bound(
        controls.begin(),
        controls.end(),
        std::make_pair(target_id, control_id),
        PersistentControlLessThanKey);
}

std::vector<RoR::DeterministicInputTrace::PersistentControl>::const_iterator
FindPersistentControl(
    const std::vector<RoR::DeterministicInputTrace::PersistentControl>& controls,
    std::uint64_t target_id,
    std::uint32_t control_id)
{
    return std::lower_bound(
        controls.begin(),
        controls.end(),
        std::make_pair(target_id, control_id),
        PersistentControlLessThanKey);
}

bool MetadataEqual(
    const RoR::DeterministicInputTrace::Metadata& left,
    const RoR::DeterministicInputTrace::Metadata& right,
    RoR::DeterministicInputTrace::MetadataField& field)
{
    using namespace RoR::DeterministicInputTrace;
    if (left.semantic_flags != right.semantic_flags)
        field = MetadataField::SEMANTICS;
    else if (left.scenario_id != right.scenario_id)
        field = MetadataField::SCENARIO_ID;
    else if (left.stream_id != right.stream_id)
        field = MetadataField::STREAM_ID;
    else if (left.first_physics_step != right.first_physics_step)
        field = MetadataField::FIRST_PHYSICS_STEP;
    else if (left.physics_step_numerator != right.physics_step_numerator ||
             left.physics_step_denominator != right.physics_step_denominator)
        field = MetadataField::PHYSICS_STEP_RATE;
    else if (left.scenario_name != right.scenario_name)
        field = MetadataField::SCENARIO_NAME;
    else if (left.source_name != right.source_name)
        field = MetadataField::SOURCE_NAME;
    else if (left.source_digest != right.source_digest)
        field = MetadataField::SOURCE_DIGEST;
    else
        field = MetadataField::NONE;
    return field == MetadataField::NONE;
}

static_assert(sizeof(double) == sizeof(std::uint64_t),
    "deterministic input traces require binary64 double storage");
static_assert(std::numeric_limits<double>::is_iec559,
    "deterministic input traces require IEEE-754 doubles");
static_assert(
    std::numeric_limits<double>::radix == 2 &&
        std::numeric_limits<double>::digits == 53,
    "deterministic input traces require IEEE-754 binary64 doubles");

} // namespace

namespace RoR {
namespace DeterministicInputTrace {

Digest::Digest(): bytes()
{
}

std::string Digest::ToHex() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        stream << std::setw(2)
               << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

bool Digest::operator==(const Digest& other) const
{
    return DigestEquals(*this, other);
}

bool Digest::operator!=(const Digest& other) const
{
    return !(*this == other);
}

Metadata::Metadata():
    semantic_flags(REQUIRED_SEMANTIC_FLAGS),
    scenario_id(0),
    stream_id(1),
    first_physics_step(0),
    physics_step_numerator(1),
    physics_step_denominator(2000),
    scenario_name("unnamed-scenario"),
    source_name("unknown-source"),
    source_digest()
{
}

Event::Event():
    target_id(0),
    control_id(0),
    kind(EventKind::STATE),
    value(0.0)
{
}

Frame::Frame():
    physics_step(0),
    events()
{
}

PersistentControl::PersistentControl():
    target_id(0),
    control_id(0),
    value(0.0)
{
}

Limits::Limits():
    max_steps(MAX_TRACE_STEPS),
    max_events(MAX_TRACE_EVENTS),
    max_bytes(MAX_TRACE_BYTES),
    max_events_per_step(MAX_EVENTS_PER_STEP),
    max_active_controls(MAX_ACTIVE_CONTROLS),
    max_identity_string_bytes(MAX_IDENTITY_STRING_BYTES)
{
}

Status::Status():
    error(Error::NONE),
    byte_offset(0),
    step_index(0),
    event_index(0)
{
}

ReplayState::ReplayState():
    m_controls()
{
}

bool ReplayState::Apply(
    const Frame& frame,
    std::uint32_t max_active_controls,
    Error* error,
    std::uint32_t* error_event_index)
{
    if (error != nullptr)
        *error = Error::NONE;
    if (error_event_index != nullptr)
        *error_event_index = 0;

    std::uint64_t additions = 0;
    std::uint64_t removals = 0;
    for (std::size_t index = 0; index < frame.events.size(); ++index)
    {
        const Event& event = frame.events[index];
        const std::uint32_t event_index =
            static_cast<std::uint32_t>(index);
        const auto fail =
            [error, error_event_index, event_index](Error failure)
            {
                if (error != nullptr)
                    *error = failure;
                if (error_event_index != nullptr)
                    *error_event_index = event_index;
                return false;
            };

        if (event.control_id == 0)
            return fail(Error::INVALID_CONTROL_ID);
        if (index != 0)
        {
            const Event& previous = frame.events[index - 1U];
            if (!EventKeyLess(
                    previous.target_id,
                    previous.control_id,
                    event.target_id,
                    event.control_id))
            {
                return fail(Error::NON_CANONICAL_EVENT_ORDER);
            }
        }

        const std::uint64_t bits = ExactBinary64Bits(event.value);
        if (!IsFiniteBinary64Bits(bits))
            return fail(Error::NON_FINITE_VALUE);
        if (IsNegativeZeroBits(bits))
            return fail(Error::NON_CANONICAL_VALUE);
        if (event.kind != EventKind::STATE &&
            event.kind != EventKind::IMPULSE)
        {
            return fail(Error::UNKNOWN_EVENT_KIND);
        }
        if (event.kind == EventKind::IMPULSE)
        {
            if (IsPositiveZeroBits(bits))
                return fail(Error::NON_CANONICAL_VALUE);
            continue;
        }

        const std::vector<PersistentControl>::const_iterator existing =
            FindPersistentControl(
                m_controls,
                event.target_id,
                event.control_id);
        const bool found =
            existing != m_controls.end() &&
            PersistentControlKeyEquals(
                *existing,
                event.target_id,
                event.control_id);
        if (IsPositiveZeroBits(bits))
        {
            if (!found)
                return fail(Error::REDUNDANT_STATE_EVENT);
            ++removals;
        }
        else if (!found)
        {
            ++additions;
        }
        else if (ExactBinary64Bits(existing->value) == bits)
        {
            return fail(Error::REDUNDANT_STATE_EVENT);
        }
    }

    const std::uint64_t current =
        static_cast<std::uint64_t>(m_controls.size());
    if (removals > current ||
        AddWouldOverflow(current - removals, additions) ||
        current - removals + additions > max_active_controls)
    {
        if (error != nullptr)
            *error = Error::ACTIVE_CONTROL_LIMIT_EXCEEDED;
        if (error_event_index != nullptr)
        {
            *error_event_index = static_cast<std::uint32_t>(
                frame.events.empty() ? 0 : frame.events.size() - 1U);
        }
        return false;
    }

    try
    {
        if (additions != 0)
        {
            m_controls.reserve(
                m_controls.size() +
                static_cast<std::size_t>(additions));
        }
    }
    catch (const std::bad_alloc&)
    {
        if (error != nullptr)
            *error = Error::ALLOCATION_FAILURE;
        return false;
    }
    catch (const std::length_error&)
    {
        if (error != nullptr)
            *error = Error::ALLOCATION_FAILURE;
        return false;
    }

    for (std::size_t index = 0; index < frame.events.size(); ++index)
    {
        const Event& event = frame.events[index];
        if (event.kind == EventKind::IMPULSE)
            continue;
        std::vector<PersistentControl>::iterator existing =
            FindPersistentControl(
                m_controls,
                event.target_id,
                event.control_id);
        const std::uint64_t bits = ExactBinary64Bits(event.value);
        if (IsPositiveZeroBits(bits))
        {
            m_controls.erase(existing);
        }
        else if (existing != m_controls.end() &&
                 PersistentControlKeyEquals(
                     *existing,
                     event.target_id,
                     event.control_id))
        {
            existing->value = event.value;
        }
        else
        {
            PersistentControl control;
            control.target_id = event.target_id;
            control.control_id = event.control_id;
            control.value = event.value;
            m_controls.insert(existing, control);
        }
    }
    return true;
}

bool ReplayState::GetValue(
    std::uint64_t target_id,
    std::uint32_t control_id,
    double& value) const
{
    const std::vector<PersistentControl>::const_iterator found =
        FindPersistentControl(m_controls, target_id, control_id);
    if (found == m_controls.end() ||
        !PersistentControlKeyEquals(*found, target_id, control_id))
    {
        value = 0.0;
        return false;
    }
    value = found->value;
    return true;
}

const std::vector<PersistentControl>& ReplayState::GetControls() const
{
    return m_controls;
}

std::size_t ReplayState::GetControlCount() const
{
    return m_controls.size();
}

bool ReplayState::operator==(const ReplayState& other) const
{
    if (m_controls.size() != other.m_controls.size())
        return false;
    for (std::size_t index = 0; index < m_controls.size(); ++index)
    {
        const PersistentControl& left = m_controls[index];
        const PersistentControl& right = other.m_controls[index];
        if (left.target_id != right.target_id ||
            left.control_id != right.control_id ||
            ExactBinary64Bits(left.value) !=
                ExactBinary64Bits(right.value))
        {
            return false;
        }
    }
    return true;
}

bool ReplayState::operator!=(const ReplayState& other) const
{
    return !(*this == other);
}

Writer::Writer(
    std::ostream& output,
    const Metadata& metadata,
    const Limits& limits):
    m_output(output),
    m_metadata(),
    m_limits(ClampLimits(limits)),
    m_status(),
    m_replay_state(),
    m_chain_digest(),
    m_step_count(0),
    m_event_count(0),
    m_bytes_written(0),
    m_next_physics_step(metadata.first_physics_step),
    m_finished(false)
{
    Error metadata_error = Error::NONE;
    if (!ValidateMetadata(metadata, m_limits, metadata_error))
    {
        Fail(metadata_error, 0);
        return;
    }
    try
    {
        m_metadata = metadata;
    }
    catch (const std::bad_alloc&)
    {
        Fail(Error::ALLOCATION_FAILURE, 0);
        return;
    }
    catch (const std::length_error&)
    {
        Fail(Error::ALLOCATION_FAILURE, 0);
        return;
    }

    const std::uint64_t string_bytes =
        static_cast<std::uint64_t>(m_metadata.scenario_name.size()) +
        static_cast<std::uint64_t>(m_metadata.source_name.size());
    const std::uint64_t header_size_64 =
        static_cast<std::uint64_t>(HEADER_MIN_SIZE) + string_bytes;
    if (header_size_64 > std::numeric_limits<std::uint32_t>::max())
    {
        Fail(Error::ARITHMETIC_OVERFLOW, 0);
        return;
    }
    if (header_size_64 > m_limits.max_bytes ||
        AddWouldOverflow(header_size_64, TRAILER_SIZE) ||
        header_size_64 + TRAILER_SIZE > m_limits.max_bytes)
    {
        Fail(Error::BYTE_LIMIT_EXCEEDED, 0);
        return;
    }

    std::vector<std::uint8_t> header;
    try
    {
        header.resize(static_cast<std::size_t>(header_size_64), 0);
    }
    catch (const std::bad_alloc&)
    {
        Fail(Error::ALLOCATION_FAILURE, 0);
        return;
    }
    catch (const std::length_error&)
    {
        Fail(Error::ALLOCATION_FAILURE, 0);
        return;
    }

    std::memcpy(header.data(), INPUT_MAGIC, sizeof(INPUT_MAGIC));
    StoreU32(header.data() + 16, SCHEMA_VERSION);
    StoreU32(
        header.data() + 20,
        static_cast<std::uint32_t>(header_size_64));
    StoreU32(header.data() + 24, m_metadata.semantic_flags);
    StoreU32(header.data() + 28, 0);
    StoreU64(header.data() + 32, m_metadata.scenario_id);
    StoreU64(header.data() + 40, m_metadata.stream_id);
    StoreU64(header.data() + 48, m_metadata.first_physics_step);
    StoreU64(header.data() + 56, m_metadata.physics_step_numerator);
    StoreU64(header.data() + 64, m_metadata.physics_step_denominator);
    StoreU32(
        header.data() + 72,
        static_cast<std::uint32_t>(m_metadata.scenario_name.size()));
    StoreU32(
        header.data() + 76,
        static_cast<std::uint32_t>(m_metadata.source_name.size()));
    std::memcpy(
        header.data() + 80,
        m_metadata.source_digest.bytes.data(),
        m_metadata.source_digest.bytes.size());
    std::size_t string_offset = HEADER_PREFIX_SIZE;
    std::memcpy(
        header.data() + string_offset,
        m_metadata.scenario_name.data(),
        m_metadata.scenario_name.size());
    string_offset += m_metadata.scenario_name.size();
    std::memcpy(
        header.data() + string_offset,
        m_metadata.source_name.data(),
        m_metadata.source_name.size());

    m_chain_digest =
        HashBytes(header.data(), header.size() - DIGEST_SIZE);
    std::memcpy(
        header.data() + header.size() - DIGEST_SIZE,
        m_chain_digest.bytes.data(),
        m_chain_digest.bytes.size());
    WriteBytes(header.data(), header.size());
}

bool Writer::IsReady() const
{
    return m_status.error == Error::NONE && !m_finished;
}

bool Writer::Fail(
    Error error,
    std::uint64_t byte_offset,
    std::uint32_t event_index)
{
    if (m_status.error == Error::NONE)
    {
        m_status.error = error;
        m_status.byte_offset = byte_offset;
        m_status.step_index = m_step_count;
        m_status.event_index = event_index;
    }
    return false;
}

bool Writer::WriteBytes(const std::uint8_t* data, std::size_t size)
{
    if (m_status.error != Error::NONE)
        return false;
    if (size > m_limits.max_bytes - m_bytes_written)
        return Fail(Error::BYTE_LIMIT_EXCEEDED, m_bytes_written);
    try
    {
        m_output.write(
            reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(size));
    }
    catch (const std::ios_base::failure&)
    {
        return Fail(Error::IO_FAILURE, m_bytes_written);
    }
    if (!m_output.good())
        return Fail(Error::IO_FAILURE, m_bytes_written);
    m_bytes_written += static_cast<std::uint64_t>(size);
    return true;
}

bool Writer::Append(const Frame& frame)
{
    if (m_status.error != Error::NONE)
        return false;
    if (m_finished)
        return Fail(Error::ALREADY_FINISHED, m_bytes_written);
    if (m_step_count >= m_limits.max_steps)
        return Fail(Error::STEP_LIMIT_EXCEEDED, m_bytes_written);
    if (frame.physics_step != m_next_physics_step)
        return Fail(Error::NON_CONTIGUOUS_STEP, m_bytes_written);
    if (frame.physics_step == std::numeric_limits<std::uint64_t>::max())
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    if (frame.events.size() > m_limits.max_events_per_step ||
        frame.events.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return Fail(Error::EVENT_LIMIT_EXCEEDED, m_bytes_written);
    }
    const std::uint64_t frame_event_count =
        static_cast<std::uint64_t>(frame.events.size());
    if (frame_event_count > m_limits.max_events - m_event_count)
        return Fail(Error::EVENT_LIMIT_EXCEEDED, m_bytes_written);
    if (MultiplyWouldOverflow(frame_event_count, EVENT_SIZE))
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    const std::uint64_t frame_size_64 =
        static_cast<std::uint64_t>(FRAME_MIN_SIZE) +
        frame_event_count * EVENT_SIZE;
    if (frame_size_64 > std::numeric_limits<std::uint32_t>::max())
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    if (frame_size_64 > m_limits.max_bytes - m_bytes_written ||
        TRAILER_SIZE >
            m_limits.max_bytes - m_bytes_written - frame_size_64)
    {
        return Fail(Error::BYTE_LIMIT_EXCEEDED, m_bytes_written);
    }

    Error replay_error = Error::NONE;
    std::uint32_t replay_error_index = 0;
    if (!m_replay_state.Apply(
            frame,
            m_limits.max_active_controls,
            &replay_error,
            &replay_error_index))
    {
        return Fail(
            replay_error,
            m_bytes_written,
            replay_error_index);
    }

    std::vector<std::uint8_t> record;
    try
    {
        record.resize(static_cast<std::size_t>(frame_size_64), 0);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_written);
    }
    catch (const std::length_error&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_written);
    }
    StoreU32(record.data(), FRAME_TAG);
    StoreU32(
        record.data() + 4,
        static_cast<std::uint32_t>(frame_size_64));
    StoreU64(record.data() + 8, frame.physics_step);
    StoreU32(
        record.data() + 16,
        static_cast<std::uint32_t>(frame_event_count));
    StoreU32(record.data() + 20, 0);
    for (std::size_t index = 0; index < frame.events.size(); ++index)
    {
        const Event& event = frame.events[index];
        std::uint8_t* encoded =
            record.data() + FRAME_PREFIX_SIZE + index * EVENT_SIZE;
        StoreU64(encoded, event.target_id);
        StoreU32(encoded + 8, event.control_id);
        StoreU32(
            encoded + 12,
            static_cast<std::uint32_t>(event.kind));
        StoreU64(encoded + 16, ExactBinary64Bits(event.value));
    }
    const std::size_t event_bytes =
        frame.events.size() * static_cast<std::size_t>(EVENT_SIZE);
    const Digest frame_digest =
        HashFrame(
            m_chain_digest,
            record.data(),
            record.data() + FRAME_PREFIX_SIZE,
            event_bytes);
    std::memcpy(
        record.data() + FRAME_PREFIX_SIZE + event_bytes,
        frame_digest.bytes.data(),
        frame_digest.bytes.size());
    if (!WriteBytes(record.data(), record.size()))
        return false;

    m_chain_digest = frame_digest;
    ++m_step_count;
    m_event_count += frame_event_count;
    ++m_next_physics_step;
    return true;
}

bool Writer::Finish()
{
    if (m_status.error != Error::NONE)
        return false;
    if (m_finished)
        return Fail(Error::ALREADY_FINISHED, m_bytes_written);

    std::array<std::uint8_t, TRAILER_SIZE> trailer = {};
    StoreU32(trailer.data(), END_TAG);
    StoreU32(trailer.data() + 4, TRAILER_SIZE);
    StoreU64(trailer.data() + 8, m_step_count);
    StoreU64(trailer.data() + 16, m_event_count);
    StoreU64(trailer.data() + 24, m_next_physics_step);
    StoreU32(
        trailer.data() + 32,
        static_cast<std::uint32_t>(
            m_replay_state.GetControlCount()));
    StoreU32(trailer.data() + 36, 0);
    std::memcpy(
        trailer.data() + 40,
        m_chain_digest.bytes.data(),
        m_chain_digest.bytes.size());
    const Digest trailer_digest = HashTrailer(trailer.data());
    std::memcpy(
        trailer.data() + 72,
        trailer_digest.bytes.data(),
        trailer_digest.bytes.size());
    if (!WriteBytes(trailer.data(), trailer.size()))
        return false;
    m_chain_digest = trailer_digest;
    m_finished = true;
    return true;
}

const Status& Writer::GetStatus() const
{
    return m_status;
}

const ReplayState& Writer::GetReplayState() const
{
    return m_replay_state;
}

const Digest& Writer::GetTraceDigest() const
{
    return m_chain_digest;
}

std::uint64_t Writer::GetStepCount() const
{
    return m_step_count;
}

std::uint64_t Writer::GetEventCount() const
{
    return m_event_count;
}

std::uint64_t Writer::GetBytesWritten() const
{
    return m_bytes_written;
}

Reader::Reader(
    std::istream& input,
    const Limits& limits):
    m_input(input),
    m_metadata(),
    m_limits(ClampLimits(limits)),
    m_status(),
    m_replay_state(),
    m_chain_digest(),
    m_step_count(0),
    m_event_count(0),
    m_bytes_read(0),
    m_next_physics_step(0),
    m_finished(false)
{
    ReadHeader();
}

bool Reader::IsReady() const
{
    return m_status.error == Error::NONE && !m_finished;
}

bool Reader::Fail(
    Error error,
    std::uint64_t byte_offset,
    std::uint32_t event_index)
{
    if (m_status.error == Error::NONE)
    {
        m_status.error = error;
        m_status.byte_offset = byte_offset;
        m_status.step_index = m_step_count;
        m_status.event_index = event_index;
    }
    return false;
}

bool Reader::ReadBytes(std::uint8_t* data, std::size_t size)
{
    if (m_status.error != Error::NONE)
        return false;
    if (size > m_limits.max_bytes - m_bytes_read)
        return Fail(Error::BYTE_LIMIT_EXCEEDED, m_bytes_read);

    std::streamsize received = 0;
    try
    {
        m_input.read(
            reinterpret_cast<char*>(data),
            static_cast<std::streamsize>(size));
        received = m_input.gcount();
    }
    catch (const std::ios_base::failure&)
    {
        received = m_input.gcount();
        if (received > 0)
            m_bytes_read += static_cast<std::uint64_t>(received);
        if (m_input.bad() && !m_input.eof())
            return Fail(Error::IO_FAILURE, m_bytes_read);
        return Fail(Error::TRUNCATED, m_bytes_read);
    }
    if (received > 0)
        m_bytes_read += static_cast<std::uint64_t>(received);
    if (received != static_cast<std::streamsize>(size))
    {
        if (m_input.bad() && !m_input.eof())
            return Fail(Error::IO_FAILURE, m_bytes_read);
        return Fail(Error::TRUNCATED, m_bytes_read);
    }
    return true;
}

bool Reader::ReadHeader()
{
    std::array<std::uint8_t, HEADER_PREFIX_SIZE> prefix = {};
    if (!ReadBytes(prefix.data(), prefix.size()))
        return false;
    if (std::memcmp(prefix.data(), INPUT_MAGIC, sizeof(INPUT_MAGIC)) != 0)
        return Fail(Error::MAGIC_MISMATCH, 0);
    if (LoadU32(prefix.data() + 16) != SCHEMA_VERSION)
        return Fail(Error::UNSUPPORTED_SCHEMA, 16);

    const std::uint32_t header_size = LoadU32(prefix.data() + 20);
    const std::uint32_t scenario_name_size =
        LoadU32(prefix.data() + 72);
    const std::uint32_t source_name_size =
        LoadU32(prefix.data() + 76);
    if (header_size < HEADER_MIN_SIZE)
        return Fail(Error::INVALID_HEADER_SIZE, 20);
    if (scenario_name_size > m_limits.max_identity_string_bytes ||
        source_name_size > m_limits.max_identity_string_bytes)
    {
        return Fail(Error::STRING_LIMIT_EXCEEDED, 72);
    }
    const std::uint64_t expected_header_size =
        static_cast<std::uint64_t>(HEADER_MIN_SIZE) +
        scenario_name_size + source_name_size;
    if (header_size != expected_header_size)
        return Fail(Error::INVALID_HEADER_SIZE, 20);
    if (header_size > m_limits.max_bytes ||
        TRAILER_SIZE > m_limits.max_bytes - header_size)
    {
        return Fail(Error::BYTE_LIMIT_EXCEEDED, 20);
    }

    std::vector<std::uint8_t> header;
    try
    {
        header.resize(header_size, 0);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_read);
    }
    catch (const std::length_error&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_read);
    }
    std::memcpy(header.data(), prefix.data(), prefix.size());
    if (!ReadBytes(
            header.data() + prefix.size(),
            header.size() - prefix.size()))
    {
        return false;
    }

    Digest stored_digest;
    std::memcpy(
        stored_digest.bytes.data(),
        header.data() + header.size() - DIGEST_SIZE,
        stored_digest.bytes.size());
    const Digest computed_digest =
        HashBytes(header.data(), header.size() - DIGEST_SIZE);
    if (!DigestEquals(stored_digest, computed_digest))
        return Fail(Error::INTEGRITY_MISMATCH, header.size() - DIGEST_SIZE);
    if (LoadU32(prefix.data() + 28) != 0)
        return Fail(Error::RESERVED_FIELD_NONZERO, 28);

    m_metadata.semantic_flags = LoadU32(prefix.data() + 24);
    m_metadata.scenario_id = LoadU64(prefix.data() + 32);
    m_metadata.stream_id = LoadU64(prefix.data() + 40);
    m_metadata.first_physics_step = LoadU64(prefix.data() + 48);
    m_metadata.physics_step_numerator = LoadU64(prefix.data() + 56);
    m_metadata.physics_step_denominator = LoadU64(prefix.data() + 64);
    std::memcpy(
        m_metadata.source_digest.bytes.data(),
        prefix.data() + 80,
        m_metadata.source_digest.bytes.size());
    const char* strings =
        reinterpret_cast<const char*>(header.data() + HEADER_PREFIX_SIZE);
    try
    {
        m_metadata.scenario_name.assign(strings, scenario_name_size);
        m_metadata.source_name.assign(
            strings + scenario_name_size,
            source_name_size);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(Error::ALLOCATION_FAILURE, HEADER_PREFIX_SIZE);
    }
    catch (const std::length_error&)
    {
        return Fail(Error::ALLOCATION_FAILURE, HEADER_PREFIX_SIZE);
    }

    Error metadata_error = Error::NONE;
    if (!ValidateMetadata(m_metadata, m_limits, metadata_error))
        return Fail(metadata_error, 24);
    m_chain_digest = stored_digest;
    m_next_physics_step = m_metadata.first_physics_step;
    return true;
}

bool Reader::ReadFrame(
    const std::uint8_t prefix[FRAME_PREFIX_SIZE],
    Frame& frame)
{
    const std::uint64_t frame_offset =
        m_bytes_read - FRAME_PREFIX_SIZE;
    const std::uint32_t record_size = LoadU32(prefix + 4);
    const std::uint32_t event_count = LoadU32(prefix + 16);
    if (event_count > m_limits.max_events_per_step)
        return Fail(Error::EVENT_LIMIT_EXCEEDED, frame_offset + 16);
    const std::uint64_t expected_size =
        static_cast<std::uint64_t>(FRAME_MIN_SIZE) +
        static_cast<std::uint64_t>(event_count) * EVENT_SIZE;
    if (record_size != expected_size)
        return Fail(Error::INVALID_RECORD_SIZE, frame_offset + 4);
    if (record_size > m_limits.max_bytes - frame_offset ||
        TRAILER_SIZE >
            m_limits.max_bytes - frame_offset - record_size)
    {
        return Fail(Error::BYTE_LIMIT_EXCEEDED, frame_offset + 4);
    }
    if (event_count > m_limits.max_events - m_event_count)
        return Fail(Error::EVENT_LIMIT_EXCEEDED, frame_offset + 16);

    const std::size_t event_bytes =
        static_cast<std::size_t>(event_count) * EVENT_SIZE;
    std::vector<std::uint8_t> remainder;
    try
    {
        remainder.resize(event_bytes + DIGEST_SIZE, 0);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_read);
    }
    catch (const std::length_error&)
    {
        return Fail(Error::ALLOCATION_FAILURE, m_bytes_read);
    }
    if (!ReadBytes(remainder.data(), remainder.size()))
        return false;

    Digest stored_digest;
    std::memcpy(
        stored_digest.bytes.data(),
        remainder.data() + event_bytes,
        stored_digest.bytes.size());
    const Digest computed_digest =
        HashFrame(
            m_chain_digest,
            prefix,
            remainder.data(),
            event_bytes);
    if (!DigestEquals(stored_digest, computed_digest))
    {
        return Fail(
            Error::INTEGRITY_MISMATCH,
            frame_offset + FRAME_PREFIX_SIZE + event_bytes);
    }
    if (LoadU32(prefix + 20) != 0)
        return Fail(Error::RESERVED_FIELD_NONZERO, frame_offset + 20);
    const std::uint64_t physics_step = LoadU64(prefix + 8);
    if (physics_step != m_next_physics_step)
        return Fail(Error::NON_CONTIGUOUS_STEP, frame_offset + 8);
    if (physics_step == std::numeric_limits<std::uint64_t>::max())
        return Fail(Error::ARITHMETIC_OVERFLOW, frame_offset + 8);
    if (m_step_count >= m_limits.max_steps)
        return Fail(Error::STEP_LIMIT_EXCEEDED, frame_offset);

    Frame parsed;
    parsed.physics_step = physics_step;
    try
    {
        parsed.events.resize(event_count);
    }
    catch (const std::bad_alloc&)
    {
        return Fail(Error::ALLOCATION_FAILURE, frame_offset + 16);
    }
    catch (const std::length_error&)
    {
        return Fail(Error::ALLOCATION_FAILURE, frame_offset + 16);
    }
    for (std::size_t index = 0; index < parsed.events.size(); ++index)
    {
        const std::uint8_t* encoded =
            remainder.data() + index * EVENT_SIZE;
        parsed.events[index].target_id = LoadU64(encoded);
        parsed.events[index].control_id = LoadU32(encoded + 8);
        parsed.events[index].kind =
            static_cast<EventKind>(LoadU32(encoded + 12));
        parsed.events[index].value =
            Binary64FromBits(LoadU64(encoded + 16));
    }

    Error replay_error = Error::NONE;
    std::uint32_t replay_error_index = 0;
    if (!m_replay_state.Apply(
            parsed,
            m_limits.max_active_controls,
            &replay_error,
            &replay_error_index))
    {
        return Fail(
            replay_error,
            frame_offset + FRAME_PREFIX_SIZE +
                static_cast<std::uint64_t>(replay_error_index) * EVENT_SIZE,
            replay_error_index);
    }

    frame.physics_step = parsed.physics_step;
    frame.events.swap(parsed.events);
    m_chain_digest = stored_digest;
    ++m_step_count;
    m_event_count += event_count;
    ++m_next_physics_step;
    return true;
}

bool Reader::ReadAndValidateTrailer(const std::uint8_t prefix[8])
{
    const std::uint64_t trailer_offset = m_bytes_read - 8;
    if (LoadU32(prefix + 4) != TRAILER_SIZE)
        return Fail(Error::INVALID_RECORD_SIZE, trailer_offset + 4);
    if (TRAILER_SIZE > m_limits.max_bytes - trailer_offset)
        return Fail(Error::BYTE_LIMIT_EXCEEDED, trailer_offset);

    std::array<std::uint8_t, TRAILER_SIZE> trailer = {};
    std::memcpy(trailer.data(), prefix, 8);
    if (!ReadBytes(
            trailer.data() + 8,
            trailer.size() - 8))
    {
        return false;
    }

    Digest stored_trailer_digest;
    std::memcpy(
        stored_trailer_digest.bytes.data(),
        trailer.data() + 72,
        stored_trailer_digest.bytes.size());
    const Digest computed_trailer_digest =
        HashTrailer(trailer.data());
    if (!DigestEquals(stored_trailer_digest, computed_trailer_digest))
    {
        return Fail(
            Error::INTEGRITY_MISMATCH,
            trailer_offset + 72);
    }
    Digest stored_final_chain;
    std::memcpy(
        stored_final_chain.bytes.data(),
        trailer.data() + 40,
        stored_final_chain.bytes.size());
    if (LoadU64(trailer.data() + 8) != m_step_count ||
        LoadU64(trailer.data() + 16) != m_event_count ||
        LoadU64(trailer.data() + 24) != m_next_physics_step ||
        LoadU32(trailer.data() + 32) !=
            m_replay_state.GetControlCount() ||
        !DigestEquals(stored_final_chain, m_chain_digest))
    {
        return Fail(Error::SUMMARY_MISMATCH, trailer_offset + 8);
    }
    if (LoadU32(trailer.data() + 36) != 0)
        return Fail(Error::RESERVED_FIELD_NONZERO, trailer_offset + 36);

    char trailing = 0;
    std::streamsize received = 0;
    try
    {
        m_input.read(&trailing, 1);
        received = m_input.gcount();
    }
    catch (const std::ios_base::failure&)
    {
        received = m_input.gcount();
        if (received == 0 && m_input.eof())
        {
            m_chain_digest = stored_trailer_digest;
            m_finished = true;
            return true;
        }
        if (received > 0)
            ++m_bytes_read;
        if (m_input.bad() && !m_input.eof())
            return Fail(Error::IO_FAILURE, m_bytes_read);
        return Fail(Error::TRAILING_DATA, m_bytes_read - 1U);
    }
    if (received != 0)
    {
        ++m_bytes_read;
        return Fail(Error::TRAILING_DATA, m_bytes_read - 1U);
    }
    if (m_input.bad() && !m_input.eof())
        return Fail(Error::IO_FAILURE, m_bytes_read);

    m_chain_digest = stored_trailer_digest;
    m_finished = true;
    return true;
}

ReadResult Reader::ReadNext(Frame& frame)
{
    if (m_status.error != Error::NONE)
        return ReadResult::READ_ERROR;
    if (m_finished)
        return ReadResult::END;

    std::array<std::uint8_t, FRAME_PREFIX_SIZE> prefix = {};
    if (!ReadBytes(prefix.data(), 8))
        return ReadResult::READ_ERROR;
    const std::uint32_t tag = LoadU32(prefix.data());
    if (tag == END_TAG)
    {
        if (!ReadAndValidateTrailer(prefix.data()))
            return ReadResult::READ_ERROR;
        return ReadResult::END;
    }
    if (tag != FRAME_TAG)
    {
        Fail(Error::INVALID_RECORD_TAG, m_bytes_read - 8);
        return ReadResult::READ_ERROR;
    }
    if (!ReadBytes(prefix.data() + 8, FRAME_PREFIX_SIZE - 8))
        return ReadResult::READ_ERROR;
    if (!ReadFrame(prefix.data(), frame))
        return ReadResult::READ_ERROR;
    return ReadResult::FRAME;
}

const Metadata& Reader::GetMetadata() const
{
    return m_metadata;
}

const Status& Reader::GetStatus() const
{
    return m_status;
}

const ReplayState& Reader::GetReplayState() const
{
    return m_replay_state;
}

const Digest& Reader::GetTraceDigest() const
{
    return m_chain_digest;
}

std::uint64_t Reader::GetStepCount() const
{
    return m_step_count;
}

std::uint64_t Reader::GetEventCount() const
{
    return m_event_count;
}

std::uint64_t Reader::GetBytesRead() const
{
    return m_bytes_read;
}

std::uint64_t Reader::GetNextPhysicsStep() const
{
    return m_next_physics_step;
}

ComparisonResult::ComparisonResult():
    status(ComparisonStatus::MATCH),
    difference(Difference::NONE),
    metadata_field(MetadataField::NONE),
    left_error(),
    right_error(),
    steps_compared(0),
    first_divergent_step(0),
    first_divergent_event(0),
    has_first_divergent_step(false),
    has_first_divergent_event(false)
{
}

ComparisonResult Compare(
    std::istream& left_stream,
    std::istream& right_stream,
    const Limits& limits)
{
    Reader left(left_stream, limits);
    Reader right(right_stream, limits);
    ComparisonResult result;

    if (left.GetStatus().error == Error::NONE &&
        right.GetStatus().error == Error::NONE)
    {
        MetadataField field = MetadataField::NONE;
        if (!MetadataEqual(
                left.GetMetadata(),
                right.GetMetadata(),
                field))
        {
            result.status = ComparisonStatus::DIVERGED;
            result.difference = Difference::METADATA;
            result.metadata_field = field;
        }
    }

    bool left_terminal = left.GetStatus().error != Error::NONE;
    bool right_terminal = right.GetStatus().error != Error::NONE;
    ReadResult left_result =
        left_terminal ? ReadResult::READ_ERROR : ReadResult::END;
    ReadResult right_result =
        right_terminal ? ReadResult::READ_ERROR : ReadResult::END;
    Frame left_frame;
    Frame right_frame;

    while (!left_terminal || !right_terminal)
    {
        if (!left_terminal)
        {
            left_result = left.ReadNext(left_frame);
            left_terminal = left_result != ReadResult::FRAME;
        }
        if (!right_terminal)
        {
            right_result = right.ReadNext(right_frame);
            right_terminal = right_result != ReadResult::FRAME;
        }

        if (left_result == ReadResult::FRAME &&
            right_result == ReadResult::FRAME &&
            result.difference == Difference::NONE)
        {
            if (left_frame.physics_step != right_frame.physics_step)
            {
                result.status = ComparisonStatus::DIVERGED;
                result.difference = Difference::PHYSICS_STEP;
                result.first_divergent_step =
                    std::min(
                        left_frame.physics_step,
                        right_frame.physics_step);
                result.has_first_divergent_step = true;
            }
            else if (left_frame.events.size() !=
                     right_frame.events.size())
            {
                result.status = ComparisonStatus::DIVERGED;
                result.difference = Difference::EVENT_COUNT;
                result.first_divergent_step =
                    left_frame.physics_step;
                result.has_first_divergent_step = true;
            }
            else
            {
                for (std::size_t index = 0;
                     index < left_frame.events.size();
                     ++index)
                {
                    const Event& left_event = left_frame.events[index];
                    const Event& right_event = right_frame.events[index];
                    Difference difference = Difference::NONE;
                    if (left_event.target_id != right_event.target_id ||
                        left_event.control_id != right_event.control_id)
                    {
                        difference = Difference::EVENT_KEY;
                    }
                    else if (left_event.kind != right_event.kind)
                    {
                        difference = Difference::EVENT_KIND;
                    }
                    else if (ExactBinary64Bits(left_event.value) !=
                             ExactBinary64Bits(right_event.value))
                    {
                        difference = Difference::EVENT_VALUE;
                    }
                    if (difference != Difference::NONE)
                    {
                        result.status = ComparisonStatus::DIVERGED;
                        result.difference = difference;
                        result.first_divergent_step =
                            left_frame.physics_step;
                        result.first_divergent_event =
                            static_cast<std::uint32_t>(index);
                        result.has_first_divergent_step = true;
                        result.has_first_divergent_event = true;
                        break;
                    }
                }
            }
            if (result.difference == Difference::NONE)
                ++result.steps_compared;
        }
        else if (result.difference == Difference::NONE &&
                 left_result != right_result &&
                 left_result != ReadResult::READ_ERROR &&
                 right_result != ReadResult::READ_ERROR)
        {
            result.status = ComparisonStatus::DIVERGED;
            result.difference = Difference::TRACE_LENGTH;
            const Frame& remaining =
                left_result == ReadResult::FRAME ?
                    left_frame : right_frame;
            result.first_divergent_step = remaining.physics_step;
            result.has_first_divergent_step = true;
        }
    }

    result.left_error = left.GetStatus();
    result.right_error = right.GetStatus();
    const bool left_invalid =
        result.left_error.error != Error::NONE;
    const bool right_invalid =
        result.right_error.error != Error::NONE;
    if (left_invalid || right_invalid)
    {
        result.status = ComparisonStatus::INVALID_INPUT;
        if (left_invalid && right_invalid)
            result.difference = Difference::BOTH_INVALID;
        else if (left_invalid)
            result.difference = Difference::LEFT_INVALID;
        else
            result.difference = Difference::RIGHT_INVALID;
        result.metadata_field = MetadataField::NONE;
        result.has_first_divergent_step = false;
        result.has_first_divergent_event = false;
    }
    return result;
}

const char* ToString(Error error)
{
    switch (error)
    {
    case Error::NONE: return "none";
    case Error::INVALID_METADATA: return "invalid_metadata";
    case Error::INVALID_IDENTITY_STRING:
        return "invalid_identity_string";
    case Error::IO_FAILURE: return "io_failure";
    case Error::TRUNCATED: return "truncated";
    case Error::MAGIC_MISMATCH: return "magic_mismatch";
    case Error::UNSUPPORTED_SCHEMA: return "unsupported_schema";
    case Error::INVALID_HEADER_SIZE: return "invalid_header_size";
    case Error::INVALID_SEMANTICS: return "invalid_semantics";
    case Error::RESERVED_FIELD_NONZERO:
        return "reserved_field_nonzero";
    case Error::INTEGRITY_MISMATCH: return "integrity_mismatch";
    case Error::INVALID_RECORD_TAG: return "invalid_record_tag";
    case Error::INVALID_RECORD_SIZE: return "invalid_record_size";
    case Error::NON_CONTIGUOUS_STEP: return "non_contiguous_step";
    case Error::UNKNOWN_EVENT_KIND: return "unknown_event_kind";
    case Error::INVALID_CONTROL_ID: return "invalid_control_id";
    case Error::NON_FINITE_VALUE: return "non_finite_value";
    case Error::NON_CANONICAL_VALUE: return "non_canonical_value";
    case Error::NON_CANONICAL_EVENT_ORDER:
        return "non_canonical_event_order";
    case Error::REDUNDANT_STATE_EVENT: return "redundant_state_event";
    case Error::STEP_LIMIT_EXCEEDED: return "step_limit_exceeded";
    case Error::EVENT_LIMIT_EXCEEDED: return "event_limit_exceeded";
    case Error::ACTIVE_CONTROL_LIMIT_EXCEEDED:
        return "active_control_limit_exceeded";
    case Error::BYTE_LIMIT_EXCEEDED: return "byte_limit_exceeded";
    case Error::STRING_LIMIT_EXCEEDED: return "string_limit_exceeded";
    case Error::ARITHMETIC_OVERFLOW: return "arithmetic_overflow";
    case Error::ALLOCATION_FAILURE: return "allocation_failure";
    case Error::SUMMARY_MISMATCH: return "summary_mismatch";
    case Error::TRAILING_DATA: return "trailing_data";
    case Error::ALREADY_FINISHED: return "already_finished";
    }
    return "unknown";
}

const char* ToString(Difference difference)
{
    switch (difference)
    {
    case Difference::NONE: return "none";
    case Difference::METADATA: return "metadata";
    case Difference::PHYSICS_STEP: return "physics_step";
    case Difference::EVENT_COUNT: return "event_count";
    case Difference::EVENT_KEY: return "event_key";
    case Difference::EVENT_KIND: return "event_kind";
    case Difference::EVENT_VALUE: return "event_value";
    case Difference::TRACE_LENGTH: return "trace_length";
    case Difference::LEFT_INVALID: return "left_invalid";
    case Difference::RIGHT_INVALID: return "right_invalid";
    case Difference::BOTH_INVALID: return "both_invalid";
    }
    return "unknown";
}

const char* ToString(MetadataField field)
{
    switch (field)
    {
    case MetadataField::NONE: return "none";
    case MetadataField::SEMANTICS: return "semantics";
    case MetadataField::SCENARIO_ID: return "scenario_id";
    case MetadataField::STREAM_ID: return "stream_id";
    case MetadataField::FIRST_PHYSICS_STEP:
        return "first_physics_step";
    case MetadataField::PHYSICS_STEP_RATE:
        return "physics_step_rate";
    case MetadataField::SCENARIO_NAME: return "scenario_name";
    case MetadataField::SOURCE_NAME: return "source_name";
    case MetadataField::SOURCE_DIGEST: return "source_digest";
    }
    return "unknown";
}

} // namespace DeterministicInputTrace
} // namespace RoR
