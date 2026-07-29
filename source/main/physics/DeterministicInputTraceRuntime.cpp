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

#include "DeterministicInputTraceRuntime.h"

#include <algorithm>
#include <cstring>
#include <istream>
#include <limits>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

namespace Trace = RoR::DeterministicInputTrace;

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool IsFinite(double value)
{
    return (DoubleBits(value) & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool KeyLess(
    std::uint64_t left_target,
    std::uint32_t left_control,
    std::uint64_t right_target,
    std::uint32_t right_control)
{
    return left_target < right_target ||
        (left_target == right_target && left_control < right_control);
}

Trace::Limits ClampLimits(const Trace::Limits& requested)
{
    Trace::Limits limits = requested;
    limits.max_steps =
        std::min(limits.max_steps, Trace::MAX_TRACE_STEPS);
    limits.max_events =
        std::min(limits.max_events, Trace::MAX_TRACE_EVENTS);
    limits.max_bytes =
        std::min(limits.max_bytes, Trace::MAX_TRACE_BYTES);
    limits.max_events_per_step =
        std::min(
            limits.max_events_per_step,
            Trace::MAX_EVENTS_PER_STEP);
    limits.max_active_controls =
        std::min(
            limits.max_active_controls,
            Trace::MAX_ACTIVE_CONTROLS);
    limits.max_identity_string_bytes =
        std::min(
            limits.max_identity_string_bytes,
            Trace::MAX_IDENTITY_STRING_BYTES);
    return limits;
}

bool MetadataEqual(
    const Trace::Metadata& left,
    const Trace::Metadata& right)
{
    return left.semantic_flags == right.semantic_flags &&
        left.scenario_id == right.scenario_id &&
        left.stream_id == right.stream_id &&
        left.first_physics_step == right.first_physics_step &&
        left.physics_step_numerator == right.physics_step_numerator &&
        left.physics_step_denominator == right.physics_step_denominator &&
        left.scenario_name == right.scenario_name &&
        left.source_name == right.source_name &&
        left.source_digest == right.source_digest;
}

bool LimitsEqual(
    const Trace::Limits& left,
    const Trace::Limits& right)
{
    return left.max_steps == right.max_steps &&
        left.max_events == right.max_events &&
        left.max_bytes == right.max_bytes &&
        left.max_events_per_step == right.max_events_per_step &&
        left.max_active_controls == right.max_active_controls &&
        left.max_identity_string_bytes ==
            right.max_identity_string_bytes;
}

void SetSyntheticTraceStatus(
    Trace::Status& status,
    Trace::Error error,
    std::uint64_t byte_offset,
    std::uint64_t step_index = 0,
    std::uint32_t event_index = 0)
{
    status.error = error;
    status.byte_offset = byte_offset;
    status.step_index = step_index;
    status.event_index = event_index;
}

void AppendHexNibble(std::string& output, std::uint8_t nibble)
{
    static const char HEX[] = "0123456789abcdef";
    output.push_back(HEX[nibble & UINT8_C(0x0f)]);
}

void AppendHexU32(std::string& output, std::uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        AppendHexNibble(
            output,
            static_cast<std::uint8_t>(value >> shift));
    }
}

void AppendHexU64(std::string& output, std::uint64_t value)
{
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        AppendHexNibble(
            output,
            static_cast<std::uint8_t>(value >> shift));
    }
}

void AppendHexDigest(
    std::string& output,
    const Trace::Digest& digest)
{
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
    {
        AppendHexNibble(
            output,
            static_cast<std::uint8_t>(digest.bytes[index] >> 4));
        AppendHexNibble(output, digest.bytes[index]);
    }
}

