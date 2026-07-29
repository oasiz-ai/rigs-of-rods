#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import importlib.util
import json
import math
from dataclasses import replace
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
BASE_PLACEMENTS = """\
// SOURCE_PLACEMENTS_PAYLOAD_MUST_NOT_LEAK
1, 2, 3, 0, 0, 0, source_only_object
485, 0.1, 370, 0, 90, 0, troadavenuesidewalk
1460.966797, 0.1, 903.098389, 0, -180, 0, crucetQr
"""


def luminaria_fixture_placements() -> str:
    lines: list[str] = []
    for index in range(42):
        lines.append(
            f"{2300 + (index % 7) * 15}, 0.1, "
            f"{900 + (index // 7) * 20}, 0, 0, 0, luminariaLQr"
        )
    for index in range(25):
        lines.append(
            f"{2450 + (index % 5) * 20}, 0.1, "
            f"{1050 + (index // 5) * 20}, 0, 0, 0, luminariaQr"
        )
    for index in range(528 - 42):
        lines.append(
            f"{2900 + (index % 20) * 3}, 0.1, "
            f"{1500 + (index // 20) * 3}, 0, 0, 0, luminariaLQr"
        )
    for index in range(239 - 25):
        lines.append(
            f"{3100 + (index % 20) * 3}, 0.1, "
            f"{1700 + (index // 20) * 3}, 0, 0, 0, luminariaQr"
        )
    for index in range(12):
        lines.append(
            f"{3300 + index * 3}, 0.1, 1900, "
            "0, 0, 0, luminariaYQr"
        )
    return "\n".join(lines) + "\n"


PLACEMENTS = BASE_PLACEMENTS + luminaria_fixture_placements()


