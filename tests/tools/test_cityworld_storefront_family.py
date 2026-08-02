#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FAMILY_PATH = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/buildings/storefront_family/"
    "rorng_city_storefront_family.v1.json"
)
FAMILY_VALIDATOR = (
    REPOSITORY_ROOT / "tools/validate_cityworld_storefront_family.py"
)
ASSET_VALIDATOR = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
ASSET_COMPILER = REPOSITORY_ROOT / "tools/compile_cityworld_asset.py"
GENERATOR_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/generate_cityworld_storefront_family.py"
)
RETENTION_CONTRACT_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/artifact_retention_contract.py"
)
REPRODUCIBILITY_TOOL_PATH = (
    REPOSITORY_ROOT
    / "tools/compare_cityworld_storefront_reproducibility.py"
)
CLEAN_REPRODUCIBILITY_TOOL_PATH = (
    REPOSITORY_ROOT
    / "tools/verify_cityworld_storefront_clean_reproducibility.py"
)
STOREFRONT_CANONICALIZER_PATH = (
    REPOSITORY_ROOT
    / "tools/blender/cityworld_next/canonicalize_storefront_glb.py"
)


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load test dependency: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RETENTION = load_module("storefront_retention_test", RETENTION_CONTRACT_PATH)
REPRODUCIBILITY = load_module(
    "storefront_reproducibility_test",
    REPRODUCIBILITY_TOOL_PATH,
)
CLEAN_REPRODUCIBILITY = load_module(
    "storefront_clean_reproducibility_test",
    CLEAN_REPRODUCIBILITY_TOOL_PATH,
)
STOREFRONT_CANONICALIZER = load_module(
    "storefront_glb_canonicalizer_test",
    STOREFRONT_CANONICALIZER_PATH,
)


