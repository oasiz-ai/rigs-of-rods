#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_shadow_scene.py"

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_shadow_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld shadow comparison tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def child_report(shadow_mode: str) -> dict[str, object]:
    pssm = (
        None
        if shadow_mode == "none"
        else {
            "cascades": 3,
            "far": 350.0,
            "format": "PF_DEPTH16",
            "lambda": 0.97,
            "near": 0.5,
            "rtss_receiver": True,
            "sizes": [[3072, 3072], [2048, 2048], [2048, 2048]],
            "split_points": [0.5, 7.8, 45.2, 350.0],
        }
    )
    shadow_requested_digest = (
        "1" * 64 if shadow_mode == "none" else "2" * 64
    )
    shadow_effective_digest = (
        "3" * 64 if shadow_mode == "none" else "4" * 64
    )
    return {
        "additional_cityworld_compile_reports": [{"sha256": "additional"}],
        "cityworld_compile_report_sha256": "compile",
        "cityworld_compile_schema": "ror-cityworld-scene-compile-report-v1",
        "content_commit": "content",
        "corridor": {"seams": [{"position_gap_m": 0.0}]},
        "executable": "/app/RoR",
        "executable_sha256": "executable",
        "format": SCENE.GATEWAY_REPORT_FORMAT,
        "machine": "arm64",
        "metrics": {
            "distance_m": 137.569,
            "exit_x": 566.0,
            "exit_z": 570.5,
            "frame_max_ms": 40.0,
            "frame_mean_ms": 3.0 if shadow_mode == "none" else 4.0,
            "frame_p95_ms": 10.0 if shadow_mode == "none" else 12.0,
            "frame_samples": 1410,
            "max_path_error_m": 1.44,
            "max_y": 0.87,
            "min_y": 0.81,
            "physics_steps": 30580,
            "speed_mps": 10.0,
            "turn_degrees": 45.0,
        },
        "platform": "macOS",
        "rendering": {
            "configs": {
                "RoR.cfg": {
                    "effective": {
                        "artifact": "diagnostics/effective-RoR.cfg",
                        "sha256": shadow_effective_digest,
                        "size": 200,
                    },
                    "effective_shadow_normalized_sha256": "6" * 64,
                    "requested": {
                        "artifact": "diagnostics/requested-RoR.cfg",
                        "sha256": shadow_requested_digest,
                        "size": 100,
                    },
                    "requested_shadow_normalized_sha256": "5" * 64,
                },
                "ogre.cfg": {
                    "effective": {
                        "artifact": "diagnostics/effective-ogre.cfg",
                        "sha256": "a" * 64,
                        "size": 200,
                    },
                    "requested": {
                        "artifact": "diagnostics/requested-ogre.cfg",
                        "sha256": "a" * 64,
                        "size": 200,
                    },
                },
            },
            "device": {
                "api_version": "4.1",
                "device": "Test GPU",
                "render_system": "OpenGL 3+ Rendering Subsystem",
                "vendor": "test",
            },
            "height": SCENE.EXPECTED_HEIGHT,
            "pssm": pssm,
            "shadow_mode": shadow_mode,
            "shadow_quality": SCENE.SHADOW_QUALITY,
            "width": SCENE.EXPECTED_WIDTH,
        },
        "repository_commit": "commit",
        "rgb": {
            "height": SCENE.EXPECTED_HEIGHT,
            "sha256": shadow_mode + "-rgb",
            "width": SCENE.EXPECTED_WIDTH,
        },
        "runners": {"tools/run_cityworld_gateway_scene.py": {"sha256": "runner"}},
        "runtime_content": "/app/content",
        "runtime_pack": {"sha256": "pack", "size": 123},
        "vehicle_archive": {"sha256": "vehicle"},
    }


def localized_shadow_pixels() -> tuple[bytes, bytes]:
    width = SCENE.EXPECTED_WIDTH
    height = SCENE.EXPECTED_HEIGHT
    baseline = bytearray([160]) * (width * height * 3)
    pssm = bytearray(baseline)
    for y in range(SCENE.ROI_TOP, SCENE.ROI_BOTTOM):
        for x in range(SCENE.ROI_LEFT, SCENE.ROI_RIGHT):
            offset = (y * width + x) * 3
            pssm[offset : offset + 3] = bytes((140, 140, 140))
    return bytes(baseline), bytes(pssm)


