#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
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
GITATTRIBUTES_PATH = REPOSITORY_ROOT / ".gitattributes"

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
AUTHENTIC_POLE_DEFINITIONS = (
    (
        "luminariaLQr.odef",
        b"luminariaLQr.mesh\r\n1,1,1\r\nbeginmesh\r\n"
        b"mesh luminariaQrCol.mesh\r\nendmesh\r\nend\r\n",
    ),
    (
        "luminariaQr.odef",
        b"luminariaQr.mesh\r\n1,1,1\r\nbeginmesh\r\n"
        b"mesh luminariaQrCol.mesh\r\nendmesh\r\nend",
    ),
    (
        "luminariaYQr.odef",
        b"luminariaYQr.mesh\r\n1,1,1\r\nbeginmesh\r\n"
        b"mesh luminariaQrCol.mesh\r\nendmesh\r\nend\r\n",
    ),
)
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
// NeoQ authenticated tree family fixture
//
//
//
1777.425049, 0.100000, 2232.668945, 0.000000, 90.000000, -0.000000, arbol1Qr
1760.757202, 0.100000, 2232.668945, 0.000000, -158.000000, 0.000000, arbol1Qr
1742.089355, 0.100000, 2232.668945, -0.000000, -139.500000, 0.000000, arbol1Qr
1721.921509, 0.100000, 2232.668945, -0.000000, 80.000000, -0.000000, arbol1Qr
1703.253662, 0.100000, 2232.668945, -0.000000, 21.500000, -0.000000, arbol1Qr
1683.585815, 0.100000, 2232.668945, -0.000000, -109.500000, 0.000000, arbol1Qr
1666.417969, 0.100000, 2232.668945, -0.000000, -116.500000, 0.000000, arbol1Qr
1645.750122, 0.100000, 2232.668945, -0.000000, 25.000000, 0.000000, arbol1Qr
1626.582275, 0.100000, 2232.668945, 0.000000, -116.000000, -0.000000, arbol1Qr
1777.425049, 0.100000, 2046.000977, 0.000000, -180.000000, -0.000000, arbol1Qr
1760.757202, 0.100000, 2046.000977, 0.000000, -154.500000, -0.000000, arbol1Qr
1742.089355, 0.100000, 2046.000977, -0.000000, -94.500000, 0.000000, arbol1Qr
1703.253662, 0.100000, 2046.000977, -0.000000, 137.000000, -0.000000, arbol1Qr
1683.585815, 0.100000, 2046.000977, 0.000000, 108.500000, 0.000000, arbol1Qr
1666.417969, 0.100000, 2046.000977, 0.000000, -153.500000, -0.000000, arbol1Qr
1645.750122, 0.100000, 2046.000977, -0.000000, 43.000000, -0.000000, arbol1Qr
1722.910278, -0.054961, 2044.339844, 0.000000, 64.000000, -0.000000, arbol1Qr
1629.423096, -0.074142, 2046.764160, 0.000000, -72.000000, -0.000000, arbol1Qr
"""
SYNTHETIC_BRIDGE_MEMBER_PAYLOADS = tuple(
    (
        record["name"],
        ("synthetic authenticated bridge member " + record["name"] + "\n").encode(),
    )
    for record in BUILDER.neoq_bridge.AUTHENTICATED_MEMBERS
)
SYNTHETIC_BRIDGE_MEMBER_CONTRACT = tuple(
    {
        "name": name,
        "role": next(
            record["role"]
            for record in BUILDER.neoq_bridge.AUTHENTICATED_MEMBERS
            if record["name"] == name
        ),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload),
    }
    for name, payload in SYNTHETIC_BRIDGE_MEMBER_PAYLOADS
)


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


def pinned_fixture_placements() -> str:
    lines = [
        f"// fixture-padding-{line_number:04d}"
        for line_number in range(1, 1355)
    ]
    tree_lines = [
        line
        for line in BASE_PLACEMENTS.splitlines()
        if line.rstrip().endswith(", arbol1Qr")
    ]
    if len(tree_lines) != 18:
        raise RuntimeError("synthetic NeoQ tree fixture must contain 18 trees")
    lines[0] = "// SOURCE_PLACEMENTS_PAYLOAD_MUST_NOT_LEAK"
    lines[1] = "1, 2, 3, 0, 0, 0, source_only_object"
    lines[2] = "1, 2, 4, 0, 0, 0, source_only_object_two"
    lines[3] = (
        "1460.966797, 0.1, 903.098389, "
        "0, -180, 0, crucetQr"
    )
    lines[8:26] = tree_lines
    lines[365] = (
        "3676.970703, 0.3, 3993.104004, "
        "90, 0, 0, distribuidorQr"
    )
    lines[377] = "0, -0.4, 0, 90, 0, 90, autopistaQr"
    lines[1229] = (
        "7000, 50, 4018, 90, 0, 0, "
        "NeoQ2-0industrial-zone-distributor-road"
    )
    lines[1353] = "485, 0.1, 370, 0, 90, 0, troadavenuesidewalk"
    light_lines = luminaria_fixture_placements().splitlines()
    available = (
        index
        for index in range(4, 1229)
        if index not in {365, 377} and not 8 <= index < 26
    )
    for line, index in zip(light_lines, available):
        lines[index] = line
    return "\n".join(lines) + "\n"


PLACEMENTS = pinned_fixture_placements()


class CityWorldLocalOverlayBuilderTests(unittest.TestCase):
    def test_hashed_runtime_material_is_checkout_stable(self) -> None:
        attributes = {
            line.strip()
            for line in GITATTRIBUTES_PATH.read_text(
                encoding="utf-8"
            ).splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        }
        self.assertIn(
            "resources/materials/ror.material text eol=lf",
            attributes,
        )

    def setUp(self) -> None:
        bridge_contract = mock.patch.object(
            BUILDER.neoq_bridge,
            "AUTHENTICATED_MEMBERS",
            SYNTHETIC_BRIDGE_MEMBER_CONTRACT,
        )
        bridge_contract.start()
        self.addCleanup(bridge_contract.stop)

    def make_archive(
        self,
        root: Path,
        *,
        terrain: str = TERRAIN,
        placements: str = PLACEMENTS,
        otc_name: str = "CityWorld.otc",
        archive_name: str = "CityWorld.zip",
        pole_definitions: tuple[tuple[str, bytes], ...] = (
            AUTHENTIC_POLE_DEFINITIONS
        ),
        extra_entries: tuple[tuple[str, bytes], ...] = (),
    ) -> tuple[Path, str]:
        archive_path = root / archive_name
        with zipfile.ZipFile(
            archive_path,
            "w",
            compression=zipfile.ZIP_STORED,
        ) as archive:
            def write_entry(name: str, payload: bytes) -> None:
                info = zipfile.ZipInfo(
                    name,
                    date_time=BUILDER.ZIP_TIMESTAMP,
                )
                info.compress_type = zipfile.ZIP_STORED
                info.create_system = 3
                info.external_attr = BUILDER.ZIP_MODE << 16
                archive.writestr(info, payload)

            write_entry("CityWorld.terrn2", terrain.encode("utf-8"))
            write_entry(otc_name, SOURCE_MARKERS["CityWorld.otc"])
            write_entry("CityWorld.tobj", placements.encode("utf-8"))
            for name, payload in pole_definitions:
                write_entry(name, payload)
            for name, payload in SYNTHETIC_BRIDGE_MEMBER_PAYLOADS:
                write_entry(name, payload)
            for name, payload in extra_entries:
                write_entry(name, payload)
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
        runtime_files = [
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
        ]
        for ordinal in range(3):
            payload = (
                f"synthetic streetlight render LOD {ordinal}\n"
            ).encode()
            runtime_files.append(
                BUILDER.RuntimeFile(
                    package_path=f"{asset_id}_lod{ordinal}.mesh",
                    repository_path=(
                        f"fixtures/{asset_id}_lod{ordinal}.mesh"
                    ),
                    role=f"render-lod{ordinal}",
                    sha256=hashlib.sha256(payload).hexdigest(),
                    size=len(payload),
                    payload=payload,
                )
            )
        runtime_files = tuple(runtime_files)
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

    def fake_regional_infill_bundle(
        self,
    ) -> tuple[object, dict[str, object], dict[str, object], tuple[object, ...]]:
        plan = BUILDER.regional_infill.build_infill_plan()
        audit = BUILDER.regional_infill.audit_plan(plan)
        manifest = BUILDER.regional_infill.build_manifest(plan)
        authored_assets = {
            asset.asset_id: asset for asset in plan.assets
        }
        assets = []
        for (
            asset_id,
            manifest_path,
            profile,
            light_ids,
        ) in BUILDER.INFILL_ASSET_CONTRACTS:
            material_payload = (
                f"material {asset_id}_test_surface\n"
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
            runtime_files = [
                BUILDER.RuntimeFile(
                    package_path=f"{asset_id}.material",
                    repository_path=f"fixtures/{asset_id}.material",
                    role="material-fallback",
                    sha256=hashlib.sha256(material_payload).hexdigest(),
                    size=len(material_payload),
                    payload=material_payload,
                )
            ]
            for suffix, role in (
                (".odef", "terrain-object"),
                ("_collision_fixture.mesh", "collision-fixture"),
                ("_lod0.mesh", "render-lod0"),
                ("_lod1.mesh", "render-lod1"),
                ("_lod2.mesh", "render-lod2"),
            ):
                payload = (
                    f"synthetic regional infill {asset_id} {role}\n"
                ).encode()
                runtime_files.append(
                    BUILDER.RuntimeFile(
                        package_path=f"{asset_id}{suffix}",
                        repository_path=f"fixtures/{asset_id}{suffix}",
                        role=role,
                        sha256=hashlib.sha256(payload).hexdigest(),
                        size=len(payload),
                        payload=payload,
                    )
                )
            runtime_lights = [
                {
                    "color_linear": [1.0, 0.66, 0.31],
                    "id": light_id,
                    "position_ogre_y_up_m": [0.0, 5.16, 0.0],
                    "range_m": 24.0,
                    "type": "point",
                }
                for light_id in light_ids
            ]
            authored = authored_assets[asset_id]
            collision_component_count = len(
                authored.collision_components
            )
            collision_components = [
                {
                    "bounds_blender_z_up": {
                        "min": [
                            component.collision_center_local_x_m
                            - component.collision_width_m / 2.0,
                            -component.collision_center_local_z_m
                            - component.collision_depth_m / 2.0,
                            0.0,
                        ],
                        "max": [
                            component.collision_center_local_x_m
                            + component.collision_width_m / 2.0,
                            -component.collision_center_local_z_m
                            + component.collision_depth_m / 2.0,
                            1.0,
                        ],
                    },
                    "component_id": component.component_id,
                    "triangles": 12,
                }
                for component in authored.collision_components
            ]
            aggregate_bounds = {
                "min": [
                    min(
                        component["bounds_blender_z_up"]["min"][axis]
                        for component in collision_components
                    )
                    for axis in range(3)
                ],
                "max": [
                    max(
                        component["bounds_blender_z_up"]["max"][axis]
                        for component in collision_components
                    )
                    for axis in range(3)
                ],
            }
            assets.append(
                BUILDER.PreparedAsset(
                    asset_id=asset_id,
                    centerline_length_m=None,
                    manifest_path=manifest_path,
                    profile=None,
                    provenance={
                        "asset": {
                            "id": asset_id,
                            "license": "GPL-3.0-or-later",
                            "profile": profile,
                        },
                        "generator": {},
                        "collision": {
                            "components": collision_components,
                            "components_format":
                                "ror-cityworld-collision-components-v1",
                            "objects": [
                                {
                                    "bounds_blender_z_up": aggregate_bounds,
                                    "name":
                                        f"{asset_id}_collision_fixture",
                                    "role": "collision-fixture",
                                    "topology": {
                                        "connected_components":
                                            collision_component_count,
                                        "intersecting_faces": 0,
                                        "outward_winding": True,
                                        "watertight": True,
                                    },
                                    "triangles":
                                        collision_component_count * 12,
                                }
                            ],
                            "profile": authored.collision_profile,
                            "purpose":
                                "bounded-landmark-or-building-envelope",
                            "separate_from_render_mesh": True,
                        },
                        "manifest": {
                            "path": manifest_path,
                            "sha256": "3" * 64,
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
                        "runtime_lights": runtime_lights,
                    },
                    runtime_files=tuple(runtime_files),
                )
            )
        return plan, audit, manifest, tuple(assets)

    @staticmethod
    def fake_regional_infill_source_authentication() -> dict[str, object]:
        plan = BUILDER.regional_infill.build_infill_plan()
        return {
            "anchor_ids": [
                anchor.anchor_id for anchor in plan.source_anchors
            ],
            "archive_sha256": BUILDER.PINNED_ARCHIVE_SHA256,
            "authenticated_placement_lines": [378, 1354],
            "format":
                BUILDER.REGIONAL_INFILL_SOURCE_AUTHENTICATION_FORMAT,
            "generated_anchor_count": 1,
            "line_0378": {
                "collision_member": "autopistaQr.mesh",
                "collision_sha256":
                    BUILDER.regional_infill.PINNED_HIGHWAY_COLLISION_SHA256,
                "line_number": 378,
                "member": "CityWorld.tobj",
                "object": "autopistaQr",
                "position_m": [0.0, -0.4, 0.0],
                "rotation_degrees": [90.0, 0.0, 90.0],
            },
            "line_1354": {
                "line_number": 1354,
                "member": "CityWorld.tobj",
                "object": "troadavenuesidewalk",
                "position_m": [485.0, 0.1, 370.0],
                "rotation_degrees": [0.0, 90.0, 0.0],
            },
            "native_anchor_count": 6,
            "source_anchor_count": 7,
            "source_tobj": {
                "member": "CityWorld.tobj",
                "sha256": "synthetic-fixture",
            },
        }

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
            mock.patch.object(
                BUILDER,
                "prepare_regional_infill_assets",
                return_value=self.fake_regional_infill_bundle(),
            ),
            mock.patch.object(
                BUILDER,
                "authenticate_regional_infill_source",
                return_value=
                    self.fake_regional_infill_source_authentication(),
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

        road_seam = BUILDER.prepare_penguin_road_seam_asset(
            REPOSITORY_ROOT
        )
        self.assertEqual(
            road_seam.asset_id,
            BUILDER.PENGUIN_ROAD_SEAM_ASSET_ID,
        )
        self.assertEqual(len(road_seam.runtime_files), 8)
        self.assertEqual(road_seam.provenance["runtime_lights"], [])
        self.assertEqual(
            {
                item.role
                for item in road_seam.runtime_files
                if item.role.startswith("collision-")
            },
            {
                "collision-road",
                "collision-shoulder-left",
                "collision-shoulder-right",
            },
        )
        self.assertIsNone(road_seam.centerline_length_m)
        self.assertIsNone(road_seam.profile)

        (
            infill_plan,
            infill_audit,
            infill_manifest,
            infill_assets,
        ) = BUILDER.prepare_regional_infill_assets(REPOSITORY_ROOT)
        self.assertEqual(len(infill_assets), 5)
        self.assertEqual(
            BUILDER.validate_regional_infill_manifest_contract(
                infill_plan,
                infill_audit,
                infill_manifest,
            ),
            BUILDER.regional_infill.canonical_manifest_sha256(
                infill_plan
            ),
        )
        authored_infill_assets = {
            asset.asset_id: asset for asset in infill_plan.assets
        }
        self.assertEqual(
            {
                asset.asset_id: (
                    asset.provenance["collision"]["profile"],
                    asset.provenance["collision"]["objects"][0][
                        "topology"
                    ]["connected_components"],
                )
                for asset in infill_assets
            },
            {
                asset_id: (
                    authored.collision_profile,
                    len(authored.collision_components),
                )
                for asset_id, authored in authored_infill_assets.items()
            },
        )
        suburb = next(
            asset
            for asset in infill_assets
            if asset.asset_id == BUILDER.regional_infill.SUBURB_ASSET_ID
        )
        stale_provenance = copy.deepcopy(suburb.provenance)
        stale_provenance["collision"]["profile"] = (
            "single-watertight-proxy-v1"
        )
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "collision manifest profile drifted",
        ):
            BUILDER.validate_regional_infill_asset_collision(
                replace(suburb, provenance=stale_provenance),
                authored_infill_assets[suburb.asset_id],
            )
        stale_provenance = copy.deepcopy(suburb.provenance)
        stale_provenance["collision"]["components"][0][
            "component_id"
        ] = "wrong-house"
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "collision component 0 drifted",
        ):
            BUILDER.validate_regional_infill_asset_collision(
                replace(suburb, provenance=stale_provenance),
                authored_infill_assets[suburb.asset_id],
            )
        stale_provenance = copy.deepcopy(suburb.provenance)
        stale_provenance["collision"]["components"][0][
            "bounds_blender_z_up"
        ]["min"][0] -= 1.0
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "bounds do not match the canonical plan",
        ):
            BUILDER.validate_regional_infill_asset_collision(
                replace(suburb, provenance=stale_provenance),
                authored_infill_assets[suburb.asset_id],
            )
        stale_provenance = copy.deepcopy(suburb.provenance)
        stale_provenance["collision"]["objects"][0][
            "bounds_blender_z_up"
        ]["max"][0] += 1.0
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "aggregate collision bounds drifted",
        ):
            BUILDER.validate_regional_infill_asset_collision(
                replace(suburb, provenance=stale_provenance),
                authored_infill_assets[suburb.asset_id],
            )

        tree_plan = BUILDER.read_native_tree_plan(REPOSITORY_ROOT)
        tree_family = BUILDER.prepare_tree_family(
            REPOSITORY_ROOT,
            tree_plan,
        )
        self.assertEqual(len(tree_plan), 18)
        self.assertEqual(len(tree_family.wrappers), 18)
        self.assertEqual(
            [asset.asset_id for asset in tree_family.assets],
            [
                "rorng_city_neoq_tree_round",
                "rorng_city_neoq_tree_columnar",
                "rorng_city_neoq_tree_windswept",
            ],
        )
        self.assertTrue(
            all(len(asset.runtime_files) == 6 for asset in tree_family.assets)
        )
        self.assertTrue(
            all(
                asset.provenance["runtime_lights"] == []
                for asset in tree_family.assets
            )
        )

    def test_neoq_tree_replacement_is_exact_scaled_and_duplicate_free(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive, output, _ = self.build_fixture(Path(directory))
            source_placements = BUILDER.source_placements(archive)
            plan = BUILDER.read_native_tree_plan(REPOSITORY_ROOT)
            authenticated = BUILDER.authenticate_neoq_tree_placements(
                source_placements,
                plan,
            )
            with zipfile.ZipFile(output) as package:
                names = package.namelist()
                replacement_payload = package.read(
                    BUILDER.NEOQ_TREE_REPLACEMENT_NAME
                )
                replacement = json.loads(replacement_payload)
                report = json.loads(package.read(BUILDER.REPORT_NAME))
                overlay_text = package.read(BUILDER.OVERLAY_NAME).decode()

                self.assertEqual(len(names), len({name.casefold() for name in names}))
                self.assertEqual(names, sorted(names))
                self.assertTrue(
                    all(
                        "\\" not in name
                        and BUILDER.safe_package_path(name) == name
                        for name in names
                    )
                )
                self.assertNotIn("arbol1Qr", overlay_text)
                self.assertNotIn("rorng_city_neoq_tree_instance_", overlay_text)

                self.assertEqual(
                    replacement["format"],
                    BUILDER.NEOQ_TREE_REPLACEMENT_FORMAT,
                )
                self.assertEqual(
                    replacement["activation"],
                    {
                        "duplicate_placements_emitted": 0,
                        "fail_closed": True,
                        "mode":
                            "native-authenticated-in-place-replacement-v1",
                        "requires_exact_archive_dependency": True,
                        "requires_exact_tobj_sha256": True,
                        "runtime_resource_preflight":
                            "all-18-scale-wrapper-odefs",
                    },
                )
                self.assertEqual(
                    [item["source_line"] for item in replacement["replacements"]],
                    list(range(9, 27)),
                )
                self.assertEqual(
                    [item["ordinal"] for item in replacement["replacements"]],
                    list(range(18)),
                )
                self.assertEqual(
                    [item["position_m"] for item in replacement["replacements"]],
                    [
                        [round(value, 9) for value in source.position]
                        for source in authenticated
                    ],
                )
                self.assertTrue(
                    all(
                        item["position_preserved"]
                        for item in replacement["replacements"]
                    )
                )
                self.assertEqual(
                    len(
                        {
                            item["object_definition"]
                            for item in replacement["replacements"]
                        }
                    ),
                    18,
                )

                for item, entry in zip(
                    replacement["replacements"],
                    plan,
                ):
                    self.assertEqual(item["variant"], entry.variant)
                    self.assertEqual(item["scale"], entry.scale)
                    self.assertEqual(
                        item["rotation_degrees"][1],
                        entry.yaw_degrees,
                    )
                    wrapper = item["wrapper"]
                    wrapper_payload = package.read(wrapper["path"])
                    self.assertEqual(
                        hashlib.sha256(wrapper_payload).hexdigest(),
                        wrapper["sha256"],
                    )
                    self.assertEqual(len(wrapper_payload), wrapper["size"])
                    wrapper_lines = wrapper_payload.decode().splitlines()
                    expected_scale = BUILDER.stable_float(entry.scale)
                    self.assertEqual(
                        wrapper_lines[1],
                        f"{expected_scale}, {expected_scale}, {expected_scale}",
                    )
                    self.assertEqual(
                        wrapper_lines[0],
                        f"{entry.variant}_lod0.mesh",
                    )
                    self.assertIn(
                        f"mesh {entry.variant}_collision_fixture.mesh",
                        wrapper_lines,
                    )

            source_tobj = next(
                record
                for record in report["source"]["archive"]["members"]
                if record["name"] == "CityWorld.tobj"
            )
            self.assertEqual(
                replacement["source"]["tobj_sha256"],
                source_tobj["sha256"],
            )
            report_tree = report["city_visuals"]["neoq_trees"]
            self.assertEqual(
                report_tree["replacement_manifest"]["sha256"],
                hashlib.sha256(replacement_payload).hexdigest(),
            )
            self.assertEqual(
                report_tree["summary"]["replacement_count"],
                18,
            )

    def test_neoq_tree_source_and_selector_drift_fail_closed(self) -> None:
        placements = PLACEMENTS.replace(
            "1777.425049, 0.100000, 2232.668945",
            "1777.425149, 0.100000, 2232.668945",
            1,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, digest = self.make_archive(
                root,
                placements=placements,
            )
            output = root / "overlay.zip"
            with (
                mock.patch.object(
                    BUILDER,
                    "PINNED_ARCHIVE_SHA256",
                    digest,
                ),
                mock.patch.object(BUILDER, "prepare_assets") as prepare_assets,
                self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    "line 9 does not match the exact NeoQ tree plan",
                ),
            ):
                BUILDER.build_local_overlay(
                    archive_path=archive,
                    repository_path=REPOSITORY_ROOT,
                    output_path=output,
                    surface_offset_m=0.08,
                )
            prepare_assets.assert_not_called()
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".*.tmp-*.part")), [])

        plan = list(BUILDER.read_native_tree_plan(REPOSITORY_ROOT))
        plan[0] = replace(plan[0], scale=plan[0].scale + 0.001)
        with self.assertRaisesRegex(
            BUILDER.OverlayFailure,
            "disagrees with the family selector",
        ):
            BUILDER.prepare_tree_family(REPOSITORY_ROOT, plan)

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

    def test_full_corridor_has_flush_open_seams_and_outboard_piers(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, output, result = self.build_fixture(Path(directory))
            report = self.read_report(output)
            corridor = report["corridor"]
            self.assertEqual(
                corridor["format"],
                "ror-cityworld-intercity-corridor-v4",
            )
            self.assertEqual(
                corridor["source"]["connection"],
                "east opened road seam after crowned-to-flat transition",
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
                    "connection_taper_grade": 0.003,
                    "connection_taper_length_m": 40.0,
                    "deck_clearance_m": 8.0,
                    "destination_connection_surface_y_m": 0.1,
                    "destination_surface_y_m": 0.18,
                    "destination_width_m": 10.0,
                    "flat_lead_length_m": 40.0,
                    "maximum_grade": 0.075,
                    "ramp_length_m": 160.0,
                    "rotation_convention":
                        "ogre-yaw-local-plus-z-cross-section",
                    "sampled_maximum_grade": 0.07379231,
                    "sample_spacing_limit_m": 20.0,
                    "source_connection_surface_y_m": 0.100001,
                    "source_surface_y_m": 0.180001,
                    "source_width_m": 9.75017,
                    "surface_offset_m": 0.08,
                    "width_transition": "full-corridor-cubic-smoothstep",
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
                0.0,
            )
            self.assertEqual(
                obstacle_audit["existing_ground_road_envelopes_intersected"],
                0,
            )
            self.assertEqual(
                obstacle_audit["source_transition_x_bounds_m"],
                [510.0, 522.0],
            )
            waypoints = corridor["waypoints"]
            self.assertGreater(len(waypoints), 50)
            self.assertEqual(
                waypoints[0]["position_m"],
                [522.0, 0.100001, 370.023095],
            )
            self.assertEqual(
                waypoints[-1]["position_m"],
                [1380.966797, 0.1, 936.098389],
            )
            self.assertEqual(waypoints[0]["yaw_degrees"], 0.0)
            self.assertEqual(waypoints[-1]["yaw_degrees"], 0.0)
            self.assertEqual(waypoints[0]["road_type"], "flat")
            self.assertEqual(waypoints[-1]["road_type"], "flat")
            self.assertEqual(waypoints[0]["width_m"], 9.75017)
            self.assertEqual(waypoints[-1]["width_m"], 10.0)
            self.assertTrue(
                all(
                    first["width_m"] <= second["width_m"]
                    for first, second in zip(waypoints, waypoints[1:])
                )
            )
            self.assertEqual(
                corridor["source"]["authenticated_placement"]["object"],
                "troadavenuesidewalk",
            )
            self.assertEqual(
                corridor["destination"]["authenticated_placement"]["object"],
                "crucetQr",
            )
            self.assertEqual(
                corridor["source"]["collision_handoff"],
                {
                    "authorities_per_station": 1,
                    "legacy_curb_collision_retained": False,
                    "replacement_mode":
                        "native-authenticated-in-place-object-definition-swap",
                    "transition_asset_id":
                        BUILDER.PENGUIN_ROAD_SEAM_ASSET_ID,
                },
            )
            self.assertEqual(
                max(point["position_m"][1] for point in waypoints),
                8.180000903,
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
            control_points = BUILDER.route_control_points(source, destination)
            arc_table = BUILDER.route_arc_table(control_points)
            for waypoint in waypoints:
                parameter = BUILDER.parameter_at_station(
                    arc_table,
                    min(waypoint["station_m"], arc_table[-1][1]),
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
            self.assertEqual(supports["requested_count"], 46)
            self.assertEqual(supports["expected_built_count"], 46)
            self.assertEqual(supports["expected_skipped_count"], 0)
            self.assertEqual(supports["no_pillar_bridge_count"], 2)
            self.assertEqual(
                supports["no_pillar_bridge_stations_m"],
                [60.0, 980.0],
            )
            self.assertTrue(supports["paired_outboard"])
            self.assertEqual(supports["centerline_pillars_requested"], 0)
            self.assertEqual(
                supports["road_type_token"],
                "bridge_side_pillars",
            )
            self.assertLessEqual(
                supports["maximum_station_spacing_m"],
                BUILDER.ROUTE_SAMPLE_SPACING_M,
            )
            fixtures = corridor["fixtures"]
            self.assertEqual(
                fixtures["format"],
                "ror-cityworld-streetlight-placement-v2",
            )
            self.assertEqual(
                fixtures["asset_id"],
                BUILDER.LED_STREETLIGHT_ASSET_ID,
            )
            self.assertFalse(fixtures["paired"])
            self.assertEqual(fixtures["station_spacing_m"], 40.0)
            self.assertEqual(fixtures["station_count"], 15)
            self.assertEqual(fixtures["instance_count"], 15)
            self.assertEqual(fixtures["mount_elevation_above_road_m"], 0.95)
            self.assertEqual(fixtures["runtime_point_lights_per_instance"], 1)
            self.assertEqual(
                fixtures["collision_authority"],
                "native-procedural-road-v4-open-seams",
            )
            self.assertEqual(
                [item["station_m"] for item in fixtures["stations"]],
                list(range(220, 781, 40)),
            )
            self.assertEqual(
                [item["side"] for item in fixtures["stations"]],
                ["left", "right"] * 7 + ["left"],
            )
            for fixture in fixtures["stations"]:
                center = fixture["centerline_position_m"]
                placement = fixture["placement_position_m"]
                offset_x = placement[0] - center[0]
                offset_z = placement[2] - center[2]
                offset_length = math.hypot(offset_x, offset_z)
                self.assertAlmostEqual(
                    offset_length,
                    fixture["lateral_mount_offset_m"],
                    places=8,
                )
                self.assertAlmostEqual(
                    fixture["lateral_mount_offset_m"],
                    fixture["road_width_m"] / 2.0
                    + BUILDER.ROUTE_BRIDGE_BORDER_WIDTH_M / 2.0,
                    places=8,
                )
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
            infill_report = report["regional_infill"]
            self.assertEqual(
                infill_report["canonical_manifest_sha256"],
                BUILDER.regional_infill.canonical_manifest_sha256(),
            )
            self.assertEqual(
                infill_report["manifest"]["sha256"],
                infill_report["canonical_manifest_sha256"],
            )
            connector_audit = infill_report["audit"]["connectors"]
            connector_statuses = [
                connector.status
                for connector in
                BUILDER.regional_infill.build_infill_plan().connectors
            ]
            self.assertEqual(
                connector_audit["active"],
                connector_statuses.count("active"),
            )
            self.assertEqual(
                connector_audit["pending"],
                connector_statuses.count("pending"),
            )
            self.assertEqual(
                connector_audit[
                    "non_designated_route_asset_intersection_count"
                ],
                0,
            )
            self.assertEqual(
                report["source"]["references"],
                {
                    "geometry_config": "CityWorld.otc",
                    "original_placements": "CityWorld.tobj",
                    "overlay_placements": BUILDER.OVERLAY_NAME,
                    "regional_infill_manifest":
                        BUILDER.REGIONAL_INFILL_MANIFEST_NAME,
                    "tree_replacement_manifest":
                        BUILDER.NEOQ_TREE_REPLACEMENT_NAME,
                    "resource_bundle_dependency":
                        "CityWorld.zip:CityWorld.terrn2:"
                        + report["source"]["archive"]["expected_sha256"],
                },
            )
            self.assertEqual(
                len(report["source"]["archive"]["members"]),
                3,
            )
            self.assertEqual(len(report["assets"]), 14)
            self.assertTrue(
                all("manifest" in asset for asset in report["assets"])
            )
            usage = report["visual_asset_usage"]
            self.assertEqual(
                usage["corridor_placement_mode"],
                "native-procedural-v7-two-corridor-open-seams-side-piers-with-"
                "blender-transition-v2-and-regional-infill-v2",
            )
            self.assertIn(
                BUILDER.PENGUIN_ROAD_SEAM_ASSET_ID,
                usage["packaged_asset_ids"],
            )
            self.assertIn(
                BUILDER.PENGUIN_ROAD_SEAM_ASSET_ID,
                usage["placed_asset_ids"],
            )
            self.assertIn("open procedural collision endcaps", usage["purpose"])
            self.assertIn(
                "procedural road2 surface and marking atlas",
                usage["purpose"],
            )
            self.assertIn(
                "one-millimetre seam gates",
                usage["purpose"],
            )
            self.assertIn(
                "exact compound suburb/station collision components",
                usage["purpose"],
            )
            self.assertIn("a second raised bridge", usage["purpose"])
            self.assertIn("without covering its median", usage["purpose"])
            self.assertIn(
                BUILDER.LED_STREETLIGHT_ASSET_ID,
                usage["packaged_asset_ids"],
            )
            self.assertIn(
                "18 polygon-authenticated no-pillar stations above autopistaQr",
                usage["purpose"],
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
            self.assertEqual(
                placement_text.count("collision_endcaps_enabled false"),
                9,
            )
            self.assertEqual(
                placement_text.count(", bridge_side_pillars\n"),
                102,
            )
            self.assertGreater(
                placement_text.count(", bridge_no_pillars\n"),
                2,
            )
            self.assertNotIn(", bridge\n", placement_text)
            self.assertNotIn("rorng_city_gateway_block_40m -", placement_text)
            self.assertEqual(
                placement_text.count(
                    f"{BUILDER.LED_STREETLIGHT_ASSET_ID} - "
                ),
                48,
            )
            self.assertIn(
                "cityworld_next_led_0220_left",
                placement_text,
            )
            self.assertIn(
                "cityworld_next_led_0780_left",
                placement_text,
            )
            self.assertEqual(
                placement_text.count(
                    f"{BUILDER.PENGUIN_ROAD_SEAM_ASSET_ID} - "
                ),
                1,
            )
            self.assertIn(
                "cityworld_next_neoq_link_led_0240_left",
                placement_text,
            )
            self.assertIn(
                "cityworld_next_neoq_link_led_2800_left",
                placement_text,
            )
            self.assertEqual(
                placement_text.count("begin_procedural_roads"),
                9,
            )
            self.assertIn(
                "522, 0.100001, 370.023095, 0, 0, 0, "
                "9.75017, 1, 0.15, flat",
                placement_text,
            )
            self.assertIn(
                "1380.966797, 0.1, 936.098389, 0, 0, 0, "
                "10, 1, 0.15, flat",
                placement_text,
            )
            seams = corridor["seams"]
            self.assertFalse(
                seams["collision_endcaps"][
                    "start_and_finish_transverse_collision_faces_emitted"
                ]
            )
            self.assertEqual(
                seams["source"]["curb_opening"][
                    "legacy_curb_collision_retained"
                ],
                False,
            )
            self.assertEqual(
                seams["source"]["curb_opening"][
                    "replacement_collision_mesh"
                ]["sha256"],
                BUILDER.penguin_neoq_seam
                    .SOURCE_REPLACEMENT_COLLISION_SHA256,
            )
            self.assertEqual(
                seams["destination"]["collision_mesh"]["surface_probe"],
                {
                    "local_xy_m": [79.999, 33.0],
                    "local_z_m": 0.0,
                    "triangle_index": 61,
                    "triangle_vertices": [105, 106, 107],
                },
            )
            self.assertEqual(
                seams["transition_asset_contract"][
                    "authoritative_collision"
                ]["open_interval_surface_count"],
                1,
            )
            self.assertIn(
                "3790.970703, 0.1, 3993.104004, 0, 0, 0, "
                "24, 0, 0, flat",
                placement_text,
            )
            self.assertIn(
                "6867, 0.2, 4018, 0, 0, 0, 15.1, 0, 0, flat",
                placement_text,
            )
            bridge = report["corridors"]["neoq_to_neoq20"]
            self.assertEqual(
                bridge["format"],
                "ror-cityworld-neoq-intercity-bridge-v4",
            )
            self.assertEqual(
                bridge["source"]["seam_m"],
                [3790.970703, 0.1, 3993.104004],
            )
            self.assertEqual(
                bridge["destination"]["seam_m"],
                [6867.0, 0.2, 4018.0],
            )
            self.assertTrue(bridge["collision"]["continuous"])
            self.assertFalse(
                bridge["collision"]["endcap_collision_enabled"]
            )
            self.assertEqual(
                bridge["collision"]["endcap_collision_triangle_count"],
                0,
            )
            self.assertEqual(
                bridge["collision"]["endpoint_wheel_path_intrusion_m"],
                0.0,
            )
            self.assertTrue(bridge["profile"]["curb_free_approaches"])
            self.assertEqual(bridge["profile"]["width_m"], 24.0)
            self.assertEqual(
                bridge["profile"]["destination_merge_width_m"],
                15.1,
            )
            self.assertEqual(
                bridge["connection"]["source_generated_overlap_m"],
                0.0,
            )
            self.assertEqual(
                bridge["connection"]["destination_generated_overlap_m"],
                0.0,
            )
            self.assertEqual(
                bridge["connection"]["destination_vertical_step_m"],
                0.0,
            )
            self.assertEqual(
                bridge["connection"]["destination_grade_discontinuity"],
                0.0,
            )
            self.assertTrue(
                bridge["destination"]["existing_lanes_preserved"]
            )
            self.assertEqual(
                bridge["source"]["elevation_authority"][
                    "runtime_origin_plus_local_surface_y_m"
                ],
                0.1,
            )
            self.assertEqual(
                bridge["destination"]["elevation_authority"][
                    "authored_placement_origin_y_m"
                ],
                50.0,
            )
            self.assertEqual(
                bridge["destination"]["elevation_authority"][
                    "runtime_origin_plus_local_surface_y_m"
                ],
                0.2,
            )
            self.assertLessEqual(
                bridge["profile"]["sampled_maximum_grade"],
                bridge["profile"]["maximum_grade"],
            )
            self.assertEqual(bridge["supports"]["requested_count"], 56)
            self.assertEqual(bridge["supports"]["column_pair_count"], 56)
            self.assertEqual(bridge["supports"]["aabb_count"], 168)
            self.assertEqual(
                bridge["supports"]["aabb_vs_swept_roadway_prism"],
                "all-disjoint",
            )
            self.assertEqual(
                bridge["obstacle_avoidance"][
                    "ground_level_support_clearance"
                ]["clearance"],
                "all-column-aabbs-clear-of-authenticated-live-road-polygons",
            )
            self.assertEqual(
                bridge["supports"]["ground_road_no_pillar_stations_m"],
                list(range(80, 761, 40)),
            )
            self.assertEqual(bridge["supports"]["stations_m"][0], 800.0)
            self.assertEqual(
                bridge["supports"]["ground_road_collision_member"],
                "autopistaQr.mesh",
            )
            self.assertEqual(
                bridge["supports"]["ground_road_surface_materials"],
                ["calleunsolosentido", "pavimento"],
            )
            self.assertEqual(
                bridge["supports"]["ground_road_surface_triangle_count"],
                9599,
            )
            ground_clearance = bridge["obstacle_avoidance"][
                "ground_level_support_clearance"
            ]
            self.assertEqual(ground_clearance["column_aabb_count"], 112)
            self.assertTrue(
                ground_clearance["legacy_mesh_world_bounds_available"]
            )
            self.assertEqual(
                ground_clearance["ground_road"]["no_pillar_station_count"],
                18,
            )
            self.assertEqual(bridge["fixtures"]["instance_count"], 33)
            self.assertEqual(
                len(bridge["authentication"]["members"]),
                8,
            )
            self.assertEqual(
                bridge["authentication"]["ground_road"]["line_number"],
                378,
            )
            self.assertEqual(
                bridge["authentication"]["ground_road"]["object"],
                "autopistaQr",
            )
            self.assertTrue(
                bridge["authentication"]["open_gap"]["verified"]
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
            "ror-cityworld-local-overlay-build-result-v7",
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
                "neoq-fixed-camera-runtime-visual-gate-unavailable",
            ],
        )
        self.assertEqual(
            manifest["activation"]["contracts"]["zero_local_shadow"],
            {
                "required_local_shadow_casters": 0,
                "runtime_marker_field": "local_shadow_casters=0",
                "satisfied": True,
                "satisfied_by":
                    "TerrainObjectManager local-light creation policy",
            },
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
        self.assertIn("Version = 6", descriptor)
        self.assertIn(
            "Name = CityWorld Next Enhanced (Use This)",
            descriptor,
        )
        self.assertIn(
            "GUID = rorng-cityworld-next-local-overlay-v7",
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

    def test_neoq_source_pole_definitions_are_exactly_authenticated(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive, _ = self.make_archive(root)
            audit = BUILDER.audit_archive(archive)
            placements = BUILDER.source_placements(archive)
            telepoint = BUILDER.exact_telepoint(
                audit,
                BUILDER.DESTINATION_TELEPOINT,
            )
            manifest = BUILDER.neoq_light_candidate_manifest(
                placements,
                telepoint,
            )

        authenticated = BUILDER.authenticate_neoq_light_audit(
            audit,
            manifest,
        )
        self.assertEqual(
            authenticated,
            [
                BUILDER.NEOQ_EXPECTED_POLE_DEFINITIONS[family]
                for family in BUILDER.NEOQ_LUMINARIA_FAMILIES
            ],
        )

        missing = copy.deepcopy(audit)
        missing["lighting"]["object_definitions"][
            "source_pole_definitions"
        ].pop()
        duplicate = copy.deepcopy(audit)
        duplicate_definitions = duplicate["lighting"][
            "object_definitions"
        ]["source_pole_definitions"]
        duplicate_definitions[2] = copy.deepcopy(duplicate_definitions[1])
        cases: list[tuple[str, dict[str, object]]] = [
            ("missing", missing),
            ("duplicate", duplicate),
        ]
        mutations = (
            ("available", False),
            ("collision_geometry", False),
            ("lod", True),
            ("point_light_directives", 1),
            ("spot_light_directives", 1),
            ("sha256", "0" * 64),
            ("definition", "changed.odef"),
            ("bytes", 76),
        )
        for field, value in mutations:
            changed = copy.deepcopy(audit)
            changed["lighting"]["object_definitions"][
                "source_pole_definitions"
            ][0][field] = value
            cases.append((field, changed))
        hostile_types = copy.deepcopy(audit)
        hostile_types["lighting"]["object_definitions"][
            "source_pole_definitions"
        ][0]["point_light_directives"] = False
        cases.append(("hostile-type", hostile_types))

        for name, changed in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                    BUILDER.OverlayFailure,
                    "source-pole definition",
                ):
                    BUILDER.authenticate_neoq_light_audit(
                        changed,
                        manifest,
                    )

    def test_neoq_source_pole_archive_drift_fails_before_assets(self) -> None:
        mutated = list(AUTHENTIC_POLE_DEFINITIONS)
        mutated[0] = (mutated[0][0], mutated[0][1] + b" ")
        cases = (
            (
                "missing",
                AUTHENTIC_POLE_DEFINITIONS[:-1],
                BUILDER.OverlayFailure,
                "source-pole definition",
            ),
            (
                "mutated",
                tuple(mutated),
                BUILDER.OverlayFailure,
                "source-pole definition",
            ),
            (
                "duplicate",
                AUTHENTIC_POLE_DEFINITIONS
                + (
                    (
                        "nested/luminariaLQr.odef",
                        AUTHENTIC_POLE_DEFINITIONS[0][1],
                    ),
                ),
                BUILDER.AuditFailure,
                "duplicate NeoQueretaro source-pole definition family",
            ),
        )
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            for name, pole_definitions, exception, message in cases:
                with self.subTest(name=name):
                    root = parent / name
                    root.mkdir()
                    archive, digest = self.make_archive(
                        root,
                        pole_definitions=pole_definitions,
                    )
                    with (
                        mock.patch.object(
                            BUILDER,
                            "PINNED_ARCHIVE_SHA256",
                            digest,
                        ),
                        self.assertRaisesRegex(exception, message),
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
            # v8 generates its own multi-layer terrain configuration, but the
        # reference-only contract is unchanged: the generated OTC names the
        # original heightmap and archive textures, and none of those payloads
        # may enter the package.
        self.assertIn(
            "GeometryConfig = CityWorldNextEnhanced.otc", descriptor)
        page_otc = payloads["CityWorldNextEnhanced-page-0-0.otc"].decode(
            "utf-8")
        self.assertIn("CityWorld.raw", page_otc)
        for referenced_only in ("CityWorld.raw", "CityWorld_grass.dds",
                                "NQ2-0-asphalt.png", "asiaconcrete.dds",
                                "NQ-rock-A.jpg"):
            self.assertNotIn(referenced_only, payloads)
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
                14,
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
                    "derived_source_placement_record_count": 91,
                    "source_textures_copied": False,
                    "replacement_textures_independently_authored": True,
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

    def test_replacement_textures_are_packaged_namespaced_and_reported(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            _, output, _ = self.build_fixture(Path(directory))
            with zipfile.ZipFile(output) as package:
                payloads = {
                    name: package.read(name)
                    for name in package.namelist()
                }
            report = json.loads(payloads[BUILDER.REPORT_NAME])
            self.assertEqual(
                report["package"]["entries"],
                BUILDER.EXPECTED_PACKAGE_ENTRIES,
            )
            namespaced = sorted(
                name
                for name in payloads
                if name.startswith(BUILDER.REPLACEMENT_NAMESPACE_PREFIX)
            )
            expected_members = sorted(
                entry.replacement_member
                for entry in
                BUILDER.replacement_textures.REPLACEMENT_TEXTURES
            )
            self.assertEqual(namespaced, expected_members)
            self.assertEqual(len(namespaced), 8)
            records = {
                record["path"]: record
                for record in report["package"]["files"]
            }
            for member in expected_members:
                payload = payloads[member]
                self.assertEqual(payload[:8], b"\x89PNG\r\n\x1a\n")
                record = records[member]
                self.assertEqual(
                    record["role"], BUILDER.REPLACEMENT_TEXTURE_ROLE)
                self.assertEqual(
                    record["sha256"],
                    hashlib.sha256(payload).hexdigest(),
                )
            replacement_report = report["replacement_textures"]
            self.assertEqual(
                replacement_report["format"],
                BUILDER.replacement_textures.REPLACEMENT_TEXTURES_FORMAT,
            )
            self.assertEqual(
                replacement_report["namespace"],
                BUILDER.REPLACEMENT_NAMESPACE_PREFIX,
            )
            self.assertTrue(replacement_report["independently_authored"])
            self.assertEqual(
                sorted(
                    record["replacement_member"]
                    for record in replacement_report["textures"]
                ),
                expected_members,
            )
            for record in replacement_report["textures"]:
                self.assertNotEqual(
                    record["replacement_member"],
                    record["original_member"],
                )
                self.assertNotIn(record["original_member"], payloads)

    def test_replacement_namespace_collision_with_source_fails_closed(
        self,
    ) -> None:
        collisions = (
            # Exact member-name collision with the audited archive index.
            "cityworld_next_replacements/asiaconcrete_1024.png",
            # Basename collision: OGRE's zip basename fallback must never be
            # able to select a replacement for an original member request.
            "asiaconcrete_1024.png",
        )
        for collision in collisions:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive, digest = self.make_archive(
                    root,
                    extra_entries=((collision, b"original-member"),),
                )
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
                        "prepare_regional_infill_assets",
                        return_value=self.fake_regional_infill_bundle(),
                    ),
                    mock.patch.object(
                        BUILDER,
                        "authenticate_regional_infill_source",
                        return_value=
                            self.fake_regional_infill_source_authentication(),
                    ),
                ):
                    with self.assertRaisesRegex(
                        BUILDER.OverlayFailure,
                        "collides with an original CityWorld member",
                    ):
                        BUILDER.build_local_overlay(
                            archive_path=archive,
                            repository_path=REPOSITORY_ROOT,
                            output_path=output,
                            surface_offset_m=0.08,
                        )
                self.assertFalse(output.exists())

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
                mock.patch.object(
                    BUILDER,
                    "prepare_regional_infill_assets",
                    return_value=self.fake_regional_infill_bundle(),
                ),
                mock.patch.object(
                    BUILDER,
                    "authenticate_regional_infill_source",
                    return_value=
                        self.fake_regional_infill_source_authentication(),
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
                    "prepare_regional_infill_assets",
                    return_value=self.fake_regional_infill_bundle(),
                ),
                mock.patch.object(
                    BUILDER,
                    "authenticate_regional_infill_source",
                    return_value=
                        self.fake_regional_infill_source_authentication(),
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
                    "prepare_regional_infill_assets",
                    return_value=self.fake_regional_infill_bundle(),
                ),
                mock.patch.object(
                    BUILDER,
                    "authenticate_regional_infill_source",
                    return_value=
                        self.fake_regional_infill_source_authentication(),
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
            source_tobj_sha256 = hashlib.sha256(
                PLACEMENTS.encode("utf-8")
            ).hexdigest()

            def command(output: Path, optimized: bool) -> list[str]:
                code = (
                    "import runpy,sys;"
                    f"ns=runpy.run_path({str(TOOL_PATH)!r});"
                    "g=ns['main'].__globals__;"
                    f"g['PINNED_ARCHIVE_SHA256']={digest!r};"
                    "g['regional_infill'].PINNED_ARCHIVE_SHA256="
                    f"{digest!r};"
                    "g['regional_infill'].PINNED_TOBJ_SHA256="
                    f"{source_tobj_sha256!r};"
                    "import dataclasses;"
                    "g['regional_infill'].SOURCE_ANCHORS=tuple("
                    "dataclasses.replace("
                    "a,"
                    f"source_archive_sha256={digest!r},"
                    f"source_tobj_sha256={source_tobj_sha256!r}"
                    ") for a in g['regional_infill'].SOURCE_ANCHORS);"
                    "g['neoq_bridge'].PINNED_TOBJ_SHA256="
                    f"{source_tobj_sha256!r};"
                    "g['neoq_bridge'].AUTHENTICATED_MEMBERS="
                    f"{SYNTHETIC_BRIDGE_MEMBER_CONTRACT!r};"
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
