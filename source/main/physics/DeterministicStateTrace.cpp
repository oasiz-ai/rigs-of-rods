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

#include "DeterministicStateTrace.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>

namespace {

const std::uint8_t TRACE_MAGIC[16] = {
    'R', 'o', 'R', '-', 'D', '0', '-', 'T',
    'r', 'a', 'c', 'e', '\r', '\n', UINT8_C(0x1a), '\n'
};

const std::uint32_t STEP_TAG = UINT32_C(0x50455453); // "STEP"
const std::uint32_t END_TAG = UINT32_C(0x21444e45);  // "END!"
const std::uint32_t CRC32_INITIAL = UINT32_C(0xffffffff);

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
        value |= static_cast<std::uint32_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t LoadU64(const std::uint8_t* source)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(source[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t UpdateCrc32(
    std::uint32_t state,
    const std::uint8_t* data,
    std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        state ^= data[index];
        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(state & UINT32_C(1)));
            state = (state >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return state;
}

std::uint32_t FinalizeCrc32(std::uint32_t state)
{
    return state ^ UINT32_C(0xffffffff);
}

std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t size)
{
    return FinalizeCrc32(UpdateCrc32(CRC32_INITIAL, data, size));
}

bool AddWouldOverflow(std::uint64_t left, std::uint64_t right)
{
    return left > std::numeric_limits<std::uint64_t>::max() - right;
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

RoR::DeterministicStateTrace::Limits ClampLimits(
    const RoR::DeterministicStateTrace::Limits& requested)
{
    using namespace RoR::DeterministicStateTrace;

    Limits limits = requested;
    limits.max_steps = std::min(limits.max_steps, MAX_TRACE_STEPS);
    limits.max_bytes = std::min(limits.max_bytes, MAX_TRACE_BYTES);
    return limits;
}

bool IsValidMetadata(
    const RoR::DeterministicStateTrace::Metadata& metadata)
{
    using namespace RoR;
    using namespace RoR::DeterministicStateTrace;

    if (metadata.state_digest_schema_version !=
        STATE_DIGEST_SCHEMA_VERSION)
    {
        return false;
    }
    if (metadata.worker_count == 0 || metadata.worker_count > MAX_WORKERS)
        return false;
    if (metadata.physics_step_numerator == 0 ||
        metadata.physics_step_denominator == 0 ||
        metadata.physics_step_numerator > metadata.physics_step_denominator ||
        metadata.physics_step_numerator > MAX_PHYSICS_RATE_COMPONENT ||
        metadata.physics_step_denominator > MAX_PHYSICS_RATE_COMPONENT)
    {
        return false;
    }
    if (GreatestCommonDivisor(
            metadata.physics_step_numerator,
            metadata.physics_step_denominator) != 1)
    {
        return false;
    }
    return (metadata.physics_flags & ~PHYSICS_FLAG_MASK) == 0;
}

bool DigestsEqual(
    const RoR::DeterministicStateDigest::Digest& left,
    const RoR::DeterministicStateDigest::Digest& right)
{
    return left.bytes == right.bytes;
}

bool IsZeroInputDigest(
    const std::array<std::uint8_t,
        RoR::DeterministicStateTrace::INPUT_DIGEST_SIZE>& digest)
{
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        if (digest[index] != 0U)
            return false;
    }
    return true;
}

bool IsValidInputBinding(
    const RoR::DeterministicStateTrace::StepRecord& record)
{
    using namespace RoR::DeterministicStateTrace;
    if ((record.input_flags & ~STEP_INPUT_FLAG_MASK) != 0U)
        return false;
    return (record.input_flags & STEP_INPUT_AUTHENTICATED_PREFIX) != 0U ||
        IsZeroInputDigest(record.input_digest);
}

void AppendJsonString(std::ostringstream& stream, const std::string& value)
{
    static const char HEX[] = "0123456789abcdef";

    stream << '"';
    for (std::string::const_iterator iterator = value.begin();
         iterator != value.end();
         ++iterator)
    {
        const unsigned char byte = static_cast<unsigned char>(*iterator);
        switch (byte)
        {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (byte >= 0x20 && byte <= 0x7e)
            {
                stream << static_cast<char>(byte);
            }
            else
            {
                stream << "\\u00"
                       << HEX[(byte >> 4U) & 0xfU]
                       << HEX[byte & 0xfU];
            }
            break;
        }
    }
    stream << '"';
}

std::string DigestHex(const RoR::DeterministicStateDigest::Digest& digest)
{
    static const char HEX[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.bytes.size() * 2);
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
    {
        const std::uint8_t byte = digest.bytes[index];
        result.push_back(HEX[(byte >> 4U) & 0xfU]);
        result.push_back(HEX[byte & 0xfU]);
    }
    return result;
}

std::string InputDigestHex(
    const std::array<std::uint8_t,
        RoR::DeterministicStateTrace::INPUT_DIGEST_SIZE>& digest)
{
    static const char HEX[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2U);
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        result.push_back(HEX[(digest[index] >> 4U) & 0xfU]);
        result.push_back(HEX[digest[index] & 0xfU]);
    }
    return result;
}

void AppendMetadataJson(
    std::ostringstream& stream,
    const RoR::DeterministicStateTrace::Metadata& metadata)
{
    stream << '{'
           << "\"digest_schema_version\":"
           << metadata.state_digest_schema_version
           << ",\"worker_count\":" << metadata.worker_count
           << ",\"scenario_id\":" << metadata.scenario_id
           << ",\"first_physics_step\":" << metadata.first_physics_step
           << ",\"physics_step_numerator\":"
           << metadata.physics_step_numerator
           << ",\"physics_step_denominator\":"
           << metadata.physics_step_denominator
           << ",\"physics_flags\":" << metadata.physics_flags
           << '}';
}

void AppendStepJson(
    std::ostringstream& stream,
    const RoR::DeterministicStateTrace::StepRecord& step)
{
    stream << '{'
           << "\"physics_step\":" << step.physics_step
           << ",\"actor_count\":" << step.actor_count
           << ",\"contact_count\":" << step.contact_count
           << ",\"input_digest\":";
    if ((step.input_flags &
            RoR::DeterministicStateTrace::
                STEP_INPUT_AUTHENTICATED_PREFIX) != 0U)
    {
        AppendJsonString(stream, InputDigestHex(step.input_digest));
    }
    else
    {
        stream << "null";
    }
    stream
           << ",\"digest\":";
    AppendJsonString(stream, DigestHex(step.digest));
    stream << '}';
}

void AppendStatusJson(
    std::ostringstream& stream,
    const RoR::DeterministicStateTrace::Status& status)
{
    stream << "{\"code\":";
    AppendJsonString(
        stream,
        RoR::DeterministicStateTrace::ToString(status.error));
    stream << ",\"byte_offset\":" << status.byte_offset
           << ",\"step_index\":" << status.step_index
           << '}';
}

} // namespace

