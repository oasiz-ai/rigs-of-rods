#include "EpisodeFormat.h"
#include "EpisodeValidator.h"
#include "EpisodeWriter.h"
#include "TestProvenance.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

const char* VALID_ID = "0123456789abcdef0123456789abcdef";

std::filesystem::path TemporaryRoot()
{
    const std::uint64_t nonce = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now()
            .time_since_epoch().count());
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("ror-episode-io-tests-" + std::to_string(nonce));
#if defined(_WIN32)
    // Exercise EpisodeWriter's native wide-path file opening.
    root /= L"unicode-\u4e16\u754c";
#endif
    return root;
}

bool WriteEpisode(
    const std::filesystem::path& root,
    const std::string& episode_id,
    std::filesystem::path& final_directory)
{
    using namespace RoR::WorldModel;
    EpisodeWriterOptions options;
    options.max_records_per_chunk = 2;
    options.max_chunk_bytes = 1024;
    EpisodeWriter writer;
    std::string error;
    if (!writer.Open(
            root,
            episode_id,
            RoRWorldModelTest::MakeProvenance(episode_id),
            options,
            &error))
    {
        std::cerr << "writer open failed: " << error << '\n';
        return false;
    }
    const std::string telemetry[] = {
        "observation-10", "observation-20", "observation-30",
        "observation-40", "observation-50"};
    for (std::size_t index = 0; index < 5; ++index)
    {
        if (!writer.AppendTelemetryRecord(
                10U + index * 10U,
                static_cast<std::uint32_t>(100U + index),
                telemetry[index].data(),
                telemetry[index].size(),
                &error))
        {
            std::cerr << "telemetry append failed: " << error << '\n';
            return false;
        }
    }
    const std::string rgb0 = "fake-rgb-frame-zero";
    const std::string rgb1 = "fake-rgb-frame-one";
    if (!writer.AppendRgbRecord(
            100, 200, rgb0.data(), rgb0.size(), &error) ||
        !writer.AppendRgbRecord(
            200, 200, rgb1.data(), rgb1.size(), &error) ||
        !writer.Complete(&error))
    {
        std::cerr << "RGB append or completion failed: " << error << '\n';
        return false;
    }
    final_directory = writer.GetFinalDirectory();
    return writer.IsComplete();
}

std::string AlternateId(unsigned int value)
{
    static const char DIGITS[] = "0123456789abcdef";
    std::string id(32, '0');
    id[30] = DIGITS[(value >> 4U) & 0xfU];
    id[31] = DIGITS[value & 0xfU];
    if (value == 0)
        id[31] = '1';
    return id;
}

void FlipByte(const std::filesystem::path& path, std::uint64_t offset)
{
    std::fstream file(
        path, std::ios::binary | std::ios::in | std::ios::out);
    CHECK(static_cast<bool>(file));
    file.seekg(static_cast<std::streamoff>(offset));
    char value = 0;
    file.read(&value, 1);
    CHECK(file.gcount() == 1);
    value ^= static_cast<char>(0x40);
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(&value, 1);
    file.flush();
    CHECK(static_cast<bool>(file));
}

void TestIntegrityPrimitives()
{
    using namespace RoR::WorldModel;
    const std::string input = "123456789";
    CHECK(ComputeCrc32c(input.data(), input.size()) == 0xe3069283U);
    CHECK(ComputeSha256(input.data(), input.size()).ToHex() ==
        "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225");
    CHECK(IsValidEpisodeId(VALID_ID));
    CHECK(!IsValidEpisodeId("00000000000000000000000000000000"));
    CHECK(!IsValidEpisodeId("0123456789ABCDEF0123456789ABCDEF"));
    CHECK(!IsValidEpisodeId("01234567-89ab-cdef-0123-456789abcdef"));
}

