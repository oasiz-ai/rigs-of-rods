/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "DeterministicInputContinuationSavegame.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace RoR {
namespace DeterministicInputContinuationSavegame {
namespace {

static const std::uint8_t MAGIC[8] = {
    'R', 'O', 'R', 'I', 'S', 'V', '1', '\0'};
static const std::uint32_t HEADER_SIZE = 204U;
static const std::uint32_t RESUME_AFTER_LOAD_FLAG = 1U;
static const std::uint32_t KNOWN_FLAGS = RESUME_AFTER_LOAD_FLAG;
static const std::uint64_t TRAILER_SIZE = 32U;

bool AddWouldOverflow(std::uint64_t left, std::uint64_t right)
{
    return left > std::numeric_limits<std::uint64_t>::max() - right;
}

void StoreU32(std::uint8_t* output, std::uint32_t value)
{
    output[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    output[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    output[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    output[3] = static_cast<std::uint8_t>(value & 0xffU);
}

void StoreU64(std::uint8_t* output, std::uint64_t value)
{
    for (std::uint32_t index = 0U; index < 8U; ++index)
    {
        output[index] = static_cast<std::uint8_t>(
            (value >> (56U - index * 8U)) & UINT64_C(0xff));
    }
}

std::uint32_t LoadU32(const std::uint8_t* input)
{
    return (static_cast<std::uint32_t>(input[0]) << 24U) |
        (static_cast<std::uint32_t>(input[1]) << 16U) |
        (static_cast<std::uint32_t>(input[2]) << 8U) |
        static_cast<std::uint32_t>(input[3]);
}

std::uint64_t LoadU64(const std::uint8_t* input)
{
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index)
        value = (value << 8U) | static_cast<std::uint64_t>(input[index]);
    return value;
}

bool IsValidPayload(const Payload& payload)
{
    using DeterministicInputTrace::RuntimeLifecycle;
    using DeterministicInputTrace::RuntimeMode;
    const DeterministicInputTrace::RuntimeContinuation& continuation =
        payload.continuation;
    return payload.schema_version == PAYLOAD_SCHEMA_VERSION &&
        payload.target_id != 0U &&
        payload.step_limit != 0U &&
        payload.step_limit <= MAX_SAVEGAME_STEPS &&
        payload.completed_physics_steps !=
            std::numeric_limits<std::uint64_t>::max() &&
        payload.actor_physics_step !=
            std::numeric_limits<std::uint64_t>::max() &&
        payload.completed_physics_steps ==
            continuation.next_physics_step &&
        continuation.schema_version ==
            DeterministicInputTrace::RUNTIME_CONTINUATION_SCHEMA_VERSION &&
        (continuation.mode == RuntimeMode::RECORD ||
            continuation.mode == RuntimeMode::REPLAY) &&
        continuation.lifecycle == RuntimeLifecycle::PAUSED &&
        continuation.limits.max_steps == payload.step_limit &&
        continuation.limits.max_steps != 0U &&
        continuation.limits.max_steps <= MAX_SAVEGAME_STEPS &&
        continuation.limits.max_events != 0U &&
        continuation.limits.max_events <= MAX_SAVEGAME_EVENTS &&
        continuation.limits.max_bytes != 0U &&
        continuation.limits.max_bytes <= MAX_SAVEGAME_TRACE_BYTES &&
        continuation.limits.max_events_per_step != 0U &&
        continuation.limits.max_events_per_step <=
            DeterministicInputTrace::MAX_EVENTS_PER_STEP &&
        continuation.limits.max_active_controls != 0U &&
        continuation.limits.max_active_controls <=
            DeterministicInputTrace::MAX_ACTIVE_CONTROLS &&
        continuation.limits.max_identity_string_bytes != 0U &&
        continuation.limits.max_identity_string_bytes <=
            DeterministicInputTrace::MAX_IDENTITY_STRING_BYTES &&
        continuation.processed_steps <= payload.step_limit &&
        continuation.authenticated_trace.size() != 0U &&
        continuation.authenticated_trace.size() <=
            continuation.limits.max_bytes &&
        continuation.authenticated_trace.size() <=
            MAX_SAVEGAME_TRACE_BYTES;
}

int Base64Value(char value)
{
    if (value >= 'A' && value <= 'Z')
        return value - 'A';
    if (value >= 'a' && value <= 'z')
        return value - 'a' + 26;
    if (value >= '0' && value <= '9')
        return value - '0' + 52;
    if (value == '-')
        return 62;
    if (value == '_')
        return 63;
    return -1;
}

bool EncodeBase64Url(
    const std::vector<std::uint8_t>& input,
    std::string& output)
{
    static const char ALPHABET[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    if (input.size() >
        (std::numeric_limits<std::size_t>::max() - 2U) / 4U * 3U)
    {
        return false;
    }
    std::string candidate;
    candidate.reserve((input.size() * 4U + 2U) / 3U);
    std::size_t offset = 0U;
    while (offset + 3U <= input.size())
    {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(input[offset]) << 16U) |
            (static_cast<std::uint32_t>(input[offset + 1U]) << 8U) |
            static_cast<std::uint32_t>(input[offset + 2U]);
        candidate.push_back(ALPHABET[(value >> 18U) & 63U]);
        candidate.push_back(ALPHABET[(value >> 12U) & 63U]);
        candidate.push_back(ALPHABET[(value >> 6U) & 63U]);
        candidate.push_back(ALPHABET[value & 63U]);
        offset += 3U;
    }
    const std::size_t remaining = input.size() - offset;
    if (remaining == 1U)
    {
        const std::uint32_t value =
            static_cast<std::uint32_t>(input[offset]) << 16U;
        candidate.push_back(ALPHABET[(value >> 18U) & 63U]);
        candidate.push_back(ALPHABET[(value >> 12U) & 63U]);
    }
    else if (remaining == 2U)
    {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(input[offset]) << 16U) |
            (static_cast<std::uint32_t>(input[offset + 1U]) << 8U);
        candidate.push_back(ALPHABET[(value >> 18U) & 63U]);
        candidate.push_back(ALPHABET[(value >> 12U) & 63U]);
        candidate.push_back(ALPHABET[(value >> 6U) & 63U]);
    }
    output.swap(candidate);
    return true;
}

bool DecodeBase64Url(
    const std::string& input,
    std::vector<std::uint8_t>& output)
{
    if (input.empty() || input.size() % 4U == 1U)
        return false;
    const std::uint64_t max_wire_size =
        static_cast<std::uint64_t>(HEADER_SIZE) +
        MAX_SAVEGAME_TRACE_BYTES + TRAILER_SIZE;
    const std::uint64_t max_encoded_size =
        (max_wire_size / 3U) * 4U +
        ((max_wire_size % 3U) == 0U ? 0U :
            (max_wire_size % 3U) + 1U);
    if (input.size() > max_encoded_size)
        return false;

    std::vector<std::uint8_t> candidate;
    candidate.reserve((input.size() * 3U) / 4U + 2U);
    std::size_t offset = 0U;
    while (offset + 4U <= input.size())
    {
        const int a = Base64Value(input[offset]);
        const int b = Base64Value(input[offset + 1U]);
        const int c = Base64Value(input[offset + 2U]);
        const int d = Base64Value(input[offset + 3U]);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return false;
        const std::uint32_t value =
            (static_cast<std::uint32_t>(a) << 18U) |
            (static_cast<std::uint32_t>(b) << 12U) |
            (static_cast<std::uint32_t>(c) << 6U) |
            static_cast<std::uint32_t>(d);
        candidate.push_back(static_cast<std::uint8_t>(value >> 16U));
        candidate.push_back(static_cast<std::uint8_t>(value >> 8U));
        candidate.push_back(static_cast<std::uint8_t>(value));
        offset += 4U;
    }
    const std::size_t remaining = input.size() - offset;
    if (remaining == 2U)
    {
        const int a = Base64Value(input[offset]);
        const int b = Base64Value(input[offset + 1U]);
        if (a < 0 || b < 0 || (b & 15) != 0)
            return false;
        candidate.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(a) << 2U) |
            (static_cast<std::uint32_t>(b) >> 4U)));
    }
    else if (remaining == 3U)
    {
        const int a = Base64Value(input[offset]);
        const int b = Base64Value(input[offset + 1U]);
        const int c = Base64Value(input[offset + 2U]);
        if (a < 0 || b < 0 || c < 0 || (c & 3) != 0)
            return false;
        const std::uint32_t value =
            (static_cast<std::uint32_t>(a) << 10U) |
            (static_cast<std::uint32_t>(b) << 4U) |
            (static_cast<std::uint32_t>(c) >> 2U);
        candidate.push_back(static_cast<std::uint8_t>(value >> 8U));
        candidate.push_back(static_cast<std::uint8_t>(value));
    }
    else if (remaining != 0U)
    {
        return false;
    }

