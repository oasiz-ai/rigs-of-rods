#!/usr/bin/env python3
"""Verify the exact Ogre-Next source closure used by PSSM_3_CASCADE_V1."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import NoReturn


SCHEMA_VERSION = 1
OGRE_NEXT_COMMIT = "37149a802de747f6806996fa3067b0748ecc1084"
PLATFORM_POLICIES = [
    "macos-arm64-metal",
    "linux-x86_64-vulkan",
    "windows-x64-d3d11",
]
SOURCE_ROLES_AND_PATHS = [
    ("shadow_node_api", "OgreMain/include/Compositor/OgreCompositorShadowNode.h"),
    (
        "shadow_node_runtime_and_helper",
        "OgreMain/src/Compositor/OgreCompositorShadowNode.cpp",
    ),
    (
        "shadow_node_definition_api",
        "OgreMain/include/Compositor/OgreCompositorShadowNodeDef.h",
    ),
    (
        "shadow_node_definition_validation",
        "OgreMain/src/Compositor/OgreCompositorShadowNodeDef.cpp",
    ),
    (
        "scene_pass_shadow_binding_api",
        "OgreMain/include/Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h",
    ),
    (
        "scene_pass_shadow_execution",
        "OgreMain/src/Compositor/Pass/PassScene/OgreCompositorPassScene.cpp",
    ),
    (
        "workspace_first_only_ordering",
        "OgreMain/src/Compositor/OgreCompositorWorkspace.cpp",
    ),
    ("frustum_projection_api", "OgreMain/include/OgreFrustum.h"),
    ("frustum_projection_runtime", "OgreMain/src/OgreFrustum.cpp"),
    (
        "compositor_node_definition_api",
        "OgreMain/include/Compositor/OgreCompositorManager2.h",
    ),
    (
        "compositor_node_definition_lifecycle",
        "OgreMain/src/Compositor/OgreCompositorManager2.cpp",
    ),
    ("pssm_split_api", "OgreMain/include/OgreShadowCameraSetupPSSM.h"),
    ("pssm_split_runtime", "OgreMain/src/OgreShadowCameraSetupPSSM.cpp"),
    ("focused_shadow_api", "OgreMain/include/OgreShadowCameraSetupFocused.h"),
    ("focused_shadow_runtime", "OgreMain/src/OgreShadowCameraSetupFocused.cpp"),
    (
        "stable_cascade_api",
        "OgreMain/include/OgreShadowCameraSetupConcentric.h",
    ),
    (
        "stable_cascade_runtime",
        "OgreMain/src/OgreShadowCameraSetupConcentric.cpp",
    ),
    ("movable_shadow_flag_api", "OgreMain/include/OgreMovableObject.inl"),
    ("movable_shadow_flag_masks", "OgreMain/src/OgreMovableObject.cpp"),
    ("pbs_shadow_filter_api", "Components/Hlms/Pbs/include/OgreHlmsPbs.h"),
    (
        "pbs_shadow_binding_runtime",
        "Components/Hlms/Pbs/src/OgreHlmsPbs.cpp",
    ),
    (
        "pbs_receive_shadow_api",
        "Components/Hlms/Pbs/include/OgreHlmsPbsDatablock.h",
    ),
    (
        "pbs_receive_shadow_runtime",
        "Components/Hlms/Pbs/src/OgreHlmsPbsDatablock.cpp",
    ),
    ("hlms_datablock_clone_api", "OgreMain/include/OgreHlmsDatablock.h"),
    ("hlms_datablock_clone_runtime", "OgreMain/src/OgreHlmsDatablock.cpp"),
    ("hlms_datablock_owner_api", "OgreMain/include/OgreHlms.h"),
    ("hlms_datablock_owner_runtime", "OgreMain/src/OgreHlms.cpp"),
    (
        "shared_shadow_math",
        "Samples/Media/Hlms/Pbs/Any/ShadowMapping_piece_all.any",
    ),
    (
        "shared_shadow_vertex_projection",
        "Samples/Media/Hlms/Pbs/Any/ShadowMapping_piece_vs.any",
    ),
    (
        "shared_shadow_pcf_sampling",
        "Samples/Media/Hlms/Pbs/Any/ShadowMapping_piece_ps.any",
    ),
    (
        "shared_shadow_constant_layout",
        "Samples/Media/Hlms/Pbs/Any/Main/500.Structs_piece_vs_piece_ps.any",
    ),
    (
        "shared_pbs_shadow_application",
        "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any",
    ),
    (
        "metal_shadow_entrypoint",
        "Samples/Media/Hlms/Pbs/Metal/PixelShader_ps.metal",
    ),
    (
        "d3d11_shadow_entrypoint",
        "Samples/Media/Hlms/Pbs/HLSL/PixelShader_ps.hlsl",
    ),
    (
        "vulkan_shadow_entrypoint",
        "Samples/Media/Hlms/Pbs/GLSL/PixelShader_ps.glsl",
    ),
]

_ROOT_KEYS = {
    "schema_version",
    "name",
    "canonical_dependency_lock",
    "ogre_next_commit",
    "platform_policies",
    "sources",
}
_SOURCE_KEYS = {"role", "path", "sha256"}
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")


class VerificationError(RuntimeError):
    """A checked source-closure property was not exact."""


def _reject(message: str) -> NoReturn:
    raise VerificationError(message)


def _require_exact_keys(value: object, keys: set[str], context: str) -> dict:
    if type(value) is not dict:
        _reject(f"{context} must be an object")
    observed = set(value)
    if observed != keys:
        _reject(
            f"{context} keys differ: missing={sorted(keys - observed)}, "
            f"extra={sorted(observed - keys)}"
        )
    return value


def _load_object(path: Path, context: str) -> dict:
    def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                _reject(f"{context} contains duplicate JSON object key {key!r}")
            result[key] = value
        return result

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise VerificationError(f"could not load {context}: {error}") from error
    if type(value) is not dict:
        _reject(f"{context} must be an object")
    return value


def validate_lock(lock_path: Path, canonical_lock_path: Path) -> dict:
    lock = _require_exact_keys(_load_object(lock_path, "PSSM lock"), _ROOT_KEYS, "PSSM lock")
    canonical = _load_object(canonical_lock_path, "canonical Ogre-Next lock")

    if type(lock["schema_version"]) is not int or lock["schema_version"] != SCHEMA_VERSION:
        _reject("PSSM lock schema_version is not exactly 1")
    if type(lock["name"]) is not str or not lock["name"]:
        _reject("PSSM lock name must be a nonempty string")
    if lock["canonical_dependency_lock"] != canonical_lock_path.name:
        _reject("PSSM lock does not name the canonical dependency lock")
    if canonical.get("commit") != OGRE_NEXT_COMMIT:
        _reject("canonical Ogre-Next dependency pin changed")
    if lock["ogre_next_commit"] != canonical.get("commit"):
        _reject("PSSM source closure pin differs from the canonical dependency pin")
    if lock["platform_policies"] != PLATFORM_POLICIES:
        _reject("PSSM platform policy list or ordering changed")

    sources = lock["sources"]
    if type(sources) is not list or len(sources) != len(SOURCE_ROLES_AND_PATHS):
        _reject(f"PSSM closure must contain exactly {len(SOURCE_ROLES_AND_PATHS)} sources")
    observed: list[tuple[str, str]] = []
    seen_paths: set[str] = set()
    for index, raw_record in enumerate(sources):
        record = _require_exact_keys(raw_record, _SOURCE_KEYS, f"sources[{index}]")
        role = record["role"]
        relative = record["path"]
        digest = record["sha256"]
        if type(role) is not str or type(relative) is not str or type(digest) is not str:
            _reject(f"sources[{index}] values must all be strings")
        pure_path = PurePosixPath(relative)
        if (
            pure_path.is_absolute()
            or not pure_path.parts
            or any(part in ("", ".", "..") for part in pure_path.parts)
            or "\\" in relative
        ):
            _reject(f"sources[{index}] has a noncanonical relative path")
        if relative in seen_paths:
            _reject(f"duplicate PSSM closure source path: {relative}")
        if _SHA256.fullmatch(digest) is None:
            _reject(f"sources[{index}] sha256 must be lowercase hexadecimal")
        seen_paths.add(relative)
        observed.append((role, relative))
    if observed != SOURCE_ROLES_AND_PATHS:
        _reject("PSSM source roles, paths, or stable ordering changed")
    return lock


def _require_tokens(root: Path, relative: str, tokens: tuple[str, ...]) -> None:
    try:
        text = (root / relative).read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise VerificationError(f"could not inspect pinned source {relative}: {error}") from error
    for token in tokens:
        if token not in text:
            _reject(f"pinned source behavior token {token!r} is absent from {relative}")


def verify_source_root(lock: dict, source_root: Path) -> None:
    try:
        canonical_root = source_root.resolve(strict=True)
    except OSError as error:
        raise VerificationError(f"Ogre-Next source root is unavailable: {error}") from error
    if not canonical_root.is_dir():
        _reject("Ogre-Next source root is not a directory")

    for index, record in enumerate(lock["sources"]):
        candidate = canonical_root / record["path"]
        try:
            resolved = candidate.resolve(strict=True)
        except OSError as error:
            raise VerificationError(
                f"PSSM closure source is missing: {record['path']}: {error}"
            ) from error
        if not resolved.is_file() or canonical_root not in resolved.parents:
            _reject(f"PSSM closure source is indirect or escapes its root: {record['path']}")
        digest = hashlib.sha256(resolved.read_bytes()).hexdigest()
        if digest != record["sha256"]:
            _reject(
                f"PSSM closure source digest mismatch at index {index}: {record['path']}"
            )

    # Hashes pin the bytes; these checks additionally state the behavior RoR
    # relies on so a deliberate future lock update cannot silently broaden it.
    _require_tokens(
        canonical_root,
        "OgreMain/src/Compositor/OgreCompositorShadowNode.cpp",
        (
            "createShadowNodeWithSettings",
            "calculateSplitPoints",
            "camera->getNearClipDistance()",
            "light->getShadowFarDistance()",
        ),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/Compositor/OgreCompositorShadowNodeDef.cpp",
        ("VisibilityFlags::LAYER_SHADOW_CASTER", "mIncludeOverlays = false"),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/Compositor/OgreCompositorWorkspace.cpp",
        ("SHADOW_NODE_FIRST_ONLY",),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/include/OgreFrustum.h",
        (
            "FET_TAN_HALF_ANGLES",
            "setFrustumExtents",
            "getFrustumExtents",
            "setCustomProjectionMatrix",
        ),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/OgreFrustum.cpp",
        (
            "mFrustrumExtentsType == FET_TAN_HALF_ANGLES",
            "Frustum::setCustomProjectionMatrix",
            "Frustum::setFrustumExtents",
        ),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/Compositor/OgreCompositorManager2.cpp",
        (
            "CompositorManager2::addNodeDefinition",
            "CompositorManager2::removeNodeDefinition",
        ),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/OgreShadowCameraSetupPSSM.cpp",
        ("PSSMShadowCameraSetup::calculateSplitPoints", "Math::Pow"),
    )
    _require_tokens(
        canonical_root,
        "Components/Hlms/Pbs/include/OgreHlmsPbs.h",
        ("PCF_4x4",),
    )
    _require_tokens(
        canonical_root,
        "Components/Hlms/Pbs/include/OgreHlmsPbsDatablock.h",
        ("setReceiveShadows", "getReceiveShadows"),
    )
    _require_tokens(
        canonical_root,
        "Components/Hlms/Pbs/src/OgreHlmsPbsDatablock.cpp",
        ("HlmsPbsDatablock::setReceiveShadows", "mReceiveShadows"),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/OgreHlmsDatablock.cpp",
        ("HlmsDatablock::clone", "cloneImpl( datablock )"),
    )
    _require_tokens(
        canonical_root,
        "OgreMain/src/OgreHlms.cpp",
        ("Hlms::destroyDatablock",),
    )
    _require_tokens(
        canonical_root,
        "Samples/Media/Hlms/Pbs/Any/ShadowMapping_piece_ps.any",
        ("PCF", "SampleCmp"),
    )
    _require_tokens(
        canonical_root,
        "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any",
        ("DarkenWithShadow", "hlms_num_shadow_map_lights"),
    )
    for relative in (
        "Samples/Media/Hlms/Pbs/Metal/PixelShader_ps.metal",
        "Samples/Media/Hlms/Pbs/HLSL/PixelShader_ps.hlsl",
        "Samples/Media/Hlms/Pbs/GLSL/PixelShader_ps.glsl",
    ):
        _require_tokens(canonical_root, relative, ("hlms_shadowcaster",))


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    script_root = Path(__file__).resolve().parent
    parser.add_argument(
        "--lock",
        type=Path,
        default=script_root / "ogre-next-pssm-shadow-v1.lock.json",
    )
    parser.add_argument(
        "--canonical-lock", type=Path, default=script_root / "ogre-next.lock.json"
    )
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--contract-only", action="store_true")
    args = parser.parse_args(argv)
    if args.contract_only == (args.source_root is not None):
        parser.error("choose exactly one of --contract-only or --source-root")
    return args


def main(argv: list[str]) -> int:
    try:
        args = parse_arguments(argv)
        lock = validate_lock(args.lock, args.canonical_lock)
        if args.source_root is not None:
            verify_source_root(lock, args.source_root)
        print(
            json.dumps(
                {
                    "status": "pass",
                    "ogre_next_commit": lock["ogre_next_commit"],
                    "source_count": len(lock["sources"]),
                    "source_bytes_verified": args.source_root is not None,
                },
                sort_keys=True,
            )
        )
        return 0
    except VerificationError as error:
        print(f"Ogre-Next PSSM source verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
