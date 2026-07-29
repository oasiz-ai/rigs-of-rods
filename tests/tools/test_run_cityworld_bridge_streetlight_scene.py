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
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = (
    REPOSITORY_ROOT / "tools/run_cityworld_bridge_streetlight_scene.py"
)
FIXTURE_ROOT = (
    REPOSITORY_ROOT
    / "tests/fixtures/cityworld_bridge_streetlight_runtime"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_bridge_streetlight_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(
        "could not load CityWorld bridge streetlight runtime tool"
    )
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
            SCENE.SCRIPT_MARKERS[0],
            SCENE.SCRIPT_MARKERS[1],
            SCENE.SCRIPT_MARKERS[2],
            SCENE.SCRIPT_MARKERS[3]
            + " frames=45 physics_steps=180",
        )
    )
    return engine, script


def copy_contract_inputs(destination: Path) -> None:
    paths = (
        SCENE.ASSET_MANIFEST,
        SCENE.COMPILE_REPORT,
        (
            "resources/nextgen/cityworld/fixtures/"
            "led_streetlight_bridge/compiled/"
            "rorng_city_led_streetlight_bridge.odef"
        ),
    )
    for relative in paths:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPOSITORY_ROOT / relative, target)
    fixture = destination / SCENE.FIXTURE_DIRECTORY
    fixture.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(FIXTURE_ROOT, fixture)


