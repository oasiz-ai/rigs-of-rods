#include "DeterministicInputTrace.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Hex(const std::string& bytes)
{
    static const char DIGITS[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        const std::uint8_t byte =
            static_cast<std::uint8_t>(bytes[index]);
        result.push_back(DIGITS[(byte >> 4U) & 0xfU]);
        result.push_back(DIGITS[byte & 0xfU]);
    }
    return result;
}

RoR::DeterministicInputTrace::Metadata MakeMetadata()
{
    using namespace RoR::DeterministicInputTrace;
    Metadata metadata;
    metadata.scenario_id = UINT64_C(0x1122334455667788);
    metadata.stream_id = UINT64_C(0xa1b2c3d4e5f60718);
    metadata.first_physics_step = 100;
    metadata.physics_step_numerator = 1;
    metadata.physics_step_denominator = 2000;
    metadata.scenario_name = "simple2-two-truck";
    metadata.source_name = "keyboard+controller-v1";
    for (std::size_t index = 0;
         index < metadata.source_digest.bytes.size();
         ++index)
    {
        metadata.source_digest.bytes[index] =
            static_cast<std::uint8_t>(3U + index * 7U);
    }
    return metadata;
}

RoR::DeterministicInputTrace::Event MakeEvent(
    std::uint64_t target_id,
    std::uint32_t control_id,
    RoR::DeterministicInputTrace::EventKind kind,
    double value)
{
    RoR::DeterministicInputTrace::Event event;
    event.target_id = target_id;
    event.control_id = control_id;
    event.kind = kind;
    event.value = value;
    return event;
}

RoR::DeterministicInputTrace::Frame MakeFrame(
    std::uint64_t physics_step,
    const std::vector<RoR::DeterministicInputTrace::Event>& events)
{
    RoR::DeterministicInputTrace::Frame frame;
    frame.physics_step = physics_step;
    frame.events = events;
    return frame;
}

std::vector<RoR::DeterministicInputTrace::Frame> MakeFrames()
{
    using namespace RoR::DeterministicInputTrace;
    std::vector<Frame> frames;
    frames.push_back(MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 10, EventKind::STATE, 0.25),
            MakeEvent(1, 20, EventKind::IMPULSE, 1.0)}));
    frames.push_back(MakeFrame(
        101,
        std::vector<Event>{
            MakeEvent(1, 10, EventKind::STATE, 0.5),
            MakeEvent(2, 1, EventKind::STATE, -0.75)}));
    frames.push_back(MakeFrame(
        102,
        std::vector<Event>{
            MakeEvent(1, 10, EventKind::STATE, 0.0),
            MakeEvent(2, 9, EventKind::IMPULSE, -1.0)}));
    frames.push_back(MakeFrame(103, std::vector<Event>()));
    return frames;
}

std::string WriteTrace(
    const RoR::DeterministicInputTrace::Metadata& metadata,
    const std::vector<RoR::DeterministicInputTrace::Frame>& frames,
    RoR::DeterministicInputTrace::Digest* digest = nullptr,
    const RoR::DeterministicInputTrace::Limits&
        limits = RoR::DeterministicInputTrace::Limits(),
    const std::vector<std::size_t>& flush_before =
        std::vector<std::size_t>())
{
    using namespace RoR::DeterministicInputTrace;
    std::ostringstream output(std::ios::out | std::ios::binary);
    Writer writer(output, metadata, limits);
    CHECK(writer.IsReady());
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        if (std::find(
                flush_before.begin(),
                flush_before.end(),
                index) != flush_before.end())
        {
            // A recording pause is an absence of fixed steps. Flushing and
            // resuming later must not introduce a wall-clock segment record.
            output.flush();
        }
        CHECK(writer.Append(frames[index]));
    }
    CHECK(writer.Finish());
    CHECK(writer.GetStatus().error == Error::NONE);
    if (digest != nullptr)
        *digest = writer.GetTraceDigest();
    return output.str();
}

bool IsValid(
    const std::string& bytes,
    RoR::DeterministicInputTrace::Error* error = nullptr,
    const RoR::DeterministicInputTrace::Limits&
        limits = RoR::DeterministicInputTrace::Limits())
{
    using namespace RoR::DeterministicInputTrace;
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Reader reader(input, limits);
    if (reader.IsReady())
    {
        Frame frame;
        while (reader.ReadNext(frame) == ReadResult::FRAME)
        {
        }
    }
    if (error != nullptr)
        *error = reader.GetStatus().error;
    return reader.GetStatus().error == Error::NONE;
}

