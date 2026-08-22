/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Strict, dependency-free tests for the playable frame-time budget.

#include "FrameTimeBudget.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::printf("FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool NearlyEqual(double left, double right, double tolerance)
{
    return std::fabs(left - right) <= tolerance;
}

RoR::FrameTimeBudgetLimits DefaultLimits()
{
    RoR::FrameTimeBudgetLimits limits;
    limits.warmup_frames = 0U;
    limits.minimum_frames = 10U;
    limits.requested_frames = 0U;
    limits.sustained_ms = 1000.0 / 60.0;
    limits.percentile_ms = 18.3;
    limits.percentile = 95U;
    return limits;
}

RoR::FrameTimeBudgetContext DefaultContext()
{
    RoR::FrameTimeBudgetContext context;
    context.scenario_id = "test";
    context.terrain = "simple2.terrn2";
    context.actor = "agoral.truck";
    context.renderer = "ogre14";
    context.width = 1920U;
    context.height = 1080U;
    context.fps_limit = 0;
    return context;
}

/// A steady 100 FPS run passes both budgets.
void TestSteadyRunPasses()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 1000; ++frame)
        CHECK(session.RecordFrame(0.010));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.accepted_frames == 1000U);
    CHECK(report.rejected_frames == 0U);
    CHECK(report.warmup_frames == 0U);
    CHECK(NearlyEqual(report.mean_ms, 10.0, 1e-6));
    CHECK(NearlyEqual(report.mean_fps, 100.0, 1e-6));
    CHECK(NearlyEqual(report.minimum_ms, 10.0, 1e-6));
    CHECK(NearlyEqual(report.maximum_ms, 10.0, 1e-6));
    CHECK(report.over_budget_frames == 0U);
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::PASS);
    CHECK(report.passed());
}

/// The ranked statistic is a bin upper edge, so it never understates the
/// measured interval.
void TestPercentileIsConservativeUpperEdge()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    // 99 frames at 10 ms and one at 40 ms: nearest-rank p95 is 10 ms and p99
    // is still 10 ms, while p100 (max) is the stall.
    for (int frame = 0; frame < 99; ++frame)
        CHECK(session.RecordFrame(0.010));
    CHECK(session.RecordFrame(0.040));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.accepted_frames == 100U);
    // 10 ms lands in the bin covering [9.984375, 10.0] ms; the upper edge is
    // exactly 10 ms because 10 ms is a whole multiple of the 1/64 ms width.
    CHECK(report.p50_ms >= 10.0);
    CHECK(report.p50_ms < 10.0 + (1.0 / 64.0) + 1e-9);
    CHECK(report.p95_ms >= 10.0);
    CHECK(report.p95_ms < 10.0 + (1.0 / 64.0) + 1e-9);
    CHECK(report.p99_ms >= 10.0);
    CHECK(report.p99_ms < 10.0 + (1.0 / 64.0) + 1e-9);
    CHECK(NearlyEqual(report.maximum_ms, 40.0, 1e-6));
    CHECK(report.over_budget_frames == 1U);
    // Every ranked value is at or above the exact minimum and at or below the
    // exact maximum plus one bin.
    CHECK(report.p99_ms >= report.p95_ms);
    CHECK(report.p95_ms >= report.p50_ms);
}

/// A distribution whose tail breaks the ceiling fails on the percentile even
/// though its mean is inside the sustained budget.
void TestPercentileFailureIsDistinctFromSustained()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 900; ++frame)
        CHECK(session.RecordFrame(0.008));
    for (int frame = 0; frame < 100; ++frame)
        CHECK(session.RecordFrame(0.030));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.mean_ms < 1000.0 / 60.0);
    CHECK(report.p95_ms > 18.3);
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::FAIL_PERCENTILE);
    CHECK(!report.passed());
}

/// A uniformly slow run fails on the sustained budget first.
void TestSustainedFailure()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 500; ++frame)
        CHECK(session.RecordFrame(0.017));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::FAIL_SUSTAINED);
    CHECK(report.over_budget_frames == 500U);
}

