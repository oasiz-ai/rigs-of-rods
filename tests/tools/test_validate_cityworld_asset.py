#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from typing import Any, Callable


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
MANIFEST_RELATIVE = Path(
    "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
MANIFEST_PATH = REPOSITORY_ROOT / MANIFEST_RELATIVE
CURVED_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/curve_left_15deg/"
    "rorng_city_bridge_curve_left_15deg_20m.asset.json"
)
CURVED_MANIFEST_RELATIVE = CURVED_MANIFEST_PATH.relative_to(REPOSITORY_ROOT)
TRANSITION_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/transition_12m/"
    "rorng_city_bridge_transition_12m.asset.json"
)
TRANSITION_MANIFEST_RELATIVE = TRANSITION_MANIFEST_PATH.relative_to(
    REPOSITORY_ROOT
)
GATEWAY_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/streetscape/gateway_block_40m/"
    "rorng_city_gateway_block_40m.asset.json"
)
BASE_GENERATOR_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_bridge_kit.py"
)
DERIVED_GENERATOR_PATHS = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_curved_bridge.py",
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_bridge_transition.py",
)


def mutate_glb_document(
    path: Path,
    mutate: Callable[[dict[str, Any]], None],
) -> None:
    data = path.read_bytes()
    magic, version, _ = struct.unpack_from("<4sII", data, 0)
    chunks: list[tuple[int, bytes]] = []
    offset = 12
    while offset < len(data):
        length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunks.append((chunk_type, data[offset : offset + length]))
        offset += length
    document = json.loads(chunks[0][1].rstrip(b" \t\r\n\x00"))
    mutate(document)
    json_payload = json.dumps(
        document,
        ensure_ascii=True,
        separators=(",", ":"),
    ).encode("utf-8")
    json_payload += b" " * (-len(json_payload) % 4)
    chunks[0] = (chunks[0][0], json_payload)
    payload = b"".join(
        struct.pack("<II", len(chunk), chunk_type) + chunk
        for chunk_type, chunk in chunks
    )
    path.write_bytes(
        struct.pack("<4sII", magic, version, 12 + len(payload)) + payload
    )


