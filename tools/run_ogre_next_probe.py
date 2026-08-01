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
import tempfile
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCE = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
LOCK_PATH = PROBE_SOURCE / "ogre-next.lock.json"
REPORT_NAME = "ror-ogre-next-probe-report.json"
BUILD_CONTRACT_NAME = "ogre-next-build-contract.json"
FRAME_REPORT_NAME = "ror-ogre-next-frame-probe-report.json"
FRAME_IMAGE_NAME = "ror-ogre-next-frame-probe.ppm"
N1_REPORT_NAME = "ror-ogre-next-frontend-n1-report.json"
N1_IMAGE_NAME = "ror-ogre-next-frontend-n1.ppm"
N1_PACKAGE_NAME = "ror-ogre-next-n1-package"
FRAME_VALIDATOR = REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py"
BUILD_SENTINEL_NAME = ".ror-ogre-next-probe-build-v1"
BUILD_SENTINEL_CONTENT = "ror-ogre-next-probe-build-v1\n"
REQUIRED_CONFIG = "Release"
ROR_SOURCE_REPOSITORY = "https://github.com/oasiz-ai/rigs-of-rods"


class ProbeError(RuntimeError):
    """Raised when a capability or provenance contract fails closed."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relevant_source_manifest() -> dict[str, Any]:
    roots = (
        REPOSITORY_ROOT / "source" / "main" / "gfx" / "render",
        PROBE_SOURCE,
    )
    paths: set[Path] = set()
    for root in roots:
        paths.update(root.rglob("*"))
    paths.update(
        {
            REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools" / "validate_ogre_next_frame_probe.py",
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py",
        }
    )
    entries: list[tuple[str, int, str]] = []
    for path in sorted(paths, key=lambda item: item.as_posix()):
        relative = path.relative_to(REPOSITORY_ROOT)
        if "__pycache__" in relative.parts or path.suffix in (".pyc", ".pyo"):
            continue
        if path.name == ".DS_Store":
            continue
        if path.is_symlink():
            raise ProbeError(
                "RoR relevant source contains a symbolic link: "
                + relative.as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ProbeError(
                "RoR relevant source is missing or irregular: "
                + relative.as_posix()
            )
        entries.append(
            (relative.as_posix(), path.stat().st_size, sha256_file(path))
        )
    if not entries:
        raise ProbeError("RoR relevant source manifest is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
    }


def ror_source_identity() -> dict[str, Any]:
    def git_output(*arguments: str) -> str:
        try:
            result = subprocess.run(
                ["git", "-C", str(REPOSITORY_ROOT), *arguments],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except OSError as error:
            raise ProbeError(f"could not execute Git for RoR provenance: {error}") from error
        value = result.stdout.strip()
        if result.returncode != 0 or not value:
            raise ProbeError("could not resolve RoR Git provenance")
        return value

    commit = git_output("rev-parse", "HEAD")
    ref = git_output("rev-parse", "--abbrev-ref", "HEAD")
    if re.fullmatch(r"[0-9a-f]{40}", commit) is None or re.fullmatch(
        r"[A-Za-z0-9._/-]+", ref
    ) is None:
        raise ProbeError("RoR Git provenance is not canonical")
    manifest = relevant_source_manifest()
    return {
        "repository": ROR_SOURCE_REPOSITORY,
        "ref": ref,
        "commit": commit,
        "relevant_manifest_sha256": manifest["sha256"],
        "relevant_manifest_file_count": manifest["file_count"],
    }


def require_source_identity_unchanged(
    expected: dict[str, Any],
) -> None:
    if ror_source_identity() != expected:
        raise ProbeError(
            "RoR relevant source or Git identity changed during the probe"
        )


def _require_sha256(value: object, label: str) -> None:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ProbeError(f"{label} is not a lowercase SHA-256")


def load_lock(path: Path = LOCK_PATH) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read OGRE-Next lock: {error}") from error

    expected_commit = "37149a802de747f6806996fa3067b0748ecc1084"
    expected_archive_sha256 = (
        "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2"
    )
    expected_ogre_license_sha256 = (
        "df6294031f26c4401ce713be0b0b3c5da27c2f1b7278a0d9833d111273174183"
    )
    expected_rapidjson_archive_sha256 = (
        "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e"
    )
    expected_rapidjson_license_sha256 = (
        "a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32"
    )
    expected_patch_sha256 = (
        "84916d0d1abf61a15d19d2c89a7d9b1a445f1a37a5067a9f8b558395fe10ead1"
    )
    if lock.get("schema_version") != 2:
        raise ProbeError("unsupported OGRE-Next lock schema")
    if lock.get("repository") != "https://github.com/OGRECave/ogre-next":
        raise ProbeError("OGRE-Next repository contract changed")
    if lock.get("branch") != "v3-0":
        raise ProbeError("OGRE-Next branch contract changed")
    if lock.get("commit") != expected_commit:
        raise ProbeError("OGRE-Next lock moved without an integration review")
    if lock.get("archive_url") != (
        f"https://github.com/OGRECave/ogre-next/archive/{expected_commit}.tar.gz"
    ):
        raise ProbeError("OGRE-Next archive URL contract changed")
    if lock.get("archive_sha256") != expected_archive_sha256:
        raise ProbeError("OGRE-Next archive hash moved without review")
    if lock.get("license", {}).get("spdx") != "MIT":
        raise ProbeError("OGRE-Next license contract changed")
    _require_sha256(lock.get("archive_sha256"), "OGRE-Next archive hash")
    _require_sha256(
        lock.get("license", {}).get("sha256"), "OGRE-Next license hash"
    )
    if lock["license"]["sha256"] != expected_ogre_license_sha256:
        raise ProbeError("OGRE-Next license hash moved without review")

    expected_shader_media = {
        "root": "Samples/Media/Hlms",
        "license_expression": (
            "MIT AND LicenseRef-Heitz-LTC-Paper-Notice"
        ),
        "third_party_notice": {
            "license_ref": "LicenseRef-Heitz-LTC-Paper-Notice",
            "source_path": (
                "Samples/Media/Hlms/Pbs/Any/AreaLights_LTC_piece_ps.any"
            ),
            "source_sha256": (
                "44146bd7eee4bd6a3bb9428352e89dc20d7690b32c609e62c5f9330678f3a124"
            ),
            "notice_path": (
                "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt"
            ),
            "notice_sha256": (
                "cc942875917be271c92fdc1fdec7a17da92b45dadf42a979b69583003f38bba6"
            ),
            "upstream_source": "https://github.com/selfshadow/ltc_code/",
            "paper_reference": (
                "Real-Time Polygonal-Light Shading with Linearly "
                "Transformed Cosines, ACM TOG 35(4), 2016"
            ),
            "source_and_binary_notice_required": True,
            "paper_reference_required": True,
        },
    }
    if lock.get("shader_media") != expected_shader_media:
        raise ProbeError(
            "OGRE-Next shader-media license contract changed without review"
        )
    shader_notice = expected_shader_media["third_party_notice"]
    _require_sha256(
        shader_notice["source_sha256"], "shader-media source hash"
    )
    _require_sha256(
        shader_notice["notice_sha256"], "shader-media notice hash"
    )
    notice_path = path.parent / shader_notice["notice_path"]
    if not notice_path.is_file():
        raise ProbeError(f"shader-media notice is missing: {notice_path}")
    if sha256_file(notice_path) != shader_notice["notice_sha256"]:
        raise ProbeError("shader-media notice SHA-256 mismatch")

    rapidjson = lock.get("dependencies", {}).get("rapidjson", {})
    if (
        rapidjson.get("repository") != "https://github.com/Tencent/rapidjson"
        or rapidjson.get("tag") != "v1.1.0"
        or rapidjson.get("archive_url")
        != "https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz"
        or rapidjson.get("archive_sha256")
        != expected_rapidjson_archive_sha256
        or rapidjson.get("license_spdx")
        != "MIT AND BSD-3-Clause AND JSON"
        or rapidjson.get("compiled_headers_spdx") != "MIT"
        or rapidjson.get("non_mit_paths")
        != ["bin/jsonchecker", "include/rapidjson/msinttypes"]
        or rapidjson.get("license_sha256")
        != expected_rapidjson_license_sha256
    ):
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
    if patches[0].get("sha256") != expected_patch_sha256:
        raise ProbeError("the reviewed OGRE-Next adaptation patch hash changed")
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
            "device_option_name": "Rendering Device",
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
            "device_option_name": "Rendering Device",
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
            "device_option_name": "Device",
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


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def default_build_dir() -> Path:
    platform_token = re.sub(
        r"[^a-z0-9]+",
        "-",
        f"{platform.system()}-{platform.machine()}".lower(),
    ).strip("-")
    return Path(tempfile.gettempdir()) / f"ror-ogre-next-probe-{platform_token}"


def prepare_build_dir(path: Path, clean: bool, reuse: bool = False) -> Path:
    requested = path.expanduser()
    if requested.is_symlink():
        raise ProbeError("--build-dir must not be a symbolic link")
    resolved = requested.resolve()
    repository_root = REPOSITORY_ROOT.resolve()
    home = Path.home().resolve()
    filesystem_root = Path(resolved.anchor).resolve()

    if (
        resolved == filesystem_root
        or resolved == home
        or _is_relative_to(repository_root, resolved)
        or _is_relative_to(resolved, repository_root)
    ):
        raise ProbeError(
            "--build-dir must be isolated from the filesystem root, home, "
            "and source checkout"
        )
    try:
        exists = resolved.exists()
        if exists and not resolved.is_dir():
            raise ProbeError(f"--build-dir is not a directory: {resolved}")
        has_entries = exists and any(resolved.iterdir())
    except OSError as error:
        raise ProbeError(f"could not inspect --build-dir: {error}") from error

    if reuse:
        if clean:
            raise ProbeError("--reuse-build-dir and --clean-build-dir conflict")
        if not has_entries:
            raise ProbeError("--reuse-build-dir requires a configured probe build")
        sentinel = resolved / BUILD_SENTINEL_NAME
        cache = resolved / "CMakeCache.txt"
        try:
            sentinel_content = sentinel.read_text(encoding="utf-8")
            cache_text = cache.read_text(encoding="utf-8")
        except OSError as error:
            raise ProbeError(
                "--reuse-build-dir requires an owned configured probe build"
            ) from error
        expected_source = f"CMAKE_HOME_DIRECTORY:INTERNAL={PROBE_SOURCE.resolve()}"
        if sentinel_content != BUILD_SENTINEL_CONTENT or expected_source not in cache_text:
            raise ProbeError(
                "--reuse-build-dir does not identify this exact probe source"
            )
        return resolved

    if has_entries:
        sentinel = resolved / BUILD_SENTINEL_NAME
        if not clean:
            raise ProbeError(
                "--build-dir is not empty; use a new directory or explicitly "
                "pass --clean-build-dir"
            )
        try:
            sentinel_content = sentinel.read_text(encoding="utf-8")
        except OSError as error:
            raise ProbeError(
                "refusing to clean a directory not owned by the OGRE-Next probe"
            ) from error
        if sentinel_content != BUILD_SENTINEL_CONTENT:
            raise ProbeError(
                "refusing to clean a directory with an invalid probe sentinel"
            )
        try:
            shutil.rmtree(resolved)
        except OSError as error:
            raise ProbeError(f"could not clean --build-dir: {error}") from error

    try:
        resolved.mkdir(parents=True, exist_ok=True)
        (resolved / BUILD_SENTINEL_NAME).write_text(
            BUILD_SENTINEL_CONTENT, encoding="utf-8"
        )
    except OSError as error:
        raise ProbeError(f"could not prepare --build-dir: {error}") from error
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
    shader_media = lock["shader_media"]
    shader_notice = shader_media["third_party_notice"]
    abi = lock["abi_contract"]

    checks = {
        "schema_version": report.get("schema_version") == 2,
        "status": report.get("status") == "pass",
        "repository": provenance.get("repository") == lock["repository"],
        "branch": provenance.get("branch") == lock["branch"],
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
        "rapidjson_license": provenance.get(
            "rapidjson_source_archive_license_spdx"
        )
        == rapidjson["license_spdx"],
        "rapidjson_compiled_headers_license": provenance.get(
            "rapidjson_compiled_headers_license_spdx"
        )
        == rapidjson["compiled_headers_spdx"],
        "rapidjson_license_sha256": provenance.get("rapidjson_license_sha256")
        == rapidjson["license_sha256"],
        "shader_media_root": provenance.get("shader_media_root")
        == shader_media["root"],
        "shader_media_license": provenance.get(
            "shader_media_license_expression"
        )
        == shader_media["license_expression"],
        "shader_media_source_path": provenance.get(
            "shader_media_third_party_source_path"
        )
        == shader_notice["source_path"],
        "shader_media_source_hash": provenance.get(
            "shader_media_third_party_source_sha256"
        )
        == shader_notice["source_sha256"],
        "shader_media_notice_path": provenance.get(
            "shader_media_notice_path"
        )
        == shader_notice["notice_path"],
        "shader_media_notice_hash": provenance.get(
            "shader_media_notice_sha256"
        )
        == shader_notice["notice_sha256"],
        "shader_media_upstream": provenance.get(
            "shader_media_upstream_source"
        )
        == shader_notice["upstream_source"],
        "shader_media_paper": provenance.get(
            "shader_media_paper_reference"
        )
        == shader_notice["paper_reference"],
        "shader_media_notice_required": provenance.get(
            "shader_media_source_and_binary_notice_required"
        )
        is True,
        "shader_media_paper_required": provenance.get(
            "shader_media_paper_reference_required"
        )
        is True,
        "ogre_version": build.get("ogre_version") == "3.0.0",
        "platform_policy": build.get("platform_policy") == policy["name"],
        "cxx_standard": build.get("cxx_standard") == 17,
        "pointer_bits": build.get("pointer_bits") == 64,
        "static_link": build.get("static_link") is True,
        "abi_cookie": isinstance(build.get("abi_cookie"), str)
        and re.fullmatch(r"[0-9a-f]{32}", build["abi_cookie"]) is not None,
        "debug_mode": build.get("debug_mode")
        == abi["debug_level_release"],
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
        "renderer_device_option": renderer.get("device_option_name")
        == policy["device_option_name"],
        "renderer_device_count": isinstance(
            renderer.get("reported_device_count"), int
        )
        and renderer["reported_device_count"] > 0,
        "renderer_device_name": isinstance(
            renderer.get("first_reported_device"), str
        )
        and bool(renderer["first_reported_device"]),
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


def validate_build_contract(
    contract: dict[str, Any], lock: dict[str, Any], policy: dict[str, str],
    source_identity: dict[str, Any] | None = None,
) -> None:
    provenance = contract.get("provenance", {})
    rapidjson_contract = contract.get("dependencies", {}).get("rapidjson", {})
    platform_contract = contract.get("platform", {})
    contract_abi = contract.get("abi", {})
    components = contract.get("components", {})
    compiler = contract.get("compiler", {})
    rapidjson = lock["dependencies"]["rapidjson"]
    shader_media = lock["shader_media"]
    abi = lock["abi_contract"]
    expected_simd_family = abi["simd"][policy["name"]]

    expected_abi = {
        key: value
        for key, value in abi.items()
        if key != "simd"
    }
    expected_abi.update(
        {
            "simd_enabled": abi["simd"]["enabled"],
            "simd_alignment": abi["simd"]["alignment"],
            "simd_family": expected_simd_family,
            "simd_neon": expected_simd_family == "neon",
            "simd_sse2": expected_simd_family == "sse2",
        }
    )
    checks = {
        "schema_version": contract.get("schema_version") == 2,
        "repository": provenance.get("repository") == lock["repository"],
        "branch": provenance.get("branch") == lock["branch"],
        "commit": provenance.get("commit") == lock["commit"],
        "archive_sha256": provenance.get("archive_sha256")
        == lock["archive_sha256"],
        "license_spdx": provenance.get("license_spdx")
        == lock["license"]["spdx"],
        "license_sha256": provenance.get("license_sha256")
        == lock["license"]["sha256"],
        "rapidjson_tag": rapidjson_contract.get("tag") == rapidjson["tag"],
        "rapidjson_archive": rapidjson_contract.get("archive_sha256")
        == rapidjson["archive_sha256"],
        "rapidjson_source_license": rapidjson_contract.get(
            "source_archive_license_spdx"
        )
        == rapidjson["license_spdx"],
        "rapidjson_compiled_license": rapidjson_contract.get(
            "compiled_headers_license_spdx"
        )
        == rapidjson["compiled_headers_spdx"],
        "rapidjson_license_hash": rapidjson_contract.get("license_sha256")
        == rapidjson["license_sha256"],
        "shader_media": contract.get("shader_media") == shader_media,
        "platform_policy": platform_contract.get("policy") == policy["name"],
        "renderer_target": platform_contract.get("renderer_target")
        == policy["renderer_target"],
        "device_option_name": platform_contract.get("device_option_name")
        == policy["device_option_name"],
        "system": isinstance(platform_contract.get("system"), str)
        and bool(platform_contract["system"]),
        "processor": isinstance(platform_contract.get("processor"), str)
        and bool(platform_contract["processor"]),
        "abi": contract_abi == expected_abi,
        "components": components
        == {
            "hlms_pbs": True,
            "compositor2_core": True,
            "json_materials": True,
            "mesh_lod": True,
            "dds_codec": True,
            "native_ray_tracing": "not_evaluated",
        },
        "compiler_id": isinstance(compiler.get("id"), str)
        and bool(compiler["id"]),
        "compiler_version": isinstance(compiler.get("version"), str)
        and bool(compiler["version"]),
        "build_type": compiler.get("build_type") == REQUIRED_CONFIG,
    }
    if source_identity is not None:
        checks["ror_source"] = contract.get("ror_source") == source_identity
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next build contract failed closed: "
            + ", ".join(sorted(failed))
        )


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    try:
        subprocess.run(command, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ProbeError(f"command failed: {command[0]}: {error}") from error


def run_frame_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    policy: dict[str, str],
    capability_report_path: Path,
) -> None:
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_frame_probe_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    frame_report_path = build_dir / FRAME_REPORT_NAME
    frame_image_path = build_dir / FRAME_IMAGE_NAME
    missing = [
        path.name
        for path in (frame_report_path, frame_image_path, capability_report_path)
        if not path.is_file()
    ]
    if missing:
        raise ProbeError(
            "OGRE-Next frame checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    run(
        [
            sys.executable,
            str(FRAME_VALIDATOR),
            "--report",
            str(frame_report_path),
            "--image",
            str(frame_image_path),
            "--capability-report",
            str(capability_report_path),
            "--platform-policy",
            policy["name"],
        ]
    )


def validate_n1_checkpoint(
    report: dict[str, Any],
    image_path: Path,
    lock: dict[str, Any],
    policy: dict[str, str],
    media_manifest: dict[str, Any],
    source_identity: dict[str, Any],
) -> None:
    try:
        image = image_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read N1 frame: {error}") from error
    header = b"P6\n192 128\n255\n"
    if not image.startswith(header) or len(image) != len(header) + 192 * 128 * 3:
        raise ProbeError("N1 frame is not the exact 192x128 RGB8 PPM contract")
    pixels = image[len(header) :]
    hash_value = 14695981039346656037
    for value in pixels:
        hash_value ^= value
        hash_value = (hash_value * 1099511628211) & ((1 << 64) - 1)
    colours = [
        bytes(pixels[offset : offset + 3])
        for offset in range(0, len(pixels), 3)
    ]
    counts: dict[bytes, int] = {}
    for colour in colours:
        counts[colour] = counts.get(colour, 0) + 1
    observed_non_background = len(colours) - max(counts.values())
    provenance = report.get("provenance", {})
    adapter = report.get("adapter", {})
    catalog = report.get("catalog", {})
    hdr = report.get("hdr", {})
    sdr = report.get("sdr", {})
    lifecycle = report.get("lifecycle", {})
    shader_media = lock["shader_media"]
    checks = {
        "schema": report.get("schema")
        == "ror.ogre_next_frontend_n1_smoke.v1",
        "status": report.get("status") == "pass",
        "commit": provenance.get("ogre_next_commit") == lock["commit"],
        "archive": provenance.get("ogre_next_archive_sha256")
        == lock["archive_sha256"],
        "ror_repository": provenance.get("ror_repository")
        == source_identity["repository"],
        "ror_ref": provenance.get("ror_ref") == source_identity["ref"],
        "ror_commit": provenance.get("ror_commit")
        == source_identity["commit"],
        "ror_source_manifest": provenance.get(
            "ror_relevant_source_manifest_sha256"
        )
        == source_identity["relevant_manifest_sha256"],
        "ror_source_count": provenance.get(
            "ror_relevant_source_manifest_file_count"
        )
        == source_identity["relevant_manifest_file_count"],
        "shader_root": provenance.get("shader_media_root")
        == shader_media["root"],
        "shader_license": provenance.get("shader_media_license_expression")
        == shader_media["license_expression"],
        "shader_notice": provenance.get("shader_media_notice_sha256")
        == shader_media["third_party_notice"]["notice_sha256"],
        "shader_manifest_hash": provenance.get(
            "shader_media_manifest_sha256"
        )
        == media_manifest["sha256"],
        "shader_manifest_count": provenance.get(
            "shader_media_manifest_file_count"
        )
        == media_manifest["file_count"],
        "platform": report.get("platform_policy") == policy["name"],
        "renderer": report.get("renderer") == policy["renderer_name"],
        "v2_vao": adapter.get("native_mesh_path")
        == "Ogre v2 Mesh plus immutable VertexArrayObject",
        "pbs": adapter.get("material_path") == "HLMS PBS metallic-roughness",
        "brdf": adapter.get("brdf")
        == "PbsBrdf::Default height-correlated GGX",
        "pbr_readback": adapter.get("pbr_datablock_readback_verified") is True,
        "runtime_media_root": adapter.get("runtime_media_root")
        == "explicit_absolute",
        "package_media": adapter.get("package_media_relative_path")
        == "share/rigsofrods/ogre-next/Samples/Media",
        "relocated_executable": adapter.get("relocated_executable") is True,
        "compositor2": adapter.get("compositor2") is True,
        "ui_free": adapter.get("ui_included") is False,
        "readback": adapter.get("cpu_readback_completed") is True,
        "uncalibrated_lights_rejected": adapter.get("analytic_lights_calibrated")
        is False
        and adapter.get("constant_environment_only") is True,
        "interop_closed": adapter.get("native_interop") is False
        and adapter.get("ray_tracing") is False,
        "catalog": catalog.get("sequence") == 1
        and catalog.get("transactional_replay_after_restart") is True,
        "hdr_format": hdr.get("format") == "RGBA16_FLOAT",
        "hdr_energy": isinstance(hdr.get("maximum_luminance"), (int, float))
        and hdr["maximum_luminance"] > 1.05,
        "hdr_geometry": isinstance(hdr.get("non_background_pixels"), int)
        and hdr["non_background_pixels"] >= 512,
        "sdr_format": sdr.get("format") == "RGBA8_SRGB",
        "sdr_hash": sdr.get("rgb8_fnv1a64") == f"{hash_value:016x}",
        "sdr_distinct": sdr.get("distinct_rgb8_values") == len(counts),
        "sdr_geometry": sdr.get("non_background_pixels")
        == observed_non_background
        and observed_non_background >= 512,
        "lifecycle": all(
            lifecycle.get(field) is True
            for field in (
                "unsupported_depth_failed_before_submission",
                "double_sided_pbs_readback",
                "lifetime_snapshot_identity_replay",
                "lifetime_completed_frame_queries",
                "process_global_root_exclusion",
                "shutdown_reinitialize_render_shutdown",
            )
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise ProbeError(
            "OGRE-Next N1 checkpoint failed closed: "
            + ", ".join(sorted(failed))
        )


def shader_media_manifest(root: Path) -> dict[str, Any]:
    if not root.is_dir():
        raise ProbeError(f"OGRE-Next N1 HLMS tree is missing: {root}")
    entries: list[tuple[str, int, str]] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path.is_symlink():
            raise ProbeError(
                "OGRE-Next N1 HLMS tree contains a symbolic link: "
                + path.relative_to(root).as_posix()
            )
        if path.is_dir():
            continue
        if not path.is_file():
            raise ProbeError(
                "OGRE-Next N1 HLMS tree contains a non-file entry: "
                + path.relative_to(root).as_posix()
            )
        entries.append(
            (
                path.relative_to(root).as_posix(),
                path.stat().st_size,
                sha256_file(path),
            )
        )
    if not entries:
        raise ProbeError("OGRE-Next N1 HLMS tree is empty")
    serialized = "".join(
        f"{relative}|{size}|{digest}\n"
        for relative, size, digest in entries
    ).encode("utf-8")
    return {
        "sha256": hashlib.sha256(serialized).hexdigest(),
        "file_count": len(entries),
        "entries": entries,
    }


def validate_n1_package(
    build_dir: Path, lock: dict[str, Any]
) -> dict[str, Any]:
    package_root = build_dir / N1_PACKAGE_NAME
    shader_notice = lock["shader_media"]["third_party_notice"]
    expected_hashes = {
        Path("licenses/Rigs-of-Rods-GPL-3.0.txt"): sha256_file(
            REPOSITORY_ROOT / "COPYING"
        ),
        Path("licenses/Ogre-Next-MIT.txt"): lock["license"]["sha256"],
        Path("licenses/RapidJSON-license.txt"): lock["dependencies"][
            "rapidjson"
        ]["license_sha256"],
        Path(shader_notice["notice_path"]): shader_notice["notice_sha256"],
    }
    failures: list[str] = []
    for relative_path, expected_hash in expected_hashes.items():
        staged_path = package_root / relative_path
        if not staged_path.is_file():
            failures.append(f"missing {relative_path.as_posix()}")
            continue
        if sha256_file(staged_path) != expected_hash:
            failures.append(f"hash mismatch {relative_path.as_posix()}")
    if failures:
        raise ProbeError(
            "OGRE-Next N1 package license validation failed closed: "
            + ", ".join(failures)
        )
    source_manifest = shader_media_manifest(
        build_dir / "_deps" / "ogre_next-src" / "Samples" / "Media" / "Hlms"
    )
    package_manifest = shader_media_manifest(
        package_root / "share" / "rigsofrods" / "ogre-next" /
        "Samples" / "Media" / "Hlms"
    )
    if package_manifest != source_manifest:
        raise ProbeError(
            "OGRE-Next N1 staged HLMS tree differs from the pinned source manifest"
        )
    return source_manifest


def run_n1_checkpoint(
    build_dir: Path,
    config: str,
    jobs: int,
    lock: dict[str, Any],
    policy: dict[str, str],
    source_identity: dict[str, Any],
) -> None:
    run(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            "ror_ogre_next_frontend_n1_report",
            "--config",
            config,
            "--parallel",
            str(jobs),
        ]
    )
    require_source_identity_unchanged(source_identity)
    report_path = build_dir / N1_REPORT_NAME
    image_path = build_dir / N1_IMAGE_NAME
    missing = [path.name for path in (report_path, image_path) if not path.is_file()]
    if missing:
        raise ProbeError(
            "OGRE-Next N1 checkpoint did not produce required artifacts: "
            + ", ".join(missing)
        )
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read N1 report: {error}") from error
    media_manifest = validate_n1_package(build_dir, lock)
    validate_n1_checkpoint(
        report, image_path, lock, policy, media_manifest, source_identity
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=default_build_dir(),
        help="fresh standalone CMake build directory outside the source checkout",
    )
    parser.add_argument(
        "--clean-build-dir",
        action="store_true",
        help="clean a non-empty build directory only when its probe sentinel matches",
    )
    parser.add_argument(
        "--reuse-build-dir",
        action="store_true",
        help="reuse an owned configured build for a later independent checkpoint",
    )
    parser.add_argument(
        "--checkpoint",
        choices=("all", "n1", "legacy"),
        default="all",
        help="run all gates, the independent N1 gate, or the legacy probes",
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
    parser.add_argument(
        "--config",
        choices=(REQUIRED_CONFIG,),
        default=REQUIRED_CONFIG,
        help="reviewed build configuration (Release only)",
    )
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
                        "schema_version": 2,
                        "status": "pass",
                        "commit": lock["commit"],
                        "platform_policy": policy["name"],
                        "configuration": REQUIRED_CONFIG,
                        "network_used": False,
                    },
                    indent=2,
                    sort_keys=True,
                )
            )
            return 0

        source_identity = ror_source_identity()

        if args.reuse_build_dir and args.checkpoint == "all":
            raise ProbeError(
                "--reuse-build-dir requires --checkpoint n1 or legacy"
            )
        if args.reuse_build_dir and (
            args.ogre_archive or args.rapidjson_archive or args.generator
        ):
            raise ProbeError(
                "reused checkpoints cannot change archives or the generator"
            )
        build_dir = prepare_build_dir(
            args.build_dir, args.clean_build_dir, args.reuse_build_dir
        )
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

        if not args.reuse_build_dir:
            run(configure)

        build_contract_path = build_dir / BUILD_CONTRACT_NAME
        try:
            build_contract = json.loads(
                build_contract_path.read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError) as error:
            raise ProbeError(f"could not read build contract: {error}") from error
        validate_build_contract(build_contract, lock, policy, source_identity)

        if args.checkpoint in ("all", "n1"):
            run_n1_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                lock,
                policy,
                source_identity,
            )

        report: dict[str, Any] = {
            "schema_version": 2,
            "status": "pass",
            "checkpoint": args.checkpoint,
            "platform_policy": policy["name"],
        }
        if args.checkpoint in ("all", "legacy"):
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
            require_source_identity_unchanged(source_identity)
            report_path = build_dir / REPORT_NAME
            try:
                report = json.loads(report_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise ProbeError(f"could not read probe report: {error}") from error
            validate_report(report, lock, policy)
            run_frame_checkpoint(
                build_dir,
                args.config,
                args.jobs,
                policy,
                report_path,
            )
            require_source_identity_unchanged(source_identity)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ProbeError as error:
        print(f"OGRE-Next probe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
