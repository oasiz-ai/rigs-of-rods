/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "FrameTimeBudget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace RoR {

namespace {

constexpr double kNanosecondsPerMillisecond = 1000000.0;

/// Optimized game builds enable `-ffast-math` globally, which lets a compiler
/// assume no NaN or infinity exists and fold `std::isfinite` to a constant
/// true. This file is compiled strictly, but the classification below does not
/// rely on that: it inspects the IEEE-754 bit pattern directly, so a malformed
/// frame interval is rejected under either floating-point mode.
bool IsFiniteBits(double value) noexcept
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "the frame-time budget requires a 64-bit IEEE-754 double");
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    // Exponent all ones selects both infinity and NaN.
    return (bits & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

/// Convert a whole number of nanoseconds to milliseconds. Every frame interval
/// this project can accept is far inside the exactly-representable integer
/// range of `double`, so the conversion is a single exact division.
double ToMilliseconds(std::uint64_t nanoseconds) noexcept
{
    return static_cast<double>(nanoseconds) / kNanosecondsPerMillisecond;
}

/// Upper edge of `bin`, in milliseconds. Percentiles report an upper edge so a
/// binned answer can never understate the measured interval.
double BinUpperEdgeMilliseconds(std::size_t bin) noexcept
{
    const std::uint64_t upper_ns =
        static_cast<std::uint64_t>(bin + 1U) * kFrameTimeBudgetBinWidthNs;
    return ToMilliseconds(upper_ns);
}

bool MillisecondsToNanoseconds(
    double milliseconds,
    std::uint64_t& nanoseconds) noexcept
{
    if (!IsFiniteBits(milliseconds) || !(milliseconds > 0.0))
        return false;
    const double scaled = milliseconds * kNanosecondsPerMillisecond;
    if (!(scaled >= 1.0) ||
            scaled > static_cast<double>(kFrameTimeBudgetMaximumSampleNs))
    {
        return false;
    }
    nanoseconds = static_cast<std::uint64_t>(scaled);
    return true;
}

std::string FormatDouble(double value, int decimals)
{
    char buffer[64];
    const int written = std::snprintf(
        buffer, sizeof(buffer), "%.*f", decimals, value);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer))
        return "0";
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::string FormatUnsigned(std::uint64_t value)
{
    char buffer[32];
    const int written = std::snprintf(
        buffer, sizeof(buffer), "%llu",
        static_cast<unsigned long long>(value));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer))
        return "0";
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::string FormatSigned(std::int64_t value)
{
    char buffer[32];
    const int written = std::snprintf(
        buffer, sizeof(buffer), "%lld",
        static_cast<long long>(value));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(buffer))
        return "0";
    return std::string(buffer, static_cast<std::size_t>(written));
}

/// Minimal JSON string escaping. Control characters are escaped rather than
/// emitted, so a hostile scenario or terrain name cannot break the receipt.
std::string EscapeJson(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8U);
    for (const char character : value)
    {
        const unsigned char code = static_cast<unsigned char>(character);
        switch (character)
        {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (code < 0x20U)
            {
                char buffer[8];
                std::snprintf(
                    buffer, sizeof(buffer), "\\u%04x",
                    static_cast<unsigned int>(code));
                escaped += buffer;
            }
            else
            {
                escaped += character;
            }
            break;
        }
    }
    return escaped;
}

std::string JsonField(const std::string& key, const std::string& value)
{
    return "  \"" + key + "\": \"" + EscapeJson(value) + "\"";
}

std::string JsonRaw(const std::string& key, const std::string& value)
{
    return "  \"" + key + "\": " + value;
}

} // namespace

bool FrameTimeBudgetLimits::valid() const noexcept
{
    if (minimum_frames == 0U)
        return false;
    if (static_cast<std::uint64_t>(warmup_frames) +
            static_cast<std::uint64_t>(minimum_frames) >
                kFrameTimeBudgetMaximumFrames)
    {
        return false;
    }
    if (requested_frames != 0U &&
            (requested_frames < minimum_frames ||
                requested_frames > kFrameTimeBudgetMaximumFrames))
    {
        return false;
    }
    if (percentile == 0U || percentile > 100U)
        return false;

    std::uint64_t sustained_ns = 0U;
    std::uint64_t percentile_ns = 0U;
    if (!MillisecondsToNanoseconds(sustained_ms, sustained_ns))
        return false;
    if (!MillisecondsToNanoseconds(percentile_ms, percentile_ns))
        return false;
    // A percentile ceiling below the sustained mean cannot be satisfied by any
    // real distribution and is therefore a configuration error.
    return percentile_ns >= sustained_ns;
}