RoR::DeterministicInputTrace::ComparisonResult CompareBytes(
    const std::string& left,
    const std::string& right)
{
    std::istringstream left_stream(
        left,
        std::ios::in | std::ios::binary);
    std::istringstream right_stream(
        right,
        std::ios::in | std::ios::binary);
    return RoR::DeterministicInputTrace::Compare(
        left_stream,
        right_stream);
}

void TestRoundTripAndReplayState()
{
    using namespace RoR::DeterministicInputTrace;
    const Metadata metadata = MakeMetadata();
    const std::vector<Frame> frames = MakeFrames();
    Digest writer_digest;
    const std::string bytes =
        WriteTrace(metadata, frames, &writer_digest);
    const std::uint32_t expected_header_size =
        HEADER_MIN_SIZE +
        static_cast<std::uint32_t>(metadata.scenario_name.size()) +
        static_cast<std::uint32_t>(metadata.source_name.size());
    CHECK(bytes.size() ==
        expected_header_size +
        FRAME_MIN_SIZE * frames.size() +
        EVENT_SIZE * 6U +
        TRAILER_SIZE);
    CHECK(bytes.substr(0, 12) == "RoR-D0-Input");
    CHECK(LoadU32(bytes, 16) == SCHEMA_VERSION);
    CHECK(LoadU32(bytes, 20) == expected_header_size);
    CHECK(LoadU32(bytes, 24) == REQUIRED_SEMANTIC_FLAGS);
    CHECK(LoadU64(bytes, 32) == metadata.scenario_id);
    CHECK(LoadU64(bytes, 40) == metadata.stream_id);
    CHECK(LoadU64(bytes, 48) == 100);
    CHECK(LoadU64(bytes, 56) == 1);
    CHECK(LoadU64(bytes, 64) == 2000);

    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Reader reader(input);
    CHECK(reader.IsReady());
    CHECK(reader.GetMetadata().scenario_name == metadata.scenario_name);
    CHECK(reader.GetMetadata().source_name == metadata.source_name);
    CHECK(reader.GetMetadata().source_digest == metadata.source_digest);
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        Frame observed;
        CHECK(reader.ReadNext(observed) == ReadResult::FRAME);
        CHECK(observed.physics_step == frames[index].physics_step);
        CHECK(observed.events.size() == frames[index].events.size());
        for (std::size_t event = 0;
             event < observed.events.size();
             ++event)
        {
            CHECK(observed.events[event].target_id ==
                frames[index].events[event].target_id);
            CHECK(observed.events[event].control_id ==
                frames[index].events[event].control_id);
            CHECK(observed.events[event].kind ==
                frames[index].events[event].kind);
            CHECK(DoubleBits(observed.events[event].value) ==
                DoubleBits(frames[index].events[event].value));
        }
    }
    Frame sentinel;
    sentinel.physics_step = 999;
    CHECK(reader.ReadNext(sentinel) == ReadResult::END);
    CHECK(reader.ReadNext(sentinel) == ReadResult::END);
    CHECK(reader.GetStatus().error == Error::NONE);
    CHECK(reader.GetStepCount() == frames.size());
    CHECK(reader.GetEventCount() == 6);
    CHECK(reader.GetBytesRead() == bytes.size());
    CHECK(reader.GetNextPhysicsStep() == 104);
    CHECK(reader.GetTraceDigest() == writer_digest);
    CHECK(reader.GetReplayState().GetControlCount() == 1);
    double value = 0.0;
    CHECK(reader.GetReplayState().GetValue(2, 1, value));
    CHECK(value == -0.75);
    CHECK(!reader.GetReplayState().GetValue(1, 10, value));
    CHECK(DoubleBits(value) == 0);

    const std::vector<Frame> empty;
    CHECK(IsValid(WriteTrace(metadata, empty)));
}

