#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_RELATIVE = Path("tools/blender/native_render/generate_a1_native_course.py")
UTILITY_RELATIVE = Path("tools/blender/native_render/generate_a0_road_tile.py")
MANIFEST_RELATIVE = Path(
    "content-source/native_render/a1_native_course_60m/rorng_a1_native_course_60m.native.json"
)
GLB_RELATIVE = MANIFEST_RELATIVE.with_suffix("").with_suffix(".glb")
ALIGNMENT_RELATIVE = MANIFEST_RELATIVE.with_suffix("").with_suffix(".alignment.json")
PACKAGE_RELATIVE = Path(
    "resources/nextgen/native/a1_native_course_60m/rorng_a1_native_course_60m.rornative"
)
REPORT_RELATIVE = PACKAGE_RELATIVE.with_suffix(".compile.json")
NATIVE_VALIDATOR = REPOSITORY_ROOT / "tools/validate_native_render_asset.py"
ALIGNMENT_VALIDATOR = REPOSITORY_ROOT / "tools/validate_native_course_alignment.py"
COMPILER = REPOSITORY_ROOT / "tools/compile_native_render_asset.py"
LEDGER = REPOSITORY_ROOT / "doc/nextgen/FORWARD_NATIVE_ASSET_LEDGER.md"
COURSE_DOC = REPOSITORY_ROOT / "doc/nextgen/NATIVE_A1_COURSE_V1.md"

EXPECTED_MANIFEST_SHA256 = "730d8e72867006f58e18b3a510839a1ae8e81eceb517724cdb91e416dffac3f8"
EXPECTED_GLB_SHA256 = "a92c99618d98659bc70d28bbdd943b028bff60f1ef26f6bbddc4a98e34dd9a69"
EXPECTED_COMPOSITION_SHA256 = "0e2a2f9d6b0a0597b2c7e28080e41a37bd8b713b964d87337eb81795efc1bb4a"
EXPECTED_ALIGNMENT_SHA256 = "68ac9757ef2e469a5ca14ea61133ad74763d7abbe6e78a1ce45232d47ddcdfbb"
EXPECTED_PACKAGE_SHA256 = "26148b7b0ceda07eecb133a2fcd51b39785ae38b892ea814a8e0a2459794abba"
EXPECTED_A0_PACKAGE_SHA256 = "226d2450c4a4612d873d15cbc124e2a4bbcc67fe9b2cbded82dcfa21427f62e2"


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_pretty(value: Any) -> str:
    return json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n"