namespace RoR {
namespace DeterministicStateTrace {

Metadata::Metadata():
    state_digest_schema_version(STATE_DIGEST_SCHEMA_VERSION),
    worker_count(1),
    scenario_id(0),
    first_physics_step(0),
    physics_step_numerator(1),
    physics_step_denominator(2000),
    physics_flags(0)
{
}

StepRecord::StepRecord():
    physics_step(0),
    actor_count(0),
    contact_count(0),
    digest(),
    input_flags(0),
    input_digest()
{
}

Limits::Limits():
    max_steps(MAX_TRACE_STEPS),
    max_bytes(MAX_TRACE_BYTES)
{
}

Status::Status():
    error(Error::NONE),
    byte_offset(0),
    step_index(0)
{
}

Writer::Writer(
    std::ostream& output,
    const Metadata& metadata,
    const Limits& limits):
    m_output(output),
    m_metadata(metadata),
    m_limits(ClampLimits(limits)),
    m_status(),
    m_step_count(0),
    m_bytes_written(0),
    m_total_actor_count(0),
    m_total_contact_count(0),
    m_aggregate_crc_state(CRC32_INITIAL),
    m_finished(false)
{
    if (!IsValidMetadata(m_metadata))
    {
        Fail(Error::INVALID_METADATA, 0);
        return;
    }
    if (m_limits.max_bytes <
        static_cast<std::uint64_t>(HEADER_SIZE + TRAILER_SIZE))
    {
        Fail(Error::BYTE_LIMIT_EXCEEDED, 0);
        return;
    }

    std::array<std::uint8_t, HEADER_SIZE> header = {};
    std::memcpy(header.data(), TRACE_MAGIC, sizeof(TRACE_MAGIC));
    StoreU32(header.data() + 16, SCHEMA_VERSION);
    StoreU32(header.data() + 20, HEADER_SIZE);
    StoreU32(
        header.data() + 24,
        m_metadata.state_digest_schema_version);
    StoreU32(header.data() + 28, m_metadata.worker_count);
    StoreU64(header.data() + 32, m_metadata.scenario_id);
    StoreU64(header.data() + 40, m_metadata.first_physics_step);
    StoreU64(header.data() + 48, m_metadata.physics_step_numerator);
    StoreU64(header.data() + 56, m_metadata.physics_step_denominator);
    StoreU32(header.data() + 64, m_metadata.physics_flags);
    StoreU32(header.data() + 68, 0);
    StoreU32(header.data() + 72, 0);
    StoreU32(
        header.data() + 76,
        ComputeCrc32(header.data(), HEADER_SIZE - 4));
    WriteBlock(
        header.data(),
        header.size(),
        header.size() - 4);
}

bool Writer::IsReady() const
{
    return m_status.error == Error::NONE && !m_finished;
}

bool Writer::Fail(Error error, std::uint64_t byte_offset)
{
    if (m_status.error == Error::NONE)
    {
        m_status.error = error;
        m_status.byte_offset = byte_offset;
        m_status.step_index = m_step_count;
    }
    return false;
}

bool Writer::WriteBlock(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t aggregate_payload_size)
{
    if (m_status.error != Error::NONE)
        return false;
    if (aggregate_payload_size > size)
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
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

    if (aggregate_payload_size != 0)
    {
        m_aggregate_crc_state =
            UpdateCrc32(
                m_aggregate_crc_state,
                data,
                aggregate_payload_size);
    }
    m_bytes_written += static_cast<std::uint64_t>(size);
    return true;
}

bool Writer::Append(const StepRecord& record)
{
    if (m_status.error != Error::NONE)
        return false;
    if (m_finished)
        return Fail(Error::ALREADY_FINISHED, m_bytes_written);
    if (m_step_count >= m_limits.max_steps)
        return Fail(Error::STEP_LIMIT_EXCEEDED, m_bytes_written);
    if (AddWouldOverflow(
            m_metadata.first_physics_step,
            m_step_count))
    {
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    }

    const std::uint64_t expected_step =
        m_metadata.first_physics_step + m_step_count;
    if (record.physics_step != expected_step)
        return Fail(Error::NON_CONTIGUOUS_STEP, m_bytes_written);
    if (record.actor_count > DeterministicStateDigest::MAX_ACTORS ||
        record.contact_count > DeterministicStateDigest::MAX_CONTACTS)
    {
        return Fail(Error::COUNT_LIMIT_EXCEEDED, m_bytes_written);
    }
    if (!IsValidInputBinding(record))
        return Fail(Error::INVALID_INPUT_BINDING, m_bytes_written);
    if (AddWouldOverflow(m_total_actor_count, record.actor_count) ||
        AddWouldOverflow(m_total_contact_count, record.contact_count))
    {
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    }

    std::array<std::uint8_t, STEP_RECORD_SIZE> frame = {};
    StoreU32(frame.data(), STEP_TAG);
    StoreU32(frame.data() + 4, STEP_RECORD_SIZE);
    StoreU64(frame.data() + 8, record.physics_step);
    StoreU32(frame.data() + 16, record.actor_count);
    StoreU32(frame.data() + 20, record.contact_count);
    std::memcpy(
        frame.data() + 24,
        record.digest.bytes.data(),
        record.digest.bytes.size());
    StoreU32(frame.data() + 56, record.input_flags);
    std::memcpy(
        frame.data() + 60,
        record.input_digest.data(),
        record.input_digest.size());
    StoreU32(
        frame.data() + 92,
        ComputeCrc32(frame.data(), STEP_RECORD_SIZE - 4));

    if (!WriteBlock(
            frame.data(),
            frame.size(),
            frame.size() - 4))
        return false;
    ++m_step_count;
    m_total_actor_count += record.actor_count;
    m_total_contact_count += record.contact_count;
    return true;
}

bool Writer::Finish()
{
    if (m_status.error != Error::NONE)
        return false;
    if (m_finished)
        return Fail(Error::ALREADY_FINISHED, m_bytes_written);
    if (AddWouldOverflow(
            m_metadata.first_physics_step,
            m_step_count))
    {
        return Fail(Error::ARITHMETIC_OVERFLOW, m_bytes_written);
    }

    std::array<std::uint8_t, TRAILER_SIZE> trailer = {};
    StoreU32(trailer.data(), END_TAG);
    StoreU32(trailer.data() + 4, TRAILER_SIZE);
    StoreU64(trailer.data() + 8, m_step_count);
    StoreU64(
        trailer.data() + 16,
        m_metadata.first_physics_step + m_step_count);
    StoreU64(trailer.data() + 24, m_total_actor_count);
    StoreU64(trailer.data() + 32, m_total_contact_count);
    StoreU32(
        trailer.data() + 40,
        FinalizeCrc32(m_aggregate_crc_state));
    StoreU32(trailer.data() + 44, 0);
    StoreU32(trailer.data() + 48, 0);
    StoreU32(
        trailer.data() + 52,
        ComputeCrc32(trailer.data(), TRAILER_SIZE - 4));

    if (!WriteBlock(trailer.data(), trailer.size(), 0))
        return false;
    try
    {
        m_output.flush();
    }
    catch (const std::ios_base::failure&)
    {
        return Fail(Error::IO_FAILURE, m_bytes_written);
    }
    if (!m_output.good())
        return Fail(Error::IO_FAILURE, m_bytes_written);

    m_finished = true;
    return true;
}

const Status& Writer::GetStatus() const
{
    return m_status;
}

std::uint64_t Writer::GetStepCount() const
{
    return m_step_count;
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
    m_step_count(0),
    m_bytes_read(0),
    m_total_actor_count(0),
    m_total_contact_count(0),
    m_aggregate_crc_state(CRC32_INITIAL),
    m_finished(false)
{
    if (m_limits.max_bytes <
        static_cast<std::uint64_t>(HEADER_SIZE + TRAILER_SIZE))
    {
        Fail(Error::BYTE_LIMIT_EXCEEDED, 0);
        return;
    }

    std::array<std::uint8_t, HEADER_SIZE> header = {};
    if (!ReadBlock(header.data(), header.size()))
        return;
    if (!std::equal(
            header.begin(),
            header.begin() + sizeof(TRACE_MAGIC),
            TRACE_MAGIC))
    {
        Fail(Error::MAGIC_MISMATCH, 0);
        return;
    }
    if (LoadU32(header.data() + 16) != SCHEMA_VERSION)
    {
        Fail(Error::UNSUPPORTED_SCHEMA, 16);
        return;
    }
    if (LoadU32(header.data() + 20) != HEADER_SIZE)
    {
        Fail(Error::INVALID_HEADER_SIZE, 20);
        return;
    }
    if (LoadU32(header.data() + 76) !=
        ComputeCrc32(header.data(), HEADER_SIZE - 4))
    {
        Fail(Error::CHECKSUM_MISMATCH, 76);
        return;
    }
    if (LoadU32(header.data() + 24) !=
        STATE_DIGEST_SCHEMA_VERSION)
    {
        Fail(Error::DIGEST_SCHEMA_MISMATCH, 24);
        return;
    }
    if (LoadU32(header.data() + 68) != 0 ||
        LoadU32(header.data() + 72) != 0)
    {
        Fail(Error::RESERVED_FIELD_NONZERO, 68);
        return;
    }

    m_metadata.state_digest_schema_version =
        LoadU32(header.data() + 24);
    m_metadata.worker_count = LoadU32(header.data() + 28);
    m_metadata.scenario_id = LoadU64(header.data() + 32);
    m_metadata.first_physics_step = LoadU64(header.data() + 40);
    m_metadata.physics_step_numerator = LoadU64(header.data() + 48);
    m_metadata.physics_step_denominator = LoadU64(header.data() + 56);
    m_metadata.physics_flags = LoadU32(header.data() + 64);
    if (!IsValidMetadata(m_metadata))
    {
        Fail(Error::INVALID_METADATA, 24);
        return;
    }
    m_aggregate_crc_state =
        UpdateCrc32(
            m_aggregate_crc_state,
            header.data(),
            header.size() - 4);
}

bool Reader::IsReady() const
{
    return m_status.error == Error::NONE;
}

bool Reader::Fail(Error error, std::uint64_t byte_offset)
{
    if (m_status.error == Error::NONE)
    {
        m_status.error = error;
        m_status.byte_offset = byte_offset;
        m_status.step_index = m_step_count;
    }
    return false;
}

bool Reader::ReadBlock(std::uint8_t* data, std::size_t size)
{
    if (m_status.error != Error::NONE)
        return false;
    if (size > m_limits.max_bytes - m_bytes_read)
        return Fail(Error::BYTE_LIMIT_EXCEEDED, m_bytes_read);

    const std::uint64_t start_offset = m_bytes_read;
    std::streamsize observed = 0;
    try
    {
        m_input.read(
            reinterpret_cast<char*>(data),
            static_cast<std::streamsize>(size));
        observed = m_input.gcount();
    }
    catch (const std::ios_base::failure&)
    {
        observed = m_input.gcount();
        if (observed > 0)
            m_bytes_read += static_cast<std::uint64_t>(observed);
        return Fail(
            m_input.eof() ? Error::TRUNCATED : Error::IO_FAILURE,
            start_offset);
    }
    if (observed > 0)
        m_bytes_read += static_cast<std::uint64_t>(observed);
    if (observed != static_cast<std::streamsize>(size))
    {
        return Fail(
            m_input.bad() ? Error::IO_FAILURE : Error::TRUNCATED,
            start_offset);
    }
    return true;
}

ReadResult Reader::ReadNext(StepRecord& record)
{
    if (m_status.error != Error::NONE)
        return ReadResult::READ_ERROR;
    if (m_finished)
        return ReadResult::END;

    const std::uint64_t record_offset = m_bytes_read;
    std::array<std::uint8_t, 8> prefix = {};
    if (!ReadBlock(prefix.data(), prefix.size()))
        return ReadResult::READ_ERROR;

    const std::uint32_t tag = LoadU32(prefix.data());
    const std::uint32_t size = LoadU32(prefix.data() + 4);
    if (tag == STEP_TAG)
    {
        if (size != STEP_RECORD_SIZE)
        {
            Fail(Error::INVALID_RECORD_SIZE, record_offset + 4);
            return ReadResult::READ_ERROR;
        }
        if (m_step_count >= m_limits.max_steps)
        {
            Fail(Error::STEP_LIMIT_EXCEEDED, record_offset);
            return ReadResult::READ_ERROR;
        }

        std::array<std::uint8_t, STEP_RECORD_SIZE> frame = {};
        std::memcpy(frame.data(), prefix.data(), prefix.size());
        if (!ReadBlock(
                frame.data() + prefix.size(),
                frame.size() - prefix.size()))
        {
            return ReadResult::READ_ERROR;
        }
        if (LoadU32(frame.data() + 92) !=
            ComputeCrc32(frame.data(), STEP_RECORD_SIZE - 4))
        {
            Fail(Error::CHECKSUM_MISMATCH, record_offset + 92);
            return ReadResult::READ_ERROR;
        }
        if (AddWouldOverflow(
                m_metadata.first_physics_step,
                m_step_count))
        {
            Fail(Error::ARITHMETIC_OVERFLOW, record_offset + 8);
            return ReadResult::READ_ERROR;
        }

        const std::uint64_t physics_step = LoadU64(frame.data() + 8);
        if (physics_step !=
            m_metadata.first_physics_step + m_step_count)
        {
            Fail(Error::NON_CONTIGUOUS_STEP, record_offset + 8);
            return ReadResult::READ_ERROR;
        }
        const std::uint32_t actor_count = LoadU32(frame.data() + 16);
        const std::uint32_t contact_count = LoadU32(frame.data() + 20);
        if (actor_count > DeterministicStateDigest::MAX_ACTORS ||
            contact_count > DeterministicStateDigest::MAX_CONTACTS)
        {
            Fail(Error::COUNT_LIMIT_EXCEEDED, record_offset + 16);
            return ReadResult::READ_ERROR;
        }
        if (AddWouldOverflow(m_total_actor_count, actor_count) ||
            AddWouldOverflow(m_total_contact_count, contact_count))
        {
            Fail(Error::ARITHMETIC_OVERFLOW, record_offset + 16);
            return ReadResult::READ_ERROR;
        }

        StepRecord completed;
        completed.physics_step = physics_step;
        completed.actor_count = actor_count;
        completed.contact_count = contact_count;
        std::memcpy(
            completed.digest.bytes.data(),
            frame.data() + 24,
            completed.digest.bytes.size());
        completed.input_flags = LoadU32(frame.data() + 56);
        std::memcpy(
            completed.input_digest.data(),
            frame.data() + 60,
            completed.input_digest.size());
        if (!IsValidInputBinding(completed))
        {
            Fail(Error::INVALID_INPUT_BINDING, record_offset + 56);
            return ReadResult::READ_ERROR;
        }
        m_aggregate_crc_state =
            UpdateCrc32(
                m_aggregate_crc_state,
                frame.data(),
                frame.size() - 4);
        ++m_step_count;
        m_total_actor_count += actor_count;
        m_total_contact_count += contact_count;
        record = completed;
        return ReadResult::STEP;
    }

    if (tag == END_TAG)
    {
        if (size != TRAILER_SIZE)
        {
            Fail(Error::INVALID_RECORD_SIZE, record_offset + 4);
            return ReadResult::READ_ERROR;
        }

        std::array<std::uint8_t, TRAILER_SIZE> trailer = {};
        std::memcpy(trailer.data(), prefix.data(), prefix.size());
        if (!ReadBlock(
                trailer.data() + prefix.size(),
                trailer.size() - prefix.size()))
        {
            return ReadResult::READ_ERROR;
        }
        if (!ValidateAndFinishTrailer(trailer.data()))
            return ReadResult::READ_ERROR;
        return ReadResult::END;
    }

    Fail(Error::INVALID_RECORD_TAG, record_offset);
    return ReadResult::READ_ERROR;
}

bool Reader::ValidateAndFinishTrailer(const std::uint8_t* trailer)
{
    const std::uint64_t trailer_offset =
        m_bytes_read - static_cast<std::uint64_t>(TRAILER_SIZE);
    if (LoadU32(trailer + 44) != 0 ||
        LoadU32(trailer + 48) != 0)
    {
        return Fail(
            Error::RESERVED_FIELD_NONZERO,
            trailer_offset + 44);
    }
    if (LoadU32(trailer + 52) !=
        ComputeCrc32(trailer, TRAILER_SIZE - 4))
    {
        return Fail(Error::CHECKSUM_MISMATCH, trailer_offset + 52);
    }
    if (LoadU32(trailer + 40) !=
        FinalizeCrc32(m_aggregate_crc_state))
    {
        return Fail(Error::CHECKSUM_MISMATCH, trailer_offset + 40);
    }
    if (AddWouldOverflow(
            m_metadata.first_physics_step,
            m_step_count))
    {
        return Fail(Error::ARITHMETIC_OVERFLOW, trailer_offset + 16);
    }
    if (LoadU64(trailer + 8) != m_step_count ||
        LoadU64(trailer + 16) !=
            m_metadata.first_physics_step + m_step_count ||
        LoadU64(trailer + 24) != m_total_actor_count ||
        LoadU64(trailer + 32) != m_total_contact_count)
    {
        return Fail(Error::SUMMARY_MISMATCH, trailer_offset + 8);
    }

    char trailing_byte = 0;
    bool has_trailing_byte = false;
    try
    {
        m_input.get(trailing_byte);
        has_trailing_byte = m_input.good();
    }
    catch (const std::ios_base::failure&)
    {
        if (!m_input.eof())
            return Fail(Error::IO_FAILURE, m_bytes_read);
    }
    if (has_trailing_byte)
    {
        ++m_bytes_read;
        return Fail(Error::TRAILING_DATA, m_bytes_read - 1);
    }
    if (m_input.bad())
        return Fail(Error::IO_FAILURE, m_bytes_read);

    m_finished = true;
    return true;
}

const Metadata& Reader::GetMetadata() const
{
    return m_metadata;
}

const Status& Reader::GetStatus() const
{
    return m_status;
}

std::uint64_t Reader::GetStepCount() const
{
    return m_step_count;
}

std::uint64_t Reader::GetBytesRead() const
{
    return m_bytes_read;
}

ComparisonOptions::ComparisonOptions():
    allow_worker_count_difference(false)
{
}

ComparisonResult::ComparisonResult():
    status(ComparisonStatus::INVALID_INPUT),
    difference(Difference::NONE),
    metadata_field(MetadataField::NONE),
    left_metadata(),
    right_metadata(),
    left_error(),
    right_error(),
    left_step(),
    right_step(),
    steps_compared(0),
    first_divergent_step(0),
    has_left_metadata(false),
    has_right_metadata(false),
    has_left_step(false),
    has_right_step(false),
    has_first_divergent_step(false)
{
}

namespace {

bool DrainTrace(Reader& reader)
{
    StepRecord ignored;
    for (;;)
    {
        const ReadResult result = reader.ReadNext(ignored);
        if (result == ReadResult::END)
            return true;
        if (result == ReadResult::READ_ERROR)
            return false;
    }
}

void SetInvalidComparison(
    ComparisonResult& result,
    bool left_valid,
    bool right_valid,
    const Reader& left_reader,
    const Reader& right_reader)
{
    result.status = ComparisonStatus::INVALID_INPUT;
    if (!left_valid && !right_valid)
        result.difference = Difference::BOTH_INVALID;
    else if (!left_valid)
        result.difference = Difference::LEFT_INVALID;
    else
        result.difference = Difference::RIGHT_INVALID;
    result.left_error = left_reader.GetStatus();
    result.right_error = right_reader.GetStatus();
}

} // namespace

ComparisonResult Compare(
    std::istream& left,
    std::istream& right,
    const ComparisonOptions& options,
    const Limits& limits)
{
    ComparisonResult result;
    Reader left_reader(left, limits);
    Reader right_reader(right, limits);

    result.has_left_metadata = left_reader.IsReady();
    result.has_right_metadata = right_reader.IsReady();
    if (result.has_left_metadata)
        result.left_metadata = left_reader.GetMetadata();
    if (result.has_right_metadata)
        result.right_metadata = right_reader.GetMetadata();
    result.left_error = left_reader.GetStatus();
    result.right_error = right_reader.GetStatus();

    if (!result.has_left_metadata || !result.has_right_metadata)
    {
        SetInvalidComparison(
            result,
            result.has_left_metadata,
            result.has_right_metadata,
            left_reader,
            right_reader);
        return result;
    }

    MetadataField metadata_difference = MetadataField::NONE;
    if (result.left_metadata.state_digest_schema_version !=
        result.right_metadata.state_digest_schema_version)
    {
        metadata_difference = MetadataField::DIGEST_SCHEMA_VERSION;
    }
    else if (!options.allow_worker_count_difference &&
        result.left_metadata.worker_count !=
            result.right_metadata.worker_count)
    {
        metadata_difference = MetadataField::WORKER_COUNT;
    }
    else if (result.left_metadata.scenario_id !=
        result.right_metadata.scenario_id)
    {
        metadata_difference = MetadataField::SCENARIO_ID;
    }
    else if (result.left_metadata.first_physics_step !=
        result.right_metadata.first_physics_step)
    {
        metadata_difference = MetadataField::FIRST_PHYSICS_STEP;
    }
    else if (result.left_metadata.physics_step_numerator !=
            result.right_metadata.physics_step_numerator ||
        result.left_metadata.physics_step_denominator !=
            result.right_metadata.physics_step_denominator)
    {
        metadata_difference = MetadataField::PHYSICS_STEP_RATE;
    }
    else if (result.left_metadata.physics_flags !=
        result.right_metadata.physics_flags)
    {
        metadata_difference = MetadataField::PHYSICS_FLAGS;
    }

    if (metadata_difference != MetadataField::NONE)
    {
        const bool left_valid = DrainTrace(left_reader);
        const bool right_valid = DrainTrace(right_reader);
        if (!left_valid || !right_valid)
        {
            SetInvalidComparison(
                result,
                left_valid,
                right_valid,
                left_reader,
                right_reader);
            return result;
        }
        result.status = ComparisonStatus::DIVERGED;
        result.difference = Difference::METADATA;
        result.metadata_field = metadata_difference;
        return result;
    }

    bool divergence_found = false;
    for (;;)
    {
        StepRecord left_step;
        StepRecord right_step;
        const ReadResult left_read = left_reader.ReadNext(left_step);
        const ReadResult right_read = right_reader.ReadNext(right_step);
        result.left_error = left_reader.GetStatus();
        result.right_error = right_reader.GetStatus();

        if (left_read == ReadResult::READ_ERROR ||
            right_read == ReadResult::READ_ERROR)
        {
            const bool left_valid =
                left_read != ReadResult::READ_ERROR &&
                (left_read == ReadResult::END || DrainTrace(left_reader));
            const bool right_valid =
                right_read != ReadResult::READ_ERROR &&
                (right_read == ReadResult::END || DrainTrace(right_reader));
            SetInvalidComparison(
                result,
                left_valid,
                right_valid,
                left_reader,
                right_reader);
            return result;
        }
        if (left_read == ReadResult::END &&
            right_read == ReadResult::END)
        {
            if (!divergence_found)
            {
                result.status = ComparisonStatus::MATCH;
                result.difference = Difference::NONE;
            }
            return result;
        }
        if (left_read == ReadResult::END ||
            right_read == ReadResult::END)
        {
            if (!divergence_found)
            {
                result.status = ComparisonStatus::DIVERGED;
                result.difference = Difference::TRACE_LENGTH;
                result.has_left_step = left_read == ReadResult::STEP;
                result.has_right_step = right_read == ReadResult::STEP;
                if (result.has_left_step)
                {
                    result.left_step = left_step;
                    result.first_divergent_step = left_step.physics_step;
                }
                else
                {
                    result.right_step = right_step;
                    result.first_divergent_step = right_step.physics_step;
                }
                result.has_first_divergent_step = true;
                divergence_found = true;
            }
            continue;
        }

        Difference step_difference = Difference::NONE;
        if (left_step.physics_step != right_step.physics_step)
        {
            step_difference = Difference::PHYSICS_STEP;
        }
        else if (left_step.actor_count != right_step.actor_count)
        {
            step_difference = Difference::ACTOR_COUNT;
        }
        else if (left_step.contact_count != right_step.contact_count)
        {
            step_difference = Difference::CONTACT_COUNT;
        }
        else if (left_step.input_flags != right_step.input_flags ||
            left_step.input_digest != right_step.input_digest)
        {
            step_difference = Difference::INPUT_DIGEST;
        }
        else if (!DigestsEqual(left_step.digest, right_step.digest))
        {
            step_difference = Difference::DIGEST;
        }

        if (!divergence_found && step_difference != Difference::NONE)
        {
            result.status = ComparisonStatus::DIVERGED;
            result.difference = step_difference;
            result.has_left_step = true;
            result.has_right_step = true;
            result.left_step = left_step;
            result.right_step = right_step;
            result.first_divergent_step =
                std::min(left_step.physics_step, right_step.physics_step);
            result.has_first_divergent_step = true;
            divergence_found = true;
        }
        else if (!divergence_found)
        {
            ++result.steps_compared;
        }
    }
}

std::string FormatComparisonJson(
    const ComparisonResult& result,
    const std::string& left_label,
    const std::string& right_label)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "{\"format\":\"ror-d0-state-trace-comparison-v2\""
           << ",\"status\":";
    AppendJsonString(stream, ToString(result.status));
    stream << ",\"difference\":";
    AppendJsonString(stream, ToString(result.difference));
    stream << ",\"metadata_field\":";
    AppendJsonString(stream, ToString(result.metadata_field));
    stream << ",\"steps_compared\":" << result.steps_compared
           << ",\"first_divergent_step\":";
    if (result.has_first_divergent_step)
        stream << result.first_divergent_step;
    else
        stream << "null";

    stream << ",\"left\":{\"label\":";
    AppendJsonString(stream, left_label);
    stream << ",\"metadata\":";
    if (result.has_left_metadata)
        AppendMetadataJson(stream, result.left_metadata);
    else
        stream << "null";
    stream << ",\"step\":";
    if (result.has_left_step)
        AppendStepJson(stream, result.left_step);
    else
        stream << "null";
    stream << ",\"error\":";
    AppendStatusJson(stream, result.left_error);
    stream << '}';

    stream << ",\"right\":{\"label\":";
    AppendJsonString(stream, right_label);
    stream << ",\"metadata\":";
    if (result.has_right_metadata)
        AppendMetadataJson(stream, result.right_metadata);
    else
        stream << "null";
    stream << ",\"step\":";
    if (result.has_right_step)
        AppendStepJson(stream, result.right_step);
    else
        stream << "null";
    stream << ",\"error\":";
    AppendStatusJson(stream, result.right_error);
    stream << "}}\n";
    return stream.str();
}

const char* ToString(Error error)
{
    switch (error)
    {
    case Error::NONE:
        return "none";
    case Error::INVALID_METADATA:
        return "invalid_metadata";
    case Error::IO_FAILURE:
        return "io_failure";
    case Error::TRUNCATED:
        return "truncated";
    case Error::MAGIC_MISMATCH:
        return "magic_mismatch";
    case Error::UNSUPPORTED_SCHEMA:
        return "unsupported_schema";
    case Error::INVALID_HEADER_SIZE:
        return "invalid_header_size";
    case Error::DIGEST_SCHEMA_MISMATCH:
        return "digest_schema_mismatch";
    case Error::RESERVED_FIELD_NONZERO:
        return "reserved_field_nonzero";
    case Error::INVALID_INPUT_BINDING:
        return "invalid_input_binding";
    case Error::CHECKSUM_MISMATCH:
        return "checksum_mismatch";
    case Error::INVALID_RECORD_TAG:
        return "invalid_record_tag";
    case Error::INVALID_RECORD_SIZE:
        return "invalid_record_size";
    case Error::NON_CONTIGUOUS_STEP:
        return "non_contiguous_step";
    case Error::COUNT_LIMIT_EXCEEDED:
        return "count_limit_exceeded";
    case Error::STEP_LIMIT_EXCEEDED:
        return "step_limit_exceeded";
    case Error::BYTE_LIMIT_EXCEEDED:
        return "byte_limit_exceeded";
    case Error::ARITHMETIC_OVERFLOW:
        return "arithmetic_overflow";
    case Error::SUMMARY_MISMATCH:
        return "summary_mismatch";
    case Error::TRAILING_DATA:
        return "trailing_data";
    case Error::ALREADY_FINISHED:
        return "already_finished";
    }
    return "unknown";
}

const char* ToString(ComparisonStatus status)
{
    switch (status)
    {
    case ComparisonStatus::MATCH:
        return "match";
    case ComparisonStatus::DIVERGED:
        return "diverged";
    case ComparisonStatus::INVALID_INPUT:
        return "invalid_input";
    }
    return "unknown";
}

const char* ToString(Difference difference)
{
    switch (difference)
    {
    case Difference::NONE:
        return "none";
    case Difference::METADATA:
        return "metadata";
    case Difference::PHYSICS_STEP:
        return "physics_step";
    case Difference::ACTOR_COUNT:
        return "actor_count";
    case Difference::CONTACT_COUNT:
        return "contact_count";
    case Difference::INPUT_DIGEST:
        return "input_digest";
    case Difference::DIGEST:
        return "digest";
    case Difference::TRACE_LENGTH:
        return "trace_length";
    case Difference::LEFT_INVALID:
        return "left_invalid";
    case Difference::RIGHT_INVALID:
        return "right_invalid";
    case Difference::BOTH_INVALID:
        return "both_invalid";
    }
    return "unknown";
}

const char* ToString(MetadataField field)
{
    switch (field)
    {
    case MetadataField::NONE:
        return "none";
    case MetadataField::DIGEST_SCHEMA_VERSION:
        return "digest_schema_version";
    case MetadataField::WORKER_COUNT:
        return "worker_count";
    case MetadataField::SCENARIO_ID:
        return "scenario_id";
    case MetadataField::FIRST_PHYSICS_STEP:
        return "first_physics_step";
    case MetadataField::PHYSICS_STEP_RATE:
        return "physics_step_rate";
    case MetadataField::PHYSICS_FLAGS:
        return "physics_flags";
    }
    return "unknown";
}

} // namespace DeterministicStateTrace
} // namespace RoR