void TestGoldenBytesAndDigest()
{
    using namespace RoR::DeterministicInputTrace;
    Metadata metadata = MakeMetadata();
    metadata.scenario_id = 1;
    metadata.stream_id = 9;
    metadata.first_physics_step = 7;
    metadata.scenario_name = "S";
    metadata.source_name = "I";
    for (std::size_t index = 0;
         index < metadata.source_digest.bytes.size();
         ++index)
    {
        metadata.source_digest.bytes[index] =
            static_cast<std::uint8_t>(index);
    }
    Frame frame = MakeFrame(
        7,
        std::vector<Event>{
            MakeEvent(5, 2, EventKind::STATE, 0.5)});
    Digest digest;
    const std::string bytes =
        WriteTrace(metadata, std::vector<Frame>{frame}, &digest);
    CHECK(bytes.size() == 330);

    const std::string expected_hex =
        "526f522d44302d496e7075740d0a1a0a01000000920000000700000000000000"
        "0100000000000000090000000000000007000000000000000100000000000000"
        "d0070000000000000100000001000000000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f53496ff44e087e641fa3ae78b3957bb6"
        "e7e26906a5266435c2ac1d439415fa6d02314652414d50000000070000000000"
        "0000010000000000000005000000000000000200000001000000000000000000"
        "e03fcd5f72c6505ea701ccc912921cb3859cdd147ed2bb735a8b548bb63b3506"
        "baa6454e44216800000001000000000000000100000000000000080000000000"
        "00000100000000000000cd5f72c6505ea701ccc912921cb3859cdd147ed2bb73"
        "5a8b548bb63b3506baa6d7a7702791bc2fe55b02c1d2b3ed6b889523089d92fd"
        "28d7dc0b498849b337cd";
    const std::string expected_digest =
        "d7a7702791bc2fe55b02c1d2b3ed6b889523089d92fd28d7dc0b498849b337cd";
    CHECK(Hex(bytes) == expected_hex);
    CHECK(digest.ToHex() == expected_digest);
}

void TestPauseResumeSegmentationAndWorkerIndependence()
{
    using namespace RoR::DeterministicInputTrace;
    const Metadata metadata = MakeMetadata();
    const std::vector<Frame> frames = MakeFrames();
    const std::string uninterrupted = WriteTrace(metadata, frames);
    const std::string segmented =
        WriteTrace(
            metadata,
            frames,
            nullptr,
            Limits(),
            std::vector<std::size_t>{1, 2, 3});
    CHECK(segmented == uninterrupted);
    CHECK(CompareBytes(uninterrupted, segmented).status ==
        ComparisonStatus::MATCH);

    // Simulate collection from eight worker buckets. Sorting happens before
    // this kernel boundary; the wire contract has no worker-count field.
    std::vector<Frame> eight_bucket_frames = frames;
    for (std::size_t frame_index = 0;
         frame_index < eight_bucket_frames.size();
         ++frame_index)
    {
        std::sort(
            eight_bucket_frames[frame_index].events.begin(),
            eight_bucket_frames[frame_index].events.end(),
            [](const Event& left, const Event& right)
            {
                if (left.target_id != right.target_id)
                    return left.target_id < right.target_id;
                return left.control_id < right.control_id;
            });
    }
    CHECK(WriteTrace(metadata, eight_bucket_frames) == uninterrupted);
}

void TestLoadingAndContinuation()
{
    using namespace RoR::DeterministicInputTrace;
    const std::vector<Frame> frames = MakeFrames();
    const std::string bytes = WriteTrace(MakeMetadata(), frames);
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Reader reader(input);

    Frame frame;
    CHECK(reader.ReadNext(frame) == ReadResult::FRAME);
    ReplayState loaded_state = reader.GetReplayState();
    CHECK(loaded_state.GetControlCount() == 1);

    while (reader.ReadNext(frame) == ReadResult::FRAME)
    {
        Error error = Error::NONE;
        std::uint32_t event_index = 0;
        CHECK(loaded_state.Apply(
            frame,
            MAX_ACTIVE_CONTROLS,
            &error,
            &event_index));
        CHECK(error == Error::NONE);
    }
    CHECK(reader.GetStatus().error == Error::NONE);
    CHECK(loaded_state == reader.GetReplayState());

    // Impulses never leak across a load boundary.
    double value = 99.0;
    CHECK(!loaded_state.GetValue(1, 20, value));
    CHECK(DoubleBits(value) == 0);
}