bool BuildContinuationAuthenticationDigest(
    const Trace::RuntimeContinuation& continuation,
    Trace::Digest& digest,
    Trace::Status& trace_status)
{
    try
    {
        // Fixed-width lowercase hex is locale-independent and gives every
        // field one unambiguous version-1 position in the authenticated
        // envelope. The independently authenticated trace digest plus byte
        // count binds the complete D0 stream without hashing gigabytes twice.
        std::string canonical;
        canonical.reserve(208);
        AppendHexU32(canonical, continuation.schema_version);
        AppendHexU32(
            canonical,
            static_cast<std::uint32_t>(continuation.mode));
        AppendHexU32(
            canonical,
            static_cast<std::uint32_t>(continuation.lifecycle));
        AppendHexU64(canonical, continuation.limits.max_steps);
        AppendHexU64(canonical, continuation.limits.max_events);
        AppendHexU64(canonical, continuation.limits.max_bytes);
        AppendHexU32(
            canonical,
            continuation.limits.max_events_per_step);
        AppendHexU32(
            canonical,
            continuation.limits.max_active_controls);
        AppendHexU32(
            canonical,
            continuation.limits.max_identity_string_bytes);
        AppendHexU64(canonical, continuation.processed_steps);
        AppendHexU64(canonical, continuation.next_physics_step);
        AppendHexDigest(canonical, continuation.trace_digest);
        AppendHexU64(
            canonical,
            static_cast<std::uint64_t>(
                continuation.authenticated_trace.size()));

        Trace::Metadata metadata;
        metadata.scenario_id =
            UINT64_C(0x52554e54494d4531); // "RUNTIME1"
        metadata.stream_id =
            UINT64_C(0x434f4e54494e5531); // "CONTINU1"
        metadata.physics_step_numerator = 1;
        metadata.physics_step_denominator = 1;
        metadata.scenario_name = "runtime-continuation-auth-v1";
        metadata.source_name.swap(canonical);
        for (std::size_t index = 0;
             index < metadata.source_digest.bytes.size();
             ++index)
        {
            metadata.source_digest.bytes[index] =
                static_cast<std::uint8_t>(
                    UINT8_C(0xa5) ^
                    static_cast<std::uint8_t>(index * 13U));
        }

        std::ostringstream stream(std::ios::out | std::ios::binary);
        Trace::Writer writer(stream, metadata);
        if (!writer.IsReady())
        {
            trace_status = writer.GetStatus();
            return false;
        }
        if (!writer.Finish())
        {
            trace_status = writer.GetStatus();
            return false;
        }
        digest = writer.GetTraceDigest();
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
    catch (const std::length_error&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
}

bool BuildAuthenticatedTrace(
    const Trace::Metadata& metadata,
    const Trace::Limits& limits,
    const std::vector<Trace::Frame>& frames,
    std::string& output,
    Trace::Digest& digest,
    Trace::Status& trace_status)
{
    try
    {
        std::ostringstream stream(std::ios::out | std::ios::binary);
        Trace::Writer writer(stream, metadata, limits);
        if (!writer.IsReady())
        {
            trace_status = writer.GetStatus();
            return false;
        }
        for (std::size_t index = 0; index < frames.size(); ++index)
        {
            if (!writer.Append(frames[index]))
            {
                trace_status = writer.GetStatus();
                return false;
            }
        }
        if (!writer.Finish())
        {
            trace_status = writer.GetStatus();
            return false;
        }
        std::string candidate = stream.str();
        if (candidate.size() != writer.GetBytesWritten())
        {
            SetSyntheticTraceStatus(
                trace_status,
                Trace::Error::IO_FAILURE,
                writer.GetBytesWritten());
            return false;
        }
        digest = writer.GetTraceDigest();
        output.swap(candidate);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
    catch (const std::length_error&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
}

struct PreflightResult
{
    std::uint64_t step_count;
    std::uint64_t event_count;
    std::uint64_t next_physics_step;
    Trace::Digest digest;
    Trace::ReplayState state;
    std::vector<Trace::Frame> frames;

    PreflightResult():
        step_count(0),
        event_count(0),
        next_physics_step(0),
        digest(),
        state(),
        frames()
    {
    }
};

bool PreflightAuthenticatedTrace(
    const std::string& bytes,
    const Trace::Metadata& expected_metadata,
    const Trace::Limits& limits,
    bool retain_frames,
    PreflightResult& result,
    Trace::Status& trace_status,
    bool& identity_matches)
{
    identity_matches = false;
    try
    {
        std::istringstream stream(
            bytes,
            std::ios::in | std::ios::binary);
        Trace::Reader reader(stream, limits);
        if (!reader.IsReady())
        {
            trace_status = reader.GetStatus();
            return false;
        }

        const bool metadata_matches =
            MetadataEqual(reader.GetMetadata(), expected_metadata);
        Trace::Frame frame;
        for (;;)
        {
            const Trace::ReadResult read_result =
                reader.ReadNext(frame);
            if (read_result == Trace::ReadResult::READ_ERROR)
            {
                trace_status = reader.GetStatus();
                return false;
            }
            if (read_result == Trace::ReadResult::END)
                break;
            if (retain_frames)
                result.frames.push_back(frame);
        }
        if (reader.GetStatus().error != Trace::Error::NONE)
        {
            trace_status = reader.GetStatus();
            return false;
        }

        result.step_count = reader.GetStepCount();
        result.event_count = reader.GetEventCount();
        result.next_physics_step = reader.GetNextPhysicsStep();
        result.digest = reader.GetTraceDigest();
        result.state = reader.GetReplayState();
        identity_matches = metadata_matches;
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
    catch (const std::length_error&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            0);
        return false;
    }
}

bool ReadAllBounded(
    std::istream& input,
    std::uint64_t max_bytes,
    std::string& output,
    Trace::Status& trace_status)
{
    std::string candidate;
    char buffer[4096];
    try
    {
        for (;;)
        {
            std::streamsize received = 0;
            bool threw = false;
            try
            {
                input.read(buffer, sizeof(buffer));
                received = input.gcount();
            }
            catch (const std::bad_alloc&)
            {
                SetSyntheticTraceStatus(
                    trace_status,
                    Trace::Error::ALLOCATION_FAILURE,
                    static_cast<std::uint64_t>(candidate.size()));
                return false;
            }
            catch (const std::length_error&)
            {
                SetSyntheticTraceStatus(
                    trace_status,
                    Trace::Error::ALLOCATION_FAILURE,
                    static_cast<std::uint64_t>(candidate.size()));
                return false;
            }
            catch (...)
            {
                received = input.gcount();
                threw = true;
            }

            if (received > 0)
            {
                const std::uint64_t received_u64 =
                    static_cast<std::uint64_t>(received);
                if (received_u64 > max_bytes -
                        static_cast<std::uint64_t>(candidate.size()))
                {
                    SetSyntheticTraceStatus(
                        trace_status,
                        Trace::Error::BYTE_LIMIT_EXCEEDED,
                        static_cast<std::uint64_t>(candidate.size()));
                    return false;
                }
                candidate.append(
                    buffer,
                    static_cast<std::size_t>(received));
            }

            if (input.bad() && !input.eof())
            {
                SetSyntheticTraceStatus(
                    trace_status,
                    Trace::Error::IO_FAILURE,
                    static_cast<std::uint64_t>(candidate.size()));
                return false;
            }
            if (input.eof())
                break;
            if (threw || input.fail() || received == 0)
            {
                SetSyntheticTraceStatus(
                    trace_status,
                    Trace::Error::IO_FAILURE,
                    static_cast<std::uint64_t>(candidate.size()));
                return false;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            static_cast<std::uint64_t>(candidate.size()));
        return false;
    }
    catch (const std::length_error&)
    {
        SetSyntheticTraceStatus(
            trace_status,
            Trace::Error::ALLOCATION_FAILURE,
            static_cast<std::uint64_t>(candidate.size()));
        return false;
    }
    output.swap(candidate);
    return true;
}

bool AddWouldOverflow(std::uint64_t left, std::uint64_t right)
{
    return left > std::numeric_limits<std::uint64_t>::max() - right;
}

} // namespace

namespace RoR {
namespace DeterministicInputTrace {

RuntimeStatus::RuntimeStatus():
    error(RuntimeError::NONE),
    trace_status(),
    physics_step(0),
    processed_steps(0)
{
}

bool FixedStepSampleSource::AcceptsRuntime(const Runtime&) const
{
    return true;
}

bool FixedStepInjectionSink::AcceptsRuntime(const Runtime&) const
{
    return true;
}

SampleCollector::SampleCollector(std::uint32_t max_events):
    m_events(),
    m_max_events(max_events),
    m_error(Error::NONE)
{
}

bool SampleCollector::AddPersistentDelta(
    std::uint64_t target_id,
    std::uint32_t control_id,
    double absolute_value)
{
    return Add(
        target_id,
        control_id,
        EventKind::STATE,
        absolute_value);
}

bool SampleCollector::AddImpulse(
    std::uint64_t target_id,
    std::uint32_t control_id,
    double value)
{
    return Add(
        target_id,
        control_id,
        EventKind::IMPULSE,
        value);
}

bool SampleCollector::Add(
    std::uint64_t target_id,
    std::uint32_t control_id,
    EventKind kind,
    double value)
{
    if (m_error != Error::NONE)
        return false;
    if (control_id == 0)
    {
        m_error = Error::INVALID_CONTROL_ID;
        return false;
    }
    if (m_events.size() >= m_max_events)
    {
        m_error = Error::EVENT_LIMIT_EXCEEDED;
        return false;
    }
    if (!m_events.empty())
    {
        const Event& previous = m_events.back();
        if (!KeyLess(
                previous.target_id,
                previous.control_id,
                target_id,
                control_id))
        {
            m_error = Error::NON_CANONICAL_EVENT_ORDER;
            return false;
        }
    }
    if (!IsFinite(value))
    {
        m_error = Error::NON_FINITE_VALUE;
        return false;
    }
    const std::uint64_t bits = DoubleBits(value);
    if (bits == UINT64_C(0x8000000000000000) ||
        (kind == EventKind::IMPULSE && bits == 0))
    {
        m_error = Error::NON_CANONICAL_VALUE;
        return false;
    }

    Event event;
    event.target_id = target_id;
    event.control_id = control_id;
    event.kind = kind;
    event.value = value;
    try
    {
        m_events.push_back(event);
    }
    catch (const std::bad_alloc&)
    {
        m_error = Error::ALLOCATION_FAILURE;
        return false;
    }
    catch (const std::length_error&)
    {
        m_error = Error::ALLOCATION_FAILURE;
        return false;
    }
    return true;
}

bool SampleCollector::IsValid() const
{
    return m_error == Error::NONE;
}

Error SampleCollector::GetError() const
{
    return m_error;
}

std::size_t SampleCollector::GetEventCount() const
{
    return m_events.size();
}

StepInjection::StepInjection():
    physics_step(0),
    persistent_state(),
    persistent_deltas(),
    impulses()
{
}

RuntimeContinuation::RuntimeContinuation():
    schema_version(RUNTIME_CONTINUATION_SCHEMA_VERSION),
    mode(RuntimeMode::NONE),
    lifecycle(RuntimeLifecycle::IDLE),
    limits(),
    processed_steps(0),
    next_physics_step(0),
    trace_digest(),
    authenticated_trace(),
    authentication_digest()
{
}

void RuntimeContinuation::Swap(RuntimeContinuation& other)
{
    using std::swap;
    swap(schema_version, other.schema_version);
    swap(mode, other.mode);
    swap(lifecycle, other.lifecycle);
    swap(limits, other.limits);
    swap(processed_steps, other.processed_steps);
    swap(next_physics_step, other.next_physics_step);
    swap(trace_digest, other.trace_digest);
    authenticated_trace.swap(other.authenticated_trace);
    swap(authentication_digest, other.authentication_digest);
}

struct Runtime::Impl
{
    RuntimeMode mode;
    RuntimeLifecycle lifecycle;
    RuntimeStatus status;
    Metadata metadata;
    Limits limits;
    ReplayState state;
    Digest trace_digest;
    std::uint64_t processed_steps;
    std::uint64_t total_replay_steps;
    std::vector<Frame> record_frames;
    std::unique_ptr<std::ostringstream> record_stream;
    std::unique_ptr<Writer> writer;
    std::string authenticated_trace;
    std::unique_ptr<std::istringstream> replay_stream;
    std::unique_ptr<Reader> reader;

    Impl():
        mode(RuntimeMode::NONE),
        lifecycle(RuntimeLifecycle::IDLE),
        status(),
        metadata(),
        limits(),
        state(),
        trace_digest(),
        processed_steps(0),
        total_replay_steps(0),
        record_frames(),
        record_stream(),
        writer(),
        authenticated_trace(),
        replay_stream(),
        reader()
    {
    }

    bool Fail(
        RuntimeError error,
        std::uint64_t physics_step,
        const Status* trace_failure = nullptr)
    {
        if (status.error == RuntimeError::NONE)
        {
            status.error = error;
            status.physics_step = physics_step;
            status.processed_steps = processed_steps;
            if (trace_failure != nullptr)
                status.trace_status = *trace_failure;
        }
        lifecycle = RuntimeLifecycle::FAULTED;
        return false;
    }

    bool FailTrace(const Status& trace_failure, std::uint64_t physics_step)
    {
        const RuntimeError runtime_error =
            trace_failure.error == Error::IO_FAILURE ?
                RuntimeError::IO_FAILURE :
            trace_failure.error == Error::ALLOCATION_FAILURE ?
                RuntimeError::ALLOCATION_FAILURE :
                RuntimeError::TRACE_FAILURE;
        return Fail(runtime_error, physics_step, &trace_failure);
    }
};

Runtime::Runtime():
    m_impl(new Impl())
{
}

Runtime::~Runtime()
{
}

bool Runtime::BeginRecording(
    const Metadata& identity,
    const Limits& requested_limits)
{
    if (m_impl->lifecycle != RuntimeLifecycle::IDLE)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }

    const Limits limits = ClampLimits(requested_limits);
    try
    {
        std::unique_ptr<std::ostringstream> stream(
            new std::ostringstream(std::ios::out | std::ios::binary));
        std::unique_ptr<Writer> writer(
            new Writer(*stream, identity, limits));
        if (!writer->IsReady())
            return m_impl->FailTrace(
                writer->GetStatus(),
                identity.first_physics_step);

        m_impl->metadata = identity;
        m_impl->limits = limits;
        m_impl->mode = RuntimeMode::RECORD;
        m_impl->lifecycle = RuntimeLifecycle::RUNNING;
        m_impl->state = ReplayState();
        m_impl->trace_digest = writer->GetTraceDigest();
        m_impl->processed_steps = 0;
        m_impl->record_stream = std::move(stream);
        m_impl->writer = std::move(writer);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            identity.first_physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            identity.first_physics_step);
    }
}

bool Runtime::BeginReplay(
    const std::string& authenticated_trace,
    const Metadata& expected_identity,
    const Limits& requested_limits)
{
    if (m_impl->lifecycle != RuntimeLifecycle::IDLE)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }

    const Limits limits = ClampLimits(requested_limits);
    if (authenticated_trace.size() > limits.max_bytes)
    {
        Status trace_status;
        SetSyntheticTraceStatus(
            trace_status,
            Error::BYTE_LIMIT_EXCEEDED,
            limits.max_bytes);
        return m_impl->FailTrace(
            trace_status,
            expected_identity.first_physics_step);
    }

    PreflightResult preflight;
    Status trace_status;
    bool identity_matches = false;
    if (!PreflightAuthenticatedTrace(
            authenticated_trace,
            expected_identity,
            limits,
            false,
            preflight,
            trace_status,
            identity_matches))
    {
        return m_impl->FailTrace(
            trace_status,
            expected_identity.first_physics_step);
    }
    if (!identity_matches)
    {
        return m_impl->Fail(
            RuntimeError::IDENTITY_MISMATCH,
            expected_identity.first_physics_step);
    }

    try
    {
        std::string trace_copy(authenticated_trace);
        std::unique_ptr<std::istringstream> stream(
            new std::istringstream(
                trace_copy,
                std::ios::in | std::ios::binary));
        std::unique_ptr<Reader> reader(new Reader(*stream, limits));
        if (!reader->IsReady())
            return m_impl->FailTrace(
                reader->GetStatus(),
                expected_identity.first_physics_step);

        m_impl->metadata = expected_identity;
        m_impl->limits = limits;
        m_impl->mode = RuntimeMode::REPLAY;
        m_impl->lifecycle =
            preflight.step_count == 0 ?
                RuntimeLifecycle::FINISHED :
                RuntimeLifecycle::RUNNING;
        m_impl->state = ReplayState();
        m_impl->trace_digest = preflight.digest;
        m_impl->processed_steps = 0;
        m_impl->total_replay_steps = preflight.step_count;
        m_impl->authenticated_trace.swap(trace_copy);
        m_impl->replay_stream = std::move(stream);
        m_impl->reader = std::move(reader);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            expected_identity.first_physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            expected_identity.first_physics_step);
    }
}

bool Runtime::BeginReplay(
    std::istream& authenticated_trace,
    const Metadata& expected_identity,
    const Limits& requested_limits)
{
    if (m_impl->lifecycle != RuntimeLifecycle::IDLE)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }
    const Limits limits = ClampLimits(requested_limits);
    std::string bytes;
    Status trace_status;
    if (!ReadAllBounded(
            authenticated_trace,
            limits.max_bytes,
            bytes,
            trace_status))
    {
        return m_impl->FailTrace(
            trace_status,
            expected_identity.first_physics_step);
    }
    return BeginReplay(bytes, expected_identity, limits);
}

bool Runtime::Pause()
{
    if (m_impl->lifecycle != RuntimeLifecycle::RUNNING)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }
    m_impl->lifecycle = RuntimeLifecycle::PAUSED;
    return true;
}

