#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static closure checks for the playable frame-time budget wiring.

The kernel itself is proven by the strict C++ binary. These checks lock the
runtime seam that the C++ tests cannot see: the CVar contract, the exact
render-loop sample point, the fail-closed startup, and the build graph.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def strip_comments(text: str) -> str:
    """Remove block and line comments so prose cannot satisfy a code check."""

    without_blocks = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", without_blocks)

BUDGET_CVARS = (
    "gfx_frame_budget_mode",
    "gfx_frame_budget_receipt_path",
    "gfx_frame_budget_scenario_id",
    "gfx_frame_budget_sustained_ms",
    "gfx_frame_budget_percentile",
    "gfx_frame_budget_percentile_ms",
    "gfx_frame_budget_warmup_frames",
    "gfx_frame_budget_minimum_frames",
    "gfx_frame_budget_requested_frames",
)


class FrameTimeBudgetContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.main = (ROOT / "source/main/main.cpp").read_text()
        self.header = (
            ROOT / "source/main/system/FrameTimeBudget.h"
        ).read_text()
        self.source = (
            ROOT / "source/main/system/FrameTimeBudget.cpp"
        ).read_text()

    def test_lifecycle_cvars_are_declared_and_never_archived(self) -> None:
        declarations = (ROOT / "source/main/Application.h").read_text()
        definitions = (ROOT / "source/main/Application.cpp").read_text()
        creation = (ROOT / "source/main/system/CVar.cpp").read_text()
        for name in BUDGET_CVARS:
            self.assertEqual(
                declarations.count(f"extern CVar* {name};"), 1, name)
            self.assertEqual(definitions.count(f"CVar* {name};"), 1, name)
            anchor = f"App::{name} = this->cVarCreate("
            self.assertEqual(creation.count(anchor), 1, name)
            block = creation[
                creation.index(anchor) : creation.index(anchor) + 420
            ]
            # A recording contract must never be silently restored from a
            # user's config file into a later, unrelated session.
            self.assertNotIn("CVAR_ARCHIVE", block, name)

    def test_default_budget_is_the_declared_sixty_hertz_contract(self) -> None:
        creation = (ROOT / "source/main/system/CVar.cpp").read_text()
        for name, default in (
            ("gfx_frame_budget_mode", '"off"'),
            ("gfx_frame_budget_sustained_ms", '"16.6667"'),
            ("gfx_frame_budget_percentile", '"95"'),
            ("gfx_frame_budget_percentile_ms", '"18.3"'),
        ):
            anchor = f"App::{name} = this->cVarCreate("
            block = creation[
                creation.index(anchor) : creation.index(anchor) + 420
            ]
            self.assertIn(default, block, name)

    def test_recorder_samples_the_render_loops_own_delta_time(self) -> None:
        anchor = "const auto record_frame_budget = [&](float frame_dt)"
        self.assertEqual(self.main.count(anchor), 1)
        block = self.main[
            self.main.index(anchor)
            : self.main.index("// In combined mode a poll", self.main.index(anchor))
        ]
        # The sample must be taken from the loop's committed delta time, and
        # the recorder must not introduce a clock of its own.
        self.assertIn(
            "frame_budget_session->RecordFrame(\n"
            "                    static_cast<double>(frame_dt));",
            block,
        )
        self.assertNotIn("high_resolution_clock", block)
        self.assertEqual(self.main.count("RecordFrame("), 1)
        self.assertEqual(self.main.count("record_frame_budget(dt);"), 2)
        self.assertNotIn("std::chrono", self.source)

    def test_combined_measurement_starts_after_exact_native_scene(self) -> None:
        self.assertIn(
            "bool frame_budget_combined_native_scene_ready = false;",
            self.main,
        )
        readiness_anchor = (
            "const auto observe_frame_budget_native_scene_ready =")
        self.assertEqual(self.main.count(readiness_anchor), 1)
        readiness = self.main[
            self.main.index(readiness_anchor)
            : self.main.index(
                "// Capture first:", self.main.index(readiness_anchor))
        ]
        for proof in (
            "retained_scene_audit.available",
            "retained_scene_audit.version >= 6U",
            "retained_scene_audit.last_native_renderer_frame_id ==",
            "retained_scene_audit.last_native_pass_metrics_exact",
            "retained_scene_audit.last_native_scene_draws > 0U",
            "frame_budget_native_readiness_minimum_frame_id == 0U",
            "frontend_frame_id <\n"
            "                        frame_budget_native_readiness_minimum_frame_id",
            "frame_budget_authoritative_scene_ready()",
        ):
            self.assertIn(proof, readiness)
        self.assertIn(
            "[RoR|Perf] Native scene measurement ready:", readiness)
        self.assertIn(
            "kFrameBudgetNativeReadinessMaxFrames = 8U", self.main)
        self.assertIn(
            "frame_budget_native_readiness_completed_frames >=\n"
            "                        kFrameBudgetNativeReadinessMaxFrames",
            readiness,
        )
        self.assertIn(
            "[RoR|Perf] Refusing frame budget: no exact ", readiness)
        self.assertIn("MSG_APP_SHUTDOWN_REQUESTED", readiness)
        self.assertIn(
            "if (frame_budget_combined_native_scene_failed)\n"
            "            application_exit_code = "
            "kFrameTimeBudgetFailureExitCode;",
            self.main,
        )

        combined_interval = self.main[
            self.main.index("const auto combined_frame_now")
            : self.main.index("#else", self.main.index(
                "const auto combined_frame_now"))
        ]
        observe = combined_interval.index(
            "observe_frame_budget_native_scene_ready(")
        record = combined_interval.index("record_frame_budget(dt);")
        self.assertLess(observe, record)
        self.assertIn(
            "if (!frame_budget_native_readiness_crossed)\n"
            "                record_frame_budget(dt);",
            combined_interval,
        )
        # A synchronously completed native frame owns the same arming seam.
        post = self.main[
            self.main.index("renderer_combined_session->PostUpdatedScene(")
            : self.main.index(
                "if (frame_budget_session != nullptr)",
                self.main.index("renderer_combined_session->PostUpdatedScene("),
            )
        ]
        self.assertIn("observe_frame_budget_native_scene_ready(", post)
        self.assertIn(
            "frame_budget_native_readiness_minimum_frame_id =\n"
            "                                scene_result.frontend_frame_id;",
            post,
        )
        self.assertLess(
            post.index("frame_budget_native_readiness_minimum_frame_id ="),
            post.index("observe_frame_budget_native_scene_ready("),
        )
        # Startup exclusion is a boundary, not a relaxed malformed-sample
        # rule: the ten-second ceiling remains exact and fail-closed.
        self.assertIn(
            "kFrameTimeBudgetMaximumSampleNs =\n"
            "    10ULL * 1000ULL * 1000ULL * 1000ULL;",
            self.header,
        )

    def test_rejected_interval_receipt_is_attributable(self) -> None:
        for field in (
            "rejected_frame_intervals_nan",
            "rejected_frame_intervals_positive_infinity",
            "rejected_frame_intervals_negative_infinity",
            "rejected_frame_intervals_non_positive",
            "rejected_frame_intervals_below_minimum",
            "rejected_frame_intervals_above_maximum",
            "first_rejected_frame_interval_reason",
            "first_rejected_frame_interval_seconds",
            "first_rejected_frame_interval_ieee754",
        ):
            self.assertIn(field, self.source)
        self.assertIn("RejectFrameInterval(", self.source)
        self.assertIn("DoubleBits(seconds)", self.source)
        self.assertNotIn("rejected_frames_ = 0", self.source)

    def test_tsan_scene_uses_bounded_native_settling_warmup(self) -> None:
        workflow = (
            ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
        ).read_text()
        for trigger in (
            "source/main/main.cpp",
            "source/main/system/FrameTimeBudget.cpp",
            "source/main/system/FrameTimeBudget.h",
            "tests/tools/test_frame_time_budget_contract.py",
        ):
            self.assertIn(f"      - {trigger}\n", workflow)
        # This is eight completed intervals after the exact native readiness
        # marker, not a wall-clock sleep. Deferred reflection publication has
        # a four-frame latency and source-backed particles settle after the
        # initial scene; the additional frames keep that bounded cold work in
        # warmup while the production ten-second ceiling remains unchanged.
        self.assertIn("--warmup-frames 8", workflow)
        self.assertNotIn("--allow-above-maximum", workflow)
        self.assertIn('              "warmup_frames_requested": 8,', workflow)
        self.assertIn('              "warmup_frames": 8,', workflow)
        self.assertIn('              "observed_frames": 32,', workflow)
        self.assertIn('              "accepted_frames": 24,', workflow)
        self.assertIn('              "rejected_frames": 0,', workflow)
        self.assertIn(
            '              "rejected_frame_intervals_above_maximum": 0,',
            workflow,
        )
        self.assertIn(
            "readiness_marker='[RoR|Perf] Native scene measurement ready:'",
            workflow,
        )
        self.assertIn(
            'readiness_count=$(grep -Fc "$readiness_marker" '
            '"$runtime_log" || true)',
            workflow,
        )
        self.assertIn("if (( readiness_count != 1 )); then", workflow)
        self.assertIn("if (( readiness_status != 0 )); then", workflow)
        for receipt_field in (
            "rejected_frame_intervals_nan",
            "rejected_frame_intervals_positive_infinity",
            "rejected_frame_intervals_negative_infinity",
            "rejected_frame_intervals_non_positive",
            "rejected_frame_intervals_below_minimum",
            "rejected_frame_intervals_above_maximum",
            "first_rejected_frame_interval_reason",
            "first_rejected_frame_interval_seconds",
            "first_rejected_frame_interval_ieee754",
        ):
            self.assertIn(f'              "{receipt_field}":', workflow)

    def test_presentation_fact_distinguishes_bridge_from_combined(
        self,
    ) -> None:
        # "This process presents" is not "OGRE 14 presents". Both the
        # two-process bridge and the combined runtime resolve OGRE 14
        # ownership with bridge_active=true, but only the bridge hands
        # presentation to a separate process. Using the OGRE 14 fact alone
        # would refuse the combined runtime, which is the one build whose loop
        # interval is a real Ogre-Next frame interval.
        anchor = "CreateFrameTimeBudgetSession("
        call = self.main[
            self.main.index(anchor, self.main.index("frame_budget_refused ="))
            : self.main.index("frame_budget_refused);")
        ]
        self.assertIn("#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)", call)
        self.assertIn("&combined_budget_presentation", call)
        self.assertIn("renderer_combined_session != nullptr", call)
        self.assertIn(
            "renderer_runtime_ownership.legacy_frame_presentation_enabled",
            call,
        )
        # The kernel refuses a non-presenting gated run outright.
        self.assertIn("FAIL_NOT_PRESENTING", self.header)
        self.assertIn("presents_frames", self.header)
        self.assertIn("if (!context_.presents_frames)", self.source)

    def test_combined_receipt_uses_the_visible_ogre_next_surface(self) -> None:
        self.assertIn(
            "renderer_combined_presenter.CurrentSurface()", self.main
        )
        self.assertIn(
            "renderer_combined_presenter.InitialFrontendRequest()", self.main
        )
        self.assertIn(
            "The combined process measures the sole visible Ogre-Next",
            self.main,
        )
        self.assertIn(
            "if (presentation_surface != nullptr)", self.main
        )

    def test_startup_and_shutdown_are_fail_closed(self) -> None:
        self.assertIn("bool frame_budget_refused = false;", self.main)
        self.assertIn(
            "application_exit_code = kFrameTimeBudgetFailureExitCode;",
            self.main,
        )
        # A refused contract must stop the run rather than leave it unmeasured.
        refusal = self.main.index(
            '[RoR|Perf] Shutting down: the frame budget was refused')
        self.assertIn(
            "MSG_APP_SHUTDOWN_REQUESTED",
            self.main[refusal : refusal + 320],
        )
        # A gated run without a receipt path cannot start.
        self.assertIn(
            "A gated run requires gfx_frame_budget_receipt_path", self.main)
        # An existing receipt is never reused or overwritten.
        self.assertIn("Refusing an existing receipt path", self.main)
        self.assertIn("FrameTimeBudgetWriteResult::EXISTS", self.source)
        self.assertIn("O_CREAT | O_EXCL", self.source)
        self.assertIn("CREATE_NEW", self.source)

    def test_forward_native_showcase_records_presented_frames(self) -> None:
        loop = self.main[
            self.main.index("const auto record_frame_budget")
            : self.main.index("// In combined mode a poll", self.main.index(
                "const auto record_frame_budget"))
        ]
        self.assertIn("renderer_combined_native_visual_showcase", loop)
        self.assertIn("kNativeVisualShowcasePackageId", loop)
        self.assertIn("kNativeVisualShowcaseA1PackageId", loop)
        self.assertIn("frame_budget_session->RecordFrame", loop)

    def test_exit_code_does_not_collide_with_the_renderer_child(self) -> None:
        child = (
            ROOT / "source/main/system/RendererOgreNextChild.h"
        ).read_text()
        self.assertIn("kFrameTimeBudgetFailureExitCode = 75", self.header)
        self.assertIn("PrePeerReadyFailureExitCode = 73", child)
        self.assertIn("PostPeerReadyFailureExitCode = 74", child)

    def test_finalize_precedes_renderer_teardown(self) -> None:
        end_of_loop = self.main.index("} // End of main rendering/input loop")
        finalize = self.main.index(
            "FinalizeFrameTimeBudgetSession(*frame_budget_session)")
        self.assertLess(end_of_loop, finalize)
        for teardown in (
            "CloseCombinedRendererSession(*renderer_combined_session)",
            "renderer_bridge_product_session->Shutdown()",
        ):
            self.assertLess(finalize, self.main.index(teardown, finalize))

    def test_kernel_is_renderer_and_configuration_free(self) -> None:
        # Prose may name OGRE; code may not depend on it. Compare the
        # comment-free text so a doc reference cannot mask a real include.
        for label, text in (
            ("header", strip_comments(self.header)),
            ("source", strip_comments(self.source)),
        ):
            for forbidden in ("Ogre", "App::", "CVar", "GUI", "Application.h"):
                self.assertNotIn(forbidden, text, f"{label}: {forbidden}")
            # The only permitted quoted include is the module's own header;
            # everything else must be a standard or platform header.
            quoted = set(re.findall(r'#include\s+"([^"]+)"', text))
            self.assertLessEqual(
                quoted, {"FrameTimeBudget.h"},
                f"{label} includes a project header: {sorted(quoted)}",
            )

    def test_kernel_bounds_are_explicit(self) -> None:
        self.assertIn("kFrameTimeBudgetBinWidthNs = 15625U", self.header)
        self.assertIn("kFrameTimeBudgetBinCount = 8192U", self.header)
        self.assertIn("kFrameTimeBudgetMaximumFrames = 2000000U", self.header)
        # The recorder allocates its histogram once, at construction.
        self.assertIn("bins_(kFrameTimeBudgetTotalBins, 0U)", self.source)
        self.assertNotIn("bins_.resize", self.source)
        self.assertNotIn("bins_.push_back", self.source)

    def test_combined_receipt_attributes_renderer_owned_native_phases(
        self,
    ) -> None:
        for phase in (
            "NATIVE_VALIDATION",
            "NATIVE_FRAME_PREPARE",
            "NATIVE_LIGHTS",
            "NATIVE_INSTANCES",
            "NATIVE_PREPARE",
            "NATIVE_RENDER",
            "NATIVE_POST_RENDER",
            "NATIVE_CLEANUP",
            "NATIVE_PUBLICATION",
        ):
            self.assertIn(f"FrameTimeBudgetPhase::{phase}", self.main)
            self.assertIn(f"FrameTimeBudgetPhase::{phase}", self.source)

        self.assertIn("RecordPhaseMicroseconds", self.main)
        self.assertIn(
            "last_native_renderer_frame_id ==\n"
            "                        frontend_frame_id",
            self.main,
        )
        self.assertIn("last_native_pass_metrics_exact", self.main)
        self.assertIn(
            "index <= static_cast<std::size_t>(\n"
            "                    FrameTimeBudgetPhase::SCENE_DISPATCH)",
            self.source,
        )

    def test_native_draw_receipt_uses_the_behavioral_pass_state_machine(
        self,
    ) -> None:
        frontend = strip_comments(
            (ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.cpp")
            .read_text()
        )
        listener_start = frontend.index(
            "class NativeRenderPassMetricsListener final"
        )
        listener_end = frontend.index("namespace {", listener_start)
        listener = frontend[listener_start:listener_end]
        self.assertIn('#include "OgreNextNativeRenderPassMetrics.h"', frontend)
        for delegation in (
            "state_.BeginFrame(",
            "state_.ScenePre(",
            "state_.SceneAfterShadowMaps(",
            "state_.ScenePost(",
            "state_.WorkspacePost(",
            "state_.EndFrame(total, output)",
            "OgreNextNativeRenderPassMetricsState state_;",
            "reinterpret_cast<std::uintptr_t>(pass)",
        ):
            self.assertIn(delegation, listener)
        for duplicated_model in (
            "expected_scene_pass_count_",
            "aggregate_shadow_maps_",
            "TryAddNativeRenderMetrics",
            "TrySubtractNativeRenderMetrics",
        ):
            self.assertNotIn(duplicated_model, listener)

        expected_ids = (
            "kOgreNextHdrBaseScenePassIdentifier",
            "kOgreNextHdrSunFullScenePassIdentifier",
            "kOgreNextHdrRasterLitScenePassIdentifier",
        )
        offsets = [listener.index(name) for name in expected_ids]
        self.assertEqual(offsets, sorted(offsets))
        self.assertIn("kOgreNextHdrSingleScenePassIdentifier", listener)

        begin = frontend.index(
            "native_render_pass_metrics_listener.BeginFrame("
        )
        begin_call = frontend[begin : begin + 260]
        self.assertIn(
            "persistent_hdr && !impl_->SingleSceneHdrPssmEnabled()",
            begin_call,
        )

        tests = (ROOT / "tests/CMakeLists.txt").read_text()
        self.assertIn("ror_ogre_next_native_render_pass_metrics_tests", tests)
        self.assertIn("OgreNextNativeRenderPassMetricsTests.cpp", tests)
        self.assertIn("OgreNextNativeRenderPassMetrics.cpp", tests)

        for provider in (
            ROOT / "cmake/ogre_next_embedded/CMakeLists.txt",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
        ):
            self.assertIn(
                "OgreNextNativeRenderPassMetrics.cpp",
                provider.read_text(),
                str(provider),
            )

    def test_build_graph_compiles_the_exact_production_kernel(self) -> None:
        game = (ROOT / "source/main/CMakeLists.txt").read_text()
        self.assertIn("system/FrameTimeBudget.{h,cpp}", game)

        tests = (ROOT / "tests/CMakeLists.txt").read_text()
        self.assertIn(
            '"${ROR_REPOSITORY_ROOT}/source/main/system/FrameTimeBudget.cpp"',
            tests,
        )
        for name in (
            "frame_time_budget",
            "frame_time_budget_runtime_contract",
            "playable_performance_scene_tool",
            "playable_performance_scene_tool_optimized",
        ):
            self.assertIn(f"NAME {name}\n", tests, name)


if __name__ == "__main__":
    unittest.main()
