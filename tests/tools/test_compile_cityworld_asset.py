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
TRANSITION_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/transition_12m/"
    "rorng_city_bridge_transition_12m.asset.json"
)
TRANSITION_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/transition_12m/compiled"
)
GATEWAY_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/streetscape/gateway_block_40m/"
    "rorng_city_gateway_block_40m.asset.json"
)
GATEWAY_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/streetscape/gateway_block_40m/compiled"
)
STREETLIGHT_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/fixtures/led_streetlight/"
    "rorng_city_led_streetlight.asset.json"
)
STREETLIGHT_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/fixtures/led_streetlight/compiled"
)
BRIDGE_STREETLIGHT_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/"
    "rorng_city_led_streetlight_bridge.asset.json"
)
BRIDGE_STREETLIGHT_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/fixtures/led_streetlight_bridge/compiled"
)
PENGUIN_SEAM_MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/streetscape/penguin_road_seam_12m/"
    "rorng_city_penguin_road_seam_12m.asset.json"
)
PENGUIN_SEAM_COMPILED_PATH = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/streetscape/penguin_road_seam_12m/"
    "compiled"
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

    def test_transition_compiles_with_abutment_connectors_and_lods(self) -> None:
        compiler = self.compiler(TRANSITION_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            TRANSITION_COMPILED_PATH,
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 62724,
                "materials": 11,
                "meshes": 6,
                "primitives": 23,
                "vertices": 56098,
            },
        )
        self.assertEqual(
            compiler.connector_runtime_contract(),
            [
                {
                    "id": "end",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, -6.0],
                    "road_width_m": 8.9,
                },
                {
                    "id": "start",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, 6.0],
                    "road_width_m": 8.9,
                },
            ],
        )
        material = compiler._material_bytes().decode("utf-8")
        self.assertIn("material rorng_transition_concrete", material)
        self.assertIn("material rorng_transition_galvanized_steel", material)

    def test_gateway_block_compiles_with_city_detail_and_emissive_lamps(
        self,
    ) -> None:
        compiler = self.compiler(GATEWAY_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            GATEWAY_COMPILED_PATH,
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 108000,
                "materials": 14,
                "meshes": 6,
                "primitives": 35,
                "vertices": 69221,
            },
        )
        assert compiler.glb is not None
        accessor_references = []
        for mesh in compiler.glb.document["meshes"]:
            for primitive in mesh["primitives"]:
                references = [
                    *primitive["attributes"].values(),
                    primitive["indices"],
                ]
                accessor_references.extend(references)
                positions = compiler.glb.accessor(
                    primitive["attributes"]["POSITION"]
                )
                position_accessor = compiler.glb.document["accessors"][
                    primitive["attributes"]["POSITION"]
                ]
                self.assertEqual(
                    tuple(float(value) for value in position_accessor["min"]),
                    tuple(
                        min(float(position[axis]) for position in positions)
                        for axis in range(3)
                    ),
                )
                self.assertEqual(
                    tuple(float(value) for value in position_accessor["max"]),
                    tuple(
                        max(float(position[axis]) for position in positions)
                        for axis in range(3)
                    ),
                )
                indices = compiler.glb.accessor(primitive["indices"])
                self.assertEqual(
                    {int(value) for value in indices},
                    set(range(len(positions))),
                )
        self.assertEqual(len(accessor_references), 175)
        self.assertEqual(
            len(set(accessor_references)),
            len(accessor_references),
        )
        self.assertEqual(
            compiler.connector_runtime_contract(),
            [
                {
                    "id": "end",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, -20.0],
                    "road_width_m": 8.9,
                },
                {
                    "id": "start",
                    "lane_centres_x_m": [-1.75, 1.75],
                    "position_ogre_y_up_m": [0.0, 0.0, 20.0],
                    "road_width_m": 8.9,
                },
            ],
        )
        material = compiler._material_bytes().decode("utf-8")
        self.assertIn("material rorng_gateway_glass_blue", material)
        self.assertIn("material rorng_gateway_tree_bark", material)
        self.assertIn("material rorng_gateway_leaf_light", material)
        self.assertIn("material rorng_gateway_lamp_emissive", material)
        self.assertIn("      emissive 1 0.69 0.25", material)
        lights = compiler.runtime_light_contract()
        self.assertEqual(len(lights), 8)
        self.assertEqual(
            lights[0],
            {
                "color_linear": [1.0, 0.62, 0.22],
                "id": "rorng_gateway_lamp_left_0",
                "position_ogre_y_up_m": [-3.95, 5.2, 12.6],
                "range_m": 13.0,
                "type": "point",
            },
        )
        self.assertEqual(report["runtime_lights"], lights)
        odef = compiler._odef_bytes().decode("utf-8")
        self.assertEqual(odef.count("\npointlight "), 8)
        self.assertIn(
            "pointlight -3.95, 5.2, 12.6, 0, -1, 0, "
            "1, 0.62, 0.22, 13",
            odef,
        )

    def test_led_streetlight_checked_fixture_package_is_complete(self) -> None:
        compiler = self.compiler(STREETLIGHT_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            STREETLIGHT_COMPILED_PATH,
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 15360,
                "materials": 6,
                "meshes": 4,
                "primitives": 16,
                "vertices": 11815,
            },
        )
        self.assertEqual(compiler.connector_runtime_contract(), [])
        self.assertEqual(compiler.runtime_light_contract(), [])
        self.assertEqual(report["connectors"], [])
        self.assertEqual(report["runtime_lights"], [])

        manifest = json.loads(
            STREETLIGHT_MANIFEST_PATH.read_text(encoding="utf-8")
        )
        self.assertEqual(report["outputs"], manifest["compiled"]["outputs"])
        self.assertEqual(
            [output["role"] for output in report["outputs"]],
            [
                "material-fallback",
                "terrain-object",
                "collision-fixture",
                "render-lod0",
                "render-lod1",
                "render-lod2",
            ],
        )
        self.assertEqual(
            [
                Path(output["path"]).name
                for output in report["outputs"]
                if output["role"].startswith("render-lod")
            ],
            [
                "rorng_city_led_streetlight_lod0.mesh",
                "rorng_city_led_streetlight_lod1.mesh",
                "rorng_city_led_streetlight_lod2.mesh",
            ],
        )
        for output in report["outputs"]:
            path = REPOSITORY_ROOT / output["path"]
            data = path.read_bytes()
            self.assertEqual(len(data), output["size"])
            self.assertEqual(
                COMPILER_MODULE.sha256_bytes(data),
                output["sha256"],
            )

        values = {
            item.path: item.data
            for item in compiler.intermediates()
        }
        lod0 = ET.fromstring(
            values["rorng_city_led_streetlight_lod0.mesh.xml"]
        )
        self.assertEqual(
            [entry.attrib for entry in lod0.find("levelofdetail")],
            [
                {
                    "meshname": "rorng_city_led_streetlight_lod1.mesh",
                    "value": "80",
                },
                {
                    "meshname": "rorng_city_led_streetlight_lod2.mesh",
                    "value": "180",
                },
            ],
        )
        collision = ET.fromstring(
            values[
                "rorng_city_led_streetlight_collision_fixture.mesh.xml"
            ]
        )
        self.assertEqual(len(collision.find("submeshes")), 1)

        material = values[
            "rorng_city_led_streetlight.material"
        ].decode("utf-8")
        self.assertEqual(material.count("\nmaterial rorng_"), 6)
        self.assertIn(
            "material rorng_fixture_galvanized_steel",
            material,
        )
        self.assertIn(
            "material rorng_fixture_powdercoat_graphite",
            material,
        )
        self.assertIn(
            "material rorng_fixture_led_lens_emissive",
            material,
        )
        self.assertIn("      emissive 1 0.72 0.3", material)

        odef = values["rorng_city_led_streetlight.odef"].decode("utf-8")
        self.assertEqual(
            odef,
            "rorng_city_led_streetlight_lod0.mesh\n"
            "1, 1, 1\n"
            "standard\n"
            "\n"
            "beginmesh\n"
            "mesh rorng_city_led_streetlight_collision_fixture.mesh\n"
            "stdfriction concrete\n"
            "endmesh\n"
            "\n"
            "end\n",
        )

    def test_bridge_streetlight_package_is_collisionless_and_lit(self) -> None:
        compiler = self.compiler(BRIDGE_STREETLIGHT_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            BRIDGE_STREETLIGHT_COMPILED_PATH,
        )
        self.assertEqual(
            report["source_stats"],
            {
                "indices": 15228,
                "materials": 4,
                "meshes": 3,
                "primitives": 12,
                "vertices": 11687,
            },
        )
        self.assertEqual(compiler.connector_runtime_contract(), [])
        self.assertEqual(
            compiler.runtime_light_contract(),
            [
                {
                    "color_linear": [1.0, 0.72, 0.3],
                    "id": "rorng_bridge_streetlight_warm",
                    "position_ogre_y_up_m": [0.0, 7.12, -1.58],
                    "range_m": 24.0,
                    "type": "point",
                }
            ],
        )
        self.assertEqual(
            [output["role"] for output in report["outputs"]],
            [
                "material-fallback",
                "terrain-object",
                "render-lod0",
                "render-lod1",
                "render-lod2",
            ],
        )
        values = {
            item.path: item.data
            for item in compiler.intermediates()
        }
        self.assertEqual(
            sorted(
                name
                for name in values
                if name.endswith(".mesh.xml")
            ),
            [
                "rorng_city_led_streetlight_bridge_lod0.mesh.xml",
                "rorng_city_led_streetlight_bridge_lod1.mesh.xml",
                "rorng_city_led_streetlight_bridge_lod2.mesh.xml",
            ],
        )
        odef = values[
            "rorng_city_led_streetlight_bridge.odef"
        ].decode("utf-8")
        self.assertNotIn("beginmesh", odef)
        self.assertNotIn("stdfriction", odef)
        self.assertEqual(odef.splitlines()[2], "standard")
        self.assertEqual(odef.count("\npointlight "), 1)
        self.assertIn(
            "pointlight 0, 7.12, -1.58, 0, -1, 0, "
            "1, 0.72, 0.3, 24",
            odef,
        )

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

    def test_existing_assets_keep_checked_material_bytes_without_parent(
        self,
    ) -> None:
        cases = (
            (MANIFEST_PATH, COMPILED_RELATIVE),
            (CURVED_MANIFEST_PATH, CURVED_COMPILED_PATH),
            (TRANSITION_MANIFEST_PATH, TRANSITION_COMPILED_PATH),
            (GATEWAY_MANIFEST_PATH, GATEWAY_COMPILED_PATH),
            (STREETLIGHT_MANIFEST_PATH, STREETLIGHT_COMPILED_PATH),
            (
                BRIDGE_STREETLIGHT_MANIFEST_PATH,
                BRIDGE_STREETLIGHT_COMPILED_PATH,
            ),
        )
        for manifest_path, compiled_path in cases:
            with self.subTest(manifest=manifest_path.name):
                compiler = self.compiler(manifest_path)
                self.assertFalse(
                    any(
                        "runtime_parent_material" in material
                        for material in compiler.manifest["materials"]
                    )
                )
                self.assertEqual(
                    compiler._material_bytes(),
                    (
                        REPOSITORY_ROOT
                        / compiled_path
                        / f"{compiler.asset_id}.material"
                    ).read_bytes(),
                )

    def test_runtime_parent_material_binds_exact_core_material_in_mesh(
        self,
    ) -> None:
        compiler = self.compiler()
        baseline = compiler._material_bytes()
        declaration = next(
            material
            for material in compiler.manifest["materials"]
            if material["name"] == "rorng_city_asphalt"
        )
        declaration["runtime_parent_material"] = "road2"

        fallback = (
            b"material rorng_city_asphalt\n"
            b"{\n"
            b"  technique\n"
            b"  {\n"
            b"    pass\n"
            b"    {\n"
            b"      ambient 0.032 0.038 0.045 1\n"
            b"      diffuse 0.032 0.038 0.045 1\n"
            b"      specular 0.04 0.04 0.04 1 1.04831581\n"
            b"    }\n"
            b"  }\n"
            b"}\n"
        )
        self.assertEqual(baseline.count(fallback), 1)
        self.assertEqual(
            compiler._material_bytes(),
            baseline.replace(b"\n" + fallback, b"", 1),
        )

        mesh_xml = compiler._mesh_xml(compiler.meshes[0])
        submeshes = ET.fromstring(mesh_xml).find("submeshes")
        self.assertIsNotNone(submeshes)
        assert submeshes is not None
        self.assertIn(
            "road2",
            [submesh.get("material") for submesh in submeshes],
        )
        self.assertNotIn(
            "rorng_city_asphalt",
            [submesh.get("material") for submesh in submeshes],
        )

    def test_runtime_parent_material_compiler_rejects_unvalidated_values(
        self,
    ) -> None:
        compiler = self.compiler()
        declaration = compiler.manifest["materials"][0]
        for runtime_parent in (
            ["road2"],
            None,
            True,
            {"name": "road2"},
            "road3",
            "road2 ",
            "road2\n{ technique { pass {} } }",
        ):
            with self.subTest(runtime_parent=runtime_parent):
                declaration["runtime_parent_material"] = runtime_parent
                with self.assertRaisesRegex(
                    COMPILER_MODULE.CompileFailure,
                    "invalid runtime parent material",
                ):
                    compiler._material_bytes()

    def test_runtime_parent_material_rejects_legacy_checked_compiler(
        self,
    ) -> None:
        compiler = self.compiler()
        compiler.manifest["materials"][0][
            "runtime_parent_material"
        ] = "road2"
        with self.assertRaisesRegex(
            COMPILER_MODULE.CompileFailure,
            "stale (profile metadata|compiler identity)",
        ):
            COMPILER_MODULE.validate_checked_outputs(
                compiler,
                REPOSITORY_ROOT / COMPILED_RELATIVE,
            )

    def test_checked_penguin_seam_lods_bind_core_road2_directly(
        self,
    ) -> None:
        compiler = self.compiler(PENGUIN_SEAM_MANIFEST_PATH)
        report = COMPILER_MODULE.validate_checked_outputs(
            compiler,
            PENGUIN_SEAM_COMPILED_PATH,
        )
        self.assertEqual(
            report["material_model"],
            "ogre-rtss-metallic-roughness-fallback-"
            "with-direct-core-bindings-v1",
        )
        self.assertEqual(
            report["runtime_material_bindings"],
            [
                {
                    "authored_material":
                        "rorng_penguin_seam_asphalt",
                    "runtime_material": "road2",
                }
            ],
        )
        material_path = (
            PENGUIN_SEAM_COMPILED_PATH
            / "rorng_city_penguin_road_seam_12m.material"
        )
        self.assertNotIn(
            b"rorng_penguin_seam_asphalt",
            material_path.read_bytes(),
        )
        render_outputs = [
            output
            for output in report["outputs"]
            if output["role"].startswith("render-lod")
        ]
        self.assertEqual(len(render_outputs), 3)
        for output in render_outputs:
            with self.subTest(role=output["role"]):
                payload = (REPOSITORY_ROOT / output["path"]).read_bytes()
                self.assertIn(b"road2", payload)
                self.assertNotIn(
                    b"rorng_penguin_seam_asphalt",
                    payload,
                )

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
