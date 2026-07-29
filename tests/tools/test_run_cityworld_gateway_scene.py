#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_gateway_scene.py"
FIXTURE_ROOT = REPOSITORY_ROOT / "tests/fixtures/cityworld_gateway_runtime"

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_gateway_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld gateway runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(SCENE.ENGINE_MARKERS)
    script = "\n".join(
        (
            *SCENE.SCRIPT_MARKERS[:-1],
            "[RoR|CW2|GatewayRuntime] PERF samples=1200 "
            "mean_ms=5.2 p95_ms=6.4 max_ms=15.0",
            "[RoR|CW2|GatewayRuntime] PASS modules=5 seams=4 "
            "turn_degrees=45 distance_m=136 min_y=0.7 max_y=1.6 "
            "path_error=1.4 exit_x=566 exit_z=570.5 speed=10 "
            "physics_steps=30000",
        )
    )
    return engine, script


class CityWorldGatewaySceneTests(unittest.TestCase):
    def test_checked_gateway_corridor_is_exact(self) -> None:
        corridor = SCENE.verify_corridor_fixture(REPOSITORY_ROOT)
        self.assertEqual(corridor["format"], "ror-cityworld-bridge-corridor-v1")
        self.assertEqual(len(corridor["modules"]), 5)
        self.assertEqual(len(corridor["seams"]), 4)
        self.assertEqual(
            corridor["modules"][-1]["asset_id"],
            "rorng_city_gateway_block_40m",
        )
        self.assertEqual(
            corridor["exit"],
            {
                "heading_degrees": 44.999999942,
                "x": 571.144946272,
                "z": 572.78853164,
            },
        )

    def test_runtime_log_gate_requires_all_assets_and_materials(self) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertAlmostEqual(metrics["distance_m"], 136.0)
        self.assertAlmostEqual(metrics["max_path_error_m"], 1.4)
        self.assertEqual(metrics["physics_steps"], 30000)
        self.assertEqual(metrics["frame_samples"], 1200)
        self.assertAlmostEqual(metrics["frame_p95_ms"], 6.4)
        for marker in SCENE.ENGINE_MARKERS:
            with self.subTest(engine_marker=marker):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine.replace(marker, ""),
                        script,
                    )
        for marker in SCENE.SCRIPT_MARKERS[:-1]:
            with self.subTest(script_marker=marker):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(marker, ""),
                    )

    def test_runtime_log_gate_rejects_out_of_contract_metrics(self) -> None:
        engine, script = valid_logs()
        replacements = (
            ("distance_m=136", "distance_m=100"),
            ("min_y=0.7", "min_y=nan"),
            ("max_y=1.6", "max_y=9"),
            ("path_error=1.4", "path_error=2.6"),
            ("exit_x=566", "exit_x=550"),
            ("exit_z=570.5", "exit_z=590"),
            ("speed=10", "speed=0"),
            ("physics_steps=30000", "physics_steps=48001"),
            ("samples=1200", "samples=499"),
            ("mean_ms=5.2", "mean_ms=7.0"),
        )
        for old, new in replacements:
            with self.subTest(value=new):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(old, new),
                    )

    def test_three_asset_package_has_unique_complete_outputs(self) -> None:
        corridor = SCENE.verify_corridor_fixture(REPOSITORY_ROOT)
        SCENE.configure_base(REPOSITORY_ROOT, corridor)
        _, outputs = SCENE.BASE.validate_cityworld_package(
            REPOSITORY_ROOT,
            30,
        )
        names = [path.name for path in outputs]
        self.assertEqual(len(names), 24)
        self.assertEqual(len(names), len(set(names)))
        for expected in (
            "rorng_city_bridge_curve_left_15deg_20m_lod0.mesh",
            "rorng_city_bridge_transition_12m_lod0.mesh",
            "rorng_city_gateway_block_40m_lod0.mesh",
        ):
            self.assertIn(expected, names)

    def test_fixture_captures_inside_gateway_with_bounded_physics(self) -> None:
        script = (
            FIXTURE_ROOT / "cityworld_gateway_runtime.as"
        ).read_text(encoding="utf-8")
        for marker in (
            "const uint64 MAX_PHYSICS_STEPS = 48000;",
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            "const uint PERFORMANCE_WARMUP_FRAMES = 120;",
            "const uint MIN_PERFORMANCE_SAMPLES = 500;",
            "(95 * gFrameTimesMs.length() + 99) / 100 - 1",
            'Fail("invalid-frame-time-" + dt);',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            "GATEWAY_ENTRY_X = 542.860675053f",
            "SEAM index=3",
            "gatewayProgress >= 8.0f",
            "PASS_PROGRESS = 35.0f",
            "MSG_APP_SCREENSHOT_REQUESTED",
            "PERF samples=",
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)


if __name__ == "__main__":
    unittest.main()