void TestCompletedEpisode(const std::filesystem::path& root)
{
    using namespace RoR::WorldModel;
    std::filesystem::path episode;
    CHECK(WriteEpisode(root, VALID_ID, episode));
    CHECK(std::filesystem::is_directory(episode));
    CHECK(!std::filesystem::exists(
        std::filesystem::path(episode.string() + ".partial")));
    CHECK(std::filesystem::is_regular_file(
        episode / "manifest.open.json"));
    CHECK(std::filesystem::is_regular_file(
        episode / "provenance.json"));
    CHECK(std::filesystem::is_regular_file(
        episode / "checksums.sha256"));
    CHECK(std::filesystem::is_regular_file(episode / "manifest.json"));
    CHECK(std::filesystem::is_regular_file(episode / "COMPLETE.json"));
    CHECK(std::filesystem::is_directory(episode / "chunks"));
    CHECK(std::filesystem::is_directory(episode / "rgb"));
    CHECK(std::filesystem::is_regular_file(
        episode / "chunks/chunk-000000.bin"));
    CHECK(std::filesystem::is_regular_file(
        episode / "chunks/chunk-000001.bin"));
    CHECK(std::filesystem::is_regular_file(
        episode / "chunks/chunk-000002.bin"));
    CHECK(std::filesystem::is_regular_file(
        episode / "rgb/chunk-000000.bin"));
    const EpisodeValidationResult result =
        EpisodeValidator::Validate(episode);
    if (!result.IsValid())
    {
        std::cerr << "valid episode rejected: "
                  << EpisodeValidationErrorName(result.error)
                  << ": " << result.detail << " (" << result.artifact
                  << ")\n";
    }
    CHECK(result.IsValid());
    CHECK(result.telemetry_record_count == 5);
    CHECK(result.rgb_record_count == 2);
    CHECK(result.telemetry_chunk_count == 3);
    CHECK(result.rgb_chunk_count == 1);
}

void TestInterruptedEpisode(const std::filesystem::path& root)
{
    using namespace RoR::WorldModel;
    std::filesystem::path partial;
    std::filesystem::path final_directory;
    {
        EpisodeWriter writer;
        std::string error;
        const std::string id = AlternateId(2);
        CHECK(writer.Open(
            root,
            id,
            RoRWorldModelTest::MakeProvenance(id),
            EpisodeWriterOptions(),
            &error));
        const std::string payload = "uncommitted";
        CHECK(writer.AppendTelemetryRecord(
            1, 1, payload.data(), payload.size(), &error));
        partial = writer.GetPartialDirectory();
        final_directory = writer.GetFinalDirectory();
        CHECK(EpisodeValidator::Validate(partial).error ==
            EpisodeValidationError::PATH_IS_PARTIAL);
    }
    CHECK(std::filesystem::is_directory(partial));
    CHECK(!std::filesystem::exists(final_directory));
    CHECK(std::filesystem::is_regular_file(
        partial / "chunks/chunk-000000.bin.tmp"));
}

void TestCorruptionRejection(const std::filesystem::path& root)
{
    using namespace RoR::WorldModel;
    std::filesystem::path episode;

    CHECK(WriteEpisode(root, AlternateId(3), episode));
    std::filesystem::remove(episode / "COMPLETE.json");
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::MISSING_ARTIFACT);

    CHECK(WriteEpisode(root, AlternateId(4), episode));
    std::ofstream(episode / "leftover.tmp") << "interrupted";
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::TEMPORARY_ARTIFACT_PRESENT);

    CHECK(WriteEpisode(root, AlternateId(5), episode));
    const std::filesystem::path telemetry =
        episode / "chunks/chunk-000000.bin";
    FlipByte(telemetry, 24U + 20U + 2U);
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::RECORD_CRC_MISMATCH);

    CHECK(WriteEpisode(root, AlternateId(6), episode));
    const std::filesystem::path truncated =
        episode / "chunks/chunk-000000.bin";
    std::filesystem::resize_file(
        truncated, std::filesystem::file_size(truncated) - 3U);
    CHECK(!EpisodeValidator::Validate(episode).IsValid());

    CHECK(WriteEpisode(root, AlternateId(7), episode));
    std::filesystem::remove(episode / "chunks/chunk-000001.bin");
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::MISSING_CHUNK);

    CHECK(WriteEpisode(root, AlternateId(8), episode));
    std::ofstream(episode / "rgb/chunk-999999.bin") << "orphan";
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::UNEXPECTED_CHUNK);

    CHECK(WriteEpisode(root, AlternateId(9), episode));
    std::filesystem::remove_all(episode / "rgb");
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::MISSING_ARTIFACT);

    CHECK(WriteEpisode(root, AlternateId(12), episode));
    FlipByte(episode / "provenance.json", 64U);
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::INVALID_PROVENANCE);

    CHECK(WriteEpisode(root, AlternateId(13), episode));
    std::filesystem::remove(episode / "provenance.json");
    CHECK(EpisodeValidator::Validate(episode).error ==
        EpisodeValidationError::MISSING_ARTIFACT);
}