class CityWorldAssetValidationTests(unittest.TestCase):
    def run_validator(
        self,
        root: Path,
        manifest: Path,
        *,
        optimized: bool = False,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [
                str(TOOL_PATH),
                str(manifest),
                "--repo-root",
                str(root),
            ]
        )
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
        return result, json.loads(result.stdout)

    def copy_fixture(
        self,
        root: Path,
        manifest_relative: Path = MANIFEST_RELATIVE,
    ) -> Path:
        source_manifest = REPOSITORY_ROOT / manifest_relative
        manifest = json.loads(source_manifest.read_text(encoding="utf-8"))
        relative_paths = {
            manifest_relative.as_posix(),
            manifest["artifacts"]["blend"]["path"],
            manifest["artifacts"]["glb"]["path"],
            manifest["artifacts"]["preview"]["path"],
            manifest["authoring"]["generator"]["path"],
        }
        relative_paths.update(
            dependency["path"]
            for dependency in manifest["authoring"]["generator"].get(
                "dependencies",
                [],
            )
        )
        for relative in sorted(relative_paths):
            source = REPOSITORY_ROOT / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        return root / manifest_relative

    @staticmethod
    def codes(report: dict[str, object]) -> set[str]:
        diagnostics = report["diagnostics"]
        assert isinstance(diagnostics, list)
        return {item["code"] for item in diagnostics}

    def test_base_generator_owns_scene_metadata_sanitation(self) -> None:
        base_tree = ast.parse(
            BASE_GENERATOR_PATH.read_text(encoding="utf-8")
        )
        reset = next(
            node
            for node in base_tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "reset_scene"
        )
        string_constants = {
            node.value
            for node in ast.walk(reset)
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
        }
        self.assertIn("rorng_", string_constants)
        self.assertIn("ror-cityworld-", string_constants)
        self.assertTrue(
            any(isinstance(node, ast.Delete) for node in ast.walk(reset))
        )

        for generator_path in DERIVED_GENERATOR_PATHS:
            with self.subTest(generator=generator_path.name):
                tree = ast.parse(
                    generator_path.read_text(encoding="utf-8")
                )
                derived_reset = next(
                    node
                    for node in tree.body
                    if isinstance(node, ast.FunctionDef)
                    and node.name == "reset_scene_fully"
                )
                constants = {
                    node.value
                    for node in ast.walk(derived_reset)
                    if isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                }
                self.assertNotIn("rorng_", constants)
                self.assertNotIn("ror-cityworld-", constants)
                generator_assignment = next(
                    node
                    for node in derived_reset.body
                    if isinstance(node, ast.Assign)
                    and any(
                        isinstance(target, ast.Attribute)
                        and isinstance(target.value, ast.Name)
                        and target.value.id == "BASE"
                        and target.attr == "GENERATOR_ID"
                        for target in node.targets
                    )
                )
                reset_call = next(
                    node
                    for node in derived_reset.body
                    if isinstance(node, ast.Expr)
                    and isinstance(node.value, ast.Call)
                    and isinstance(node.value.func, ast.Attribute)
                    and isinstance(node.value.func.value, ast.Name)
                    and node.value.func.value.id == "BASE"
                    and node.value.func.attr == "reset_scene"
                )
                self.assertLess(
                    generator_assignment.lineno,
                    reset_call.lineno,
                )

    def test_checked_in_bridge_asset_passes_full_gate(self) -> None:
        result, report = self.run_validator(
            REPOSITORY_ROOT,
            MANIFEST_PATH,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["diagnostics"], [])
        self.assertEqual(
            report["summary"],
            {
                "collision_objects": 3,
                "errors": 0,
                "glb_materials": 7,
                "glb_nodes": 6,
                "lod_objects": 3,
                "runtime_lights": 0,
                "triangles": 5020,
                "valid": True,
            },
        )

    def test_checked_in_curved_bridge_asset_passes_full_gate(self) -> None:
        result, report = self.run_validator(
            REPOSITORY_ROOT,
            CURVED_MANIFEST_PATH,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["diagnostics"], [])
        self.assertEqual(
            report["summary"],
            {
                "collision_objects": 3,
                "errors": 0,
                "glb_materials": 8,
                "glb_nodes": 6,
                "lod_objects": 3,
                "runtime_lights": 0,
                "triangles": 8476,
                "valid": True,
            },
        )
        manifest = json.loads(
            CURVED_MANIFEST_PATH.read_text(encoding="utf-8")
        )
        self.assertEqual(
            [
                dependency["path"]
                for dependency in manifest["authoring"]["generator"][
                    "dependencies"
                ]
            ],
            ["tools/blender/cityworld_next/generate_bridge_kit.py"],
        )

    def test_checked_in_transition_asset_passes_full_gate(self) -> None:
        result, report = self.run_validator(
            REPOSITORY_ROOT,
            TRANSITION_MANIFEST_PATH,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["diagnostics"], [])
        self.assertEqual(
            report["summary"],
            {
                "collision_objects": 3,
                "errors": 0,
                "glb_materials": 7,
                "glb_nodes": 6,
                "lod_objects": 3,
                "runtime_lights": 0,
                "triangles": 1720,
                "valid": True,
            },
        )
        manifest = json.loads(
            TRANSITION_MANIFEST_PATH.read_text(encoding="utf-8")
        )
        self.assertEqual(
            [
                dependency["path"]
                for dependency in manifest["authoring"]["generator"][
                    "dependencies"
                ]
            ],
            ["tools/blender/cityworld_next/generate_bridge_kit.py"],
        )

    def test_checked_in_gateway_block_asset_passes_full_gate(self) -> None:
        result, report = self.run_validator(
            REPOSITORY_ROOT,
            GATEWAY_MANIFEST_PATH,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["diagnostics"], [])
        self.assertEqual(
            report["summary"],
            {
                "collision_objects": 3,
                "errors": 0,
                "glb_materials": 14,
                "glb_nodes": 6,
                "lod_objects": 3,
                "runtime_lights": 8,
                "triangles": 36000,
                "valid": True,
            },
        )
        manifest = json.loads(
            GATEWAY_MANIFEST_PATH.read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["asset"]["version"], 2)
        self.assertEqual(
            manifest["authoring"]["generator"]["format"],
            "ror-cityworld-gateway-block-generator-v2",
        )
        dependency = manifest["authoring"]["generator"]["dependencies"]
        self.assertEqual(len(dependency), 1)
        self.assertEqual(
            dependency[0]["path"],
            "tools/blender/cityworld_next/generate_bridge_kit.py",
        )
        self.assertRegex(dependency[0]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(
            manifest["authoring"]["procedural_provenance"],
            {
                "external_geometry": False,
                "external_textures": False,
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": (
                    "GPL-3.0-or-later project-authored source"
                ),
            },
        )
        self.assertEqual(
            [
                entry["triangles"]
                for entry in manifest["geometry"]["lods"]
            ],
            [32092, 3596, 276],
        )
        self.assertEqual(
            manifest["geometry"]["detail_profile"],
            {
                "building_facades": (
                    "recessed-glazing-frames-doors-balconies-stepped-roofs"
                ),
                "collision_revision": 1,
                "lod_policy": (
                    "authored-three-level-silhouette-preserving"
                ),
                "tree_canopies": (
                    "branched-varied-six-lobe-close-three-lobe-medium"
                ),
            },
        )

    def test_gate_is_equivalent_under_python_optimized_mode(self) -> None:
        normal_result, normal = self.run_validator(
            REPOSITORY_ROOT,
            MANIFEST_PATH,
        )
        optimized_result, optimized = self.run_validator(
            REPOSITORY_ROOT,
            MANIFEST_PATH,
            optimized=True,
        )
        self.assertEqual(normal_result.returncode, 0)
        self.assertEqual(optimized_result.returncode, 0)
        self.assertEqual(normal, optimized)

    def test_generator_dependency_hash_is_fail_closed(self) -> None:
        gateway_relative = GATEWAY_MANIFEST_PATH.relative_to(REPOSITORY_ROOT)
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(root, gateway_relative)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            dependency_path = (
                root
                / manifest["authoring"]["generator"]["dependencies"][0]["path"]
            )
            dependency_path.write_bytes(
                dependency_path.read_bytes() + b"\n# stale helper\n"
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("GENERATOR_DEPENDENCY_STALE", self.codes(report))

    def test_imported_generator_dependency_must_be_declared(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(
                root,
                CURVED_MANIFEST_RELATIVE,
            )
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            manifest["authoring"]["generator"]["dependencies"] = []
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "GENERATOR_DEPENDENCY_UNDECLARED",
            self.codes(report),
        )

    def test_generator_dependency_records_reject_hostile_shapes(self) -> None:
        gateway_relative = GATEWAY_MANIFEST_PATH.relative_to(REPOSITORY_ROOT)
        mutations = (
            lambda manifest: manifest["authoring"]["generator"].update(
                {"dependencies": "not-a-list"}
            ),
            lambda manifest: manifest["authoring"]["generator"].update(
                {"dependencies": [None]}
            ),
            lambda manifest: manifest["authoring"]["generator"].update(
                {
                    "dependencies": [
                        {
                            "path": "../escape.py",
                            "sha256": "0" * 64,
                        }
                    ]
                }
            ),
            lambda manifest: manifest["authoring"]["generator"].update(
                {
                    "dependencies": [
                        {
                            "path": manifest["authoring"]["generator"]["path"],
                            "sha256": manifest["authoring"]["generator"][
                                "sha256"
                            ],
                        }
                    ]
                }
            ),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    root = Path(temporary_directory)
                    manifest_path = self.copy_fixture(
                        root,
                        gateway_relative,
                    )
                    manifest = json.loads(
                        manifest_path.read_text(encoding="utf-8")
                    )
                    mutate(manifest)
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )

                    result, report = self.run_validator(root, manifest_path)

                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "GENERATOR_DEPENDENCY_RECORD",
                    self.codes(report),
                )

    def test_artifact_corruption_is_both_stale_and_invalid(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            glb_path = root / manifest["artifacts"]["glb"]["path"]
            with glb_path.open("ab") as handle:
                handle.write(b"\x00")

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("ARTIFACT_STALE", self.codes(report))
        self.assertIn("GLB_INVALID", self.codes(report))

    def test_scene_extras_are_an_exact_fail_closed_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(
                root,
                TRANSITION_MANIFEST_RELATIVE,
            )
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            glb_path = root / manifest["artifacts"]["glb"]["path"]

            def mutate(document: dict[str, object]) -> None:
                scene = document["scenes"][document["scene"]]
                extras = scene["extras"]
                extras["rorng_stale_asset_metadata"] = True
                extras["rorng_asset_id"] = "rorng_wrong_asset"
                del extras["rorng_units"]

            mutate_glb_document(glb_path, mutate)
            manifest["artifacts"]["glb"]["sha256"] = hashlib.sha256(
                glb_path.read_bytes()
            ).hexdigest()
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        codes = self.codes(report)
        self.assertIn("GLTF_SCENE_EXTRAS_UNKNOWN", codes)
        self.assertIn("GLTF_SCENE_EXTRAS_MISSING", codes)
        self.assertIn("GLTF_SCENE_EXTRAS_VALUE", codes)

    def test_connector_and_lod_contract_mutations_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["connectors"][1]["position_blender_z_up_m"][1] = 11.0
            manifest["geometry"]["lod1_max_ratio"] = 0.01
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("CONNECTOR_LENGTH", self.codes(report))
        self.assertIn("LOD1_RATIO", self.codes(report))

    def test_curve_geometry_and_non_numeric_forward_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(
                root,
                CURVED_MANIFEST_RELATIVE,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["geometry"]["curve_radius_m"] *= 2.0
            manifest["connectors"][0]["forward"][0] = "not-a-number"
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("CURVE_GEOMETRY", self.codes(report))
        self.assertIn("CONNECTOR_FORWARD", self.codes(report))

    def test_material_contract_must_exactly_cover_glb(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["materials"][0]["color_space"] = "srgb"
            manifest["materials"].pop()
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            result, report = self.run_validator(root, manifest_path)

        self.assertEqual(result.returncode, 1)
        self.assertIn("MATERIAL_COLOR_SPACE", self.codes(report))
        self.assertIn("MATERIAL_COVERAGE", self.codes(report))

    def test_runtime_lights_are_bounded_and_fail_closed(self) -> None:
        gateway_relative = GATEWAY_MANIFEST_PATH.relative_to(REPOSITORY_ROOT)
        mutations = (
            (
                lambda manifest: manifest["runtime_lights"]["lights"][0].update(
                    {"range_m": 1000.0}
                ),
                "RUNTIME_LIGHT_RANGE",
            ),
            (
                lambda manifest: manifest["runtime_lights"]["lights"][0].update(
                    {"range_m": "13.0"}
                ),
                "RUNTIME_LIGHT_RANGE",
            ),
            (
                lambda manifest: manifest["runtime_lights"]["lights"][0].update(
                    {"position_blender_z_up_m": ["-3.95", 5.2, 12.6]}
                ),
                "RUNTIME_LIGHT_POSITION",
            ),
            (
                lambda manifest: manifest["runtime_lights"]["lights"][0].update(
                    {"color_linear": [1.0, -0.1, 0.2]}
                ),
                "RUNTIME_LIGHT_COLOR",
            ),
            (
                lambda manifest: manifest["runtime_lights"]["lights"][0].update(
                    {"type": "spot"}
                ),
                "RUNTIME_LIGHT_TYPE",
            ),
            (
                lambda manifest: manifest["runtime_lights"]["lights"][1].update(
                    {"id": manifest["runtime_lights"]["lights"][0]["id"]}
                ),
                "RUNTIME_LIGHT_DUPLICATE",
            ),
        )
        for mutate, expected_code in mutations:
            with self.subTest(expected_code=expected_code):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    root = Path(temporary_directory)
                    manifest_path = self.copy_fixture(
                        root,
                        gateway_relative,
                    )
                    manifest = json.loads(
                        manifest_path.read_text(encoding="utf-8")
                    )
                    mutate(manifest)
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                    result, report = self.run_validator(root, manifest_path)
                self.assertEqual(result.returncode, 1)
                self.assertIn(expected_code, self.codes(report))

    def test_duplicate_keys_and_path_escape_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            manifest_path = self.copy_fixture(root)
            manifest_path.write_text(
                '{"format":"ror-cityworld-asset-v1","format":"duplicate"}\n',
                encoding="utf-8",
            )
            duplicate_result, duplicate_report = self.run_validator(
                root,
                manifest_path,
            )

            manifest_path = self.copy_fixture(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["artifacts"]["glb"]["path"] = "../escape.glb"
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            escape_result, escape_report = self.run_validator(root, manifest_path)

        self.assertEqual(duplicate_result.returncode, 1)
        self.assertIn("MANIFEST_INVALID", self.codes(duplicate_report))
        self.assertEqual(escape_result.returncode, 1)
        self.assertIn("ARTIFACT_PATH", self.codes(escape_report))

    def test_canonical_cli_output_is_byte_stable(self) -> None:
        first = subprocess.run(
            [
                sys.executable,
                str(TOOL_PATH),
                str(MANIFEST_PATH),
                "--repo-root",
                str(REPOSITORY_ROOT),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        second = subprocess.run(
            [
                sys.executable,
                str(TOOL_PATH),
                str(MANIFEST_PATH),
                "--repo-root",
                str(REPOSITORY_ROOT),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(first.returncode, 0)
        self.assertEqual(second.returncode, 0)
        self.assertEqual(first.stdout, second.stdout)
        self.assertNotIn(str(REPOSITORY_ROOT), first.stdout)


if __name__ == "__main__":
    unittest.main()
