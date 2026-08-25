#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the playable performance scene runner's pure decision logic."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TEST_ROOT = Path(tempfile.gettempdir()).resolve() / "ror-perf"

_SPEC = importlib.util.spec_from_file_location(
    "run_playable_performance_scene",
    ROOT / "tools/run_playable_performance_scene.py",
)
runner = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
_SPEC.loader.exec_module(runner)


def make_request(**overrides):
    parameters = dict(
        scenario_id="playable-cityworld-alexis",
        terrain="CityWorld.terrn2",
        actor="AlexisSaber.truck",
        width=1920,
        height=1080,
        warmup_frames=120,
        minimum_frames=600,
        requested_frames=1800,
        sustained_ms=16.6667,
        percentile=95,
        percentile_ms=18.3,
        receipt_path=TEST_ROOT / "frame-time-receipt.json",
        mode="gate",
        graphics_preset="high",
        target_platform="darwin",
    )
    parameters.update(overrides)
    return runner.BudgetRequest(**parameters)


def make_receipt(request, **overrides):
    document = {
        "format": "ror-frame-time-budget-v1",
        "mode": request.mode,
        "verdict": "pass" if request.mode == "gate" else "advisory",
        "passed": request.mode == "gate",
        "scenario_id": request.scenario_id,
        "terrain": request.terrain,
        "actor": request.actor,
        "renderer": "ogre14",
        "width": request.width,
        "height": request.height,
        "fullscreen": False,
        "vsync": False,
        "presents_frames": True,
        "fps_limit": 0,
        "warmup_frames_requested": request.warmup_frames,
        "minimum_frames": request.minimum_frames,
        "requested_frames": request.requested_frames,
        "sustained_budget_ms": request.sustained_ms,
        "percentile": request.percentile,
        "percentile_budget_ms": request.percentile_ms,
        "observed_frames": request.warmup_frames + request.requested_frames,
        "warmup_frames": request.warmup_frames,
        "accepted_frames": request.requested_frames,
        "rejected_frames": 0,
        "saturated_frames": 0,
        "over_budget_frames": 12,
        "minimum_ms": 6.5,
        "mean_ms": 9.9,
        "maximum_ms": 24.0,
        "p50_ms": 9.5,
        "p95_ms": 13.0,
        "p99_ms": 17.0,
        "ranked_ms": 13.0,
        "mean_fps": 101.01,
        "bin_width_ns": 15625,
        "bin_count": 8192,
    }
    document.update(overrides)
    return document


def make_combined_receipt(request, **overrides):
    document = make_receipt(
        request,
        renderer="ogre-next-combined",
        requires_native_scene_draw_metrics=True,
        native_scene_draw_p99_limit=2500,
        native_scene_draw_exact_samples=request.requested_frames,
        native_scene_draw_rejected_samples=0,
        native_scene_draw_p99=934,
        native_scene_draw_maximum=1158,
        phase_scene_dispatch_samples=request.requested_frames,
        phase_scene_dispatch_total_ms=9000.0,
    )
    for phase in runner.NATIVE_PHASE_NAMES:
        prefix = f"phase_{phase}_"
        document[prefix + "samples"] = request.requested_frames
        document[prefix + "total_ms"] = 450.0
        document[prefix + "mean_ms"] = 0.25
        document[prefix + "max_ms"] = 0.5
        document[prefix + "share"] = 0.025
    document.update(overrides)
    return document


class BudgetRequestTests(unittest.TestCase):
    def test_valid_request_round_trips(self) -> None:
        request = make_request()
        record = request.as_record()
        self.assertEqual(record["width"], 1920)
        self.assertEqual(record["percentile_budget_ms"], 18.3)
        self.assertEqual(record["mode"], "gate")

    def test_invalid_requests_are_refused(self) -> None:
        for overrides in (
            {"scenario_id": ""},
            {"width": 0},
            {"height": -1080},
            {"minimum_frames": 0},
            {"requested_frames": 100},  # below the minimum
            {"percentile": 0},
            {"percentile": 101},
            {"sustained_ms": 0.0},
            {"percentile_ms": -1.0},
            {"percentile_ms": 8.0},  # ceiling below the sustained budget
            {"mode": "off"},
            {"receipt_path": Path("relative/receipt.json")},
            {"target_platform": "haiku"},
            {"graphics_preset": "ultra"},
        ):
            with self.subTest(overrides=overrides):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    make_request(**overrides)