class NativeA1CourseTests(unittest.TestCase):
    maxDiff = None

    def run_tool(self, *arguments: object, cwd: Path = REPOSITORY_ROOT) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, *(str(argument) for argument in arguments)],
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
        )

    def test_checked_source_alignment_and_package_validate(self) -> None:
        native = self.run_tool(
            NATIVE_VALIDATOR,
            MANIFEST_RELATIVE,
            "--repo-root",
            REPOSITORY_ROOT,
        )
        self.assertEqual(native.returncode, 0, native.stdout + native.stderr)
        native_report = json.loads(native.stdout)
        self.assertEqual(native_report["diagnostics"], [])
        self.assertEqual(
            native_report["summary"],
            {
                "diagnostic_count": 0,
                "indices": 1728,
                "instances": 8,
                "materials": 7,
                "meshes": 8,
                "samplers": 2,
                "texture_bytes": 39675196,
                "textures": 19,
                "triangles": 576,
                "valid": True,
                "vertices": 1152,
            },
        )

        alignment = self.run_tool(
            ALIGNMENT_VALIDATOR,
            ALIGNMENT_RELATIVE,
            "--repo-root",
            REPOSITORY_ROOT,
        )
        self.assertEqual(alignment.returncode, 0, alignment.stdout + alignment.stderr)
        alignment_report = json.loads(alignment.stdout)
        self.assertEqual(alignment_report["diagnostics"], [])
        self.assertEqual(
            alignment_report["summary"],
            {"placements": 49, "seams": 4, "surfaces": 6, "valid": True},
        )

        checked = self.run_tool(
            COMPILER,
            MANIFEST_RELATIVE,
            "--repo-root",
            REPOSITORY_ROOT,
            "--validate-checked",
        )
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_exact_hashes_and_a0_remains_unchanged(self) -> None:
        manifest = json.loads((REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8"))
        report = json.loads((REPOSITORY_ROOT / REPORT_RELATIVE).read_text(encoding="utf-8"))
        self.assertEqual(sha256_file(REPOSITORY_ROOT / MANIFEST_RELATIVE), EXPECTED_MANIFEST_SHA256)
        self.assertEqual(sha256_file(REPOSITORY_ROOT / GLB_RELATIVE), EXPECTED_GLB_SHA256)
        self.assertEqual(manifest["source"]["composition"]["sha256"], EXPECTED_COMPOSITION_SHA256)
        self.assertEqual(sha256_file(REPOSITORY_ROOT / ALIGNMENT_RELATIVE), EXPECTED_ALIGNMENT_SHA256)
        self.assertEqual(sha256_file(REPOSITORY_ROOT / PACKAGE_RELATIVE), EXPECTED_PACKAGE_SHA256)
        self.assertEqual(report["source"]["manifest_sha256"], EXPECTED_MANIFEST_SHA256)
        self.assertEqual(report["source"]["glb"]["sha256"], EXPECTED_GLB_SHA256)
        self.assertEqual(report["source"]["composition"]["sha256"], EXPECTED_COMPOSITION_SHA256)
        self.assertEqual(report["output"]["sha256"], EXPECTED_PACKAGE_SHA256)
        a0_package = REPOSITORY_ROOT / "resources/nextgen/native/a0_road_tile_12m/rorng_a0_road_tile_12m.rornative"
        self.assertEqual(sha256_file(a0_package), EXPECTED_A0_PACKAGE_SHA256)

    def test_course_material_texture_and_mip_profile(self) -> None:
        manifest = json.loads((REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8"))
        self.assertEqual(manifest["format"], "ror-native-render-source-v1")
        self.assertEqual(manifest["package"]["id"], "rorng_a1_native_course_60m")
        self.assertEqual(manifest["package"]["dimensions_m"], [13.0, 2.92, 60.0])
        self.assertEqual(
            manifest["claims"],
            {
                "ambient_occlusion": False,
                "collision": False,
                "lods": False,
                "native_terrain": False,
                "visual_only": True,
            },
        )
        materials = {entry["id"]: entry for entry in manifest["materials"]}
        self.assertEqual(len(materials), 7)
        dry = materials["rorng_a1_road_surface_material"]
        wet = materials["rorng_a1_wet_asphalt_material"]
        self.assertEqual(dry["workflow"], "metallic_roughness")
        self.assertEqual(set(dry["textures"]), {"base_color", "metallic_roughness", "normal"})
        self.assertEqual(wet["workflow"], "specular")
        self.assertEqual(wet["roughness_factor"], 0.07)
        self.assertEqual(set(wet["textures"]), {"base_color", "normal", "specular"})

        textures = {entry["id"]: entry for entry in manifest["textures"]}
        self.assertEqual(len(textures), 19)
        for prefix in ("road", "wet"):
            selected = [entry for identifier, entry in textures.items() if identifier.startswith(f"rorng_a1_{prefix}_")]
            self.assertEqual(len(selected), 3)
            for texture in selected:
                self.assertEqual((texture["mips"][0]["width"], texture["mips"][0]["height"]), (1024, 1024))
                self.assertEqual(len(texture["mips"]), 11)
                self.assertEqual((texture["mips"][-1]["width"], texture["mips"][-1]["height"]), (1, 1))
        shoulder = [entry for identifier, entry in textures.items() if identifier.startswith("rorng_a1_shoulder_")]
        self.assertEqual(len(shoulder), 3)
        self.assertTrue(all(len(texture["mips"]) == 10 for texture in shoulder))
        for texture in textures.values():
            for previous, current in zip(texture["mips"], texture["mips"][1:]):
                self.assertEqual(current["width"], max(1, previous["width"] // 2))
                self.assertEqual(current["height"], max(1, previous["height"] // 2))
        self.assertEqual(sum(len(texture["mips"]) for texture in textures.values()), 172)

        meshes = {entry["id"]: entry for entry in manifest["meshes"]}
        self.assertEqual(len(meshes), 8)
        self.assertEqual(meshes["rorng_a1_lane_marking_mesh"]["instance_flags"], [])
        for receiver in ("rorng_a0_road_surface_mesh", "rorng_a1_shoulder_mesh", "rorng_a0_wet_asphalt_mesh"):
            self.assertEqual(meshes[receiver]["instance_flags"], ["receives_shadow", "visible_in_reflections"])
        self.assertEqual(
            meshes["rorng_a0_road_shadow_gate_mesh"]["instance_flags"],
            ["casts_shadow", "receives_shadow", "visible_in_reflections"],
        )

    def test_alignment_is_explicit_and_physics_pending(self) -> None:
        alignment = json.loads((REPOSITORY_ROOT / ALIGNMENT_RELATIVE).read_text(encoding="utf-8"))
        self.assertEqual(alignment["format"], "ror-native-course-alignment-v1")
        self.assertTrue(alignment["visual_only"])
        self.assertEqual(alignment["collision"], {"binding_exists": False, "status": "pending"})
        self.assertEqual(alignment["course"]["length_m"], 60.0)
        self.assertEqual(alignment["course"]["nominal_road_width_m"], 8.0)
        self.assertEqual(alignment["course"]["centerline_m"], [[0.0, 0.0, -30.0], [0.0, 0.0, 30.0]])
        self.assertEqual(
            [entry["id"] for entry in alignment["surfaces"]],
            [
                "curb_left",
                "curb_right",
                "dry_asphalt",
                "shoulder_left",
                "shoulder_right",
                "wet_asphalt_overlay",
            ],
        )
        self.assertTrue(all(entry["collision_binding"] is None and entry["physics_material"] is None for entry in alignment["surfaces"]))
        surfaces = {entry["id"]: entry for entry in alignment["surfaces"]}
        self.assertEqual(surfaces["curb_left"]["polygon_xz_m"], [[-4.15, -30.0], [-4.15, 30.0], [-4.0, 30.0], [-4.0, -30.0]])
        self.assertEqual(surfaces["curb_right"]["polygon_xz_m"], [[4.0, -30.0], [4.0, 30.0], [4.15, 30.0], [4.15, -30.0]])
        self.assertEqual(surfaces["curb_left"]["geometry_y_range_m"], [-0.02, 0.12])
        self.assertEqual(surfaces["curb_right"]["geometry_y_range_m"], [-0.02, 0.12])
        self.assertTrue(all(entry["slope_dy_dx_dy_dz"] == [0.0, 0.0] for entry in alignment["surfaces"]))
        placement_ids = [entry["id"] for entry in alignment["placements"]]
        self.assertEqual(placement_ids, sorted(placement_ids))
        self.assertEqual(len(placement_ids), len(set(placement_ids)))
        self.assertTrue(all(entry["collision_binding"] is None for entry in alignment["placements"]))
        curb_placements = {
            entry["id"]: entry
            for entry in alignment["placements"]
            if entry["category"] == "curb"
        }
        self.assertEqual(set(curb_placements), {"curb_left", "curb_right"})
        self.assertEqual(curb_placements["curb_left"]["surface_id"], "curb_left")
        self.assertEqual(curb_placements["curb_right"]["surface_id"], "curb_right")
        self.assertTrue(
            all(
                entry["surface_id"] is None
                for entry in alignment["placements"]
                if entry["category"] != "curb"
            )
        )
        self.assertEqual(
            {entry["category"] for entry in alignment["placements"]},
            {"barrier", "barrier_post", "calibration_gate", "calibration_marker", "curb", "lane_marking"},
        )
        self.assertEqual([entry["seam_x_m"] for entry in alignment["seams"]], [-4.15, -4.0, 4.0, 4.15])
        self.assertEqual(
            [(entry["left_surface"], entry["right_surface"]) for entry in alignment["seams"]],
            [
                ("shoulder_left", "curb_left"),
                ("curb_left", "dry_asphalt"),
                ("dry_asphalt", "curb_right"),
                ("curb_right", "shoulder_right"),
            ],
        )
        self.assertEqual(
            [(entry["lower_surface_y_m"], entry["upper_surface_y_m"]) for entry in alignment["seams"]],
            [(-0.02, 0.12), (0.0, 0.12), (0.0, 0.12), (-0.02, 0.12)],
        )
        self.assertTrue(all(entry["z_range_m"] == [-30.0, 30.0] for entry in alignment["seams"]))
        self.assertTrue(all(entry["vertical_face_min_y_m"] == -0.02 for entry in alignment["seams"]))
        self.assertTrue(all(entry["vertical_face_max_y_m"] == 0.12 for entry in alignment["seams"]))

    def test_alignment_mutations_fail_closed(self) -> None:
        original = json.loads((REPOSITORY_ROOT / ALIGNMENT_RELATIVE).read_text(encoding="utf-8"))

        def collision_claim(value: dict[str, Any]) -> None:
            value["collision"] = {"binding_exists": True, "status": "ready"}

        def wrong_length(value: dict[str, Any]) -> None:
            value["course"]["length_m"] = 47.0

        def instance_collision(value: dict[str, Any]) -> None:
            value["placements"][0]["collision_binding"] = "ror-physics-body-1"

        def duplicate_placement(value: dict[str, Any]) -> None:
            value["placements"][1]["id"] = value["placements"][0]["id"]

        def missing_coverage(value: dict[str, Any]) -> None:
            value["placements"] = [entry for entry in value["placements"] if entry["category"] != "calibration_gate"]

        def unknown_surface_mesh(value: dict[str, Any]) -> None:
            value["surfaces"][0]["mesh_id"] = "rorng_missing_surface_mesh"

        def unknown_placement_mesh(value: dict[str, Any]) -> None:
            value["placements"][0]["batch_mesh_id"] = "rorng_missing_batch_mesh"

        def package_mismatch(value: dict[str, Any]) -> None:
            value["package_id"] = "rorng_a1_wrong_package"

        def wrong_road_curb_seam(value: dict[str, Any]) -> None:
            value["seams"][1]["seam_x_m"] = -4.075

        def wrong_curb_shoulder_seam(value: dict[str, Any]) -> None:
            value["seams"][3]["seam_x_m"] = 4.075

        def wrong_seam_span(value: dict[str, Any]) -> None:
            value["seams"][0]["z_range_m"] = [-29.5, 29.5]

        def wrong_seam_logical_height(value: dict[str, Any]) -> None:
            value["seams"][2]["lower_surface_y_m"] = -0.02

        def wrong_vertical_face_height(value: dict[str, Any]) -> None:
            value["seams"][2]["vertical_face_min_y_m"] = 0.0

        def wrong_surface_polygon(value: dict[str, Any]) -> None:
            value["surfaces"][0]["polygon_xz_m"][2][0] = -4.01

        def wrong_surface_component(value: dict[str, Any]) -> None:
            value["surfaces"][3]["mesh_component_index"] = 1

        def wrong_curb_geometry_height(value: dict[str, Any]) -> None:
            value["surfaces"][1]["geometry_y_range_m"] = [0.0, 0.12]

        def wrong_surface_slope(value: dict[str, Any]) -> None:
            value["surfaces"][2]["slope_dy_dx_dy_dz"] = [0.01, 0.0]

        def wrong_curb_placement(value: dict[str, Any]) -> None:
            placement = next(entry for entry in value["placements"] if entry["id"] == "curb_left")
            placement["position_m"][0] = -4.05

        def wrong_curb_surface_binding(value: dict[str, Any]) -> None:
            placement = next(entry for entry in value["placements"] if entry["id"] == "curb_right")
            placement["surface_id"] = "curb_left"

        cases = (
            (collision_claim, "COLLISION_NONCLAIM"),
            (wrong_length, "COURSE_LENGTH"),
            (instance_collision, "COLLISION_NONCLAIM"),
            (duplicate_placement, "PLACEMENT_ORDER"),
            (missing_coverage, "PLACEMENT_COVERAGE"),
            (unknown_surface_mesh, "SURFACE_MESH"),
            (unknown_placement_mesh, "PLACEMENT_MESH"),
            (package_mismatch, "PACKAGE_MISMATCH"),
            (wrong_road_curb_seam, "SEAM_POSITION"),
            (wrong_curb_shoulder_seam, "SEAM_POSITION"),
            (wrong_seam_span, "SEAM_GEOMETRY"),
            (wrong_seam_logical_height, "SEAM_HEIGHT"),
            (wrong_vertical_face_height, "SEAM_FACE_HEIGHT"),
            (wrong_surface_polygon, "SURFACE_POLYGON"),
            (wrong_surface_component, "SURFACE_GEOMETRY"),
            (wrong_curb_geometry_height, "SURFACE_GEOMETRY"),
            (wrong_surface_slope, "SURFACE_SLOPE"),
            (wrong_curb_placement, "CURB_PLACEMENT_GEOMETRY"),
            (wrong_curb_surface_binding, "CURB_PLACEMENT_BINDING"),
        )
        for mutate, expected in cases:
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "rorng_a1_native_course_60m.alignment.json"
                shutil.copy2(
                    REPOSITORY_ROOT / MANIFEST_RELATIVE,
                    path.with_name("rorng_a1_native_course_60m.native.json"),
                )
                changed = copy.deepcopy(original)
                mutate(changed)
                path.write_text(canonical_pretty(changed), encoding="ascii")
                result = self.run_tool(ALIGNMENT_VALIDATOR, path, "--repo-root", REPOSITORY_ROOT)
                self.assertNotEqual(result.returncode, 0)
                codes = {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}
                self.assertIn(expected, codes)

    def test_alignment_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "target.alignment.json"
            shutil.copy2(REPOSITORY_ROOT / ALIGNMENT_RELATIVE, target)
            link = root / "rorng_a1_native_course_60m.alignment.json"
            link.symlink_to(target)
            shutil.copy2(
                REPOSITORY_ROOT / MANIFEST_RELATIVE,
                root / "rorng_a1_native_course_60m.native.json",
            )
            result = self.run_tool(ALIGNMENT_VALIDATOR, link, "--repo-root", root)
            self.assertNotEqual(result.returncode, 0)
            codes = {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}
            self.assertIn("SOURCE_INVALID", codes)

    def test_alignment_rejects_mutated_glb_geometry_even_with_matching_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (MANIFEST_RELATIVE, ALIGNMENT_RELATIVE, GLB_RELATIVE):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / relative, destination)

            glb_path = root / GLB_RELATIVE
            glb = bytearray(glb_path.read_bytes())
            json_size = struct.unpack_from("<I", glb, 12)[0]
            binary_offset = 20 + json_size + 8
            document = json.loads(bytes(glb[20 : 20 + json_size]).rstrip(b" \x00"))
            node_index = next(
                index
                for index, node in enumerate(document["nodes"])
                if node["name"] == "rorng_a1_curb_mesh"
            )
            mesh = document["meshes"][document["nodes"][node_index]["mesh"]]
            accessor = document["accessors"][mesh["primitives"][0]["attributes"]["POSITION"]]
            view = document["bufferViews"][accessor["bufferView"]]
            position_offset = (
                binary_offset
                + view.get("byteOffset", 0)
                + accessor.get("byteOffset", 0)
            )
            original_x = struct.unpack_from("<f", glb, position_offset)[0]
            struct.pack_into("<f", glb, position_offset, original_x + 0.03)
            glb_path.write_bytes(glb)

            manifest_path = root / MANIFEST_RELATIVE
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["source"]["glb"]["sha256"] = sha256_file(glb_path)
            manifest_path.write_text(canonical_pretty(manifest), encoding="ascii")

            result = self.run_tool(
                ALIGNMENT_VALIDATOR,
                root / ALIGNMENT_RELATIVE,
                "--repo-root",
                root,
            )
            self.assertNotEqual(result.returncode, 0)
            codes = {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}
            self.assertTrue(
                {
                    "SURFACE_GEOMETRY",
                    "SURFACE_TOPOLOGY",
                    "SEAM_GEOMETRY",
                    "SEAM_FACE_TOPOLOGY",
                }
                & codes,
                result.stdout,
            )

    def run_matching_hash_glb_index_mutation(
        self,
        mesh_name: str,
        mutate: Any,
    ) -> set[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (MANIFEST_RELATIVE, ALIGNMENT_RELATIVE, GLB_RELATIVE):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / relative, destination)
            glb_path = root / GLB_RELATIVE
            glb = bytearray(glb_path.read_bytes())
            json_size = struct.unpack_from("<I", glb, 12)[0]
            binary_offset = 20 + json_size + 8
            document = json.loads(bytes(glb[20 : 20 + json_size]).rstrip(b" \x00"))
            node = next(entry for entry in document["nodes"] if entry["name"] == mesh_name)
            mesh = document["meshes"][node["mesh"]]
            accessor = document["accessors"][mesh["primitives"][0]["indices"]]
            view = document["bufferViews"][accessor["bufferView"]]
            index_offset = binary_offset + view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            format_code = "H" if accessor["componentType"] == 5123 else "I"
            index_struct = struct.Struct("<" + format_code)
            indices = [
                index_struct.unpack_from(glb, index_offset + index * index_struct.size)[0]
                for index in range(accessor["count"])
            ]
            mutate(indices)
            for index, value in enumerate(indices):
                index_struct.pack_into(glb, index_offset + index * index_struct.size, value)
            glb_path.write_bytes(glb)
            manifest_path = root / MANIFEST_RELATIVE
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["source"]["glb"]["sha256"] = sha256_file(glb_path)
            manifest_path.write_text(canonical_pretty(manifest), encoding="ascii")
            result = self.run_tool(
                ALIGNMENT_VALIDATOR,
                root / ALIGNMENT_RELATIVE,
                "--repo-root",
                root,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout)
            return {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}

    def test_alignment_rejects_matching_hash_horizontal_overlap_and_hole(self) -> None:
        def mutate(indices: list[int]) -> None:
            self.assertEqual(indices[:6], [0, 1, 2, 0, 2, 3])
            indices[3:6] = [0, 1, 3]

        codes = self.run_matching_hash_glb_index_mutation(
            "rorng_a1_curb_mesh",
            mutate,
        )
        self.assertIn("SURFACE_TOPOLOGY", codes)

    def test_alignment_rejects_matching_hash_vertical_overlap_and_hole(self) -> None:
        def mutate(indices: list[int]) -> None:
            # The first curb box's +X face is face 4 in the project-owned box
            # profile: six indices per face.
            start = 4 * 6
            base = indices[start]
            self.assertEqual(indices[start : start + 6], [base, base + 1, base + 2, base, base + 2, base + 3])
            indices[start + 3 : start + 6] = [base, base + 1, base + 3]

        codes = self.run_matching_hash_glb_index_mutation(
            "rorng_a1_curb_mesh",
            mutate,
        )
        self.assertIn("SEAM_FACE_TOPOLOGY", codes)

    def test_alignment_rejects_matching_hash_face_winding_flip(self) -> None:
        def mutate(indices: list[int]) -> None:
            indices[1], indices[2] = indices[2], indices[1]

        codes = self.run_matching_hash_glb_index_mutation(
            "rorng_a1_curb_mesh",
            mutate,
        )
        self.assertIn("SURFACE_WINDING", codes)

    def test_alignment_rejects_nonidentity_native_mesh_transform(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            alignment_path = root / "rorng_a1_native_course_60m.alignment.json"
            manifest_path = root / "rorng_a1_native_course_60m.native.json"
            shutil.copy2(REPOSITORY_ROOT / ALIGNMENT_RELATIVE, alignment_path)
            manifest = json.loads((REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8"))
            curb_mesh = next(
                entry
                for entry in manifest["meshes"]
                if entry["id"] == "rorng_a1_curb_mesh"
            )
            curb_mesh["render_from_object"][12] = 0.25
            manifest_path.write_text(canonical_pretty(manifest), encoding="ascii")
            result = self.run_tool(
                ALIGNMENT_VALIDATOR,
                alignment_path,
                "--repo-root",
                REPOSITORY_ROOT,
            )
            self.assertNotEqual(result.returncode, 0)
            codes = {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}
            self.assertIn("NATIVE_MESH_TRANSFORM", codes)

    def test_alignment_rejects_manifest_mesh_to_glb_node_remap(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            alignment_path = root / "rorng_a1_native_course_60m.alignment.json"
            manifest_path = root / "rorng_a1_native_course_60m.native.json"
            shutil.copy2(REPOSITORY_ROOT / ALIGNMENT_RELATIVE, alignment_path)
            manifest = json.loads((REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8"))
            curb_mesh = next(
                entry
                for entry in manifest["meshes"]
                if entry["id"] == "rorng_a1_curb_mesh"
            )
            curb_mesh["node"] = "rorng_a1_shoulder_mesh"
            manifest_path.write_text(canonical_pretty(manifest), encoding="ascii")
            result = self.run_tool(
                ALIGNMENT_VALIDATOR,
                alignment_path,
                "--repo-root",
                REPOSITORY_ROOT,
            )
            self.assertNotEqual(result.returncode, 0)
            codes = {entry["code"] for entry in json.loads(result.stdout)["diagnostics"]}
            self.assertIn("NATIVE_MESH_BINDING", codes)

    def test_generator_and_compiler_are_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in (GENERATOR_RELATIVE, UTILITY_RELATIVE):
                destination = root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPOSITORY_ROOT / relative, destination)
            generated = self.run_tool(root / GENERATOR_RELATIVE, "--repo-root", root, cwd=root)
            self.assertEqual(generated.returncode, 0, generated.stdout + generated.stderr)
            compiled = self.run_tool(COMPILER, root / MANIFEST_RELATIVE, "--repo-root", root, cwd=root)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for relative in (MANIFEST_RELATIVE, GLB_RELATIVE, ALIGNMENT_RELATIVE, PACKAGE_RELATIVE, REPORT_RELATIVE):
                self.assertEqual(
                    sha256_file(root / relative),
                    sha256_file(REPOSITORY_ROOT / relative),
                    relative.as_posix(),
                )

    def test_docs_ledger_and_cmake_pin_both_artifacts_and_nonclaims(self) -> None:
        ledger = LEDGER.read_text(encoding="utf-8")
        documentation = COURSE_DOC.read_text(encoding="utf-8")
        cmake = (REPOSITORY_ROOT / "source/main/CMakeLists.txt").read_text(encoding="utf-8")
        for text in (ledger, documentation):
            self.assertIn("NATIVE-A1-001", text)
            self.assertIn(EXPECTED_MANIFEST_SHA256, text)
            self.assertIn(EXPECTED_GLB_SHA256, text)
            self.assertIn(EXPECTED_ALIGNMENT_SHA256, text)
            self.assertIn(EXPECTED_PACKAGE_SHA256, text)
        for nonclaim in ("visual-only", "collision", "native terrain", "playability"):
            self.assertIn(nonclaim, documentation.lower())
        self.assertIn(EXPECTED_ALIGNMENT_SHA256, cmake)
        self.assertIn(EXPECTED_PACKAGE_SHA256, cmake)
        self.assertIn("ror_native_a1_course_package", cmake)


if __name__ == "__main__":
    unittest.main()
