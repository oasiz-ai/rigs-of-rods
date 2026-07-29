#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import importlib.util
import json
import math
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/build_cityworld_local_overlay.py"

SPEC = importlib.util.spec_from_file_location(
    "build_cityworld_local_overlay",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld local overlay builder")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)

SOURCE_MARKERS = {
    "CityWorld.terrn2": b"SOURCE_TERRAIN_PAYLOAD_MUST_NOT_LEAK",
    "CityWorld.otc": b"SOURCE_GEOMETRY_PAYLOAD_MUST_NOT_LEAK",
    "CityWorld.tobj": b"SOURCE_PLACEMENTS_PAYLOAD_MUST_NOT_LEAK",
}
TERRAIN = """\
[General]
Name = Synthetic pinned CityWorld
GeometryConfig = CityWorld.otc
AmbientColor = 0.93, 0.86, 0.76
StartPosition = 436.5 0.1 446

[Objects]
CityWorld.tobj =

[Teleport]
Telepoint1/Name=Penguinville Spawn
Telepoint1/Position=436.5,0.1,446
Telepoint2/Name=NeoQueretaro Spawn
Telepoint2/Position=2425,0.30000000149,1013

# SOURCE_TERRAIN_PAYLOAD_MUST_NOT_LEAK
"""
PLACEMENTS = """\
// SOURCE_PLACEMENTS_PAYLOAD_MUST_NOT_LEAK
1, 2, 3, 0, 0, 0, source_only_object
"""


class CityWorldLocalOverlayBuilderTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
        *,
        terrain: str = TERRAIN,
        otc_name: str = "CityWorld.otc",
        archive_name: str = "CityWorld.zip",
        extra_entries: tuple[tuple[str, bytes], ...] = (),
    ) -> tuple[Path, str]:
        archive_path = root / archive_name
        with zipfile.ZipFile(
            archive_path,
            "w",
            compression=zipfile.ZIP_STORED,
        ) as archive:
            archive.writestr("CityWorld.terrn2", terrain.encode("utf-8"))
            archive.writestr(
                otc_name,
                SOURCE_MARKERS["CityWorld.otc"],
            )
            archive.writestr("CityWorld.tobj", PLACEMENTS.encode("utf-8"))
            for name, payload in extra_entries:
                archive.writestr(name, payload)
        digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
        return archive_path, digest

    def fake_assets(self) -> tuple[object, ...]:
        lengths = {
            "rorng_city_gateway_block_40m": 40.0,
            "rorng_city_bridge_transition_12m": 12.0,
            "rorng_city_bridge_curve_left_15deg_20m": 20.0,
            "rorng_city_bridge_span_20m": 20.0,
        }
        manifests = {
            profile.asset_id: manifest
            for manifest in BUILDER.ASSET_MANIFESTS
            for profile in (
                BUILDER.load_asset_profile(REPOSITORY_ROOT, manifest),
            )
        }
        assets = []
        for asset_id in dict.fromkeys(BUILDER.MODULE_ASSET_IDS):
            manifest = manifests[asset_id]
            payload = f"project-owned runtime for {asset_id}\n".encode()
            runtime = BUILDER.RuntimeFile(
                package_path=f"{asset_id}.odef",
                repository_path=f"fixtures/{asset_id}.odef",
                role="terrain-object",
                sha256=hashlib.sha256(payload).hexdigest(),
                size=len(payload),
                payload=payload,
            )
            assets.append(
                BUILDER.PreparedAsset(
                    asset_id=asset_id,
                    centerline_length_m=lengths[asset_id],
                    manifest_path=manifest,
                    profile=BUILDER.load_asset_profile(
                        REPOSITORY_ROOT,
                        manifest,
                    ),
                    provenance={
                        "asset": {
                            "id": asset_id,
                            "license": "GPL-3.0-or-later",
                        },
                        "generator": {},
                        "manifest": {
                            "path": manifest,
                            "sha256": "1" * 64,
                        },
                        "runtime_files": [
                            {
                                "package_path": runtime.package_path,
                                "path": runtime.repository_path,
                                "role": runtime.role,
                                "sha256": runtime.sha256,
                                "size": runtime.size,
                            }
                        ],
                    },
                    runtime_files=(runtime,),
                )
            )
        return tuple(assets)

    def build_fixture(
        self,
        root: Path,
        *,
        terrain: str = TERRAIN,
        output_name: str = "CityWorldNextLocalOverlay.zip",
    ) -> tuple[Path, Path, dict[str, object]]:
        archive, digest = self.make_archive(root, terrain=terrain)
        output = root / output_name
        with (
            mock.patch.object(
                BUILDER,
                "PINNED_ARCHIVE_SHA256",
                digest,
            ),
            mock.patch.object(
                BUILDER,
                "prepare_assets",
                return_value=self.fake_assets(),
            ),
        ):
            result = BUILDER.build_local_overlay(
                archive_path=archive,
                repository_path=REPOSITORY_ROOT,
                output_path=output,
                surface_offset_m=0.08,
            )
        return archive, output, result

    def read_report(self, output: Path) -> dict[str, object]:
        with zipfile.ZipFile(output) as archive:
            return json.loads(archive.read(BUILDER.REPORT_NAME))

    def test_checked_project_assets_are_complete_and_current(self) -> None:
        assets = BUILDER.prepare_assets(REPOSITORY_ROOT)
        self.assertEqual(
            [asset.asset_id for asset in assets],
            [
                "rorng_city_gateway_block_40m",
                "rorng_city_bridge_transition_12m",
                "rorng_city_bridge_curve_left_15deg_20m",
                "rorng_city_bridge_span_20m",
            ],
        )
        self.assertTrue(all(len(asset.runtime_files) == 8 for asset in assets))
        self.assertEqual(
            sum(
                len(asset.provenance["runtime_lights"])
                for asset in assets
            ),
            8,
        )

    def test_repeated_builds_are_byte_identical_with_fixed_zip_metadata(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_root = root / "first"
            second_root = root / "second"
            first_root.mkdir()
            second_root.mkdir()
            first_archive, first_output, first_result = self.build_fixture(
                first_root
            )
            second_archive, second_output, second_result = self.build_fixture(
                second_root
            )
            self.assertEqual(
                first_archive.read_bytes(),
                second_archive.read_bytes(),
            )
            self.assertEqual(first_output.read_bytes(), second_output.read_bytes())
            self.assertEqual(
                first_result["output"]["sha256"],
                second_result["output"]["sha256"],
            )
            with zipfile.ZipFile(first_output) as package:
                infos = package.infolist()
                self.assertEqual(
                    [info.filename for info in infos],
                    sorted(info.filename for info in infos),
                )
                self.assertTrue(
                    all(info.date_time == BUILDER.ZIP_TIMESTAMP for info in infos)
                )
                self.assertTrue(
                    all(info.compress_type == zipfile.ZIP_STORED for info in infos)
                )
                self.assertTrue(
                    all(
                        info.external_attr >> 16 == BUILDER.ZIP_MODE
                        for info in infos
                    )
                )

    def test_corridor_order_exact_seams_heading_surface_and_provenance(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, output, result = self.build_fixture(Path(directory))
            report = self.read_report(output)
            corridor = report["corridor"]
            self.assertEqual(
                [module["asset_id"] for module in corridor["modules"]],
                list(BUILDER.MODULE_ASSET_IDS),
            )
            self.assertEqual(len(corridor["modules"]), 9)
            self.assertEqual(
                [module["centerline_length_m"] for module in corridor["modules"]],
                [40.0, 12.0, 20.0, 20.0, 20.0, 20.0, 20.0, 20.0, 20.0],
            )
            self.assertEqual(corridor["covered_centerline_length_m"], 192.0)
            self.assertEqual(corridor["target_distance_m"], 2067.757541396)
            self.assertEqual(
                corridor["heading"]["derivation"],
                "degrees(atan2(destination_x-source_x,destination_z-source_z))",
            )
            self.assertEqual(corridor["heading"]["final_error_degrees"], 0.0)
            self.assertAlmostEqual(
                corridor["heading"]["initial_heading_degrees"],
                corridor["heading"]["target_heading_degrees"]
                - corridor["heading"]["module_heading_change_degrees"],
                places=8,
            )
            self.assertEqual(
                corridor["exit"]["heading_degrees"],
                corridor["heading"]["target_heading_degrees"],
            )
            self.assertTrue(
                all(
                    seam
                    == {
                        "heading_error_degrees": 0.0,
                        "index": index,
                        "position_gap_m": 0.0,
                    }
                    for index, seam in enumerate(corridor["seams"])
                )
            )
            self.assertEqual(
                corridor["surface"],
                {"offset_m": 0.08, "source_y_m": 0.1, "y_m": 0.18},
            )
            self.assertEqual(
                report["source"]["references"],
                {
                    "geometry_config": "CityWorld.otc",
                    "original_placements": "CityWorld.tobj",
                    "overlay_placements": BUILDER.OVERLAY_NAME,
                },
            )
            self.assertEqual(
                len(report["source"]["archive"]["members"]),
                3,
            )
            self.assertEqual(len(report["assets"]), 4)
            self.assertTrue(
                all("manifest" in asset for asset in report["assets"])
            )
            self.assertIn(
                "tools/build_cityworld_local_overlay.py",
                [tool["path"] for tool in report["tools"]],
            )
            with zipfile.ZipFile(output) as package:
                report_payload = package.read(BUILDER.REPORT_NAME)
            self.assertEqual(
                result["report"]["sha256"],
                hashlib.sha256(report_payload).hexdigest(),
            )

    def test_package_references_but_never_copies_original_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive, output, _ = self.build_fixture(Path(directory))
            original_archive = archive.read_bytes()
            with zipfile.ZipFile(output) as package:
                names = set(package.namelist())
                payloads = {
                    name: package.read(name)
                    for name in package.namelist()
                }
            self.assertFalse(set(BUILDER.SOURCE_MEMBERS) & names)
            self.assertNotIn(original_archive, output.read_bytes())
            for marker in SOURCE_MARKERS.values():
                self.assertFalse(
                    any(marker in payload for payload in payloads.values())
                )
            descriptor = payloads[BUILDER.TERRAIN_NAME].decode()
            self.assertIn("GeometryConfig = CityWorld.otc", descriptor)
            self.assertIn("CityWorld.tobj =", descriptor)
            self.assertIn(f"{BUILDER.OVERLAY_NAME} =", descriptor)
            self.assertIn("Redistribution and shipping", descriptor)
            report = json.loads(payloads[BUILDER.REPORT_NAME])
            self.assertEqual(
                report["rights"],
                {
                    "local_only": True,
                    "redistribution_allowed": False,
                    "shipping_allowed": False,
                    "source_archive_copied": False,
                    "source_geometry_copied": False,
                    "source_objects_copied": False,
                    "source_placements_copied": False,
                    "source_textures_copied": False,
                },
            )

    def test_wrong_hash_name_and_member_path_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(root)
            with self.assertRaisesRegex(
                BUILDER.AuditFailure,
                "SHA-256 mismatch",
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=root / "wrong-hash.zip",
                    surface_offset_m=0.08,
                )

            renamed, _ = self.make_archive(
                root,
                archive_name="renamed.zip",
            )
            with self.assertRaisesRegex(BUILDER.OverlayFailure, "named CityWorld"):
                BUILDER.build_local_overlay(
                    archive_path=renamed,
                    repository_path=REPOSITORY_ROOT,
                    output_path=root / "renamed-output.zip",
                    surface_offset_m=0.08,
                )

            archive.unlink()
            misplaced, misplaced_digest = self.make_archive(
                root,
                otc_name="nested/CityWorld.otc",
            )
            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    misplaced_digest,
                ),
                self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    "exact member CityWorld.otc",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=misplaced,
                    repository_path=REPOSITORY_ROOT,
                    output_path=root / "misplaced.zip",
                    surface_offset_m=0.08,
                )
            self.assertEqual(len(digest), 64)

            unsafe, unsafe_digest = self.make_archive(
                root,
                extra_entries=(("../escape.mesh", b"hostile"),),
            )
            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    unsafe_digest,
                ),
                self.assertRaisesRegex(
                    BUILDER.AuditFailure,
                    "unsafe ZIP member path",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=unsafe,
                    repository_path=REPOSITORY_ROOT,
                    output_path=root / "unsafe-member.zip",
                    surface_offset_m=0.08,
                )

    def test_missing_duplicate_and_nonfinite_telepoints_fail_closed(self) -> None:
        missing = TERRAIN.replace(
            "Telepoint2/Name=NeoQueretaro Spawn",
            "Telepoint2/Name=Elsewhere",
        )
        duplicate = TERRAIN.replace(
            "Telepoint2/Name=NeoQueretaro Spawn",
            "Telepoint2/Name=Penguinville Spawn",
        )
        nonfinite = TERRAIN.replace(
            "Telepoint1/Position=436.5,0.1,446",
            "Telepoint1/Position=nan,0.1,446",
        )
        for label, terrain in (
            ("missing", missing),
            ("duplicate", duplicate),
            ("nonfinite", nonfinite),
        ):
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive, digest = self.make_archive(root, terrain=terrain)
                with (
                    mock.patch.object(
                        BUILDER,
                        "PINNED_ARCHIVE_SHA256",
                        digest,
                    ),
                    self.assertRaisesRegex(
                        BUILDER.OverlayFailure,
                        "expected exactly one telepoint",
                    ),
                ):
                    BUILDER.build_local_overlay(
                        archive_path=archive,
                        repository_path=REPOSITORY_ROOT,
                        output_path=root / "result.zip",
                        surface_offset_m=0.08,
                    )
        with self.assertRaisesRegex(BUILDER.OverlayFailure, "finite coordinates"):
            BUILDER.finite_vector3([math.inf, 0.0, 0.0], "hostile")

    def test_unsafe_existing_and_repository_outputs_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(root)
            existing = root / "existing.zip"
            existing.write_bytes(b"preserve me")
            cases = (
                existing,
                root / "not-a-package.txt",
                REPOSITORY_ROOT / "forbidden-local-overlay.zip",
            )
            for output in cases:
                with (
                    self.subTest(output=output),
                    mock.patch.object(
                        BUILDER,
                        "PINNED_ARCHIVE_SHA256",
                        digest,
                    ),
                    self.assertRaises(BUILDER.OverlayFailure),
                ):
                    BUILDER.build_local_overlay(
                        archive_path=archive,
                        repository_path=REPOSITORY_ROOT,
                        output_path=output,
                        surface_offset_m=0.08,
                    )
            self.assertEqual(existing.read_bytes(), b"preserve me")
            self.assertFalse(
                (REPOSITORY_ROOT / "forbidden-local-overlay.zip").exists()
            )
        for path in (
            "../escape",
            "/absolute",
            "bad\\name",
            "bad\nname",
            "é.zip",
            "a//b",
        ):
            with self.subTest(path=path), self.assertRaises(
                BUILDER.OverlayFailure
            ):
                BUILDER.safe_package_path(path)

    def test_surface_offset_is_explicit_finite_and_bounded(self) -> None:
        assets = self.fake_assets()
        source = (436.5, 0.1, 446.0)
        destination = (2425.0, 0.3, 1013.0)
        for value in (
            math.nan,
            math.inf,
            BUILDER.MIN_SURFACE_OFFSET_M - 0.001,
            BUILDER.MAX_SURFACE_OFFSET_M + 0.001,
        ):
            with self.subTest(value=value), self.assertRaises(
                BUILDER.OverlayFailure
            ):
                BUILDER.solve_segment(assets, source, destination, value)

    def test_stale_asset_helper_and_compiled_output_are_rejected(self) -> None:
        for code in ("ARTIFACT_STALE", "GENERATOR_DEPENDENCY_STALE"):
            invalid = {
                "diagnostics": [{"code": code}],
                "format": "ror-cityworld-asset-validation-v1",
                "summary": {"valid": False},
            }
            with (
                self.subTest(code=code),
                mock.patch.object(BUILDER, "Validator") as validator,
                self.assertRaisesRegex(BUILDER.OverlayFailure, code),
            ):
                validator.return_value.validate.return_value = invalid
                BUILDER.prepare_asset(
                    REPOSITORY_ROOT,
                    BUILDER.GATEWAY_MANIFEST,
                )
        with (
            mock.patch.object(
                BUILDER,
                "validate_checked_outputs",
                side_effect=BUILDER.CompileFailure(
                    "checked output hash is stale"
                ),
            ),
            self.assertRaisesRegex(
                BUILDER.CompileFailure,
                "checked output hash is stale",
            ),
        ):
            BUILDER.prepare_asset(
                REPOSITORY_ROOT,
                BUILDER.GATEWAY_MANIFEST,
            )

    def test_duplicate_package_names_and_unsafe_generated_names_fail(self) -> None:
        payloads: dict[str, bytes] = {}
        BUILDER.add_payload(payloads, "Asset.mesh", b"first")
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "duplicate generated package name",
        ):
            BUILDER.add_payload(payloads, "asset.mesh", b"second")
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "unsafe generated package path",
        ):
            BUILDER.add_payload({}, "../escape.mesh", b"bad")

    def test_transaction_cleans_temporary_output_on_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(root)
            output = root / "CityWorldNextLocalOverlay.zip"
            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    digest,
                ),
                mock.patch.object(
                    BUILDER,
                    "prepare_assets",
                    return_value=self.fake_assets(),
                ),
                mock.patch.object(
                    BUILDER,
                    "write_deterministic_zip",
                    side_effect=OSError("injected package failure"),
                ),
                self.assertRaisesRegex(OSError, "injected package failure"),
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=output,
                    surface_offset_m=0.08,
                )
            self.assertFalse(output.exists())
            self.assertEqual(
                list(root.glob(".CityWorldNextLocalOverlay.zip.tmp-*.part")),
                [],
            )

    def test_target_appearing_during_publish_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(root)
            output = root / "CityWorldNextLocalOverlay.zip"
            original_writer = BUILDER.write_deterministic_zip

            def write_then_race(path: Path, payloads: dict[str, bytes]) -> None:
                original_writer(path, payloads)
                output.write_bytes(b"concurrent owner")

            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    digest,
                ),
                mock.patch.object(
                    BUILDER,
                    "prepare_assets",
                    return_value=self.fake_assets(),
                ),
                mock.patch.object(
                    BUILDER,
                    "write_deterministic_zip",
                    side_effect=write_then_race,
                ),
                self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    "output target appeared during the build",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=output,
                    surface_offset_m=0.08,
                )
            self.assertEqual(output.read_bytes(), b"concurrent owner")
            self.assertEqual(
                list(root.glob(".CityWorldNextLocalOverlay.zip.tmp-*.part")),
                [],
            )

    def test_cli_success_is_identical_under_optimized_python(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(root)
            normal_root = root / "normal"
            optimized_root = root / "optimized"
            normal_root.mkdir()
            optimized_root.mkdir()

            def command(output: Path, optimized: bool) -> list[str]:
                code = (
                    "import runpy,sys;"
                    f"ns=runpy.run_path({str(TOOL_PATH)!r});"
                    "g=ns['main'].__globals__;"
                    f"g['PINNED_ARCHIVE_SHA256']={digest!r};"
                    "sys.exit(ns['main'](["
                    f"'--archive',{str(archive)!r},"
                    f"'--repo-root',{str(REPOSITORY_ROOT)!r},"
                    f"'--output',{str(output)!r},"
                    "'--surface-offset-m','0.08']))"
                )
                return [
                    sys.executable,
                    *(["-O"] if optimized else []),
                    "-c",
                    code,
                ]

            normal_output = normal_root / "CityWorldNextLocalOverlay.zip"
            optimized_output = (
                optimized_root / "CityWorldNextLocalOverlay.zip"
            )
            normal = subprocess.run(
                command(normal_output, False),
                check=False,
                capture_output=True,
                text=True,
            )
            optimized = subprocess.run(
                command(optimized_output, True),
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(normal.returncode, 0, normal.stderr)
            self.assertEqual(optimized.returncode, 0, optimized.stderr)
            self.assertEqual(json.loads(normal.stdout), json.loads(optimized.stdout))
            self.assertEqual(
                normal_output.read_bytes(),
                optimized_output.read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