FrameTimeBudgetSession::FrameTimeBudgetSession(
    FrameTimeBudgetMode mode,
    const FrameTimeBudgetLimits& limits,
    const FrameTimeBudgetContext& context)
    : mode_(mode)
    , limits_(limits)
    , context_(context)
    , limits_valid_(limits.valid())
    , bins_(kFrameTimeBudgetTotalBins, 0U)
    , minimum_ns_(std::numeric_limits<std::uint64_t>::max())
{
}

bool FrameTimeBudgetSession::RecordFrame(double seconds)
{
    ++observed_frames_;

    if (warmup_frames_ < static_cast<std::uint64_t>(limits_.warmup_frames))
    {
        ++warmup_frames_;
        last_frame_retained_ = false;
        return true;
    }

    if (accepted_frames_ >= kFrameTimeBudgetMaximumFrames)
    {
        last_frame_retained_ = false;
        // The bounded run is complete. Additional frames are neither recorded
        // nor treated as observation errors.
        return true;
    }

    if (!IsFiniteBits(seconds) || !(seconds > 0.0))
    {
        ++rejected_frames_;
        last_frame_retained_ = false;
        return false;
    }

    // Bound the interval in the seconds domain first. Scaling an unbounded
    // value to nanoseconds could overflow to infinity, and comparing that
    // result is exactly the case `-ffast-math` is allowed to assume away.
    constexpr double kMaximumSeconds =
        static_cast<double>(kFrameTimeBudgetMaximumSampleNs) / 1000000000.0;
    constexpr double kMinimumSeconds = 1.0 / 1000000000.0;
    if (seconds < kMinimumSeconds || seconds > kMaximumSeconds)
    {
        ++rejected_frames_;
        last_frame_retained_ = false;
        return false;
    }

    const std::uint64_t sample_ns =
        static_cast<std::uint64_t>(seconds * 1000000000.0);
    const std::size_t bin = static_cast<std::size_t>(
        sample_ns / kFrameTimeBudgetBinWidthNs);
    if (bin >= kFrameTimeBudgetBinCount)
    {
        ++bins_[kFrameTimeBudgetBinCount];
        ++saturated_frames_;
    }
    else
    {
        ++bins_[bin];
    }

    ++accepted_frames_;
    last_frame_retained_ = true;
    total_ns_ += sample_ns;
    minimum_ns_ = std::min(minimum_ns_, sample_ns);
    maximum_ns_ = std::max(maximum_ns_, sample_ns);

    std::uint64_t sustained_ns = 0U;
    if (MillisecondsToNanoseconds(limits_.sustained_ms, sustained_ns) &&
            sample_ns > sustained_ns)
    {
        ++over_budget_frames_;
    }
    return true;
}

void FrameTimeBudgetSession::RecordPhase(
    FrameTimeBudgetPhase phase, double seconds)
{
    const std::size_t index = static_cast<std::size_t>(phase);
    if (index >= kFrameTimeBudgetPhaseCount)
        return;
    // Phase totals must describe the same frames as the frame totals, so
    // ignore warm-up entirely rather than accumulating a share of frames the
    // distribution never saw.
    if (!Recording())
        return;
    if (!IsFiniteBits(seconds) || !(seconds > 0.0))
        return;

    constexpr double kMaximumSeconds =
        static_cast<double>(kFrameTimeBudgetMaximumSampleNs) / 1000000000.0;
    if (seconds > kMaximumSeconds)
        return;

    const std::uint64_t sample_ns =
        static_cast<std::uint64_t>(seconds * 1000000000.0);
    ++phase_samples_[index];
    phase_total_ns_[index] += sample_ns;
    if (sample_ns > phase_maximum_ns_[index])
        phase_maximum_ns_[index] = sample_ns;
}

void FrameTimeBudgetSession::ObserveSceneIdentity(
    const std::string& terrain,
    const std::string& actor)
{
    // An empty terrain means the scene has not finished loading, which is not
    // a scene change. Only a named scene can name or contradict the run.
    if (terrain.empty())
        return;

    if (!scene_identity_observed_)
    {
        scene_identity_observed_ = true;
        context_.terrain = terrain;
        context_.actor = actor;
        return;
    }
    if (context_.terrain != terrain || context_.actor != actor)
        scene_identity_changed_ = true;
}

