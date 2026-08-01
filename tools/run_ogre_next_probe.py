#!/usr/bin/env python3
"""Build and validate the isolated, pinned OGRE-Next capability probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCE = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_SOURCE / "ogre-next.lock.json"
REPORT_NAME = "ror-ogre-next-probe-report.json"


class ProbeError(RuntimeError):
    """Raised when a capability or provenance contract fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ProbeError(f"{label} is not a lowercase SHA-256")


def load_lock(path: Path = LOCK_PATH) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read OGRE-Next lock: {error}") from error

    expected_commit = "37149a802de747f6806996fa3067b0748ecc1084"
    if lock.get("schema_version") != 1:
        raise ProbeError("unsupported OGRE-Next lock schema")
    if lock.get("repository") != "https://github.com/OGRECave/ogre-next":
        raise ProbeError("OGRE-Next repository contract changed")
    if lock.get("branch") != "v3-0":
        raise ProbeError("OGRE-Next branch contract changed")
    if lock.get("commit") != expected_commit:
        raise ProbeError("OGRE-Next lock moved without an integration review")
    if lock.get("license", {}).get("spdx") != "MIT":
        raise ProbeError("OGRE-Next license contract changed")
    _require_sha256(lock.get("archive_sha256"), "OGRE-Next archive hash")
    _require_sha256(
        lock.get("license", {}).get("sha256"), "OGRE-Next license hash"
    )

    rapidjson = lock.get("dependencies", {}).get("rapidjson", {})
    if rapidjson.get("tag") != "v1.1.0" or rapidjson.get("license_spdx") != "MIT":
        raise ProbeError("RapidJSON dependency contract changed")
    _require_sha256(rapidjson.get("archive_sha256"), "RapidJSON archive hash")
    _require_sha256(rapidjson.get("license_sha256"), "RapidJSON license hash")

    patches = lock.get("patches")
    if not isinstance(patches, list) or len(patches) != 1:
        raise ProbeError("the reviewed OGRE-Next adaptation patch set changed")
    if patches[0].get("path") != (
        "patches/0001-macos-non-xcode-framework-path.patch"
    ):
        raise ProbeError("the reviewed OGRE-Next adaptation patch path changed")
    for patch in patches:
        _require_sha256(patch.get("sha256"), "adaptation patch hash")
        patch_path = path.parent / patch["path"]
        if not patch_path.is_file():
            raise ProbeError(f"pinned patch is missing: {patch_path}")
        actual_hash = sha256_file(patch_path)
        if actual_hash != patch.get("sha256"):
            raise ProbeError(
                f"pinned patch SHA-256 mismatch for {patch_path.name}: "
                f"expected {patch.get('sha256')}, got {actual_hash}"
            )
    expected_abi = {
        "cxx_standard": 17,
        "static_link": True,
        "new_project_name": True,
        "debug_level_debug": 3,
        "debug_level_release": 0,
        "embed_debug_mode": "auto",
        "assert_mode": 0,
        "double_precision": False,
        "allocator": 0,
        "container_custom_allocator": False,
        "string_custom_allocator": False,
        "memory_tracker_debug": False,
        "memory_tracker_release": False,
        "thread_support": 0,
        "thread_provider": "none",
        "id_string_128": False,
        "id_string_always_readable": False,
        "node_inherit_transform": False,
        "restrict_aliasing": True,
        "flexibility_level": 0,
        "planar_reflections": False,
        "simd": {
            "enabled": True,
            "alignment": 16,
            "macos-arm64-metal": "neon",
            "windows-x64-d3d11": "sse2",
            "linux-x86_64-vulkan": "sse2",
        },
    }
    if lock.get("abi_contract") != expected_abi:
        raise ProbeError("OGRE-Next ABI contract changed without review")
    return lock


