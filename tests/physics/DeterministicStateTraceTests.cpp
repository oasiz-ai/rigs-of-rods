#include "DeterministicStateTrace.h"
#include "DeterministicStateTraceCli.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

std::uint32_t LoadU32(const std::string& bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

std::uint64_t LoadU64(const std::string& bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

void StoreU32(std::string& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index)
    {
        bytes[offset + index] =
            static_cast<char>(value >> (index * 8U));
    }
}

void StoreU64(std::string& bytes, std::size_t offset, std::uint64_t value)
{
    for (unsigned int index = 0; index < 8; ++index)
    {
        bytes[offset + index] =
            static_cast<char>(value >> (index * 8U));
    }
}

std::uint32_t UpdateCrc32(
    std::uint32_t state,
    const char* bytes,
    std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        state ^= static_cast<unsigned char>(bytes[index]);
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

std::uint32_t ComputeCrc32(
    const std::string& bytes,
    std::size_t offset,
    std::size_t size)
{
    return UpdateCrc32(
        UINT32_C(0xffffffff),
        bytes.data() + offset,
        size) ^ UINT32_C(0xffffffff);
}

void RepairHeaderCrc(std::string& bytes)
{
    StoreU32(
        bytes,
        76,
        ComputeCrc32(
            bytes,
            0,
            RoR::DeterministicStateTrace::HEADER_SIZE - 4));
}

void RepairFrameCrc(std::string& bytes, std::size_t frame_offset)
{
    StoreU32(
        bytes,
        frame_offset +
            RoR::DeterministicStateTrace::STEP_RECORD_SIZE - 4,
        ComputeCrc32(
            bytes,
            frame_offset,
            RoR::DeterministicStateTrace::STEP_RECORD_SIZE - 4));
}

void RepairTrailerCrc(std::string& bytes, std::size_t trailer_offset)
{
    StoreU32(
        bytes,
        trailer_offset + 52,
        ComputeCrc32(
            bytes,
            trailer_offset,
            RoR::DeterministicStateTrace::TRAILER_SIZE - 4));
}

RoR::DeterministicStateTrace::Metadata MakeMetadata(
    std::uint32_t workers = 1)
{
    RoR::DeterministicStateTrace::Metadata metadata;
    metadata.worker_count = workers;
    metadata.scenario_id = UINT64_C(0x1122334455667788);
    metadata.first_physics_step = 100;
    metadata.physics_step_numerator = 1;
    metadata.physics_step_denominator = 2000;
    metadata.physics_flags = 0;
    return metadata;
}

RoR::DeterministicStateTrace::StepRecord MakeStep(
    std::uint64_t physics_step,
    std::uint8_t digest_seed,
    std::uint32_t actor_count,
    std::uint32_t contact_count)
{
    RoR::DeterministicStateTrace::StepRecord step;
    step.physics_step = physics_step;
    step.actor_count = actor_count;
    step.contact_count = contact_count;
    for (std::size_t index = 0; index < step.digest.bytes.size(); ++index)
    {
        step.digest.bytes[index] =
            static_cast<std::uint8_t>(digest_seed + index * 7U);
    }
    step.input_flags = RoR::DeterministicStateTrace::
        STEP_INPUT_AUTHENTICATED_PREFIX;
    for (std::size_t index = 0; index < step.input_digest.size(); ++index)
    {
        step.input_digest[index] =
            static_cast<std::uint8_t>(digest_seed ^ (index * 11U + 3U));
    }
    return step;
}

std::vector<RoR::DeterministicStateTrace::StepRecord> MakeSteps()
{
    std::vector<RoR::DeterministicStateTrace::StepRecord> steps;
    steps.push_back(MakeStep(100, 0x10, 2, 4));
    steps.push_back(MakeStep(101, 0x20, 3, 7));
    steps.push_back(MakeStep(102, 0x30, 3, 0));
    return steps;
}

std::string WriteTrace(
    const RoR::DeterministicStateTrace::Metadata& metadata,
    const std::vector<RoR::DeterministicStateTrace::StepRecord>& steps,
    const RoR::DeterministicStateTrace::Limits&
        limits = RoR::DeterministicStateTrace::Limits())
{
    std::ostringstream output(std::ios::out | std::ios::binary);
    RoR::DeterministicStateTrace::Writer writer(output, metadata, limits);
    CHECK(writer.IsReady());
    for (std::size_t index = 0; index < steps.size(); ++index)
        CHECK(writer.Append(steps[index]));
    CHECK(writer.Finish());
    CHECK(writer.GetStatus().error ==
        RoR::DeterministicStateTrace::Error::NONE);
    return output.str();
}

bool IsValidTrace(
    const std::string& bytes,
    RoR::DeterministicStateTrace::Error* error = nullptr)
{
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    RoR::DeterministicStateTrace::Reader reader(input);
    if (reader.IsReady())
    {
        RoR::DeterministicStateTrace::StepRecord step;
        while (reader.ReadNext(step) ==
            RoR::DeterministicStateTrace::ReadResult::STEP)
        {
        }
    }
    if (error != nullptr)
        *error = reader.GetStatus().error;
    return reader.GetStatus().error ==
        RoR::DeterministicStateTrace::Error::NONE;
}

RoR::DeterministicStateTrace::ComparisonResult CompareBytes(
    const std::string& left,
    const std::string& right,
    bool allow_worker_difference = false)
{
    std::istringstream left_stream(
        left,
        std::ios::in | std::ios::binary);
    std::istringstream right_stream(
        right,
        std::ios::in | std::ios::binary);
    RoR::DeterministicStateTrace::ComparisonOptions options;
    options.allow_worker_count_difference = allow_worker_difference;
    return RoR::DeterministicStateTrace::Compare(
        left_stream,
        right_stream,
        options);
}

void TestRoundTripAndFormat()
{
    using namespace RoR::DeterministicStateTrace;

    const Metadata metadata = MakeMetadata(8);
    const std::vector<StepRecord> steps = MakeSteps();
    const std::string bytes = WriteTrace(metadata, steps);
    CHECK(bytes.size() ==
        HEADER_SIZE + steps.size() * STEP_RECORD_SIZE + TRAILER_SIZE);
    CHECK(bytes.substr(0, 12) == "RoR-D0-Trace");
    CHECK(LoadU32(bytes, 16) == SCHEMA_VERSION);
    CHECK(LoadU32(bytes, 20) == HEADER_SIZE);
    CHECK(LoadU32(bytes, 24) ==
        RoR::DeterministicStateDigest::SCHEMA_VERSION);
    CHECK(LoadU32(bytes, 28) == 8);
    CHECK(LoadU64(bytes, 32) == metadata.scenario_id);
    CHECK(LoadU64(bytes, 40) == 100);
    CHECK(LoadU64(bytes, 48) == 1);
    CHECK(LoadU64(bytes, 56) == 2000);
    CHECK(LoadU32(bytes, 64) == 0);

    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Reader reader(input);
    CHECK(reader.IsReady());
    CHECK(reader.GetMetadata().worker_count == 8);
    CHECK(reader.GetMetadata().scenario_id == metadata.scenario_id);
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        StepRecord observed;
        CHECK(reader.ReadNext(observed) == ReadResult::STEP);
        CHECK(observed.physics_step == steps[index].physics_step);
        CHECK(observed.actor_count == steps[index].actor_count);
        CHECK(observed.contact_count == steps[index].contact_count);
        CHECK(observed.digest.bytes == steps[index].digest.bytes);
        CHECK(observed.input_flags == steps[index].input_flags);
        CHECK(observed.input_digest == steps[index].input_digest);
    }
    StepRecord sentinel = MakeStep(999, 0xff, 1, 1);
    CHECK(reader.ReadNext(sentinel) == ReadResult::END);
    CHECK(reader.ReadNext(sentinel) == ReadResult::END);
    CHECK(reader.GetStatus().error == Error::NONE);
    CHECK(reader.GetStepCount() == steps.size());
    CHECK(reader.GetBytesRead() == bytes.size());

    const std::vector<StepRecord> empty;
    const std::string empty_bytes = WriteTrace(metadata, empty);
    CHECK(empty_bytes.size() == HEADER_SIZE + TRAILER_SIZE);
    CHECK(IsValidTrace(empty_bytes));
}

void TestWriterValidationAndLimits()
{
    using namespace RoR::DeterministicStateTrace;

    Metadata metadata = MakeMetadata();
    std::ostringstream output(std::ios::out | std::ios::binary);
    metadata.worker_count = 0;
    Writer invalid_worker(output, metadata);
    CHECK(!invalid_worker.IsReady());
    CHECK(invalid_worker.GetStatus().error == Error::INVALID_METADATA);

    metadata = MakeMetadata();
    metadata.worker_count = MAX_WORKERS + 1;
    std::ostringstream output2(std::ios::out | std::ios::binary);
    Writer too_many_workers(output2, metadata);
    CHECK(!too_many_workers.IsReady());

    metadata = MakeMetadata();
    metadata.physics_step_numerator = 2;
    metadata.physics_step_denominator = 4000;
    std::ostringstream output3(std::ios::out | std::ios::binary);
    Writer noncanonical_rate(output3, metadata);
    CHECK(!noncanonical_rate.IsReady());

    metadata = MakeMetadata();
    metadata.physics_step_numerator = 2;
    metadata.physics_step_denominator = 1;
    std::ostringstream output4(std::ios::out | std::ios::binary);
    Writer over_one_second(output4, metadata);
    CHECK(!over_one_second.IsReady());

    metadata = MakeMetadata();
    metadata.physics_flags = UINT32_C(0x80000000);
    std::ostringstream output5(std::ios::out | std::ios::binary);
    Writer unknown_flags(output5, metadata);
    CHECK(!unknown_flags.IsReady());

    metadata = MakeMetadata();
    metadata.state_digest_schema_version += 1;
    std::ostringstream output6(std::ios::out | std::ios::binary);
    Writer wrong_digest_schema(output6, metadata);
    CHECK(!wrong_digest_schema.IsReady());

    metadata = MakeMetadata();
    StepRecord unknown_input_flag = MakeStep(100, 1, 0, 0);
    unknown_input_flag.input_flags |= UINT32_C(1) << 31;
    std::ostringstream output6b(std::ios::out | std::ios::binary);
    Writer invalid_input_flag(output6b, metadata);
    CHECK(!invalid_input_flag.Append(unknown_input_flag));
    CHECK(invalid_input_flag.GetStatus().error ==
        Error::INVALID_INPUT_BINDING);

    StepRecord unbound_nonzero_digest = MakeStep(100, 1, 0, 0);
    unbound_nonzero_digest.input_flags = 0;
    std::ostringstream output6c(std::ios::out | std::ios::binary);
    Writer invalid_unbound_digest(output6c, metadata);
    CHECK(!invalid_unbound_digest.Append(unbound_nonzero_digest));
    CHECK(invalid_unbound_digest.GetStatus().error ==
        Error::INVALID_INPUT_BINDING);

    StepRecord explicit_no_input = MakeStep(100, 1, 0, 0);
    explicit_no_input.input_flags = 0;
    explicit_no_input.input_digest.fill(0U);
    std::ostringstream output6d(std::ios::out | std::ios::binary);
    Writer valid_no_input(output6d, metadata);
    CHECK(valid_no_input.Append(explicit_no_input));
    CHECK(valid_no_input.Finish());

    Limits tiny_bytes;
    tiny_bytes.max_bytes = HEADER_SIZE + TRAILER_SIZE - 1;
    metadata = MakeMetadata();
    std::ostringstream output7(std::ios::out | std::ios::binary);
    Writer no_room_for_valid_trace(output7, metadata, tiny_bytes);
    CHECK(!no_room_for_valid_trace.IsReady());
    CHECK(no_room_for_valid_trace.GetStatus().error ==
        Error::BYTE_LIMIT_EXCEEDED);

    Limits no_room_for_step;
    no_room_for_step.max_bytes = HEADER_SIZE + TRAILER_SIZE;
    std::ostringstream output7b(std::ios::out | std::ios::binary);
    Writer step_byte_limited(output7b, metadata, no_room_for_step);
    CHECK(step_byte_limited.IsReady());
    CHECK(!step_byte_limited.Append(MakeStep(100, 1, 0, 0)));
    CHECK(step_byte_limited.GetStatus().error ==
        Error::BYTE_LIMIT_EXCEEDED);

    Limits one_step;
    one_step.max_steps = 1;
    std::ostringstream output8(std::ios::out | std::ios::binary);
    Writer limited(output8, metadata, one_step);
    CHECK(limited.Append(MakeStep(100, 1, 1, 1)));
    CHECK(!limited.Append(MakeStep(101, 2, 1, 1)));
    CHECK(limited.GetStatus().error == Error::STEP_LIMIT_EXCEEDED);

    std::ostringstream output9(std::ios::out | std::ios::binary);
    Writer noncontiguous(output9, metadata);
    CHECK(!noncontiguous.Append(MakeStep(101, 1, 1, 1)));
    CHECK(noncontiguous.GetStatus().error == Error::NON_CONTIGUOUS_STEP);

    std::ostringstream output10(std::ios::out | std::ios::binary);
    Writer actor_limit(output10, metadata);
    CHECK(!actor_limit.Append(MakeStep(
        100,
        1,
        RoR::DeterministicStateDigest::MAX_ACTORS + 1,
        0)));
    CHECK(actor_limit.GetStatus().error == Error::COUNT_LIMIT_EXCEEDED);

    std::ostringstream output11(std::ios::out | std::ios::binary);
    Writer contact_limit(output11, metadata);
    CHECK(!contact_limit.Append(MakeStep(
        100,
        1,
        0,
        RoR::DeterministicStateDigest::MAX_CONTACTS + 1)));
    CHECK(contact_limit.GetStatus().error == Error::COUNT_LIMIT_EXCEEDED);

    metadata.first_physics_step =
        std::numeric_limits<std::uint64_t>::max();
    std::ostringstream output12(std::ios::out | std::ios::binary);
    Writer overflow(output12, metadata);
    CHECK(overflow.Append(MakeStep(
        std::numeric_limits<std::uint64_t>::max(),
        1,
        0,
        0)));
    CHECK(!overflow.Append(MakeStep(0, 2, 0, 0)));
    CHECK(overflow.GetStatus().error == Error::ARITHMETIC_OVERFLOW);

    metadata = MakeMetadata();
    std::ostringstream output13(std::ios::out | std::ios::binary);
    Writer finished(output13, metadata);
    CHECK(finished.Finish());
    CHECK(!finished.Finish());
    CHECK(finished.GetStatus().error == Error::ALREADY_FINISHED);
}

void TestEveryPrefixIsDetectedAsTruncated()
{
    using namespace RoR::DeterministicStateTrace;

    const std::string complete = WriteTrace(MakeMetadata(), MakeSteps());
    for (std::size_t size = 0; size < complete.size(); ++size)
    {
        const std::string truncated = complete.substr(0, size);
        Error error = Error::NONE;
        CHECK(!IsValidTrace(truncated, &error));
        CHECK(error == Error::TRUNCATED);
    }
}

void TestEverySingleBitCorruptionIsDetected()
{
    using namespace RoR::DeterministicStateTrace;

    const std::string complete = WriteTrace(MakeMetadata(), MakeSteps());
    for (std::size_t index = 0; index < complete.size(); ++index)
    {
        std::string corrupted = complete;
        corrupted[index] =
            static_cast<char>(
                static_cast<unsigned char>(corrupted[index]) ^ 1U);
        CHECK(!IsValidTrace(corrupted));
    }

    std::string trailing = complete;
    trailing.push_back('x');
    Error error = Error::NONE;
    CHECK(!IsValidTrace(trailing, &error));
    CHECK(error == Error::TRAILING_DATA);
}

void TestStreamsWithExceptionMasks()
{
    using namespace RoR::DeterministicStateTrace;

    const std::string complete = WriteTrace(MakeMetadata(), MakeSteps());
    std::istringstream input(
        complete,
        std::ios::in | std::ios::binary);
    input.exceptions(
        std::ios::badbit | std::ios::failbit | std::ios::eofbit);
    Reader reader(input);
    StepRecord step;
    while (reader.ReadNext(step) == ReadResult::STEP)
    {
    }
    CHECK(reader.GetStatus().error == Error::NONE);
    CHECK(reader.GetStepCount() == MakeSteps().size());

    std::istringstream truncated(
        complete.substr(0, complete.size() - 1),
        std::ios::in | std::ios::binary);
    truncated.exceptions(
        std::ios::badbit | std::ios::failbit | std::ios::eofbit);
    Reader truncated_reader(truncated);
    while (truncated_reader.ReadNext(step) == ReadResult::STEP)
    {
    }
    CHECK(truncated_reader.GetStatus().error == Error::TRUNCATED);
}

void TestReaderStructuralFailures()
{
    using namespace RoR::DeterministicStateTrace;

    const std::string complete = WriteTrace(MakeMetadata(), MakeSteps());
    const std::size_t first_frame = HEADER_SIZE;
    const std::size_t trailer =
        HEADER_SIZE + MakeSteps().size() * STEP_RECORD_SIZE;
    Error error = Error::NONE;

    std::string malformed = complete;
    StoreU32(malformed, 16, SCHEMA_VERSION + 1);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::UNSUPPORTED_SCHEMA);

    malformed = complete;
    StoreU32(malformed, 20, HEADER_SIZE + 4);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::INVALID_HEADER_SIZE);

    malformed = complete;
    StoreU32(
        malformed,
        24,
        RoR::DeterministicStateDigest::SCHEMA_VERSION + 1);
    RepairHeaderCrc(malformed);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::DIGEST_SCHEMA_MISMATCH);

    malformed = complete;
    StoreU32(malformed, 68, 1);
    RepairHeaderCrc(malformed);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::RESERVED_FIELD_NONZERO);

    malformed = complete;
    StoreU32(malformed, first_frame, UINT32_C(0xdeadbeef));
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::INVALID_RECORD_TAG);

    malformed = complete;
    StoreU32(malformed, first_frame + 4, STEP_RECORD_SIZE + 1);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::INVALID_RECORD_SIZE);

    malformed = complete;
    StoreU64(malformed, first_frame + 8, 101);
    RepairFrameCrc(malformed, first_frame);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::NON_CONTIGUOUS_STEP);

    malformed = complete;
    StoreU32(
        malformed,
        first_frame + 16,
        RoR::DeterministicStateDigest::MAX_ACTORS + 1);
    RepairFrameCrc(malformed, first_frame);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::COUNT_LIMIT_EXCEEDED);

    malformed = complete;
    StoreU32(malformed, first_frame + 56, UINT32_C(1) << 31);
    RepairFrameCrc(malformed, first_frame);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::INVALID_INPUT_BINDING);

    malformed = complete;
    StoreU32(malformed, first_frame + 56, 0);
    RepairFrameCrc(malformed, first_frame);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::INVALID_INPUT_BINDING);

    malformed = complete;
    malformed[first_frame + 24] ^= 1;
    RepairFrameCrc(malformed, first_frame);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::CHECKSUM_MISMATCH);

    malformed = complete;
    StoreU64(
        malformed,
        32,
        LoadU64(malformed, 32) + 1);
    RepairHeaderCrc(malformed);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::CHECKSUM_MISMATCH);

    malformed = complete;
    StoreU64(malformed, trailer + 8, 4);
    RepairTrailerCrc(malformed, trailer);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::SUMMARY_MISMATCH);

    malformed = complete;
    StoreU32(malformed, trailer + 44, 1);
    RepairTrailerCrc(malformed, trailer);
    CHECK(!IsValidTrace(malformed, &error));
    CHECK(error == Error::RESERVED_FIELD_NONZERO);

    Limits one_step;
    one_step.max_steps = 1;
    std::istringstream limited_input(
        complete,
        std::ios::in | std::ios::binary);
    Reader limited_reader(limited_input, one_step);
    StepRecord step;
    CHECK(limited_reader.ReadNext(step) == ReadResult::STEP);
    CHECK(limited_reader.ReadNext(step) == ReadResult::READ_ERROR);
    CHECK(limited_reader.GetStatus().error == Error::STEP_LIMIT_EXCEEDED);

    Limits tiny_bytes;
    tiny_bytes.max_bytes = HEADER_SIZE + TRAILER_SIZE - 1;
    std::istringstream byte_limited_input(
        complete,
        std::ios::in | std::ios::binary);
    Reader byte_limited_reader(byte_limited_input, tiny_bytes);
    CHECK(!byte_limited_reader.IsReady());
    CHECK(byte_limited_reader.GetStatus().error ==
        Error::BYTE_LIMIT_EXCEEDED);
}

