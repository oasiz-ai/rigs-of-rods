#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Solve deterministic CityWorld bridge placements from asset connectors.

The solver consumes already validated CityWorld asset manifests. It places
each module so its declared entry connector and travel tangent exactly match
the previous module's exit. Output is a canonical RoR ``.tobj`` fragment plus
a JSON report suitable for checked runtime fixtures.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path, PurePosixPath
import sys
from typing import Any, Sequence


POSITION_EPSILON = 1e-6


class CorridorFailure(RuntimeError):
    """Fail-closed error for invalid connector data or placement."""


@dataclass(frozen=True)
class Connector:
    identifier: str
    position_x: float
    position_z: float
    forward_x: float
    forward_z: float


@dataclass(frozen=True)
class AssetProfile:
    asset_id: str
    entry: Connector
    exit: Connector
    manifest_path: str


@dataclass(frozen=True)
class Placement:
    asset_id: str
    entry_heading_degrees: float
    entry_x: float
    entry_z: float
    exit_heading_degrees: float
    exit_x: float
    exit_z: float
    instance_name: str
    origin_x: float
    origin_z: float
    yaw_degrees: float


def canonical_json(document: Any) -> str:
    return json.dumps(
        document,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    )


def stable_float(value: float) -> str:
    rounded = 0.0 if abs(value) < 5e-10 else value
    return f"{rounded:.9f}".rstrip("0").rstrip(".")


def safe_relative_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if (
        not value
        or "\\" in value
        or path.is_absolute()
        or any(part in ("", ".", "..") for part in path.parts)
        or path.as_posix() != value
    ):
        raise CorridorFailure(f"unsafe manifest path: {value}")
    return path


def finite_float(value: Any, label: str) -> float:
    if isinstance(value, bool):
        raise CorridorFailure(f"{label} must be a finite number")
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise CorridorFailure(f"{label} must be a finite number") from error
    if not math.isfinite(result):
        raise CorridorFailure(f"{label} must be a finite number")
    return result


def vector3(value: Any, label: str) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) != 3:
        raise CorridorFailure(f"{label} must contain three numbers")
    return tuple(
        finite_float(component, f"{label}[{index}]")
        for index, component in enumerate(value)
    )


def blender_to_runtime(
    value: tuple[float, float, float],
) -> tuple[float, float, float]:
    return (value[0], value[2], -value[1])


def heading(x: float, z: float) -> float:
    length = math.hypot(x, z)
    if abs(length - 1.0) > POSITION_EPSILON:
        raise CorridorFailure("connector forward vector is not unit length")
    return math.atan2(x, z)


def rotate(x: float, z: float, yaw: float) -> tuple[float, float]:
    cosine = math.cos(yaw)
    sine = math.sin(yaw)
    return (
        cosine * x + sine * z,
        -sine * x + cosine * z,
    )


def normalized_degrees(value: float) -> float:
    result = math.degrees(math.atan2(math.sin(value), math.cos(value)))
    return 0.0 if abs(result) < 5e-10 else result


def load_asset_profile(repository: Path, relative_manifest: str) -> AssetProfile:
    pure = safe_relative_path(relative_manifest)
    manifest_path = (repository / pure).resolve()
    try:
        manifest_path.relative_to(repository.resolve())
    except ValueError as error:
        raise CorridorFailure(
            f"manifest escapes repository: {relative_manifest}"
        ) from error
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CorridorFailure(
            f"cannot read asset manifest: {relative_manifest}"
        ) from error
    if not isinstance(manifest, dict):
        raise CorridorFailure(f"invalid asset contract: {relative_manifest}")
    asset = manifest.get("asset")
    connectors = manifest.get("connectors")
    if (
        manifest.get("format") != "ror-cityworld-asset-v1"
        or not isinstance(asset, dict)
        or not isinstance(asset.get("id"), str)
        or not asset["id"].startswith("rorng_")
        or not isinstance(connectors, list)
        or len(connectors) != 2
    ):
        raise CorridorFailure(f"invalid asset contract: {relative_manifest}")
    records = {
        item.get("id"): item
        for item in connectors
        if isinstance(item, dict)
    }
    if set(records) != {"start", "end"}:
        raise CorridorFailure(
            f"asset connectors must be start/end: {relative_manifest}"
        )

    def connector(identifier: str) -> Connector:
        record = records[identifier]
        position = blender_to_runtime(
            vector3(
                record.get("position_blender_z_up_m"),
                f"{identifier}.position",
            )
        )
        forward = blender_to_runtime(
            vector3(record.get("forward"), f"{identifier}.forward")
        )
        if abs(position[1]) > POSITION_EPSILON:
            raise CorridorFailure(
                f"{identifier} connector is not on the road plane"
            )
        heading(forward[0], forward[2])
        return Connector(
            identifier=identifier,
            position_x=position[0],
            position_z=position[2],
            forward_x=forward[0],
            forward_z=forward[2],
        )

    # Runtime traversal goes from manifest "end" toward manifest "start".
    return AssetProfile(
        asset_id=asset["id"],
        entry=connector("end"),
        exit=connector("start"),
        manifest_path=relative_manifest,
    )