bool Runtime::Resume()
{
    if (m_impl->lifecycle != RuntimeLifecycle::PAUSED)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }
    m_impl->lifecycle = RuntimeLifecycle::RUNNING;
    return true;
}

bool Runtime::RecordFixedStep(
    std::uint64_t physics_step,
    FixedStepSampleSource& source)
{
    if (m_impl->mode != RuntimeMode::RECORD ||
        m_impl->lifecycle != RuntimeLifecycle::RUNNING ||
        !m_impl->writer)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            physics_step);
    }
    bool accepts_runtime = false;
    try
    {
        accepts_runtime = source.AcceptsRuntime(*this);
    }
    catch (...)
    {
        return m_impl->Fail(
            RuntimeError::SOURCE_EXCEPTION,
            physics_step);
    }
    if (!accepts_runtime)
    {
        return m_impl->Fail(
            RuntimeError::SOURCE_REJECTED,
            physics_step);
    }
    const std::uint64_t expected = GetNextPhysicsStep();
    if (physics_step != expected)
    {
        return m_impl->Fail(
            RuntimeError::FIXED_STEP_MISMATCH,
            physics_step);
    }
    if (m_impl->processed_steps >= m_impl->limits.max_steps)
    {
        Status trace_status;
        SetSyntheticTraceStatus(
            trace_status,
            Error::STEP_LIMIT_EXCEEDED,
            m_impl->writer->GetBytesWritten(),
            m_impl->processed_steps);
        return m_impl->FailTrace(trace_status, physics_step);
    }

    const std::uint64_t used_events = m_impl->writer->GetEventCount();
    const std::uint64_t remaining_events =
        used_events >= m_impl->limits.max_events ?
            0 :
            m_impl->limits.max_events - used_events;
    const std::uint32_t collector_limit =
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                m_impl->limits.max_events_per_step,
                remaining_events));
    SampleCollector collector(collector_limit);
    bool sampled = false;
    try
    {
        sampled = source.SampleFixedStepStart(
            physics_step,
            collector);
    }
    catch (...)
    {
        return m_impl->Fail(
            RuntimeError::SOURCE_EXCEPTION,
            physics_step);
    }
    if (!collector.IsValid())
    {
        Status trace_status;
        SetSyntheticTraceStatus(
            trace_status,
            collector.GetError(),
            m_impl->writer->GetBytesWritten(),
            m_impl->processed_steps,
            static_cast<std::uint32_t>(
                collector.GetEventCount()));
        return m_impl->FailTrace(trace_status, physics_step);
    }
    if (!sampled)
    {
        return m_impl->Fail(
            RuntimeError::SOURCE_REJECTED,
            physics_step);
    }

    Frame frame;
    frame.physics_step = physics_step;
    frame.events.swap(collector.m_events);
    ReplayState next_state;
    try
    {
        next_state = m_impl->state;
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    Error replay_error = Error::NONE;
    std::uint32_t replay_event_index = 0;
    if (!next_state.Apply(
            frame,
            m_impl->limits.max_active_controls,
            &replay_error,
            &replay_event_index))
    {
        Status trace_status;
        SetSyntheticTraceStatus(
            trace_status,
            replay_error,
            m_impl->writer->GetBytesWritten(),
            m_impl->processed_steps,
            replay_event_index);
        return m_impl->FailTrace(trace_status, physics_step);
    }

    try
    {
        m_impl->record_frames.push_back(frame);
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }

    bool appended = false;
    try
    {
        appended = m_impl->writer->Append(
            m_impl->record_frames.back());
    }
    catch (const std::bad_alloc&)
    {
        m_impl->record_frames.pop_back();
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    catch (const std::length_error&)
    {
        m_impl->record_frames.pop_back();
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    if (!appended)
    {
        m_impl->record_frames.pop_back();
        return m_impl->FailTrace(
            m_impl->writer->GetStatus(),
            physics_step);
    }

    m_impl->state = std::move(next_state);
    m_impl->trace_digest = m_impl->writer->GetTraceDigest();
    ++m_impl->processed_steps;
    m_impl->status.processed_steps = m_impl->processed_steps;
    m_impl->status.physics_step = physics_step;
    return true;
}

bool Runtime::ReplayFixedStep(
    std::uint64_t physics_step,
    FixedStepInjectionSink& sink)
{
    if (m_impl->mode != RuntimeMode::REPLAY ||
        m_impl->lifecycle != RuntimeLifecycle::RUNNING ||
        !m_impl->reader)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            physics_step);
    }
    bool accepts_runtime = false;
    try
    {
        accepts_runtime = sink.AcceptsRuntime(*this);
    }
    catch (...)
    {
        return m_impl->Fail(
            RuntimeError::SINK_EXCEPTION,
            physics_step);
    }
    if (!accepts_runtime)
    {
        return m_impl->Fail(
            RuntimeError::SINK_REJECTED,
            physics_step);
    }
    const std::uint64_t expected = GetNextPhysicsStep();
    if (physics_step != expected)
    {
        return m_impl->Fail(
            RuntimeError::FIXED_STEP_MISMATCH,
            physics_step);
    }

    Frame frame;
    const ReadResult result = m_impl->reader->ReadNext(frame);
    if (result != ReadResult::FRAME)
    {
        if (result == ReadResult::READ_ERROR)
        {
            return m_impl->FailTrace(
                m_impl->reader->GetStatus(),
                physics_step);
        }
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            physics_step);
    }

    StepInjection injection;
    ReplayState next_state;
    try
    {
        injection.physics_step = frame.physics_step;
        injection.persistent_state =
            m_impl->reader->GetReplayState().GetControls();
        for (std::size_t index = 0; index < frame.events.size(); ++index)
        {
            if (frame.events[index].kind == EventKind::STATE)
                injection.persistent_deltas.push_back(frame.events[index]);
            else
                injection.impulses.push_back(frame.events[index]);
        }
        next_state = m_impl->reader->GetReplayState();
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            physics_step);
    }

    const bool is_last =
        m_impl->processed_steps + 1U ==
            m_impl->total_replay_steps;
    if (is_last)
    {
        Frame sentinel;
        const ReadResult terminal =
            m_impl->reader->ReadNext(sentinel);
        if (terminal != ReadResult::END ||
            m_impl->reader->GetStatus().error != Error::NONE)
        {
            if (m_impl->reader->GetStatus().error != Error::NONE)
            {
                return m_impl->FailTrace(
                    m_impl->reader->GetStatus(),
                    physics_step);
            }
            return m_impl->Fail(
                RuntimeError::TRACE_FAILURE,
                physics_step);
        }
    }

    bool injected = false;
    try
    {
        injected = sink.InjectFixedStepStart(injection);
    }
    catch (...)
    {
        return m_impl->Fail(
            RuntimeError::SINK_EXCEPTION,
            physics_step);
    }
    if (!injected)
    {
        return m_impl->Fail(
            RuntimeError::SINK_REJECTED,
            physics_step);
    }

    m_impl->state = std::move(next_state);
    ++m_impl->processed_steps;
    m_impl->status.processed_steps = m_impl->processed_steps;
    m_impl->status.physics_step = physics_step;
    if (is_last)
        m_impl->lifecycle = RuntimeLifecycle::FINISHED;
    return true;
}

