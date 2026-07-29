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
/// @brief Bounded, versioned per-step deterministic-state trace contract.

#pragma once

#include "DeterministicStateDigest.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

namespace RoR {
namespace DeterministicStateTrace {

static const std::uint32_t SCHEMA_VERSION = 1;
static const std::uint32_t STATE_DIGEST_SCHEMA_VERSION = 1;
static const std::uint32_t HEADER_SIZE = 80;
static const std::uint32_t STEP_RECORD_SIZE = 64;
static const std::uint32_t TRAILER_SIZE = 56;

// These are immutable format ceilings. Limits supplied by a caller may only
// tighten them.
static const std::uint64_t MAX_TRACE_STEPS = UINT64_C(16777216);
static const std::uint64_t MAX_TRACE_BYTES =
    static_cast<std::uint64_t>(HEADER_SIZE) +
    MAX_TRACE_STEPS * static_cast<std::uint64_t>(STEP_RECORD_SIZE) +
    static_cast<std::uint64_t>(TRAILER_SIZE);
static const std::uint32_t MAX_WORKERS = 4096;
static const std::uint64_t MAX_PHYSICS_RATE_COMPONENT =
    UINT64_C(1000000000000);

static_assert(
    DeterministicStateDigest::SCHEMA_VERSION ==
        STATE_DIGEST_SCHEMA_VERSION,
    "state trace v1 must be reviewed when the digest schema changes");

/// Version-1 encoding is fixed-width and little-endian:
///
/// - 80-byte header: magic, trace/digest schemas, worker count, scenario ID,
///   first step, rational step duration, physics flags, reserved zeroes, CRC32.
/// - 64-byte `STEP` record: step, actor/contact counts, 32-byte digest,
///   reserved zero, CRC32.
/// - 56-byte mandatory `END!` trailer: record count, next expected step,
///   accumulated actor/contact counts, aggregate CRC32 over header/record
///   payloads (excluding their individual CRC fields), reserved zeroes, trailer
///   CRC32.
///
/// CRC32 uses the IEEE polynomial and detects accidental storage/transport
/// corruption; it is an integrity check, not authentication against a party
/// able to rewrite both data and checksums.
enum PhysicsFlags : std::uint32_t
{
    PHYSICS_FLAG_FAST_MATH = UINT32_C(1) << 0
};

static const std::uint32_t PHYSICS_FLAG_MASK = PHYSICS_FLAG_FAST_MATH;

/// Run-level identity. The physics step is a canonical rational number of
/// seconds; numerator and denominator must be positive, reduced, and describe
/// a step no longer than one second.
struct Metadata
{
    std::uint32_t state_digest_schema_version;
    std::uint32_t worker_count;
    std::uint64_t scenario_id;
    std::uint64_t first_physics_step;
    std::uint64_t physics_step_numerator;
    std::uint64_t physics_step_denominator;
    std::uint32_t physics_flags;

    Metadata();
};

/// One fixed-step state sample. Counts describe the exact snapshot that
/// produced the digest, not accumulated run totals.
struct StepRecord
{
    std::uint64_t physics_step;
    std::uint32_t actor_count;
    std::uint32_t contact_count;
    DeterministicStateDigest::Digest digest;

    StepRecord();
};

struct Limits
{
    std::uint64_t max_steps;
    std::uint64_t max_bytes;

    Limits();
};

enum class Error
{
    NONE = 0,
    INVALID_METADATA,
    IO_FAILURE,
    TRUNCATED,
    MAGIC_MISMATCH,
    UNSUPPORTED_SCHEMA,
    INVALID_HEADER_SIZE,
    DIGEST_SCHEMA_MISMATCH,
    RESERVED_FIELD_NONZERO,
    CHECKSUM_MISMATCH,
    INVALID_RECORD_TAG,
    INVALID_RECORD_SIZE,
    NON_CONTIGUOUS_STEP,
    COUNT_LIMIT_EXCEEDED,
    STEP_LIMIT_EXCEEDED,
    BYTE_LIMIT_EXCEEDED,
    ARITHMETIC_OVERFLOW,
    SUMMARY_MISMATCH,
    TRAILING_DATA,
    ALREADY_FINISHED
};

struct Status
{
    Error error;
    std::uint64_t byte_offset;
    std::uint64_t step_index;