    std::string canonical;
    if (!EncodeBase64Url(candidate, canonical) || canonical != input)
        return false;
    output.swap(candidate);
    return true;
}

void SetFailure(Status& status, Error error, std::uint64_t offset)
{
    status.error = error;
    status.byte_offset = offset;
}

} // namespace

Payload::Payload()
    : schema_version(PAYLOAD_SCHEMA_VERSION),
      resume_after_load(false),
      scenario_id(0U),
      target_id(0U),
      step_limit(0U),
      completed_physics_steps(0U),
      actor_physics_step(0U),
      continuation()
{
}

void Payload::Swap(Payload& other)
{
    std::swap(schema_version, other.schema_version);
    std::swap(resume_after_load, other.resume_after_load);
    std::swap(scenario_id, other.scenario_id);
    std::swap(target_id, other.target_id);
    std::swap(step_limit, other.step_limit);
    std::swap(completed_physics_steps, other.completed_physics_steps);
    std::swap(actor_physics_step, other.actor_physics_step);
    continuation.Swap(other.continuation);
}

Status::Status(): error(Error::NONE), byte_offset(0U)
{
}

bool Encode(
    const Payload& payload,
    std::string& output,
    Status& status)
{
    status = Status();
    if (!IsValidPayload(payload))
    {
        SetFailure(status, Error::INVALID_PAYLOAD, 0U);
        return false;
    }
    if (AddWouldOverflow(
            static_cast<std::uint64_t>(HEADER_SIZE),
            payload.continuation.authenticated_trace.size()) ||
        AddWouldOverflow(
            static_cast<std::uint64_t>(HEADER_SIZE) +
                payload.continuation.authenticated_trace.size(),
            TRAILER_SIZE))
    {
        SetFailure(status, Error::SIZE_LIMIT_EXCEEDED, 0U);
        return false;
    }

    try
    {
        const std::size_t trace_size =
            payload.continuation.authenticated_trace.size();
        std::vector<std::uint8_t> wire(
            static_cast<std::size_t>(HEADER_SIZE) + trace_size +
                static_cast<std::size_t>(TRAILER_SIZE),
            UINT8_C(0));
        std::copy(MAGIC, MAGIC + sizeof(MAGIC), wire.begin());
        StoreU32(&wire[8], payload.schema_version);
        StoreU32(&wire[12], HEADER_SIZE);
        StoreU32(
            &wire[16],
            payload.resume_after_load ? RESUME_AFTER_LOAD_FLAG : 0U);
        StoreU32(
            &wire[20],
            static_cast<std::uint32_t>(payload.continuation.mode));
        StoreU32(
            &wire[24],
            static_cast<std::uint32_t>(payload.continuation.lifecycle));
        StoreU32(&wire[28], 0U);
        StoreU64(&wire[32], payload.scenario_id);
        StoreU64(&wire[40], payload.target_id);
        StoreU64(&wire[48], payload.step_limit);
        StoreU64(&wire[56], payload.completed_physics_steps);
        StoreU64(&wire[64], payload.actor_physics_step);
        StoreU64(&wire[72], payload.continuation.limits.max_steps);
        StoreU64(&wire[80], payload.continuation.limits.max_events);
        StoreU64(&wire[88], payload.continuation.limits.max_bytes);
        StoreU32(&wire[96],
            payload.continuation.limits.max_events_per_step);
        StoreU32(&wire[100],
            payload.continuation.limits.max_active_controls);
        StoreU32(&wire[104],
            payload.continuation.limits.max_identity_string_bytes);
        StoreU32(&wire[108], payload.continuation.schema_version);
        StoreU64(&wire[112], payload.continuation.processed_steps);
        StoreU64(&wire[120], payload.continuation.next_physics_step);
        std::copy(
            payload.continuation.trace_digest.bytes.begin(),
            payload.continuation.trace_digest.bytes.end(),
            wire.begin() + 128);
        std::copy(
            payload.continuation.authentication_digest.bytes.begin(),
            payload.continuation.authentication_digest.bytes.end(),
            wire.begin() + 160);
        StoreU64(&wire[192], static_cast<std::uint64_t>(trace_size));
        StoreU32(&wire[200], 0U);
        std::copy(
            payload.continuation.authenticated_trace.begin(),
            payload.continuation.authenticated_trace.end(),
            wire.begin() + HEADER_SIZE);
        const DeterministicInputTrace::Digest digest =
            DeterministicInputTrace::ComputeSha256(
                wire.data(),
                static_cast<std::size_t>(HEADER_SIZE) + trace_size);
        std::copy(
            digest.bytes.begin(),
            digest.bytes.end(),
            wire.end() - static_cast<std::ptrdiff_t>(TRAILER_SIZE));

        std::string candidate;
        if (!EncodeBase64Url(wire, candidate))
        {
            SetFailure(status, Error::SIZE_LIMIT_EXCEEDED, 0U);
            return false;
        }
        output.swap(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetFailure(status, Error::ALLOCATION_FAILURE, 0U);
        return false;
    }
    catch (const std::length_error&)
    {
        SetFailure(status, Error::ALLOCATION_FAILURE, 0U);
        return false;
    }
}

bool Decode(
    const std::string& encoded,
    Payload& output,
    Status& status)
{
    status = Status();
    try
    {
        std::vector<std::uint8_t> wire;
        if (!DecodeBase64Url(encoded, wire))
        {
            SetFailure(status, Error::INVALID_BASE64, 0U);
            return false;
        }
        const std::size_t minimum_size =
            static_cast<std::size_t>(HEADER_SIZE + TRAILER_SIZE);
        if (wire.size() < minimum_size ||
            !std::equal(MAGIC, MAGIC + sizeof(MAGIC), wire.begin()) ||
            LoadU32(&wire[8]) != PAYLOAD_SCHEMA_VERSION ||
            LoadU32(&wire[12]) != HEADER_SIZE ||
            (LoadU32(&wire[16]) & ~KNOWN_FLAGS) != 0U ||
            LoadU32(&wire[28]) != 0U ||
            LoadU32(&wire[200]) != 0U)
        {
            SetFailure(status, Error::INVALID_WIRE_FORMAT, 0U);
            return false;
        }
        const std::uint64_t trace_size = LoadU64(&wire[192]);
        if (trace_size == 0U || trace_size > MAX_SAVEGAME_TRACE_BYTES ||
            AddWouldOverflow(HEADER_SIZE, trace_size) ||
            AddWouldOverflow(HEADER_SIZE + trace_size, TRAILER_SIZE) ||
            HEADER_SIZE + trace_size + TRAILER_SIZE != wire.size())
        {
            SetFailure(status, Error::SIZE_LIMIT_EXCEEDED, 192U);
            return false;
        }
        const std::size_t payload_size =
            static_cast<std::size_t>(HEADER_SIZE + trace_size);
        const DeterministicInputTrace::Digest expected_digest =
            DeterministicInputTrace::ComputeSha256(
                wire.data(),
                payload_size);
        if (!std::equal(
                expected_digest.bytes.begin(),
                expected_digest.bytes.end(),
                wire.begin() + static_cast<std::ptrdiff_t>(payload_size)))
        {
            SetFailure(status, Error::INTEGRITY_MISMATCH, payload_size);
            return false;
        }

        Payload candidate;
        candidate.schema_version = LoadU32(&wire[8]);
        candidate.resume_after_load =
            (LoadU32(&wire[16]) & RESUME_AFTER_LOAD_FLAG) != 0U;
        candidate.continuation.mode =
            static_cast<DeterministicInputTrace::RuntimeMode>(
                LoadU32(&wire[20]));
        candidate.continuation.lifecycle =
            static_cast<DeterministicInputTrace::RuntimeLifecycle>(
                LoadU32(&wire[24]));
        candidate.scenario_id = LoadU64(&wire[32]);
        candidate.target_id = LoadU64(&wire[40]);
        candidate.step_limit = LoadU64(&wire[48]);
        candidate.completed_physics_steps = LoadU64(&wire[56]);
        candidate.actor_physics_step = LoadU64(&wire[64]);
        candidate.continuation.limits.max_steps = LoadU64(&wire[72]);
        candidate.continuation.limits.max_events = LoadU64(&wire[80]);
        candidate.continuation.limits.max_bytes = LoadU64(&wire[88]);
        candidate.continuation.limits.max_events_per_step =
            LoadU32(&wire[96]);
        candidate.continuation.limits.max_active_controls =
            LoadU32(&wire[100]);
        candidate.continuation.limits.max_identity_string_bytes =
            LoadU32(&wire[104]);
        candidate.continuation.schema_version = LoadU32(&wire[108]);
        candidate.continuation.processed_steps = LoadU64(&wire[112]);
        candidate.continuation.next_physics_step = LoadU64(&wire[120]);
        std::copy(
            wire.begin() + 128,
            wire.begin() + 160,
            candidate.continuation.trace_digest.bytes.begin());
        std::copy(
            wire.begin() + 160,
            wire.begin() + 192,
            candidate.continuation.authentication_digest.bytes.begin());
        candidate.continuation.authenticated_trace.assign(
            reinterpret_cast<const char*>(wire.data() + HEADER_SIZE),
            static_cast<std::size_t>(trace_size));
        if (!IsValidPayload(candidate))
        {
            SetFailure(status, Error::INVALID_PAYLOAD, 0U);
            return false;
        }
        output.Swap(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetFailure(status, Error::ALLOCATION_FAILURE, 0U);
        return false;
    }
    catch (const std::length_error&)
    {
        SetFailure(status, Error::ALLOCATION_FAILURE, 0U);
        return false;
    }
}

const char* ToString(Error error)
{
    switch (error)
    {
    case Error::NONE: return "none";
    case Error::INVALID_PAYLOAD: return "invalid_payload";
    case Error::SIZE_LIMIT_EXCEEDED: return "size_limit_exceeded";
    case Error::INVALID_BASE64: return "invalid_base64";
    case Error::INVALID_WIRE_FORMAT: return "invalid_wire_format";
    case Error::INTEGRITY_MISMATCH: return "integrity_mismatch";
    case Error::ALLOCATION_FAILURE: return "allocation_failure";
    }
    return "unknown";
}

} // namespace DeterministicInputContinuationSavegame
} // namespace RoR