class CityWorldShadowSceneTests(unittest.TestCase):
    def test_paired_reports_require_exact_identity_and_physics(self) -> None:
        baseline = child_report("none")
        pssm = child_report("pssm")
        SCENE.validate_child_report(baseline, "none")
        SCENE.validate_child_report(pssm, "pssm")
        physics = SCENE.compare_identity(baseline, pssm)
        self.assertEqual(physics["physics_steps"], 30580)

        changed_identity = copy.deepcopy(pssm)
        changed_identity["executable_sha256"] = "different"
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_identity(baseline, changed_identity)

        changed_physics = copy.deepcopy(pssm)
        changed_physics["metrics"]["exit_x"] = 566.001
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_identity(baseline, changed_physics)

        changed_camera = copy.deepcopy(pssm)
        changed_camera["rendering"]["width"] = 1920
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_child_report(changed_camera, "pssm")

        changed_config = copy.deepcopy(pssm)
        changed_config["rendering"]["configs"]["RoR.cfg"][
            "effective_shadow_normalized_sha256"
        ] = "b" * 64
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_identity(baseline, changed_config)

        changed_device = copy.deepcopy(pssm)
        changed_device["rendering"]["device"]["device"] = "Different GPU"
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_identity(baseline, changed_device)

    def test_performance_gate_bounds_absolute_time_and_overhead(self) -> None:
        baseline = child_report("none")
        pssm = child_report("pssm")
        record = SCENE.compare_performance(
            baseline,
            pssm,
            SCENE.DEFAULT_MAX_P95_MS,
        )
        self.assertEqual(record["samples"], 1410)
        self.assertAlmostEqual(record["pssm_mean_overhead_ms"], 1.0)
        self.assertAlmostEqual(record["pssm_p95_overhead_ms"], 2.0)

        over_budget = copy.deepcopy(pssm)
        over_budget["metrics"]["frame_p95_ms"] = 20.0
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_performance(
                baseline,
                over_budget,
                SCENE.DEFAULT_MAX_P95_MS,
            )

        unequal_samples = copy.deepcopy(pssm)
        unequal_samples["metrics"]["frame_samples"] = 1409
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_performance(
                baseline,
                unequal_samples,
                SCENE.DEFAULT_MAX_P95_MS,
            )

    def test_rgb_gate_accepts_localized_cast_shadow(self) -> None:
        baseline, pssm = localized_shadow_pixels()
        record = SCENE.compare_rgb(baseline, pssm)
        self.assertGreater(record["darkened_by_4_fraction"], 0.01)
        self.assertGreater(record["darkened_by_12_fraction"], 0.005)
        self.assertLess(record["lightened_by_4_fraction"], 0.005)
        self.assertGreater(
            record["roi"]["darkened_by_12_fraction"],
            0.04,
        )

    def test_rgb_gate_rejects_no_effect_global_dimming_and_lightening(
        self,
    ) -> None:
        baseline, pssm = localized_shadow_pixels()
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_rgb(baseline, baseline)

        globally_dimmed = bytes(
            max(value - 5, 0)
            for value in baseline
        )
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_rgb(baseline, globally_dimmed)

        lightened = bytearray(baseline)
        for y in range(SCENE.ROI_TOP, SCENE.ROI_BOTTOM):
            for x in range(SCENE.ROI_LEFT, SCENE.ROI_RIGHT):
                offset = (y * SCENE.EXPECTED_WIDTH + x) * 3
                lightened[offset : offset + 3] = bytes((180, 180, 180))
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_rgb(baseline, bytes(lightened))

        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.compare_rgb(baseline[:-3], pssm)

    def test_artifact_record_authenticates_path_size_and_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "child.json"
            artifact.write_bytes(b"authenticated child\n")
            record = SCENE.artifact_record(root, "child.json")
            self.assertEqual(record["path"], "child.json")
            self.assertEqual(record["size"], 20)
            self.assertEqual(
                record["sha256"],
                SCENE.BASE.sha256_file(artifact),
            )
            with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                SCENE.artifact_record(root, "missing.json")


if __name__ == "__main__":
    unittest.main()
