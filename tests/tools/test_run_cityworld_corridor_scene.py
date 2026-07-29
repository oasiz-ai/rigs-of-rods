#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_corridor_scene.py"
FIXTURE_PATH = (
    REPOSITORY_ROOT
    / "tests/fixtures/cityworld_corridor_runtime/"
    "cityworld_corridor_runtime.as"
)

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_corridor_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld corridor runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCENE
SPEC.loader.exec_module(SCENE)


def valid_logs() -> tuple[str, str]:
    light_marker = (
        "[RoR|TerrainObject|Lights] "
        "odef=rorng_city_led_streetlight_bridge.odef "
        "spotlights=0 point_lights=1 local_shadow_casters=0"
    )
    engine = "\n".join(
        (
            *SCENE.ENGINE_MARKERS,
            "[RoR|TerrainDependency] Mounted "
            "'/isolated/mods/CityWorld.zip' into "
            "'{bundle USER:/mods/CityWorldNextLocalOverlay.zip}'",
            SCENE.CITYWORLD_NAME,
            *(light_marker for _ in range(SCENE.EXPECTED_LIGHTS)),
        )
    )
    script = "\n".join(
        (
            SCENE.SCRIPT_MARKERS[0],
            "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901 "
            "heading=1.5708 station=-9.8439 cross_track=0.65625 "
            "height=1.50233",
            SCENE.SCRIPT_MARKERS[2],
            SCENE.SCRIPT_MARKERS[3],
            SCENE.SCRIPT_MARKERS[4],
            SCENE.SCRIPT_MARKERS[5],
            "[RoR|CW2|CorridorRuntime] PASS seams=2 "
            "route_m=1075.447727259 distance_m=1097.64 "
            "path_error_m=0.912104 vertical_error_m=0.772327 "
            "regression_m=0.00750732 speed_mps=14.3433 "
            "physics_steps=170960",
        )
    )
    return engine, script


def report_matching_script() -> dict[str, object]:
    text = FIXTURE_PATH.read_text(encoding="utf-8")
    path_match = SCENE.re.search(
        r"array<vector3>\s+gPath\s*=\s*\{(?P<body>.*?)\};",
        text,
        flags=SCENE.re.DOTALL,
    )
    station_match = SCENE.re.search(
        r"array<float>\s+gStation\s*=\s*\{(?P<body>.*?)\};",
        text,
        flags=SCENE.re.DOTALL,
    )
    if path_match is None or station_match is None:
        raise RuntimeError("fixture path arrays are missing")
    vectors = [
        [float(match.group(key)) for key in ("x", "y", "z")]
        for match in SCENE.VECTOR_PATTERN.finditer(path_match.group("body"))
    ]
    stations = [
        float(value[:-1])
        for value in SCENE.FLOAT_PATTERN.findall(station_match.group("body"))
    ]
    return {
        "corridor": {
            "waypoints": [
                {
                    "index": index,
                    "position_m": vectors[index + 1],
                    "station_m": stations[index + 1],
                }
                for index in range(SCENE.EXPECTED_WAYPOINTS)
            ]
        }
    }


def synthetic_light_candidate_manifest() -> dict[str, object]:
    families = (
        ["luminariaLQr"] * 42
        + ["luminariaQr"] * 25
    )
    candidates = []
    for index, family in enumerate(families):
        line = 100 + index
        adapter = SCENE.NEOQ_LIGHT_ADAPTERS[family]
        candidates.append(
            {
                "adapter": {
                    "coordinate_system": "legacy-odef-local-z-up",
                    "future_object_definition":
                        adapter["future_object_definition"],
                    "light_only_mesh_header": "none",
                    "local_light_position_m":
                        adapter["local_light_position_m"],
                    "runtime_definition_emitted": False,
                },
                "candidate_id": f"neoq-core-light-line-{line:06d}",
                "light": {
                    "color_rgb": [1.0, 0.72, 0.3],
                    "hard_max_range_m": 24.0,
                    "representative_lights": 1,
                    "shadow_casting_requested": False,
                    "type": "point",
                },
                "source": {
                    "distance_from_telepoint_m": round(index * 0.1, 9),
                    "line": line,
                    "object": family,
                    "position_m": [
                        2425.0 + index * 0.1,
                        0.3,
                        1013.0,
                    ],
                    "rotation_degrees": [0.0, 0.0, 0.0],
                },
            }
        )
    return {
        "activation": copy.deepcopy(SCENE.NEOQ_ACTIVATION_CONTRACT),
        "candidate_family_counts":
            copy.deepcopy(SCENE.NEOQ_CANDIDATE_FAMILY_COUNTS),
        "candidate_poles": 67,
        "candidate_runtime_point_lights": 67,
        "candidates": candidates,
        "format": SCENE.NEOQ_LIGHT_CANDIDATE_FORMAT,
        "policy_contract": {
            "hard_max_range_m": 24.0,
            "maximum_candidate_lights": 67,
            "policy_id": SCENE.NEOQ_LIGHT_POLICY_ID,
            "required_local_shadow_casters": 0,
            "sampling_strategy":
                "one-bounded-representative-light-per-existing-pole",
        },
        "scope": {
            "map_family_counts":
                copy.deepcopy(SCENE.NEOQ_MAP_FAMILY_COUNTS),
            "radius_m": 400.0,
            "source_telepoint": SCENE.NEOQ_LIGHT_TELEPOINT,
            "source_telepoint_position_m":
                SCENE.NEOQ_LIGHT_TELEPOINT_POSITION_M,
        },
        "visual_geometry": {
            "duplicate_pole_geometry_emitted": False,
            "existing_cityworld_poles_reused": True,
            "future_adapter_mesh_header": "none",
        },
    }