void TestFixedSeedLongReplay()
{
    using namespace RoR::DeterministicInputTrace;
    static const std::size_t TARGET_COUNT = 8;
    static const std::size_t CONTROL_COUNT = 16;
    double expected[TARGET_COUNT][CONTROL_COUNT] = {};
    std::vector<Frame> frames;
    frames.reserve(10000);

    for (std::uint64_t offset = 0; offset < 10000; ++offset)
    {
        const std::uint64_t step = 100 + offset;
        std::vector<Event> events;
        if ((offset % 3U) == 0)
        {
            const std::size_t target =
                static_cast<std::size_t>((offset * 5U + 1U) % TARGET_COUNT);
            const std::size_t control =
                static_cast<std::size_t>((offset * 7U + 3U) % CONTROL_COUNT);
            double value = 0.0;
            if (expected[target][control] == 0.0 ||
                (offset % 33U) != 0)
            {
                value =
                    static_cast<double>((offset % 997U) + 1U) / 998.0;
                if ((offset & 1U) != 0)
                    value = -value;
                if (DoubleBits(value) ==
                    DoubleBits(expected[target][control]))
                {
                    value *= 0.5;
                }
            }
            events.push_back(MakeEvent(
                target + 1U,
                static_cast<std::uint32_t>(control + 1U),
                EventKind::STATE,
                value));
            expected[target][control] = value;
        }
        if ((offset % 5U) == 0)
        {
            std::size_t target =
                static_cast<std::size_t>((offset * 11U + 2U) % TARGET_COUNT);
            std::size_t control =
                static_cast<std::size_t>((offset * 13U + 5U) % CONTROL_COUNT);
            if (!events.empty() &&
                events[0].target_id == target + 1U &&
                events[0].control_id == control + 1U)
            {
                control = (control + 1U) % CONTROL_COUNT;
            }
            events.push_back(MakeEvent(
                target + 1U,
                static_cast<std::uint32_t>(control + 1U),
                EventKind::IMPULSE,
                (offset & 1U) == 0 ? 1.0 : -1.0));
        }
        std::sort(
            events.begin(),
            events.end(),
            [](const Event& left, const Event& right)
            {
                if (left.target_id != right.target_id)
                    return left.target_id < right.target_id;
                return left.control_id < right.control_id;
            });
        frames.push_back(MakeFrame(step, events));
    }

    const std::string bytes = WriteTrace(MakeMetadata(), frames);
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Reader reader(input);
    Frame frame;
    ReplayState resumed;
    bool captured_resume = false;
    while (reader.ReadNext(frame) == ReadResult::FRAME)
    {
        if (frame.physics_step == 4321)
        {
            resumed = reader.GetReplayState();
            captured_resume = true;
        }
        else if (captured_resume && frame.physics_step > 4321)
        {
            CHECK(resumed.Apply(frame));
        }
    }
    CHECK(reader.GetStatus().error == Error::NONE);
    CHECK(captured_resume);
    CHECK(resumed == reader.GetReplayState());
    for (std::size_t target = 0; target < TARGET_COUNT; ++target)
    {
        for (std::size_t control = 0;
             control < CONTROL_COUNT;
             ++control)
        {
            double actual = 0.0;
            const bool active =
                reader.GetReplayState().GetValue(
                    target + 1U,
                    static_cast<std::uint32_t>(control + 1U),
                    actual);
            CHECK(active == (expected[target][control] != 0.0));
            CHECK(DoubleBits(actual) ==
                DoubleBits(expected[target][control]));
        }
    }

    CHECK(bytes == WriteTrace(
        MakeMetadata(),
        frames,
        nullptr,
        Limits(),
        std::vector<std::size_t>{
            1, 997, 2048, 4322, 5000, 8191, 9999}));
}

