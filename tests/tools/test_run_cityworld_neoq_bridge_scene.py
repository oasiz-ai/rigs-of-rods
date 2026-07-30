#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


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
    side_pier_counts: tuple[int, ...] = (74,),
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
        "[RoR|CityWorld|NeoQ20Grounding] Applied placements=35 renames=3 "
        "telepoints=1 tree_replacements=18 transactionally before object "
        "instantiation (tobj_sha256="
        + SCENE.corridor.NEOQ_TREE_SOURCE_TOBJ_SHA256
        + ")",
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
        lines.append("===== LOADING VEHICLE: b6b0UID-semi.truck")
    return "\n".join(lines)


def valid_static_script() -> str:
    return "\n".join(
        (
            *SCENE.STATIC_MARKERS,
            "[RoR|CW2|NeoBridgeRuntime] PASS cameras=6 frames=265 "
            "physics_steps=1060 route_m=3076.132100441 "
            "supports=74 lights=33",
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
            "[RoR|CW2|NeoBridgeDrive] PASS seams=2 "
            "route_m=3076.132100441 station_m=3076.1321 "
            "destination_x_m=6877.2 destination_local_z_m=4.9 "
            "distance_m=3091.8 path_error_m=0.8 "
            "vertical_error_m=0.7 regression_m=0.02 "
            "speed_mps=9.2 physics_steps=420000",
        )
    )


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
                        "requested_count": 74,
                        "style": SCENE.SIDE_PIER_STYLE,
                    }
                },
                "penguinville_to_neoq": {
                    "supports": {
                        "requested_count": 46,
                        "style": SCENE.SIDE_PIER_STYLE,
                    }
                },
            }
        }
        self.assertEqual(
            SCENE.expected_side_pier_counts(report),
            (46, 74),
        )

    def test_static_runtime_accepts_six_ordered_captures(self) -> None:
        metrics = SCENE.validate_static_logs(
            0,
            "",
            valid_engine(drive=False),
            valid_static_script(),
            (74,),
        )
        self.assertEqual(metrics["captures"], 6)
        self.assertEqual(metrics["physics_steps"], 1060)
        self.assertEqual(
            metrics["side_piers"],
            [{"built": 74, "requested": 74, "skipped": 0}],
        )

    def test_drive_runtime_accepts_both_seams_and_live_lane(self) -> None:
        metrics = SCENE.validate_drive_logs(
            0,
            "",
            valid_engine(drive=True),
            valid_drive_script(),
            (74,),
        )
        self.assertEqual(metrics["destination_local_z_m"], 4.9)
        self.assertEqual(metrics["physics_steps"], 420000)
        self.assertGreaterEqual(metrics["destination_x_m"], 6877.0)

    def test_runtime_rejects_missing_or_partial_side_pier_summary(
        self,
    ) -> None:
        engine = valid_engine((46, 74), drive=False)
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
                (46, 74),
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
                (46, 74),
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
                (74,),
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


if __name__ == "__main__":
    unittest.main()