    Status();
};

/// Streaming writer. Finish() is mandatory: it appends the checked summary
/// trailer used to distinguish a valid short run from a truncated artifact.
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
    bool Append(const StepRecord& record);
    bool Finish();

    const Status& GetStatus() const;
    std::uint64_t GetStepCount() const;
    std::uint64_t GetBytesWritten() const;

private:
    bool Fail(Error error, std::uint64_t byte_offset);
    bool WriteBlock(
        const std::uint8_t* data,
        std::size_t size,
        std::size_t aggregate_payload_size);

    std::ostream& m_output;
    Metadata m_metadata;
    Limits m_limits;
    Status m_status;
    std::uint64_t m_step_count;
    std::uint64_t m_bytes_written;
    std::uint64_t m_total_actor_count;
    std::uint64_t m_total_contact_count;
    std::uint32_t m_aggregate_crc_state;
    bool m_finished;
};

enum class ReadResult
{
    STEP,
    END,
    READ_ERROR
};

/// Streaming fail-closed reader. Reaching byte EOF before a valid trailer is
/// always TRUNCATED. A valid trailer followed by any byte is TRAILING_DATA.
class Reader
{
public:
    Reader(
        std::istream& input,
        const Limits& limits = Limits());

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool IsReady() const;
    ReadResult ReadNext(StepRecord& record);

    const Metadata& GetMetadata() const;
    const Status& GetStatus() const;
    std::uint64_t GetStepCount() const;
    std::uint64_t GetBytesRead() const;

private:
    bool Fail(Error error, std::uint64_t byte_offset);
    bool ReadBlock(std::uint8_t* data, std::size_t size);
    bool ValidateAndFinishTrailer(const std::uint8_t* trailer);

    std::istream& m_input;
    Metadata m_metadata;
    Limits m_limits;
    Status m_status;
    std::uint64_t m_step_count;
    std::uint64_t m_bytes_read;
    std::uint64_t m_total_actor_count;
    std::uint64_t m_total_contact_count;
    std::uint32_t m_aggregate_crc_state;
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
    ACTOR_COUNT,
    CONTACT_COUNT,
    DIGEST,
    TRACE_LENGTH,
    LEFT_INVALID,
    RIGHT_INVALID,
    BOTH_INVALID
};

enum class MetadataField
{
    NONE = 0,
    DIGEST_SCHEMA_VERSION,
    WORKER_COUNT,
    SCENARIO_ID,
    FIRST_PHYSICS_STEP,
    PHYSICS_STEP_RATE,
    PHYSICS_FLAGS
};

struct ComparisonOptions
{
    bool allow_worker_count_difference;

    ComparisonOptions();
};

struct ComparisonResult
{
    ComparisonStatus status;
    Difference difference;
    MetadataField metadata_field;
    Metadata left_metadata;
    Metadata right_metadata;
    Status left_error;
    Status right_error;
    StepRecord left_step;
    StepRecord right_step;
    std::uint64_t steps_compared;
    std::uint64_t first_divergent_step;
    bool has_left_metadata;
    bool has_right_metadata;
    bool has_left_step;
    bool has_right_step;
    bool has_first_divergent_step;

    ComparisonResult();
};

/// Compares two streams without retaining their step records. Metadata is
/// strict by default. The explicit worker-count exception exists for D0's
/// required one-worker versus eight-worker comparison.
ComparisonResult Compare(
    std::istream& left,
    std::istream& right,
    const ComparisonOptions& options = ComparisonOptions(),
    const Limits& limits = Limits());

/// Emits one canonical JSON object suitable for CI artifacts. Labels are
/// escaped as data and never interpreted as JSON fragments.
std::string FormatComparisonJson(
    const ComparisonResult& result,
    const std::string& left_label,
    const std::string& right_label);

const char* ToString(Error error);
const char* ToString(ComparisonStatus status);
const char* ToString(Difference difference);
const char* ToString(MetadataField field);

} // namespace DeterministicStateTrace
} // namespace RoR