/// Warm-up frames are observed but excluded from the distribution.
void TestWarmupFramesAreExcluded()
{
    RoR::FrameTimeBudgetLimits limits = DefaultLimits();
    limits.warmup_frames = 100U;
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, limits, DefaultContext());

    // A hundred slow load frames followed by a steady playable run.
    for (int frame = 0; frame < 100; ++frame)
        CHECK(session.RecordFrame(0.500));
    for (int frame = 0; frame < 400; ++frame)
        CHECK(session.RecordFrame(0.008));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.observed_frames == 500U);
    CHECK(report.warmup_frames == 100U);
    CHECK(report.accepted_frames == 400U);
    CHECK(NearlyEqual(report.maximum_ms, 8.0, 1e-6));
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::PASS);
}

/// Malformed observations are never clamped into the distribution, and one
/// rejection permanently fails a gated run.
void TestRejectedSamplesFailClosed()
{
    const double malformed[] = {
        0.0,
        -0.001,
        std::nan(""),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        // Just inside and far beyond the ten-second ceiling.
        10.000001,
        1.0e9,
    };

    for (const double sample : malformed)
    {
        RoR::FrameTimeBudgetSession session(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
        for (int frame = 0; frame < 100; ++frame)
            CHECK(session.RecordFrame(0.008));
        CHECK(!session.RecordFrame(sample));
        for (int frame = 0; frame < 100; ++frame)
            CHECK(session.RecordFrame(0.008));

        const RoR::FrameTimeBudgetReport report = session.Finalize();
        CHECK(report.accepted_frames == 200U);
        CHECK(report.rejected_frames == 1U);
        CHECK(NearlyEqual(report.maximum_ms, 8.0, 1e-6));
        CHECK(report.verdict ==
            RoR::FrameTimeBudgetVerdict::FAIL_REJECTED_SAMPLE);
    }
}

/// A short run cannot pass, whatever its distribution looks like.
void TestShortRunFails()
{
    RoR::FrameTimeBudgetLimits limits = DefaultLimits();
    limits.minimum_frames = 600U;
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, limits, DefaultContext());
    for (int frame = 0; frame < 599; ++frame)
        CHECK(session.RecordFrame(0.004));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::FAIL_SHORT_RUN);
}

/// An active frame-rate limiter describes the limiter, not the renderer.
void TestActiveLimiterFailsClosed()
{
    RoR::FrameTimeBudgetContext context = DefaultContext();
    context.fps_limit = 60;
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
    for (int frame = 0; frame < 1000; ++frame)
        CHECK(session.RecordFrame(0.008));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.verdict ==
        RoR::FrameTimeBudgetVerdict::FAIL_LIMITER_ACTIVE);

    // The same run is still reported in measure mode, where the limiter is a
    // recorded fact rather than a gate.
    RoR::FrameTimeBudgetSession advisory(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), context);
    for (int frame = 0; frame < 1000; ++frame)
        CHECK(advisory.RecordFrame(0.008));
    CHECK(advisory.Finalize().verdict ==
        RoR::FrameTimeBudgetVerdict::ADVISORY);
}

/// A loop that does not present its own frames is reporting a producer
/// cadence, which must never be published as a frame rate.
void TestNonPresentingLoopFailsClosed()
{
    RoR::FrameTimeBudgetContext context = DefaultContext();
    context.presents_frames = false;
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
    // The two-process bridge's producer loop spins far faster than any
    // display: 15,000 FPS is exactly the signature this rejects.
    for (int frame = 0; frame < 5000; ++frame)
        CHECK(session.RecordFrame(0.000065));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.mean_fps > 10000.0);
    CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::FAIL_NOT_PRESENTING);
    CHECK(!report.passed());

    // The same run is still recorded in measure mode, where the fact is
    // reported rather than gated.
    RoR::FrameTimeBudgetSession advisory(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), context);
    for (int frame = 0; frame < 5000; ++frame)
        CHECK(advisory.RecordFrame(0.000065));
    const RoR::FrameTimeBudgetReport observed = advisory.Finalize();
    CHECK(observed.verdict == RoR::FrameTimeBudgetVerdict::ADVISORY);
    CHECK(RoR::SerializeFrameTimeBudgetReport(observed).find(
        "\"presents_frames\": false") != std::string::npos);
}