void TestComparisonAndFirstDivergence()
{
    using namespace RoR::DeterministicStateTrace;

    const std::vector<StepRecord> baseline_steps = MakeSteps();
    const std::string baseline =
        WriteTrace(MakeMetadata(1), baseline_steps);
    ComparisonResult result = CompareBytes(baseline, baseline);
    CHECK(result.status == ComparisonStatus::MATCH);
    CHECK(result.difference == Difference::NONE);
    CHECK(result.steps_compared == baseline_steps.size());

    const std::string workers_eight =
        WriteTrace(MakeMetadata(8), baseline_steps);
    result = CompareBytes(baseline, workers_eight);
    CHECK(result.status == ComparisonStatus::DIVERGED);
    CHECK(result.difference == Difference::METADATA);
    CHECK(result.metadata_field == MetadataField::WORKER_COUNT);
    result = CompareBytes(baseline, workers_eight, true);
    CHECK(result.status == ComparisonStatus::MATCH);
    CHECK(result.steps_compared == baseline_steps.size());

    Metadata changed_metadata = MakeMetadata();
    changed_metadata.scenario_id += 1;
    result = CompareBytes(
        baseline,
        WriteTrace(changed_metadata, baseline_steps));
    CHECK(result.status == ComparisonStatus::DIVERGED);
    CHECK(result.metadata_field == MetadataField::SCENARIO_ID);

    changed_metadata = MakeMetadata();
    changed_metadata.physics_step_numerator = 1;
    changed_metadata.physics_step_denominator = 1000;
    result = CompareBytes(
        baseline,
        WriteTrace(changed_metadata, baseline_steps));
    CHECK(result.metadata_field == MetadataField::PHYSICS_STEP_RATE);

    changed_metadata = MakeMetadata();
    changed_metadata.physics_flags = PHYSICS_FLAG_FAST_MATH;
    result = CompareBytes(
        baseline,
        WriteTrace(changed_metadata, baseline_steps));
    CHECK(result.metadata_field == MetadataField::PHYSICS_FLAGS);

    std::vector<StepRecord> changed_steps = baseline_steps;
    changed_steps[1].input_digest[13] ^= 1U;
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed_steps));
    CHECK(result.status == ComparisonStatus::DIVERGED);
    CHECK(result.difference == Difference::INPUT_DIGEST);
    CHECK(result.steps_compared == 1);
    CHECK(result.first_divergent_step == 101);

    changed_steps = baseline_steps;
    changed_steps[1].digest.bytes[13] ^= 1U;
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed_steps));
    CHECK(result.status == ComparisonStatus::DIVERGED);
    CHECK(result.difference == Difference::DIGEST);
    CHECK(result.steps_compared == 1);
    CHECK(result.has_first_divergent_step);
    CHECK(result.first_divergent_step == 101);
    CHECK(result.has_left_step);
    CHECK(result.has_right_step);

    changed_steps = baseline_steps;
    changed_steps[2].actor_count += 1;
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed_steps));
    CHECK(result.difference == Difference::ACTOR_COUNT);
    CHECK(result.steps_compared == 2);
    CHECK(result.first_divergent_step == 102);

    changed_steps = baseline_steps;
    changed_steps[0].contact_count += 1;
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed_steps));
    CHECK(result.difference == Difference::CONTACT_COUNT);
    CHECK(result.steps_compared == 0);
    CHECK(result.first_divergent_step == 100);

    changed_steps = baseline_steps;
    changed_steps.pop_back();
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed_steps));
    CHECK(result.difference == Difference::TRACE_LENGTH);
    CHECK(result.steps_compared == 2);
    CHECK(result.first_divergent_step == 102);
    CHECK(result.has_left_step);
    CHECK(!result.has_right_step);

    const std::string truncated =
        baseline.substr(0, baseline.size() - 1);
    result = CompareBytes(truncated, baseline);
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::LEFT_INVALID);
    CHECK(result.left_error.error == Error::TRUNCATED);

    result = CompareBytes(truncated, truncated);
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::BOTH_INVALID);

    Metadata different_scenario = MakeMetadata();
    different_scenario.scenario_id += 1;
    const std::string metadata_mismatch_then_truncation =
        WriteTrace(different_scenario, baseline_steps);
    result = CompareBytes(
        baseline,
        metadata_mismatch_then_truncation.substr(
            0,
            metadata_mismatch_then_truncation.size() - 1));
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::RIGHT_INVALID);
    CHECK(result.right_error.error == Error::TRUNCATED);

    changed_steps = baseline_steps;
    changed_steps[0].digest.bytes[0] ^= 1U;
    const std::string early_divergence_then_truncation =
        WriteTrace(MakeMetadata(), changed_steps);
    result = CompareBytes(
        baseline,
        early_divergence_then_truncation.substr(
            0,
            early_divergence_then_truncation.size() - 1));
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::RIGHT_INVALID);
    CHECK(result.right_error.error == Error::TRUNCATED);
}

