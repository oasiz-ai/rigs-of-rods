#!/usr/bin/env python3
"""Stage and verify the relocatable, real RoR-OgreNext product child.

The standalone probe intentionally builds several executables.  This tool is
the narrow product boundary: only a child carrying the generated production
identity marker can enter the package, and only the authenticated N1 media,
presentation media, notices, and provenance are copied with it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
from typing import Any, Iterable


MANIFEST_SCHEMA = "ror.ogre_next.product_package.v1"
COMPLETION_SCHEMA = "ror.ogre_next.product_package_completion.v1"
PRODUCTION_IDENTITY_PREFIX = b"ror-ogre-next-production-child-v1|"
MANIFEST_RELATIVE = PurePosixPath(
    "provenance/ogre-next-product-package.manifest.json"
)
BUILD_CONTRACT_RELATIVE = PurePosixPath(
    "provenance/ogre-next-product-build-contract.json"
)
COMPLETION_RELATIVE = PurePosixPath(".ror-ogre-next-product-complete.json")
N1_MEDIA_RELATIVE = PurePosixPath(
    "share/rigsofrods/ogre-next/Samples/Media"
)
BASE_NOTICES = frozenset(
    {
        "FreeType-GPLv2.txt",
        "FreeType-LICENSE.txt",
        "IBLBaker.txt",
        "LicenseRef-Heitz-LTC-Paper-Notice.txt",
        "Ogre-Next-MIT.txt",
        "RapidJSON-license.txt",
        "Rigs-of-Rods-GPL-3.0.txt",
    }
)
POLICY_CHILD_NAMES = {
    "macos-arm64-metal": "RoR-OgreNext",
    "linux-x86_64-vulkan": "RoR-OgreNext",
    "windows-x64-d3d11": "RoR-OgreNext.exe",
}


class PackageError(RuntimeError):
    """Raised when an input or staged package violates the product contract."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _canonical_json(payload: Any) -> bytes:
    return (
        json.dumps(payload, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")


def _load_json(path: Path, description: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError(f"invalid {description}: {path}: {error}") from error


def _assert_regular(path: Path, description: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"{description} must be one regular file: {path}")


def _assert_directory(path: Path, description: str) -> None:
    if path.is_symlink() or not path.is_dir():
        raise PackageError(f"{description} must be one real directory: {path}")


def _relative_files(root: Path) -> tuple[PurePosixPath, ...]:
    _assert_directory(root, "closure root")
    result: list[PurePosixPath] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path.is_symlink():
            raise PackageError(f"symbolic links are prohibited: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise PackageError(f"irregular package entry is prohibited: {path}")
        result.append(PurePosixPath(path.relative_to(root).as_posix()))
    return tuple(result)


def _copy_tree(source: Path, destination: Path, description: str) -> None:
    files = _relative_files(source)
    if not files:
        raise PackageError(f"{description} closure is empty: {source}")
    shutil.copytree(source, destination, copy_function=shutil.copy2)
    copied = _relative_files(destination)
    if copied != files:
        raise PackageError(f"{description} file set changed while copying")
    for relative in files:
        if _sha256(source / relative) != _sha256(destination / relative):
            raise PackageError(
                f"{description} bytes changed while copying: {relative}"
            )


def _safe_manifest_relative(value: Any) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise PackageError("manifest path must be a non-empty string")
    relative = PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts or "." in relative.parts:
        raise PackageError(f"unsafe manifest path: {value!r}")
    if relative.as_posix() != value or "\\" in value:
        raise PackageError(f"non-canonical manifest path: {value!r}")
    return relative


def _expected_identity(identity: str) -> bytes:
    try:
        encoded = identity.encode("ascii")
    except UnicodeEncodeError as error:
        raise PackageError("product identity must be ASCII") from error
    if not encoded.startswith(PRODUCTION_IDENTITY_PREFIX):
        raise PackageError("product identity has an unreviewed schema")
    if b"probe" in encoded.lower() or b"\x00" in encoded:
        raise PackageError("product identity must not describe a probe binary")
    return encoded


def _assert_identity(child: Path, identity: str) -> None:
    _assert_regular(child, "OgreNext product child")
    expected = _expected_identity(identity)
    try:
        payload = child.read_bytes()
    except OSError as error:
        raise PackageError(f"cannot inspect product child: {error}") from error
    if expected not in payload:
        raise PackageError(
            "child does not carry the exact generated production identity; "
            "probe/smoke substitution is prohibited"
        )
    if payload.count(expected) != 1:
        raise PackageError("child production identity is not unique")


def _product_build_contract(
    source: Path,
    *,
    policy: str,
    identity: str,
    forbidden_prefixes: Iterable[Path],
) -> dict[str, Any]:
    contract = _load_json(source, "OgreNext build contract")
    if not isinstance(contract, dict) or contract.get("schema_version") != 6:
        raise PackageError("OgreNext build contract schema changed")
    platform = contract.get("platform")
    components = contract.get("components")
    if not isinstance(platform, dict) or platform.get("policy") != policy:
        raise PackageError("OgreNext build contract platform policy mismatch")
    if not isinstance(components, dict):
        raise PackageError("OgreNext build contract components are unavailable")
    required_components = {
        "headless_child_bootstrap": True,
        "headless_child_output_name": "RoR-OgreNext",
        "headless_child_packaged": True,
        "headless_child_production_admitted": False,
    }
    for key, expected in required_components.items():
        if components.get(key) != expected:
            raise PackageError(
                f"OgreNext product build contract has {key}={components.get(key)!r}, "
                f"expected {expected!r}"
            )

    # The probe contract records its source media root for diagnostics.  A
    # product artifact records the relocatable package path instead and never
    # leaks the build/worktree path into its provenance.
    shader_media = contract.get("shader_media")
    if not isinstance(shader_media, dict):
        raise PackageError("OgreNext shader-media provenance is unavailable")
    shader_media["root"] = "resources/ogrenext/Hlms"
    contract["product_package"] = {
        "schema": MANIFEST_SCHEMA,
        "identity": identity,
        "child": POLICY_CHILD_NAMES[policy],
        "shader_media": "resources/ogrenext/Hlms",
        "presentation_media": "resources/ogrenext/Presentation",
        "production_admitted": False,
    }
    serialized = _canonical_json(contract).decode("utf-8")
    for prefix in forbidden_prefixes:
        resolved = str(prefix.resolve())
        if resolved and resolved in serialized:
            raise PackageError(
                "relocatable product provenance contains a build/source path: "
                + resolved
            )
    return contract


def _manifest_entries(root: Path) -> list[dict[str, Any]]:
    excluded = {MANIFEST_RELATIVE, COMPLETION_RELATIVE}
    entries: list[dict[str, Any]] = []
    for relative in _relative_files(root):
        if relative in excluded:
            continue
        path = root / relative
        entries.append(
            {
                "path": relative.as_posix(),
                "sha256": _sha256(path),
                "size": path.stat().st_size,
            }
        )
    return entries


def verify_package(
    root: Path,
    *,
    expected_policy: str | None = None,
    expected_identity: str | None = None,
    strict_root: bool = False,
) -> dict[str, Any]:
    _assert_directory(root, "OgreNext product package")
    manifest_path = root / MANIFEST_RELATIVE
    completion_path = root / COMPLETION_RELATIVE
    _assert_regular(manifest_path, "product manifest")
    _assert_regular(completion_path, "product completion marker")
    manifest = _load_json(manifest_path, "product manifest")
    completion = _load_json(completion_path, "product completion marker")
    if not isinstance(manifest, dict) or manifest.get("schema") != MANIFEST_SCHEMA:
        raise PackageError("product manifest schema changed")
    if (
        not isinstance(completion, dict)
        or completion.get("schema") != COMPLETION_SCHEMA
        or completion.get("manifest") != MANIFEST_RELATIVE.as_posix()
        or completion.get("manifest_sha256") != _sha256(manifest_path)
    ):
        raise PackageError("product completion marker does not seal the manifest")

    policy = manifest.get("platform_policy")
    identity = manifest.get("identity")
    if policy not in POLICY_CHILD_NAMES or not isinstance(identity, str):
        raise PackageError("product manifest identity/platform is invalid")
    if expected_policy is not None and policy != expected_policy:
        raise PackageError(
            f"product platform mismatch: observed {policy!r}, expected {expected_policy!r}"
        )
    if expected_identity is not None and identity != expected_identity:
        raise PackageError("product identity differs from the requested build")
    if completion.get("identity") != identity:
        raise PackageError("completion marker identity differs from manifest")

    child_relative = PurePosixPath(POLICY_CHILD_NAMES[policy])
    if manifest.get("child") != child_relative.as_posix():
        raise PackageError("product manifest child path is not canonical")
    child = root / child_relative
    _assert_identity(child, identity)

    entries = manifest.get("files")
    if not isinstance(entries, list) or not entries:
        raise PackageError("product manifest file list is empty")
    observed_relatives: list[PurePosixPath] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise PackageError("product manifest contains a non-object entry")
        relative = _safe_manifest_relative(entry.get("path"))
        if relative in (MANIFEST_RELATIVE, COMPLETION_RELATIVE):
            raise PackageError("manifest cannot recursively authenticate itself")
        path = root / relative
        _assert_regular(path, f"manifest payload {relative}")
        if (
            not isinstance(entry.get("size"), int)
            or entry["size"] < 0
            or path.stat().st_size != entry["size"]
            or not isinstance(entry.get("sha256"), str)
            or len(entry["sha256"]) != 64
            or _sha256(path) != entry["sha256"]
        ):
            raise PackageError(f"manifest payload changed: {relative}")
        observed_relatives.append(relative)
    if observed_relatives != sorted(set(observed_relatives)):
        raise PackageError("product manifest paths are duplicated or unsorted")
    required_files = {
        child_relative,
        BUILD_CONTRACT_RELATIVE,
    }
    required_directories = {
        PurePosixPath("resources/ogrenext/Hlms/Hlms"),
        PurePosixPath("resources/ogrenext/Presentation/CommonCopy"),
    }
    observed_set = set(observed_relatives)
    for required_path in required_files:
        if required_path not in observed_set:
            raise PackageError(f"required product payload is absent: {required_path}")
    for required_path in required_directories:
        if not (root / required_path).is_dir():
            raise PackageError(f"required product directory is absent: {required_path}")
    notice_names = {
        path.name
        for path in (root / "licenses").iterdir()
        if path.is_file() and not path.is_symlink()
    }
    missing_notices = BASE_NOTICES - notice_names
    if missing_notices:
        raise PackageError(
            "product package is missing notices: " + ", ".join(sorted(missing_notices))
        )

    if strict_root:
        actual = set(_relative_files(root))
        expected = observed_set | {MANIFEST_RELATIVE, COMPLETION_RELATIVE}
        if actual != expected:
            unexpected = sorted(str(path) for path in actual - expected)
            missing = sorted(str(path) for path in expected - actual)
            raise PackageError(
                f"strict product file set differs: unexpected={unexpected}, missing={missing}"
            )
    for forbidden in (
        "ror_ogre_next_frontend_n1_smoke",
        "ror_ogre_next_frame_probe",
        "ror_ogre_next_pssm_shadow_smoke",
    ):
        if (root / forbidden).exists() or (root / f"{forbidden}.exe").exists():
            raise PackageError(f"probe/smoke executable entered product root: {forbidden}")
    return manifest


def stage_package(
    *,
    child: Path,
    n1_package: Path,
    presentation_root: Path,
    build_contract: Path,
    output: Path,
    identity: str,
    policy: str,
) -> dict[str, Any]:
    if policy not in POLICY_CHILD_NAMES:
        raise PackageError(f"unsupported product platform policy: {policy}")
    if not output.is_absolute() or output == Path(output.anchor):
        raise PackageError("product output must be one safe absolute directory")
    output_parent = output.parent.resolve(strict=True)
    output = output_parent / output.name
    if output.is_symlink():
        raise PackageError("product output must not be a symbolic link")
    expected_child_name = POLICY_CHILD_NAMES[policy]
    if child.name != expected_child_name:
        raise PackageError(
            f"product child must be named {expected_child_name}, got {child.name}"
        )
    _assert_identity(child, identity)
    _assert_directory(n1_package, "N1 authenticated package")
    _assert_regular(n1_package / ".stage-v10", "N1 completion stamp")
    _assert_directory(presentation_root, "presentation media")
    _assert_regular(build_contract, "OgreNext build contract")
    media_root = n1_package / N1_MEDIA_RELATIVE
    licenses_root = n1_package / "licenses"
    _assert_directory(media_root, "authenticated N1 media")
    _assert_directory(licenses_root, "authenticated N1 notices")

    source_roots = (
        child.resolve(),
        n1_package.resolve(),
        presentation_root.resolve(),
        build_contract.resolve(),
    )
    output_resolved = output.resolve(strict=False)
    for source in source_roots:
        if (
            output_resolved == source
            or output_resolved in source.parents
            or source in output_resolved.parents
        ):
            raise PackageError("product output overlaps an authenticated input")

    temporary = output_parent / f".{output.name}.tmp-{os.getpid()}"
    backup = output_parent / f".{output.name}.previous-{os.getpid()}"
    for scratch in (temporary, backup):
        if scratch.exists() or scratch.is_symlink():
            raise PackageError(f"refusing pre-existing package scratch path: {scratch}")
    temporary.mkdir(mode=0o755)
    try:
        shutil.copy2(child, temporary / expected_child_name)
        child_mode = (temporary / expected_child_name).stat().st_mode
        (temporary / expected_child_name).chmod(
            child_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        )
        _copy_tree(
            media_root,
            temporary / "resources" / "ogrenext" / "Hlms",
            "OgreNext shader/media",
        )
        _copy_tree(
            presentation_root,
            temporary / "resources" / "ogrenext" / "Presentation",
            "OgreNext presentation media",
        )
        _copy_tree(licenses_root, temporary / "licenses", "license/notice")
        n1_provenance = n1_package / "provenance"
        if n1_provenance.is_dir() and any(n1_provenance.iterdir()):
            _copy_tree(
                n1_provenance,
                temporary / "provenance" / "platform",
                "platform provenance",
            )
        (temporary / "provenance").mkdir(parents=True, exist_ok=True)
        product_contract = _product_build_contract(
            build_contract,
            policy=policy,
            identity=identity,
            forbidden_prefixes=source_roots,
        )
        (temporary / BUILD_CONTRACT_RELATIVE).write_bytes(
            _canonical_json(product_contract)
        )
        entries = _manifest_entries(temporary)
        manifest = {
            "schema": MANIFEST_SCHEMA,
            "platform_policy": policy,
            "identity": identity,
            "child": expected_child_name,
            "production_admitted": False,
            "files": entries,
        }
        manifest_path = temporary / MANIFEST_RELATIVE
        manifest_path.write_bytes(_canonical_json(manifest))
        completion = {
            "schema": COMPLETION_SCHEMA,
            "identity": identity,
            "manifest": MANIFEST_RELATIVE.as_posix(),
            "manifest_sha256": _sha256(manifest_path),
        }
        # The completion record is deliberately the final write in the staged
        # directory. Consumers reject a missing or non-sealing record.
        (temporary / COMPLETION_RELATIVE).write_bytes(
            _canonical_json(completion)
        )
        verify_package(
            temporary,
            expected_policy=policy,
            expected_identity=identity,
            strict_root=True,
        )

        if output.exists():
            verify_package(output, strict_root=True)
            os.replace(output, backup)
        try:
            os.replace(temporary, output)
        except BaseException:
            if backup.exists() and not output.exists():
                os.replace(backup, output)
            raise
        if backup.exists():
            shutil.rmtree(backup)
        return verify_package(
            output,
            expected_policy=policy,
            expected_identity=identity,
            strict_root=True,
        )
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    stage = subparsers.add_parser("stage")
    stage.add_argument("--child", type=Path, required=True)
    stage.add_argument("--n1-package", type=Path, required=True)
    stage.add_argument("--presentation-root", type=Path, required=True)
    stage.add_argument("--build-contract", type=Path, required=True)
    stage.add_argument("--output", type=Path, required=True)
    stage.add_argument("--identity", required=True)
    stage.add_argument("--platform-policy", choices=sorted(POLICY_CHILD_NAMES), required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--root", type=Path, required=True)
    verify.add_argument("--platform-policy", choices=sorted(POLICY_CHILD_NAMES))
    verify.add_argument("--identity")
    verify.add_argument("--strict-root", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "stage":
            manifest = stage_package(
                child=arguments.child,
                n1_package=arguments.n1_package,
                presentation_root=arguments.presentation_root,
                build_contract=arguments.build_contract,
                output=arguments.output,
                identity=arguments.identity,
                policy=arguments.platform_policy,
            )
        else:
            manifest = verify_package(
                arguments.root,
                expected_policy=arguments.platform_policy,
                expected_identity=arguments.identity,
                strict_root=arguments.strict_root,
            )
    except PackageError as error:
        print(f"OgreNext product package rejected: {error}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "valid": True,
                "schema": manifest["schema"],
                "platform_policy": manifest["platform_policy"],
                "identity": manifest["identity"],
                "file_count": len(manifest["files"]),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