class CityWorldBridgeStreetlightSceneTests(unittest.TestCase):
    def test_lighting_policy_markers_are_pinned(self) -> None:
        self.assertEqual(
            SCENE.FALLBACK_LIGHTING_MARKER,
            "[RoR|Terrain|Lighting] policy=fallback-v1 "
            "ambient_scale=0.350 directional_shadow_casters=1 "
            "ambient_rgb=0.084,0.084,0.084",
        )
        self.assertIn(
            SCENE.FALLBACK_LIGHTING_MARKER,
            SCENE.ENGINE_SINGLETON_MARKERS,
        )
        self.assertIn(
            "[RoR|TerrainObject|Lights] "
            "odef=rorng_city_led_streetlight_bridge.odef "
            "spotlights=0 point_lights=1 local_shadow_casters=0",
            SCENE.ENGINE_SINGLETON_MARKERS,
        )
        self.assertIn(
            "[RoR|TerrainObject|LocalLightBudget] "
            "discovered=1 active=1 budget=64",
            SCENE.ENGINE_SINGLETON_MARKERS,
        )

    def test_runtime_log_gate_requires_visual_and_point_light_evidence(
        self,
    ) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertEqual(metrics["collision_objects"], 0)
        self.assertEqual(metrics["point_lights"], 1)
        self.assertEqual(metrics["frames"], 45)
        self.assertEqual(metrics["physics_steps"], 180)

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
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(7, "", engine, script)
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(-11, "", engine, script)

    def test_runtime_log_gate_rejects_out_of_contract_metrics(self) -> None:
        engine, script = valid_logs()
        for old, new in (
            ("frames=45", "frames=44"),
            ("physics_steps=180", "physics_steps=0"),
            ("physics_steps=180", "physics_steps=4097"),
        ):
            with self.subTest(value=new):
                with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(old, new),
                    )
        with self.assertRaises(SCENE.BASE.BridgeSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine
                + "\nError: ScriptCompiler - unexpected token in "
                "rorng_city_led_streetlight_bridge.material",
                script,
            )

    def test_checked_fixture_contract_is_point_lit_and_collisionless(
        self,
    ) -> None:
        contract = SCENE.validate_fixture_contract(REPOSITORY_ROOT)
        self.assertEqual(contract["asset_id"], SCENE.ASSET_ID)
        self.assertEqual(
            contract["collision"],
            {"objects": 0, "profile": "collisionless-visual-v1"},
        )
        self.assertEqual(
            contract["runtime_light"],
            SCENE.EXPECTED_RUNTIME_LIGHT,
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
            manifest["collision"]["objects"] = [
                {"name": "unexpected-collision"}
            ]
            manifest_path.write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                SCENE.BASE.BridgeSceneFailure,
                "empty collision inventory",
            ):
                SCENE.validate_fixture_contract(repository)

            shutil.copy2(
                REPOSITORY_ROOT / SCENE.ASSET_MANIFEST,
                manifest_path,
            )
            odef = (
                repository
                / "resources/nextgen/cityworld/fixtures/"
                "led_streetlight_bridge/compiled/"
                "rorng_city_led_streetlight_bridge.odef"
            )
            odef.write_text(
                SCENE.EXPECTED_ODEF.replace(
                    "\nend\n",
                    "\nbeginmesh\ncollision.mesh\nendmesh\n\nend\n",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                SCENE.BASE.BridgeSceneFailure,
                "reviewed contract",
            ):
                SCENE.validate_fixture_contract(repository)

    def test_scene_configuration_keeps_native_backends_isolated(self) -> None:
        contract = SCENE.validate_fixture_contract(REPOSITORY_ROOT)
        SCENE.configure_base(REPOSITORY_ROOT, contract)
        self.assertEqual(SCENE.BASE.ASSET_MANIFEST, SCENE.ASSET_MANIFEST)
        self.assertEqual(SCENE.BASE.COMPILE_REPORT, SCENE.COMPILE_REPORT)
        self.assertEqual(SCENE.BASE.FIXTURE_FILES, SCENE.FIXTURE_FILES)
        self.assertEqual(SCENE.BASE.TERRAIN, SCENE.TERRAIN)
        self.assertIs(
            SCENE.BASE.validate_runtime_logs,
            SCENE.validate_runtime_logs,
        )
        self.assertEqual(
            SCENE.BASE.RUNNER_PATHS,
            (
                "tools/run_cityworld_bridge_scene.py",
                "tools/run_cityworld_bridge_streetlight_scene.py",
            ),
        )

        root = Path("/isolated-bridge-streetlight")
        for target, backend in (
            ("darwin", "gl3plus"),
            ("linux", "gl3plus"),
            ("win32", "d3d11"),
        ):
            with self.subTest(target=target):
                layout = SCENE.BASE.runtime_layout(root, target)
                for path in layout.values():
                    self.assertTrue(path.is_relative_to(root))
                self.assertEqual(
                    SCENE.BASE.renderer_contract(target).backend,
                    backend,
                )

    def test_runtime_pack_contains_only_visual_fixture_outputs(self) -> None:
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
            self.assertFalse(
                any(
                    "collision" in path.name.casefold()
                    for path in compiled
                )
            )
            with zipfile.ZipFile(first) as archive:
                self.assertEqual(
                    archive.namelist(),
                    sorted(first_inventory),
                )
                self.assertIsNone(archive.testzip())

    def test_fixture_is_ui_free_and_keeps_world_collisions_enabled(self) -> None:
        script = (
            FIXTURE_ROOT / "cityworld_bridge_streetlight_runtime.as"
        ).read_text(encoding="utf-8")
        for value in (
            'console.cVarSet("ui_hide_gui", "true")',
            'console.cVarSet("sim_no_collisions", "false")',
            '"sim_deterministic_fixed_steps_per_frame", "4"',
            "MSG_APP_SCREENSHOT_REQUESTED",
            "collision_subsystem=enabled",
            "collision_objects=0 point_lights=1",
        ):
            with self.subTest(contract=value):
                self.assertIn(value, script)
        for forbidden in (
            "MSG_SIM_SPAWN_ACTOR_REQUESTED",
            "setEventSimulatedValue",
            "sim_no_collisions\", \"true",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, script)
        self.assertEqual(
            (
                FIXTURE_ROOT
                / "cityworld_bridge_streetlight_runtime.tobj"
            ).read_text(encoding="utf-8"),
            SCENE.EXPECTED_TOBJ,
        )

    def test_native_workflows_cover_all_supported_renderers(self) -> None:
        matrix = (
            REPOSITORY_ROOT / ".github/workflows/ogre14-native.yml"
        ).read_text(encoding="utf-8")
        macos = (
            REPOSITORY_ROOT / ".github/workflows/macos-native.yml"
        ).read_text(encoding="utf-8")
        invocation = "tools/run_cityworld_bridge_streetlight_scene.py"
        self.assertEqual(matrix.count(invocation), 2)
        self.assertIn("if: runner.os == 'Linux'", matrix)
        self.assertIn("if: runner.os == 'Windows'", matrix)
        self.assertEqual(macos.count(invocation), 1)
        self.assertIn(
            "Bridge streetlight with macOS arm64 GL3Plus",
            macos,
        )

    def test_tool_is_standard_library_only_and_optimization_safe(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        tree = ast.parse(source)
        imported: set[str] = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(
                    alias.name.split(".", 1)[0] for alias in node.names
                )
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


if __name__ == "__main__":
    unittest.main()
