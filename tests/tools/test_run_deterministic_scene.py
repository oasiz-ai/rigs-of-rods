#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_deterministic_scene.py"
SCRIPT_PATH = (
    REPOSITORY_ROOT
    / "resources/scripts/example_deterministic_two_truck_trace.as"
)
GAME_SCRIPT_PATH = (
    REPOSITORY_ROOT / "source/main/scripting/GameScript.cpp"
)

SPEC = importlib.util.spec_from_file_location(
    "run_deterministic_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load deterministic scene tool")
SCENE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCENE)


def comparison(
    *,
    status: str = "match",
    steps: int = 1000,
    left_workers: int = 1,
    right_workers: int = 8,
) -> dict[str, object]:
    def side(workers: int) -> dict[str, object]:
        return {
            "label": "fixture",
            "metadata": {
                "digest_schema_version": 1,
                "worker_count": workers,
                "scenario_id": SCENE.SCENARIO_ID,
                "first_physics_step": 0,
                "physics_step_numerator": 1,
                "physics_step_denominator": 2000,
                "physics_flags": 0,
            },
            "step": None,
            "error": {
                "code": "none",
                "byte_offset": 0,
                "step_index": 0,
            },
        }

    return {
        "format": "ror-d0-state-trace-comparison-v1",
        "status": status,
        "difference": "none",
        "metadata_field": "none",
        "steps_compared": steps,
        "first_divergent_step": None,
        "left": side(left_workers),
        "right": side(right_workers),
    }


