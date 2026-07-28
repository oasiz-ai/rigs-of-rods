#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/validate_cityworld_asset.py"
MANIFEST_RELATIVE = Path(
    "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
MANIFEST_PATH = REPOSITORY_ROOT / MANIFEST_RELATIVE


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

    def copy_fixture(self, root: Path) -> Path:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        relative_paths = {
            MANIFEST_RELATIVE.as_posix(),
            manifest["artifacts"]["blend"]["path"],
            manifest["artifacts"]["glb"]["path"],
            manifest["artifacts"]["preview"]["path"],
            manifest["authoring"]["generator"]["path"],
        }
        for relative in sorted(relative_paths):
            source = REPOSITORY_ROOT / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        return root / MANIFEST_RELATIVE

    @staticmethod
    def codes(report: dict[str, object]) -> set[str]:
        diagnostics = report["diagnostics"]
        assert isinstance(diagnostics, list)
        return {item["code"] for item in diagnostics}

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
                "triangles": 5020,
                "valid": True,
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