void TestWriterAdmission(const std::filesystem::path& root)
{
    using namespace RoR::WorldModel;
    EpisodeWriter invalid;
    std::string error;
    CHECK(!invalid.Open(
        root, "00000000000000000000000000000000",
        RoRWorldModelTest::MakeProvenance(VALID_ID),
        EpisodeWriterOptions(), &error));

    EpisodeWriter sequence;
    CHECK(sequence.Open(
        root,
        AlternateId(10),
        RoRWorldModelTest::MakeProvenance(AlternateId(10)),
        EpisodeWriterOptions(),
        &error));
    const char payload = 'x';
    CHECK(sequence.AppendTelemetryRecord(2, 1, &payload, 1, &error));
    CHECK(!sequence.AppendTelemetryRecord(2, 1, &payload, 1, &error));

    EpisodeWriter zero_id;
    const std::string zero_id_episode = AlternateId(14);
    CHECK(zero_id.Open(
        root,
        zero_id_episode,
        RoRWorldModelTest::MakeProvenance(zero_id_episode),
        EpisodeWriterOptions(),
        &error));
    CHECK(!zero_id.AppendTelemetryRecord(
        0, 1, &payload, 1, &error));

    EpisodeWriter zero_type;
    const std::string zero_type_episode = AlternateId(15);
    CHECK(zero_type.Open(
        root,
        zero_type_episode,
        RoRWorldModelTest::MakeProvenance(zero_type_episode),
        EpisodeWriterOptions(),
        &error));
    CHECK(!zero_type.AppendTelemetryRecord(
        1, 0, &payload, 1, &error));

    EpisodeWriter empty_payload;
    const std::string empty_payload_episode = AlternateId(16);
    CHECK(empty_payload.Open(
        root,
        empty_payload_episode,
        RoRWorldModelTest::MakeProvenance(empty_payload_episode),
        EpisodeWriterOptions(),
        &error));
    CHECK(!empty_payload.AppendTelemetryRecord(
        1, 1, nullptr, 0, &error));

    EpisodeWriter quarantined;
    const std::string quarantined_id = AlternateId(11);
    CHECK(quarantined.Open(
        root,
        quarantined_id,
        RoRWorldModelTest::MakeProvenance(quarantined_id),
        EpisodeWriterOptions(),
        &error));
    CHECK(quarantined.AppendTelemetryRecord(
        1, 1, &payload, 1, &error));
    std::ofstream(
        quarantined.GetPartialDirectory() / "unexpected.log")
        << "must prevent publication";
    CHECK(!quarantined.Complete(&error));
    CHECK(std::filesystem::is_directory(
        quarantined.GetPartialDirectory()));
    CHECK(!std::filesystem::exists(
        quarantined.GetFinalDirectory()));
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path root =
        argc == 3 && std::string(argv[1]) == "--emit-fixture"
            ? std::filesystem::path(argv[2])
            : TemporaryRoot();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);

    if (argc == 3 && std::string(argv[1]) == "--emit-fixture")
    {
        std::filesystem::path episode;
        if (!WriteEpisode(root, VALID_ID, episode))
            return 1;
        std::cout << episode.string() << '\n';
        return 0;
    }

    TestIntegrityPrimitives();
    TestCompletedEpisode(root);
    TestInterruptedEpisode(root);
    TestCorruptionRejection(root);
    TestWriterAdmission(root);
    std::filesystem::remove_all(root, error);
    if (g_failures != 0)
    {
        std::cerr << g_failures << " episode I/O test(s) failed\n";
        return 1;
    }
    std::cout << "episode I/O tests passed\n";
    return 0;
}