void TestCanonicalOrderingAndValues()
{
    using namespace RoR::DeterministicInputTrace;
    const Metadata metadata = MakeMetadata();

    Frame unsorted = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(2, 1, EventKind::STATE, 0.25),
            MakeEvent(1, 2, EventKind::STATE, 0.5)});
    std::ostringstream output(std::ios::out | std::ios::binary);
    Writer writer(output, metadata);
    CHECK(!writer.Append(unsorted));
    CHECK(writer.GetStatus().error ==
        Error::NON_CANONICAL_EVENT_ORDER);
    CHECK(writer.GetStatus().event_index == 1);

    Frame duplicate = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 2, EventKind::STATE, 0.25),
            MakeEvent(1, 2, EventKind::IMPULSE, 1.0)});
    std::ostringstream output2(std::ios::out | std::ios::binary);
    Writer duplicate_writer(output2, metadata);
    CHECK(!duplicate_writer.Append(duplicate));
    CHECK(duplicate_writer.GetStatus().error ==
        Error::NON_CANONICAL_EVENT_ORDER);

    Frame zero_control = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 0, EventKind::STATE, 1.0)});
    std::ostringstream output3(std::ios::out | std::ios::binary);
    Writer zero_control_writer(output3, metadata);
    CHECK(!zero_control_writer.Append(zero_control));
    CHECK(zero_control_writer.GetStatus().error ==
        Error::INVALID_CONTROL_ID);

    Frame unknown_kind = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(
                1,
                1,
                static_cast<EventKind>(99),
                1.0)});
    std::ostringstream output4(std::ios::out | std::ios::binary);
    Writer unknown_kind_writer(output4, metadata);
    CHECK(!unknown_kind_writer.Append(unknown_kind));
    CHECK(unknown_kind_writer.GetStatus().error ==
        Error::UNKNOWN_EVENT_KIND);

    const double invalid_values[] = {
        DoubleFromBits(UINT64_C(0x7ff0000000000000)),
        DoubleFromBits(UINT64_C(0xfff0000000000000)),
        DoubleFromBits(UINT64_C(0x7ff8000000000001)),
        DoubleFromBits(UINT64_C(0x8000000000000000))
    };
    for (std::size_t index = 0;
         index < sizeof(invalid_values) / sizeof(invalid_values[0]);
         ++index)
    {
        Frame invalid = MakeFrame(
            100,
            std::vector<Event>{
                MakeEvent(1, 1, EventKind::STATE, invalid_values[index])});
        std::ostringstream stream(std::ios::out | std::ios::binary);
        Writer invalid_writer(stream, metadata);
        CHECK(!invalid_writer.Append(invalid));
        if (index < 3)
        {
            CHECK(invalid_writer.GetStatus().error ==
                Error::NON_FINITE_VALUE);
        }
        else
        {
            CHECK(invalid_writer.GetStatus().error ==
                Error::NON_CANONICAL_VALUE);
        }
    }

    Frame zero_impulse = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 1, EventKind::IMPULSE, 0.0)});
    std::ostringstream output5(std::ios::out | std::ios::binary);
    Writer zero_impulse_writer(output5, metadata);
    CHECK(!zero_impulse_writer.Append(zero_impulse));
    CHECK(zero_impulse_writer.GetStatus().error ==
        Error::NON_CANONICAL_VALUE);

    Frame redundant_zero = MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 1, EventKind::STATE, 0.0)});
    std::ostringstream output6(std::ios::out | std::ios::binary);
    Writer redundant_zero_writer(output6, metadata);
    CHECK(!redundant_zero_writer.Append(redundant_zero));
    CHECK(redundant_zero_writer.GetStatus().error ==
        Error::REDUNDANT_STATE_EVENT);

    std::ostringstream output7(std::ios::out | std::ios::binary);
    Writer redundant_writer(output7, metadata);
    CHECK(redundant_writer.Append(MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 1, EventKind::STATE, 0.25)})));
    CHECK(!redundant_writer.Append(MakeFrame(
        101,
        std::vector<Event>{
            MakeEvent(1, 1, EventKind::STATE, 0.25)})));
    CHECK(redundant_writer.GetStatus().error ==
        Error::REDUNDANT_STATE_EVENT);
}