bool Runtime::FinalizeRecording(std::string& output)
{
    if (m_impl->mode != RuntimeMode::RECORD ||
        (m_impl->lifecycle != RuntimeLifecycle::RUNNING &&
         m_impl->lifecycle != RuntimeLifecycle::PAUSED))
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }

    std::string bytes;
    Digest digest;
    Status trace_status;
    if (!BuildAuthenticatedTrace(
            m_impl->metadata,
            m_impl->limits,
            m_impl->record_frames,
            bytes,
            digest,
            trace_status))
    {
        return m_impl->FailTrace(
            trace_status,
            GetNextPhysicsStep());
    }

    std::string caller_bytes;
    try
    {
        caller_bytes = bytes;
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            GetNextPhysicsStep());
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            GetNextPhysicsStep());
    }

    m_impl->authenticated_trace.swap(bytes);
    output.swap(caller_bytes);
    m_impl->trace_digest = digest;
    m_impl->lifecycle = RuntimeLifecycle::FINISHED;
    m_impl->writer.reset();
    m_impl->record_stream.reset();
    return true;
}

bool Runtime::FinalizeRecording(std::ostream& output)
{
    if (m_impl->mode != RuntimeMode::RECORD ||
        (m_impl->lifecycle != RuntimeLifecycle::RUNNING &&
         m_impl->lifecycle != RuntimeLifecycle::PAUSED))
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }

    std::string bytes;
    Digest digest;
    Status trace_status;
    if (!BuildAuthenticatedTrace(
            m_impl->metadata,
            m_impl->limits,
            m_impl->record_frames,
            bytes,
            digest,
            trace_status))
    {
        return m_impl->FailTrace(
            trace_status,
            GetNextPhysicsStep());
    }

    const std::size_t max_stream_chunk =
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max());
    static_assert(
        std::numeric_limits<std::streamsize>::max() > 0,
        "std::streamsize must represent positive write sizes");
    const std::size_t output_chunk_size =
        std::min<std::size_t>(UINT32_C(65536), max_stream_chunk);

    try
    {
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const std::size_t chunk =
                std::min(output_chunk_size, bytes.size() - offset);
            output.write(
                bytes.data() + offset,
                static_cast<std::streamsize>(chunk));
            if (!output.good())
            {
                return m_impl->Fail(
                    RuntimeError::IO_FAILURE,
                    GetNextPhysicsStep());
            }
            offset += chunk;
        }
        output.flush();
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            GetNextPhysicsStep());
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            GetNextPhysicsStep());
    }
    catch (...)
    {
        return m_impl->Fail(
            RuntimeError::IO_FAILURE,
            GetNextPhysicsStep());
    }
    if (!output.good())
    {
        return m_impl->Fail(
            RuntimeError::IO_FAILURE,
            GetNextPhysicsStep());
    }

    m_impl->authenticated_trace.swap(bytes);
    m_impl->trace_digest = digest;
    m_impl->lifecycle = RuntimeLifecycle::FINISHED;
    m_impl->writer.reset();
    m_impl->record_stream.reset();
    return true;
}

