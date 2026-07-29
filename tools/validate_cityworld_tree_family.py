#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the deterministic NeoQueretaro tree-family contract and assets."""

from __future__ import annotations

import argparse
from collections import Counter
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
    canonical_json,
    reject_duplicate_keys,
)


FAMILY_FORMAT = "ror-cityworld-tree-family-v1"
FAMILY_ID = "rorng_city_neoq_tree_family"
WIND_FORMAT = "ror-cityworld-vegetation-wind-v1"
IMPOSTOR_FORMAT = "ror-cityworld-vegetation-impostor-contract-v1"
ASSET_PROFILE = "static-fixture-v1"
SOURCE_PLACEMENT_COUNT = 18
EXPECTED_SILHOUETTES = {"round", "columnar", "windswept"}
EXPECTED_COMPILED_ROLES = {
    "material-fallback",
    "terrain-object",
    "collision-fixture",
    "render-lod0",
    "render-lod1",
    "render-lod2",
}
SHA256_HEX = frozenset("0123456789abcdef")


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
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError("path escapes repository root") from error
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


def placement_assignment(
    namespace: str,
    variants: tuple[str, ...],
    ordinal: int,
) -> dict[str, Any]:
    digest = hashlib.sha256(f"{namespace}:{ordinal}".encode("ascii")).digest()
    variant = variants[int.from_bytes(digest[:8], "little") % len(variants)]
    return {
        "digest_sha256": digest.hex(),
        "placement_ordinal": ordinal,
        "scale": round(
            0.94
            + int.from_bytes(digest[10:12], "little") * 0.12 / 65535.0,
            5,
        ),
        "variant": variant,
        "yaw_degrees": round(
            int.from_bytes(digest[8:10], "little") * 360.0 / 65536.0,
            3,
        ),
    }