class CityWorldStorefrontFamilyTests(unittest.TestCase):
    @staticmethod
    def family() -> dict[str, object]:
        return json.loads(FAMILY_PATH.read_text(encoding="utf-8"))

    def manifests(self) -> list[Path]:
        return [
            REPOSITORY_ROOT / variant["manifest"]
            for variant in self.family()["variants"]
        ]

    @staticmethod
    def copy_repo_file(source: Path, target_root: Path) -> Path:
        relative = source.relative_to(REPOSITORY_ROOT)
        target = target_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        return target

    @staticmethod
    def blender_executable() -> Path | None:
        configured = os.environ.get("BLENDER_BIN")
        candidates = [
            Path(configured) if configured else None,
            Path(shutil.which("blender")) if shutil.which("blender") else None,
            Path("/Applications/Blender.app/Contents/MacOS/Blender"),
        ]
        return next(
            (
                candidate
                for candidate in candidates
                if candidate is not None
                and candidate.is_file()
                and not candidate.is_symlink()
            ),
            None,
        )

    def run_family(
        self,
        family: Path = FAMILY_PATH,
        *,
        optimized: bool = False,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [
                str(FAMILY_VALIDATOR),
                str(family),
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
        return result, json.loads(result.stdout)

    def test_checked_family_passes_normal_and_optimized_gate(self) -> None:
        expected = {
            "assets": 5,
            "compiled_outputs": 30,
            "errors": 0,
            "placements": 40,
            "styles": 5,
            "valid": True,
        }
        for optimized in (False, True):
            with self.subTest(optimized=optimized):
                result, report = self.run_family(optimized=optimized)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(report["diagnostics"], [])
                self.assertEqual(report["summary"], expected)

    def test_textureless_tangent_and_vertex_lanes_are_canonical(self) -> None:
        def write_fixture(
            path: Path,
            *,
            positions: list[tuple[float, float, float]],
            tangents: list[tuple[float, float, float, float]],
            indices: list[int],
            index_component_type: int = 5123,
            primitive_mode: int | None = None,
        ) -> None:
            normals = [(0.0, 0.0, 1.0)] * len(positions)
            texcoords = [(0.0, 0.0)] * len(positions)
            binary = bytearray()
            views: list[dict[str, int]] = []
            accessors: list[dict[str, object]] = []

            def append(
                values: list[tuple[float | int, ...]],
                component_type: int,
                accessor_type: str,
                target: int,
            ) -> int:
                format_code = "H" if component_type == 5123 else "f"
                while len(binary) % 4:
                    binary.append(0)
                offset = len(binary)
                for value in values:
                    binary.extend(
                        struct.pack("<" + format_code * len(value), *value)
                    )
                views.append(
                    {
                        "buffer": 0,
                        "byteLength": len(binary) - offset,
                        "byteOffset": offset,
                        "target": target,
                    }
                )
                accessor: dict[str, object] = {
                    "bufferView": len(views) - 1,
                    "componentType": component_type,
                    "count": len(values),
                    "type": accessor_type,
                }
                if accessor_type == "VEC3" and values is positions:
                    accessor["min"] = [
                        min(float(value[axis]) for value in values)
                        for axis in range(3)
                    ]
                    accessor["max"] = [
                        max(float(value[axis]) for value in values)
                        for axis in range(3)
                    ]
                accessors.append(accessor)
                return len(accessors) - 1

            attributes = {
                "NORMAL": append(normals, 5126, "VEC3", 34962),
                "POSITION": append(positions, 5126, "VEC3", 34962),
                "TANGENT": append(tangents, 5126, "VEC4", 34962),
                "TEXCOORD_0": append(texcoords, 5126, "VEC2", 34962),
            }
            index_accessor = append(
                [(index,) for index in indices],
                index_component_type,
                "SCALAR",
                34963,
            )
            primitive: dict[str, object] = {
                "attributes": attributes,
                "indices": index_accessor,
            }
            if primitive_mode is not None:
                primitive["mode"] = primitive_mode
            document = {
                "accessors": accessors,
                "asset": {"version": "2.0"},
                "bufferViews": views,
                "buffers": [{"byteLength": len(binary)}],
                "meshes": [
                    {
                        "name": "fixture_lod0_mesh",
                        "primitives": [primitive],
                    }
                ],
            }
            STOREFRONT_CANONICALIZER._write_glb(path, document, bytes(binary))

        with tempfile.TemporaryDirectory(
            prefix="storefront-tangent-canonicalization-"
        ) as directory:
            root = Path(directory)
            left = root / "left.glb"
            right = root / "right.glb"
            position_a = (0.0, 0.0, 0.0)
            position_b = (1.0, 0.0, 0.0)
            position_c = (0.0, 1.0, 0.0)
            write_fixture(
                left,
                positions=[position_a, position_a, position_b, position_c],
                tangents=[
                    (1.0, 0.0, 0.0, 1.0),
                    (0.999, 0.001, 0.0, 1.0),
                    (1.0, 0.0, 0.0, 1.0),
                    (1.0, 0.0, 0.0, -1.0),
                ],
                indices=[0, 1, 2, 0, 2, 3],
            )
            write_fixture(
                right,
                positions=[
                    position_a,
                    position_a,
                    position_a,
                    position_b,
                    position_c,
                ],
                tangents=[
                    (0.0, 1.0, 0.0, 1.0),
                    (0.001, 0.999, 0.0, 1.0),
                    (0.0, 1.0, 0.0, 1.0),
                    (0.0, 1.0, 0.0, 1.0),
                    (0.0, 1.0, 0.0, -1.0),
                ],
                indices=[0, 1, 3, 2, 3, 4],
            )

            STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(left)
            STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(right)
            self.assertEqual(left.read_bytes(), right.read_bytes())
            document, _binary = STOREFRONT_CANONICALIZER._load_glb(left)
            primitive = document["meshes"][0]["primitives"][0]
            self.assertEqual(
                document["accessors"][primitive["attributes"]["POSITION"]][
                    "count"
                ],
                4,
            )
            self.assertEqual(
                document["accessors"][primitive["indices"]]["count"],
                6,
            )

            invalid_float_indices = root / "float-indices.glb"
            write_fixture(
                invalid_float_indices,
                positions=[position_a, position_a, position_b, position_c],
                tangents=[(1.0, 0.0, 0.0, 1.0)] * 4,
                indices=[0, 1, 2, 0, 2, 3],
                index_component_type=5126,
            )
            with self.assertRaisesRegex(RuntimeError, "index accessor profile"):
                STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(
                    invalid_float_indices
                )

            invalid_strip = root / "triangle-strip.glb"
            write_fixture(
                invalid_strip,
                positions=[position_a, position_a, position_b, position_c],
                tangents=[(1.0, 0.0, 0.0, 1.0)] * 4,
                indices=[0, 1, 2, 3],
                primitive_mode=5,
            )
            with self.assertRaisesRegex(RuntimeError, "triangle list"):
                STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(
                    invalid_strip
                )

            invalid_non_finite = root / "non-finite.glb"
            write_fixture(
                invalid_non_finite,
                positions=[
                    (float("nan"), 0.0, 0.0),
                    position_a,
                    position_b,
                    position_c,
                ],
                tangents=[(1.0, 0.0, 0.0, 1.0)] * 4,
                indices=[0, 1, 2, 0, 2, 3],
            )
            with self.assertRaisesRegex(RuntimeError, "non-finite"):
                STOREFRONT_CANONICALIZER.canonicalize_storefront_indices(
                    invalid_non_finite
                )

    def test_assets_and_checked_ogre_packages_pass(self) -> None:
        for manifest in self.manifests():
            with self.subTest(manifest=manifest.name):
                validation = subprocess.run(
                    [
                        sys.executable,
                        str(ASSET_VALIDATOR),
                        str(manifest),
                        "--repo-root",
                        str(REPOSITORY_ROOT),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(
                    validation.returncode,
                    0,
                    validation.stderr or validation.stdout,
                )
                report = json.loads(validation.stdout)
                self.assertEqual(report["summary"]["glb_nodes"], 4)
                self.assertEqual(report["summary"]["glb_materials"], 10)
                self.assertEqual(report["summary"]["runtime_lights"], 0)

                compilation = subprocess.run(
                    [
                        sys.executable,
                        str(ASSET_COMPILER),
                        str(manifest),
                        "--repo-root",
                        str(REPOSITORY_ROOT),
                        "--validate-checked",
                    ],
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
                    {output["role"] for output in compile_report["outputs"]},
                    {
                        "material-fallback",
                        "terrain-object",
                        "collision-fixture",
                        "render-lod0",
                        "render-lod1",
                        "render-lod2",
                    },
                )

    def test_audit_covers_all_high_reuse_storefronts(self) -> None:
        family = self.family()
        audit = family["legacy_audit"]
        counts = {
            item["legacy_object"]: item["placement_count"]
            for item in audit["objects"]
        }
        self.assertEqual(
            counts,
            {
                "store02": 6,
                "store03": 9,
                "store05": 9,
                "store06": 9,
                "store08": 7,
            },
        )
        self.assertEqual(sum(counts.values()), 40)
        self.assertFalse(audit["source_geometry_imported"])
        self.assertFalse(audit["source_materials_imported"])
        self.assertFalse(audit["source_textures_imported"])

    def test_selector_preserves_exact_fit_yaw_and_scale(self) -> None:
        family = self.family()
        variants = {
            item["legacy_object"]: item["asset_id"]
            for item in family["variants"]
        }
        assignments = family["selector"]["assignments"]
        self.assertEqual(
            [item["global_ordinal"] for item in assignments],
            list(range(40)),
        )
        self.assertTrue(
            all(item["variant"] == variants[item["legacy_object"]] for item in assignments)
        )
        self.assertTrue(all(item["uniform_scale"] == 1.0 for item in assignments))
        self.assertTrue(all(item["yaw_offset_degrees"] == 0.0 for item in assignments))
        self.assertTrue(
            all(item["yaw_degrees"] in {0.0, 90.0, 180.0, 270.0} for item in assignments)
        )
        self.assertEqual(
            Counter(item["legacy_object"] for item in assignments),
            Counter(
                {
                    "store02": 6,
                    "store03": 9,
                    "store05": 9,
                    "store06": 9,
                    "store08": 7,
                }
            ),
        )
        self.assertTrue(
            all(
                not ({"position", "location", "x", "y", "z"} & set(item))
                for item in assignments
            )
        )

    def test_buildings_are_grounded_and_lod_budgeted(self) -> None:
        for manifest_path in self.manifests():
            with self.subTest(manifest=manifest_path.name):
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                geometry = manifest["geometry"]
                grounding = manifest["storefront"]["grounding"]
                self.assertEqual(manifest["asset"]["profile"], "static-building-v1")
                self.assertEqual(geometry["ground_plane_z_m"], 0.0)
                self.assertEqual(grounding["foundation_below_ground_m"], 0.0)
                self.assertEqual(grounding["placement_vertical_offset_m"], 0.0)
                self.assertTrue(
                    all(
                        lod["bounds_blender_z_up"]["min"][2] == 0.0
                        for lod in geometry["lods"]
                    )
                )
                self.assertEqual(
                    manifest["collision"]["objects"][0]["bounds_blender_z_up"]["min"][2],
                    0.0,
                )
                lods = {
                    item["lod"]: item["triangles"]
                    for item in geometry["lods"]
                }
                self.assertGreaterEqual(lods[0], 15_000)
                self.assertLessEqual(lods[0], 70_000)
                self.assertLessEqual(lods[1] / lods[0], 0.35)
                self.assertLessEqual(lods[2] / lods[0], 0.12)

    def test_portable_material_policy_has_one_justified_emissive(self) -> None:
        styles = set()
        for manifest_path in self.manifests():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            styles.add(manifest["geometry"]["style_variant"])
            emissive = [
                material
                for material in manifest["materials"]
                if material.get("emissive_factor_linear")
            ]
            self.assertEqual(len(emissive), 1)
            self.assertEqual(
                emissive[0]["emissive_factor_linear"],
                [0.684, 0.3312, 0.0864],
            )
            self.assertEqual(
                manifest["storefront"]["emissive_policy"]["purpose"],
                "selected-occupied-interior-windows-only",
            )
            self.assertEqual(
                manifest["storefront"]["emissive_policy"]["runtime_point_lights"],
                0,
            )
            self.assertEqual(manifest["export"]["textures"], [])
            self.assertEqual(
                manifest["geometry"]["texcoord_policy"],
                "canonical-zero-textureless-v1",
            )
        self.assertEqual(len(styles), 5)

    def test_retention_contract_rejects_real_blender_artifact_tamper(self) -> None:
        manifest_path = self.manifests()[0]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for tampered_role in ("blend", "preview"):
            with (
                self.subTest(tampered_role=tampered_role),
                tempfile.TemporaryDirectory(
                    prefix=f"storefront-retention-{tampered_role}-"
                ) as directory,
            ):
                clean_root = Path(directory)
                copied_manifest = self.copy_repo_file(manifest_path, clean_root)
                expected_paths = {
                    role: self.copy_repo_file(
                        REPOSITORY_ROOT / manifest["artifacts"][role]["path"],
                        clean_root,
                    )
                    for role in ("blend", "glb", "preview")
                }
                checked_document = RETENTION.load_previous_manifest(copied_manifest)
                authenticated = RETENTION.authenticate_retained_artifacts(
                    checked_document,
                    repo_root=clean_root,
                    expected_paths=expected_paths,
                )
                self.assertEqual(
                    authenticated,
                    {
                        role: manifest["artifacts"][role]["sha256"]
                        for role in ("blend", "glb", "preview")
                    },
                )

                manifest_before = copied_manifest.read_bytes()
                with expected_paths[tampered_role].open("ab") as handle:
                    handle.write(b"\nstorefront-tamper-regression\n")
                with self.assertRaisesRegex(
                    RETENTION.ArtifactContractError,
                    f"retained {tampered_role} artifact SHA-256 mismatch",
                ):
                    RETENTION.authenticate_retained_artifacts(
                        checked_document,
                        repo_root=clean_root,
                        expected_paths=expected_paths,
                    )
                self.assertEqual(copied_manifest.read_bytes(), manifest_before)

    def test_comparator_checks_actual_glb_and_compiled_outputs(self) -> None:
        manifest_path = self.manifests()[0]
        manifest_relative = manifest_path.relative_to(REPOSITORY_ROOT).as_posix()
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        records = [
            manifest["artifacts"]["glb"],
            *manifest["compiled"]["outputs"],
        ]
        with (
            tempfile.TemporaryDirectory(prefix="storefront-clean-left-") as left,
            tempfile.TemporaryDirectory(prefix="storefront-clean-right-") as right,
        ):
            roots = (Path(left), Path(right))
            for root in roots:
                self.copy_repo_file(manifest_path, root)
                for record in records:
                    self.copy_repo_file(REPOSITORY_ROOT / record["path"], root)

            report = REPRODUCIBILITY.compare_roots(
                roots[0],
                roots[1],
                [manifest_relative],
            )
            self.assertTrue(report["valid"])
            self.assertEqual(report["outputs"], 7)

            right_manifest_path = roots[1] / manifest_relative
            right_manifest = json.loads(
                right_manifest_path.read_text(encoding="utf-8")
            )
            collision = next(
                output
                for output in right_manifest["compiled"]["outputs"]
                if output["role"] == "collision-fixture"
            )
            collision_path = roots[1] / collision["path"]
            with collision_path.open("ab") as handle:
                handle.write(b"\nclean-room-output-drift\n")
            collision["sha256"] = hashlib.sha256(
                collision_path.read_bytes()
            ).hexdigest()
            collision["size"] = collision_path.stat().st_size
            right_manifest_path.write_text(
                json.dumps(
                    right_manifest,
                    indent=2,
                    ensure_ascii=True,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )

            drift_report = REPRODUCIBILITY.compare_roots(
                roots[0],
                roots[1],
                [manifest_relative],
            )
            self.assertFalse(drift_report["valid"])
            self.assertEqual(
                {
                    (item["asset_id"], item["role"])
                    for item in drift_report["mismatches"]
                },
                {
                    (
                        manifest["asset"]["id"],
                        "collision-fixture",
                    )
                },
            )

    def test_clean_gate_copies_only_authored_toolchain_inputs(self) -> None:
        self.assertEqual(
            CLEAN_REPRODUCIBILITY.DEFAULT_GENERATION_TIMEOUT_SECONDS,
            600,
        )
        self.assertEqual(
            CLEAN_REPRODUCIBILITY.DEFAULT_GENERATION_WORKERS,
            1,
        )
        with tempfile.TemporaryDirectory(
            prefix="storefront-artifact-free-root-"
        ) as directory:
            clean_root = Path(directory)
            CLEAN_REPRODUCIBILITY.prepare_artifact_free_root(
                REPOSITORY_ROOT,
                clean_root,
            )
            actual_files = {
                path.relative_to(clean_root)
                for path in clean_root.rglob("*")
                if path.is_file()
            }
            self.assertEqual(
                actual_files,
                set(CLEAN_REPRODUCIBILITY.AUTHORING_INPUTS),
            )
            self.assertFalse((clean_root / "content-source").exists())
            self.assertFalse((clean_root / "resources").exists())
            for relative in CLEAN_REPRODUCIBILITY.AUTHORING_INPUTS:
                self.assertEqual(
                    hashlib.sha256((clean_root / relative).read_bytes()).digest(),
                    hashlib.sha256(
                        (REPOSITORY_ROOT / relative).read_bytes()
                    ).digest(),
                )

    def test_clean_gate_rejects_seeded_generated_artifacts(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="storefront-seeded-root-"
        ) as directory:
            seeded_root = Path(directory)
            seeded = seeded_root / "resources/nextgen/cityworld/stale.mesh"
            seeded.parent.mkdir(parents=True)
            seeded.write_bytes(b"stale-but-rehashed")
            with self.assertRaisesRegex(
                CLEAN_REPRODUCIBILITY.CleanReproducibilityFailure,
                "not artifact-free",
            ):
                CLEAN_REPRODUCIBILITY.prepare_artifact_free_root(
                    REPOSITORY_ROOT,
                    seeded_root,
                )

    def test_clean_gate_preserves_bounded_glb_failure_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory(
            prefix="storefront-failure-diagnostics-test-"
        ) as directory:
            root = Path(directory)
            left_root = root / "left-root"
            right_root = root / "right-root"
            destination = root / "diagnostics"
            asset_ids = []
            for variant in self.family()["variants"]:
                source_manifest = REPOSITORY_ROOT / variant["manifest"]
                manifest = json.loads(
                    source_manifest.read_text(encoding="utf-8")
                )
                source_glb = (
                    REPOSITORY_ROOT
                    / manifest["artifacts"]["glb"]["path"]
                )
                asset_id = manifest["asset"]["id"]
                asset_ids.append(asset_id)
                for side_root in (left_root, right_root):
                    asset_directory = (
                        side_root
                        / "resources/nextgen/cityworld/buildings/"
                        "storefront_family"
                        / asset_id
                    )
                    asset_directory.mkdir(parents=True)
                    glb = asset_directory / f"{asset_id}.glb"
                    shutil.copyfile(source_glb, glb)

            changed_asset = asset_ids[3]
            changed = (
                right_root
                / "resources/nextgen/cityworld/buildings/storefront_family"
                / changed_asset
                / f"{changed_asset}.glb"
            )
            changed_payload = bytearray(changed.read_bytes())
            changed_payload[-1] ^= 1
            changed.write_bytes(changed_payload)

            report = CLEAN_REPRODUCIBILITY.collect_failure_diagnostics(
                left_root,
                right_root,
                destination,
                allowed_root=root,
                error=RuntimeError("fixture mismatch"),
            )

            self.assertEqual(
                report["format"],
                CLEAN_REPRODUCIBILITY.DIAGNOSTIC_FORMAT,
            )
            self.assertEqual(report["variants"], 5)
            self.assertEqual(report["error"], "fixture mismatch")
            self.assertEqual(
                {
                    path.relative_to(destination).as_posix()
                    for path in destination.rglob("*.glb")
                },
                {
                    f"{side}/{asset_id}.glb"
                    for side in ("left", "right")
                    for asset_id in asset_ids
                },
            )
            changed_record = next(
                item
                for item in report["assets"]
                if item["asset_id"] == changed_asset
            )
            self.assertIsNone(changed_record["json_first_difference"])
            self.assertIsNotNone(changed_record["binary_first_difference"])
            self.assertTrue((destination / "diagnostics.json").is_file())

            diagnostic_error = (
                CLEAN_REPRODUCIBILITY.collect_failure_diagnostics_safely(
                    left_root,
                    right_root,
                    destination,
                    allowed_root=root,
                    error=RuntimeError("primary comparison mismatch"),
                )
            )
            self.assertIsNotNone(diagnostic_error)
            self.assertIn("not empty", diagnostic_error)
            with mock.patch.object(
                CLEAN_REPRODUCIBILITY,
                "collect_failure_diagnostics",
                side_effect=ValueError("unexpected diagnostic failure"),
            ):
                unexpected_diagnostic_error = (
                    CLEAN_REPRODUCIBILITY.collect_failure_diagnostics_safely(
                        left_root,
                        right_root,
                        root / "unexpected-diagnostics",
                        allowed_root=root,
                        error=RuntimeError("primary comparison mismatch"),
                    )
                )
            self.assertIsNotNone(unexpected_diagnostic_error)
            self.assertIn(
                "unexpected diagnostic failure",
                unexpected_diagnostic_error,
            )

            with self.assertRaisesRegex(
                CLEAN_REPRODUCIBILITY.CleanReproducibilityFailure,
                "not empty",
            ):
                CLEAN_REPRODUCIBILITY.collect_failure_diagnostics(
                    left_root,
                    right_root,
                    destination,
                    allowed_root=root,
                    error=RuntimeError("second mismatch"),
                )

            oversized_destination = root / "oversized-diagnostics"
            with changed.open("r+b") as handle:
                handle.truncate(
                    CLEAN_REPRODUCIBILITY.MAX_DIAGNOSTIC_GLB_BYTES + 1
                )
            with self.assertRaisesRegex(
                CLEAN_REPRODUCIBILITY.CleanReproducibilityFailure,
                "outside the bounded profile",
            ):
                CLEAN_REPRODUCIBILITY.collect_failure_diagnostics(
                    left_root,
                    right_root,
                    oversized_destination,
                    allowed_root=root,
                    error=RuntimeError("oversized mismatch"),
                )
            self.assertFalse(oversized_destination.exists())

            linked_parent = root / "linked-diagnostics"
            linked_parent.symlink_to(root.parent, target_is_directory=True)
            with self.assertRaisesRegex(
                CLEAN_REPRODUCIBILITY.CleanReproducibilityFailure,
                "escapes its root|symlinked parent",
            ):
                CLEAN_REPRODUCIBILITY.validate_diagnostics_destination(
                    linked_parent / "failure",
                    root,
                )

            right_source = (
                left_root
                / "resources/nextgen/cityworld/buildings/storefront_family"
            )
            escaped_source = root / "escaped-storefront-family"
            right_source.rename(escaped_source)
            right_source.symlink_to(escaped_source, target_is_directory=True)
            with self.assertRaisesRegex(
                CLEAN_REPRODUCIBILITY.CleanReproducibilityFailure,
                "escapes its clean root",
            ):
                CLEAN_REPRODUCIBILITY.collect_failure_diagnostics(
                    left_root,
                    right_root,
                    root / "escaped-source-diagnostics",
                    allowed_root=root,
                    error=RuntimeError("escaped source mismatch"),
                )

    def test_blender_generator_fails_closed_in_tampered_clean_room(self) -> None:
        blender = self.blender_executable()
        if blender is None:
            self.skipTest("Blender is unavailable")
        version = subprocess.run(
            [str(blender), "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
        if version.returncode != 0 or "Blender 5.2.0 LTS" not in version.stdout:
            self.skipTest("the pinned Blender 5.2.0 LTS is unavailable")

        manifest_path = self.manifests()[0]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="storefront-blender-tamper-") as directory:
            clean_root = Path(directory)
            (clean_root / ".git").mkdir()
            for source in (
                GENERATOR_PATH,
                RETENTION_CONTRACT_PATH,
                GENERATOR_PATH.parent / "generate_bridge_kit.py",
                GENERATOR_PATH.parent / "canonicalize_static_glb.py",
                GENERATOR_PATH.parent / "canonicalize_storefront_glb.py",
                manifest_path,
                *(
                    REPOSITORY_ROOT / manifest["artifacts"][role]["path"]
                    for role in ("blend", "glb", "preview")
                ),
            ):
                self.copy_repo_file(source, clean_root)

            copied_manifest = clean_root / manifest_path.relative_to(REPOSITORY_ROOT)
            copied_blend = clean_root / manifest["artifacts"]["blend"]["path"]
            copied_glb = clean_root / manifest["artifacts"]["glb"]["path"]
            manifest_before = copied_manifest.read_bytes()
            glb_before = copied_glb.read_bytes()
            with copied_blend.open("ab") as handle:
                handle.write(b"\nblender-clean-room-tamper\n")

            result = subprocess.run(
                [
                    str(blender),
                    "--background",
                    "--factory-startup",
                    "--python-exit-code",
                    "1",
                    "--python",
                    str(clean_root / GENERATOR_PATH.relative_to(REPOSITORY_ROOT)),
                    "--",
                    "--output-root",
                    str(clean_root),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(
                "retained blend artifact SHA-256 mismatch",
                result.stdout + result.stderr,
            )
            self.assertEqual(copied_manifest.read_bytes(), manifest_before)
            self.assertEqual(copied_glb.read_bytes(), glb_before)
            self.assertFalse(
                any(clean_root.rglob(".*.candidate.*")),
                "authentication must fail before creating candidate outputs",
            )

    def test_mutated_audit_selector_and_grounding_fail_closed(self) -> None:
        cases = (
            (
                lambda document: document["legacy_audit"]["archive"].update(
                    {"sha256": "0" * 64}
                ),
                "LEGACY_ARCHIVE",
            ),
            (
                lambda document: document["selector"]["assignments"][0].update(
                    {"uniform_scale": 1.1}
                ),
                "SELECTOR_STALE",
            ),
            (
                lambda document: document["placement_target"].update(
                    {"integration_status": "placed"}
                ),
                "PLACEMENT_TARGET",
            ),
        )
        for mutate, expected_code in cases:
            with self.subTest(code=expected_code):
                document = self.family()
                mutate(document)
                with tempfile.NamedTemporaryFile(
                    mode="w",
                    suffix=".json",
                    prefix=".storefront-family-test-",
                    dir=FAMILY_PATH.parent,
                    encoding="utf-8",
                    delete=False,
                ) as temporary:
                    json.dump(document, temporary, ensure_ascii=True, sort_keys=True)
                    temporary.write("\n")
                    temporary_path = Path(temporary.name)
                try:
                    result, report = self.run_family(temporary_path)
                finally:
                    temporary_path.unlink()
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    expected_code,
                    {item["code"] for item in report["diagnostics"]},
                )

    def test_static_building_profile_rejects_corridor_and_subgrade_data(self) -> None:
        manifest_path = self.manifests()[0]
        cases = (
            (
                lambda document: document["geometry"].update(
                    {"road_width_m": 8.9}
                ),
                "BUILDING_GEOMETRY",
            ),
            (
                lambda document: document["collision"]["objects"][0][
                    "bounds_blender_z_up"
                ].update({"min": [-9.83, -9.83, -0.1]}),
                "BUILDING_COLLISION_GROUND",
            ),
        )
        for mutate, expected_code in cases:
            with self.subTest(code=expected_code):
                document = json.loads(manifest_path.read_text(encoding="utf-8"))
                mutate(document)
                with tempfile.NamedTemporaryFile(
                    mode="w",
                    suffix=".asset.json",
                    prefix=".storefront-building-test-",
                    dir=manifest_path.parent,
                    encoding="utf-8",
                    delete=False,
                ) as temporary:
                    json.dump(document, temporary, ensure_ascii=True, sort_keys=True)
                    temporary.write("\n")
                    temporary_path = Path(temporary.name)
                try:
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(ASSET_VALIDATOR),
                            str(temporary_path),
                            "--repo-root",
                            str(REPOSITORY_ROOT),
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                finally:
                    temporary_path.unlink()
                self.assertEqual(result.returncode, 1)
                report = json.loads(result.stdout)
                self.assertIn(
                    expected_code,
                    {item["code"] for item in report["diagnostics"]},
                )

    def test_generator_is_project_authored_and_has_no_archive_dependency(self) -> None:
        source = GENERATOR_PATH.read_text(encoding="utf-8")
        self.assertIn('"external_geometry": False', source)
        self.assertIn('"external_materials": False', source)
        self.assertIn('"external_textures": False', source)
        self.assertNotIn("zipfile", source)
        self.assertNotIn("urllib", source)
        self.assertNotIn("requests", source)
        self.assertNotIn("import random", source)
        self.assertIn("canonicalize_textureless_texcoords", source)


if __name__ == "__main__":
    unittest.main()