bool Runtime::ExportContinuation(RuntimeContinuation& output)
{
    if (m_impl->mode == RuntimeMode::NONE ||
        m_impl->lifecycle == RuntimeLifecycle::IDLE ||
        m_impl->lifecycle == RuntimeLifecycle::FAULTED)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            GetNextPhysicsStep());
    }

    RuntimeContinuation candidate;
    candidate.mode = m_impl->mode;
    candidate.lifecycle = m_impl->lifecycle;
    candidate.limits = m_impl->limits;
    candidate.processed_steps = m_impl->processed_steps;
    candidate.next_physics_step = GetNextPhysicsStep();

    if (m_impl->mode == RuntimeMode::RECORD &&
        m_impl->lifecycle != RuntimeLifecycle::FINISHED)
    {
        Status trace_status;
        if (!BuildAuthenticatedTrace(
                m_impl->metadata,
                m_impl->limits,
                m_impl->record_frames,
                candidate.authenticated_trace,
                candidate.trace_digest,
                trace_status))
        {
            return m_impl->FailTrace(
                trace_status,
                GetNextPhysicsStep());
        }
    }
    else
    {
        try
        {
            candidate.authenticated_trace =
                m_impl->authenticated_trace;
        }
        catch (const std::bad_alloc&)
        {
            return m_impl->Fail(
                RuntimeError::ALLOCATION_FAILURE,
                GetNextPhysicsStep());
        }
        catch (const std::length_error&)
        {
            return m_impl->Fail(
                RuntimeError::ALLOCATION_FAILURE,
                GetNextPhysicsStep());
        }
        candidate.trace_digest = m_impl->trace_digest;
    }

    Status authentication_status;
    if (!BuildContinuationAuthenticationDigest(
            candidate,
            candidate.authentication_digest,
            authentication_status))
    {
        return m_impl->FailTrace(
            authentication_status,
            GetNextPhysicsStep());
    }

    output.Swap(candidate);
    return true;
}

