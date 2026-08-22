#!/usr/bin/env python3
"""Fail closed unless every aerial-haze media file matches its reviewed lock.

The runtime media manifest already refuses to initialize on a byte change, but
that gate lives downstream of configure. This script is the configure-time
sibling: it authenticates the RoR-owned haze material and its three shader
siblings before CMake ever embeds their digests, so a local edit cannot slip
into a generated manifest unreviewed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


_VULKAN_PROGRAM = re.compile(
    r"fragment_program\s+(?P<name>\S+)\s+glslvk\s*\{(?P<body>.*?)\}",
    re.DOTALL,
)
_SOURCE = re.compile(r"^\s*source\s+(?P<source>\S+)\s*$", re.MULTILINE)
_ROOT_LAYOUT = re.compile(
    r"^\s*root_layout\s+(?P<layout>\S+)\s*$", re.MULTILINE
)
_VULKAN_RESOURCE_SLOT = re.compile(r"\bogre_[ts](?P<slot>[0-9]+)\b")
_ROOT_LAYOUT_CAPACITY = {"standard": 4, "high": 8, "max": 32}


def validate_vulkan_root_layout_capacity(root: Path, material: Path) -> None:
    """Ensure low-level Vulkan shader declarations cover every used slot.

    Ogre-Next defaults script-declared low-level programs to its four-slot
    ``standard`` root layout.  The resource binding macros are generated from
    that declaration before GLSL compilation, so a shader that uses ``ogre_t4``
    or above must explicitly select a larger prefab layout.
    """

    root = root.resolve(strict=True)
    material = material.resolve(strict=True)
    material_source = material.read_text(encoding="utf-8")
    for program in _VULKAN_PROGRAM.finditer(material_source):
        body = program.group("body")
        source_match = _SOURCE.search(body)
        if source_match is None:
            raise SystemExit(
                f"Vulkan program has no source: {program.group('name')}"
            )
        shader_name = source_match.group("source")
        shader_candidates = (
            material.parent / shader_name,
            material.parent / "GLSL" / shader_name,
        )
        shader = next((path for path in shader_candidates if path.is_file()), None)
        if shader is None:
            raise SystemExit(
                f"Vulkan shader source is missing for {program.group('name')}: "
                f"{shader_name}"
            )
        shader = shader.resolve(strict=True)
        if root not in shader.parents:
            raise SystemExit(
                f"Vulkan shader source escaped the probe root: {shader_name}"
            )
        slots = [
            int(match.group("slot"))
            for match in _VULKAN_RESOURCE_SLOT.finditer(
                shader.read_text(encoding="utf-8")
            )
        ]
        required_capacity = max(slots, default=-1) + 1
        layout_match = _ROOT_LAYOUT.search(body)
        layout = layout_match.group("layout") if layout_match else "standard"
        capacity = _ROOT_LAYOUT_CAPACITY.get(layout)
        if capacity is None:
            raise SystemExit(
                f"unsupported Vulkan root layout for {program.group('name')}: {layout}"
            )
        if required_capacity > capacity:
            raise SystemExit(
                f"Vulkan root layout is too small for {program.group('name')}: "
                f"{layout} exposes {capacity} slots, shader requires "
                f"{required_capacity}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    lock_path = args.lock.resolve(strict=True)
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if lock.get("schema") != "ror.ogre_next_aerial_haze_media_lock.v1":
        raise SystemExit("unsupported aerial-haze media-lock schema")
    files = lock.get("files")
    if not isinstance(files, list) or not files:
        raise SystemExit("aerial-haze media lock is empty")
    materials: list[Path] = []
    for entry in files:
        if not isinstance(entry, dict):
            raise SystemExit("aerial-haze media lock entry is malformed")
        relative = entry.get("path")
        expected = entry.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise SystemExit("aerial-haze media lock entry is incomplete")
        shader = (root / relative).resolve(strict=True)
        if root not in shader.parents:
            raise SystemExit(
                f"aerial-haze media file escaped the probe root: {relative}"
            )
        observed = hashlib.sha256(shader.read_bytes()).hexdigest()
        if observed != expected:
            raise SystemExit(
                f"aerial-haze media digest mismatch for {relative}: "
                f"expected {expected}, got {observed}"
            )
        if shader.suffix == ".material":
            materials.append(shader)
    for material in materials:
        validate_vulkan_root_layout_capacity(root, material)
    print(f"aerial-haze media verified: {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