def solve_corridor(
    profiles: Sequence[AssetProfile],
    *,
    entry_x: float,
    entry_z: float,
    heading_degrees: float,
) -> tuple[Placement, ...]:
    if not profiles:
        raise CorridorFailure("corridor requires at least one module")
    current_x = finite_float(entry_x, "entry_x")
    current_z = finite_float(entry_z, "entry_z")
    current_heading = math.radians(
        finite_float(heading_degrees, "heading_degrees")
    )
    placements: list[Placement] = []
    for index, profile in enumerate(profiles):
        local_entry_heading = heading(
            -profile.entry.forward_x,
            -profile.entry.forward_z,
        )
        yaw = current_heading - local_entry_heading
        rotated_entry = rotate(
            profile.entry.position_x,
            profile.entry.position_z,
            yaw,
        )
        origin_x = current_x - rotated_entry[0]
        origin_z = current_z - rotated_entry[1]
        rotated_exit = rotate(
            profile.exit.position_x,
            profile.exit.position_z,
            yaw,
        )
        exit_x = origin_x + rotated_exit[0]
        exit_z = origin_z + rotated_exit[1]
        exit_forward = rotate(
            profile.exit.forward_x,
            profile.exit.forward_z,
            yaw,
        )
        exit_heading = heading(exit_forward[0], exit_forward[1])
        entry_travel = rotate(
            -profile.entry.forward_x,
            -profile.entry.forward_z,
            yaw,
        )
        if (
            abs(entry_travel[0] - math.sin(current_heading))
            > POSITION_EPSILON
            or abs(entry_travel[1] - math.cos(current_heading))
            > POSITION_EPSILON
        ):
            raise CorridorFailure("entry tangent alignment is not exact")
        placements.append(
            Placement(
                asset_id=profile.asset_id,
                entry_heading_degrees=normalized_degrees(current_heading),
                entry_x=current_x,
                entry_z=current_z,
                exit_heading_degrees=normalized_degrees(exit_heading),
                exit_x=exit_x,
                exit_z=exit_z,
                instance_name=f"bridge_module_{index:02d}",
                origin_x=origin_x,
                origin_z=origin_z,
                yaw_degrees=normalized_degrees(yaw),
            )
        )
        current_x = exit_x
        current_z = exit_z
        current_heading = exit_heading
    return tuple(placements)


def tobj_text(
    placements: Sequence[Placement],
    *,
    surface_y: float,
) -> str:
    y = finite_float(surface_y, "surface_y")
    lines = [
        "// Generated by tools/solve_cityworld_bridge_corridor.py.",
        "// Adjacent connector positions and travel tangents are exact.",
    ]
    for placement in placements:
        lines.append(
            ", ".join(
                (
                    stable_float(placement.origin_x),
                    stable_float(y),
                    stable_float(placement.origin_z),
                    "0",
                    stable_float(placement.yaw_degrees),
                    "0",
                )
            )
            + f", {placement.asset_id} - {placement.instance_name}"
        )
    return "\n".join(lines) + "\n"


def report(
    profiles: Sequence[AssetProfile],
    placements: Sequence[Placement],
    *,
    surface_y: float,
) -> dict[str, Any]:
    seams = []
    for index in range(len(placements) - 1):
        first = placements[index]
        second = placements[index + 1]
        seams.append(
            {
                "heading_error_degrees": round(
                    abs(
                        first.exit_heading_degrees
                        - second.entry_heading_degrees
                    ),
                    9,
                ),
                "index": index,
                "position_gap_m": round(
                    math.hypot(
                        first.exit_x - second.entry_x,
                        first.exit_z - second.entry_z,
                    ),
                    9,
                ),
            }
        )
    return {
        "entry": {
            "heading_degrees": round(placements[0].entry_heading_degrees, 9),
            "x": round(placements[0].entry_x, 9),
            "z": round(placements[0].entry_z, 9),
        },
        "exit": {
            "heading_degrees": round(placements[-1].exit_heading_degrees, 9),
            "x": round(placements[-1].exit_x, 9),
            "z": round(placements[-1].exit_z, 9),
        },
        "format": "ror-cityworld-bridge-corridor-v1",
        "modules": [
            {
                "asset_id": placement.asset_id,
                "entry_heading_degrees": round(
                    placement.entry_heading_degrees,
                    9,
                ),
                "entry_x": round(placement.entry_x, 9),
                "entry_z": round(placement.entry_z, 9),
                "exit_heading_degrees": round(
                    placement.exit_heading_degrees,
                    9,
                ),
                "exit_x": round(placement.exit_x, 9),
                "exit_z": round(placement.exit_z, 9),
                "instance_name": placement.instance_name,
                "manifest": profiles[index].manifest_path,
                "origin_x": round(placement.origin_x, 9),
                "origin_z": round(placement.origin_z, 9),
                "yaw_degrees": round(placement.yaw_degrees, 9),
            }
            for index, placement in enumerate(placements)
        ],
        "seams": seams,
        "surface_y": round(finite_float(surface_y, "surface_y"), 9),
    }


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--asset", action="append", required=True)
    parser.add_argument("--entry-x", type=float, required=True)
    parser.add_argument("--entry-z", type=float, required=True)
    parser.add_argument("--heading-degrees", type=float, required=True)
    parser.add_argument("--surface-y", type=float, default=0.08)
    parser.add_argument(
        "--format",
        choices=("json", "tobj"),
        default="json",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    repository = args.repository.resolve()
    profiles = tuple(
        load_asset_profile(repository, relative)
        for relative in args.asset
    )
    placements = solve_corridor(
        profiles,
        entry_x=args.entry_x,
        entry_z=args.entry_z,
        heading_degrees=args.heading_degrees,
    )
    if args.format == "tobj":
        sys.stdout.write(tobj_text(placements, surface_y=args.surface_y))
    else:
        sys.stdout.write(
            canonical_json(
                report(
                    profiles,
                    placements,
                    surface_y=args.surface_y,
                )
            )
            + "\n"
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CorridorFailure as error:
        print(f"CityWorld corridor solve failed: {error}", file=sys.stderr)
        raise SystemExit(1)