bool Runtime::ImportContinuation(
    const RuntimeContinuation& continuation,
    const Metadata& expected_identity)
{
    if (m_impl->lifecycle != RuntimeLifecycle::IDLE)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_TRANSITION,
            expected_identity.first_physics_step);
    }
    if (continuation.schema_version !=
            RUNTIME_CONTINUATION_SCHEMA_VERSION ||
        (continuation.mode != RuntimeMode::RECORD &&
         continuation.mode != RuntimeMode::REPLAY) ||
        (continuation.lifecycle != RuntimeLifecycle::RUNNING &&
         continuation.lifecycle != RuntimeLifecycle::PAUSED &&
         continuation.lifecycle != RuntimeLifecycle::FINISHED))
    {
        return m_impl->Fail(
            RuntimeError::INVALID_CONTINUATION,
            expected_identity.first_physics_step);
    }

    const Limits limits = ClampLimits(continuation.limits);
    if (!LimitsEqual(limits, continuation.limits) ||
        continuation.authenticated_trace.size() > limits.max_bytes)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_CONTINUATION,
            expected_identity.first_physics_step);
    }

    const bool retain_frames =
        continuation.mode == RuntimeMode::RECORD;
    PreflightResult preflight;
    Status trace_status;
    bool identity_matches = false;
    if (!PreflightAuthenticatedTrace(
            continuation.authenticated_trace,
            expected_identity,
            limits,
            retain_frames,
            preflight,
            trace_status,
            identity_matches))
    {
        return m_impl->FailTrace(
            trace_status,
            expected_identity.first_physics_step);
    }
    if (!identity_matches)
    {
        return m_impl->Fail(
            RuntimeError::IDENTITY_MISMATCH,
            expected_identity.first_physics_step);
    }
    if (continuation.trace_digest != preflight.digest ||
        continuation.processed_steps > preflight.step_count ||
        AddWouldOverflow(
            expected_identity.first_physics_step,
            continuation.processed_steps) ||
        continuation.next_physics_step !=
            expected_identity.first_physics_step +
                continuation.processed_steps)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_CONTINUATION,
            expected_identity.first_physics_step);
    }

    Digest expected_authentication_digest;
    Status authentication_status;
    if (!BuildContinuationAuthenticationDigest(
            continuation,
            expected_authentication_digest,
            authentication_status))
    {
        return m_impl->FailTrace(
            authentication_status,
            expected_identity.first_physics_step);
    }
    if (continuation.authentication_digest !=
        expected_authentication_digest)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_CONTINUATION,
            expected_identity.first_physics_step);
    }
    if (continuation.mode == RuntimeMode::RECORD &&
        continuation.processed_steps != preflight.step_count)
    {
        return m_impl->Fail(
            RuntimeError::INVALID_CONTINUATION,
            continuation.next_physics_step);
    }
    if (continuation.mode == RuntimeMode::REPLAY)
    {
        const bool at_end =
            continuation.processed_steps == preflight.step_count;
        if ((at_end &&
             continuation.lifecycle != RuntimeLifecycle::FINISHED) ||
            (!at_end &&
             continuation.lifecycle == RuntimeLifecycle::FINISHED))
        {
            return m_impl->Fail(
                RuntimeError::INVALID_CONTINUATION,
                continuation.next_physics_step);
        }
    }

    try
    {
        std::unique_ptr<std::ostringstream> record_stream;
        std::unique_ptr<Writer> writer;
        std::unique_ptr<std::istringstream> replay_stream;
        std::unique_ptr<Reader> reader;
        std::string trace_copy;
        ReplayState current_state;
        Digest current_digest = continuation.trace_digest;

        if (continuation.mode == RuntimeMode::RECORD &&
            continuation.lifecycle != RuntimeLifecycle::FINISHED)
        {
            record_stream.reset(
                new std::ostringstream(
                    std::ios::out | std::ios::binary));
            writer.reset(
                new Writer(
                    *record_stream,
                    expected_identity,
                    limits));
            if (!writer->IsReady())
            {
                return m_impl->FailTrace(
                    writer->GetStatus(),
                    continuation.next_physics_step);
            }
            for (std::size_t index = 0;
                 index < preflight.frames.size();
                 ++index)
            {
                if (!writer->Append(preflight.frames[index]))
                {
                    return m_impl->FailTrace(
                        writer->GetStatus(),
                        continuation.next_physics_step);
                }
            }
            current_state = writer->GetReplayState();
            current_digest = writer->GetTraceDigest();
        }
        else if (continuation.mode == RuntimeMode::REPLAY &&
                 continuation.lifecycle != RuntimeLifecycle::FINISHED)
        {
            trace_copy = continuation.authenticated_trace;
            replay_stream.reset(
                new std::istringstream(
                    trace_copy,
                    std::ios::in | std::ios::binary));
            reader.reset(new Reader(*replay_stream, limits));
            if (!reader->IsReady())
            {
                return m_impl->FailTrace(
                    reader->GetStatus(),
                    continuation.next_physics_step);
            }
            Frame skipped;
            for (std::uint64_t index = 0;
                 index < continuation.processed_steps;
                 ++index)
            {
                if (reader->ReadNext(skipped) != ReadResult::FRAME)
                {
                    return m_impl->FailTrace(
                        reader->GetStatus(),
                        continuation.next_physics_step);
                }
            }
            current_state = reader->GetReplayState();
        }
        else
        {
            current_state = preflight.state;
            trace_copy = continuation.authenticated_trace;
        }

        m_impl->metadata = expected_identity;
        m_impl->limits = limits;
        m_impl->mode = continuation.mode;
        m_impl->lifecycle = continuation.lifecycle;
        m_impl->state = std::move(current_state);
        m_impl->trace_digest = current_digest;
        m_impl->processed_steps = continuation.processed_steps;
        m_impl->total_replay_steps =
            continuation.mode == RuntimeMode::REPLAY ?
                preflight.step_count :
                0;
        m_impl->record_frames.swap(preflight.frames);
        m_impl->record_stream = std::move(record_stream);
        m_impl->writer = std::move(writer);
        if (continuation.mode == RuntimeMode::REPLAY ||
            continuation.lifecycle == RuntimeLifecycle::FINISHED)
        {
            if (trace_copy.empty())
                trace_copy = continuation.authenticated_trace;
            m_impl->authenticated_trace.swap(trace_copy);
        }
        m_impl->replay_stream = std::move(replay_stream);
        m_impl->reader = std::move(reader);
        m_impl->status.processed_steps = m_impl->processed_steps;
        m_impl->status.physics_step =
            m_impl->processed_steps == 0 ?
                expected_identity.first_physics_step :
                continuation.next_physics_step - 1U;
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            continuation.next_physics_step);
    }
    catch (const std::length_error&)
    {
        return m_impl->Fail(
            RuntimeError::ALLOCATION_FAILURE,
            continuation.next_physics_step);
    }
}

