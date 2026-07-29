#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"
MANIFEST_RELATIVE = Path(
    "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
MANIFEST_PATH = REPOSITORY_ROOT / MANIFEST_RELATIVE
COMPILED_RELATIVE = Path(
    "resources/nextgen/cityworld/bridge/compiled"
)
CURVED_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/curve_left_15deg/"
    "rorng_city_bridge_curve_left_15deg_20m.asset.json"
)
CURVED_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/curve_left_15deg/compiled"
)

SPEC = importlib.util.spec_from_file_location("compile_cityworld_asset", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
COMPILER_MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = COMPILER_MODULE
SPEC.loader.exec_module(COMPILER_MODULE)


class CityWorldSceneCompilerTests(unittest.TestCase):
    def compiler(self, manifest_path: Path = MANIFEST_PATH) -> object:
        compiler = COMPILER_MODULE.SceneCompiler(
            REPOSITORY_ROOT,
            manifest_path,
        )
        compiler.prepare()
        return compiler

    def intermediates(self) -> dict[str, bytes]:
        return {
            item.path: item.data
            for item in self.compiler().intermediates()
        }

    def test_checked_bridge_compile_passes_offline_gate(self) -> None:
        compiler = self.compiler()
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            REPOSITORY_ROOT / COMPILED_RELATIVE,
        )
        self.assertEqual(
            report["format"],
            "ror-cityworld-scene-compile-report-v1",
        )
        self.assertEqual(
            report["mesh_format"],
            "MeshSerializer_v1.100-little-endian",
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 15060,
                "materials": 7,
                "meshes": 6,
                "primitives": 16,
                "vertices": 12642,
            },
        )
        for output in report["outputs"]:
            path = REPOSITORY_ROOT / output["path"]
            if path.suffix == ".mesh":
                self.assertEqual(
                    path.read_bytes()[: len(COMPILER_MODULE.OGRE_MESH_HEADER)],
                    COMPILER_MODULE.OGRE_MESH_HEADER,
                )

    def test_xml_lowering_preserves_geometry_materials_and_manual_lods(self) -> None:
        values = self.intermediates()
        root = ET.fromstring(
            values["rorng_city_bridge_span_20m_lod0.mesh.xml"]
        )
        submeshes = list(root.find("submeshes"))
        self.assertEqual(len(submeshes), 6)
        self.assertEqual(
            sum(int(submesh.find("faces").get("count")) for submesh in submeshes),
            4636,
        )
        self.assertEqual(
            [submesh.get("material") for submesh in submeshes],
            [
                "rorng_city_concrete",
                "rorng_city_asphalt",
                "rorng_city_galvanized_steel",
                "rorng_city_dark_steel",
                "rorng_city_lane_white",
                "rorng_city_lane_yellow",
            ],
        )
        positions = [
            tuple(float(vertex.find("position").get(axis)) for axis in "xyz")
            for submesh in submeshes
            for vertex in submesh.find("geometry").find("vertexbuffer")
        ]
        self.assertEqual(
            [min(position[axis] for position in positions) for axis in range(3)],
            [-5.0, -1.51500034, -10.0],
        )
        self.assertEqual(
            [max(position[axis] for position in positions) for axis in range(3)],
            [5.0, 1.08500004, 10.0],
        )
        level_of_detail = root.find("levelofdetail")
        self.assertEqual(
            level_of_detail.attrib,
            {
                "manual": "true",
                "numlevels": "3",
                "strategy": "distance_sphere",
            },
        )
        self.assertEqual(
            [entry.attrib for entry in level_of_detail],
            [
                {
                    "meshname": "rorng_city_bridge_span_20m_lod1.mesh",
                    "value": "80",
                },
                {
                    "meshname": "rorng_city_bridge_span_20m_lod2.mesh",
                    "value": "180",
                },
            ],
        )

    def test_basis_connectors_material_and_collision_resources_are_explicit(self) -> None:
        compiler = self.compiler()
        self.assertEqual(
            compiler.connector_runtime_contract(),
            [
                {
                    "id": "end",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, -10.0],
                    "road_width_m": 8.9,
                },
                {
                    "id": "start",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, 10.0],
                    "road_width_m": 8.9,
                },
            ],
        )
        values = {
            item.path: item.data.decode("utf-8")
            for item in compiler.intermediates()
            if not item.path.endswith(".mesh.xml")
        }
        material = values["rorng_city_bridge_span_20m.material"]
        self.assertEqual(material.count("\nmaterial rorng_"), 7)
        self.assertIn("material rorng_city_asphalt", material)
        self.assertIn("material rorng_city_galvanized_steel", material)
        odef = values["rorng_city_bridge_span_20m.odef"]
        self.assertTrue(
            odef.startswith(
                "rorng_city_bridge_span_20m_lod0.mesh\n"
                "1, 1, 1\n"
                "standard\n"
            )
        )
        self.assertEqual(odef.count("beginmesh\n"), 3)
        self.assertEqual(odef.count("stdfriction concrete"), 2)
        self.assertEqual(odef.count("stdfriction asphalt"), 1)

    def test_curved_bridge_compiles_with_connectors_and_emissive_fixture(self) -> None:
        compiler = self.compiler(CURVED_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            CURVED_COMPILED_PATH,
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 25428,
                "materials": 8,
                "meshes": 6,
                "primitives": 17,
                "vertices": 18752,
            },
        )
        self.assertEqual(
            compiler.connector_runtime_contract(),
            [
                {
                    "id": "end",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [
                        0.653564449,
                        0.0,
                        -9.971466573,
                    ],
                    "road_width_m": 8.9,
                },
                {
                    "id": "start",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [
                        0.653564449,
                        0.0,
                        9.971466573,
                    ],
                    "road_width_m": 8.9,
                },
            ],
        )
        material = compiler._material_bytes().decode("utf-8")
        self.assertIn("material rorng_city_lamp_emissive", material)
        self.assertIn("      emissive 1 0.72 0.28", material)

    def test_intermediates_are_byte_deterministic(self) -> None:
        first = self.intermediates()
        second = self.intermediates()
        self.assertEqual(first, second)
        report = json.loads(
            (
                REPOSITORY_ROOT
                / COMPILED_RELATIVE
                / "rorng_city_bridge_span_20m.compile.json"
            ).read_text(encoding="utf-8")
        )
        expected = {
            name: COMPILER_MODULE.sha256_bytes(data)
            for name, data in first.items()
            if name.endswith(".mesh.xml")
        }
        self.assertEqual(report["xml_intermediate_sha256"], expected)

    def test_unapplied_transform_and_unsupported_attribute_fail_closed(self) -> None:
        compiler = self.compiler()
        compiler.glb.document["nodes"][0]["translation"] = [0.0, 0.0, 0.0]
        with self.assertRaisesRegex(
            COMPILER_MODULE.CompileFailure,
            "unapplied transform",
        ):
            compiler._validate_profile()

        compiler = self.compiler()
        node = compiler.glb.node_by_name("rorng_city_bridge_span_20m_lod0")
        mesh = compiler.glb.mesh_for_node(node)
        mesh["primitives"][0]["attributes"]["COLOR_0"] = mesh["primitives"][0][
            "attributes"
        ]["POSITION"]
        with self.assertRaisesRegex(
            COMPILER_MODULE.CompileFailure,
            "attributes must be exactly",
        ):
            compiler._extract_meshes()

    def test_cli_requires_converter_and_is_equivalent_under_optimized_python(self) -> None:
        outputs = []
        for optimized in (False, True):
            command = [sys.executable]
            if optimized:
                command.append("-O")
            command.extend(
                [
                    str(TOOL_PATH),
                    str(MANIFEST_PATH),
                    "--repo-root",
                    str(REPOSITORY_ROOT),
                ]
            )
            result = subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            outputs.append(json.loads(result.stdout))
        self.assertEqual(outputs[0], outputs[1])
        self.assertIn("requires an explicit --converter", outputs[0]["error"])

    def test_checked_output_corruption_fails_without_running_converter(self) -> None:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        paths = {
            MANIFEST_RELATIVE.as_posix(),
            "tools/compile_cityworld_asset.py",
            "tools/validate_cityworld_asset.py",
            manifest["authoring"]["generator"]["path"],
            manifest["artifacts"]["blend"]["path"],
            manifest["artifacts"]["glb"]["path"],
            manifest["artifacts"]["preview"]["path"],
            manifest["compiled"]["report"]["path"],
        }
        paths.update(output["path"] for output in manifest["compiled"]["outputs"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for relative in sorted(paths):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / relative, destination)
            material_path = (
                root
                / COMPILED_RELATIVE
                / "rorng_city_bridge_span_20m.material"
            )
            material_path.write_bytes(material_path.read_bytes() + b"\n")
            result = subprocess.run(
                [
                    sys.executable,
                    str(root / "tools/compile_cityworld_asset.py"),
                    str(root / MANIFEST_RELATIVE),
                    "--repo-root",
                    str(root),
                    "--validate-checked",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertIn("checked output size is stale", report["error"])


if __name__ == "__main__":
    unittest.main()
