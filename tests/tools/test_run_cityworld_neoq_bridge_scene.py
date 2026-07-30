#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_neoq_bridge_scene.py"
SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_neoq_bridge_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld Neo bridge runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_engine(
    side_pier_counts: tuple[int, ...] = (46, 56),
    *,
    drive: bool,
) -> str:
    contract = SCENE.base.renderer_contract(sys.platform)
    if sys.platform == "win32":
        api = "D3D11: Device Feature Level 11.0"
    else:
        api = "GL_VERSION = 4.1 synthetic"
    lines = [
        f"RenderSystem Name: {contract.render_system}",
        "Device Name: synthetic GPU",
        "GPU Vendor: synthetic",
        api,
        SCENE.corridor.CITYWORLD_FALLBACK_LIGHTING_MARKER,
        "[RoR|Shadow|PSSM] enabled quality=2 cascades=3 "
        "rtss_receiver=1 format=PF_DEPTH16 "
        "sizes=3072x3072/2048x2048/2048x2048 "
        "lambda=0.970 near=0.5 far=350.0 "
        "splits=0.5/10.0/80.0/350.0",
        SCENE.corridor.ENGINE_MARKERS[1],
        "===== TERRAIN LOADING DONE CityWorldNextLocalOverlay.terrn2",
        "[RoR|TerrainDependency] Mounted "
        "'/isolated/mods/CityWorld.zip' into "
        "'{bundle USER:/mods/CityWorldNextLocalOverlay.zip}'",
        *(SCENE.LIGHT_MARKER for _ in range(SCENE.EXPECTED_LIGHTS)),
        *(
            "[RoR|ProceduralRoad|SidePiers] "
            f"requested={count} built={count} skipped=0"
            for count in side_pier_counts
        ),
    ]
    if drive:
        lines.extend(
            (
                "===== LOADING VEHICLE: b6b0UID-semi.truck",
                "===== LOADING VEHICLE: b6b0UID-semi.truck",
            )
        )
    return "\n".join(lines)


def valid_static_script() -> str:
    return "\n".join(
        (
            *SCENE.STATIC_MARKERS,
            "[RoR|CW2|NeoBridgeRuntime] PASS cameras=6 frames=265 "
            "physics_steps=1060 route_m=3076.132100441 "
            "supports=56 lights=33",
        )
    )


def valid_drive_script() -> str:
    return "\n".join(
        (
            SCENE.DRIVE_MARKERS[0],
            "[RoR|CW2|NeoBridgeDrive] ARMED actor=2026072902 "
            "heading=3.14159 station=0 cross_track=0.2 height=1.5",
            "[RoR|CW2|NeoBridgeDrive] SEAM name=source "
            "station=0.25 x=3791.2 z=3993.3",
            "[RoR|CW2|NeoBridgeDrive] MIDPOINT "
            "station=1538.2 x=5329.0 z=4005.5",
            "[RoR|CW2|NeoBridgeDrive] SEAM name=destination "
            "station=3076.1321 x=6867.2 z=4022.9",
            "[RoR|CW2|NeoBridgeDrive] REVERSE_ARMED actor=2026072903 "
            "direction=neoq20_to_neoq heading=0 station=3096.0 "
            "cross_track=5.8 height=1.6 forward_distance_m=3105.0",
            "[RoR|CW2|NeoBridgeDrive] REVERSE_DESTINATION_SEAM "
            "direction=neoq20_to_neoq target_station=3076.132100441 "
            "station=3075.8 x=6866.7 z=4023.7 local_z=5.7",
            "[RoR|CW2|NeoBridgeDrive] PASS seams=3 traversals=2 "
            "destination_traversals=2 route_m=3076.132100441 "
            "station_m=3076.1321 reverse_station_m=3036.0 "
            "destination_x_m=6877.2 destination_local_z_m=4.9 "
            "reverse_destination_local_z_m=5.7 "
            "distance_m=3170.0 forward_distance_m=3105.0 "
            "reverse_distance_m=65.0 path_error_m=0.8 "
            "vertical_error_m=0.7 regression_m=0.02 "
            "speed_mps=9.2 physics_steps=420000",
        )
    )


