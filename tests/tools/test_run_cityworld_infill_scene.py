#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_infill_scene.py"
FIXTURE_PATH = (
    REPOSITORY_ROOT
    / "tests/fixtures/cityworld_infill_runtime/"
    "cityworld_infill_runtime.as"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_infill_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld infill runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)
BUILDER = SCENE.load_module(
    "cityworld_infill_builder_for_scene_test",
    REPOSITORY_ROOT / "tools/build_cityworld_local_overlay.py",
)


def canonical_overlay_contract() -> tuple[bytes, dict[str, object]]:
    payload = SCENE.infill.canonical_manifest_bytes()
    manifest = json.loads(payload)
    record = {
        "path": SCENE.INFILL_MANIFEST_MEMBER,
        "role": SCENE.INFILL_MANIFEST_ROLE,
        "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload),
    }
    report = {
        "format": SCENE.OVERLAY_REPORT_FORMAT,
        "package": {"files": [record]},
        "regional_infill": {
            "audit": manifest["audit"],
            "manifest": record,
            "source_authentication":
                SCENE.expected_source_authentication(manifest),
            "summary": manifest["audit"]["summary"],
        },
        "rights": {
            "derived_source_placement_record_count":
                SCENE.EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS,
        },
        "source": {
            "references": {
                "regional_infill_manifest":
                    SCENE.INFILL_MANIFEST_MEMBER,
            },
        },
    }
    return payload, report


def write_manifest_archive(path: Path, payload: bytes) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(SCENE.INFILL_MANIFEST_MEMBER, payload)


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(
        (
            SCENE.corridor.CITYWORLD_FALLBACK_LIGHTING_MARKER,
            SCENE.corridor.ENGINE_MARKERS[1],
            "[RoR|ProceduralRoad|SidePiers] "
            "requested=46 built=46 skipped=0",
            "[RoR|ProceduralRoad|SidePiers] "
            "requested=56 built=56 skipped=0",
            "===== TERRAIN LOADING DONE "
            "CityWorldNextLocalOverlay.terrn2",
            "[RoR|TerrainDependency] Mounted "
            "'/isolated/mods/CityWorld.zip' into "
            "'{bundle USER:/mods/CityWorldNextLocalOverlay.zip}'",
            *(
                SCENE.BRIDGE_LIGHT_MARKER
                for _ in range(SCENE.corridor.EXPECTED_LIGHTS)
            ),
            SCENE.STATION_LIGHT_MARKER,
            SCENE.STATION_LIGHT_MARKER,
        )
    )
    script = "\n".join(
        (
            *SCENE.SCRIPT_MARKERS,
            "[RoR|CW2|InfillRuntime] PASS cameras=8 hold_frames=40 "
            "frames=345 physics_steps=1380 placements=46 routes=7 "
            "stations=2 station_lights=12",
        )
    )
    return engine, script