class CityWorldLocalOverlayBuilderTests(unittest.TestCase):
    def make_archive(
        self,
        root: Path,
        *,
        terrain: str = TERRAIN,
        placements: str = PLACEMENTS,
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
            archive.writestr("CityWorld.tobj", placements.encode("utf-8"))
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
            material_payload = (
                "// Synthetic standalone asset material script.\n"
                "material rorng_shared_surface\n"
                "{\n"
                "  technique\n"
                "  {\n"
                "    pass\n"
                "    {\n"
                "      ambient 0.1 0.2 0.3 1\n"
                "    }\n"
                "  }\n"
                "}\n\n"
                f"material {asset_id}_surface\n"
                "{\n"
                "  technique\n"
                "  {\n"
                "    pass\n"
                "    {\n"
                "      ambient 0.2 0.3 0.4 1\n"
                "    }\n"
                "  }\n"
                "}\n"
            ).encode()
            material = BUILDER.RuntimeFile(
                package_path=f"{asset_id}.material",
                repository_path=f"fixtures/{asset_id}.material",
                role="material-fallback",
                sha256=hashlib.sha256(material_payload).hexdigest(),
                size=len(material_payload),
                payload=material_payload,
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
                                "package_path": item.package_path,
                                "path": item.repository_path,
                                "role": item.role,
                                "sha256": item.sha256,
                                "size": item.size,
                            }
                            for item in (runtime, material)
                        ],
                    },
                    runtime_files=(runtime, material),
                )
            )
        return tuple(assets)

    def fake_streetlight_asset(self) -> object:
        asset_id = BUILDER.LED_STREETLIGHT_ASSET_ID
        odef_payload = (
            f"{asset_id}_lod0.mesh\n"
            "1, 1, 1\n"
            "standard\n\n"
            "pointlight 0, 7.12, -1.58, 0, -1, 0, 1, 0.72, 0.3, 24\n\n"
            "end\n"
        ).encode()
        material_payload = (
            "material rorng_bridge_streetlight_test\n"
            "{\n"
            "  technique\n"
            "  {\n"
            "    pass\n"
            "    {\n"
            "      emissive 1 0.72 0.3\n"
            "    }\n"
            "  }\n"
            "}\n"
        ).encode()
        runtime_files = (
            BUILDER.RuntimeFile(
                package_path=f"{asset_id}.odef",
                repository_path=f"fixtures/{asset_id}.odef",
                role="terrain-object",
                sha256=hashlib.sha256(odef_payload).hexdigest(),
                size=len(odef_payload),
                payload=odef_payload,
            ),
            BUILDER.RuntimeFile(
                package_path=f"{asset_id}.material",
                repository_path=f"fixtures/{asset_id}.material",
                role="material-fallback",
                sha256=hashlib.sha256(material_payload).hexdigest(),
                size=len(material_payload),
                payload=material_payload,
            ),
        )
        return BUILDER.PreparedAsset(
            asset_id=asset_id,
            centerline_length_m=None,
            manifest_path=BUILDER.LED_STREETLIGHT_MANIFEST,
            profile=None,
            provenance={
                "asset": {
                    "id": asset_id,
                    "license": "GPL-3.0-or-later",
                    "profile": "static-visual-v1",
                },
                "generator": {},
                "manifest": {
                    "path": BUILDER.LED_STREETLIGHT_MANIFEST,
                    "sha256": "2" * 64,
                },
                "runtime_files": [
                    {
                        "package_path": item.package_path,
                        "path": item.repository_path,
                        "role": item.role,
                        "sha256": item.sha256,
                        "size": item.size,
                    }
                    for item in runtime_files
                ],
                "runtime_lights": [
                    {
                        "id": "rorng_bridge_streetlight_warm",
                        "type": "point",
                    }
                ],
            },
            runtime_files=runtime_files,
        )

    def replace_material(
        self,
        asset: object,
        payload: bytes,
    ) -> object:
        original = next(
            runtime_file
            for runtime_file in asset.runtime_files
            if runtime_file.role == "material-fallback"
        )
        material = BUILDER.RuntimeFile(
            package_path=original.package_path,
            repository_path=original.repository_path,
            role=original.role,
            sha256=hashlib.sha256(payload).hexdigest(),
            size=len(payload),
            payload=payload,
        )
        return replace(
            asset,
            runtime_files=tuple(
                material
                if runtime_file.role == "material-fallback"
                else runtime_file
                for runtime_file in asset.runtime_files
            ),
        )

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
            mock.patch.object(
                BUILDER,
                "prepare_streetlight_asset",
                return_value=self.fake_streetlight_asset(),
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
        streetlight = BUILDER.prepare_streetlight_asset(REPOSITORY_ROOT)
        self.assertEqual(
            streetlight.asset_id,
            BUILDER.LED_STREETLIGHT_ASSET_ID,
        )
        self.assertEqual(len(streetlight.runtime_files), 5)
        self.assertEqual(len(streetlight.provenance["runtime_lights"]), 1)
        streetlight_odef = next(
            item
            for item in streetlight.runtime_files
            if item.role == "terrain-object"
        ).payload.decode("utf-8")
        self.assertNotIn("beginmesh", streetlight_odef)
        self.assertNotIn("stdfriction", streetlight_odef)
        self.assertIn("pointlight ", streetlight_odef)
        self.assertIsNone(streetlight.centerline_length_m)
        self.assertIsNone(streetlight.profile)

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

    def test_full_corridor_closes_at_edge_roads_with_ramps_and_pillars(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, output, result = self.build_fixture(Path(directory))
            report = self.read_report(output)
            corridor = report["corridor"]
            self.assertEqual(
                corridor["format"],
                "ror-cityworld-intercity-corridor-v3",
            )
            self.assertEqual(
                corridor["source"]["connection"],
                "east T-junction",
            )
            self.assertEqual(
                corridor["destination"]["connection"],
                "west perimeter T-junction carriageway",
            )
            self.assertEqual(
                corridor["source"]["object"],
                "troadavenuesidewalk",
            )
            self.assertEqual(
                corridor["destination"]["object"],
                "crucetQr",
            )
            self.assertTrue(
                corridor["covered_centerline_length_m"]
                > corridor["target_distance_m"]
            )
            self.assertEqual(
                corridor["remaining_straight_line_distance_m"],
                0.0,
            )
            self.assertEqual(
                corridor["connection"],
                {
                    "destination_heading_error_degrees": 0.0,
                    "destination_position_gap_m": 0.0,
                    "source_heading_error_degrees": 0.0,
                    "source_position_gap_m": 0.0,
                },
            )
            self.assertEqual(
                corridor["profile"],
                {
                    "connection_surface_y_m": 0.1,
                    "connection_taper_grade": 0.0112,
                    "connection_taper_length_m": 40.0,
                    "deck_clearance_m": 8.0,
                    "flat_lead_length_m": 40.0,
                    "maximum_grade": 0.075,
                    "ramp_length_m": 160.0,
                    "rotation_convention":
                        "ogre-yaw-local-plus-z-cross-section",
                    "sampled_maximum_grade": 0.073573604,
                    "sample_spacing_limit_m": 20.0,
                    "surface_offset_m": 0.08,
                    "surface_y_m": 0.18,
                    "width_m": 8.9,
                },
            )
            obstacle_audit = corridor["obstacle_avoidance"]
            self.assertTrue(
                obstacle_audit["city_edge_seams_authenticated"]
            )
            self.assertTrue(
                obstacle_audit["open_gap_placement_origin_audit"]["verified"]
            )
            self.assertEqual(
                obstacle_audit["open_gap_placement_origin_audit"][
                    "placement_origin_count"
                ],
                0,
            )
            self.assertEqual(
                obstacle_audit["swept_mesh_clearance"],
                "native-visual-and-drive-gate-required",
            )
            self.assertEqual(
                obstacle_audit["intentional_source_overlap_m"],
                14.8491,
            )
            waypoints = corridor["waypoints"]
            self.assertGreater(len(waypoints), 50)
            self.assertEqual(
                waypoints[0]["position_m"],
                [480.0, 0.198, 370.0],
            )
            self.assertEqual(
                waypoints[1]["position_m"],
                [490.0, 0.31, 370.0],
            )
            self.assertEqual(
                waypoints[2]["position_m"],
                [494.8491, 0.31, 370.0],
            )
            self.assertEqual(
                waypoints[-1]["position_m"],
                [1380.966797, 0.1, 936.098389],
            )
            self.assertEqual(waypoints[0]["yaw_degrees"], 0.0)
            self.assertEqual(waypoints[-1]["yaw_degrees"], 0.0)
            self.assertEqual(waypoints[0]["road_type"], "flat")
            self.assertEqual(waypoints[-1]["road_type"], "flat")
            self.assertEqual(
                corridor["source"]["authenticated_placement"]["object"],
                "troadavenuesidewalk",
            )
            self.assertEqual(
                corridor["destination"]["authenticated_placement"]["object"],
                "crucetQr",
            )
            self.assertEqual(
                corridor["source"]["apron"],
                {
                    "collision_authority": "native-procedural-road-v3",
                    "curb_clearance_m": 0.01,
                    "curb_top_y_m": 0.3,
                    "legacy_collision_mesh":
                        "troadavenuesidewalkbox.mesh",
                    "legacy_road_surface_y_m": 0.198,
                    "overlap_length_m": 14.8491,
                    "plateau_y_m": 0.31,
                    "rise_length_m": 10.0,
                    "surface_continuous": True,
                },
            )
            self.assertGreater(
                waypoints[2]["position_m"][1],
                corridor["source"]["apron"]["curb_top_y_m"],
            )
            self.assertEqual(
                max(point["position_m"][1] for point in waypoints),
                8.18,
            )
            self.assertTrue(
                all(
                    first["position_m"][0] < second["position_m"][0]
                    for first, second in zip(waypoints, waypoints[1:])
                )
            )
            source = tuple(BUILDER.ROUTE_SOURCE_ANCHOR["connection_position_m"])
            destination = tuple(
                BUILDER.ROUTE_DESTINATION_ANCHOR["connection_position_m"]
            )
            apron_length = (
                source[0] - BUILDER.ROUTE_SOURCE_APRON_START_X_M
            )
            control_points = BUILDER.route_control_points(source, destination)
            arc_table = BUILDER.route_arc_table(control_points)
            for waypoint in waypoints[2:]:
                parameter = BUILDER.parameter_at_station(
                    arc_table,
                    max(0.0, waypoint["station_m"] - apron_length),
                )
                tangent_x, tangent_z = BUILDER.cubic_bezier_derivative(
                    control_points,
                    parameter,
                )
                yaw = math.radians(waypoint["yaw_degrees"])
                cross_section_x = math.sin(yaw)
                cross_section_z = math.cos(yaw)
                self.assertLess(
                    abs(
                        tangent_x * cross_section_x
                        + tangent_z * cross_section_z
                    )
                    / math.hypot(tangent_x, tangent_z),
                    1e-8,
                )
            supports = corridor["supports"]
            self.assertTrue(supports["enabled"])
            self.assertGreater(supports["requested_count"], 40)
            self.assertLessEqual(
                supports["maximum_station_spacing_m"],
                BUILDER.ROUTE_SAMPLE_SPACING_M,
            )
            fixtures = corridor["fixtures"]
            self.assertEqual(
                fixtures["format"],
                "ror-cityworld-streetlight-placement-v1",
            )
            self.assertEqual(
                fixtures["asset_id"],
                BUILDER.LED_STREETLIGHT_ASSET_ID,
            )
            self.assertFalse(fixtures["paired"])
            self.assertEqual(fixtures["station_spacing_m"], 40.0)
            self.assertEqual(fixtures["station_count"], 16)
            self.assertEqual(fixtures["instance_count"], 16)
            self.assertEqual(fixtures["lateral_mount_offset_m"], 4.675)
            self.assertEqual(fixtures["mount_elevation_above_road_m"], 0.95)
            self.assertEqual(fixtures["runtime_point_lights_per_instance"], 1)
            self.assertEqual(
                fixtures["collision_authority"],
                "native-procedural-road-v3",
            )
            self.assertEqual(
                [item["station_m"] for item in fixtures["stations"]],
                [
                    round(apron_length + value, 9)
                    for value in range(220, 821, 40)
                ],
            )
            self.assertEqual(
                [item["side"] for item in fixtures["stations"]],
                ["left", "right"] * 8,
            )
            for fixture in fixtures["stations"]:
                center = fixture["centerline_position_m"]
                placement = fixture["placement_position_m"]
                offset_x = placement[0] - center[0]
                offset_z = placement[2] - center[2]
                offset_length = math.hypot(offset_x, offset_z)
                self.assertAlmostEqual(offset_length, 4.675, places=8)
                self.assertAlmostEqual(
                    placement[1] - center[1],
                    0.95,
                    places=8,
                )
                yaw = math.radians(
                    fixture["rotation_degrees"][1]
                )
                arm_x = -math.sin(yaw)
                arm_z = -math.cos(yaw)
                inward_x = -offset_x / offset_length
                inward_z = -offset_z / offset_length
                self.assertAlmostEqual(
                    arm_x * inward_x + arm_z * inward_z,
                    1.0,
                    places=8,
                )
            self.assertEqual(
                report["source"]["references"],
                {
                    "geometry_config": "CityWorld.otc",
                    "original_placements": "CityWorld.tobj",
                    "overlay_placements": BUILDER.OVERLAY_NAME,
                    "resource_bundle_dependency":
                        "CityWorld.zip:CityWorld.terrn2:"
                        + report["source"]["archive"]["expected_sha256"],
                },
            )
            self.assertEqual(
                len(report["source"]["archive"]["members"]),
                3,
            )
            self.assertEqual(len(report["assets"]), 5)
            self.assertTrue(
                all("manifest" in asset for asset in report["assets"])
            )
            self.assertEqual(
                report["visual_asset_usage"],
                {
                    "corridor_placement_mode":
                        "native-procedural-v3-curb-cut-with-blender-fixtures-v1",
                    "disabled_light_candidate_manifest":
                        BUILDER.NEOQ_LIGHT_CANDIDATE_NAME,
                    "neoq_core_runtime_light_activation":
                        "blocked-fail-closed",
                    "packaged_asset_ids": [
                        "rorng_city_led_streetlight_bridge",
                    ],
                    "placed_asset_ids": [
                        "rorng_city_led_streetlight_bridge",
                    ],
                    "unplaced_asset_ids": [
                        "rorng_city_gateway_block_40m",
                        "rorng_city_bridge_transition_12m",
                        "rorng_city_bridge_curve_left_15deg_20m",
                        "rorng_city_bridge_span_20m",
                    ],
                    "validated_asset_ids": [
                        "rorng_city_gateway_block_40m",
                        "rorng_city_bridge_transition_12m",
                        "rorng_city_bridge_curve_left_15deg_20m",
                        "rorng_city_bridge_span_20m",
                        "rorng_city_led_streetlight_bridge",
                    ],
                    "purpose":
                        "curb-free Penguinville overlap apron plus route-safe "
                        "Blender lighting; deterministic NeoQueretaro "
                        "pole-light candidates remain disabled pending the "
                        "renderer budget and zero-shadow contracts; bridge "
                        "modules remain validated candidates for deck and "
                        "abutment replacement",
                },
            )
            self.assertIn(
                "tools/build_cityworld_local_overlay.py",
                [tool["path"] for tool in report["tools"]],
            )
            with zipfile.ZipFile(output) as package:
                report_payload = package.read(BUILDER.REPORT_NAME)
                placement_text = package.read(BUILDER.OVERLAY_NAME).decode()
            self.assertIn("begin_procedural_roads", placement_text)
            self.assertIn("collision_enabled true", placement_text)
            self.assertGreater(placement_text.count(", bridge\n"), 40)
            self.assertNotIn("rorng_city_gateway_block_40m -", placement_text)
            self.assertEqual(
                placement_text.count(
                    f"{BUILDER.LED_STREETLIGHT_ASSET_ID} - "
                ),
                16,
            )
            self.assertIn(
                "cityworld_next_led_0235_left",
                placement_text,
            )
            self.assertIn(
                "cityworld_next_led_0835_right",
                placement_text,
            )
            self.assertIn(
                "480, 0.198, 370, 0, 0, 0, 8.9, 1, 0.15, flat",
                placement_text,
            )
            self.assertIn(
                "490, 0.31, 370, 0, 0, 0, 8.9, 1, 0.15, flat",
                placement_text,
            )
            self.assertIn(
                "494.8491, 0.31, 370, 0, 0, 0, 8.9, 1, 0.15, flat",
                placement_text,
            )
            self.assertIn(
                "1380.966797, 0.1, 936.098389, 0, 0, 0, 8.9, 1, 0.15, flat",
                placement_text,
            )
            self.assertEqual(
                result["report"]["sha256"],
                hashlib.sha256(report_payload).hexdigest(),
            )

    def test_neoq_light_candidates_are_deterministic_bounded_and_disabled(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, output, result = self.build_fixture(Path(directory))
            report = self.read_report(output)
            with zipfile.ZipFile(output) as package:
                manifest_payload = package.read(
                    BUILDER.NEOQ_LIGHT_CANDIDATE_NAME
                )
                manifest = json.loads(manifest_payload)
                placement_text = package.read(BUILDER.OVERLAY_NAME).decode()
                descriptor = package.read(BUILDER.TERRAIN_NAME).decode()

        self.assertEqual(
            result["format"],
            "ror-cityworld-local-overlay-build-result-v4",
        )
        self.assertEqual(
            manifest["format"],
            "ror-cityworld-neoq-core-light-candidates-v1",
        )
        self.assertEqual(manifest["candidate_poles"], 67)
        self.assertEqual(manifest["candidate_runtime_point_lights"], 67)
        self.assertEqual(
            manifest["candidate_family_counts"],
            {
                "luminariaLQr": 42,
                "luminariaQr": 25,
                "luminariaYQr": 0,
            },
        )
        self.assertFalse(manifest["activation"]["enabled"])
        self.assertTrue(manifest["activation"]["fail_closed"])
        self.assertEqual(
            manifest["activation"]["runtime_point_lights_emitted"],
            0,
        )
        self.assertEqual(
            manifest["activation"]["blockers"],
            [
                "renderer-local-light-budget-policy-unavailable",
                "zero-local-shadow-runtime-contract-unavailable",
                "neoq-fixed-camera-runtime-visual-gate-unavailable",
            ],
        )
        self.assertEqual(
            manifest["policy_contract"],
            {
                "hard_max_range_m": 24.0,
                "maximum_candidate_lights": 67,
                "policy_id": "ror-cityworld-local-light-budget-v1",
                "required_local_shadow_casters": 0,
                "sampling_strategy":
                    "one-bounded-representative-light-per-existing-pole",
            },
        )
        candidates = manifest["candidates"]
        self.assertEqual(len(candidates), 67)
        self.assertEqual(
            len({candidate["candidate_id"] for candidate in candidates}),
            67,
        )
        self.assertTrue(
            all(
                candidate["source"]["distance_from_telepoint_m"] <= 400.0
                and candidate["light"]["hard_max_range_m"] == 24.0
                and not candidate["light"]["shadow_casting_requested"]
                and not candidate["adapter"]["runtime_definition_emitted"]
                and candidate["adapter"]["light_only_mesh_header"] == "none"
                for candidate in candidates
            )
        )
        self.assertTrue(
            manifest["visual_geometry"]["existing_cityworld_poles_reused"]
        )
        self.assertFalse(
            manifest["visual_geometry"]["duplicate_pole_geometry_emitted"]
        )
        self.assertNotIn("rorng_city_neoq_luminaria", placement_text)
        self.assertIn("Version = 4", descriptor)
        self.assertIn(
            "GUID = rorng-cityworld-next-local-overlay-v4",
            descriptor,
        )
        lighting_report = report["city_lighting"]["neoq_core"]
        self.assertEqual(lighting_report["candidate_poles"], 67)
        self.assertEqual(
            lighting_report["candidate_manifest"]["sha256"],
            hashlib.sha256(manifest_payload).hexdigest(),
        )
        self.assertEqual(
            lighting_report["candidate_manifest"]["role"],
            "disabled-light-candidate-manifest",
        )

    def test_neoq_light_candidate_scope_drift_fails_closed(self) -> None:
        changed_placements = PLACEMENTS.replace(
            "2300, 0.1, 900, 0, 0, 0, luminariaLQr",
            "2900, 0.1, 2200, 0, 0, 0, luminariaLQr",
            1,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(
                root,
                placements=changed_placements,
            )
            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    digest,
                ),
                self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    "core luminaria counts changed",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=root / "overlay.zip",
                    surface_offset_m=0.08,
                )

    def test_route_anchor_and_open_gap_drift_fail_closed(self) -> None:
        cases = (
            (
                "missing-source",
                PLACEMENTS.replace(
                    "485, 0.1, 370, 0, 90, 0, troadavenuesidewalk\n",
                    "",
                ),
                "authenticated source road placement",
            ),
            (
                "changed-destination-rotation",
                PLACEMENTS.replace(
                    "1460.966797, 0.1, 903.098389, 0, -180, 0, crucetQr",
                    "1460.966797, 0.1, 903.098389, 0, -175, 0, crucetQr",
                ),
                "authenticated destination road placement",
            ),
            (
                "occupied-open-gap",
                PLACEMENTS
                + "900, 0.1, 700, 0, 0, 0, unexpected_building\n",
                "intercity placement-origin gap is no longer empty",
            ),
        )
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            for name, placements, message in cases:
                with self.subTest(name=name):
                    root = parent / name
                    root.mkdir()
                    archive, digest = self.make_archive(
                        root,
                        placements=placements,
                    )
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
                            "prepare_streetlight_asset",
                            return_value=self.fake_streetlight_asset(),
                        ),
                        self.assertRaisesRegex(
                            BUILDER.OverlayFailure,
                            message,
                        ),
                    ):
                        BUILDER.build_local_overlay(
                            archive_path=archive,
                            repository_path=REPOSITORY_ROOT,
                            output_path=root / "overlay.zip",
                            surface_offset_m=0.08,
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
            report = json.loads(payloads[BUILDER.REPORT_NAME])
            expected_dependency = (
                "CityWorld.zip:CityWorld.terrn2:"
                + report["source"]["archive"]["expected_sha256"]
            )
            descriptor = payloads[BUILDER.TERRAIN_NAME].decode()
            self.assertIn("GeometryConfig = CityWorld.otc", descriptor)
            self.assertIn("CityWorld.tobj =", descriptor)
            self.assertIn(f"{BUILDER.OVERLAY_NAME} =", descriptor)
            self.assertIn(
                f"Dependency = {expected_dependency}",
                descriptor,
            )
            self.assertNotIn(
                "Dependency = CityWorld.zip:CityWorld.terrn2\n",
                descriptor,
            )
            self.assertIn("Redistribution and shipping", descriptor)
            material_names = sorted(
                name for name in names if name.endswith(".material")
            )
            self.assertEqual(
                material_names,
                [BUILDER.MERGED_MATERIAL_NAME],
            )
            merged_material = payloads[BUILDER.MERGED_MATERIAL_NAME].decode()
            self.assertEqual(
                merged_material.count(
                    "material rorng_bridge_streetlight_test\n"
                ),
                1,
            )
            self.assertNotIn("rorng_shared_surface", merged_material)
            self.assertTrue(
                all(
                    f"{asset.asset_id}.material" not in names
                    for asset in self.fake_assets()
                )
            )
            self.assertTrue(
                all(
                    f"{asset.asset_id}.odef" not in names
                    for asset in self.fake_assets()
                )
            )
            material_record = next(
                record
                for record in report["package"]["files"]
                if record["path"] == BUILDER.MERGED_MATERIAL_NAME
            )
            self.assertEqual(material_record["role"], "material-fallback")
            self.assertEqual(
                material_record["sha256"],
                hashlib.sha256(
                    payloads[BUILDER.MERGED_MATERIAL_NAME]
                ).hexdigest(),
            )
            self.assertEqual(
                sum(
                    1
                    for asset in report["assets"]
                    for runtime_file in asset["runtime_files"]
                    if runtime_file["role"] == "material-fallback"
                ),
                5,
            )
            self.assertEqual(
                report["rights"],
                {
                    "local_only": True,
                    "redistribution_allowed": False,
                    "shipping_allowed": False,
                    "source_archive_copied": False,
                    "source_geometry_copied": False,
                    "source_objects_copied": False,
                    "source_placement_payload_copied": False,
                    "source_placements_copied": False,
                    "source_placement_records_derived": True,
                    "derived_source_placement_record_count": 67,
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
                root / "CON.zip",
                root / "CON.overlay.zip",
                root / "AUX..zip",
                root / "com1.zip",
                root / "LPT1.backup.zip",
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

    def test_material_merge_deduplicates_byte_and_semantic_equivalents(
        self,
    ) -> None:
        exact = b"""\
// First source comment.
material rorng_shared
{
  technique
  {
    pass
    {
      ambient 0.1 0.2 0.3 1
    }
  }
}
"""
        semantic = b"""\
/* Formatting and comments do not alter this definition. */
material   rorng_shared {
 technique {
  pass {
   ambient   0.1  0.2 0.3 1
  }
 }
}

material rorng_unique
{
 technique
 {
  pass
  {
   ambient 0.4 0.5 0.6 1
  }
 }
}
"""
        assets = list(self.fake_assets())
        assets[0] = self.replace_material(assets[0], exact)
        assets[1] = self.replace_material(assets[1], exact)
        assets[2] = self.replace_material(assets[2], semantic)
        assets[3] = self.replace_material(assets[3], exact)
        original_runtime_files = tuple(
            asset.runtime_files for asset in assets
        )

        merged = BUILDER.merge_material_scripts(assets)
        reversed_merge = BUILDER.merge_material_scripts(
            tuple(reversed(assets))
        )
        text = merged.decode()

        self.assertEqual(merged, reversed_merge)
        self.assertEqual(text.count("material rorng_shared\n"), 1)
        self.assertEqual(text.count("material rorng_unique\n"), 1)
        self.assertLess(
            text.index("material rorng_shared\n"),
            text.index("material rorng_unique\n"),
        )
        self.assertEqual(
            tuple(asset.runtime_files for asset in assets),
            original_runtime_files,
        )

    def test_conflicting_same_name_materials_fail_before_publish(self) -> None:
        shared = b"""\
material rorng_bridge_streetlight_test
{
 technique
 {
  pass
  {
   ambient 0.1 0.2 0.3 1
  }
 }
}
"""
        conflict = shared.replace(b"0.1 0.2 0.3", b"0.9 0.8 0.7")
        streetlight = self.replace_material(
            self.fake_streetlight_asset(),
            shared + b"\n" + conflict,
        )

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
                    "prepare_streetlight_asset",
                    return_value=streetlight,
                ),
                self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    r"conflicting material definition "
                    r"'rorng_bridge_streetlight_test'.*"
                    r"\.material.*\.material",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=output,
                    surface_offset_m=0.08,
                )
            self.assertFalse(output.exists())

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
                    "prepare_streetlight_asset",
                    return_value=self.fake_streetlight_asset(),
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

    def test_regular_file_sync_uses_writable_binary_descriptor(self) -> None:
        path = mock.MagicMock(spec=Path)
        stream = mock.MagicMock()
        path.open.return_value.__enter__.return_value = stream
        with mock.patch.object(BUILDER.os, "fsync") as fsync:
            BUILDER.sync_regular_file(path)
        path.open.assert_called_once_with("r+b")
        stream.flush.assert_called_once_with()
        fsync.assert_called_once_with(stream.fileno.return_value)

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
                    "prepare_streetlight_asset",
                    return_value=self.fake_streetlight_asset(),
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