class DeterministicSceneToolTests(unittest.TestCase):
    def test_trace_comparison_requires_exact_runtime_contract(self) -> None:
        SCENE.validate_trace_comparison(comparison(), 1, 8)

        mutations = (
            ("status", "diverged"),
            ("steps_compared", 999),
            ("format", "unknown"),
        )
        for field, value in mutations:
            payload = comparison()
            payload[field] = value
            with self.subTest(field=field):
                with self.assertRaises(SCENE.SceneFailure):
                    SCENE.validate_trace_comparison(payload, 1, 8)

        for side_name, field, value in (
            ("left", "worker_count", 2),
            ("right", "scenario_id", 0),
            ("right", "first_physics_step", 1),
            ("left", "physics_step_denominator", 1000),
        ):
            payload = comparison()
            side = payload[side_name]
            self.assertIsInstance(side, dict)
            metadata = side["metadata"]
            self.assertIsInstance(metadata, dict)
            metadata[field] = value
            with self.subTest(side=side_name, field=field):
                with self.assertRaises(SCENE.SceneFailure):
                    SCENE.validate_trace_comparison(payload, 1, 8)

    def test_comparison_parser_rejects_non_json_and_non_objects(self) -> None:
        with self.assertRaises(SCENE.SceneFailure):
            SCENE.parse_trace_comparison("not json")
        with self.assertRaises(SCENE.SceneFailure):
            SCENE.parse_trace_comparison("[]")
        self.assertEqual(
            SCENE.parse_trace_comparison(
                json.dumps(comparison(left_workers=1, right_workers=1))
            )["status"],
            "match",
        )

    def test_runtime_log_gate_requires_success_markers(self) -> None:
        script_log = "\n".join(SCENE.SCRIPT_MARKERS)
        engine_log = "\n".join(SCENE.ENGINE_MARKERS)
        SCENE.validate_runtime_logs(0, "", engine_log, script_log)

        for marker in SCENE.SCRIPT_MARKERS:
            with self.subTest(script_marker=marker):
                with self.assertRaises(SCENE.SceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine_log,
                        script_log.replace(marker, ""),
                    )
        for marker in SCENE.FATAL_MARKERS:
            with self.subTest(fatal_marker=marker):
                with self.assertRaises(SCENE.SceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        marker,
                        engine_log,
                        script_log,
                    )

    def test_runtime_fixture_copy_is_byte_exact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            runtime = root / "runtime"
            relative = "dafsemi/fixture.bin"
            (source / relative).parent.mkdir(parents=True)
            (runtime / relative).parent.mkdir(parents=True)
            (source / relative).write_bytes(b"pinned fixture")
            (runtime / relative).write_bytes(b"pinned fixture")

            SCENE.verify_runtime_fixture_files(
                source,
                runtime,
                (relative,),
            )
            (runtime / relative).write_bytes(b"changed fixture")
            with self.assertRaises(SCENE.SceneFailure):
                SCENE.verify_runtime_fixture_files(
                    source,
                    runtime,
                    (relative,),
                )
            (runtime / relative).unlink()
            with self.assertRaises(SCENE.SceneFailure):
                SCENE.verify_runtime_fixture_files(
                    source,
                    runtime,
                    (relative,),
                )

    def test_packaged_fixture_archive_is_complete_and_byte_exact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            runtime = root / "runtime"
            tracked = (
                "dafsemi/first.bin",
                "dafsemi/second.bin",
            )
            for relative, payload in zip(
                tracked,
                (b"first pinned file", b"second pinned file"),
            ):
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
            runtime.mkdir()
            archive_path = runtime / "dafsemi.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                for relative in tracked:
                    archive.write(
                        source / relative,
                        Path(relative).name,
                    )

            SCENE.verify_runtime_fixture_files(
                source,
                runtime,
                tracked,
            )

            with zipfile.ZipFile(archive_path, "a") as archive:
                archive.writestr("unexpected.bin", b"unexpected")
            with self.assertRaises(SCENE.SceneFailure):
                SCENE.verify_runtime_fixture_files(
                    source,
                    runtime,
                    tracked,
                )

    def test_generated_config_is_isolated_and_worker_specific(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = SCENE.write_runtime_config(root, 8, True)
            self.assertTrue(config.is_relative_to(root))
            text = config.read_text(encoding="utf-8")
            self.assertIn("app_num_workers=8", text)
            self.assertIn("app_force_cache_update=true", text)
            self.assertIn("app_disable_online_api=true", text)
            self.assertIn("gfx_shadow_type=0", text)
            self.assertIn("gfx_sky_mode=0", text)
            self.assertIn("gfx_water_mode=0", text)
            self.assertNotIn(str(Path.home()), text)
            ogre = config.parent / "ogre.cfg"
            ogre_text = ogre.read_text(encoding="utf-8")
            self.assertIn("Content Scaling Factor=1", ogre_text)
            self.assertIn("Video Mode=1280 x 720", ogre_text)
            self.assertIn("VSync=No", ogre_text)

    def test_macos_scene_command_disables_state_restoration(self) -> None:
        command = SCENE.build_scene_command(Path("/tmp/RoR"))
        self.assertEqual(command[0], "/tmp/RoR")
        self.assertEqual(
            command[-4:],
            (
                "-map",
                SCENE.TERRAIN,
                "-runscript",
                SCENE.SCENARIO_SCRIPT,
            ),
        )
        if SCENE.sys.platform == "darwin":
            self.assertEqual(
                command[1:3],
                ("-ApplePersistenceIgnoreState", "YES"),
            )

    def test_trace_discovery_rejects_zero_or_multiple_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(SCENE.SceneFailure):
                SCENE.find_single_trace(root)
            first = root / "first.rortrace"
            first.write_bytes(b"trace")
            self.assertEqual(SCENE.find_single_trace(root), first)
            (root / "second.rortrace").write_bytes(b"trace")
            with self.assertRaises(SCENE.SceneFailure):
                SCENE.find_single_trace(root)

    def test_scene_script_locks_the_documented_d0_fixture(self) -> None:
        script = SCRIPT_PATH.read_text(encoding="utf-8")
        for marker in (
            'const int64 FIRST_ACTOR_ID = 1001;',
            'const int64 SECOND_ACTOR_ID = 1002;',
            'const int EXPECTED_NODES_PER_ACTOR = 176;',
            'const uint64 EXPECTED_PHYSICS_STEPS = 1000;',
            'const string SCENARIO_ID = "2026072801";',
            '{"free_position", true}',
            '"sim_deterministic_fixed_steps_per_frame", "10"',
            '"sim_deterministic_state_trace_step_limit"',
            "game.getCompletedPhysicsSteps()",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, script)

    def test_script_spawn_bridge_honors_exact_position_and_int64_id(self) -> None:
        source = GAME_SCRIPT_PATH.read_text(encoding="utf-8")
        self.assertIn('"instance_id",\n                        "int64"', source)
        self.assertIn('"free_position",\n                    "bool"', source)
        self.assertIn("rq->asr_free_position", source)


if __name__ == "__main__":
    unittest.main()
