#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_bridge_transition_scene.py"
)
FIXTURE_ROOT = (
    REPOSITORY_ROOT / "tests/fixtures/cityworld_bridge_transition_runtime"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_bridge_transition_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load bridge transition runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(SCENE.ENGINE_MARKERS)
    script = "\n".join(
        (
            *SCENE.SCRIPT_MARKERS[:-1],
            "[RoR|CW2|TransitionRuntime] PASS modules=4 seams=3 "
            "turn_degrees=45 distance_m=100.5 min_y=0.7 max_y=1.6 "
            "path_error=1.0 exit_x=541.8 exit_z=543.4 speed=13.5 "
            "physics_steps=26000",
        )
    )
    return engine, script


class CityWorldBridgeTransitionSceneTests(unittest.TestCase):
    def test_checked_transition_corridor_is_exact(self) -> None:
        corridor = SCENE.verify_corridor_fixture(REPOSITORY_ROOT)
        self.assertEqual(corridor["format"], "ror-cityworld-bridge-corridor-v1")
        self.assertEqual(len(corridor["modules"]), 4)
        self.assertEqual(len(corridor["seams"]), 3)
        self.assertEqual(
            corridor["modules"][-1]["asset_id"],
            "rorng_city_bridge_transition_12m",
        )
        self.assertEqual(
            corridor["exit"],
            {
                "heading_degrees": 44.999999942,
                "x": 542.860675053,
                "z": 544.504260364,
            },
        )

    def test_runtime_log_gate_requires_both_assets_and_physics(self) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertEqual(metrics["turn_degrees"], 45.0)
        self.assertAlmostEqual(metrics["max_path_error_m"], 1.0)
        self.assertEqual(metrics["physics_steps"], 26000)
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
            ("distance_m=100.5", "distance_m=80"),
            ("min_y=0.7", "min_y=nan"),
            ("max_y=1.6", "max_y=9"),
            ("path_error=1.0", "path_error=2.6"),
            ("exit_x=541.8", "exit_x=530"),
            ("exit_z=543.4", "exit_z=555"),
            ("speed=13.5", "speed=0"),
            ("physics_steps=26000", "physics_steps=40001"),
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

    def test_multi_asset_package_has_unique_complete_outputs(self) -> None:
        corridor = SCENE.verify_corridor_fixture(REPOSITORY_ROOT)
        SCENE.configure_base(REPOSITORY_ROOT, corridor)
        _, outputs = SCENE.BASE.validate_cityworld_package(
            REPOSITORY_ROOT,
            30,
        )
        names = [path.name for path in outputs]
        self.assertEqual(len(names), 16)
        self.assertEqual(len(names), len(set(names)))
        self.assertIn(
            "rorng_city_bridge_curve_left_15deg_20m_lod0.mesh",
            names,
        )
        self.assertIn(
            "rorng_city_bridge_transition_12m_lod0.mesh",
            names,
        )

    def test_fixture_enables_collision_steering_capture_and_bounded_steps(
        self,
    ) -> None:
        script = (
            FIXTURE_ROOT / "cityworld_bridge_transition_runtime.as"
        ).read_text(encoding="utf-8")
        for marker in (
            "const uint64 MAX_PHYSICS_STEPS = 40000;",
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            "TransitionCrossTrack",
            "SEAM index=2",
            "PASS_PROGRESS = 10.5f",
            "MSG_APP_SCREENSHOT_REQUESTED",
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)


if __name__ == "__main__":
    unittest.main()
