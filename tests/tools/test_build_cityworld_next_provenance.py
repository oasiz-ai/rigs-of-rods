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
TOOL_PATH = REPOSITORY_ROOT / "tools/build_cityworld_next_provenance.py"
ASSET_MANIFEST = (
    REPOSITORY_ROOT
    / "resources/nextgen/cityworld/bridge/"
    "rorng_city_bridge_span_20m.asset.json"
)
PROVENANCE_MANIFEST = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/provenance/cityworld_next.manifest.json"
)
INVENTORY = (
    REPOSITORY_ROOT
    / "content-source/cityworld_next/provenance/cityworld_next.inventory.json"
)
PACKAGE_FILE_COUNT = 53
STREETLIGHT_PACKAGE_PATHS = {
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight.compile.json",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight.material",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight.odef",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight_collision_fixture.mesh",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight_lod0.mesh",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight_lod1.mesh",
    "fixtures/led_streetlight/compiled/"
    "rorng_city_led_streetlight_lod2.mesh",
    "fixtures/led_streetlight/"
    "rorng_city_led_streetlight.asset.json",
    "fixtures/led_streetlight/"
    "rorng_city_led_streetlight.glb",
}


class CityWorldNextProvenanceBuildTests(unittest.TestCase):
    def run_tool(
        self,
        root: Path,
        *extra: str,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(TOOL_PATH),
                "--repo-root",
                str(root),
                *extra,
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def copy_fixture(self, root: Path) -> None:
        asset = json.loads(ASSET_MANIFEST.read_text(encoding="utf-8"))
        paths = {
            ASSET_MANIFEST.relative_to(REPOSITORY_ROOT).as_posix(),
            asset["artifacts"]["blend"]["path"],
            asset["artifacts"]["glb"]["path"],
            asset["authoring"]["generator"]["path"],
            asset["compiled"]["report"]["path"],
        }
        paths.update(output["path"] for output in asset["compiled"]["outputs"])
        report = json.loads(
            (REPOSITORY_ROOT / asset["compiled"]["report"]["path"]).read_text(
                encoding="utf-8"
            )
        )
        paths.add(report["compiler"]["path"])
        for relative in sorted(paths):
            source = REPOSITORY_ROOT / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    def test_checked_in_documents_are_current(self) -> None:
        result = self.run_tool(REPOSITORY_ROOT, "--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {
                "assets": PACKAGE_FILE_COUNT,
                "format": "ror-cityworld-provenance-build-v1",
                "inventory_files": PACKAGE_FILE_COUNT,
                "mode": "check",
            },
        )
        manifest = json.loads(
            PROVENANCE_MANIFEST.read_text(encoding="utf-8")
        )
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        self.assertEqual(len(manifest["assets"]), PACKAGE_FILE_COUNT)
        self.assertEqual(len(inventory["files"]), PACKAGE_FILE_COUNT)
        self.assertTrue(
            STREETLIGHT_PACKAGE_PATHS.issubset(
                asset["path"] for asset in manifest["assets"]
            )
        )
        self.assertTrue(
            STREETLIGHT_PACKAGE_PATHS.issubset(
                item["path"] for item in inventory["files"]
            )
        )

    def test_write_is_deterministic_and_exactly_covers_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.copy_fixture(root)
            first = self.run_tool(root)
            self.assertEqual(first.returncode, 0, first.stderr)
            manifest_path = (
                root
                / "content-source/cityworld_next/provenance/"
                "cityworld_next.manifest.json"
            )
            inventory_path = (
                root
                / "content-source/cityworld_next/provenance/"
                "cityworld_next.inventory.json"
            )
            first_manifest = manifest_path.read_bytes()
            first_inventory = inventory_path.read_bytes()
            second = self.run_tool(root)

            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first_manifest, manifest_path.read_bytes())
            self.assertEqual(first_inventory, inventory_path.read_bytes())
            manifest = json.loads(first_manifest)
            inventory = json.loads(first_inventory)

        self.assertEqual(
            [asset["path"] for asset in manifest["assets"]],
            [
                "bridge/compiled/rorng_city_bridge_span_20m.compile.json",
                "bridge/compiled/rorng_city_bridge_span_20m.material",
                "bridge/compiled/rorng_city_bridge_span_20m.odef",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_barrier_left.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_barrier_right.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_road.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod0.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod1.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod2.mesh",
                "bridge/rorng_city_bridge_span_20m.asset.json",
                "bridge/rorng_city_bridge_span_20m.glb",
            ],
        )
        self.assertEqual(
            [item["path"] for item in inventory["files"]],
            [
                "bridge/compiled/rorng_city_bridge_span_20m.compile.json",
                "bridge/compiled/rorng_city_bridge_span_20m.material",
                "bridge/compiled/rorng_city_bridge_span_20m.odef",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_barrier_left.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_barrier_right.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_collision_road.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod0.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod1.mesh",
                "bridge/compiled/rorng_city_bridge_span_20m_lod2.mesh",
                "bridge/rorng_city_bridge_span_20m.asset.json",
                "bridge/rorng_city_bridge_span_20m.glb",
            ],
        )

    def test_check_rejects_stale_generated_documents(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.copy_fixture(root)
            write = self.run_tool(root)
            self.assertEqual(write.returncode, 0, write.stderr)
            manifest_path = (
                root
                / "content-source/cityworld_next/provenance/"
                "cityworld_next.manifest.json"
            )
            manifest_path.write_text("{}\n", encoding="utf-8")

            result = self.run_tool(root, "--check")

        self.assertEqual(result.returncode, 1)
        self.assertIn("generated provenance is stale", result.stderr)

    def test_unowned_package_file_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.copy_fixture(root)
            unknown = root / "resources/nextgen/cityworld/unknown.bin"
            unknown.write_bytes(b"not declared by an asset manifest")

            result = self.run_tool(root)

        self.assertEqual(result.returncode, 1)
        self.assertIn("package contains unowned files", result.stderr)

    def test_stale_generator_hash_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.copy_fixture(root)
            asset = json.loads(ASSET_MANIFEST.read_text(encoding="utf-8"))
            generator_path = root / asset["authoring"]["generator"]["path"]
            with generator_path.open("a", encoding="utf-8") as handle:
                handle.write("\n# mutation\n")

            result = self.run_tool(root)

        self.assertEqual(result.returncode, 1)
        self.assertIn("stale generator hash", result.stderr)

    def test_generator_dependencies_fail_closed_on_hostile_records(
        self,
    ) -> None:
        cases = (
            ("not-a-list", "invalid generator dependencies"),
            ([None], "invalid generator dependency"),
            (
                [{"path": "../escape.py", "sha256": "0" * 64}],
                "unsafe declared path",
            ),
            ("self", "duplicate generator dependency"),
            (
                [
                    {
                        "path": "tools/compile_cityworld_asset.py",
                        "sha256": "0" * 64,
                    }
                ],
                "stale generator dependency",
            ),
        )
        for dependencies, expected in cases:
            with self.subTest(expected=expected):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    root = Path(temporary_directory)
                    self.copy_fixture(root)
                    manifest_path = (
                        root
                        / ASSET_MANIFEST.relative_to(REPOSITORY_ROOT)
                    )
                    manifest = json.loads(
                        manifest_path.read_text(encoding="utf-8")
                    )
                    if dependencies == "self":
                        generator = manifest["authoring"]["generator"]
                        generator["dependencies"] = [
                            {
                                "path": generator["path"],
                                "sha256": generator["sha256"],
                            }
                        ]
                    else:
                        manifest["authoring"]["generator"][
                            "dependencies"
                        ] = dependencies
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )

                    result = self.run_tool(root)

                self.assertEqual(result.returncode, 1)
                self.assertIn(expected, result.stderr)

    def test_generated_documents_pass_release_provenance_gate(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(REPOSITORY_ROOT / "tools/content_provenance_audit.py"),
                "--manifest",
                str(PROVENANCE_MANIFEST),
                "--inventory",
                str(INVENTORY),
                "--release-gate",
                "--package-root",
                str(REPOSITORY_ROOT / "resources/nextgen/cityworld"),
                "--editable-root",
                str(REPOSITORY_ROOT),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        report = json.loads(result.stdout)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["ok"])
        self.assertEqual(
            report["summary"]["checksum_matched_files"],
            PACKAGE_FILE_COUNT,
        )


if __name__ == "__main__":
    unittest.main()