void TestCanonicalJsonReport()
{
    using namespace RoR::DeterministicStateTrace;

    std::vector<StepRecord> changed = MakeSteps();
    changed[1].digest.bytes[0] ^= 1U;
    const ComparisonResult result = CompareBytes(
        WriteTrace(MakeMetadata(), MakeSteps()),
        WriteTrace(MakeMetadata(), changed));
    const std::string json = FormatComparisonJson(
        result,
        "left\"trace",
        "right\\trace\n");
    CHECK(!json.empty());
    CHECK(json[json.size() - 1] == '\n');
    CHECK(json.find(
        "\"format\":\"ror-d0-state-trace-comparison-v2\"") !=
        std::string::npos);
    CHECK(json.find("\"status\":\"diverged\"") != std::string::npos);
    CHECK(json.find("\"difference\":\"digest\"") != std::string::npos);
    CHECK(json.find("\"steps_compared\":1") != std::string::npos);
    CHECK(json.find("\"first_divergent_step\":101") !=
        std::string::npos);
    CHECK(json.find("\"input_digest\":\"") !=
        std::string::npos);
    CHECK(json.find("left\\\"trace") != std::string::npos);
    CHECK(json.find("right\\\\trace\\n") != std::string::npos);
}

std::string UniqueTestPath(const char* suffix)
{
    std::ostringstream path;
    path << "ror_state_trace_test_"
         << reinterpret_cast<std::uintptr_t>(&g_failures)
         << '_' << suffix << ".bin";
    return path.str();
}