void TestMetadataAndLimits()
{
    using namespace RoR::DeterministicInputTrace;
    Metadata metadata = MakeMetadata();

    metadata.semantic_flags &= ~SEMANTIC_WORKER_INDEPENDENT;
    std::ostringstream output(std::ios::out | std::ios::binary);
    Writer semantics(output, metadata);
    CHECK(!semantics.IsReady());
    CHECK(semantics.GetStatus().error == Error::INVALID_SEMANTICS);

    metadata = MakeMetadata();
    metadata.stream_id = 0;
    std::ostringstream output2(std::ios::out | std::ios::binary);
    Writer stream_id(output2, metadata);
    CHECK(!stream_id.IsReady());
    CHECK(stream_id.GetStatus().error == Error::INVALID_METADATA);

    metadata = MakeMetadata();
    metadata.physics_step_numerator = 2;
    metadata.physics_step_denominator = 4000;
    std::ostringstream output3(std::ios::out | std::ios::binary);
    Writer cadence(output3, metadata);
    CHECK(!cadence.IsReady());

    metadata = MakeMetadata();
    metadata.source_digest = Digest();
    std::ostringstream output4(std::ios::out | std::ios::binary);
    Writer no_source_digest(output4, metadata);
    CHECK(!no_source_digest.IsReady());

    metadata = MakeMetadata();
    metadata.scenario_name.assign(
        MAX_IDENTITY_STRING_BYTES + 1U,
        'x');
    std::ostringstream output5(std::ios::out | std::ios::binary);
    Writer long_name(output5, metadata);
    CHECK(!long_name.IsReady());
    CHECK(long_name.GetStatus().error ==
        Error::STRING_LIMIT_EXCEEDED);

    const std::string invalid_names[] = {
        std::string(),
        std::string("line\nbreak"),
        std::string("\xc0\x80", 2),
        std::string("\xed\xa0\x80", 3),
        std::string("\xef\xb7\x90", 3)
    };
    for (std::size_t index = 0;
         index < sizeof(invalid_names) / sizeof(invalid_names[0]);
         ++index)
    {
        metadata = MakeMetadata();
        metadata.scenario_name = invalid_names[index];
        std::ostringstream stream(std::ios::out | std::ios::binary);
        Writer invalid_name(stream, metadata);
        CHECK(!invalid_name.IsReady());
        CHECK(invalid_name.GetStatus().error ==
            Error::INVALID_IDENTITY_STRING);
    }

    metadata = MakeMetadata();
    metadata.scenario_name = "D0 \xe2\x80\x93 replay";
    std::ostringstream unicode_output(std::ios::out | std::ios::binary);
    Writer unicode_writer(unicode_output, metadata);
    CHECK(unicode_writer.IsReady());
    CHECK(unicode_writer.Finish());
    CHECK(IsValid(unicode_output.str()));

    Limits no_header;
    no_header.max_bytes = HEADER_MIN_SIZE + TRAILER_SIZE - 1;
    metadata = MakeMetadata();
    metadata.scenario_name = "S";
    metadata.source_name = "I";
    std::ostringstream output6(std::ios::out | std::ios::binary);
    Writer tiny(output6, metadata, no_header);
    CHECK(!tiny.IsReady());
    CHECK(tiny.GetStatus().error == Error::BYTE_LIMIT_EXCEEDED);

    Limits one_step;
    one_step.max_steps = 1;
    std::ostringstream output7(std::ios::out | std::ios::binary);
    Writer step_limited(output7, MakeMetadata(), one_step);
    CHECK(step_limited.Append(MakeFrame(100, std::vector<Event>())));
    CHECK(!step_limited.Append(MakeFrame(101, std::vector<Event>())));
    CHECK(step_limited.GetStatus().error ==
        Error::STEP_LIMIT_EXCEEDED);

    Limits one_event;
    one_event.max_events = 1;
    std::ostringstream output8(std::ios::out | std::ios::binary);
    Writer event_limited(output8, MakeMetadata(), one_event);
    CHECK(!event_limited.Append(MakeFrames()[0]));
    CHECK(event_limited.GetStatus().error ==
        Error::EVENT_LIMIT_EXCEEDED);

    Limits one_active;
    one_active.max_active_controls = 1;
    std::ostringstream output9(std::ios::out | std::ios::binary);
    Writer active_limited(output9, MakeMetadata(), one_active);
    CHECK(!active_limited.Append(MakeFrame(
        100,
        std::vector<Event>{
            MakeEvent(1, 1, EventKind::STATE, 0.25),
            MakeEvent(1, 2, EventKind::STATE, 0.5)})));
    CHECK(active_limited.GetStatus().error ==
        Error::ACTIVE_CONTROL_LIMIT_EXCEEDED);

    std::ostringstream output10(std::ios::out | std::ios::binary);
    Writer noncontiguous(output10, MakeMetadata());
    CHECK(!noncontiguous.Append(
        MakeFrame(101, std::vector<Event>())));
    CHECK(noncontiguous.GetStatus().error ==
        Error::NON_CONTIGUOUS_STEP);

    metadata = MakeMetadata();
    metadata.first_physics_step =
        std::numeric_limits<std::uint64_t>::max();
    std::ostringstream output11(std::ios::out | std::ios::binary);
    Writer overflow(output11, metadata);
    CHECK(!overflow.Append(
        MakeFrame(
            std::numeric_limits<std::uint64_t>::max(),
            std::vector<Event>())));
    CHECK(overflow.GetStatus().error == Error::ARITHMETIC_OVERFLOW);

    std::ostringstream output12(std::ios::out | std::ios::binary);
    Writer finished(output12, MakeMetadata());
    CHECK(finished.Finish());
    CHECK(!finished.Finish());
    CHECK(finished.GetStatus().error == Error::ALREADY_FINISHED);
}