bool FrameTimeBudgetSession::ShutdownRequested() const noexcept
{
    if (limits_.requested_frames == 0U)
        return false;
    return accepted_frames_ >= limits_.requested_frames;
}

double FrameTimeBudgetSession::RankedMilliseconds(
    std::uint32_t percentile) const
{
    if (accepted_frames_ == 0U || percentile == 0U || percentile > 100U)
        return 0.0;

    // Nearest-rank on the retained histogram: the smallest bin whose
    // cumulative count reaches ceil(p * N / 100).
    const std::uint64_t rank =
        ((accepted_frames_ * static_cast<std::uint64_t>(percentile)) + 99U) /
        100U;
    std::uint64_t cumulative = 0U;
    for (std::size_t bin = 0U; bin < bins_.size(); ++bin)
    {
        cumulative += bins_[bin];
        if (cumulative >= rank)
        {
            if (bin >= kFrameTimeBudgetBinCount)
            {
                // The saturating bin has no upper edge; report the exact
                // measured maximum so the ranked value stays truthful.
                return ToMilliseconds(maximum_ns_);
            }
            return BinUpperEdgeMilliseconds(bin);
        }
    }
    return ToMilliseconds(maximum_ns_);
}

FrameTimeBudgetReport FrameTimeBudgetSession::Finalize() const
{
    FrameTimeBudgetReport report;
    report.mode = mode_;
    report.limits = limits_;
    report.context = context_;
    report.observed_frames = observed_frames_;
    report.warmup_frames = warmup_frames_;
    report.accepted_frames = accepted_frames_;
    report.rejected_frames = rejected_frames_;
    report.saturated_frames = saturated_frames_;
    report.over_budget_frames = over_budget_frames_;

    if (accepted_frames_ > 0U)
    {
        report.minimum_ms = ToMilliseconds(minimum_ns_);
        report.maximum_ms = ToMilliseconds(maximum_ns_);
        report.mean_ms =
            ToMilliseconds(total_ns_) / static_cast<double>(accepted_frames_);
        report.p50_ms = RankedMilliseconds(50U);
        report.p95_ms = RankedMilliseconds(95U);
        report.p99_ms = RankedMilliseconds(99U);
        report.ranked_ms = RankedMilliseconds(limits_.percentile);
        report.mean_fps = report.mean_ms > 0.0 ? 1000.0 / report.mean_ms : 0.0;

        const double total_frame_ms = ToMilliseconds(total_ns_);
        std::uint64_t attributed_ns = 0U;
        for (std::size_t index = 0U; index < kFrameTimeBudgetPhaseCount;
             ++index)
        {
            FrameTimeBudgetPhaseStats& stats = report.phases[index];
            stats.samples = phase_samples_[index];
            stats.total_ms = ToMilliseconds(phase_total_ns_[index]);
            stats.maximum_ms = ToMilliseconds(phase_maximum_ns_[index]);
            stats.mean_ms = stats.samples > 0U
                ? stats.total_ms / static_cast<double>(stats.samples)
                : 0.0;
            stats.share = total_frame_ms > 0.0
                ? stats.total_ms / total_frame_ms
                : 0.0;
            attributed_ns += phase_total_ns_[index];
        }
        // The remainder is whatever the declared phases did not claim. A
        // negative remainder would mean the phases overlapped, so clamp at
        // zero and let the share expose the inconsistency instead.
        const std::uint64_t remainder_ns =
            attributed_ns <= total_ns_ ? total_ns_ - attributed_ns : 0U;
        report.remainder.samples = accepted_frames_;
        report.remainder.total_ms = ToMilliseconds(remainder_ns);
        report.remainder.mean_ms =
            report.remainder.total_ms / static_cast<double>(accepted_frames_);
        report.remainder.maximum_ms = 0.0;
        report.remainder.share = total_frame_ms > 0.0
            ? report.remainder.total_ms / total_frame_ms
            : 0.0;
    }

    if (mode_ != FrameTimeBudgetMode::GATE)
    {
        report.verdict = FrameTimeBudgetVerdict::ADVISORY;
        return report;
    }

    if (!limits_valid_)
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_INVALID_LIMITS;
        return report;
    }
    if (!context_.presents_frames)
    {
        // The interval describes how fast this loop produced scenes for
        // another process, which is not the rate anything was displayed at.
        report.verdict = FrameTimeBudgetVerdict::FAIL_NOT_PRESENTING;
        return report;
    }
    if (context_.fps_limit != 0)
    {
        // A limiter turns the distribution into a description of the limiter.
        report.verdict = FrameTimeBudgetVerdict::FAIL_LIMITER_ACTIVE;
        return report;
    }
    if (rejected_frames_ != 0U)
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_REJECTED_SAMPLE;
        return report;
    }
    if (scene_identity_changed_)
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_SCENE_CHANGED;
        return report;
    }
    if (accepted_frames_ < static_cast<std::uint64_t>(limits_.minimum_frames))
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_SHORT_RUN;
        return report;
    }
    if (report.mean_ms > limits_.sustained_ms)
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_SUSTAINED;
        return report;
    }
    if (report.ranked_ms > limits_.percentile_ms)
    {
        report.verdict = FrameTimeBudgetVerdict::FAIL_PERCENTILE;
        return report;
    }
    report.verdict = FrameTimeBudgetVerdict::PASS;
    return report;
}