/// Invalid limits are refused rather than silently repaired.
void TestInvalidLimitsAreRefused()
{
    RoR::FrameTimeBudgetLimits limits = DefaultLimits();
    CHECK(limits.valid());

    RoR::FrameTimeBudgetLimits no_minimum = limits;
    no_minimum.minimum_frames = 0U;
    CHECK(!no_minimum.valid());

    RoR::FrameTimeBudgetLimits inverted = limits;
    inverted.percentile_ms = 8.0;
    inverted.sustained_ms = 16.0;
    CHECK(!inverted.valid());

    RoR::FrameTimeBudgetLimits bad_percentile = limits;
    bad_percentile.percentile = 0U;
    CHECK(!bad_percentile.valid());
    bad_percentile.percentile = 101U;
    CHECK(!bad_percentile.valid());

    RoR::FrameTimeBudgetLimits negative = limits;
    negative.sustained_ms = -1.0;
    CHECK(!negative.valid());

    RoR::FrameTimeBudgetLimits unbounded = limits;
    unbounded.minimum_frames = 1000U;
    unbounded.requested_frames = 10U; // below the minimum
    CHECK(!unbounded.valid());

    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, inverted, DefaultContext());
    for (int frame = 0; frame < 100; ++frame)
        CHECK(session.RecordFrame(0.008));
    CHECK(session.Finalize().verdict ==
        RoR::FrameTimeBudgetVerdict::FAIL_INVALID_LIMITS);
}

/// The requested-frame ceiling arms exactly once, after the warm-up.
void TestRequestedFrameCeiling()
{
    RoR::FrameTimeBudgetLimits limits = DefaultLimits();
    limits.warmup_frames = 5U;
    limits.minimum_frames = 10U;
    limits.requested_frames = 20U;
    CHECK(limits.valid());

    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, limits, DefaultContext());
    for (int frame = 0; frame < 5; ++frame)
    {
        CHECK(session.RecordFrame(0.008));
        CHECK(!session.ShutdownRequested());
    }
    for (int frame = 0; frame < 19; ++frame)
    {
        CHECK(session.RecordFrame(0.008));
        CHECK(!session.ShutdownRequested());
    }
    CHECK(session.RecordFrame(0.008));
    CHECK(session.ShutdownRequested());
    CHECK(session.AcceptedFrames() == 20U);

    // A session with no ceiling never asks for shutdown.
    RoR::FrameTimeBudgetSession open(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 5000; ++frame)
        CHECK(open.RecordFrame(0.008));
    CHECK(!open.ShutdownRequested());
}

/// Intervals at or beyond the saturating bin are retained, counted, and ranked
/// against the exact maximum.
void TestSaturatingBin()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 10; ++frame)
        CHECK(session.RecordFrame(0.008));
    // 2000 ms is far past the 128 ms bin range but well inside the ten-second
    // acceptance ceiling, so it is retained rather than rejected.
    CHECK(session.RecordFrame(2.0));
    // The ceiling itself is inclusive.
    CHECK(session.RecordFrame(10.0));

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.accepted_frames == 12U);
    CHECK(report.saturated_frames == 2U);
    CHECK(NearlyEqual(report.maximum_ms, 10000.0, 1e-6));
    // The nearest-rank p99 of 12 samples is the twelfth, which is the stall.
    CHECK(NearlyEqual(report.p99_ms, 10000.0, 1e-6));
}

