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
/// @brief Canonical, bounded fixed-step input recording and replay contract.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace RoR {
namespace DeterministicInputTrace {

static const std::uint32_t SCHEMA_VERSION = 1;
static const std::uint32_t HEADER_PREFIX_SIZE = 112;
static const std::uint32_t DIGEST_SIZE = 32;
static const std::uint32_t HEADER_MIN_SIZE =
    HEADER_PREFIX_SIZE + DIGEST_SIZE;
static const std::uint32_t EVENT_SIZE = 24;
static const std::uint32_t FRAME_PREFIX_SIZE = 24;
static const std::uint32_t FRAME_MIN_SIZE =
    FRAME_PREFIX_SIZE + DIGEST_SIZE;
static const std::uint32_t TRAILER_SIZE = 104;

// Immutable format ceilings. Caller-supplied Limits may only tighten them.
static const std::uint64_t MAX_TRACE_STEPS = UINT64_C(16777216);
static const std::uint64_t MAX_TRACE_EVENTS = UINT64_C(67108864);
static const std::uint64_t MAX_TRACE_BYTES = UINT64_C(4294967296);
static const std::uint32_t MAX_EVENTS_PER_STEP = 4096;
static const std::uint32_t MAX_ACTIVE_CONTROLS = 65536;
static const std::uint32_t MAX_IDENTITY_STRING_BYTES = 255;
static const std::uint64_t MAX_PHYSICS_RATE_COMPONENT =
    UINT64_C(1000000000000);

/// Version 1 deliberately contains no worker-count field. Input is sampled
/// after device aggregation, at the beginning of an exact fixed physics step,
/// before any worker dispatch. `target_id` is a stable scenario-assigned
/// logical ID, never a runtime actor index or worker-local ID.
enum SemanticFlags : std::uint32_t
{
    SEMANTIC_FIXED_STEP_START = UINT32_C(1) << 0,
    SEMANTIC_PERSISTENT_DELTAS = UINT32_C(1) << 1,
    SEMANTIC_WORKER_INDEPENDENT = UINT32_C(1) << 2
};

static const std::uint32_t REQUIRED_SEMANTIC_FLAGS =
    SEMANTIC_FIXED_STEP_START |
    SEMANTIC_PERSISTENT_DELTAS |
    SEMANTIC_WORKER_INDEPENDENT;

struct Digest
{
    std::array<std::uint8_t, DIGEST_SIZE> bytes;

    Digest();
    std::string ToHex() const;
    bool operator==(const Digest& other) const;
    bool operator!=(const Digest& other) const;
};

/// Run identity and cadence. The two names are canonical UTF-8 byte strings:
/// no controls, overlong encodings, surrogates, or Unicode noncharacters.
/// `source_digest` identifies the exact scenario/content/configuration source
/// and must not be all zero. `stream_id` is the stable input-source namespace.
struct Metadata
{
    std::uint32_t semantic_flags;
    std::uint64_t scenario_id;
    std::uint64_t stream_id;
    std::uint64_t first_physics_step;
    std::uint64_t physics_step_numerator;
    std::uint64_t physics_step_denominator;
    std::string scenario_name;
    std::string source_name;
    Digest source_digest;

    Metadata();
};

enum class EventKind : std::uint32_t
{
    /// Persistent absolute scalar. Positive zero removes the control from the
    /// active-state set; repeating its exact current value is noncanonical.
    STATE = 1,
    /// One-step action delivered exactly once and never retained.
    IMPULSE = 2
};

/// Post-aggregation input addressed by stable logical target/action IDs.
/// Values are exact IEEE-754 binary64 payloads. NaN, infinity, negative zero,
/// zero impulses, duplicate keys, and redundant persistent assignments are
/// rejected.
struct Event
{
    std::uint64_t target_id;
    std::uint32_t control_id;
    EventKind kind;
    double value;

    Event();
};

/// One record exists for every simulated fixed step, including steps with no
/// changed input. The initial value of every persistent control is positive
/// zero, so the first frame must establish every nonzero control active at
/// capture start. A paused simulation emits no records and consumes no step.
struct Frame
{
    std::uint64_t physics_step;
    std::vector<Event> events;

    Frame();
};

struct PersistentControl
{
    std::uint64_t target_id;
    std::uint32_t control_id;
    double value;

    PersistentControl();
};

struct Limits
{
    std::uint64_t max_steps;
    std::uint64_t max_events;
    std::uint64_t max_bytes;
    std::uint32_t max_events_per_step;
    std::uint32_t max_active_controls;
    std::uint32_t max_identity_string_bytes;

    Limits();
};

enum class Error
{
    NONE = 0,
    INVALID_METADATA,
    INVALID_IDENTITY_STRING,
    IO_FAILURE,
    TRUNCATED,
    MAGIC_MISMATCH,
    UNSUPPORTED_SCHEMA,
    INVALID_HEADER_SIZE,
    INVALID_SEMANTICS,
    RESERVED_FIELD_NONZERO,
    INTEGRITY_MISMATCH,
    INVALID_RECORD_TAG,
    INVALID_RECORD_SIZE,
    NON_CONTIGUOUS_STEP,
    UNKNOWN_EVENT_KIND,
    INVALID_CONTROL_ID,
    NON_FINITE_VALUE,
    NON_CANONICAL_VALUE,
    NON_CANONICAL_EVENT_ORDER,
    REDUNDANT_STATE_EVENT,
    STEP_LIMIT_EXCEEDED,
    EVENT_LIMIT_EXCEEDED,
    ACTIVE_CONTROL_LIMIT_EXCEEDED,
    BYTE_LIMIT_EXCEEDED,
    STRING_LIMIT_EXCEEDED,
    ARITHMETIC_OVERFLOW,
    ALLOCATION_FAILURE,
    SUMMARY_MISMATCH,
    TRAILING_DATA,
    ALREADY_FINISHED
};

struct Status
{
    Error error;
    std::uint64_t byte_offset;
    std::uint64_t step_index;
    std::uint32_t event_index;