bool ParseFrameTimeBudgetMode(
    const std::string& name,
    FrameTimeBudgetMode& mode)
{
    if (name == "off")
    {
        mode = FrameTimeBudgetMode::OFF;
        return true;
    }
    if (name == "measure")
    {
        mode = FrameTimeBudgetMode::MEASURE;
        return true;
    }
    if (name == "gate")
    {
        mode = FrameTimeBudgetMode::GATE;
        return true;
    }
    return false;
}

const char* ToString(FrameTimeBudgetMode mode) noexcept
{
    switch (mode)
    {
    case FrameTimeBudgetMode::OFF: return "off";
    case FrameTimeBudgetMode::MEASURE: return "measure";
    case FrameTimeBudgetMode::GATE: return "gate";
    }
    return "off";
}

const char* ToString(FrameTimeBudgetPhase phase) noexcept
{
    switch (phase)
    {
    case FrameTimeBudgetPhase::PRODUCER: return "producer";
    case FrameTimeBudgetPhase::SCENE_JOINED_READ: return "scene_joined_read";
    case FrameTimeBudgetPhase::SCENE_NORMALIZE: return "scene_normalize";
    case FrameTimeBudgetPhase::SCENE_PRODUCE: return "scene_produce";
    case FrameTimeBudgetPhase::SCENE_DISPATCH: return "scene_dispatch";
    case FrameTimeBudgetPhase::COUNT: break;
    }
    return "unknown";
}

const char* ToString(FrameTimeBudgetVerdict verdict) noexcept
{
    switch (verdict)
    {
    case FrameTimeBudgetVerdict::ADVISORY: return "advisory";
    case FrameTimeBudgetVerdict::PASS: return "pass";
    case FrameTimeBudgetVerdict::FAIL_SHORT_RUN: return "fail-short-run";
    case FrameTimeBudgetVerdict::FAIL_REJECTED_SAMPLE:
        return "fail-rejected-sample";
    case FrameTimeBudgetVerdict::FAIL_SUSTAINED: return "fail-sustained";
    case FrameTimeBudgetVerdict::FAIL_PERCENTILE: return "fail-percentile";
    case FrameTimeBudgetVerdict::FAIL_LIMITER_ACTIVE:
        return "fail-limiter-active";
    case FrameTimeBudgetVerdict::FAIL_INVALID_LIMITS:
        return "fail-invalid-limits";
    case FrameTimeBudgetVerdict::FAIL_SCENE_CHANGED:
        return "fail-scene-changed";
    case FrameTimeBudgetVerdict::FAIL_NOT_PRESENTING:
        return "fail-not-presenting";
    }
    return "fail-short-run";
}