def detect_policy(system: str, machine: str) -> dict[str, str]:
    normalized_system = system.strip().lower()
    normalized_machine = machine.strip().lower()
    if normalized_system == "darwin" and normalized_machine in {
        "arm64",
        "aarch64",
    }:
        return {
            "name": "macos-arm64-metal",
            "renderer_target": "RenderSystem_Metal",
            "renderer_name": "Metal Rendering Subsystem",
            "shader_data_path": "Hlms/Pbs/Metal",
        }
    if normalized_system == "windows" and normalized_machine in {
        "amd64",
        "x86_64",
    }:
        return {
            "name": "windows-x64-d3d11",
            "renderer_target": "RenderSystem_Direct3D11",
            "renderer_name": "Direct3D11 Rendering Subsystem",
            "shader_data_path": "Hlms/Pbs/HLSL",
        }
    if normalized_system == "linux" and normalized_machine in {
        "amd64",
        "x86_64",
    }:
        return {
            "name": "linux-x86_64-vulkan",
            "renderer_target": "RenderSystem_Vulkan",
            "renderer_name": "Vulkan Rendering Subsystem",
            "shader_data_path": "Hlms/Pbs/GLSL",
        }
    raise ProbeError(f"no reviewed OGRE-Next policy for {system}/{machine}")