class CityWorldInfillSceneTests(unittest.TestCase):
    def test_fixture_pins_all_eight_ui_hidden_40_frame_views(self) -> None:
        record = SCENE.validate_fixture(FIXTURE_PATH)
        self.assertEqual(record["cameras"], 8)
        self.assertEqual(record["capture_hold_frames"], 40)
        text = FIXTURE_PATH.read_text(encoding="utf-8")
        self.assertIn(
            'console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");',
            text,
        )
        self.assertIn(
            'console.cVarSet("ui_hide_gui", "true");',
            text,
        )
        self.assertIn(
            "gReadyFrames == (gCaptures + 1) * CAPTURE_HOLD_FRAMES",
            text,
        )
        for capture_id, position, target in SCENE.CAMERA_CONTRACT:
            with self.subTest(capture_id=capture_id):
                self.assertIn(f'"{capture_id}"', text)
                self.assertIn(position, text)
                self.assertIn(target, text)

    def test_embedded_manifest_must_be_exact_canonical_plan(self) -> None:
        payload = SCENE.infill.canonical_manifest_bytes()
        manifest = SCENE.validate_manifest_payload(payload)
        self.assertEqual(manifest["format"], SCENE.infill.FORMAT)
        self.assertEqual(
            manifest["audit"]["summary"],
            {
                "access_routes": 7,
                "assets": 5,
                "placements": 46,
                "placements_by_category": {
                    "farmland": 13,
                    "natural-landmark": 14,
                    "service-station": 2,
                    "suburb": 17,
                },
                "sites": 8,
                "sites_by_category": {
                    "farmland": 2,
                    "natural-landmark": 2,
                    "service-station": 2,
                    "suburb": 2,
                },
            },
        )
        with self.assertRaisesRegex(
            SCENE.InfillSceneFailure,
            "differs from the canonical",
        ):
            SCENE.validate_manifest_payload(payload + b" ")

    def test_overlay_requires_v6_manifest_record_reference_and_audit(
        self,
    ) -> None:
        payload, report = canonical_overlay_contract()
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / SCENE.OVERLAY_NAME
            write_manifest_archive(archive, payload)
            record = SCENE.validate_overlay_infill(archive, report)
            self.assertEqual(record["format"], SCENE.infill.FORMAT)
            self.assertEqual(record["summary"]["placements"], 46)
            self.assertEqual(
                record["manifest"]["role"],
                SCENE.INFILL_MANIFEST_ROLE,
            )

            mutations = []
            changed = copy.deepcopy(report)
            changed["format"] = "ror-cityworld-local-overlay-v5"
            mutations.append(("version", changed))
            changed = copy.deepcopy(report)
            changed["source"]["references"]["regional_infill_manifest"] = (
                "other.json"
            )
            mutations.append(("source-reference", changed))
            changed = copy.deepcopy(report)
            changed["package"]["files"][0]["role"] = "data"
            changed["regional_infill"]["manifest"] = (
                changed["package"]["files"][0]
            )
            mutations.append(("role", changed))
            changed = copy.deepcopy(report)
            changed["regional_infill"]["summary"]["placements"] = 45
            mutations.append(("summary", changed))
            changed = copy.deepcopy(report)
            changed["regional_infill"]["unexpected"] = True
            mutations.append(("regional-fields", changed))
            changed = copy.deepcopy(report)
            changed["regional_infill"]["source_authentication"][
                "native_anchor_count"
            ] = 5
            mutations.append(("source-authentication", changed))
            changed = copy.deepcopy(report)
            changed["rights"][
                "derived_source_placement_record_count"
            ] = 137
            mutations.append(("source-derived-placement-rights", changed))
            for label, mutation in mutations:
                with self.subTest(label=label):
                    with self.assertRaises(SCENE.InfillSceneFailure):
                        SCENE.validate_overlay_infill(
                            archive,
                            mutation,
                        )

            changed_payload = payload + b" "
            write_manifest_archive(archive, changed_payload)
            changed = copy.deepcopy(report)
            changed_record = changed["package"]["files"][0]
            changed_record["sha256"] = hashlib.sha256(
                changed_payload
            ).hexdigest()
            changed_record["size"] = len(changed_payload)
            changed["regional_infill"]["manifest"] = changed_record
            with self.assertRaisesRegex(
                SCENE.InfillSceneFailure,
                "differs from the canonical",
            ):
                SCENE.validate_overlay_infill(archive, changed)

    def test_source_authentication_matches_builder_exactly(self) -> None:
        plan = BUILDER.regional_infill.build_infill_plan()
        manifest = BUILDER.regional_infill.build_manifest(plan)
        expected = SCENE.expected_source_authentication(manifest)
        line_0378 = expected["line_0378"]
        line_1354 = expected["line_1354"]
        placements = (
            BUILDER.SourcePlacement(
                line_number=line_0378["line_number"],
                position=tuple(line_0378["position_m"]),
                rotation_degrees=tuple(
                    line_0378["rotation_degrees"]
                ),
                object_name=line_0378["object"],
            ),
            BUILDER.SourcePlacement(
                line_number=line_1354["line_number"],
                position=tuple(line_1354["position_m"]),
                rotation_degrees=tuple(
                    line_1354["rotation_degrees"]
                ),
                object_name=line_1354["object"],
            ),
        )
        actual = BUILDER.authenticate_regional_infill_source(
            plan=plan,
            source_tobj_sha256=(
                BUILDER.regional_infill.PINNED_TOBJ_SHA256
            ),
            placements=placements,
            route_anchor_evidence={"source": line_1354},
            neoq_bridge_authentication={
                "ground_road": line_0378,
                "tobj": {
                    "name": BUILDER.regional_infill.PINNED_TOBJ_MEMBER,
                    "sha256":
                        BUILDER.regional_infill.PINNED_TOBJ_SHA256,
                },
            },
        )
        self.assertEqual(set(actual), set(expected))
        self.assertEqual(actual, expected)
        self.assertEqual(
            len(BUILDER.regional_infill_routes(plan)),
            SCENE.EXPECTED_ROUTES,
        )
        self.assertEqual(
            len(BUILDER.regional_infill_placements(plan)),
            SCENE.EXPECTED_PLACEMENTS,
        )
        self.assertEqual(
            SCENE.EXPECTED_DERIVED_SOURCE_PLACEMENT_RECORDS,
            67 + 19 + 5,
        )

    def test_runtime_requires_all_captures_and_exact_station_lights(
        self,
    ) -> None:
        engine, script = valid_logs()
        with mock.patch.object(
            SCENE.base,
            "parse_renderer_identity",
            return_value={"renderer": "test"},
        ):
            metrics = SCENE.validate_runtime_logs(
                0,
                "",
                engine,
                script,
                target_platform="linux",
            )
        self.assertEqual(metrics["captures"], 8)
        self.assertEqual(metrics["frames"], 345)
        self.assertEqual(metrics["physics_steps"], 1380)
        self.assertEqual(metrics["station_light_instances"], 2)
        self.assertEqual(metrics["station_lights"], 12)
        self.assertEqual(
            metrics["side_piers"],
            [[46, 46, 0], [56, 56, 0]],
        )

        failures = (
            (
                "missing-capture",
                engine,
                script.replace(SCENE.SCRIPT_MARKERS[4], ""),
            ),
            (
                "one-station",
                engine.replace(
                    SCENE.STATION_LIGHT_MARKER + "\n",
                    "",
                    1,
                ),
                script,
            ),
            (
                    "wrong-physics-steps",
                engine,
                script.replace(
                    "physics_steps=1380",
                    "physics_steps=1000",
                ),
            ),
            (
                "side-pier-drift",
                engine.replace(
                    "requested=56 built=56 skipped=0",
                    "requested=56 built=55 skipped=1",
                ),
                script,
            ),
        )
        for label, changed_engine, changed_script in failures:
            with self.subTest(label=label):
                with self.assertRaises(SCENE.InfillSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        changed_engine,
                        changed_script,
                        target_platform="linux",
                    )

    def test_runtime_rejects_ogre_gl_and_infill_material_errors(self) -> None:
        engine, script = valid_logs()
        markers = (
            "OGRE EXCEPTION",
            "GL_INVALID_OPERATION",
            "RenderingAPIException",
            "Error: material "
            "rorng_city_infill_suburb_block_96x88_stucco has no "
            "supportable Techniques and will be blank",
            "Cannot find rorng_city_infill_red_mesa_19m_rock_dark",
        )
        for marker in markers:
            with self.subTest(marker=marker):
                with self.assertRaises(SCENE.InfillSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine + "\n" + marker,
                        script,
                        target_platform="linux",
                    )

    def test_eight_rgb_screenshots_are_nonblank_distinct_and_regular(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, capture_id in enumerate(SCENE.RGB_CAPTURE_IDS):
                (root / f"{index:02d}-{capture_id}.png").write_bytes(
                    f"rgb-{index}".encode()
                )

            def validate(path: Path) -> dict[str, object]:
                return {
                    "height": 720,
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "size": path.stat().st_size,
                    "width": 1280,
                }

            with mock.patch.object(
                SCENE.base,
                "validate_rgb_png",
                side_effect=validate,
            ):
                paths, records = SCENE.validate_rgb_screenshots(root)
            self.assertEqual(len(paths), 8)
            self.assertEqual(list(records), list(SCENE.RGB_CAPTURE_IDS))

            with mock.patch.object(
                SCENE.base,
                "validate_rgb_png",
                return_value={
                    "height": 720,
                    "sha256": "same",
                    "size": 10,
                    "width": 1280,
                },
            ):
                with self.assertRaisesRegex(
                    SCENE.InfillSceneFailure,
                    "not distinct",
                ):
                    SCENE.validate_rgb_screenshots(root)

            (root / "unexpected.png").write_bytes(b"ninth")
            with self.assertRaisesRegex(
                SCENE.InfillSceneFailure,
                "exactly eight",
            ):
                SCENE.validate_rgb_screenshots(root)

    def test_command_and_isolated_layout_are_cross_platform(self) -> None:
        command_executable = Path("/runtime/RoR")
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
        for target, executable, expected_user, expected_logs in cases:
            with self.subTest(target=target, executable=executable):
                with mock.patch.object(SCENE.sys, "platform", target):
                    platform_flags = (
                        ("-ApplePersistenceIgnoreState", "YES")
                        if target == "darwin"
                        else ()
                    )
                    self.assertEqual(
                        SCENE.build_command(command_executable),
                        (
                            str(command_executable),
                            *platform_flags,
                            "-map",
                            SCENE.OVERLAY_TERRAIN,
                            "-runscript",
                            SCENE.SCRIPT_NAME,
                        ),
                    )
                layout = SCENE.isolated_runtime_layout(
                    Path("/isolated"),
                    executable,
                    target,
                )
                self.assertEqual(layout["user"], expected_user)
                self.assertEqual(layout["logs"], expected_logs)
                for path in layout.values():
                    self.assertTrue(
                        path.is_relative_to(Path("/isolated"))
                    )


if __name__ == "__main__":
    unittest.main()