std::string FormatFrameTimeBudgetSummary(const FrameTimeBudgetReport& report)
{
    std::string summary = "[RoR|Perf] mode=";
    summary += ToString(report.mode);
    summary += " verdict=";
    summary += ToString(report.verdict);
    summary += " frames=" + FormatUnsigned(report.accepted_frames);
    summary += " warmup=" + FormatUnsigned(report.warmup_frames);
    summary += " rejected=" + FormatUnsigned(report.rejected_frames);
    summary += " mean_ms=" + FormatDouble(report.mean_ms, 4);
    summary += " mean_fps=" + FormatDouble(report.mean_fps, 3);
    summary += " p50_ms=" + FormatDouble(report.p50_ms, 4);
    summary += " p95_ms=" + FormatDouble(report.p95_ms, 4);
    summary += " p99_ms=" + FormatDouble(report.p99_ms, 4);
    summary += " max_ms=" + FormatDouble(report.maximum_ms, 4);
    summary += " over_budget=" + FormatUnsigned(report.over_budget_frames);
    for (std::size_t index = 0U; index < kFrameTimeBudgetPhaseCount; ++index)
    {
        const FrameTimeBudgetPhaseStats& stats = report.phases[index];
        if (stats.samples == 0U)
            continue;
        summary += std::string(" ") +
            ToString(static_cast<FrameTimeBudgetPhase>(index)) + "_ms=" +
            FormatDouble(stats.mean_ms, 4) + " " +
            ToString(static_cast<FrameTimeBudgetPhase>(index)) + "_share=" +
            FormatDouble(stats.share, 4);
    }
    return summary;
}

std::string SerializeFrameTimeBudgetReport(const FrameTimeBudgetReport& report)
{
    std::vector<std::string> fields;
    fields.push_back(JsonField("format", "ror-frame-time-budget-v1"));
    fields.push_back(JsonField("mode", ToString(report.mode)));
    fields.push_back(JsonField("verdict", ToString(report.verdict)));
    fields.push_back(JsonRaw("passed", report.passed() ? "true" : "false"));
    fields.push_back(JsonField("scenario_id", report.context.scenario_id));
    fields.push_back(JsonField("terrain", report.context.terrain));
    fields.push_back(JsonField("actor", report.context.actor));
    fields.push_back(JsonField("renderer", report.context.renderer));
    fields.push_back(JsonRaw(
        "width", FormatUnsigned(report.context.width)));
    fields.push_back(JsonRaw(
        "height", FormatUnsigned(report.context.height)));
    fields.push_back(JsonRaw(
        "fullscreen", report.context.fullscreen ? "true" : "false"));
    fields.push_back(JsonRaw(
        "vsync", report.context.vsync ? "true" : "false"));
    fields.push_back(JsonRaw(
        "presents_frames",
        report.context.presents_frames ? "true" : "false"));
    fields.push_back(JsonRaw(
        "fps_limit",
        FormatSigned(static_cast<std::int64_t>(report.context.fps_limit))));
    fields.push_back(JsonRaw(
        "warmup_frames_requested",
        FormatUnsigned(report.limits.warmup_frames)));
    fields.push_back(JsonRaw(
        "minimum_frames", FormatUnsigned(report.limits.minimum_frames)));
    fields.push_back(JsonRaw(
        "requested_frames", FormatUnsigned(report.limits.requested_frames)));
    fields.push_back(JsonRaw(
        "sustained_budget_ms", FormatDouble(report.limits.sustained_ms, 4)));
    fields.push_back(JsonRaw(
        "percentile", FormatUnsigned(report.limits.percentile)));
    fields.push_back(JsonRaw(
        "percentile_budget_ms", FormatDouble(report.limits.percentile_ms, 4)));
    fields.push_back(JsonRaw(
        "observed_frames", FormatUnsigned(report.observed_frames)));
    fields.push_back(JsonRaw(
        "warmup_frames", FormatUnsigned(report.warmup_frames)));
    fields.push_back(JsonRaw(
        "accepted_frames", FormatUnsigned(report.accepted_frames)));
    fields.push_back(JsonRaw(
        "rejected_frames", FormatUnsigned(report.rejected_frames)));
    fields.push_back(JsonRaw(
        "saturated_frames", FormatUnsigned(report.saturated_frames)));
    fields.push_back(JsonRaw(
        "over_budget_frames", FormatUnsigned(report.over_budget_frames)));
    fields.push_back(JsonRaw("minimum_ms", FormatDouble(report.minimum_ms, 4)));
    fields.push_back(JsonRaw("mean_ms", FormatDouble(report.mean_ms, 4)));
    fields.push_back(JsonRaw("maximum_ms", FormatDouble(report.maximum_ms, 4)));
    fields.push_back(JsonRaw("p50_ms", FormatDouble(report.p50_ms, 4)));
    fields.push_back(JsonRaw("p95_ms", FormatDouble(report.p95_ms, 4)));
    fields.push_back(JsonRaw("p99_ms", FormatDouble(report.p99_ms, 4)));
    fields.push_back(JsonRaw("ranked_ms", FormatDouble(report.ranked_ms, 4)));
    fields.push_back(JsonRaw("mean_fps", FormatDouble(report.mean_fps, 3)));
    for (std::size_t index = 0U; index < kFrameTimeBudgetPhaseCount; ++index)
    {
        const std::string prefix =
            std::string("phase_") +
            ToString(static_cast<FrameTimeBudgetPhase>(index)) + "_";
        const FrameTimeBudgetPhaseStats& stats = report.phases[index];
        fields.push_back(JsonRaw(
            prefix + "samples", FormatUnsigned(stats.samples)));
        fields.push_back(JsonRaw(
            prefix + "total_ms", FormatDouble(stats.total_ms, 4)));
        fields.push_back(JsonRaw(
            prefix + "mean_ms", FormatDouble(stats.mean_ms, 4)));
        fields.push_back(JsonRaw(
            prefix + "max_ms", FormatDouble(stats.maximum_ms, 4)));
        fields.push_back(JsonRaw(
            prefix + "share", FormatDouble(stats.share, 6)));
    }
    fields.push_back(JsonRaw(
        "phase_remainder_mean_ms", FormatDouble(report.remainder.mean_ms, 4)));
    fields.push_back(JsonRaw(
        "phase_remainder_share", FormatDouble(report.remainder.share, 6)));
    fields.push_back(JsonRaw(
        "bin_width_ns", FormatUnsigned(kFrameTimeBudgetBinWidthNs)));
    fields.push_back(JsonRaw(
        "bin_count", FormatUnsigned(kFrameTimeBudgetBinCount)));

    std::string document = "{\n";
    for (std::size_t index = 0U; index < fields.size(); ++index)
    {
        document += fields[index];
        if (index + 1U < fields.size())
            document += ",";
        document += "\n";
    }
    document += "}\n";
    return document;
}

