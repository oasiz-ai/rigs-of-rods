/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free, bounded, fail-closed playable frame-time budget.
///
/// The roadmap's CityWorld visual gate and the Ogre-Next combined-runtime
/// milestone both declare a sustained frame-rate budget that had no
/// measurement seam. This module owns that seam. It is intentionally free of
/// OGRE, GUI, and configuration dependencies so the exact production kernel is
/// compiled and executed by a standalone strict test binary on every platform.
///
/// The recorder never allocates after construction, never samples a clock
/// itself, and never silently repairs a malformed observation: an out-of-range
/// interval is counted and permanently invalidates the run instead of being
/// clamped into the distribution.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {

/// One bin is exactly 1/64 ms. The width is a binary fraction so every bin
/// edge is exact in both `double` and the integer nanosecond domain.
inline constexpr std::uint64_t kFrameTimeBudgetBinWidthNs = 15625U;

/// 8192 bins cover [0 ms, 128 ms). One additional saturating bin retains
/// everything at or above 128 ms so a stall is never lost from the ranking.
inline constexpr std::size_t kFrameTimeBudgetBinCount = 8192U;
inline constexpr std::size_t kFrameTimeBudgetTotalBins =
    kFrameTimeBudgetBinCount + 1U;

/// Observations outside this range are rejected rather than recorded. A
/// non-positive interval cannot come from a monotonic frame clock, and a
/// ten-second interval is a suspended process, not a rendered frame. The
/// integer suffix is mandatory: the same product written with plain `unsigned`
/// literals wraps to 1.41 s before it is widened.
inline constexpr std::uint64_t kFrameTimeBudgetMaximumSampleNs =
    10ULL * 1000ULL * 1000ULL * 1000ULL;

/// Hard ceiling on retained frames. It bounds the run and keeps every bin
/// counter inside 32 bits.
inline constexpr std::uint64_t kFrameTimeBudgetMaximumFrames = 2000000U;

/// Process exit code for a refused or failed gated run. 73 and 74 are already
/// reserved by the renderer child contract, so the budget owns 75 exclusively.
inline constexpr int kFrameTimeBudgetFailureExitCode = 75;

enum class FrameTimeBudgetMode : std::uint8_t {
    /// No recorder is created and the render loop is untouched.
    OFF = 0U,
    /// Record and report the distribution; the verdict is advisory.
    MEASURE,
    /// Record, report, and fail closed against the declared budgets.
    GATE,
};

enum class FrameTimeBudgetVerdict : std::uint8_t {
    /// The run was recorded but the caller has not requested a verdict.
    ADVISORY = 0U,
    PASS,
    /// Fewer accepted frames than the declared minimum.
    FAIL_SHORT_RUN,
    /// At least one observation was outside the accepted range.
    FAIL_REJECTED_SAMPLE,
    /// The mean interval missed the sustained frame-rate budget.
    FAIL_SUSTAINED,
    /// The ranked percentile interval exceeded its ceiling.
    FAIL_PERCENTILE,
    /// A frame-rate limiter was active, so the distribution is synthetic.
    FAIL_LIMITER_ACTIVE,
    /// The declared limits were themselves invalid.
    FAIL_INVALID_LIMITS,
    /// The measured scene changed while the run was recording.
    FAIL_SCENE_CHANGED,
    /// The measured loop does not present frames, so its interval is a
    /// producer cadence rather than a frame rate.
    FAIL_NOT_PRESENTING,
};

/// Declared acceptance budget. Defaults carry the roadmap's macOS arm64 high
/// preset contract: sustained 60 FPS with a 18.3 ms p95 ceiling.
struct FrameTimeBudgetLimits {
    /// Frames discarded before recording starts. Shader compilation, streaming
    /// and first-light residency belong to load, not to the playable budget.
    std::uint32_t warmup_frames = 120U;
    /// Minimum accepted frames for a verdict to be meaningful.
    std::uint32_t minimum_frames = 600U;
    /// Requested accepted frames; the session asks for shutdown when reached.
    /// Zero records until the game exits on its own.
    std::uint64_t requested_frames = 0U;
    /// Sustained budget for the mean interval, in milliseconds.
    double sustained_ms = 1000.0 / 60.0;
    /// Ranked-percentile ceiling, in milliseconds.
    double percentile_ms = 18.3;
    /// Percentile to rank, in whole percent.
    std::uint32_t percentile = 95U;