class ReceiptValidationTests(unittest.TestCase):
    def test_matching_receipt_is_accepted(self) -> None:
        request = make_request()
        receipt = runner.validate_receipt(make_receipt(request), request)
        self.assertEqual(receipt["accepted_frames"], 1800)

    def test_failed_verdict_is_refused_with_its_numbers(self) -> None:
        request = make_request()
        document = make_receipt(
            request,
            verdict="fail-percentile",
            passed=False,
            p95_ms=21.0,
            p99_ms=23.0,
            ranked_ms=21.0,
        )
        with self.assertRaises(runner.PerformanceSceneFailure) as caught:
            runner.validate_receipt(document, request)
        self.assertIn("fail-percentile", str(caught.exception))
        self.assertIn("21.0", str(caught.exception))

    def test_measure_mode_requires_an_advisory_verdict(self) -> None:
        request = make_request(mode="measure")
        runner.validate_receipt(make_receipt(request), request)
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.validate_receipt(
                make_receipt(request, verdict="pass"), request)

    def test_identity_drift_is_refused(self) -> None:
        request = make_request()
        for overrides in (
            {"format": "ror-frame-time-budget-v2"},
            {"scenario_id": "other-scenario"},
            {"terrain": "simple2.terrn2"},
            {"width": 1280},
            {"height": 720},
            {"mode": "measure"},
            {"percentile": 99},
            {"percentile_budget_ms": 25.0},
            {"sustained_budget_ms": 33.3},
            {"requested_frames": 900},
            {"minimum_frames": 10},
            {"warmup_frames_requested": 0},
        ):
            with self.subTest(overrides=overrides):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.validate_receipt(
                        make_receipt(request, **overrides), request)

    def test_synthetic_cadence_is_refused(self) -> None:
        request = make_request()
        for overrides in (
            {"fps_limit": 60},
            {"vsync": True},
            {"fullscreen": True},
            {"presents_frames": False},
        ):
            with self.subTest(overrides=overrides):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.validate_receipt(
                        make_receipt(request, **overrides), request)

    def test_incoherent_statistics_are_refused(self) -> None:
        request = make_request()
        for overrides in (
            {"rejected_frames": 1},
            {"accepted_frames": 599},
            {"warmup_frames": 60},
            {"observed_frames": 100},
            {"p95_ms": 4.0},  # below p50
            {"p99_ms": 1.0},  # below p95
            {"maximum_ms": 5.0},  # below p99 by more than one bin
            {"bin_width_ns": 0},
            {"mean_ms": "fast"},
            {"mean_ms": -1.0},
            {"accepted_frames": True},
        ):
            with self.subTest(overrides=overrides):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.validate_receipt(
                        make_receipt(request, **overrides), request)

    def test_upper_edge_within_one_bin_is_accepted(self) -> None:
        # A ranked value may exceed the exact maximum by at most one bin width.
        request = make_request()
        document = make_receipt(
            request, p99_ms=24.0 + (15625 / 1e6), maximum_ms=24.0)
        runner.validate_receipt(document, request)
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.validate_receipt(
                make_receipt(
                    request, p99_ms=24.0 + (2 * 15625 / 1e6), maximum_ms=24.0),
                request,
            )

    def test_combined_receipt_requires_exact_native_draw_distribution(
        self,
    ) -> None:
        request = make_request()
        receipt = runner.validate_receipt(
            make_combined_receipt(request), request)
        self.assertEqual(receipt["native_scene_draw_p99"], 934)

        malformed = (
            {"requires_native_scene_draw_metrics": False},
            {"native_scene_draw_exact_samples": request.requested_frames - 1},
            {"native_scene_draw_rejected_samples": 1},
            {"native_scene_draw_p99": 0},
            {"native_scene_draw_p99": 1200, "native_scene_draw_maximum": 1100},
            {"native_scene_draw_p99_limit": 2499},
        )
        for overrides in malformed:
            with self.subTest(overrides=overrides):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.validate_receipt(
                        make_combined_receipt(request, **overrides), request)

    def test_combined_gate_rejects_native_draw_p99_over_roadmap_limit(
        self,
    ) -> None:
        request = make_request()
        with self.assertRaises(runner.PerformanceSceneFailure) as caught:
            runner.validate_receipt(
                make_combined_receipt(
                    request,
                    native_scene_draw_p99=2501,
                    native_scene_draw_maximum=3000,
                ),
                request,
            )
        self.assertIn("draw budget failed", str(caught.exception))

    def test_combined_receipt_requires_exact_native_phase_coverage(
        self,
    ) -> None:
        request = make_request()
        runner.validate_receipt(make_combined_receipt(request), request)

        for phase in runner.NATIVE_PHASE_NAMES:
            key = f"phase_{phase}_samples"
            with self.subTest(phase=phase):
                with self.assertRaisesRegex(
                    runner.PerformanceSceneFailure,
                    "covers",
                ):
                    runner.validate_receipt(
                        make_combined_receipt(request, **{key: 0}), request
                    )

        with self.assertRaisesRegex(
            runner.PerformanceSceneFailure,
            "native phase total exceeds scene dispatch",
        ):
            runner.validate_receipt(
                make_combined_receipt(
                    request,
                    phase_native_render_total_ms=6000.0,
                ),
                request,
            )