class FamilyValidator:
    def __init__(self, repo_root: Path, family_path: Path):
        self.repo_root = repo_root.resolve()
        self.family_path = family_path.resolve()
        self.diagnostics: list[Diagnostic] = []
        self.family: dict[str, Any] | None = None
        self.stats = {
            "assets": 0,
            "compiled_outputs": 0,
            "placements": 0,
            "silhouettes": 0,
        }

    def add(self, code: str, path: str, message: str) -> None:
        self.diagnostics.append(Diagnostic(code, path, message))

    def load(self) -> None:
        try:
            self.family_path.relative_to(self.repo_root)
            self.family = load_json(self.family_path)
        except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
            self.add("FAMILY_READ", "$", str(error))

    def validate_identity(self) -> None:
        assert self.family is not None
        if self.family.get("format") != FAMILY_FORMAT:
            self.add("FAMILY_FORMAT", "$.format", f"expected {FAMILY_FORMAT}")
        asset = self.family.get("asset")
        if not isinstance(asset, dict):
            self.add("FAMILY_ASSET", "$.asset", "asset record is required")
        elif asset != {
            "author": "Oasiz AI and Rigs of Rods contributors",
            "id": FAMILY_ID,
            "license": "GPL-3.0-or-later",
            "source_uri": "https://github.com/oasiz-ai/rigs-of-rods",
            "version": 1,
        }:
            self.add("FAMILY_IDENTITY", "$.asset", "family identity is not canonical")

        authoring = self.family.get("authoring")
        generator = (
            authoring.get("generator")
            if isinstance(authoring, dict)
            else None
        )
        if not isinstance(generator, dict):
            self.add(
                "FAMILY_GENERATOR",
                "$.authoring.generator",
                "generator record is required",
            )
        else:
            try:
                generator_path = resolve_portable(
                    self.repo_root,
                    generator.get("path"),
                )
                expected_hash = generator.get("sha256")
                if not is_sha256(expected_hash) or (
                    sha256_file(generator_path) != expected_hash
                ):
                    self.add(
                        "FAMILY_GENERATOR_STALE",
                        "$.authoring.generator.sha256",
                        "generator hash is stale",
                    )
            except (OSError, ValueError) as error:
                self.add(
                    "FAMILY_GENERATOR",
                    "$.authoring.generator.path",
                    str(error),
                )
        provenance = (
            authoring.get("procedural_provenance")
            if isinstance(authoring, dict)
            else None
        )
        if provenance != {
            "external_geometry": False,
            "external_textures": False,
            "method": "deterministic-project-authored-blender-python",
            "rights_basis": "GPL-3.0-or-later project-authored source",
        }:
            self.add(
                "FAMILY_RIGHTS",
                "$.authoring.procedural_provenance",
                "project-owned procedural rights record is incomplete",
            )

    def validate_impostor_and_wind(self) -> None:
        assert self.family is not None
        impostor = self.family.get("impostor")
        if not isinstance(impostor, dict):
            self.add("IMPOSTOR_CONTRACT", "$.impostor", "contract is required")
        else:
            azimuth = impostor.get("azimuth_degrees")
            elevation = impostor.get("elevation_degrees")
            if (
                impostor.get("format") != IMPOSTOR_FORMAT
                or impostor.get("status") != "contract-only"
                or impostor.get("compiler_emits") is not False
                or not isinstance(azimuth, list)
                or len(azimuth) != 8
                or len(set(azimuth)) != 8
                or not isinstance(elevation, list)
                or len(elevation) != 3
                or impostor.get("frame_count") != 24
                or impostor.get("atlas_resolution_px") != [4096, 4096]
                or impostor.get("pivot") != "trunk-ground-centre"
            ):
                self.add(
                    "IMPOSTOR_PROFILE",
                    "$.impostor",
                    "impostor-ready bake profile is incomplete",
                )
            alpha = impostor.get("alpha")
            if alpha != {
                "coverage_preserving_mips": True,
                "edge_dilation_px": 8,
                "mode": "mask",
            }:
                self.add(
                    "IMPOSTOR_ALPHA",
                    "$.impostor.alpha",
                    "alpha-safe mip contract is incomplete",
                )

        wind = self.family.get("wind")
        if (
            not isinstance(wind, dict)
            or wind.get("format") != WIND_FORMAT
            or wind.get("runtime_consumes") is not False
            or wind.get("phase_source") != "instance-hash"
            or wind.get("anchor_height_m") != 0.65
            or wind.get("canopy_start_height_m") != 2.5
            or wind.get("max_tip_displacement_m") != 0.48
        ):
            self.add(
                "WIND_PROFILE",
                "$.wind",
                "wind-ready family metadata is incomplete",
            )

    def validate_asset(
        self,
        *,
        index: int,
        entry: dict[str, Any],
    ) -> tuple[str, str] | None:
        pointer = f"$.variants[{index}]"
        asset_id = entry.get("asset_id")
        silhouette = entry.get("silhouette")
        if (
            not isinstance(asset_id, str)
            or not asset_id.startswith("rorng_city_neoq_tree_")
            or silhouette not in EXPECTED_SILHOUETTES
            or entry.get("weight") != 1
        ):
            self.add(
                "VARIANT_IDENTITY",
                pointer,
                "variant identity, silhouette, or weight is invalid",
            )
            return None
        try:
            manifest_path = resolve_portable(
                self.repo_root,
                entry.get("manifest"),
            )
        except ValueError as error:
            self.add("VARIANT_PATH", f"{pointer}.manifest", str(error))
            return None

        validator = Validator(self.repo_root, manifest_path)
        report = validator.validate()
        if not report["summary"]["valid"]:
            first = report["diagnostics"][0]
            self.add(
                "VARIANT_ASSET_INVALID",
                f"{pointer}.manifest",
                f"{first['code']} at {first['path']}: {first['message']}",
            )
            return None
        assert (
            validator.manifest is not None
            and validator.glb is not None
        )
        manifest = validator.manifest
        if (
            manifest.get("asset", {}).get("id") != asset_id
            or manifest.get("asset", {}).get("profile") != ASSET_PROFILE
        ):
            self.add(
                "VARIANT_MANIFEST_ID",
                pointer,
                "asset manifest identity does not match the family",
            )
        geometry = manifest.get("geometry", {})
        lods = geometry.get("lods", [])
        counts = {
            lod.get("lod"): lod.get("triangles")
            for lod in lods
            if isinstance(lod, dict)
        }
        if (
            geometry.get("lod0_triangle_ceiling") != 35_000
            or not isinstance(counts.get(0), int)
            or not 10_000 <= counts[0] <= 35_000
            or not isinstance(counts.get(1), int)
            or counts[1] / counts[0] > 0.4
            or not isinstance(counts.get(2), int)
            or counts[2] / counts[0] > 0.12
        ):
            self.add(
                "VARIANT_LOD_BUDGET",
                f"{pointer}.manifest",
                "tree triangle count or authored LOD reduction is invalid",
            )
        height = geometry.get("fixture_height_m")
        footprint = geometry.get("footprint_diameter_m")
        if (
            not isinstance(height, (int, float))
            or not 8.0 <= float(height) <= 12.0
            or not isinstance(footprint, (int, float))
            or not 4.0 <= float(footprint) <= 9.0
        ):
            self.add(
                "VARIANT_SCALE",
                f"{pointer}.manifest",
                "tree scale is outside the urban broadleaf envelope",
            )
        vegetation = manifest.get("vegetation")
        if not isinstance(vegetation, dict):
            self.add(
                "VARIANT_VEGETATION",
                f"{pointer}.manifest",
                "vegetation metadata is required",
            )
        else:
            foliage = vegetation.get("foliage")
            wind = vegetation.get("wind")
            asset_impostor = vegetation.get("impostor")
            if (
                vegetation.get("family") != FAMILY_ID
                or vegetation.get("silhouette_variant") != silhouette
                or foliage
                != {
                    "alpha_mode": "opaque-geometry",
                    "mip_safe": True,
                    "texture_dependencies": [],
                }
                or not isinstance(wind, dict)
                or wind.get("format") != WIND_FORMAT
                or wind.get("runtime_consumes") is not False
                or asset_impostor
                != {
                    "compiler_emits": False,
                    "format": IMPOSTOR_FORMAT,
                    "status": "contract-only",
                }
            ):
                self.add(
                    "VARIANT_VEGETATION_PROFILE",
                    f"{pointer}.manifest",
                    "foliage, wind, or impostor metadata is incomplete",
                )
        if manifest.get("export", {}).get("textures") != []:
            self.add(
                "VARIANT_TEXTURES",
                f"{pointer}.manifest",
                "v1 tree foliage must remain geometry-only",
            )

        for lod in lods:
            if not isinstance(lod, dict):
                continue
            node = validator.glb.node_by_name(lod.get("name"))
            extras = node.get("extras", {}) if isinstance(node, dict) else {}
            required = {
                "rorng_family_id": FAMILY_ID,
                "rorng_impostor_format": IMPOSTOR_FORMAT,
                "rorng_impostor_status": "contract-only",
                "rorng_silhouette_variant": silhouette,
                "rorng_wind_anchor_height_m": 0.65,
                "rorng_wind_canopy_start_height_m": 2.5,
                "rorng_wind_format": WIND_FORMAT,
                "rorng_wind_max_tip_displacement_m": 0.48,
                "rorng_wind_phase_source": "instance-hash",
            }
            if any(extras.get(key) != value for key, value in required.items()):
                self.add(
                    "VARIANT_NODE_METADATA",
                    f"{pointer}.manifest",
                    f"LOD{lod.get('lod')} node lacks wind/impostor metadata",
                )

        collision = manifest.get("collision", {})
        objects = collision.get("objects", [])
        collision_entry = objects[0] if len(objects) == 1 else {}
        bounds = (
            collision_entry.get("bounds_blender_z_up", {})
            if isinstance(collision_entry, dict)
            else {}
        )
        minimum = bounds.get("min")
        maximum = bounds.get("max")
        if (
            collision.get("purpose") != "trunk-only-vehicle-contact"
            or not isinstance(minimum, list)
            or not isinstance(maximum, list)
            or len(minimum) != 3
            or len(maximum) != 3
            or abs(float(minimum[2])) > 1e-5
            or not 3.0 <= float(maximum[2]) <= 4.0
            or max(abs(float(minimum[0])), abs(float(maximum[0]))) > 0.6
            or max(abs(float(minimum[1])), abs(float(maximum[1]))) > 0.6
        ):
            self.add(
                "VARIANT_COLLISION",
                f"{pointer}.manifest",
                "collision must remain a bounded trunk-only proxy",
            )

        compiled = manifest.get("compiled")
        outputs = compiled.get("outputs") if isinstance(compiled, dict) else None
        if not isinstance(outputs, list):
            self.add(
                "VARIANT_COMPILED",
                f"{pointer}.manifest",
                "compiled runtime record is required",
            )
        else:
            roles = {
                output.get("role")
                for output in outputs
                if isinstance(output, dict)
            }
            if roles != EXPECTED_COMPILED_ROLES or len(outputs) != 6:
                self.add(
                    "VARIANT_COMPILED_ROLES",
                    f"{pointer}.manifest",
                    "compiled runtime outputs are incomplete",
                )
            for output_index, output in enumerate(outputs):
                if not isinstance(output, dict):
                    continue
                try:
                    output_path = resolve_portable(
                        self.repo_root,
                        output.get("path"),
                    )
                    if (
                        output_path.stat().st_size != output.get("size")
                        or not is_sha256(output.get("sha256"))
                        or sha256_file(output_path) != output.get("sha256")
                    ):
                        self.add(
                            "VARIANT_COMPILED_STALE",
                            f"{pointer}.compiled.outputs[{output_index}]",
                            "compiled output hash or size is stale",
                        )
                except (OSError, ValueError) as error:
                    self.add(
                        "VARIANT_COMPILED_PATH",
                        f"{pointer}.compiled.outputs[{output_index}]",
                        str(error),
                    )
            self.stats["compiled_outputs"] += len(outputs)
        return asset_id, silhouette

    def validate_variants_and_selector(self) -> None:
        assert self.family is not None
        variants = self.family.get("variants")
        if not isinstance(variants, list) or len(variants) != 3:
            self.add(
                "VARIANT_COUNT",
                "$.variants",
                "exactly three tree variants are required",
            )
            return
        validated = [
            self.validate_asset(index=index, entry=entry)
            if isinstance(entry, dict)
            else None
            for index, entry in enumerate(variants)
        ]
        valid_entries = [entry for entry in validated if entry is not None]
        asset_ids = tuple(entry[0] for entry in valid_entries)
        silhouettes = {entry[1] for entry in valid_entries}
        self.stats["assets"] = len(asset_ids)
        self.stats["silhouettes"] = len(silhouettes)
        if (
            len(set(asset_ids)) != 3
            or silhouettes != EXPECTED_SILHOUETTES
        ):
            self.add(
                "VARIANT_COVERAGE",
                "$.variants",
                "round, columnar, and windswept assets must be unique",
            )
            return

        target = self.family.get("placement_target")
        if target != {
            "integration_status": "asset-ready-placement-deferred",
            "legacy_object": "arbol1Qr",
            "map": "CityWorld/NeoQueretaro",
            "placement_count": SOURCE_PLACEMENT_COUNT,
        }:
            self.add(
                "PLACEMENT_TARGET",
                "$.placement_target",
                "legacy replacement target is not canonical",
            )
        selector = self.family.get("selector")
        namespace = (
            selector.get("namespace")
            if isinstance(selector, dict)
            else None
        )
        assignments = (
            selector.get("assignments")
            if isinstance(selector, dict)
            else None
        )
        if (
            not isinstance(namespace, str)
            or not namespace
            or not isinstance(assignments, list)
            or selector.get("algorithm")
            != "sha256-little-endian-modulo-v1"
        ):
            self.add(
                "SELECTOR_PROFILE",
                "$.selector",
                "selector contract is incomplete",
            )
            return
        expected = [
            placement_assignment(namespace, asset_ids, ordinal)
            for ordinal in range(SOURCE_PLACEMENT_COUNT)
        ]
        if assignments != expected:
            self.add(
                "SELECTOR_STALE",
                "$.selector.assignments",
                "placement selection is not canonical",
            )
        counts = Counter(
            assignment.get("variant")
            for assignment in assignments
            if isinstance(assignment, dict)
        )
        if set(counts) != set(asset_ids) or min(counts.values()) < 3:
            self.add(
                "SELECTOR_DIVERSITY",
                "$.selector.assignments",
                "each silhouette needs at least three of the 18 placements",
            )
        for index, assignment in enumerate(assignments):
            if not isinstance(assignment, dict):
                continue
            scale = assignment.get("scale")
            yaw = assignment.get("yaw_degrees")
            if (
                not isinstance(scale, (int, float))
                or not 0.94 <= float(scale) <= 1.06
                or not isinstance(yaw, (int, float))
                or not 0.0 <= float(yaw) < 360.0
                or not math.isfinite(float(scale))
                or not math.isfinite(float(yaw))
            ):
                self.add(
                    "SELECTOR_TRANSFORM",
                    f"$.selector.assignments[{index}]",
                    "scale or yaw is outside the bounded variation profile",
                )
        self.stats["placements"] = len(assignments)

    def validate(self) -> dict[str, Any]:
        self.load()
        if self.family is not None:
            self.validate_identity()
            self.validate_impostor_and_wind()
            self.validate_variants_and_selector()
        diagnostics = sorted(
            (diagnostic.record() for diagnostic in self.diagnostics),
            key=lambda item: (item["code"], item["path"], item["message"]),
        )
        return {
            "diagnostics": diagnostics,
            "format": "ror-cityworld-tree-family-validation-v1",
            "summary": {
                **self.stats,
                "errors": len(diagnostics),
                "valid": not diagnostics,
            },
        }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "family",
        type=Path,
        nargs="?",
        default=Path(
            "content-source/cityworld_next/vegetation/"
            "rorng_city_neoq_tree_family.v1.json"
        ),
    )
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--pretty", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = FamilyValidator(args.repo_root, args.family).validate()
    if args.pretty:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    else:
        sys.stdout.write(canonical_json(report) + "\n")
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