    [[nodiscard]] bool valid() const noexcept;
};

/// Immutable identity of the measured configuration. It is copied into the
/// receipt so a report can never be read as belonging to another scene.
struct FrameTimeBudgetContext {
    std::string scenario_id;
    std::string terrain;
    std::string actor;
    std::string renderer;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool fullscreen = false;
    /// Value of `gfx_fps_limit`. Any non-zero limiter fails a gated run.
    std::int32_t fps_limit = 0;
    bool vsync = false;
    /// True when this loop presents its own frames. In the two-process bridge
    /// the game loop only produces scenes for a separate presentation child,
    /// so its inter-frame interval is not a frame rate and must never be
    /// reported as one.
    bool presents_frames = true;
};

/// Frame phases attributed inside one recorded frame. The combined runtime
/// still drives a hidden OGRE 14 producer before dispatching to Ogre-Next, so
/// a frame-time total alone cannot say which of the two costs what. Everything
/// outside these phases is reported as the remainder.
enum class FrameTimeBudgetPhase : std::uint8_t {
    /// The hidden OGRE 14 scene and resource producer.
    PRODUCER = 0U,
    /// CPU conversion of the joined scene, up to and including snapshot
    /// production.
    SCENE_CAPTURE,
    /// Dispatch to the frontend, render submission, and completion wait.
    SCENE_DISPATCH,
    COUNT,
};

inline constexpr std::size_t kFrameTimeBudgetPhaseCount =
    static_cast<std::size_t>(FrameTimeBudgetPhase::COUNT);

struct FrameTimeBudgetPhaseStats {
    std::uint64_t samples = 0U;
    double total_ms = 0.0;
    double mean_ms = 0.0;
    double maximum_ms = 0.0;
    /// Share of the run's total accepted frame time, in [0, 1].
    double share = 0.0;
};

const char* ToString(FrameTimeBudgetPhase phase) noexcept;

struct FrameTimeBudgetReport {
    FrameTimeBudgetMode mode = FrameTimeBudgetMode::OFF;
    FrameTimeBudgetVerdict verdict = FrameTimeBudgetVerdict::FAIL_SHORT_RUN;
    FrameTimeBudgetLimits limits;
    FrameTimeBudgetContext context;

    std::uint64_t observed_frames = 0U;
    std::uint64_t warmup_frames = 0U;
    std::uint64_t accepted_frames = 0U;
    std::uint64_t rejected_frames = 0U;
    std::uint64_t saturated_frames = 0U;
    std::uint64_t over_budget_frames = 0U;

    double minimum_ms = 0.0;
    double mean_ms = 0.0;
    double maximum_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    /// Percentile named by `limits.percentile`. It is the gated statistic.
    double ranked_ms = 0.0;
    double mean_fps = 0.0;

    FrameTimeBudgetPhaseStats phases[kFrameTimeBudgetPhaseCount];
    /// Accepted frame time attributed to no declared phase.
    FrameTimeBudgetPhaseStats remainder;

    [[nodiscard]] bool passed() const noexcept {
        return verdict == FrameTimeBudgetVerdict::PASS;
    }
};

/// Bounded streaming recorder. One instance owns one run.
class FrameTimeBudgetSession {
public:
    FrameTimeBudgetSession(
        FrameTimeBudgetMode mode,
        const FrameTimeBudgetLimits& limits,
        const FrameTimeBudgetContext& context);

    /// Record one inter-frame interval. `seconds` comes from the render loop's
    /// existing frame clock; the session performs no timing of its own.
    /// Returns false when the observation was rejected.
    bool RecordFrame(double seconds);

