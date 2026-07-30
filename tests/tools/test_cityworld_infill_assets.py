#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from tools import validate_cityworld_asset as ASSET_VALIDATOR_MODULE


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FAMILY_PATH = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/regional_infill/"
    "rorng_city_regional_infill_family.v1.json"
)
ASSET_VALIDATOR = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
ASSET_COMPILER = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"

EXPECTED_VARIANTS = {
    "rorng_city_infill_farmstead_98x86": ("farmland", [4696, 372, 132]),
    "rorng_city_infill_suburb_block_96x88": ("suburb", [8976, 560, 292]),
    "rorng_city_infill_service_station_90x65": (
        "service-station",
        [4120, 232, 140],
    ),
    "rorng_city_infill_red_mesa_19m": (
        "natural-landmark",
        [4552, 348, 36],
    ),
    "rorng_city_infill_arroyo_oasis_19m": (
        "natural-landmark",
        [7892, 560, 112],
    ),
}
EXPECTED_RUNTIME_LIGHTS = {
    "rorng_city_infill_farmstead_98x86": 0,
    "rorng_city_infill_suburb_block_96x88": 0,
    "rorng_city_infill_service_station_90x65": 6,
    "rorng_city_infill_red_mesa_19m": 0,
    "rorng_city_infill_arroyo_oasis_19m": 0,
}
EXPECTED_COMPONENT_IDS = {
    "rorng_city_infill_farmstead_98x86": ("farmhouse",),
    "rorng_city_infill_suburb_block_96x88": (
        *(f"house-{index:02d}" for index in range(6)),
        "west-perimeter-wall",
        "east-perimeter-wall",
    ),
    "rorng_city_infill_service_station_90x65": (
        "market",
        "canopy",
        *(f"canopy-column-{index:02d}" for index in range(4)),
        *(f"fuel-pump-{index:02d}" for index in range(6)),
        "price-pylon",
        *(f"ev-charger-{index:02d}" for index in range(4)),
    ),
    "rorng_city_infill_red_mesa_19m": ("mesa",),
    "rorng_city_infill_arroyo_oasis_19m": ("palm-trunk",),
}
EXPECTED_RUNTIME_ROLES = {
    "collision-fixture",
    "material-fallback",
    "render-lod0",
    "render-lod1",
    "render-lod2",
    "terrain-object",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def collision_geometry(
    glb_path: Path,
    node_name: str,
) -> tuple[object, ...]:
    glb = ASSET_VALIDATOR_MODULE.Glb.read(glb_path)
    node = glb.node_by_name(node_name)
    if node is None:
        raise AssertionError(f"missing collision node: {node_name}")
    primitive = glb.mesh_for_node(node)["primitives"][0]
    positions = glb.accessor(primitive["attributes"]["POSITION"])
    triangles = glb.primitive_triangles(primitive)
    keys = [
        ASSET_VALIDATOR_MODULE.Validator.welded_key(tuple(position))
        for position in positions
    ]
    key_to_triangles: dict[tuple[int, int, int], set[int]] = defaultdict(set)
    for triangle_index, triangle in enumerate(triangles):
        for vertex_index in triangle:
            key_to_triangles[keys[vertex_index]].add(triangle_index)
    remaining = set(range(len(triangles)))
    components = []
    while remaining:
        seen = {next(iter(remaining))}
        pending = list(seen)
        while pending:
            triangle_index = pending.pop()
            for vertex_index in triangles[triangle_index]:
                for neighbour in key_to_triangles[keys[vertex_index]]:
                    if neighbour not in seen:
                        seen.add(neighbour)
                        pending.append(neighbour)
        remaining.difference_update(seen)
        components.append(tuple(sorted(seen)))

    data = bytearray(glb_path.read_bytes())
    offset = 12
    while offset < len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        if chunk_type == ASSET_VALIDATOR_MODULE.GLB_BIN_CHUNK:
            binary_offset = offset + 8
            break
        offset += 8 + chunk_length
    else:
        raise AssertionError("GLB has no BIN chunk")
    return (
        glb,
        primitive,
        positions,
        triangles,
        components,
        data,
        binary_offset,
    )


def accessor_layout(
    glb: object,
    accessor_index: int,
    binary_offset: int,
) -> tuple[str, int, int]:
    accessor = glb.document["accessors"][accessor_index]
    view = glb.document["bufferViews"][accessor["bufferView"]]
    format_code, component_size = ASSET_VALIDATOR_MODULE.COMPONENT_TYPES[
        accessor["componentType"]
    ]
    width = ASSET_VALIDATOR_MODULE.ACCESSOR_WIDTHS[accessor["type"]]
    stride = view.get("byteStride", component_size * width)
    start = (
        binary_offset
        + view.get("byteOffset", 0)
        + accessor.get("byteOffset", 0)
    )
    return format_code, stride, start


def invert_one_collision_component(glb_path: Path, node_name: str) -> None:
    (
        glb,
        primitive,
        positions,
        triangles,
        components,
        data,
        binary_offset,
    ) = collision_geometry(glb_path, node_name)

    def signed_volume(component: tuple[int, ...]) -> float:
        return sum(
            ASSET_VALIDATOR_MODULE.vector_dot(
                positions[triangles[index][0]],
                ASSET_VALIDATOR_MODULE.vector_cross(
                    positions[triangles[index][1]],
                    positions[triangles[index][2]],
                ),
            )
            for index in component
        ) / 6.0

    target = min(
        components,
        key=lambda component: (abs(signed_volume(component)), component[0]),
    )
    format_code, stride, start = accessor_layout(
        glb,
        primitive["indices"],
        binary_offset,
    )
    for triangle_index in target:
        second = start + (triangle_index * 3 + 1) * stride
        third = start + (triangle_index * 3 + 2) * stride
        second_value = struct.unpack_from(
            "<" + format_code,
            data,
            second,
        )[0]
        third_value = struct.unpack_from(
            "<" + format_code,
            data,
            third,
        )[0]
        struct.pack_into("<" + format_code, data, second, third_value)
        struct.pack_into("<" + format_code, data, third, second_value)
    glb_path.write_bytes(data)


def overlap_one_collision_component(glb_path: Path, node_name: str) -> None:
    (
        glb,
        primitive,
        positions,
        triangles,
        components,
        data,
        binary_offset,
    ) = collision_geometry(glb_path, node_name)

    def vertices(component: tuple[int, ...]) -> set[int]:
        return {
            vertex_index
            for triangle_index in component
            for vertex_index in triangles[triangle_index]
        }

    target = max(
        components,
        key=lambda component: sum(
            positions[index][0] for index in vertices(component)
        ) / len(vertices(component)),
    )
    format_code, stride, start = accessor_layout(
        glb,
        primitive["attributes"]["POSITION"],
        binary_offset,
    )
    if format_code != "f":
        raise AssertionError("collision positions are not float32")
    for vertex_index in vertices(target):
        position = start + vertex_index * stride
        x = struct.unpack_from("<f", data, position)[0]
        struct.pack_into("<f", data, position, x - 94.5)
    glb_path.write_bytes(data)


def deform_one_collision_component(glb_path: Path, node_name: str) -> None:
    (
        glb,
        primitive,
        positions,
        triangles,
        components,
        data,
        binary_offset,
    ) = collision_geometry(glb_path, node_name)
    target = components[0]
    target_vertices = {
        vertex_index
        for triangle_index in target
        for vertex_index in triangles[triangle_index]
    }
    keys = [
        ASSET_VALIDATOR_MODULE.Validator.welded_key(tuple(position))
        for position in positions
    ]
    target_key = min(keys[index] for index in target_vertices)
    format_code, stride, start = accessor_layout(
        glb,
        primitive["attributes"]["POSITION"],
        binary_offset,
    )
    if format_code != "f":
        raise AssertionError("collision positions are not float32")
    for vertex_index in target_vertices:
        if keys[vertex_index] != target_key:
            continue
        position = start + vertex_index * stride
        x = struct.unpack_from("<f", data, position)[0]
        struct.pack_into("<f", data, position, x + 0.25)
    glb_path.write_bytes(data)


class CityWorldInfillAssetTests(unittest.TestCase):
    @staticmethod
    def family() -> dict[str, object]:
        return json.loads(FAMILY_PATH.read_text(encoding="utf-8"))

    def variants(self) -> list[tuple[dict[str, object], Path]]:
        return [
            (variant, REPOSITORY_ROOT / variant["manifest"])
            for variant in self.family()["variants"]
        ]

    def test_family_is_project_authored_and_pins_the_generator(self) -> None:
        family = self.family()
        self.assertEqual(
            family["format"],
            "ror-cityworld-regional-infill-family-v1",
        )
        self.assertEqual(
            family["asset"],
            {
                "author": "Oasiz AI and Rigs of Rods contributors",
                "id": "rorng_city_regional_infill_family",
                "license": "GPL-3.0-or-later",
                "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
                "version": 1,
            },
        )
        provenance = family["authoring"]["procedural_provenance"]
        self.assertEqual(
            provenance,
            {
                "external_geometry": False,
                "external_materials": False,
                "external_textures": False,
                "method": "deterministic-project-authored-blender-python",
                "rights_basis": "GPL-3.0-or-later project-authored source",
            },
        )
        generator = family["authoring"]["generator"]
        generator_path = REPOSITORY_ROOT / generator["path"]
        self.assertEqual(
            generator["format"],
            "ror-cityworld-regional-infill-generator-v1",
        )
        self.assertEqual(generator["sha256"], sha256(generator_path))
        self.assertEqual(
            family["placement_target"],
            {
                "integration_status": "asset-ready-overlay-v7",
                "source_archive_sha256":
                    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3",
            },
        )

    def test_family_has_the_exact_five_portable_variants(self) -> None:
        family = self.family()
        observed = {
            variant["asset_id"]: (
                variant["category"],
                variant["lod_triangles"],
            )
            for variant in family["variants"]
        }
        self.assertEqual(observed, EXPECTED_VARIANTS)
        for variant, manifest_path in self.variants():
            with self.subTest(asset=variant["asset_id"]):
                self.assertTrue(manifest_path.is_file())
                self.assertFalse(manifest_path.is_symlink())
                self.assertEqual(
                    manifest_path.relative_to(REPOSITORY_ROOT).as_posix(),
                    variant["manifest"],
                )

    def test_assets_and_checked_ogre_packages_pass_normal_and_optimized(
        self,
    ) -> None:
        for variant, manifest_path in self.variants():
            asset_id = variant["asset_id"]
            for optimized in (False, True):
                with self.subTest(asset=asset_id, optimized=optimized):
                    command = [sys.executable]
                    if optimized:
                        command.append("-O")
                    command.extend(
                        [
                            str(ASSET_VALIDATOR),
                            str(manifest_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                        ]
                    )
                    validation = subprocess.run(
                        command,
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(
                        validation.returncode,
                        0,
                        validation.stderr or validation.stdout,
                    )
                    validation_report = json.loads(validation.stdout)
                    self.assertTrue(validation_report["summary"]["valid"])
                    self.assertEqual(
                        validation_report["summary"]["runtime_lights"],
                        EXPECTED_RUNTIME_LIGHTS[asset_id],
                    )

                    command = [sys.executable]
                    if optimized:
                        command.append("-O")
                    command.extend(
                        [
                            str(ASSET_COMPILER),
                            str(manifest_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                            "--validate-checked",
                        ]
                    )
                    compilation = subprocess.run(
                        command,
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(
                        compilation.returncode,
                        0,
                        compilation.stderr or compilation.stdout,
                    )
                    compile_report = json.loads(compilation.stdout)
                    self.assertEqual(
                        {
                            output["role"]
                            for output in compile_report["outputs"]
                        },
                        EXPECTED_RUNTIME_ROLES,
                    )
                    self.assertEqual(
                        len(compile_report["runtime_lights"]),
                        EXPECTED_RUNTIME_LIGHTS[asset_id],
                    )

    def test_geometry_is_grounded_lod_budgeted_and_texture_free(self) -> None:
        for variant, manifest_path in self.variants():
            with self.subTest(asset=variant["asset_id"]):
                manifest = json.loads(
                    manifest_path.read_text(encoding="utf-8")
                )
                self.assertEqual(
                    manifest["geometry"]["asset_family"],
                    "rorng_city_regional_infill_family",
                )
                lods = manifest["geometry"]["lods"]
                triangles = [lod["triangles"] for lod in lods]
                self.assertEqual(triangles, variant["lod_triangles"])
                self.assertGreater(triangles[0], triangles[1])
                self.assertGreater(triangles[1], triangles[2])
                self.assertLessEqual(
                    triangles[0],
                    manifest["geometry"]["lod0_triangle_ceiling"],
                )
                self.assertEqual(
                    manifest["geometry"]["texcoord_policy"],
                    "canonical-zero-textureless-v1",
                )
                self.assertFalse(
                    any(
                        key.startswith("base_color_texture")
                        or key.startswith("normal_texture")
                        for material in manifest["materials"]
                        for key in material
                    )
                )
                self.assertTrue(
                    all(
                        lod["bounds_blender_z_up"]["min"][2] >= -0.15
                        for lod in lods
                    )
                )
                collision = manifest["collision"]["objects"][0]
                asset_id = variant["asset_id"]
                self.assertEqual(
                    manifest["collision"]["components_format"],
                    "ror-cityworld-collision-components-v1",
                )
                self.assertEqual(
                    tuple(
                        component["component_id"]
                        for component in manifest["collision"]["components"]
                    ),
                    EXPECTED_COMPONENT_IDS[asset_id],
                )
                self.assertTrue(
                    all(
                        component["triangles"] == 12
                        for component in manifest["collision"]["components"]
                    )
                )
                expected_components = (
                    8
                    if asset_id
                    == "rorng_city_infill_suburb_block_96x88"
                    else 17
                    if asset_id
                    == "rorng_city_infill_service_station_90x65"
                    else 1
                )
                self.assertEqual(
                    manifest["collision"]["profile"],
                    (
                        "compound-watertight-proxy-v1"
                        if expected_components > 1
                        else "single-watertight-proxy-v1"
                    ),
                )
                self.assertEqual(
                    collision["topology"]["connected_components"],
                    expected_components,
                )
                self.assertTrue(collision["topology"]["watertight"])
                self.assertTrue(collision["topology"]["outward_winding"])
                self.assertEqual(
                    collision["topology"]["intersecting_faces"],
                    0,
                )

    def test_compound_collision_and_foundation_mutations_fail_closed(
        self,
    ) -> None:
        suburb_path = next(
            manifest_path
            for variant, manifest_path in self.variants()
            if variant["asset_id"]
            == "rorng_city_infill_suburb_block_96x88"
        )
        source = json.loads(suburb_path.read_text(encoding="utf-8"))
        mutations = (
            (
                "component-format",
                lambda manifest: manifest["collision"].__setitem__(
                    "components_format",
                    "ror-cityworld-collision-components-v0",
                ),
                "COLLISION_COMPONENT_FORMAT",
            ),
            (
                "component-id",
                lambda manifest: manifest["collision"]["components"][0]
                .__setitem__("component_id", "wrong-house"),
                "COLLISION_COMPONENT_EXTRAS",
            ),
            (
                "component-bounds",
                lambda manifest: manifest["collision"]["components"][0][
                    "bounds_blender_z_up"
                ]["min"].__setitem__(0, -38.9),
                "COLLISION_COMPONENT_BOUNDS",
            ),
            (
                "component-triangles",
                lambda manifest: manifest["collision"]["components"][0]
                .__setitem__("triangles", 11),
                "COLLISION_COMPONENT_TRIANGLES",
            ),
            (
                "collision-components",
                lambda manifest: manifest["collision"]["objects"][0][
                    "topology"
                ].__setitem__("connected_components", 7),
                "COLLISION_COMPONENTS",
            ),
            (
                "foundation-recess",
                lambda manifest: manifest["geometry"].__setitem__(
                    "foundation_recess_m",
                    0.6,
                ),
                "BUILDING_FOUNDATION_RECESS",
            ),
            (
                "render-footprint",
                lambda manifest: manifest["geometry"].__setitem__(
                    "footprint_m",
                    [90.0, 88.0],
                ),
                "BUILDING_RENDER_FOOTPRINT",
            ),
        )
        with tempfile.TemporaryDirectory(
            prefix=".cityworld-infill-validator-",
            dir=REPOSITORY_ROOT,
        ) as temporary:
            temporary_root = Path(temporary)
            for name, mutate, expected_code in mutations:
                for optimized in (False, True):
                    with self.subTest(
                        mutation=name,
                        optimized=optimized,
                    ):
                        manifest = json.loads(json.dumps(source))
                        mutate(manifest)
                        candidate = temporary_root / f"{name}.asset.json"
                        candidate.write_text(
                            json.dumps(
                                manifest,
                                ensure_ascii=True,
                                sort_keys=True,
                            )
                            + "\n",
                            encoding="utf-8",
                        )
                        command = [sys.executable]
                        if optimized:
                            command.append("-O")
                        command.extend(
                            [
                                str(ASSET_VALIDATOR),
                                str(candidate),
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
                        self.assertEqual(
                            result.returncode,
                            1,
                            result.stderr or result.stdout,
                        )
                        report = json.loads(result.stdout)
                        self.assertIn(
                            expected_code,
                            {
                                diagnostic["code"]
                                for diagnostic in report["diagnostics"]
                            },
                        )

    def test_corrupt_compound_collision_geometry_fails_closed(self) -> None:
        suburb_path = next(
            manifest_path
            for variant, manifest_path in self.variants()
            if variant["asset_id"]
            == "rorng_city_infill_suburb_block_96x88"
        )
        source = json.loads(suburb_path.read_text(encoding="utf-8"))
        source_glb = REPOSITORY_ROOT / source["artifacts"]["glb"]["path"]
        node_name = source["collision"]["objects"][0]["name"]
        mutations = (
            ("inverted-component", invert_one_collision_component,
             "COLLISION_WINDING"),
            ("overlapping-components", overlap_one_collision_component,
             "COLLISION_INTERSECTION"),
            ("non-cuboid-component", deform_one_collision_component,
             "COLLISION_COMPONENT_SHAPE"),
        )
        with tempfile.TemporaryDirectory(
            prefix=".cityworld-infill-collision-geometry-",
            dir=REPOSITORY_ROOT,
        ) as temporary:
            temporary_root = Path(temporary)
            for name, mutate, expected_code in mutations:
                candidate_glb = temporary_root / f"{name}.glb"
                shutil.copyfile(source_glb, candidate_glb)
                mutate(candidate_glb, node_name)
                manifest = json.loads(json.dumps(source))
                manifest["artifacts"]["glb"] = {
                    "path": candidate_glb.relative_to(
                        REPOSITORY_ROOT
                    ).as_posix(),
                    "sha256": sha256(candidate_glb),
                }
                candidate_manifest = temporary_root / f"{name}.asset.json"
                candidate_manifest.write_text(
                    json.dumps(
                        manifest,
                        ensure_ascii=True,
                        sort_keys=True,
                    )
                    + "\n",
                    encoding="utf-8",
                )
                for optimized in (False, True):
                    with self.subTest(
                        mutation=name,
                        optimized=optimized,
                    ):
                        command = [sys.executable]
                        if optimized:
                            command.append("-O")
                        command.extend(
                            [
                                str(ASSET_VALIDATOR),
                                str(candidate_manifest),
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
                        self.assertEqual(
                            result.returncode,
                            1,
                            result.stderr or result.stdout,
                        )
                        report = json.loads(result.stdout)
                        self.assertIn(
                            expected_code,
                            {
                                diagnostic["code"]
                                for diagnostic in report["diagnostics"]
                            },
                        )


if __name__ == "__main__":
    unittest.main()