void TestEveryPrefixAndBitFlipFailClosed()
{
    using namespace RoR::DeterministicInputTrace;
    const std::string complete =
        WriteTrace(MakeMetadata(), MakeFrames());
    for (std::size_t size = 0; size < complete.size(); ++size)
    {
        Error error = Error::NONE;
        CHECK(!IsValid(complete.substr(0, size), &error));
        CHECK(error == Error::TRUNCATED);
    }

    for (std::size_t byte = 0; byte < complete.size(); ++byte)
    {
        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            std::string corrupted = complete;
            corrupted[byte] = static_cast<char>(
                static_cast<std::uint8_t>(corrupted[byte]) ^
                static_cast<std::uint8_t>(1U << bit));
            CHECK(!IsValid(corrupted));
        }
    }

    std::string trailing = complete;
    trailing.push_back('x');
    Error error = Error::NONE;
    CHECK(!IsValid(trailing, &error));
    CHECK(error == Error::TRAILING_DATA);
}

void TestHostileEncodedSizes()
{
    using namespace RoR::DeterministicInputTrace;
    const std::string complete =
        WriteTrace(MakeMetadata(), MakeFrames());
    const std::size_t header_size = LoadU32(complete, 20);
    Error error = Error::NONE;

    std::string hostile = complete;
    StoreU32(hostile, 20, UINT32_MAX);
    CHECK(!IsValid(hostile, &error));
    CHECK(error == Error::INVALID_HEADER_SIZE ||
        error == Error::STRING_LIMIT_EXCEEDED ||
        error == Error::BYTE_LIMIT_EXCEEDED);

    hostile = complete;
    StoreU32(hostile, 72, UINT32_MAX);
    CHECK(!IsValid(hostile, &error));
    CHECK(error == Error::STRING_LIMIT_EXCEEDED);

    hostile = complete;
    StoreU32(
        hostile,
        header_size + 16,
        MAX_EVENTS_PER_STEP + 1U);
    CHECK(!IsValid(hostile, &error));
    CHECK(error == Error::EVENT_LIMIT_EXCEEDED);

    hostile = complete;
    StoreU32(hostile, header_size + 4, UINT32_MAX);
    CHECK(!IsValid(hostile, &error));
    CHECK(error == Error::INVALID_RECORD_SIZE ||
        error == Error::BYTE_LIMIT_EXCEEDED);

    hostile = complete;
    StoreU64(
        hostile,
        header_size + 8,
        std::numeric_limits<std::uint64_t>::max());
    CHECK(!IsValid(hostile, &error));
    CHECK(error == Error::INTEGRITY_MISMATCH);

    Limits reader_event_limit;
    reader_event_limit.max_events = 1;
    CHECK(!IsValid(complete, &error, reader_event_limit));
    CHECK(error == Error::EVENT_LIMIT_EXCEEDED);

    Limits reader_active_limit;
    reader_active_limit.max_active_controls = 1;
    CHECK(!IsValid(complete, &error, reader_active_limit));
    CHECK(error == Error::ACTIVE_CONTROL_LIMIT_EXCEEDED);

    Limits reader_byte_limit;
    reader_byte_limit.max_bytes = complete.size() - 1U;
    CHECK(!IsValid(complete, &error, reader_byte_limit));
    CHECK(error == Error::BYTE_LIMIT_EXCEEDED);
}

void TestStreamsWithExceptionMasks()
{
    using namespace RoR::DeterministicInputTrace;
    const std::string complete =
        WriteTrace(MakeMetadata(), MakeFrames());
    std::istringstream input(
        complete,
        std::ios::in | std::ios::binary);
    input.exceptions(
        std::ios::badbit | std::ios::failbit | std::ios::eofbit);
    Reader reader(input);
    Frame frame;
    while (reader.ReadNext(frame) == ReadResult::FRAME)
    {
    }
    CHECK(reader.GetStatus().error == Error::NONE);

    std::istringstream truncated(
        complete.substr(0, complete.size() - 1U),
        std::ios::in | std::ios::binary);
    truncated.exceptions(
        std::ios::badbit | std::ios::failbit | std::ios::eofbit);
    Reader truncated_reader(truncated);
    while (truncated_reader.ReadNext(frame) == ReadResult::FRAME)
    {
    }
    CHECK(truncated_reader.GetStatus().error == Error::TRUNCATED);
}