/// The measured scene is named by its first observation, and a later
/// disagreement invalidates the run instead of averaging two scenes together.
void TestSceneIdentityIsObservedAndPinned()
{
    RoR::FrameTimeBudgetSession stable(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    CHECK(!stable.RecordingStarted());
    CHECK(stable.RecordFrame(0.008));
    CHECK(stable.RecordingStarted());
    stable.ObserveSceneIdentity("CityWorld.terrn2", "AlexisSaber.truck");
    for (int frame = 0; frame < 200; ++frame)
        CHECK(stable.RecordFrame(0.008));
    stable.ObserveSceneIdentity("CityWorld.terrn2", "AlexisSaber.truck");

    const RoR::FrameTimeBudgetReport pinned = stable.Finalize();
    CHECK(pinned.context.terrain == "CityWorld.terrn2");
    CHECK(pinned.context.actor == "AlexisSaber.truck");
    CHECK(pinned.verdict == RoR::FrameTimeBudgetVerdict::PASS);

    for (int changed = 0; changed < 2; ++changed)
    {
        RoR::FrameTimeBudgetSession reset(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
        for (int frame = 0; frame < 100; ++frame)
            CHECK(reset.RecordFrame(0.008));
        reset.ObserveSceneIdentity("CityWorld.terrn2", "AlexisSaber.truck");
        for (int frame = 0; frame < 100; ++frame)
            CHECK(reset.RecordFrame(0.008));
        // Either half of the identity changing is a different measurement.
        reset.ObserveSceneIdentity(
            changed == 0 ? "simple2.terrn2" : "CityWorld.terrn2",
            changed == 0 ? "AlexisSaber.truck" : "agoral.truck");

        const RoR::FrameTimeBudgetReport report = reset.Finalize();
        CHECK(report.verdict ==
            RoR::FrameTimeBudgetVerdict::FAIL_SCENE_CHANGED);
        // The first observation still names the run in the receipt.
        CHECK(report.context.terrain == "CityWorld.terrn2");
    }

    // An unloaded scene is not a scene change: the recorder may start before
    // the message queue has named the terrain.
    RoR::FrameTimeBudgetSession loading(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 50; ++frame)
        CHECK(loading.RecordFrame(0.008));
    loading.ObserveSceneIdentity("", "");
    for (int frame = 0; frame < 50; ++frame)
        CHECK(loading.RecordFrame(0.008));
    loading.ObserveSceneIdentity("CityWorld.terrn2", "AlexisSaber.truck");

    const RoR::FrameTimeBudgetReport loaded = loading.Finalize();
    CHECK(loaded.verdict == RoR::FrameTimeBudgetVerdict::PASS);
    CHECK(loaded.context.terrain == "CityWorld.terrn2");
    CHECK(loaded.context.actor == "AlexisSaber.truck");

    // In measure mode the change is recorded but the verdict stays advisory.
    RoR::FrameTimeBudgetSession advisory(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 100; ++frame)
        CHECK(advisory.RecordFrame(0.008));
    advisory.ObserveSceneIdentity("a.terrn2", "a.truck");
    advisory.ObserveSceneIdentity("b.terrn2", "b.truck");
    CHECK(advisory.Finalize().verdict ==
        RoR::FrameTimeBudgetVerdict::ADVISORY);
}

/// Phase attribution splits a frame into its declared parts plus a remainder,
/// and describes exactly the frames the distribution retained.
void TestPhaseAttribution()
{
    RoR::FrameTimeBudgetLimits limits = DefaultLimits();
    limits.warmup_frames = 10U;
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::MEASURE, limits, DefaultContext());

    // Warm-up frames and their phases are both excluded.
    for (int frame = 0; frame < 10; ++frame)
    {
        CHECK(session.RecordFrame(0.050));
        session.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, 0.040);
        CHECK(!session.Recording());
    }
    // Recording reflects the frame just observed, so it turns on only once an
    // accepted frame has been reported, not merely when warm-up is exhausted.
    CHECK(!session.Recording());

    // A 10 ms frame: 6 ms producer, 3 ms renderer, 1 ms remainder.
    for (int frame = 0; frame < 100; ++frame)
    {
        CHECK(session.RecordFrame(0.010));
        CHECK(session.Recording());
        session.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, 0.006);
        session.RecordPhase(RoR::FrameTimeBudgetPhase::SCENE_DISPATCH, 0.003);
    }

    const RoR::FrameTimeBudgetReport report = session.Finalize();
    CHECK(report.accepted_frames == 100U);
    const std::size_t producer =
        static_cast<std::size_t>(RoR::FrameTimeBudgetPhase::PRODUCER);
    const std::size_t renderer =
        static_cast<std::size_t>(RoR::FrameTimeBudgetPhase::SCENE_DISPATCH);
    CHECK(report.phases[producer].samples == 100U);
    CHECK(NearlyEqual(report.phases[producer].mean_ms, 6.0, 1e-6));
    CHECK(NearlyEqual(report.phases[producer].share, 0.6, 1e-6));
    CHECK(NearlyEqual(report.phases[renderer].mean_ms, 3.0, 1e-6));
    CHECK(NearlyEqual(report.phases[renderer].share, 0.3, 1e-6));
    CHECK(NearlyEqual(report.remainder.mean_ms, 1.0, 1e-6));
    CHECK(NearlyEqual(report.remainder.share, 0.1, 1e-6));
    CHECK(NearlyEqual(report.phases[producer].share +
                          report.phases[renderer].share +
                          report.remainder.share,
                      1.0, 1e-9));

    // Overlapping phases cannot produce a negative remainder.
    RoR::FrameTimeBudgetSession overlapping(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 50; ++frame)
    {
        CHECK(overlapping.RecordFrame(0.010));
        overlapping.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, 0.009);
        overlapping.RecordPhase(RoR::FrameTimeBudgetPhase::SCENE_DISPATCH, 0.009);
    }
    const RoR::FrameTimeBudgetReport overlap = overlapping.Finalize();
    CHECK(overlap.remainder.total_ms == 0.0);
    CHECK(overlap.remainder.share == 0.0);

    // Malformed phase samples are ignored, never accumulated.
    RoR::FrameTimeBudgetSession malformed(
        RoR::FrameTimeBudgetMode::MEASURE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 20; ++frame)
        CHECK(malformed.RecordFrame(0.010));
    malformed.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, std::nan(""));
    malformed.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, -1.0);
    malformed.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, 0.0);
    malformed.RecordPhase(RoR::FrameTimeBudgetPhase::PRODUCER, 1.0e9);
    CHECK(malformed.Finalize().phases[producer].samples == 0U);
}