def verify_archive(path: Path, expected_sha256: str, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ProbeError(f"{label} archive does not exist: {resolved}")
    actual_sha256 = sha256_file(resolved)
    if actual_sha256 != expected_sha256:
        raise ProbeError(
            f"{label} archive SHA-256 mismatch: expected {expected_sha256}, "
            f"got {actual_sha256}"
        )
    return resolved


def validate_report(
    report: dict[str, Any], lock: dict[str, Any], policy: dict[str, str]
) -> None:
    provenance = report.get("provenance", {})
    build = report.get("build", {})
    capabilities = report.get("capabilities", {})
    renderer = capabilities.get("renderer", {})
    pbs = capabilities.get("hlms_pbs", {})
    compositor = capabilities.get("compositor2", {})
    rapidjson = lock["dependencies"]["rapidjson"]
    abi = lock["abi_contract"]

    checks = {
        "schema_version": report.get("schema_version") == 1,
        "status": report.get("status") == "pass",
        "commit": provenance.get("commit") == lock["commit"],
        "archive_sha256": provenance.get("archive_sha256")
        == lock["archive_sha256"],
        "license": provenance.get("license_spdx")
        == lock["license"]["spdx"],
        "license_sha256": provenance.get("license_sha256")
        == lock["license"]["sha256"],
        "rapidjson": provenance.get("rapidjson_archive_sha256")
        == rapidjson["archive_sha256"],
        "rapidjson_tag": provenance.get("rapidjson_tag") == rapidjson["tag"],
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "platform_policy": build.get("platform_policy") == policy["name"],
        "cxx_standard": build.get("cxx_standard") == 17,
        "pointer_bits": build.get("pointer_bits") == 64,
        "static_link": build.get("static_link") is True,
        "abi_cookie": isinstance(build.get("abi_cookie"), str)
        and re.fullmatch(r"[0-9a-f]{32}", build["abi_cookie"]) is not None,
        "debug_mode": build.get("debug_mode")
        in {abi["debug_level_debug"], abi["debug_level_release"]},
        "double_precision": build.get("double_precision")
        is abi["double_precision"],
        "memory_allocator": build.get("memory_allocator") == abi["allocator"],
        "container_custom_allocator": build.get("container_custom_allocator")
        is abi["container_custom_allocator"],
        "string_custom_allocator": build.get("string_custom_allocator")
        is abi["string_custom_allocator"],
        "thread_support": build.get("thread_support") == abi["thread_support"],
        "thread_provider": build.get("thread_provider") == 0,
        "id_string_bits": build.get("id_string_bits") == 32,
        "id_string_size": build.get("id_string_size") == 4,
        "flexibility_level": build.get("flexibility_level")
        == abi["flexibility_level"],
        "simd_alignment": build.get("simd_alignment")
        == abi["simd"]["alignment"],
        "use_simd": build.get("use_simd") == int(abi["simd"]["enabled"]),
        "restrict_aliasing": build.get("restrict_aliasing")
        == int(abi["restrict_aliasing"]),
        "assert_mode": build.get("assert_mode") == abi["assert_mode"],
        "renderer_target": renderer.get("target")
        == policy["renderer_target"],
        "renderer_name": renderer.get("name") == policy["renderer_name"],
        "renderer_registered": renderer.get("registered") is True,
        "renderer_count": renderer.get("registered_renderer_count") == 1,
        "renderer_options": isinstance(
            renderer.get("configuration_option_count"), int
        )
        and renderer["configuration_option_count"] > 0,
        "pbs_linked": pbs.get("compiled_and_linked") is True,
        "pbs_path": pbs.get("shader_data_path") == policy["shader_data_path"],
        "pbs_policy": pbs.get("shader_path_matches_policy") is True,
        "pbs_libraries": isinstance(pbs.get("library_path_count"), int)
        and pbs["library_path_count"] >= 4,
        "compositor_linked": compositor.get("compiled_and_linked") is True,
        "compositor_deferred": compositor.get("runtime_initialization")
        == "deferred_until_real_window",
        "compositor_deferred_observed": compositor.get(
            "deferred_contract_observed"
        )
        is True,
        "ray_tracing_scope": capabilities.get("native_ray_tracing")
        == "not_evaluated",
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next report failed closed: " + ", ".join(sorted(failed))
        )


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    try:
        subprocess.run(command, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProbeError(f"command failed: {command[0]}: {error}") from error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=REPOSITORY_ROOT / "build-ogre-next-probe",
        help="standalone CMake build directory",
    )
    parser.add_argument(
        "--ogre-archive",
        type=Path,
        help="optional already-downloaded pinned OGRE-Next archive",
    )
    parser.add_argument(
        "--rapidjson-archive",
        type=Path,
        help="optional already-downloaded pinned RapidJSON archive",
    )
    parser.add_argument("--generator", help="optional CMake generator")
    parser.add_argument("--config", default="Release")
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(os.cpu_count() or 1, 8))
    )
    parser.add_argument(
        "--validate-contract-only",
        action="store_true",
        help="validate checked-in pins and platform policy without network/build",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.jobs <= 0:
            raise ProbeError("--jobs must be positive")
        lock = load_lock()
        policy = detect_policy(platform.system(), platform.machine())
        if args.validate_contract_only:
            print(
                json.dumps(
                    {
                        "schema_version": 1,
                        "status": "pass",
                        "commit": lock["commit"],
                        "platform_policy": policy["name"],
                        "network_used": False,
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0

        build_dir = args.build_dir.expanduser().resolve()
        configure = [
            "cmake",
            "-S",
            str(PROBE_SOURCE),
            "-B",
            str(build_dir),
            "-DROR_OGRE_NEXT_PROBE=ON",
            f"-DCMAKE_BUILD_TYPE={args.config}",
        ]
        if args.generator:
            configure.extend(["-G", args.generator])
        elif shutil.which("ninja"):
            configure.extend(["-G", "Ninja"])

        if policy["name"] == "macos-arm64-metal":
            configure.extend(
                [
                    "-DCMAKE_OSX_ARCHITECTURES=arm64",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
                ]
            )
        if args.ogre_archive:
            archive = verify_archive(
                args.ogre_archive, lock["archive_sha256"], "OGRE-Next"
            )
            configure.append(f"-DROR_OGRE_NEXT_ARCHIVE={archive}")
        if args.rapidjson_archive:
            rapidjson_lock = lock["dependencies"]["rapidjson"]
            archive = verify_archive(
                args.rapidjson_archive,
                rapidjson_lock["archive_sha256"],
                "RapidJSON",
            )
            configure.append(f"-DROR_RAPIDJSON_ARCHIVE={archive}")

        run(configure)
        run(
            [
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                "ror_ogre_next_probe_report",
                "--config",
                args.config,
                "--parallel",
                str(args.jobs),
            ]
        )

        report_path = build_dir / REPORT_NAME
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ProbeError(f"could not read probe report: {error}") from error
        validate_report(report, lock, policy)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ProbeError as error:
        print(f"OGRE-Next probe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
