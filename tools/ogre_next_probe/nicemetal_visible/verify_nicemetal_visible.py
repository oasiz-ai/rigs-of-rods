#!/usr/bin/env python3
"""Fail-closed verifier for the isolated Ogre-Next NiceMetal visible proof."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[2]
DEFAULT_LOCK = SCRIPT_DIR / "ogre-next-nicemetal-visible-v1.lock.json"


def fail(message: str) -> None:
    raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        fail(f"JSON root is not an object: {path}")
    return value


def source_closure_digest(files: list[dict[str, str]]) -> str:
    digest = hashlib.sha256()
    for entry in sorted(files, key=lambda item: item["path"]):
        digest.update(entry["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(entry["sha256"]))
        digest.update(b"\n")
    return digest.hexdigest()


def verify_lock(lock_path: Path) -> tuple[dict[str, Any], str]:
    lock = load_json(lock_path)
    expected_top = {
        "schema_version",
        "profile_id",
        "ogre_next",
        "selector",
        "semantics",
        "source_files",
        "source_closure_sha256",
        "claim_limits",
    }
    if set(lock) != expected_top:
        fail("lock top-level keys differ from the reviewed closure")
    if lock["schema_version"] != "ror.ogre-next.nice-metal-visible-lock@1":
        fail("lock schema version changed")
    if lock["profile_id"] != "nice-metal-flex-v1":
        fail("profile id changed")
    if lock["ogre_next"] != {
        "commit": "37149a802de747f6806996fa3067b0748ecc1084",
        "archive_sha256": "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2",
        "renderer": "Metal Rendering Subsystem",
        "platform": "macos-arm64-metal",
    }:
        fail("pinned Ogre-Next identity or first-proof platform changed")
    if lock["selector"] != {
        "datablock_prefix": "RoRNiceMetalProof_",
        "hlms_property": "ror_nice_metal_flex_v1",
    }:
        fail("HLMS selector changed")
    if lock["semantics"] != {
        "base_texture_slot": "PBSM_DIFFUSE",
        "damaged_texture_slot": "PBSM_DETAIL0",
        "specular_texture_slot": "PBSM_SPECULAR",
        "vertex_red": "ignored",
        "vertex_green": "ignored",
        "vertex_blue": "wear_wetness",
        "vertex_alpha": "damage",
        "reflection": "stock_hlms_pbs_environment",
    }:
        fail("NiceMetal lowering semantics changed")
    if lock["claim_limits"] != {
        "legacy_renderer_used": False,
        "product_capture_integrated": False,
        "playable": False,
    }:
        fail("claim limits changed")
    files = lock["source_files"]
    if not isinstance(files, list) or not files:
        fail("source_files must be a non-empty list")
    paths: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict) or set(entry) != {"path", "sha256"}:
            fail("source file entry shape changed")
        relative = entry["path"]
        if relative in paths or relative.startswith("/") or ".." in Path(relative).parts:
            fail(f"unsafe or duplicate source path: {relative}")
        paths.add(relative)
        source = REPOSITORY_ROOT / relative
        if not source.is_file() or source.is_symlink():
            fail(f"locked source is missing or a symlink: {relative}")
        actual = sha256(source)
        if actual != entry["sha256"]:
            fail(f"locked source digest changed: {relative}: {actual}")
    closure = source_closure_digest(files)
    if closure != lock["source_closure_sha256"]:
        fail(f"source closure digest changed: {closure}")

    shader_path = (
        REPOSITORY_ROOT
        / "tools/ogre_next_probe/nicemetal_visible/media/Hlms/RoR/NiceMetal/NiceMetalFlex_piece_vs_piece_ps.any"
    )
    shader = shader_path.read_text(encoding="utf-8")
    required_shader_tokens = (
        "@property( ror_nice_metal_flex_v1 )",
        "VES_DIFFUSE",
        "COLOR0",
        "OGRE_DIFFUSE",
        "inPs.rorNiceMetalFlex.z",
        "inPs.rorNiceMetalFlex.w",
        "textureMaps@value( diffuse_map_idx )",
        "textureMaps@value( detail_map0_idx )",
        "textureMaps@value( specular_map_idx )",
        "@undefpiece( SampleDiffuseMap )",
        "@undefpiece( SampleSpecularMap )",
    )
    missing = [token for token in required_shader_tokens if token not in shader]
    if missing:
        fail(f"shader semantic tokens are missing: {missing}")
    if re.search(r"\b(Cg|Ogre14|HighLevelGpuProgram)\b", shader):
        fail("visible proof shader references the legacy program path")
    return lock, closure


def verify_build_root(build_root: Path, lock: dict[str, Any]) -> None:
    if not build_root.is_absolute() or not build_root.is_dir() or build_root.is_symlink():
        fail("Ogre-Next probe build root must be an absolute real directory")
    config = build_root / "generated/ror_ogre_next_frame_probe_config.h"
    if not config.is_file() or config.is_symlink():
        fail("Ogre-Next build root lacks its generated frame provenance")
    text = config.read_text(encoding="utf-8")
    for token in (
        f'ROR_OGRE_NEXT_FRAME_OGRE_COMMIT "{lock["ogre_next"]["commit"]}"',
        f'ROR_OGRE_NEXT_FRAME_OGRE_ARCHIVE_SHA256 "{lock["ogre_next"]["archive_sha256"]}"',
        f'ROR_OGRE_NEXT_FRAME_RENDERER_NAME "{lock["ogre_next"]["renderer"]}"',
    ):
        if token not in text:
            fail(f"Ogre-Next build provenance does not contain: {token}")


def verify_runtime(report_path: Path, image_path: Path, binary_path: Path,
                   lock_path: Path, lock: dict[str, Any], closure: str) -> None:
    for path, label in ((report_path, "report"), (image_path, "image"),
                        (binary_path, "binary")):
        if not path.is_file() or path.is_symlink():
            fail(f"runtime {label} is missing or a symlink: {path}")
    report = load_json(report_path)
    if report.get("schema_version") != "ror.ogre-next.nice-metal-visible-runtime@1":
        fail("runtime receipt schema changed")
    for key, expected in (
        ("status", "pass"),
        ("renderer", "ogre-next"),
        ("renderer_name", lock["ogre_next"]["renderer"]),
        ("gpu_readback", True),
        ("readback_api", "Ogre::Image2::convertFromTexture"),
        ("compositor2", True),
        ("hlms_property", lock["selector"]["hlms_property"]),
        ("datablock_prefix", lock["selector"]["datablock_prefix"]),
    ):
        if report.get(key) != expected:
            fail(f"runtime receipt field {key} differs from {expected!r}")
    if report.get("texture_bindings") != {
        "base": "PBSM_DIFFUSE",
        "damaged": "PBSM_DETAIL0",
        "specular": "PBSM_SPECULAR",
    }:
        fail("runtime texture-binding receipt changed")
    if report.get("vertex_colour_semantics") != {
        "red": "ignored",
        "green": "ignored",
        "blue": "wear_wetness",
        "alpha": "damage",
    }:
        fail("runtime vertex-colour receipt changed")
    metamorphic = report.get("metamorphic")
    if not isinstance(metamorphic, dict) or any(
        metamorphic.get(key) is not True
        for key in ("red_green_ignored", "alpha_selects_damaged",
                    "blue_dims_base_by_one_third")
    ):
        fail("runtime metamorphic flags are not all true")
    samples = metamorphic.get("samples")
    if not isinstance(samples, list) or len(samples) != 4:
        fail("runtime metamorphic samples are missing")
    base, rg_mutation, damaged, wear = samples
    channels = ("r", "g", "b")
    if max(abs(float(base[c]) - float(rg_mutation[c])) for c in channels) > 0.015:
        fail("red/green-only mutation changed the rendered pixels")
    if not float(base["r"]) > float(base["g"]) * 1.5:
        fail("base panel is not red-dominant")
    if not float(damaged["g"]) > float(damaged["r"]) * 1.5:
        fail("damage panel is not damaged-texture green-dominant")
    wear_ratio = float(wear["r"]) / float(base["r"])
    if not 0.45 < wear_ratio < 0.82:
        fail("blue wear panel did not attenuate the base within the reviewed range")
    frame = report.get("frame")
    if not isinstance(frame, dict) or frame.get("width") != 256 or frame.get("height") != 144:
        fail("runtime frame dimensions changed")
    if int(frame.get("non_background_pixels", 0)) < 3000:
        fail("runtime frame does not contain enough rendered geometry")
    evidence = report.get("evidence")
    expected_evidence = {
        "lock_sha256": sha256(lock_path),
        "source_closure_sha256": closure,
        "frame_sha256": sha256(image_path),
        "binary_sha256": sha256(binary_path),
        "ogre_next_commit": lock["ogre_next"]["commit"],
        "ogre_next_archive_sha256": lock["ogre_next"]["archive_sha256"],
    }
    if evidence != expected_evidence:
        fail("runtime evidence digests do not bind the exact lock, source, image, and binary")
    if report.get("scope") != lock["claim_limits"]:
        fail("runtime scope overstates product integration or playability")
    header = image_path.read_bytes()[:32]
    if not header.startswith(b"P6\n256 144\n255\n"):
        fail("runtime image is not the reviewed PPM readback shape")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--ogre-build-root", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--binary", type=Path)
    arguments = parser.parse_args()
    try:
        lock_path = arguments.lock.resolve(strict=True)
        lock, closure = verify_lock(lock_path)
        if arguments.ogre_build_root is not None:
            verify_build_root(arguments.ogre_build_root.resolve(strict=True), lock)
        runtime_args = (arguments.report, arguments.image, arguments.binary)
        if any(value is not None for value in runtime_args):
            if any(value is None for value in runtime_args):
                fail("--report, --image, and --binary must be supplied together")
            verify_runtime(
                arguments.report.resolve(strict=True),
                arguments.image.resolve(strict=True),
                arguments.binary.resolve(strict=True),
                lock_path,
                lock,
                closure,
            )
        print(json.dumps({
            "status": "pass",
            "lock_sha256": sha256(lock_path),
            "source_closure_sha256": closure,
            "runtime_verified": arguments.report is not None,
        }, sort_keys=True))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"NiceMetal visible proof verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