void TestComparisonAndOrderingSensitivity()
{
    using namespace RoR::DeterministicInputTrace;
    const std::vector<Frame> baseline_frames = MakeFrames();
    const std::string baseline =
        WriteTrace(MakeMetadata(), baseline_frames);
    ComparisonResult result = CompareBytes(baseline, baseline);
    CHECK(result.status == ComparisonStatus::MATCH);
    CHECK(result.difference == Difference::NONE);
    CHECK(result.steps_compared == baseline_frames.size());

    std::vector<Frame> changed = baseline_frames;
    changed[1].events[0].value = 0.625;
    const std::string changed_bytes =
        WriteTrace(MakeMetadata(), changed);
    result = CompareBytes(baseline, changed_bytes);
    CHECK(result.status == ComparisonStatus::DIVERGED);
    CHECK(result.difference == Difference::EVENT_VALUE);
    CHECK(result.steps_compared == 1);
    CHECK(result.has_first_divergent_step);
    CHECK(result.first_divergent_step == 101);
    CHECK(result.has_first_divergent_event);
    CHECK(result.first_divergent_event == 0);

    changed = baseline_frames;
    changed[0].events[0].target_id = 0;
    const std::string changed_key =
        WriteTrace(MakeMetadata(), changed);
    result = CompareBytes(baseline, changed_key);
    CHECK(result.difference == Difference::EVENT_KEY);
    CHECK(result.first_divergent_step == 100);

    changed = baseline_frames;
    changed[0].events.pop_back();
    const std::string changed_count =
        WriteTrace(MakeMetadata(), changed);
    result = CompareBytes(baseline, changed_count);
    CHECK(result.difference == Difference::EVENT_COUNT);

    changed = baseline_frames;
    changed.pop_back();
    result = CompareBytes(
        baseline,
        WriteTrace(MakeMetadata(), changed));
    CHECK(result.difference == Difference::TRACE_LENGTH);
    CHECK(result.first_divergent_step == 103);

    Metadata changed_metadata = MakeMetadata();
    changed_metadata.source_name = "controller-remap-v2";
    result = CompareBytes(
        baseline,
        WriteTrace(changed_metadata, baseline_frames));
    CHECK(result.difference == Difference::METADATA);
    CHECK(result.metadata_field == MetadataField::SOURCE_NAME);

    std::string corrupt_tail = changed_bytes;
    corrupt_tail[corrupt_tail.size() - 1U] ^= 1;
    result = CompareBytes(baseline, corrupt_tail);
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::RIGHT_INVALID);
    CHECK(result.right_error.error == Error::INTEGRITY_MISMATCH);

    const std::string truncated =
        baseline.substr(0, baseline.size() - 1U);
    result = CompareBytes(truncated, truncated);
    CHECK(result.status == ComparisonStatus::INVALID_INPUT);
    CHECK(result.difference == Difference::BOTH_INVALID);
}

void TestToStringCoverage()
{
    using namespace RoR::DeterministicInputTrace;
    CHECK(std::string(ToString(Error::NONE)) == "none");
    CHECK(std::string(ToString(Error::INTEGRITY_MISMATCH)) ==
        "integrity_mismatch");
    CHECK(std::string(ToString(Difference::EVENT_VALUE)) ==
        "event_value");
    CHECK(std::string(ToString(MetadataField::STREAM_ID)) ==
        "stream_id");
}

} // namespace

int main()
{
    TestRoundTripAndReplayState();
    TestGoldenBytesAndDigest();
    TestPauseResumeSegmentationAndWorkerIndependence();
    TestLoadingAndContinuation();
    TestFixedSeedLongReplay();
    TestCanonicalOrderingAndValues();
    TestMetadataAndLimits();
    TestEveryPrefixAndBitFlipFailClosed();
    TestHostileEncodedSizes();
    TestStreamsWithExceptionMasks();
    TestComparisonAndOrderingSensitivity();
    TestToStringCoverage();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic input-trace checks failed\n";
        return 1;
    }
    std::cout << "All deterministic input-trace checks passed\n";
    return 0;
}
