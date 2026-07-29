#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import ast
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_led_streetlight_scene.py"
)
FIXTURE_ROOT = (
    REPOSITORY_ROOT / "tests/fixtures/cityworld_led_streetlight_runtime"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_led_streetlight_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld LED streetlight runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(
        marker
        for marker in SCENE.ENGINE_MARKERS
        for _ in (
            range(1)
            if marker in SCENE.ENGINE_SINGLETON_MARKERS
            else range(2)
        )
    )
    script = "\n".join(
        (
            "[RoR|CW1|StreetlightRuntime] START fixtures=1 "
            "collision_triangles=44 pole_radius=0.34",
            "[RoR|CW1|StreetlightRuntime] ARMED actor=2026072901 "
            "nodes=79 heading=3.14159",
            "[RoR|CW1|StreetlightRuntime] CONTACT step=12000 "
            "clearance=0.08 approach_speed=6 actor_z=496",
            "[RoR|CW1|StreetlightRuntime] CAPTURE",
            "[RoR|CW1|StreetlightRuntime] PASS fixtures=1 "
            "collision_triangles=44 approach_speed=6 "
            "post_contact_speed=2 min_clearance=-0.02 "
            "contact_travel=1.5 max_z=497.5 physics_steps=14000",
        )
    )
    return engine, script


def copy_contract_inputs(destination: Path) -> None:
    manifest = destination / SCENE.ASSET_MANIFEST
    manifest.parent.mkdir(parents=True)
    shutil.copy2(REPOSITORY_ROOT / SCENE.ASSET_MANIFEST, manifest)
    fixture = destination / SCENE.FIXTURE_DIRECTORY
    fixture.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(FIXTURE_ROOT, fixture)


class CityWorldLedStreetlightSceneTests(unittest.TestCase):
    def test_runtime_log_gate_requires_render_and_collision_evidence(
        self,
    ) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertEqual(metrics["contact_step"], 12000)
        self.assertEqual(metrics["physics_steps"], 14000)
        self.assertAlmostEqual(metrics["min_clearance_m"], -0.02)
        self.assertAlmostEqual(metrics["contact_travel_m"], 1.5)
        self.assertAlmostEqual(metrics["distance_m"], 30.0)

        for marker in SCENE.ENGINE_MARKERS:
            with self.subTest(engine_marker=marker):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine.replace(marker, ""),
                        script,
                    )
                duplicate_count = (
                    1
                    if marker in SCENE.ENGINE_SINGLETON_MARKERS
                    else 3
                )
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine
                        + ("\n" + marker) * duplicate_count,
                        script,
                    )
        for marker in SCENE.SCRIPT_MARKERS:
            with self.subTest(script_marker=marker):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(marker, ""),
                    )
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script + "\n" + marker,
                    )
        for marker in SCENE.FATAL_MARKERS:
            with self.subTest(fatal_marker=marker):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        marker,
                        engine,
                        script,
                    )
        unrelated_legacy_error = (
            engine
            + "\nError: ScriptCompiler - unexpected token in "
            "simple2.os(1): 'caelum_sky_system'"
        )
        SCENE.validate_runtime_logs(
            0,
            "",
            unrelated_legacy_error,
            script,
        )
        with self.assertRaisesRegex(
            SCENE.BASE.BridgeSceneFailure,
            "streetlight material",
        ):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine
                + "\nError: ScriptCompiler - unexpected token in "
                "rorng_city_led_streetlight.material(1): 'material'",
                script,
            )
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(7, "", engine, script)
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(-11, "", engine, script)

    def test_runtime_log_gate_rejects_out_of_contract_metrics(self) -> None:
        engine, script = valid_logs()
        replacements = (
            ("approach_speed=6", "approach_speed=1"),
            ("post_contact_speed=2", "post_contact_speed=5"),
            ("min_clearance=-0.02", "min_clearance=-0.16"),
            ("contact_travel=1.5", "contact_travel=3.6"),
            ("max_z=497.5", "max_z=500"),
            ("physics_steps=14000", "physics_steps=24001"),
            ("CONTACT step=12000", "CONTACT step=14000"),
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

        disagreement = script.replace(
            "CONTACT step=12000 clearance=0.08 approach_speed=6",
            "CONTACT step=12000 clearance=0.08 approach_speed=5.8",
        )
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(0, "", engine, disagreement)
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine,
                script + "\n" + script.splitlines()[-1],
            )

    def test_checked_fixture_contract_is_exact_and_fail_closed(self) -> None:
        contract = SCENE.validate_fixture_contract(REPOSITORY_ROOT)
        self.assertEqual(contract["asset_id"], SCENE.ASSET_ID)
        self.assertEqual(contract["collision"]["triangles"], 44)
        self.assertTrue(contract["collision"]["watertight"])
        self.assertEqual(
            contract["placement"],
            {"x": 512.0, "y": 0.08, "z": 500.0},
        )
        self.assertEqual(
            set(contract["materials"]),
            SCENE.EXPECTED_MATERIALS,
        )

        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            copy_contract_inputs(repository)
            manifest_path = repository / SCENE.ASSET_MANIFEST
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            manifest["collision"]["objects"][0]["triangles"] = 45
            manifest_path.write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                SCENE.BASE.BridgeSceneFailure,
                "reviewed proxy",
            ):
                SCENE.validate_fixture_contract(repository)

            shutil.copy2(
                REPOSITORY_ROOT / SCENE.ASSET_MANIFEST,
                manifest_path,
            )
            tobj = (
                repository
                / SCENE.FIXTURE_DIRECTORY
                / "cityworld_led_streetlight_runtime.tobj"
            )
            tobj.write_text(
                SCENE.EXPECTED_TOBJ + SCENE.EXPECTED_TOBJ,
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                SCENE.BASE.BridgeSceneFailure,
                "placement fixture drifted",
            ):
                SCENE.validate_fixture_contract(repository)

    def test_scene_configuration_keeps_native_backends_isolated(self) -> None:
        contract = SCENE.validate_fixture_contract(REPOSITORY_ROOT)
        SCENE.configure_base(REPOSITORY_ROOT, contract)
        self.assertEqual(SCENE.BASE.ASSET_MANIFEST, SCENE.ASSET_MANIFEST)
        self.assertEqual(SCENE.BASE.COMPILE_REPORT, SCENE.COMPILE_REPORT)
        self.assertEqual(SCENE.BASE.FIXTURE_FILES, SCENE.FIXTURE_FILES)
        self.assertEqual(SCENE.BASE.TERRAIN, SCENE.TERRAIN)
        self.assertEqual(SCENE.BASE.EXPECTED_WIDTH, 1280)
        self.assertEqual(SCENE.BASE.EXPECTED_HEIGHT, 720)
        self.assertIs(
            SCENE.BASE.validate_runtime_logs,
            SCENE.validate_runtime_logs,
        )
        self.assertEqual(
            SCENE.BASE.RUNNER_PATHS,
            (
                "tools/run_cityworld_bridge_scene.py",
                "tools/run_cityworld_led_streetlight_scene.py",
            ),
        )

        root = Path("/isolated-streetlight")
        for target in ("darwin", "linux", "win32"):
            with self.subTest(target=target):
                layout = SCENE.BASE.runtime_layout(root, target)
                for path in layout.values():
                    self.assertTrue(path.is_relative_to(root))
        self.assertEqual(
            SCENE.BASE.renderer_contract("darwin").backend,
            "gl3plus",
        )
        self.assertEqual(
            SCENE.BASE.renderer_contract("linux").backend,
            "gl3plus",
        )
        self.assertEqual(
            SCENE.BASE.renderer_contract("win32").backend,
            "d3d11",
        )

        with tempfile.TemporaryDirectory() as directory:
            config_root = Path(directory)
            for target in ("darwin", "linux", "win32"):
                with self.subTest(config=target):
                    ror, ogre = SCENE.BASE.write_runtime_config(
                        config_root / target,
                        target_platform=target,
                    )
                    self.assertIn(
                        "app_force_cache_update=true",
                        ror.read_text(encoding="utf-8"),
                    )
                    ogre_text = ogre.read_text(encoding="utf-8")
                    self.assertIn("1280", ogre_text)
                    self.assertIn("720", ogre_text)
                    self.assertIn(
                        SCENE.BASE.renderer_contract(target).render_system,
                        ogre_text,
                    )

    def test_runtime_pack_contains_fixture_and_compiled_outputs(self) -> None:
        contract = SCENE.validate_fixture_contract(REPOSITORY_ROOT)
        SCENE.configure_base(REPOSITORY_ROOT, contract)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "content"
            simple = source / "simple2-terrain"
            simple.mkdir(parents=True)
            for name in SCENE.BASE.SIMPLE2_FILES:
                (simple / name).write_bytes(("simple-" + name).encode())
            compiled: list[Path] = []
            for name in SCENE.EXPECTED_OUTPUTS.values():
                path = root / "compiled" / name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(("compiled-" + name).encode())
                compiled.append(path)

            first = root / "first.zip"
            second = root / "second.zip"
            first_inventory, first_hash = SCENE.BASE.build_runtime_pack(
                REPOSITORY_ROOT,
                source,
                compiled,
                first,
            )
            second_inventory, second_hash = SCENE.BASE.build_runtime_pack(
                REPOSITORY_ROOT,
                source,
                reversed(compiled),
                second,
            )
            self.assertEqual(first_inventory, second_inventory)
            self.assertEqual(first_hash, second_hash)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            for name in (
                *SCENE.FIXTURE_FILES,
                *SCENE.EXPECTED_OUTPUTS.values(),
            ):
                self.assertIn(name, first_inventory)
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(
                    archive.namelist(),
                    sorted(first_inventory),
                )
                self.assertIsNone(archive.testzip())

    def test_fixture_script_enforces_ui_free_collision_capture(self) -> None:
        script = (
            FIXTURE_ROOT / "cityworld_led_streetlight_runtime.as"
        ).read_text(encoding="utf-8")
        required = (
            'console.cVarSet("ui_hide_gui", "true")',
            'console.cVarSet("sim_no_collisions", "false")',
            "sim_deterministic_fixed_steps_per_frame",
            "gActor.getNodePosition(index)",
            "gActor.getSpeed()",
            "POST_CONTACT_STEPS",
            "collision-proxy-not-contacted",
            "collision-proxy-penetrated",
            "insufficient-speed-loss",
            "MSG_APP_SCREENSHOT_REQUESTED",
            "[RoR|CW1|StreetlightRuntime] CONTACT",
            "[RoR|CW1|StreetlightRuntime] PASS",
        )
        for value in required:
            with self.subTest(contract=value):
                self.assertIn(value, script)
        self.assertEqual(
            (
                FIXTURE_ROOT / "cityworld_led_streetlight_runtime.tobj"
            ).read_text(encoding="utf-8"),
            SCENE.EXPECTED_TOBJ,
        )
        terrain = (
            FIXTURE_ROOT / "cityworld_led_streetlight_runtime.terrn2"
        ).read_text(encoding="utf-8")
        self.assertEqual(
            terrain.count("cityworld_led_streetlight_runtime.tobj ="),
            1,
        )
        self.assertEqual(
            terrain.count("cityworld_led_streetlight_runtime.as ="),
            1,
        )

    def test_tool_is_standard_library_only_and_optimization_safe(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        tree = ast.parse(source)
        imported: set[str] = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(alias.name.split(".", 1)[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                if node.module is not None:
                    imported.add(node.module.split(".", 1)[0])
        self.assertEqual(
            imported,
            {
                "__future__",
                "argparse",
                "importlib",
                "math",
                "pathlib",
                "re",
                "sys",
                "typing",
            },
        )
        self.assertFalse(
            any(isinstance(node, ast.Assert) for node in ast.walk(tree))
        )
        lowered = source.casefold()
        self.assertNotIn("source/main/worldmodel", lowered)
        self.assertNotIn("tests/worldmodel", lowered)
        self.assertNotIn("git clean", lowered)
        self.assertNotIn("git reset", lowered)
        self.assertNotIn("git restore", lowered)

    def test_invalid_contract_stops_before_base_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            copy_contract_inputs(repository)
            manifest_path = repository / SCENE.ASSET_MANIFEST
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            manifest["asset"]["profile"] = "corridor-module-v1"
            manifest_path.write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            with mock.patch.object(SCENE.BASE, "main") as base_main:
                with self.assertRaises(
                    SCENE.BASE.BridgeSceneFailure,
                ):
                    SCENE.main(
                        (
                            "--repository",
                            str(repository),
                            "--executable",
                            str(repository / "RoR"),
                            "--artifact-dir",
                            str(repository / "artifacts"),
                        )
                    )
                base_main.assert_not_called()


if __name__ == "__main__":
    unittest.main()
