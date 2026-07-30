#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/cityworld_penguin_neoq_corridor.py"
SPEC = importlib.util.spec_from_file_location(
    "cityworld_penguin_neoq_corridor",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load Penguinville-NeoQ seam contract")
SEAM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SEAM
SPEC.loader.exec_module(SEAM)


class CityWorldPenguinNeoQCorridorTests(unittest.TestCase):
    def transition_provenance(self) -> dict[str, object]:
        path = REPOSITORY_ROOT / SEAM.TRANSITION_MANIFEST
        manifest = json.loads(path.read_text(encoding="utf-8"))
        return {
            "asset": manifest["asset"],
            "collision": manifest["collision"],
            "connectors": manifest["connectors"],
            "geometry": manifest["geometry"],
            "materials": manifest["materials"],
            "runtime_material_dependencies":
                manifest["runtime_material_dependencies"],
            "runtime_files": manifest["compiled"]["outputs"],
        }

    def points(self) -> list[dict[str, object]]:
        total = 100.0
        return [
            {
                "station_m": 0.0,
                "x": SEAM.ROUTE_SOURCE_POSITION_M[0],
                "y": SEAM.ROUTE_SOURCE_POSITION_M[1],
                "z": SEAM.ROUTE_SOURCE_POSITION_M[2],
                "yaw_degrees": 0.0,
                "road_type": "flat",
                "width_m": SEAM.width_at_station(0.0, total),
            },
            {
                "station_m": 50.0,
                "x": 900.0,
                "y": 8.18,
                "z": 650.0,
                "yaw_degrees": -35.0,
                "road_type": SEAM.BRIDGE_TOKEN,
                "width_m": SEAM.width_at_station(50.0, total),
            },
            {
                "station_m": total,
                "x": SEAM.ROUTE_DESTINATION_POSITION_M[0],
                "y": SEAM.ROUTE_DESTINATION_POSITION_M[1],
                "z": SEAM.ROUTE_DESTINATION_POSITION_M[2],
                "yaw_degrees": 0.0,
                "road_type": "flat",
                "width_m": SEAM.width_at_station(total, total),
            },
        ]

    def validate(
        self,
        points: list[dict[str, object]] | None = None,
        provenance: dict[str, object] | None = None,
        text: str | None = None,
    ) -> dict[str, object]:
        return SEAM.validate_seams(
            self.points() if points is None else points,
            procedural_text=(
                f"collision_enabled true\n{SEAM.OPEN_ENDCAP_DIRECTIVE}\n"
                if text is None
                else text
            ),
            transition_asset_provenance=(
                self.transition_provenance()
                if provenance is None
                else provenance
            ),
        )

    def test_exact_two_handoffs_and_outboard_supports(self) -> None:
        report = self.validate()
        self.assertEqual(report["format"], SEAM.FORMAT)
        self.assertFalse(
            report["collision_endcaps"][
                "start_and_finish_transverse_collision_faces_emitted"
            ]
        )
        self.assertEqual(
            report["collision_endcaps"]["source_exposed_vertical_face_m"],
            0.0,
        )
        self.assertEqual(
            report["collision_endcaps"][
                "destination_exposed_vertical_face_m"
            ],
            0.0,
        )
        self.assertEqual(
            report["source"]["curb_opening"],
            {
                "authenticated_source_line": 1354,
                "legacy_collision_mesh": {
                    "name": "troadavenuesidewalkbox.mesh",
                    "sha256": SEAM.SOURCE_LEGACY_COLLISION_SHA256,
                },
                "legacy_curb_collision_retained": False,
                "legacy_object": "troadavenuesidewalk",
                "replacement_collision_mesh": {
                    "name": "crossroadavenuesidewalkbox.mesh",
                    "sha256": SEAM.SOURCE_REPLACEMENT_COLLISION_SHA256,
                },
                "replacement_object": "crossroadavenuesidewalk",
                "status": "native-authenticated-in-place-replacement",
            },
        )
        self.assertEqual(
            report["destination"]["collision_mesh"]["surface_probe"],
            {
                "local_xy_m": [79.999, 33.0],
                "local_z_m": 0.0,
                "triangle_index": 61,
                "triangle_vertices": [105, 106, 107],
            },
        )
        authority = report["transition_asset_contract"][
            "authoritative_collision"
        ]
        self.assertEqual(authority["open_interval_surface_count"], 1)
        self.assertEqual(authority["shared_boundary_area_m2"], 0.0)
        self.assertEqual(
            authority["replacement_outgoing_branch_interval_world_x_m"],
            [485.0, 510.0],
        )
        self.assertEqual(
            authority["shared_boundaries_world_x_m"],
            [510.0, 522.0],
        )
        self.assertEqual(
            report["transition_asset_contract"]["runtime_surface"],
            {
                "material": "road2",
                "material_script_sha256":
                    SEAM.TRANSITION_RUNTIME_MATERIAL_SCRIPT_SHA256,
                "texture_sha256":
                    SEAM.TRANSITION_RUNTIME_TEXTURE_SHA256,
                "uv_profile": "ror-procedural-road-road2-atlas-v1",
            },
        )
        self.assertEqual(
            report["supports"],
            {
                "legacy_ground_road_envelopes_intersected": 0,
                "road_type_token": "bridge_side_pillars",
                "support_layout": "paired-outboard",
                "swept_bridge_carriageway_intrusion_m": 0.0,
            },
        )

    def test_endpoint_and_marker_drift_fail_closed(self) -> None:
        cases = []
        for label, index, field, delta in (
            ("source-x", 0, "x", 0.001),
            ("source-y", 0, "y", 0.001),
            ("source-z", 0, "z", 0.001),
            ("source-yaw", 0, "yaw_degrees", 0.001),
            ("source-width", 0, "width_m", 0.001),
            ("destination-x", -1, "x", 0.001),
            ("destination-y", -1, "y", 0.001),
            ("destination-z", -1, "z", 0.001),
            ("destination-yaw", -1, "yaw_degrees", 0.001),
            ("destination-width", -1, "width_m", 0.001),
            ("middle-width", 1, "width_m", 0.001),
        ):
            points = self.points()
            points[index][field] = float(points[index][field]) + delta
            cases.append((label, points, None))
        no_bridge = self.points()
        no_bridge[1]["road_type"] = "flat"
        cases.append(("no-bridge", no_bridge, None))
        legacy_bridge = self.points()
        legacy_bridge[1]["road_type"] = "bridge"
        cases.append(("legacy-bridge", legacy_bridge, None))
        cases.extend(
            (
                ("endcaps-default", self.points(), "collision_enabled true\n"),
                (
                    "endcaps-duplicate",
                    self.points(),
                    f"{SEAM.OPEN_ENDCAP_DIRECTIVE}\n"
                    f"{SEAM.OPEN_ENDCAP_DIRECTIVE}\n",
                ),
            )
        )
        for label, points, text in cases:
            with self.subTest(label=label), self.assertRaises(SEAM.SeamFailure):
                self.validate(points=points, text=text)

    def test_transition_collision_contract_drift_fails_closed(self) -> None:
        cases = []
        wrong_hash = self.transition_provenance()
        wrong_hash["runtime_files"][2]["sha256"] = "0" * 64
        cases.append(("compiled-hash", wrong_hash))
        wrong_crown = self.transition_provenance()
        wrong_crown["connectors"][0]["cross_section"][
            "crown_height_m"
        ] = 0.2
        cases.append(("connector-crown", wrong_crown))
        wrong_topology = self.transition_provenance()
        wrong_topology["collision"]["objects"][0]["topology"][
            "watertight"
        ] = False
        cases.append(("topology", wrong_topology))
        duplicate_role = self.transition_provenance()
        duplicate_role["collision"]["objects"][1]["role"] = "collision-road"
        cases.append(("role", duplicate_role))
        wrong_parent = self.transition_provenance()
        wrong_parent["materials"][0]["runtime_parent_material"] = "metal"
        cases.append(("runtime-parent", wrong_parent))
        wrong_texture = self.transition_provenance()
        wrong_texture["runtime_material_dependencies"][0][
            "texture_sha256"
        ] = "0" * 64
        cases.append(("runtime-texture", wrong_texture))
        for label, provenance in cases:
            with self.subTest(label=label), self.assertRaises(SEAM.SeamFailure):
                self.validate(provenance=provenance)


if __name__ == "__main__":
    unittest.main()
