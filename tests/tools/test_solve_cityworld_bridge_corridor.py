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


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/solve_cityworld_bridge_corridor.py"
MANIFEST_RELATIVE = (
    "resources/nextgen/cityworld/bridge/curve_left_15deg/"
    "rorng_city_bridge_curve_left_15deg_20m.asset.json"
)
FIXTURE_PATH = (
    REPOSITORY_ROOT
    / "tests/fixtures/cityworld_curved_bridge_runtime/"
    "cityworld_curved_bridge_runtime.tobj"
)

SPEC = importlib.util.spec_from_file_location(
    "solve_cityworld_bridge_corridor",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load corridor solver")
SOLVER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SOLVER
SPEC.loader.exec_module(SOLVER)


class CityWorldBridgeCorridorSolverTests(unittest.TestCase):
    def profiles(self) -> tuple[object, ...]:
        return tuple(
            SOLVER.load_asset_profile(REPOSITORY_ROOT, MANIFEST_RELATIVE)
            for _ in range(3)
        )

    def placements(self) -> tuple[object, ...]:
        return SOLVER.solve_corridor(
            self.profiles(),
            entry_x=512.0,
            entry_z=482.0,
            heading_degrees=0.0,
        )

    def test_three_curves_have_exact_connector_positions_and_tangents(
        self,
    ) -> None:
        placements = self.placements()
        self.assertEqual(len(placements), 3)
        self.assertEqual(
            [round(item.yaw_degrees, 6) for item in placements],
            [7.5, 22.5, 37.5],
        )
        for first, second in zip(placements, placements[1:]):
            self.assertAlmostEqual(first.exit_x, second.entry_x, places=9)
            self.assertAlmostEqual(first.exit_z, second.entry_z, places=9)
            self.assertAlmostEqual(
                first.exit_heading_degrees,
                second.entry_heading_degrees,
                places=9,
            )
        self.assertAlmostEqual(placements[-1].exit_x, 534.375393687, places=9)
        self.assertAlmostEqual(placements[-1].exit_z, 536.018978981, places=9)
        self.assertAlmostEqual(
            placements[-1].exit_heading_degrees,
            44.999999942,
            places=9,
        )

    def test_checked_tobj_is_canonical_solver_output(self) -> None:
        expected = SOLVER.tobj_text(self.placements(), surface_y=0.08)
        self.assertEqual(
            FIXTURE_PATH.read_text(encoding="utf-8"),
            expected,
        )
        self.assertNotIn(
            "rorng_city_bridge_curve_left_15deg_20m,",
            expected,
        )

    def test_json_report_has_zero_seam_error(self) -> None:
        result = SOLVER.report(
            self.profiles(),
            self.placements(),
            surface_y=0.08,
        )
        self.assertEqual(result["format"], "ror-cityworld-bridge-corridor-v1")
        self.assertEqual(
            result["seams"],
            [
                {
                    "heading_error_degrees": 0.0,
                    "index": 0,
                    "position_gap_m": 0.0,
                },
                {
                    "heading_error_degrees": 0.0,
                    "index": 1,
                    "position_gap_m": 0.0,
                },
            ],
        )

    def test_invalid_forward_vector_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_path = root / "asset.json"
            source = json.loads(
                (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(
                    encoding="utf-8"
                )
            )
            source["connectors"][0]["forward"] = [0.0, 0.0, 0.0]
            manifest_path.write_text(
                json.dumps(source),
                encoding="utf-8",
            )
            with self.assertRaises(SOLVER.CorridorFailure):
                SOLVER.load_asset_profile(root, "asset.json")

    def test_cli_is_identical_under_optimized_python(self) -> None:
        arguments = [
            str(TOOL_PATH),
            "--repository",
            str(REPOSITORY_ROOT),
            "--asset",
            MANIFEST_RELATIVE,
            "--asset",
            MANIFEST_RELATIVE,
            "--asset",
            MANIFEST_RELATIVE,
            "--entry-x",
            "512",
            "--entry-z",
            "482",
            "--heading-degrees",
            "0",
            "--surface-y",
            "0.08",
            "--format",
            "json",
        ]
        normal = subprocess.run(
            [sys.executable, *arguments],
            check=False,
            capture_output=True,
            text=True,
        )
        optimized = subprocess.run(
            [sys.executable, "-O", *arguments],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(normal.returncode, 0, normal.stderr)
        self.assertEqual(optimized.returncode, 0, optimized.stderr)
        self.assertEqual(normal.stdout, optimized.stdout)


if __name__ == "__main__":
    unittest.main()