/// The combined-runtime workload gate ranks the renderer-owned Ogre-Next HDR
/// scene pass for every accepted frame. Missing, duplicate, or inexact
/// compositor receipts cannot be replaced with hidden producer counters.
void TestNativeSceneDrawSubmissionGate()
{
    RoR::FrameTimeBudgetContext context = DefaultContext();
    context.renderer = "ogre-next-combined";
    context.requires_native_scene_draw_metrics = true;

    {
        RoR::FrameTimeBudgetSession session(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
        for (int frame = 0; frame < 1000; ++frame)
        {
            CHECK(session.RecordFrame(0.008));
            session.RecordNativeSceneDrawSubmissions(
                frame < 990 ? 934U : 2400U, true);
        }
        const RoR::FrameTimeBudgetReport report = session.Finalize();
        CHECK(report.verdict == RoR::FrameTimeBudgetVerdict::PASS);
        CHECK(report.native_scene_draws.exact_samples == 1000U);
        CHECK(report.native_scene_draws.rejected_samples == 0U);
        CHECK(report.native_scene_draws.p99 == 934U);
        CHECK(report.native_scene_draws.maximum == 2400U);
    }

    {
        RoR::FrameTimeBudgetSession missing(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
        for (int frame = 0; frame < 10; ++frame)
        {
            CHECK(missing.RecordFrame(0.008));
            if (frame != 9)
                missing.RecordNativeSceneDrawSubmissions(934U, true);
        }
        CHECK(missing.Finalize().verdict ==
            RoR::FrameTimeBudgetVerdict::FAIL_NATIVE_SCENE_DRAW_METRICS);
    }

    {
        RoR::FrameTimeBudgetSession duplicate(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
        for (int frame = 0; frame < 10; ++frame)
        {
            CHECK(duplicate.RecordFrame(0.008));
            duplicate.RecordNativeSceneDrawSubmissions(934U, true);
        }
        duplicate.RecordNativeSceneDrawSubmissions(934U, true);
        CHECK(duplicate.Finalize().verdict ==
            RoR::FrameTimeBudgetVerdict::FAIL_NATIVE_SCENE_DRAW_METRICS);
    }

    {
        RoR::FrameTimeBudgetSession over(
            RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
        for (int frame = 0; frame < 1000; ++frame)
        {
            CHECK(over.RecordFrame(0.008));
            over.RecordNativeSceneDrawSubmissions(
                frame < 989 ? 934U : 3000U, true);
        }
        const RoR::FrameTimeBudgetReport report = over.Finalize();
        CHECK(report.native_scene_draws.p99 == 3000U);
        CHECK(report.verdict ==
            RoR::FrameTimeBudgetVerdict::FAIL_NATIVE_SCENE_DRAW_BUDGET);
    }
}

/// Finalize is pure: repeated calls return the same report.
void TestFinalizeIsRepeatable()
{
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), DefaultContext());
    for (int frame = 0; frame < 300; ++frame)
        CHECK(session.RecordFrame(0.009));

    const RoR::FrameTimeBudgetReport first = session.Finalize();
    const RoR::FrameTimeBudgetReport second = session.Finalize();
    CHECK(first.accepted_frames == second.accepted_frames);
    CHECK(first.verdict == second.verdict);
    CHECK(NearlyEqual(first.mean_ms, second.mean_ms, 0.0));
    CHECK(NearlyEqual(first.p95_ms, second.p95_ms, 0.0));
    CHECK(SerializeFrameTimeBudgetReport(first) ==
        SerializeFrameTimeBudgetReport(second));
}

void TestModeParsing()
{
    RoR::FrameTimeBudgetMode mode = RoR::FrameTimeBudgetMode::GATE;
    CHECK(RoR::ParseFrameTimeBudgetMode("off", mode));
    CHECK(mode == RoR::FrameTimeBudgetMode::OFF);
    CHECK(RoR::ParseFrameTimeBudgetMode("measure", mode));
    CHECK(mode == RoR::FrameTimeBudgetMode::MEASURE);
    CHECK(RoR::ParseFrameTimeBudgetMode("gate", mode));
    CHECK(mode == RoR::FrameTimeBudgetMode::GATE);

    // Unknown spellings are refused, leaving the caller's value untouched.
    for (const char* unknown : {"", "Off", "GATE", "on", "true", "measure "})
    {
        RoR::FrameTimeBudgetMode preserved = RoR::FrameTimeBudgetMode::MEASURE;
        CHECK(!RoR::ParseFrameTimeBudgetMode(unknown, preserved));
        CHECK(preserved == RoR::FrameTimeBudgetMode::MEASURE);
    }
}

/// The receipt carries the identity and every gated number, and hostile names
/// cannot break out of their JSON strings.
void TestSerializationIsCompleteAndEscaped()
{
    RoR::FrameTimeBudgetContext context = DefaultContext();
    context.scenario_id = "cityworld\"\n\\playable";
    RoR::FrameTimeBudgetSession session(
        RoR::FrameTimeBudgetMode::GATE, DefaultLimits(), context);
    for (int frame = 0; frame < 200; ++frame)
        session.RecordFrame(0.008);

    const std::string document =
        RoR::SerializeFrameTimeBudgetReport(session.Finalize());
    CHECK(document.find("\"format\": \"ror-frame-time-budget-v1\"") !=
        std::string::npos);
    CHECK(document.find("\"verdict\": \"pass\"") != std::string::npos);
    CHECK(document.find("\"passed\": true") != std::string::npos);
    CHECK(document.find("\"accepted_frames\": 200") != std::string::npos);
    CHECK(document.find("\"percentile\": 95") != std::string::npos);
    CHECK(document.find("\"percentile_budget_ms\": 18.3000") !=
        std::string::npos);
    CHECK(document.find("\"width\": 1920") != std::string::npos);
    CHECK(document.find("\"height\": 1080") != std::string::npos);
    CHECK(document.find("\"fps_limit\": 0") != std::string::npos);
    CHECK(document.find("\"bin_width_ns\": 15625") != std::string::npos);
    CHECK(document.find("cityworld\\\"\\n\\\\playable") != std::string::npos);
    // The escaped payload must not terminate its own string.
    CHECK(document.find("cityworld\"\n") == std::string::npos);

    const std::string summary =
        RoR::FormatFrameTimeBudgetSummary(session.Finalize());
    CHECK(summary.rfind("[RoR|Perf] ", 0U) == 0U);
    CHECK(summary.find("verdict=pass") != std::string::npos);
    CHECK(summary.find("frames=200") != std::string::npos);
}

/// A receipt is created exclusively and never overwritten.
void TestReceiptWriteIsExclusive(const char* directory)
{
    std::string path = std::string(directory) + "/frame-time-receipt.json";
    std::remove(path.c_str());

    const std::string document = "{\n  \"format\": \"test\"\n}\n";
    CHECK(RoR::WriteFrameTimeBudgetReceipt(path, document) ==
        RoR::FrameTimeBudgetWriteResult::WRITTEN);

    std::FILE* handle = std::fopen(path.c_str(), "rb");
    CHECK(handle != nullptr);
    if (handle != nullptr)
    {
        char buffer[128] = {0};
        const std::size_t read =
            std::fread(buffer, 1U, sizeof(buffer) - 1U, handle);
        std::fclose(handle);
        CHECK(std::string(buffer, read) == document);
    }

    // A second write refuses instead of replacing the retained evidence.
    CHECK(RoR::WriteFrameTimeBudgetReceipt(path, "{}\n") ==
        RoR::FrameTimeBudgetWriteResult::EXISTS);
    CHECK(RoR::WriteFrameTimeBudgetReceipt("", document) ==
        RoR::FrameTimeBudgetWriteResult::FAILED);
    CHECK(RoR::WriteFrameTimeBudgetReceipt(
            std::string(directory) + "/missing-directory/receipt.json",
            document) == RoR::FrameTimeBudgetWriteResult::FAILED);

    std::remove(path.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    TestSteadyRunPasses();
    TestPercentileIsConservativeUpperEdge();
    TestPercentileFailureIsDistinctFromSustained();
    TestSustainedFailure();
    TestWarmupFramesAreExcluded();
    TestRejectedSamplesFailClosed();
    TestShortRunFails();
    TestActiveLimiterFailsClosed();
    TestInvalidLimitsAreRefused();
    TestRequestedFrameCeiling();
    TestSaturatingBin();
    TestSceneIdentityIsObservedAndPinned();
    TestPhaseAttribution();
    TestNativeSceneDrawSubmissionGate();
    TestNonPresentingLoopFailsClosed();
    TestFinalizeIsRepeatable();
    TestModeParsing();
    TestSerializationIsCompleteAndEscaped();
    TestReceiptWriteIsExclusive(argc > 1 ? argv[1] : ".");

    if (g_failures != 0)
    {
        std::printf("%d frame-time budget check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("frame time budget tests passed\n");
    return 0;
}