bool WriteFile(const std::string& path, const std::string& bytes)
{
    std::ofstream output(path.c_str(), std::ios::out | std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

int RunCli(
    const std::vector<std::string>& arguments,
    std::string& output,
    std::string& error)
{
    std::vector<const char*> argv;
    for (std::size_t index = 0; index < arguments.size(); ++index)
        argv.push_back(arguments[index].c_str());
    std::ostringstream output_stream;
    std::ostringstream error_stream;
    const int result =
        RoR::DeterministicStateTrace::RunComparisonCli(
            static_cast<int>(argv.size()),
            argv.empty() ? nullptr : argv.data(),
            output_stream,
            error_stream);
    output = output_stream.str();
    error = error_stream.str();
    return result;
}

void TestComparisonCli()
{
    using namespace RoR::DeterministicStateTrace;

    const std::string left_path = UniqueTestPath("left");
    const std::string right_path = UniqueTestPath("right");
    const std::string invalid_path = UniqueTestPath("invalid");
    const std::string missing_path = UniqueTestPath("missing");
    const std::vector<StepRecord> steps = MakeSteps();
    CHECK(WriteFile(left_path, WriteTrace(MakeMetadata(1), steps)));
    CHECK(WriteFile(right_path, WriteTrace(MakeMetadata(1), steps)));
    CHECK(WriteFile(
        invalid_path,
        WriteTrace(MakeMetadata(1), steps).substr(0, 100)));
    std::remove(missing_path.c_str());

    std::string output;
    std::string error;
    int result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", left_path, right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_MATCH);
    CHECK(output.find("\"status\":\"match\"") != std::string::npos);
    CHECK(error.empty());

    CHECK(WriteFile(right_path, WriteTrace(MakeMetadata(8), steps)));
    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", left_path, right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_DIVERGED);
    CHECK(output.find("\"metadata_field\":\"worker_count\"") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace",
            "--allow-worker-count-difference",
            left_path,
            right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_MATCH);
    CHECK(output.find("\"status\":\"match\"") != std::string::npos);

    std::vector<StepRecord> changed = steps;
    changed[2].digest.bytes[31] ^= 1U;
    CHECK(WriteFile(right_path, WriteTrace(MakeMetadata(1), changed)));
    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", left_path, right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_DIVERGED);
    CHECK(output.find("\"first_divergent_step\":102") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", invalid_path, right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(output.find("\"status\":\"invalid_input\"") !=
        std::string::npos);
    CHECK(output.find("\"code\":\"truncated\"") != std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", missing_path, right_path},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(output.find("\"difference\":\"left_open_failed\"") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{"ror-state-trace", "--help"},
        output,
        error);
    CHECK(result == CLI_EXIT_MATCH);
    CHECK(output.find("Usage:") != std::string::npos);
    CHECK(output.find("--inspect TRACE.trace") != std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", "--inspect", left_path},
        output,
        error);
    CHECK(result == CLI_EXIT_MATCH);
    CHECK(error.empty());
    CHECK(output.find(
        "\"format\":\"ror-d0-state-trace-inspection-v2\"") !=
        std::string::npos);
    CHECK(output.find("\"status\":\"valid\"") !=
        std::string::npos);
    CHECK(output.find("\"step_count\":3") != std::string::npos);
    CHECK(output.find(
        "\"contact_summary\":{\"total_contact_count\":11,"
        "\"contact_step_count\":2,\"maximum_contact_count\":7,"
        "\"first_contact_physics_step\":100,"
        "\"last_contact_physics_step\":101}") !=
        std::string::npos);
    CHECK(output.find("\"has_final_step\":true") !=
        std::string::npos);
    CHECK(output.find("\"physics_step\":102") != std::string::npos);
    CHECK(output.find(
        "\"state_digest\":\"" + steps.back().digest.ToHex() + "\"") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", "--inspect", invalid_path},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(error.empty());
    CHECK(output.find("\"status\":\"invalid_input\"") !=
        std::string::npos);
    CHECK(output.find("\"code\":\"truncated\"") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace", "--inspect", missing_path},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(output.find("\"code\":\"open_failed\"") !=
        std::string::npos);

    result = RunCli(
        std::vector<std::string>{
            "ror-state-trace",
            "--inspect",
            "--allow-worker-count-difference",
            left_path},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(!error.empty());

    result = RunCli(
        std::vector<std::string>{"ror-state-trace", "--unknown"},
        output,
        error);
    CHECK(result == CLI_EXIT_INVALID);
    CHECK(!error.empty());

    std::remove(left_path.c_str());
    std::remove(right_path.c_str());
    std::remove(invalid_path.c_str());
}

} // namespace

int main()
{
    TestRoundTripAndFormat();
    TestWriterValidationAndLimits();
    TestEveryPrefixIsDetectedAsTruncated();
    TestEverySingleBitCorruptionIsDetected();
    TestStreamsWithExceptionMasks();
    TestReaderStructuralFailures();
    TestComparisonAndFirstDivergence();
    TestCanonicalJsonReport();
    TestComparisonCli();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic state-trace checks failed\n";
        return 1;
    }
    std::cout << "All deterministic state-trace checks passed\n";
    return 0;
}