RuntimeMode Runtime::GetMode() const
{
    return m_impl->mode;
}

RuntimeLifecycle Runtime::GetLifecycle() const
{
    return m_impl->lifecycle;
}

const RuntimeStatus& Runtime::GetStatus() const
{
    return m_impl->status;
}

const Metadata& Runtime::GetIdentity() const
{
    return m_impl->metadata;
}

const Limits& Runtime::GetLimits() const
{
    return m_impl->limits;
}

std::uint64_t Runtime::GetProcessedStepCount() const
{
    return m_impl->processed_steps;
}

std::uint64_t Runtime::GetNextPhysicsStep() const
{
    if (AddWouldOverflow(
            m_impl->metadata.first_physics_step,
            m_impl->processed_steps))
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return m_impl->metadata.first_physics_step +
        m_impl->processed_steps;
}

const ReplayState& Runtime::GetPersistentState() const
{
    return m_impl->state;
}

const Digest& Runtime::GetTraceDigest() const
{
    return m_impl->trace_digest;
}

const std::string& Runtime::GetAuthenticatedTrace() const
{
    return m_impl->authenticated_trace;
}

const char* ToString(RuntimeError error)
{
    switch (error)
    {
    case RuntimeError::NONE: return "none";
    case RuntimeError::INVALID_TRANSITION: return "invalid_transition";
    case RuntimeError::IDENTITY_MISMATCH: return "identity_mismatch";
    case RuntimeError::FIXED_STEP_MISMATCH: return "fixed_step_mismatch";
    case RuntimeError::SOURCE_REJECTED: return "source_rejected";
    case RuntimeError::SOURCE_EXCEPTION: return "source_exception";
    case RuntimeError::SINK_REJECTED: return "sink_rejected";
    case RuntimeError::SINK_EXCEPTION: return "sink_exception";
    case RuntimeError::TRACE_FAILURE: return "trace_failure";
    case RuntimeError::INVALID_CONTINUATION:
        return "invalid_continuation";
    case RuntimeError::IO_FAILURE: return "io_failure";
    case RuntimeError::ALLOCATION_FAILURE: return "allocation_failure";
    }
    return "unknown_runtime_error";
}

const char* ToString(RuntimeMode mode)
{
    switch (mode)
    {
    case RuntimeMode::NONE: return "none";
    case RuntimeMode::RECORD: return "record";
    case RuntimeMode::REPLAY: return "replay";
    }
    return "unknown_runtime_mode";
}

const char* ToString(RuntimeLifecycle lifecycle)
{
    switch (lifecycle)
    {
    case RuntimeLifecycle::IDLE: return "idle";
    case RuntimeLifecycle::RUNNING: return "running";
    case RuntimeLifecycle::PAUSED: return "paused";
    case RuntimeLifecycle::FINISHED: return "finished";
    case RuntimeLifecycle::FAULTED: return "faulted";
    }
    return "unknown_runtime_lifecycle";
}

} // namespace DeterministicInputTrace
} // namespace RoR
