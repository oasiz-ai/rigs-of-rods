#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/audit_cityworld_visuals.py"

SPEC = importlib.util.spec_from_file_location(
    "audit_cityworld_visuals", TOOL_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld visual auditor")
AUDITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDITOR)

TERRAIN = """\
[General]
Name = Synthetic CityWorld
GUID = synthetic-cityworld
AmbientColor = 0.5, 0.6, 0.7
StartPosition = 10 2 20

[Teleport]
Telepoint1/Name=West City
Telepoint1/Position=0,0,0
Telepoint2/Name=East City
Telepoint2/Position=1000,0,0
"""

PLACEMENTS = """\
//collision-tris 1000
0, 0, 0, 0, 0, 0, oak_tree
50, 0, 0, 0, 0, 0, streetlamp
950, 0, 0, 0, 0, 0, tower
500, 5, 0, 0, 0, 0, intercity_bridge
20, 0, 10, 0, 0, 0, truck\twrecker.truck
30, 0, 10, 0, 0, 0, hangar\tsale\tspawnZone_hangar_1
// 25, 0, 0, 0, 0, 0, old_tree
not,a,valid,placement,line,here,bad
set_default_rendering_distance 1000
"""


class CityWorldVisualAuditTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
        *,
        entries: list[tuple[str, bytes]] | None = None,
        name: str = "CityWorld.zip",
    ) -> Path:
        archive_path = root / name
        members = entries or [
            ("CityWorld.terrn2", TERRAIN.encode()),
            ("CityWorld.tobj", PLACEMENTS.encode()),
            ("oak_tree.odef", b"oak_tree.mesh\n1,1,1\nend\n"),
            ("oak_tree.mesh", b"mesh"),
            ("streetlamp.odef", b"streetlamp.mesh\n1,1,1\nend\n"),
            ("streetlamp.mesh", b"mesh"),
            ("tower.odef", b"tower.mesh\n1,1,1\nend\n"),
            ("tower.mesh", b"mesh"),
            (
                "intercity_bridge.odef",
                b"intercity_bridge.mesh\n1,1,1\nend\n",
            ),
            ("intercity_bridge.mesh", b"mesh"),
            ("city.material", b"material city {}\n"),
            ("facade.png", b"texture"),
        ]
        with zipfile.ZipFile(
            archive_path, "w", compression=zipfile.ZIP_DEFLATED
        ) as archive:
            for member_name, payload in members:
                archive.writestr(member_name, payload)
        return archive_path

    def test_report_covers_visual_categories_and_intercity_links(self) -> None:
        with tempfile.TemporaryDirectory() as temp_directory:
            archive = self.make_archive(Path(temp_directory))
            report = AUDITOR.audit_archive(archive)

        self.assertTrue(report["ok"])
        self.assertEqual(report["format"], "ror-cityworld-visual-audit-v1")
        self.assertEqual(report["placements"]["total"], 6)
        self.assertEqual(report["placements"]["commented_placements"], 1)
        self.assertEqual(report["placements"]["malformed"], 1)
        self.assertEqual(report["placements"]["directives"], 1)
        self.assertEqual(
            report["placements"]["category_counts"],
            {
                "bridge": 1,
                "building": 2,
                "fixture": 1,
                "other": 1,
                "vegetation": 1,
            },
        )
        self.assertEqual(report["assets"]["model_files"], 4)
        self.assertEqual(report["assets"]["object_definition_files"], 4)
        self.assertEqual(report["assets"]["material_files"], 1)
        self.assertEqual(report["assets"]["texture_files"], 1)
        self.assertEqual(
            report["intercity_links"],
            [
                {
                    "distance_m": 1000.0,
                    "from": "East City",
                    "to": "West City",
                }
            ],
        )
        self.assertEqual(
            [cluster["placement_count"] for cluster in report["city_clusters"]],
            [5, 1],
        )
        self.assertNotIn(
            "PLACEMENT_DEFINITION_UNRESOLVED",
            [warning["code"] for warning in report["warnings"]],
        )

    def test_output_is_deterministic_and_contains_no_host_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp_directory:
            root = Path(temp_directory)
            archive = self.make_archive(root)
            first = AUDITOR.canonical_json(AUDITOR.audit_archive(archive))
            second = AUDITOR.canonical_json(AUDITOR.audit_archive(archive))

        self.assertEqual(first, second)
        self.assertNotIn(str(root), first)
        self.assertEqual(json.loads(first)["archive"]["name"], "CityWorld.zip")

    def test_sha256_expectation_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_directory:
            archive = self.make_archive(Path(temp_directory))
            with self.assertRaisesRegex(
                AUDITOR.AuditFailure, "SHA-256 mismatch"
            ):
                AUDITOR.audit_archive(archive, expected_sha256="0" * 64)

    def test_unsafe_and_case_colliding_members_fail_closed(self) -> None:
        for extra_name in ("../escape.mesh", "CITYWORLD.TOBJ"):
            with self.subTest(extra_name=extra_name):
                with tempfile.TemporaryDirectory() as temp_directory:
                    entries = [
                        ("CityWorld.terrn2", TERRAIN.encode()),
                        ("CityWorld.tobj", PLACEMENTS.encode()),
                        (extra_name, b"bad"),
                    ]
                    archive = self.make_archive(
                        Path(temp_directory), entries=entries
                    )
                    with self.assertRaises(AUDITOR.AuditFailure):
                        AUDITOR.audit_archive(archive)

    def test_cli_writes_report_and_rejects_bad_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temp_directory:
            root = Path(temp_directory)
            archive = self.make_archive(root)
            output = root / "report.json"
            success = subprocess.run(
                [
                    sys.executable,
                    str(TOOL_PATH),
                    str(archive),
                    "--pretty",
                    "--output",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            failure = subprocess.run(
                [
                    sys.executable,
                    str(TOOL_PATH),
                    str(archive),
                    "--expect-sha256",
                    "f" * 64,
                ],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertEqual(json.loads(output.read_text())["ok"], True)
            self.assertEqual(failure.returncode, 2)
            self.assertIn("SHA-256 mismatch", failure.stderr)


if __name__ == "__main__":
    unittest.main()
