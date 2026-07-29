#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_curved_bridge_scene.py"
FIXTURE_ROOT = (
    REPOSITORY_ROOT / "tests/fixtures/cityworld_curved_bridge_runtime"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_curved_bridge_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load curved bridge runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(SCENE.ENGINE_MARKERS)
    script = "\n".join(
        (
            *SCENE.SCRIPT_MARKERS[:-1],
            "[RoR|CW2|CurveRuntime] PASS modules=3 seams=2 "
            "turn_degrees=45 distance_m=89.5 min_y=0.7 max_y=1.6 "
            "path_error=0.8 exit_x=533.2 exit_z=535.1 speed=13.5 "
            "physics_steps=24000",
        )
    )
    return engine, script


class CityWorldCurvedBridgeSceneTests(unittest.TestCase):
    def test_checked_corridor_fixture_is_exact(self) -> None:
        corridor = SCENE.verify_corridor_fixture(REPOSITORY_ROOT)
        self.assertEqual(corridor["format"], "ror-cityworld-bridge-corridor-v1")
        self.assertEqual(len(corridor["modules"]), 3)
        self.assertEqual(corridor["exit"]["heading_degrees"], 44.999999942)

    def test_runtime_log_gate_requires_curve_physics_and_shader_evidence(
        self,
    ) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertEqual(metrics["turn_degrees"], 45.0)
        self.assertAlmostEqual(metrics["max_path_error_m"], 0.8)
        self.assertEqual(metrics["physics_steps"], 24000)
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
            ("distance_m=89.5", "distance_m=40"),
            ("min_y=0.7", "min_y=nan"),
            ("max_y=1.6", "max_y=9"),
            ("path_error=0.8", "path_error=2.3"),
            ("exit_x=533.2", "exit_x=520"),
            ("exit_z=535.1", "exit_z=550"),
            ("speed=13.5", "speed=0"),
            ("physics_steps=24000", "physics_steps=36001"),
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

    def test_fixture_enables_collision_steering_capture_and_bounded_steps(
        self,
    ) -> None:
        script = (
            FIXTURE_ROOT / "cityworld_curved_bridge_runtime.as"
        ).read_text(encoding="utf-8")
        for marker in (
            'const uint64 MAX_PHYSICS_STEPS = 36000;',
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            "EV_TRUCK_STEER_LEFT",
            "PATH_RADIUS = 76.394372684f",
            "SEAM index=0",
            "SEAM index=1",
            "MSG_APP_SCREENSHOT_REQUESTED",
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)


if __name__ == "__main__":
    unittest.main()