def valid_destination_report() -> dict[str, object]:
    waypoints = [
        {
            "position_m": [3790.970703 + index, 0.1, 3993.104004],
            "road_type": "bridge_side_pillars",
        }
        for index in range(79)
    ]
    waypoints.append(
        {
            "position_m": [6867.0, 0.2, 4018.0],
            "road_type": "flat",
        }
    )
    return {
        "corridors": {
            "neoq_to_neoq20": {
                "format": "ror-cityworld-neoq-intercity-bridge-v4",
                "collision": {
                    "authoritative_collision_surfaces_per_seam": 1,
                    "continuous": True,
                    "duplicate_authoritative_collision_surface": False,
                    "endcap_collision_enabled": False,
                    "endcap_collision_triangle_count": 0,
                    "endpoint_wheel_path_intrusion_m": 0.0,
                    "single_surface": True,
                },
                "connection": {
                    "destination_generated_overlap_m": 0.0,
                    "destination_grade_discontinuity": 0.0,
                    "destination_heading_error_degrees": 0.0,
                    "destination_position_gap_m": 0.0,
                    "destination_route_vs_decoded_surface_step_m": 0.0,
                    "destination_vertical_step_m": 0.0,
                    "destination_width_edge_error_m": 0.0,
                },
                "destination": {
                    "existing_lanes_preserved": True,
                    "generated_overlap_length_m": 0.0,
                    "lane_handoff": {
                        "carriageway": "positive-local-z",
                        "local_position_m": [-133.0, 0.2, 4.125],
                        "world_position_m": [6867.0, 0.2, 4022.125],
                    },
                    "median_local_z_m": [-0.7, 0.7],
                    "merge_width_m": 15.1,
                    "open_carriageways_local_z_m":
                        [[-7.55, -0.7], [0.7, 7.55]],
                    "outer_collision_bounds_local_z_m": [-8.15, 8.15],
                    "seam_m": [6867.0, 0.2, 4018.0],
                },
                "obstacle_avoidance": {
                    "destination_existing_lane_collision_preserved": True,
                    "destination_generated_overlap_m": 0.0,
                },
                "profile": {
                    "curb_free_approaches": True,
                    "destination_merge_width_m": 15.1,
                },
                "waypoints": waypoints,
            }
        }
    }


