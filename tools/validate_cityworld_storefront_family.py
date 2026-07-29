#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed validation for the CityWorld storefront family."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import sys
from typing import Any


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

from validate_cityworld_asset import (  # noqa: E402
    Validator,
    reject_duplicate_keys,
)


FAMILY_FORMAT = "ror-cityworld-storefront-family-v1"
FAMILY_ID = "rorng_city_storefront_family"
GENERATOR_FORMAT = "ror-cityworld-storefront-family-generator-v1"
SELECTOR_NAMESPACE = "cityworld:penguinville:storefronts:v1"
SOURCE_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)
SHA256_HEX = frozenset("0123456789abcdef")
EXPECTED_COMPILED_ROLES = {
    "material-fallback",
    "terrain-object",
    "collision-fixture",
    "render-lod0",
    "render-lod1",
    "render-lod2",
}
ARTIFACT_REPRODUCIBILITY = {
    "blend": "authenticated-retained-session-metadata-bearing",
    "glb": "byte-deterministic-pinned-toolchain",
    "preview": "authenticated-retained-render-metadata-bearing",
}
EXPECTED_VARIANTS: tuple[dict[str, Any], ...] = (
    {
        "asset_id": "rorng_city_storefront_corner_20x20",
        "legacy_object": "store02",
        "style": "contemporary-corner",
        "footprint_m": [20.0, 20.0],
        "placement_count": 6,
        "legacy_bounds_m": {
            "min": [-10.0, -10.0, -1.0],
            "max": [10.0, 10.0, 12.7238],
        },
        "source_yaws": [0.0, 0.0, 180.0, 0.0, -180.0, -90.0],
        "render_mesh_sha256": (
            "1bf020b9786206ae46b98d04066dda05bf85fcc0863f514b4fb303e39a2abe1f"
        ),
        "collision_mesh_sha256": (
            "1d189b589170ba24962f580f728a901e3dc6774cec7cd6ddff6149fbcb330a58"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_heritage_20x30",
        "legacy_object": "store03",
        "style": "heritage-mixed-use",
        "footprint_m": [20.0, 30.0],
        "placement_count": 9,
        "legacy_bounds_m": {
            "min": [-10.0, -15.0, -1.0],
            "max": [10.0, 15.0, 16.5552],
        },
        "source_yaws": [90.0, -90.0, 0.0, 0.0, 0.0, 0.0, 0.0, -90.0, -90.0],
        "render_mesh_sha256": (
            "b0ccde2c053607cb0e1f7c78ef55461bf2fdda0706f5662ab220d6d27beaff99"
        ),
        "collision_mesh_sha256": (
            "36ea0e53be1909afa1031917f0e5893873aca92f3cf95e4befe119f387756f68"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_market_20x50",
        "legacy_object": "store05",
        "style": "market-hall",
        "footprint_m": [20.0, 50.0],
        "placement_count": 9,
        "legacy_bounds_m": {
            "min": [-10.0, -25.0, -1.0],
            "max": [10.0, 25.0, 20.5984],
        },
        "source_yaws": [-180.0, 90.0, 180.0, 90.0, 90.0, -90.0, 0.0, 180.0, -90.0],
        "render_mesh_sha256": (
            "66e5d94aceebbeea2ef658eaeb0ae764560dbe877ab13618998f1eaf3a130d3d"
        ),
        "collision_mesh_sha256": (
            "ead083e47534df7f9d1db32c0be7f608882ec7dad96f319314ce879709491e4e"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_arcade_30x10",
        "legacy_object": "store06",
        "style": "industrial-arcade",
        "footprint_m": [30.0, 10.0],
        "placement_count": 9,
        "legacy_bounds_m": {
            "min": [-15.0, -5.0, -1.0],
            "max": [15.0, 5.0, 12.3121],
        },
        "source_yaws": [-90.0, -180.0, -90.0, 0.0, -180.0, 0.0, -180.0, -90.0, -180.0],
        "render_mesh_sha256": (
            "6f1fe827a62f49db82a699ea01af9401c6ee65d24ef4d1a9c578cd0674530b26"
        ),
        "collision_mesh_sha256": (
            "5eb686c44552a1450705c4b4acfe67a23a5e3b027ad68ddd5987fc92b740f7f2"
        ),
    },
    {
        "asset_id": "rorng_city_storefront_gabled_20x10",
        "legacy_object": "store08",
        "style": "gabled-townhouse",
        "footprint_m": [20.0, 10.0],
        "placement_count": 7,
        "legacy_bounds_m": {
            "min": [-10.0, -5.05501, -1.0],
            "max": [10.0, 5.0, 16.9956],
        },
        "source_yaws": [-90.0, -90.0, -90.0, -180.0, -180.0, -90.0, 0.0],
        "render_mesh_sha256": (
            "04c0f4d00c22e6cb033db72e14f26ef10c97527e219d8dd402b34bdcca8947a0"
        ),
        "collision_mesh_sha256": (
            "7ef51b2da959f3ca90ab97a5fa7373f38692253ec6a96974aea152e6f7549dec"
        ),
    },
)


@dataclass(frozen=True)
class Diagnostic:
    code: str
    path: str
    message: str

    def record(self) -> dict[str, str]:
        return {
            "code": self.code,
            "message": self.message,
            "path": self.path,
        }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and set(value) <= SHA256_HEX
    )


def resolve_portable(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise ValueError("path is not a portable relative path")
    relative = PurePosixPath(value)
    if relative.is_absolute() or any(
        part in ("", ".", "..") for part in relative.parts
    ):
        raise ValueError("path is not a portable relative path")
    path = (root / value).resolve()
    path.relative_to(root.resolve())
    return path


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=reject_duplicate_keys,
        parse_constant=lambda token: (_ for _ in ()).throw(
            ValueError(f"non-finite JSON number: {token}")
        ),
    )
    if not isinstance(value, dict):
        raise ValueError("document root must be an object")
    return value


class FamilyValidator:
    def __init__(self, repo_root: Path, family_path: Path):
        self.repo_root = repo_root.resolve()
        self.family_path = family_path.resolve()
        self.family: dict[str, Any] | None = None
        self.diagnostics: list[Diagnostic] = []
        self.stats = {
            "assets": 0,
            "compiled_outputs": 0,
            "errors": 0,
            "placements": 0,
            "styles": 0,
        }

    def add(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic(code, path, message))

    def load(self) -> None:
        try:
            self.family_path.relative_to(self.repo_root)
            self.family = load_json(self.family_path)
        except (
            OSError,
            UnicodeDecodeError,
            json.JSONDecodeError,
            ValueError,
        ) as error:
            self.add("FAMILY_READ", "$", str(error))

    def validate_identity(self) -> None:
        assert self.family is not None
        if self.family.get("format") != FAMILY_FORMAT:
            self.add("FAMILY_FORMAT", "$.format", f"expected {FAMILY_FORMAT}")
        if self.family.get("asset") != {
            "author": "Oasiz AI and Rigs of Rods contributors",
            "id": FAMILY_ID,
            "license": "GPL-3.0-or-later",
            "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
            "version": 1,
        }:
            self.add("FAMILY_IDENTITY", "$.asset", "family identity is not canonical")
        authoring = self.family.get("authoring")
        generator = authoring.get("generator") if isinstance(authoring, dict) else None
        if not isinstance(generator, dict):
            self.add("FAMILY_GENERATOR", "$.authoring.generator", "record is required")
        else:
            if generator.get("format") != GENERATOR_FORMAT:
                self.add(
                    "FAMILY_GENERATOR_FORMAT",
                    "$.authoring.generator.format",
                    "generator format is not pinned",
                )
            for pointer, record in [
                ("$.authoring.generator", generator),
                *[
                    (f"$.authoring.generator.dependencies[{index}]", dependency)
                    for index, dependency in enumerate(
                        generator.get("dependencies", [])
                        if isinstance(generator.get("dependencies"), list)
                        else []
                    )
                ],
            ]:
                try:
                    source_path = resolve_portable(self.repo_root, record.get("path"))
                    if (
                        not source_path.is_file()
                        or source_path.is_symlink()
                        or not is_sha256(record.get("sha256"))
                        or sha256_file(source_path) != record["sha256"]
                    ):
                        self.add(
                            "FAMILY_GENERATOR_STALE",
                            f"{pointer}.sha256",
                            "generator source hash is stale",
                        )
                except (OSError, ValueError) as error:
                    self.add("FAMILY_GENERATOR", f"{pointer}.path", str(error))
        provenance = (
            authoring.get("procedural_provenance")
            if isinstance(authoring, dict)
            else None
        )
        if provenance != {
            "external_geometry": False,
            "external_materials": False,
            "external_textures": False,
            "legacy_facts_only": [
                "placement-count",
                "axis-aligned-bounds",
                "source-yaw",
            ],
            "method": "deterministic-project-authored-blender-python",
            "rights_basis": "GPL-3.0-or-later project-authored source",
        }:
            self.add(
                "FAMILY_RIGHTS",
                "$.authoring.procedural_provenance",
                "project-authored rights record is incomplete",
            )
        reproducibility = (
            authoring.get("artifact_reproducibility")
            if isinstance(authoring, dict)
            else None
        )
        if reproducibility != ARTIFACT_REPRODUCIBILITY:
            self.add(
                "FAMILY_REPRODUCIBILITY",
                "$.authoring.artifact_reproducibility",
                "artifact retention and deterministic output policy is incomplete",
            )

    def validate_legacy_audit(self) -> None:
        assert self.family is not None
        audit = self.family.get("legacy_audit")
        if not isinstance(audit, dict):
            self.add("LEGACY_AUDIT", "$.legacy_audit", "audit is required")
            return
        if audit.get("archive") != {
            "bytes": 158_845_395,
            "entries": 1_411,
            "name": "CityWorld.zip",
            "sha256": SOURCE_ARCHIVE_SHA256,
        }:
            self.add(
                "LEGACY_ARCHIVE",
                "$.legacy_audit.archive",
                "source archive identity is not pinned",
            )
        expected_objects = [
            {
                "collision_mesh_sha256": spec["collision_mesh_sha256"],
                "legacy_bounds_m": spec["legacy_bounds_m"],
                "legacy_object": spec["legacy_object"],
                "placement_count": spec["placement_count"],
                "render_mesh_sha256": spec["render_mesh_sha256"],
            }
            for spec in EXPECTED_VARIANTS
        ]
        if audit.get("objects") != expected_objects:
            self.add(
                "LEGACY_OBJECTS",
                "$.legacy_audit.objects",
                "legacy counts, bounds, or source hashes changed",
            )
        if (
            audit.get("method")
            != "read-only-placement-count-and-ogre-14.5.2-vertex-bounds"
            or audit.get("placement_count") != 40
            or audit.get("source_geometry_imported") is not False
            or audit.get("source_materials_imported") is not False
            or audit.get("source_textures_imported") is not False
        ):
            self.add(
                "LEGACY_SCOPE",
                "$.legacy_audit",
                "read-only compatibility-fact scope is not fail-closed",
            )

    def validate_variant(self, index: int, entry: Any) -> None:
        pointer = f"$.variants[{index}]"
        if not isinstance(entry, dict):
            self.add("VARIANT_RECORD", pointer, "variant must be an object")
            return
        expected = EXPECTED_VARIANTS[index]
        expected_record = {
            "asset_id": expected["asset_id"],
            "footprint_m": expected["footprint_m"],
            "legacy_object": expected["legacy_object"],
            "manifest": (
                "resources/nextgen/cityworld/buildings/storefront_family/"
                f"{expected['asset_id']}/{expected['asset_id']}.asset.json"
            ),
            "placement_count": expected["placement_count"],
            "style": expected["style"],
        }
        if entry != expected_record:
            self.add(
                "VARIANT_IDENTITY",
                pointer,
                "variant does not match the audited exact-fit contract",
            )
            return
        try:
            manifest_path = resolve_portable(self.repo_root, entry["manifest"])
            manifest = load_json(manifest_path)
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
            self.add("VARIANT_MANIFEST", f"{pointer}.manifest", str(error))
            return
        report = Validator(self.repo_root, manifest_path).validate()
        if not report["summary"]["valid"]:
            first = report["diagnostics"][0]
            self.add(
                "VARIANT_ASSET",
                f"{pointer}.manifest",
                f"{first['code']}: {first['message']}",
            )
        asset = manifest.get("asset", {})
        geometry = manifest.get("geometry", {})
        storefront = manifest.get("storefront", {})
        authoring = manifest.get("authoring", {})
        if asset.get("profile") != "static-building-v1":
            self.add(
                "VARIANT_PROFILE",
                f"{pointer}.manifest",
                "asset must use the static-building-v1 profile",
            )
        if (
            not isinstance(authoring, dict)
            or authoring.get("artifact_reproducibility")
            != ARTIFACT_REPRODUCIBILITY
        ):
            self.add(
                "VARIANT_REPRODUCIBILITY",
                f"{pointer}.manifest",
                "asset does not pin the authenticated retention policy",
            )
        if (
            geometry.get("asset_family") != FAMILY_ID
            or geometry.get("footprint_m") != expected["footprint_m"]
            or geometry.get("legacy_object") != expected["legacy_object"]
            or geometry.get("ground_plane_z_m") != 0.0
            or geometry.get("texcoord_policy")
            != "canonical-zero-textureless-v1"
        ):
            self.add(
                "VARIANT_GEOMETRY",
                f"{pointer}.manifest",
                "asset geometry does not match the exact-fit family",
            )
        grounding = storefront.get("grounding") if isinstance(storefront, dict) else None
        if grounding != {
            "foundation_below_ground_m": 0.0,
            "minimum_collision_z_m": 0.0,
            "minimum_render_z_m": 0.0,
            "placement_vertical_offset_m": 0.0,
        }:
            self.add(
                "VARIANT_GROUNDING",
                f"{pointer}.manifest",
                "building must be authored on the zero-metre ground plane",
            )
        lods = geometry.get("lods", [])
        if (
            not isinstance(lods, list)
            or len(lods) != 3
            or any(
                not isinstance(lod, dict)
                or lod.get("bounds_blender_z_up", {}).get("min", [None, None, None])[2]
                != 0.0
                for lod in lods
            )
        ):
            self.add(
                "VARIANT_RENDER_GROUND",
                f"{pointer}.manifest",
                "every render LOD must begin at exactly ground Z=0",
            )
        collision_objects = manifest.get("collision", {}).get("objects", [])
        if (
            not isinstance(collision_objects, list)
            or len(collision_objects) != 1
            or collision_objects[0]
            .get("bounds_blender_z_up", {})
            .get("min", [None, None, None])[2]
            != 0.0
        ):
            self.add(
                "VARIANT_COLLISION_GROUND",
                f"{pointer}.manifest",
                "collision proxy must begin at exactly ground Z=0",
            )
        emissive = [
            material
            for material in manifest.get("materials", [])
            if material.get("emissive_factor_linear")
        ]
        if (
            len(emissive) != 1
            or emissive[0].get("emissive_factor_linear")
            != [0.684, 0.3312, 0.0864]
            or storefront.get("emissive_policy", {}).get("runtime_point_lights")
            != 0
            or report["summary"]["runtime_lights"] != 0
        ):
            self.add(
                "VARIANT_EMISSIVE",
                f"{pointer}.manifest",
                "only the selected warm interior material may emit",
            )
        compiled = manifest.get("compiled")
        if not isinstance(compiled, dict):
            self.add(
                "VARIANT_COMPILED",
                f"{pointer}.manifest.compiled",
                "checked OGRE package is required",
            )
            return
        outputs = compiled.get("outputs")
        if (
            not isinstance(outputs, list)
            or {item.get("role") for item in outputs if isinstance(item, dict)}
            != EXPECTED_COMPILED_ROLES
        ):
            self.add(
                "VARIANT_COMPILED_ROLES",
                f"{pointer}.manifest.compiled.outputs",
                "compiled output role set is incomplete",
            )
            return
        for output_index, output in enumerate(outputs):
            output_pointer = (
                f"{pointer}.manifest.compiled.outputs[{output_index}]"
            )
            try:
                output_path = resolve_portable(self.repo_root, output.get("path"))
                if (
                    not output_path.is_file()
                    or output_path.is_symlink()
                    or output_path.stat().st_size != output.get("size")
                    or not is_sha256(output.get("sha256"))
                    or sha256_file(output_path) != output["sha256"]
                ):
                    self.add(
                        "VARIANT_COMPILED_STALE",
                        output_pointer,
                        "compiled output hash or size is stale",
                    )
            except (OSError, ValueError) as error:
                self.add("VARIANT_COMPILED_PATH", output_pointer, str(error))
        self.stats["assets"] += 1
        self.stats["compiled_outputs"] += len(outputs)

    def validate_variants(self) -> None:
        assert self.family is not None
        variants = self.family.get("variants")
        if not isinstance(variants, list) or len(variants) != len(EXPECTED_VARIANTS):
            self.add("VARIANTS", "$.variants", "exactly five variants are required")
            return
        for index, entry in enumerate(variants):
            self.validate_variant(index, entry)
        self.stats["styles"] = len(
            {
                entry.get("style")
                for entry in variants
                if isinstance(entry, dict)
            }
        )

    def validate_selector(self) -> None:
        assert self.family is not None
        selector = self.family.get("selector")
        if not isinstance(selector, dict):
            self.add("SELECTOR", "$.selector", "selector is required")
            return
        if (
            selector.get("algorithm")
            != "legacy-object-exact-fit-and-source-transform-v1"
            or selector.get("namespace") != SELECTOR_NAMESPACE
            or selector.get("scale_policy") != "preserve-source-uniform-one"
            or selector.get("yaw_policy") != "preserve-source-yaw-no-offset"
        ):
            self.add(
                "SELECTOR_POLICY",
                "$.selector",
                "selector policy is not compatibility preserving",
            )
        expected_assignments: list[dict[str, Any]] = []
        global_ordinal = 0
        for spec in EXPECTED_VARIANTS:
            for source_ordinal, source_yaw in enumerate(spec["source_yaws"]):
                yaw = float(source_yaw) % 360.0
                payload = (
                    f"{SELECTOR_NAMESPACE}:{spec['legacy_object']}:"
                    f"{source_ordinal}:{yaw:.3f}"
                ).encode("ascii")
                expected_assignments.append(
                    {
                        "digest_sha256": hashlib.sha256(payload).hexdigest(),
                        "global_ordinal": global_ordinal,
                        "legacy_object": spec["legacy_object"],
                        "source_ordinal": source_ordinal,
                        "uniform_scale": 1.0,
                        "variant": spec["asset_id"],
                        "yaw_degrees": yaw,
                        "yaw_offset_degrees": 0.0,
                    }
                )
                global_ordinal += 1
        if selector.get("assignments") != expected_assignments:
            self.add(
                "SELECTOR_STALE",
                "$.selector.assignments",
                "variant, yaw, scale, or digest assignments are stale",
            )
        else:
            self.stats["placements"] = len(expected_assignments)
        forbidden = {"position", "location", "x", "y", "z"}
        for index, assignment in enumerate(
            selector.get("assignments", [])
            if isinstance(selector.get("assignments"), list)
            else []
        ):
            if isinstance(assignment, dict) and forbidden & set(assignment):
                self.add(
                    "SELECTOR_PLACEMENT",
                    f"$.selector.assignments[{index}]",
                    "family milestone must not emit map positions",
                )

    def validate_target(self) -> None:
        assert self.family is not None
        if self.family.get("placement_target") != {
            "city": "Penguinville",
            "integration_status": "asset-ready-placement-deferred",
            "map": "CityWorld",
            "placement_count": 40,
        }:
            self.add(
                "PLACEMENT_TARGET",
                "$.placement_target",
                "placement integration must remain explicitly deferred",
            )

    def validate(self) -> dict[str, Any]:
        self.load()
        if self.family is not None:
            self.validate_identity()
            self.validate_legacy_audit()
            self.validate_variants()
            self.validate_selector()
            self.validate_target()
        self.diagnostics.sort(key=lambda item: (item.path, item.code, item.message))
        self.stats["errors"] = len(self.diagnostics)
        return {
            "diagnostics": [item.record() for item in self.diagnostics],
            "format": "ror-cityworld-storefront-family-validation-v1",
            "summary": {
                **self.stats,
                "valid": not self.diagnostics,
            },
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("family", type=Path)
    parser.add_argument("--repo-root", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = FamilyValidator(args.repo_root, args.family).validate()
    print(
        json.dumps(
            report,
            ensure_ascii=True,
            separators=(",", ":"),
            sort_keys=True,
        )
    )
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