class PresentationPacingTests(unittest.TestCase):
    def test_sixty_hertz_pacing_is_reported(self) -> None:
        request = make_request()
        pacing = runner.detect_presentation_pacing(
            make_receipt(request, p50_ms=16.33, p95_ms=17.81, p99_ms=18.41))
        self.assertIsNotNone(pacing)
        self.assertEqual(pacing["suspected_hz"], 60.0)
        self.assertEqual(pacing["median_ms"], 16.33)

    def test_every_listed_refresh_rate_is_recognized(self) -> None:
        request = make_request()
        for interval in runner.REFRESH_INTERVALS_MS:
            with self.subTest(interval=interval):
                pacing = runner.detect_presentation_pacing(
                    make_receipt(
                        request,
                        p50_ms=interval,
                        p95_ms=interval + 1.0,
                        p99_ms=interval + 1.5,
                    )
                )
                self.assertIsNotNone(pacing)
                self.assertAlmostEqual(
                    pacing["suspected_hz"], 1000.0 / interval, places=2)

    def test_unpaced_and_loose_distributions_are_not_reported(self) -> None:
        request = make_request()
        # A median away from any refresh interval.
        self.assertIsNone(
            runner.detect_presentation_pacing(
                make_receipt(request, p50_ms=4.72, p95_ms=6.0, p99_ms=7.0)))
        # A median on 60 Hz but with a wide tail is renderer-bound variance.
        self.assertIsNone(
            runner.detect_presentation_pacing(
                make_receipt(request, p50_ms=16.67, p95_ms=30.0, p99_ms=40.0)))


class LogScanTests(unittest.TestCase):
    HEADER = (
        "RenderSystem Name: OpenGL 3+ Rendering Subsystem\n"
        "Device Name: Apple M5\n"
        "GPU Vendor: apple\n"
    )

    def test_clean_log_yields_renderer_identity(self) -> None:
        identity = runner.scan_runtime_log(self.HEADER)
        self.assertEqual(identity["device"], "Apple M5")
        self.assertEqual(identity["vendor"], "apple")
        self.assertEqual(identity["content_diagnostics"], {})

    def test_missing_render_system_is_refused(self) -> None:
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.scan_runtime_log("Device Name: Apple M5\n")

    def test_fatal_markers_are_always_refused(self) -> None:
        for marker in runner.FATAL_LOG_MARKERS:
            with self.subTest(marker=marker):
                with self.assertRaises(runner.PerformanceSceneFailure) as bad:
                    runner.scan_runtime_log(self.HEADER + f"{marker} thing\n")
                self.assertIn(marker, str(bad.exception))

    def test_capture_rejections_are_diagnostic_not_lod_lineage(self) -> None:
        native = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7398 casters=200 "
            "lod_items=120 lod_reduced=96 lod_max=3 lod_level_sum=211 "
            "triangles_base=7130751 triangles_selected=2419000 "
            "lod_exact=true pssm=true reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=1\n"
        )
        for text in (
            self.HEADER + runner.CAPTURE_REJECTED_MARKER + "\n",
            self.HEADER + native + runner.CAPTURE_REJECTED_MARKER + "\n",
            self.HEADER + runner.CAPTURE_REJECTED_MARKER + "\n" + native,
        ):
            with self.subTest(text=text):
                identity = runner.scan_runtime_log(text)
                self.assertEqual(identity["startup_capture_rejections"], 1)

    def test_content_markers_are_counted_and_optionally_gated(self) -> None:
        for marker in runner.CONTENT_LOG_MARKERS:
            with self.subTest(marker=marker):
                text = self.HEADER + f"{marker} thing\n" * 3
                identity = runner.scan_runtime_log(text)
                self.assertEqual(identity["content_diagnostics"][marker], 3)
                with self.assertRaises(runner.PerformanceSceneFailure) as bad:
                    runner.scan_runtime_log(text, require_clean_content=True)
                self.assertIn(marker, str(bad.exception))
                self.assertIn("x3", str(bad.exception))

    def test_clean_content_requirement_accepts_a_clean_log(self) -> None:
        identity = runner.scan_runtime_log(
            self.HEADER, require_clean_content=True)
        self.assertEqual(identity["content_diagnostics"], {})