FrameTimeBudgetWriteResult WriteFrameTimeBudgetReceipt(
    const std::string& path,
    const std::string& document)
{
    if (path.empty())
        return FrameTimeBudgetWriteResult::FAILED;

#if defined(_WIN32)
    const int wide_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (wide_size <= 0)
        return FrameTimeBudgetWriteResult::FAILED;
    std::wstring wide_path;
    try
    {
        wide_path.resize(static_cast<std::size_t>(wide_size));
    }
    catch (...)
    {
        return FrameTimeBudgetWriteResult::FAILED;
    }
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
            &wide_path[0], wide_size) != wide_size)
    {
        return FrameTimeBudgetWriteResult::FAILED;
    }

    HANDLE handle = CreateFileW(
        wide_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            return FrameTimeBudgetWriteResult::EXISTS;
        return FrameTimeBudgetWriteResult::FAILED;
    }

    const char* data = document.c_str();
    DWORD remaining = static_cast<DWORD>(document.size());
    while (remaining > 0U)
    {
        DWORD written = 0U;
        if (!WriteFile(handle, data, remaining, &written, nullptr) ||
                written == 0U)
        {
            CloseHandle(handle);
            return FrameTimeBudgetWriteResult::FAILED;
        }
        data += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(handle) || !CloseHandle(handle))
        return FrameTimeBudgetWriteResult::FAILED;
    return FrameTimeBudgetWriteResult::WRITTEN;
#else
    int descriptor = -1;
    do
    {
        descriptor = ::open(
            path.c_str(), O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    }
    while (descriptor < 0 && errno == EINTR);

    if (descriptor < 0)
    {
        if (errno == EEXIST)
            return FrameTimeBudgetWriteResult::EXISTS;
        return FrameTimeBudgetWriteResult::FAILED;
    }

    const char* data = document.c_str();
    std::size_t remaining = document.size();
    while (remaining > 0U)
    {
        const ssize_t written = ::write(descriptor, data, remaining);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            ::close(descriptor);
            return FrameTimeBudgetWriteResult::FAILED;
        }
        if (written == 0)
        {
            ::close(descriptor);
            return FrameTimeBudgetWriteResult::FAILED;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }

    const int close_result = ::close(descriptor);
    return close_result == 0 || errno == EINTR
        ? FrameTimeBudgetWriteResult::WRITTEN
        : FrameTimeBudgetWriteResult::FAILED;
#endif
}

} // namespace RoR