    Status();
};

/// Bounded persistent input state. It is copyable so a save/load boundary can
/// preserve the exact replay continuation state. Impulses are intentionally
/// not retained.
class ReplayState
{
public:
    ReplayState();

    bool Apply(
        const Frame& frame,
        std::uint32_t max_active_controls = MAX_ACTIVE_CONTROLS,
        Error* error = nullptr,
        std::uint32_t* error_event_index = nullptr);

    bool GetValue(
        std::uint64_t target_id,
        std::uint32_t control_id,
        double& value) const;
    const std::vector<PersistentControl>& GetControls() const;
    std::size_t GetControlCount() const;
    bool operator==(const ReplayState& other) const;
    bool operator!=(const ReplayState& other) const;

private:
    std::vector<PersistentControl> m_controls;
};

/// Streaming writer. Finish() is mandatory. It appends an authenticated
/// summary; an unfinished stream is always invalid. Calls may be arbitrarily
/// segmented or delayed without changing bytes because no wall clock enters
/// the contract.
class Writer
{
public:
    Writer(
        std::ostream& output,
        const Metadata& metadata,
        const Limits& limits = Limits());

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool IsReady() const;
    bool Append(const Frame& frame);
    bool Finish();

    const Status& GetStatus() const;
    const ReplayState& GetReplayState() const;
    const Digest& GetTraceDigest() const;
    std::uint64_t GetStepCount() const;
    std::uint64_t GetEventCount() const;
    std::uint64_t GetBytesWritten() const;

private:
    bool Fail(
        Error error,
        std::uint64_t byte_offset,
        std::uint32_t event_index = 0);
    bool WriteBytes(const std::uint8_t* data, std::size_t size);

    std::ostream& m_output;
    Metadata m_metadata;
    Limits m_limits;
    Status m_status;
    ReplayState m_replay_state;
    Digest m_chain_digest;
    std::uint64_t m_step_count;
    std::uint64_t m_event_count;
    std::uint64_t m_bytes_written;
    std::uint64_t m_next_physics_step;
    bool m_finished;
};

enum class ReadResult
{
    FRAME,
    END,
    READ_ERROR
};

/// Streaming reader. Every frame's hash-chain link is checked before the frame
/// is exposed. Consumers must still read through END so the mandatory trailer,
/// summary, final digest, and absence of trailing data are verified.
class Reader
{
public:
    Reader(
        std::istream& input,
        const Limits& limits = Limits());

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool IsReady() const;
    ReadResult ReadNext(Frame& frame);

    const Metadata& GetMetadata() const;
    const Status& GetStatus() const;
    const ReplayState& GetReplayState() const;
    const Digest& GetTraceDigest() const;
    std::uint64_t GetStepCount() const;
    std::uint64_t GetEventCount() const;
    std::uint64_t GetBytesRead() const;
    std::uint64_t GetNextPhysicsStep() const;

private:
    bool Fail(
        Error error,
        std::uint64_t byte_offset,
        std::uint32_t event_index = 0);
    bool ReadBytes(std::uint8_t* data, std::size_t size);
    bool ReadHeader();
    bool ReadFrame(
        const std::uint8_t prefix[FRAME_PREFIX_SIZE],
        Frame& frame);
    bool ReadAndValidateTrailer(
        const std::uint8_t prefix[8]);

    std::istream& m_input;
    Metadata m_metadata;
    Limits m_limits;
    Status m_status;
    ReplayState m_replay_state;
    Digest m_chain_digest;
    std::uint64_t m_step_count;
    std::uint64_t m_event_count;
    std::uint64_t m_bytes_read;
    std::uint64_t m_next_physics_step;
    bool m_finished;
};

enum class ComparisonStatus
{
    MATCH,
    DIVERGED,
    INVALID_INPUT
};

enum class Difference
{
    NONE = 0,
    METADATA,
    PHYSICS_STEP,
    EVENT_COUNT,
    EVENT_KEY,
    EVENT_KIND,
    EVENT_VALUE,
    TRACE_LENGTH,
    LEFT_INVALID,
    RIGHT_INVALID,
    BOTH_INVALID
};

enum class MetadataField
{
    NONE = 0,
    SEMANTICS,
    SCENARIO_ID,
    STREAM_ID,
    FIRST_PHYSICS_STEP,
    PHYSICS_STEP_RATE,
    SCENARIO_NAME,
    SOURCE_NAME,
    SOURCE_DIGEST
};

struct ComparisonResult
{
    ComparisonStatus status;
    Difference difference;
    MetadataField metadata_field;
    Status left_error;
    Status right_error;
    std::uint64_t steps_compared;
    std::uint64_t first_divergent_step;
    std::uint32_t first_divergent_event;
    bool has_first_divergent_step;
    bool has_first_divergent_event;

    ComparisonResult();
};

/// Compares canonical streams without retaining the run. Any corruption in a
/// later byte overrides an earlier semantic divergence with INVALID_INPUT.
ComparisonResult Compare(
    std::istream& left,
    std::istream& right,
    const Limits& limits = Limits());

const char* ToString(Error error);
const char* ToString(Difference difference);
const char* ToString(MetadataField field);

} // namespace DeterministicInputTrace
} // namespace RoR