class ConfigurationTests(unittest.TestCase):
    def test_runtime_config_pins_the_named_preset(self) -> None:
        request = make_request()
        config = runner.build_ror_config(request)
        self.assertIn("gfx_fps_limit=0", config)
        self.assertIn("audio_master_volume=0", config)
        # The budget is non-archived; a config file must never arm it.
        self.assertNotIn("gfx_frame_budget", config)
        # Enum settings are written as the display strings the parser accepts,
        # never as their integer values, which it would silently reinterpret.
        self.assertIn("gfx_shadow_type=Parallel-split Shadow Maps", config)
        self.assertNotIn("gfx_shadow_type=1", config)
        self.assertIn("gfx_texture_filter=Anisotropic (best looking)", config)
        self.assertIn("gfx_vegetation_mode=Full (best looking, slower)", config)
        self.assertIn("gfx_shadow_quality=3", config)
        self.assertIn("gfx_auto_lod=true", config)
        for name, (value, _) in runner.GRAPHICS_PRESETS["high"].items():
            self.assertIn(f"{name}={value}", config)

        low = runner.build_ror_config(make_request(graphics_preset="low"))
        self.assertIn("gfx_shadow_type=No shadows (fastest)", low)
        self.assertIn("gfx_vegetation_mode=None (fastest)", low)

    def test_preset_identity_reaches_the_run_record(self) -> None:
        record = make_request().as_record()
        self.assertEqual(record["graphics_preset"], "high")
        self.assertEqual(record["graphics_settings"]["gfx_shadow_type"], "1")
        # Presets must name the same settings so two runs stay comparable.
        self.assertEqual(
            set(runner.GRAPHICS_PRESETS["high"]),
            set(runner.GRAPHICS_PRESETS["low"]),
        )

    def test_effective_preset_is_read_back_from_the_runtime(self) -> None:
        preset = runner.GRAPHICS_PRESETS["high"]
        statement = "12:00:00: [RoR|Perf] Graphics: " + " ".join(
            f"{name}={expected}" for name, (_, expected) in preset.items()
        )
        observed = runner.verify_graphics_preset(statement + "\n", "high")
        self.assertEqual(observed["gfx_shadow_type"], "1")
        self.assertEqual(observed["gfx_envmap_enabled"], "1")

    def test_a_silently_reinterpreted_setting_is_refused(self) -> None:
        # The exact regression this check exists for: the config asked for
        # PSSM and the parser turned shadows off.
        preset = runner.GRAPHICS_PRESETS["high"]
        statement = "12:00:00: [RoR|Perf] Graphics: " + " ".join(
            f"{name}={'0' if name == 'gfx_shadow_type' else expected}"
            for name, (_, expected) in preset.items()
        )
        with self.assertRaises(runner.PerformanceSceneFailure) as caught:
            runner.verify_graphics_preset(statement + "\n", "high")
        self.assertIn("gfx_shadow_type", str(caught.exception))
        self.assertIn("did not take effect", str(caught.exception))

    def test_combined_runtime_verifies_visible_renderer_not_hidden_rtts(
        self,
    ) -> None:
        preset = runner.GRAPHICS_PRESETS["high"]
        effective = {
            name: expected for name, (_, expected) in preset.items()
        }
        effective.update(runner.COMBINED_PRODUCER_OVERRIDES)
        statement = "[RoR|Perf] Graphics: " + " ".join(
            f"{name}={effective[name]}" for name in preset
        )
        observed = runner.verify_graphics_preset(
            statement + "\n", "high", combined_runtime=True
        )
        self.assertEqual(observed["gfx_shadow_type"], "0")
        self.assertEqual(observed["gfx_envmap_enabled"], "0")
        self.assertEqual(observed["gfx_auto_lod"], "1")

        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.verify_graphics_preset(statement + "\n", "high")

    def test_native_combined_lod_receipt_proves_visible_reduction(self) -> None:
        statement = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7406 casters=200 "
            "lod_items=1378 lod_reduced=1005 lod_max=4 lod_level_sum=2494 "
            "triangles_base=7135111 triangles_selected=5773448 "
            "lod_exact=true pssm=true reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=3067\n"
        )
        receipt = runner.verify_combined_native_distance_lod(statement)
        self.assertEqual(receipt["lod_items"], 1378)
        self.assertEqual(receipt["lod_reduced"], 1005)
        self.assertTrue(receipt["reduced_this_frame"])
        self.assertLess(
            receipt["triangles_selected"], receipt["triangles_base"]
        )

    def test_scene_worker_receipt_binds_native_runtime_count(self) -> None:
        default = (
            "[RoR|OgreNext|SceneWorkers] requested=4 native=4 hardware=12 "
            "override_present=false override_valid=false\n"
        )
        receipt = runner.verify_ogrenext_scene_workers(default)
        self.assertEqual(receipt["native"], 4)
        self.assertFalse(receipt["override_present"])

        overridden = (
            "[RoR|OgreNext|SceneWorkers] requested=6 native=6 hardware=12 "
            "override_present=true override_valid=true\n"
        )
        receipt = runner.verify_ogrenext_scene_workers(overridden)
        self.assertEqual(receipt["native"], 6)
        self.assertTrue(receipt["override_valid"])

        for hardware, expected in ((0, 1), (1, 1), (2, 2), (4, 4), (64, 4)):
            with self.subTest(hardware=hardware):
                statement = (
                    "[RoR|OgreNext|SceneWorkers] "
                    f"requested={expected} native={expected} "
                    f"hardware={hardware} "
                    "override_present=false override_valid=false\n"
                )
                receipt = runner.verify_ogrenext_scene_workers(statement)
                self.assertEqual(receipt["requested"], expected)
                self.assertEqual(receipt["native"], expected)

    def test_scene_worker_receipt_rejects_unproved_or_invalid_state(self) -> None:
        valid = (
            "[RoR|OgreNext|SceneWorkers] requested=4 native=4 hardware=12 "
            "override_present=false override_valid=false\n"
        )
        malformed = (
            "",
            valid + valid,
            valid.replace("native=4", "native=3"),
            valid.replace("requested=4", "requested=9").replace(
                "native=4", "native=9"
            ),
            valid.replace("requested=4", "requested=3").replace(
                "native=4", "native=3"
            ),
            valid.replace("override_present=false", "override_present=true"),
        )
        for statement in malformed:
            with self.subTest(statement=statement):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.verify_ogrenext_scene_workers(statement)

    def test_native_combined_lod_requires_recovery_after_rejection(self) -> None:
        native = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7406 casters=200 "
            "lod_items=1378 lod_reduced=1005 lod_max=4 "
            "lod_level_sum=2494 triangles_base=7135111 "
            "triangles_selected=5773448 lod_exact=true pssm=true "
            "reflection_initialized=true native_scene_lighting=true "
            "gpu_only=true no_ogre14_lighting=true completed_frames=1\n"
        )
        recovered = runner.CAPTURE_REJECTED_MARKER + "\n" + native
        receipt = runner.verify_combined_native_distance_lod(recovered)
        self.assertEqual(receipt["completed_frames"], 1)

        unrecovered = native + runner.CAPTURE_REJECTED_MARKER + "\n"
        with self.assertRaises(runner.PerformanceSceneFailure) as caught:
            runner.verify_combined_native_distance_lod(unrecovered)
        self.assertIn("was not recovered", str(caught.exception))

    def test_native_combined_lod_receipt_accepts_exact_base_selection(
        self,
    ) -> None:
        statement = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7398 casters=200 "
            "lod_items=1378 lod_reduced=0 lod_max=0 lod_level_sum=0 "
            "triangles_base=7130751 triangles_selected=7130751 "
            "lod_exact=true pssm=true reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=1800\n"
        )
        receipt = runner.verify_combined_native_distance_lod(statement)
        self.assertFalse(receipt["reduced_this_frame"])
        self.assertEqual(
            receipt["triangles_selected"], receipt["triangles_base"]
        )

    def test_native_combined_lod_receipt_rejects_missing_ladder(self) -> None:
        statement = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7398 casters=200 "
            "lod_items=0 lod_reduced=0 lod_max=0 lod_level_sum=0 "
            "triangles_base=7130751 triangles_selected=7130751 "
            "lod_exact=true pssm=true reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=1800\n"
        )
        with self.assertRaises(runner.PerformanceSceneFailure) as caught:
            runner.verify_combined_native_distance_lod(statement)
        self.assertIn("no native distance-LOD ladders", str(caught.exception))

    def test_native_combined_lod_receipt_rejects_mismatched_selection(
        self,
    ) -> None:
        common = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=7398 casters=200 "
            "lod_items=1378 "
            "lod_exact=true pssm=true reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=1800 "
        )
        malformed = (
            common + "lod_reduced=0 lod_max=0 lod_level_sum=0 "
            "triangles_base=7130751 triangles_selected=7000000\n",
            common + "lod_reduced=1 lod_max=1 lod_level_sum=1 "
            "triangles_base=7130751 triangles_selected=7130751\n",
            common + "lod_reduced=1 lod_max=1 lod_level_sum=1 "
            "triangles_base=7130751 triangles_selected=7130752\n",
            common + "lod_reduced=-1 lod_max=0 lod_level_sum=0 "
            "triangles_base=7130751 triangles_selected=7130751\n",
        )
        for statement in malformed:
            with self.subTest(statement=statement):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.verify_combined_native_distance_lod(statement)

    def test_native_combined_lod_allows_empty_shadow_caster_inventory(
        self,
    ) -> None:
        statement = (
            "[RoR|RendererCombined|NativeLighting] "
            "schema_version=6 available=true pbs=256 casters=0 "
            "lod_items=256 lod_reduced=244 lod_max=4 lod_level_sum=688 "
            "triangles_base=2228224 triangles_selected=194560 "
            "lod_exact=true pssm=false reflection_initialized=true "
            "native_scene_lighting=true gpu_only=true "
            "no_ogre14_lighting=true completed_frames=300\n"
        )
        receipt = runner.verify_combined_native_distance_lod(statement)
        self.assertFalse(receipt["pssm"])
        self.assertEqual(receipt["casters"], 0)

    def test_scene_source_timing_reads_real_separate_material_apply(self) -> None:
        statement = (
            "[RoR|SceneSource] captures=1 mean_ns terrain=1675902 "
            "static=30859736 dynamic=2943982 dynamic_setup=111 "
            "dynamic_deformable=222 dynamic_rigid=333 "
            "dynamic_validation=444 dynamic_inventory=555 retained=3141 merge=579941 "
            "union=40911 particles=81234 material_apply=6278541 "
            "other=526984 material_index=671204 material_plan=148226 "
            "material_authority=3547001 material_owners=912104 "
            "material_finalize=602817 material_authority_plan_cache_hit=1\n"
        )
        timing = runner.read_scene_source_timing(statement)
        self.assertEqual(timing["captures"], 1)
        self.assertEqual(timing["mean_ns"]["dynamic_inventory"], 555)
        self.assertEqual(timing["mean_ns"]["particles"], 81234)
        self.assertEqual(timing["mean_ns"]["material_apply"], 6278541)
        self.assertEqual(timing["mean_ns"]["material_index"], 671204)
        self.assertEqual(timing["mean_ns"]["material_authority"], 3547001)

    def test_scene_source_timing_requires_new_runtime_receipt(self) -> None:
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.read_scene_source_timing(
                "[RoR|SceneSource] captures=300 mean_ns "
                "terrain=1 static=2 dynamic=3 retained=4 merge=5 union=6 "
                "particles=7 other=8\n"
            )

    def test_combined_presentation_ownership_rejects_legacy_visibility(self) -> None:
        receipts = (
            runner.OGRE_NEXT_PRESENTATION_OWNER_RECEIPT
            + " backend=ogre-next-metal"
            + "\n"
            + runner.OGRE14_HIDDEN_RESOURCE_HOST_RECEIPT
            + "\n"
        )
        ownership = runner.verify_combined_presentation_ownership(
            receipts, "darwin"
        )
        self.assertEqual(ownership["presentation_owner"], "ogre-next")
        self.assertEqual(
            ownership["visible_render_system"], "ogre-next-metal"
        )
        self.assertFalse(ownership["legacy_visible_fallback"])
        self.assertFalse(ownership["resource_host_visible"])

        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.verify_combined_presentation_ownership(
                receipts.replace("visible_window=false", "visible_window=true"),
                "darwin",
            )
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.verify_combined_presentation_ownership(
                receipts + runner.OGRE_NEXT_PRESENTATION_OWNER_RECEIPT,
                "darwin",
            )
        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.verify_combined_presentation_ownership(
                receipts, "linux"
            )

    def test_actor_control_receipt_binds_press_release_and_native_frames(
        self,
    ) -> None:
        statement = (
            "[RoR|RendererCombined|ActorControl] "
            "schema=ror.ogre_next_actor_control_receipt.v1 "
            "qualified=true input_source=visible_window_sdl "
            "presenter=ogre-next legacy_visible_fallback=false "
            "control=truck_accelerate actor_instance_id=0 key=200 "
            "press_transition=1 press_event_id=8 press_issued=1 "
            "press_resolved=1 press_frame_id=21 press_dynamic_updates=7 "
            "press_scene_draws=30 release_transition=2 "
            "release_event_id=8 release_issued=0 release_resolved=0 "
            "release_frame_id=25 release_dynamic_updates=7 "
            "release_scene_draws=30\n"
        )
        receipt = runner.verify_combined_actor_control(statement)
        self.assertTrue(receipt["qualified"])
        self.assertFalse(receipt["legacy_visible_fallback"])
        self.assertEqual(receipt["actor_instance_id"], 0)
        self.assertEqual(receipt["key"], 200)
        self.assertLess(receipt["press_frame_id"], receipt["release_frame_id"])

        for malformed in (
            statement.replace("release_frame_id=25", "release_frame_id=21"),
            statement.replace("release_event_id=8", "release_event_id=7"),
            statement.replace("press_resolved=1", "press_resolved=0"),
            statement.replace("legacy_visible_fallback=false", "legacy_visible_fallback=true"),
            statement + statement,
        ):
            with self.subTest(malformed=malformed):
                with self.assertRaises(runner.PerformanceSceneFailure):
                    runner.verify_combined_actor_control(malformed)

    def test_a_missing_or_repeated_statement_is_refused(self) -> None:
        preset = runner.GRAPHICS_PRESETS["high"]
        statement = "[RoR|Perf] Graphics: " + " ".join(
            f"{name}={expected}" for name, (_, expected) in preset.items()
        )
        with self.assertRaises(runner.PerformanceSceneFailure) as absent:
            runner.verify_graphics_preset("no statement here\n", "high")
        self.assertIn("does not state", str(absent.exception))

        with self.assertRaises(runner.PerformanceSceneFailure) as repeated:
            runner.verify_graphics_preset(
                statement + "\n" + statement + "\n", "high")
        self.assertIn("2 times", str(repeated.exception))

        with self.assertRaises(runner.PerformanceSceneFailure) as partial:
            runner.verify_graphics_preset(
                "[RoR|Perf] Graphics: gfx_shadow_type=1\n", "high")
        self.assertIn("did not state", str(partial.exception))

        with self.assertRaises(runner.PerformanceSceneFailure):
            runner.verify_graphics_preset(statement + "\n", "ultra")

    def test_command_selects_the_pinned_scene(self) -> None:
        command = runner.build_command(Path("/bin/RoR"), make_request())
        self.assertIn("-map", command)
        self.assertIn("CityWorld.terrn2", command)
        self.assertIn("-truck", command)
        self.assertIn("AlexisSaber.truck", command)
        self.assertIn("-enter", command)
        self.assertIn("-checkcache", command)
        self.assertNotIn("--native-visual-showcase", command)
        self.assertNotIn("--native-visual-showcase-a0", command)

        without_actor = runner.build_command(
            Path("/bin/RoR"), make_request(actor=""))
        self.assertNotIn("-truck", without_actor)
        self.assertNotIn("-enter", without_actor)

    def test_renderer_only_showcases_cannot_satisfy_the_playable_gate(
        self,
    ) -> None:
        for option in sorted(runner.NON_PLAYABLE_COMBINED_OPTIONS):
            with self.subTest(option=option):
                with self.assertRaisesRegex(
                    runner.PerformanceSceneFailure,
                    "renderer-only showcase mode",
                ):
                    runner.build_command(
                        Path("/bin/RoR"),
                        make_request(),
                        launcher_arguments=(option,),
                    )

        ordinary = runner.build_command(
            Path("/bin/RoR"),
            make_request(),
            launcher_arguments=("--renderer-log-level=info",),
        )
        self.assertEqual(ordinary[1], "--renderer-log-level=info")

    def test_environment_isolates_the_profile(self) -> None:
        isolated_home = Path(tempfile.gettempdir()).resolve() / "ror-home"
        environment = runner.build_environment(isolated_home, make_request())
        self.assertEqual(
            environment["ROR_D0_SCENE_HOME"], str(isolated_home)
        )
        self.assertEqual(
            environment["ROR_D0_EXACT_WINDOW_EXTENT"], "1920x1080"
        )
        self.assertEqual(environment["ALSOFT_DRIVERS"], "null")
        self.assertNotIn("SNAP_USER_COMMON", environment)

    def test_actor_control_waits_for_a_completed_native_actor_scene(
        self,
    ) -> None:
        self.assertEqual(
            runner.ACTOR_CONTROL_SPAWN_MARKER,
            "== Spawning vehicle:",
        )
        self.assertEqual(
            runner.ACTOR_CONTROL_PRESENTED_SCENE_MARKER,
            "[RoR|RendererCombined|RetainedScene]",
        )
        source = (
            ROOT / "tools/run_playable_performance_scene.py"
        ).read_text(encoding="utf-8")
        spawn_wait = source.index(
            "ACTOR_CONTROL_SPAWN_MARKER,",
            source.index("process = subprocess.Popen"),
        )
        presented_wait = source.index(
            "ACTOR_CONTROL_PRESENTED_SCENE_MARKER,", spawn_wait
        )
        input_drive = source.index(
            "driver = drive_external_actor_control(", presented_wait
        )
        self.assertLess(spawn_wait, presented_wait)
        self.assertLess(presented_wait, input_drive)


class ReceiptIoTests(unittest.TestCase):
    def test_missing_and_malformed_receipts_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(runner.PerformanceSceneFailure):
                runner.load_receipt(root / "absent.json")

            empty = root / "empty.json"
            empty.write_text("", encoding="utf-8")
            with self.assertRaises(runner.PerformanceSceneFailure):
                runner.load_receipt(empty)

            broken = root / "broken.json"
            broken.write_text("{not json", encoding="utf-8")
            with self.assertRaises(runner.PerformanceSceneFailure):
                runner.load_receipt(broken)

            array = root / "array.json"
            array.write_text("[]", encoding="utf-8")
            with self.assertRaises(runner.PerformanceSceneFailure):
                runner.load_receipt(array)

            good = root / "good.json"
            good.write_text(
                json.dumps(make_receipt(make_request())), encoding="utf-8")
            self.assertEqual(
                runner.load_receipt(good)["format"],
                "ror-frame-time-budget-v1",
            )

    def test_existing_artifact_directory_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code = runner.main(
                [
                    "--executable", sys.executable,
                    "--artifact-dir", directory,
                    "--scenario-id", "x",
                    "--terrain", "simple2.terrn2",
                ]
            )
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