def synthetic_light_candidate_payload() -> bytes:
    return (
        json.dumps(
            synthetic_light_candidate_manifest(),
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def synthetic_overlay_report(
    repository: Path,
    payload: bytes,
) -> dict[str, object]:
    tool_records = []
    for relative in sorted(SCENE.REQUIRED_OVERLAY_TOOLS):
        tool = repository / relative
        tool.parent.mkdir(parents=True, exist_ok=True)
        tool.write_bytes(("project-owned " + relative + "\n").encode())
        tool_records.append(
            {
                "path": relative,
                "sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
            }
        )
    covered = 1075.447727259
    waypoints = report_matching_script()["corridor"]["waypoints"]
    candidate_manifest = synthetic_light_candidate_manifest()
    candidate_payload = synthetic_light_candidate_payload()
    candidate_record = {
        "path": SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER,
        "role": "disabled-light-candidate-manifest",
        "sha256": hashlib.sha256(candidate_payload).hexdigest(),
        "size": len(candidate_payload),
    }
    return {
        "format": SCENE.OVERLAY_REPORT_FORMAT,
        "source": {
            "archive": {"sha256": SCENE.CITYWORLD_SHA256},
            "references": {
                "overlay_placements": SCENE.OVERLAY_PLACEMENT_MEMBER,
                "resource_bundle_dependency": (
                    "CityWorld.zip:CityWorld.terrn2:"
                    + SCENE.CITYWORLD_SHA256
                )
            },
        },
        "rights": {
            "local_only": True,
            "redistribution_allowed": False,
            "shipping_allowed": False,
            "source_archive_copied": False,
            "source_geometry_copied": False,
            "source_objects_copied": False,
            "source_placement_payload_copied": False,
            "source_placement_records_derived": True,
            "source_placements_copied": False,
            "derived_source_placement_record_count": 67,
            "source_textures_copied": False,
        },
        "package": {
            "entries": 3,
            "files": [
                {
                    "path": SCENE.OVERLAY_PLACEMENT_MEMBER,
                    "role": "overlay-placement",
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "size": len(payload),
                },
                candidate_record,
            ],
        },
        "tools": tool_records,
        "city_lighting": {
            "neoq_core": {
                "activation": candidate_manifest["activation"],
                "candidate_family_counts":
                    candidate_manifest["candidate_family_counts"],
                "candidate_manifest": candidate_record,
                "candidate_poles": 67,
                "candidate_runtime_point_lights": 67,
                "policy_contract": candidate_manifest["policy_contract"],
                "scope": candidate_manifest["scope"],
                "source_pole_definitions":
                    copy.deepcopy(SCENE.NEOQ_SOURCE_POLE_DEFINITIONS),
                "visual_geometry": candidate_manifest["visual_geometry"],
            }
        },
        "corridor": {
            "format": "ror-cityworld-intercity-corridor-v3",
            "covered_centerline_length_m": covered,
            "waypoints": waypoints,
            "connection": {
                "source_position_gap_m": 0.0,
                "source_heading_error_degrees": 0.0,
                "destination_position_gap_m": 0.0,
                "destination_heading_error_degrees": 0.0,
            },
            "fixtures": {
                "instance_count": SCENE.EXPECTED_LIGHTS,
                "runtime_point_lights_per_instance": 1,
                "collision_authority": "native-procedural-road-v3",
            },
            "profile": {
                "width_m": 8.9,
                "sampled_maximum_grade": 0.073,
                "surface_offset_m": 0.08,
            },
            "supports": {
                "enabled": True,
                "requested_count": 47,
                "terrain_contact_resolved_at_runtime": True,
            },
            "source": {
                "position_m": [480.0, 0.198, 370.0],
                "apron": {
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
            },
        },
        "visual_asset_usage": {
            "corridor_placement_mode":
                "native-procedural-v3-curb-cut-with-blender-fixtures-v1",
            "disabled_light_candidate_manifest":
                SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER,
            "neoq_core_runtime_light_activation": "blocked-fail-closed",
            "purpose": SCENE.EXPECTED_VISUAL_PURPOSE,
            "packaged_asset_ids": ["rorng_city_led_streetlight_bridge"],
            "placed_asset_ids": ["rorng_city_led_streetlight_bridge"],
            "unplaced_asset_ids": SCENE.EXPECTED_UNPLACED_ASSETS,
            "validated_asset_ids": (
                SCENE.EXPECTED_UNPLACED_ASSETS
                + ["rorng_city_led_streetlight_bridge"]
            ),
        },
    }


def write_overlay(path: Path, report: dict[str, object], payload: bytes) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(
            SCENE.OVERLAY_REPORT_MEMBER,
            json.dumps(report, sort_keys=True, separators=(",", ":")),
        )
        archive.writestr(SCENE.OVERLAY_PLACEMENT_MEMBER, payload)
        archive.writestr(
            SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER,
            synthetic_light_candidate_payload(),
        )


class CityWorldCorridorSceneTests(unittest.TestCase):
    def test_lighting_policy_markers_are_fail_closed(self) -> None:
        engine, script = valid_logs()
        self.assertEqual(
            SCENE.CITYWORLD_FALLBACK_LIGHTING_MARKER,
            "[RoR|Terrain|Lighting] policy=fallback-v1 "
            "ambient_scale=0.350 directional_shadow_casters=1 "
            "ambient_rgb=0.326,0.301,0.266",
        )
        self.assertIn(SCENE.CITYWORLD_FALLBACK_LIGHTING_MARKER, engine)
        self.assertEqual(
            engine.count("local_shadow_casters=0"),
            SCENE.EXPECTED_LIGHTS,
        )
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine.replace(
                    SCENE.CITYWORLD_FALLBACK_LIGHTING_MARKER,
                    "",
                ),
                script,
            )
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine + "\n" + SCENE.CITYWORLD_FALLBACK_LIGHTING_MARKER,
                script,
            )
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine.replace(
                    "local_shadow_casters=0",
                    "local_shadow_casters=1",
                ),
                script,
            )

    def test_runtime_log_gate_requires_both_seams_and_physical_bounds(
        self,
    ) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertAlmostEqual(metrics["armed_station_m"], -9.8439)
        self.assertAlmostEqual(metrics["distance_m"], 1097.64)
        self.assertAlmostEqual(metrics["path_error_m"], 0.912104)
        self.assertEqual(metrics["physics_steps"], 170960)
        for marker in SCENE.SCRIPT_MARKERS:
            with self.subTest(marker=marker):
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(marker, ""),
                    )

    def test_runtime_log_gate_rejects_shortcut_and_unstable_metrics(self) -> None:
        engine, script = valid_logs()
        replacements = (
            ("station=-9.8439", "station=-2"),
            ("distance_m=1097.64", "distance_m=800"),
            ("path_error_m=0.912104", "path_error_m=2.1"),
            ("vertical_error_m=0.772327", "vertical_error_m=1.6"),
            ("regression_m=0.00750732", "regression_m=1.1"),
            ("speed_mps=14.3433", "speed_mps=0"),
            ("physics_steps=170960", "physics_steps=99999"),
        )
        for old, new in replacements:
            with self.subTest(value=new):
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine,
                        script.replace(old, new),
                    )

    def test_fixture_route_matches_overlay_waypoint_contract(self) -> None:
        report = report_matching_script()
        record = SCENE.validate_script_route(FIXTURE_PATH, report)
        self.assertEqual(record["samples"], 61)
        self.assertEqual(record["path"], SCENE.FIXTURE_PATH)
        changed = copy.deepcopy(report)
        changed["corridor"]["waypoints"][30]["position_m"][0] += 0.01
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_script_route(FIXTURE_PATH, changed)
        script = FIXTURE_PATH.read_text(encoding="utf-8")
        for marker in (
            'const uint64 MAX_PHYSICS_STEPS = 240000;',
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            '{"position", vector3(470.0f, 2.1f, 370.0f)}',
            "const float SOURCE_SEAM_STATION = 14.8491f;",
            'Fail("spawn-not-inside-penguinville-road-"',
            "gClosestStation >= DESTINATION_SEAM_STATION",
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)

    def test_overlay_validation_is_complete_and_tool_pinned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "repository"
            repository.mkdir()
            payload = b"compiled overlay payload"
            report = synthetic_overlay_report(repository, payload)
            archive = root / SCENE.OVERLAY_NAME
            write_overlay(archive, report, payload)
            validated, record = SCENE.validate_overlay_archive(
                archive,
                repository,
            )
            self.assertEqual(validated["format"], report["format"])
            self.assertEqual(record["name"], SCENE.OVERLAY_NAME)
            self.assertEqual(record["size"], archive.stat().st_size)

            changed = copy.deepcopy(report)
            changed["package"]["files"][0]["sha256"] = "0" * 64
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["connection"]["source_position_gap_m"] = 0.1
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["source"]["apron"]["curb_clearance_m"] = 0.0
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["waypoints"][2]["position_m"][1] = 0.3
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["city_lighting"]["neoq_core"][
                "source_pole_definitions"
            ][0]["sha256"] = "0" * 64
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["visual_asset_usage"]["purpose"] = "stale v3 purpose"
            write_overlay(archive, changed, payload)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

    def test_neoq_candidate_manifest_is_exact_and_fail_closed(self) -> None:
        manifest = synthetic_light_candidate_manifest()
        validated = SCENE.validate_neoq_light_candidates(manifest)
        self.assertEqual(validated["candidate_poles"], 67)
        self.assertTrue(
            validated["activation"]["contracts"]["zero_local_shadow"][
                "satisfied"
            ]
        )
        cases: list[tuple[str, dict[str, object]]] = []

        changed = copy.deepcopy(manifest)
        changed["activation"]["enabled"] = True
        cases.append(("activation", changed))
        changed = copy.deepcopy(manifest)
        changed["activation"]["contracts"]["zero_local_shadow"][
            "satisfied"
        ] = False
        cases.append(("zero-shadow", changed))
        changed = copy.deepcopy(manifest)
        changed["candidates"][0]["light"]["hard_max_range_m"] = 24.001
        cases.append(("range", changed))
        changed = copy.deepcopy(manifest)
        changed["candidates"][0]["light"][
            "shadow_casting_requested"
        ] = True
        cases.append(("shadow-request", changed))
        changed = copy.deepcopy(manifest)
        changed["candidates"][0]["adapter"][
            "runtime_definition_emitted"
        ] = True
        cases.append(("adapter-emission", changed))
        changed = copy.deepcopy(manifest)
        changed["candidates"][1]["source"]["line"] = (
            changed["candidates"][0]["source"]["line"]
        )
        cases.append(("duplicate-line", changed))
        changed = copy.deepcopy(manifest)
        changed["candidates"][0]["source"]["object"] = "luminariaYQr"
        cases.append(("family-count", changed))
        changed = copy.deepcopy(manifest)
        changed["unexpected"] = True
        cases.append(("unexpected-field", changed))

        for name, changed in cases:
            with self.subTest(name=name):
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.validate_neoq_light_candidates(changed)

    def test_cityworld_and_vehicle_archives_are_authenticated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cityworld = root / SCENE.CITYWORLD_NAME
            with zipfile.ZipFile(cityworld, "w") as archive:
                for name in (
                    "CityWorld.terrn2",
                    "CityWorld.otc",
                    "CityWorld.tobj",
                ):
                    archive.writestr(name, name)
            with mock.patch.object(
                SCENE,
                "sha256_file",
                return_value=SCENE.CITYWORLD_SHA256,
            ):
                record = SCENE.validate_cityworld_archive(cityworld)
            self.assertEqual(record["sha256"], SCENE.CITYWORLD_SHA256)

            runtime = root / "runtime"
            runtime.mkdir()
            truck = b"pinned DAF"
            with zipfile.ZipFile(runtime / SCENE.VEHICLE_ARCHIVE, "w") as archive:
                archive.writestr(SCENE.VEHICLE_ENTRY, truck)
            with mock.patch.object(
                SCENE,
                "VEHICLE_ENTRY_SHA256",
                hashlib.sha256(truck).hexdigest(),
            ):
                vehicle = SCENE.verify_vehicle_archive(runtime)
            self.assertEqual(vehicle["entry"], SCENE.VEHICLE_ENTRY)

    def test_overlay_rebuild_must_be_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "repository"
            builder = (
                repository / "tools/build_cityworld_local_overlay.py"
            )
            builder.parent.mkdir(parents=True)
            builder.write_text("# deterministic builder\n", encoding="utf-8")
            cityworld = root / SCENE.CITYWORLD_NAME
            cityworld.write_bytes(b"authenticated CityWorld")
            overlay = root / SCENE.OVERLAY_NAME
            overlay.write_bytes(b"deterministic overlay bytes")
            report = {
                "corridor": {
                    "profile": {
                        "surface_offset_m": 0.08,
                    },
                },
            }

            def rebuild(
                command: tuple[str, ...],
                timeout: int,
                *,
                cwd: Path,
            ) -> subprocess.CompletedProcess[bytes]:
                self.assertEqual(timeout, 60)
                self.assertEqual(cwd, repository)
                output = Path(command[command.index("--output") + 1])
                output.write_bytes(overlay.read_bytes())
                return subprocess.CompletedProcess(command, 0, b"ok")

            with mock.patch.object(
                SCENE.base,
                "run_command",
                side_effect=rebuild,
            ):
                record = SCENE.verify_overlay_rebuild(
                    cityworld,
                    overlay,
                    report,
                    repository,
                    60,
                )
            self.assertTrue(record["byte_identical"])
            self.assertEqual(
                record["sha256"],
                hashlib.sha256(overlay.read_bytes()).hexdigest(),
            )

            def rebuild_different(
                command: tuple[str, ...],
                timeout: int,
                *,
                cwd: Path,
            ) -> subprocess.CompletedProcess[bytes]:
                output = Path(command[command.index("--output") + 1])
                output.write_bytes(b"different overlay bytes")
                return subprocess.CompletedProcess(command, 0, b"ok")

            with mock.patch.object(
                SCENE.base,
                "run_command",
                side_effect=rebuild_different,
            ):
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.verify_overlay_rebuild(
                        cityworld,
                        overlay,
                        report,
                        repository,
                        60,
                    )

    def test_command_and_layout_are_cross_platform(self) -> None:
        executable = Path("/runtime/RoR")
        with mock.patch.object(SCENE.sys, "platform", "darwin"):
            self.assertEqual(
                SCENE.build_command(executable),
                (
                    str(executable),
                    "-ApplePersistenceIgnoreState",
                    "YES",
                    "-map",
                    SCENE.OVERLAY_TERRAIN,
                    "-runscript",
                    SCENE.SCRIPT_NAME,
                ),
            )
        for target in ("linux", "win32"):
            with self.subTest(platform=target):
                with mock.patch.object(SCENE.sys, "platform", target):
                    self.assertEqual(
                        SCENE.build_command(executable),
                        (
                            str(executable),
                            "-map",
                            SCENE.OVERLAY_TERRAIN,
                            "-runscript",
                            SCENE.SCRIPT_NAME,
                        ),
                    )
                layout = SCENE.base.runtime_layout(Path("/isolated"), target)
                for path in layout.values():
                    self.assertTrue(path.is_relative_to(Path("/isolated")))

    def test_incomplete_overlay_diagnostic_requires_explicit_opt_in(
        self,
    ) -> None:
        common = (
            "--executable",
            "/runtime/RoR",
            "--cityworld-archive",
            "/private/CityWorld.zip",
            "--overlay-archive",
            "/private/CityWorldNextLocalOverlay.zip",
            "--artifact-dir",
            "/artifacts",
        )
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                SCENE.parse_args(common)
        args = SCENE.parse_args(
            common + ("--diagnostic-allow-incomplete-overlay",)
        )
        self.assertTrue(args.diagnostic_allow_incomplete_overlay)

    def test_staged_inputs_and_artifact_publication_are_fail_closed(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged_input = root / "CityWorld.zip"
            staged_input.write_bytes(b"validated bytes")
            expected = hashlib.sha256(staged_input.read_bytes()).hexdigest()
            SCENE.verify_staged_file(staged_input, expected, "CityWorld")
            staged_input.write_bytes(b"changed during staging")
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.verify_staged_file(staged_input, expected, "CityWorld")

            staging = root / ".artifacts.partial-1"
            staging.mkdir()
            (staging / "report.json").write_text("complete\n", encoding="utf-8")
            published = root / "artifacts"
            SCENE.publish_artifact_directory(staging, published)
            self.assertFalse(staging.exists())
            self.assertEqual(
                (published / "report.json").read_text(encoding="utf-8"),
                "complete\n",
            )
            another_staging = root / ".artifacts.partial-2"
            another_staging.mkdir()
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.publish_artifact_directory(
                    another_staging,
                    published,
                )


if __name__ == "__main__":
    unittest.main()
