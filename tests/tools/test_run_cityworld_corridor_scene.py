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
BRIDGE = SCENE.load_module(
    "cityworld_neoq_intercity_bridge_for_corridor_scene_test",
    REPOSITORY_ROOT / "tools/cityworld_neoq_intercity_bridge.py",
)


def valid_logs() -> tuple[str, str]:
    light_marker = (
        "[RoR|TerrainObject|Lights] "
        "odef=rorng_city_led_streetlight_bridge.odef "
        "spotlights=0 point_lights=1 local_shadow_casters=0"
    )
    engine = "\n".join(
        (
            *SCENE.ENGINE_MARKERS,
            SCENE.ENGINE_MARKERS[-1],
            "[RoR|TerrainDependency] Mounted "
            "'/isolated/mods/CityWorld.zip' into "
            "'{bundle USER:/mods/CityWorldNextLocalOverlay.zip}'",
            SCENE.CITYWORLD_NAME,
            "[RoR|ProceduralRoad|SidePiers] "
            "requested=56 built=56 skipped=0",
            *(light_marker for _ in range(SCENE.EXPECTED_LIGHTS)),
        )
    )
    script = "\n".join(
        (
            SCENE.SCRIPT_MARKERS[0],
            "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901 "
            "direction=penguinville_to_neoq "
            "heading=1.5708 station=-19.8439 cross_track=0.65625 "
            "height=1.50233",
            *SCENE.SCRIPT_MARKERS[2:-1],
            "[RoR|CW2|CorridorRuntime] PASS seams=4 screenshots=4 "
            "traversals=2 route_m=1038.350024882 distance_m=2144.2 "
            "forward_distance_m=1068.1 reverse_distance_m=1076.1 "
            "path_error_m=0.912104 vertical_error_m=0.772327 "
            "regression_m=0.00750732 speed_mps=14.3433 "
            "physics_steps=340960",
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
) -> tuple[dict[str, object], dict[str, bytes]]:
    tool_records = []
    for relative in sorted(SCENE.REQUIRED_OVERLAY_TOOLS):
        tool = repository / relative
        tool.parent.mkdir(parents=True, exist_ok=True)
        if relative == SCENE.NEOQ_TREE_NATIVE_PLAN:
            tool.write_bytes((REPOSITORY_ROOT / relative).read_bytes())
        else:
            tool.write_bytes(("project-owned " + relative + "\n").encode())
        tool_records.append(
            {
                "path": relative,
                "sha256": hashlib.sha256(tool.read_bytes()).hexdigest(),
            }
        )
    family_path = repository / SCENE.NEOQ_TREE_FAMILY_MANIFEST
    family_path.parent.mkdir(parents=True, exist_ok=True)
    family_path.write_bytes(
        (REPOSITORY_ROOT / SCENE.NEOQ_TREE_FAMILY_MANIFEST).read_bytes()
    )
    covered = SCENE.EXPECTED_ROUTE_LENGTH_M
    waypoints = report_matching_script()["corridor"]["waypoints"]
    for index, waypoint in enumerate(waypoints):
        waypoint["road_type"] = (
            "bridge_side_pillars"
            if 4 <= index <= 49
            else (
                "bridge_no_pillars"
                if index in (3, 50)
                else "flat"
            )
        )
        waypoint["width_m"] = 9.75017 + (10.0 - 9.75017) * (
            waypoint["station_m"] / covered
        )
        waypoint["yaw_degrees"] = 0.0
    placement_lines = [
        "begin_procedural_roads",
        "    collision_enabled true",
        "    collision_endcaps_enabled false",
        *(
            f"    0, 0, 0, 0, 0, 0, 10, .45, .95, "
            f"{waypoint['road_type']}"
            for waypoint in waypoints
        ),
        "end_procedural_roads",
        (
            "516, 0.100001, 370.023095, 0, -90, 0, "
            "rorng_city_penguin_road_seam_12m - "
            "cityworld_next_penguin_road_seam_12m"
        ),
        "// " + payload.decode("utf-8", errors="replace"),
    ]
    payload = ("\n".join(placement_lines) + "\n").encode("utf-8")
    candidate_manifest = synthetic_light_candidate_manifest()
    candidate_payload = synthetic_light_candidate_payload()
    native_plan = SCENE.read_neoq_tree_native_plan(repository)
    package_payloads = {
        SCENE.OVERLAY_PLACEMENT_MEMBER: payload,
        SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER: candidate_payload,
    }
    package_roles = {
        SCENE.OVERLAY_PLACEMENT_MEMBER: "overlay-placement",
        SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER:
            "disabled-light-candidate-manifest",
    }
    material_lines = ["// synthetic merged material"]
    for variant in SCENE.NEOQ_TREE_VARIANTS:
        package_payloads[variant + ".odef"] = b"synthetic tree odef\n"
        package_roles[variant + ".odef"] = "terrain-object"
        for suffix, role in (
            ("_collision_fixture.mesh", "collision-fixture"),
            ("_lod0.mesh", "render-lod0"),
            ("_lod1.mesh", "render-lod1"),
            ("_lod2.mesh", "render-lod2"),
        ):
            name = variant + suffix
            package_payloads[name] = ("synthetic " + name + "\n").encode()
            package_roles[name] = role
        for suffix in ("bark", "foliage_dark", "foliage_light"):
            material_lines.extend(
                ("material " + variant + "_" + suffix, "{", "}", "")
            )
    material_name = "cityworld_next_local_overlay.material"
    package_payloads[material_name] = (
        "\n".join(material_lines) + "\n"
    ).encode()
    package_roles[material_name] = "material-fallback"

    replacements = []
    for ordinal, plan in enumerate(native_plan):
        scale = plan["scale"]
        variant = plan["variant"]
        wrapper_name = f"rorng_city_neoq_tree_instance_{ordinal:02d}.odef"
        wrapper_payload = (
            variant
            + "_lod0.mesh\n"
            + f"{scale}, {scale}, {scale}\n"
            + "standard\n\nbeginmesh\nmesh "
            + variant
            + "_collision_fixture.mesh\n"
            + "stdfriction concrete\nendmesh\n\nend\n"
        ).encode()
        package_payloads[wrapper_name] = wrapper_payload
        package_roles[wrapper_name] = "terrain-object-scale-wrapper"
        replacements.append(
            {
                "legacy_object": "arbol1Qr",
                "object_definition": plan["object_definition"],
                "ordinal": ordinal,
                "position_m": plan["position_m"],
                "position_preserved": True,
                "rotation_degrees": [0.0, plan["yaw_degrees"], 0.0],
                "scale": scale,
                "source_line": plan["source_line"],
                "source_rotation_degrees": plan["rotation_degrees"],
                "variant": variant,
                "wrapper": {
                    "path": wrapper_name,
                    "sha256": hashlib.sha256(wrapper_payload).hexdigest(),
                    "size": len(wrapper_payload),
                },
            }
        )
    tree_manifest = {
        "activation": {
            "duplicate_placements_emitted": 0,
            "fail_closed": True,
            "mode": "native-authenticated-in-place-replacement-v1",
            "requires_exact_archive_dependency": True,
            "requires_exact_tobj_sha256": True,
            "runtime_resource_preflight": "all-18-scale-wrapper-odefs",
        },
        "family": {
            "asset": {
                "author": "Oasiz AI and Rigs of Rods contributors",
                "id": "rorng_city_neoq_tree_family",
                "license": "GPL-3.0-or-later",
                "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
                "version": 1,
            },
            "family_manifest": {
                "path": SCENE.NEOQ_TREE_FAMILY_MANIFEST,
                "sha256": hashlib.sha256(family_path.read_bytes()).hexdigest(),
            },
            "native_plan": {
                "path": SCENE.NEOQ_TREE_NATIVE_PLAN,
                "sha256": hashlib.sha256(
                    (repository / SCENE.NEOQ_TREE_NATIVE_PLAN).read_bytes()
                ).hexdigest(),
            },
            "selector": {
                "algorithm": "sha256-little-endian-modulo-v1",
                "namespace": "cityworld:neoqueretaro:arbol1qr:v1",
            },
            "validation": {
                "format": "ror-cityworld-tree-family-validation-v1",
                "summary": {
                    "assets": 3,
                    "compiled_outputs": 18,
                    "errors": 0,
                    "placements": 18,
                    "silhouettes": 3,
                    "valid": True,
                },
            },
        },
        "format": SCENE.NEOQ_TREE_REPLACEMENT_FORMAT,
        "replacements": replacements,
        "source": {
            "legacy_object": "arbol1Qr",
            "placement_count": 18,
            "source_lines": [9, 26],
            "tobj": "CityWorld.tobj",
            "tobj_sha256": SCENE.NEOQ_TREE_SOURCE_TOBJ_SHA256,
        },
        "summary": {
            "collision_scale_matches_visual_scale": True,
            "positions_preserved": 18,
            "replacement_count": 18,
            "unique_scale_wrappers": 18,
            "variants": SCENE.NEOQ_TREE_VARIANTS,
        },
    }
    tree_payload = (
        json.dumps(
            tree_manifest,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode()
    package_payloads[SCENE.NEOQ_TREE_REPLACEMENT_MEMBER] = tree_payload
    package_roles[SCENE.NEOQ_TREE_REPLACEMENT_MEMBER] = (
        "authenticated-in-place-tree-replacement-plan"
    )
    package_records = [
        {
            "path": name,
            "role": package_roles[name],
            "sha256": hashlib.sha256(package_payload).hexdigest(),
            "size": len(package_payload),
        }
        for name, package_payload in sorted(package_payloads.items())
    ]
    records_by_name = {
        record["path"]: record
        for record in package_records
    }
    candidate_record = records_by_name[SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER]
    tree_record = records_by_name[SCENE.NEOQ_TREE_REPLACEMENT_MEMBER]
    legacy_corridor = {
        "format": "ror-cityworld-intercity-corridor-v4",
        "covered_centerline_length_m": covered,
        "waypoints": waypoints,
        "connection": {
            "source_position_gap_m": 0.0,
            "source_heading_error_degrees": 0.0,
            "destination_position_gap_m": 0.0,
            "destination_heading_error_degrees": 0.0,
        },
        "fixtures": {
            "instance_count": SCENE.EXPECTED_CORRIDOR_LIGHTS,
            "runtime_point_lights_per_instance": 1,
            "collision_authority":
                "native-procedural-road-v4-open-seams",
        },
        "profile": {
            "source_width_m": 9.75017,
            "destination_width_m": 10.0,
            "sampled_maximum_grade": 0.073,
            "surface_offset_m": 0.08,
        },
        "supports": {
            "enabled": True,
            "requested_count": 46,
            "expected_built_count": 46,
            "expected_skipped_count": 0,
            "road_type_token": "bridge_side_pillars",
            "paired_outboard": True,
            "centerline_pillars_requested": 0,
            "terrain_contact_resolved_at_runtime": True,
        },
        "source": {
            "position_m": [522.0, 0.100001, 370.023095],
            "connection_position_m":
                [522.0, 0.100001, 370.023095],
            "collision_handoff": {
                "authorities_per_station": 1,
                "legacy_curb_collision_retained": False,
                "replacement_mode":
                    "native-authenticated-in-place-object-definition-swap",
                "transition_asset_id":
                    "rorng_city_penguin_road_seam_12m",
            },
        },
        "seams": {
            "format": "ror-cityworld-penguin-neoq-seams-v1",
            "collision_endcaps": {
                "destination_exposed_vertical_face_m": 0.0,
                "directive": "collision_endcaps_enabled false",
                "maximum_exposed_vertical_face_m": 1e-06,
                "source_exposed_vertical_face_m": 0.0,
                "start_and_finish_transverse_collision_faces_emitted":
                    False,
            },
            "source": {
                "heading_error_degrees": 0.0,
                "position_gap_m": 0.0,
                "road_width_gap_m": 0.0,
                "surface_overlap_m": 0.0,
                "transition": {
                    "asset_id": "rorng_city_penguin_road_seam_12m",
                    "placement_position_m":
                        [516.0, 0.100001, 370.023095],
                    "end_position_m":
                        [522.0, 0.100001, 370.023095],
                    "transition_to_procedural_gap_m": 0.0,
                },
            },
            "destination": {
                "heading_error_degrees": 0.0,
                "position_gap_m": 0.0,
                "road_width_gap_m": 0.0,
                "surface_overlap_m": 0.0,
            },
            "supports": {
                "legacy_ground_road_envelopes_intersected": 0,
                "road_type_token": "bridge_side_pillars",
                "support_layout": "paired-outboard",
                "swept_bridge_carriageway_intrusion_m": 0.0,
            },
        },
    }
    _, neoq_bridge = BRIDGE.build_route(surface_offset_m=0.08)
    bridge_points, _ = BRIDGE.build_route(surface_offset_m=0.08)
    _, bridge_fixtures = BRIDGE.build_streetlights(bridge_points)
    neoq_bridge["fixtures"] = bridge_fixtures
    neoq_bridge["authentication"] = {
        "destination": {
            "line_number": 1230,
        },
        "format": BRIDGE.AUTHENTICATION_FORMAT,
        "ground_road": {
            "collision_member": BRIDGE.GROUND_ROAD_COLLISION_MEMBER,
            "collision_sha256": BRIDGE.GROUND_ROAD_COLLISION_SHA256,
            "decoded_surface_materials":
                list(BRIDGE.GROUND_ROAD_SURFACE_MATERIALS),
            "decoded_surface_triangle_count":
                BRIDGE.GROUND_ROAD_SURFACE_TRIANGLE_COUNT,
            "line_number": BRIDGE.GROUND_ROAD_PLACEMENT["line_number"],
            "local_to_world_mapping": [
                "world_x=-local_z",
                "world_y=local_y-0.4",
                "world_z=local_x",
            ],
            "object": BRIDGE.GROUND_ROAD_PLACEMENT["object"],
            "odef_member": BRIDGE.GROUND_ROAD_ODEF_MEMBER,
            "odef_sha256": BRIDGE.GROUND_ROAD_ODEF_SHA256,
            "position_m": list(
                BRIDGE.GROUND_ROAD_PLACEMENT["position_m"]
            ),
            "rotation_degrees": list(
                BRIDGE.GROUND_ROAD_PLACEMENT["rotation_degrees"]
            ),
        },
        "members": [
            {
                "name": record["name"],
                "role": record["role"],
                "sha256": record["sha256"],
                "size": record["size"],
            }
            for record in BRIDGE.AUTHENTICATED_MEMBERS
        ],
        "open_gap": {
            "bounds_xz_m": [
                BRIDGE.SOURCE_SEAM[0],
                BRIDGE.DESTINATION_SEAM[0],
                (
                    min(BRIDGE.SOURCE_SEAM[2], BRIDGE.DESTINATION_SEAM[2])
                    - BRIDGE.OPEN_GAP_HALF_WIDTH_M
                ),
                (
                    max(BRIDGE.SOURCE_SEAM[2], BRIDGE.DESTINATION_SEAM[2])
                    + BRIDGE.OPEN_GAP_HALF_WIDTH_M
                ),
            ],
            "placement_origin_count": 0,
            "verified": True,
        },
        "source": {
            "line_number": 366,
        },
        "tobj": {
            "sha256": BRIDGE.PINNED_TOBJ_SHA256,
        },
    }
    neoq_bridge["obstacle_avoidance"] = {
        "destination_existing_lane_collision_preserved": True,
        "destination_generated_overlap_m": 0.0,
        "ground_level_support_clearance":
            BRIDGE.validate_ground_road_clearance(
                neoq_bridge,
                neoq_bridge["authentication"],
            ),
        "source_existing_lane_collision_preserved": True,
        "source_flush_join_at_authenticated_mesh_edge": True,
        "source_generated_overlap_m": 0.0,
        "open_gap_placement_origin_audit":
            neoq_bridge["authentication"]["open_gap"],
        "swept_mesh_clearance":
            "native-multi-camera-and-drive-gate-required",
    }
    report = {
        "format": SCENE.OVERLAY_REPORT_FORMAT,
        "source": {
            "archive": {"sha256": SCENE.CITYWORLD_SHA256},
            "references": {
                "overlay_placements": SCENE.OVERLAY_PLACEMENT_MEMBER,
                "tree_replacement_manifest":
                    SCENE.NEOQ_TREE_REPLACEMENT_MEMBER,
                "resource_bundle_dependency": (
                    "CityWorld.zip:CityWorld.terrn2:"
                    + SCENE.CITYWORLD_SHA256
                ),
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
            "derived_source_placement_record_count": 91,
            "source_textures_copied": False,
        },
        "package": {
            "entries": len(package_payloads) + 1,
            "files": package_records,
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
        "city_visuals": {
            "neoq_trees": {
                "activation": tree_manifest["activation"],
                "family": tree_manifest["family"],
                "replacement_manifest": tree_record,
                "source": tree_manifest["source"],
                "summary": tree_manifest["summary"],
            },
        },
        "corridor": {
            "format": "ror-cityworld-intercity-corridor-v4",
            "covered_centerline_length_m": covered,
            "waypoints": waypoints,
            "connection": {
                "source_position_gap_m": 0.0,
                "source_heading_error_degrees": 0.0,
                "destination_position_gap_m": 0.0,
                "destination_heading_error_degrees": 0.0,
            },
            "fixtures": {
                "instance_count": SCENE.EXPECTED_CORRIDOR_LIGHTS,
                "runtime_point_lights_per_instance": 1,
                "collision_authority":
                    "native-procedural-road-v4-open-seams",
            },
            "profile": {
                "source_width_m": 9.75017,
                "destination_width_m": 10.0,
                "sampled_maximum_grade": 0.073,
                "surface_offset_m": 0.08,
            },
            "supports": {
                "enabled": True,
                "requested_count": 46,
                "expected_built_count": 46,
                "expected_skipped_count": 0,
                "road_type_token": "bridge_side_pillars",
                "paired_outboard": True,
                "centerline_pillars_requested": 0,
                "terrain_contact_resolved_at_runtime": True,
            },
            "source": {
                "position_m": [522.0, 0.100001, 370.023095],
                "connection_position_m":
                    [522.0, 0.100001, 370.023095],
                "collision_handoff": {
                    "authorities_per_station": 1,
                    "legacy_curb_collision_retained": False,
                    "replacement_mode":
                        "native-authenticated-in-place-object-definition-swap",
                    "transition_asset_id":
                        "rorng_city_penguin_road_seam_12m",
                },
            },
            "seams": {
                "format": "ror-cityworld-penguin-neoq-seams-v1",
                "collision_endcaps": {
                    "destination_exposed_vertical_face_m": 0.0,
                    "directive": "collision_endcaps_enabled false",
                    "maximum_exposed_vertical_face_m": 1e-06,
                    "source_exposed_vertical_face_m": 0.0,
                    "start_and_finish_transverse_collision_faces_emitted":
                        False,
                },
                "source": {
                    "heading_error_degrees": 0.0,
                    "position_gap_m": 0.0,
                    "road_width_gap_m": 0.0,
                    "surface_overlap_m": 0.0,
                    "transition": {
                        "asset_id": "rorng_city_penguin_road_seam_12m",
                        "placement_position_m":
                            [516.0, 0.100001, 370.023095],
                        "end_position_m":
                            [522.0, 0.100001, 370.023095],
                        "transition_to_procedural_gap_m": 0.0,
                    },
                },
                "destination": {
                    "heading_error_degrees": 0.0,
                    "position_gap_m": 0.0,
                    "road_width_gap_m": 0.0,
                    "surface_overlap_m": 0.0,
                },
                "supports": {
                    "legacy_ground_road_envelopes_intersected": 0,
                    "road_type_token": "bridge_side_pillars",
                    "support_layout": "paired-outboard",
                    "swept_bridge_carriageway_intrusion_m": 0.0,
                },
            },
        },
        "corridors": {
            "neoq_to_neoq20": neoq_bridge,
            "penguinville_to_neoq": copy.deepcopy(legacy_corridor),
        },
        "visual_asset_usage": {
            "corridor_placement_mode":
                "native-procedural-v5-two-corridor-open-seams-side-piers-with-"
                "blender-transition-v2",
            "disabled_light_candidate_manifest":
                SCENE.NEOQ_LIGHT_CANDIDATE_MEMBER,
            "neoq_core_runtime_light_activation": "blocked-fail-closed",
            "purpose": SCENE.EXPECTED_VISUAL_PURPOSE,
            "packaged_asset_ids": [
                "rorng_city_penguin_road_seam_12m",
                "rorng_city_led_streetlight_bridge",
                *SCENE.EXPECTED_TREE_ASSET_IDS,
            ],
            "placed_asset_ids": [
                "rorng_city_penguin_road_seam_12m",
                "rorng_city_led_streetlight_bridge",
                *SCENE.EXPECTED_TREE_ASSET_IDS,
            ],
            "unplaced_asset_ids": SCENE.EXPECTED_UNPLACED_ASSETS,
            "validated_asset_ids": (
                SCENE.EXPECTED_UNPLACED_ASSETS
                + [
                    "rorng_city_led_streetlight_bridge",
                    "rorng_city_penguin_road_seam_12m",
                    *SCENE.EXPECTED_TREE_ASSET_IDS,
                ]
            ),
        },
    }
    return report, package_payloads


def write_overlay(
    path: Path,
    report: dict[str, object],
    package_payloads: dict[str, bytes],
) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(
            SCENE.OVERLAY_REPORT_MEMBER,
            json.dumps(report, sort_keys=True, separators=(",", ":")),
        )
        for name, package_payload in package_payloads.items():
            archive.writestr(name, package_payload)


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
        self.assertAlmostEqual(metrics["armed_station_m"], -19.8439)
        self.assertAlmostEqual(metrics["distance_m"], 2144.2)
        self.assertAlmostEqual(metrics["forward_distance_m"], 1068.1)
        self.assertAlmostEqual(metrics["reverse_distance_m"], 1076.1)
        self.assertAlmostEqual(metrics["path_error_m"], 0.912104)
        self.assertEqual(metrics["physics_steps"], 340960)
        self.assertEqual(
            sorted(
                (
                    record["requested"],
                    record["built"],
                    record["skipped"],
                )
                for record in metrics["side_piers"]
            ),
            [(46, 46, 0), (56, 56, 0)],
        )
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
            ("station=-19.8439", "station=-2"),
            ("distance_m=2144.2", "distance_m=1800"),
            ("forward_distance_m=1068.1", "forward_distance_m=900"),
            ("reverse_distance_m=1076.1", "reverse_distance_m=900"),
            ("path_error_m=0.912104", "path_error_m=2.1"),
            ("vertical_error_m=0.772327", "vertical_error_m=1.6"),
            ("regression_m=0.00750732", "regression_m=1.1"),
            ("speed_mps=14.3433", "speed_mps=0"),
            ("physics_steps=340960", "physics_steps=199999"),
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

    def test_runtime_log_gate_rejects_side_pier_skips_and_one_way_runs(
        self,
    ) -> None:
        engine, script = valid_logs()
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine.replace(
                    "requested=46 built=46 skipped=0",
                    "requested=46 built=45 skipped=1",
                )
                + "\n[RoR|ProceduralRoad|SidePiers] "
                "skip reason=insufficient-column-height",
                script,
            )
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine.replace(
                    SCENE.ENGINE_MARKERS[-1] + "\n",
                    "",
                    1,
                ),
                script,
            )
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_runtime_logs(
                0,
                "",
                engine,
                script.replace(
                    "[RoR|CW2|CorridorRuntime] REVERSE_SOURCE_SEAM",
                    "[missing reverse source seam]",
                ),
            )

    def test_runtime_log_gate_rejects_blank_transition_material(
        self,
    ) -> None:
        engine, script = valid_logs()
        markers = (
            "Error: ScriptCompiler - base object not found in "
            "cityworld_next_local_overlay.material(213): road2",
            "Warning: material rorng_penguin_seam_asphalt has no "
            "supportable Techniques and will be blank.",
        )
        for marker in markers:
            with self.subTest(marker=marker):
                with self.assertRaisesRegex(
                    SCENE.CorridorSceneFailure,
                    "transition material did not resolve",
                ):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        engine + "\n" + marker,
                        script,
                    )

    def test_four_rgb_screenshots_are_exact_distinct_and_regular(self) -> None:
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
            self.assertEqual(len(paths), 4)
            self.assertEqual(list(records), list(SCENE.RGB_CAPTURE_IDS))

            (root / "unexpected.png").write_bytes(b"fifth")
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_rgb_screenshots(root)
            (root / "unexpected.png").unlink()
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
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.validate_rgb_screenshots(root)

    def test_runtime_log_gate_requires_exact_side_pier_completion(self) -> None:
        engine, script = valid_logs()
        cases = (
            (
                "missing-corridor",
                engine.replace(
                    "[RoR|ProceduralRoad|SidePiers] "
                    "requested=46 built=46 skipped=0",
                    "",
                ),
            ),
            (
                "missing-neo",
                engine.replace(
                    "[RoR|ProceduralRoad|SidePiers] "
                    "requested=56 built=56 skipped=0",
                    "",
                ),
            ),
            (
                "duplicate",
                engine
                + "\n[RoR|ProceduralRoad|SidePiers] "
                "requested=56 built=56 skipped=0",
            ),
            (
                "count-drift",
                engine.replace(
                    "[RoR|ProceduralRoad|SidePiers] "
                    "requested=56 built=56 skipped=0",
                    "[RoR|ProceduralRoad|SidePiers] "
                    "requested=56 built=55 skipped=1",
                ),
            ),
            (
                "silent-extra-summary",
                engine
                + "\n[RoR|ProceduralRoad|SidePiers] "
                "requested=1 built=1 skipped=0",
            ),
            (
                "skip-diagnostic",
                engine
                + "\n[RoR|ProceduralRoad|SidePiers] "
                "skip reason=roadway-swept-prism-overlap "
                "pos=(1.000000,2.000000,3.000000)",
            ),
        )
        for label, changed in cases:
            with self.subTest(label=label):
                with self.assertRaises(SCENE.CorridorSceneFailure):
                    SCENE.validate_runtime_logs(
                        0,
                        "",
                        changed,
                        script,
                    )

    def test_fixture_route_matches_overlay_waypoint_contract(self) -> None:
        report = report_matching_script()
        record = SCENE.validate_script_route(FIXTURE_PATH, report)
        self.assertEqual(record["samples"], 57)
        self.assertEqual(record["path"], SCENE.FIXTURE_PATH)
        changed = copy.deepcopy(report)
        changed["corridor"]["waypoints"][30]["position_m"][0] += 0.01
        with self.assertRaises(SCENE.CorridorSceneFailure):
            SCENE.validate_script_route(FIXTURE_PATH, changed)
        script = FIXTURE_PATH.read_text(encoding="utf-8")
        for marker in (
            'const uint64 MAX_PHYSICS_STEPS = 480000;',
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            '{"position", vector3(502.0f, 2.1f, 370.000002f)}',
            "1400.966797f",
            "const float SOURCE_SEAM_STATION = 0.0f;",
            'Fail("spawn-not-inside-penguinville-road-"',
            "gClosestStation >= DESTINATION_SEAM_STATION",
            "gClosestStation <= SOURCE_SEAM_STATION",
            "MSG_SIM_DELETE_ACTOR_REQUESTED",
            "REVERSE_ACTOR_ID",
            'console.cVarSet("ui_hide_gui", "true")',
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)

    def test_overlay_validation_is_complete_and_tool_pinned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "repository"
            repository.mkdir()
            payload = b"compiled overlay payload"
            report, package_payloads = synthetic_overlay_report(
                repository,
                payload,
            )
            archive = root / SCENE.OVERLAY_NAME
            write_overlay(archive, report, package_payloads)
            validated, record = SCENE.validate_overlay_archive(
                archive,
                repository,
            )
            self.assertEqual(validated["format"], report["format"])
            self.assertEqual(record["name"], SCENE.OVERLAY_NAME)
            self.assertEqual(record["size"], archive.stat().st_size)

            changed = copy.deepcopy(report)
            changed["package"]["files"][0]["sha256"] = "0" * 64
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["connection"]["source_position_gap_m"] = 0.1
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["seams"]["collision_endcaps"][
                "start_and_finish_transverse_collision_faces_emitted"
            ] = True
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["corridor"]["supports"]["expected_skipped_count"] = 1
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["city_lighting"]["neoq_core"][
                "source_pole_definitions"
            ][0]["sha256"] = "0" * 64
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

            changed = copy.deepcopy(report)
            changed["visual_asset_usage"]["purpose"] = "stale v3 purpose"
            write_overlay(archive, changed, package_payloads)
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_overlay_archive(archive, repository)

    def test_neoq_tree_manifest_and_wrappers_are_exact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory) / "repository"
            repository.mkdir()
            report, package_payloads = synthetic_overlay_report(
                repository,
                b"overlay placements without tree duplicates",
            )
            records = {
                record["path"]: record
                for record in report["package"]["files"]
            }
            tree_manifest = json.loads(
                package_payloads[
                    SCENE.NEOQ_TREE_REPLACEMENT_MEMBER
                ].decode("utf-8")
            )
            validated = SCENE.validate_neoq_tree_replacements(
                tree_manifest,
                repository,
                package_payloads,
                records,
            )
            self.assertEqual(
                validated["summary"]["replacement_count"],
                SCENE.NEOQ_TREE_COUNT,
            )

            changed = copy.deepcopy(tree_manifest)
            changed["replacements"][0]["scale"] += 0.01
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_neoq_tree_replacements(
                    changed,
                    repository,
                    package_payloads,
                    records,
                )

            changed_payloads = dict(package_payloads)
            wrapper = "rorng_city_neoq_tree_instance_00.odef"
            changed_payloads[wrapper] = changed_payloads[wrapper].replace(
                b"1.02212, 1.02212, 1.02212",
                b"1.02212, 1.02212, 1.5",
            )
            with self.assertRaises(SCENE.CorridorSceneFailure):
                SCENE.validate_neoq_tree_replacements(
                    tree_manifest,
                    repository,
                    changed_payloads,
                    records,
                )

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

    def test_runtime_acceptance_requires_no_diagnostic_opt_in(self) -> None:
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
        args = SCENE.parse_args(common)
        self.assertEqual(args.artifact_dir, Path("/artifacts"))
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                SCENE.parse_args(
                    common + ("--diagnostic-allow-incomplete-overlay",)
                )

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