    /// Observe the live scene identity. The terrain and actor are not known
    /// when the recorder is armed, because the render loop's own message queue
    /// loads them. The first observation therefore names the measured scene,
    /// and any later disagreement means a map reset or actor change happened
    /// inside the recorded window, which invalidates the distribution.
    void ObserveSceneIdentity(
        const std::string& terrain,
        const std::string& actor);

    /// Attribute part of the current frame to a phase. Ignored during warm-up
    /// so phase totals and frame totals describe the same frames.
    void RecordPhase(FrameTimeBudgetPhase phase, double seconds);

    /// True when the most recently observed frame was retained rather than
    /// discarded as warm-up. `RecordFrame` reports the interval that just
    /// elapsed, while phases are attributed to the frame now being built, so
    /// tying attribution to the frame counter directly would let the last
    /// warm-up frame contribute phases the distribution never counted.
    [[nodiscard]] bool Recording() const noexcept {
        return last_frame_retained_;
    }

    /// True once the recorder has accepted at least one frame and can name the
    /// scene it is measuring.
    [[nodiscard]] bool RecordingStarted() const noexcept {
        return accepted_frames_ > 0U;
    }

    /// True once `limits.requested_frames` accepted frames exist. The render
    /// loop uses this to request an orderly shutdown.
    [[nodiscard]] bool ShutdownRequested() const noexcept;

    [[nodiscard]] std::uint64_t AcceptedFrames() const noexcept {
        return accepted_frames_;
    }

    [[nodiscard]] const FrameTimeBudgetLimits& Limits() const noexcept {
        return limits_;
    }

    /// Compute the report. The session is not modified, so a caller may
    /// finalize repeatedly (for example for a periodic progress line).
    [[nodiscard]] FrameTimeBudgetReport Finalize() const;

private:
    [[nodiscard]] double RankedMilliseconds(std::uint32_t percentile) const;

    FrameTimeBudgetMode mode_;
    FrameTimeBudgetLimits limits_;
    FrameTimeBudgetContext context_;
    bool limits_valid_;

    bool last_frame_retained_ = false;
    bool scene_identity_observed_ = false;
    bool scene_identity_changed_ = false;

    std::vector<std::uint32_t> bins_;
    std::uint64_t observed_frames_ = 0U;
    std::uint64_t warmup_frames_ = 0U;
    std::uint64_t accepted_frames_ = 0U;
    std::uint64_t rejected_frames_ = 0U;
    std::uint64_t saturated_frames_ = 0U;
    std::uint64_t over_budget_frames_ = 0U;
    std::uint64_t minimum_ns_ = 0U;
    std::uint64_t maximum_ns_ = 0U;
    std::uint64_t total_ns_ = 0U;
    std::uint64_t phase_samples_[kFrameTimeBudgetPhaseCount] = {};
    std::uint64_t phase_total_ns_[kFrameTimeBudgetPhaseCount] = {};
    std::uint64_t phase_maximum_ns_[kFrameTimeBudgetPhaseCount] = {};
};

/// Parse a mode name. Unknown names are rejected instead of defaulting.
bool ParseFrameTimeBudgetMode(
    const std::string& name,
    FrameTimeBudgetMode& mode);

const char* ToString(FrameTimeBudgetMode mode) noexcept;
const char* ToString(FrameTimeBudgetVerdict verdict) noexcept;

/// Canonical single-line log summary.
std::string FormatFrameTimeBudgetSummary(const FrameTimeBudgetReport& report);

/// Canonical `ror-frame-time-budget-v1` receipt document.
std::string SerializeFrameTimeBudgetReport(const FrameTimeBudgetReport& report);

enum class FrameTimeBudgetWriteResult : std::uint8_t {
    WRITTEN = 0U,
    /// The path already exists. A receipt is never overwritten.
    EXISTS,
    FAILED,
};

/// Create `path` exclusively and write `document`. An existing path is an
/// explicit refusal, not an overwrite.
FrameTimeBudgetWriteResult WriteFrameTimeBudgetReceipt(
    const std::string& path,
    const std::string& document);

} // namespace RoR