class NeoBridgeSceneTests(unittest.TestCase):
    def test_checked_fixtures_lock_ui_free_and_collision_contracts(
        self,
    ) -> None:
        static = SCENE.validate_fixture(
            REPOSITORY_ROOT / SCENE.STATIC_FIXTURE_PATH,
            drive=False,
        )
        drive = SCENE.validate_fixture(
            REPOSITORY_ROOT / SCENE.DRIVE_FIXTURE_PATH,
            drive=True,
        )
        self.assertEqual(static["path"], SCENE.STATIC_FIXTURE_PATH)
        self.assertEqual(drive["path"], SCENE.DRIVE_FIXTURE_PATH)

    def test_expected_side_piers_are_derived_as_exact_multiset(self) -> None:
        report = {
            "corridors": {
                "neoq_to_neoq20": {
                    "supports": {
                        "requested_count": 56,
                        "style": SCENE.SIDE_PIER_STYLE,
                    }
                },
                "penguinville_to_neoq": {
                    "supports": {
                        "requested_count": 46,
                        "style":
                            "ror-native-procedural-paired-outboard-piers-v1",
                    }
                },
            }
        }
        self.assertEqual(
            SCENE.expected_side_pier_counts(report),
            (46, 56),
        )

    def test_expected_side_piers_reject_contract_drift(self) -> None:
        report = {
            "corridors": {
                "neoq_to_neoq20": {
                    "supports": {
                        "requested_count": 56,
                        "style": SCENE.SIDE_PIER_STYLE,
                    }
                },
                "penguinville_to_neoq": {
                    "supports": {
                        "requested_count": 46,
                        "style":
                            "ror-native-procedural-paired-outboard-piers-v1",
                    }
                },
            }
        }
        cases = (
            (
                "missing-corridor",
                lambda value: value["corridors"].pop(
                    "penguinville_to_neoq"
                ),
                "two-bridge contract",
            ),
            (
                "extra-corridor",
                lambda value: value["corridors"].update(
                    {"unexpected": {"supports": {}}}
                ),
                "two-bridge contract",
            ),
            (
                "style",
                lambda value: value["corridors"]["penguinville_to_neoq"][
                    "supports"
                ].update({"style": SCENE.SIDE_PIER_STYLE}),
                "style drifted",
            ),
            (
                "count",
                lambda value: value["corridors"]["neoq_to_neoq20"][
                    "supports"
                ].update({"requested_count": 55}),
                "count is invalid",
            ),
        )
        for label, mutate, message in cases:
            candidate = copy.deepcopy(report)
            mutate(candidate)
            with (
                self.subTest(label=label),
                self.assertRaisesRegex(
                    SCENE.NeoBridgeSceneFailure,
                    message,
                ),
            ):
                SCENE.expected_side_pier_counts(candidate)

    def test_destination_join_contract_is_exact_and_fail_closed(self) -> None:
        contract = SCENE.validate_destination_join_contract(
            valid_destination_report()
        )
        self.assertEqual(contract["status"], "verified")
        self.assertFalse(contract["endcap_collision_enabled"])
        self.assertFalse(contract["generated_median_coverage"])

        cases = (
            (
                "overlap",
                ("connection", "destination_generated_overlap_m"),
                0.01,
                "not exactly zero",
            ),
            (
                "endcap",
                ("collision", "endcap_collision_enabled"),
                True,
                "open collision cap",
            ),
            (
                "median",
                ("destination", "median_local_z_m"),
                [-0.6, 0.6],
                "median, barriers, or live carriageways",
            ),
            (
                "lane-preservation",
                (
                    "obstacle_avoidance",
                    "destination_existing_lane_collision_preserved",
                ),
                False,
                "independent lane collision",
            ),
        )
        for label, path, value, message in cases:
            report = copy.deepcopy(valid_destination_report())
            bridge = report["corridors"]["neoq_to_neoq20"]
            bridge[path[0]][path[1]] = value
            with (
                self.subTest(label=label),
                self.assertRaisesRegex(
                    SCENE.NeoBridgeSceneFailure,
                    message,
                ),
            ):
                SCENE.validate_destination_join_contract(report)

    def test_static_runtime_accepts_six_ordered_captures(self) -> None:
        metrics = SCENE.validate_static_logs(
            0,
            "",
            valid_engine(drive=False),
            valid_static_script(),
            (46, 56),
        )
        self.assertEqual(metrics["captures"], 6)
        self.assertEqual(metrics["physics_steps"], 1060)
        self.assertEqual(
            metrics["side_piers"],
            [
                {"built": 46, "requested": 46, "skipped": 0},
                {"built": 56, "requested": 56, "skipped": 0},
            ],
        )

    def test_drive_runtime_accepts_both_seams_and_live_lane(self) -> None:
        metrics = SCENE.validate_drive_logs(
            0,
            "",
            valid_engine(drive=True),
            valid_drive_script(),
            (46, 56),
        )
        self.assertEqual(metrics["destination_local_z_m"], 4.9)
        self.assertEqual(metrics["reverse_destination_local_z_m"], 5.7)
        self.assertEqual(metrics["reverse_distance_m"], 65.0)
        self.assertEqual(metrics["physics_steps"], 420000)
        self.assertGreaterEqual(metrics["destination_x_m"], 6877.0)

    def test_runtime_rejects_missing_or_partial_side_pier_summary(
        self,
    ) -> None:
        engine = valid_engine((46, 56), drive=False)
        with self.assertRaisesRegex(
            SCENE.NeoBridgeSceneFailure,
            "multiset drifted",
        ):
            SCENE.validate_static_logs(
                0,
                "",
                engine.replace(
                    "requested=46 built=46 skipped=0",
                    "requested=46 built=45 skipped=1",
                ),
                valid_static_script(),
                (46, 56),
            )
        with self.assertRaisesRegex(
            SCENE.NeoBridgeSceneFailure,
            "logged a skip",
        ):
            SCENE.validate_static_logs(
                0,
                "",
                engine
                + "\n[RoR|ProceduralRoad|SidePiers] skip reason=terrain",
                valid_static_script(),
                (46, 56),
            )

    def test_drive_runtime_rejects_destination_outside_live_lane(
        self,
    ) -> None:
        with self.assertRaisesRegex(
            SCENE.NeoBridgeSceneFailure,
            "live-lane footprint",
        ):
            SCENE.validate_drive_logs(
                0,
                "",
                valid_engine(drive=True),
                valid_drive_script().replace(
                    "destination_local_z_m=4.9",
                    "destination_local_z_m=0.0",
                ),
                (46, 56),
            )

    def test_drive_runtime_rejects_reverse_destination_outside_live_lane(
        self,
    ) -> None:
        with self.assertRaisesRegex(
            SCENE.NeoBridgeSceneFailure,
            "westbound DAF left",
        ):
            SCENE.validate_drive_logs(
                0,
                "",
                valid_engine(drive=True),
                valid_drive_script().replace(
                    "reverse_destination_local_z_m=5.7",
                    "reverse_destination_local_z_m=0.0",
                ),
                (46, 56),
            )

    def test_build_command_is_cross_platform_and_script_specific(
        self,
    ) -> None:
        command = SCENE.build_command(
            Path("/opt/ror/RoR"),
            SCENE.DRIVE_SCRIPT_NAME,
        )
        self.assertEqual(
            command[-4:],
            (
                "-map",
                SCENE.OVERLAY_TERRAIN,
                "-runscript",
                SCENE.DRIVE_SCRIPT_NAME,
            ),
        )

    def test_isolated_runtime_layout_matches_packaging_forms(self) -> None:
        cases = (
            (
                "darwin",
                Path("/loose/bin/RoR"),
                Path("/isolated/RigsOfRods"),
                Path("/isolated/RigsOfRods/logs"),
            ),
            (
                "darwin",
                Path("/Applications/RoR.app/Contents/MacOS/RoR"),
                Path(
                    "/isolated/Library/Application Support/Rigs of Rods"
                ),
                Path("/isolated/Library/Logs/Rigs of Rods"),
            ),
            (
                "linux",
                Path("/opt/ror/RoR"),
                Path("/isolated/.rigsofrods"),
                Path("/isolated/.rigsofrods/logs"),
            ),
            (
                "win32",
                Path("C:/RoR/RoR.exe"),
                Path("/isolated/My Games/Rigs of Rods"),
                Path("/isolated/My Games/Rigs of Rods/logs"),
            ),
        )
        for target_platform, executable, user, logs in cases:
            with self.subTest(
                target_platform=target_platform,
                executable=executable,
            ):
                layout = SCENE.isolated_runtime_layout(
                    Path("/isolated"),
                    executable,
                    target_platform,
                )
                self.assertEqual(layout["user"], user)
                self.assertEqual(layout["logs"], logs)

    def test_six_static_rgb_hashes_must_be_distinct(self) -> None:
        records = [
            {"sha256": f"{index:064x}"}
            for index in range(6)
        ]
        SCENE.require_distinct_rgb_records(records)
        records[-1]["sha256"] = records[0]["sha256"]
        with self.assertRaisesRegex(
            SCENE.NeoBridgeSceneFailure,
            "not byte-distinct",
        ):
            SCENE.require_distinct_rgb_records(records)

    @staticmethod
    def rgb_fixture(colour: tuple[int, int, int]) -> bytearray:
        return bytearray(colour * (1280 * 720))

    @staticmethod
    def rgb_record() -> dict[str, object]:
        return {
            "height": 720,
            "sha256": "1" * 64,
            "width": 1280,
        }

    def test_ui_free_rgb_accepts_an_unobstructed_top_region(self) -> None:
        pixels = self.rgb_fixture((180, 190, 200))
        with mock.patch.object(
            SCENE.base,
            "decode_rgb_png",
            return_value=(self.rgb_record(), bytes(pixels)),
        ):
            record = SCENE.validate_ui_free_rgb_png(Path("/synthetic.png"))
        self.assertEqual(
            record["ui_free_top_menu_check"]["status"],
            "passed",
        )
        self.assertEqual(
            record["ui_free_top_menu_check"]["edge_contrast"],
            0.0,
        )

    def test_ui_free_rgb_rejects_a_bright_scene_top_menu(self) -> None:
        pixels = self.rgb_fixture((180, 190, 200))
        for x in SCENE.TOP_MENU_X_RANGE:
            offset = (SCENE.TOP_MENU_EDGE_ROWS[1] * 1280 + x) * 3
            pixels[offset : offset + 3] = bytes((20, 20, 20))
        with (
            mock.patch.object(
                SCENE.base,
                "decode_rgb_png",
                return_value=(self.rgb_record(), bytes(pixels)),
            ),
            self.assertRaisesRegex(
                SCENE.NeoBridgeSceneFailure,
                "hover-activated RoR top menu",
            ),
        ):
            SCENE.validate_ui_free_rgb_png(Path("/synthetic.png"))

    def test_ui_free_rgb_rejects_dark_scene_menu_text(self) -> None:
        pixels = self.rgb_fixture((30, 30, 30))
        injected = 0
        for y in SCENE.TOP_MENU_DARK_REGION_Y_RANGE:
            for x in SCENE.TOP_MENU_X_RANGE:
                if injected > SCENE.TOP_MENU_DARK_SCENE_NEUTRAL_BRIGHT_LIMIT:
                    break
                offset = (y * 1280 + x) * 3
                pixels[offset : offset + 3] = bytes((210, 210, 210))
                injected += 1
            if injected > SCENE.TOP_MENU_DARK_SCENE_NEUTRAL_BRIGHT_LIMIT:
                break
        with (
            mock.patch.object(
                SCENE.base,
                "decode_rgb_png",
                return_value=(self.rgb_record(), bytes(pixels)),
            ),
            self.assertRaisesRegex(
                SCENE.NeoBridgeSceneFailure,
                "hover-activated RoR top menu",
            ),
        ):
            SCENE.validate_ui_free_rgb_png(Path("/synthetic.png"))


if __name__ == "__main__":
    unittest.main()
